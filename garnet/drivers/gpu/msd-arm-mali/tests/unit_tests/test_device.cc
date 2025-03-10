// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "helper/platform_device_helper.h"
#include "mock/mock_mmio.h"
#include "msd_arm_device.h"
#include "registers.h"

bool IsStringInDump(const std::vector<std::string>& dump, const std::string& str) {
  return std::any_of(dump.begin(), dump.end(), [str](const std::string& input_str) {
    return input_str.find(str) != std::string::npos;
  });
}
// These tests are unit testing the functionality of MsdArmDevice.
// All of these tests instantiate the device in test mode, that is without the device thread active.
class TestMsdArmDevice {
 public:
  void CreateAndDestroy() {
    std::unique_ptr<MsdArmDevice> device = MsdArmDevice::Create(GetTestDeviceHandle(), false);
    EXPECT_NE(device, nullptr);
  }

  void Dump() {
    std::unique_ptr<MsdArmDevice> device = MsdArmDevice::Create(GetTestDeviceHandle(), false);
    EXPECT_NE(device, nullptr);

    MsdArmDevice::DumpState dump_state;
    device->Dump(&dump_state, true);
    ASSERT_EQ(12u, dump_state.power_states.size());
    EXPECT_EQ(std::string("L2 Cache"), dump_state.power_states[0].core_type);
    EXPECT_EQ(std::string("Present"), dump_state.power_states[0].status_type);
    EXPECT_EQ(1lu, dump_state.power_states[0].bitmask);

    EXPECT_EQ(0u, dump_state.gpu_fault_status);
    EXPECT_EQ(0u, dump_state.gpu_fault_address);

    EXPECT_EQ(3u, dump_state.job_slot_status.size());
    for (size_t i = 0; i < dump_state.job_slot_status.size(); i++)
      EXPECT_EQ(0u, dump_state.job_slot_status[i].status);

    EXPECT_EQ(8u, dump_state.address_space_status.size());
    for (size_t i = 0; i < dump_state.address_space_status.size(); i++)
      EXPECT_EQ(0u, dump_state.address_space_status[i].status);

    std::vector<std::string> dump_string;
    device->FormatDump(dump_state, &dump_string);
    EXPECT_TRUE(IsStringInDump(dump_string, "Core type L2 Cache state Present bitmap: 0x1"));
    EXPECT_TRUE(IsStringInDump(dump_string, "Job slot 2 status 0x0 head 0x0 tail 0x0 config 0x0"));
    EXPECT_TRUE(IsStringInDump(dump_string, "AS 7 status 0x0 fault status 0x0 fault address 0x0"));
    EXPECT_TRUE(IsStringInDump(
        dump_string, "Fault source_id 0, access type \"unknown\", exception type: \"Unknown\""));
  }

  void MockDump() {
    auto reg_io = std::make_unique<magma::RegisterIo>(MockMmio::Create(1024 * 1024));

    uint32_t offset = static_cast<uint32_t>(registers::CoreReadyState::CoreType::kShader) +
                      static_cast<uint32_t>(registers::CoreReadyState::StatusType::kReady);
    reg_io->Write32(offset, 2);
    reg_io->Write32(offset + 4, 5);

    static constexpr uint64_t kFaultAddress = 0xffffffff88888888lu;
    registers::GpuFaultAddress::Get().FromValue(kFaultAddress).WriteTo(reg_io.get());
    registers::GpuFaultStatus::Get().FromValue(5).WriteTo(reg_io.get());

    registers::AsRegisters(7).Status().FromValue(5).WriteTo(reg_io.get());
    registers::AsRegisters(7).FaultStatus().FromValue(12).WriteTo(reg_io.get());
    registers::AsRegisters(7).FaultAddress().FromValue(kFaultAddress).WriteTo(reg_io.get());
    registers::JobSlotRegisters(2).Status().FromValue(10).WriteTo(reg_io.get());
    registers::JobSlotRegisters(1).Head().FromValue(9).WriteTo(reg_io.get());
    registers::JobSlotRegisters(0).Tail().FromValue(8).WriteTo(reg_io.get());
    registers::JobSlotRegisters(0).Config().FromValue(7).WriteTo(reg_io.get());

    MsdArmDevice::DumpState dump_state;
    GpuFeatures features;
    features.address_space_count = 9;
    features.job_slot_count = 7;
    MsdArmDevice::DumpRegisters(features, reg_io.get(), &dump_state);
    bool found = false;
    for (auto& pstate : dump_state.power_states) {
      if (std::string("Shader") == pstate.core_type && std::string("Ready") == pstate.status_type) {
        EXPECT_EQ(0x500000002ul, pstate.bitmask);
        found = true;
      }
    }
    EXPECT_EQ(5u, dump_state.gpu_fault_status);
    EXPECT_EQ(kFaultAddress, dump_state.gpu_fault_address);
    EXPECT_EQ(5u, dump_state.address_space_status[7].status);
    EXPECT_EQ(12u, dump_state.address_space_status[7].fault_status);
    EXPECT_EQ(kFaultAddress, dump_state.address_space_status[7].fault_address);
    EXPECT_EQ(10u, dump_state.job_slot_status[2].status);
    EXPECT_EQ(9u, dump_state.job_slot_status[1].head);
    EXPECT_EQ(8u, dump_state.job_slot_status[0].tail);
    EXPECT_EQ(7u, dump_state.job_slot_status[0].config);
    EXPECT_TRUE(found);
  }

