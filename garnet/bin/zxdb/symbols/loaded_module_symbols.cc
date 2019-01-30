// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/loaded_module_symbols.h"
#include "garnet/bin/zxdb/symbols/location.h"
#include "garnet/bin/zxdb/symbols/module_symbols.h"

namespace zxdb {

LoadedModuleSymbols::LoadedModuleSymbols(
    fxl::RefPtr<SystemSymbols::ModuleRef> module, std::string build_id,
    uint64_t load_address)
    : module_(std::move(module)),
      load_address_(load_address),
      build_id_(std::move(build_id)),
      symbol_context_(load_address),
      weak_factory_(this) {}

LoadedModuleSymbols::~LoadedModuleSymbols() = default;

fxl::WeakPtr<LoadedModuleSymbols> LoadedModuleSymbols::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

std::vector<Location> LoadedModuleSymbols::ResolveInputLocation(
    const InputLocation& input_location, const ResolveOptions& options) const {
  std::vector<Location> ret;

  if (module_) {
    ret = module_symbols()->ResolveInputLocation(symbol_context(),
                                                 input_location, options);
  }

  if (input_location.type == InputLocation::Type::kElfSymbol ||
      input_location.type == InputLocation::Type::kSymbol) {
    for (const auto& sym : elf_symbols_) {
      if (sym.name == input_location.symbol) {
        ret.emplace_back(Location::State::kAddress, sym.value);
        break;
      }
    }
  }

  return ret;
}

}  // namespace zxdb
