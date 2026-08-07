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

#include "zomlang/compiler/binder/owner-body-query.h"

#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-schema.h"
#include "zomlang/compiler/binder/stable-binding-codec.h"
#include "zomlang/compiler/identity/canonical-decoder.h"

namespace zomlang::compiler::binder {

namespace owner_body_query_detail {

struct OwnerBodySyntaxTraversalData final {
  zc::Vector<OwnerBodySyntaxPathEntry> entries;
};

struct OwnerBodyScopeProjectionData final {
  CanonicalSequence<StableBodyScopeFact> scopes;
  CanonicalSequence<StableBodyNodeScopeFact> nodeScopes;
};

struct OwnerBodyBindingProjectionData final {
  CanonicalSequence<StableOwnerLocalBindingFact> bindings;
};

struct OwnerBodyShadowProjectionData final {
  CanonicalSequence<StableShadowTargetFact> shadows;
};

struct OwnerBodyLookupProjectionData final {
  CanonicalSequence<StableResolutionFact> resolutions;
  CanonicalSequence<StableFailedLookupFact> failedLookups;
};

struct OwnerBodySelfTypeProjectionData final {
  CanonicalSequence<StableSelfTypeFact> selfTypes;
};

struct OwnerBodyReceiverProjectionData final {
  CanonicalSequence<StableThisBindingFact> bindings;
};

struct OwnerBodyDeferredMemberProjectionData final {
  CanonicalSequence<StableDeferredMemberFact> deferredMembers;
};

struct OwnerBodyClosureProjectionData final {
  CanonicalSequence<StableClosureFact> closures;
};

struct OwnerBodyFreeVariableProjectionData final {
  CanonicalSequence<StableClosureFreeVariableFact> freeVariables;
};

struct OwnerBodyExplicitCaptureProjectionData final {
  CanonicalSequence<StableExplicitClosureCaptureFact> captures;
};

struct OwnerBodyLabelProjectionData final {
  CanonicalSequence<StableLabelFact> labels;
};

struct OwnerBodyControlProjectionData final {
  CanonicalSequence<StableControlTransferFact> transfers;
};

}  // namespace owner_body_query_detail

namespace {

constexpr uint32_t kNoParent = UINT32_MAX;

struct PendingChildren final {
  uint32_t nodeIndex;
  uint32_t nextChild;
  uint32_t childCount;
  uint32_t rootIndex;
};

struct VerifierPendingNode final {
  LocalSyntaxPath path;
  StableScopeOwnerKey scope;
  uint32_t childCount;
  uint32_t nextChild;
};

struct VerifierBindingPendingNode final {
  LocalSyntaxPath path;
  ast::SyntaxKind syntaxKind;
  bool hasVariableDeclaratorAncestor;
  uint32_t childCount;
  uint32_t nextChild;
};

struct LookupCandidate final {
  StableBindingTargetKey binding;
  StableBindingTargetKey canonicalTarget;
  BindingOrigin origin;
};

struct LookupCandidates final {
  zc::Vector<LookupCandidate> values;
};

struct OwnerBodyLookupFacts final {
  CanonicalSequence<StableResolutionFact> resolutions;
  CanonicalSequence<StableFailedLookupFact> failedLookups;
};

struct VerifierClosurePendingNode final {
  LocalSyntaxPath path;
  uint32_t childCount;
  uint32_t nextChild;
};

struct PendingFreeVariable final {
  StableBindingTargetKey target;
  zc::Vector<LocalSyntaxPath> referencePaths;
};

struct PendingClosureFreeVariables final {
  AnonymousOwnerLocalKey closure;
  zc::Vector<PendingFreeVariable> variables;
};

struct VerifierLabelNode final {
  LocalSyntaxPath path;
  uint32_t nodeIndex;
  ast::SyntaxKind syntaxKind;
};

struct VerifierExplicitCaptureNode final {
  LocalSyntaxPath path;
  uint32_t nodeIndex;
  ast::SyntaxKind syntaxKind;
};

struct VerifierControlPendingNode final {
  LocalSyntaxPath path;
  uint32_t nodeIndex;
  uint32_t childCount;
  uint32_t nextChild;
};

struct VerifierControlNode final {
  LocalSyntaxPath path;
  uint32_t nodeIndex;
  uint32_t parentIndex;
  ast::SyntaxKind syntaxKind;
};

bool completeTop(zc::Vector<PendingChildren>& pending) {
  while (!pending.empty() && pending.back().nextChild == pending.back().childCount) {
    pending.removeLast();
  }
  return pending.empty() || pending.back().nextChild < pending.back().childCount;
}

zc::Maybe<LocalSyntaxPath> providerRootPath(uint32_t rootIndex) {
  zc::Vector<uint32_t> components;
  components.add(rootIndex);
  return LocalSyntaxPath::from(zc::mv(components));
}

zc::Maybe<LocalSyntaxPath> providerChildPath(const LocalSyntaxPath& parent, uint32_t childIndex) {
  zc::Vector<uint32_t> components(parent.components().size() + 1);
  components.addAll(parent.components());
  components.add(childIndex);
  return LocalSyntaxPath::from(zc::mv(components));
}

zc::Maybe<LocalSyntaxPath> verifierRootPath(uint32_t rootIndex) {
  zc::Vector<uint32_t> components(1);
  components.add(rootIndex);
  return LocalSyntaxPath::from(zc::mv(components));
}

zc::Maybe<LocalSyntaxPath> verifierChildPath(const LocalSyntaxPath& parent, uint32_t childIndex) {
  zc::Vector<uint32_t> components;
  for (const auto component : parent.components()) { components.add(component); }
  components.add(childIndex);
  return LocalSyntaxPath::from(zc::mv(components));
}

zc::Maybe<MemberAccessKind> memberAccessKind(const DetachedModuleBodyNode& node) {
  if (node.kind() != DetachedModuleBodyNodeKind::Syntax ||
      node.syntaxKind() != ast::SyntaxKind::MemberExpression) {
    return zc::none;
  }
  identity::CanonicalDecoder decoder(node.canonicalPayload());
  auto fields = decoder.decodeSequenceSize(3);
  auto objectStorage = decoder.decodeUint8();
  auto objectOptional = decoder.decodeBool();
  auto objectPresent = decoder.decodeBool();
  auto propertyStorage = decoder.decodeUint8();
  auto propertyOptional = decoder.decodeBool();
  auto propertyPresent = decoder.decodeBool();
  auto property = propertyPresent == zc::none || !ZC_ASSERT_NONNULL(propertyPresent)
                      ? zc::Maybe<zc::Array<uint8_t>>()
                      : decoder.decodeByteString(64 * 1024);
  auto accessStorage = decoder.decodeUint8();
  auto accessOptional = decoder.decodeBool();
  auto access = decoder.decodeUint32();
  if (fields == zc::none || ZC_ASSERT_NONNULL(fields) != 3 || objectStorage == zc::none ||
      objectOptional == zc::none || objectPresent == zc::none || propertyStorage == zc::none ||
      propertyOptional == zc::none || propertyPresent == zc::none || property == zc::none ||
      accessStorage == zc::none || accessOptional == zc::none || access == zc::none ||
      ZC_ASSERT_NONNULL(objectStorage) !=
          static_cast<uint8_t>(ast::NodeSchemaFieldStorage::NodeId) + 1 ||
      ZC_ASSERT_NONNULL(objectOptional) || !ZC_ASSERT_NONNULL(objectPresent) ||
      ZC_ASSERT_NONNULL(propertyStorage) !=
          static_cast<uint8_t>(ast::NodeSchemaFieldStorage::IdentId) + 1 ||
      ZC_ASSERT_NONNULL(propertyOptional) ||
      ZC_ASSERT_NONNULL(accessStorage) !=
          static_cast<uint8_t>(ast::NodeSchemaFieldStorage::Enum) + 1 ||
      ZC_ASSERT_NONNULL(accessOptional) || !decoder.finished()) {
    return zc::none;
  }
  switch (ZC_ASSERT_NONNULL(access)) {
    case 0:
      return MemberAccessKind::Dot;
    case 1:
      return MemberAccessKind::Optional;
    case 2:
      return MemberAccessKind::Qualified;
    default:
      return zc::none;
  }
}

zc::Maybe<StableExplicitCaptureMode> explicitCaptureMode(const DetachedModuleBodyNode& node) {
  if (node.kind() != DetachedModuleBodyNodeKind::Syntax ||
      node.syntaxKind() != ast::SyntaxKind::CaptureItem) {
    return zc::none;
  }
  identity::CanonicalDecoder decoder(node.canonicalPayload());
  auto fields = decoder.decodeSequenceSize(2);
  auto modeStorage = decoder.decodeUint8();
  auto modeOptional = decoder.decodeBool();
  auto mode = decoder.decodeUint32();
  auto nameStorage = decoder.decodeUint8();
  auto nameOptional = decoder.decodeBool();
  auto namePresent = decoder.decodeBool();
  auto name = namePresent == zc::none || !ZC_ASSERT_NONNULL(namePresent)
                  ? zc::Maybe<zc::Array<uint8_t>>()
                  : decoder.decodeByteString(64 * 1024);
  if (fields == zc::none || ZC_ASSERT_NONNULL(fields) != 2 || modeStorage == zc::none ||
      modeOptional == zc::none || mode == zc::none || nameStorage == zc::none ||
      nameOptional == zc::none || namePresent == zc::none || name == zc::none ||
      ZC_ASSERT_NONNULL(modeStorage) !=
          static_cast<uint8_t>(ast::NodeSchemaFieldStorage::Enum) + 1 ||
      ZC_ASSERT_NONNULL(modeOptional) ||
      ZC_ASSERT_NONNULL(nameStorage) !=
          static_cast<uint8_t>(ast::NodeSchemaFieldStorage::IdentId) + 1 ||
      ZC_ASSERT_NONNULL(nameOptional) || !ZC_ASSERT_NONNULL(namePresent) || !decoder.finished()) {
    return zc::none;
  }
  switch (ZC_ASSERT_NONNULL(mode)) {
    case 0:
      return StableExplicitCaptureMode::ByValue;
    case 1:
      return StableExplicitCaptureMode::ByReference;
    case 2:
      return StableExplicitCaptureMode::This;
    default:
      return zc::none;
  }
}

zc::Maybe<ScopeKind> providerScopeKind(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::FunctionDecl:
    case ast::SyntaxKind::ExternDecl:
    case ast::SyntaxKind::MethodDecl:
    case ast::SyntaxKind::ConstructorDecl:
    case ast::SyntaxKind::DestructorDecl:
      return ScopeKind::Function;
    case ast::SyntaxKind::FunctionExpression:
    case ast::SyntaxKind::LambdaExpression:
      return ScopeKind::Closure;
    case ast::SyntaxKind::ClassDecl:
    case ast::SyntaxKind::StructDecl:
    case ast::SyntaxKind::InterfaceDecl:
    case ast::SyntaxKind::EnumDeclaration:
    case ast::SyntaxKind::ErrorDecl:
      return ScopeKind::TypeBody;
    case ast::SyntaxKind::StandaloneImplDecl:
    case ast::SyntaxKind::MarkerImpl:
      return ScopeKind::ImplBody;
    case ast::SyntaxKind::BlockStmt:
      return ScopeKind::Block;
    case ast::SyntaxKind::WhileStmt:
    case ast::SyntaxKind::ForStmt:
    case ast::SyntaxKind::ForInStatement:
    case ast::SyntaxKind::DoWhileStatement:
      return ScopeKind::Loop;
    case ast::SyntaxKind::MatchStmt:
      return ScopeKind::Match;
    case ast::SyntaxKind::MatchArmStmt:
      return ScopeKind::MatchArm;
    case ast::SyntaxKind::UnsafeBlockExpr:
      return ScopeKind::UnsafeBlock;
    default:
      return zc::none;
  }
}

zc::Maybe<ScopeKind> verifierScopeKind(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::FunctionDecl:
    case ast::SyntaxKind::ExternDecl:
    case ast::SyntaxKind::MethodDecl:
    case ast::SyntaxKind::ConstructorDecl:
    case ast::SyntaxKind::DestructorDecl:
      return ScopeKind::Function;
    case ast::SyntaxKind::FunctionExpression:
    case ast::SyntaxKind::LambdaExpression:
      return ScopeKind::Closure;
    case ast::SyntaxKind::ClassDecl:
    case ast::SyntaxKind::StructDecl:
    case ast::SyntaxKind::InterfaceDecl:
    case ast::SyntaxKind::EnumDeclaration:
    case ast::SyntaxKind::ErrorDecl:
      return ScopeKind::TypeBody;
    case ast::SyntaxKind::StandaloneImplDecl:
    case ast::SyntaxKind::MarkerImpl:
      return ScopeKind::ImplBody;
    case ast::SyntaxKind::BlockStmt:
      return ScopeKind::Block;
    case ast::SyntaxKind::WhileStmt:
    case ast::SyntaxKind::ForStmt:
    case ast::SyntaxKind::ForInStatement:
    case ast::SyntaxKind::DoWhileStatement:
      return ScopeKind::Loop;
    case ast::SyntaxKind::MatchStmt:
      return ScopeKind::Match;
    case ast::SyntaxKind::MatchArmStmt:
      return ScopeKind::MatchArm;
    case ast::SyntaxKind::UnsafeBlockExpr:
      return ScopeKind::UnsafeBlock;
    default:
      return zc::none;
  }
}

