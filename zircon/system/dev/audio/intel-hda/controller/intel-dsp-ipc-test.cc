// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-dsp-ipc.h"

#include <lib/zx/time.h>
#include <threads.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <cstdint>
#include <unordered_set>
#include <vector>

#include <ddk/debug.h>
#include <zxtest/zxtest.h>

#include "debug-logging.h"

namespace audio::intel_hda {
namespace {

// A simple C11 thread wrapper.
class Thread {
 public:
  Thread(std::function<void()> start) : start_(std::move(start)) {
    int result = thrd_create(
        &thread_,
        [](void* start_ptr) {
          auto start = static_cast<std::function<void()>*>(start_ptr);
          (*start)();
          return 0;
        },
        &start_);
    ZX_ASSERT(result == thrd_success);
  }

  void Join() {
    ZX_ASSERT(!joined_);
    int result;
    thrd_join(thread_, &result);
    joined_ = true;
  }

  ~Thread() {
    if (!joined_) {
      Join();
    }
  }

  // Disallow copy/move.
  Thread(const Thread&) = delete;
  Thread(Thread&&) = delete;
  Thread& operator=(Thread&&) = delete;
  Thread& operator=(const Thread&) = delete;

 private:
  bool joined_ = false;
  std::function<void()> start_;
  thrd_t thread_;
};

// Wait for a condition to be set, using exponential backoff.
template <typename F>
void WaitForCond(F cond) {
  zx::duration delay = zx::usec(1);
  while (!cond()) {
    zx::nanosleep(zx::deadline_after(delay));
    delay *= 2;
  }
}

// Simulate the hardware sending an IPC reply, and firing an interrupt.
void SendReply(IntelDspIpc* dsp, adsp_registers_t* regs, const IpcMessage& reply) {
  // Send the reply.
  REG_WR(&regs->hipct, reply.primary | ADSP_REG_HIPCT_BUSY | (1 << IPC_PRI_RSP_SHIFT));
  REG_WR(&regs->hipcte, reply.extension);
  REG_SET_BITS(&regs->adspis, ADSP_REG_ADSPIC_IPC);  // Indicate IPC reply ready.
  dsp->ProcessIrq();
}

// Poll the IPC-related registers until a message is sent by the driver.
IpcMessage ReadMessage(adsp_registers_t* regs) {
  // Wait for a message.
  WaitForCond([&]() { return (REG_RD(&regs->hipci) & ADSP_REG_HIPCI_BUSY) != 0; });

  // Read it.
  uint32_t primary = REG_RD(&regs->hipci);
  uint32_t extension = REG_RD(&regs->hipcie);

  // Clear the busy bit.
  REG_CLR_BITS(&regs->hipci, ADSP_REG_HIPCI_BUSY);

  return IpcMessage(primary, extension);
}

TEST(Ipc, ConstructDestruct) {
  adsp_registers_t regs = {};
  IntelDspIpc dsp("UnitTests", &regs, zx::duration::infinite());
}

TEST(Ipc, SimpleSend) {
  driver_set_log_flags(ZX_LOG_LEVEL_MASK);
  adsp_registers_t regs = {};
  IntelDspIpc dsp("UnitTests", &regs, zx::duration::infinite());

  // Start a thread, give it a chance to run.
  auto worker = Thread([&]() {
    Status result = dsp.Send(0xaa, 0x55);
    ZX_ASSERT_MSG(result.ok(), "Send failed: %s (code: %d)", result.ToString().c_str(),
                  result.code());
  });

  // Simulate the DSP reading the message.
  IpcMessage message = ReadMessage(&regs);
  EXPECT_EQ(message.primary & IPC_PRI_MODULE_ID_MASK, 0xaa);
  EXPECT_EQ(message.extension & IPC_EXT_DATA_OFF_SIZE_MASK, 0x55);

  // Ensure the thread remains blocked on the send, even if we wait a little.
  zx::nanosleep(zx::deadline_after(zx::msec(10)));
  EXPECT_TRUE(dsp.IsOperationPending());

  // Simulate the DSP sending a successful reply.
  SendReply(&dsp, &regs, IpcMessage(0, 0));
}

TEST(Ipc, ErrorReply) {
  adsp_registers_t regs = {};
  IntelDspIpc dsp("UnitTests", &regs, zx::duration::infinite());

  // Start a thread.
  auto worker = Thread([&]() {
    Status result = dsp.Send(0xaa, 0x55);
    ZX_ASSERT_MSG(result.ToString() == "DSP returned error 42 (ZX_ERR_INTERNAL)",
                  "Got incorrect error message: %s", result.ToString().c_str());
  });

  // Read (and ignore) the message.
  (void)ReadMessage(&regs);

  // Simulate the DSP sending an error reply, with an arbitrary error code (42).
  //
  // The test will abort if the child thread gets the wrong error code.
  SendReply(&dsp, &regs, IpcMessage(42, 0));
}

TEST(Ipc, HardwareTimeout) {
  adsp_registers_t regs = {};
  IntelDspIpc dsp("UnitTests", &regs, zx::usec(1));

  // Start a thread.
  auto worker = Thread([&]() {
    Status result = dsp.Send(0xaa, 0x55);
    ZX_ASSERT_MSG(result.code() == ZX_ERR_TIMED_OUT, "Got incorrect error: %s",
                  result.ToString().c_str());
  });

  // Wait for it to time out.
  worker.Join();
}

TEST(Ipc, UnsolicitedReply) {
  adsp_registers_t regs = {};
  IntelDspIpc dsp("UnitTests", &regs, zx::duration::infinite());

  // Ensuring sending a reply and an IRQ doesn't crash.
  SendReply(&dsp, &regs, IpcMessage(42, 0));
}

TEST(Ipc, QueuedMessages) {
  constexpr int kNumThreads = 10;
  adsp_registers_t regs = {};
  IntelDspIpc dsp("UnitTests", &regs, zx::duration::infinite());

  // Start many threads, racing to send messages.
  std::array<std::unique_ptr<Thread>, kNumThreads> workers;
  for (int i = 0; i < kNumThreads; i++) {
    workers[i] = std::make_unique<Thread>([i, &dsp]() {
      Status result = dsp.Send(0, i);
      ZX_ASSERT_MSG(result.ok(), "Send failed: %s (code: %d)", result.ToString().c_str(),
                    result.code());
    });
  }

  // Simulate the DSP reading off the messages one by one, and ensure that
  // we got all the messages.
  std::unordered_set<int> seen_messages{};
  for (int i = 0; i < kNumThreads; i++) {
    IpcMessage message = ReadMessage(&regs);
    EXPECT_TRUE(seen_messages.find(message.extension) == seen_messages.end());
    seen_messages.insert(message.extension);
    SendReply(&dsp, &regs, IpcMessage(0, 0));
  }
}

TEST(Ipc, ShutdownWithQueuedSend) {
  adsp_registers_t regs = {};
  IntelDspIpc dsp("UnitTests", &regs, zx::duration::infinite());

  // Start a thread, and wait for it to send.
  Thread thread([&dsp]() {
    Status result = dsp.Send(0, 0);
    ZX_ASSERT_MSG(!result.ok() && result.code() == ZX_ERR_CANCELED,
                  "Expected send to fail with 'ZX_ERR_CANCELED', but got: %s",
                  result.ToString().c_str());
  });
  WaitForCond([&]() { return dsp.IsOperationPending(); });

  // Shut down the IPC object.
  dsp.Shutdown();
}

TEST(Ipc, DestructWithQueuedThread) {
  adsp_registers_t regs = {};
  std::optional<IntelDspIpc> dsp;
  dsp.emplace("UnitTests", &regs, zx::duration::infinite());

  // Start a thread, and wait for it to send.
  Thread thread([&dsp]() {
    Status result = dsp->Send(0, 0);
    ZX_ASSERT_MSG(!result.ok() && result.code() == ZX_ERR_CANCELED,
                  "Expected send to fail with 'ZX_ERR_CANCELED', but got: %s",
                  result.ToString().c_str());
  });
  WaitForCond([&]() { return dsp->IsOperationPending(); });

  // Destruct the object.
  dsp.reset();
}

}  // namespace
}  // namespace audio::intel_hda
