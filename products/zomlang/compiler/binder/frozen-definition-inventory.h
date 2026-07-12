// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zomlang/compiler/ast/node-id.h"
#include "zomlang/compiler/binder/definition-identity-map.h"
#include "zomlang/compiler/binder/parsed-module.h"
#include "zomlang/compiler/identity/canonical-scalar.h"
#include "zomlang/compiler/identity/definition-key.h"

namespace zomlang::compiler::binder {

enum class FrozenInventoryInvariantKind : uint8_t {
  InputMismatch,
  IncompleteInventory,
  InvalidDefinitionSite,
  InvalidDefinitionIdentity,
  UnsupportedImplInventory
};

struct FrozenInventoryInvariantFact final {
  FrozenInventoryInvariantKind kind;
  uint32_t occurrence;
};

/// \brief One declaration site paired with its frozen semantic identity.
struct FrozenDefinitionEntry final {
  FrozenDefinitionEntry(ast::NodeId node, identity::DefId definition, identity::DefinitionKey&& key,
                        identity::DefinitionKind kind, identity::DefinitionNameKey&& name,
                        zc::Maybe<identity::SemanticIdentifier>&& bindingName,
                        identity::SourceSpan&& source) noexcept;
  FrozenDefinitionEntry(FrozenDefinitionEntry&&) noexcept = default;
  FrozenDefinitionEntry& operator=(FrozenDefinitionEntry&&) noexcept = default;
  ZC_DISALLOW_COPY(FrozenDefinitionEntry);

  ast::NodeId node;
  identity::DefId definition;
  identity::DefinitionKey key;
  identity::DefinitionKind kind;
  identity::DefinitionNameKey name;
  zc::Maybe<identity::SemanticIdentifier> bindingName;
  identity::SourceSpan source;
};

/// \brief Immutable single-module projection of the context-global identity inventory.
class FrozenDefinitionInventoryView final {
public:
  ~FrozenDefinitionInventoryView() noexcept(false);
  FrozenDefinitionInventoryView(FrozenDefinitionInventoryView&&) noexcept;
  FrozenDefinitionInventoryView& operator=(FrozenDefinitionInventoryView&&) noexcept;
  ZC_DISALLOW_COPY(FrozenDefinitionInventoryView);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD ast::NodeId moduleNode() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const FrozenDefinitionEntry> definitions() const;
  ZC_NODISCARD zc::Maybe<identity::DefId> definitionAt(ast::NodeId node) const;

private:
  struct Impl;
  explicit FrozenDefinitionInventoryView(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class FrozenDefinitionInventoryVerifier;
};

using FrozenDefinitionInventoryResult =
    zc::OneOf<FrozenDefinitionInventoryView, FrozenInventoryInvariantFact>;

/// \brief Verifies the dependency-free frozen identity projection used by RFC 0004.
class FrozenDefinitionInventoryVerifier final {
public:
  ZC_NODISCARD static FrozenDefinitionInventoryResult verifySingleModule(
      identity::SemanticContextBrand context, identity::ModuleId module,
      const VerifiedParsedModule& parsedModule,
      const identity::SemanticIdentityRegistrySet& registries,
      const DefinitionIdentityMap& definitions);
};

}  // namespace zomlang::compiler::binder
