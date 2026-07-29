// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zomlang/compiler/identity/compilation-unit-key.h"
#include "zomlang/compiler/identity/crate-key.h"
#include "zomlang/compiler/identity/definition-key.h"
#include "zomlang/compiler/identity/handle.h"
#include "zomlang/compiler/identity/source-key.h"

namespace zomlang::compiler::identity {

enum class IdentityInternerFailure : uint8_t {
  AllocationFailure = 0x01,
  SlotOverflow = 0x02,
  ForeignBrand = 0x03,
  MalformedRecord = 0x04,
  CanonicalCollision = 0x05,
};

template <typename Handle>
using IdentityInternResult = zc::OneOf<Handle, IdentityInternerFailure>;

/// \brief One immutable canonical identity authority paired with its arena-local handle.
template <typename Key, typename Record, typename Handle>
class CanonicalIdentityEntry final {
public:
  CanonicalIdentityEntry(CanonicalIdentityEntry&&) noexcept = default;
  CanonicalIdentityEntry& operator=(CanonicalIdentityEntry&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalIdentityEntry);

  ZC_NODISCARD CanonicalIdentityEntry clone() const {
    return CanonicalIdentityEntry(keyField.clone(), recordField.clone(), handleField);
  }
  ZC_NODISCARD const Key& key() const noexcept { return keyField; }
  ZC_NODISCARD const Record& record() const noexcept { return recordField; }
  ZC_NODISCARD Handle handle() const noexcept { return handleField; }

private:
  CanonicalIdentityEntry(Key&& key, Record&& record, Handle handle) noexcept
      : keyField(zc::mv(key)), recordField(zc::mv(record)), handleField(handle) {}

  Key keyField;
  Record recordField;
  Handle handleField;

  template <typename InternerKey, typename InternerRecord, typename Tag>
  friend class CanonicalIdentityInterner;
};

using CompilationUnitIdentityEntry =
    CanonicalIdentityEntry<CompilationUnitIdentity, CompilationUnitIdentity, CompilationUnitId>;
using CrateIdentityEntry = CanonicalIdentityEntry<CrateKey, CrateKey, CrateId>;
using SourceFileIdentityEntry = CanonicalIdentityEntry<SourceFileKey, SourceFileKey, SourceFileId>;
using ModuleIdentityEntry = CanonicalIdentityEntry<ModuleKey, ModuleKey, ModuleId>;
using DefinitionIdentityEntry =
    CanonicalIdentityEntry<DefinitionKey, DefinitionIdentityRecord, DefId>;
using ImplementationIdentityEntry = CanonicalIdentityEntry<ImplKey, ImplIdentityRecord, ImplId>;
using GenericParameterIdentityEntry =
    CanonicalIdentityEntry<GenericParameterKey, GenericParameterIdentityRecord, GenericParameterId>;
using CallableParameterIdentityEntry =
    CanonicalIdentityEntry<CallableParameterKey, CallableParameterIdentityRecord,
                           CallableParameterId>;

/// \brief Eight independently locked append-only global identity interners for one context.
class CanonicalIdentityInternerSet final {
public:
  ~CanonicalIdentityInternerSet() noexcept(false);
  CanonicalIdentityInternerSet(CanonicalIdentityInternerSet&&) noexcept;
  CanonicalIdentityInternerSet& operator=(CanonicalIdentityInternerSet&&) noexcept;
  ZC_DISALLOW_COPY(CanonicalIdentityInternerSet);

  ZC_NODISCARD static zc::Maybe<CanonicalIdentityInternerSet> create(
      SemanticContextFactory& factory, SemanticContextBrand context);
  ZC_NODISCARD SemanticContextBrand context() const noexcept;

  ZC_NODISCARD IdentityInternResult<CompilationUnitId> internCompilationUnit(
      SemanticContextBrand context, const CompilationUnitIdentity& key);
  ZC_NODISCARD IdentityInternResult<CrateId> internCrate(SemanticContextBrand context,
                                                         const CrateKey& key);
  ZC_NODISCARD IdentityInternResult<SourceFileId> internSourceFile(SemanticContextBrand context,
                                                                   const SourceFileKey& key);
  ZC_NODISCARD IdentityInternResult<ModuleId> internModule(SemanticContextBrand context,
                                                           const ModuleKey& key);
  ZC_NODISCARD IdentityInternResult<DefId> internDefinition(SemanticContextBrand context,
                                                            const DefinitionKey& key,
                                                            const DefinitionIdentityRecord& record);
  ZC_NODISCARD IdentityInternResult<ImplId> internImplementation(SemanticContextBrand context,
                                                                 const ImplKey& key,
                                                                 const ImplIdentityRecord& record);
  ZC_NODISCARD IdentityInternResult<GenericParameterId> internGenericParameter(
      SemanticContextBrand context, const GenericParameterKey& key,
      const GenericParameterIdentityRecord& record);
  ZC_NODISCARD IdentityInternResult<CallableParameterId> internCallableParameter(
      SemanticContextBrand context, const CallableParameterKey& key,
      const CallableParameterIdentityRecord& record);

  ZC_NODISCARD zc::Maybe<CompilationUnitIdentityEntry> compilationUnit(
      CompilationUnitId handle) const;
  ZC_NODISCARD zc::Maybe<CrateIdentityEntry> crate(CrateId handle) const;
  ZC_NODISCARD zc::Maybe<SourceFileIdentityEntry> sourceFile(SourceFileId handle) const;
  ZC_NODISCARD zc::Maybe<ModuleIdentityEntry> module(ModuleId handle) const;
  ZC_NODISCARD zc::Maybe<DefinitionIdentityEntry> definition(DefId handle) const;
  ZC_NODISCARD zc::Maybe<ImplementationIdentityEntry> implementation(ImplId handle) const;
  ZC_NODISCARD zc::Maybe<GenericParameterIdentityEntry> genericParameter(
      GenericParameterId handle) const;
  ZC_NODISCARD zc::Maybe<CallableParameterIdentityEntry> callableParameter(
      CallableParameterId handle) const;

private:
  struct Impl;
  explicit CanonicalIdentityInternerSet(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::identity
