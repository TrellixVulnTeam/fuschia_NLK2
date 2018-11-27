// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/arch/arm64/gic_distributor.h"

#include <endian.h>
#include <fcntl.h>

#include <fbl/unique_fd.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/fdio/util.h>
#include <lib/zx/channel.h>
#include <libzbi/zbi.h>
#include <zircon/boot/driver-config.h>

#include "garnet/lib/machina/bits.h"
#include "garnet/lib/machina/guest.h"
#include "garnet/lib/machina/vcpu.h"
#include "lib/fxl/logging.h"

__BEGIN_CDECLS;
#include <libfdt.h>
__END_CDECLS;

namespace machina {

static constexpr char kSysInfoPath[] = "/dev/misc/sysinfo";
static constexpr uint32_t kGicv2Revision = 2;
static constexpr uint32_t kGicv3Revision = 3;
static constexpr uint32_t kGicdCtlr = 0x7;
static constexpr uint32_t kGicdCtlrARENSMask = 1u << 5;
static constexpr uint32_t kGicdIrouteIRMMask = 1u << 31;

static zx_status_t get_gic_version(GicVersion* version) {
  fbl::unique_fd fd(open(kSysInfoPath, O_RDWR));
  if (!fd) {
    return ZX_ERR_IO;
  }

  zx::channel channel;
  zx_status_t status =
      fdio_get_service_handle(fd.release(), channel.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }

  fuchsia_sysinfo_InterruptControllerInfo info;
  zx_status_t fidl_status = fuchsia_sysinfo_DeviceGetInterruptControllerInfo(
      channel.get(), &status, &info);
  if (fidl_status != ZX_OK) {
    return fidl_status;
  } else if (status != ZX_OK) {
    return status;
  }

  switch (info.type) {
    case fuchsia_sysinfo_InterruptControllerType_GIC_V2:
      *version = GicVersion::V2;
      return ZX_OK;
    case fuchsia_sysinfo_InterruptControllerType_GIC_V3:
      *version = GicVersion::V3;
      return ZX_OK;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

// clang-format off

// For arm64, memory addresses must be in a 36-bit range. This is due to limits
// placed within the MMU code based on the limits of a Cortex-A53.
//
//See ARM DDI 0487B.b, Table D4-25 for the maximum IPA range that can be used.

// GIC v2 distributor memory range.
static constexpr uint64_t kGicv2DistributorPhysBase      = 0x800001000;
static constexpr uint64_t kGicv2DistributorSize          = 0x1000;

// GIC v3 distributor memory range.
static constexpr uint64_t kGicv3DistributorPhysBase      = 0x800000000;
static constexpr uint64_t kGicv3DistributorSize          = 0x10000;

// GIC v3 Redistributor memory range.
//
// See GIC v3.0/v4.0 Architecture Spec 8.10.
static constexpr uint64_t kGicv3RedistributorPhysBase    = 0x800010000; // GICR_RD_BASE
static constexpr uint64_t kGicv3RedistributorSize        = 0x10000;
static constexpr uint64_t kGicv3RedistributorSgiPhysBase = 0x800020000; // GICR_SGI_BASE
static constexpr uint64_t kGicv3RedistributorSgiSize     = 0x10000;
static constexpr uint64_t kGicv3RedistributorStride      = 0x20000;
static_assert(kGicv3RedistributorPhysBase + kGicv3RedistributorSize == kGicv3RedistributorSgiPhysBase,
              "GICv3 Redistributor base and SGI base must be continguous");
static_assert(kGicv3RedistributorStride >= kGicv3RedistributorSize + kGicv3RedistributorSgiSize,
              "GICv3 Redistributor stride must be >= the size of a single mapping");

// GIC Distributor registers.
enum class GicdRegister : uint64_t {
    CTL           = 0x000,
    TYPE          = 0x004,
    IGROUP0       = 0x080,
    IGROUP31      = 0x0FC,
    ISENABLE0     = 0x100,
    ISENABLE31    = 0x11c,
    ICENABLE0     = 0x180,
    ICENABLE31    = 0x19c,
    ICPEND0       = 0x280,
    ICPEND31      = 0x2bc,
    ICFG0         = 0xc00,
    ICFG1         = 0xc04,
    ICFG31        = 0xc7c,
    ICACTIVE0     = 0x380,
    ICACTIVE15    = 0x3bc,
    IPRIORITY0    = 0x400,
    IPRIORITY255  = 0x4fc,
    ITARGETS0     = 0x800,
    ITARGETS7     = 0x81c,
    ITARGETS8     = 0x820,
    ITARGETS63    = 0x8fc,
    IGRPMOD0      = 0xd00,
    IGRPMOD31     = 0xd7c,
    SGI           = 0xf00,
    PID2_V2       = 0xfe8,
    // This is the offset of PID2 register when are running GICv3,
    // since the offset mappings of GICD & GICR are 0x1000 apart
    PID2_V2_V3    = 0x1fe8,
    PID2_V3       = 0xffe8,
    IROUTE32      = 0x6100,
    IROUTE1019    = 0x7fd8,
};

// GIC Redistributor registers.
enum class GicrRegister : uint64_t {
    // Offset from RD_BASE
    CTL           = 0x000,
    TYPE          = 0x008,
    WAKE          = 0x014,
    PID2_V3       = 0xffe8,
    // Offset from SGI_BASE
    IGROUP0       = 0x10080,
    ISENABLE0     = 0x10100,
    ICENABLE0     = 0x10180,
    ICPEND0       = 0x10280,
    ICACTIVE0     = 0x10380,
    IPRIORITY0    = 0x10400,
    IPRIORITY255  = 0x104fc,
    ICFG0         = 0x10c00,
    ICFG1         = 0x10c04,
};

// Target CPU for the software-generated interrupt.
enum class InterruptTarget {
  MASK            = 0b00,
  ALL_BUT_LOCAL   = 0b01,
  LOCAL           = 0b10,
};

// clang-format on

// Software-generated interrupt received by the GIC distributor.
struct SoftwareGeneratedInterrupt {
  InterruptTarget target;
  uint8_t cpu_mask;
  uint8_t vector;

  SoftwareGeneratedInterrupt(uint32_t sgi) {
    target = static_cast<InterruptTarget>(bits_shift(sgi, 25, 24));
    cpu_mask = static_cast<uint8_t>(bits_shift(sgi, 23, 16));
    vector = static_cast<uint8_t>(bits_shift(sgi, 3, 0));
  }
};

static size_t gicd_register_size(uint64_t addr) {
  if (addr >= static_cast<uint64_t>(GicdRegister::IROUTE32) &&
      addr <= static_cast<uint64_t>(GicdRegister::IROUTE1019)) {
    return 8;
  } else {
    return 4;
  }
}

static uint32_t typer(uint32_t num_interrupts, uint8_t num_cpus,
                      GicVersion version) {
  uint32_t typer = set_bits((num_interrupts >> 5) - 1, 4, 0);
  typer |= set_bits(num_cpus - 1, 7, 5);
  if (version == GicVersion::V3) {
    // Take log2 of num_interrupts
    uint8_t num_bits =
        (sizeof(num_interrupts) * CHAR_BIT) - __builtin_clz(num_interrupts - 1);
    typer |= set_bits(num_bits - 1, 23, 19);
  }
  return typer;
}

static uint32_t pidr2_arch_rev(uint32_t revision) {
  return set_bits(revision, 7, 4);
}

zx_status_t GicDistributor::Init(Guest* guest, uint8_t num_cpus) {
  zx_status_t status = get_gic_version(&gic_version_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get GIC version from sysinfo " << status;
    return status;
  }

  if (gic_version_ == GicVersion::V2) {
    return guest->CreateMapping(TrapType::MMIO_SYNC, kGicv2DistributorPhysBase,
                                kGicv2DistributorSize, 0, this);
  }

  // Map the distributor
  status = guest->CreateMapping(TrapType::MMIO_SYNC, kGicv3DistributorPhysBase,
                                kGicv3DistributorSize, 0, this);
  if (status != ZX_OK) {
    return status;
  }

  // Map the redistributors, map both RD_BASE and SGI_BASE as one since they
  // are contiguous. See GIC v3.0/v4.0 Architecture Spec 8.10.
  for (uint16_t id = 0; id != num_cpus; ++id) {
    auto redistributor =
        std::make_unique<GicRedistributor>(id, id == num_cpus - 1);
    status = guest->CreateMapping(
        TrapType::MMIO_SYNC,
        kGicv3RedistributorPhysBase + (id * kGicv3RedistributorStride),
        kGicv3RedistributorSize + kGicv3RedistributorSgiSize, 0,
        redistributor.get());
    if (status != ZX_OK) {
      return status;
    }
    redistributors_.push_back(std::move(redistributor));
  }
  return status;
}

zx_status_t GicDistributor::Read(uint64_t addr, IoValue* value) const {
  if (addr % 4 != 0 || value->access_size != gicd_register_size(addr)) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  switch (static_cast<GicdRegister>(addr)) {
    case GicdRegister::TYPE:
      value->u32 = typer(kNumInterrupts, kMaxVcpus, gic_version_);
      return ZX_OK;
    case GicdRegister::ICFG0:
      // SGIs are RAO/WI.
      value->u32 = UINT32_MAX;
      return ZX_OK;
    case GicdRegister::ICFG1... GicdRegister::ICFG31:
      value->u32 = 0;
      return ZX_OK;
    case GicdRegister::ISENABLE0... GicdRegister::ISENABLE31: {
      std::lock_guard<std::mutex> lock(mutex_);
      const uint8_t* enable =
          &enabled_[addr - static_cast<uint64_t>(GicdRegister::ISENABLE0)];
      value->u32 = *reinterpret_cast<const uint32_t*>(enable);
      return ZX_OK;
    }
    case GicdRegister::ITARGETS0... GicdRegister::ITARGETS7: {
      // GIC Architecture Spec 4.3.12: Each field of ITARGETS0 to ITARGETS7
      // returns a mask that corresponds only to the current processor.
      uint8_t mask = 1u << Vcpu::GetCurrent()->id();
      value->u32 = mask | mask << 8 | mask << 16 | mask << 24;
      return ZX_OK;
    }
    case GicdRegister::ITARGETS8... GicdRegister::ITARGETS63: {
      std::lock_guard<std::mutex> lock(mutex_);
      if (affinity_routing_) {
        value->u32 = 0;
        return ZX_OK;
      }
      const uint8_t* cpu_mask =
          &cpu_masks_[addr - static_cast<uint64_t>(GicdRegister::ITARGETS0)];
      // Target registers are read from 4 at a time.
      value->u32 = *reinterpret_cast<const uint32_t*>(cpu_mask);
      return ZX_OK;
    }
    case GicdRegister::IROUTE32... GicdRegister::IROUTE1019: {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!affinity_routing_) {
        value->u32 = 0;
        return ZX_OK;
      }
      uint16_t int_id = (addr - static_cast<uint64_t>(GicdRegister::IROUTE32)) /
                        value->access_size;
      if (broadcast_[int_id - kNumSgisAndPpis]) {
        value->u64 = kGicdIrouteIRMMask;
      } else {
        value->u64 =
            static_cast<uint64_t>(cpu_routes_[int_id - kNumSgisAndPpis]);
      }
      return ZX_OK;
    }
    case GicdRegister::PID2_V2_V3:
      value->u32 = pidr2_arch_rev(kGicv3Revision);
      return ZX_OK;
    case GicdRegister::PID2_V2:
      value->u32 = pidr2_arch_rev(kGicv2Revision);
      return ZX_OK;
    case GicdRegister::PID2_V3:
      value->u32 = pidr2_arch_rev(kGicv3Revision);
      return ZX_OK;
    case GicdRegister::CTL: {
      std::lock_guard<std::mutex> lock(mutex_);
      value->u32 = kGicdCtlr;
      if (gic_version_ == GicVersion::V3 && affinity_routing_) {
        value->u32 |= kGicdCtlrARENSMask;
      }
      return ZX_OK;
    }
    default:
      FXL_LOG(ERROR) << "Unhandled GIC distributor address read 0x" << std::hex
                     << addr;
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t GicDistributor::Write(uint64_t addr, const IoValue& value) {
  if (addr % 4 != 0 || value.access_size != gicd_register_size(addr)) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  switch (static_cast<GicdRegister>(addr)) {
    case GicdRegister::ITARGETS0... GicdRegister::ITARGETS7: {
      // GIC Architecture Spec 4.3.12: ITARGETS0 to ITARGETS7 are read only.
      FXL_LOG(ERROR) << "Write to read-only GIC distributor address 0x"
                     << std::hex << addr;
      return ZX_ERR_INVALID_ARGS;
    }
    case GicdRegister::ITARGETS8... GicdRegister::ITARGETS63: {
      std::lock_guard<std::mutex> lock(mutex_);
      if (affinity_routing_) {
        return ZX_OK;
      }
      uint8_t* cpu_mask =
          &cpu_masks_[addr - static_cast<uint64_t>(GicdRegister::ITARGETS0)];
      *reinterpret_cast<uint32_t*>(cpu_mask) = value.u32;
      return ZX_OK;
    }
    case GicdRegister::SGI: {
      SoftwareGeneratedInterrupt sgi(value.u32);
      uint8_t cpu_mask;
      switch (sgi.target) {
        case InterruptTarget::MASK:
          cpu_mask = sgi.cpu_mask;
          break;
        case InterruptTarget::ALL_BUT_LOCAL:
          cpu_mask = ~(1u << Vcpu::GetCurrent()->id());
          break;
        case InterruptTarget::LOCAL:
          cpu_mask = 1u << Vcpu::GetCurrent()->id();
          break;
        default:
          return ZX_ERR_NOT_SUPPORTED;
      }
      return TargetInterrupt(sgi.vector, cpu_mask);
    }
    case GicdRegister::ISENABLE0... GicdRegister::ISENABLE31: {
      std::lock_guard<std::mutex> lock(mutex_);
      uint8_t* enable =
          &enabled_[addr - static_cast<uint64_t>(GicdRegister::ISENABLE0)];
      *reinterpret_cast<uint32_t*>(enable) |= value.u32;
      return ZX_OK;
    }
    case GicdRegister::ICENABLE0... GicdRegister::ICENABLE31: {
      std::lock_guard<std::mutex> lock(mutex_);
      uint8_t* enable =
          &enabled_[addr - static_cast<uint64_t>(GicdRegister::ICENABLE0)];
      *reinterpret_cast<uint32_t*>(enable) &= ~value.u32;
      return ZX_OK;
    }
    case GicdRegister::CTL: {
      std::lock_guard<std::mutex> lock(mutex_);
      if (gic_version_ == GicVersion::V3 && (value.u32 & kGicdCtlrARENSMask)) {
        // Affinity routing is being enabled.
        affinity_routing_ = true;
        memset(broadcast_, UINT8_MAX, sizeof(broadcast_));
        return ZX_OK;
      }
      // Affinity routing is being disabled.
      affinity_routing_ = false;
      uint8_t default_mask = 0;
      for (size_t i = 0; i != kMaxVcpus; i++) {
        if (vcpus_[i] != nullptr) {
          default_mask |= 1 << i;
        }
      }
      memset(cpu_masks_, default_mask, sizeof(cpu_masks_));
      return ZX_OK;
    }
    case GicdRegister::IROUTE32... GicdRegister::IROUTE1019: {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!affinity_routing_) {
        return ZX_OK;
      }
      uint16_t int_id = (addr - static_cast<uint64_t>(GicdRegister::IROUTE32)) /
                        value.access_size;
      if (value.u64 == kGicdIrouteIRMMask) {
        broadcast_[int_id - kNumSgisAndPpis] = true;
      } else {
        broadcast_[int_id - kNumInterrupts] = false;
        cpu_routes_[int_id - kNumInterrupts] =
            static_cast<uint8_t>(value.u64 & UINT8_MAX);
      }
      return ZX_OK;
    }
    case GicdRegister::ICACTIVE0... GicdRegister::ICACTIVE15:
    case GicdRegister::ICFG0... GicdRegister::ICFG31:
    case GicdRegister::ICPEND0... GicdRegister::ICPEND31:
    case GicdRegister::IPRIORITY0... GicdRegister::IPRIORITY255:
    case GicdRegister::IGROUP0... GicdRegister::IGROUP31:
    case GicdRegister::IGRPMOD0... GicdRegister::IGRPMOD31:
      return ZX_OK;
    default:
      FXL_LOG(ERROR) << "Unhandled GIC distributor address write 0x" << std::hex
                     << addr;
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t GicDistributor::RegisterVcpu(uint8_t vcpu_num, Vcpu* vcpu) {
  if (vcpu_num > kMaxVcpus) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (vcpus_[vcpu_num] != nullptr) {
    return ZX_ERR_ALREADY_EXISTS;
  }
  vcpus_[vcpu_num] = vcpu;
  // We set the default state of all CPU masks to target every registered VCPU.
  uint8_t default_mask = cpu_masks_[0] | 1u << vcpu_num;
  memset(cpu_masks_, default_mask, sizeof(cpu_masks_));
  return ZX_OK;
}

zx_status_t GicDistributor::Interrupt(uint32_t global_irq) {
  uint8_t cpu_mask;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (gic_version_ == GicVersion::V3 && affinity_routing_) {
      if (global_irq < kNumSgisAndPpis || global_irq >= kNumInterrupts) {
        return ZX_ERR_INVALID_ARGS;
      }
      if (!broadcast_[global_irq - kNumSgisAndPpis]) {
        cpu_mask = 1 << cpu_routes_[global_irq - kNumSgisAndPpis];
      } else {
        cpu_mask = UINT8_MAX;
      }
    } else {
      cpu_mask = cpu_masks_[global_irq];
    }
  }
  return TargetInterrupt(global_irq, cpu_mask);
}

zx_status_t GicDistributor::TargetInterrupt(uint32_t global_irq,
                                            uint8_t cpu_mask) {
  if (global_irq >= kNumInterrupts) {
    return ZX_ERR_INVALID_ARGS;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    bool is_enabled =
        enabled_[global_irq / CHAR_BIT] & (1u << global_irq % CHAR_BIT);
    if (!is_enabled) {
      return ZX_OK;
    }
  }
  for (size_t i = 0; cpu_mask != 0; cpu_mask >>= 1, i++) {
    if (!(cpu_mask & 1)) {
      continue;
    }
    Vcpu* vcpu;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      vcpu = vcpus_[i];
    }
    if (vcpu == nullptr) {
      continue;
    }
    zx_status_t status = vcpu->Interrupt(global_irq);
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t GicDistributor::ConfigureZbi(void* zbi_base, size_t zbi_max) const {
  const dcfg_arm_gicv2_driver_t gic_v2 = {
      .mmio_phys = machina::kGicv2DistributorPhysBase,
      .gicd_offset = 0x0000,
      .gicc_offset = machina::kGicv2DistributorSize,
      .ipi_base = 12,
      .optional = true,
      .use_msi = true,
  };

  const dcfg_arm_gicv3_driver_t gic_v3 = {
      .mmio_phys = machina::kGicv3DistributorPhysBase,
      .gicd_offset = 0x00000,
      .gicr_offset = machina::kGicv3RedistributorSize,
      .gicr_stride = machina::kGicv3RedistributorStride,
      .ipi_base = 12,
      .optional = true,
  };

  zbi_result_t res;
  if (gic_version_ == machina::GicVersion::V2) {
    res =
        zbi_append_section(zbi_base, zbi_max, sizeof(gic_v2),
                           ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_GIC_V2, 0, &gic_v2);
  } else {
    // GICv3 driver.
    res =
        zbi_append_section(zbi_base, zbi_max, sizeof(gic_v3),
                           ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_GIC_V3, 0, &gic_v3);
  }
  return res == ZBI_RESULT_OK ? ZX_OK : ZX_ERR_INTERNAL;
}

static inline void gic_dtb_error(const char* reg) {
  FXL_LOG(ERROR) << "Failed to add GiC property \"" << reg << "\" to device "
                 << "tree, space must be reserved in the device tree";
}

zx_status_t GicDistributor::ConfigureDtb(void* dtb) const {
  int gic_off = fdt_path_offset(dtb, "/interrupt-controller@800000000");
  if (gic_off < 0) {
    FXL_LOG(ERROR) << "Failed to find \"/interrupt-controller\" in device tree";
    return ZX_ERR_BAD_STATE;
  }
  const char* compatible;
  uint64_t reg_prop[4];

  if (gic_version_ == machina::GicVersion::V2) {
    compatible = "arm,gic-400";
    // GICD memory map
    reg_prop[0] = machina::kGicv2DistributorPhysBase;
    reg_prop[1] = machina::kGicv2DistributorSize;
    // GICC memory map
    reg_prop[2] =
        machina::kGicv2DistributorPhysBase + machina::kGicv2DistributorSize;
    reg_prop[3] = 0x2000;
  } else {
    // Set V3 only properties
    int ret = fdt_setprop_u32(dtb, gic_off, "#redistributor-regions", 1);
    if (ret != 0) {
      gic_dtb_error("#redistributor-regions");
      return ZX_ERR_BAD_STATE;
    }

    compatible = "arm,gic-v3";
    // GICD memory map
    reg_prop[0] = machina::kGicv3DistributorPhysBase;
    reg_prop[1] = machina::kGicv3DistributorSize;
    // GICR memory map
    reg_prop[2] = machina::kGicv3RedistributorPhysBase;
    std::lock_guard<std::mutex> lock(mutex_);
    reg_prop[3] = machina::kGicv3RedistributorStride * redistributors_.size();
  }
  int ret = fdt_setprop_string(dtb, gic_off, "compatible", compatible);
  if (ret != 0) {
    gic_dtb_error("compatible");
    return ZX_ERR_BAD_STATE;
  }
  for (auto& prop : reg_prop) {
    prop = htobe64(prop);
  }
  ret = fdt_setprop(dtb, gic_off, "reg", reg_prop, sizeof(reg_prop));
  if (ret != 0) {
    gic_dtb_error("reg");
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

zx_status_t GicRedistributor::Read(uint64_t addr, IoValue* value) const {
  if (addr % 4 != 0) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  switch (static_cast<GicrRegister>(addr)) {
    case GicrRegister::CTL:
    case GicrRegister::WAKE:
    case GicrRegister::ICFG0:
    case GicrRegister::ICFG1:
      if (value->access_size != 4) {
        return ZX_ERR_IO_DATA_INTEGRITY;
      }
      value->u32 = 0;
      return ZX_OK;
    case GicrRegister::TYPE:
      if (value->access_size != 8) {
        return ZX_ERR_IO_DATA_INTEGRITY;
      }
      // Set both Processor_Number and Affinity_Value to id_.
      value->u64 = set_bits(static_cast<uint64_t>(id_), 23, 8) |
                   set_bits(static_cast<uint64_t>(id_), 39, 32);
      if (last_) {
        value->u64 |= 1u << 4;
      }
      return ZX_OK;
    case GicrRegister::PID2_V3:
      if (value->access_size != 4) {
        return ZX_ERR_IO_DATA_INTEGRITY;
      }
      value->u32 = pidr2_arch_rev(kGicv3Revision);
      return ZX_OK;
    default:
      FXL_LOG(ERROR) << "Unhandled GIC redistributor address read 0x"
                     << std::hex << addr;
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

zx_status_t GicRedistributor::Write(uint64_t addr, const IoValue& value) {
  if (addr % 4 != 0 || value.access_size != 4) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  switch (static_cast<GicrRegister>(addr)) {
    case GicrRegister::ICENABLE0:
    case GicrRegister::ISENABLE0:
    case GicrRegister::WAKE:
    case GicrRegister::IGROUP0:
    case GicrRegister::ICPEND0:
    case GicrRegister::ICACTIVE0:
    case GicrRegister::IPRIORITY0... GicrRegister::IPRIORITY255:
    case GicrRegister::ICFG0:
    case GicrRegister::ICFG1:
      return ZX_OK;
    default:
      FXL_LOG(ERROR) << "Unhandled GIC redistributor address write 0x"
                     << std::hex << addr;
      return ZX_ERR_NOT_SUPPORTED;
  }
}

}  // namespace machina
