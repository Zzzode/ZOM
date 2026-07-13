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
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/binder/internal/body-binding.h"

#include <cstdint>

#include "zc/core/map.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/generated/node-schema.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/binder/internal/binding-skeleton.h"

namespace zomlang::compiler::binder {
namespace {

constexpr uint32_t kMissingIndex = UINT32_MAX;
constexpr size_t kMissingSize = static_cast<size_t>(-1);

BinderInvariantFact failure(const VerifiedBindingInput& input, BinderInvariantKind kind,
                            uint32_t ordinal) {
  return BinderInvariantFact{kind, input.module(), zc::none, BinderEmitterSite::BodyBinding,
                             ordinal};
}

bool ownsScope(identity::DefinitionKind kind) {
  using identity::DefinitionKind;
  switch (kind) {
    case DefinitionKind::Function:
    case DefinitionKind::Method:
    case DefinitionKind::Constructor:
    case DefinitionKind::Destructor:
    case DefinitionKind::Class:
    case DefinitionKind::Struct:
    case DefinitionKind::Interface:
    case DefinitionKind::Enum:
    case DefinitionKind::Error:
    case DefinitionKind::Closure:
      return true;
    case DefinitionKind::ModuleAlias:
    case DefinitionKind::TypeAlias:
    case DefinitionKind::AssociatedType:
    case DefinitionKind::Field:
    case DefinitionKind::EnumVariant:
    case DefinitionKind::Parameter:
    case DefinitionKind::TypeParameter:
    case DefinitionKind::Constant:
    case DefinitionKind::Static:
    case DefinitionKind::Local:
    case DefinitionKind::PatternBinding:
    case DefinitionKind::ImportAlias:
    case DefinitionKind::ReexportAlias:
      return false;
  }
  ZC_UNREACHABLE;
}

zc::Maybe<Namespace> definitionNamespace(identity::DefinitionKind kind) {
  using identity::DefinitionKind;
  switch (kind) {
    case DefinitionKind::Function:
    case DefinitionKind::Method:
    case DefinitionKind::Field:
    case DefinitionKind::EnumVariant:
    case DefinitionKind::Parameter:
    case DefinitionKind::Constant:
    case DefinitionKind::Static:
    case DefinitionKind::Local:
    case DefinitionKind::PatternBinding:
      return Namespace::Value;
    case DefinitionKind::Class:
    case DefinitionKind::Struct:
    case DefinitionKind::Interface:
    case DefinitionKind::Enum:
    case DefinitionKind::Error:
    case DefinitionKind::TypeAlias:
    case DefinitionKind::AssociatedType:
    case DefinitionKind::TypeParameter:
      return Namespace::Type;
    case DefinitionKind::ModuleAlias:
      return Namespace::Module;
    case DefinitionKind::Constructor:
    case DefinitionKind::Destructor:
    case DefinitionKind::Closure:
    case DefinitionKind::ImportAlias:
    case DefinitionKind::ReexportAlias:
      return zc::none;
  }
  ZC_UNREACHABLE;
}

zc::Maybe<DefinitionActivation> activationFor(const ast::Tree& tree,
                                              const FrozenDefinitionEntry& entry) {
  using identity::DefinitionKind;
  switch (entry.kind) {
    case DefinitionKind::TypeParameter:
      return DefinitionActivation::GenericList;
    case DefinitionKind::Parameter:
      return DefinitionActivation::ParameterList;
    case DefinitionKind::Closure:
      return DefinitionActivation::ExpressionIntroduction;
    case DefinitionKind::Local:
      return DefinitionActivation::AfterInitializer;
    case DefinitionKind::PatternBinding: {
      if (!entry.site.value().is<PatternBindingSite>()) { return zc::none; }
      const auto introducer = entry.site.value().get<PatternBindingSite>().introducer;
      if (!tree.contains(introducer)) { return zc::none; }
      if (tree.node(introducer).kind == ast::SyntaxKind::ForInStatement) {
        return DefinitionActivation::LoopPattern;
      }
      if (tree.node(introducer).kind == ast::SyntaxKind::MatchArmStmt) {
        return DefinitionActivation::MatchPattern;
      }
      return zc::none;
    }
    case DefinitionKind::Function:
    case DefinitionKind::Method:
    case DefinitionKind::Constructor:
    case DefinitionKind::Destructor:
    case DefinitionKind::Class:
    case DefinitionKind::Struct:
    case DefinitionKind::Interface:
    case DefinitionKind::Enum:
    case DefinitionKind::Error:
    case DefinitionKind::TypeAlias:
    case DefinitionKind::AssociatedType:
    case DefinitionKind::Field:
    case DefinitionKind::EnumVariant:
    case DefinitionKind::Constant:
    case DefinitionKind::Static:
      return DefinitionActivation::ModuleSkeleton;
    case DefinitionKind::ModuleAlias:
      return DefinitionActivation::ImportSurface;
    case DefinitionKind::ImportAlias:
      return DefinitionActivation::ImportSurface;
    case DefinitionKind::ReexportAlias:
      return DefinitionActivation::ReexportSurface;
  }
  ZC_UNREACHABLE;
}

struct SourceOrderKey final {
  uint64_t start;
  uint64_t end;
  size_t inventoryIndex;

