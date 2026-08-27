// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/query/semantic-context-capability-arena.h"

#include "zc/core/debug.h"

namespace zomlang::compiler::query {

struct SemanticContextCapabilityArena::Impl final {
  Impl() = default;
  explicit Impl(zc::Own<SemanticContextCapabilityResources>&& resources) noexcept
      : resources(zc::mv(resources)) {}

  zc::Own<SemanticContextCapabilityResources> resources;
};

SemanticContextCapabilityArena::SemanticContextCapabilityArena() : impl(zc::heap<Impl>()) {}

SemanticContextCapabilityArena::SemanticContextCapabilityArena(
    zc::Own<SemanticContextCapabilityResources>&& resources)
    : impl(zc::heap<Impl>(zc::mv(resources))) {
  ZC_IREQUIRE(impl->resources.get() != nullptr,
              "semantic context capability arena has no resources");
}

SemanticContextCapabilityArena::~SemanticContextCapabilityArena() noexcept(false) = default;

bool SemanticContextCapabilityArena::hasResources() const noexcept {
  return impl->resources.get() != nullptr;
}

const SemanticContextCapabilityResources& SemanticContextCapabilityArena::resources() const {
  ZC_IREQUIRE(impl->resources.get() != nullptr,
              "semantic context capability arena has no resources");
  return *impl->resources;
}

}  // namespace zomlang::compiler::query
