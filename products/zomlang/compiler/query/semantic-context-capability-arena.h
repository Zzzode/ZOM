// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/refcount.h"

namespace zomlang::compiler::query {

class SnapshotCapabilityArena;

/// \brief Type-erased owner of session semantic resources retained by capability leases.
class SemanticContextCapabilityResources {
public:
  virtual ~SemanticContextCapabilityResources() noexcept(false) = default;
  ZC_DISALLOW_COPY_AND_MOVE(SemanticContextCapabilityResources);

protected:
  SemanticContextCapabilityResources() = default;
};

/// \brief Refcounted lifetime root for semantic resources and global identity interners.
class SemanticContextCapabilityArena final : public zc::AtomicRefcounted {
public:
  SemanticContextCapabilityArena();
  explicit SemanticContextCapabilityArena(zc::Own<SemanticContextCapabilityResources>&& resources);
  ~SemanticContextCapabilityArena() noexcept(false);
  ZC_DISALLOW_COPY_AND_MOVE(SemanticContextCapabilityArena);

  ZC_NODISCARD bool hasResources() const noexcept;

private:
  ZC_NODISCARD const SemanticContextCapabilityResources& resources() const ZC_LIFETIMEBOUND;

  struct Impl;
  zc::Own<Impl> impl;

  friend class SnapshotCapabilityArena;
};

}  // namespace zomlang::compiler::query