bool sameOwnerModule(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

bool providerOwnsRootScope(const StableOwnerBodyQueryKey& owner,
                           const StableScopeOwnerKey& rootScope) {
  const auto& scope = rootScope.value();
  if (owner.owner().kind() == StableBodyOwnerKind::Module) {
    return scope.is<StableModuleScope>() &&
           sameOwnerModule(scope.get<StableModuleScope>().module, owner.module());
  }
  if (!scope.is<StableDefinitionScope>()) { return false; }
  auto definition = owner.owner().definitionKey();
  return definition != zc::none &&
         sameOwnerModule(scope.get<StableDefinitionScope>().definition.module(), owner.module()) &&
         scope.get<StableDefinitionScope>().definition.definition() ==
             ZC_ASSERT_NONNULL(definition);
}

bool verifierOwnsRootScope(const StableOwnerBodyQueryKey& owner,
                           const StableScopeOwnerKey& rootScope) {
  const auto& scopeValue = rootScope.value();
  switch (owner.owner().kind()) {
    case StableBodyOwnerKind::Module:
      return scopeValue.is<StableModuleScope>() &&
             sameOwnerModule(scopeValue.get<StableModuleScope>().module, owner.module());
    case StableBodyOwnerKind::Definition: {
      if (!scopeValue.is<StableDefinitionScope>()) { return false; }
      auto definition = owner.owner().definitionKey();
      if (definition == zc::none) { return false; }
      const auto& definitionScope = scopeValue.get<StableDefinitionScope>().definition;
      return sameOwnerModule(definitionScope.module(), owner.module()) &&
             definitionScope.definition() == ZC_ASSERT_NONNULL(definition);
    }
  }
  ZC_UNREACHABLE;
}

template <typename T>
void sortProviderCanonical(zc::Vector<T>& values) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    const auto currentBytes = StableBindingCodec<T>::encode(current);
    size_t insertion = index;
    while (insertion != 0) {
      const auto previousBytes = StableBindingCodec<T>::encode(values[insertion - 1]);
      if (previousBytes.asPtr() < currentBytes.asPtr()) { break; }
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
}

template <typename T>
void sortVerifierCanonical(zc::Vector<T>& values) {
  for (size_t tail = values.size(); tail > 1; --tail) {
    for (size_t index = 1; index < tail; ++index) {
      const auto previous = StableBindingCodec<T>::encode(values[index - 1]);
      const auto current = StableBindingCodec<T>::encode(values[index]);
      if (previous.asPtr() <= current.asPtr()) { continue; }
      auto displaced = zc::mv(values[index - 1]);
      values[index - 1] = zc::mv(values[index]);
      values[index] = zc::mv(displaced);
    }
  }
}

bool sameModule(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

bool hasExactOwner(const StableOwnerBodyQueryKey& owner, const BoundModuleSkeleton& skeleton) {
  size_t matches = 0;
  for (const auto& candidate : skeleton.bodyOwners().values()) {
    if (candidate == owner) { ++matches; }
  }
  return matches == 1;
}

zc::Maybe<StableScopeOwnerKey> providerNodeScope(
    const LocalSyntaxPath& path, const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes) {
  zc::Maybe<StableScopeOwnerKey> result;
  for (const auto& fact : nodeScopes.values()) {
    if (fact.nodePath() != path) { continue; }
    if (result != zc::none) { return zc::none; }
    result = fact.scope().clone();
  }
  return result;
}

zc::Maybe<StableScopeOwnerKey> verifierNodeScope(
    const LocalSyntaxPath& path, const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes) {
  for (size_t index = nodeScopes.values().size(); index != 0; --index) {
    const auto& fact = nodeScopes.values()[index - 1];
    if (fact.nodePath() == path) { return fact.scope().clone(); }
  }
  return zc::none;
}

bool isVariableBindingPattern(const OwnerBodySyntaxPathEntry& entry,
                              zc::ArrayPtr<const OwnerBodySyntaxPathEntry> entries) {
  if (entry.kind != DetachedModuleBodyNodeKind::Syntax ||
      (entry.syntaxKind != ast::SyntaxKind::BindingPattern &&
       entry.syntaxKind != ast::SyntaxKind::IdentifierPattern)) {
    return false;
  }
  uint32_t ancestorIndex = entry.parentIndex;
  while (ancestorIndex != kNoParent) {
    if (ancestorIndex >= entries.size()) { return false; }
    const auto& ancestor = entries[ancestorIndex];
    if (ancestor.kind == DetachedModuleBodyNodeKind::Syntax &&
        ancestor.syntaxKind == ast::SyntaxKind::VariableDeclarator) {
      return true;
    }
    ancestorIndex = ancestor.parentIndex;
  }
  return false;
}

bool isVerifierVariableBindingPattern(const DetachedModuleBodyNode& node,
                                      bool hasVariableDeclaratorAncestor) {
  return hasVariableDeclaratorAncestor && (node.syntaxKind() == ast::SyntaxKind::BindingPattern ||
                                           node.syntaxKind() == ast::SyntaxKind::IdentifierPattern);
}

bool pathHasPrefix(const LocalSyntaxPath& path, const LocalSyntaxPath& prefix) {
  if (prefix.components().size() > path.components().size()) { return false; }
  for (size_t index = 0; index < prefix.components().size(); ++index) {
    if (path.components()[index] != prefix.components()[index]) { return false; }
  }
  return true;
}

zc::Maybe<size_t> entryAtPath(zc::ArrayPtr<const OwnerBodySyntaxPathEntry> entries,
                              const LocalSyntaxPath& path) {
  for (size_t index = 0; index < entries.size(); ++index) {
    if (entries[index].path == path) { return index; }
  }
  return zc::none;
}

zc::Maybe<size_t> variableDeclaratorEnd(zc::ArrayPtr<const OwnerBodySyntaxPathEntry> entries,
                                        const LocalSyntaxPath& bindingPath) {
  auto binding = entryAtPath(entries, bindingPath);
  if (binding == zc::none) { return zc::none; }
  size_t declarator = ZC_ASSERT_NONNULL(binding);
  while (entries[declarator].parentIndex != kNoParent) {
    declarator = entries[declarator].parentIndex;
    if (declarator >= entries.size()) { return zc::none; }
    if (entries[declarator].syntaxKind == ast::SyntaxKind::VariableDeclarator) { break; }
  }
  if (entries[declarator].syntaxKind != ast::SyntaxKind::VariableDeclarator) { return zc::none; }
  size_t end = declarator + 1;
  while (end < entries.size() && pathHasPrefix(entries[end].path, entries[declarator].path)) {
    ++end;
  }
  return end;
}

zc::Maybe<StableScopeOwnerKey> scopeParent(
    const StableScopeOwnerKey& scope, const BoundModuleSkeleton& skeleton,
    const CanonicalSequence<StableBodyScopeFact>& bodyScopes) {
  zc::Maybe<StableScopeOwnerKey> parent;
  for (const auto& bodyScope : bodyScopes.values()) {
    if (bodyScope.scope() != scope) { continue; }
    if (parent != zc::none) { return zc::none; }
    parent = bodyScope.parent().clone();
  }
  for (const auto& skeletonScope : skeleton.scopes().values()) {
    if (skeletonScope.owner() != scope) { continue; }
    if (parent != zc::none || skeletonScope.parent() == zc::none) { return zc::none; }
    parent = ZC_ASSERT_NONNULL(skeletonScope.parent()).clone();
  }
  return parent;
}

bool appendLocalCandidates(const StableOwnerBodyQueryKey& owner, const StableScopeOwnerKey& scope,
                           const identity::DeclaredDefinitionName& name,
                           zc::ArrayPtr<const OwnerBodySyntaxPathEntry> entries, size_t useIndex,
                           const CanonicalSequence<StableOwnerLocalBindingFact>& bindings,
                           zc::Vector<LookupCandidate>& candidates) {
  for (const auto& binding : bindings.values()) {
    if (binding.declaringScope() != scope || binding.nameSpace() != Namespace::Value ||
        binding.name() != name) {
      continue;
    }
    auto activationEnd = variableDeclaratorEnd(entries, binding.key().path());
    if (activationEnd == zc::none) { return false; }
    if (useIndex < ZC_ASSERT_NONNULL(activationEnd)) { continue; }
    auto target = StableBindingTargetKey::ownerLocal(owner.clone(), binding.key().clone());
    if (target == zc::none) { return false; }
    candidates.add(LookupCandidate{ZC_ASSERT_NONNULL(target).clone(),
                                   zc::mv(ZC_ASSERT_NONNULL(target)),
                                   BindingOrigin::LocalDeclaration});
  }
  return true;
}

void appendSkeletonCandidates(const StableScopeOwnerKey& scope,
                              const identity::DeclaredDefinitionName& name, Namespace nameSpace,
                              const BoundModuleSkeleton& skeleton,
                              zc::Vector<LookupCandidate>& candidates) {
  for (const auto& declaration : skeleton.declarations().values()) {
    if (declaration.declaringScope() != scope || declaration.nameSpace() != nameSpace ||
        declaration.name() != name ||
        (declaration.activation() != DefinitionActivation::ModuleSkeleton &&
         declaration.activation() != DefinitionActivation::ImportSurface)) {
      continue;
    }
    candidates.add(
        LookupCandidate{StableBindingTargetKey::definition(declaration.queryKey().clone()),
                        StableBindingTargetKey::definition(declaration.queryKey().clone()),
                        BindingOrigin::LocalDeclaration});
  }
  for (const auto& parameter : skeleton.genericParameterDeclarations().values()) {
    if (parameter.declaringScope() != scope || nameSpace != Namespace::Type ||
        parameter.name() != name) {
      continue;
    }
    candidates.add(
        LookupCandidate{StableBindingTargetKey::genericParameter(parameter.queryKey().clone()),
                        StableBindingTargetKey::genericParameter(parameter.queryKey().clone()),
                        BindingOrigin::LocalDeclaration});
  }
  for (const auto& parameter : skeleton.callableParameterDeclarations().values()) {
    ZC_IF_SOME(parameterName, parameter.name()) {
      if (parameter.declaringScope() != scope || nameSpace != Namespace::Value ||
          parameterName != name) {
        continue;
      }
      candidates.add(
          LookupCandidate{StableBindingTargetKey::callableParameter(parameter.queryKey().clone()),
                          StableBindingTargetKey::callableParameter(parameter.queryKey().clone()),
                          BindingOrigin::LocalDeclaration});
    }
  }
  for (const auto& import : skeleton.imports().values()) {
    if (import.declaringScope() != scope || import.nameSpace() != nameSpace ||
        import.queryKey().binding().localName() != name) {
      continue;
    }
    candidates.add(LookupCandidate{import.target().clone(), import.canonicalTarget().clone(),
                                   import.origin()});
  }
}

zc::Maybe<LookupCandidates> lexicalCandidates(
    const StableOwnerBodyQueryKey& owner, const StableScopeOwnerKey& useScope,
    const identity::DeclaredDefinitionName& name, Namespace nameSpace,
    zc::ArrayPtr<const OwnerBodySyntaxPathEntry> entries, size_t useIndex,
    const BoundModuleSkeleton& skeleton, const CanonicalSequence<StableBodyScopeFact>& scopes,
    const CanonicalSequence<StableOwnerLocalBindingFact>& bindings) {
  auto scope = useScope.clone();
  const size_t maximumDepth = scopes.values().size() + skeleton.scopes().values().size() + 1;
  for (size_t depth = 0; depth < maximumDepth; ++depth) {
    LookupCandidates candidates;
    if (nameSpace == Namespace::Value) {
      if (!appendLocalCandidates(owner, scope, name, entries, useIndex, bindings,
                                 candidates.values)) {
        return zc::none;
      }
      if (candidates.values.empty()) {
        appendSkeletonCandidates(scope, name, nameSpace, skeleton, candidates.values);
      }
    } else {
      appendSkeletonCandidates(scope, name, nameSpace, skeleton, candidates.values);
    }
    if (!candidates.values.empty()) { return candidates; }
    auto parent = scopeParent(scope, skeleton, scopes);
    if (parent == zc::none) { return LookupCandidates{}; }
    scope = zc::mv(ZC_ASSERT_NONNULL(parent));
  }
  return zc::none;
}

zc::Maybe<StableScopeOwnerKey> verifierScopeParent(
    const StableScopeOwnerKey& scope, const BoundModuleSkeleton& skeleton,
    const CanonicalSequence<StableBodyScopeFact>& bodyScopes) {
  zc::Maybe<StableScopeOwnerKey> parent;
  for (size_t index = skeleton.scopes().values().size(); index != 0; --index) {
    const auto& skeletonScope = skeleton.scopes().values()[index - 1];
    if (skeletonScope.owner() != scope) { continue; }
    if (parent != zc::none || skeletonScope.parent() == zc::none) { return zc::none; }
    parent = ZC_ASSERT_NONNULL(skeletonScope.parent()).clone();
  }
  for (size_t index = bodyScopes.values().size(); index != 0; --index) {
    const auto& bodyScope = bodyScopes.values()[index - 1];
    if (bodyScope.scope() != scope) { continue; }
    if (parent != zc::none) { return zc::none; }
    parent = bodyScope.parent().clone();
  }
  return parent;
}

zc::Maybe<StableScopeOwnerKey> providerCapturableTargetScope(
    const StableOwnerBodyQueryKey& owner, const BoundModuleSkeleton& skeleton,
    const CanonicalSequence<StableOwnerLocalBindingFact>& bindings,
    const StableBindingTargetKey& target, bool& capturable) {
  capturable = false;
  const auto& value = target.value();
  if (value.is<StableOwnerLocalBindingTarget>()) {
    capturable = true;
    const auto& local = value.get<StableOwnerLocalBindingTarget>();
    if (local.owner != owner) { return zc::none; }
    for (const auto& binding : bindings.values()) {
      if (binding.key() == local.binding) { return binding.declaringScope().clone(); }
    }
    return zc::none;
  }
  if (value.is<StableCallableParameterBindingTarget>()) {
    capturable = true;
    const auto& parameter = value.get<StableCallableParameterBindingTarget>().parameter;
    for (const auto& declaration : skeleton.callableParameterDeclarations().values()) {
      if (declaration.queryKey() == parameter) { return declaration.declaringScope().clone(); }
    }
  }
  return zc::none;
}

zc::Maybe<StableScopeOwnerKey> verifierCapturableTargetScope(
    const StableOwnerBodyQueryKey& owner, const BoundModuleSkeleton& skeleton,
    const CanonicalSequence<StableOwnerLocalBindingFact>& bindings,
    const StableBindingTargetKey& target, bool& capturable) {
  capturable = false;
  const auto& value = target.value();
  if (value.is<StableCallableParameterBindingTarget>()) {
    capturable = true;
    const auto& parameter = value.get<StableCallableParameterBindingTarget>().parameter;
    for (size_t index = skeleton.callableParameterDeclarations().values().size(); index != 0;
         --index) {
      const auto& declaration = skeleton.callableParameterDeclarations().values()[index - 1];
      if (declaration.queryKey() == parameter) { return declaration.declaringScope().clone(); }
    }
    return zc::none;
  }
  if (value.is<StableOwnerLocalBindingTarget>()) {
    capturable = true;
    const auto& local = value.get<StableOwnerLocalBindingTarget>();
    if (local.owner != owner) { return zc::none; }
    for (size_t index = bindings.values().size(); index != 0; --index) {
      const auto& binding = bindings.values()[index - 1];
      if (binding.key() == local.binding) { return binding.declaringScope().clone(); }
    }
  }
  return zc::none;
}

bool providerScopeIsWithin(const StableScopeOwnerKey& candidate,
                           const StableScopeOwnerKey& enclosing,
                           const BoundModuleSkeleton& skeleton,
                           const CanonicalSequence<StableBodyScopeFact>& scopes) {
  auto scope = candidate.clone();
  for (size_t depth = 0; depth <= scopes.values().size() + skeleton.scopes().values().size();
       ++depth) {
    if (scope == enclosing) { return true; }
    auto parent = scopeParent(scope, skeleton, scopes);
    if (parent == zc::none) { return false; }
    scope = zc::mv(ZC_ASSERT_NONNULL(parent));
  }
  return false;
}

bool verifierScopeIsWithin(const StableScopeOwnerKey& candidate,
                           const StableScopeOwnerKey& enclosing,
                           const BoundModuleSkeleton& skeleton,
                           const CanonicalSequence<StableBodyScopeFact>& scopes) {
  auto scope = candidate.clone();
  for (size_t depth = 0; depth <= scopes.values().size() + skeleton.scopes().values().size();
       ++depth) {
    if (scope == enclosing) { return true; }
    auto parent = verifierScopeParent(scope, skeleton, scopes);
    if (parent == zc::none) { return false; }
    scope = zc::mv(ZC_ASSERT_NONNULL(parent));
  }
  return false;
}

bool appendProviderFreeVariable(zc::Vector<PendingClosureFreeVariables>& rows,
                                const AnonymousOwnerLocalKey& closure,
                                const StableBindingTargetKey& target,
                                const LocalSyntaxPath& referencePath) {
  for (auto& row : rows) {
    if (row.closure != closure) { continue; }
    for (auto& variable : row.variables) {
      if (variable.target != target) { continue; }
      for (const auto& existing : variable.referencePaths) {
        if (existing == referencePath) { return true; }
      }
      variable.referencePaths.add(referencePath.clone());
      return true;
    }
    zc::Vector<LocalSyntaxPath> references;
    references.add(referencePath.clone());
    row.variables.add(PendingFreeVariable{target.clone(), zc::mv(references)});
    return true;
  }
  return false;
}

bool appendVerifierFreeVariable(zc::Vector<PendingClosureFreeVariables>& rows,
                                const AnonymousOwnerLocalKey& closure,
                                const StableBindingTargetKey& target,
                                const LocalSyntaxPath& referencePath) {
  for (size_t rowIndex = rows.size(); rowIndex != 0; --rowIndex) {
    auto& row = rows[rowIndex - 1];
    if (row.closure != closure) { continue; }
    for (size_t variableIndex = row.variables.size(); variableIndex != 0; --variableIndex) {
      auto& variable = row.variables[variableIndex - 1];
      if (variable.target != target) { continue; }
      for (size_t referenceIndex = variable.referencePaths.size(); referenceIndex != 0;
           --referenceIndex) {
        if (variable.referencePaths[referenceIndex - 1] == referencePath) { return true; }
      }
      variable.referencePaths.add(referencePath.clone());
      return true;
    }
    zc::Vector<LocalSyntaxPath> references;
    references.add(referencePath.clone());
    row.variables.add(PendingFreeVariable{target.clone(), zc::mv(references)});
    return true;
  }
  return false;
}

bool appendVerifierLocalCandidates(const StableOwnerBodyQueryKey& owner,
                                   const StableScopeOwnerKey& scope,
                                   const identity::DeclaredDefinitionName& name,
                                   zc::ArrayPtr<const OwnerBodySyntaxPathEntry> entries,
                                   size_t useIndex,
                                   const CanonicalSequence<StableOwnerLocalBindingFact>& bindings,
                                   zc::Vector<LookupCandidate>& candidates) {
  for (size_t index = bindings.values().size(); index != 0; --index) {
    const auto& binding = bindings.values()[index - 1];
    if (binding.declaringScope() != scope || binding.nameSpace() != Namespace::Value ||
        binding.name() != name) {
      continue;
    }
    auto activationEnd = variableDeclaratorEnd(entries, binding.key().path());
    if (activationEnd == zc::none) { return false; }
    if (useIndex < ZC_ASSERT_NONNULL(activationEnd)) { continue; }
    auto target = StableBindingTargetKey::ownerLocal(owner.clone(), binding.key().clone());
    if (target == zc::none) { return false; }
    candidates.add(LookupCandidate{ZC_ASSERT_NONNULL(target).clone(),
                                   zc::mv(ZC_ASSERT_NONNULL(target)),
                                   BindingOrigin::LocalDeclaration});
  }
  return true;
}

void appendVerifierSkeletonCandidates(const StableScopeOwnerKey& scope,
                                      const identity::DeclaredDefinitionName& name,
                                      Namespace nameSpace, const BoundModuleSkeleton& skeleton,
                                      zc::Vector<LookupCandidate>& candidates) {
  for (size_t index = skeleton.imports().values().size(); index != 0; --index) {
    const auto& imported = skeleton.imports().values()[index - 1];
    if (imported.declaringScope() != scope || imported.nameSpace() != nameSpace ||
        imported.queryKey().binding().localName() != name) {
      continue;
    }
    candidates.add(LookupCandidate{imported.target().clone(), imported.canonicalTarget().clone(),
                                   imported.origin()});
  }
  for (size_t index = skeleton.callableParameterDeclarations().values().size(); index != 0;
       --index) {
    const auto& parameter = skeleton.callableParameterDeclarations().values()[index - 1];
    const auto& parameterName = parameter.name();
    if (parameterName == zc::none || parameter.declaringScope() != scope ||
        nameSpace != Namespace::Value || ZC_ASSERT_NONNULL(parameterName) != name) {
      continue;
    }
    candidates.add(
        LookupCandidate{StableBindingTargetKey::callableParameter(parameter.queryKey().clone()),
                        StableBindingTargetKey::callableParameter(parameter.queryKey().clone()),
                        BindingOrigin::LocalDeclaration});
  }
  for (size_t index = skeleton.genericParameterDeclarations().values().size(); index != 0;
       --index) {
    const auto& parameter = skeleton.genericParameterDeclarations().values()[index - 1];
    if (parameter.declaringScope() != scope || nameSpace != Namespace::Type ||
        parameter.name() != name) {
      continue;
    }
    candidates.add(
        LookupCandidate{StableBindingTargetKey::genericParameter(parameter.queryKey().clone()),
                        StableBindingTargetKey::genericParameter(parameter.queryKey().clone()),
                        BindingOrigin::LocalDeclaration});
  }
  for (size_t index = skeleton.declarations().values().size(); index != 0; --index) {
    const auto& declaration = skeleton.declarations().values()[index - 1];
    if (declaration.declaringScope() != scope || declaration.nameSpace() != nameSpace ||
        declaration.name() != name ||
        (declaration.activation() != DefinitionActivation::ModuleSkeleton &&
         declaration.activation() != DefinitionActivation::ImportSurface)) {
      continue;
    }
    candidates.add(
        LookupCandidate{StableBindingTargetKey::definition(declaration.queryKey().clone()),
                        StableBindingTargetKey::definition(declaration.queryKey().clone()),
                        BindingOrigin::LocalDeclaration});
  }
}

zc::Maybe<LookupCandidates> verifierLexicalCandidates(
    const StableOwnerBodyQueryKey& owner, const StableScopeOwnerKey& useScope,
    const identity::DeclaredDefinitionName& name, Namespace nameSpace,
    zc::ArrayPtr<const OwnerBodySyntaxPathEntry> entries, size_t useIndex,
    const BoundModuleSkeleton& skeleton, const CanonicalSequence<StableBodyScopeFact>& scopes,
    const CanonicalSequence<StableOwnerLocalBindingFact>& bindings) {
  auto scope = useScope.clone();
  const size_t maximumDepth = scopes.values().size() + skeleton.scopes().values().size() + 1;
  for (size_t depth = 0; depth < maximumDepth; ++depth) {
    LookupCandidates candidates;
    if (nameSpace == Namespace::Value) {
      if (!appendVerifierLocalCandidates(owner, scope, name, entries, useIndex, bindings,
                                         candidates.values)) {
        return zc::none;
      }
      if (candidates.values.empty()) {
        appendVerifierSkeletonCandidates(scope, name, nameSpace, skeleton, candidates.values);
      }
    } else {
      appendVerifierSkeletonCandidates(scope, name, nameSpace, skeleton, candidates.values);
    }
    if (!candidates.values.empty()) { return candidates; }
    auto parent = verifierScopeParent(scope, skeleton, scopes);
    if (parent == zc::none) { return LookupCandidates{}; }
    scope = zc::mv(ZC_ASSERT_NONNULL(parent));
  }
  return zc::none;
}

zc::Maybe<StableFailedLookupOutcome> ambiguousLookupOutcome(const LookupCandidates& candidates) {
  if (candidates.values.size() < 2) { return zc::none; }
  zc::Vector<StableBindingTargetKey> targets(candidates.values.size());
  for (const auto& candidate : candidates.values) { targets.add(candidate.binding.clone()); }
  sortProviderCanonical(targets);
  auto admitted =
      StableBindingSequenceBuilder<StableBindingTargetKey>::fromNonEmpty(zc::mv(targets));
  if (admitted == zc::none) { return zc::none; }
  return StableFailedLookupOutcome::ambiguous(zc::mv(ZC_ASSERT_NONNULL(admitted)));
}

zc::Maybe<StableFailedLookupOutcome> verifierAmbiguousLookupOutcome(
    const LookupCandidates& candidates) {
  if (candidates.values.size() < 2) { return zc::none; }
  zc::Vector<StableBindingTargetKey> targets(candidates.values.size());
  for (size_t index = candidates.values.size(); index != 0; --index) {
    targets.add(candidates.values[index - 1].binding.clone());
  }
  sortVerifierCanonical(targets);
  auto admitted =
      StableBindingSequenceBuilder<StableBindingTargetKey>::fromNonEmpty(zc::mv(targets));
  if (admitted == zc::none) { return zc::none; }
  return StableFailedLookupOutcome::ambiguous(zc::mv(ZC_ASSERT_NONNULL(admitted)));
}

bool isContextualSelfTypePath(const OwnerBodySyntaxPathEntry& entry,
                              zc::ArrayPtr<const OwnerBodySyntaxPathEntry> entries,
                              const ModuleBodySyntax& syntax) {
  if (entry.kind != DetachedModuleBodyNodeKind::Syntax ||
      entry.syntaxKind != ast::SyntaxKind::ModulePath || entry.parentIndex == kNoParent ||
      entry.parentIndex >= entries.size() ||
      entries[entry.parentIndex].syntaxKind != ast::SyntaxKind::NamedTypeExpr) {
    return false;
  }
  auto segments = syntax.nodes()[entry.nodeIndex].identifierListField(0);
  return segments != zc::none && ZC_ASSERT_NONNULL(segments).size() == 1 &&
         ZC_ASSERT_NONNULL(segments)[0].text() == "Self"_zc;
}

zc::Maybe<identity::DeclaredDefinitionName> markerImplPathTerminalName(
    zc::ArrayPtr<const identity::DeclaredDefinitionName> segments,
    const identity::ModuleKey& module) {
  if (segments.size() == 0) { return zc::none; }
  if (segments.size() == 1) { return segments[0].clone(); }
  const auto modulePath = module.path();
  if (modulePath.size() + 1 != segments.size()) { return zc::none; }
  for (size_t index = 0; index < modulePath.size(); ++index) {
    if (modulePath[index].text() != segments[index].text()) { return zc::none; }
  }
  return segments[segments.size() - 1].clone();
}

bool isNominalSelfOwnerKind(identity::DefinitionKind kind) {
  switch (kind) {
    case identity::DefinitionKind::Class:
    case identity::DefinitionKind::Struct:
    case identity::DefinitionKind::Enum:
    case identity::DefinitionKind::Error:
      return true;
    default:
      return false;
  }
}

zc::Maybe<StableSelfOwner> providerSelfOwnerForDefinition(const identity::DefinitionKey& definition,
                                                          const BoundModuleSkeleton& skeleton) {
  zc::Maybe<const StableDeclarationFact&> selected;
  for (const auto& declaration : skeleton.declarations().values()) {
    if (declaration.queryKey().definition() != definition) { continue; }
    if (selected != zc::none) { return zc::none; }
    selected = declaration;
  }
  if (selected == zc::none) { return zc::none; }
  const auto& declaration = ZC_ASSERT_NONNULL(selected);
  if (isNominalSelfOwnerKind(declaration.kind())) {
    return StableSelfOwner::nominal(declaration.queryKey().clone());
  }
  if (declaration.kind() == identity::DefinitionKind::Interface) {
    return StableSelfOwner::interface(declaration.queryKey().clone());
  }
  return zc::none;
}

zc::Maybe<StableSelfOwner> providerContextualSelfOwner(const StableOwnerBodyQueryKey& owner,
                                                       const BoundModuleSkeleton& skeleton) {
  ZC_IF_SOME(definition, owner.owner().definitionKey()) {
    auto direct = providerSelfOwnerForDefinition(definition, skeleton);
    if (direct != zc::none) { return direct; }

    zc::Maybe<const StableDeclarationFact&> bodyDeclaration;
    for (const auto& declaration : skeleton.declarations().values()) {
      if (declaration.queryKey().definition() != definition) { continue; }
      if (bodyDeclaration != zc::none) { return zc::none; }
      bodyDeclaration = declaration;
    }
    if (bodyDeclaration == zc::none) { return zc::none; }
    const auto owners = ZC_ASSERT_NONNULL(bodyDeclaration).record().owners();
    for (size_t index = owners.size(); index != 0; --index) {
      ZC_IF_SOME(enclosing, owners[index - 1].definitionKey()) {
        auto selfOwner = providerSelfOwnerForDefinition(enclosing, skeleton);
        if (selfOwner != zc::none) { return selfOwner; }
      }
    }
  }
  return zc::none;
}

zc::Maybe<StableSelfOwner> verifierSelfOwnerForDefinition(const identity::DefinitionKey& definition,
                                                          const BoundModuleSkeleton& skeleton) {
  zc::Maybe<const StableDeclarationFact&> selected;
  const auto declarations = skeleton.declarations().values();
  for (size_t index = declarations.size(); index != 0; --index) {
    const auto& declaration = declarations[index - 1];
    if (declaration.queryKey().definition() != definition) { continue; }
    if (selected != zc::none) { return zc::none; }
    selected = declaration;
  }
  if (selected == zc::none) { return zc::none; }
  const auto& declaration = ZC_ASSERT_NONNULL(selected);
  if (declaration.kind() == identity::DefinitionKind::Interface) {
    return StableSelfOwner::interface(declaration.queryKey().clone());
  }
  if (isNominalSelfOwnerKind(declaration.kind())) {
    return StableSelfOwner::nominal(declaration.queryKey().clone());
  }
  return zc::none;
}

zc::Maybe<StableSelfOwner> verifierContextualSelfOwner(const StableOwnerBodyQueryKey& owner,
                                                       const BoundModuleSkeleton& skeleton) {
  ZC_IF_SOME(definition, owner.owner().definitionKey()) {
    auto direct = verifierSelfOwnerForDefinition(definition, skeleton);
    if (direct != zc::none) { return direct; }

    zc::Maybe<const StableDeclarationFact&> bodyDeclaration;
    const auto declarations = skeleton.declarations().values();
    for (size_t index = declarations.size(); index != 0; --index) {
      const auto& declaration = declarations[index - 1];
      if (declaration.queryKey().definition() != definition) { continue; }
      if (bodyDeclaration != zc::none) { return zc::none; }
      bodyDeclaration = declaration;
    }
    if (bodyDeclaration == zc::none) { return zc::none; }
    const auto owners = ZC_ASSERT_NONNULL(bodyDeclaration).record().owners();
    for (size_t index = owners.size(); index != 0; --index) {
      ZC_IF_SOME(enclosing, owners[index - 1].definitionKey()) {
        auto selfOwner = verifierSelfOwnerForDefinition(enclosing, skeleton);
        if (selfOwner != zc::none) { return selfOwner; }
      }
    }
  }
  return zc::none;
}

zc::Maybe<OwnerBodyLookupFacts> projectOwnerBodyLookups(
    const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
    const BoundModuleSkeleton& skeleton, const CanonicalSequence<StableBodyScopeFact>& scopes,
    const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes,
    const CanonicalSequence<StableOwnerLocalBindingFact>& bindings) {
  auto traversal = OwnerBodySyntaxTraversal::from(syntax);
  if (traversal == zc::none || !sameModule(owner.module(), skeleton.module())) { return zc::none; }
  const auto entries = ZC_ASSERT_NONNULL(traversal).entries();
  zc::Vector<StableResolutionFact> resolutions;
  zc::Vector<StableFailedLookupFact> failedLookups;
  for (const auto& entry : entries) {
    if (entry.kind != DetachedModuleBodyNodeKind::Syntax) { continue; }
    if (isContextualSelfTypePath(entry, entries, syntax)) { continue; }
    zc::Maybe<identity::DeclaredDefinitionName> name;
    Namespace nameSpace = Namespace::Value;
    if (entry.syntaxKind == ast::SyntaxKind::IdentExpr) {
      name = syntax.nodes()[entry.nodeIndex].identifierField(0);
    } else if (entry.syntaxKind == ast::SyntaxKind::ModulePath ||
               entry.syntaxKind == ast::SyntaxKind::AttributePath) {
      if (entry.parentIndex == kNoParent || entry.parentIndex >= entries.size()) {
        continue;
      }
      const auto parentKind = entries[entry.parentIndex].syntaxKind;
      if ((entry.syntaxKind == ast::SyntaxKind::ModulePath &&
           parentKind != ast::SyntaxKind::NamedTypeExpr) ||
          (entry.syntaxKind == ast::SyntaxKind::AttributePath &&
           parentKind != ast::SyntaxKind::MarkerImpl)) {
        continue;
      }
      auto segments = syntax.nodes()[entry.nodeIndex].identifierListField(0);
      if (segments == zc::none || ZC_ASSERT_NONNULL(segments).size() == 0) { return zc::none; }
      name =
          entry.syntaxKind == ast::SyntaxKind::AttributePath
              ? markerImplPathTerminalName(ZC_ASSERT_NONNULL(segments), skeleton.module())
              : zc::Maybe<identity::DeclaredDefinitionName>(ZC_ASSERT_NONNULL(segments)[0].clone());
      if (name == zc::none) { return zc::none; }
      nameSpace = Namespace::Type;
    } else {
      continue;
    }
    auto scope = providerNodeScope(entry.path, nodeScopes);
    if (name == zc::none || scope == zc::none) { return zc::none; }
    auto candidates =
        lexicalCandidates(owner, ZC_ASSERT_NONNULL(scope), ZC_ASSERT_NONNULL(name), nameSpace,
                          entries, entry.nodeIndex, skeleton, scopes, bindings);
    if (candidates == zc::none) { return zc::none; }
    if (ZC_ASSERT_NONNULL(candidates).values.size() == 1) {
      const auto& candidate = ZC_ASSERT_NONNULL(candidates).values[0];
      auto fact = StableResolutionFact::from(owner.clone(), entry.path.clone(), nameSpace,
                                             candidate.binding.clone(),
                                             candidate.canonicalTarget.clone(), candidate.origin);
      if (fact == zc::none) { return zc::none; }
      resolutions.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
      continue;
    }
    StableFailedLookupOutcome outcome = StableFailedLookupOutcome::missing();
    if (ZC_ASSERT_NONNULL(candidates).values.size() > 1) {
      auto ambiguous = ambiguousLookupOutcome(ZC_ASSERT_NONNULL(candidates));
      if (ambiguous == zc::none) { return zc::none; }
      outcome = zc::mv(ZC_ASSERT_NONNULL(ambiguous));
    } else {
      const auto alternateNamespace =
          nameSpace == Namespace::Value ? Namespace::Type : Namespace::Value;
      auto alternates = lexicalCandidates(owner, ZC_ASSERT_NONNULL(scope), ZC_ASSERT_NONNULL(name),
                                          alternateNamespace, entries, entry.nodeIndex, skeleton,
                                          scopes, bindings);
      if (alternates == zc::none) { return zc::none; }
      if (!ZC_ASSERT_NONNULL(alternates).values.empty()) {
        zc::Vector<Namespace> namespaces;
        namespaces.add(alternateNamespace);
        auto available = StableBindingSequenceBuilder<Namespace>::fromNonEmpty(zc::mv(namespaces));
        if (available == zc::none) { return zc::none; }
        outcome =
            StableFailedLookupOutcome::namespaceMismatch(zc::mv(ZC_ASSERT_NONNULL(available)));
      }
    }
    auto fact =
        StableFailedLookupFact::from(BinderQueryOwner::body(owner.clone()), entry.path.clone(),
                                     nameSpace, zc::mv(ZC_ASSERT_NONNULL(name)), zc::mv(outcome));
    if (fact == zc::none) { return zc::none; }
    failedLookups.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
  }
  sortProviderCanonical(resolutions);
  sortProviderCanonical(failedLookups);
  auto admittedResolutions =
      StableBindingSequenceBuilder<StableResolutionFact>::from(zc::mv(resolutions));
  auto admittedFailedLookups =
      StableBindingSequenceBuilder<StableFailedLookupFact>::from(zc::mv(failedLookups));
  if (admittedResolutions == zc::none || admittedFailedLookups == zc::none) { return zc::none; }
  return OwnerBodyLookupFacts{zc::mv(ZC_ASSERT_NONNULL(admittedResolutions)),
                              zc::mv(ZC_ASSERT_NONNULL(admittedFailedLookups))};
}

zc::Maybe<StableCallableParameterQueryKey> providerReceiver(const StableOwnerBodyQueryKey& owner,
                                                            const BoundModuleSkeleton& skeleton) {
  ZC_IF_SOME(definition, owner.owner().definitionKey()) {
    zc::Maybe<StableCallableParameterQueryKey> receiver;
    for (const auto& parameter : skeleton.callableParameterDeclarations().values()) {
      if (parameter.record().owner() != definition ||
          parameter.record().position().kind() !=
              identity::CallableParameterPositionKind::Receiver) {
        continue;
      }
      if (receiver != zc::none) { return zc::none; }
      receiver = parameter.queryKey().clone();
    }
    return receiver;
  }
  return zc::none;
}

zc::Maybe<StableCallableParameterQueryKey> verifierReceiver(const StableOwnerBodyQueryKey& owner,
                                                            const BoundModuleSkeleton& skeleton) {
  ZC_IF_SOME(definition, owner.owner().definitionKey()) {
    zc::Maybe<StableCallableParameterQueryKey> receiver;
    const auto parameters = skeleton.callableParameterDeclarations().values();
    for (size_t index = parameters.size(); index != 0; --index) {
      const auto& parameter = parameters[index - 1];
      if (parameter.record().position().kind() !=
              identity::CallableParameterPositionKind::Receiver ||
          parameter.record().owner() != definition) {
        continue;
      }
      if (receiver != zc::none) { return zc::none; }
      receiver = parameter.queryKey().clone();
    }
    return receiver;
  }
  return zc::none;
}

bool isLoopSyntaxKind(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::WhileStmt:
    case ast::SyntaxKind::ForStmt:
    case ast::SyntaxKind::ForInStatement:
    case ast::SyntaxKind::DoWhileStatement:
      return true;
    default:
      return false;
  }
}

zc::Maybe<StableLabelTarget> labelTargetForSyntaxKind(ast::SyntaxKind kind,
                                                      StableScopeOwnerKey&& scope) {
  if (kind == ast::SyntaxKind::BlockStmt) { return StableLabelTarget::block(zc::mv(scope)); }
  if (isLoopSyntaxKind(kind)) { return StableLabelTarget::loop(zc::mv(scope)); }
  return zc::none;
}

zc::Maybe<const OwnerBodySyntaxPathEntry&> providerEntryAtPath(
    zc::ArrayPtr<const OwnerBodySyntaxPathEntry> entries, const LocalSyntaxPath& path) {
  for (const auto& entry : entries) {
    if (entry.path == path) { return entry; }
  }
  return zc::none;
}

zc::Maybe<const VerifierLabelNode&> verifierEntryAtPath(
    zc::ArrayPtr<const VerifierLabelNode> entries, const LocalSyntaxPath& path) {
  for (const auto& entry : entries) {
    if (entry.path == path) { return entry; }
  }
  return zc::none;
}

bool isStrictAncestorPath(const LocalSyntaxPath& ancestor, const LocalSyntaxPath& descendant) {
  if (ancestor.components().size() >= descendant.components().size()) { return false; }
  for (size_t index = 0; index < ancestor.components().size(); ++index) {
    if (ancestor.components()[index] != descendant.components()[index]) { return false; }
  }
  return true;
}

zc::Maybe<StableControlTarget> explicitControlTarget(
    const identity::DeclaredDefinitionName& name, const LocalSyntaxPath& transferPath,
    ControlTransferKind kind, const CanonicalSequence<StableLabelFact>& labels) {
  zc::Maybe<const StableLabelFact&> selected;
  for (const auto& label : labels.values()) {
    if (label.name() != name ||
        !isStrictAncestorPath(label.key().declarationPath(), transferPath)) {
      continue;
    }
    if (selected == zc::none ||
        label.key().declarationPath().components().size() >
            ZC_ASSERT_NONNULL(selected).key().declarationPath().components().size()) {
      selected = label;
    }
  }
  if (selected == zc::none) { return zc::none; }
  if (kind == ControlTransferKind::Continue &&
      !ZC_ASSERT_NONNULL(selected).target().value().is<StableLoopLabelTarget>()) {
    return zc::none;
  }
  return StableControlTarget::explicitLabel(ZC_ASSERT_NONNULL(selected).key().clone());
}

zc::Maybe<StableScopeOwnerKey> providerRootScope(const StableOwnerBodyQueryKey& owner,
                                                 const BoundModuleSkeleton& skeleton) {
  if (!sameModule(owner.module(), skeleton.module()) || !hasExactOwner(owner, skeleton)) {
    return zc::none;
  }
  if (owner.owner().kind() == StableBodyOwnerKind::Module) {
    const auto expected = StableScopeOwnerKey::module(skeleton.module().clone());
    zc::Maybe<StableScopeOwnerKey> root;
    for (const auto& scope : skeleton.scopes().values()) {
      if (scope.owner() != expected) { continue; }
      if (root != zc::none || scope.parent() != zc::none || scope.kind() != ScopeKind::Module) {
        return zc::none;
      }
      root = scope.owner().clone();
    }
    return root;
  }

  const auto& definition = ZC_ASSERT_NONNULL(owner.owner().definitionKey());
  zc::Maybe<const StableDeclarationFact&> declaration;
  for (const auto& candidate : skeleton.declarations().values()) {
    if (candidate.queryKey().definition() != definition) { continue; }
    if (declaration != zc::none) { return zc::none; }
    declaration = candidate;
  }
  if (declaration == zc::none) { return zc::none; }

  ScopeRole role = ScopeRole::Declaration;
  ScopeKind expectedKind = ScopeKind::TypeBody;
  switch (ZC_ASSERT_NONNULL(declaration).kind()) {
    case identity::DefinitionKind::Function:
    case identity::DefinitionKind::Method:
    case identity::DefinitionKind::Constructor:
    case identity::DefinitionKind::Destructor:
      role = ScopeRole::Parameters;
      expectedKind = ScopeKind::Function;
      break;
    case identity::DefinitionKind::Field:
    case identity::DefinitionKind::Constant:
      break;
    default:
      return zc::none;
  }
  auto expected =
      StableScopeOwnerKey::definition(ZC_ASSERT_NONNULL(declaration).queryKey().clone(), role);
  if (expected == zc::none) { return zc::none; }
  zc::Maybe<StableScopeOwnerKey> root;
  for (const auto& scope : skeleton.scopes().values()) {
    if (scope.owner() != ZC_ASSERT_NONNULL(expected)) { continue; }
    if (root != zc::none || scope.kind() != expectedKind) { return zc::none; }
    root = scope.owner().clone();
  }
  return root;
}

zc::Maybe<StableScopeOwnerKey> verifierRootScope(const StableOwnerBodyQueryKey& owner,
                                                 const BoundModuleSkeleton& skeleton) {
  if (owner.module().encode().asPtr() != skeleton.module().encode().asPtr()) { return zc::none; }
  size_t ownerCount = 0;
  for (const auto& candidate : skeleton.bodyOwners().values()) {
    ownerCount += candidate == owner ? 1 : 0;
  }
  if (ownerCount != 1) { return zc::none; }
  if (owner.owner().kind() == StableBodyOwnerKind::Module) {
    zc::Maybe<StableScopeOwnerKey> root;
    for (const auto& scope : skeleton.scopes().values()) {
      if (!scope.owner().value().is<StableModuleScope>()) { continue; }
      const auto& moduleScope = scope.owner().value().get<StableModuleScope>();
      if (moduleScope.module.encode().asPtr() != owner.module().encode().asPtr()) { continue; }
      if (root != zc::none || scope.parent() != zc::none || scope.kind() != ScopeKind::Module) {
        return zc::none;
      }
      root = scope.owner().clone();
    }
    return root;
  }

  const auto& definition = ZC_ASSERT_NONNULL(owner.owner().definitionKey());
  zc::Maybe<identity::DefinitionKind> definitionKind;
  for (const auto& candidate : skeleton.declarations().values()) {
    if (candidate.queryKey().definition() != definition) { continue; }
    if (definitionKind != zc::none) { return zc::none; }
    definitionKind = candidate.kind();
  }
  if (definitionKind == zc::none) { return zc::none; }

  ScopeRole role = ScopeRole::Declaration;
  ScopeKind expectedKind = ScopeKind::TypeBody;
  switch (ZC_ASSERT_NONNULL(definitionKind)) {
    case identity::DefinitionKind::Function:
    case identity::DefinitionKind::Method:
    case identity::DefinitionKind::Constructor:
    case identity::DefinitionKind::Destructor:
      role = ScopeRole::Parameters;
      expectedKind = ScopeKind::Function;
      break;
    case identity::DefinitionKind::Field:
    case identity::DefinitionKind::Constant:
      break;
    default:
      return zc::none;
  }
  zc::Maybe<StableScopeOwnerKey> root;
  for (const auto& scope : skeleton.scopes().values()) {
    if (!scope.owner().value().is<StableDefinitionScope>()) { continue; }
    const auto& definitionScope = scope.owner().value().get<StableDefinitionScope>();
    if (definitionScope.definition.definition() != definition || definitionScope.role != role) {
      continue;
    }
    if (root != zc::none || scope.kind() != expectedKind) { return zc::none; }
    root = scope.owner().clone();
  }
  return root;
}

}  // namespace

