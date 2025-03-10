// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "paver.h"

#include <dirent.h>
#include <fcntl.h>
#include <lib/fdio/directory.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/epitaph.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <libgen.h>
#include <stddef.h>
#include <string.h>
#include <zircon/device/block.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <memory>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/unique_fd.h>
#include <fs-management/fvm.h>
#include <fs-management/mount.h>
#include <zxcrypt/fdio-volume.h>

#include "fvm.h"
#include "pave-logging.h"
#include "stream-reader.h"
#include "vmo-reader.h"

#define ZXCRYPT_DRIVER_LIB "/boot/driver/zxcrypt.so"

namespace paver {
namespace {

using ::llcpp::fuchsia::paver::Asset;
using ::llcpp::fuchsia::paver::Configuration;

Partition PartitionType(Configuration configuration, Asset asset) {
  switch (asset) {
    case Asset::KERNEL: {
      switch (configuration) {
        case Configuration::A:
          return Partition::kZirconA;
        case Configuration::B:
          return Partition::kZirconB;
        case Configuration::RECOVERY:
          return Partition::kZirconR;
      };
      break;
    }
    case Asset::VERIFIED_BOOT_METADATA: {
      switch (configuration) {
        case Configuration::A:
          return Partition::kVbMetaA;
        case Configuration::B:
          return Partition::kVbMetaB;
        case Configuration::RECOVERY:
          return Partition::kVbMetaR;
      };
      break;
    }
  };
  return Partition::kUnknown;
}

// Best effort attempt to see if payload contents match what is already inside
// of the partition.
bool CheckIfSame(PartitionClient* partition, const zx::vmo& vmo, size_t payload_size,
                 size_t block_size) {
  const size_t payload_size_aligned = fbl::round_up(payload_size, block_size);
  zx::vmo read_vmo;
  auto status = zx::vmo::create(fbl::round_up(payload_size_aligned, ZX_PAGE_SIZE), 0, &read_vmo);
  if (status != ZX_OK) {
    ERROR("Failed to create VMO: %s\n", zx_status_get_string(status));
    return false;
  }

  if ((status = partition->Read(read_vmo, payload_size_aligned)) != ZX_OK) {
    return false;
  }

  fzl::VmoMapper first_mapper;
  fzl::VmoMapper second_mapper;

  status = first_mapper.Map(vmo, 0, 0, ZX_VM_PERM_READ);
  if (status != ZX_OK) {
    ERROR("Error mapping vmo: %s\n", zx_status_get_string(status));
    return false;
  }

  status = second_mapper.Map(read_vmo, 0, 0, ZX_VM_PERM_READ);
  if (status != ZX_OK) {
    ERROR("Error mapping vmo: %s\n", zx_status_get_string(status));
    return false;
  }

  return memcmp(first_mapper.start(), second_mapper.start(), payload_size) == 0;
}

#if 0
// Checks first few bytes of buffer to ensure it is a ZBI.
// Also validates architecture in kernel header matches the target.
bool ValidateKernelZbi(const uint8_t* buffer, size_t size, Arch arch) {
    const auto payload = reinterpret_cast<const zircon_kernel_t*>(buffer);
    const uint32_t expected_kernel =
        (arch == Arch::X64) ? ZBI_TYPE_KERNEL_X64 : ZBI_TYPE_KERNEL_ARM64;

    const auto crc_valid = [](const zbi_header_t* hdr) {
        const uint32_t crc = crc32(0, reinterpret_cast<const uint8_t*>(hdr + 1), hdr->length);
        return hdr->crc32 == crc;
    };

    return size >= sizeof(zircon_kernel_t) &&
           // Container header
           payload->hdr_file.type == ZBI_TYPE_CONTAINER &&
           payload->hdr_file.extra == ZBI_CONTAINER_MAGIC &&
           (payload->hdr_file.length - offsetof(zircon_kernel_t, hdr_kernel)) <= size &&
           payload->hdr_file.magic == ZBI_ITEM_MAGIC &&
           payload->hdr_file.flags == ZBI_FLAG_VERSION &&
           payload->hdr_file.crc32 == ZBI_ITEM_NO_CRC32 &&
           // Kernel header
           payload->hdr_kernel.type == expected_kernel &&
           (payload->hdr_kernel.length - offsetof(zircon_kernel_t, data_kernel)) <= size &&
           payload->hdr_kernel.magic == ZBI_ITEM_MAGIC &&
           (payload->hdr_kernel.flags & ZBI_FLAG_VERSION) == ZBI_FLAG_VERSION &&
           ((payload->hdr_kernel.flags & ZBI_FLAG_CRC32)
                ? crc_valid(&payload->hdr_kernel)
                : payload->hdr_kernel.crc32 == ZBI_ITEM_NO_CRC32);
}

// Parses a partition and validates that it matches the expected format.
zx_status_t ValidateKernelPayload(const fzl::ResizeableVmoMapper& mapper, size_t vmo_size,
                                  Partition partition_type, Arch arch) {
    // TODO(surajmalhotra): Re-enable this as soon as we have a good way to
    // determine whether the payload is signed or not. (Might require bootserver
    // changes).
    if (false) {
        const auto* buffer = reinterpret_cast<uint8_t*>(mapper.start());
        switch (partition_type) {
        case Partition::kZirconA:
        case Partition::kZirconB:
        case Partition::kZirconR:
            if (!ValidateKernelZbi(buffer, vmo_size, arch)) {
                ERROR("Invalid ZBI payload!");
                return ZX_ERR_BAD_STATE;
            }
            break;

        default:
            // TODO(surajmalhotra): Validate non-zbi payloads as well.
            LOG("Skipping validation as payload is not a ZBI\n");
            break;
        }
    }

    return ZX_OK;
}
#endif

// Returns a client for the FVM partition. If the FVM volume doesn't exist, a new
// volume will be created, without any associated children partitions.
zx_status_t GetFvmPartition(const DevicePartitioner& partitioner,
                            std::unique_ptr<PartitionClient>* client) {
  constexpr Partition partition = Partition::kFuchsiaVolumeManager;
  zx_status_t status = partitioner.FindPartition(partition, client);
  if (status != ZX_OK) {
    if (status != ZX_ERR_NOT_FOUND) {
      ERROR("Failure looking for FVM partition: %s\n", zx_status_get_string(status));
      return status;
    }

    LOG("Could not find FVM Partition on device. Attemping to add new partition\n");

    if ((status = partitioner.AddPartition(partition, client)) != ZX_OK) {
      ERROR("Failure creating FVM partition: %s\n", zx_status_get_string(status));
      return status;
    }
  } else {
    LOG("FVM Partition already exists\n");
  }
  return ZX_OK;
}

zx_status_t FvmPave(const fbl::unique_fd& devfs_root, const DevicePartitioner& partitioner,
                    std::unique_ptr<fvm::ReaderInterface> payload) {
  LOG("Paving FVM partition.\n");
  std::unique_ptr<PartitionClient> partition;
  zx_status_t status = GetFvmPartition(partitioner, &partition);
  if (status != ZX_OK) {
    return status;
  }

  if (partitioner.IsFvmWithinFtl()) {
    LOG("Attempting to format FTL...\n");
    status = partitioner.WipeFvm();
    if (status != ZX_OK) {
      ERROR("Failed to format FTL: %s\n", zx_status_get_string(status));
    } else {
      LOG("Formatted partition successfully!\n");
    }
  }
  LOG("Streaming partitions to FVM...\n");
  status = FvmStreamPartitions(devfs_root, std::move(partition), std::move(payload));
  if (status != ZX_OK) {
    ERROR("Failed to stream partitions to FVM: %s\n", zx_status_get_string(status));
    return status;
  }
  LOG("Completed FVM paving successfully\n");
  return ZX_OK;
}

// Formats the FVM partition and returns a channel to the new volume.
zx_status_t FormatFvm(const fbl::unique_fd& devfs_root, const DevicePartitioner& partitioner,
                      zx::channel* channel) {
  std::unique_ptr<PartitionClient> partition;
  zx_status_t status = GetFvmPartition(partitioner, &partition);
  if (status != ZX_OK) {
    return status;
  }

  // TODO(39753): Configuration values should come from the build or environment.
  fvm::sparse_image_t header = {};
  header.slice_size = 1 << 20;

  fbl::unique_fd fvm_fd(
      FvmPartitionFormat(devfs_root, partition->block_fd(), header, BindOption::Reformat));
  if (!fvm_fd) {
    ERROR("Couldn't format FVM partition\n");
    return ZX_ERR_IO;
  }

  status = AllocateEmptyPartitions(devfs_root, fvm_fd);
  if (status != ZX_OK) {
    ERROR("Couldn't allocate empty partitions\n");
    return status;
  }

  status = fdio_get_service_handle(fvm_fd.release(), channel->reset_and_get_address());
  if (status != ZX_OK) {
    ERROR("Couldn't get fvm handle\n");
    return ZX_ERR_IO;
  }

  return ZX_OK;
}

// Reads an image from disk into a vmo.
zx_status_t PartitionRead(const DevicePartitioner& partitioner, Partition partition_type,
                          zx::vmo* out_vmo, size_t* out_vmo_size) {
  LOG("Reading partition \"%s\".\n", PartitionName(partition_type));

  std::unique_ptr<PartitionClient> partition;
  if (zx_status_t status = partitioner.FindPartition(partition_type, &partition); status != ZX_OK) {
    ERROR("Could not find \"%s\" Partition on device: %s\n", PartitionName(partition_type),
          zx_status_get_string(status));
    return status;
  }

  uint64_t partition_size;
  if (zx_status_t status = partition->GetPartitionSize(&partition_size); status != ZX_OK) {
    ERROR("Error getting partition \"%s\" size: %s\n", PartitionName(partition_type),
          zx_status_get_string(status));
    return status;
  }

  zx::vmo vmo;
  if (zx_status_t status = zx::vmo::create(fbl::round_up(partition_size, ZX_PAGE_SIZE), 0, &vmo);
      status != ZX_OK) {
    ERROR("Error creating vmo for \"%s\": %s\n", PartitionName(partition_type),
          zx_status_get_string(status));
    return status;
  }

  if (zx_status_t status = partition->Read(vmo, static_cast<size_t>(partition_size));
      status != ZX_OK) {
    ERROR("Error writing partition data for \"%s\": %s\n", PartitionName(partition_type),
          zx_status_get_string(status));
    return status;
  }

  *out_vmo = std::move(vmo);
  *out_vmo_size = static_cast<size_t>(partition_size);
  LOG("Completed successfully\n");
  return ZX_OK;
}

// Paves an image onto the disk.
zx_status_t PartitionPave(const DevicePartitioner& partitioner, zx::vmo payload_vmo,
                          size_t payload_size, Partition partition_type) {
  LOG("Paving partition \"%s\".\n", PartitionName(partition_type));

  zx_status_t status;
  std::unique_ptr<PartitionClient> partition;
  if ((status = partitioner.FindPartition(partition_type, &partition)) != ZX_OK) {
    if (status != ZX_ERR_NOT_FOUND) {
      ERROR("Failure looking for partition \"%s\": %s\n", PartitionName(partition_type),
            zx_status_get_string(status));
      return status;
    }

    LOG("Could not find \"%s\" Partition on device. Attemping to add new partition\n",
        PartitionName(partition_type));

    if ((status = partitioner.AddPartition(partition_type, &partition)) != ZX_OK) {
      ERROR("Failure creating partition \"%s\": %s\n", PartitionName(partition_type),
            zx_status_get_string(status));
      return status;
    }
  } else {
    LOG("Partition \"%s\" already exists\n", PartitionName(partition_type));
  }

  size_t block_size_bytes;
  if ((status = partition->GetBlockSize(&block_size_bytes)) != ZX_OK) {
    ERROR("Couldn't get partition \"%s\" block size\n", PartitionName(partition_type));
    return status;
  }

  if (CheckIfSame(partition.get(), payload_vmo, payload_size, block_size_bytes)) {
    LOG("Skipping write as partition \"%s\" contents match payload.\n",
        PartitionName(partition_type));
  } else {
    // Pad payload with 0s to make it block size aligned.
    if (payload_size % block_size_bytes != 0) {
      const size_t remaining_bytes = block_size_bytes - (payload_size % block_size_bytes);
      size_t vmo_size;
      if ((status = payload_vmo.get_size(&vmo_size)) != ZX_OK) {
        ERROR("Couldn't get vmo size for \"%s\"\n", PartitionName(partition_type));
        return status;
      }
      // Grow VMO if it's too small.
      if (vmo_size < payload_size + remaining_bytes) {
        const auto new_size = fbl::round_up(payload_size + remaining_bytes, ZX_PAGE_SIZE);
        status = payload_vmo.set_size(new_size);
        if (status != ZX_OK) {
          ERROR("Couldn't grow vmo for \"%s\"\n", PartitionName(partition_type));
          return status;
        }
      }
      auto buffer = std::make_unique<uint8_t[]>(remaining_bytes);
      memset(buffer.get(), 0, remaining_bytes);
      status = payload_vmo.write(buffer.get(), payload_size, remaining_bytes);
      if (status != ZX_OK) {
        ERROR("Failed to write padding to vmo for \"%s\"\n", PartitionName(partition_type));
        return status;
      }
      payload_size += remaining_bytes;
    }
    if ((status = partition->Write(payload_vmo, payload_size)) != ZX_OK) {
      ERROR("Error writing partition \"%s\" data: %s\n", PartitionName(partition_type),
            zx_status_get_string(status));
      return status;
    }
  }

  if ((status = partitioner.FinalizePartition(partition_type)) != ZX_OK) {
    ERROR("Failed to finalize partition \"%s\"\n", PartitionName(partition_type));
    return status;
  }

  LOG("Completed paving partition \"%s\" successfully\n", PartitionName(partition_type));
  return ZX_OK;
}

zx::channel OpenServiceRoot() {
  zx::channel request, service_root;
  if (zx::channel::create(0, &request, &service_root) != ZX_OK) {
    return zx::channel();
  }
  if (fdio_service_connect("/svc/.", request.release()) != ZX_OK) {
    return zx::channel();
  }
  return service_root;
}

bool IsBootable(const abr::SlotData& slot) {
  return slot.priority > 0 && (slot.tries_remaining > 0 || slot.successful_boot);
}

std::optional<Configuration> GetActiveConfiguration(const abr::Client& abr_client) {
  const bool config_a_bootable = IsBootable(abr_client.Data().slots[0]);
  const bool config_b_bootable = IsBootable(abr_client.Data().slots[1]);
  const uint8_t config_a_priority = abr_client.Data().slots[0].priority;
  const uint8_t config_b_priority = abr_client.Data().slots[1].priority;

  // A wins on ties.
  if (config_a_bootable && (config_a_priority >= config_b_priority || !config_b_bootable)) {
    return Configuration::A;
  } else if (config_b_bootable) {
    return Configuration::B;
  } else {
    return std::nullopt;
  }
}

}  // namespace

void Paver::FindDataSink(zx::channel data_sink, FindDataSinkCompleter::Sync _completer) {
  // Use global devfs if one wasn't injected via set_devfs_root.
  if (!devfs_root_) {
    devfs_root_ = fbl::unique_fd(open("/dev", O_RDONLY));
  }
  if (!svc_root_) {
    svc_root_ = OpenServiceRoot();
  }

  DataSink::Bind(dispatcher_, devfs_root_.duplicate(), std::move(svc_root_), std::move(data_sink));
}

void Paver::UseBlockDevice(zx::channel block_device, zx::channel dynamic_data_sink,
                           UseBlockDeviceCompleter::Sync _completer) {
  // Use global devfs if one wasn't injected via set_devfs_root.
  if (!devfs_root_) {
    devfs_root_ = fbl::unique_fd(open("/dev", O_RDONLY));
  }
  if (!svc_root_) {
    svc_root_ = OpenServiceRoot();
  }

  DynamicDataSink::Bind(dispatcher_, devfs_root_.duplicate(), std::move(svc_root_),
                        std::move(block_device), std::move(dynamic_data_sink));
}

void Paver::FindBootManager(zx::channel boot_manager, bool initialize,
                            FindBootManagerCompleter::Sync _completer) {
  // Use global devfs if one wasn't injected via set_devfs_root.
  if (!devfs_root_) {
    devfs_root_ = fbl::unique_fd(open("/dev", O_RDONLY));
  }
  if (!svc_root_) {
    svc_root_ = OpenServiceRoot();
  }

  BootManager::Bind(dispatcher_, devfs_root_.duplicate(), std::move(svc_root_),
                    std::move(boot_manager), initialize);
}

void DataSink::ReadAsset(::llcpp::fuchsia::paver::Configuration configuration,
                         ::llcpp::fuchsia::paver::Asset asset,
                         ReadAssetCompleter::Sync completer) {
  ::llcpp::fuchsia::mem::Buffer buf;
  zx_status_t status = sink_.ReadAsset(configuration, asset, &buf);
  if (status == ZX_OK) {
    completer.ReplySuccess(std::move(buf));
  } else {
    completer.ReplyError(status);
  }
}

void DataSink::WipeVolume(WipeVolumeCompleter::Sync completer) {
  zx::channel out;
  zx_status_t status = sink_.WipeVolume(&out);
  if (status == ZX_OK) {
    completer.ReplySuccess(std::move(out));
  } else {
    completer.ReplyError(status);
  }
}

zx_status_t DataSinkImpl::ReadAsset(Configuration configuration, Asset asset,
                                    ::llcpp::fuchsia::mem::Buffer* buf) {
  return PartitionRead(*partitioner_, PartitionType(configuration, asset), &buf->vmo, &buf->size);
}

zx_status_t DataSinkImpl::WriteAsset(Configuration configuration, Asset asset,
                                     ::llcpp::fuchsia::mem::Buffer payload) {
  return PartitionPave(*partitioner_, std::move(payload.vmo), payload.size,
                       PartitionType(configuration, asset));
}

zx_status_t DataSinkImpl::WriteVolumes(zx::channel payload_stream) {
  std::unique_ptr<StreamReader> reader;
  zx_status_t status = StreamReader::Create(std::move(payload_stream), &reader);
  if (status != ZX_OK) {
    ERROR("Unable to create stream.\n");
    return status;
  }
  return FvmPave(devfs_root_, *partitioner_, std::move(reader));
}

zx_status_t DataSinkImpl::WriteBootloader(::llcpp::fuchsia::mem::Buffer payload) {
  return PartitionPave(*partitioner_, std::move(payload.vmo), payload.size, Partition::kBootloader);
}

zx_status_t DataSinkImpl::WriteDataFile(fidl::StringView filename,
                                        ::llcpp::fuchsia::mem::Buffer payload) {
  const char* mount_path = "/volume/data";
  const uint8_t data_guid[] = GUID_DATA_VALUE;
  char minfs_path[PATH_MAX] = {0};
  char path[PATH_MAX] = {0};
  zx_status_t status = ZX_OK;

  fbl::unique_fd part_fd(
      open_partition_with_devfs(devfs_root_.get(), nullptr, data_guid, ZX_SEC(1), path));
  if (!part_fd) {
    ERROR("DATA partition not found in FVM\n");
    return ZX_ERR_NOT_FOUND;
  }

  auto disk_format = detect_disk_format(part_fd.get());
  fbl::unique_fd mountpoint_dev_fd;
  // By the end of this switch statement, mountpoint_dev_fd needs to be an
  // open handle to the block device that we want to mount at mount_path.
  switch (disk_format) {
    case DISK_FORMAT_MINFS:
      // If the disk we found is actually minfs, we can just use the block
      // device path we were given by open_partition.
      strncpy(minfs_path, path, PATH_MAX);
      mountpoint_dev_fd.reset(open(minfs_path, O_RDWR));
      break;

    case DISK_FORMAT_ZXCRYPT: {
      std::unique_ptr<zxcrypt::FdioVolume> zxc_volume;
      uint8_t slot = 0;
      if ((status = zxcrypt::FdioVolume::UnlockWithDeviceKey(
               std::move(part_fd), devfs_root_.duplicate(), static_cast<zxcrypt::key_slot_t>(slot),
               &zxc_volume)) != ZX_OK) {
        ERROR("Couldn't unlock zxcrypt volume: %s\n", zx_status_get_string(status));
        return status;
      }

      // Most of the time we'll expect the volume to actually already be
      // unsealed, because we created it and unsealed it moments ago to
      // format minfs.
      if ((status = zxc_volume->Open(zx::sec(0), &mountpoint_dev_fd)) == ZX_OK) {
        // Already unsealed, great, early exit.
        break;
      }

      // Ensure zxcrypt volume manager is bound.
      zx::channel zxc_manager_chan;
      if ((status = zxc_volume->OpenManager(zx::sec(5),
                                            zxc_manager_chan.reset_and_get_address())) != ZX_OK) {
        ERROR("Couldn't open zxcrypt volume manager: %s\n", zx_status_get_string(status));
        return status;
      }

      // Unseal.
      zxcrypt::FdioVolumeManager zxc_manager(std::move(zxc_manager_chan));
      if ((status = zxc_manager.UnsealWithDeviceKey(slot)) != ZX_OK) {
        ERROR("Couldn't unseal zxcrypt volume: %s\n", zx_status_get_string(status));
        return status;
      }

      // Wait for the device to appear, and open it.
      if ((status = zxc_volume->Open(zx::sec(5), &mountpoint_dev_fd)) != ZX_OK) {
        ERROR("Couldn't open block device atop unsealed zxcrypt volume: %s\n",
              zx_status_get_string(status));
        return status;
      }
    } break;

    default:
      ERROR("unsupported disk format at %s\n", path);
      return ZX_ERR_NOT_SUPPORTED;
  }

  mount_options_t opts(default_mount_options);
  opts.create_mountpoint = true;
  if ((status = mount(mountpoint_dev_fd.get(), mount_path, DISK_FORMAT_MINFS, &opts,
                      launch_logs_async)) != ZX_OK) {
    ERROR("mount error: %s\n", zx_status_get_string(status));
    return status;
  }

  int filename_size = static_cast<int>(filename.size());

  // mkdir any intermediate directories between mount_path and basename(filename).
  snprintf(path, sizeof(path), "%s/%.*s", mount_path, filename_size, filename.data());
  size_t cur = strlen(mount_path);
  size_t max = strlen(path) - strlen(basename(path));
  // note: the call to basename above modifies path, so it needs reconstruction.
  snprintf(path, sizeof(path), "%s/%.*s", mount_path, filename_size, filename.data());
  while (cur < max) {
    ++cur;
    if (path[cur] == '/') {
      path[cur] = 0;
      // errors ignored, let the open() handle that later.
      mkdir(path, 0700);
      path[cur] = '/';
    }
  }

  // We append here, because the primary use case here is to send SSH keys
  // which can be appended, but we may want to revisit this choice for other
  // files in the future.
  {
    uint8_t buf[8192];
    fbl::unique_fd kfd(open(path, O_CREAT | O_WRONLY | O_APPEND, 0600));
    if (!kfd) {
      umount(mount_path);
      ERROR("open %.*s error: %s\n", filename_size, filename.data(), strerror(errno));
      return ZX_ERR_IO;
    }
    VmoReader reader(std::move(payload));
    size_t actual;
    while ((status = reader.Read(buf, sizeof(buf), &actual)) == ZX_OK && actual > 0) {
      if (write(kfd.get(), buf, actual) != static_cast<ssize_t>(actual)) {
        umount(mount_path);
        ERROR("write %.*s error: %s\n", filename_size, filename.data(), strerror(errno));
        return ZX_ERR_IO;
      }
    }
    fsync(kfd.get());
  }

  if ((status = umount(mount_path)) != ZX_OK) {
    ERROR("unmount %s failed: %s\n", mount_path, zx_status_get_string(status));
    return status;
  }

  LOG("Wrote %.*s\n", filename_size, filename.data());
  return ZX_OK;
}

zx_status_t DataSinkImpl::WipeVolume(zx::channel* out) {
  std::unique_ptr<PartitionClient> partition;
  zx_status_t status = GetFvmPartition(*partitioner_, &partition);
  if (status != ZX_OK) {
    return status;
  }

  // Bind the FVM driver to be in a well known state regarding races with block watcher.
  // The block watcher will attempt to bind the FVM driver automatically based on
  // the contents of the partition. However, that operation is not synchronized in
  // any way with this service so the driver can be loaded at any time.
  // WipeFvm basically writes underneath that driver, which means that we should
  // eliminate the races at this point: assuming that the driver can load, either
  // this call or the block watcher will succeed (and the other one will fail),
  // but the driver will be loaded before moving on.
  TryBindToFvmDriver(devfs_root_, partition->block_fd(), zx::sec(3));

  status = partitioner_->WipeFvm();
  if (status != ZX_OK) {
    ERROR("Failure wiping partition: %s\n", zx_status_get_string(status));
    return status;
  }

  zx::channel channel;
  status = FormatFvm(devfs_root_, *partitioner_, &channel);
  if (status != ZX_OK) {
    ERROR("Failure formatting partition: %s\n", zx_status_get_string(status));
    return status;
  }

  *out = std::move(channel);
  return ZX_OK;
}

void DataSink::Bind(async_dispatcher_t* dispatcher, fbl::unique_fd devfs_root, zx::channel svc_root,
                    zx::channel server) {
#if defined(__x86_64__)
  Arch arch = Arch::kX64;
#elif defined(__aarch64__)
  Arch arch = Arch::kArm64;
#else
#error "Unknown arch"
#endif
  auto partitioner = DevicePartitioner::Create(devfs_root.duplicate(), std::move(svc_root), arch);
  if (!partitioner) {
    ERROR("Unable to initialize a partitioner.\n");
    fidl_epitaph_write(server.get(), ZX_ERR_BAD_STATE);
    return;
  }
  auto data_sink = std::make_unique<DataSink>(std::move(devfs_root), std::move(partitioner));
  fidl::Bind(dispatcher, std::move(server), std::move(data_sink));
}

void DynamicDataSink::Bind(async_dispatcher_t* dispatcher, fbl::unique_fd devfs_root,
                           zx::channel svc_root, zx::channel block_device, zx::channel server) {
#if defined(__x86_64__)
  Arch arch = Arch::kX64;
#elif defined(__aarch64__)
  Arch arch = Arch::kArm64;
#else
#error "Unknown arch"
#endif
  auto partitioner = DevicePartitioner::Create(devfs_root.duplicate(), std::move(svc_root), arch,
                                               std::move(block_device));
  if (!partitioner) {
    ERROR("Unable to initialize a partitioner.\n");
    fidl_epitaph_write(server.get(), ZX_ERR_BAD_STATE);
    return;
  }
  auto data_sink = std::make_unique<DynamicDataSink>(std::move(devfs_root), std::move(partitioner));
  fidl::Bind(dispatcher, std::move(server), std::move(data_sink));
}

void DynamicDataSink::InitializePartitionTables(
    InitializePartitionTablesCompleter::Sync completer) {
  completer.Reply(sink_.partitioner()->InitPartitionTables());
}

void DynamicDataSink::WipePartitionTables(WipePartitionTablesCompleter::Sync completer) {
  completer.Reply(sink_.partitioner()->WipePartitionTables());
}

void DynamicDataSink::ReadAsset(::llcpp::fuchsia::paver::Configuration configuration,
                                ::llcpp::fuchsia::paver::Asset asset,
                                ReadAssetCompleter::Sync completer) {
  ::llcpp::fuchsia::mem::Buffer buf;
  auto status = sink_.ReadAsset(configuration, asset, &buf);
  if (status == ZX_OK) {
    completer.ReplySuccess(std::move(buf));
  } else {
    completer.ReplyError(status);
  }
}

void DynamicDataSink::WipeVolume(WipeVolumeCompleter::Sync completer) {
  zx::channel out;
  zx_status_t status = sink_.WipeVolume(&out);
  if (status == ZX_OK) {
    completer.ReplySuccess(std::move(out));
  } else {
    completer.ReplyError(status);
  }
}

void BootManager::Bind(async_dispatcher_t* dispatcher, fbl::unique_fd devfs_root,
                       zx::channel svc_root, zx::channel server, bool initialize) {
  std::unique_ptr<abr::Client> abr_client;
  if (zx_status_t status =
          abr::Client::Create(std::move(devfs_root), std::move(svc_root), &abr_client);
      status != ZX_OK) {
    ERROR("Failed to get ABR client: %s\n", zx_status_get_string(status));
    fidl_epitaph_write(server.get(), status);
    return;
  }

  const bool valid = abr_client->IsValid();

  if (!valid && initialize) {
    abr::Data data = abr_client->Data();
    memset(&data, 0, sizeof(data));
    memcpy(data.magic, abr::kMagic, sizeof(abr::kMagic));
    data.version_major = abr::kMajorVersion;
    data.version_minor = abr::kMinorVersion;

    if (zx_status_t status = abr_client->Persist(data); status != ZX_OK) {
      ERROR("Unabled to persist ABR metadata %s\n", zx_status_get_string(status));
      fidl_epitaph_write(server.get(), status);
      return;
    }
    ZX_DEBUG_ASSERT(abr_client->IsValid());
  } else if (!valid) {
    ERROR("ABR metadata is not valid!\n");
    fidl_epitaph_write(server.get(), ZX_ERR_NOT_SUPPORTED);
    return;
  }

  auto boot_manager = std::make_unique<BootManager>(std::move(abr_client));
  fidl::Bind(dispatcher, std::move(server), std::move(boot_manager));
}

void BootManager::QueryActiveConfiguration(QueryActiveConfigurationCompleter::Sync completer) {
  std::optional<Configuration> config = GetActiveConfiguration(*abr_client_);
  if (!config) {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  completer.ReplySuccess(config.value());
}

void BootManager::QueryConfigurationStatus(Configuration configuration,
                                           QueryConfigurationStatusCompleter::Sync completer) {
  const abr::SlotData* slot;
  switch (configuration) {
    case Configuration::A:
      slot = &abr_client_->Data().slots[0];
      break;
    case Configuration::B:
      slot = &abr_client_->Data().slots[1];
      break;
    default:
      ERROR("Unexpected configuration: %d\n", static_cast<uint32_t>(configuration));
      completer.ReplyError(ZX_ERR_INVALID_ARGS);
      return;
  }

  if (!IsBootable(*slot)) {
    completer.ReplySuccess(::llcpp::fuchsia::paver::ConfigurationStatus::UNBOOTABLE);
  } else if (slot->successful_boot == 0) {
    completer.ReplySuccess(::llcpp::fuchsia::paver::ConfigurationStatus::PENDING);
  } else {
    completer.ReplySuccess(::llcpp::fuchsia::paver::ConfigurationStatus::HEALTHY);
  }
}

void BootManager::SetConfigurationActive(Configuration configuration,
                                         SetConfigurationActiveCompleter::Sync completer) {
  abr::Data data = abr_client_->Data();

  abr::SlotData *primary, *secondary;
  switch (configuration) {
    case Configuration::A:
      primary = &data.slots[0];
      secondary = &data.slots[1];
      break;
    case Configuration::B:
      primary = &data.slots[1];
      secondary = &data.slots[0];
      break;
    default:
      ERROR("Unexpected configuration: %d\n", static_cast<uint32_t>(configuration));
      completer.Reply(ZX_ERR_INVALID_ARGS);
      return;
  }
  if (secondary->priority >= abr::kMaxPriority) {
    // 0 means unbootable, so we reset down to 1 to indicate lowest priority.
    secondary->priority = 1;
  }
  primary->successful_boot = 0;
  primary->tries_remaining = abr::kMaxTriesRemaining;
  primary->priority = static_cast<uint8_t>(secondary->priority + 1);

  if (zx_status_t status = abr_client_->Persist(data); status != ZX_OK) {
    ERROR("Unabled to persist ABR metadata %s\n", zx_status_get_string(status));
    completer.Reply(status);
    return;
  }

  completer.Reply(ZX_OK);
}

void BootManager::SetConfigurationUnbootable(Configuration configuration,
                                             SetConfigurationUnbootableCompleter::Sync completer) {
  auto data = abr_client_->Data();

  abr::SlotData* slot;
  switch (configuration) {
    case Configuration::A:
      slot = &data.slots[0];
      break;
    case Configuration::B:
      slot = &data.slots[1];
      break;
    default:
      ERROR("Unexpected configuration: %d\n", static_cast<uint32_t>(configuration));
      completer.Reply(ZX_ERR_INVALID_ARGS);
      return;
  }
  slot->successful_boot = 0;
  slot->tries_remaining = 0;
  slot->priority = 0;

  if (zx_status_t status = abr_client_->Persist(data); status != ZX_OK) {
    ERROR("Unabled to persist ABR metadata %s\n", zx_status_get_string(status));
    completer.Reply(status);
    return;
  }

  completer.Reply(ZX_OK);
}

void BootManager::SetActiveConfigurationHealthy(
    SetActiveConfigurationHealthyCompleter::Sync completer) {
  abr::Data data = abr_client_->Data();

  std::optional<Configuration> config = GetActiveConfiguration(*abr_client_);
  if (!config) {
    ERROR("No configuration bootable. Cannot mark as successful boot.\n");
    completer.Reply(ZX_ERR_BAD_STATE);
    return;
  }

  abr::SlotData* slot;
  switch (*config) {
    case Configuration::A:
      slot = &data.slots[0];
      break;
    case Configuration::B:
      slot = &data.slots[1];
      break;
    default:
      // We've previously validated active is A or B.
      ZX_ASSERT(false);
  }
  slot->tries_remaining = 0;
  slot->successful_boot = 1;

  if (zx_status_t status = abr_client_->Persist(data); status != ZX_OK) {
    ERROR("Unabled to persist ABR metadata %s\n", zx_status_get_string(status));
    completer.Reply(status);
    return;
  }

  completer.Reply(ZX_OK);
}

}  //  namespace paver
