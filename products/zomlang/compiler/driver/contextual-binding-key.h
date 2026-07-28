// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zomlang/compiler/binder/stable-binding-facts.h"
#include "zomlang/compiler/driver/incremental-binding-query-adapter.h"

namespace zomlang::compiler::driver::incremental_binding_query {

#define ZOM_DECLARE_CONTEXTUAL_BINDING_KEY(Name, ValueType, ValueName)                      \
  class Name final {                                                                        \
  public:                                                                                   \
    Name(Name&&) noexcept = default;                                                        \
    Name& operator=(Name&&) noexcept = default;                                             \
    ZC_DISALLOW_COPY(Name);                                                                 \
    ZC_NODISCARD static Name from(CompilationRootSetQueryKey&& contextRoots,                \
                                  ValueType&& ValueName);                                   \
    ZC_NODISCARD static zc::Maybe<Name> decodeCanonical(zc::ArrayPtr<const uint8_t> bytes); \
    ZC_NODISCARD Name clone() const;                                                        \
    ZC_NODISCARD const CompilationRootSetQueryKey& contextRoots() const noexcept;           \
    ZC_NODISCARD const ValueType& ValueName() const noexcept;                               \
    ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;                                \
    bool operator==(const Name& other) const;                                               \
    bool operator!=(const Name& other) const { return !(*this == other); }                  \
                                                                                            \
  private:                                                                                  \
    Name(CompilationRootSetQueryKey&& contextRoots, ValueType&& ValueName) noexcept;        \
    CompilationRootSetQueryKey contextRootsField;                                           \
    ValueType ValueName##Field;                                                             \
  }

/// \brief Complete compilation context and stable body-owner query selector.
ZOM_DECLARE_CONTEXTUAL_BINDING_KEY(ContextualBodyOwnerKey, binder::StableOwnerBodyQueryKey, body);
/// \brief Complete compilation context and compilation-unit selector.
ZOM_DECLARE_CONTEXTUAL_BINDING_KEY(ContextualCompilationUnitKey, identity::CompilationUnitIdentity,
                                   unit);
/// \brief Complete compilation context and crate selector.
ZOM_DECLARE_CONTEXTUAL_BINDING_KEY(ContextualCrateKey, identity::CrateKey, crate);
/// \brief Complete compilation context and source selector.
ZOM_DECLARE_CONTEXTUAL_BINDING_KEY(ContextualSourceKey, identity::SourceFileKey, source);
/// \brief Complete compilation context and module selector.
ZOM_DECLARE_CONTEXTUAL_BINDING_KEY(ContextualModuleKey, identity::ModuleKey, module);
/// \brief Complete compilation context and stable definition selector.
ZOM_DECLARE_CONTEXTUAL_BINDING_KEY(ContextualDefinitionKey, binder::StableDefinitionQueryKey,
                                   definition);
/// \brief Complete compilation context and stable implementation selector.
ZOM_DECLARE_CONTEXTUAL_BINDING_KEY(ContextualImplementationKey,
                                   binder::StableImplementationQueryKey, implementation);
/// \brief Complete compilation context and stable generic-parameter selector.
ZOM_DECLARE_CONTEXTUAL_BINDING_KEY(ContextualGenericParameterKey,
                                   binder::StableGenericParameterQueryKey, parameter);
/// \brief Complete compilation context and stable callable-parameter selector.
ZOM_DECLARE_CONTEXTUAL_BINDING_KEY(ContextualCallableParameterKey,
                                   binder::StableCallableParameterQueryKey, parameter);

#undef ZOM_DECLARE_CONTEXTUAL_BINDING_KEY

}  // namespace zomlang::compiler::driver::incremental_binding_query