OwnerBodySyntaxTraversal::OwnerBodySyntaxTraversal(
    zc::Own<owner_body_query_detail::OwnerBodySyntaxTraversalData>&& impl) noexcept
    : impl(zc::mv(impl)) {}

OwnerBodySyntaxTraversal::~OwnerBodySyntaxTraversal() noexcept(false) = default;
OwnerBodySyntaxTraversal::OwnerBodySyntaxTraversal(OwnerBodySyntaxTraversal&&) noexcept = default;
OwnerBodySyntaxTraversal& OwnerBodySyntaxTraversal::operator=(OwnerBodySyntaxTraversal&&) noexcept =
    default;

zc::Maybe<OwnerBodySyntaxTraversal> OwnerBodySyntaxTraversal::from(const ModuleBodySyntax& syntax) {
  zc::Vector<OwnerBodySyntaxPathEntry> entries(syntax.nodes().size());
  zc::Vector<PendingChildren> pending;
  uint32_t rootIndex = 0;
  for (size_t index = 0; index < syntax.nodes().size(); ++index) {
    if (!completeTop(pending)) { return zc::none; }
    const auto& node = syntax.nodes()[index];
    if (node.kind() != DetachedModuleBodyNodeKind::Syntax && node.childCount() != 0) {
      return zc::none;
    }
    const auto syntaxKind = node.syntaxKind();
    if ((node.kind() == DetachedModuleBodyNodeKind::Syntax) != (syntaxKind != zc::none)) {
      return zc::none;
    }

    uint32_t parentIndex = kNoParent;
    uint32_t entryRootIndex = 0;
    zc::Maybe<LocalSyntaxPath> path;
    if (pending.empty()) {
      if (rootIndex == syntax.rootCount()) { return zc::none; }
      entryRootIndex = rootIndex;
      path = providerRootPath(rootIndex++);
    } else {
      auto& parent = pending.back();
      parentIndex = parent.nodeIndex;
      entryRootIndex = parent.rootIndex;
      path = providerChildPath(entries[parentIndex].path, parent.nextChild++);
    }
    if (path == zc::none) { return zc::none; }
    entries.add(OwnerBodySyntaxPathEntry{
        zc::mv(ZC_ASSERT_NONNULL(path)), static_cast<uint32_t>(index), parentIndex, entryRootIndex,
        node.childCount(), node.kind(),
        syntaxKind == zc::none ? ast::SyntaxKind::Unknown : ZC_ASSERT_NONNULL(syntaxKind),
        syntaxKind == zc::none ? zc::Maybe<ScopeKind>()
                               : providerScopeKind(ZC_ASSERT_NONNULL(syntaxKind))});
    if (node.childCount() != 0) {
      pending.add(
          PendingChildren{static_cast<uint32_t>(index), 0, node.childCount(), entryRootIndex});
    }
  }
  if (!completeTop(pending) || !pending.empty() || rootIndex != syntax.rootCount()) {
    return zc::none;
  }
  return OwnerBodySyntaxTraversal(zc::heap<owner_body_query_detail::OwnerBodySyntaxTraversalData>(
      owner_body_query_detail::OwnerBodySyntaxTraversalData{zc::mv(entries)}));
}

