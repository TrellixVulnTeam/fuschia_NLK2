// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_bus.h"

#include <zircon/errors.h>

#include <algorithm>
#include <string>

#include <ddk/metadata.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/core.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/device.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/firmware.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_buscore.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_firmware.h"

namespace wlan {
namespace brcmfmac {

PcieBus::PcieBus() = default;

PcieBus::~PcieBus() {
  if (device_ != nullptr) {
    if (device_->drvr()->settings == brcmf_mp_device_.get()) {
      device_->drvr()->settings = nullptr;
    }
    if (device_->drvr()->bus_if == brcmf_bus_.get()) {
      device_->drvr()->bus_if = nullptr;
    }
    device_ = nullptr;
  }
}

// static
zx_status_t PcieBus::Create(Device* device, std::unique_ptr<PcieBus>* bus_out) {
  zx_status_t status = ZX_OK;
  auto pcie_bus = std::make_unique<PcieBus>();

  std::unique_ptr<PcieBuscore> pcie_buscore;
  if ((status = PcieBuscore::Create(device->parent(), &pcie_buscore)) != ZX_OK) {
    return status;
  }

  std::unique_ptr<PcieFirmware> pcie_firmware;
  if ((status = PcieFirmware::Create(device, pcie_buscore.get(), &pcie_firmware)) != ZX_OK) {
    return status;
  }

  auto bus = std::make_unique<brcmf_bus>();
  bus->bus_priv.pcie = pcie_bus.get();
  bus->ops = PcieBus::GetBusOps();

  auto mp_device = std::make_unique<brcmf_mp_device>();
  brcmf_get_module_param(brcmf_bus_type::BRCMF_BUS_TYPE_PCIE, pcie_buscore->chip()->chip,
                         pcie_buscore->chip()->chiprev, mp_device.get());

  device->drvr()->bus_if = bus.get();
  device->drvr()->settings = mp_device.get();

  pcie_bus->device_ = device;
  pcie_bus->pcie_buscore_ = std::move(pcie_buscore);
  pcie_bus->pcie_firmware_ = std::move(pcie_firmware);
  pcie_bus->brcmf_bus_ = std::move(bus);
  pcie_bus->brcmf_mp_device_ = std::move(mp_device);

  *bus_out = std::move(pcie_bus);
  return ZX_OK;
}

// static
const brcmf_bus_ops* PcieBus::GetBusOps() {
  static constexpr brcmf_bus_ops bus_ops = {
      .get_bus_type = []() { return PcieBus::GetBusType(); },
      .stop = [](brcmf_bus* bus) { return bus->bus_priv.pcie->Stop(); },
      .txdata = [](brcmf_bus* bus,
                   brcmf_netbuf* netbuf) { return bus->bus_priv.pcie->TxData(netbuf); },
      .txctl = [](brcmf_bus* bus, unsigned char* msg,
                  uint len) { return bus->bus_priv.pcie->TxCtl(msg, len); },
      .rxctl = [](brcmf_bus* bus, unsigned char* msg, uint len,
                  int* rxlen_out) { return bus->bus_priv.pcie->RxCtl(msg, len, rxlen_out); },
      .wowl_config = [](brcmf_bus* bus,
                        bool enabled) { return bus->bus_priv.pcie->WowlConfig(enabled); },
      .get_ramsize = [](brcmf_bus* bus) { return bus->bus_priv.pcie->GetRamsize(); },
      .get_memdump = [](brcmf_bus* bus, void* data,
                        size_t len) { return bus->bus_priv.pcie->GetMemdump(data, len); },
      .get_fwname =
          [](brcmf_bus* bus, uint chip, uint chiprev, unsigned char* fw_name,
             size_t* fw_name_size) {
            return bus->bus_priv.pcie->GetFwname(chip, chiprev, fw_name, fw_name_size);
          },
      .get_bootloader_macaddr =
          [](brcmf_bus* bus, uint8_t* mac_addr) {
            return bus->bus_priv.pcie->GetBootloaderMacaddr(mac_addr);
          },
      .get_wifi_metadata =
          [](brcmf_bus* bus, void* config, size_t exp_size, size_t* actual) {
            return bus->bus_priv.pcie->GetWifiMetadata(config, exp_size, actual);
          },
  };
  return &bus_ops;
}

// static
brcmf_bus_type PcieBus::GetBusType() { return BRCMF_BUS_TYPE_PCIE; }

void PcieBus::Stop() {}

zx_status_t PcieBus::TxData(brcmf_netbuf* netbuf) { return ZX_OK; }

zx_status_t PcieBus::TxCtl(unsigned char* msg, uint len) { return ZX_OK; }

zx_status_t PcieBus::RxCtl(unsigned char* msg, uint len, int* rxlen_out) {
  if (rxlen_out != nullptr) {
    *rxlen_out = 0;
  }
  return ZX_OK;
}

void PcieBus::WowlConfig(bool enabled) { BRCMF_ERR("PcieBus::WowlConfig unimplemented\n"); }

size_t PcieBus::GetRamsize() {
  return pcie_buscore_->chip()->ramsize - pcie_buscore_->chip()->srsize;
}

zx_status_t PcieBus::GetMemdump(void* data, size_t len) {
  pcie_buscore_->RamRead(0, data, len);
  return ZX_OK;
}

zx_status_t PcieBus::GetFwname(uint chip, uint chiprev, unsigned char* fw_name,
                               size_t* fw_name_size) {
  zx_status_t status = ZX_OK;

  if (fw_name == nullptr || fw_name_size == nullptr || *fw_name_size == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  std::string_view firmware_name;
  if ((status = GetFirmwareName(brcmf_bus_type::BRCMF_BUS_TYPE_PCIE, pcie_buscore_->chip()->chip,
                                pcie_buscore_->chip()->chiprev, &firmware_name)) != ZX_OK) {
    return status;
  }

  // Almost, but not quite, entirely unlike strlcpy().
  const size_t copy_size = std::min<size_t>(*fw_name_size - 1, firmware_name.size());
  std::copy(firmware_name.begin(), firmware_name.begin() + copy_size, fw_name);
  fw_name[copy_size] = '\0';
  return ZX_OK;
}

zx_status_t PcieBus::GetBootloaderMacaddr(uint8_t* mac_addr) {
  BRCMF_ERR("PcieBus::GetBootloaderMacaddr unimplemented\n");
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t PcieBus::GetWifiMetadata(void* config, size_t exp_size, size_t* actual) {
  return device_->DeviceGetMetadata(DEVICE_METADATA_WIFI_CONFIG, config, exp_size, actual);
}

}  // namespace brcmfmac
}  // namespace wlan
