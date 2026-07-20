// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/internal/binding-skeleton.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"

namespace zomlang::compiler::binder {
namespace {

enum class SkeletonEligibility : uint8_t {
  Value,
  SpecialCallable,
  Closure,
  Pattern,
  Type,
  Generic,
  Parameter,
  Deferred
};

SkeletonEligibility eligibility(identity::DefinitionKind kind) {
  using identity::DefinitionKind;
  switch (kind) {
    case DefinitionKind::Function:
    case DefinitionKind::Method:
    case DefinitionKind::Field:
    case DefinitionKind::EnumVariant:
    case DefinitionKind::Constant:
    case DefinitionKind::Static:
      return SkeletonEligibility::Value;
    case DefinitionKind::Constructor:
    case DefinitionKind::Destructor:
      return SkeletonEligibility::SpecialCallable;
    case DefinitionKind::Closure:
      return SkeletonEligibility::Closure;
    case DefinitionKind::PatternBinding:
      return SkeletonEligibility::Pattern;
    case DefinitionKind::Class:
    case DefinitionKind::Struct:
    case DefinitionKind::Interface:
    case DefinitionKind::Enum:
    case DefinitionKind::Error:
    case DefinitionKind::TypeAlias:
    case DefinitionKind::AssociatedType:
      return SkeletonEligibility::Type;
    case DefinitionKind::TypeParameter:
      return SkeletonEligibility::Generic;
    case DefinitionKind::Parameter:
      return SkeletonEligibility::Parameter;
    case DefinitionKind::ModuleAlias:
    case DefinitionKind::Local:
      return SkeletonEligibility::Deferred;
    default:
      ZC_UNREACHABLE;
  }
}

zc::Maybe<ast::NodeId> parameterListNode(const ast::Node& syntax) {
  switch (syntax.kind) {
    case ast::SyntaxKind::FunctionDecl:
      return ast::NodeId(syntax.payload.words[ast::kFunctionDeclParamsIdWord]);
    case ast::SyntaxKind::MethodDecl:
      return ast::NodeId(syntax.payload.words[ast::kMethodDeclParamsIdWord]);
    case ast::SyntaxKind::ConstructorDecl:
      return ast::NodeId(syntax.payload.words[ast::kConstructorDeclParamsIdWord]);
    case ast::SyntaxKind::DestructorDecl:
      return ast::NodeId(syntax.payload.words[ast::kDestructorDeclParamsIdWord]);
    case ast::SyntaxKind::FunctionExpression:
      return ast::NodeId(syntax.payload.words[ast::kFunctionExpressionParamsIdWord]);
    case ast::SyntaxKind::LambdaExpression:
      return ast::NodeId(syntax.payload.words[ast::kLambdaExpressionParamsIdWord]);
    default:
      return zc::none;
  }
}

bool parameterListContains(const ast::Tree& tree, ast::NodeId listNode, ast::NodeId parameter) {
  if (!tree.contains(listNode) ||
      tree.node(listNode).kind != ast::SyntaxKind::FunctionParameterList) {
    return false;
  }
  const auto& list = tree.node(listNode);
  ast::NodeList parameters{list.payload.words[ast::kFunctionParameterListParamsFirstWord],
                           list.payload.words[ast::kFunctionParameterListParamsSizeWord]};
  for (ast::NodeId candidate : tree.list(parameters)) {
    if (candidate == parameter) { return true; }
  }
  return false;
}

bool externParametersContain(const ast::Tree& tree, const ast::Node& syntax,
                             ast::NodeId parameter) {
  if (syntax.kind != ast::SyntaxKind::ExternDecl) { return false; }
  ast::NodeList parameters{syntax.payload.words[ast::kExternDeclParamsFirstWord],
                           syntax.payload.words[ast::kExternDeclParamsSizeWord]};
  for (ast::NodeId candidate : tree.list(parameters)) {
    if (candidate == parameter) { return true; }
  }
  return false;
}

zc::Maybe<ast::NodeId> parameterOwner(const ast::Tree& tree, ast::NodeId parameter,
                                      bool& ambiguous) {
  zc::Maybe<ast::NodeId> result;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    bool containsParameter = externParametersContain(tree, syntax, parameter);
    ZC_IF_SOME(listNode, parameterListNode(syntax)) {
      containsParameter = parameterListContains(tree, listNode, parameter);
    }
    if (!containsParameter) { return; }
    if (result != zc::none) {
      ambiguous = true;
      return;
    }
    result = node;
  });
  return result;
}

zc::Maybe<ast::NodeId> genericParamsNode(const ast::Node& syntax) {
  switch (syntax.kind) {
    case ast::SyntaxKind::EnumDeclaration:
      return ast::NodeId(syntax.payload.words[ast::kEnumDeclarationTypeParamsIdWord]);
    case ast::SyntaxKind::FunctionExpression:
      return ast::NodeId(syntax.payload.words[ast::kFunctionExpressionTypeParamsIdWord]);
    case ast::SyntaxKind::StandaloneImplDecl:
      return ast::NodeId(syntax.payload.words[ast::kStandaloneImplDeclTypeParamsIdWord]);
    case ast::SyntaxKind::FunctionDecl:
      return ast::NodeId(syntax.payload.words[ast::kFunctionDeclTypeParamsIdWord]);
    case ast::SyntaxKind::ClassDecl:
      return ast::NodeId(syntax.payload.words[ast::kClassDeclTypeParamsIdWord]);
    case ast::SyntaxKind::StructDecl:
      return ast::NodeId(syntax.payload.words[ast::kStructDeclTypeParamsIdWord]);
    case ast::SyntaxKind::InterfaceDecl:
      return ast::NodeId(syntax.payload.words[ast::kInterfaceDeclTypeParamsIdWord]);
    case ast::SyntaxKind::MethodDecl:
      return ast::NodeId(syntax.payload.words[ast::kMethodDeclTypeParamsIdWord]);
    default:
      return zc::none;
  }
}