  bool operator==(const SourceOrderKey& other) const noexcept {
    return start == other.start && end == other.end && inventoryIndex == other.inventoryIndex;
  }
  bool operator<(const SourceOrderKey& other) const noexcept {
    if (start != other.start) { return start < other.start; }
    if (end != other.end) { return end < other.end; }
    return inventoryIndex < other.inventoryIndex;
  }
};

struct ActiveScopeIndex final {
  zc::HashMap<zc::String, size_t> values;
  zc::HashMap<zc::String, size_t> types;
};

zc::HashMap<zc::String, size_t>& bindingsFor(ActiveScopeIndex& scope, Namespace nameSpace) {
  if (nameSpace == Namespace::Value) { return scope.values; }
  if (nameSpace == Namespace::Type) { return scope.types; }
  ZC_UNREACHABLE;
}

const zc::HashMap<zc::String, size_t>& bindingsFor(const ActiveScopeIndex& scope,
                                                   Namespace nameSpace) {
  if (nameSpace == Namespace::Value) { return scope.values; }
  if (nameSpace == Namespace::Type) { return scope.types; }
  ZC_UNREACHABLE;
}

struct LocalFactRecord final {
  DefinitionFact fact;
};

struct ShadowRecord final {
  size_t inventoryIndex;
  ShadowTargetFact fact;
};

struct BindingOrderKey final {
  BindingOrderKey(Namespace nameSpace, zc::String&& name) noexcept
      : nameSpace(nameSpace), name(zc::mv(name)) {}
  BindingOrderKey(BindingOrderKey&&) noexcept = default;
  BindingOrderKey& operator=(BindingOrderKey&&) noexcept = default;
  ZC_DISALLOW_COPY(BindingOrderKey);

  bool operator==(const BindingOrderKey& other) const noexcept {
    return nameSpace == other.nameSpace && name == other.name;
  }
  bool operator<(const BindingOrderKey& other) const noexcept {
    if (nameSpace != other.nameSpace) {
      return static_cast<uint8_t>(nameSpace) < static_cast<uint8_t>(other.nameSpace);
    }
    return name < other.name;
  }

  Namespace nameSpace;
  zc::String name;
};

zc::String encodedDefinitionKey(const FrozenDefinitionEntry& entry) {
  const auto bytes = entry.key.encode();
  return zc::str(bytes.asChars());
}

Namespace childNamespace(const ast::NodeSchemaFieldEntry& field, Namespace inherited) {
  if (field.castTarget == nullptr) { return inherited; }
  const zc::StringPtr target(field.castTarget);
  if (target == "TypeExpr"_zc || target == "TypeParamDecl"_zc) { return Namespace::Type; }
  if (target == "Expression"_zc || target == "LiteralExpr"_zc) { return Namespace::Value; }
  return inherited;
}

}  // namespace

class BodyBindingCursor final {
public:
  BodyBindingCursor(const VerifiedBindingInput& input, ScopeArenaCandidate& arena,
                    DefinitionSkeletonCandidate& skeleton)
      : input(input),
        tree(input.tree()),
        inventory(input.definitions().definitions()),
        arena(arena),
        skeleton(skeleton) {}

  BodyBindingBuildResult run() {
    initializeIndices();
    if (rejected != zc::none) { return takeRejection(); }
    seedDefinitions(DefinitionActivation::ModuleSkeleton);
    seedDefinitions(DefinitionActivation::GenericList);
    if (rejected != zc::none) { return takeRejection(); }

    const auto rootScope = scopeIndexFor(tree.root());
    if (rootScope == zc::none) {
      return rejectNow(BinderInvariantKind::MalformedScopeGraph, tree.root());
    }
    ZC_IF_SOME(scopeIndex, rootScope) { visitNode(tree.root(), scopeIndex, Namespace::Value); }
    if (rejected != zc::none) { return takeRejection(); }

    for (size_t index = 0; index < inventory.size(); ++index) {
      if (inventory[index].kind == identity::DefinitionKind::Local &&
          localFactSlots[index] == kMissingSize) {
        return rejectNow(BinderInvariantKind::MissingRequiredResolution, inventory[index].node);
      }
    }
    if (!finishDefinitions() || !finishNodeBindings() || !finishShadowTargets() ||
        !finishScopeBindings()) {
      return takeRejection();
    }
    return zc::mv(result);
  }

private:
  const VerifiedBindingInput& input;
  const ast::Tree& tree;
  zc::ArrayPtr<const FrozenDefinitionEntry> inventory;
  ScopeArenaCandidate& arena;
  DefinitionSkeletonCandidate& skeleton;
  BodyBindingCandidate result;
  zc::Vector<uint32_t> nodeScopeIndices;
  zc::Vector<uint32_t> schemaOrdinals;
  zc::Vector<uint32_t> definitionScopeIndices;
  zc::Vector<zc::Vector<size_t>> definitionsByIntroducer;
  zc::Vector<zc::Vector<size_t>> definitionsByScope;
  zc::Vector<ActiveScopeIndex> activeScopes;
  zc::Vector<LocalFactRecord> localFacts;
  zc::Vector<size_t> localFactSlots;
  zc::Vector<ShadowRecord> shadows;
  zc::Maybe<BinderInvariantFact> rejected;

  BodyBindingBuildResult rejectNow(BinderInvariantKind kind, ast::NodeId node) {
    reject(kind, node);
    return takeRejection();
  }

  BinderInvariantFact takeRejection() {
    ZC_IF_SOME(fact, rejected) { return zc::mv(fact); }
    ZC_UNREACHABLE;
  }

  void reject(BinderInvariantKind kind, ast::NodeId node) {
    if (rejected != zc::none) { return; }
    uint32_t ordinal = node.value;
    if (node.value < schemaOrdinals.size() && schemaOrdinals[node.value] != kMissingIndex) {
      ordinal = schemaOrdinals[node.value];
    }
    rejected = failure(input, kind, ordinal);
  }

  void initializeIndices() {
    nodeScopeIndices.resize(tree.nodeCount() + 1);
    schemaOrdinals.resize(tree.nodeCount() + 1);
    definitionsByIntroducer.resize(tree.nodeCount() + 1);
    for (size_t index = 0; index < nodeScopeIndices.size(); ++index) {
      nodeScopeIndices[index] = kMissingIndex;
      schemaOrdinals[index] = kMissingIndex;
    }
    definitionsByScope.resize(arena.scopes.size());
    activeScopes.reserve(arena.scopes.size());
    for (size_t index = 0; index < arena.scopes.size(); ++index) { activeScopes.add(); }

    for (const auto& fact : arena.nodeScopes) {
      if (!tree.contains(fact.node) || fact.node.value >= nodeScopeIndices.size() ||
          fact.scope.index() >= arena.scopes.size() ||
          arena.scopes[fact.scope.index()].id != fact.scope ||
          nodeScopeIndices[fact.node.value] != kMissingIndex) {
        reject(BinderInvariantKind::MalformedScopeGraph, fact.node);
        return;
      }
      nodeScopeIndices[fact.node.value] = fact.scope.index();
    }
    uint32_t ordinal = 0;
    ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node&) {
      if (node.value >= schemaOrdinals.size() || schemaOrdinals[node.value] != kMissingIndex) {
        reject(BinderInvariantKind::InvalidEmitterOrdinal, node);
        return;
      }
      schemaOrdinals[node.value] = ordinal++;
    });
    if (rejected != zc::none || ordinal != tree.nodeCount()) {
      if (rejected == zc::none) { reject(BinderInvariantKind::InvalidEmitterOrdinal, tree.root()); }
      return;
    }

