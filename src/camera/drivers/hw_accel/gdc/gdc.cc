// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gdc.h"

#include <lib/image-format/image_format.h>
#include <stdint.h>
#include <zircon/assert.h>
#include <zircon/threads.h>
#include <zircon/types.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <hw/reg.h>

#include "gdc-regs.h"
#include "src/lib/syslog/cpp/logger.h"

namespace gdc {

namespace {

constexpr auto kTag = "gdc";

constexpr uint32_t kHiu = 0;
constexpr uint32_t kGdc = 1;
constexpr uint32_t kAxiAlignment = 16;
constexpr uint32_t kWordSize = 4;

enum {
  COMPONENT_PDEV,
  COMPONENT_SENSOR,
  COMPONENT_COUNT,
};

}  // namespace

static inline uint32_t AxiWordAlign(uint32_t value) { return fbl::round_up(value, kAxiAlignment); }

void GdcDevice::InitClocks() {
  // First reset the clocks.
  GdcClkCntl::Get().ReadFrom(&clock_mmio_).reset_axi().reset_core().WriteTo(&clock_mmio_);

  // Set the clocks to 8Mhz
  // Source XTAL
  // Clock divisor = 3
  GdcClkCntl::Get()
      .ReadFrom(&clock_mmio_)
      .set_axi_clk_div(3)
      .set_axi_clk_en(1)
      .set_axi_clk_sel(0)
      .set_core_clk_div(3)
      .set_core_clk_en(1)
      .set_core_clk_sel(0)
      .WriteTo(&clock_mmio_);

  // Enable GDC Power domain.
  GdcMemPowerDomain::Get().ReadFrom(&clock_mmio_).set_gdc_pd(0).WriteTo(&clock_mmio_);
}

zx_status_t GdcDevice::GdcInitTask(const buffer_collection_info_2_t* input_buffer_collection,
                                   const buffer_collection_info_2_t* output_buffer_collection,
                                   const image_format_2_t* input_image_format,
                                   const image_format_2_t* output_image_format_table_list,
                                   size_t output_image_format_table_count,
                                   uint32_t output_image_format_index,
                                   const gdc_config_info* config_vmo_list, size_t config_vmos_count,
                                   const hw_accel_frame_callback_t* frame_callback,
                                   const hw_accel_res_change_callback* res_callback,
                                   const hw_accel_remove_task_callback_t* remove_task_callback,
                                   uint32_t* out_task_index) {
  fbl::AutoLock al(&interface_lock_);

  if (out_task_index == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AllocChecker ac;
  auto task = std::unique_ptr<GdcTask>(new (&ac) GdcTask());
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  zx_status_t status = task->Init(
      input_buffer_collection, output_buffer_collection, input_image_format,
      output_image_format_table_list, output_image_format_table_count, output_image_format_index,
      config_vmo_list, config_vmos_count, frame_callback, res_callback, remove_task_callback, bti_);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "Task Creation Failed";
    return status;
  }

  // Put an entry in the hashmap.
  task_map_[next_task_index_] = std::move(task);
  *out_task_index = next_task_index_;
  next_task_index_++;
  return ZX_OK;
}

void GdcDevice::Start() {
  // Transition from 0->1 means GDC latches the data on the
  // configuration ports and starts the processing.
  // clang-format off
  Config::Get()
      .ReadFrom(gdc_mmio())
      .set_start(0)
      .WriteTo(gdc_mmio());

  Config::Get()
      .ReadFrom(gdc_mmio())
      .set_start(1)
      .WriteTo(gdc_mmio());
  // clang-format on
}

void GdcDevice::Stop() {
  // clang-format off
  Config::Get()
      .ReadFrom(gdc_mmio())
      .set_start(0)
      .WriteTo(gdc_mmio());
  // clang-format on
}

void GdcDevice::ProcessFrame(TaskInfo& info) {
  auto task = info.task;
  // The way we have our SW instrumented, GDC should never be busy
  // proccessing at this point. Doing a sanity check here to ensure
  // that its not busy processing an image.
  ZX_ASSERT(!Status::Get().ReadFrom(gdc_mmio()).busy());

  auto input_buffer_index = info.index;

  Stop();

  // Program the GDC configuration registers.
  auto size = task->GetConfigVmoSize(task->output_format_index()) / kWordSize;
  auto addr = AxiWordAlign(task->GetConfigVmoPhysAddr(task->output_format_index()));

  // clang-format off
  ConfigAddr::Get()
      .ReadFrom(gdc_mmio())
      .set_config_addr(addr)
      .WriteTo(gdc_mmio());

  ConfigSize::Get()
      .ReadFrom(gdc_mmio())
      .set_config_size(size)
      .WriteTo(gdc_mmio());

  // Program the Input frame details.
  auto input_format = task->input_format();
  DataInWidth::Get()
      .ReadFrom(gdc_mmio())
      .set_width(input_format.display_width)
      .WriteTo(gdc_mmio());

  DataInHeight::Get()
      .ReadFrom(gdc_mmio())
      .set_height(input_format.display_height)
      .WriteTo(gdc_mmio());

  // Program the Output frame details.
  auto output_format = task->output_format();
  DataOutWidth::Get()
      .ReadFrom(gdc_mmio())
      .set_width(output_format.display_width)
      .WriteTo(gdc_mmio());

  DataOutHeight::Get()
      .ReadFrom(gdc_mmio())
      .set_height(output_format.display_height)
      .WriteTo(gdc_mmio());

  // Program Data1In Address Register (Y).
  zx_paddr_t input_y_addr;
  auto input_line_offset = input_format.bytes_per_row;
  ZX_ASSERT(ZX_OK == task->GetInputBufferPhysAddr(input_buffer_index, &input_y_addr));
  Data1InAddr::Get()
      .ReadFrom(gdc_mmio())
      .set_addr(AxiWordAlign(input_y_addr))
      .WriteTo(gdc_mmio());

  // Program Data1In Offset Register (Y)
  Data1InOffset::Get()
      .ReadFrom(gdc_mmio())
      .set_offset(input_line_offset)
      .WriteTo(gdc_mmio());

  // Program Data2In Address Register (UV).
  auto input_uv_addr = input_y_addr + (input_line_offset * input_format.display_height);
  Data2InAddr::Get()
      .ReadFrom(gdc_mmio())
      .set_addr(AxiWordAlign(input_uv_addr))
      .WriteTo(gdc_mmio());

  // Program Data2In Offset Register (UV)
  Data2InOffset::Get()
      .ReadFrom(gdc_mmio())
      .set_offset(input_line_offset)
      .WriteTo(gdc_mmio());

  // clang-format on

  // Now programming the output DMA registers.
  // First fetch an unused buffer from the VMO pool.
  auto result = task->GetOutputBufferPhysAddr();
  if (result.is_error()) {
    frame_available_info info;
    info.frame_status = FRAME_STATUS_ERROR_BUFFER_FULL;
    info.metadata.input_buffer_index = input_buffer_index;
    info.metadata.timestamp = static_cast<uint64_t>(zx_clock_get_monotonic());
    info.metadata.image_format_index = task->output_format_index();
    task->FrameReadyCallback(&info);
    return;
  }

  auto output_y_addr = result.value();

  // Program Data1Out Address Register (Y).
  auto output_line_offset = output_format.bytes_per_row;
  // clang-format off

  Data1OutAddr::Get()
      .ReadFrom(gdc_mmio())
      .set_addr(AxiWordAlign(output_y_addr))
      .WriteTo(gdc_mmio());

  // Program Data1Out Offset Register (Y)
  Data1OutOffset::Get()
      .ReadFrom(gdc_mmio())
      .set_offset(output_line_offset)
      .WriteTo(gdc_mmio());

  // Program Data2Out Address Register (UV).
  auto output_uv_addr = output_y_addr + (output_line_offset * output_format.display_height);
  Data2OutAddr::Get()
      .ReadFrom(gdc_mmio())
      .set_addr(AxiWordAlign(output_uv_addr))
      .WriteTo(gdc_mmio());

  // Program Data2Out Offset Register (UV)
  Data2OutOffset::Get()
      .ReadFrom(gdc_mmio())
      .set_offset(output_line_offset)
      .WriteTo(gdc_mmio());
  // clang-format on

  // Start GDC processing.
  Start();

  zx_port_packet_t packet;
  ZX_ASSERT(ZX_OK == WaitForInterrupt(&packet));

  // Only Assert on ACK failure if its an actual HW interrupt.
  // Currently we are injecting packets on the same ports for tests to
  // fake an actual HW interrupt to test the callback functionality.
  // This causes the IRQ object to be in a bad state when ACK'd.
  if (packet.key == kPortKeyIrqMsg) {
    ZX_ASSERT(gdc_irq_.ack() == ZX_OK);
  }

  if (packet.key == kPortKeyDebugFakeInterrupt || packet.key == kPortKeyIrqMsg) {
    // Invoke the callback function and tell about the output buffer index
    // which is ready to be used.
    frame_available_info info;
    info.frame_status = FRAME_STATUS_OK;
    info.buffer_id = task->GetOutputBufferIndex();
    info.metadata.input_buffer_index = input_buffer_index;
    info.metadata.timestamp = static_cast<uint64_t>(zx_clock_get_monotonic());
    info.metadata.image_format_index = task->output_format_index();
    task->FrameReadyCallback(&info);
  }
}

void GdcDevice::ChangeOutputResoultion(TaskInfo& info) {
  auto task = info.task;
  // TODO: GDC needs an array of config vmos, one corresponding to
  // an output resolution. Pass an array of config vmos via init,
  // and add a method to get the config vmo corresponding to the
  // new format. For now, all we do is switch to the new imageformat.
  task->set_output_format_index(info.index);
  // Invoke the callback function and tell about the output buffer index
  // which is ready to be used.
  frame_available_info f_info;
  f_info.frame_status = FRAME_STATUS_OK;
  f_info.metadata.timestamp = static_cast<uint64_t>(zx_clock_get_monotonic());
  f_info.metadata.image_format_index = task->output_format_index();
  task->ResolutionChangeCallback(&f_info);
}

void GdcDevice::RemoveTask(TaskInfo& info) {
  // TODO(braval): Implement this functionality.
}

void GdcDevice::ProcessTask(TaskInfo& info) {
  switch (info.op) {
    case GDC_OP_FRAME: {
      return ProcessFrame(info);
    }
    case GDC_OP_SETOUTPUTRES: {
      return ChangeOutputResoultion(info);
    }
    case GDC_OP_REMOVE_TASK: {
      return RemoveTask(info);
    }
    default: {
      ZX_ASSERT_MSG(false, "Unknown GDC Op\n");
      return;
    }
  }
}

int GdcDevice::FrameProcessingThread() {
  FX_LOG(INFO, kTag, "start");
  for (;;) {
    fbl::AutoLock al(&processing_queue_lock_);
    while (processing_queue_.empty() && !shutdown_) {
      frame_processing_signal_.Wait(&processing_queue_lock_);
    }
    if (shutdown_) {
      break;
    }
    auto info = processing_queue_.back();
    processing_queue_.pop_back();
    al.release();
    ProcessTask(info);
  }
  return ZX_OK;
}

zx_status_t GdcDevice::GdcSetOutputResolution(uint32_t task_index,
                                              uint32_t new_output_image_format_index) {
  fbl::AutoLock al(&interface_lock_);

  // Find the entry in hashmap.
  auto task_entry = task_map_.find(task_index);
  if (task_entry == task_map_.end()) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Validate new image format index|.
  if (!task_entry->second->IsOutputFormatIndexValid(new_output_image_format_index)) {
    return ZX_ERR_INVALID_ARGS;
  }

  TaskInfo info;
  info.op = GDC_OP_SETOUTPUTRES;
  info.task = task_entry->second.get();
  info.index = new_output_image_format_index;
  info.task_index = task_index;

  // Put the task on queue.
  fbl::AutoLock lock(&processing_queue_lock_);
  processing_queue_.push_front(info);
  frame_processing_signal_.Signal();
  return ZX_OK;
}

zx_status_t GdcDevice::GdcProcessFrame(uint32_t task_index, uint32_t input_buffer_index) {
  fbl::AutoLock al(&interface_lock_);

  // Find the entry in hashmap.
  auto task_entry = task_map_.find(task_index);
  if (task_entry == task_map_.end()) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Validate |input_buffer_index|.
  if (!task_entry->second->IsInputBufferIndexValid(input_buffer_index)) {
    return ZX_ERR_INVALID_ARGS;
  }

  TaskInfo info;
  info.op = GDC_OP_FRAME;
  info.task = task_entry->second.get();
  info.index = input_buffer_index;
  info.task_index = task_index;

  // Put the task on queue.
  fbl::AutoLock lock(&processing_queue_lock_);
  processing_queue_.push_front(info);
  frame_processing_signal_.Signal();
  return ZX_OK;
}

zx_status_t GdcDevice::StartThread() {
  return thrd_status_to_zx_status(thrd_create_with_name(
      &processing_thread_,
      [](void* arg) -> int { return reinterpret_cast<GdcDevice*>(arg)->FrameProcessingThread(); },
      this, "gdc-processing-thread"));
}

zx_status_t GdcDevice::StopThread() {
  // Signal the worker thread and wait for it to terminate.
  {
    fbl::AutoLock al(&processing_queue_lock_);
    shutdown_ = true;
    frame_processing_signal_.Signal();
  }
  JoinThread();
  return ZX_OK;
}

zx_status_t GdcDevice::WaitForInterrupt(zx_port_packet_t* packet) {
  return port_.wait(zx::time::infinite(), packet);
}

void GdcDevice::GdcRemoveTask(uint32_t task_index) {
  fbl::AutoLock al(&interface_lock_);

  // Find the entry in hashmap.
  auto task_entry = task_map_.find(task_index);
  ZX_ASSERT(task_entry != task_map_.end());

  // Remove map entry.
  task_map_.erase(task_entry);
}

void GdcDevice::GdcReleaseFrame(uint32_t task_index, uint32_t buffer_index) {
  fbl::AutoLock al(&interface_lock_);

  // Find the entry in hashmap.
  auto task_entry = task_map_.find(task_index);
  ZX_ASSERT(task_entry != task_map_.end());

  auto task = task_entry->second.get();
  ZX_ASSERT(ZX_OK == task->ReleaseOutputBuffer(buffer_index));
}

// static
zx_status_t GdcDevice::Setup(void* /*ctx*/, zx_device_t* parent, std::unique_ptr<GdcDevice>* out) {
  ddk::CompositeProtocolClient composite(parent);
  if (!composite.is_valid()) {
    FX_LOG(ERROR, kTag, "could not get composite protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_device_t* components[COMPONENT_COUNT];
  size_t actual;
  composite.GetComponents(components, COMPONENT_COUNT, &actual);
  if (actual != COMPONENT_COUNT) {
    FX_LOG(ERROR, kTag, "Could not get components");
    return ZX_ERR_NOT_SUPPORTED;
  }

  ddk::PDev pdev(components[COMPONENT_PDEV]);
  if (!pdev.is_valid()) {
    FX_LOG(ERROR, kTag, "ZX_PROTOCOL_PDEV not available");
    return ZX_ERR_NO_RESOURCES;
  }

  std::optional<ddk::MmioBuffer> clk_mmio;
  zx_status_t status = pdev.MapMmio(kHiu, &clk_mmio);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "pdev_.MapMmio failed";
    return status;
  }

  std::optional<ddk::MmioBuffer> gdc_mmio;
  status = pdev.MapMmio(kGdc, &gdc_mmio);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "pdev_.MapMmio failed";
    return status;
  }

  zx::interrupt gdc_irq;
  status = pdev.GetInterrupt(0, &gdc_irq);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "pdev_.GetInterrupt failed";
    return status;
  }

