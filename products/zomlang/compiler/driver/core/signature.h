// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/memory.h"
#include "zomlang/compiler/binder/module-body-syntax.h"
#include "zomlang/compiler/checker/signature-facts.h"
#include "zomlang/compiler/identity/key/source-key.h"
#include "zomlang/compiler/query/query-database.h"
#include "zomlang/compiler/source/core-distribution.h"

namespace zomlang::compiler::driver::core_library_query {
enum class CoreBootstrapModuleSurface : uint8_t;
class VerifiedCoreRoleSeed;
class VerifiedCoreBootstrapModuleInterface;
}  // namespace zomlang::compiler::driver::core_library_query

namespace zomlang::compiler::driver::module_graph_query {
class VerifiedBoundModule;
}  // namespace zomlang::compiler::driver::module_graph_query

namespace zomlang::compiler::checker {
class CheckerIdentityAuthority;
}

namespace zomlang::compiler::driver::module_graph_query {
class CheckerBoundModuleView;
}

namespace zomlang::compiler::driver::core {

/// \brief Checks the declaration-only marker interface shape admitted during core bootstrap.
ZC_NODISCARD bool isInitialMarkerInterface(const binder::NamedItemSyntax& syntax);

/// \brief Checks the complete closed declaration surface of one initial core module.
ZC_NODISCARD bool matchesInitialSurface(core_library_query::CoreBootstrapModuleSurface surface,
                                        const module_graph_query::VerifiedBoundModule& bound,
                                        const core_library_query::VerifiedCoreRoleSeed& seed);

/// \brief Decoded canonical form of one closed type-free marker interface signature.
class TypeFreeInterfaceSignatureRecord final {
public:
  ~TypeFreeInterfaceSignatureRecord() noexcept(false);
  TypeFreeInterfaceSignatureRecord(TypeFreeInterfaceSignatureRecord&&) noexcept;
  TypeFreeInterfaceSignatureRecord& operator=(TypeFreeInterfaceSignatureRecord&&) noexcept;
  ZC_DISALLOW_COPY(TypeFreeInterfaceSignatureRecord);

  ZC_NODISCARD static zc::Maybe<TypeFreeInterfaceSignatureRecord> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD TypeFreeInterfaceSignatureRecord clone() const;
  ZC_NODISCARD const identity::DefinitionKey& definition() const noexcept;
  ZC_NODISCARD const identity::SourceFileKey& source() const noexcept;
  ZC_NODISCARD uint64_t byteStart() const noexcept;
  ZC_NODISCARD uint64_t byteEnd() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  struct Impl;
  explicit TypeFreeInterfaceSignatureRecord(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief One role-backed type-free signature fact admitted during core bootstrap.
struct CoreSignatureFact final {
  source::core::CoreSemanticRole role;
  checker::signature::SemanticSignature signature;
  zc::Array<uint8_t> canonical;
};

/// \brief Immutable signature projection for the closed type-free core bootstrap algebra.
class VerifiedCoreSignatureFacts final {
public:
  ~VerifiedCoreSignatureFacts() noexcept(false);
  VerifiedCoreSignatureFacts(VerifiedCoreSignatureFacts&&) noexcept;
  VerifiedCoreSignatureFacts& operator=(VerifiedCoreSignatureFacts&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedCoreSignatureFacts);

  /// \brief Builds the sole type-free facts permitted before ordinary Checker bootstrap.
  ZC_NODISCARD static zc::Maybe<VerifiedCoreSignatureFacts> from(
      core_library_query::CoreBootstrapModuleSurface surface,
      const module_graph_query::VerifiedBoundModule& bound,
      const core_library_query::VerifiedCoreRoleSeed& seed,
      const checker::CheckerIdentityAuthority& identities);
  ZC_NODISCARD core_library_query::CoreBootstrapModuleSurface surface() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CoreSignatureFact> facts() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  struct Impl;
  explicit VerifiedCoreSignatureFacts(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Bootstrap-only imported signatures retained from dependency-ordered core interfaces.
class VerifiedCoreImportedSignatureView final {
public:
  using BootstrapInterfaceLease =
      query::QueryCapabilityLease<const core_library_query::VerifiedCoreBootstrapModuleInterface>;

  ~VerifiedCoreImportedSignatureView() noexcept(false);
  VerifiedCoreImportedSignatureView(VerifiedCoreImportedSignatureView&&) noexcept;
  VerifiedCoreImportedSignatureView& operator=(VerifiedCoreImportedSignatureView&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedCoreImportedSignatureView);

  /// \brief Validates the complete type-free imports admitted during core bootstrap.
  ZC_NODISCARD static zc::Maybe<VerifiedCoreImportedSignatureView> from(
      const module_graph_query::CheckerBoundModuleView& requester,
      const identity::CoreSemanticContextFingerprint& coreContext,
      zc::Vector<BootstrapInterfaceLease>&& sources);
  ZC_NODISCARD identity::ModuleId requester() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const BootstrapInterfaceLease> sources() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  struct Impl;
  explicit VerifiedCoreImportedSignatureView(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::driver::core