zc::ArrayPtr<const OwnerBodySyntaxPathEntry> OwnerBodySyntaxTraversal::entries() const noexcept {
  return impl->entries.asPtr();
}

OwnerBodyScopeProjection::OwnerBodyScopeProjection(
    zc::Own<owner_body_query_detail::OwnerBodyScopeProjectionData>&& impl) noexcept
    : impl(zc::mv(impl)) {}

OwnerBodyScopeProjection::~OwnerBodyScopeProjection() noexcept(false) = default;
OwnerBodyScopeProjection::OwnerBodyScopeProjection(OwnerBodyScopeProjection&&) noexcept = default;
OwnerBodyScopeProjection& OwnerBodyScopeProjection::operator=(OwnerBodyScopeProjection&&) noexcept =
    default;

zc::Maybe<OwnerBodyScopeProjection> OwnerBodyScopeProjection::from(
    const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
    const StableScopeOwnerKey& rootScope) {
  if (!providerOwnsRootScope(owner, rootScope)) { return zc::none; }
  auto traversal = OwnerBodySyntaxTraversal::from(syntax);
  if (traversal == zc::none) { return zc::none; }

  zc::Vector<StableBodyScopeFact> scopeFacts;
  zc::Vector<StableBodyNodeScopeFact> nodeScopeFacts;
  zc::Vector<zc::Maybe<StableScopeOwnerKey>> scopeAtNode;
  const auto entries = ZC_ASSERT_NONNULL(traversal).entries();
  scopeAtNode.reserve(entries.size());
  for (const auto& entry : entries) {
    if (entry.nodeIndex != scopeAtNode.size()) { return zc::none; }
    if (entry.kind != DetachedModuleBodyNodeKind::Syntax) {
      scopeAtNode.add(zc::Maybe<StableScopeOwnerKey>());
      continue;
    }
    zc::Maybe<StableScopeOwnerKey> currentScope;
    if (entry.parentIndex == kNoParent) {
      currentScope = rootScope.clone();
    } else if (entry.parentIndex < scopeAtNode.size() &&
               scopeAtNode[entry.parentIndex] != zc::none) {
      currentScope = ZC_ASSERT_NONNULL(scopeAtNode[entry.parentIndex]).clone();
    } else {
      return zc::none;
    }
    ZC_IF_SOME(scopeKind, entry.scopeKind) {
      auto bodyScope = StableScopeOwnerKey::body(owner.clone(), entry.path.clone());
      auto fact = StableBodyScopeFact::from(owner.clone(), bodyScope.clone(),
                                            zc::mv(ZC_ASSERT_NONNULL(currentScope)), scopeKind);
      if (fact == zc::none) { return zc::none; }
      scopeFacts.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
      currentScope = zc::mv(bodyScope);
    }
    auto fact = StableBodyNodeScopeFact::from(owner.clone(), entry.path.clone(),
                                              ZC_ASSERT_NONNULL(currentScope).clone());
    if (fact == zc::none) { return zc::none; }
    nodeScopeFacts.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
    scopeAtNode.add(zc::mv(currentScope));
  }
  sortProviderCanonical(scopeFacts);
  sortProviderCanonical(nodeScopeFacts);
  auto scopes = StableBindingSequenceBuilder<StableBodyScopeFact>::from(zc::mv(scopeFacts));
  auto nodeScopes =
      StableBindingSequenceBuilder<StableBodyNodeScopeFact>::from(zc::mv(nodeScopeFacts));
  if (scopes == zc::none || nodeScopes == zc::none) { return zc::none; }
  return OwnerBodyScopeProjection(zc::heap<owner_body_query_detail::OwnerBodyScopeProjectionData>(
      owner_body_query_detail::OwnerBodyScopeProjectionData{
          zc::mv(ZC_ASSERT_NONNULL(scopes)), zc::mv(ZC_ASSERT_NONNULL(nodeScopes))}));
}

zc::Maybe<OwnerBodyScopeProjection> OwnerBodyScopeProjection::fromSkeleton(
    const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
    const BoundModuleSkeleton& skeleton) {
  auto rootScope = providerRootScope(owner, skeleton);
  if (rootScope == zc::none) { return zc::none; }
  return from(owner, syntax, ZC_ASSERT_NONNULL(rootScope));
}

bool OwnerBodyScopeProjection::verify(
    const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
    const StableScopeOwnerKey& rootScope, const CanonicalSequence<StableBodyScopeFact>& scopes,
    const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes) {
  if (!verifierOwnsRootScope(owner, rootScope)) { return false; }
  zc::Vector<StableBodyScopeFact> expectedScopes;
  zc::Vector<StableBodyNodeScopeFact> expectedNodeScopes;
  zc::Vector<VerifierPendingNode> pending;
  uint32_t rootIndex = 0;
  for (const auto& node : syntax.nodes()) {
    while (!pending.empty() && pending.back().nextChild == pending.back().childCount) {
      pending.removeLast();
    }
    zc::Maybe<LocalSyntaxPath> path;
    zc::Maybe<StableScopeOwnerKey> currentScope;
    if (pending.empty()) {
      if (rootIndex == syntax.rootCount()) { return false; }
      path = verifierRootPath(rootIndex++);
      currentScope = rootScope.clone();
    } else {
      auto& parent = pending.back();
      path = verifierChildPath(parent.path, parent.nextChild++);
      currentScope = parent.scope.clone();
    }
    if (path == zc::none || currentScope == zc::none ||
        (node.kind() != DetachedModuleBodyNodeKind::Syntax && node.childCount() != 0)) {
      return false;
    }
    const auto syntaxKind = node.syntaxKind();
    if ((node.kind() == DetachedModuleBodyNodeKind::Syntax) != (syntaxKind != zc::none)) {
      return false;
    }
    if (node.kind() != DetachedModuleBodyNodeKind::Syntax) { continue; }
    ZC_IF_SOME(scopeKind, verifierScopeKind(ZC_ASSERT_NONNULL(syntaxKind))) {
      auto bodyScope = StableScopeOwnerKey::body(owner.clone(), ZC_ASSERT_NONNULL(path).clone());
      auto fact = StableBodyScopeFact::from(owner.clone(), bodyScope.clone(),
                                            zc::mv(ZC_ASSERT_NONNULL(currentScope)), scopeKind);
      if (fact == zc::none) { return false; }
      expectedScopes.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
      currentScope = zc::mv(bodyScope);
    }
    auto fact = StableBodyNodeScopeFact::from(owner.clone(), ZC_ASSERT_NONNULL(path).clone(),
                                              ZC_ASSERT_NONNULL(currentScope).clone());
    if (fact == zc::none) { return false; }
    expectedNodeScopes.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
    if (node.childCount() != 0) {
      pending.add(VerifierPendingNode{zc::mv(ZC_ASSERT_NONNULL(path)),
                                      zc::mv(ZC_ASSERT_NONNULL(currentScope)), node.childCount(),
                                      0});
    }
  }
  while (!pending.empty() && pending.back().nextChild == pending.back().childCount) {
    pending.removeLast();
  }
  if (!pending.empty() || rootIndex != syntax.rootCount()) { return false; }
  sortVerifierCanonical(expectedScopes);
  sortVerifierCanonical(expectedNodeScopes);
  auto admittedScopes =
      StableBindingSequenceBuilder<StableBodyScopeFact>::from(zc::mv(expectedScopes));
  auto admittedNodeScopes =
      StableBindingSequenceBuilder<StableBodyNodeScopeFact>::from(zc::mv(expectedNodeScopes));
  return admittedScopes != zc::none && admittedNodeScopes != zc::none &&
         ZC_ASSERT_NONNULL(admittedScopes) == scopes &&
         ZC_ASSERT_NONNULL(admittedNodeScopes) == nodeScopes;
}

bool OwnerBodyScopeProjection::verifyFromSkeleton(
    const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
    const BoundModuleSkeleton& skeleton, const CanonicalSequence<StableBodyScopeFact>& scopes,
    const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes) {
  auto rootScope = verifierRootScope(owner, skeleton);
  return rootScope != zc::none &&
         verify(owner, syntax, ZC_ASSERT_NONNULL(rootScope), scopes, nodeScopes);
}

const CanonicalSequence<StableBodyScopeFact>& OwnerBodyScopeProjection::scopes() const noexcept {
  return impl->scopes;
}

const CanonicalSequence<StableBodyNodeScopeFact>& OwnerBodyScopeProjection::nodeScopes()
    const noexcept {
  return impl->nodeScopes;
}

OwnerBodyBindingProjection::OwnerBodyBindingProjection(
    zc::Own<owner_body_query_detail::OwnerBodyBindingProjectionData>&& impl) noexcept
    : impl(zc::mv(impl)) {}

OwnerBodyBindingProjection::~OwnerBodyBindingProjection() noexcept(false) = default;
OwnerBodyBindingProjection::OwnerBodyBindingProjection(OwnerBodyBindingProjection&&) noexcept =
    default;
OwnerBodyBindingProjection& OwnerBodyBindingProjection::operator=(
    OwnerBodyBindingProjection&&) noexcept = default;

zc::Maybe<OwnerBodyBindingProjection> OwnerBodyBindingProjection::from(
    const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
    const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes) {
  auto traversal = OwnerBodySyntaxTraversal::from(syntax);
  if (traversal == zc::none) { return zc::none; }
  zc::Vector<StableOwnerLocalBindingFact> facts;
  const auto entries = ZC_ASSERT_NONNULL(traversal).entries();
  for (const auto& entry : entries) {
    if (!isVariableBindingPattern(entry, entries)) { continue; }
    auto name = syntax.nodes()[entry.nodeIndex].identifierField(0);
    auto scope = providerNodeScope(entry.path, nodeScopes);
    if (name == zc::none || scope == zc::none) { return zc::none; }
    auto key = OwnerLocalBindingKey::from(
        owner.owner().clone(), entry.path.clone(), OwnerLocalBindingNamespace::Value,
        OwnerLocalBindingKind::Local, ZC_ASSERT_NONNULL(name).clone());
    if (key == zc::none) { return zc::none; }
    auto fact = StableOwnerLocalBindingFact::from(
        owner.clone(), zc::mv(ZC_ASSERT_NONNULL(key)), OwnerLocalBindingKind::Local,
        zc::mv(ZC_ASSERT_NONNULL(name)), Namespace::Value, zc::mv(ZC_ASSERT_NONNULL(scope)),
        DefinitionActivation::ExpressionIntroduction);
    if (fact == zc::none) { return zc::none; }
    facts.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
  }
  sortProviderCanonical(facts);
  auto bindings = StableBindingSequenceBuilder<StableOwnerLocalBindingFact>::from(zc::mv(facts));
  if (bindings == zc::none) { return zc::none; }
  return OwnerBodyBindingProjection(
      zc::heap<owner_body_query_detail::OwnerBodyBindingProjectionData>(
          owner_body_query_detail::OwnerBodyBindingProjectionData{
              zc::mv(ZC_ASSERT_NONNULL(bindings))}));
}

bool OwnerBodyBindingProjection::verify(
    const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
    const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes,
    const CanonicalSequence<StableOwnerLocalBindingFact>& bindings) {
  zc::Vector<StableOwnerLocalBindingFact> expected;
  zc::Vector<VerifierBindingPendingNode> pending;
  uint32_t rootIndex = 0;
  for (const auto& node : syntax.nodes()) {
    while (!pending.empty() && pending.back().nextChild == pending.back().childCount) {
      pending.removeLast();
    }
    zc::Maybe<LocalSyntaxPath> path;
    bool hasVariableDeclaratorAncestor = false;
    if (pending.empty()) {
      if (rootIndex == syntax.rootCount()) { return false; }
      path = verifierRootPath(rootIndex++);
    } else {
      auto& parent = pending.back();
      path = verifierChildPath(parent.path, parent.nextChild++);
      hasVariableDeclaratorAncestor = parent.hasVariableDeclaratorAncestor ||
                                      parent.syntaxKind == ast::SyntaxKind::VariableDeclarator;
    }
    if (path == zc::none ||
        (node.kind() != DetachedModuleBodyNodeKind::Syntax && node.childCount())) {
      return false;
    }
    const auto syntaxKind = node.syntaxKind();
    if ((node.kind() == DetachedModuleBodyNodeKind::Syntax) != (syntaxKind != zc::none)) {
      return false;
    }
    if (node.kind() == DetachedModuleBodyNodeKind::Syntax &&
        isVerifierVariableBindingPattern(node, hasVariableDeclaratorAncestor)) {
      auto name = node.identifierField(0);
      auto scope = verifierNodeScope(ZC_ASSERT_NONNULL(path), nodeScopes);
      if (name == zc::none || scope == zc::none) { return false; }
      auto key = OwnerLocalBindingKey::from(
          owner.owner().clone(), ZC_ASSERT_NONNULL(path).clone(), OwnerLocalBindingNamespace::Value,
          OwnerLocalBindingKind::Local, ZC_ASSERT_NONNULL(name).clone());
      if (key == zc::none) { return false; }
      auto fact = StableOwnerLocalBindingFact::from(
          owner.clone(), zc::mv(ZC_ASSERT_NONNULL(key)), OwnerLocalBindingKind::Local,
          zc::mv(ZC_ASSERT_NONNULL(name)), Namespace::Value, zc::mv(ZC_ASSERT_NONNULL(scope)),
          DefinitionActivation::ExpressionIntroduction);
      if (fact == zc::none) { return false; }
      expected.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
    }
    if (node.kind() == DetachedModuleBodyNodeKind::Syntax && node.childCount() != 0) {
      pending.add(VerifierBindingPendingNode{zc::mv(ZC_ASSERT_NONNULL(path)),
                                             ZC_ASSERT_NONNULL(syntaxKind),
                                             hasVariableDeclaratorAncestor, node.childCount(), 0});
    }
  }
  sortVerifierCanonical(expected);
  auto admitted = StableBindingSequenceBuilder<StableOwnerLocalBindingFact>::from(zc::mv(expected));
  return admitted != zc::none && ZC_ASSERT_NONNULL(admitted) == bindings;
}

const CanonicalSequence<StableOwnerLocalBindingFact>& OwnerBodyBindingProjection::bindings()
    const noexcept {
  return impl->bindings;
}