zc::Maybe<ast::NodeId> genericOwner(const ast::Tree& tree, ast::NodeId parameter, bool& ambiguous) {
  zc::Maybe<ast::NodeId> result;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    auto genericParams = genericParamsNode(syntax);
    ZC_IF_SOME(paramsNode, genericParams) {
      if (!tree.contains(paramsNode) ||
          tree.node(paramsNode).kind != ast::SyntaxKind::GenericParams) {
        return;
      }
      const auto& params = tree.node(paramsNode);
      ast::NodeList list{params.payload.words[ast::kGenericParamsParamsFirstWord],
                         params.payload.words[ast::kGenericParamsParamsSizeWord]};
      for (ast::NodeId candidate : tree.list(list)) {
        if (candidate != parameter) { continue; }
        if (result != zc::none) {
          ambiguous = true;
          return;
        }
        result = node;
      }
    }
  });
  return result;
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

bool isInterfaceOwner(const VerifiedBindingInput& input, const ScopeRecord& scope) {
  if (scope.kind != ScopeKind::TypeBody || !scope.owner.value().is<DefinitionScopeOwner>()) {
    return false;
  }
  const auto owner = scope.owner.value().get<DefinitionScopeOwner>().definition;
  for (const auto& definition : input.definitions().definitions()) {
    if (definition.definition == owner) {
      return definition.record.kind() == identity::DefinitionKind::Interface;
    }
  }
  return false;
}

zc::Maybe<MemberVisibility> memberVisibility(const VerifiedBindingInput& input,
                                             const ScopeRecord& scope, const ast::Node& syntax,
                                             bool& invalid) {
  uint32_t encoded = 0;
  switch (syntax.kind) {
    case ast::SyntaxKind::MethodDecl:
      encoded = syntax.payload.words[ast::kMethodDeclVisibilityWord];
      break;
    case ast::SyntaxKind::FieldDecl:
      encoded = syntax.payload.words[ast::kFieldDeclVisibilityWord];
      break;
    case ast::SyntaxKind::ConstructorDecl:
      encoded = syntax.payload.words[ast::kConstructorDeclVisibilityWord];
      break;
    case ast::SyntaxKind::DestructorDecl:
      encoded = syntax.payload.words[ast::kDestructorDeclVisibilityWord];
      break;
    case ast::SyntaxKind::ClassConstDecl:
      encoded = syntax.payload.words[ast::kClassConstDeclVisibilityWord];
      break;
    default:
      return zc::none;
  }
  switch (encoded) {
    case 0:
      return isInterfaceOwner(input, scope) ? MemberVisibility::Public : MemberVisibility::Private;
    case 1:
      return MemberVisibility::Public;
    case 2:
      return MemberVisibility::Private;
    case 3:
      return MemberVisibility::Protected;
    default:
      invalid = true;
      return zc::none;
  }
}

bool hasLexicalBinding(SkeletonEligibility classification) {
  return classification != SkeletonEligibility::SpecialCallable &&
         classification != SkeletonEligibility::Closure;
}

bool isReceiverParameter(const VerifiedBindingInput& input,
                         const FrozenCallableParameterEntry& definition) {
  return definition.record.position().kind() == identity::CallableParameterPositionKind::Receiver &&
         input.tree().contains(definition.node) &&
         input.tree().node(definition.node).kind == ast::SyntaxKind::FunctionParameterDecl &&
         input.parsedModule().functionParameterNameSpan(definition.node,
                                                        ast::SyntaxKind::ThisKeyword) != zc::none;
}

struct ReceiverCandidate final {
  ScopeId scope;
  identity::CallableParameterId parameter;
  ast::NodeId node;
  identity::SourceSpan source;
};

bool receiverLess(const ReceiverCandidate& left, const ReceiverCandidate& right) {
  if (left.scope.index() != right.scope.index()) {
    return left.scope.index() < right.scope.index();
  }
  if (left.source.byteStart() != right.source.byteStart()) {
    return left.source.byteStart() < right.source.byteStart();
  }
  if (left.source.byteEnd() != right.source.byteEnd()) {
    return left.source.byteEnd() < right.source.byteEnd();
  }
  return left.node.value < right.node.value;
}

zc::Maybe<DefinitionActivation> patternActivation(const ast::Tree& tree,
                                                  const DefinitionSite& site) {
  if (!site.value().is<PatternBindingSite>()) { return zc::none; }
  const auto introducer = site.value().get<PatternBindingSite>().introducer;
  if (!tree.contains(introducer)) { return zc::none; }
  switch (tree.node(introducer).kind) {
    case ast::SyntaxKind::ForInStatement:
      return DefinitionActivation::LoopPattern;
    case ast::SyntaxKind::MatchArmStmt:
      return DefinitionActivation::MatchPattern;
    default:
      return zc::none;
  }
}

