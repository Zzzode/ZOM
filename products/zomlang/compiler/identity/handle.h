// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include <cstdint>

#include "zomlang/compiler/identity/brand.h"

namespace zomlang::compiler::identity {

template <typename Key, typename Record, typename Tag>
class CanonicalIdentityInterner;

struct CompilationUnitIdentityTag final {};
struct CrateIdentityTag final {};
struct SourceFileIdentityTag final {};
struct ModuleIdentityTag final {};
struct DefinitionIdentityTag final {};
struct ImplIdentityTag final {};
struct GenericParameterIdentityTag final {};
struct CallableParameterIdentityTag final {};

/// \brief Context-owned semantic handle whose tag is its only issuing registry or store.
template <typename Tag>
class ContextHandle final {
public:
  constexpr ContextHandle() noexcept = default;

  /// \brief Returns true when the handle was issued for a valid semantic context.
  ZC_NODISCARD constexpr bool isValid() const noexcept { return context.isValid(); }

  /// \brief Returns true when the handle belongs to the supplied semantic context.
  ZC_NODISCARD constexpr bool belongsTo(SemanticContextBrand expected) const noexcept {
    return isValid() && context == expected;
  }

  constexpr bool operator==(ContextHandle other) const noexcept {
    return context == other.context && slot == other.slot;
  }
  constexpr bool operator!=(ContextHandle other) const noexcept { return !(*this == other); }
  constexpr bool operator<(ContextHandle other) const noexcept {
    if (context != other.context) return context < other.context;
    return slot < other.slot;
  }

private:
  constexpr ContextHandle(SemanticContextBrand owner, uint32_t value) noexcept
      : context(owner), slot(value) {}

  SemanticContextBrand context;
  uint32_t slot = 0;

  friend Tag;
  template <typename Key, typename Record, typename IdentityTag>
  friend class CanonicalIdentityInterner;
};

/// \brief Store-owned semantic handle for tags with multiple issuing stores per context.
template <typename Tag>
class StoreHandle final {
public:
  constexpr StoreHandle() noexcept = default;

  /// \brief Returns true when the handle was issued by a valid context-local registry.
  ZC_NODISCARD constexpr bool isValid() const noexcept {
    return context.isValid() && issuer.belongsTo(context);
  }

  /// \brief Returns true when the handle belongs to the supplied semantic context.
  ZC_NODISCARD constexpr bool belongsTo(SemanticContextBrand expected) const noexcept {
    return isValid() && context == expected;
  }

  /// \brief Returns true when the handle belongs to the supplied context-local registry.
  ZC_NODISCARD constexpr bool belongsTo(RegistryBrand expected) const noexcept {
    return isValid() && issuer == expected;
  }

  constexpr bool operator==(StoreHandle other) const noexcept {
    return context == other.context && issuer == other.issuer && slot == other.slot;
  }
  constexpr bool operator!=(StoreHandle other) const noexcept { return !(*this == other); }

private:
  constexpr StoreHandle(SemanticContextBrand owner, RegistryBrand registry, uint32_t value) noexcept
      : context(owner), issuer(registry), slot(value) {}

  SemanticContextBrand context;
  RegistryBrand issuer;
  uint32_t slot = 0;

  friend Tag;
};

using CompilationUnitId = ContextHandle<CompilationUnitIdentityTag>;
using CrateId = ContextHandle<CrateIdentityTag>;
using SourceFileId = ContextHandle<SourceFileIdentityTag>;
using ModuleId = ContextHandle<ModuleIdentityTag>;
using DefId = ContextHandle<DefinitionIdentityTag>;
using ImplId = ContextHandle<ImplIdentityTag>;
using GenericParameterId = ContextHandle<GenericParameterIdentityTag>;
using CallableParameterId = ContextHandle<CallableParameterIdentityTag>;

}  // namespace zomlang::compiler::identity