namespace {

int compareLocalPaths(const LocalSyntaxPath& left, const LocalSyntaxPath& right) {
  const auto leftComponents = left.components();
  const auto rightComponents = right.components();
  const size_t shared = leftComponents.size() < rightComponents.size() ? leftComponents.size()
                                                                       : rightComponents.size();
  for (size_t index = 0; index < shared; ++index) {
    if (leftComponents[index] < rightComponents[index]) { return -1; }
    if (leftComponents[index] > rightComponents[index]) { return 1; }
  }
  if (leftComponents.size() < rightComponents.size()) { return -1; }
  if (leftComponents.size() > rightComponents.size()) { return 1; }
  return 0;
}

zc::Maybe<CanonicalSequence<StableShadowTargetFact>> projectOwnerBodyShadows(
    const StableOwnerBodyQueryKey& owner, const CanonicalSequence<StableBodyScopeFact>& scopes,
    const CanonicalSequence<StableOwnerLocalBindingFact>& bindings) {
  zc::Vector<StableShadowTargetFact> facts;
  for (const auto& binding : bindings.values()) {
    if (binding.owner() != owner ||
        (binding.nameSpace() != Namespace::Value && binding.nameSpace() != Namespace::Type)) {
      return zc::none;
    }
    auto scope = binding.declaringScope().clone();
    for (size_t depth = 0; depth <= scopes.values().size(); ++depth) {
      zc::Maybe<const StableBodyScopeFact&> current;
      for (const auto& candidate : scopes.values()) {
        if (candidate.owner() != owner || candidate.scope() != scope) { continue; }
        if (current != zc::none) { return zc::none; }
        current = candidate;
      }
      if (current == zc::none) { break; }
      scope = ZC_ASSERT_NONNULL(current).parent().clone();
      zc::Maybe<const StableOwnerLocalBindingFact&> shadowed;
      for (const auto& candidate : bindings.values()) {
        if (candidate.owner() != owner || candidate.declaringScope() != scope ||
            candidate.nameSpace() != binding.nameSpace() || candidate.name() != binding.name() ||
            compareLocalPaths(candidate.key().path(), binding.key().path()) >= 0) {
          continue;
        }
        if (shadowed != zc::none) { return zc::none; }
        shadowed = candidate;
      }
      if (shadowed == zc::none) { continue; }
      auto target = StableBindingTargetKey::ownerLocal(owner.clone(), binding.key().clone());
      auto shadowTarget = StableBindingTargetKey::ownerLocal(
          owner.clone(), ZC_ASSERT_NONNULL(shadowed).key().clone());
      if (target == zc::none || shadowTarget == zc::none) { return zc::none; }
      auto fact = StableShadowTargetFact::from(owner.clone(), zc::mv(ZC_ASSERT_NONNULL(target)),
                                               zc::mv(ZC_ASSERT_NONNULL(shadowTarget)));
      if (fact == zc::none) { return zc::none; }
      facts.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
      break;
    }
  }
  return StableBindingSequenceBuilder<StableShadowTargetFact>::from(zc::mv(facts));
}

bool verifyOwnerBodyShadows(const StableOwnerBodyQueryKey& owner,
                            const CanonicalSequence<StableBodyScopeFact>& scopes,
                            const CanonicalSequence<StableOwnerLocalBindingFact>& bindings,
                            const CanonicalSequence<StableShadowTargetFact>& shadows) {
  zc::Vector<StableShadowTargetFact> expectedFacts;
  for (const auto& binding : bindings.values()) {
    if (binding.owner() != owner ||
        (binding.nameSpace() != Namespace::Value && binding.nameSpace() != Namespace::Type)) {
      return false;
    }
    auto scope = binding.declaringScope().clone();
    for (size_t depth = 0; depth <= scopes.values().size(); ++depth) {
      zc::Maybe<const StableBodyScopeFact&> current;
      for (const auto& candidate : scopes.values()) {
        if (candidate.owner() != owner || candidate.scope() != scope) { continue; }
        if (current != zc::none) { return false; }
        current = candidate;
      }
      if (current == zc::none) { break; }
      scope = ZC_ASSERT_NONNULL(current).parent().clone();
      zc::Maybe<const StableOwnerLocalBindingFact&> shadowed;
      for (const auto& candidate : bindings.values()) {
        if (candidate.owner() != owner || candidate.declaringScope() != scope ||
            candidate.nameSpace() != binding.nameSpace() || candidate.name() != binding.name() ||
            compareLocalPaths(candidate.key().path(), binding.key().path()) >= 0) {
          continue;
        }
        if (shadowed != zc::none) { return false; }
        shadowed = candidate;
      }
      if (shadowed == zc::none) { continue; }
      auto target = StableBindingTargetKey::ownerLocal(owner.clone(), binding.key().clone());
      auto shadowTarget = StableBindingTargetKey::ownerLocal(
          owner.clone(), ZC_ASSERT_NONNULL(shadowed).key().clone());
      if (target == zc::none || shadowTarget == zc::none) { return false; }
      auto fact = StableShadowTargetFact::from(owner.clone(), zc::mv(ZC_ASSERT_NONNULL(target)),
                                               zc::mv(ZC_ASSERT_NONNULL(shadowTarget)));
      if (fact == zc::none) { return false; }
      expectedFacts.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
      break;
    }
  }
  auto expected = StableBindingSequenceBuilder<StableShadowTargetFact>::from(zc::mv(expectedFacts));
  return expected != zc::none && ZC_ASSERT_NONNULL(expected) == shadows;
}

}  // namespace

OwnerBodyShadowProjection::OwnerBodyShadowProjection(
    zc::Own<owner_body_query_detail::OwnerBodyShadowProjectionData>&& impl) noexcept
    : impl(zc::mv(impl)) {}

OwnerBodyShadowProjection::~OwnerBodyShadowProjection() noexcept(false) = default;
OwnerBodyShadowProjection::OwnerBodyShadowProjection(OwnerBodyShadowProjection&&) noexcept =
    default;
OwnerBodyShadowProjection& OwnerBodyShadowProjection::operator=(
    OwnerBodyShadowProjection&&) noexcept = default;

zc::Maybe<OwnerBodyShadowProjection> OwnerBodyShadowProjection::from(
    const StableOwnerBodyQueryKey& owner, const CanonicalSequence<StableBodyScopeFact>& scopes,
    const CanonicalSequence<StableOwnerLocalBindingFact>& bindings) {
  auto shadows = projectOwnerBodyShadows(owner, scopes, bindings);
  if (shadows == zc::none) { return zc::none; }
  return OwnerBodyShadowProjection(zc::heap<owner_body_query_detail::OwnerBodyShadowProjectionData>(
      owner_body_query_detail::OwnerBodyShadowProjectionData{zc::mv(ZC_ASSERT_NONNULL(shadows))}));
}

bool OwnerBodyShadowProjection::verify(
    const StableOwnerBodyQueryKey& owner, const CanonicalSequence<StableBodyScopeFact>& scopes,
    const CanonicalSequence<StableOwnerLocalBindingFact>& bindings,
    const CanonicalSequence<StableShadowTargetFact>& shadows) {
  return verifyOwnerBodyShadows(owner, scopes, bindings, shadows);
}

const CanonicalSequence<StableShadowTargetFact>& OwnerBodyShadowProjection::shadows()
    const noexcept {
  return impl->shadows;
}

OwnerBodyLookupProjection::OwnerBodyLookupProjection(
    zc::Own<owner_body_query_detail::OwnerBodyLookupProjectionData>&& impl) noexcept
    : impl(zc::mv(impl)) {}

OwnerBodyLookupProjection::~OwnerBodyLookupProjection() noexcept(false) = default;
OwnerBodyLookupProjection::OwnerBodyLookupProjection(OwnerBodyLookupProjection&&) noexcept =
    default;
OwnerBodyLookupProjection& OwnerBodyLookupProjection::operator=(
    OwnerBodyLookupProjection&&) noexcept = default;

zc::Maybe<OwnerBodyLookupProjection> OwnerBodyLookupProjection::from(
    const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
    const BoundModuleSkeleton& skeleton, const CanonicalSequence<StableBodyScopeFact>& scopes,
    const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes,
    const CanonicalSequence<StableOwnerLocalBindingFact>& bindings) {
  auto facts = projectOwnerBodyLookups(owner, syntax, skeleton, scopes, nodeScopes, bindings);
  if (facts == zc::none) { return zc::none; }
  return OwnerBodyLookupProjection(zc::heap<owner_body_query_detail::OwnerBodyLookupProjectionData>(
      owner_body_query_detail::OwnerBodyLookupProjectionData{
          zc::mv(ZC_ASSERT_NONNULL(facts).resolutions),
          zc::mv(ZC_ASSERT_NONNULL(facts).failedLookups)}));
}

bool OwnerBodyLookupProjection::verify(
    const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
    const BoundModuleSkeleton& skeleton, const CanonicalSequence<StableBodyScopeFact>& scopes,
    const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes,
    const CanonicalSequence<StableOwnerLocalBindingFact>& bindings,
    const CanonicalSequence<StableResolutionFact>& resolutions,
    const CanonicalSequence<StableFailedLookupFact>& failedLookups) {
  auto traversal = OwnerBodySyntaxTraversal::from(syntax);
  if (traversal == zc::none ||
      owner.module().encode().asPtr() != skeleton.module().encode().asPtr()) {
    return false;
  }
  const auto entries = ZC_ASSERT_NONNULL(traversal).entries();
  zc::Vector<StableResolutionFact> expectedResolutions;
  zc::Vector<StableFailedLookupFact> expectedFailedLookups;
  for (const auto& entry : entries) {
    if (entry.kind != DetachedModuleBodyNodeKind::Syntax) { continue; }
    if (isContextualSelfTypePath(entry, entries, syntax)) { continue; }
    zc::Maybe<identity::DeclaredDefinitionName> name;
    Namespace nameSpace = Namespace::Value;
    if (entry.syntaxKind == ast::SyntaxKind::IdentExpr) {
      name = syntax.nodes()[entry.nodeIndex].identifierField(0);
    } else if (entry.syntaxKind == ast::SyntaxKind::ModulePath ||
               entry.syntaxKind == ast::SyntaxKind::AttributePath) {
      if (entry.parentIndex == kNoParent || entry.parentIndex >= entries.size()) {
        continue;
      }
      const auto parentKind = entries[entry.parentIndex].syntaxKind;
      if ((entry.syntaxKind == ast::SyntaxKind::ModulePath &&
           parentKind != ast::SyntaxKind::NamedTypeExpr) ||
          (entry.syntaxKind == ast::SyntaxKind::AttributePath &&
           parentKind != ast::SyntaxKind::MarkerImpl)) {
        continue;
      }
      auto segments = syntax.nodes()[entry.nodeIndex].identifierListField(0);
      if (segments == zc::none || ZC_ASSERT_NONNULL(segments).size() == 0) { return false; }
      name =
          entry.syntaxKind == ast::SyntaxKind::AttributePath
              ? markerImplPathTerminalName(ZC_ASSERT_NONNULL(segments), skeleton.module())
              : zc::Maybe<identity::DeclaredDefinitionName>(ZC_ASSERT_NONNULL(segments)[0].clone());
      if (name == zc::none) { return false; }
      nameSpace = Namespace::Type;
    } else {
      continue;
    }
    auto scope = verifierNodeScope(entry.path, nodeScopes);
    if (name == zc::none || scope == zc::none) { return false; }
    auto candidates =
        verifierLexicalCandidates(owner, ZC_ASSERT_NONNULL(scope), ZC_ASSERT_NONNULL(name),
                                  nameSpace, entries, entry.nodeIndex, skeleton, scopes, bindings);
    if (candidates == zc::none) { return false; }
    if (ZC_ASSERT_NONNULL(candidates).values.size() == 1) {
      const auto& candidate = ZC_ASSERT_NONNULL(candidates).values[0];
      auto fact = StableResolutionFact::from(owner.clone(), entry.path.clone(), nameSpace,
                                             candidate.binding.clone(),
                                             candidate.canonicalTarget.clone(), candidate.origin);
      if (fact == zc::none) { return false; }
      expectedResolutions.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
      continue;
    }
    StableFailedLookupOutcome outcome = StableFailedLookupOutcome::missing();
    if (ZC_ASSERT_NONNULL(candidates).values.size() > 1) {
      auto ambiguous = verifierAmbiguousLookupOutcome(ZC_ASSERT_NONNULL(candidates));
      if (ambiguous == zc::none) { return false; }
      outcome = zc::mv(ZC_ASSERT_NONNULL(ambiguous));
    } else {
      const auto alternateNamespace =
          nameSpace == Namespace::Value ? Namespace::Type : Namespace::Value;
      auto alternates = verifierLexicalCandidates(
          owner, ZC_ASSERT_NONNULL(scope), ZC_ASSERT_NONNULL(name), alternateNamespace, entries,
          entry.nodeIndex, skeleton, scopes, bindings);
      if (alternates == zc::none) { return false; }
      if (!ZC_ASSERT_NONNULL(alternates).values.empty()) {
        zc::Vector<Namespace> namespaces;
        namespaces.add(alternateNamespace);
        auto available = StableBindingSequenceBuilder<Namespace>::fromNonEmpty(zc::mv(namespaces));
        if (available == zc::none) { return false; }
        outcome =
            StableFailedLookupOutcome::namespaceMismatch(zc::mv(ZC_ASSERT_NONNULL(available)));
      }
    }
    auto fact =
        StableFailedLookupFact::from(BinderQueryOwner::body(owner.clone()), entry.path.clone(),
                                     nameSpace, zc::mv(ZC_ASSERT_NONNULL(name)), zc::mv(outcome));
    if (fact == zc::none) { return false; }
    expectedFailedLookups.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
  }
  sortVerifierCanonical(expectedResolutions);
  sortVerifierCanonical(expectedFailedLookups);
  auto admittedResolutions =
      StableBindingSequenceBuilder<StableResolutionFact>::from(zc::mv(expectedResolutions));
  auto admittedFailedLookups =
      StableBindingSequenceBuilder<StableFailedLookupFact>::from(zc::mv(expectedFailedLookups));
  return admittedResolutions != zc::none && admittedFailedLookups != zc::none &&
         ZC_ASSERT_NONNULL(admittedResolutions) == resolutions &&
         ZC_ASSERT_NONNULL(admittedFailedLookups) == failedLookups;
}

const CanonicalSequence<StableResolutionFact>& OwnerBodyLookupProjection::resolutions()
    const noexcept {
  return impl->resolutions;
}

const CanonicalSequence<StableFailedLookupFact>& OwnerBodyLookupProjection::failedLookups()
    const noexcept {
  return impl->failedLookups;
}

OwnerBodySelfTypeProjection::OwnerBodySelfTypeProjection(
    zc::Own<owner_body_query_detail::OwnerBodySelfTypeProjectionData>&& impl) noexcept
    : impl(zc::mv(impl)) {}

OwnerBodySelfTypeProjection::~OwnerBodySelfTypeProjection() noexcept(false) = default;
OwnerBodySelfTypeProjection::OwnerBodySelfTypeProjection(OwnerBodySelfTypeProjection&&) noexcept =
    default;
OwnerBodySelfTypeProjection& OwnerBodySelfTypeProjection::operator=(
    OwnerBodySelfTypeProjection&&) noexcept = default;

zc::Maybe<OwnerBodySelfTypeProjection> OwnerBodySelfTypeProjection::from(
    const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
    const BoundModuleSkeleton& skeleton) {
  auto traversal = OwnerBodySyntaxTraversal::from(syntax);
  if (traversal == zc::none || !sameModule(owner.module(), skeleton.module())) { return zc::none; }
  const auto entries = ZC_ASSERT_NONNULL(traversal).entries();
  zc::Vector<StableSelfTypeFact> facts;
  for (const auto& entry : entries) {
    if (!isContextualSelfTypePath(entry, entries, syntax)) { continue; }
    auto selfOwner = providerContextualSelfOwner(owner, skeleton);
    if (selfOwner == zc::none) { return zc::none; }
    auto fact = StableSelfTypeFact::from(owner.clone(), entries[entry.parentIndex].path.clone(),
                                         zc::mv(ZC_ASSERT_NONNULL(selfOwner)));
    if (fact == zc::none) { return zc::none; }
    facts.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
  }
  sortProviderCanonical(facts);
  auto selfTypes = StableBindingSequenceBuilder<StableSelfTypeFact>::from(zc::mv(facts));
  if (selfTypes == zc::none) { return zc::none; }
  return OwnerBodySelfTypeProjection(
      zc::heap<owner_body_query_detail::OwnerBodySelfTypeProjectionData>(
          owner_body_query_detail::OwnerBodySelfTypeProjectionData{
              zc::mv(ZC_ASSERT_NONNULL(selfTypes))}));
}

bool OwnerBodySelfTypeProjection::verify(const StableOwnerBodyQueryKey& owner,
                                         const ModuleBodySyntax& syntax,
                                         const BoundModuleSkeleton& skeleton,
                                         const CanonicalSequence<StableSelfTypeFact>& selfTypes) {
  auto traversal = OwnerBodySyntaxTraversal::from(syntax);
  if (traversal == zc::none ||
      owner.module().encode().asPtr() != skeleton.module().encode().asPtr()) {
    return false;
  }
  const auto entries = ZC_ASSERT_NONNULL(traversal).entries();
  zc::Vector<StableSelfTypeFact> expected;
  for (size_t index = entries.size(); index != 0; --index) {
    const auto& entry = entries[index - 1];
    if (!isContextualSelfTypePath(entry, entries, syntax)) { continue; }
    auto selfOwner = verifierContextualSelfOwner(owner, skeleton);
    if (selfOwner == zc::none) { return false; }
    auto fact = StableSelfTypeFact::from(owner.clone(), entries[entry.parentIndex].path.clone(),
                                         zc::mv(ZC_ASSERT_NONNULL(selfOwner)));
    if (fact == zc::none) { return false; }
    expected.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
  }
  sortVerifierCanonical(expected);
  auto admitted = StableBindingSequenceBuilder<StableSelfTypeFact>::from(zc::mv(expected));
  return admitted != zc::none && ZC_ASSERT_NONNULL(admitted) == selfTypes;
}

const CanonicalSequence<StableSelfTypeFact>& OwnerBodySelfTypeProjection::selfTypes()
    const noexcept {
  return impl->selfTypes;
}

OwnerBodyReceiverProjection::OwnerBodyReceiverProjection(
    zc::Own<owner_body_query_detail::OwnerBodyReceiverProjectionData>&& impl) noexcept
    : impl(zc::mv(impl)) {}

OwnerBodyReceiverProjection::~OwnerBodyReceiverProjection() noexcept(false) = default;
OwnerBodyReceiverProjection::OwnerBodyReceiverProjection(OwnerBodyReceiverProjection&&) noexcept =
    default;
OwnerBodyReceiverProjection& OwnerBodyReceiverProjection::operator=(
    OwnerBodyReceiverProjection&&) noexcept = default;

zc::Maybe<OwnerBodyReceiverProjection> OwnerBodyReceiverProjection::from(
    const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
    const BoundModuleSkeleton& skeleton) {
  if (!sameModule(owner.module(), skeleton.module())) { return zc::none; }
  auto traversal = OwnerBodySyntaxTraversal::from(syntax);
  if (traversal == zc::none) { return zc::none; }
  auto receiver = providerReceiver(owner, skeleton);
  zc::Vector<StableThisBindingFact> bindings;
  for (const auto& entry : ZC_ASSERT_NONNULL(traversal).entries()) {
    if (entry.kind != DetachedModuleBodyNodeKind::Syntax ||
        entry.syntaxKind != ast::SyntaxKind::ThisExpr) {
      continue;
    }
    if (receiver == zc::none) { return zc::none; }
    auto binding = StableThisBindingFact::from(owner.clone(), entry.path.clone(),
                                               ZC_ASSERT_NONNULL(receiver).clone());
    if (binding == zc::none) { return zc::none; }
    bindings.add(zc::mv(ZC_ASSERT_NONNULL(binding)));
  }
  sortProviderCanonical(bindings);
  auto admitted = StableBindingSequenceBuilder<StableThisBindingFact>::from(zc::mv(bindings));
  if (admitted == zc::none) { return zc::none; }
  return OwnerBodyReceiverProjection(
      zc::heap<owner_body_query_detail::OwnerBodyReceiverProjectionData>(
          owner_body_query_detail::OwnerBodyReceiverProjectionData{
              zc::mv(ZC_ASSERT_NONNULL(admitted))}));
}

