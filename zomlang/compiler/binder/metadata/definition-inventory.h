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

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/binder/metadata/definition-site.h"
#include "zomlang/compiler/identity/key/definition-key.h"

namespace zomlang::compiler::binder {

enum class StructuralIdentityParentKind : uint8_t { Definition = 0x01, Impl = 0x02 };

/// \brief Syntax identity that structurally contains a nested definition.
struct StructuralIdentityParent final {
  StructuralIdentityParentKind kind;
  ast::NodeId node;
};

enum class InventoryDefinitionNameKind : uint8_t { Declared = 0x01, Anonymous = 0x02 };

enum class AnonymousSyntaxRole : uint8_t { Lambda = 0x01, FunctionExpression = 0x02 };

/// \brief Prebinding description of one semantic definition producer.
struct DefinitionInventoryEntry final {
  ast::NodeId node;
  DefinitionSite site;
  ast::NodeId moduleNode;
  identity::DefinitionKind kind;
  InventoryDefinitionNameKind nameKind;
  ast::IdentId declaredName;
  zc::Maybe<AnonymousSyntaxRole> anonymousRole;
  source::SourceRange source;
  zc::Vector<StructuralIdentityParent> parentPath;
};

/// \brief Prebinding description of one implementation identity producer.
struct ImplInventoryEntry final {
  ast::NodeId node;
  ast::NodeId moduleNode;
  source::SourceRange source;
  zc::Vector<StructuralIdentityParent> parentPath;
};

/// \brief Prebinding description of one explicit module identity producer.
struct ModuleInventoryEntry final {
  ast::NodeId node;
  ast::NodeId parentModuleNode;
  ast::ModuleDeclarationForm form;
  ast::IdentId declaredName;
  source::SourceRange source;
};

/// \brief Exhaustive prebinding inventory split by stable and revision-local identity domain.
class DefinitionInventory final {
public:
  DefinitionInventory() noexcept;
  ~DefinitionInventory() noexcept(false);

  DefinitionInventory(DefinitionInventory&&) noexcept;
  DefinitionInventory& operator=(DefinitionInventory&&) noexcept;
  ZC_DISALLOW_COPY(DefinitionInventory);

  /// \brief Walk a schema-verified tree before symbol creation or name resolution.
  ZC_NODISCARD static DefinitionInventory collect(const ast::Tree& tree);

  /// \brief Return an owning immutable snapshot of this inventory.
  ZC_NODISCARD DefinitionInventory clone() const;

  ZC_NODISCARD zc::ArrayPtr<const ModuleInventoryEntry> modules() const;
  /// \brief Stable named definitions eligible for DefinitionKey admission.
  ZC_NODISCARD zc::ArrayPtr<const DefinitionInventoryEntry> definitions() const;
  ZC_NODISCARD zc::ArrayPtr<const DefinitionInventoryEntry> genericParameters() const;
  ZC_NODISCARD zc::ArrayPtr<const DefinitionInventoryEntry> callableParameters() const;
  ZC_NODISCARD zc::ArrayPtr<const DefinitionInventoryEntry> ownerLocalBindings() const;
  ZC_NODISCARD zc::ArrayPtr<const DefinitionInventoryEntry> anonymousEntities() const;
  ZC_NODISCARD zc::ArrayPtr<const ImplInventoryEntry> impls() const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::binder
