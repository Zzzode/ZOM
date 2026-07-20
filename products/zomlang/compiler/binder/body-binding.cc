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

#include "zc/core/debug.h"
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
      return false;
    default:
      ZC_UNREACHABLE;
  }
}

bool isCapturable(identity::DefinitionKind kind) {
  return kind == identity::DefinitionKind::Parameter || kind == identity::DefinitionKind::Local ||
         kind == identity::DefinitionKind::PatternBinding;
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
      return zc::none;
    default:
      ZC_UNREACHABLE;
  }
}

identity::DefinitionKind definitionKindFor(OwnerLocalBindingKind kind) {
  switch (kind) {
    case OwnerLocalBindingKind::CallableParameter:
      return identity::DefinitionKind::Parameter;
    case OwnerLocalBindingKind::GenericParameter:
      return identity::DefinitionKind::TypeParameter;
    case OwnerLocalBindingKind::Local:
      return identity::DefinitionKind::Local;
    case OwnerLocalBindingKind::PatternBinding:
      return identity::DefinitionKind::PatternBinding;
    default:
      ZC_UNREACHABLE;
  }
}

OwnerLocalBindingKind ownerLocalKindFor(identity::DefinitionKind kind) {
  switch (kind) {
    case identity::DefinitionKind::Parameter:
      return OwnerLocalBindingKind::CallableParameter;
    case identity::DefinitionKind::TypeParameter:
      return OwnerLocalBindingKind::GenericParameter;
    case identity::DefinitionKind::Local:
      return OwnerLocalBindingKind::Local;
    case identity::DefinitionKind::PatternBinding:
      return OwnerLocalBindingKind::PatternBinding;
    default:
      ZC_UNREACHABLE;
  }
}

struct BodyIdentityEntry final {
  BodyIdentityEntry(ast::NodeId node, DefinitionSite&& site, identity::DefinitionKind kind,
                    zc::Maybe<identity::DeclaredDefinitionName>&& bindingName,
                    identity::SourceSpan&& source, zc::Maybe<BindingTarget>&& target,
                    zc::Maybe<AnonymousOwnerLocalKey>&& anonymous, zc::String&& canonicalKey)
      : node(node),
        site(zc::mv(site)),
        kind(kind),
        bindingName(zc::mv(bindingName)),
        source(zc::mv(source)),
        target(zc::mv(target)),
        anonymous(zc::mv(anonymous)),
        canonicalKey(zc::mv(canonicalKey)) {}
  BodyIdentityEntry(BodyIdentityEntry&&) noexcept = default;
  BodyIdentityEntry& operator=(BodyIdentityEntry&&) noexcept = default;
  ZC_DISALLOW_COPY(BodyIdentityEntry);

  ast::NodeId node;
  DefinitionSite site;
  identity::DefinitionKind kind;
  zc::Maybe<identity::DeclaredDefinitionName> bindingName;
  identity::SourceSpan source;
  zc::Maybe<BindingTarget> target;
  zc::Maybe<AnonymousOwnerLocalKey> anonymous;
  zc::String canonicalKey;
};

bool hasOwnerLocalTarget(const BodyIdentityEntry& entry) {
  return entry.target != zc::none &&
         ZC_ASSERT_NONNULL(entry.target).value().is<OwnerLocalBindingTarget>();
}

zc::String bodyIdentityKey(uint8_t domain, zc::ArrayPtr<const uint8_t> key) {
  zc::Vector<uint8_t> framed(key.size() + 1);
  framed.add(domain);
  framed.addAll(key);
  return zc::str(framed.asPtr().asChars());
}

zc::Maybe<DefinitionActivation> activationFor(const ast::Tree& tree,
                                              const BodyIdentityEntry& entry) {
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
    default:
      ZC_UNREACHABLE;
  }
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
  size_t receiver = kMissingSize;
};

enum class ClosureCaptureDomain : uint8_t { NotClosure, Inferred, Explicit };

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
  OwnerLocalBindingFact fact;
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

zc::String encodedDefinitionKey(const BodyIdentityEntry& entry) {
  return zc::str(entry.canonicalKey);
}

bool sameTarget(const BindingTarget& left, const BindingTarget& right) {
  const auto& leftValue = left.value();
  const auto& rightValue = right.value();
  if (leftValue.is<DefinitionBindingTarget>()) {
    return rightValue.is<DefinitionBindingTarget>() &&
           leftValue.get<DefinitionBindingTarget>().definition ==
               rightValue.get<DefinitionBindingTarget>().definition;
  }
  if (leftValue.is<GenericParameterBindingTarget>()) {
    return rightValue.is<GenericParameterBindingTarget>() &&
           leftValue.get<GenericParameterBindingTarget>().parameter ==
               rightValue.get<GenericParameterBindingTarget>().parameter;
  }
  if (leftValue.is<CallableParameterBindingTarget>()) {
    return rightValue.is<CallableParameterBindingTarget>() &&
           leftValue.get<CallableParameterBindingTarget>().parameter ==
               rightValue.get<CallableParameterBindingTarget>().parameter;
  }
  if (leftValue.is<OwnerLocalBindingTarget>()) {
    return rightValue.is<OwnerLocalBindingTarget>() &&
           leftValue.get<OwnerLocalBindingTarget>().binding ==
               rightValue.get<OwnerLocalBindingTarget>().binding;
  }
  if (leftValue.is<SemanticImportBindingTarget>()) {
    return rightValue.is<SemanticImportBindingTarget>() &&
           leftValue.get<SemanticImportBindingTarget>().binding ==
               rightValue.get<SemanticImportBindingTarget>().binding;
  }
  return rightValue.is<ModuleBindingTarget>() && leftValue.get<ModuleBindingTarget>().module ==
                                                     rightValue.get<ModuleBindingTarget>().module;
}

Namespace childNamespace(const ast::NodeSchemaFieldEntry& field, Namespace inherited) {
  if (field.castTarget == nullptr) { return inherited; }
  const zc::StringPtr target(field.castTarget);
  if (target == "TypeExpr"_zc || target == "TypeParamDecl"_zc) { return Namespace::Type; }
  if (target == "Expression"_zc || target == "LiteralExpr"_zc) { return Namespace::Value; }
  return inherited;
}

DeferredMemberFact cloneDeferredMemberFact(const DeferredMemberFact& fact) {
  zc::Vector<Namespace> expectedNamespaces;
  for (const auto nameSpace : fact.expectedNamespaces) { expectedNamespaces.add(nameSpace); }
  zc::Vector<ast::NodeId> genericArguments;
  for (const auto argument : fact.genericArguments) { genericArguments.add(argument); }
  return DeferredMemberFact{fact.node,
                            fact.base,
                            fact.member.clone(),
                            zc::mv(expectedNamespaces),
                            zc::mv(genericArguments),
                            fact.source.clone()};
}

}  // namespace

class BodyBindingCursor final {
public:
  BodyBindingCursor(const VerifiedBindingInput& input, ScopeArenaCandidate& arena,
                    DefinitionSkeletonCandidate& skeleton)
      : input(input), tree(input.tree()), arena(arena), skeleton(skeleton) {
    initializeInventory();
  }

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
      if (hasOwnerLocalTarget(inventory[index]) && localFactSlots[index] == kMissingSize) {
        return rejectNow(BinderInvariantKind::MissingRequiredResolution, inventory[index].node);
      }
    }
    if (!finishDefinitions() || !finishNodeBindings() || !finishSelfTypes() ||
        !finishThisBindings() || !finishDeferredMembers() || !finishShadowTargets() ||
        !finishExplicitCaptures() || !finishScopeBindings()) {
      return takeRejection();
    }
    return zc::mv(result);
  }