bool OwnerBodyReceiverProjection::verify(const StableOwnerBodyQueryKey& owner,
                                         const ModuleBodySyntax& syntax,
                                         const BoundModuleSkeleton& skeleton,
                                         const CanonicalSequence<StableThisBindingFact>& bindings) {
  if (!sameModule(owner.module(), skeleton.module())) { return false; }
  auto traversal = OwnerBodySyntaxTraversal::from(syntax);
  if (traversal == zc::none) { return false; }
  auto receiver = verifierReceiver(owner, skeleton);
  zc::Vector<StableThisBindingFact> expected;
  const auto entries = ZC_ASSERT_NONNULL(traversal).entries();
  for (size_t index = entries.size(); index != 0; --index) {
    const auto& entry = entries[index - 1];
    if (entry.kind != DetachedModuleBodyNodeKind::Syntax ||
        entry.syntaxKind != ast::SyntaxKind::ThisExpr) {
      continue;
    }
    if (receiver == zc::none) { return false; }
    auto binding = StableThisBindingFact::from(owner.clone(), entry.path.clone(),
                                               ZC_ASSERT_NONNULL(receiver).clone());
    if (binding == zc::none) { return false; }
    expected.add(zc::mv(ZC_ASSERT_NONNULL(binding)));
  }
  sortVerifierCanonical(expected);
  auto admitted = StableBindingSequenceBuilder<StableThisBindingFact>::from(zc::mv(expected));
  return admitted != zc::none && ZC_ASSERT_NONNULL(admitted) == bindings;
}

const CanonicalSequence<StableThisBindingFact>& OwnerBodyReceiverProjection::bindings()
    const noexcept {
  return impl->bindings;
}

OwnerBodyDeferredMemberProjection::OwnerBodyDeferredMemberProjection(
    zc::Own<owner_body_query_detail::OwnerBodyDeferredMemberProjectionData>&& impl) noexcept
    : impl(zc::mv(impl)) {}

OwnerBodyDeferredMemberProjection::~OwnerBodyDeferredMemberProjection() noexcept(false) = default;
OwnerBodyDeferredMemberProjection::OwnerBodyDeferredMemberProjection(
    OwnerBodyDeferredMemberProjection&&) noexcept = default;
OwnerBodyDeferredMemberProjection& OwnerBodyDeferredMemberProjection::operator=(
    OwnerBodyDeferredMemberProjection&&) noexcept = default;

zc::Maybe<OwnerBodyDeferredMemberProjection> OwnerBodyDeferredMemberProjection::from(
    const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax) {
  auto traversal = OwnerBodySyntaxTraversal::from(syntax);
  if (traversal == zc::none) { return zc::none; }
  const auto entries = ZC_ASSERT_NONNULL(traversal).entries();
  zc::Vector<StableDeferredMemberFact> facts;
  for (const auto& entry : entries) {
    if (entry.kind != DetachedModuleBodyNodeKind::Syntax ||
        entry.syntaxKind != ast::SyntaxKind::MemberExpression) {
      continue;
    }
    const auto& node = syntax.nodes()[entry.nodeIndex];
    auto base = node.childField(0);
    auto member = node.identifierField(1);
    auto access = memberAccessKind(node);
    if (base == zc::none || !ZC_ASSERT_NONNULL(base).present ||
        ZC_ASSERT_NONNULL(base).childCount != 1 || member == zc::none || access == zc::none) {
      return zc::none;
    }
    auto basePath = providerChildPath(entry.path, ZC_ASSERT_NONNULL(base).firstChildOrdinal);
    if (basePath == zc::none ||
        providerEntryAtPath(entries, ZC_ASSERT_NONNULL(basePath)) == zc::none) {
      return zc::none;
    }
    zc::Vector<LocalSyntaxPath> arguments;
    if (entry.parentIndex != kNoParent) {
      if (entry.parentIndex >= entries.size()) { return zc::none; }
      const auto& parentEntry = entries[entry.parentIndex];
      if (parentEntry.syntaxKind == ast::SyntaxKind::CallExpression) {
        const auto& parent = syntax.nodes()[parentEntry.nodeIndex];
        auto callee = parent.childField(0);
        auto typeArguments = parent.childField(1);
        if (callee == zc::none || typeArguments == zc::none || !ZC_ASSERT_NONNULL(callee).present) {
          return zc::none;
        }
        auto calleePath =
            providerChildPath(parentEntry.path, ZC_ASSERT_NONNULL(callee).firstChildOrdinal);
        if (calleePath == zc::none) { return zc::none; }
        if (ZC_ASSERT_NONNULL(calleePath) == entry.path) {
          for (uint32_t index = 0; index < ZC_ASSERT_NONNULL(typeArguments).childCount; ++index) {
            auto argumentPath = providerChildPath(
                parentEntry.path, ZC_ASSERT_NONNULL(typeArguments).firstChildOrdinal + index);
            if (argumentPath == zc::none ||
                providerEntryAtPath(entries, ZC_ASSERT_NONNULL(argumentPath)) == zc::none) {
              return zc::none;
            }
            arguments.add(zc::mv(ZC_ASSERT_NONNULL(argumentPath)));
          }
        }
      }
    }
    zc::Vector<Namespace> namespaces;
    namespaces.add(Namespace::Value);
    auto expectedNamespaces =
        StableBindingSequenceBuilder<Namespace>::fromNonEmpty(zc::mv(namespaces));
    auto genericArguments = StableBindingSequenceBuilder<LocalSyntaxPath>::from(zc::mv(arguments));
    if (expectedNamespaces == zc::none || genericArguments == zc::none) { return zc::none; }
    auto fact = StableDeferredMemberFact::from(
        owner.clone(), entry.path.clone(), zc::mv(ZC_ASSERT_NONNULL(basePath)),
        ZC_ASSERT_NONNULL(access), zc::mv(ZC_ASSERT_NONNULL(member)),
        zc::mv(ZC_ASSERT_NONNULL(expectedNamespaces)), zc::mv(ZC_ASSERT_NONNULL(genericArguments)));
    if (fact == zc::none) { return zc::none; }
    facts.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
  }
  for (const auto& entry : entries) {
    if (entry.kind != DetachedModuleBodyNodeKind::Syntax ||
        entry.syntaxKind != ast::SyntaxKind::NamedTypeExpr) {
      continue;
    }
    const auto& node = syntax.nodes()[entry.nodeIndex];
    auto path = node.childField(0);
    auto arguments = node.childField(1);
    if (path == zc::none || arguments == zc::none || !ZC_ASSERT_NONNULL(path).present ||
        ZC_ASSERT_NONNULL(path).childCount != 1) {
      return zc::none;
    }
    auto basePath = providerChildPath(entry.path, ZC_ASSERT_NONNULL(path).firstChildOrdinal);
    auto baseEntry =
        basePath == zc::none ? zc::Maybe<const OwnerBodySyntaxPathEntry&>()
                             : providerEntryAtPath(entries, ZC_ASSERT_NONNULL(basePath));
    if (baseEntry == zc::none ||
        ZC_ASSERT_NONNULL(baseEntry).syntaxKind != ast::SyntaxKind::ModulePath) {
      return zc::none;
    }
    const auto& pathNode = syntax.nodes()[ZC_ASSERT_NONNULL(baseEntry).nodeIndex];
    auto segments = pathNode.identifierListField(0);
    if (segments == zc::none || ZC_ASSERT_NONNULL(segments).size() < 2) { continue; }
    zc::Vector<LocalSyntaxPath> genericArguments(ZC_ASSERT_NONNULL(arguments).childCount);
    for (uint32_t index = 0; index < ZC_ASSERT_NONNULL(arguments).childCount; ++index) {
      auto argumentPath = providerChildPath(
          entry.path, ZC_ASSERT_NONNULL(arguments).firstChildOrdinal + index);
      if (argumentPath == zc::none ||
          providerEntryAtPath(entries, ZC_ASSERT_NONNULL(argumentPath)) == zc::none) {
        return zc::none;
      }
      genericArguments.add(zc::mv(ZC_ASSERT_NONNULL(argumentPath)));
    }
    zc::Vector<Namespace> namespaces;
    namespaces.add(Namespace::Type);
    auto expectedNamespaces =
        StableBindingSequenceBuilder<Namespace>::fromNonEmpty(zc::mv(namespaces));
    auto admittedGenericArguments =
        StableBindingSequenceBuilder<LocalSyntaxPath>::from(zc::mv(genericArguments));
    if (expectedNamespaces == zc::none || admittedGenericArguments == zc::none) {
      return zc::none;
    }
    auto fact = StableDeferredMemberFact::from(
        owner.clone(), entry.path.clone(), zc::mv(ZC_ASSERT_NONNULL(basePath)),
        MemberAccessKind::Qualified, ZC_ASSERT_NONNULL(segments).back().clone(),
        zc::mv(ZC_ASSERT_NONNULL(expectedNamespaces)),
        zc::mv(ZC_ASSERT_NONNULL(admittedGenericArguments)));
    if (fact == zc::none) { return zc::none; }
    facts.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
  }
  sortProviderCanonical(facts);
  auto deferredMembers =
      StableBindingSequenceBuilder<StableDeferredMemberFact>::from(zc::mv(facts));
  if (deferredMembers == zc::none) { return zc::none; }
  return OwnerBodyDeferredMemberProjection(
      zc::heap<owner_body_query_detail::OwnerBodyDeferredMemberProjectionData>(
          owner_body_query_detail::OwnerBodyDeferredMemberProjectionData{
              zc::mv(ZC_ASSERT_NONNULL(deferredMembers))}));
}

bool OwnerBodyDeferredMemberProjection::verify(
    const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
    const CanonicalSequence<StableDeferredMemberFact>& deferredMembers) {
  auto traversal = OwnerBodySyntaxTraversal::from(syntax);
  if (traversal == zc::none) { return false; }
  const auto entries = ZC_ASSERT_NONNULL(traversal).entries();
  const auto containsPath = [&](const LocalSyntaxPath& path) {
    for (const auto& entry : entries) {
      if (entry.path == path) { return true; }
    }
    return false;
  };
  zc::Vector<StableDeferredMemberFact> expected;
  for (const auto& entry : entries) {
    if (entry.kind != DetachedModuleBodyNodeKind::Syntax ||
        entry.syntaxKind != ast::SyntaxKind::MemberExpression) {
      continue;
    }
    const auto& node = syntax.nodes()[entry.nodeIndex];
    auto base = node.childField(0);
    auto member = node.identifierField(1);
    auto access = memberAccessKind(node);
    if (base == zc::none || !ZC_ASSERT_NONNULL(base).present ||
        ZC_ASSERT_NONNULL(base).childCount != 1 || member == zc::none || access == zc::none) {
      return false;
    }
    auto basePath = verifierChildPath(entry.path, ZC_ASSERT_NONNULL(base).firstChildOrdinal);
    if (basePath == zc::none || !containsPath(ZC_ASSERT_NONNULL(basePath))) { return false; }
    zc::Vector<LocalSyntaxPath> arguments;
    if (entry.parentIndex != kNoParent) {
      if (entry.parentIndex >= entries.size()) { return false; }
      const auto& parentEntry = entries[entry.parentIndex];
      if (parentEntry.syntaxKind == ast::SyntaxKind::CallExpression) {
        const auto& parent = syntax.nodes()[parentEntry.nodeIndex];
        auto callee = parent.childField(0);
        auto typeArguments = parent.childField(1);
        if (callee == zc::none || typeArguments == zc::none || !ZC_ASSERT_NONNULL(callee).present) {
          return false;
        }
        auto calleePath =
            verifierChildPath(parentEntry.path, ZC_ASSERT_NONNULL(callee).firstChildOrdinal);
        if (calleePath == zc::none) { return false; }
        if (ZC_ASSERT_NONNULL(calleePath) == entry.path) {
          for (uint32_t index = 0; index < ZC_ASSERT_NONNULL(typeArguments).childCount; ++index) {
            auto argumentPath = verifierChildPath(
                parentEntry.path, ZC_ASSERT_NONNULL(typeArguments).firstChildOrdinal + index);
            if (argumentPath == zc::none || !containsPath(ZC_ASSERT_NONNULL(argumentPath))) {
              return false;
            }
            arguments.add(zc::mv(ZC_ASSERT_NONNULL(argumentPath)));
          }
        }
      }
    }
    zc::Vector<Namespace> namespaces;
    namespaces.add(Namespace::Value);
    auto expectedNamespaces =
        StableBindingSequenceBuilder<Namespace>::fromNonEmpty(zc::mv(namespaces));
    auto genericArguments = StableBindingSequenceBuilder<LocalSyntaxPath>::from(zc::mv(arguments));
    if (expectedNamespaces == zc::none || genericArguments == zc::none) { return false; }
    auto fact = StableDeferredMemberFact::from(
        owner.clone(), entry.path.clone(), zc::mv(ZC_ASSERT_NONNULL(basePath)),
        ZC_ASSERT_NONNULL(access), zc::mv(ZC_ASSERT_NONNULL(member)),
        zc::mv(ZC_ASSERT_NONNULL(expectedNamespaces)), zc::mv(ZC_ASSERT_NONNULL(genericArguments)));
    if (fact == zc::none) { return false; }
    expected.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
  }
  for (const auto& entry : entries) {
    if (entry.kind != DetachedModuleBodyNodeKind::Syntax ||
        entry.syntaxKind != ast::SyntaxKind::NamedTypeExpr) {
      continue;
    }
    const auto& node = syntax.nodes()[entry.nodeIndex];
    auto path = node.childField(0);
    auto arguments = node.childField(1);
    if (path == zc::none || arguments == zc::none || !ZC_ASSERT_NONNULL(path).present ||
        ZC_ASSERT_NONNULL(path).childCount != 1) {
      return false;
    }
    auto basePath = verifierChildPath(entry.path, ZC_ASSERT_NONNULL(path).firstChildOrdinal);
    zc::Maybe<const OwnerBodySyntaxPathEntry&> baseEntry;
    if (basePath != zc::none) {
      for (const auto& candidate : entries) {
        if (candidate.path == ZC_ASSERT_NONNULL(basePath)) {
          if (baseEntry != zc::none) { return false; }
          baseEntry = candidate;
        }
      }
    }
    if (baseEntry == zc::none ||
        ZC_ASSERT_NONNULL(baseEntry).syntaxKind != ast::SyntaxKind::ModulePath) {
      return false;
    }
    const auto& pathNode = syntax.nodes()[ZC_ASSERT_NONNULL(baseEntry).nodeIndex];
    auto segments = pathNode.identifierListField(0);
    if (segments == zc::none || ZC_ASSERT_NONNULL(segments).size() < 2) { continue; }
    zc::Vector<LocalSyntaxPath> genericArguments(ZC_ASSERT_NONNULL(arguments).childCount);
    for (uint32_t index = 0; index < ZC_ASSERT_NONNULL(arguments).childCount; ++index) {
      auto argumentPath = verifierChildPath(
          entry.path, ZC_ASSERT_NONNULL(arguments).firstChildOrdinal + index);
      if (argumentPath == zc::none || !containsPath(ZC_ASSERT_NONNULL(argumentPath))) {
        return false;
      }
      genericArguments.add(zc::mv(ZC_ASSERT_NONNULL(argumentPath)));
    }
    zc::Vector<Namespace> namespaces;
    namespaces.add(Namespace::Type);
    auto expectedNamespaces =
        StableBindingSequenceBuilder<Namespace>::fromNonEmpty(zc::mv(namespaces));
    auto admittedGenericArguments =
        StableBindingSequenceBuilder<LocalSyntaxPath>::from(zc::mv(genericArguments));
    if (expectedNamespaces == zc::none || admittedGenericArguments == zc::none) {
      return false;
    }
    auto fact = StableDeferredMemberFact::from(
        owner.clone(), entry.path.clone(), zc::mv(ZC_ASSERT_NONNULL(basePath)),
        MemberAccessKind::Qualified, ZC_ASSERT_NONNULL(segments).back().clone(),
        zc::mv(ZC_ASSERT_NONNULL(expectedNamespaces)),
        zc::mv(ZC_ASSERT_NONNULL(admittedGenericArguments)));
    if (fact == zc::none) { return false; }
    expected.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
  }
  sortVerifierCanonical(expected);
  auto admitted = StableBindingSequenceBuilder<StableDeferredMemberFact>::from(zc::mv(expected));
  return admitted != zc::none && ZC_ASSERT_NONNULL(admitted) == deferredMembers;
}

const CanonicalSequence<StableDeferredMemberFact>&
OwnerBodyDeferredMemberProjection::deferredMembers() const noexcept {
  return impl->deferredMembers;
}

OwnerBodyClosureProjection::OwnerBodyClosureProjection(
    zc::Own<owner_body_query_detail::OwnerBodyClosureProjectionData>&& impl) noexcept
    : impl(zc::mv(impl)) {}

OwnerBodyClosureProjection::~OwnerBodyClosureProjection() noexcept(false) = default;
OwnerBodyClosureProjection::OwnerBodyClosureProjection(OwnerBodyClosureProjection&&) noexcept =
    default;
OwnerBodyClosureProjection& OwnerBodyClosureProjection::operator=(
    OwnerBodyClosureProjection&&) noexcept = default;

zc::Maybe<OwnerBodyClosureProjection> OwnerBodyClosureProjection::from(
    const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
    const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes) {
  auto traversal = OwnerBodySyntaxTraversal::from(syntax);
  if (traversal == zc::none) { return zc::none; }
  zc::Vector<StableClosureFact> closureFacts;
  for (const auto& entry : ZC_ASSERT_NONNULL(traversal).entries()) {
    if (entry.kind != DetachedModuleBodyNodeKind::Syntax ||
        (entry.syntaxKind != ast::SyntaxKind::FunctionExpression &&
         entry.syntaxKind != ast::SyntaxKind::LambdaExpression)) {
      continue;
    }
    auto scope = providerNodeScope(entry.path, nodeScopes);
    auto closure = AnonymousOwnerLocalKey::from(owner.owner().clone(), entry.path.clone(),
                                                AnonymousOwnerLocalRole::Closure);
    if (scope == zc::none || closure == zc::none) { return zc::none; }
    auto closureFact = StableClosureFact::from(owner.clone(), ZC_ASSERT_NONNULL(closure).clone(),
                                               zc::mv(ZC_ASSERT_NONNULL(scope)));
    if (closureFact == zc::none) { return zc::none; }
    closureFacts.add(zc::mv(ZC_ASSERT_NONNULL(closureFact)));
  }
  sortProviderCanonical(closureFacts);
  auto closures = StableBindingSequenceBuilder<StableClosureFact>::from(zc::mv(closureFacts));
  if (closures == zc::none) { return zc::none; }
  return OwnerBodyClosureProjection(
      zc::heap<owner_body_query_detail::OwnerBodyClosureProjectionData>(
          owner_body_query_detail::OwnerBodyClosureProjectionData{
              zc::mv(ZC_ASSERT_NONNULL(closures))}));
}

