// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/spawn.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/integration_tests/message_loop_wrapper.h"
#include "src/developer/debug/debug_agent/integration_tests/so_wrapper.h"
#include "src/developer/debug/debug_agent/local_stream_backend.h"
#include "src/developer/debug/debug_agent/object_provider.h"
#include "src/developer/debug/debug_agent/system_info.h"
#include "src/developer/debug/ipc/agent_protocol.h"
#include "src/developer/debug/ipc/client_protocol.h"
#include "src/developer/debug/ipc/message_writer.h"
#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/lib/fxl/logging.h"

// This test tests that the debug agent can effectively capture process being
// launched by zircon, and set breakpoints on them.
//
// The overall test goes like this:
//
// 1. Attach to root component (this is the first thing the zxdb client does).
// 2. Set up filters ("true" and "false").
// 3. Launch "debug_test_true" and "debug_test_false" binaries.
// 4. The agent should capture them and should finish correctly.
// 5. Set filter to "breakpoint".
// 6. Launch "breakpoint_test_exe" and "debug_test_true". Only the former should
//    be captured.
// 7. After receiving the modules, set a breakpoint.
// 8. Resume the thread and verify that the breakpoint was hit.
// 9. Resume the thread. The process should exit successfully.

using namespace debug_ipc;

namespace debug_agent {

namespace {

zx::job CreateJob() {
  zx_handle_t default_job = zx_job_default();
  zx_handle_t job;
  zx_status_t status = zx_job_create(default_job, 0u, &job);
  if (status != ZX_OK)
    FXL_NOTREACHED() << "Failed to create job: " << ZxStatusToString(status);
  return zx::job(job);
}

// Uses fdio to launch a process under a job.
// The process will start immediatelly.
zx::process LaunchProcess(const zx::job& job, const std::string name,
                          std::vector<const char*> argv) {
  // fdio_spawn requires that argv has a nullptr in the end.
  std::vector<const char*> normalized_argv = argv;
  normalized_argv.push_back(nullptr);

  std::vector<fdio_spawn_action_t> actions;
  normalized_argv.push_back(nullptr);
  actions.push_back({.action = FDIO_SPAWN_ACTION_SET_NAME, .name = {.data = name.c_str()}});

  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx_handle_t process_handle;
  zx_status_t status = fdio_spawn_etc(job.get(), FDIO_SPAWN_CLONE_ALL, argv[0], argv.data(),
                                      nullptr,  // Environ
                                      actions.size(), actions.data(), &process_handle, err_msg);
  if (status != ZX_OK) {
    FXL_NOTREACHED() << "Failed to spawn command (" << ZxStatusToString(status) << "): " << err_msg;
  }
  return zx::process(process_handle);
}

// This class will capture all the async notifications sent by the debug agent.
// These mostly correspond to the zircon exceptions.
//
// The class will record all those so that the test can verify the behaviour.
class JobStreamBackend : public LocalStreamBackend {
 public:
  JobStreamBackend(MessageLoop* message_loop) : message_loop_(message_loop) {}

  void set_remote_api(RemoteAPI* remote_api) { remote_api_ = remote_api; }

  void ClearAttachReply() { attach_reply_ = std::nullopt; }

  // Notification Handling -----------------------------------------------------

  void HandleAttach(debug_ipc::AttachReply attach) override {
    FXL_DCHECK(!attach_reply_.has_value());
    attach_reply_ = std::move(attach);
  }

  void HandleNotifyProcessStarting(NotifyProcessStarting process) override {
    process_start_events_.push_back(std::move(process));
    message_loop_->QuitNow();
  }

  void HandleNotifyProcessExiting(NotifyProcessExiting process) override {
    process_exit_events_.push_back(process);
    message_loop_->QuitNow();
  }

  void HandleNotifyThreadStarting(NotifyThread thread) override {
    thread_start_events_.push_back(std::move(thread));
    message_loop_->QuitNow();
  }

  void HandleNotifyModules(NotifyModules modules) override {
    module_events_.push_back(std::move(modules));
    message_loop_->QuitNow();
  }

  void HandleNotifyException(NotifyException exception) override {
    exceptions_.push_back(std::move(exception));
    message_loop_->QuitNow();
  }

  // Counters ------------------------------------------------------------------

  void Reset() {
    process_start_events_.clear();
    process_exit_events_.clear();
    thread_start_events_.clear();
    module_events_.clear();
  }