  void ProcessRequest() {
    std::unique_ptr<MsdArmDevice> device(MsdArmDevice::Create(GetTestDeviceHandle(), false));
    ASSERT_NE(device, nullptr);

    class TestRequest : public DeviceRequest {
     public:
      TestRequest(std::shared_ptr<bool> processing_complete)
          : processing_complete_(processing_complete) {}

     protected:
      magma::Status Process(MsdArmDevice* device) override {
        *processing_complete_ = true;
        return MAGMA_STATUS_OK;
      }

     private:
      std::shared_ptr<bool> processing_complete_;
    };

    auto processing_complete = std::make_shared<bool>(false);

    auto request = std::make_unique<TestRequest>(processing_complete);
    request->ProcessAndReply(device.get());

    EXPECT_TRUE(processing_complete);
  }

  void MockExecuteAtom() {
    auto register_io = std::make_unique<magma::RegisterIo>(MockMmio::Create(1024 * 1024));
    magma::RegisterIo* reg_io = register_io.get();

    std::unique_ptr<MsdArmDevice> device = MsdArmDevice::Create(GetTestDeviceHandle(), false);
    EXPECT_NE(device, nullptr);
    device->set_register_io(std::move(register_io));
    auto connection = MsdArmConnection::Create(0, device.get());

    auto null_atom =
        std::make_unique<MsdArmAtom>(connection, 0, 0, 0, magma_arm_mali_user_data(), 0);
    device->scheduler_->EnqueueAtom(std::move(null_atom));
    device->scheduler_->TryToSchedule();

    // Atom has 0 job chain address and should be thrown out.
    EXPECT_EQ(0u, device->scheduler_->GetAtomListSize());

    MsdArmAtom atom(connection, 5, 0, 0, magma_arm_mali_user_data(), 0);
    atom.set_require_cycle_counter();
    device->ExecuteAtomOnDevice(&atom, reg_io);
    EXPECT_EQ(registers::GpuCommand::kCmdCycleCountStart,
              reg_io->Read32(registers::GpuCommand::kOffset));

    constexpr uint32_t kJobSlot = 1;
    auto connection1 = MsdArmConnection::Create(0, device.get());
    MsdArmAtom atom1(connection1, 100, kJobSlot, 0, magma_arm_mali_user_data(), 0);

    device->ExecuteAtomOnDevice(&atom1, reg_io);

    registers::JobSlotRegisters regs(kJobSlot);
    EXPECT_EQ(0xffffffffffffffffu, regs.AffinityNext().ReadFrom(reg_io).reg_value());
    EXPECT_EQ(100u, regs.HeadNext().ReadFrom(reg_io).reg_value());
    constexpr uint32_t kCommandStart = registers::JobSlotCommand::kCommandStart;
    EXPECT_EQ(kCommandStart, regs.CommandNext().ReadFrom(reg_io).reg_value());
    auto config_next = regs.ConfigNext().ReadFrom(reg_io);

    // connection should get address slot 0, and connection1 should get
    // slot 1.
    EXPECT_EQ(1u, config_next.address_space().get());
    EXPECT_EQ(1u, config_next.start_flush_clean().get());
    EXPECT_EQ(1u, config_next.start_flush_invalidate().get());
    EXPECT_EQ(0u, config_next.job_chain_flag().get());
    EXPECT_EQ(1u, config_next.end_flush_clean().get());
    EXPECT_EQ(1u, config_next.end_flush_invalidate().get());
    EXPECT_EQ(0u, config_next.enable_flush_reduction().get());
    EXPECT_EQ(0u, config_next.disable_descriptor_write_back().get());
    EXPECT_EQ(8u, config_next.thread_priority().get());

    EXPECT_EQ(registers::GpuCommand::kCmdCycleCountStart,
              reg_io->Read32(registers::GpuCommand::kOffset));
    device->AtomCompleted(&atom, kArmMaliResultSuccess);
    EXPECT_EQ(registers::GpuCommand::kCmdCycleCountStop,
              reg_io->Read32(registers::GpuCommand::kOffset));
  }