    definitionScopeIndices.resize(inventory.size());
    localFactSlots.resize(inventory.size());
    for (size_t index = 0; index < inventory.size(); ++index) {
      definitionScopeIndices[index] = kMissingIndex;
      localFactSlots[index] = kMissingSize;
      const auto& entry = inventory[index];
      auto scope = scopeIndexFor(entry.node);
      if (scope == zc::none) {
        reject(BinderInvariantKind::MalformedScopeGraph, entry.node);
        return;
      }
      ZC_IF_SOME(scopeIndex, scope) {
        uint32_t declaringScope = scopeIndex;
        if (ownsScope(entry.kind)) {
          const auto& record = arena.scopes[scopeIndex];
          if (record.parent == zc::none) {
            reject(BinderInvariantKind::MalformedScopeGraph, entry.node);
            return;
          }
          ZC_IF_SOME(parent, record.parent) { declaringScope = parent.index(); }
        }
        if (declaringScope >= definitionsByScope.size()) {
          reject(BinderInvariantKind::MalformedScopeGraph, entry.node);
          return;
        }
        definitionScopeIndices[index] = declaringScope;
        definitionsByScope[declaringScope].add(index);
      }

      ast::NodeId introducer = entry.node;
      if (entry.site.value().is<PatternBindingSite>()) {
        introducer = entry.site.value().get<PatternBindingSite>().introducer;
      }
      if (!tree.contains(introducer) || introducer.value >= definitionsByIntroducer.size()) {
        reject(BinderInvariantKind::InvalidBindingFact, entry.node);
        return;
      }
      definitionsByIntroducer[introducer.value].add(index);
    }
  }

  zc::Maybe<uint32_t> scopeIndexFor(ast::NodeId node) const {
    if (!tree.contains(node) || node.value >= nodeScopeIndices.size() ||
        nodeScopeIndices[node.value] == kMissingIndex) {
      return zc::none;
    }
    return nodeScopeIndices[node.value];
  }

  void seedDefinitions(DefinitionActivation activation) {
    for (uint32_t scopeIndex = 0; scopeIndex < definitionsByScope.size(); ++scopeIndex) {
      zc::TreeMap<SourceOrderKey, size_t> order;
      for (const size_t inventoryIndex : definitionsByScope[scopeIndex]) {
        auto entryActivation = activationFor(tree, inventory[inventoryIndex]);
        if (entryActivation == zc::none || entryActivation != activation) { continue; }
        const auto& source = inventory[inventoryIndex].source;
        order.insert(SourceOrderKey{source.byteStart(), source.byteEnd(), inventoryIndex},
                     inventoryIndex);
      }
      for (const auto& ordered : order) {
        activateDefinition(ordered.value, activation, false);
        if (rejected != zc::none) { return; }
      }
    }
  }

  zc::Maybe<size_t> activeDefinition(uint32_t scopeIndex, Namespace nameSpace, zc::StringPtr name,
                                     bool includeCurrent = true) const {
    uint32_t current = scopeIndex;
    bool first = true;
    while (current < arena.scopes.size()) {
      if (includeCurrent || !first) {
        auto found = bindingsFor(activeScopes[current], nameSpace).find(name);
        ZC_IF_SOME(index, found) { return index; }
      }
      first = false;
      const auto& scope = arena.scopes[current];
      if (scope.parent == zc::none) { break; }
      ZC_IF_SOME(parent, scope.parent) { current = parent.index(); }
    }
    return zc::none;
  }

  void publishLocalFact(size_t inventoryIndex, uint32_t scopeIndex) {
    if (inventoryIndex >= localFactSlots.size() || localFactSlots[inventoryIndex] != kMissingSize) {
      reject(BinderInvariantKind::InvalidBindingFact, inventory[inventoryIndex].node);
      return;
    }
    const auto& entry = inventory[inventoryIndex];
    localFactSlots[inventoryIndex] = localFacts.size();
    localFacts.add(LocalFactRecord{DefinitionFact(entry.definition, entry.site.clone(), entry.kind,
                                                  entry.name.clone(), Namespace::Value,
                                                  arena.scopes[scopeIndex].id, entry.source.clone(),
                                                  DefinitionActivation::AfterInitializer)});
  }

  void activateDefinition(size_t inventoryIndex, DefinitionActivation expected,
                          bool recordLocalDuplicate) {
    if (inventoryIndex >= inventory.size() ||
        definitionScopeIndices[inventoryIndex] == kMissingIndex) {
      reject(BinderInvariantKind::InvalidBindingFact, tree.root());
      return;
    }
    const auto& entry = inventory[inventoryIndex];
    auto actual = activationFor(tree, entry);
    if (actual == zc::none || actual != expected) {
      reject(BinderInvariantKind::InvalidBindingFact, entry.node);
      return;
    }
    const uint32_t scopeIndex = definitionScopeIndices[inventoryIndex];
    if (entry.kind == identity::DefinitionKind::Local) {
      publishLocalFact(inventoryIndex, scopeIndex);
      if (rejected != zc::none) { return; }
    }
    auto nameSpace = definitionNamespace(entry.kind);
    if (nameSpace == zc::none) { return; }
    if (entry.bindingName == zc::none) {
      reject(BinderInvariantKind::InvalidBindingFact, entry.node);
      return;
    }
    ZC_IF_SOME(namespaceValue, nameSpace) {
      if (namespaceValue != Namespace::Value && namespaceValue != Namespace::Type) { return; }
      ZC_IF_SOME(name, entry.bindingName) {
        auto& currentBindings = bindingsFor(activeScopes[scopeIndex], namespaceValue);
        auto existing = currentBindings.find(name.text());
        if (existing != zc::none) {
          if (recordLocalDuplicate) {
            ZC_IF_SOME(previousIndex, existing) {
              const auto& previous = inventory[previousIndex];
              skeleton.duplicates.add(BindingDuplicateFact{
                  BinderDiagnosticCode::RedeclareVariable, BinderEmitterSite::BodyBinding,
                  name.clone(), entry.definition, entry.node, previous.node, entry.source.clone(),
                  previous.source.clone()});
            }
          }
          return;
        }

        auto outer = activeDefinition(scopeIndex, namespaceValue, name.text(), false);
        ZC_IF_SOME(outerIndex, outer) {
          shadows.add(ShadowRecord{
              inventoryIndex,
              ShadowTargetFact{entry.definition,
                               BindingTarget::definition(inventory[outerIndex].definition)}});
        }
        if (entry.kind == identity::DefinitionKind::Local) {
          zc::Maybe<identity::SourceSpan> noAlias;
          arena.scopes[scopeIndex].bindings.add(ScopeBindingEntry(
              BindingNameKey(namespaceValue, name.clone()),
              NameBinding(BindingTarget::definition(entry.definition),
                          BindingTarget::definition(entry.definition), namespaceValue,
                          BindingOrigin::LocalDeclaration, entry.source.clone(), zc::mv(noAlias))));
        }
        currentBindings.insert(zc::str(name.text()), inventoryIndex);
      }
    }
  }

  void activateIntroducer(ast::NodeId introducer, DefinitionActivation activation,
                          bool recordLocalDuplicate) {
    if (!tree.contains(introducer) || introducer.value >= definitionsByIntroducer.size()) {
      reject(BinderInvariantKind::InvalidBindingFact, introducer);
      return;
    }
    zc::TreeMap<SourceOrderKey, size_t> order;
    for (const size_t inventoryIndex : definitionsByIntroducer[introducer.value]) {
      auto actual = activationFor(tree, inventory[inventoryIndex]);
      if (actual == zc::none || actual != activation) { continue; }
      const auto& source = inventory[inventoryIndex].source;
      order.insert(SourceOrderKey{source.byteStart(), source.byteEnd(), inventoryIndex},
                   inventoryIndex);
    }
    for (const auto& ordered : order) {
      activateDefinition(ordered.value, activation, recordLocalDuplicate);
      if (rejected != zc::none) { return; }
    }
  }

  void resolveName(ast::NodeId node, uint32_t scopeIndex, Namespace expected,
                   zc::StringPtr sourceName) {
    auto semanticName = identity::SemanticIdentifier::fromSource(sourceName);
    auto source = input.parsedModule().spanFor(tree.node(node).range);
    if (semanticName == zc::none || source == zc::none ||
        (expected != Namespace::Value && expected != Namespace::Type)) {
      reject(BinderInvariantKind::InvalidBindingFact, node);
      return;
    }
    ZC_IF_SOME(name, semanticName) {
      auto resolved = activeDefinition(scopeIndex, expected, name.text());
      ZC_IF_SOME(inventoryIndex, resolved) {
        const auto definition = inventory[inventoryIndex].definition;
        result.nodeBindings.add(BindingResolution{
            node, BindingResolutionValue(BoundNameResolution{
                      BindingTarget::definition(definition), BindingTarget::definition(definition),
                      expected, BindingOrigin::LocalDeclaration})});
        return;
      }
      const Namespace alternate = expected == Namespace::Value ? Namespace::Type : Namespace::Value;
      const BinderDiagnosticCode diagnostic =
          activeDefinition(scopeIndex, alternate, name.text()) == zc::none
              ? BinderDiagnosticCode::UndefinedIdentifier
              : BinderDiagnosticCode::SymbolNamespaceMismatch;
      ZC_IF_SOME(span, source) {
        result.failures.add(BodyBindingFailureFact{diagnostic, node, zc::mv(name), expected,
                                                   zc::mv(span), schemaOrdinals[node.value]});
      }
    }
  }

  void resolveIdentifier(ast::NodeId node, uint32_t scopeIndex, Namespace expected) {
    const auto& syntax = tree.node(node);
    const ast::IdentId identifier(syntax.payload.words[ast::kIdentExprNameWord]);
    resolveName(node, scopeIndex, expected, tree.ident(identifier));
  }

  void resolveIdentifierPath(ast::NodeId node, uint32_t scopeIndex, Namespace expected) {
    const auto& syntax = tree.node(node);
    auto mappedScope = scopeIndexFor(node);
    if (mappedScope == zc::none || mappedScope != scopeIndex) {
      reject(BinderInvariantKind::MalformedScopeGraph, node);
      return;
    }
    ast::IdentList segments;
    if (syntax.kind == ast::SyntaxKind::ModulePath) {
      segments = ast::IdentList{syntax.payload.words[ast::kModulePathSegmentsFirstWord],
                                syntax.payload.words[ast::kModulePathSegmentsSizeWord]};
    } else if (syntax.kind == ast::SyntaxKind::AttributePath) {
      segments = ast::IdentList{syntax.payload.words[ast::kAttributePathSegmentsFirstWord],
                                syntax.payload.words[ast::kAttributePathSegmentsSizeWord]};
    } else {
      reject(BinderInvariantKind::InvalidBindingFact, node);
      return;
    }
    if (!tree.contains(segments)) {
      reject(BinderInvariantKind::InvalidBindingFact, node);
      return;
    }
    const auto names = tree.identList(segments);
    if (names.size() != 1) {
      reject(BinderInvariantKind::MissingRequiredResolution, node);
      return;
    }
    resolveName(node, scopeIndex, expected, tree.ident(names[0]));
  }

  void visitSchemaChildren(ast::NodeId node, uint32_t scopeIndex, Namespace inherited) {
    const auto& syntax = tree.node(node);
    const auto* schema = ast::lookupNodeSchema(syntax.kind);
    if (schema == nullptr) {
      reject(BinderInvariantKind::InvalidBindingFact, node);
      return;
    }
    for (uint32_t index = 0; index < schema->fieldCount; ++index) {
      const auto& field = schema->fields[index];
      const auto nameSpace = childNamespace(field, inherited);
      if (field.storage == ast::NodeSchemaFieldStorage::NodeId) {
        const ast::NodeId child(syntax.payload.words[field.firstWord]);
        if (tree.contains(child)) { visitNode(child, scopeIndex, nameSpace); }
      } else if (field.storage == ast::NodeSchemaFieldStorage::NodeList) {
        const ast::NodeList children{syntax.payload.words[field.firstWord],
                                     syntax.payload.words[field.secondWord]};
        if (!tree.contains(children)) {
          reject(BinderInvariantKind::InvalidBindingFact, node);
          return;
        }
        for (const ast::NodeId child : tree.list(children)) {
          visitNode(child, scopeIndex, nameSpace);
          if (rejected != zc::none) { return; }
        }
      }
      if (rejected != zc::none) { return; }
    }
  }

  void visitNode(ast::NodeId node, uint32_t enclosingScopeIndex, Namespace inherited) {
    if (rejected != zc::none || !tree.contains(node)) { return; }
    auto mappedScope = scopeIndexFor(node);
    if (mappedScope == zc::none) {
      reject(BinderInvariantKind::MalformedScopeGraph, node);
      return;
    }
    ZC_IF_SOME(scopeIndex, mappedScope) {
      if (scopeIndex >= arena.scopes.size()) {
        reject(BinderInvariantKind::MalformedScopeGraph, node);
        return;
      }
      if (scopeIndex != enclosingScopeIndex) {
        const auto& scope = arena.scopes[scopeIndex];
        if (scope.parent == zc::none || scope.parent != arena.scopes[enclosingScopeIndex].id) {
          reject(BinderInvariantKind::MalformedScopeGraph, node);
          return;
        }
      }
      switch (tree.node(node).kind) {
        case ast::SyntaxKind::IdentExpr:
          resolveIdentifier(node, scopeIndex, inherited);
          return;
        case ast::SyntaxKind::ModulePath:
          resolveIdentifierPath(node, scopeIndex, inherited);
          return;
        case ast::SyntaxKind::TypeQueryExpr:
          visitTypeQuery(node, scopeIndex);
          return;
        case ast::SyntaxKind::DynTypeMarkerList:
          visitDynTypeMarkers(node, scopeIndex);
          return;
        case ast::SyntaxKind::ObjectProperty:
          visitObjectProperty(node, scopeIndex);
          return;
        case ast::SyntaxKind::LetStmt:
          visitLet(node, scopeIndex);
          return;
        case ast::SyntaxKind::FunctionParameterDecl:
          reject(BinderInvariantKind::InvalidBindingFact, node);
          return;
        case ast::SyntaxKind::FunctionDecl:
        case ast::SyntaxKind::MethodDecl:
        case ast::SyntaxKind::ConstructorDecl:
        case ast::SyntaxKind::DestructorDecl:
        case ast::SyntaxKind::ExternDecl:
        case ast::SyntaxKind::FunctionExpression:
        case ast::SyntaxKind::LambdaExpression:
          visitCallable(node, scopeIndex);
          return;
        case ast::SyntaxKind::ForInStatement:
          visitForIn(node, scopeIndex);
          return;
        case ast::SyntaxKind::MatchArmStmt:
          visitMatchArm(node, scopeIndex);
          return;
        case ast::SyntaxKind::StructPattern:
          visitStructPattern(node, scopeIndex);
          return;
        case ast::SyntaxKind::MarkerImpl:
          visitMarkerImpl(node, scopeIndex);
          return;
        default:
          visitSchemaChildren(node, scopeIndex, inherited);
          return;
      }
    }
  }

  void visitTypeQuery(ast::NodeId node, uint32_t scopeIndex) {
    const auto& query = tree.node(node);
    const ast::NodeId path(query.payload.words[ast::kTypeQueryExprPathWord]);
    if (!tree.contains(path) || tree.node(path).kind != ast::SyntaxKind::ModulePath) {
      reject(BinderInvariantKind::InvalidBindingFact, node);
      return;
    }
    resolveIdentifierPath(path, scopeIndex, Namespace::Value);
  }

  void visitDynTypeMarkers(ast::NodeId node, uint32_t scopeIndex) {
    const auto& markerList = tree.node(node);
    const ast::NodeList markers{markerList.payload.words[ast::kDynTypeMarkerListMarkersFirstWord],
                                markerList.payload.words[ast::kDynTypeMarkerListMarkersSizeWord]};
    if (!tree.contains(markers) ||
        markerList.payload.words[ast::kDynTypeMarkerListNMarkersWord] != markers.size) {
      reject(BinderInvariantKind::InvalidBindingFact, node);
      return;
    }
    for (const ast::NodeId marker : tree.list(markers)) {
      if (!tree.contains(marker) || tree.node(marker).kind != ast::SyntaxKind::AttributePath) {
        reject(BinderInvariantKind::InvalidBindingFact, marker);
        return;
      }
      resolveIdentifierPath(marker, scopeIndex, Namespace::Type);
      if (rejected != zc::none) { return; }
    }
  }

  void visitObjectProperty(ast::NodeId node, uint32_t scopeIndex) {
    const auto& property = tree.node(node);
    const bool shortForm = property.payload.words[ast::kObjectPropertyShortFormWord] != 0;
    const ast::NodeId value(property.payload.words[ast::kObjectPropertyValueWord]);
    if (shortForm) {
      if (tree.contains(value)) {
        reject(BinderInvariantKind::InvalidBindingFact, node);
        return;
      }
      const ast::IdentId name(property.payload.words[ast::kObjectPropertyNameWord]);
      resolveName(node, scopeIndex, Namespace::Value, tree.ident(name));
      return;
    }
    if (!tree.contains(value)) {
      reject(BinderInvariantKind::InvalidBindingFact, node);
      return;
    }
    visitNode(value, scopeIndex, Namespace::Value);
  }

  void visitLet(ast::NodeId node, uint32_t scopeIndex) {
    const auto& statement = tree.node(node);
    const ast::NodeId declarations(statement.payload.words[ast::kLetStmtDeclarationsWord]);
    if (!tree.contains(declarations) ||
        tree.node(declarations).kind != ast::SyntaxKind::VariableDeclaratorList) {
      reject(BinderInvariantKind::MissingRequiredResolution, node);
      return;
    }
    const auto& list = tree.node(declarations);
    const ast::NodeList declarators{list.payload.words[ast::kVariableDeclaratorListDeclsFirstWord],
                                    list.payload.words[ast::kVariableDeclaratorListDeclsSizeWord]};
    for (const ast::NodeId declarator : tree.list(declarators)) {
      visitVariableDeclarator(declarator, scopeIndex);
      if (rejected != zc::none) { return; }
    }
  }

  void visitVariableDeclarator(ast::NodeId node, uint32_t scopeIndex) {
    if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::VariableDeclarator) {
      reject(BinderInvariantKind::MissingRequiredResolution, node);
      return;
    }
    const auto& declarator = tree.node(node);
    const ast::NodeId typeAnnotation(declarator.payload.words[ast::kVariableDeclaratorTyWord]);
    if (tree.contains(typeAnnotation)) { visitNode(typeAnnotation, scopeIndex, Namespace::Type); }
    const ast::NodeId initializer(declarator.payload.words[ast::kVariableDeclaratorInitWord]);
    if (tree.contains(initializer)) { visitNode(initializer, scopeIndex, Namespace::Value); }
    const ast::NodeId pattern(declarator.payload.words[ast::kVariableDeclaratorPatternWord]);
    if (tree.contains(pattern)) { visitNode(pattern, scopeIndex, Namespace::Value); }
    if (rejected != zc::none) { return; }
    activateIntroducer(node, DefinitionActivation::AfterInitializer, true);
  }

  bool appendParameterList(ast::NodeId listNode, zc::Vector<ast::NodeId>& parameters) {
    if (!tree.contains(listNode) ||
        tree.node(listNode).kind != ast::SyntaxKind::FunctionParameterList) {
      reject(BinderInvariantKind::InvalidBindingFact, listNode);
      return false;
    }
    const auto& list = tree.node(listNode);
    const ast::NodeList values{list.payload.words[ast::kFunctionParameterListParamsFirstWord],
                               list.payload.words[ast::kFunctionParameterListParamsSizeWord]};
    if (!tree.contains(values)) {
      reject(BinderInvariantKind::InvalidBindingFact, listNode);
      return false;
    }
    for (const ast::NodeId parameter : tree.list(values)) { parameters.add(parameter); }
    return true;
  }

  bool appendExternParameters(const ast::Node& callable, zc::Vector<ast::NodeId>& parameters) {
    const ast::NodeList values{callable.payload.words[ast::kExternDeclParamsFirstWord],
                               callable.payload.words[ast::kExternDeclParamsSizeWord]};
    if (!tree.contains(values)) { return false; }
    for (const ast::NodeId parameter : tree.list(values)) { parameters.add(parameter); }
    return true;
  }

  bool validateParameter(ast::NodeId parameter, uint32_t scopeIndex) {
    if (!tree.contains(parameter) ||
        tree.node(parameter).kind != ast::SyntaxKind::FunctionParameterDecl) {
      reject(BinderInvariantKind::InvalidBindingFact, parameter);
      return false;
    }
    auto mappedScope = scopeIndexFor(parameter);
    if (mappedScope == zc::none || mappedScope != scopeIndex) {
      reject(BinderInvariantKind::MalformedScopeGraph, parameter);
      return false;
    }
    return true;
  }

  void visitParameterSignature(ast::NodeId parameter, uint32_t scopeIndex) {
    if (!validateParameter(parameter, scopeIndex)) { return; }
    const auto& syntax = tree.node(parameter);
    const ast::NodeId type(syntax.payload.words[ast::kFunctionParameterDeclTyWord]);
    const ast::NodeId attributes(syntax.payload.words[ast::kFunctionParameterDeclAttrsWord]);
    if (tree.contains(type)) { visitNode(type, scopeIndex, Namespace::Type); }
    if (tree.contains(attributes)) { visitNode(attributes, scopeIndex, Namespace::Attribute); }
  }

  void visitParameterDefaultAndActivate(ast::NodeId parameter, uint32_t scopeIndex) {
    if (!validateParameter(parameter, scopeIndex)) { return; }
    const auto& syntax = tree.node(parameter);
    const ast::NodeId defaultValue(syntax.payload.words[ast::kFunctionParameterDeclDefaultWord]);
    if (tree.contains(defaultValue)) { visitNode(defaultValue, scopeIndex, Namespace::Value); }
    if (rejected != zc::none) { return; }
    activateIntroducer(parameter, DefinitionActivation::ParameterList, false);
  }

  void visitCallable(ast::NodeId node, uint32_t scopeIndex) {
    const auto& callable = tree.node(node);
    ast::NodeId parameterList;
    ast::NodeId genericParameters;
    ast::NodeId captures;
    ast::NodeId returnType;
    ast::NodeId raisesType;
    ast::NodeId body;
    ast::NodeId expressionBody;
    bool isExtern = false;
    bool isClosure = false;
    switch (callable.kind) {
      case ast::SyntaxKind::FunctionDecl:
        parameterList = ast::NodeId(callable.payload.words[ast::kFunctionDeclParamsIdWord]);
        genericParameters = ast::NodeId(callable.payload.words[ast::kFunctionDeclTypeParamsIdWord]);
        returnType = ast::NodeId(callable.payload.words[ast::kFunctionDeclRetTyWord]);
        raisesType = ast::NodeId(callable.payload.words[ast::kFunctionDeclRaisesTyWord]);
        body = ast::NodeId(callable.payload.words[ast::kFunctionDeclBodyWord]);
        break;
      case ast::SyntaxKind::MethodDecl:
        parameterList = ast::NodeId(callable.payload.words[ast::kMethodDeclParamsIdWord]);
        genericParameters = ast::NodeId(callable.payload.words[ast::kMethodDeclTypeParamsIdWord]);
        returnType = ast::NodeId(callable.payload.words[ast::kMethodDeclRetTyWord]);
        raisesType = ast::NodeId(callable.payload.words[ast::kMethodDeclRaisesTyWord]);
        body = ast::NodeId(callable.payload.words[ast::kMethodDeclBodyWord]);
        break;
      case ast::SyntaxKind::ConstructorDecl:
        parameterList = ast::NodeId(callable.payload.words[ast::kConstructorDeclParamsIdWord]);
        raisesType = ast::NodeId(callable.payload.words[ast::kConstructorDeclRaisesTyWord]);
        body = ast::NodeId(callable.payload.words[ast::kConstructorDeclBodyWord]);
        break;
      case ast::SyntaxKind::DestructorDecl:
        parameterList = ast::NodeId(callable.payload.words[ast::kDestructorDeclParamsIdWord]);
        raisesType = ast::NodeId(callable.payload.words[ast::kDestructorDeclRaisesTyWord]);
        body = ast::NodeId(callable.payload.words[ast::kDestructorDeclBodyWord]);
        break;
      case ast::SyntaxKind::ExternDecl:
        isExtern = true;
        returnType = ast::NodeId(callable.payload.words[ast::kExternDeclRetTyWord]);
        raisesType = ast::NodeId(callable.payload.words[ast::kExternDeclRaisesTyWord]);
        break;
      case ast::SyntaxKind::FunctionExpression:
        isClosure = true;
        parameterList = ast::NodeId(callable.payload.words[ast::kFunctionExpressionParamsIdWord]);
        genericParameters =
            ast::NodeId(callable.payload.words[ast::kFunctionExpressionTypeParamsIdWord]);
        captures = ast::NodeId(callable.payload.words[ast::kFunctionExpressionCapturesIdWord]);
        returnType = ast::NodeId(callable.payload.words[ast::kFunctionExpressionRetTyWord]);
        raisesType = ast::NodeId(callable.payload.words[ast::kFunctionExpressionRaisesTyWord]);
        body = ast::NodeId(callable.payload.words[ast::kFunctionExpressionBodyWord]);
        break;
      case ast::SyntaxKind::LambdaExpression:
        isClosure = true;
        parameterList = ast::NodeId(callable.payload.words[ast::kLambdaExpressionParamsIdWord]);
        returnType = ast::NodeId(callable.payload.words[ast::kLambdaExpressionRetTyWord]);
        raisesType = ast::NodeId(callable.payload.words[ast::kLambdaExpressionRaisesTyWord]);
        body = ast::NodeId(callable.payload.words[ast::kLambdaExpressionBodyWord]);
        expressionBody = ast::NodeId(callable.payload.words[ast::kLambdaExpressionExprBodyWord]);
        break;
      default:
        reject(BinderInvariantKind::InvalidBindingFact, node);
        return;
    }

    if (isClosure) {
      activateIntroducer(node, DefinitionActivation::ExpressionIntroduction, false);
      if (rejected != zc::none) { return; }
    }
    if (tree.contains(genericParameters)) {
      visitNode(genericParameters, scopeIndex, Namespace::Type);
    }
    if (tree.contains(captures)) { visitNode(captures, scopeIndex, Namespace::Value); }

    zc::Vector<ast::NodeId> parameters;
    if (isExtern) {
      if (!appendExternParameters(callable, parameters)) {
        reject(BinderInvariantKind::InvalidBindingFact, node);
        return;
      }
    } else if (!appendParameterList(parameterList, parameters)) {
      return;
    }
    for (const ast::NodeId parameter : parameters) {
      visitParameterSignature(parameter, scopeIndex);
      if (rejected != zc::none) { return; }
    }
    if (tree.contains(returnType)) { visitNode(returnType, scopeIndex, Namespace::Type); }
    if (tree.contains(raisesType)) { visitNode(raisesType, scopeIndex, Namespace::Type); }
    if (rejected != zc::none) { return; }
    for (const ast::NodeId parameter : parameters) {
      visitParameterDefaultAndActivate(parameter, scopeIndex);
      if (rejected != zc::none) { return; }
    }
    if (tree.contains(body)) { visitNode(body, scopeIndex, Namespace::Value); }
    if (tree.contains(expressionBody)) { visitNode(expressionBody, scopeIndex, Namespace::Value); }
  }

  void visitStructPattern(ast::NodeId node, uint32_t scopeIndex) {
    const auto& pattern = tree.node(node);
    const ast::NodeId typePath(pattern.payload.words[ast::kStructPatternTyPathWord]);
    const ast::NodeList fields{pattern.payload.words[ast::kStructPatternFieldsFirstWord],
                               pattern.payload.words[ast::kStructPatternFieldsSizeWord]};
    const ast::NodeId rest(pattern.payload.words[ast::kStructPatternRestWord]);
    if (tree.contains(typePath)) { visitNode(typePath, scopeIndex, Namespace::Type); }
    if (!tree.contains(fields)) {
      reject(BinderInvariantKind::InvalidBindingFact, node);
      return;
    }
    for (const ast::NodeId field : tree.list(fields)) {
      visitNode(field, scopeIndex, Namespace::Value);
      if (rejected != zc::none) { return; }
    }
    if (tree.contains(rest)) { visitNode(rest, scopeIndex, Namespace::Value); }
  }

  void visitMarkerImpl(ast::NodeId node, uint32_t scopeIndex) {
    const auto& implementation = tree.node(node);
    const ast::NodeId genericParameters(
        implementation.payload.words[ast::kMarkerImplTypeParamsIdWord]);
    const ast::NodeId markerPath(implementation.payload.words[ast::kMarkerImplMarkerPathWord]);
    const ast::NodeId targetType(implementation.payload.words[ast::kMarkerImplForTyWord]);
    const ast::NodeId whereClause(implementation.payload.words[ast::kMarkerImplWhereWord]);
    if (tree.contains(genericParameters)) {
      visitNode(genericParameters, scopeIndex, Namespace::Type);
    }
    if (!tree.contains(markerPath)) {
      reject(BinderInvariantKind::InvalidBindingFact, node);
      return;
    }
    resolveIdentifierPath(markerPath, scopeIndex, Namespace::Type);
    if (tree.contains(targetType)) { visitNode(targetType, scopeIndex, Namespace::Type); }
    if (tree.contains(whereClause)) { visitNode(whereClause, scopeIndex, Namespace::Type); }
  }

  void visitForIn(ast::NodeId node, uint32_t scopeIndex) {
    const auto& statement = tree.node(node);
    const ast::NodeId expression(statement.payload.words[ast::kForInStatementExpressionWord]);
    const ast::NodeId binding(statement.payload.words[ast::kForInStatementBindingWord]);
    const ast::NodeId body(statement.payload.words[ast::kForInStatementBodyWord]);
    if (tree.contains(expression)) { visitNode(expression, scopeIndex, Namespace::Value); }
    if (tree.contains(binding)) { visitNode(binding, scopeIndex, Namespace::Value); }
    if (rejected != zc::none) { return; }
    activateIntroducer(node, DefinitionActivation::LoopPattern, false);
    if (tree.contains(body)) { visitNode(body, scopeIndex, Namespace::Value); }
  }

  void visitMatchArm(ast::NodeId node, uint32_t scopeIndex) {
    const auto& arm = tree.node(node);
    const ast::NodeId pattern(arm.payload.words[ast::kMatchArmStmtPatternWord]);
    const ast::NodeId guard(arm.payload.words[ast::kMatchArmStmtGuardWord]);
    const ast::NodeId body(arm.payload.words[ast::kMatchArmStmtBodyWord]);
    if (tree.contains(pattern)) { visitNode(pattern, scopeIndex, Namespace::Value); }
    if (rejected != zc::none) { return; }
    activateIntroducer(node, DefinitionActivation::MatchPattern, false);
    if (tree.contains(guard)) { visitNode(guard, scopeIndex, Namespace::Value); }
    if (tree.contains(body)) { visitNode(body, scopeIndex, Namespace::Value); }
  }

  bool finishDefinitions() {
    zc::TreeMap<zc::String, size_t> canonicalInventory;
    for (size_t index = 0; index < inventory.size(); ++index) {
      canonicalInventory.insert(encodedDefinitionKey(inventory[index]), index);
    }
    zc::Vector<DefinitionFact> canonical;
    size_t skeletonIndex = 0;
    for (const auto& ordered : canonicalInventory) {
      const size_t inventoryIndex = ordered.value;
      if (inventory[inventoryIndex].kind == identity::DefinitionKind::Local) {
        if (localFactSlots[inventoryIndex] == kMissingSize ||
            localFactSlots[inventoryIndex] >= localFacts.size()) {
          reject(BinderInvariantKind::MissingRequiredResolution, inventory[inventoryIndex].node);
          return false;
        }
        canonical.add(zc::mv(localFacts[localFactSlots[inventoryIndex]].fact));
        continue;
      }
      if (skeletonIndex >= skeleton.definitions.size() ||
          skeleton.definitions[skeletonIndex].identity != inventory[inventoryIndex].definition) {
        reject(BinderInvariantKind::InvalidBindingFact, inventory[inventoryIndex].node);
        return false;
      }
      canonical.add(zc::mv(skeleton.definitions[skeletonIndex++]));
    }
    if (skeletonIndex != skeleton.definitions.size()) {
      reject(BinderInvariantKind::InvalidBindingFact, tree.root());
      return false;
    }
    skeleton.definitions = zc::mv(canonical);
    return true;
  }

  bool finishNodeBindings() {
    zc::TreeMap<uint32_t, size_t> order;
    for (size_t index = 0; index < result.nodeBindings.size(); ++index) {
      if (!tree.contains(result.nodeBindings[index].node)) {
        reject(BinderInvariantKind::InvalidBindingFact, tree.root());
        return false;
      }
      order.insert(result.nodeBindings[index].node.value, index);
    }
    zc::Vector<BindingResolution> canonical;
    for (const auto& ordered : order) { canonical.add(zc::mv(result.nodeBindings[ordered.value])); }
    result.nodeBindings = zc::mv(canonical);
    return true;
  }

  bool finishShadowTargets() {
    zc::TreeMap<zc::String, size_t> order;
    for (size_t index = 0; index < shadows.size(); ++index) {
      order.insert(encodedDefinitionKey(inventory[shadows[index].inventoryIndex]), index);
    }
    for (const auto& ordered : order) {
      result.shadowTargets.add(zc::mv(shadows[ordered.value].fact));
    }
    return true;
  }

  bool finishScopeBindings() {
    for (auto& scope : arena.scopes) {
      zc::TreeMap<BindingOrderKey, size_t> order;
      for (size_t index = 0; index < scope.bindings.size(); ++index) {
        order.insert(BindingOrderKey(scope.bindings[index].name.nameSpace(),
                                     zc::str(scope.bindings[index].name.name().text())),
                     index);
      }
      zc::Vector<ScopeBindingEntry> canonical;
      for (const auto& ordered : order) { canonical.add(zc::mv(scope.bindings[ordered.value])); }
      scope.bindings = zc::mv(canonical);
    }
    return true;
  }
};

BodyBindingBuildResult BodyBindingBuilder::build(const VerifiedBindingInput& input,
                                                 ScopeArenaCandidate& arena,
                                                 DefinitionSkeletonCandidate& skeleton) {
  return BodyBindingCursor(input, arena, skeleton).run();
}

}  // namespace zomlang::compiler::binder