BinderInvariantFact failure(const VerifiedBindingInput& input, BinderInvariantKind kind,
                            ast::NodeId node) {
  return BinderInvariantFact{kind, input.module(), zc::none, BinderEmitterSite::ModuleSkeleton,
                             node.value};
}

int compareBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  const size_t count = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < count; ++index) {
    if (left[index] < right[index]) { return -1; }
    if (left[index] > right[index]) { return 1; }
  }
  if (left.size() < right.size()) { return -1; }
  if (left.size() > right.size()) { return 1; }
  return 0;
}

zc::Maybe<ScopeId> scopeForNode(const ScopeArenaCandidate& arena, ast::NodeId node) {
  for (const auto& fact : arena.nodeScopes) {
    if (fact.node == node) { return fact.scope; }
  }
  return zc::none;
}

bool sameSpan(const identity::SourceSpan& left, const identity::SourceSpan& right) {
  return left.byteStart() == right.byteStart() && left.byteEnd() == right.byteEnd();
}

bool contains(const identity::SourceSpan& parent, const identity::SourceSpan& child) {
  return parent.byteStart() <= child.byteStart() && child.byteEnd() <= parent.byteEnd();
}

bool bindingLess(const ScopeBindingEntry& left, const ScopeBindingEntry& right) {
  if (left.name.nameSpace() != right.name.nameSpace()) {
    return static_cast<uint8_t>(left.name.nameSpace()) <
           static_cast<uint8_t>(right.name.nameSpace());
  }
  if (left.name.name() != right.name.name()) { return left.name.name() < right.name.name(); }
  if (left.binding.declarationSpan.byteStart() != right.binding.declarationSpan.byteStart()) {
    return left.binding.declarationSpan.byteStart() < right.binding.declarationSpan.byteStart();
  }
  return left.binding.declarationSpan.byteEnd() < right.binding.declarationSpan.byteEnd();
}

bool sameBindingName(const ScopeBindingEntry& left, const ScopeBindingEntry& right) {
  return left.name.nameSpace() == right.name.nameSpace() && left.name.name() == right.name.name();
}

void sortBindings(zc::Vector<ScopeBindingEntry>& bindings) {
  for (size_t index = 1; index < bindings.size(); ++index) {
    auto current = zc::mv(bindings[index]);
    size_t insertion = index;
    while (insertion > 0 && bindingLess(current, bindings[insertion - 1])) {
      bindings[insertion] = zc::mv(bindings[insertion - 1]);
      --insertion;
    }
    bindings[insertion] = zc::mv(current);
  }
}

bool seedLess(const ModuleSkeletonSurfaceSeed& left, const ModuleSkeletonSurfaceSeed& right) {
  if (left.name.nameSpace() != right.name.nameSpace()) {
    return static_cast<uint8_t>(left.name.nameSpace()) <
           static_cast<uint8_t>(right.name.nameSpace());
  }
  return left.name.name() < right.name.name();
}

void sortSurfaceSeeds(zc::Vector<ModuleSkeletonSurfaceSeed>& seeds) {
  for (size_t index = 1; index < seeds.size(); ++index) {
    auto current = zc::mv(seeds[index]);
    size_t insertion = index;
    while (insertion > 0 && seedLess(current, seeds[insertion - 1])) {
      seeds[insertion] = zc::mv(seeds[insertion - 1]);
      --insertion;
    }
    seeds[insertion] = zc::mv(current);
  }
}

zc::Maybe<const DefinitionFact&> definitionFact(const DefinitionSkeletonCandidate& candidate,
                                                identity::DefId identity) {
  for (const auto& fact : candidate.definitions) {
    if (fact.identity == identity) { return fact; }
  }
  return zc::none;
}

zc::Maybe<BinderDiagnosticCode> redeclarationCode(identity::DefinitionKind kind) {
  using identity::DefinitionKind;
  switch (kind) {
    case DefinitionKind::Function:
    case DefinitionKind::Method:
    case DefinitionKind::Constructor:
    case DefinitionKind::Destructor:
      return BinderDiagnosticCode::RedeclareFunction;
    case DefinitionKind::Class:
      return BinderDiagnosticCode::RedeclareClass;
    case DefinitionKind::Interface:
      return BinderDiagnosticCode::RedeclareInterface;
    case DefinitionKind::Enum:
      return BinderDiagnosticCode::RedeclareEnum;
    case DefinitionKind::TypeAlias:
    case DefinitionKind::AssociatedType:
      return BinderDiagnosticCode::RedeclareTypeAlias;
    case DefinitionKind::Parameter:
      return BinderDiagnosticCode::RedeclareParameter;
    case DefinitionKind::Field:
    case DefinitionKind::Constant:
    case DefinitionKind::Static:
    case DefinitionKind::Local:
    case DefinitionKind::PatternBinding:
      return BinderDiagnosticCode::RedeclareVariable;
    case DefinitionKind::ModuleAlias:
    case DefinitionKind::Struct:
    case DefinitionKind::Error:
    case DefinitionKind::EnumVariant:
    case DefinitionKind::TypeParameter:
      return BinderDiagnosticCode::DuplicateIdentifier;
    case DefinitionKind::Closure:
      return zc::none;
    default:
      ZC_UNREACHABLE;
  }
}