bool OwnerBodyClosureProjection::verify(
    const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
    const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes,
    const CanonicalSequence<StableClosureFact>& closures) {
  zc::Vector<StableClosureFact> expectedClosures;
  zc::Vector<VerifierClosurePendingNode> pending;
  uint32_t rootIndex = 0;
  for (const auto& node : syntax.nodes()) {
    while (!pending.empty() && pending.back().nextChild == pending.back().childCount) {
      pending.removeLast();
    }
    zc::Maybe<LocalSyntaxPath> path;
    if (pending.empty()) {
      if (rootIndex == syntax.rootCount()) { return false; }
      path = verifierRootPath(rootIndex++);
    } else {
      auto& parent = pending.back();
      path = verifierChildPath(parent.path, parent.nextChild++);
    }
    const auto syntaxKind = node.syntaxKind();
    if (path == zc::none ||
        (node.kind() != DetachedModuleBodyNodeKind::Syntax && node.childCount()) ||
        (node.kind() == DetachedModuleBodyNodeKind::Syntax) != (syntaxKind != zc::none)) {
      return false;
    }
    if (syntaxKind == ast::SyntaxKind::FunctionExpression ||
        syntaxKind == ast::SyntaxKind::LambdaExpression) {
      auto scope = verifierNodeScope(ZC_ASSERT_NONNULL(path), nodeScopes);
      auto closure = AnonymousOwnerLocalKey::from(
          owner.owner().clone(), ZC_ASSERT_NONNULL(path).clone(), AnonymousOwnerLocalRole::Closure);
      if (scope == zc::none || closure == zc::none) { return false; }
      auto closureFact = StableClosureFact::from(owner.clone(), ZC_ASSERT_NONNULL(closure).clone(),
                                                 zc::mv(ZC_ASSERT_NONNULL(scope)));
      if (closureFact == zc::none) { return false; }
      expectedClosures.add(zc::mv(ZC_ASSERT_NONNULL(closureFact)));
    }
    if (node.kind() == DetachedModuleBodyNodeKind::Syntax && node.childCount() != 0) {
      pending.add(
          VerifierClosurePendingNode{zc::mv(ZC_ASSERT_NONNULL(path)), node.childCount(), 0});
    }
  }
  while (!pending.empty() && pending.back().nextChild == pending.back().childCount) {
    pending.removeLast();
  }
  if (!pending.empty() || rootIndex != syntax.rootCount()) { return false; }
  sortVerifierCanonical(expectedClosures);
  auto admittedClosures =
      StableBindingSequenceBuilder<StableClosureFact>::from(zc::mv(expectedClosures));
  return admittedClosures != zc::none && ZC_ASSERT_NONNULL(admittedClosures) == closures;
}

const CanonicalSequence<StableClosureFact>& OwnerBodyClosureProjection::closures() const noexcept {
  return impl->closures;
}

OwnerBodyFreeVariableProjection::OwnerBodyFreeVariableProjection(
    zc::Own<owner_body_query_detail::OwnerBodyFreeVariableProjectionData>&& impl) noexcept
    : impl(zc::mv(impl)) {}

OwnerBodyFreeVariableProjection::~OwnerBodyFreeVariableProjection() noexcept(false) = default;
OwnerBodyFreeVariableProjection::OwnerBodyFreeVariableProjection(
    OwnerBodyFreeVariableProjection&&) noexcept = default;
OwnerBodyFreeVariableProjection& OwnerBodyFreeVariableProjection::operator=(
    OwnerBodyFreeVariableProjection&&) noexcept = default;

zc::Maybe<OwnerBodyFreeVariableProjection> OwnerBodyFreeVariableProjection::from(
    const StableOwnerBodyQueryKey& owner, const BoundModuleSkeleton& skeleton,
    const CanonicalSequence<StableBodyScopeFact>& scopes,
    const CanonicalSequence<StableOwnerLocalBindingFact>& bindings,
    const CanonicalSequence<StableClosureFact>& closures,
    const CanonicalSequence<StableResolutionFact>& resolutions) {
  zc::Vector<PendingClosureFreeVariables> pending(closures.values().size());
  for (const auto& closure : closures.values()) {
    if (closure.owner() != owner) { return zc::none; }
    pending.add(
        PendingClosureFreeVariables{closure.closure().clone(), zc::Vector<PendingFreeVariable>()});
  }
  for (const auto& resolution : resolutions.values()) {
    if (resolution.owner() != owner || resolution.nameSpace() != Namespace::Value ||
        resolution.origin() != BindingOrigin::LocalDeclaration ||
        resolution.binding() != resolution.canonicalTarget()) {
      continue;
    }
    bool capturable = false;
    auto targetScope =
        providerCapturableTargetScope(owner, skeleton, bindings, resolution.binding(), capturable);
    if (!capturable) { continue; }
    if (targetScope == zc::none) { return zc::none; }
    for (const auto& closure : closures.values()) {
      if (!isStrictAncestorPath(closure.closure().path(), resolution.usePath()) ||
          providerScopeIsWithin(ZC_ASSERT_NONNULL(targetScope), closure.scope(), skeleton,
                                scopes)) {
        continue;
      }
      if (!appendProviderFreeVariable(pending, closure.closure(), resolution.binding(),
                                      resolution.usePath())) {
        return zc::none;
      }
    }
  }
  zc::Vector<StableClosureFreeVariableFact> facts(pending.size());
  for (auto& closure : pending) {
    zc::Vector<StableClosureFreeVariable> variables(closure.variables.size());
    for (auto& variable : closure.variables) {
      sortProviderCanonical(variable.referencePaths);
      auto references = StableBindingSequenceBuilder<LocalSyntaxPath>::fromNonEmpty(
          zc::mv(variable.referencePaths));
      if (references == zc::none) { return zc::none; }
      variables.add(StableClosureFreeVariable::from(zc::mv(variable.target),
                                                    zc::mv(ZC_ASSERT_NONNULL(references))));
    }
    sortProviderCanonical(variables);
    auto admittedVariables =
        StableBindingSequenceBuilder<StableClosureFreeVariable>::from(zc::mv(variables));
    if (admittedVariables == zc::none) { return zc::none; }
    auto fact = StableClosureFreeVariableFact::from(owner.clone(), zc::mv(closure.closure),
                                                    zc::mv(ZC_ASSERT_NONNULL(admittedVariables)));
    if (fact == zc::none) { return zc::none; }
    facts.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
  }
  sortProviderCanonical(facts);
  auto admitted = StableBindingSequenceBuilder<StableClosureFreeVariableFact>::from(zc::mv(facts));
  if (admitted == zc::none) { return zc::none; }
  return OwnerBodyFreeVariableProjection(
      zc::heap<owner_body_query_detail::OwnerBodyFreeVariableProjectionData>(
          owner_body_query_detail::OwnerBodyFreeVariableProjectionData{
              zc::mv(ZC_ASSERT_NONNULL(admitted))}));
}

bool OwnerBodyFreeVariableProjection::verify(
    const StableOwnerBodyQueryKey& owner, const BoundModuleSkeleton& skeleton,
    const CanonicalSequence<StableBodyScopeFact>& scopes,
    const CanonicalSequence<StableOwnerLocalBindingFact>& bindings,
    const CanonicalSequence<StableClosureFact>& closures,
    const CanonicalSequence<StableResolutionFact>& resolutions,
    const CanonicalSequence<StableClosureFreeVariableFact>& freeVariables) {
  zc::Vector<PendingClosureFreeVariables> pending(closures.values().size());
  for (size_t closureIndex = closures.values().size(); closureIndex != 0; --closureIndex) {
    const auto& closure = closures.values()[closureIndex - 1];
    if (closure.owner() != owner) { return false; }
    pending.add(
        PendingClosureFreeVariables{closure.closure().clone(), zc::Vector<PendingFreeVariable>()});
  }
  for (size_t resolutionIndex = resolutions.values().size(); resolutionIndex != 0;
       --resolutionIndex) {
    const auto& resolution = resolutions.values()[resolutionIndex - 1];
    if (resolution.owner() != owner || resolution.nameSpace() != Namespace::Value ||
        resolution.origin() != BindingOrigin::LocalDeclaration ||
        resolution.binding() != resolution.canonicalTarget()) {
      continue;
    }
    bool capturable = false;
    auto targetScope =
        verifierCapturableTargetScope(owner, skeleton, bindings, resolution.binding(), capturable);
    if (!capturable) { continue; }
    if (targetScope == zc::none) { return false; }
    for (size_t closureIndex = closures.values().size(); closureIndex != 0; --closureIndex) {
      const auto& closure = closures.values()[closureIndex - 1];
      if (!isStrictAncestorPath(closure.closure().path(), resolution.usePath()) ||
          verifierScopeIsWithin(ZC_ASSERT_NONNULL(targetScope), closure.scope(), skeleton,
                                scopes)) {
        continue;
      }
      if (!appendVerifierFreeVariable(pending, closure.closure(), resolution.binding(),
                                      resolution.usePath())) {
        return false;
      }
    }
  }
  zc::Vector<StableClosureFreeVariableFact> expected(pending.size());
  for (size_t closureIndex = pending.size(); closureIndex != 0; --closureIndex) {
    auto& closure = pending[closureIndex - 1];
    zc::Vector<StableClosureFreeVariable> variables(closure.variables.size());
    for (size_t variableIndex = closure.variables.size(); variableIndex != 0; --variableIndex) {
      auto& variable = closure.variables[variableIndex - 1];
      sortVerifierCanonical(variable.referencePaths);
      auto references = StableBindingSequenceBuilder<LocalSyntaxPath>::fromNonEmpty(
          zc::mv(variable.referencePaths));
      if (references == zc::none) { return false; }
      variables.add(StableClosureFreeVariable::from(zc::mv(variable.target),
                                                    zc::mv(ZC_ASSERT_NONNULL(references))));
    }
    sortVerifierCanonical(variables);
    auto admittedVariables =
        StableBindingSequenceBuilder<StableClosureFreeVariable>::from(zc::mv(variables));
    if (admittedVariables == zc::none) { return false; }
    auto fact = StableClosureFreeVariableFact::from(owner.clone(), zc::mv(closure.closure),
                                                    zc::mv(ZC_ASSERT_NONNULL(admittedVariables)));
    if (fact == zc::none) { return false; }
    expected.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
  }
  sortVerifierCanonical(expected);
  auto admitted =
      StableBindingSequenceBuilder<StableClosureFreeVariableFact>::from(zc::mv(expected));
  return admitted != zc::none && ZC_ASSERT_NONNULL(admitted) == freeVariables;
}

const CanonicalSequence<StableClosureFreeVariableFact>&
OwnerBodyFreeVariableProjection::freeVariables() const noexcept {
  return impl->freeVariables;
}

OwnerBodyExplicitCaptureProjection::OwnerBodyExplicitCaptureProjection(
    zc::Own<owner_body_query_detail::OwnerBodyExplicitCaptureProjectionData>&& impl) noexcept
    : impl(zc::mv(impl)) {}

OwnerBodyExplicitCaptureProjection::~OwnerBodyExplicitCaptureProjection() noexcept(false) = default;
OwnerBodyExplicitCaptureProjection::OwnerBodyExplicitCaptureProjection(
    OwnerBodyExplicitCaptureProjection&&) noexcept = default;
OwnerBodyExplicitCaptureProjection& OwnerBodyExplicitCaptureProjection::operator=(
    OwnerBodyExplicitCaptureProjection&&) noexcept = default;

zc::Maybe<OwnerBodyExplicitCaptureProjection> OwnerBodyExplicitCaptureProjection::from(
    const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
    const BoundModuleSkeleton& skeleton, const CanonicalSequence<StableBodyScopeFact>& scopes,
    const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes,
    const CanonicalSequence<StableOwnerLocalBindingFact>& bindings,
    const CanonicalSequence<StableClosureFact>& closures) {
  auto traversal = OwnerBodySyntaxTraversal::from(syntax);
  if (traversal == zc::none) { return zc::none; }
  const auto entries = ZC_ASSERT_NONNULL(traversal).entries();
  zc::Vector<StableExplicitClosureCaptureFact> rows;
  for (const auto& entry : entries) {
    if (entry.syntaxKind != ast::SyntaxKind::FunctionExpression) { continue; }
    auto captureField = syntax.nodes()[entry.nodeIndex].childField(2);
    if (captureField == zc::none) { return zc::none; }
    if (!ZC_ASSERT_NONNULL(captureField).present) { continue; }
    if (ZC_ASSERT_NONNULL(captureField).childCount != 1) { return zc::none; }
    auto capturePath =
        providerChildPath(entry.path, ZC_ASSERT_NONNULL(captureField).firstChildOrdinal);
    auto captureIndex = capturePath == zc::none
                            ? zc::Maybe<size_t>()
                            : entryAtPath(entries, ZC_ASSERT_NONNULL(capturePath));
    if (captureIndex == zc::none ||
        entries[ZC_ASSERT_NONNULL(captureIndex)].syntaxKind != ast::SyntaxKind::CaptureList)
      return zc::none;
    auto closure = AnonymousOwnerLocalKey::from(owner.owner().clone(), entry.path.clone(),
                                                AnonymousOwnerLocalRole::FunctionExpression);
    auto closureScope = providerNodeScope(entry.path, nodeScopes);
    auto enclosingScope = closureScope == zc::none
                              ? zc::Maybe<StableScopeOwnerKey>()
                              : scopeParent(ZC_ASSERT_NONNULL(closureScope), skeleton, scopes);
    auto items = syntax.nodes()[entries[ZC_ASSERT_NONNULL(captureIndex)].nodeIndex].childField(1);
    if (closure == zc::none || enclosingScope == zc::none || items == zc::none) return zc::none;
    zc::Vector<StableExplicitCaptureBindingFact> captures;
    for (uint32_t ordinal = 0; ordinal < ZC_ASSERT_NONNULL(items).childCount; ++ordinal) {
      auto itemPath = providerChildPath(ZC_ASSERT_NONNULL(capturePath),
                                        ZC_ASSERT_NONNULL(items).firstChildOrdinal + ordinal);
      auto itemIndex = itemPath == zc::none ? zc::Maybe<size_t>()
                                            : entryAtPath(entries, ZC_ASSERT_NONNULL(itemPath));
      if (itemIndex == zc::none ||
          entries[ZC_ASSERT_NONNULL(itemIndex)].syntaxKind != ast::SyntaxKind::CaptureItem)
        return zc::none;
      const auto& item = syntax.nodes()[entries[ZC_ASSERT_NONNULL(itemIndex)].nodeIndex];
      auto name = item.identifierField(1);
      auto mode = explicitCaptureMode(item);
      if (name == zc::none || mode == zc::none) return zc::none;
      zc::Maybe<StableBindingTargetKey> target;
      if (ZC_ASSERT_NONNULL(mode) == StableExplicitCaptureMode::This) {
        auto receiver = providerReceiver(owner, skeleton);
        target =
            receiver == zc::none
                ? zc::Maybe<StableBindingTargetKey>()
                : StableBindingTargetKey::callableParameter(zc::mv(ZC_ASSERT_NONNULL(receiver)));
      } else {
        auto candidates = lexicalCandidates(
            owner, ZC_ASSERT_NONNULL(enclosingScope), ZC_ASSERT_NONNULL(name), Namespace::Value,
            entries, ZC_ASSERT_NONNULL(itemIndex), skeleton, scopes, bindings);
        if (candidates == zc::none || ZC_ASSERT_NONNULL(candidates).values.size() != 1) {
          return zc::none;
        }
        target = ZC_ASSERT_NONNULL(candidates).values[0].binding.clone();
      }
      if (target == zc::none) return zc::none;
      auto capture = StableExplicitCaptureBindingFact::from(ZC_ASSERT_NONNULL(itemPath).clone(),
                                                            zc::mv(ZC_ASSERT_NONNULL(target)),
                                                            ZC_ASSERT_NONNULL(mode));
      if (capture == zc::none) return zc::none;
      captures.add(zc::mv(ZC_ASSERT_NONNULL(capture)));
    }
    sortProviderCanonical(captures);
    auto admitted =
        StableBindingSequenceBuilder<StableExplicitCaptureBindingFact>::from(zc::mv(captures));
    if (admitted == zc::none) return zc::none;
    auto row = StableExplicitClosureCaptureFact::from(
        owner.clone(), ZC_ASSERT_NONNULL(closure).clone(), ZC_ASSERT_NONNULL(capturePath).clone(),
        zc::mv(ZC_ASSERT_NONNULL(admitted)));
    if (row == zc::none) return zc::none;
    rows.add(zc::mv(ZC_ASSERT_NONNULL(row)));
  }
  sortProviderCanonical(rows);
  auto admitted =
      StableBindingSequenceBuilder<StableExplicitClosureCaptureFact>::from(zc::mv(rows));
  if (admitted == zc::none) return zc::none;
  return OwnerBodyExplicitCaptureProjection(
      zc::heap<owner_body_query_detail::OwnerBodyExplicitCaptureProjectionData>(
          owner_body_query_detail::OwnerBodyExplicitCaptureProjectionData{
              zc::mv(ZC_ASSERT_NONNULL(admitted))}));
}