  const auto& attach_reply() const { return attach_reply_; }
  const auto& process_start_events() const { return process_start_events_; }
  const auto& process_exit_events() const { return process_exit_events_; }
  const auto& thread_start_events() const { return thread_start_events_; }
  const auto& module_events() const { return module_events_; }
  const auto& exceptions() const { return exceptions_; }

 private:
  std::optional<AttachReply> attach_reply_;
  std::vector<NotifyProcessStarting> process_start_events_;
  std::vector<NotifyProcessExiting> process_exit_events_;
  std::vector<NotifyThread> thread_start_events_;
  std::vector<NotifyModules> module_events_;
  std::vector<NotifyException> exceptions_;

  MessageLoop* message_loop_ = nullptr;
  RemoteAPI* remote_api_ = nullptr;
};

// Process Management Utility Functions ----------------------------------------

void ResumeAllProcesses(RemoteAPI* remote_api, const JobStreamBackend& backend) {
  for (const auto& start_event : backend.process_start_events()) {
    // We continue the process.
    ResumeRequest resume_request;
    resume_request.how = ResumeRequest::How::kContinue;
    resume_request.process_koid = start_event.koid;
    ResumeReply resume_reply;
    remote_api->OnResume(resume_request, &resume_reply);
  }
}

void VerifyAllProcessesStarted(const JobStreamBackend& backend,
                               const std::vector<std::string>& process_names) {
  ASSERT_EQ(backend.process_start_events().size(), process_names.size());
  for (const auto& process_name : process_names) {
    bool found = false;
    for (const auto& start_event : backend.process_start_events()) {
      if (start_event.name == process_name) {
        found = true;
        break;
      }
    }

    ASSERT_TRUE(found) << "Didn't find process " << process_name;
  }
}

struct ProcessIdentifier {
  std::string process_name;
  uint64_t koid;
  int expected_return_code;
};
void VerifyAllProcessesExited(const JobStreamBackend& backend,
                              std::vector<ProcessIdentifier> expected) {
  for (const auto& [name, koid, return_code] : expected) {
    const NotifyProcessExiting* found = nullptr;
    for (const auto& exit_event : backend.process_exit_events()) {
      if (koid == exit_event.process_koid) {
        found = &exit_event;
        break;
      }
    }

    if (!found)
      FXL_NOTREACHED() << "Process " << name << " did not exit.";
    ASSERT_EQ(found->return_code, return_code) << " Process " << name << " expected return code "
                                               << return_code << ", got " << found->return_code;
  }
}

uint64_t FindModuleBaseAddress(const NotifyModules& modules, const std::string& module_name) {
  for (const auto& module : modules.modules) {
    if (module.name == module_name)
      return module.base;
  }

  FXL_NOTREACHED() << "Could not find module " << module_name;
  return 0;
}

}  // namespace

TEST(DebuggedJobIntegrationTest, DISABLED_RepresentativeScenario) {
  MessageLoopWrapper message_loop_wrapper;
  MessageLoop* message_loop = message_loop_wrapper.loop();

  JobStreamBackend backend(message_loop);

  auto services = sys::ServiceDirectory::CreateFromNamespace();
  DebugAgent agent(services, SystemProviders::CreateDefaults(services));
  RemoteAPI* remote_api = &agent;

  agent.Connect(&backend.stream());
  backend.set_remote_api(remote_api);

  FXL_VLOG(1) << "Attaching to root component.";

  // Attach to the component root.
  AttachRequest attach_request;
  attach_request.type = TaskType::kComponentRoot;
  remote_api->OnAttach(0, attach_request);

  // We should've received an attach reply.
  const auto& attach_reply = backend.attach_reply();
  ASSERT_TRUE(attach_reply.has_value());
  ASSERT_EQ(attach_reply->status, ZX_OK)
      << "Expected ZX_OK, Got: " << ZxStatusToString(attach_reply->status);
  ASSERT_NE(attach_reply->koid, 0u);

  FXL_VLOG(1) << "Setting job filters.";

  // Sent the Job filter.
  JobFilterRequest filter_request;
  filter_request.job_koid = attach_reply->koid;
  filter_request.filters = {"true", "false"};
  JobFilterReply filter_reply;
  remote_api->OnJobFilter(filter_request, &filter_reply);
  ASSERT_EQ(filter_reply.status, ZX_OK)
      << "Expected ZX_OK, Got: " << ZxStatusToString(filter_reply.status);

  FXL_VLOG(1) << "Launching jobs.";

  // We launch a some processes.
  zx::job job = CreateJob();
  std::vector<zx::process> processes;
  processes.push_back(LaunchProcess(job, "true", {"/pkg/bin/debug_test_true"}));
  processes.push_back(LaunchProcess(job, "false", {"/pkg/bin/debug_test_false"}));

  // We should receive all the start events.
  for (size_t i = 0; i < processes.size(); i++) {
    message_loop->Run();
    ASSERT_EQ(backend.process_start_events().size(), i + 1);
  }
  // We resume the processes, which are in the initial waiting state.
  VerifyAllProcessesStarted(backend, {"true", "false"});

  FXL_VLOG(1) << "Starting threads.";

  // All threads should start
  for (size_t i = 0; i < processes.size(); i++) {
    message_loop->Run();
    ASSERT_EQ(backend.thread_start_events().size(), i + 1);
  }

  // Now that all threads started, we resume them all.
  ResumeAllProcesses(remote_api, backend);

  FXL_VLOG(1) << "Receiving modules.";

  // We should receive all the modules notifications.
  for (size_t i = 0; i < processes.size(); i++) {
    message_loop->Run();
    ASSERT_EQ(backend.module_events().size(), i + 1);
  }

  FXL_VLOG(1) << "Resuming proceses.";

  // We need to resume the thread again after getting the modules.
  ResumeAllProcesses(remote_api, backend);

  // All processes should exit.
  for (size_t i = 0; i < processes.size(); i++) {
    message_loop->Run();
    ASSERT_EQ(backend.process_exit_events().size(), i + 1);
  }

  // Create the expected.
  std::vector<ProcessIdentifier> expected;
  for (const auto& start_event : backend.process_start_events()) {
    ProcessIdentifier identifier;
    identifier.process_name = start_event.name;
    identifier.koid = start_event.koid;
    identifier.expected_return_code = start_event.name == "true" ? 0 : 1;
    expected.push_back(std::move(identifier));
  }
  VerifyAllProcessesExited(backend, expected);

  // We reset the state so that the stats are easier to reason about.
  processes.clear();
  backend.Reset();

  FXL_VLOG(1) << "Changing filters.";

  // We change the filters. A partial match should work.
  filter_request.filters = {"breakpoint"};
  remote_api->OnJobFilter(filter_request, &filter_reply);
  ASSERT_EQ(filter_reply.status, ZX_OK)
      << "Expected ZX_OK, Got: " << ZxStatusToString(filter_reply.status);

  FXL_VLOG(1) << "Launching new processes.";

  // We launch two processes.
  processes.push_back(LaunchProcess(job, "breakpoint_test_exe", {"/pkg/bin/breakpoint_test_exe"}));
  processes.push_back(LaunchProcess(job, "true", {"/pkg/bin/debug_test_true"}));

  // Should only catch one.
  message_loop->Run();
  ASSERT_EQ(backend.process_start_events().size(), 1u);

  // Catch thread start event.
  message_loop->Run();
  ASSERT_EQ(backend.thread_start_events().size(), 1u);

  // Need to resume the thread at this point.
  ResumeAllProcesses(remote_api, backend);
  message_loop->Run();

  ASSERT_EQ(backend.module_events().size(), 1u);

  FXL_VLOG(1) << "Setting up breakpoint.";

  // The test .so we load in order to search the offset of the exported symbol
  // within it.
  const char* kTestSo = "debug_agent_test_so.so";
  const char* kModuleToSearch = "libdebug_agent_test_so.so";

  // We now have modules, so we can insert a breakpoint!
  SoWrapper so_wrapper;
  ASSERT_TRUE(so_wrapper.Init(kTestSo)) << "Could not load .so " << kTestSo;

  // The exported symbol we're going to put the breakpoint on.
  const char* kExportedFunctionName = "InsertBreakpointFunction";
  uint64_t symbol_offset = so_wrapper.GetSymbolOffset(kTestSo, kExportedFunctionName);
  ASSERT_NE(symbol_offset, 0u);

  uint64_t base_address = FindModuleBaseAddress(backend.module_events().back(), kModuleToSearch);
  uint64_t function_address = base_address + symbol_offset;

  uint64_t process_koid = backend.process_start_events().back().koid;
  uint32_t breakpoint_id = 1;

  // We add a breakpoint.
  ProcessBreakpointSettings location;
  location.process_koid = process_koid;
  location.address = function_address;
  AddOrChangeBreakpointRequest breakpoint_request;
  breakpoint_request.breakpoint_type = debug_ipc::BreakpointType::kSoftware;
  breakpoint_request.breakpoint.id = breakpoint_id;
  breakpoint_request.breakpoint.locations.push_back(location);
  AddOrChangeBreakpointReply breakpoint_reply;
  remote_api->OnAddOrChangeBreakpoint(breakpoint_request, &breakpoint_reply);
  ASSERT_EQ(breakpoint_reply.status, ZX_OK)
      << "Received: " << ZxStatusToString(breakpoint_reply.status);

  // Resume the process.
  ResumeAllProcesses(remote_api, backend);

  message_loop->Run();

  FXL_VLOG(1) << "Hit breakpoint.";

  // We should've received a breakpoint event.
  ASSERT_EQ(backend.exceptions().size(), 1u);
  const auto& exception = backend.exceptions().back();
  EXPECT_EQ(exception.type, ExceptionType::kSoftware);
  EXPECT_EQ(exception.thread.process_koid, process_koid);
  ASSERT_EQ(exception.hit_breakpoints.size(), 1u);
  const auto& breakpoint_stat = exception.hit_breakpoints.back();
  EXPECT_EQ(breakpoint_stat.id, breakpoint_id);
  EXPECT_EQ(breakpoint_stat.hit_count, 1u);
  EXPECT_EQ(breakpoint_stat.should_delete, false);  // Non one-shot breakpoint.

  FXL_VLOG(1) << "Resuming process.";

  // We resume the thread.
  ResumeAllProcesses(remote_api, backend);
  message_loop->Run();

  // We should've received the exit event.
  // There should be no events except for the process exiting.
  ASSERT_EQ(backend.process_start_events().size(), 1u);
  ASSERT_EQ(backend.thread_start_events().size(), 1u);
  ASSERT_EQ(backend.module_events().size(), 1u);
  ASSERT_EQ(backend.process_exit_events().size(), 1u);
  const auto& exit_event = backend.process_exit_events().back();
  EXPECT_EQ(exit_event.process_koid, process_koid);
  EXPECT_EQ(exit_event.return_code, 0);
}

TEST(DebuggedJobIntegrationTest, AttachSpecial) {
  MessageLoopWrapper loop_wrapper;

  JobStreamBackend backend(loop_wrapper.loop());

  auto services = sys::ServiceDirectory::CreateFromNamespace();
  SystemProviders providers = SystemProviders::CreateDefaults(services);
  DebugAgent agent(std::move(services), providers);
  RemoteAPI* remote_api = &agent;

  agent.Connect(&backend.stream());
  backend.set_remote_api(remote_api);

  // Request attaching to the component root job.
  debug_ipc::AttachRequest attach_request = {};
  attach_request.type = debug_ipc::TaskType::kComponentRoot;

  // OnAttach is special and takes a serialized message.
  const uint32_t kTransactionId = 1;
  debug_ipc::MessageWriter writer;
  debug_ipc::WriteRequest(attach_request, kTransactionId, &writer);
  remote_api->OnAttach(writer.MessageComplete());

  ASSERT_TRUE(backend.attach_reply());
  debug_ipc::AttachReply comp_reply = *backend.attach_reply();

  // At this point we can't validate that much since the test environment is
  // special and the component manager's job won't actually be the real one.
  // But we can at least check that the KOID matches what the component job
  // KOID getter computes.
  EXPECT_EQ(providers.object_provider->GetComponentJobKoid(), comp_reply.koid);

  // Now attach to the system root.
  backend.ClearAttachReply();
  attach_request.type = debug_ipc::TaskType::kSystemRoot;

  debug_ipc::WriteRequest(attach_request, kTransactionId, &writer);
  remote_api->OnAttach(writer.MessageComplete());

  ASSERT_TRUE(backend.attach_reply());
  debug_ipc::AttachReply root_reply = *backend.attach_reply();

  // Validate we got a root job KOID and that its different than the
  // component one. As above, this isn't the real root job so we can't do
  // too much checking.
  EXPECT_EQ(providers.object_provider->GetRootJobKoid(), root_reply.koid);
  EXPECT_NE(root_reply.koid, comp_reply.koid);
}

}  // namespace debug_agent