zc::Maybe<BinderDiagnosticCode> redeclarationCode(const DefinitionSkeletonCandidate& candidate,
                                                  const BindingTarget& target) {
  const auto& value = target.value();
  if (value.is<DefinitionBindingTarget>()) {
    auto fact = definitionFact(candidate, value.get<DefinitionBindingTarget>().definition);
    ZC_IF_SOME(definition, fact) { return redeclarationCode(definition.kind); }
    return zc::none;
  }
  if (value.is<GenericParameterBindingTarget>()) {
    const auto parameter = value.get<GenericParameterBindingTarget>().parameter;
    for (const auto& fact : candidate.genericParameters) {
      if (fact.identity == parameter) { return BinderDiagnosticCode::DuplicateIdentifier; }
    }
    return zc::none;
  }
  if (value.is<CallableParameterBindingTarget>()) {
    const auto parameter = value.get<CallableParameterBindingTarget>().parameter;
    for (const auto& fact : candidate.callableParameters) {
      if (fact.identity == parameter) { return BinderDiagnosticCode::RedeclareParameter; }
    }
  }
  return zc::none;
}

zc::Maybe<ast::NodeId> bindingNode(const VerifiedBindingInput& input, const BindingTarget& target) {
  const auto& value = target.value();
  if (value.is<DefinitionBindingTarget>()) {
    const auto definition = value.get<DefinitionBindingTarget>().definition;
    for (const auto& entry : input.definitions().definitions()) {
      if (entry.definition == definition) { return entry.node; }
    }
    return zc::none;
  }
  if (value.is<GenericParameterBindingTarget>()) {
    const auto parameter = value.get<GenericParameterBindingTarget>().parameter;
    for (const auto& entry : input.definitions().genericParameters()) {
      if (entry.parameter == parameter) { return entry.node; }
    }
    return zc::none;
  }
  if (value.is<CallableParameterBindingTarget>()) {
    const auto parameter = value.get<CallableParameterBindingTarget>().parameter;
    for (const auto& entry : input.definitions().callableParameters()) {
      if (entry.parameter == parameter) { return entry.node; }
    }
  }
  return zc::none;
}

bool isRejected(const DefinitionSkeletonCandidate& candidate, identity::DefId identity) {
  for (const auto& duplicate : candidate.duplicates) {
    const auto& target = duplicate.rejected.value();
    if (target.is<DefinitionBindingTarget>() &&
        target.get<DefinitionBindingTarget>().definition == identity) {
      return true;
    }
  }
  return false;
}

zc::Maybe<ast::NodeId> declarationExport(const ast::Tree& tree, ast::NodeId target,
                                         bool& ambiguous) {
  zc::Maybe<ast::NodeId> result;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    if (syntax.kind != ast::SyntaxKind::ExportDeclaration) { return; }
    const ast::NodeId declaration(syntax.payload.words[ast::kExportDeclarationDeclarationWord]);
    if (!tree.contains(declaration)) { return; }
    bool containsTarget = false;
    ast::visitTreePreOrder(tree, declaration, [&](ast::NodeId child, const ast::Node&) {
      if (child == target) { containsTarget = true; }
    });
    if (!containsTarget) { return; }
    if (result != zc::none) {
      ambiguous = true;
      return;
    }
    result = node;
  });
  return result;
}

}  // namespace

ModuleSkeletonSurfaceSeed::ModuleSkeletonSurfaceSeed(
    BindingNameKey&& name, identity::DefId identity, identity::SourceSpan&& source, bool exported,
    zc::Maybe<identity::SourceSpan>&& exportSpan) noexcept
    : name(zc::mv(name)),
      identity(identity),
      source(zc::mv(source)),
      exported(exported),
      exportSpan(zc::mv(exportSpan)) {}