  zx::port port;
  status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "port create failed";
    return status;
  }

  status = gdc_irq.bind(port, kPortKeyIrqMsg, 0 /*options*/);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "interrupt bind failed";
    return status;
  }

  zx::bti bti;
  status = pdev.GetBti(0, &bti);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "could not obtain bti";
    return status;
  }

  fbl::AllocChecker ac;
  auto gdc_device = std::unique_ptr<GdcDevice>(
      new (&ac) GdcDevice(parent, std::move(*clk_mmio), std::move(*gdc_mmio), std::move(gdc_irq),
                          std::move(bti), std::move(port)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  gdc_device->InitClocks();

  status = gdc_device->StartThread();
  *out = std::move(gdc_device);
  return status;
}

void GdcDevice::DdkUnbindNew(ddk::UnbindTxn txn) {
  ShutDown();
  txn.Reply();
}

void GdcDevice::DdkRelease() {
  StopThread();
  delete this;
}

void GdcDevice::ShutDown() {}

zx_status_t GdcBind(void* ctx, zx_device_t* device) {
  std::unique_ptr<GdcDevice> gdc_device;
  zx_status_t status = gdc::GdcDevice::Setup(ctx, device, &gdc_device);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "Could not setup gdc device";
    return status;
  }
  zx_device_prop_t props[] = {
      {BIND_PLATFORM_PROTO, 0, ZX_PROTOCOL_GDC},
  };

// Run the unit tests for this device
// TODO(braval): CAM-44 (Run only when build flag enabled)
// This needs to be replaced with run unittests hooks when
// the framework is available.
#if 0
    status = gdc::GdcDeviceTester::RunTests(gdc_device.get());
    if (status != ZX_OK) {
        FX_LOG(ERROR, kTag, "Device Unit Tests Failed");
        return status;
    }
#endif

  status = gdc_device->DdkAdd("gdc", 0, props, countof(props));
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "Could not add gdc device";
    return status;
  }

  FX_LOG(INFO, kTag, "gdc driver added");

  // gdc device intentionally leaked as it is now held by DevMgr.
  __UNUSED auto* dev = gdc_device.release();
  return status;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = GdcBind;
  return ops;
}();

}  // namespace gdc

// clang-format off
ZIRCON_DRIVER_BEGIN(gdc, gdc::driver_ops, "gdc", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_ARM),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GDC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_ARM_MALI_IV010),
ZIRCON_DRIVER_END(gdc)