bool OwnerBodyExplicitCaptureProjection::verify(
    const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
    const BoundModuleSkeleton& skeleton, const CanonicalSequence<StableBodyScopeFact>& scopes,
    const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes,
    const CanonicalSequence<StableOwnerLocalBindingFact>& bindings,
    const CanonicalSequence<StableClosureFact>& closures,
    const CanonicalSequence<StableExplicitClosureCaptureFact>& captures) {
  auto traversal = OwnerBodySyntaxTraversal::from(syntax);
  if (traversal == zc::none) { return false; }
  const auto entries = ZC_ASSERT_NONNULL(traversal).entries();
  size_t expectedRows = 0;
  for (size_t entryIndex = entries.size(); entryIndex != 0; --entryIndex) {
    const auto& entry = entries[entryIndex - 1];
    if (entry.kind != DetachedModuleBodyNodeKind::Syntax ||
        entry.syntaxKind != ast::SyntaxKind::FunctionExpression) {
      continue;
    }
    const auto& function = syntax.nodes()[entry.nodeIndex];
    auto captureField = function.childField(2);
    if (captureField == zc::none) { return false; }
    if (!ZC_ASSERT_NONNULL(captureField).present) { continue; }
    if (ZC_ASSERT_NONNULL(captureField).childCount != 1) { return false; }
    auto captureListPath =
        verifierChildPath(entry.path, ZC_ASSERT_NONNULL(captureField).firstChildOrdinal);
    auto captureListIndex = captureListPath == zc::none
                                ? zc::Maybe<size_t>()
                                : entryAtPath(entries, ZC_ASSERT_NONNULL(captureListPath));
    auto captureClosure = AnonymousOwnerLocalKey::from(owner.owner().clone(), entry.path.clone(),
                                                       AnonymousOwnerLocalRole::FunctionExpression);
    auto closureKey = AnonymousOwnerLocalKey::from(owner.owner().clone(), entry.path.clone(),
                                                   AnonymousOwnerLocalRole::Closure);
    auto closureScope = verifierNodeScope(entry.path, nodeScopes);
    auto enclosingScope =
        closureScope == zc::none
            ? zc::Maybe<StableScopeOwnerKey>()
            : verifierScopeParent(ZC_ASSERT_NONNULL(closureScope), skeleton, scopes);
    if (captureListIndex == zc::none ||
        entries[ZC_ASSERT_NONNULL(captureListIndex)].syntaxKind != ast::SyntaxKind::CaptureList ||
        captureClosure == zc::none || closureKey == zc::none || closureScope == zc::none ||
        enclosingScope == zc::none) {
      return false;
    }
    size_t closureMatches = 0;
    for (size_t index = closures.values().size(); index != 0; --index) {
      const auto& candidate = closures.values()[index - 1];
      if (candidate.owner() == owner && candidate.closure() == ZC_ASSERT_NONNULL(closureKey) &&
          candidate.scope() == ZC_ASSERT_NONNULL(closureScope)) {
        ++closureMatches;
      }
    }
    if (closureMatches != 1) { return false; }
    zc::Maybe<size_t> rowIndex;
    for (size_t index = captures.values().size(); index != 0; --index) {
      const auto& candidate = captures.values()[index - 1];
      if (candidate.closure() != ZC_ASSERT_NONNULL(captureClosure)) { continue; }
      if (rowIndex != zc::none) { return false; }
      rowIndex = index - 1;
    }
    if (rowIndex == zc::none) { return false; }
    const auto& row = captures.values()[ZC_ASSERT_NONNULL(rowIndex)];
    if (row.owner() != owner || row.captureListPath() != ZC_ASSERT_NONNULL(captureListPath)) {
      return false;
    }
    auto items =
        syntax.nodes()[entries[ZC_ASSERT_NONNULL(captureListIndex)].nodeIndex].childField(1);
    if (items == zc::none ||
        row.captures().values().size() != ZC_ASSERT_NONNULL(items).childCount) {
      return false;
    }
    for (uint32_t ordinal = 0; ordinal < ZC_ASSERT_NONNULL(items).childCount; ++ordinal) {
      auto itemPath = verifierChildPath(ZC_ASSERT_NONNULL(captureListPath),
                                        ZC_ASSERT_NONNULL(items).firstChildOrdinal + ordinal);
      if (itemPath == zc::none) { return false; }
      zc::Maybe<size_t> captureIndex;
      for (size_t index = row.captures().values().size(); index != 0; --index) {
        if (row.captures().values()[index - 1].itemPath() != ZC_ASSERT_NONNULL(itemPath)) {
          continue;
        }
        if (captureIndex != zc::none) { return false; }
        captureIndex = index - 1;
      }
      if (captureIndex == zc::none) { return false; }
      const auto& capture = row.captures().values()[ZC_ASSERT_NONNULL(captureIndex)];
      auto itemIndex = entryAtPath(entries, ZC_ASSERT_NONNULL(itemPath));
      if (itemIndex == zc::none ||
          entries[ZC_ASSERT_NONNULL(itemIndex)].syntaxKind != ast::SyntaxKind::CaptureItem) {
        return false;
      }
      const auto& item = syntax.nodes()[entries[ZC_ASSERT_NONNULL(itemIndex)].nodeIndex];
      auto name = item.identifierField(1);
      auto mode = explicitCaptureMode(item);
      if (name == zc::none || mode == zc::none || ZC_ASSERT_NONNULL(mode) != capture.mode()) {
        return false;
      }
      zc::Maybe<StableBindingTargetKey> target;
      if (ZC_ASSERT_NONNULL(mode) == StableExplicitCaptureMode::This) {
        auto receiver = verifierReceiver(owner, skeleton);
        target =
            receiver == zc::none
                ? zc::Maybe<StableBindingTargetKey>()
                : StableBindingTargetKey::callableParameter(zc::mv(ZC_ASSERT_NONNULL(receiver)));
      } else {
        auto candidates = verifierLexicalCandidates(
            owner, ZC_ASSERT_NONNULL(enclosingScope), ZC_ASSERT_NONNULL(name), Namespace::Value,
            entries, ZC_ASSERT_NONNULL(itemIndex), skeleton, scopes, bindings);
        if (candidates == zc::none || ZC_ASSERT_NONNULL(candidates).values.size() != 1) {
          return false;
        }
        target = ZC_ASSERT_NONNULL(candidates).values[0].binding.clone();
      }
      if (target == zc::none || ZC_ASSERT_NONNULL(target) != capture.target()) { return false; }
    }
    ++expectedRows;
  }
  return expectedRows == captures.values().size();
}

const CanonicalSequence<StableExplicitClosureCaptureFact>&
OwnerBodyExplicitCaptureProjection::captures() const noexcept {
  return impl->captures;
}

OwnerBodyLabelProjection::OwnerBodyLabelProjection(
    zc::Own<owner_body_query_detail::OwnerBodyLabelProjectionData>&& impl) noexcept
    : impl(zc::mv(impl)) {}

OwnerBodyLabelProjection::~OwnerBodyLabelProjection() noexcept(false) = default;
OwnerBodyLabelProjection::OwnerBodyLabelProjection(OwnerBodyLabelProjection&&) noexcept = default;
OwnerBodyLabelProjection& OwnerBodyLabelProjection::operator=(OwnerBodyLabelProjection&&) noexcept =
    default;

zc::Maybe<OwnerBodyLabelProjection> OwnerBodyLabelProjection::from(
    const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
    const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes) {
  auto traversal = OwnerBodySyntaxTraversal::from(syntax);
  if (traversal == zc::none) { return zc::none; }
  const auto entries = ZC_ASSERT_NONNULL(traversal).entries();
  zc::Vector<StableLabelFact> facts;
  for (const auto& entry : entries) {
    if (entry.kind != DetachedModuleBodyNodeKind::Syntax ||
        entry.syntaxKind != ast::SyntaxKind::LabeledStatement) {
      continue;
    }
    const auto& node = syntax.nodes()[entry.nodeIndex];
    auto name = node.identifierField(0);
    auto child = node.childField(1);
    if (name == zc::none || child == zc::none || !ZC_ASSERT_NONNULL(child).present ||
        ZC_ASSERT_NONNULL(child).childCount != 1) {
      return zc::none;
    }
    auto statementPath = providerChildPath(entry.path, ZC_ASSERT_NONNULL(child).firstChildOrdinal);
    if (statementPath == zc::none) { return zc::none; }
    auto statement = providerEntryAtPath(entries, ZC_ASSERT_NONNULL(statementPath));
    auto scope = providerNodeScope(ZC_ASSERT_NONNULL(statementPath), nodeScopes);
    if (statement == zc::none || scope == zc::none) { return zc::none; }
    auto target = labelTargetForSyntaxKind(ZC_ASSERT_NONNULL(statement).syntaxKind,
                                           zc::mv(ZC_ASSERT_NONNULL(scope)));
    if (target == zc::none) { return zc::none; }
    auto key = StableLabelKey::from(owner.clone(), entry.path.clone());
    auto fact = StableLabelFact::from(zc::mv(key), zc::mv(ZC_ASSERT_NONNULL(name)),
                                      zc::mv(ZC_ASSERT_NONNULL(statementPath)),
                                      zc::mv(ZC_ASSERT_NONNULL(target)));
    if (fact == zc::none) { return zc::none; }
    facts.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
  }
  sortProviderCanonical(facts);
  auto labels = StableBindingSequenceBuilder<StableLabelFact>::from(zc::mv(facts));
  if (labels == zc::none) { return zc::none; }
  return OwnerBodyLabelProjection(zc::heap<owner_body_query_detail::OwnerBodyLabelProjectionData>(
      owner_body_query_detail::OwnerBodyLabelProjectionData{zc::mv(ZC_ASSERT_NONNULL(labels))}));
}

bool OwnerBodyLabelProjection::verify(const StableOwnerBodyQueryKey& owner,
                                      const ModuleBodySyntax& syntax,
                                      const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes,
                                      const CanonicalSequence<StableLabelFact>& labels) {
  zc::Vector<VerifierLabelNode> entries;
  zc::Vector<VerifierClosurePendingNode> pending;
  uint32_t rootIndex = 0;
  for (size_t nodeIndex = 0; nodeIndex < syntax.nodes().size(); ++nodeIndex) {
    while (!pending.empty() && pending.back().nextChild == pending.back().childCount) {
      pending.removeLast();
    }
    const auto& node = syntax.nodes()[nodeIndex];
    zc::Maybe<LocalSyntaxPath> path;
    if (pending.empty()) {
      if (rootIndex == syntax.rootCount()) { return false; }
      path = verifierRootPath(rootIndex++);
    } else {
      auto& parent = pending.back();
      path = verifierChildPath(parent.path, parent.nextChild++);
    }
    const auto syntaxKind = node.syntaxKind();
    if (path == zc::none ||
        (node.kind() != DetachedModuleBodyNodeKind::Syntax && node.childCount()) ||
        (node.kind() == DetachedModuleBodyNodeKind::Syntax) != (syntaxKind != zc::none)) {
      return false;
    }
    if (node.kind() != DetachedModuleBodyNodeKind::Syntax) { continue; }
    entries.add(VerifierLabelNode{ZC_ASSERT_NONNULL(path).clone(), static_cast<uint32_t>(nodeIndex),
                                  ZC_ASSERT_NONNULL(syntaxKind)});
    if (node.childCount() != 0) {
      pending.add(
          VerifierClosurePendingNode{zc::mv(ZC_ASSERT_NONNULL(path)), node.childCount(), 0});
    }
  }
  while (!pending.empty() && pending.back().nextChild == pending.back().childCount) {
    pending.removeLast();
  }
  if (!pending.empty() || rootIndex != syntax.rootCount()) { return false; }

  zc::Vector<StableLabelFact> expected;
  for (const auto& entry : entries) {
    if (entry.syntaxKind != ast::SyntaxKind::LabeledStatement) { continue; }
    const auto& node = syntax.nodes()[entry.nodeIndex];
    auto name = node.identifierField(0);
    auto child = node.childField(1);
    if (name == zc::none || child == zc::none || !ZC_ASSERT_NONNULL(child).present ||
        ZC_ASSERT_NONNULL(child).childCount != 1) {
      return false;
    }
    auto statementPath = verifierChildPath(entry.path, ZC_ASSERT_NONNULL(child).firstChildOrdinal);
    if (statementPath == zc::none) { return false; }
    auto statement = verifierEntryAtPath(entries.asPtr(), ZC_ASSERT_NONNULL(statementPath));
    auto scope = verifierNodeScope(ZC_ASSERT_NONNULL(statementPath), nodeScopes);
    if (statement == zc::none || scope == zc::none) { return false; }
    auto target = labelTargetForSyntaxKind(ZC_ASSERT_NONNULL(statement).syntaxKind,
                                           zc::mv(ZC_ASSERT_NONNULL(scope)));
    if (target == zc::none) { return false; }
    auto key = StableLabelKey::from(owner.clone(), entry.path.clone());
    auto fact = StableLabelFact::from(zc::mv(key), zc::mv(ZC_ASSERT_NONNULL(name)),
                                      zc::mv(ZC_ASSERT_NONNULL(statementPath)),
                                      zc::mv(ZC_ASSERT_NONNULL(target)));
    if (fact == zc::none) { return false; }
    expected.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
  }
  sortVerifierCanonical(expected);
  auto admitted = StableBindingSequenceBuilder<StableLabelFact>::from(zc::mv(expected));
  return admitted != zc::none && ZC_ASSERT_NONNULL(admitted) == labels;
}

const CanonicalSequence<StableLabelFact>& OwnerBodyLabelProjection::labels() const noexcept {
  return impl->labels;
}

OwnerBodyControlProjection::OwnerBodyControlProjection(
    zc::Own<owner_body_query_detail::OwnerBodyControlProjectionData>&& impl) noexcept
    : impl(zc::mv(impl)) {}

OwnerBodyControlProjection::~OwnerBodyControlProjection() noexcept(false) = default;
OwnerBodyControlProjection::OwnerBodyControlProjection(OwnerBodyControlProjection&&) noexcept =
    default;
OwnerBodyControlProjection& OwnerBodyControlProjection::operator=(
    OwnerBodyControlProjection&&) noexcept = default;

zc::Maybe<OwnerBodyControlProjection> OwnerBodyControlProjection::from(
    const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
    const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes,
    const CanonicalSequence<StableLabelFact>& labels) {
  auto traversal = OwnerBodySyntaxTraversal::from(syntax);
  if (traversal == zc::none ||
      !OwnerBodyLabelProjection::verify(owner, syntax, nodeScopes, labels)) {
    return zc::none;
  }
  const auto entries = ZC_ASSERT_NONNULL(traversal).entries();
  zc::Vector<StableControlTransferFact> facts;
  for (const auto& entry : entries) {
    if (entry.kind != DetachedModuleBodyNodeKind::Syntax ||
        (entry.syntaxKind != ast::SyntaxKind::BreakStmt &&
         entry.syntaxKind != ast::SyntaxKind::ContinueStatement)) {
      continue;
    }
    const auto kind = entry.syntaxKind == ast::SyntaxKind::BreakStmt
                          ? ControlTransferKind::Break
                          : ControlTransferKind::Continue;
    auto labelName = syntax.nodes()[entry.nodeIndex].identifierField(0);
    zc::Maybe<StableControlTarget> target;
    if (labelName != zc::none) {
      target = explicitControlTarget(ZC_ASSERT_NONNULL(labelName), entry.path, kind, labels);
    } else {
      uint32_t ancestorIndex = entry.parentIndex;
      while (ancestorIndex != kNoParent) {
        if (ancestorIndex >= entries.size()) { return zc::none; }
        const auto& ancestor = entries[ancestorIndex];
        if (isLoopSyntaxKind(ancestor.syntaxKind)) {
          auto scope = providerNodeScope(ancestor.path, nodeScopes);
          if (scope == zc::none) { return zc::none; }
          target = StableControlTarget::loop(zc::mv(ZC_ASSERT_NONNULL(scope)));
          break;
        }
        if (kind == ControlTransferKind::Break &&
            ancestor.syntaxKind == ast::SyntaxKind::MatchStmt) {
          auto scope = providerNodeScope(ancestor.path, nodeScopes);
          if (scope == zc::none) { return zc::none; }
          target = StableControlTarget::match(zc::mv(ZC_ASSERT_NONNULL(scope)));
          break;
        }
        ancestorIndex = ancestor.parentIndex;
      }
    }
    if (target == zc::none) { return zc::none; }
    auto fact = StableControlTransferFact::from(owner.clone(), entry.path.clone(), kind,
                                                zc::mv(ZC_ASSERT_NONNULL(target)));
    if (fact == zc::none) { return zc::none; }
    facts.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
  }
  sortProviderCanonical(facts);
  auto transfers = StableBindingSequenceBuilder<StableControlTransferFact>::from(zc::mv(facts));
  if (transfers == zc::none) { return zc::none; }
  return OwnerBodyControlProjection(
      zc::heap<owner_body_query_detail::OwnerBodyControlProjectionData>(
          owner_body_query_detail::OwnerBodyControlProjectionData{
              zc::mv(ZC_ASSERT_NONNULL(transfers))}));
}

bool OwnerBodyControlProjection::verify(
    const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
    const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes,
    const CanonicalSequence<StableLabelFact>& labels,
    const CanonicalSequence<StableControlTransferFact>& transfers) {
  if (!OwnerBodyLabelProjection::verify(owner, syntax, nodeScopes, labels)) { return false; }
  zc::Vector<VerifierControlNode> entries;
  zc::Vector<VerifierControlPendingNode> pending;
  uint32_t rootIndex = 0;
  for (size_t nodeIndex = 0; nodeIndex < syntax.nodes().size(); ++nodeIndex) {
    while (!pending.empty() && pending.back().nextChild == pending.back().childCount) {
      pending.removeLast();
    }
    const auto& node = syntax.nodes()[nodeIndex];
    zc::Maybe<LocalSyntaxPath> path;
    uint32_t parentIndex = kNoParent;
    if (pending.empty()) {
      if (rootIndex == syntax.rootCount()) { return false; }
      path = verifierRootPath(rootIndex++);
    } else {
      auto& parent = pending.back();
      path = verifierChildPath(parent.path, parent.nextChild++);
      parentIndex = parent.nodeIndex;
    }
    const auto syntaxKind = node.syntaxKind();
    if (path == zc::none ||
        (node.kind() != DetachedModuleBodyNodeKind::Syntax && node.childCount()) ||
        (node.kind() == DetachedModuleBodyNodeKind::Syntax) != (syntaxKind != zc::none)) {
      return false;
    }
    if (node.kind() != DetachedModuleBodyNodeKind::Syntax) { continue; }
    entries.add(VerifierControlNode{ZC_ASSERT_NONNULL(path).clone(),
                                    static_cast<uint32_t>(nodeIndex), parentIndex,
                                    ZC_ASSERT_NONNULL(syntaxKind)});
    if (node.childCount() != 0) {
      pending.add(VerifierControlPendingNode{zc::mv(ZC_ASSERT_NONNULL(path)),
                                             static_cast<uint32_t>(entries.size() - 1),
                                             node.childCount(), 0});
    }
  }
  while (!pending.empty() && pending.back().nextChild == pending.back().childCount) {
    pending.removeLast();
  }
  if (!pending.empty() || rootIndex != syntax.rootCount()) { return false; }

  zc::Vector<StableControlTransferFact> expected;
  for (const auto& entry : entries) {
    if (entry.syntaxKind != ast::SyntaxKind::BreakStmt &&
        entry.syntaxKind != ast::SyntaxKind::ContinueStatement) {
      continue;
    }
    const auto kind = entry.syntaxKind == ast::SyntaxKind::BreakStmt
                          ? ControlTransferKind::Break
                          : ControlTransferKind::Continue;
    auto labelName = syntax.nodes()[entry.nodeIndex].identifierField(0);
    zc::Maybe<StableControlTarget> target;
    if (labelName != zc::none) {
      target = explicitControlTarget(ZC_ASSERT_NONNULL(labelName), entry.path, kind, labels);
    } else {
      uint32_t ancestorIndex = entry.parentIndex;
      while (ancestorIndex != kNoParent) {
        if (ancestorIndex >= entries.size()) { return false; }
        const auto& ancestor = entries[ancestorIndex];
        if (isLoopSyntaxKind(ancestor.syntaxKind)) {
          auto scope = verifierNodeScope(ancestor.path, nodeScopes);
          if (scope == zc::none) { return false; }
          target = StableControlTarget::loop(zc::mv(ZC_ASSERT_NONNULL(scope)));
          break;
        }
        if (kind == ControlTransferKind::Break &&
            ancestor.syntaxKind == ast::SyntaxKind::MatchStmt) {
          auto scope = verifierNodeScope(ancestor.path, nodeScopes);
          if (scope == zc::none) { return false; }
          target = StableControlTarget::match(zc::mv(ZC_ASSERT_NONNULL(scope)));
          break;
        }
        ancestorIndex = ancestor.parentIndex;
      }
    }
    if (target == zc::none) { return false; }
    auto fact = StableControlTransferFact::from(owner.clone(), entry.path.clone(), kind,
                                                zc::mv(ZC_ASSERT_NONNULL(target)));
    if (fact == zc::none) { return false; }
    expected.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
  }
  sortVerifierCanonical(expected);
  auto admitted = StableBindingSequenceBuilder<StableControlTransferFact>::from(zc::mv(expected));
  return admitted != zc::none && ZC_ASSERT_NONNULL(admitted) == transfers;
}

const CanonicalSequence<StableControlTransferFact>& OwnerBodyControlProjection::transfers()
    const noexcept {
  return impl->transfers;
}

}  // namespace zomlang::compiler::binder