DefinitionSkeletonBuildResult BindingSkeletonBuilder::build(const VerifiedBindingInput& input,
                                                            ScopeArenaCandidate& arena) {
  if (arena.scopes.empty() || arena.scopes[0].kind != ScopeKind::Module) {
    return failure(input, BinderInvariantKind::MalformedScopeGraph, input.tree().root());
  }
  const auto inventory = input.definitions().definitions();
  zc::Vector<size_t> order;
  for (size_t index = 0; index < inventory.size(); ++index) { order.add(index); }
  for (size_t index = 1; index < order.size(); ++index) {
    const size_t current = order[index];
    const auto currentKey = inventory[current].key.encode();
    size_t insertion = index;
    while (insertion > 0) {
      const auto previousKey = inventory[order[insertion - 1]].key.encode();
      if (compareBytes(currentKey.asPtr(), previousKey.asPtr()) >= 0) { break; }
      order[insertion] = order[insertion - 1];
      --insertion;
    }
    order[insertion] = current;
  }

  DefinitionSkeletonCandidate result;
  zc::Vector<ReceiverCandidate> receivers;
  for (const size_t index : order) {
    const auto& definition = inventory[index];
    const auto definitionKind = definition.record.kind();
    const auto classification = eligibility(definitionKind);
    if (classification == SkeletonEligibility::Deferred) { continue; }
    const bool receiver = false;
    const bool lexicalBinding = hasLexicalBinding(classification);
    if (definition.bindingName == zc::none || !input.tree().contains(definition.node)) {
      return failure(input, BinderInvariantKind::InvalidBindingFact, definition.node);
    }
    auto syntaxSpan = input.parsedModule().spanFor(input.tree().node(definition.node).range);
    auto nodeScope = scopeForNode(arena, definition.node);
    if (syntaxSpan == zc::none || nodeScope == zc::none) {
      return failure(input, BinderInvariantKind::InvalidBindingFact, definition.node);
    }

    zc::Maybe<ScopeId> declaringScope;
    ZC_IF_SOME(scope, nodeScope) {
      if (scope.index() >= arena.scopes.size() || arena.scopes[scope.index()].id != scope) {
        return failure(input, BinderInvariantKind::MalformedScopeGraph, definition.node);
      }
      if (ownsScope(definitionKind)) {
        const auto& owned = arena.scopes[scope.index()];
        const auto& owner = owned.owner.value();
        if (!owner.is<DefinitionScopeOwner>() ||
            owner.get<DefinitionScopeOwner>().definition != definition.definition ||
            owned.parent == zc::none) {
          return failure(input, BinderInvariantKind::MalformedScopeGraph, definition.node);
        }
        ZC_IF_SOME(parent, owned.parent) { declaringScope = parent; }
      } else {
        declaringScope = scope;
      }
    }
    if (classification == SkeletonEligibility::Generic) {
      bool ambiguousOwner = false;
      auto owner = genericOwner(input.tree(), definition.node, ambiguousOwner);
      if (ambiguousOwner || owner == zc::none) {
        return failure(input, BinderInvariantKind::MissingRequiredResolution, definition.node);
      }
      ZC_IF_SOME(ownerNode, owner) {
        auto ownerScope = scopeForNode(arena, ownerNode);
        if (ownerScope == zc::none || ownerScope != nodeScope) {
          return failure(input, BinderInvariantKind::MalformedScopeGraph, definition.node);
        }
      }
    }
    if (classification == SkeletonEligibility::Parameter) {
      bool ambiguousOwner = false;
      auto owner = parameterOwner(input.tree(), definition.node, ambiguousOwner);
      if (ambiguousOwner || owner == zc::none) {
        return failure(input, BinderInvariantKind::MissingRequiredResolution, definition.node);
      }
      ZC_IF_SOME(ownerNode, owner) {
        auto ownerScope = scopeForNode(arena, ownerNode);
        if (ownerScope == zc::none || ownerScope != nodeScope) {
          return failure(input, BinderInvariantKind::MalformedScopeGraph, definition.node);
        }
      }
    }
    if (declaringScope == zc::none) {
      return failure(input, BinderInvariantKind::MalformedScopeGraph, definition.node);
    }
    zc::Maybe<DefinitionActivation> patternActivationValue;
    if (classification == SkeletonEligibility::Pattern) {
      patternActivationValue = patternActivation(input.tree(), definition.site);
      if (patternActivationValue == zc::none) {
        return failure(input, BinderInvariantKind::MissingRequiredResolution, definition.node);
      }
    }
    ZC_IF_SOME(scope, declaringScope) {
      if (scope.index() >= arena.scopes.size() || arena.scopes[scope.index()].id != scope) {
        return failure(input, BinderInvariantKind::MalformedScopeGraph, definition.node);
      }
      auto& record = arena.scopes[scope.index()];
      const bool genericScope =
          record.kind == ScopeKind::Function || record.kind == ScopeKind::Closure ||
          record.kind == ScopeKind::TypeBody || record.kind == ScopeKind::ImplBody;
      const bool skeletonScope =
          record.kind == ScopeKind::Module || record.kind == ScopeKind::TypeBody ||
          record.kind == ScopeKind::ImplBody ||
          (record.kind == ScopeKind::Block && definitionKind == identity::DefinitionKind::Function);
      const bool specialCallableScope =
          record.kind == ScopeKind::TypeBody || record.kind == ScopeKind::ImplBody;
      const bool patternScope = (patternActivationValue == DefinitionActivation::LoopPattern &&
                                 record.kind == ScopeKind::Loop) ||
                                (patternActivationValue == DefinitionActivation::MatchPattern &&
                                 record.kind == ScopeKind::MatchArm);
      if ((classification == SkeletonEligibility::Generic && !genericScope) ||
          (classification == SkeletonEligibility::Parameter && record.kind != ScopeKind::Function &&
           record.kind != ScopeKind::Closure) ||
          (classification == SkeletonEligibility::SpecialCallable && !specialCallableScope) ||
          (classification == SkeletonEligibility::Pattern && !patternScope) ||
          (classification != SkeletonEligibility::Generic &&
           classification != SkeletonEligibility::Parameter &&
           classification != SkeletonEligibility::Closure &&
           classification != SkeletonEligibility::Pattern && !skeletonScope)) {
        return failure(input, BinderInvariantKind::MissingRequiredResolution, definition.node);
      }
      ZC_IF_SOME(span, syntaxSpan) {
        if (!sameSpan(span, definition.source) || !contains(record.source, definition.source)) {
          return failure(input, BinderInvariantKind::InvalidBindingFact, definition.node);
        }
      }
      const Namespace nameSpace = classification == SkeletonEligibility::Value ||
                                          classification == SkeletonEligibility::SpecialCallable ||
                                          classification == SkeletonEligibility::Closure ||
                                          classification == SkeletonEligibility::Pattern ||
                                          classification == SkeletonEligibility::Parameter
                                      ? Namespace::Value
                                      : Namespace::Type;
      const DefinitionActivation activation =
          classification == SkeletonEligibility::Generic     ? DefinitionActivation::GenericList
          : classification == SkeletonEligibility::Parameter ? DefinitionActivation::ParameterList
          : classification == SkeletonEligibility::Closure
              ? DefinitionActivation::ExpressionIntroduction
          : classification == SkeletonEligibility::Pattern
              ? ZC_ASSERT_NONNULL(patternActivationValue)
              : DefinitionActivation::ModuleSkeleton;
      if (lexicalBinding) {
        const auto& name = ZC_ASSERT_NONNULL(definition.bindingName);
        zc::Maybe<identity::SourceSpan> noAlias;
        record.bindings.add(
            ScopeBindingEntry(BindingNameKey(nameSpace, name.clone()),
                              NameBinding(BindingTarget::definition(definition.definition),
                                          BindingTarget::definition(definition.definition),
                                          nameSpace, BindingOrigin::LocalDeclaration,
                                          definition.source.clone(), zc::mv(noAlias))));
      }
      bool invalidVisibility = false;
      auto visibility =
          memberVisibility(input, record, input.tree().node(definition.node), invalidVisibility);
      if (invalidVisibility) {
        return failure(input, BinderInvariantKind::InvalidBindingFact, definition.node);
      }
      result.definitions.add(
          DefinitionFact(definition.definition, definition.site.clone(), definitionKind,
                         ZC_ASSERT_NONNULL(definition.bindingName).clone(), nameSpace, scope,
                         definition.source.clone(), activation, zc::mv(visibility)));
      if (receiver) {
        auto source = input.parsedModule().functionParameterNameSpan(definition.node,
                                                                     ast::SyntaxKind::ThisKeyword);
        if (source == zc::none) {
          return failure(input, BinderInvariantKind::InvalidBindingFact, definition.node);
        }
        ZC_IF_SOME(value, source) {
          receivers.add(ReceiverCandidate{scope, identity::CallableParameterId(), definition.node,
                                          zc::mv(value)});
        }
      }
      if (lexicalBinding && classification != SkeletonEligibility::Generic &&
          classification != SkeletonEligibility::Parameter && record.kind == ScopeKind::Module) {
        const auto& name = ZC_ASSERT_NONNULL(definition.bindingName);
        bool ambiguousExport = false;
        auto exportNode = declarationExport(input.tree(), definition.node, ambiguousExport);
        if (ambiguousExport) {
          return failure(input, BinderInvariantKind::InvalidBindingFact, definition.node);
        }
        zc::Maybe<identity::SourceSpan> exportSpan;
        ZC_IF_SOME(node, exportNode) {
          exportSpan = input.parsedModule().spanFor(input.tree().node(node).range);
          if (exportSpan == zc::none) {
            return failure(input, BinderInvariantKind::InvalidBindingFact, definition.node);
          }
        }
        result.moduleSurfaceSeeds.add(ModuleSkeletonSurfaceSeed(
            BindingNameKey(nameSpace, name.clone()), definition.definition,
            definition.source.clone(), exportNode != zc::none, zc::mv(exportSpan)));
      }
    }
  }

  const auto genericParameters = input.definitions().genericParameters();
  zc::Vector<size_t> genericOrder;
  for (size_t index = 0; index < genericParameters.size(); ++index) { genericOrder.add(index); }
  for (size_t index = 1; index < genericOrder.size(); ++index) {
    const size_t current = genericOrder[index];
    const auto currentKey = genericParameters[current].key.encode();
    size_t insertion = index;
    while (insertion > 0) {
      const auto previousKey = genericParameters[genericOrder[insertion - 1]].key.encode();
      if (compareBytes(currentKey.asPtr(), previousKey.asPtr()) >= 0) { break; }
      genericOrder[insertion] = genericOrder[insertion - 1];
      --insertion;
    }
    genericOrder[insertion] = current;
  }
  for (const size_t index : genericOrder) {
    const auto& parameter = genericParameters[index];
    auto scope = scopeForNode(arena, parameter.node);
    bool ambiguousOwner = false;
    auto owner = genericOwner(input.tree(), parameter.node, ambiguousOwner);
    if (scope == zc::none || owner == zc::none || ambiguousOwner ||
        !sameSpan(parameter.source, ZC_ASSERT_NONNULL(input.parsedModule().spanFor(
                                        input.tree().node(parameter.node).range)))) {
      return failure(input, BinderInvariantKind::MissingRequiredResolution, parameter.node);
    }
    ZC_IF_SOME(ownerNode, owner) {
      if (scopeForNode(arena, ownerNode) != scope) {
        return failure(input, BinderInvariantKind::MalformedScopeGraph, parameter.node);
      }
    }
    ZC_IF_SOME(scopeValue, scope) {
      if (scopeValue.index() >= arena.scopes.size() ||
          arena.scopes[scopeValue.index()].id != scopeValue) {
        return failure(input, BinderInvariantKind::MalformedScopeGraph, parameter.node);
      }
      auto& record = arena.scopes[scopeValue.index()];
      if (record.kind != ScopeKind::Function && record.kind != ScopeKind::Closure &&
          record.kind != ScopeKind::TypeBody && record.kind != ScopeKind::ImplBody) {
        return failure(input, BinderInvariantKind::MissingRequiredResolution, parameter.node);
      }
      zc::Maybe<identity::SourceSpan> noAlias;
      record.bindings.add(ScopeBindingEntry(
          BindingNameKey(Namespace::Type, parameter.bindingName.clone()),
          NameBinding(BindingTarget::genericParameter(parameter.parameter),
                      BindingTarget::genericParameter(parameter.parameter), Namespace::Type,
                      BindingOrigin::LocalDeclaration, parameter.source.clone(), zc::mv(noAlias))));
      result.genericParameters.add(GenericParameterFact{parameter.parameter, parameter.site.clone(),
                                                        parameter.bindingName.clone(), scopeValue,
                                                        parameter.source.clone()});
    }
  }

  const auto callableParameters = input.definitions().callableParameters();
  zc::Vector<size_t> callableOrder;
  for (size_t index = 0; index < callableParameters.size(); ++index) { callableOrder.add(index); }
  for (size_t index = 1; index < callableOrder.size(); ++index) {
    const size_t current = callableOrder[index];
    const auto currentKey = callableParameters[current].key.encode();
    size_t insertion = index;
    while (insertion > 0) {
      const auto previousKey = callableParameters[callableOrder[insertion - 1]].key.encode();
      if (compareBytes(currentKey.asPtr(), previousKey.asPtr()) >= 0) { break; }
      callableOrder[insertion] = callableOrder[insertion - 1];
      --insertion;
    }
    callableOrder[insertion] = current;
  }
  for (const size_t index : callableOrder) {
    const auto& parameter = callableParameters[index];
    auto scope = scopeForNode(arena, parameter.node);
    bool ambiguousOwner = false;
    auto owner = parameterOwner(input.tree(), parameter.node, ambiguousOwner);
    const bool receiver = isReceiverParameter(input, parameter);
    if (scope == zc::none || owner == zc::none || ambiguousOwner ||
        (receiver && parameter.bindingName != zc::none) ||
        (!receiver && parameter.bindingName == zc::none)) {
      return failure(input, BinderInvariantKind::MissingRequiredResolution, parameter.node);
    }
    ZC_IF_SOME(ownerNode, owner) {
      if (scopeForNode(arena, ownerNode) != scope) {
        return failure(input, BinderInvariantKind::MalformedScopeGraph, parameter.node);
      }
    }
    ZC_IF_SOME(scopeValue, scope) {
      if (scopeValue.index() >= arena.scopes.size() ||
          arena.scopes[scopeValue.index()].id != scopeValue) {
        return failure(input, BinderInvariantKind::MalformedScopeGraph, parameter.node);
      }
      auto& record = arena.scopes[scopeValue.index()];
      if (record.kind != ScopeKind::Function && record.kind != ScopeKind::Closure) {
        return failure(input, BinderInvariantKind::MissingRequiredResolution, parameter.node);
      }
      if (!receiver) {
        const auto& name = ZC_ASSERT_NONNULL(parameter.bindingName);
        zc::Maybe<identity::SourceSpan> noAlias;
        record.bindings.add(
            ScopeBindingEntry(BindingNameKey(Namespace::Value, name.clone()),
                              NameBinding(BindingTarget::callableParameter(parameter.parameter),
                                          BindingTarget::callableParameter(parameter.parameter),
                                          Namespace::Value, BindingOrigin::LocalDeclaration,
                                          parameter.source.clone(), zc::mv(noAlias))));
      }
      zc::Maybe<identity::DeclaredDefinitionName> factName;
      ZC_IF_SOME(name, parameter.bindingName) { factName = name.clone(); }
      result.callableParameters.add(
          CallableParameterFact{parameter.parameter, parameter.site.clone(), zc::mv(factName),
                                scopeValue, parameter.source.clone(), receiver});
      if (receiver) {
        auto source = input.parsedModule().functionParameterNameSpan(parameter.node,
                                                                     ast::SyntaxKind::ThisKeyword);
        if (source == zc::none) {
          return failure(input, BinderInvariantKind::InvalidBindingFact, parameter.node);
        }
        ZC_IF_SOME(value, source) {
          receivers.add(
              ReceiverCandidate{scopeValue, parameter.parameter, parameter.node, zc::mv(value)});
        }
      }
    }
  }

  for (size_t index = 1; index < receivers.size(); ++index) {
    auto current = zc::mv(receivers[index]);
    size_t insertion = index;
    while (insertion > 0 && receiverLess(current, receivers[insertion - 1])) {
      receivers[insertion] = zc::mv(receivers[insertion - 1]);
      --insertion;
    }
    receivers[insertion] = zc::mv(current);
  }
  size_t receiverIndex = 0;
  while (receiverIndex < receivers.size()) {
    const auto& previous = receivers[receiverIndex++];
    while (receiverIndex < receivers.size() && receivers[receiverIndex].scope == previous.scope) {
      const auto& rejected = receivers[receiverIndex++];
      auto name = identity::DeclaredDefinitionName::fromCanonical("this"_zc);
      if (name == zc::none) {
        return failure(input, BinderInvariantKind::InvalidBindingFact, rejected.node);
      }
      ZC_IF_SOME(nameValue, name) {
        result.duplicates.add(BindingDuplicateFact{
            BinderDiagnosticCode::RedeclareParameter, BinderEmitterSite::ModuleSkeleton,
            zc::mv(nameValue), BindingTarget::callableParameter(rejected.parameter), rejected.node,
            previous.node, rejected.source.clone(), previous.source.clone()});
      }
    }
  }

  const auto implementations = input.definitions().impls();
  zc::Vector<size_t> implOrder;
  for (size_t index = 0; index < implementations.size(); ++index) { implOrder.add(index); }
  for (size_t index = 1; index < implOrder.size(); ++index) {
    const size_t current = implOrder[index];
    const auto currentKey = implementations[current].key.encode();
    size_t insertion = index;
    while (insertion > 0) {
      const auto previousKey = implementations[implOrder[insertion - 1]].key.encode();
      if (compareBytes(currentKey.asPtr(), previousKey.asPtr()) >= 0) { break; }
      implOrder[insertion] = implOrder[insertion - 1];
      --insertion;
    }
    implOrder[insertion] = current;
  }
  for (const size_t index : implOrder) {
    const auto& implementation = implementations[index];
    auto scope = scopeForNode(arena, implementation.node);
    auto syntaxSpan = input.parsedModule().spanFor(input.tree().node(implementation.node).range);
    if (scope == zc::none || syntaxSpan == zc::none) {
      return failure(input, BinderInvariantKind::MissingRequiredResolution, implementation.node);
    }
    ZC_IF_SOME(scopeValue, scope) {
      if (scopeValue.index() >= arena.scopes.size() ||
          arena.scopes[scopeValue.index()].id != scopeValue) {
        return failure(input, BinderInvariantKind::MalformedScopeGraph, implementation.node);
      }
      const auto& record = arena.scopes[scopeValue.index()];
      const auto& owner = record.owner.value();
      if (record.kind != ScopeKind::ImplBody || !owner.is<ImplScopeOwner>() ||
          owner.get<ImplScopeOwner>().occurrence != implementation.occurrence ||
          !sameSpan(record.source, implementation.source)) {
        return failure(input, BinderInvariantKind::MalformedScopeGraph, implementation.node);
      }
      ZC_IF_SOME(span, syntaxSpan) {
        if (!sameSpan(span, implementation.source)) {
          return failure(input, BinderInvariantKind::InvalidBindingFact, implementation.node);
        }
      }
      zc::Vector<identity::DefId> members;
      for (const auto& definition : result.definitions) {
        if (definition.declaringScope == scopeValue &&
            definition.activation == DefinitionActivation::ModuleSkeleton) {
          members.add(definition.identity);
        }
      }
      result.impls.add(ImplBindingFact{implementation.occurrence, implementation.authority,
                                       implementation.node, scopeValue, zc::mv(members),
                                       implementation.source.clone()});
    }
  }

  for (auto& scope : arena.scopes) {
    sortBindings(scope.bindings);
    zc::Vector<ScopeBindingEntry> unique;
    for (auto& binding : scope.bindings) {
      if (!unique.empty() && sameBindingName(unique.back(), binding)) {
        const auto& rejected = binding.binding.bindingIdentity;
        const auto& previous = unique.back().binding.bindingIdentity;
        auto primaryNode = bindingNode(input, rejected);
        auto previousNode = bindingNode(input, previous);
        auto code = redeclarationCode(result, rejected);
        if (primaryNode == zc::none || previousNode == zc::none || code == zc::none) {
          return failure(input, BinderInvariantKind::InvalidBindingFact, input.tree().root());
        }
        ZC_IF_SOME(primaryNodeValue, primaryNode) {
          auto name = identity::DeclaredDefinitionName::fromCanonical(binding.name.name().text());
          if (name == zc::none) {
            return failure(input, BinderInvariantKind::InvalidBindingFact, primaryNodeValue);
          }
          ZC_IF_SOME(previousNodeValue, previousNode) {
            ZC_IF_SOME(codeValue, code) {
              ZC_IF_SOME(nameValue, name) {
                result.duplicates.add(
                    BindingDuplicateFact{codeValue, BinderEmitterSite::ModuleSkeleton,
                                         zc::mv(nameValue), rejected.clone(), primaryNodeValue,
                                         previousNodeValue, binding.binding.declarationSpan.clone(),
                                         unique.back().binding.declarationSpan.clone()});
              }
            }
          }
        }
        continue;
      }
      unique.add(zc::mv(binding));
    }
    scope.bindings = zc::mv(unique);
  }
  zc::Vector<ModuleSkeletonSurfaceSeed> retainedSeeds;
  for (auto& seed : result.moduleSurfaceSeeds) {
    if (!isRejected(result, seed.identity)) { retainedSeeds.add(zc::mv(seed)); }
  }
  result.moduleSurfaceSeeds = zc::mv(retainedSeeds);
  sortSurfaceSeeds(result.moduleSurfaceSeeds);
  return result;
}

}  // namespace zomlang::compiler::binder
