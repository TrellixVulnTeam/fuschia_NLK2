// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_CORE_BOOTSVC_SVCFS_SERVICE_H_
#define ZIRCON_SYSTEM_CORE_BOOTSVC_SVCFS_SERVICE_H_

#include <lib/zx/channel.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/resource.h>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>
#include <fuchsia/kernel/llcpp/fidl.h>
#include <fs/pseudo_dir.h>
#include <fs/service.h>
#include <fs/synchronous_vfs.h>

#include "util.h"

namespace bootsvc {

// A VFS used to provide services to the next process in the boot sequence.
class SvcfsService : public fbl::RefCounted<SvcfsService> {
 public:
  // Create a SvcfsService using the given |dispatcher|.
  static fbl::RefPtr<SvcfsService> Create(async_dispatcher_t* dispatcher);

  // Add a |service| named |service_name| to the VFS.
  void AddService(const char* service_name, fbl::RefPtr<fs::Service> service);

  // Create a connection to the root of the VFS.
  zx_status_t CreateRootConnection(zx::channel* out);

 private:
  explicit SvcfsService(async_dispatcher_t* dispatcher);

  SvcfsService(const SvcfsService&) = delete;
  SvcfsService(SvcfsService&&) = delete;
  SvcfsService& operator=(const SvcfsService&) = delete;
  SvcfsService& operator=(SvcfsService&&) = delete;

  fs::SynchronousVfs vfs_;
  // Root node for |vfs_|.
  fbl::RefPtr<fs::PseudoDir> root_;
};

// Create a service to retrieve boot arguments.
fbl::RefPtr<fs::Service> CreateArgumentsService(async_dispatcher_t* dispatcher, zx::vmo vmo,
                                                uint64_t size);

// Create a service to retrive factory ZBI items.
fbl::RefPtr<fs::Service> CreateFactoryItemsService(async_dispatcher_t* dispatcher,
                                                   FactoryItemMap map);

// Create a service to retrieve ZBI items.
fbl::RefPtr<fs::Service> CreateItemsService(async_dispatcher_t* dispatcher, zx::vmo vmo,
                                            ItemMap map);

// Create a service to provide a read-only version of the kernel log.
// Note that the root resource is passed in here, rather than a read-only log handle to be
// duplicated, because a fresh debuglog (LogDispatcher) object needs to be returned for each call.
// This is because LogDispatcher holds the implicit read location for reading from the log, so if a
// handle to the same object was duplicated, this would mistakenly share read location amongst all
// retrievals.
fbl::RefPtr<fs::Service> CreateReadOnlyLogService(async_dispatcher_t* dispatcher,
                                                  const zx::resource& root_resource);

// Create a service to provide a write-only version of the kernel log.
fbl::RefPtr<fs::Service> CreateWriteOnlyLogService(async_dispatcher_t* dispatcher,
                                                   const zx::debuglog& log);

// Create a service to provide the root job.
fbl::RefPtr<fs::Service> CreateRootJobService(async_dispatcher_t* dispatcher);

// Create a service to provide the root job with restricted rights.
fbl::RefPtr<fs::Service> CreateRootJobForInspectService(async_dispatcher_t* dispatcher);

// Create a service to provide the root resource.
fbl::RefPtr<fs::Service> CreateRootResourceService(async_dispatcher_t* dispatcher,
                                                   zx::resource root_resource);

// A service that implements a fidl protocol to vend kernel statistics.
class KernelStatsImpl : public llcpp::fuchsia::kernel::Stats::Interface {
 public:
  // The service requires the root resource as that is necessary today to call
  // the appropriate zx_object_get_info syscalls. It does not require any rights
  // on that handle though.
  explicit KernelStatsImpl(zx::resource root_resource_handle)
      : root_resource_handle_(std::move(root_resource_handle)) {}

  // Binds the implementation to the passed in dispatcher.
  fbl::RefPtr<fs::Service> CreateService(async_dispatcher_t* dispatcher);

  void GetMemoryStats(GetMemoryStatsCompleter::Sync completer) override;

  void GetCpuStats(GetCpuStatsCompleter::Sync completer) override;
 private:
  zx::resource root_resource_handle_;
};

}  // namespace bootsvc

#endif  // ZIRCON_SYSTEM_CORE_BOOTSVC_SVCFS_SERVICE_H_