  void TestIdle() {
    std::unique_ptr<MsdArmDevice> device = MsdArmDevice::Create(GetTestDeviceHandle(), false);
    EXPECT_NE(device, nullptr);

    MsdArmDevice::DumpState dump_state;
    device->Dump(&dump_state, false);

    // Ensure that the GPU is idle and not doing anything at this point. A
    // failure in this may be caused by a previous test.
    EXPECT_EQ(0u, dump_state.gpu_status);
  }

  void MockInitializeQuirks() {
    auto reg_io = std::make_unique<magma::RegisterIo>(MockMmio::Create(1024 * 1024));
    GpuFeatures features;
    features.gpu_id.set_reg_value(0x72120000);

    MsdArmDevice::InitializeHardwareQuirks(&features, reg_io.get());
    EXPECT_EQ(1u << 17, reg_io->Read32(0xf04));
    features.gpu_id.set_reg_value(0x08201000);  // T820 R1P0
    MsdArmDevice::InitializeHardwareQuirks(&features, reg_io.get());
    EXPECT_EQ(1u << 16, reg_io->Read32(0xf04));
    features.gpu_id.set_reg_value(0x9990000);
    MsdArmDevice::InitializeHardwareQuirks(&features, reg_io.get());
    EXPECT_EQ(0u, reg_io->Read32(0xf04));
  }

  void ProtectedMode() {
    // Use device thread so the test can wait for a reset interrupt.
    std::unique_ptr<MsdArmDevice> device = MsdArmDevice::Create(GetTestDeviceHandle(), true);
    ASSERT_NE(nullptr, device);
    if (!device->IsProtectedModeSupported()) {
      printf("Protected mode not supported, skipping test\n");
      return;
    }

    EXPECT_FALSE(device->IsInProtectedMode());
    EXPECT_EQ(1u, device->power_manager_->l2_ready_status());

    EXPECT_TRUE(device->perf_counters_->Enable());

    device->EnterProtectedMode();
    EXPECT_EQ(1u, device->power_manager_->l2_ready_status());
    EXPECT_TRUE(device->IsInProtectedMode());
    EXPECT_TRUE(device->perf_counters_->running());

    EXPECT_TRUE(device->ExitProtectedMode());
    EXPECT_EQ(1u, device->power_manager_->l2_ready_status());
    EXPECT_FALSE(device->IsInProtectedMode());
    // Exiting protected mode should disable performance counters.
    EXPECT_FALSE(device->perf_counters_->running());
  }

  void PowerDownL2() {
    // Use device thread so the test can wait for a power down interrupt.
    std::unique_ptr<MsdArmDevice> device = MsdArmDevice::Create(GetTestDeviceHandle(), true);
    ASSERT_NE(nullptr, device);

    // In theory this could work without protected mode, but it's not needed. On the amlogic
    // T820 in the VIM2, powering down the L2 seems to cause GPU faults when the shader cores
    // are later powered back up again.
    if (!device->IsProtectedModeSupported()) {
      printf("Protected mode not supported, skipping test\n");
      return;
    }

    EXPECT_TRUE(device->PowerDownL2());
    EXPECT_EQ(0u, device->power_manager_->l2_ready_status());
  }
};

TEST(MsdArmDevice, CreateAndDestroy) {
  TestMsdArmDevice test;
  test.CreateAndDestroy();
}

TEST(MsdArmDevice, Dump) {
  TestMsdArmDevice test;
  test.Dump();
}

TEST(MsdArmDevice, MockDump) {
  TestMsdArmDevice test;
  test.MockDump();
}

TEST(MsdArmDevice, ProcessRequest) {
  TestMsdArmDevice test;
  test.ProcessRequest();
}

TEST(MsdArmDevice, MockExecuteAtom) {
  TestMsdArmDevice test;
  test.MockExecuteAtom();
}

TEST(MsdArmDevice, Idle) {
  TestMsdArmDevice test;
  test.TestIdle();
}

TEST(MsdArmDevice, MockInitializeQuirks) {
  TestMsdArmDevice test;
  test.MockInitializeQuirks();
}

TEST(MsdArmDevice, ProtectMode) {
  TestMsdArmDevice test;
  test.ProtectedMode();
}

TEST(MsdArmDevice, PowerDownL2) {
  TestMsdArmDevice test;
  test.PowerDownL2();
}