private:
  const VerifiedBindingInput& input;
  const ast::Tree& tree;
  zc::Vector<BodyIdentityEntry> inventory;
  ScopeArenaCandidate& arena;
  DefinitionSkeletonCandidate& skeleton;
  BodyBindingCandidate result;
  zc::Vector<uint32_t> nodeScopeIndices;
  zc::Vector<uint32_t> schemaOrdinals;
  zc::Vector<ast::NodeId> parentNodes;
  zc::Vector<uint32_t> definitionScopeIndices;
  zc::Vector<size_t> callableDefinitionIndices;
  zc::Vector<ClosureCaptureDomain> closureCaptureDomains;
  zc::Vector<size_t> explicitCaptureRowSlots;
  zc::TreeMap<zc::String, size_t> definitionIndices;
  zc::Vector<zc::Vector<size_t>> definitionsByIntroducer;
  zc::Vector<zc::Vector<size_t>> definitionsByScope;
  zc::Vector<ActiveScopeIndex> activeScopes;
  zc::Vector<LocalFactRecord> localFacts;
  zc::Vector<size_t> localFactSlots;
  zc::Vector<ShadowRecord> shadows;
  zc::Maybe<BinderInvariantFact> rejected;

  void initializeInventory() {
    for (const auto& entry : input.definitions().definitions()) {
      zc::Maybe<identity::DeclaredDefinitionName> name;
      ZC_IF_SOME(value, entry.bindingName) { name = value.clone(); }
      zc::Maybe<BindingTarget> target = BindingTarget::definition(entry.definition);
      zc::Maybe<AnonymousOwnerLocalKey> noAnonymous;
      const auto bytes = entry.key.encode();
      inventory.add(BodyIdentityEntry(entry.node, entry.site.clone(), entry.record.kind(),
                                      zc::mv(name), entry.source.clone(), zc::mv(target),
                                      zc::mv(noAnonymous), bodyIdentityKey(0x01, bytes.asPtr())));
    }
    for (const auto& entry : input.definitions().genericParameters()) {
      zc::Maybe<identity::DeclaredDefinitionName> name = entry.bindingName.clone();
      zc::Maybe<BindingTarget> target = BindingTarget::genericParameter(entry.parameter);
      zc::Maybe<AnonymousOwnerLocalKey> noAnonymous;
      const auto bytes = entry.key.encode();
      inventory.add(BodyIdentityEntry(entry.node, entry.site.clone(),
                                      identity::DefinitionKind::TypeParameter, zc::mv(name),
                                      entry.source.clone(), zc::mv(target), zc::mv(noAnonymous),
                                      bodyIdentityKey(0x02, bytes.asPtr())));
    }
    for (const auto& entry : input.definitions().callableParameters()) {
      zc::Maybe<identity::DeclaredDefinitionName> name;
      ZC_IF_SOME(value, entry.bindingName) { name = value.clone(); }
      zc::Maybe<BindingTarget> target = BindingTarget::callableParameter(entry.parameter);
      zc::Maybe<AnonymousOwnerLocalKey> noAnonymous;
      const auto bytes = entry.key.encode();
      inventory.add(BodyIdentityEntry(entry.node, entry.site.clone(),
                                      identity::DefinitionKind::Parameter, zc::mv(name),
                                      entry.source.clone(), zc::mv(target), zc::mv(noAnonymous),
                                      bodyIdentityKey(0x03, bytes.asPtr())));
    }
    for (const auto& entry : input.definitions().ownerLocalBindings()) {
      zc::Maybe<identity::DeclaredDefinitionName> name = entry.key.name().clone();
      zc::Maybe<BindingTarget> target = BindingTarget::ownerLocal(entry.binding);
      zc::Maybe<AnonymousOwnerLocalKey> noAnonymous;
      const auto bytes = entry.key.encode();
      const auto kind = definitionKindFor(entry.key.kind());
      inventory.add(BodyIdentityEntry(entry.node, entry.site.clone(), kind, zc::mv(name),
                                      entry.source.clone(), zc::mv(target), zc::mv(noAnonymous),
                                      bodyIdentityKey(0x04, bytes.asPtr())));
    }
    for (const auto& entry : input.definitions().anonymousEntities()) {
      zc::Maybe<identity::DeclaredDefinitionName> noName;
      zc::Maybe<BindingTarget> noTarget;
      zc::Maybe<AnonymousOwnerLocalKey> anonymous = entry.key.clone();
      const auto bytes = entry.key.encode();
      inventory.add(BodyIdentityEntry(entry.node, entry.site.clone(),
                                      identity::DefinitionKind::Closure, zc::mv(noName),
                                      entry.source.clone(), zc::mv(noTarget), zc::mv(anonymous),
                                      bodyIdentityKey(0x05, bytes.asPtr())));
    }
  }

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
    parentNodes.resize(tree.nodeCount() + 1);
    definitionsByIntroducer.resize(tree.nodeCount() + 1);
    for (size_t index = 0; index < nodeScopeIndices.size(); ++index) {
      nodeScopeIndices[index] = kMissingIndex;
      schemaOrdinals[index] = kMissingIndex;
      parentNodes[index] = ast::NodeId();
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
      ast::visitChildNodeIds(tree, tree.node(node), [&](ast::NodeId child) {
        if (!tree.contains(child) || child.value >= parentNodes.size() ||
            parentNodes[child.value]) {
          reject(BinderInvariantKind::InvalidBindingFact, child);
          return;
        }
        parentNodes[child.value] = node;
      });
    });
    if (rejected != zc::none || ordinal != tree.nodeCount()) {
      if (rejected == zc::none) { reject(BinderInvariantKind::InvalidEmitterOrdinal, tree.root()); }
      return;
    }

    definitionScopeIndices.resize(inventory.size());
    closureCaptureDomains.resize(inventory.size());
    explicitCaptureRowSlots.resize(inventory.size());
    callableDefinitionIndices.resize(arena.scopes.size());
    localFactSlots.resize(inventory.size());
    for (size_t index = 0; index < callableDefinitionIndices.size(); ++index) {
      callableDefinitionIndices[index] = kMissingSize;
    }
    for (size_t index = 0; index < inventory.size(); ++index) {
      definitionScopeIndices[index] = kMissingIndex;
      closureCaptureDomains[index] = ClosureCaptureDomain::NotClosure;
      explicitCaptureRowSlots[index] = kMissingSize;
      localFactSlots[index] = kMissingSize;
      const auto& entry = inventory[index];
      auto definitionKey = encodedDefinitionKey(entry);
      if (definitionIndices.find(definitionKey) != zc::none) {
        reject(BinderInvariantKind::InvalidBindingFact, entry.node);
        return;
      }
      definitionIndices.insert(zc::mv(definitionKey), index);
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
          if (record.kind == ScopeKind::Function || record.kind == ScopeKind::Closure) {
            const auto& owner = record.owner.value();
            bool ownerMatches = false;
            if (record.kind == ScopeKind::Function && owner.is<DefinitionScopeOwner>() &&
                entry.target != zc::none) {
              const auto& target = ZC_ASSERT_NONNULL(entry.target).value();
              ownerMatches = target.is<DefinitionBindingTarget>() &&
                             target.get<DefinitionBindingTarget>().definition ==
                                 owner.get<DefinitionScopeOwner>().definition;
            } else if (record.kind == ScopeKind::Closure && owner.is<AnonymousScopeOwner>() &&
                       entry.anonymous != zc::none) {
              ownerMatches =
                  ZC_ASSERT_NONNULL(entry.anonymous) == owner.get<AnonymousScopeOwner>().anonymous;
            }
            if (!ownerMatches || callableDefinitionIndices[scopeIndex] != kMissingSize) {
              reject(BinderInvariantKind::MalformedScopeGraph, entry.node);
              return;
            }
            callableDefinitionIndices[scopeIndex] = index;
          }
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

      if (entry.kind == identity::DefinitionKind::Closure) {
        const auto& syntax = tree.node(entry.node);
        if (syntax.kind == ast::SyntaxKind::LambdaExpression) {
          closureCaptureDomains[index] = ClosureCaptureDomain::Inferred;
        } else if (syntax.kind == ast::SyntaxKind::FunctionExpression) {
          const ast::NodeId captures(syntax.payload.words[ast::kFunctionExpressionCapturesIdWord]);
          if (captures && (!tree.contains(captures) ||
                           tree.node(captures).kind != ast::SyntaxKind::CaptureList)) {
            reject(BinderInvariantKind::InvalidBindingFact, entry.node);
            return;
          }
          closureCaptureDomains[index] =
              captures ? ClosureCaptureDomain::Explicit : ClosureCaptureDomain::Inferred;
        } else {
          reject(BinderInvariantKind::InvalidBindingFact, entry.node);
          return;
        }
      }
    }

    for (size_t scopeIndex = 0; scopeIndex < arena.scopes.size(); ++scopeIndex) {
      const auto kind = arena.scopes[scopeIndex].kind;
      if ((kind == ScopeKind::Function || kind == ScopeKind::Closure) &&
          callableDefinitionIndices[scopeIndex] == kMissingSize) {
        reject(BinderInvariantKind::MalformedScopeGraph, tree.root());
        return;
      }
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

  zc::Maybe<const NameBinding&> projectedModuleBinding(Namespace nameSpace,
                                                       zc::StringPtr name) const {
    if (arena.scopes.empty() || arena.scopes[0].kind != ScopeKind::Module) { return zc::none; }
    for (const auto& entry : arena.scopes[0].bindings) {
      if (entry.binding.origin == BindingOrigin::LocalDeclaration ||
          entry.name.nameSpace() != nameSpace || entry.name.name().text() != name) {
        continue;
      }
      return entry.binding;
    }
    return zc::none;
  }

  zc::Maybe<size_t> activeReceiver(uint32_t scopeIndex, bool includeCurrent = true) const {
    uint32_t current = scopeIndex;
    bool first = true;
    while (current < arena.scopes.size()) {
      if ((includeCurrent || !first) && activeScopes[current].receiver != kMissingSize) {
        return activeScopes[current].receiver;
      }
      first = false;
      const auto& scope = arena.scopes[current];
      if (scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Module ||
          scope.parent == zc::none) {
        break;
      }
      ZC_IF_SOME(parent, scope.parent) { current = parent.index(); }
    }
    return zc::none;
  }

  bool isReceiverParameter(const BodyIdentityEntry& entry) const {
    return entry.kind == identity::DefinitionKind::Parameter && tree.contains(entry.node) &&
           tree.node(entry.node).kind == ast::SyntaxKind::FunctionParameterDecl &&
           input.parsedModule().functionParameterNameSpan(entry.node,
                                                          ast::SyntaxKind::ThisKeyword) != zc::none;
  }

  zc::Maybe<size_t> targetIndex(const BindingTarget& target) const {
    for (size_t index = 0; index < inventory.size(); ++index) {
      ZC_IF_SOME(candidate, inventory[index].target) {
        if (sameTarget(candidate, target)) { return index; }
      }
    }
    return zc::none;
  }

  zc::Maybe<size_t> anonymousIndex(const AnonymousOwnerLocalKey& anonymous) const {
    for (size_t index = 0; index < inventory.size(); ++index) {
      ZC_IF_SOME(candidate, inventory[index].anonymous) {
        if (candidate == anonymous) { return index; }
      }
    }
    return zc::none;
  }

  zc::Maybe<const ExplicitClosureCaptureFact&> explicitCaptureRow(
      size_t closureInventoryIndex) const {
    if (closureInventoryIndex >= explicitCaptureRowSlots.size() ||
        closureCaptureDomains[closureInventoryIndex] != ClosureCaptureDomain::Explicit) {
      return zc::none;
    }
    const size_t slot = explicitCaptureRowSlots[closureInventoryIndex];
    if (slot >= result.explicitClosureCaptures.size() ||
        inventory[closureInventoryIndex].anonymous == zc::none ||
        result.explicitClosureCaptures[slot].closure !=
            ZC_ASSERT_NONNULL(inventory[closureInventoryIndex].anonymous)) {
      return zc::none;
    }
    return result.explicitClosureCaptures[slot];
  }

  enum class CaptureAccess : uint8_t { Allowed, Denied, Malformed };

  CaptureAccess captureAccess(uint32_t referenceScope, size_t targetIndex) const {
    if (targetIndex >= inventory.size() || targetIndex >= definitionScopeIndices.size()) {
      return CaptureAccess::Malformed;
    }
    if (!isCapturable(inventory[targetIndex].kind)) { return CaptureAccess::Allowed; }
    const uint32_t targetDeclaringScope = definitionScopeIndices[targetIndex];
    if (targetDeclaringScope >= arena.scopes.size()) { return CaptureAccess::Malformed; }
    uint32_t scopeIndex = referenceScope;
    for (size_t traversed = 0; traversed < arena.scopes.size(); ++traversed) {
      if (scopeIndex >= arena.scopes.size()) { return CaptureAccess::Malformed; }
      if (scopeIndex == targetDeclaringScope) { return CaptureAccess::Allowed; }
      const auto& scope = arena.scopes[scopeIndex];
      if (scope.kind == ScopeKind::Function) { return CaptureAccess::Denied; }
      if (scope.kind == ScopeKind::Closure) {
        if (scopeIndex >= callableDefinitionIndices.size() ||
            callableDefinitionIndices[scopeIndex] == kMissingSize) {
          return CaptureAccess::Malformed;
        }
        const size_t closureIndex = callableDefinitionIndices[scopeIndex];
        if (closureIndex >= closureCaptureDomains.size() ||
            closureCaptureDomains[closureIndex] == ClosureCaptureDomain::NotClosure) {
          return CaptureAccess::Malformed;
        }
        if (closureCaptureDomains[closureIndex] == ClosureCaptureDomain::Explicit) {
          auto row = explicitCaptureRow(closureIndex);
          if (row == zc::none) { return CaptureAccess::Malformed; }
          bool listed = false;
          ZC_IF_SOME(value, row) {
            for (const auto& capture : value.captures) {
              if (inventory[targetIndex].target != zc::none &&
                  sameTarget(capture.target, ZC_ASSERT_NONNULL(inventory[targetIndex].target))) {
                listed = true;
                break;
              }
            }
          }
          if (!listed) { return CaptureAccess::Denied; }
        }
      }
      if (scope.kind == ScopeKind::Module || scope.parent == zc::none) {
        return CaptureAccess::Denied;
      }
      ZC_IF_SOME(parent, scope.parent) { scopeIndex = parent.index(); }
    }
    return CaptureAccess::Malformed;
  }

  void publishLocalFact(size_t inventoryIndex, uint32_t scopeIndex,
                        DefinitionActivation activation) {
    if (inventoryIndex >= localFactSlots.size() || localFactSlots[inventoryIndex] != kMissingSize) {
      reject(BinderInvariantKind::InvalidBindingFact, inventory[inventoryIndex].node);
      return;
    }
    const auto& entry = inventory[inventoryIndex];
    auto nameSpace = definitionNamespace(entry.kind);
    if (!hasOwnerLocalTarget(entry) || entry.bindingName == zc::none || nameSpace == zc::none) {
      reject(BinderInvariantKind::InvalidBindingFact, entry.node);
      return;
    }
    ZC_IF_SOME(namespaceValue, nameSpace) {
      localFactSlots[inventoryIndex] = localFacts.size();
      localFacts.add(LocalFactRecord{OwnerLocalBindingFact{
          ZC_ASSERT_NONNULL(entry.target).value().get<OwnerLocalBindingTarget>().binding,
          entry.site.clone(), ownerLocalKindFor(entry.kind),
          ZC_ASSERT_NONNULL(entry.bindingName).clone(), namespaceValue, arena.scopes[scopeIndex].id,
          entry.source.clone(), activation}});
    }
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
    if (hasOwnerLocalTarget(entry)) {
      publishLocalFact(inventoryIndex, scopeIndex, expected);
      if (rejected != zc::none) { return; }
    }
    if (isReceiverParameter(entry)) {
      if (entry.bindingName != zc::none || expected != DefinitionActivation::ParameterList) {
        reject(BinderInvariantKind::InvalidBindingFact, entry.node);
        return;
      }
      if (activeScopes[scopeIndex].receiver == kMissingSize) {
        activeScopes[scopeIndex].receiver = inventoryIndex;
      }
      return;
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
              auto diagnosticName = identity::DeclaredDefinitionName::fromCanonical(name.text());
              if (diagnosticName == zc::none) {
                reject(BinderInvariantKind::InvalidBindingFact, entry.node);
                return;
              }
              ZC_IF_SOME(nameValue, diagnosticName) {
                skeleton.duplicates.add(BindingDuplicateFact{
                    BinderDiagnosticCode::RedeclareVariable, BinderEmitterSite::BodyBinding,
                    zc::mv(nameValue), ZC_ASSERT_NONNULL(entry.target).clone(), entry.node,
                    previous.node, entry.source.clone(), previous.source.clone()});
              }
            }
          }
          return;
        }

        auto outer = activeDefinition(scopeIndex, namespaceValue, name.text(), false);
        ZC_IF_SOME(outerIndex, outer) {
          if (entry.target == zc::none || inventory[outerIndex].target == zc::none) {
            reject(BinderInvariantKind::InvalidBindingFact, entry.node);
            return;
          }
          shadows.add(ShadowRecord{
              inventoryIndex,
              ShadowTargetFact{ZC_ASSERT_NONNULL(entry.target).clone(),
                               ZC_ASSERT_NONNULL(inventory[outerIndex].target).clone()}});
        }
        if (entry.kind == identity::DefinitionKind::Local ||
            entry.kind == identity::DefinitionKind::PatternBinding) {
          zc::Maybe<identity::SourceSpan> noAlias;
          if (entry.target == zc::none) {
            reject(BinderInvariantKind::InvalidBindingFact, entry.node);
            return;
          }
          arena.scopes[scopeIndex].bindings.add(ScopeBindingEntry(
              BindingNameKey(namespaceValue, name.clone()),
              NameBinding(ZC_ASSERT_NONNULL(entry.target).clone(),
                          ZC_ASSERT_NONNULL(entry.target).clone(), namespaceValue,
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

  void recordLookupFailure(ast::NodeId node, zc::StringPtr sourceName, Namespace expected,
                           const identity::SourceSpan& source, BinderDiagnosticCode diagnostic,
                           BinderEmitterSite emitterSite) {
    auto name = identity::DeclaredDefinitionName::fromCanonical(sourceName);
    if (name == zc::none) {
      reject(BinderInvariantKind::InvalidBindingFact, node);
      return;
    }
    ZC_IF_SOME(nameValue, name) {
      result.failures.add(BodyBindingFailureFact{diagnostic, node, zc::mv(nameValue), expected,
                                                 source.clone(), emitterSite,
                                                 schemaOrdinals[node.value]});
    }
  }

  void resolveNameAt(ast::NodeId node, uint32_t scopeIndex, Namespace expected,
                     zc::StringPtr sourceName, const identity::SourceSpan& source,
                     BinderEmitterSite emitterSite) {
    auto semanticName = identity::SemanticIdentifier::fromSource(sourceName);
    if (semanticName == zc::none || (expected != Namespace::Value && expected != Namespace::Type)) {
      reject(BinderInvariantKind::InvalidBindingFact, node);
      return;
    }
    ZC_IF_SOME(name, semanticName) {
      auto resolved = activeDefinition(scopeIndex, expected, name.text());
      ZC_IF_SOME(inventoryIndex, resolved) {
        const auto access = captureAccess(scopeIndex, inventoryIndex);
        if (access == CaptureAccess::Malformed) {
          reject(BinderInvariantKind::MalformedScopeGraph, node);
          return;
        }
        if (access == CaptureAccess::Denied) {
          recordLookupFailure(node, name.text(), expected, source,
                              BinderDiagnosticCode::UndefinedIdentifier, emitterSite);
          return;
        }
        if (inventory[inventoryIndex].target == zc::none) {
          reject(BinderInvariantKind::InvalidBindingFact, node);
          return;
        }
        const auto& target = ZC_ASSERT_NONNULL(inventory[inventoryIndex].target);
        result.nodeBindings.add(BindingResolution{
            node, BindingResolutionValue(BoundNameResolution{
                      target.clone(), target.clone(), expected, BindingOrigin::LocalDeclaration})});
        return;
      }
      ZC_IF_SOME(binding, projectedModuleBinding(expected, name.text())) {
        result.nodeBindings.add(BindingResolution{
            node, BindingResolutionValue(BoundNameResolution{binding.bindingIdentity.clone(),
                                                             binding.canonicalTarget.clone(),
                                                             expected, binding.origin})});
        return;
      }
      const Namespace alternate = expected == Namespace::Value ? Namespace::Type : Namespace::Value;
      const BinderDiagnosticCode diagnostic =
          activeDefinition(scopeIndex, alternate, name.text()) == zc::none &&
                  projectedModuleBinding(alternate, name.text()) == zc::none
              ? BinderDiagnosticCode::UndefinedIdentifier
              : BinderDiagnosticCode::SymbolNamespaceMismatch;
      recordLookupFailure(node, name.text(), expected, source, diagnostic, emitterSite);
    }
  }

  void resolveName(ast::NodeId node, uint32_t scopeIndex, Namespace expected,
                   zc::StringPtr sourceName) {
    auto source = input.parsedModule().spanFor(tree.node(node).range);
    if (source == zc::none) {
      reject(BinderInvariantKind::InvalidBindingFact, node);
      return;
    }
    ZC_IF_SOME(span, source) {
      resolveNameAt(node, scopeIndex, expected, sourceName, span, BinderEmitterSite::BodyBinding);
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

  void resolveMarkerImplPath(ast::NodeId node, uint32_t scopeIndex) {
    if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::AttributePath) {
      reject(BinderInvariantKind::InvalidBindingFact, node);
      return;
    }
    const auto& syntax = tree.node(node);
    const ast::IdentList segments{syntax.payload.words[ast::kAttributePathSegmentsFirstWord],
                                  syntax.payload.words[ast::kAttributePathSegmentsSizeWord]};
    if (segments.empty() || !tree.contains(segments)) {
      reject(BinderInvariantKind::InvalidBindingFact, node);
      return;
    }
    const auto names = tree.identList(segments);
    if (names.size() == 1) {
      resolveName(node, scopeIndex, Namespace::Type, tree.ident(names[0]));
      return;
    }

    const auto modulePath = input.moduleKey().path();
    if (modulePath.size() + 1 != names.size()) {
      reject(BinderInvariantKind::MissingRequiredResolution, node);
      return;
    }
    for (size_t index = 0; index < modulePath.size(); ++index) {
      if (modulePath[index].text() != tree.ident(names[index])) {
        reject(BinderInvariantKind::MissingRequiredResolution, node);
        return;
      }
    }
    resolveName(node, scopeIndex, Namespace::Type, tree.ident(names[names.size() - 1]));
  }

  bool isContextualSelfRoot(ast::NodeId node) const {
    if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::NamedTypeExpr) {
      return false;
    }
    const auto& type = tree.node(node);
    const ast::NodeId path(type.payload.words[ast::kNamedTypeExprPathWord]);
    if (!tree.contains(path) || tree.node(path).kind != ast::SyntaxKind::ModulePath) {
      return false;
    }
    const auto& pathSyntax = tree.node(path);
    const ast::IdentList segments{pathSyntax.payload.words[ast::kModulePathSegmentsFirstWord],
                                  pathSyntax.payload.words[ast::kModulePathSegmentsSizeWord]};
    if (!tree.contains(segments)) { return false; }
    const auto names = tree.identList(segments);
    return names.size() != 0 && tree.ident(names[0]) == "Self"_zc;
  }

  zc::Maybe<SelfOwner> contextualSelfOwner(ast::NodeId node) const {
    ast::NodeId child = node;
    while (tree.contains(child) && child.value < parentNodes.size()) {
      const ast::NodeId parent = parentNodes[child.value];
      if (!tree.contains(parent)) { break; }
      const auto& syntax = tree.node(parent);
      ast::NodeId body;
      bool nominal = false;
      bool interface = false;
      bool implementation = false;
      switch (syntax.kind) {
        case ast::SyntaxKind::ClassDecl:
          body = ast::NodeId(syntax.payload.words[ast::kClassDeclMembersIdWord]);
          nominal = true;
          break;
        case ast::SyntaxKind::StructDecl:
          body = ast::NodeId(syntax.payload.words[ast::kStructDeclMembersIdWord]);
          nominal = true;
          break;
        case ast::SyntaxKind::EnumDeclaration:
          body = ast::NodeId(syntax.payload.words[ast::kEnumDeclarationVariantsIdWord]);
          nominal = true;
          break;
        case ast::SyntaxKind::ErrorDecl:
          body = ast::NodeId(syntax.payload.words[ast::kErrorDeclMembersIdWord]);
          nominal = true;
          break;
        case ast::SyntaxKind::InterfaceDecl:
          body = ast::NodeId(syntax.payload.words[ast::kInterfaceDeclMembersIdWord]);
          interface = true;
          break;
        case ast::SyntaxKind::StandaloneImplDecl:
          body = ast::NodeId(syntax.payload.words[ast::kStandaloneImplDeclMembersIdWord]);
          implementation = true;
          break;
        default:
          break;
      }
      if (body && child == body) {
        if (implementation) {
          ZC_IF_SOME(owner, input.definitions().implAt(parent)) {
            return SelfOwner(ImplSelfOwner{owner});
          }
          return zc::none;
        }
        ZC_IF_SOME(owner, input.definitions().definitionAt(parent)) {
          if (interface) { return SelfOwner(InterfaceSelfOwner{owner}); }
          if (nominal) { return SelfOwner(NominalSelfOwner{owner}); }
        }
        return zc::none;
      }
      child = parent;
    }
    return zc::none;
  }

  void visitNamedType(ast::NodeId node, uint32_t scopeIndex) {
    if (!isContextualSelfRoot(node)) {
      visitSchemaChildren(node, scopeIndex, Namespace::Type);
      return;
    }
    const auto& syntax = tree.node(node);
    const ast::NodeList arguments{syntax.payload.words[ast::kNamedTypeExprArgsFirstWord],
                                  syntax.payload.words[ast::kNamedTypeExprArgsSizeWord]};
    if (!tree.contains(arguments)) {
      reject(BinderInvariantKind::InvalidBindingFact, node);
      return;
    }
    for (const ast::NodeId argument : tree.list(arguments)) {
      visitNode(argument, scopeIndex, Namespace::Type);
      if (rejected != zc::none) { return; }
    }
    auto source = input.parsedModule().retainedTokenSpan(node, 0, ast::SyntaxKind::Identifier);
    if (source == zc::none) {
      reject(BinderInvariantKind::InvalidBindingFact, node);
      return;
    }
    auto owner = contextualSelfOwner(node);
    ZC_IF_SOME(sourceValue, source) {
      ZC_IF_SOME(ownerValue, owner) {
        result.selfTypes.add(BoundSelfType{node, zc::mv(ownerValue), sourceValue.clone()});
        return;
      }
      recordLookupFailure(node, "Self"_zc, Namespace::Type, sourceValue,
                          BinderDiagnosticCode::ContextualSelfOutsideType,
                          BinderEmitterSite::BodyBinding);
    }
  }

  void visitSchemaChildren(ast::NodeId node, uint32_t scopeIndex, Namespace inherited) {
    const auto& syntax = tree.node(node);
    zc::Maybe<const ast::NodeSchemaEntry&> schema = ast::lookupNodeSchema(syntax.kind);
    if (schema == zc::none) {
      reject(BinderInvariantKind::InvalidBindingFact, node);
      return;
    }
    ZC_IF_SOME(schemaValue, schema) {
      for (uint32_t index = 0; index < schemaValue.fieldCount; ++index) {
        const auto& field = schemaValue.fields[index];
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
        case ast::SyntaxKind::ImportDeclaration:
          return;
        case ast::SyntaxKind::ExportDeclaration: {
          const ast::NodeId declaration(
              tree.node(node).payload.words[ast::kExportDeclarationDeclarationWord]);
          if (tree.contains(declaration)) { visitNode(declaration, scopeIndex, inherited); }
          return;
        }
        case ast::SyntaxKind::ModuleDeclaration:
          if (static_cast<ast::ModuleDeclarationForm>(
                  tree.node(node).payload.words[ast::kModuleDeclarationFormWord]) ==
              ast::ModuleDeclarationForm::Alias) {
            return;
          }
          visitSchemaChildren(node, scopeIndex, inherited);
          return;
        case ast::SyntaxKind::IdentExpr:
          resolveIdentifier(node, scopeIndex, inherited);
          return;
        case ast::SyntaxKind::ThisExpr:
          resolveThis(node, scopeIndex);
          return;
        case ast::SyntaxKind::ModulePath:
          resolveIdentifierPath(node, scopeIndex, inherited);
          return;
        case ast::SyntaxKind::NamedTypeExpr:
          visitNamedType(node, scopeIndex);
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
        case ast::SyntaxKind::CallExpression:
          visitCallExpression(node, scopeIndex);
          return;
        case ast::SyntaxKind::MemberExpression:
          visitMemberExpression(node, scopeIndex, ast::NodeList());
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

  void resolveThis(ast::NodeId node, uint32_t scopeIndex) {
    auto source = input.parsedModule().retainedTokenSpan(node, 0, ast::SyntaxKind::ThisKeyword);
    if (source == zc::none) {
      reject(BinderInvariantKind::InvalidBindingFact, node);
      return;
    }
    ZC_IF_SOME(span, source) {
      auto resolved = activeReceiver(scopeIndex);
      ZC_IF_SOME(inventoryIndex, resolved) {
        const auto access = captureAccess(scopeIndex, inventoryIndex);
        if (access == CaptureAccess::Malformed) {
          reject(BinderInvariantKind::MalformedScopeGraph, node);
          return;
        }
        if (access == CaptureAccess::Allowed) {
          if (inventory[inventoryIndex].target == zc::none ||
              !ZC_ASSERT_NONNULL(inventory[inventoryIndex].target)
                   .value()
                   .is<CallableParameterBindingTarget>()) {
            reject(BinderInvariantKind::InvalidBindingFact, node);
            return;
          }
          const auto target = ZC_ASSERT_NONNULL(inventory[inventoryIndex].target)
                                  .value()
                                  .get<CallableParameterBindingTarget>()
                                  .parameter;
          result.thisBindings.add(BoundThis{node, ThisBinding{target}, span.clone()});
          return;
        }
      }
      recordLookupFailure(node, "this"_zc, Namespace::Value, span,
                          BinderDiagnosticCode::UndefinedIdentifier,
                          BinderEmitterSite::BodyBinding);
    }
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

  void visitCallExpression(ast::NodeId node, uint32_t scopeIndex) {
    const auto& call = tree.node(node);
    const ast::NodeId callee(call.payload.words[ast::kCallExpressionCalleeWord]);
    const ast::NodeList typeArguments{call.payload.words[ast::kCallExpressionTypeArgsFirstWord],
                                      call.payload.words[ast::kCallExpressionTypeArgsSizeWord]};
    const ast::NodeList arguments{call.payload.words[ast::kCallExpressionArgsFirstWord],
                                  call.payload.words[ast::kCallExpressionArgsSizeWord]};
    if (!tree.contains(callee) || !tree.contains(typeArguments) || !tree.contains(arguments)) {
      reject(BinderInvariantKind::InvalidBindingFact, node);
      return;
    }
    if (tree.node(callee).kind == ast::SyntaxKind::MemberExpression) {
      visitMemberExpression(callee, scopeIndex, typeArguments);
    } else {
      visitNode(callee, scopeIndex, Namespace::Value);
    }
    if (rejected != zc::none) { return; }
    for (const ast::NodeId argument : tree.list(typeArguments)) {
      visitNode(argument, scopeIndex, Namespace::Type);
      if (rejected != zc::none) { return; }
    }
    for (const ast::NodeId argument : tree.list(arguments)) {
      visitNode(argument, scopeIndex, Namespace::Value);
      if (rejected != zc::none) { return; }
    }
  }

  void visitMemberExpression(ast::NodeId node, uint32_t scopeIndex,
                             ast::NodeList genericArguments) {
    if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::MemberExpression ||
        !tree.contains(genericArguments)) {
      reject(BinderInvariantKind::InvalidBindingFact, node);
      return;
    }
    const auto& member = tree.node(node);
    switch (static_cast<ast::MemberAccessKind>(
        member.payload.words[ast::kMemberExpressionAccessWord])) {
      case ast::MemberAccessKind::Dot:
      case ast::MemberAccessKind::Optional:
        break;
      case ast::MemberAccessKind::Qualified:
        reject(BinderInvariantKind::MissingRequiredResolution, node);
        return;
      default:
        reject(BinderInvariantKind::InvalidBindingFact, node);
        return;
    }
    const ast::NodeId base(member.payload.words[ast::kMemberExpressionObjectWord]);
    if (!tree.contains(base)) {
      reject(BinderInvariantKind::InvalidBindingFact, node);
      return;
    }
    visitNode(base, scopeIndex, Namespace::Value);
    if (rejected != zc::none) { return; }

    auto name = identity::DeclaredDefinitionName::fromSource(
        tree.ident(ast::IdentId(member.payload.words[ast::kMemberExpressionPropertyWord])));
    auto source = input.parsedModule().spanFor(member.range);
    if (name == zc::none || source == zc::none) {
      reject(BinderInvariantKind::InvalidBindingFact, node);
      return;
    }
    zc::Vector<Namespace> expectedNamespaces;
    expectedNamespaces.add(Namespace::Value);
    zc::Vector<ast::NodeId> arguments;
    for (const ast::NodeId argument : tree.list(genericArguments)) { arguments.add(argument); }
    ZC_IF_SOME(nameValue, name) {
      ZC_IF_SOME(sourceValue, source) {
        DeferredMemberFact fact{node,
                                base,
                                zc::mv(nameValue),
                                zc::mv(expectedNamespaces),
                                zc::mv(arguments),
                                zc::mv(sourceValue)};
        result.deferredMembers.add(cloneDeferredMemberFact(fact));
        result.nodeBindings.add(BindingResolution{node, BindingResolutionValue(zc::mv(fact))});
      }
    }
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
    if (tree.contains(type) &&
        !input.parsedModule().functionParameterHasImplicitSelfType(parameter)) {
      visitNode(type, scopeIndex, Namespace::Type);
    }
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

  zc::Maybe<ExplicitCaptureBindingFact> resolveCaptureItem(ast::NodeId item, uint32_t closureScope,
                                                           uint32_t enclosingScope) {
    if (!tree.contains(item) || tree.node(item).kind != ast::SyntaxKind::CaptureItem ||
        scopeIndexFor(item) != closureScope) {
      reject(BinderInvariantKind::InvalidBindingFact, item);
      return zc::none;
    }
    const auto& syntax = tree.node(item);
    const auto mode =
        static_cast<ast::CaptureMode>(syntax.payload.words[ast::kCaptureItemModeWord]);
    size_t tokenOrdinal = 0;
    ast::SyntaxKind tokenKind = ast::SyntaxKind::Identifier;
    switch (mode) {
      case ast::CaptureMode::ByValue:
        break;
      case ast::CaptureMode::ByRef:
        tokenOrdinal = 1;
        break;
      case ast::CaptureMode::This:
        tokenKind = ast::SyntaxKind::ThisKeyword;
        break;
      default:
        reject(BinderInvariantKind::InvalidBindingFact, item);
        return zc::none;
    }
    const ast::IdentId identifier(syntax.payload.words[ast::kCaptureItemNameWord]);
    auto source = input.parsedModule().retainedTokenSpan(item, tokenOrdinal, tokenKind);
    if (source == zc::none) {
      reject(BinderInvariantKind::InvalidBindingFact, item);
      return zc::none;
    }
    if (mode == ast::CaptureMode::This) {
      ZC_IF_SOME(span, source) {
        auto resolved = activeReceiver(enclosingScope);
        ZC_IF_SOME(inventoryIndex, resolved) {
          const auto access = captureAccess(enclosingScope, inventoryIndex);
          if (access == CaptureAccess::Malformed) {
            reject(BinderInvariantKind::MalformedScopeGraph, item);
            return zc::none;
          }
          if (access == CaptureAccess::Allowed) {
            if (inventory[inventoryIndex].target == zc::none) {
              reject(BinderInvariantKind::InvalidBindingFact, item);
              return zc::none;
            }
            const auto& target = ZC_ASSERT_NONNULL(inventory[inventoryIndex].target);
            result.nodeBindings.add(
                BindingResolution{item, BindingResolutionValue(BoundNameResolution{
                                            target.clone(), target.clone(), Namespace::Value,
                                            BindingOrigin::LocalDeclaration})});
            return ExplicitCaptureBindingFact{item, target.clone(), span.clone()};
          }
        }
        recordLookupFailure(item, "this"_zc, Namespace::Value, span,
                            BinderDiagnosticCode::UndefinedIdentifier,
                            BinderEmitterSite::LabelAndClosure);
      }
      return zc::none;
    }
    auto semanticName = identity::SemanticIdentifier::fromSource(tree.ident(identifier));
    if (semanticName == zc::none) {
      reject(BinderInvariantKind::InvalidBindingFact, item);
      return zc::none;
    }
    ZC_IF_SOME(name, semanticName) {
      ZC_IF_SOME(span, source) {
        auto resolved = activeDefinition(enclosingScope, Namespace::Value, name.text());
        if (resolved != zc::none) {
          const auto inventoryIndex = ZC_ASSERT_NONNULL(resolved);
          if (isCapturable(inventory[inventoryIndex].kind)) {
            const auto access = captureAccess(enclosingScope, inventoryIndex);
            if (access == CaptureAccess::Malformed) {
              reject(BinderInvariantKind::MalformedScopeGraph, item);
              return zc::none;
            }
            if (access == CaptureAccess::Allowed) {
              if (inventory[inventoryIndex].target == zc::none) {
                reject(BinderInvariantKind::InvalidBindingFact, item);
                return zc::none;
              }
              const auto& target = ZC_ASSERT_NONNULL(inventory[inventoryIndex].target);
              result.nodeBindings.add(
                  BindingResolution{item, BindingResolutionValue(BoundNameResolution{
                                              target.clone(), target.clone(), Namespace::Value,
                                              BindingOrigin::LocalDeclaration})});
              return ExplicitCaptureBindingFact{item, target.clone(), span.clone()};
            }
          }
          recordLookupFailure(item, name.text(), Namespace::Value, span,
                              BinderDiagnosticCode::UndefinedIdentifier,
                              BinderEmitterSite::LabelAndClosure);
          return zc::none;
        }
        const BinderDiagnosticCode diagnostic =
            activeDefinition(enclosingScope, Namespace::Type, name.text()) == zc::none
                ? BinderDiagnosticCode::UndefinedIdentifier
                : BinderDiagnosticCode::SymbolNamespaceMismatch;
        recordLookupFailure(item, name.text(), Namespace::Value, span, diagnostic,
                            BinderEmitterSite::LabelAndClosure);
      }
    }
    return zc::none;
  }

  void visitExplicitCaptureList(ast::NodeId callableNode, ast::NodeId listNode,
                                uint32_t closureScope) {
    if (!tree.contains(listNode) || tree.node(listNode).kind != ast::SyntaxKind::CaptureList ||
        scopeIndexFor(listNode) != closureScope || closureScope >= arena.scopes.size() ||
        arena.scopes[closureScope].kind != ScopeKind::Closure ||
        arena.scopes[closureScope].parent == zc::none) {
      reject(BinderInvariantKind::InvalidBindingFact, callableNode);
      return;
    }
    const auto& owner = arena.scopes[closureScope].owner.value();
    if (!owner.is<AnonymousScopeOwner>()) {
      reject(BinderInvariantKind::MalformedScopeGraph, callableNode);
      return;
    }
    const auto& closure = owner.get<AnonymousScopeOwner>().anonymous;
    if (closureScope >= callableDefinitionIndices.size() ||
        callableDefinitionIndices[closureScope] == kMissingSize) {
      reject(BinderInvariantKind::MalformedScopeGraph, callableNode);
      return;
    }
    const size_t closureIndex = callableDefinitionIndices[closureScope];
    if (closureIndex >= inventory.size() || inventory[closureIndex].anonymous == zc::none ||
        ZC_ASSERT_NONNULL(inventory[closureIndex].anonymous) != closure ||
        inventory[closureIndex].node != callableNode ||
        inventory[closureIndex].kind != identity::DefinitionKind::Closure ||
        closureCaptureDomains[closureIndex] != ClosureCaptureDomain::Explicit ||
        explicitCaptureRowSlots[closureIndex] != kMissingSize) {
      reject(BinderInvariantKind::InvalidBindingFact, callableNode);
      return;
    }
    const auto& list = tree.node(listNode);
    const ast::NodeList items{list.payload.words[ast::kCaptureListCapturesFirstWord],
                              list.payload.words[ast::kCaptureListCapturesSizeWord]};
    if (!tree.contains(items) || list.payload.words[ast::kCaptureListNCapturesWord] != items.size) {
      reject(BinderInvariantKind::InvalidBindingFact, listNode);
      return;
    }
    auto source = input.parsedModule().spanFor(list.range);
    if (source == zc::none) {
      reject(BinderInvariantKind::InvalidBindingFact, listNode);
      return;
    }

    zc::Vector<ExplicitCaptureBindingFact> captures;
    const uint32_t enclosingScope = ZC_ASSERT_NONNULL(arena.scopes[closureScope].parent).index();
    for (const auto item : tree.list(items)) {
      auto capture = resolveCaptureItem(item, closureScope, enclosingScope);
      if (rejected != zc::none) { return; }
      ZC_IF_SOME(value, capture) {
        for (const auto& previous : captures) {
          if (!sameTarget(previous.target, value.target)) { continue; }
          const auto& syntax = tree.node(item);
          auto name = identity::DeclaredDefinitionName::fromSource(
              tree.ident(ast::IdentId(syntax.payload.words[ast::kCaptureItemNameWord])));
          if (name == zc::none) {
            reject(BinderInvariantKind::InvalidBindingFact, item);
            return;
          }
          ZC_IF_SOME(nameValue, name) {
            skeleton.duplicates.add(BindingDuplicateFact{
                BinderDiagnosticCode::DuplicateIdentifier, BinderEmitterSite::LabelAndClosure,
                zc::mv(nameValue), value.target.clone(), item, previous.item, value.source.clone(),
                previous.source.clone()});
          }
          break;
        }
        captures.add(zc::mv(value));
      }
    }
    ZC_IF_SOME(span, source) {
      explicitCaptureRowSlots[closureIndex] = result.explicitClosureCaptures.size();
      result.explicitClosureCaptures.add(
          ExplicitClosureCaptureFact{closure.clone(), listNode, span.clone(), zc::mv(captures)});
    }
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
    if (tree.contains(captures)) { visitExplicitCaptureList(node, captures, scopeIndex); }
    if (rejected != zc::none) { return; }

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
    const ast::NodeId markerPath(implementation.payload.words[ast::kMarkerImplMarkerPathWord]);
    const ast::NodeId targetType(implementation.payload.words[ast::kMarkerImplForTyWord]);
    if (!tree.contains(markerPath)) {
      reject(BinderInvariantKind::InvalidBindingFact, node);
      return;
    }
    resolveMarkerImplPath(markerPath, scopeIndex);
    if (tree.contains(targetType)) { visitNode(targetType, scopeIndex, Namespace::Type); }
  }

  void visitForIn(ast::NodeId node, uint32_t scopeIndex) {
    const auto& statement = tree.node(node);
    const ast::NodeId expression(statement.payload.words[ast::kForInStatementExpressionWord]);
    const ast::NodeId binding(statement.payload.words[ast::kForInStatementBindingWord]);
    const ast::NodeId body(statement.payload.words[ast::kForInStatementBodyWord]);
    if (tree.contains(expression)) { visitNode(expression, scopeIndex, Namespace::Value); }
    if (tree.contains(binding)) { visitNode(binding, scopeIndex, Namespace::Value); }
    if (rejected != zc::none) { return; }
    activateIntroducer(node, DefinitionActivation::LoopPattern, true);
    if (tree.contains(body)) { visitNode(body, scopeIndex, Namespace::Value); }
  }

  void visitMatchArm(ast::NodeId node, uint32_t scopeIndex) {
    const auto& arm = tree.node(node);
    const ast::NodeId pattern(arm.payload.words[ast::kMatchArmStmtPatternWord]);
    const ast::NodeId guard(arm.payload.words[ast::kMatchArmStmtGuardWord]);
    const ast::NodeId body(arm.payload.words[ast::kMatchArmStmtBodyWord]);
    if (tree.contains(pattern)) { visitNode(pattern, scopeIndex, Namespace::Value); }
    if (rejected != zc::none) { return; }
    activateIntroducer(node, DefinitionActivation::MatchPattern, true);
    if (tree.contains(guard)) { visitNode(guard, scopeIndex, Namespace::Value); }
    if (tree.contains(body)) { visitNode(body, scopeIndex, Namespace::Value); }
  }

  bool finishDefinitions() {
    zc::TreeMap<zc::String, size_t> canonicalInventory;
    for (size_t index = 0; index < inventory.size(); ++index) {
      if (!hasOwnerLocalTarget(inventory[index])) { continue; }
      canonicalInventory.insert(encodedDefinitionKey(inventory[index]), index);
    }
    zc::Vector<OwnerLocalBindingFact> canonical;
    for (const auto& ordered : canonicalInventory) {
      const size_t inventoryIndex = ordered.value;
      if (localFactSlots[inventoryIndex] == kMissingSize ||
          localFactSlots[inventoryIndex] >= localFacts.size()) {
        reject(BinderInvariantKind::MissingRequiredResolution, inventory[inventoryIndex].node);
        return false;
      }
      canonical.add(zc::mv(localFacts[localFactSlots[inventoryIndex]].fact));
    }
    skeleton.ownerLocalBindings = zc::mv(canonical);
    return true;
  }

  bool finishNodeBindings() {
    zc::TreeMap<uint32_t, size_t> order;
    for (size_t index = 0; index < result.nodeBindings.size(); ++index) {
      if (!tree.contains(result.nodeBindings[index].node) ||
          order.find(result.nodeBindings[index].node.value) != zc::none) {
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

  bool finishSelfTypes() {
    zc::TreeMap<uint32_t, size_t> order;
    for (size_t index = 0; index < result.selfTypes.size(); ++index) {
      const auto node = result.selfTypes[index].syntax;
      if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::NamedTypeExpr ||
          order.find(node.value) != zc::none) {
        reject(BinderInvariantKind::InvalidBindingFact, node);
        return false;
      }
      order.insert(node.value, index);
    }
    zc::Vector<BoundSelfType> canonical;
    for (const auto& ordered : order) { canonical.add(zc::mv(result.selfTypes[ordered.value])); }
    result.selfTypes = zc::mv(canonical);
    return true;
  }

  bool finishThisBindings() {
    zc::TreeMap<uint32_t, size_t> order;
    for (size_t index = 0; index < result.thisBindings.size(); ++index) {
      const auto node = result.thisBindings[index].expression;
      if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::ThisExpr ||
          order.find(node.value) != zc::none) {
        reject(BinderInvariantKind::InvalidBindingFact, node);
        return false;
      }
      order.insert(node.value, index);
    }
    zc::Vector<BoundThis> canonical;
    for (const auto& ordered : order) { canonical.add(zc::mv(result.thisBindings[ordered.value])); }
    result.thisBindings = zc::mv(canonical);
    return true;
  }

  bool finishDeferredMembers() {
    zc::TreeMap<uint32_t, size_t> order;
    for (size_t index = 0; index < result.deferredMembers.size(); ++index) {
      const auto node = result.deferredMembers[index].node;
      if (!tree.contains(node) || order.find(node.value) != zc::none) {
        reject(BinderInvariantKind::InvalidBindingFact, tree.root());
        return false;
      }
      order.insert(node.value, index);
    }
    zc::Vector<DeferredMemberFact> canonical;
    for (const auto& ordered : order) {
      canonical.add(zc::mv(result.deferredMembers[ordered.value]));
    }
    result.deferredMembers = zc::mv(canonical);
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

  bool finishExplicitCaptures() {
    for (size_t index = 0; index < inventory.size(); ++index) {
      const bool explicitClosure = closureCaptureDomains[index] == ClosureCaptureDomain::Explicit;
      const bool hasRow = explicitCaptureRowSlots[index] != kMissingSize;
      if (explicitClosure == hasRow) { continue; }
      reject(explicitClosure ? BinderInvariantKind::MissingRequiredResolution
                             : BinderInvariantKind::InvalidBindingFact,
             inventory[index].node);
      return false;
    }
    zc::TreeMap<zc::String, size_t> order;
    for (size_t index = 0; index < result.explicitClosureCaptures.size(); ++index) {
      const auto& row = result.explicitClosureCaptures[index];
      auto inventoryIndex = anonymousIndex(row.closure);
      if (inventoryIndex == zc::none ||
          inventory[ZC_ASSERT_NONNULL(inventoryIndex)].kind != identity::DefinitionKind::Closure ||
          closureCaptureDomains[ZC_ASSERT_NONNULL(inventoryIndex)] !=
              ClosureCaptureDomain::Explicit ||
          explicitCaptureRowSlots[ZC_ASSERT_NONNULL(inventoryIndex)] != index) {
        reject(BinderInvariantKind::InvalidBindingFact, row.captureList);
        return false;
      }
      auto key = encodedDefinitionKey(inventory[ZC_ASSERT_NONNULL(inventoryIndex)]);
      if (order.find(key) != zc::none) {
        reject(BinderInvariantKind::InvalidBindingFact, row.captureList);
        return false;
      }
      order.insert(zc::mv(key), index);
    }
    zc::Vector<ExplicitClosureCaptureFact> canonical;
    for (const auto& ordered : order) {
      canonical.add(zc::mv(result.explicitClosureCaptures[ordered.value]));
    }
    result.explicitClosureCaptures = zc::mv(canonical);
    for (auto& slot : explicitCaptureRowSlots) { slot = kMissingSize; }
    for (size_t index = 0; index < result.explicitClosureCaptures.size(); ++index) {
      const auto& row = result.explicitClosureCaptures[index];
      auto inventoryIndex = anonymousIndex(row.closure);
      if (inventoryIndex == zc::none ||
          explicitCaptureRowSlots[ZC_ASSERT_NONNULL(inventoryIndex)] != kMissingSize) {
        reject(BinderInvariantKind::InvalidBindingFact, row.captureList);
        return false;
      }
      explicitCaptureRowSlots[ZC_ASSERT_NONNULL(inventoryIndex)] = index;
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
