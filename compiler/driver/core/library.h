// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "compiler/driver/core/query.h"

namespace zomlang::compiler::driver {
class CompilerSession;
}

namespace zomlang::compiler::driver::core {

/// \brief One declaration-only core module retaining its finalized interface memo.
class VerifiedCoreModule final {
public:
  using InterfaceLease =
      query::QueryCapabilityLease<const core_library_query::VerifiedCoreModuleInterface>;

  ~VerifiedCoreModule() noexcept(false);
  VerifiedCoreModule(VerifiedCoreModule&&) noexcept;
  VerifiedCoreModule& operator=(VerifiedCoreModule&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedCoreModule);

  ZC_NODISCARD VerifiedCoreModule clone() const;
  ZC_NODISCARD const identity::ModuleKey& module() const noexcept;
  ZC_NODISCARD const InterfaceLease& interfaceLease() const noexcept;

private:
  struct Impl;
  ZC_NODISCARD static zc::Maybe<VerifiedCoreModule> from(identity::ModuleKey&& module,
                                                         InterfaceLease&& interface);
  explicit VerifiedCoreModule(zc::Own<Impl>&& impl) noexcept;
  friend class ::zomlang::compiler::driver::CompilerSession;
  zc::Own<Impl> impl;
};

/// \brief Session-local source-backed core library retaining only final interface leases.
class VerifiedCoreLibrary final {
public:
  using AuthorityLease =
      query::QueryCapabilityLease<const core_library_query::VerifiedCoreAuthorityBundle>;

  ~VerifiedCoreLibrary() noexcept(false);
  VerifiedCoreLibrary(VerifiedCoreLibrary&&) noexcept;
  VerifiedCoreLibrary& operator=(VerifiedCoreLibrary&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedCoreLibrary);

  ZC_NODISCARD identity::SemanticContextBrand context() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& fingerprint() const noexcept;
  ZC_NODISCARD const incremental_binding_query::CompilationRootSetQueryKey& contextRoots()
      const noexcept;
  ZC_NODISCARD query::DatabaseRevision revision() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& distribution() const noexcept;
  ZC_NODISCARD const core_library_query::CoreModuleGraphRecord& graph() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const VerifiedCoreModule> modules() const noexcept;
  ZC_NODISCARD const identity::ModuleKey& prelude() const noexcept;
  ZC_NODISCARD const AuthorityLease& authorityLease() const noexcept;

private:
  struct Impl;
  ZC_NODISCARD static zc::Maybe<VerifiedCoreLibrary> from(
      identity::SemanticContextBrand context, identity::ContextFingerprint&& fingerprint,
      incremental_binding_query::CompilationRootSetQueryKey&& contextRoots,
      query::DatabaseRevision revision, const identity::Sha256Digest& distribution,
      core_library_query::CoreModuleGraphRecord&& graph, zc::Vector<VerifiedCoreModule>&& modules,
      identity::ModuleKey&& prelude, AuthorityLease&& authority);
  explicit VerifiedCoreLibrary(zc::Own<Impl>&& impl) noexcept;
  friend class ::zomlang::compiler::driver::CompilerSession;
  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::driver::core
