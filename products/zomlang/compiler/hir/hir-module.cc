// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/hir/hir-module.h"

#include <cstdint>

#include "zc/core/encoding.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/binder/metadata/definition-inventory.h"
#include "zomlang/compiler/binder/metadata/definition-site.h"
#include "zomlang/compiler/binder/metadata/immutable-binding-metadata.h"
#include "zomlang/compiler/binder/metadata/immutable-definition-inventory.h"
#include "zomlang/compiler/checker/facts/signature-facts.h"
#include "zomlang/compiler/identity/key/definition-key.h"
#include "zomlang/compiler/ownership/surface-admission.h"

namespace zomlang::compiler::hir {
namespace {

bool sameSpan(const identity::SourceSpan& left, const identity::SourceSpan& right) {
  return left.source().sameAs(right.source()) && left.byteStart() == right.byteStart() &&
         left.byteEnd() == right.byteEnd();
}

bool lessBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] != right[index]) return left[index] < right[index];
  }
  return left.size() < right.size();
}

identity::IdentityInvariant invalidIdentity(identity::IdentityAllocationPhase phase,
                                            uint32_t ordinal) {
  zc::Maybe<zc::Array<uint8_t>> noKey;
  zc::Maybe<identity::UnbrandedSourceRange> noRange;
  auto invariant = identity::IdentityInvariant::from(
      identity::IdentityInvariantKind::InvalidHandle, phase, zc::mv(noKey), zc::mv(noRange),
      identity::IdentityApiSite::HandleLookup, ordinal);
  ZC_IF_SOME(value, invariant) { return zc::mv(value); }
  ZC_UNREACHABLE
}

class AuthorityIdentityResolver final : public ir::IrFailureIdentityResolver {
public:
  explicit AuthorityIdentityResolver(const checker::CheckerIdentityAuthority& identities) noexcept
      : identities(identities) {}

  ir::ExpandedIrIdentityResult expand(identity::ModuleId module) const override {
    auto key = identities.module(module);
    if (key == zc::none) {
      return ir::RejectedIrIdentityValue{
          invalidIdentity(identity::IdentityAllocationPhase::Module, 0)};
    }
    ZC_IF_SOME(value, key) {
      auto expanded = ir::ExpandedIrIdentity::from(value.key().encode());
      ZC_IF_SOME(bytes, expanded) { return ir::ExpandedIrIdentityValue{zc::mv(bytes)}; }
    }
    return ir::RejectedIrIdentityValue{
        invalidIdentity(identity::IdentityAllocationPhase::Encoding, 0)};
  }

  ir::ExpandedIrIdentityResult expand(identity::DefId definition) const override {
    auto key = identities.definition(definition);
    if (key == zc::none) {
      return ir::RejectedIrIdentityValue{
          invalidIdentity(identity::IdentityAllocationPhase::Definition, 0)};
    }
    ZC_IF_SOME(value, key) {
      auto expanded = ir::ExpandedIrIdentity::from(value.key().encode());
      ZC_IF_SOME(bytes, expanded) { return ir::ExpandedIrIdentityValue{zc::mv(bytes)}; }
    }
    return ir::RejectedIrIdentityValue{
        invalidIdentity(identity::IdentityAllocationPhase::Encoding, 0)};
  }

  ir::ExpandedIrIdentityResult expand(ir::InstanceId) const override {
    return ir::RejectedIrIdentityValue{
        invalidIdentity(identity::IdentityAllocationPhase::Definition, 0)};
  }

private:
  const checker::CheckerIdentityAuthority& identities;
};

template <typename VerifiedValue>
ir::IrOperationResult<VerifiedValue> rejectHir(
    ir::IrFailurePhase phase, ir::IrFailureKind kind, identity::ModuleId module,
    const checker::CheckerIdentityAuthority& identities, uint32_t ordinal,
    zc::Vector<uint32_t>&& fieldPath = zc::Vector<uint32_t>()) {
  AuthorityIdentityResolver resolver(identities);
  auto fallback = ir::IrFailureFallbackContext::from(phase, ir::IrFailureOwner::module(module));
  ZC_IREQUIRE(fallback != zc::none, "HIR failure fallback must be legal");
  zc::Maybe<ir::IrFailureSite> noSite;
  zc::Maybe<identity::SourceSpan> noSpan;
  auto descriptor = ir::IrFailureDescriptor::decoded(
      ir::IrRejectedBranch::IrInvariantRejected, phase, kind, ir::IrFailureOwner::module(module),
      zc::mv(noSite), ir::IrFailureDetail::none(), zc::mv(noSpan), zc::mv(fieldPath), ordinal);
  ZC_IF_SOME(fallbackValue, fallback) {
    auto admitted = ir::IrFailureFactory::admit(zc::mv(descriptor), fallbackValue, resolver);
    if (admitted.is<ir::IdentityRejectedIrFailureDescriptor>()) {
      zc::Vector<identity::IdentityInvariant> failures;
      failures.add(zc::mv(admitted).get<ir::IdentityRejectedIrFailureDescriptor>().failure);
      auto sorted = ir::SortedIdentityInvariantFacts::from(zc::mv(failures));
      ZC_IF_SOME(values, sorted) {
        return ir::IrOperationResult<VerifiedValue>::identityInvariantRejected(zc::mv(values));
      }
      ZC_UNREACHABLE
    }
    zc::Vector<ir::IrFailureFact> failures;
    if (admitted.is<ir::AcceptedIrFailureDescriptor>()) {
      failures.add(zc::mv(admitted).get<ir::AcceptedIrFailureDescriptor>().fact);
    } else {
      failures.add(zc::mv(admitted).get<ir::FallbackIrFailureDescriptor>().fact);
    }
    auto sorted = ir::SortedIrInvariantFailureFacts::from(zc::mv(failures));
    ZC_IF_SOME(values, sorted) {
      return ir::IrOperationResult<VerifiedValue>::irInvariantRejected(zc::mv(values));
    }
  }
  ZC_UNREACHABLE
}

bool isScalarLiteral(ast::SyntaxKind kind) noexcept {
  switch (kind) {
    case ast::SyntaxKind::NullLiteral:
    case ast::SyntaxKind::BoolLiteral:
    case ast::SyntaxKind::IntLiteral:
    case ast::SyntaxKind::FloatLiteralExpr:
    case ast::SyntaxKind::BigIntLiteral:
    case ast::SyntaxKind::StringLiteralExpr:
    case ast::SyntaxKind::UnitLiteral:
    case ast::SyntaxKind::CharacterLiteralExpr:
    case ast::SyntaxKind::NoSubstitutionTemplateLiteralExpr:
      return true;
    default:
      return false;
  }
}

template <typename Map, typename Key>
zc::Maybe<size_t> factIndex(const Map& map, const Key& key) {
  const auto entries = map.entries();
  for (size_t index = 0; index < entries.size(); ++index) {
    if (entries[index].key == key) return index;
  }
  return zc::none;
}

zc::Maybe<size_t> definitionIndex(const binder::ImmutableDefinitionInventory& definitions,
                                  identity::DefId definition) {
  const auto entries = definitions.definitions();
  for (size_t index = 0; index < entries.size(); ++index) {
    if (entries[index].definition == definition) return index;
  }
  return zc::none;
}

zc::Maybe<const binder::PatternBindingSite&> patternBindingSite(
    const binder::DefinitionInventory& inventory,
    const binder::MaterializedDefinitionInventoryEntry& definition) {
  const auto& materialized = definition.site.value();
  if (materialized.is<binder::PatternBindingSite>()) {
    return materialized.get<binder::PatternBindingSite>();
  }
  zc::Maybe<const binder::PatternBindingSite&> result;
  for (const auto& candidate : inventory.definitions()) {
    if (candidate.node != definition.node || candidate.kind != definition.record.kind()) {
      continue;
    }
    const auto& site = candidate.site.value();
    if (!site.is<binder::PatternBindingSite>() || result != zc::none) { return zc::none; }
    result = site.get<binder::PatternBindingSite>();
  }
  return result;
}

bool hasExecutableBody(const binder::MaterializedDefinitionInventoryEntry& definition,
                       const binder::ImmutableDefinitionInventory& definitions) {
  for (const auto& body : definitions.ownerBodies()) {
    const auto& owner = body.owner().owner();
    if (owner.kind() == binder::StableBodyOwnerKind::Definition) {
      auto ownerDefinition = owner.definitionKey();
      ZC_IF_SOME(key, ownerDefinition) {
        if (key == definition.key) { return true; }
      }
      continue;
    }
    if (definition.record.owners().size() == 0) { return true; }
  }
  return false;
}

size_t executableDefinitionCount(const binder::ImmutableDefinitionInventory& definitions) {
  size_t count = 0;
  for (const auto& definition : definitions.definitions()) {
    if (hasExecutableBody(definition, definitions) &&
        (definition.record.kind() == identity::DefinitionKind::Function ||
         definition.record.kind() == identity::DefinitionKind::Static ||
         definition.record.kind() == identity::DefinitionKind::Constant)) {
      ++count;
    }
  }
  return count;
}

bool sameConstant(const checker::checked::CanonicalConstValue& left,
                  const checker::checked::CanonicalConstValue& right, identity::ModuleId module,
                  const checker::CheckerIdentityAuthority& identities,
                  const type::SemanticTypeStore& semanticTypes) {
  auto leftBytes =
      checker::signature::SignatureFactsCanonicalCodec::encodeCanonicalConstValueFromAuthority(
          left, module, identities, semanticTypes);
  auto rightBytes =
      checker::signature::SignatureFactsCanonicalCodec::encodeCanonicalConstValueFromAuthority(
          right, module, identities, semanticTypes);
  if (leftBytes == zc::none || rightBytes == zc::none) return false;
  bool equal = false;
  ZC_IF_SOME(leftValue, leftBytes) {
    ZC_IF_SOME(rightValue, rightBytes) { equal = leftValue.asPtr() == rightValue.asPtr(); }
  }
  return equal;
}

zc::Maybe<HirLinkage> linkage(const checker::signature::ValueSignature& signature) {
  if (signature.abi == zc::none) return HirLinkage::Internal;
  ZC_IF_SOME(abi, signature.abi) {
    switch (abi) {
      case checker::signature::ExternAbi::Cdecl:
        return HirLinkage::ExternalCdecl;
      case checker::signature::ExternAbi::Stdcall:
        return HirLinkage::ExternalStdcall;
      case checker::signature::ExternAbi::ZomNative:
        return HirLinkage::ExternalZomNative;
    }
  }
  return zc::none;
}

zc::Maybe<HirLinkage> linkage(const checker::signature::CallableSignature& signature) {
  if (signature.abi == zc::none) return HirLinkage::Internal;
  ZC_IF_SOME(abi, signature.abi) {
    switch (abi) {
      case checker::signature::ExternAbi::Cdecl:
        return HirLinkage::ExternalCdecl;
      case checker::signature::ExternAbi::Stdcall:
        return HirLinkage::ExternalStdcall;
      case checker::signature::ExternAbi::ZomNative:
        return HirLinkage::ExternalZomNative;
    }
  }
  return zc::none;
}

zc::Maybe<HirVisibility> visibility(const binder::VisibilityEnvelope& source) {
  if (source.value().is<binder::ModuleVisibility>()) {
    return HirVisibility::module(source.value().get<binder::ModuleVisibility>().module);
  }
  if (source.value().is<binder::ExternalVisibility>()) return HirVisibility::external();
  return zc::none;
}

bool sameVisibility(const HirVisibility& left, const HirVisibility& right) {
  if (left.kind() != right.kind()) return false;
  if (left.kind() == HirVisibilityKind::External) return true;
  return left.visibleModule() == right.visibleModule();
}

HirNodeId hirId(uint32_t ordinal) {
  auto value = HirNodeId::fromOrdinal(ordinal);
  ZC_IF_SOME(id, value) { return id; }
  ZC_UNREACHABLE
}

HirLocalId hirLocalId(uint32_t ordinal) {
  auto value = HirLocalId::fromOrdinal(ordinal);
  ZC_IF_SOME(id, value) { return id; }
  ZC_UNREACHABLE
}

struct PendingValueDeclaration final {
  identity::DefId definition;
  identity::DefinitionKind definitionKind;
  identity::SemanticTypeId declaredType;
  identity::SemanticTypeId inferredType;
  type::semantic::Mutability mutability;
  HirVisibility visibility;
  HirLinkage linkage;
  identity::SourceSpan declarationSpan;
  identity::SourceSpan patternSpan;
  identity::SourceSpan initializerSpan;
  checker::checked::CanonicalConstValue literal;
  zc::Maybe<checker::checked::CanonicalConstValue> constant;
  zc::Array<uint8_t> orderingKey;
};

struct PendingSequentialLocalReturn final {
  HirLocalBinding source;
  HirLocalBinding destination;
  zc::Maybe<checker::checked::CanonicalConstValue> literal;
  zc::Maybe<HirNominalAggregateExpression> aggregate;
  HirLocalReferenceExpression initializerReference;
  HirLocalReferenceExpression returnReference;
  bool returnsSource;
};

struct PendingFunctionDeclaration final {
  identity::DefId definition;
  identity::SemanticTypeId resultType;
  zc::Vector<HirParameter> parameters;
  HirVisibility visibility;
  HirLinkage linkage;
  identity::SourceSpan declarationSpan;
  identity::SourceSpan bodySpan;
  identity::SourceSpan returnSpan;
  identity::SourceSpan valueSpan;
  zc::Maybe<checker::checked::CanonicalConstValue> literal;
  zc::Maybe<HirDirectCallExpression> call;
  zc::Maybe<HirReceiverCallExpression> receiverCall;
  zc::Maybe<HirLocalBinding> local;
  zc::Maybe<HirNominalAggregateExpression> aggregate;
  zc::Vector<HirLocalWriteStatement> localWrites;
  zc::Vector<checker::checked::CanonicalConstValue> localWriteLiterals;
  zc::Maybe<HirLocalReferenceExpression> localReference;
  zc::Maybe<HirLocalFieldProjectionExpression> localFieldProjection;
  zc::Maybe<HirParameterReferenceExpression> parameterReference;
  zc::Maybe<HirParameterIndexExpression> parameterIndex;
  zc::Maybe<HirParameterReborrowExpression> parameterReborrow;
  zc::Maybe<HirLocalBorrowExpression> localBorrow;
  zc::Maybe<PendingSequentialLocalReturn> sequentialLocalReturn;
  zc::Maybe<identity::SourceSpan> unsafeBlockSpan;
  zc::Array<uint8_t> orderingKey;
};

void sortPendingDeclarations(zc::Vector<PendingValueDeclaration>& values) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    size_t insertion = index;
    while (insertion != 0) {
      const auto& previous = values[insertion - 1];
      const bool less =
          current.declarationSpan.byteStart() < previous.declarationSpan.byteStart() ||
          (current.declarationSpan.byteStart() == previous.declarationSpan.byteStart() &&
           lessBytes(current.orderingKey.asPtr(), previous.orderingKey.asPtr()));
      if (!less) break;
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
}

void sortPendingFunctions(zc::Vector<PendingFunctionDeclaration>& values) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    size_t insertion = index;
    while (insertion != 0) {
      const auto& previous = values[insertion - 1];
      const bool less =
          current.declarationSpan.byteStart() < previous.declarationSpan.byteStart() ||
          (current.declarationSpan.byteStart() == previous.declarationSpan.byteStart() &&
           lessBytes(current.orderingKey.asPtr(), previous.orderingKey.asPtr()));
      if (!less) break;
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
}

struct FunctionReturnShape final {
  ast::NodeId body;
  ast::NodeId returnStatement;
  ast::NodeId value;
  ast::NodeId localPattern;
  zc::Maybe<ast::NodeId> localInitializer;
  ast::NodeList localWrites;
  bool returnsLocal = false;
  ast::NodeId localReference;
  bool returnsLocalField = false;
  bool returnsLocalReborrow = false;
  ast::NodeId sourceLocalPattern;
  ast::NodeId sourceLocalInitializer;
  ast::NodeId destinationLocalInitializer;
  bool isSequentialLocalReturn = false;
  bool sequentialReturnUsesSource = false;
  bool returnsReceiverCall = false;
  bool returnsLocalBorrow = false;
  zc::Maybe<ast::NodeId> unsafeBlock;
};

zc::Maybe<ast::NodeId> statementItem(const ast::Tree& tree, ast::NodeId statement) {
  if (!tree.contains(statement)) return zc::none;
  if (tree.node(statement).kind != ast::SyntaxKind::StatementListItem) { return statement; }
  const ast::NodeId item(tree.node(statement).payload.words[ast::kStatementListItemItemWord]);
  if (!tree.contains(item)) return zc::none;
  return item;
}

zc::Maybe<ast::NodeId> localDeclarator(const ast::Tree& tree, ast::NodeId statement) {
  auto item = statementItem(tree, statement);
  if (item == zc::none) return zc::none;
  ast::NodeId local;
  ZC_IF_SOME(value, item) { local = value; }
  if (tree.node(local).kind != ast::SyntaxKind::LetStmt) return zc::none;
  const ast::NodeId declarations(tree.node(local).payload.words[ast::kLetStmtDeclarationsWord]);
  if (!tree.contains(declarations) ||
      tree.node(declarations).kind != ast::SyntaxKind::VariableDeclaratorList) {
    return zc::none;
  }
  const auto& declarationList = tree.node(declarations);
  const ast::NodeList declarators{
      declarationList.payload.words[ast::kVariableDeclaratorListDeclsFirstWord],
      declarationList.payload.words[ast::kVariableDeclaratorListDeclsSizeWord]};
  if (!tree.contains(declarators) || declarators.size != 1) return zc::none;
  const ast::NodeId declarator(tree.list(declarators)[0]);
  if (!tree.contains(declarator) ||
      tree.node(declarator).kind != ast::SyntaxKind::VariableDeclarator) {
    return zc::none;
  }
  const ast::NodeId pattern(
      tree.node(declarator).payload.words[ast::kVariableDeclaratorPatternWord]);
  const ast::NodeId initializer(
      tree.node(declarator).payload.words[ast::kVariableDeclaratorInitWord]);
  if (!tree.contains(pattern) || tree.node(pattern).kind != ast::SyntaxKind::IdentifierPattern ||
      !tree.contains(initializer)) {
    return zc::none;
  }
  return declarator;
}

bool matchesLocalReference(const ast::Tree& tree, ast::NodeId pattern, ast::NodeId reference) {
  if (!tree.contains(pattern) || !tree.contains(reference) ||
      tree.node(pattern).kind != ast::SyntaxKind::IdentifierPattern ||
      tree.node(reference).kind != ast::SyntaxKind::IdentExpr) {
    return false;
  }
  return tree.node(pattern).payload.words[ast::kIdentifierPatternNameWord] ==
         tree.node(reference).payload.words[ast::kIdentExprNameWord];
}

zc::Maybe<ast::NodeId> reborrowReference(const ast::Tree& tree, ast::NodeId expression) {
  if (!tree.contains(expression) ||
      tree.node(expression).kind != ast::SyntaxKind::UnaryExpression) {
    return zc::none;
  }
  const auto operation = static_cast<ast::UnaryOperatorKind>(
      tree.node(expression).payload.words[ast::kUnaryExpressionOpWord]);
  if (operation != ast::UnaryOperatorKind::Ref && operation != ast::UnaryOperatorKind::RefMut) {
    return zc::none;
  }
  const ast::NodeId dereference(
      tree.node(expression).payload.words[ast::kUnaryExpressionOperandWord]);
  if (!tree.contains(dereference) ||
      tree.node(dereference).kind != ast::SyntaxKind::UnaryExpression ||
      static_cast<ast::UnaryOperatorKind>(
          tree.node(dereference).payload.words[ast::kUnaryExpressionOpWord]) !=
          ast::UnaryOperatorKind::Deref) {
    return zc::none;
  }
  const ast::NodeId reference(
      tree.node(dereference).payload.words[ast::kUnaryExpressionOperandWord]);
  if (!tree.contains(reference) || tree.node(reference).kind != ast::SyntaxKind::IdentExpr) {
    return zc::none;
  }
  return reference;
}

zc::Maybe<ast::NodeId> localBorrowReference(const ast::Tree& tree, ast::NodeId expression) {
  if (!tree.contains(expression) ||
      tree.node(expression).kind != ast::SyntaxKind::UnaryExpression) {
    return zc::none;
  }
  const auto operation = static_cast<ast::UnaryOperatorKind>(
      tree.node(expression).payload.words[ast::kUnaryExpressionOpWord]);
  if (operation != ast::UnaryOperatorKind::Ref && operation != ast::UnaryOperatorKind::RefMut) {
    return zc::none;
  }
  const ast::NodeId operand(tree.node(expression).payload.words[ast::kUnaryExpressionOperandWord]);
  if (!tree.contains(operand) || tree.node(operand).kind != ast::SyntaxKind::IdentExpr) {
    return zc::none;
  }
  return operand;
}

zc::Maybe<FunctionReturnShape> functionReturnShape(const ast::Tree& tree,
                                                   const ast::Node& function) {
  if (function.kind != ast::SyntaxKind::FunctionDecl) return zc::none;
  const ast::NodeId body(function.payload.words[ast::kFunctionDeclBodyWord]);
  if (!tree.contains(body) || tree.node(body).kind != ast::SyntaxKind::BlockStmt) return zc::none;
  const auto& block = tree.node(body);
  const ast::NodeList statements{block.payload.words[ast::kBlockStmtStmtsFirstWord],
                                 block.payload.words[ast::kBlockStmtStmtsSizeWord]};
  if (!tree.contains(statements) || statements.empty()) return zc::none;
  auto returnStatement = statementItem(tree, tree.list(statements)[statements.size - 1]);
  if (returnStatement == zc::none) return zc::none;
  ZC_IF_SOME(statement, returnStatement) {
    if (tree.node(statement).kind != ast::SyntaxKind::ReturnStmt) return zc::none;
  }
  ast::NodeId returnNode;
  ZC_IF_SOME(statement, returnStatement) { returnNode = statement; }
  ast::NodeId value(tree.node(returnNode).payload.words[ast::kReturnStmtValueWord]);
  if (!tree.contains(value)) return zc::none;
  zc::Maybe<ast::NodeId> unsafeBlock;
  if (statements.size == 1 && tree.node(value).kind == ast::SyntaxKind::UnsafeBlockExpr) {
    const ast::NodeId unsafeBody(tree.node(value).payload.words[ast::kUnsafeBlockExprBodyWord]);
    if (!tree.contains(unsafeBody) || tree.node(unsafeBody).kind != ast::SyntaxKind::BlockStmt) {
      return zc::none;
    }
    const auto& unsafeBodyNode = tree.node(unsafeBody);
    const ast::NodeList unsafeStatements{
        unsafeBodyNode.payload.words[ast::kBlockStmtStmtsFirstWord],
        unsafeBodyNode.payload.words[ast::kBlockStmtStmtsSizeWord]};
    if (!tree.contains(unsafeStatements) || unsafeStatements.empty()) return zc::none;
    auto unsafeItem = statementItem(tree, tree.list(unsafeStatements)[unsafeStatements.size - 1]);
    if (unsafeItem == zc::none) return zc::none;
    ast::NodeId innerStatement;
    ZC_IF_SOME(item, unsafeItem) { innerStatement = item; }
    if (tree.node(innerStatement).kind != ast::SyntaxKind::ExpressionStatement) return zc::none;
    const ast::NodeId innerValue(
        tree.node(innerStatement).payload.words[ast::kExpressionStatementExpressionWord]);
    if (!tree.contains(innerValue)) return zc::none;
    if (isScalarLiteral(tree.node(innerValue).kind)) { unsafeBlock = value; }
    value = innerValue;
  }
  if (statements.size == 1) {
    return FunctionReturnShape{
        body,          returnNode,    value, ast::NodeId(), zc::none,      {},
        false,         ast::NodeId(), false, false,         ast::NodeId(), ast::NodeId(),
        ast::NodeId(), false,         false, false,         false,         zc::mv(unsafeBlock)};
  }
  if (statements.size == 3) {
    auto sourceDeclarator = localDeclarator(tree, tree.list(statements)[0]);
    auto destinationDeclarator = localDeclarator(tree, tree.list(statements)[1]);
    if (sourceDeclarator != zc::none && destinationDeclarator != zc::none) {
      if (tree.node(value).kind != ast::SyntaxKind::IdentExpr) return zc::none;
      ast::NodeId source;
      ast::NodeId destination;
      ZC_IF_SOME(item, sourceDeclarator) { source = item; }
      ZC_IF_SOME(item, destinationDeclarator) { destination = item; }
      const ast::NodeId sourcePattern(
          tree.node(source).payload.words[ast::kVariableDeclaratorPatternWord]);
      const ast::NodeId sourceInitializer(
          tree.node(source).payload.words[ast::kVariableDeclaratorInitWord]);
      const ast::NodeId destinationPattern(
          tree.node(destination).payload.words[ast::kVariableDeclaratorPatternWord]);
      const ast::NodeId destinationInitializer(
          tree.node(destination).payload.words[ast::kVariableDeclaratorInitWord]);
      const bool returnsSource = matchesLocalReference(tree, sourcePattern, value);
      const bool returnsDestination = matchesLocalReference(tree, destinationPattern, value);
      if ((!isScalarLiteral(tree.node(sourceInitializer).kind) &&
           tree.node(sourceInitializer).kind != ast::SyntaxKind::StructLiteralExpr) ||
          !matchesLocalReference(tree, sourcePattern, destinationInitializer) ||
          (!returnsSource && !returnsDestination)) {
        return zc::none;
      }
      return FunctionReturnShape{body,
                                 returnNode,
                                 value,
                                 destinationPattern,
                                 destinationInitializer,
                                 {},
                                 true,
                                 value,
                                 false,
                                 false,
                                 sourcePattern,
                                 sourceInitializer,
                                 destinationInitializer,
                                 true,
                                 returnsSource,
                                 false,
                                 false,
                                 zc::mv(unsafeBlock)};
    }
  }
  auto localStatement = statementItem(tree, tree.list(statements)[0]);
  if (localStatement == zc::none) return zc::none;
  ast::NodeId letNode;
  ZC_IF_SOME(statement, localStatement) { letNode = statement; }
  if (tree.node(letNode).kind != ast::SyntaxKind::LetStmt) { return zc::none; }
  ast::NodeId localReference = value;
  bool returnsReceiverCall = false;
  const bool returnsLocalField = tree.node(value).kind == ast::SyntaxKind::MemberExpression;
  const auto reborrow = reborrowReference(tree, value);
  const auto localBorrow = localBorrowReference(tree, value);
  if (tree.node(value).kind == ast::SyntaxKind::CallExpression) {
    const ast::NodeId callee(tree.node(value).payload.words[ast::kCallExpressionCalleeWord]);
    if (!tree.contains(callee) || tree.node(callee).kind != ast::SyntaxKind::MemberExpression ||
        static_cast<ast::MemberAccessKind>(
            tree.node(callee).payload.words[ast::kMemberExpressionAccessWord]) !=
            ast::MemberAccessKind::Dot) {
      return zc::none;
    }
    localReference = ast::NodeId(tree.node(callee).payload.words[ast::kMemberExpressionObjectWord]);
    returnsReceiverCall = true;
  } else if (returnsLocalField) {
    localReference = ast::NodeId(tree.node(value).payload.words[ast::kMemberExpressionObjectWord]);
  } else if (reborrow != zc::none) {
    ZC_IF_SOME(reference, reborrow) { localReference = reference; }
  } else if (localBorrow != zc::none) {
    ZC_IF_SOME(reference, localBorrow) { localReference = reference; }
  }
  if (!tree.contains(localReference) ||
      tree.node(localReference).kind != ast::SyntaxKind::IdentExpr) {
    return zc::none;
  }
  const ast::NodeId declarations(tree.node(letNode).payload.words[ast::kLetStmtDeclarationsWord]);
  if (!tree.contains(declarations) ||
      tree.node(declarations).kind != ast::SyntaxKind::VariableDeclaratorList) {
    return zc::none;
  }
  const auto& declarationList = tree.node(declarations);
  const ast::NodeList declarators{
      declarationList.payload.words[ast::kVariableDeclaratorListDeclsFirstWord],
      declarationList.payload.words[ast::kVariableDeclaratorListDeclsSizeWord]};
  if (!tree.contains(declarators) || declarators.size != 1) return zc::none;
  const auto declarator = tree.list(declarators)[0];
  if (!tree.contains(declarator) ||
      tree.node(declarator).kind != ast::SyntaxKind::VariableDeclarator) {
    return zc::none;
  }
  const ast::NodeId pattern(
      tree.node(declarator).payload.words[ast::kVariableDeclaratorPatternWord]);
  const ast::NodeId initializer(
      tree.node(declarator).payload.words[ast::kVariableDeclaratorInitWord]);
  if (!tree.contains(pattern) || tree.node(pattern).kind != ast::SyntaxKind::IdentifierPattern) {
    return zc::none;
  }
  if (!tree.contains(initializer) && statements.size == 2) {
    return FunctionReturnShape{body,
                               returnNode,
                               value,
                               pattern,
                               zc::none,
                               {},
                               true,
                               localReference,
                               returnsLocalField,
                               reborrow != zc::none,
                               ast::NodeId(),
                               ast::NodeId(),
                               ast::NodeId(),
                               false,
                               false,
                               returnsReceiverCall,
                               localBorrow != zc::none,
                               zc::mv(unsafeBlock)};
  }
  if (tree.contains(initializer) && !isScalarLiteral(tree.node(initializer).kind) &&
      tree.node(initializer).kind != ast::SyntaxKind::CallExpression &&
      tree.node(initializer).kind != ast::SyntaxKind::IdentExpr &&
      tree.node(initializer).kind != ast::SyntaxKind::StructLiteralExpr) {
    return zc::none;
  }
  if (statements.size == 2) {
    return FunctionReturnShape{body,
                               returnNode,
                               value,
                               pattern,
                               initializer,
                               {},
                               true,
                               localReference,
                               returnsLocalField,
                               reborrow != zc::none,
                               ast::NodeId(),
                               ast::NodeId(),
                               ast::NodeId(),
                               false,
                               false,
                               returnsReceiverCall,
                               localBorrow != zc::none,
                               zc::mv(unsafeBlock)};
  }
  if (static_cast<ast::BindingDeclarationKind>(
          tree.node(letNode).payload.words[ast::kLetStmtKindWord]) !=
      ast::BindingDeclarationKind::Mut) {
    return zc::none;
  }
  for (size_t index = 1; index + 1 < statements.size; ++index) {
    auto writeStatement = statementItem(tree, tree.list(statements)[index]);
    if (writeStatement == zc::none) return zc::none;
    ZC_IF_SOME(statement, writeStatement) {
      if (tree.node(statement).kind != ast::SyntaxKind::ExpressionStatement) return zc::none;
      const ast::NodeId assignment(
          tree.node(statement).payload.words[ast::kExpressionStatementExpressionWord]);
      if (!tree.contains(assignment) ||
          tree.node(assignment).kind != ast::SyntaxKind::AssignmentExpr ||
          static_cast<ast::AssignmentOperatorKind>(
              tree.node(assignment).payload.words[ast::kAssignmentExprOpWord]) !=
              ast::AssignmentOperatorKind::Assign) {
        return zc::none;
      }
      const ast::NodeId target(tree.node(assignment).payload.words[ast::kAssignmentExprLhsWord]);
      const ast::NodeId writeValue(
          tree.node(assignment).payload.words[ast::kAssignmentExprRhsWord]);
      if (!tree.contains(target) || !tree.contains(writeValue) ||
          !isScalarLiteral(tree.node(writeValue).kind)) {
        return zc::none;
      }
      if (tree.node(target).kind == ast::SyntaxKind::IdentExpr) continue;
      if (!returnsLocalField || tree.node(target).kind != ast::SyntaxKind::MemberExpression) {
        return zc::none;
      }
      const ast::NodeId object(tree.node(target).payload.words[ast::kMemberExpressionObjectWord]);
      if (!tree.contains(object) || tree.node(object).kind != ast::SyntaxKind::IdentExpr) {
        return zc::none;
      }
    }
  }
  zc::Maybe<ast::NodeId> localInitializer;
  if (tree.contains(initializer)) { localInitializer = initializer; }
  return FunctionReturnShape{body,
                             returnNode,
                             value,
                             pattern,
                             zc::mv(localInitializer),
                             ast::NodeList{statements.first + 1, statements.size - 2},
                             true,
                             localReference,
                             returnsLocalField,
                             reborrow != zc::none,
                             ast::NodeId(),
                             ast::NodeId(),
                             ast::NodeId(),
                             false,
                             false,
                             returnsReceiverCall,
                             localBorrow != zc::none,
                             zc::mv(unsafeBlock)};
}

bool noUnsupportedFacts(const checker::checked::VerifiedCheckedFacts& facts) {
  return facts.coercions().size() == 0 && facts.casts().size() == 0 &&
         facts.compoundAssignments().size() == 0 && facts.observedOperations().size() == 0 &&
         facts.captures().size() == 0 && facts.exhaustiveness().size() == 0 &&
         facts.unsafeOperations().size() == 0 && facts.projections().size() == 0 &&
         facts.obligations().size() == 0 && facts.errorUnionShapes().size() == 0 &&
         facts.errorOperators().size() == 0;
}

zc::Maybe<identity::DefId> resolvedDefinition(const binder::ImmutableBindingMetadata& bindings,
                                              ast::NodeId node) {
  zc::Maybe<identity::DefId> result;
  for (const auto& resolution : bindings.nodeBindings()) {
    if (resolution.node != node || !resolution.value.is<binder::BoundNameResolution>()) {
      continue;
    }
    const auto& target =
        resolution.value.get<binder::BoundNameResolution>().canonicalTarget.value();
    if (!target.is<binder::DefinitionBindingTarget>() || result != zc::none) { return zc::none; }
    result = target.get<binder::DefinitionBindingTarget>().definition;
  }
  return result;
}

zc::Maybe<binder::OwnerLocalBindingId> resolvedOwnerLocal(
    const binder::ImmutableBindingMetadata& bindings, ast::NodeId node) {
  zc::Maybe<binder::OwnerLocalBindingId> result;
  for (const auto& resolution : bindings.nodeBindings()) {
    if (resolution.node != node || !resolution.value.is<binder::BoundNameResolution>()) continue;
    const auto& target =
        resolution.value.get<binder::BoundNameResolution>().canonicalTarget.value();
    if (!target.is<binder::OwnerLocalBindingTarget>() || result != zc::none) { return zc::none; }
    result = target.get<binder::OwnerLocalBindingTarget>().binding;
  }
  return result;
}

zc::Maybe<identity::CallableParameterId> resolvedCallableParameter(
    const binder::ImmutableBindingMetadata& bindings, ast::NodeId node) {
  zc::Maybe<identity::CallableParameterId> result;
  for (const auto& resolution : bindings.nodeBindings()) {
    if (resolution.node != node || !resolution.value.is<binder::BoundNameResolution>()) {
      continue;
    }
    const auto& target =
        resolution.value.get<binder::BoundNameResolution>().canonicalTarget.value();
    if (!target.is<binder::CallableParameterBindingTarget>() || result != zc::none) {
      return zc::none;
    }
    result = target.get<binder::CallableParameterBindingTarget>().parameter;
  }
  return result;
}

bool ownerLocalMatches(const binder::ImmutableDefinitionInventory& definitions,
                       binder::OwnerLocalBindingId binding, ast::NodeId pattern,
                       const ast::Tree& tree) {
  for (const auto& local : definitions.ownerLocalBindings()) {
    if (local.binding != binding) continue;
    if (!local.site.value().is<binder::PatternBindingSite>()) return false;
    const auto& site = local.site.value().get<binder::PatternBindingSite>();
    if (!tree.contains(site.introducer) ||
        tree.node(site.introducer).kind != ast::SyntaxKind::VariableDeclarator) {
      return false;
    }
    return ast::NodeId(
               tree.node(site.introducer).payload.words[ast::kVariableDeclaratorPatternWord]) ==
           pattern;
  }
  return false;
}

zc::Maybe<binder::OwnerLocalBindingId> ownerLocalBindingForPattern(
    const binder::ImmutableDefinitionInventory& definitions, ast::NodeId pattern,
    const ast::Tree& tree) {
  zc::Maybe<binder::OwnerLocalBindingId> result;
  for (const auto& local : definitions.ownerLocalBindings()) {
    if (!local.site.value().is<binder::PatternBindingSite>()) continue;
    const auto& site = local.site.value().get<binder::PatternBindingSite>();
    if (!tree.contains(site.introducer) ||
        tree.node(site.introducer).kind != ast::SyntaxKind::VariableDeclarator) {
      continue;
    }
    if (ast::NodeId(
            tree.node(site.introducer).payload.words[ast::kVariableDeclaratorPatternWord]) !=
        pattern) {
      continue;
    }
    if (result != zc::none) return zc::none;
    result = local.binding;
  }
  return result;
}

zc::Maybe<checker::checked::CheckedNodeKey> checkedNodeKey(
    const ast::Tree& tree, const binder::CanonicalParsedModule& parsedModule, ast::NodeId target) {
  zc::Maybe<checker::checked::CheckedNodeKey> result;
  uint32_t preorder = 0;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    const uint32_t ordinal = preorder++;
    if (node != target || result != zc::none) return;
    auto sourceSpan = parsedModule.spanFor(syntax.range);
    ZC_IF_SOME(span, sourceSpan) {
      result = checker::checked::CheckedNodeKey{static_cast<uint32_t>(syntax.kind), ordinal,
                                                span.clone()};
    }
  });
  return result;
}

bool sameNodeKey(const checker::checked::CheckedNodeKey& left,
                 const checker::checked::CheckedNodeKey& right) {
  return left.syntaxKind == right.syntaxKind && left.schemaPreorder == right.schemaPreorder &&
         sameSpan(left.sourceSpan, right.sourceSpan);
}

zc::Maybe<size_t> dispatchFactIndex(
    zc::ArrayPtr<const checker::dispatch::VerifiedDispatchFact> facts,
    const checker::checked::CheckedNodeKey& node) {
  zc::Maybe<size_t> result;
  for (size_t index = 0; index < facts.size(); ++index) {
    if (!sameNodeKey(facts[index].checkedNode, node)) continue;
    if (result != zc::none) return zc::none;
    result = index;
  }
  return result;
}

zc::Maybe<size_t> signatureIndex(
    zc::ArrayPtr<const checker::signature::SemanticSignature> signatures,
    identity::DefId definition) {
  zc::Maybe<size_t> result;
  for (size_t index = 0; index < signatures.size(); ++index) {
    if (signatures[index].definition != definition) continue;
    if (result != zc::none) return zc::none;
    result = index;
  }
  return result;
}

zc::Maybe<size_t> signatureRootIndex(
    zc::ArrayPtr<const module_interface::SignatureRootAuthorization> roots,
    identity::DefId definition) {
  zc::Maybe<size_t> result;
  for (size_t index = 0; index < roots.size(); ++index) {
    if (roots[index].canonicalDefinition != definition) continue;
    if (result != zc::none) return zc::none;
    result = index;
  }
  return result;
}

bool definitionBelongsToModule(const binder::MaterializedDefinitionInventoryEntry& definition,
                               const binder::ImmutableDefinitionInventory& definitions) {
  return definition.record.module().encode().asPtr() ==
         definitions.identities().stableWitness().module().encode().asPtr();
}

bool typeExists(identity::SemanticTypeId semanticType,
                const type::SemanticTypeStore& semanticTypes) {
  return semanticTypes.get(semanticType).is<type::SemanticTypeLookup>();
}

void append(zc::Vector<char>& output, zc::StringPtr text) { output.addAll(text); }

void appendDigest(zc::Vector<char>& output, const identity::Sha256Digest& digest) {
  append(output, zc::encodeHex(digest.bytes()));
}

void appendInterfaceRevision(zc::Vector<char>& output,
                             const module_interface::ImportedInterfaceRevision& revision) {
  const auto& value = revision.variant();
  if (value.is<module_interface::UserImportedInterfaceRevision>()) {
    append(output, "user:"_zc);
    appendDigest(output,
                 value.get<module_interface::UserImportedInterfaceRevision>().value.digest());
    return;
  }
  append(output, "core:"_zc);
  appendDigest(
      output, value.get<module_interface::ToolchainCoreImportedInterfaceRevision>().value.digest());
}

}  // namespace

HirVisibility HirVisibility::module(identity::ModuleId module) noexcept {
  return HirVisibility(ModuleHirVisibility{module});
}

HirVisibility HirVisibility::external() noexcept { return HirVisibility(ExternalHirVisibility{}); }

HirVisibility HirVisibility::clone() const noexcept {
  if (value.is<ModuleHirVisibility>()) { return module(value.get<ModuleHirVisibility>().module); }
  return external();
}

HirVisibilityKind HirVisibility::kind() const noexcept {
  return value.is<ModuleHirVisibility>() ? HirVisibilityKind::Module : HirVisibilityKind::External;
}

zc::Maybe<identity::ModuleId> HirVisibility::visibleModule() const noexcept {
  if (!value.is<ModuleHirVisibility>()) return zc::none;
  return value.get<ModuleHirVisibility>().module;
}

struct HirModuleCandidate::Impl final {
  Impl(VerifiedCheckedModule&& checkedModule, zc::Vector<HirValueDeclaration>&& declarations,
       zc::Vector<HirFunctionDeclaration>&& functions, zc::Vector<HirBlockStatement>&& blocks,
       zc::Vector<HirReturnStatement>&& returns, zc::Vector<HirBindingPattern>&& patterns,
       zc::Vector<HirScalarLiteralExpression>&& expressions,
       zc::Vector<HirNominalAggregateExpression>&& aggregates, zc::Vector<HirLocalBinding>&& locals,
       zc::Vector<HirLocalWriteStatement>&& localWrites,
       zc::Vector<HirLocalReferenceExpression>&& localReferences,
       zc::Vector<HirLocalFieldProjectionExpression>&& localFieldProjections,
       zc::Vector<HirParameterReferenceExpression>&& parameterReferences,
       zc::Vector<HirParameterIndexExpression>&& parameterIndexes,
       zc::Vector<HirParameterReborrowExpression>&& parameterReborrows,
       zc::Vector<HirLocalBorrowExpression>&& localBorrows,
       zc::Vector<HirDirectCallExpression>&& calls,
       zc::Vector<HirReceiverCallExpression>&& receiverCalls,
       zc::Vector<HirUnsafeBlockExpression>&& unsafeBlocks) noexcept
      : checkedModule(zc::mv(checkedModule)),
        declarations(zc::mv(declarations)),
        functions(zc::mv(functions)),
        blocks(zc::mv(blocks)),
        returns(zc::mv(returns)),
        patterns(zc::mv(patterns)),
        expressions(zc::mv(expressions)),
        aggregates(zc::mv(aggregates)),
        locals(zc::mv(locals)),
        localWrites(zc::mv(localWrites)),
        localReferences(zc::mv(localReferences)),
        localFieldProjections(zc::mv(localFieldProjections)),
        parameterReferences(zc::mv(parameterReferences)),
        parameterIndexes(zc::mv(parameterIndexes)),
        parameterReborrows(zc::mv(parameterReborrows)),
        localBorrows(zc::mv(localBorrows)),
        calls(zc::mv(calls)),
        receiverCalls(zc::mv(receiverCalls)),
        unsafeBlocks(zc::mv(unsafeBlocks)) {}

  VerifiedCheckedModule checkedModule;
  zc::Vector<HirValueDeclaration> declarations;
  zc::Vector<HirFunctionDeclaration> functions;
  zc::Vector<HirBlockStatement> blocks;
  zc::Vector<HirReturnStatement> returns;
  zc::Vector<HirBindingPattern> patterns;
  zc::Vector<HirScalarLiteralExpression> expressions;
  zc::Vector<HirNominalAggregateExpression> aggregates;
  zc::Vector<HirLocalBinding> locals;
  zc::Vector<HirLocalWriteStatement> localWrites;
  zc::Vector<HirLocalReferenceExpression> localReferences;
  zc::Vector<HirLocalFieldProjectionExpression> localFieldProjections;
  zc::Vector<HirParameterReferenceExpression> parameterReferences;
  zc::Vector<HirParameterIndexExpression> parameterIndexes;
  zc::Vector<HirParameterReborrowExpression> parameterReborrows;
  zc::Vector<HirLocalBorrowExpression> localBorrows;
  zc::Vector<HirDirectCallExpression> calls;
  zc::Vector<HirReceiverCallExpression> receiverCalls;
  zc::Vector<HirUnsafeBlockExpression> unsafeBlocks;
};

HirModuleCandidate::HirModuleCandidate(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
HirModuleCandidate::~HirModuleCandidate() noexcept(false) = default;
HirModuleCandidate::HirModuleCandidate(HirModuleCandidate&&) noexcept = default;
HirModuleCandidate& HirModuleCandidate::operator=(HirModuleCandidate&&) noexcept = default;

struct VerifiedHirModule::Impl final {
  Impl(VerifiedCheckedModule&& admittedCheckedModule,
       ownership::OwnershipAdmittedBoundModule&& boundModule,
       checker::CheckerIdentityAuthority&& identities,
       const checker::checked::CheckedFactsRepository& checkedRepository,
       driver::borrow_evidence::BorrowEvidenceRepositoryCapability&& borrowEvidenceCapability,
       const type::SemanticTypeStore& semanticTypes, zc::Vector<HirValueDeclaration>&& declarations,
       zc::Vector<HirFunctionDeclaration>&& functions, zc::Vector<HirBlockStatement>&& blocks,
       zc::Vector<HirReturnStatement>&& returns, zc::Vector<HirBindingPattern>&& patterns,
       zc::Vector<HirScalarLiteralExpression>&& expressions,
       zc::Vector<HirNominalAggregateExpression>&& aggregates, zc::Vector<HirLocalBinding>&& locals,
       zc::Vector<HirLocalWriteStatement>&& localWrites,
       zc::Vector<HirLocalReferenceExpression>&& localReferences,
       zc::Vector<HirLocalFieldProjectionExpression>&& localFieldProjections,
       zc::Vector<HirParameterReferenceExpression>&& parameterReferences,
       zc::Vector<HirParameterIndexExpression>&& parameterIndexes,
       zc::Vector<HirParameterReborrowExpression>&& parameterReborrows,
       zc::Vector<HirLocalBorrowExpression>&& localBorrows,
       zc::Vector<HirDirectCallExpression>&& calls,
       zc::Vector<HirReceiverCallExpression>&& receiverCalls,
       zc::Vector<HirUnsafeBlockExpression>&& unsafeBlocks) noexcept
      : admittedCheckedModule(zc::mv(admittedCheckedModule)),
        boundModule(zc::mv(boundModule)),
        identities(zc::mv(identities)),
        semanticContext(this->admittedCheckedModule.semanticContext()),
        contextFingerprint(this->admittedCheckedModule.contextFingerprint().clone()),
        compilationUnit(this->admittedCheckedModule.compilationUnit()),
        crate(this->admittedCheckedModule.crate()),
        module(this->admittedCheckedModule.module()),
        sourceContentDigest(this->admittedCheckedModule.sourceContentDigest()),
        parsedModuleReceipt(this->admittedCheckedModule.parsedModuleReceipt().digest()),
        checkedFactsRevision(this->admittedCheckedModule.checkedFactsRevision()),
        dispatchFactsRevision(this->admittedCheckedModule.dispatchFactsRevision()),
        borrowEvidenceRevision(this->admittedCheckedModule.borrowEvidenceRevision()),
        ownInterface(
            ModuleInterfaceLineage{this->admittedCheckedModule.ownInterface().module,
                                   this->admittedCheckedModule.ownInterface().revision.clone()}),
        checkedRepository(checkedRepository),
        borrowEvidenceCapability(zc::mv(borrowEvidenceCapability)),
        semanticTypes(semanticTypes),
        declarations(zc::mv(declarations)),
        functions(zc::mv(functions)),
        blocks(zc::mv(blocks)),
        returns(zc::mv(returns)),
        patterns(zc::mv(patterns)),
        expressions(zc::mv(expressions)),
        aggregates(zc::mv(aggregates)),
        locals(zc::mv(locals)),
        localWrites(zc::mv(localWrites)),
        localReferences(zc::mv(localReferences)),
        localFieldProjections(zc::mv(localFieldProjections)),
        parameterReferences(zc::mv(parameterReferences)),
        parameterIndexes(zc::mv(parameterIndexes)),
        parameterReborrows(zc::mv(parameterReborrows)),
        localBorrows(zc::mv(localBorrows)),
        calls(zc::mv(calls)),
        receiverCalls(zc::mv(receiverCalls)),
        unsafeBlocks(zc::mv(unsafeBlocks)) {
    for (const auto& interface : this->admittedCheckedModule.visibleImportedInterfaces()) {
      visibleImportedInterfaces.add(
          ModuleInterfaceLineage{interface.module, interface.revision.clone()});
    }
  }

  VerifiedCheckedModule admittedCheckedModule;
  ownership::OwnershipAdmittedBoundModule boundModule;
  checker::CheckerIdentityAuthority identities;
  identity::SemanticContextBrand semanticContext;
  identity::ContextFingerprint contextFingerprint;
  identity::CompilationUnitId compilationUnit;
  identity::CrateId crate;
  identity::ModuleId module;
  identity::Sha256Digest sourceContentDigest;
  identity::Sha256Digest parsedModuleReceipt;
  checker::checked::CheckedFactsRevision checkedFactsRevision;
  checker::dispatch::DispatchFactsRevision dispatchFactsRevision;
  driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision;
  ModuleInterfaceLineage ownInterface;
  zc::Vector<ModuleInterfaceLineage> visibleImportedInterfaces;
  const checker::checked::CheckedFactsRepository& checkedRepository;
  driver::borrow_evidence::BorrowEvidenceRepositoryCapability borrowEvidenceCapability;
  const type::SemanticTypeStore& semanticTypes;
  zc::Vector<HirValueDeclaration> declarations;
  zc::Vector<HirFunctionDeclaration> functions;
  zc::Vector<HirBlockStatement> blocks;
  zc::Vector<HirReturnStatement> returns;
  zc::Vector<HirBindingPattern> patterns;
  zc::Vector<HirScalarLiteralExpression> expressions;
  zc::Vector<HirNominalAggregateExpression> aggregates;
  zc::Vector<HirLocalBinding> locals;
  zc::Vector<HirLocalWriteStatement> localWrites;
  zc::Vector<HirLocalReferenceExpression> localReferences;
  zc::Vector<HirLocalFieldProjectionExpression> localFieldProjections;
  zc::Vector<HirParameterReferenceExpression> parameterReferences;
  zc::Vector<HirParameterIndexExpression> parameterIndexes;
  zc::Vector<HirParameterReborrowExpression> parameterReborrows;
  zc::Vector<HirLocalBorrowExpression> localBorrows;
  zc::Vector<HirDirectCallExpression> calls;
  zc::Vector<HirReceiverCallExpression> receiverCalls;
  zc::Vector<HirUnsafeBlockExpression> unsafeBlocks;
};

VerifiedHirModule::VerifiedHirModule(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
VerifiedHirModule::~VerifiedHirModule() noexcept(false) = default;
VerifiedHirModule::VerifiedHirModule(VerifiedHirModule&&) noexcept = default;
VerifiedHirModule& VerifiedHirModule::operator=(VerifiedHirModule&&) noexcept = default;

identity::SemanticContextBrand VerifiedHirModule::semanticContext() const noexcept {
  return impl->semanticContext;
}

const identity::ContextFingerprint& VerifiedHirModule::contextFingerprint() const noexcept {
  return impl->contextFingerprint;
}

identity::CompilationUnitId VerifiedHirModule::compilationUnit() const noexcept {
  return impl->compilationUnit;
}
identity::CrateId VerifiedHirModule::crate() const noexcept { return impl->crate; }
identity::ModuleId VerifiedHirModule::module() const noexcept { return impl->module; }

const identity::Sha256Digest& VerifiedHirModule::sourceContentDigest() const noexcept {
  return impl->sourceContentDigest;
}

const identity::Sha256Digest& VerifiedHirModule::parsedModuleReceiptDigest() const noexcept {
  return impl->parsedModuleReceipt;
}

const checker::checked::CheckedFactsRevision& VerifiedHirModule::checkedFactsRevision()
    const noexcept {
  return impl->checkedFactsRevision;
}

const checker::dispatch::DispatchFactsRevision& VerifiedHirModule::dispatchFactsRevision()
    const noexcept {
  return impl->dispatchFactsRevision;
}

const driver::borrow_evidence::BorrowEvidenceRevision& VerifiedHirModule::borrowEvidenceRevision()
    const noexcept {
  return impl->borrowEvidenceRevision;
}

const ModuleInterfaceLineage& VerifiedHirModule::ownInterface() const noexcept {
  return impl->ownInterface;
}

zc::ArrayPtr<const ModuleInterfaceLineage> VerifiedHirModule::visibleImportedInterfaces()
    const noexcept {
  return impl->visibleImportedInterfaces.asPtr();
}

ownership::OwnershipAdmittedBoundModule VerifiedHirModule::retainAdmittedBoundModule() const {
  return impl->boundModule.retain();
}

checker::CheckerIdentityAuthority VerifiedHirModule::retainIdentityAuthority() const {
  return impl->identities.clone();
}

const checker::checked::CheckedEvidenceLease& VerifiedHirModule::checkedEvidenceLease()
    const noexcept {
  return impl->admittedCheckedModule.checkedEvidenceLease();
}

const driver::borrow_evidence::VerifiedBorrowEvidenceLease& VerifiedHirModule::borrowEvidenceLease()
    const noexcept {
  return impl->admittedCheckedModule.borrowEvidenceLease();
}

const VerifiedCheckedModule& VerifiedHirModule::admittedCheckedModule() const noexcept {
  return impl->admittedCheckedModule;
}

driver::borrow_evidence::BorrowEvidenceRepositoryCapability
VerifiedHirModule::borrowEvidenceCapability() const noexcept {
  return impl->borrowEvidenceCapability.clone();
}

const type::SemanticTypeStore& VerifiedHirModule::semanticTypes() const noexcept {
  return impl->semanticTypes;
}

zc::ArrayPtr<const HirValueDeclaration> VerifiedHirModule::declarations() const noexcept {
  return impl->declarations.asPtr();
}

zc::ArrayPtr<const HirFunctionDeclaration> VerifiedHirModule::functions() const noexcept {
  return impl->functions.asPtr();
}

zc::ArrayPtr<const HirBlockStatement> VerifiedHirModule::blocks() const noexcept {
  return impl->blocks.asPtr();
}

zc::ArrayPtr<const HirReturnStatement> VerifiedHirModule::returns() const noexcept {
  return impl->returns.asPtr();
}

zc::ArrayPtr<const HirBindingPattern> VerifiedHirModule::patterns() const noexcept {
  return impl->patterns.asPtr();
}

zc::ArrayPtr<const HirScalarLiteralExpression> VerifiedHirModule::expressions() const noexcept {
  return impl->expressions.asPtr();
}

zc::ArrayPtr<const HirNominalAggregateExpression> VerifiedHirModule::aggregates() const noexcept {
  return impl->aggregates.asPtr();
}

zc::ArrayPtr<const HirLocalBinding> VerifiedHirModule::locals() const noexcept {
  return impl->locals.asPtr();
}

zc::ArrayPtr<const HirLocalWriteStatement> VerifiedHirModule::localWrites() const noexcept {
  return impl->localWrites.asPtr();
}

zc::ArrayPtr<const HirLocalReferenceExpression> VerifiedHirModule::localReferences()
    const noexcept {
  return impl->localReferences.asPtr();
}

zc::ArrayPtr<const HirLocalFieldProjectionExpression> VerifiedHirModule::localFieldProjections()
    const noexcept {
  return impl->localFieldProjections.asPtr();
}

zc::ArrayPtr<const HirParameterReferenceExpression> VerifiedHirModule::parameterReferences()
    const noexcept {
  return impl->parameterReferences.asPtr();
}

zc::ArrayPtr<const HirParameterIndexExpression> VerifiedHirModule::parameterIndexes()
    const noexcept {
  return impl->parameterIndexes.asPtr();
}

zc::ArrayPtr<const HirParameterReborrowExpression> VerifiedHirModule::parameterReborrows()
    const noexcept {
  return impl->parameterReborrows.asPtr();
}

zc::ArrayPtr<const HirLocalBorrowExpression> VerifiedHirModule::localBorrows() const noexcept {
  return impl->localBorrows.asPtr();
}

zc::ArrayPtr<const HirDirectCallExpression> VerifiedHirModule::calls() const noexcept {
  return impl->calls.asPtr();
}

zc::ArrayPtr<const HirReceiverCallExpression> VerifiedHirModule::receiverCalls() const noexcept {
  return impl->receiverCalls.asPtr();
}

zc::ArrayPtr<const HirUnsafeBlockExpression> VerifiedHirModule::unsafeBlocks() const noexcept {
  return impl->unsafeBlocks.asPtr();
}

zc::Maybe<zc::String> VerifiedHirModule::dump() const {
  auto moduleKey = impl->identities.module(impl->module);
  const auto& checkedLease = impl->admittedCheckedModule.checkedEvidenceLease();
  const auto& borrowLease = impl->admittedCheckedModule.borrowEvidenceLease();
  const auto borrowCapability = borrowEvidenceCapability();
  const auto borrowEvidence = borrowCapability.lookup(borrowLease);
  if (moduleKey == zc::none || impl->checkedRepository.lookup(checkedLease) == zc::none ||
      !borrowEvidence.isResolved() ||
      borrowEvidence.evidence().revision().digest() != impl->borrowEvidenceRevision.digest() ||
      borrowLease.key().revision.digest() != impl->borrowEvidenceRevision.digest()) {
    return zc::none;
  }
  zc::Vector<char> output;
  append(output, "zom.hir\nmodule "_zc);
  ZC_IF_SOME(key, moduleKey) { append(output, zc::encodeHex(key.key().encode().asPtr())); }
  append(output, "\ncontext "_zc);
  appendDigest(output, impl->contextFingerprint.digest());
  append(output, "\nchecked "_zc);
  appendDigest(output, impl->checkedFactsRevision.digest());
  append(output, "\nsource "_zc);
  appendDigest(output, impl->sourceContentDigest);
  append(output, "\nparsed "_zc);
  appendDigest(output, impl->parsedModuleReceipt);
  append(output, "\ndispatch "_zc);
  appendDigest(output, impl->dispatchFactsRevision.digest());
  append(output, "\nborrow-evidence "_zc);
  appendDigest(output, impl->borrowEvidenceRevision.digest());
  append(output, "\ninterface "_zc);
  appendInterfaceRevision(output, impl->ownInterface.revision);
  append(output, "\n"_zc);
  for (const auto& imported : impl->visibleImportedInterfaces) {
    auto importedModule = impl->identities.module(imported.module);
    if (importedModule == zc::none) { return zc::none; }
    append(output, "import-interface "_zc);
    ZC_IF_SOME(key, importedModule) { append(output, zc::encodeHex(key.key().encode().asPtr())); }
    append(output, " "_zc);
    appendInterfaceRevision(output, imported.revision);
    append(output, "\n"_zc);
  }

  for (const auto& declaration : impl->declarations) {
    auto definition = impl->identities.definition(declaration.definition);
    auto semanticType = impl->semanticTypes.get(declaration.inferredType);
    if (definition == zc::none || !semanticType.is<type::SemanticTypeLookup>()) return zc::none;
    append(output, "decl h"_zc);
    append(output, zc::str(declaration.node.ordinal()));
    append(output, " def="_zc);
    ZC_IF_SOME(key, definition) { append(output, zc::encodeHex(key.key().encode().asPtr())); }
    append(output, " type="_zc);
    append(output, zc::encodeHex(semanticType.get<type::SemanticTypeLookup>().key().bytes()));
    append(output, " pattern=h"_zc);
    append(output, zc::str(declaration.pattern.ordinal()));
    append(output, " initializer=h"_zc);
    append(output, zc::str(declaration.initializer.ordinal()));
    append(output, "\n"_zc);
  }
  for (const auto& pattern : impl->patterns) {
    append(output, "pattern h"_zc);
    append(output, zc::str(pattern.node.ordinal()));
    append(output, " binding="_zc);
    auto definition = impl->identities.definition(pattern.binding);
    if (definition == zc::none) return zc::none;
    ZC_IF_SOME(key, definition) { append(output, zc::encodeHex(key.key().encode().asPtr())); }
    append(output, "\n"_zc);
  }
  for (const auto& function : impl->functions) {
    auto definition = impl->identities.definition(function.definition);
    auto resultType = impl->semanticTypes.get(function.resultType);
    if (definition == zc::none || !resultType.is<type::SemanticTypeLookup>()) return zc::none;
    append(output, "function h"_zc);
    append(output, zc::str(function.node.ordinal()));
    append(output, " def="_zc);
    ZC_IF_SOME(key, definition) { append(output, zc::encodeHex(key.key().encode().asPtr())); }
    append(output, " result="_zc);
    append(output, zc::encodeHex(resultType.get<type::SemanticTypeLookup>().key().bytes()));
    append(output, " body=h"_zc);
    append(output, zc::str(function.body.ordinal()));
    append(output, "\n"_zc);
  }
  for (const auto& block : impl->blocks) {
    append(output, "block h"_zc);
    append(output, zc::str(block.node.ordinal()));
    for (const auto statement : block.statements) {
      append(output, " statement=h"_zc);
      append(output, zc::str(statement.ordinal()));
    }
    append(output, "\n"_zc);
  }
  for (const auto& statement : impl->returns) {
    append(output, "return h"_zc);
    append(output, zc::str(statement.node.ordinal()));
    append(output, " value=h"_zc);
    append(output, zc::str(statement.value.ordinal()));
    append(output, "\n"_zc);
  }
  for (const auto& expression : impl->expressions) {
    append(output, "literal h"_zc);
    append(output, zc::str(expression.node.ordinal()));
    append(output, " value="_zc);
    auto encoded =
        checker::signature::SignatureFactsCanonicalCodec::encodeCanonicalConstValueFromAuthority(
            expression.value, impl->module, impl->identities, impl->semanticTypes);
    if (encoded == zc::none) return zc::none;
    ZC_IF_SOME(bytes, encoded) { append(output, zc::encodeHex(bytes.asPtr())); }
    append(output, "\n"_zc);
  }
  for (const auto& local : impl->locals) {
    auto semanticType = impl->semanticTypes.get(local.type);
    if (!semanticType.is<type::SemanticTypeLookup>()) return zc::none;
    append(output, "local h"_zc);
    append(output, zc::str(local.node.ordinal()));
    append(output, " l"_zc);
    append(output, zc::str(local.local.ordinal()));
    append(output, " type="_zc);
    append(output, zc::encodeHex(semanticType.get<type::SemanticTypeLookup>().key().bytes()));
    append(output, " initializer="_zc);
    ZC_IF_SOME(initializer, local.initializer) {
      append(output, "h"_zc);
      append(output, zc::str(initializer.ordinal()));
    } else {
      append(output, "none"_zc);
    }
    append(output, "\n"_zc);
  }
  for (const auto& reference : impl->localReferences) {
    auto semanticType = impl->semanticTypes.get(reference.type);
    if (!semanticType.is<type::SemanticTypeLookup>()) return zc::none;
    append(output, "local-ref h"_zc);
    append(output, zc::str(reference.node.ordinal()));
    append(output, " l"_zc);
    append(output, zc::str(reference.local.ordinal()));
    append(output, " type="_zc);
    append(output, zc::encodeHex(semanticType.get<type::SemanticTypeLookup>().key().bytes()));
    append(output, "\n"_zc);
  }
  for (const auto& call : impl->calls) {
    auto callee = impl->identities.definition(call.callee);
    auto calleeType = impl->semanticTypes.get(call.calleeType);
    auto resultType = impl->semanticTypes.get(call.resultType);
    if (callee == zc::none || !calleeType.is<type::SemanticTypeLookup>() ||
        !resultType.is<type::SemanticTypeLookup>()) {
      return zc::none;
    }
    append(output, "call h"_zc);
    append(output, zc::str(call.node.ordinal()));
    append(output, " callee="_zc);
    ZC_IF_SOME(key, callee) { append(output, zc::encodeHex(key.key().encode().asPtr())); }
    append(output, " callee-type="_zc);
    append(output, zc::encodeHex(calleeType.get<type::SemanticTypeLookup>().key().bytes()));
    append(output, " result="_zc);
    append(output, zc::encodeHex(resultType.get<type::SemanticTypeLookup>().key().bytes()));
    append(output, "\n"_zc);
  }
  for (const auto& unsafeBlock : impl->unsafeBlocks) {
    auto resultType = impl->semanticTypes.get(unsafeBlock.type);
    if (!resultType.is<type::SemanticTypeLookup>()) return zc::none;
    append(output, "unsafe-block h"_zc);
    append(output, zc::str(unsafeBlock.node.ordinal()));
    append(output, " body=h"_zc);
    append(output, zc::str(unsafeBlock.body.ordinal()));
    append(output, " type="_zc);
    append(output, zc::encodeHex(resultType.get<type::SemanticTypeLookup>().key().bytes()));
    append(output, "\n"_zc);
  }
  return zc::str(output.releaseAsArray());
}

ir::IrOperationResult<HirModuleCandidate> HirBuilder::build(VerifiedCheckedModule&& checkedModule) {
  const auto module = checkedModule.module();
  const auto registries = checkedModule.retainIdentityAuthority();
  const auto& facts = checkedModule.checkedFacts();
  const auto bound = checkedModule.retainAdmittedBoundModule();
  const auto borrowCapability = checkedModule.borrowEvidenceCapability();
  const auto borrowEvidence = borrowCapability.lookup(checkedModule.borrowEvidenceLease());
  if (registries.semanticContext() != checkedModule.semanticContext() ||
      registries.fingerprint().digest() != checkedModule.contextFingerprint().digest() ||
      registries.boundModule(module) == zc::none ||
      checkedModule.checkedRepository().lookup(checkedModule.checkedEvidenceLease()) == zc::none ||
      !borrowEvidence.isResolved() ||
      borrowEvidence.evidence().revision().digest() !=
          checkedModule.borrowEvidenceRevision().digest() ||
      checkedModule.borrowEvidenceLease().key().revision.digest() !=
          checkedModule.borrowEvidenceRevision().digest() ||
      checkedModule.dispatchFacts().facts().size() != facts.calls().size() ||
      !noUnsupportedFacts(facts)) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::AdditionalFact, module, registries, 0);
  }

  const auto definitions = bound.definitions().definitions();
  if (definitions.size() > UINT32_MAX / 4) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::InvalidFact, module, registries, 1);
  }
  zc::Vector<PendingValueDeclaration> pending;
  zc::Vector<PendingFunctionDeclaration> pendingFunctions;
  const auto& ownInterface = checkedModule.ownModuleInterface();
  const auto& signatures = ownInterface.signatures();
  const auto definitionInventory = binder::DefinitionInventory::collect(bound.tree());
  for (size_t definitionIndex = 0; definitionIndex < definitions.size(); ++definitionIndex) {
    const auto ordinal = static_cast<uint32_t>(definitionIndex);
    const auto& definition = definitions[ordinal];
    if (!hasExecutableBody(definition, bound.definitions())) { continue; }
    if (definition.record.kind() != identity::DefinitionKind::Function &&
        definition.record.kind() != identity::DefinitionKind::Static &&
        definition.record.kind() != identity::DefinitionKind::Constant) {
      continue;
    }
    if (definition.record.kind() == identity::DefinitionKind::Function) {
      const auto& tree = bound.tree();
      if (!tree.contains(definition.node) ||
          tree.node(definition.node).kind != ast::SyntaxKind::FunctionDecl ||
          !definition.site.value().is<binder::DeclarationDefinitionSite>()) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries,
                                             ordinal + 2);
      }
      auto bodyShape = functionReturnShape(tree, tree.node(definition.node));
      auto signaturePosition =
          signatureIndex(signatures.definitions.asPtr(), definition.definition);
      auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), definition.definition);
      if (bodyShape == zc::none || signaturePosition == zc::none || rootPosition == zc::none) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::MissingRequiredFact, module,
                                             registries, ordinal + 2);
      }
      FunctionReturnShape shape{};
      size_t signatureSlot = 0;
      size_t rootSlot = 0;
      ZC_IF_SOME(value, bodyShape) { shape = value; }
      ZC_IF_SOME(index, signaturePosition) { signatureSlot = index; }
      ZC_IF_SOME(index, rootPosition) { rootSlot = index; }
      auto nodeTypeIndex = factIndex(facts.nodeTypes(), shape.value);
      auto bodySpan = bound.parsedModule().spanFor(tree.node(shape.body).range);
      auto returnSpan = bound.parsedModule().spanFor(tree.node(shape.returnStatement).range);
      auto valueSpan = bound.parsedModule().spanFor(tree.node(shape.value).range);
      if (nodeTypeIndex == zc::none || bodySpan == zc::none || returnSpan == zc::none ||
          valueSpan == zc::none) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::MissingRequiredFact, module,
                                             registries, ordinal + 2);
      }
      zc::Maybe<identity::SourceSpan> unsafeBlockSpan;
      ZC_IF_SOME(unsafeNode, shape.unsafeBlock) {
        auto unsafeSpan = bound.parsedModule().spanFor(tree.node(unsafeNode).range);
        if (unsafeSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        ZC_IF_SOME(span, unsafeSpan) { unsafeBlockSpan = span.clone(); }
      }
      size_t nodeTypeSlot = 0;
      ZC_IF_SOME(index, nodeTypeIndex) { nodeTypeSlot = index; }
      const auto& signature = signatures.definitions[signatureSlot];
      const auto& root = signatures.roots[rootSlot];
      const auto& nodeType = facts.nodeTypes().entries()[nodeTypeSlot];
      if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
          !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>()) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries,
                                             ordinal + 2);
      }
      const auto& callable =
          signature.payload.variant().get<checker::signature::CallableSignature>();
      auto functionVisibility = visibility(root.visibility);
      auto functionLinkage = linkage(callable);
      if (functionVisibility == zc::none || functionLinkage == zc::none ||
          signature.definitionKind != identity::DefinitionKind::Function ||
          root.sourceModule != module || root.canonicalDefinition != definition.definition ||
          callable.receiver != zc::none || callable.raises != zc::none ||
          callable.success != nodeType.value ||
          !sameSpan(signature.declarationSpan, definition.source)) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries,
                                             ordinal + 2);
      }
      const ast::NodeId parameterListNode(
          tree.node(definition.node).payload.words[ast::kFunctionDeclParamsIdWord]);
      if (!tree.contains(parameterListNode) ||
          tree.node(parameterListNode).kind != ast::SyntaxKind::FunctionParameterList) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries,
                                             ordinal + 2);
      }
      const auto& parameterList = tree.node(parameterListNode);
      const ast::NodeList parameterNodes{
          parameterList.payload.words[ast::kFunctionParameterListParamsFirstWord],
          parameterList.payload.words[ast::kFunctionParameterListParamsSizeWord]};
      if (!tree.contains(parameterNodes) || parameterNodes.size != callable.parameters.size()) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::MissingRequiredFact, module,
                                             registries, ordinal + 2);
      }
      zc::Vector<HirParameter> parameters(callable.parameters.size());
      for (size_t index = 0; index < callable.parameters.size(); ++index) {
        const auto parameterNode = tree.list(parameterNodes)[index];
        const auto& parameter = callable.parameters[index];
        if (!tree.contains(parameterNode) ||
            tree.node(parameterNode).kind != ast::SyntaxKind::FunctionParameterDecl ||
            parameter.hasDefault || !typeExists(parameter.type, checkedModule.semanticTypes())) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        auto parameterSpan = bound.parsedModule().spanFor(tree.node(parameterNode).range);
        if (parameterSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        ZC_IF_SOME(span, parameterSpan) {
          parameters.add(HirParameter{parameter.parameter.clone(), parameter.type, span.clone()});
        }
      }
      if (!typeExists(callable.success, checkedModule.semanticTypes())) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries,
                                             ordinal + 2);
      }
      HirVisibility visibilityValue = HirVisibility::external();
      HirLinkage linkageValue = HirLinkage::Internal;
      identity::SourceSpan bodySpanValue = definition.source.clone();
      identity::SourceSpan returnSpanValue = definition.source.clone();
      identity::SourceSpan valueSpanValue = definition.source.clone();
      zc::Array<uint8_t> orderingKey;
      ZC_IF_SOME(value, functionVisibility) { visibilityValue = zc::mv(value); }
      ZC_IF_SOME(value, functionLinkage) { linkageValue = value; }
      ZC_IF_SOME(value, bodySpan) { bodySpanValue = value.clone(); }
      ZC_IF_SOME(value, returnSpan) { returnSpanValue = value.clone(); }
      ZC_IF_SOME(value, valueSpan) { valueSpanValue = value.clone(); }
      orderingKey = definition.key.encode();
      zc::Maybe<checker::checked::CanonicalConstValue> literal;
      zc::Maybe<HirDirectCallExpression> call;
      zc::Maybe<HirReceiverCallExpression> receiverCall;
      zc::Maybe<HirLocalBinding> local;
      zc::Maybe<HirNominalAggregateExpression> aggregate;
      zc::Vector<HirLocalWriteStatement> localWrites;
      zc::Vector<checker::checked::CanonicalConstValue> localWriteLiterals;
      zc::Maybe<HirLocalReferenceExpression> localReference;
      zc::Maybe<HirLocalFieldProjectionExpression> localFieldProjection;
      zc::Maybe<HirParameterReferenceExpression> parameterReference;
      zc::Maybe<HirParameterIndexExpression> parameterIndex;
      zc::Maybe<HirParameterReborrowExpression> parameterReborrow;
      zc::Maybe<HirLocalBorrowExpression> localBorrow;
      if (shape.isSequentialLocalReturn) {
        auto sourceBinding =
            resolvedOwnerLocal(bound.bindings(), shape.destinationLocalInitializer);
        auto destinationBinding =
            ownerLocalBindingForPattern(bound.definitions(), shape.localPattern, tree);
        const bool sourceIsLiteral = isScalarLiteral(tree.node(shape.sourceLocalInitializer).kind);
        const bool sourceIsAggregate =
            tree.node(shape.sourceLocalInitializer).kind == ast::SyntaxKind::StructLiteralExpr;
        auto literalIndex = factIndex(facts.literals(), shape.sourceLocalInitializer);
        auto aggregateIndex = factIndex(facts.aggregates(), shape.sourceLocalInitializer);
        auto sourceTypeIndex = factIndex(facts.nodeTypes(), shape.sourceLocalInitializer);
        auto initializerTypeIndex = factIndex(facts.nodeTypes(), shape.destinationLocalInitializer);
        auto returnTypeIndex = factIndex(facts.nodeTypes(), shape.value);
        auto sourcePatternSpan =
            bound.parsedModule().spanFor(tree.node(shape.sourceLocalPattern).range);
        auto destinationPatternSpan =
            bound.parsedModule().spanFor(tree.node(shape.localPattern).range);
        auto sourceInitializerSpan =
            bound.parsedModule().spanFor(tree.node(shape.sourceLocalInitializer).range);
        auto destinationInitializerSpan =
            bound.parsedModule().spanFor(tree.node(shape.destinationLocalInitializer).range);
        if (sourceBinding == zc::none || destinationBinding == zc::none ||
            sourceBinding == destinationBinding || (!sourceIsLiteral && !sourceIsAggregate) ||
            (sourceIsLiteral && literalIndex == zc::none) ||
            (sourceIsAggregate && aggregateIndex == zc::none) || sourceTypeIndex == zc::none ||
            initializerTypeIndex == zc::none || returnTypeIndex == zc::none ||
            sourcePatternSpan == zc::none || destinationPatternSpan == zc::none ||
            sourceInitializerSpan == zc::none || destinationInitializerSpan == zc::none ||
            !ownerLocalMatches(bound.definitions(), ZC_ASSERT_NONNULL(sourceBinding),
                               shape.sourceLocalPattern, tree) ||
            !ownerLocalMatches(bound.definitions(), ZC_ASSERT_NONNULL(destinationBinding),
                               shape.localPattern, tree)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t literalSlot = 0;
        size_t aggregateSlot = 0;
        size_t sourceTypeSlot = 0;
        size_t initializerTypeSlot = 0;
        size_t returnTypeSlot = 0;
        ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
        ZC_IF_SOME(index, aggregateIndex) { aggregateSlot = index; }
        ZC_IF_SOME(index, sourceTypeIndex) { sourceTypeSlot = index; }
        ZC_IF_SOME(index, initializerTypeIndex) { initializerTypeSlot = index; }
        ZC_IF_SOME(index, returnTypeIndex) { returnTypeSlot = index; }
        const auto sourceType = facts.nodeTypes().entries()[sourceTypeSlot].value;
        const auto initializerType = facts.nodeTypes().entries()[initializerTypeSlot].value;
        const auto returnType = facts.nodeTypes().entries()[returnTypeSlot].value;
        if (sourceType != initializerType || initializerType != returnType ||
            returnType != callable.success ||
            !typeExists(sourceType, checkedModule.semanticTypes())) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        zc::Maybe<checker::checked::CanonicalConstValue> sequentialLiteral;
        zc::Maybe<HirNominalAggregateExpression> sequentialAggregate;
        if (sourceIsLiteral) {
          const auto& sourceLiteral = facts.literals().entries()[literalSlot].value;
          if (sourceLiteral.type != sourceType ||
              !sameSpan(sourceLiteral.sourceSpan, ZC_ASSERT_NONNULL(sourceInitializerSpan))) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          sequentialLiteral = sourceLiteral.literal.clone();
        } else {
          const auto& sourceAggregate = facts.aggregates().entries()[aggregateSlot].value;
          if (sourceAggregate.node != shape.sourceLocalInitializer ||
              !sourceAggregate.kind.variant().is<checker::checked::NominalAggregate>() ||
              sourceAggregate.resultType != sourceType ||
              !sameSpan(sourceAggregate.sourceSpan, ZC_ASSERT_NONNULL(sourceInitializerSpan))) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          zc::Vector<HirNominalAggregateElement> elements;
          for (const auto& sourceElement : sourceAggregate.elements) {
            if (sourceElement.field == zc::none ||
                sourceElement.sourceType != sourceElement.destinationType ||
                sourceElement.adjustment != zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            auto elementLiteral = factIndex(facts.literals(), sourceElement.sourceNode);
            auto elementSpan =
                bound.parsedModule().spanFor(tree.node(sourceElement.sourceNode).range);
            if (elementLiteral == zc::none || elementSpan == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t elementSlot = 0;
            ZC_IF_SOME(index, elementLiteral) { elementSlot = index; }
            const auto& literalFact = facts.literals().entries()[elementSlot].value;
            if (literalFact.type != sourceElement.destinationType ||
                !sameSpan(literalFact.sourceSpan, ZC_ASSERT_NONNULL(elementSpan))) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            ZC_IF_SOME(field, sourceElement.field) {
              elements.add(HirNominalAggregateElement{field, sourceElement.destinationType,
                                                      literalFact.literal.clone(),
                                                      ZC_ASSERT_NONNULL(elementSpan).clone()});
            }
          }
          sequentialAggregate = HirNominalAggregateExpression{
              HirNodeId(),
              sourceAggregate.kind.variant().get<checker::checked::NominalAggregate>().definition,
              sourceType,
              zc::mv(elements),
              HirValueCategory::Value,
              ZC_ASSERT_NONNULL(sourceInitializerSpan).clone()};
        }
        zc::Maybe<HirNodeId> sourceInitializerNode{HirNodeId()};
        zc::Maybe<identity::SourceSpan> sourceInitializerSource(
            ZC_ASSERT_NONNULL(sourceInitializerSpan).clone());
        zc::Maybe<HirNodeId> destinationInitializerNode{HirNodeId()};
        zc::Maybe<identity::SourceSpan> destinationInitializerSource(
            ZC_ASSERT_NONNULL(destinationInitializerSpan).clone());
        PendingSequentialLocalReturn sequential{
            HirLocalBinding{HirNodeId(), HirLocalId(), sourceType, zc::mv(sourceInitializerNode),
                            ZC_ASSERT_NONNULL(sourcePatternSpan).clone(),
                            zc::mv(sourceInitializerSource)},
            HirLocalBinding{HirNodeId(), HirLocalId(), sourceType,
                            zc::mv(destinationInitializerNode),
                            ZC_ASSERT_NONNULL(destinationPatternSpan).clone(),
                            zc::mv(destinationInitializerSource)},
            zc::mv(sequentialLiteral),
            zc::mv(sequentialAggregate),
            HirLocalReferenceExpression{HirNodeId(), HirLocalId(), sourceType,
                                        HirValueCategory::Place,
                                        ZC_ASSERT_NONNULL(destinationInitializerSpan).clone()},
            HirLocalReferenceExpression{HirNodeId(), HirLocalId(), sourceType,
                                        HirValueCategory::Place, valueSpanValue.clone()},
            shape.sequentialReturnUsesSource};
        pendingFunctions.add(PendingFunctionDeclaration{definition.definition,
                                                        callable.success,
                                                        zc::mv(parameters),
                                                        zc::mv(visibilityValue),
                                                        linkageValue,
                                                        definition.source.clone(),
                                                        bodySpanValue.clone(),
                                                        returnSpanValue.clone(),
                                                        valueSpanValue.clone(),
                                                        zc::none,
                                                        zc::none,
                                                        zc::none,
                                                        zc::none,
                                                        zc::none,
                                                        {},
                                                        {},
                                                        zc::none,
                                                        zc::none,
                                                        zc::none,
                                                        zc::none,
                                                        zc::none,
                                                        zc::none,
                                                        zc::mv(sequential),
                                                        zc::none,
                                                        zc::mv(orderingKey)});
        continue;
      }
      if (shape.returnsLocal) {
        auto localBinding = resolvedOwnerLocal(bound.bindings(), shape.localReference);
        auto patternSpan = bound.parsedModule().spanFor(tree.node(shape.localPattern).range);
        if (localBinding == zc::none || patternSpan == zc::none ||
            !ownerLocalMatches(bound.definitions(), ZC_ASSERT_NONNULL(localBinding),
                               shape.localPattern, tree)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        identity::SemanticTypeId localType = nodeType.value;
        if (shape.returnsLocalBorrow) {
          auto borrowTypeLookup = checkedModule.semanticTypes().get(nodeType.value);
          if (!borrowTypeLookup.is<type::SemanticTypeLookup>() ||
              !borrowTypeLookup.get<type::SemanticTypeLookup>()
                   .data()
                   .is<type::semantic::ReferenceTypeData>()) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          localType = borrowTypeLookup.get<type::SemanticTypeLookup>()
                          .data()
                          .get<type::semantic::ReferenceTypeData>()
                          .referent;
        }
        if (shape.returnsLocalField) {
          auto memberIndex = factIndex(facts.members(), shape.value);
          if (memberIndex == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t memberSlot = 0;
          ZC_IF_SOME(index, memberIndex) { memberSlot = index; }
          const auto& member = facts.members().entries()[memberSlot].value;
          if (member.node != shape.value || member.memberType != nodeType.value ||
              member.adjustment != zc::none ||
              !typeExists(member.receiverType, checkedModule.semanticTypes())) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          localType = member.receiverType;
        }
        zc::Maybe<HirNodeId> noInitializer;
        zc::Maybe<identity::SourceSpan> noInitializerSpan;
        local = HirLocalBinding{HirNodeId(),
                                HirLocalId(),
                                localType,
                                zc::mv(noInitializer),
                                ZC_ASSERT_NONNULL(patternSpan).clone(),
                                zc::mv(noInitializerSpan)};
        if (!shape.returnsLocalField && !shape.returnsLocalReborrow && !shape.returnsLocalBorrow) {
          localReference =
              HirLocalReferenceExpression{HirNodeId(), HirLocalId(), nodeType.value,
                                          HirValueCategory::Place, valueSpanValue.clone()};
        }
        ZC_IF_SOME(initializer, shape.localInitializer) {
          auto initializerTypeIndex = factIndex(facts.nodeTypes(), initializer);
          auto initializerSpan = bound.parsedModule().spanFor(tree.node(initializer).range);
          if (initializerTypeIndex == zc::none || initializerSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t initializerTypeSlot = 0;
          ZC_IF_SOME(index, initializerTypeIndex) { initializerTypeSlot = index; }
          const auto& initializerType = facts.nodeTypes().entries()[initializerTypeSlot].value;
          if ((!shape.returnsLocalField && !shape.returnsReceiverCall &&
               !shape.returnsLocalBorrow && initializerType != nodeType.value) ||
              (!shape.returnsLocalField && !shape.returnsReceiverCall &&
               !shape.returnsLocalBorrow && initializerType != callable.success) ||
              (shape.returnsLocalBorrow && initializerType != localType)) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          zc::Maybe<HirNodeId> initializerNode;
          initializerNode = HirNodeId();
          zc::Maybe<identity::SourceSpan> initializerSource;
          ZC_IF_SOME(value, initializerSpan) { initializerSource = value.clone(); }
          local = HirLocalBinding{HirNodeId(),
                                  HirLocalId(),
                                  initializerType,
                                  zc::mv(initializerNode),
                                  ZC_ASSERT_NONNULL(patternSpan).clone(),
                                  zc::mv(initializerSource)};
          if (!shape.returnsLocalField && !shape.returnsLocalReborrow &&
              !shape.returnsLocalBorrow) {
            identity::SourceSpan referenceSpan = valueSpanValue.clone();
            if (shape.returnsReceiverCall) {
              const auto& sourceCall = tree.node(shape.value);
              const ast::NodeId calleeNode(
                  sourceCall.payload.words[ast::kCallExpressionCalleeWord]);
              const ast::NodeId receiverNode(
                  tree.node(calleeNode).payload.words[ast::kMemberExpressionObjectWord]);
              auto receiverSpan = bound.parsedModule().spanFor(tree.node(receiverNode).range);
              if (receiverSpan == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              referenceSpan = ZC_ASSERT_NONNULL(receiverSpan).clone();
            }
            localReference =
                HirLocalReferenceExpression{HirNodeId(), HirLocalId(), initializerType,
                                            HirValueCategory::Place, zc::mv(referenceSpan)};
          }
          if (isScalarLiteral(tree.node(initializer).kind)) {
            auto literalIndex = factIndex(facts.literals(), initializer);
            if (literalIndex == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t literalSlot = 0;
            ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
            const auto& sourceLiteral = facts.literals().entries()[literalSlot].value;
            if (sourceLiteral.type != initializerType ||
                !sameSpan(sourceLiteral.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan))) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            literal = sourceLiteral.literal.clone();
          } else if (tree.node(initializer).kind == ast::SyntaxKind::StructLiteralExpr) {
            auto aggregateIndex = factIndex(facts.aggregates(), initializer);
            if (aggregateIndex == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t aggregateSlot = 0;
            ZC_IF_SOME(index, aggregateIndex) { aggregateSlot = index; }
            const auto& sourceAggregate = facts.aggregates().entries()[aggregateSlot].value;
            if (sourceAggregate.node != initializer ||
                !sourceAggregate.kind.variant().is<checker::checked::NominalAggregate>() ||
                sourceAggregate.resultType != initializerType ||
                !sameSpan(sourceAggregate.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan))) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            zc::Vector<HirNominalAggregateElement> elements;
            for (const auto& sourceElement : sourceAggregate.elements) {
              if (sourceElement.field == zc::none ||
                  sourceElement.sourceType != sourceElement.destinationType ||
                  sourceElement.adjustment != zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::InvalidFact, module,
                                                     registries, ordinal + 2);
              }
              auto elementLiteral = factIndex(facts.literals(), sourceElement.sourceNode);
              auto elementSpan =
                  bound.parsedModule().spanFor(tree.node(sourceElement.sourceNode).range);
              if (elementLiteral == zc::none || elementSpan == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              size_t literalSlot = 0;
              ZC_IF_SOME(index, elementLiteral) { literalSlot = index; }
              const auto& literalFact = facts.literals().entries()[literalSlot].value;
              if (literalFact.type != sourceElement.destinationType ||
                  !sameSpan(literalFact.sourceSpan, ZC_ASSERT_NONNULL(elementSpan))) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::InvalidFact, module,
                                                     registries, ordinal + 2);
              }
              ZC_IF_SOME(field, sourceElement.field) {
                elements.add(HirNominalAggregateElement{field, sourceElement.destinationType,
                                                        literalFact.literal.clone(),
                                                        ZC_ASSERT_NONNULL(elementSpan).clone()});
              }
            }
            aggregate = HirNominalAggregateExpression{
                HirNodeId(),
                sourceAggregate.kind.variant().get<checker::checked::NominalAggregate>().definition,
                initializerType,
                zc::mv(elements),
                HirValueCategory::Value,
                ZC_ASSERT_NONNULL(initializerSpan).clone()};
          } else if (tree.node(initializer).kind == ast::SyntaxKind::IdentExpr) {
            auto parameter = resolvedCallableParameter(bound.bindings(), initializer);
            if (parameter == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            ZC_IF_SOME(handle, parameter) {
              auto authority = registries.callableParameter(handle);
              if (authority == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              ZC_IF_SOME(entry, authority) {
                bool matches = false;
                for (const auto& candidate : parameters) {
                  if (candidate.key == entry.key() && candidate.type == initializerType) {
                    matches = true;
                  }
                }
                if (!matches) {
                  return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                       ir::IrFailureKind::InvalidFact, module,
                                                       registries, ordinal + 2);
                }
                parameterReference = HirParameterReferenceExpression{
                    HirNodeId(), entry.key().clone(), initializerType, HirValueCategory::Place,
                    ZC_ASSERT_NONNULL(initializerSpan).clone()};
              }
            }
          }
        }
        if (shape.returnsLocalField) {
          auto memberIndex = factIndex(facts.members(), shape.value);
          auto placeIndex = factIndex(facts.places(), shape.value);
          if (memberIndex == zc::none || placeIndex == zc::none || local == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t memberSlot = 0;
          size_t placeSlot = 0;
          ZC_IF_SOME(index, memberIndex) { memberSlot = index; }
          ZC_IF_SOME(index, placeIndex) { placeSlot = index; }
          const auto& member = facts.members().entries()[memberSlot].value;
          const auto& place = facts.places().entries()[placeSlot].value;
          const auto& root = place.root.variant();
          if (member.node != shape.value || member.receiverType != ZC_ASSERT_NONNULL(local).type ||
              member.memberType != nodeType.value || member.adjustment != zc::none ||
              !root.is<checker::checked::OwnerLocalPlaceRoot>() ||
              root.get<checker::checked::OwnerLocalPlaceRoot>().binding !=
                  ZC_ASSERT_NONNULL(localBinding) ||
              place.projections.size() != 1 ||
              !place.projections[0].variant().is<checker::checked::FieldProjection>() ||
              place.projections[0].variant().get<checker::checked::FieldProjection>().field !=
                  member.member ||
              place.type != nodeType.value || !place.movable) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          localFieldProjection = HirLocalFieldProjectionExpression{
              HirNodeId(),           HirLocalId(),      member.member,
              member.receiverType,   member.memberType, HirValueCategory::Place,
              valueSpanValue.clone()};
        }
        for (size_t writeIndex = 0; writeIndex < shape.localWrites.size; ++writeIndex) {
          auto writeStatement = statementItem(tree, tree.list(shape.localWrites)[writeIndex]);
          if (writeStatement == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          ast::NodeId writeStatementNode;
          ZC_IF_SOME(value, writeStatement) { writeStatementNode = value; }
          const ast::NodeId write(
              tree.node(writeStatementNode).payload.words[ast::kExpressionStatementExpressionWord]);
          ast::NodeId target;
          ast::NodeId writeValue;
          target = ast::NodeId(tree.node(write).payload.words[ast::kAssignmentExprLhsWord]);
          writeValue = ast::NodeId(tree.node(write).payload.words[ast::kAssignmentExprRhsWord]);
          auto assignmentTypeIndex = factIndex(facts.nodeTypes(), write);
          auto targetTypeIndex = factIndex(facts.nodeTypes(), target);
          auto valueTypeIndex = factIndex(facts.nodeTypes(), writeValue);
          auto literalIndex = factIndex(facts.literals(), writeValue);
          auto assignmentSpan = bound.parsedModule().spanFor(tree.node(write).range);
          auto valueSpan = bound.parsedModule().spanFor(tree.node(writeValue).range);
          ast::NodeId targetReference = target;
          if (tree.node(target).kind == ast::SyntaxKind::MemberExpression) {
            targetReference =
                ast::NodeId(tree.node(target).payload.words[ast::kMemberExpressionObjectWord]);
          }
          auto targetBinding = resolvedOwnerLocal(bound.bindings(), targetReference);
          auto returnBinding = resolvedOwnerLocal(bound.bindings(), shape.localReference);
          if (assignmentTypeIndex == zc::none || targetTypeIndex == zc::none ||
              valueTypeIndex == zc::none || literalIndex == zc::none ||
              assignmentSpan == zc::none || valueSpan == zc::none || targetBinding == zc::none ||
              returnBinding == zc::none || targetBinding != returnBinding) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t assignmentSlot = 0;
          size_t targetSlot = 0;
          size_t valueSlot = 0;
          size_t literalSlot = 0;
          ZC_IF_SOME(index, assignmentTypeIndex) { assignmentSlot = index; }
          ZC_IF_SOME(index, targetTypeIndex) { targetSlot = index; }
          ZC_IF_SOME(index, valueTypeIndex) { valueSlot = index; }
          ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
          const auto& assignmentType = facts.nodeTypes().entries()[assignmentSlot].value;
          const auto& targetType = facts.nodeTypes().entries()[targetSlot].value;
          const auto& valueType = facts.nodeTypes().entries()[valueSlot].value;
          const auto& sourceLiteral = facts.literals().entries()[literalSlot].value;
          identity::SemanticTypeId writeType = targetType;
          zc::Maybe<identity::DefId> field;
          if (tree.node(target).kind == ast::SyntaxKind::MemberExpression) {
            auto memberIndex = factIndex(facts.members(), target);
            auto placeIndex = factIndex(facts.places(), target);
            if (memberIndex == zc::none || placeIndex == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t memberSlot = 0;
            size_t placeSlot = 0;
            ZC_IF_SOME(index, memberIndex) { memberSlot = index; }
            ZC_IF_SOME(index, placeIndex) { placeSlot = index; }
            const auto& member = facts.members().entries()[memberSlot].value;
            const auto& place = facts.places().entries()[placeSlot].value;
            if (member.node != target || member.memberType != targetType || place.node != target ||
                place.type != targetType || !place.mutablePlace || place.projections.size() != 1 ||
                !place.projections[0].variant().is<checker::checked::FieldProjection>() ||
                place.projections[0].variant().get<checker::checked::FieldProjection>().field !=
                    member.member) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            field = member.member;
          }
          if (local == zc::none || assignmentType != writeType || targetType != writeType ||
              valueType != writeType || sourceLiteral.type != writeType ||
              !sameSpan(sourceLiteral.sourceSpan, ZC_ASSERT_NONNULL(valueSpan))) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          bool firstFieldWrite = false;
          ZC_IF_SOME(currentField, field) {
            firstFieldWrite = true;
            for (const auto& previous : localWrites) {
              ZC_IF_SOME(previousField, previous.field) {
                if (previousField == currentField) {
                  firstFieldWrite = false;
                  break;
                }
              }
            }
          }
          const auto kind =
              shape.localInitializer == zc::none
                  ? (field != zc::none ? (firstFieldWrite ? HirLocalWriteKind::Initialize
                                                          : HirLocalWriteKind::Overwrite)
                                       : (writeIndex == 0 ? HirLocalWriteKind::Initialize
                                                          : HirLocalWriteKind::Overwrite))
                  : HirLocalWriteKind::Overwrite;
          localWrites.add(HirLocalWriteStatement{
              HirNodeId(), HirLocalId(), zc::mv(field), writeType, HirNodeId(), kind,
              ZC_ASSERT_NONNULL(assignmentSpan).clone(), ZC_ASSERT_NONNULL(valueSpan).clone()});
          localWriteLiterals.add(sourceLiteral.literal.clone());
        }
      } else if (isScalarLiteral(tree.node(shape.value).kind)) {
        auto literalIndex = factIndex(facts.literals(), shape.value);
        if (literalIndex == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t literalSlot = 0;
        ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
        const auto& sourceLiteral = facts.literals().entries()[literalSlot].value;
        if (sourceLiteral.type != nodeType.value ||
            !sameSpan(valueSpanValue, sourceLiteral.sourceSpan)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        literal = sourceLiteral.literal.clone();
      } else if (tree.node(shape.value).kind == ast::SyntaxKind::IdentExpr) {
        auto parameter = resolvedCallableParameter(bound.bindings(), shape.value);
        if (parameter == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        ZC_IF_SOME(handle, parameter) {
          auto authority = registries.callableParameter(handle);
          if (authority == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          ZC_IF_SOME(entry, authority) {
            bool matches = false;
            for (const auto& candidate : parameters) {
              if (candidate.key == entry.key() && candidate.type == nodeType.value) {
                matches = true;
              }
            }
            if (!matches) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            parameterReference =
                HirParameterReferenceExpression{HirNodeId(), entry.key().clone(), nodeType.value,
                                                HirValueCategory::Place, valueSpanValue.clone()};
          }
        }
      } else if (tree.node(shape.value).kind == ast::SyntaxKind::IndexExpression) {
        const auto& sourceIndex = tree.node(shape.value);
        const ast::NodeId base(sourceIndex.payload.words[ast::kIndexExpressionObjectWord]);
        const ast::NodeId index(sourceIndex.payload.words[ast::kIndexExpressionIndexWord]);
        auto parameter = resolvedCallableParameter(bound.bindings(), base);
        auto baseType = factIndex(facts.nodeTypes(), base);
        auto indexType = factIndex(facts.nodeTypes(), index);
        auto indexLiteral = factIndex(facts.literals(), index);
        auto callIndex = factIndex(facts.calls(), shape.value);
        auto placeIndex = factIndex(facts.places(), shape.value);
        auto checkedIndex = factIndex(facts.indexes(), shape.value);
        auto marker = factIndex(facts.markerObligations(), shape.value);
        auto indexSpan = bound.parsedModule().spanFor(tree.node(index).range);
        if (!tree.contains(base) || !tree.contains(index) ||
            tree.node(base).kind != ast::SyntaxKind::IdentExpr ||
            tree.node(index).kind != ast::SyntaxKind::IntLiteral || parameter == zc::none ||
            baseType == zc::none || indexType == zc::none || indexLiteral == zc::none ||
            callIndex == zc::none || placeIndex == zc::none || checkedIndex == zc::none ||
            marker == zc::none || indexSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t baseTypeSlot = 0;
        size_t indexTypeSlot = 0;
        size_t literalSlot = 0;
        size_t callSlot = 0;
        size_t placeSlot = 0;
        size_t indexSlot = 0;
        size_t markerSlot = 0;
        ZC_IF_SOME(value, baseType) { baseTypeSlot = value; }
        ZC_IF_SOME(value, indexType) { indexTypeSlot = value; }
        ZC_IF_SOME(value, indexLiteral) { literalSlot = value; }
        ZC_IF_SOME(value, callIndex) { callSlot = value; }
        ZC_IF_SOME(value, placeIndex) { placeSlot = value; }
        ZC_IF_SOME(value, checkedIndex) { indexSlot = value; }
        ZC_IF_SOME(value, marker) { markerSlot = value; }
        auto authority = registries.callableParameter(ZC_ASSERT_NONNULL(parameter));
        if (authority == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        const auto& literalFact = facts.literals().entries()[literalSlot].value;
        const auto& callFact = facts.calls().entries()[callSlot].value;
        const auto& call = callFact.invocation;
        const auto& selected = call.selected.variant();
        const auto& place = facts.places().entries()[placeSlot].value;
        const auto& root = place.root.variant();
        const auto& indexFact = facts.indexes().entries()[indexSlot].value;
        const auto& markerFact = facts.markerObligations().entries()[markerSlot].value;
        ZC_IF_SOME(entry, authority) {
          bool matches = false;
          for (const auto& candidate : parameters) {
            if (candidate.key == entry.key() &&
                candidate.type == facts.nodeTypes().entries()[baseTypeSlot].value) {
              matches = true;
            }
          }
          if (!matches || literalFact.node != index ||
              literalFact.type != facts.nodeTypes().entries()[indexTypeSlot].value ||
              !sameSpan(literalFact.sourceSpan, ZC_ASSERT_NONNULL(indexSpan)) ||
              callFact.node != shape.value || !selected.is<checker::checked::PrimitiveCallable>() ||
              selected.get<checker::checked::PrimitiveCallable>().operation !=
                  checker::PrimitiveOperation::Index ||
              call.calleeType != facts.nodeTypes().entries()[baseTypeSlot].value ||
              call.receiver == zc::none || call.receiverMode == zc::none ||
              call.receiverAdjustment == zc::none || call.arguments.size() != 1 ||
              call.successType != nodeType.value || call.resultType != nodeType.value ||
              call.substitutions != zc::none || call.witnesses != zc::none ||
              call.raises != zc::none || !root.is<checker::checked::CallableParameterPlaceRoot>() ||
              root.get<checker::checked::CallableParameterPlaceRoot>().parameter !=
                  ZC_ASSERT_NONNULL(parameter) ||
              place.projections.size() != 1 ||
              !place.projections[0].variant().is<checker::checked::IndexProjection>() ||
              place.projections[0].variant().get<checker::checked::IndexProjection>().index !=
                  index ||
              place.type != nodeType.value || place.mutablePlace || place.movable ||
              indexFact.node != shape.value ||
              indexFact.collectionType != facts.nodeTypes().entries()[baseTypeSlot].value ||
              indexFact.indexType != facts.nodeTypes().entries()[indexTypeSlot].value ||
              indexFact.elementType != nodeType.value ||
              indexFact.accessMode != checker::checked::IndexAccessMode::Read ||
              indexFact.accessResultType != nodeType.value || markerFact.node != shape.value ||
              markerFact.subject != nodeType.value ||
              markerFact.polarity != checker::checked::Polarity::Positive) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          const auto& receiver = ZC_ASSERT_NONNULL(call.receiver);
          const auto& adjustment = ZC_ASSERT_NONNULL(call.receiverAdjustment);
          const auto& argument = call.arguments[0];
          const auto mode = ZC_ASSERT_NONNULL(call.receiverMode);
          if (receiver.sourceNode != base ||
              receiver.sourceType != facts.nodeTypes().entries()[baseTypeSlot].value ||
              receiver.parameterType != receiver.sourceType || receiver.adjustment != zc::none ||
              mode != checker::checked::ReceiverMode::Shared ||
              adjustment.source != receiver.sourceType ||
              adjustment.destination != receiver.parameterType || adjustment.steps.size() != 1 ||
              adjustment.steps[0] != checker::checked::ReceiverAdjustmentStep::BorrowShared ||
              argument.sourceNode != index ||
              argument.sourceType != facts.nodeTypes().entries()[indexTypeSlot].value ||
              argument.parameterType != argument.sourceType || argument.adjustment != zc::none ||
              !sameSpan(callFact.sourceSpan, valueSpanValue)) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          parameterIndex = HirParameterIndexExpression{HirNodeId(),
                                                       entry.key().clone(),
                                                       receiver.sourceType,
                                                       argument.sourceType,
                                                       literalFact.literal.clone(),
                                                       nodeType.value,
                                                       HirValueCategory::Place,
                                                       valueSpanValue.clone(),
                                                       ZC_ASSERT_NONNULL(indexSpan).clone()};
        }
      }
      if (!shape.returnsLocalBorrow &&
          tree.node(shape.value).kind == ast::SyntaxKind::UnaryExpression &&
          (static_cast<ast::UnaryOperatorKind>(
               tree.node(shape.value).payload.words[ast::kUnaryExpressionOpWord]) ==
               ast::UnaryOperatorKind::Ref ||
           static_cast<ast::UnaryOperatorKind>(
               tree.node(shape.value).payload.words[ast::kUnaryExpressionOpWord]) ==
               ast::UnaryOperatorKind::RefMut)) {
        const ast::NodeId dereference(
            tree.node(shape.value).payload.words[ast::kUnaryExpressionOperandWord]);
        if (!tree.contains(dereference) ||
            tree.node(dereference).kind != ast::SyntaxKind::UnaryExpression ||
            static_cast<ast::UnaryOperatorKind>(
                tree.node(dereference).payload.words[ast::kUnaryExpressionOpWord]) !=
                ast::UnaryOperatorKind::Deref) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        const ast::NodeId parameter(
            tree.node(dereference).payload.words[ast::kUnaryExpressionOperandWord]);
        auto parameterTypeIndex = factIndex(facts.nodeTypes(), parameter);
        auto dereferenceTypeIndex = factIndex(facts.nodeTypes(), dereference);
        auto parameterHandle = resolvedCallableParameter(bound.bindings(), parameter);
        zc::Maybe<HirLocalId> sourceAlias;
        if (parameterHandle == zc::none && shape.returnsLocalReborrow) {
          auto alias = resolvedOwnerLocal(bound.bindings(), parameter);
          auto localRoot = resolvedOwnerLocal(bound.bindings(), shape.localReference);
          if (alias == zc::none || localRoot == zc::none || local == zc::none ||
              shape.localInitializer == zc::none ||
              ZC_ASSERT_NONNULL(alias) != ZC_ASSERT_NONNULL(localRoot)) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          ZC_IF_SOME(initializer, shape.localInitializer) {
            parameterHandle = resolvedCallableParameter(bound.bindings(), initializer);
          }
          if (parameterHandle == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          sourceAlias = HirLocalId();
        }
        if (!tree.contains(parameter) || tree.node(parameter).kind != ast::SyntaxKind::IdentExpr ||
            parameterTypeIndex == zc::none || dereferenceTypeIndex == zc::none ||
            parameterHandle == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t parameterTypeSlot = 0;
        size_t dereferenceTypeSlot = 0;
        ZC_IF_SOME(index, parameterTypeIndex) { parameterTypeSlot = index; }
        ZC_IF_SOME(index, dereferenceTypeIndex) { dereferenceTypeSlot = index; }
        const auto sourceType = facts.nodeTypes().entries()[parameterTypeSlot].value;
        const auto referentType = facts.nodeTypes().entries()[dereferenceTypeSlot].value;
        const auto operation = static_cast<ast::UnaryOperatorKind>(
            tree.node(shape.value).payload.words[ast::kUnaryExpressionOpWord]);
        const auto expectedMutability = operation == ast::UnaryOperatorKind::Ref
                                            ? type::semantic::Mutability::Const
                                            : type::semantic::Mutability::Mutable;
        auto typeLookup = checkedModule.semanticTypes().get(sourceType);
        if (!typeLookup.is<type::SemanticTypeLookup>() ||
            !typeLookup.get<type::SemanticTypeLookup>()
                 .data()
                 .is<type::semantic::ReferenceTypeData>() ||
            typeLookup.get<type::SemanticTypeLookup>()
                    .data()
                    .get<type::semantic::ReferenceTypeData>()
                    .mutability != expectedMutability ||
            typeLookup.get<type::SemanticTypeLookup>()
                    .data()
                    .get<type::semantic::ReferenceTypeData>()
                    .referent != referentType ||
            sourceType != nodeType.value) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        ZC_IF_SOME(handle, parameterHandle) {
          auto authority = registries.callableParameter(handle);
          if (authority == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          ZC_IF_SOME(entry, authority) {
            bool matches = false;
            for (const auto& candidate : parameters) {
              if (candidate.key == entry.key() && candidate.type == sourceType) { matches = true; }
            }
            if (!matches) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            if (sourceAlias != zc::none) {
              ast::NodeId initializer;
              ZC_IF_SOME(value, shape.localInitializer) { initializer = value; }
              auto initializerTypeIndex = factIndex(facts.nodeTypes(), initializer);
              auto initializerSpan = bound.parsedModule().spanFor(tree.node(initializer).range);
              if (initializerTypeIndex == zc::none || initializerSpan == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              size_t initializerTypeSlot = 0;
              ZC_IF_SOME(index, initializerTypeIndex) { initializerTypeSlot = index; }
              if (facts.nodeTypes().entries()[initializerTypeSlot].value != sourceType) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::InvalidFact, module,
                                                     registries, ordinal + 2);
              }
              parameterReference = HirParameterReferenceExpression{
                  HirNodeId(), entry.key().clone(), sourceType, HirValueCategory::Place,
                  ZC_ASSERT_NONNULL(initializerSpan).clone()};
            }
            parameterReborrow = HirParameterReborrowExpression{
                HirNodeId(),    entry.key().clone(), zc::mv(sourceAlias),   sourceType,
                nodeType.value, expectedMutability,  valueSpanValue.clone()};
          }
        }
      }
      if (shape.returnsLocalBorrow) {
        auto sourceTypeIndex = factIndex(facts.nodeTypes(), shape.localReference);
        if (sourceTypeIndex == zc::none || local == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t sourceTypeSlot = 0;
        ZC_IF_SOME(index, sourceTypeIndex) { sourceTypeSlot = index; }
        const auto sourceType = facts.nodeTypes().entries()[sourceTypeSlot].value;
        const auto operation = static_cast<ast::UnaryOperatorKind>(
            tree.node(shape.value).payload.words[ast::kUnaryExpressionOpWord]);
        const auto expectedMutability = operation == ast::UnaryOperatorKind::Ref
                                            ? type::semantic::Mutability::Const
                                            : type::semantic::Mutability::Mutable;
        auto borrowTypeLookup = checkedModule.semanticTypes().get(nodeType.value);
        if (!borrowTypeLookup.is<type::SemanticTypeLookup>() ||
            !borrowTypeLookup.get<type::SemanticTypeLookup>()
                 .data()
                 .is<type::semantic::ReferenceTypeData>() ||
            borrowTypeLookup.get<type::SemanticTypeLookup>()
                    .data()
                    .get<type::semantic::ReferenceTypeData>()
                    .mutability != expectedMutability ||
            borrowTypeLookup.get<type::SemanticTypeLookup>()
                    .data()
                    .get<type::semantic::ReferenceTypeData>()
                    .referent != sourceType ||
            ZC_ASSERT_NONNULL(local).type != sourceType) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        localBorrow =
            HirLocalBorrowExpression{HirNodeId(),    HirLocalId(),       sourceType,
                                     nodeType.value, expectedMutability, valueSpanValue.clone()};
      }
      ast::NodeId callNode = shape.value;
      if (!shape.returnsReceiverCall) {
        ZC_IF_SOME(initializer, shape.localInitializer) { callNode = initializer; }
      }
      if (!tree.contains(callNode)) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::MissingRequiredFact, module,
                                             registries, ordinal + 2);
      }
      if (tree.node(callNode).kind == ast::SyntaxKind::CallExpression) {
        const auto& sourceCall = tree.node(callNode);
        auto callSpan = bound.parsedModule().spanFor(sourceCall.range);
        const ast::NodeId calleeNode(sourceCall.payload.words[ast::kCallExpressionCalleeWord]);
        const ast::NodeList typeArguments{
            sourceCall.payload.words[ast::kCallExpressionTypeArgsFirstWord],
            sourceCall.payload.words[ast::kCallExpressionTypeArgsSizeWord]};
        const ast::NodeList arguments{sourceCall.payload.words[ast::kCallExpressionArgsFirstWord],
                                      sourceCall.payload.words[ast::kCallExpressionArgsSizeWord]};
        auto calleeTypeIndex = factIndex(facts.nodeTypes(), calleeNode);
        auto checkedCallIndex = factIndex(facts.calls(), callNode);
        auto callKey = checkedNodeKey(tree, bound.parsedModule(), callNode);
        if (shape.returnsReceiverCall) {
          const ast::NodeId receiverNode(
              tree.node(calleeNode).payload.words[ast::kMemberExpressionObjectWord]);
          auto receiverTypeIndex = factIndex(facts.nodeTypes(), receiverNode);
          auto memberIndex = factIndex(facts.members(), calleeNode);
          auto receiverBinding = resolvedOwnerLocal(bound.bindings(), receiverNode);
          auto dispatchIndex =
              dispatchFactIndex(checkedModule.dispatchFacts().facts(), ZC_ASSERT_NONNULL(callKey));
          if (!tree.contains(calleeNode) ||
              tree.node(calleeNode).kind != ast::SyntaxKind::MemberExpression ||
              static_cast<ast::MemberAccessKind>(
                  tree.node(calleeNode).payload.words[ast::kMemberExpressionAccessWord]) !=
                  ast::MemberAccessKind::Dot ||
              !tree.contains(receiverNode) ||
              tree.node(receiverNode).kind != ast::SyntaxKind::IdentExpr ||
              receiverTypeIndex == zc::none || memberIndex == zc::none ||
              receiverBinding == zc::none || calleeTypeIndex == zc::none ||
              checkedCallIndex == zc::none || callKey == zc::none || dispatchIndex == zc::none ||
              callSpan == zc::none || local == zc::none ||
              ZC_ASSERT_NONNULL(local).type !=
                  facts.nodeTypes().entries()[ZC_ASSERT_NONNULL(receiverTypeIndex)].value) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t receiverTypeSlot = 0;
          size_t memberSlot = 0;
          size_t calleeTypeSlot = 0;
          size_t checkedCallSlot = 0;
          size_t dispatchSlot = 0;
          ZC_IF_SOME(index, receiverTypeIndex) { receiverTypeSlot = index; }
          ZC_IF_SOME(index, memberIndex) { memberSlot = index; }
          ZC_IF_SOME(index, calleeTypeIndex) { calleeTypeSlot = index; }
          ZC_IF_SOME(index, checkedCallIndex) { checkedCallSlot = index; }
          ZC_IF_SOME(index, dispatchIndex) { dispatchSlot = index; }
          const auto& member = facts.members().entries()[memberSlot].value;
          const auto& invocation = facts.calls().entries()[checkedCallSlot].value.invocation;
          const auto& selected = invocation.selected.variant();
          const auto& dispatch = checkedModule.dispatchFacts().facts()[dispatchSlot];
          const auto& target = dispatch.fact.target.variant();
          const auto& transform = dispatch.fact.resultTransform.variant();
          bool dispatchOwnerMatches = false;
          ZC_IF_SOME(owner, dispatch.owner) {
            dispatchOwnerMatches = owner == definition.definition;
          }
          if (!selected.is<checker::checked::ConcreteMethodCallable>() ||
              !target.is<checker::dispatch::ConcreteMethodTarget>() ||
              !transform.is<checker::dispatch::IdentityResultTransform>() ||
              selected.get<checker::checked::ConcreteMethodCallable>().method != member.member ||
              target.get<checker::dispatch::ConcreteMethodTarget>().method != member.member ||
              member.node != calleeNode ||
              member.receiverType != facts.nodeTypes().entries()[receiverTypeSlot].value ||
              member.memberType != facts.nodeTypes().entries()[calleeTypeSlot].value ||
              member.adjustment != zc::none || invocation.calleeType != member.memberType ||
              invocation.successType != nodeType.value || invocation.resultType != nodeType.value ||
              invocation.receiver == zc::none || invocation.receiverMode == zc::none ||
              invocation.receiverAdjustment == zc::none ||
              invocation.arguments.size() != arguments.size ||
              invocation.substitutions != zc::none || invocation.witnesses != zc::none ||
              invocation.raises != zc::none || !dispatchOwnerMatches ||
              dispatch.fact.receiver == zc::none ||
              dispatch.fact.arguments.size() != arguments.size ||
              dispatch.fact.successType != nodeType.value ||
              dispatch.fact.resultType != nodeType.value ||
              dispatch.fact.substitutions != zc::none || dispatch.fact.witnesses != zc::none ||
              dispatch.fact.raises != zc::none ||
              !sameSpan(facts.calls().entries()[checkedCallSlot].value.sourceSpan,
                        ZC_ASSERT_NONNULL(callSpan)) ||
              !sameSpan(dispatch.fact.sourceSpan, ZC_ASSERT_NONNULL(callSpan))) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          ZC_IF_SOME(receiver, invocation.receiver) {
            ZC_IF_SOME(mode, invocation.receiverMode) {
              ZC_IF_SOME(adjustment, invocation.receiverAdjustment) {
                auto receiverParameter = checkedModule.semanticTypes().get(receiver.parameterType);
                if (!receiverParameter.is<type::SemanticTypeLookup>() ||
                    !receiverParameter.get<type::SemanticTypeLookup>()
                         .data()
                         .is<type::semantic::ReferenceTypeData>() ||
                    receiverParameter.get<type::SemanticTypeLookup>()
                            .data()
                            .get<type::semantic::ReferenceTypeData>()
                            .mutability != type::semantic::Mutability::Mutable ||
                    receiverParameter.get<type::SemanticTypeLookup>()
                            .data()
                            .get<type::semantic::ReferenceTypeData>()
                            .referent != receiver.sourceType) {
                  return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                       ir::IrFailureKind::InvalidFact, module,
                                                       registries, ordinal + 2);
                }
                if (receiver.sourceNode != receiverNode ||
                    receiver.sourceType != facts.nodeTypes().entries()[receiverTypeSlot].value ||
                    receiver.adjustment != zc::none ||
                    mode != checker::checked::ReceiverMode::Mutable ||
                    adjustment.source != receiver.sourceType ||
                    adjustment.destination != receiver.parameterType ||
                    adjustment.steps.size() != 1 ||
                    adjustment.steps[0] !=
                        checker::checked::ReceiverAdjustmentStep::BorrowMutable) {
                  return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                       ir::IrFailureKind::InvalidFact, module,
                                                       registries, ordinal + 2);
                }
                zc::Vector<checker::checked::ReceiverAdjustmentStep> steps;
                steps.add(checker::checked::ReceiverAdjustmentStep::BorrowMutable);
                zc::Vector<HirDirectCallArgument> callArguments;
                const auto argumentNodes = tree.list(arguments);
                for (size_t index = 0; index < argumentNodes.size(); ++index) {
                  const auto argument = argumentNodes[index];
                  auto argumentTypeIndex = factIndex(facts.nodeTypes(), argument);
                  auto literalIndex = factIndex(facts.literals(), argument);
                  auto argumentSpan = bound.parsedModule().spanFor(tree.node(argument).range);
                  if (!tree.contains(argument) || !isScalarLiteral(tree.node(argument).kind) ||
                      argumentTypeIndex == zc::none || literalIndex == zc::none ||
                      argumentSpan == zc::none) {
                    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                         ir::IrFailureKind::MissingRequiredFact,
                                                         module, registries, ordinal + 2);
                  }
                  size_t argumentTypeSlot = 0;
                  size_t literalSlot = 0;
                  ZC_IF_SOME(value, argumentTypeIndex) { argumentTypeSlot = value; }
                  ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
                  const auto argumentType = facts.nodeTypes().entries()[argumentTypeSlot].value;
                  const auto& checkedArgument = invocation.arguments[index];
                  const auto& literal = facts.literals().entries()[literalSlot].value;
                  if (checkedArgument.sourceNode != argument ||
                      checkedArgument.sourceType != argumentType ||
                      checkedArgument.parameterType != argumentType ||
                      checkedArgument.adjustment != zc::none || literal.node != argument ||
                      literal.type != argumentType ||
                      !sameSpan(literal.sourceSpan, ZC_ASSERT_NONNULL(argumentSpan))) {
                    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                         ir::IrFailureKind::InvalidFact, module,
                                                         registries, ordinal + 2);
                  }
                  callArguments.add(HirDirectCallArgument{argumentType, literal.literal.clone(),
                                                          ZC_ASSERT_NONNULL(argumentSpan).clone()});
                }
                receiverCall = HirReceiverCallExpression{HirNodeId(),
                                                         HirNodeId(),
                                                         member.member,
                                                         invocation.calleeType,
                                                         receiver.sourceType,
                                                         receiver.parameterType,
                                                         mode,
                                                         zc::mv(steps),
                                                         invocation.resultType,
                                                         zc::mv(callArguments),
                                                         ZC_ASSERT_NONNULL(callSpan).clone()};
              }
            }
          }
          if (receiverCall == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
        } else {
          if (!tree.contains(calleeNode) ||
              tree.node(calleeNode).kind != ast::SyntaxKind::IdentExpr ||
              !tree.contains(typeArguments) || !tree.contains(arguments) ||
              !typeArguments.empty() || calleeTypeIndex == zc::none ||
              checkedCallIndex == zc::none || callKey == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          auto dispatchIndex =
              dispatchFactIndex(checkedModule.dispatchFacts().facts(), ZC_ASSERT_NONNULL(callKey));
          auto callee = resolvedDefinition(bound.bindings(), calleeNode);
          if (dispatchIndex == zc::none || callee == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t calleeTypeSlot = 0;
          size_t checkedCallSlot = 0;
          size_t dispatchSlot = 0;
          ZC_IF_SOME(index, calleeTypeIndex) { calleeTypeSlot = index; }
          ZC_IF_SOME(index, checkedCallIndex) { checkedCallSlot = index; }
          ZC_IF_SOME(index, dispatchIndex) { dispatchSlot = index; }
          const auto& checkedCall = facts.calls().entries()[checkedCallSlot].value;
          const auto& invocation = checkedCall.invocation;
          const auto& selected = invocation.selected.variant();
          const auto& dispatch = checkedModule.dispatchFacts().facts()[dispatchSlot];
          const auto& target = dispatch.fact.target.variant();
          const auto& transform = dispatch.fact.resultTransform.variant();
          bool dispatchOwnerMatches = false;
          ZC_IF_SOME(owner, dispatch.owner) {
            dispatchOwnerMatches = owner == definition.definition;
          }
          if (!selected.is<checker::checked::DirectCallable>() ||
              selected.get<checker::checked::DirectCallable>().callee !=
                  ZC_ASSERT_NONNULL(callee) ||
              invocation.calleeType != facts.nodeTypes().entries()[calleeTypeSlot].value ||
              invocation.successType != nodeType.value || invocation.resultType != nodeType.value ||
              invocation.receiver != zc::none || invocation.receiverMode != zc::none ||
              invocation.receiverAdjustment != zc::none ||
              invocation.arguments.size() != arguments.size ||
              invocation.substitutions != zc::none || invocation.witnesses != zc::none ||
              invocation.raises != zc::none || callSpan == zc::none ||
              !sameSpan(checkedCall.sourceSpan, ZC_ASSERT_NONNULL(callSpan)) ||
              !dispatchOwnerMatches || !target.is<checker::dispatch::DirectTarget>() ||
              target.get<checker::dispatch::DirectTarget>().callee != ZC_ASSERT_NONNULL(callee) ||
              !transform.is<checker::dispatch::IdentityResultTransform>() ||
              dispatch.fact.receiver != zc::none ||
              dispatch.fact.arguments.size() != arguments.size ||
              dispatch.fact.successType != nodeType.value ||
              dispatch.fact.resultType != nodeType.value ||
              dispatch.fact.substitutions != zc::none || dispatch.fact.witnesses != zc::none ||
              dispatch.fact.raises != zc::none ||
              !sameSpan(dispatch.fact.sourceSpan, ZC_ASSERT_NONNULL(callSpan)) ||
              !typeExists(invocation.calleeType, checkedModule.semanticTypes())) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          zc::Vector<HirDirectCallArgument> callArguments;
          const auto argumentNodes = tree.list(arguments);
          for (size_t index = 0; index < argumentNodes.size(); ++index) {
            const auto argument = argumentNodes[index];
            auto argumentTypeIndex = factIndex(facts.nodeTypes(), argument);
            auto literalIndex = factIndex(facts.literals(), argument);
            auto argumentKey = checkedNodeKey(tree, bound.parsedModule(), argument);
            auto argumentSpan = bound.parsedModule().spanFor(tree.node(argument).range);
            if (!tree.contains(argument) || !isScalarLiteral(tree.node(argument).kind) ||
                argumentTypeIndex == zc::none || literalIndex == zc::none ||
                argumentKey == zc::none || argumentSpan == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t argumentTypeSlot = 0;
            size_t literalSlot = 0;
            ZC_IF_SOME(value, argumentTypeIndex) { argumentTypeSlot = value; }
            ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
            const auto& checkedArgument = invocation.arguments[index];
            const auto& dispatchArgument = dispatch.fact.arguments[index];
            const auto& literal = facts.literals().entries()[literalSlot].value;
            const auto argumentType = facts.nodeTypes().entries()[argumentTypeSlot].value;
            if (checkedArgument.sourceNode != argument ||
                checkedArgument.sourceType != argumentType ||
                checkedArgument.parameterType != argumentType ||
                checkedArgument.adjustment != zc::none ||
                !sameNodeKey(dispatchArgument.sourceNode, ZC_ASSERT_NONNULL(argumentKey)) ||
                dispatchArgument.sourceType != argumentType ||
                dispatchArgument.parameterType != argumentType ||
                dispatchArgument.adjustment != zc::none || literal.node != argument ||
                literal.type != argumentType ||
                !sameSpan(literal.sourceSpan, ZC_ASSERT_NONNULL(argumentSpan))) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            callArguments.add(HirDirectCallArgument{argumentType, literal.literal.clone(),
                                                    ZC_ASSERT_NONNULL(argumentSpan).clone()});
          }
          call =
              HirDirectCallExpression{HirNodeId(),           ZC_ASSERT_NONNULL(callee),
                                      invocation.calleeType, invocation.resultType,
                                      zc::mv(callArguments), ZC_ASSERT_NONNULL(callSpan).clone()};
        }
      } else if (literal == zc::none && aggregate == zc::none && parameterReference == zc::none &&
                 parameterIndex == zc::none && shape.localInitializer != zc::none) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::MissingRequiredFact, module,
                                             registries, ordinal + 2);
      }
      pendingFunctions.add(PendingFunctionDeclaration{definition.definition,
                                                      callable.success,
                                                      zc::mv(parameters),
                                                      zc::mv(visibilityValue),
                                                      linkageValue,
                                                      definition.source.clone(),
                                                      bodySpanValue.clone(),
                                                      returnSpanValue.clone(),
                                                      valueSpanValue.clone(),
                                                      zc::mv(literal),
                                                      zc::mv(call),
                                                      zc::mv(receiverCall),
                                                      zc::mv(local),
                                                      zc::mv(aggregate),
                                                      zc::mv(localWrites),
                                                      zc::mv(localWriteLiterals),
                                                      zc::mv(localReference),
                                                      zc::mv(localFieldProjection),
                                                      zc::mv(parameterReference),
                                                      zc::mv(parameterIndex),
                                                      zc::mv(parameterReborrow),
                                                      zc::mv(localBorrow),
                                                      zc::none,
                                                      zc::mv(unsafeBlockSpan),
                                                      zc::mv(orderingKey)});
      continue;
    }
    if (definition.record.kind() != identity::DefinitionKind::Static &&
        definition.record.kind() != identity::DefinitionKind::Constant) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::MissingRequiredFact, module,
                                           registries, ordinal + 2);
    }
    auto patternSite = patternBindingSite(definitionInventory, definition);
    if (patternSite == zc::none) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::InvalidFact, module, registries,
                                           ordinal + 2);
    }
    const auto& bindingSite = ZC_ASSERT_NONNULL(patternSite);
    const auto& tree = bound.tree();
    if (bindingSite.patternPath.size() != 0 || !tree.contains(bindingSite.introducer) ||
        tree.node(bindingSite.introducer).kind != ast::SyntaxKind::VariableDeclarator) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::InvalidFact, module, registries,
                                           ordinal + 2);
    }
    const auto& declarator = tree.node(bindingSite.introducer);
    const ast::NodeId patternNode(declarator.payload.words[ast::kVariableDeclaratorPatternWord]);
    const ast::NodeId initializer(declarator.payload.words[ast::kVariableDeclaratorInitWord]);
    if (!tree.contains(patternNode) ||
        tree.node(patternNode).kind != ast::SyntaxKind::IdentifierPattern ||
        !tree.contains(initializer) || !isScalarLiteral(tree.node(initializer).kind)) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::MissingRequiredFact, module,
                                           registries, ordinal + 2);
    }

    auto definitionTypeIndex = factIndex(facts.definitionTypes(), definition.definition);
    auto patternIndex = factIndex(facts.patterns(), patternNode);
    auto nodeTypeIndex = factIndex(facts.nodeTypes(), initializer);
    auto literalIndex = factIndex(facts.literals(), initializer);
    auto signaturePosition = signatureIndex(signatures.definitions.asPtr(), definition.definition);
    auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), definition.definition);
    auto declarationSpan = bound.parsedModule().spanFor(declarator.range);
    auto initializerSpan = bound.parsedModule().spanFor(tree.node(initializer).range);
    if (definitionTypeIndex == zc::none || patternIndex == zc::none || nodeTypeIndex == zc::none ||
        literalIndex == zc::none || signaturePosition == zc::none || rootPosition == zc::none ||
        declarationSpan == zc::none || initializerSpan == zc::none) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::MissingRequiredFact, module,
                                           registries, ordinal + 2);
    }

    size_t definitionTypeSlot = 0;
    size_t patternSlot = 0;
    size_t nodeTypeSlot = 0;
    size_t literalSlot = 0;
    size_t signatureSlot = 0;
    size_t rootSlot = 0;
    ZC_IF_SOME(value, definitionTypeIndex) { definitionTypeSlot = value; }
    ZC_IF_SOME(value, patternIndex) { patternSlot = value; }
    ZC_IF_SOME(value, nodeTypeIndex) { nodeTypeSlot = value; }
    ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
    ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
    ZC_IF_SOME(value, rootPosition) { rootSlot = value; }
    const auto& definitionType = facts.definitionTypes().entries()[definitionTypeSlot];
    const auto& pattern = facts.patterns().entries()[patternSlot].value;
    const auto& nodeType = facts.nodeTypes().entries()[nodeTypeSlot];
    const auto& literal = facts.literals().entries()[literalSlot].value;
    const auto& signature = signatures.definitions[signatureSlot];
    const auto& root = signatures.roots[rootSlot];
    if (!signature.payload.variant().is<checker::signature::ValueSignature>() ||
        !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>()) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::InvalidFact, module, registries,
                                           ordinal + 2);
    }
    const auto& valueSignature =
        signature.payload.variant().get<checker::signature::ValueSignature>();
    auto declarationLinkage = linkage(valueSignature);
    auto declarationVisibility = visibility(root.visibility);
    if (declarationLinkage == zc::none || declarationVisibility == zc::none ||
        signature.definitionKind != definition.record.kind() || root.sourceModule != module ||
        root.canonicalDefinition != definition.definition || !valueSignature.hasInitializer ||
        valueSignature.type != definitionType.value || definitionType.value != nodeType.value ||
        literal.type != nodeType.value || pattern.scrutineeType != definitionType.value ||
        pattern.bindings.size() != 1 || pattern.bindings[0].binding != definition.definition ||
        pattern.bindings[0].type != definitionType.value || pattern.refinements.size() != 0 ||
        !pattern.constructor.variant().is<checker::checked::WildcardPattern>() ||
        !pattern.reachable || pattern.guardMayRaise != zc::none ||
        !sameSpan(signature.declarationSpan, definition.source)) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::InvalidFact, module, registries,
                                           ordinal + 2);
    }

    auto constantIndex = factIndex(facts.constants(), definition.definition);
    zc::Maybe<checker::checked::CanonicalConstValue> constant;
    if (definition.record.kind() == identity::DefinitionKind::Constant) {
      if (constantIndex == zc::none || valueSignature.constantValue == zc::none) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::MissingRequiredFact, module,
                                             registries, ordinal + 2);
      }
      size_t constantSlot = 0;
      ZC_IF_SOME(value, constantIndex) { constantSlot = value; }
      const auto& evaluated = facts.constants().entries()[constantSlot].value;
      bool signatureMatches = false;
      ZC_IF_SOME(signatureValue, valueSignature.constantValue) {
        signatureMatches = sameConstant(signatureValue, evaluated.value, module, registries,
                                        checkedModule.semanticTypes());
      }
      if (evaluated.expression != initializer || evaluated.type != definitionType.value ||
          evaluated.dependencies.size() != 0 ||
          !sameConstant(evaluated.value, literal.literal, module, registries,
                        checkedModule.semanticTypes()) ||
          !signatureMatches) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries,
                                             ordinal + 2);
      }
      constant = evaluated.value.clone();
    } else if (constantIndex != zc::none || valueSignature.constantValue != zc::none) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::AdditionalFact, module, registries,
                                           ordinal + 2);
    }

    if (!typeExists(definitionType.value, checkedModule.semanticTypes())) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::InvalidFact, module, registries,
                                           ordinal + 2);
    }
    zc::Array<uint8_t> orderingKey;
    orderingKey = definition.key.encode();
    HirVisibility visibilityValue = HirVisibility::external();
    HirLinkage linkageValue = HirLinkage::Internal;
    identity::SourceSpan declarationSpanValue = definition.source.clone();
    identity::SourceSpan initializerSpanValue = literal.sourceSpan.clone();
    ZC_IF_SOME(value, declarationVisibility) { visibilityValue = zc::mv(value); }
    ZC_IF_SOME(value, declarationLinkage) { linkageValue = value; }
    ZC_IF_SOME(value, declarationSpan) { declarationSpanValue = value.clone(); }
    ZC_IF_SOME(value, initializerSpan) { initializerSpanValue = value.clone(); }
    if (!sameSpan(literal.sourceSpan, initializerSpanValue)) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::InvalidFact, module, registries,
                                           ordinal + 2);
    }
    pending.add(PendingValueDeclaration{
        definition.definition, definition.record.kind(), valueSignature.type, definitionType.value,
        valueSignature.mutability, zc::mv(visibilityValue), linkageValue,
        declarationSpanValue.clone(), definition.source.clone(), literal.sourceSpan.clone(),
        literal.literal.clone(), zc::mv(constant), zc::mv(orderingKey)});
  }

  size_t directCallCount = 0;
  size_t directCallArgumentCount = 0;
  size_t receiverCallCount = 0;
  size_t receiverCallArgumentCount = 0;
  size_t localReturnCount = 0;
  size_t uninitializedLocalReturnCount = 0;
  size_t localWriteCount = 0;
  size_t parameterReferenceCount = 0;
  size_t parameterIndexCount = 0;
  size_t parameterReborrowCount = 0;
  size_t localAliasReborrowCount = 0;
  size_t localBorrowCount = 0;
  size_t aggregateCount = 0;
  size_t aggregateElementCount = 0;
  size_t localFieldProjectionCount = 0;
  size_t localFieldWriteCount = 0;
  size_t unsafeBlockCount = 0;
  for (const auto& function : pendingFunctions) {
    const bool hasSequentialLocalReturn = function.sequentialLocalReturn != zc::none;
    if (hasSequentialLocalReturn) {
      ++localReturnCount;
      ++localReturnCount;
      ZC_IF_SOME(sequential, function.sequentialLocalReturn) {
        ZC_IF_SOME(aggregate, sequential.aggregate) {
          ++aggregateCount;
          aggregateElementCount += aggregate.elements.size();
        }
      }
      continue;
    }
    bool missingInitializer = false;
    ZC_IF_SOME(local, function.local) { missingInitializer = local.initializer == zc::none; }
    const bool uninitializedLocal = missingInitializer && function.localWrites.size() == 0;
    const bool hasParameterReference = function.parameterReference != zc::none;
    const bool hasParameterIndex = function.parameterIndex != zc::none;
    const bool hasParameterReborrow = function.parameterReborrow != zc::none;
    const bool hasLocalBorrow = function.localBorrow != zc::none;
    bool localAliasReborrow = false;
    ZC_IF_SOME(reborrow, function.parameterReborrow) {
      localAliasReborrow = function.local != zc::none && reborrow.sourceAlias != zc::none;
    }
    if (function.literal == zc::none && function.call == zc::none &&
        function.receiverCall == zc::none && function.aggregate == zc::none &&
        !uninitializedLocal && !hasParameterReference && !hasParameterIndex &&
        !hasParameterReborrow && !hasLocalBorrow && function.localWrites.size() == 0) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::MissingRequiredFact, module,
                                           registries, 1);
    }
    if ((function.literal != zc::none && function.call != zc::none) ||
        (function.literal != zc::none && function.aggregate != zc::none) ||
        (function.call != zc::none && function.aggregate != zc::none) ||
        (hasParameterReference &&
         (function.literal != zc::none || function.call != zc::none ||
          function.aggregate != zc::none || (hasParameterReborrow && !localAliasReborrow))) ||
        (hasParameterIndex &&
         (function.literal != zc::none || function.call != zc::none ||
          function.receiverCall != zc::none || function.aggregate != zc::none ||
          function.local != zc::none || function.localReference != zc::none ||
          hasParameterReference || hasParameterReborrow)) ||
        (hasParameterReborrow && (function.literal != zc::none || function.call != zc::none ||
                                  function.aggregate != zc::none)) ||
        (hasLocalBorrow && (function.call != zc::none || function.receiverCall != zc::none ||
                            function.aggregate != zc::none || hasParameterReference ||
                            hasParameterIndex || hasParameterReborrow))) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::AdditionalFact, module, registries,
                                           1);
    }
    ZC_IF_SOME(call, function.call) {
      ++directCallCount;
      directCallArgumentCount += call.arguments.size();
    }
    ZC_IF_SOME(call, function.receiverCall) {
      ++receiverCallCount;
      receiverCallArgumentCount += call.arguments.size();
      if (function.local == zc::none || function.localReference == zc::none ||
          function.call != zc::none || function.literal != zc::none ||
          function.aggregate == zc::none ||
          call.receiverMode != checker::checked::ReceiverMode::Mutable ||
          call.receiverAdjustments.size() != 1 ||
          call.receiverAdjustments[0] != checker::checked::ReceiverAdjustmentStep::BorrowMutable) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries, 1);
      }
    }
    ZC_IF_SOME(aggregate, function.aggregate) {
      ++aggregateCount;
      aggregateElementCount += aggregate.elements.size();
    }
    if (function.localFieldProjection != zc::none) ++localFieldProjectionCount;
    if (hasParameterReference) ++parameterReferenceCount;
    if (hasParameterIndex) ++parameterIndexCount;
    ZC_IF_SOME(reborrow, function.parameterReborrow) {
      ++parameterReborrowCount;
      if (reborrow.sourceAlias != zc::none) ++localAliasReborrowCount;
    }
    if (hasLocalBorrow) ++localBorrowCount;
    if (function.unsafeBlockSpan != zc::none) ++unsafeBlockCount;
    if ((function.local == zc::none) != (function.localReference == zc::none) &&
        function.localFieldProjection == zc::none && !localAliasReborrow && !hasLocalBorrow) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::AdditionalFact, module, registries,
                                           1);
    }
    if (function.localFieldProjection != zc::none &&
        (function.local == zc::none || function.localReference != zc::none)) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::AdditionalFact, module, registries,
                                           1);
    }
    if (function.local != zc::none) {
      ++localReturnCount;
      if (missingInitializer) ++uninitializedLocalReturnCount;
    }
    if (function.localWrites.size() != 0) {
      if (function.local == zc::none ||
          function.localWrites.size() != function.localWriteLiterals.size()) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::MissingRequiredFact, module,
                                             registries, 1);
      }
      localWriteCount += function.localWrites.size();
      for (const auto& write : function.localWrites) {
        if (write.field != zc::none) ++localFieldWriteCount;
      }
    } else if (function.localWriteLiterals.size() != 0) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::AdditionalFact, module, registries,
                                           1);
    }
  }
  if (facts.nodeTypes().size() !=
      pending.size() + pendingFunctions.size() + directCallCount + receiverCallCount * 2 +
          localReturnCount - uninitializedLocalReturnCount + localWriteCount * 3 +
          aggregateElementCount + localFieldProjectionCount + localFieldWriteCount +
          parameterIndexCount * 2 + parameterReborrowCount * 2 + directCallArgumentCount +
          receiverCallArgumentCount + localBorrowCount + unsafeBlockCount) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::AdditionalFact, module, registries, 1);
  }
  if (facts.definitionTypes().size() != pending.size()) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::AdditionalFact, module, registries, 2);
  }
  if (facts.literals().size() !=
      pending.size() + pendingFunctions.size() - directCallCount - aggregateCount -
          uninitializedLocalReturnCount - parameterReferenceCount - parameterReborrowCount +
          localAliasReborrowCount + localWriteCount + aggregateElementCount +
          directCallArgumentCount + receiverCallArgumentCount) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::AdditionalFact, module, registries, 3);
  }
  if (facts.calls().size() != directCallCount + receiverCallCount + parameterIndexCount ||
      checkedModule.dispatchFacts().facts().size() !=
          directCallCount + receiverCallCount + parameterIndexCount) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::AdditionalFact, module, registries, 4);
  }
  if (facts.patterns().size() != pending.size()) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::AdditionalFact, module, registries, 5);
  }
  if (facts.aggregates().size() != aggregateCount) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::AdditionalFact, module, registries, 6);
  }
  if (facts.members().size() !=
      localFieldProjectionCount + localFieldWriteCount + receiverCallCount) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::AdditionalFact, module, registries, 7);
  }
  if (facts.places().size() !=
          localFieldProjectionCount + localFieldWriteCount + parameterIndexCount ||
      facts.indexes().size() != parameterIndexCount ||
      facts.markerObligations().size() != parameterIndexCount) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::AdditionalFact, module, registries, 8);
  }
  size_t expectedConstants = 0;
  for (const auto& value : pending) {
    if (value.definitionKind == identity::DefinitionKind::Constant) ++expectedConstants;
  }
  if (facts.constants().size() != expectedConstants) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::AdditionalFact, module, registries, 2);
  }

  sortPendingDeclarations(pending);
  sortPendingFunctions(pendingFunctions);
  zc::Vector<HirValueDeclaration> declarations;
  zc::Vector<HirFunctionDeclaration> functions;
  zc::Vector<HirBlockStatement> blocks;
  zc::Vector<HirReturnStatement> returns;
  zc::Vector<HirBindingPattern> patterns;
  zc::Vector<HirScalarLiteralExpression> expressions;
  zc::Vector<HirNominalAggregateExpression> aggregates;
  zc::Vector<HirLocalBinding> locals;
  zc::Vector<HirLocalWriteStatement> localWrites;
  zc::Vector<HirLocalReferenceExpression> localReferences;
  zc::Vector<HirLocalFieldProjectionExpression> localFieldProjections;
  zc::Vector<HirParameterReferenceExpression> parameterReferences;
  zc::Vector<HirParameterIndexExpression> parameterIndexes;
  zc::Vector<HirParameterReborrowExpression> parameterReborrows;
  zc::Vector<HirLocalBorrowExpression> localBorrows;
  zc::Vector<HirDirectCallExpression> calls;
  zc::Vector<HirReceiverCallExpression> receiverCalls;
  zc::Vector<HirUnsafeBlockExpression> unsafeBlocks;
  uint32_t next = 1;
  for (auto& value : pending) {
    const auto declarationId = hirId(next++);
    const auto patternId = hirId(next++);
    const auto initializerId = hirId(next++);
    zc::Maybe<checker::checked::CanonicalConstValue> constant;
    ZC_IF_SOME(constantValue, value.constant) { constant = constantValue.clone(); }
    declarations.add(HirValueDeclaration{
        declarationId, value.definition, value.definitionKind, value.declaredType,
        value.inferredType, value.mutability, value.visibility.clone(), value.linkage,
        value.declarationSpan.clone(), patternId, initializerId, zc::mv(constant)});
    patterns.add(HirBindingPattern{patternId, value.definition, value.inferredType,
                                   value.inferredType, true, value.patternSpan.clone()});
    expressions.add(HirScalarLiteralExpression{initializerId, value.inferredType,
                                               value.literal.clone(), HirValueCategory::Value,
                                               value.initializerSpan.clone()});
  }
  for (auto& value : pendingFunctions) {
    const auto functionId = hirId(next++);
    const auto bodyId = hirId(next++);
    ZC_IF_SOME(sequential, value.sequentialLocalReturn) {
      const auto sourceLocalId = hirId(next++);
      const auto sourceInitializerId = hirId(next++);
      const auto destinationLocalId = hirId(next++);
      const auto destinationInitializerId = hirId(next++);
      const auto returnId = hirId(next++);
      const auto returnValueId = hirId(next++);
      functions.add(HirFunctionDeclaration{functionId, value.definition, value.resultType,
                                           zc::mv(value.parameters), value.visibility.clone(),
                                           value.linkage, value.declarationSpan.clone(), bodyId,
                                           zc::none});
      zc::Vector<HirNodeId> statements;
      statements.add(sourceLocalId);
      statements.add(destinationLocalId);
      statements.add(returnId);
      blocks.add(HirBlockStatement{bodyId, zc::mv(statements), value.bodySpan.clone()});
      returns.add(
          HirReturnStatement{returnId, value.resultType, returnValueId, value.returnSpan.clone()});
      ZC_IF_SOME(literal, sequential.literal) {
        expressions.add(HirScalarLiteralExpression{
            sourceInitializerId, sequential.source.type, literal.clone(), HirValueCategory::Value,
            ZC_ASSERT_NONNULL(sequential.source.initializerSpan).clone()});
      }
      ZC_IF_SOME(aggregate, sequential.aggregate) {
        aggregates.add(HirNominalAggregateExpression{
            sourceInitializerId, aggregate.definition, aggregate.type, zc::mv(aggregate.elements),
            aggregate.category, aggregate.sourceSpan.clone()});
      }
      locals.add(HirLocalBinding{sourceLocalId, hirLocalId(1), sequential.source.type,
                                 sourceInitializerId, sequential.source.sourceSpan.clone(),
                                 ZC_ASSERT_NONNULL(sequential.source.initializerSpan).clone()});
      locals.add(
          HirLocalBinding{destinationLocalId, hirLocalId(2), sequential.destination.type,
                          destinationInitializerId, sequential.destination.sourceSpan.clone(),
                          ZC_ASSERT_NONNULL(sequential.destination.initializerSpan).clone()});
      localReferences.add(HirLocalReferenceExpression{
          destinationInitializerId, hirLocalId(1), sequential.initializerReference.type,
          sequential.initializerReference.category,
          sequential.initializerReference.sourceSpan.clone()});
      localReferences.add(HirLocalReferenceExpression{
          returnValueId, sequential.returnsSource ? hirLocalId(1) : hirLocalId(2),
          sequential.returnReference.type, sequential.returnReference.category,
          sequential.returnReference.sourceSpan.clone()});
      continue;
    }
    HirNodeId localId;
    zc::Maybe<HirNodeId> initializerId;
    if (value.local != zc::none) {
      localId = hirId(next++);
      ZC_IF_SOME(local, value.local) {
        if (local.initializer != zc::none) { initializerId = hirId(next++); }
      }
    }
    ZC_IF_SOME(aggregate, value.aggregate) {
      if (initializerId == zc::none) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::MissingRequiredFact, module,
                                             registries, 1);
      }
      ZC_IF_SOME(identifier, initializerId) {
        aggregates.add(HirNominalAggregateExpression{
            identifier, aggregate.definition, aggregate.type, zc::mv(aggregate.elements),
            aggregate.category, aggregate.sourceSpan.clone()});
      }
    }
    zc::Vector<HirNodeId> writeIds;
    zc::Vector<HirNodeId> writeValueIds;
    for (size_t index = 0; index < value.localWrites.size(); ++index) {
      writeIds.add(hirId(next++));
      writeValueIds.add(hirId(next++));
    }
    const auto returnId = hirId(next++);
    HirNodeId receiverId;
    if (value.receiverCall != zc::none) { receiverId = hirId(next++); }
    const auto valueId = hirId(next++);
    zc::Maybe<HirNodeId> unsafeBlockId;
    if (value.unsafeBlockSpan != zc::none) {
      unsafeBlockId = hirId(next++);
      unsafeBlocks.add(HirUnsafeBlockExpression{ZC_ASSERT_NONNULL(unsafeBlockId), valueId,
                                                value.resultType,
                                                ZC_ASSERT_NONNULL(value.unsafeBlockSpan).clone()});
    }
    functions.add(HirFunctionDeclaration{functionId, value.definition, value.resultType,
                                         zc::mv(value.parameters), value.visibility.clone(),
                                         value.linkage, value.declarationSpan.clone(), bodyId,
                                         zc::mv(unsafeBlockId)});
    zc::Vector<HirNodeId> statements;
    if (value.local != zc::none) { statements.add(localId); }
    for (const auto writeId : writeIds) { statements.add(writeId); }
    statements.add(returnId);
    blocks.add(HirBlockStatement{bodyId, zc::mv(statements), value.bodySpan.clone()});
    returns.add(HirReturnStatement{returnId, value.resultType, valueId, value.returnSpan.clone()});
    ZC_IF_SOME(literal, value.literal) {
      HirNodeId expressionId = valueId;
      identity::SemanticTypeId expressionType = value.resultType;
      identity::SourceSpan expressionSpan = value.valueSpan.clone();
      ZC_IF_SOME(local, value.local) {
        if (initializerId == zc::none || local.initializerSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, 1);
        }
        ZC_IF_SOME(value, initializerId) { expressionId = value; }
        expressionType = local.type;
        ZC_IF_SOME(span, local.initializerSpan) { expressionSpan = span.clone(); }
      }
      expressions.add(HirScalarLiteralExpression{expressionId, expressionType, literal.clone(),
                                                 HirValueCategory::Value, zc::mv(expressionSpan)});
    }
    if (value.localWrites.size() != value.localWriteLiterals.size() ||
        writeIds.size() != value.localWrites.size() ||
        writeValueIds.size() != value.localWrites.size()) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::MissingRequiredFact, module,
                                           registries, 1);
    }
    for (size_t index = 0; index < value.localWrites.size(); ++index) {
      const auto& write = value.localWrites[index];
      const auto& literal = value.localWriteLiterals[index];
      expressions.add(HirScalarLiteralExpression{writeValueIds[index], write.type, literal.clone(),
                                                 HirValueCategory::Value, write.valueSpan.clone()});
    }
    ZC_IF_SOME(local, value.local) {
      zc::Maybe<HirNodeId> initializer;
      zc::Maybe<identity::SourceSpan> initializerSpan;
      ZC_IF_SOME(value, initializerId) { initializer = value; }
      ZC_IF_SOME(span, local.initializerSpan) { initializerSpan = span.clone(); }
      locals.add(HirLocalBinding{localId, hirLocalId(1), local.type, zc::mv(initializer),
                                 local.sourceSpan.clone(), zc::mv(initializerSpan)});
    }
    for (size_t index = 0; index < value.localWrites.size(); ++index) {
      const auto& write = value.localWrites[index];
      localWrites.add(HirLocalWriteStatement{writeIds[index], hirLocalId(1), write.field,
                                             write.type, writeValueIds[index], write.kind,
                                             write.sourceSpan.clone(), write.valueSpan.clone()});
    }
    ZC_IF_SOME(reference, value.localReference) {
      const auto referenceId = value.receiverCall != zc::none ? receiverId : valueId;
      localReferences.add(HirLocalReferenceExpression{referenceId, hirLocalId(1), reference.type,
                                                      reference.category,
                                                      reference.sourceSpan.clone()});
    }
    ZC_IF_SOME(projection, value.localFieldProjection) {
      localFieldProjections.add(HirLocalFieldProjectionExpression{
          valueId, hirLocalId(1), projection.field, projection.receiverType, projection.type,
          projection.category, projection.sourceSpan.clone()});
    }
    ZC_IF_SOME(reference, value.parameterReference) {
      HirNodeId referenceId = valueId;
      ZC_IF_SOME(initializer, initializerId) { referenceId = initializer; }
      parameterReferences.add(
          HirParameterReferenceExpression{referenceId, reference.parameter.clone(), reference.type,
                                          reference.category, reference.sourceSpan.clone()});
    }
    ZC_IF_SOME(index, value.parameterIndex) {
      parameterIndexes.add(HirParameterIndexExpression{
          valueId, index.parameter.clone(), index.receiverType, index.indexType,
          index.index.clone(), index.type, index.category, index.sourceSpan.clone(),
          index.indexSpan.clone()});
    }
    ZC_IF_SOME(reborrow, value.parameterReborrow) {
      zc::Maybe<HirLocalId> sourceAlias;
      if (reborrow.sourceAlias != zc::none) { sourceAlias = hirLocalId(1); }
      parameterReborrows.add(HirParameterReborrowExpression{
          valueId, reborrow.parameter.clone(), zc::mv(sourceAlias), reborrow.sourceType,
          reborrow.type, reborrow.mutability, reborrow.sourceSpan.clone()});
    }
    ZC_IF_SOME(borrow, value.localBorrow) {
      localBorrows.add(HirLocalBorrowExpression{valueId, hirLocalId(1), borrow.sourceType,
                                                borrow.type, borrow.mutability,
                                                borrow.sourceSpan.clone()});
    }
    ZC_IF_SOME(call, value.call) {
      HirNodeId callId = valueId;
      if (value.local != zc::none) {
        if (initializerId == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, 1);
        }
        ZC_IF_SOME(value, initializerId) { callId = value; }
      }
      zc::Vector<HirDirectCallArgument> arguments;
      for (const auto& argument : call.arguments) {
        arguments.add(HirDirectCallArgument{argument.type, argument.value.clone(),
                                            argument.sourceSpan.clone()});
      }
      calls.add(HirDirectCallExpression{callId, call.callee, call.calleeType, call.resultType,
                                        zc::mv(arguments), call.sourceSpan.clone()});
    }
    ZC_IF_SOME(call, value.receiverCall) {
      if (value.local == zc::none || value.localReference == zc::none) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::MissingRequiredFact, module,
                                             registries, 1);
      }
      zc::Vector<checker::checked::ReceiverAdjustmentStep> adjustments;
      for (const auto adjustment : call.receiverAdjustments) { adjustments.add(adjustment); }
      zc::Vector<HirDirectCallArgument> arguments;
      for (const auto& argument : call.arguments) {
        arguments.add(HirDirectCallArgument{argument.type, argument.value.clone(),
                                            argument.sourceSpan.clone()});
      }
      receiverCalls.add(HirReceiverCallExpression{
          valueId, receiverId, call.callee, call.calleeType, call.receiverSourceType,
          call.receiverType, call.receiverMode, zc::mv(adjustments), call.resultType,
          zc::mv(arguments), call.sourceSpan.clone()});
    }
  }

  auto impl = zc::heap<HirModuleCandidate::Impl>(
      zc::mv(checkedModule), zc::mv(declarations), zc::mv(functions), zc::mv(blocks),
      zc::mv(returns), zc::mv(patterns), zc::mv(expressions), zc::mv(aggregates), zc::mv(locals),
      zc::mv(localWrites), zc::mv(localReferences), zc::mv(localFieldProjections),
      zc::mv(parameterReferences), zc::mv(parameterIndexes), zc::mv(parameterReborrows),
      zc::mv(localBorrows), zc::mv(calls), zc::mv(receiverCalls), zc::mv(unsafeBlocks));
  return ir::IrOperationResult<HirModuleCandidate>::verified(HirModuleCandidate(zc::mv(impl)));
}

ir::IrOperationResult<VerifiedHirModule> HirVerifier::verify(HirModuleCandidate&& candidate) {
  const auto module = candidate.impl->checkedModule.module();
  const auto registries = candidate.impl->checkedModule.retainIdentityAuthority();
  const auto& semanticTypes = candidate.impl->checkedModule.semanticTypes();
  const auto& facts = candidate.impl->checkedModule.checkedFacts();
  const auto bound = candidate.impl->checkedModule.retainAdmittedBoundModule();
  const auto& definitions = bound.definitions();
  const auto& signatures = candidate.impl->checkedModule.ownModuleInterface().signatures();
  const auto declarationCount = candidate.impl->declarations.size();
  const auto functionCount = candidate.impl->functions.size();
  const auto directCallCount = candidate.impl->calls.size();
  const auto receiverCallCount = candidate.impl->receiverCalls.size();
  size_t directCallArgumentCount = 0;
  size_t receiverCallArgumentCount = 0;
  const auto localReturnCount = candidate.impl->locals.size();
  const auto localWriteCount = candidate.impl->localWrites.size();
  const auto parameterReferenceCount = candidate.impl->parameterReferences.size();
  const auto parameterIndexCount = candidate.impl->parameterIndexes.size();
  const auto parameterReborrowCount = candidate.impl->parameterReborrows.size();
  const auto localBorrowCount = candidate.impl->localBorrows.size();
  const auto aggregateCount = candidate.impl->aggregates.size();
  const auto localFieldProjectionCount = candidate.impl->localFieldProjections.size();
  size_t localFieldWriteCount = 0;
  size_t localAliasReborrowCount = 0;
  size_t aggregateElementCount = 0;
  const auto unsafeBlockCount = candidate.impl->unsafeBlocks.size();
  for (const auto& write : candidate.impl->localWrites) {
    if (write.field != zc::none) ++localFieldWriteCount;
  }
  for (const auto& reborrow : candidate.impl->parameterReborrows) {
    if (reborrow.sourceAlias != zc::none) ++localAliasReborrowCount;
  }
  for (const auto& aggregate : candidate.impl->aggregates) {
    aggregateElementCount += aggregate.elements.size();
  }
  for (const auto& call : candidate.impl->calls) {
    directCallArgumentCount += call.arguments.size();
  }
  for (const auto& call : candidate.impl->receiverCalls) {
    receiverCallArgumentCount += call.arguments.size();
  }
  size_t uninitializedLocalReturnCount = 0;
  for (const auto& local : candidate.impl->locals) {
    if (local.initializer == zc::none) ++uninitializedLocalReturnCount;
  }
  const auto executableDefinitions = executableDefinitionCount(definitions);
  const auto borrowCapability = candidate.impl->checkedModule.borrowEvidenceCapability();
  const auto borrowEvidence =
      borrowCapability.lookup(candidate.impl->checkedModule.borrowEvidenceLease());
  if (registries.semanticContext() != candidate.impl->checkedModule.semanticContext() ||
      registries.fingerprint().digest() !=
          candidate.impl->checkedModule.contextFingerprint().digest() ||
      registries.boundModule(module) == zc::none ||
      candidate.impl->checkedModule.checkedRepository().lookup(
          candidate.impl->checkedModule.checkedEvidenceLease()) == zc::none ||
      !borrowEvidence.isResolved() ||
      borrowEvidence.evidence().revision().digest() !=
          candidate.impl->checkedModule.borrowEvidenceRevision().digest() ||
      candidate.impl->checkedModule.borrowEvidenceLease().key().revision.digest() !=
          candidate.impl->checkedModule.borrowEvidenceRevision().digest() ||
      candidate.impl->checkedModule.dispatchFacts().facts().size() !=
          directCallCount + receiverCallCount ||
      !noUnsupportedFacts(facts) || candidate.impl->patterns.size() != declarationCount ||
      candidate.impl->localReferences.size() + localFieldProjectionCount + localAliasReborrowCount +
              localBorrowCount !=
          localReturnCount ||
      parameterReferenceCount + parameterIndexCount + parameterReborrowCount >
          functionCount + localAliasReborrowCount ||
      candidate.impl->blocks.size() != functionCount ||
      candidate.impl->returns.size() != functionCount ||
      candidate.impl->expressions.size() != declarationCount + functionCount - directCallCount -
                                                aggregateCount - uninitializedLocalReturnCount -
                                                parameterReferenceCount - parameterReborrowCount +
                                                localAliasReborrowCount + localWriteCount ||
      executableDefinitions != declarationCount + functionCount ||
      facts.definitionTypes().size() != declarationCount ||
      facts.nodeTypes().size() !=
          declarationCount + functionCount + directCallCount + receiverCallCount * 2 +
              localReturnCount - uninitializedLocalReturnCount + localWriteCount * 3 +
              aggregateElementCount + localFieldProjectionCount + localFieldWriteCount +
              parameterIndexCount * 2 + parameterReborrowCount * 2 + directCallArgumentCount +
              receiverCallArgumentCount + localBorrowCount + unsafeBlockCount ||
      facts.literals().size() !=
          declarationCount + functionCount - directCallCount - aggregateCount -
              uninitializedLocalReturnCount - parameterReferenceCount - parameterReborrowCount +
              localAliasReborrowCount + localWriteCount + aggregateElementCount +
              directCallArgumentCount + receiverCallArgumentCount ||
      facts.calls().size() != directCallCount + receiverCallCount + parameterIndexCount ||
      facts.patterns().size() != declarationCount || facts.aggregates().size() != aggregateCount ||
      facts.members().size() !=
          localFieldProjectionCount + localFieldWriteCount + receiverCallCount ||
      facts.places().size() !=
          localFieldProjectionCount + localFieldWriteCount + parameterIndexCount ||
      facts.indexes().size() != parameterIndexCount ||
      facts.markerObligations().size() != parameterIndexCount) {
    return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                        ir::IrFailureKind::InputRevisionMismatch, module,
                                        registries, 0);
  }

  size_t expectedConstantCount = 0;
  for (const auto& declaration : candidate.impl->declarations) {
    if (declaration.definitionKind == identity::DefinitionKind::Constant) {
      ++expectedConstantCount;
    }
  }
  if (facts.constants().size() != expectedConstantCount) {
    return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                        ir::IrFailureKind::AdditionalFact, module, registries, 0);
  }

  for (size_t sourceIndex = 0; sourceIndex < declarationCount; ++sourceIndex) {
    const auto index = static_cast<uint32_t>(sourceIndex);
    const auto& declaration = candidate.impl->declarations[index];
    const auto& pattern = candidate.impl->patterns[index];
    const auto& expression = candidate.impl->expressions[index];
    const uint32_t expectedDeclaration = index * 3 + 1;
    if (declaration.node.ordinal() != expectedDeclaration ||
        pattern.node.ordinal() != expectedDeclaration + 1 ||
        expression.node.ordinal() != expectedDeclaration + 2 ||
        declaration.pattern != pattern.node || declaration.initializer != expression.node ||
        pattern.binding != declaration.definition || declaration.inferredType != pattern.type ||
        pattern.type != pattern.scrutineeType || declaration.inferredType != expression.type ||
        expression.category != HirValueCategory::Value || !pattern.reachable ||
        !typeExists(declaration.declaredType, semanticTypes) ||
        !typeExists(declaration.inferredType, semanticTypes) ||
        (index != 0 && declaration.sourceSpan.byteStart() <
                           candidate.impl->declarations[index - 1].sourceSpan.byteStart())) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }

    auto sourceDefinitionIndex = definitionIndex(definitions, declaration.definition);
    if (sourceDefinitionIndex == zc::none) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::AdditionalFact, module, registries,
                                          index + 1);
    }
    size_t definitionSlot = 0;
    ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
    const auto& sourceDefinition = definitions.definitions()[definitionSlot];
    if (!hasExecutableBody(sourceDefinition, definitions) ||
        !definitionBelongsToModule(sourceDefinition, definitions) ||
        sourceDefinition.record.kind() != declaration.definitionKind) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    const auto& tree = bound.tree();
    const auto definitionInventory = binder::DefinitionInventory::collect(tree);
    auto patternSite = patternBindingSite(definitionInventory, sourceDefinition);
    if (patternSite == zc::none) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    const auto& bindingSite = ZC_ASSERT_NONNULL(patternSite);
    if (bindingSite.patternPath.size() != 0 || !tree.contains(bindingSite.introducer) ||
        tree.node(bindingSite.introducer).kind != ast::SyntaxKind::VariableDeclarator) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    const auto& declarator = tree.node(bindingSite.introducer);
    const ast::NodeId patternNode(declarator.payload.words[ast::kVariableDeclaratorPatternWord]);
    const ast::NodeId initializer(declarator.payload.words[ast::kVariableDeclaratorInitWord]);
    if (!tree.contains(patternNode) ||
        tree.node(patternNode).kind != ast::SyntaxKind::IdentifierPattern ||
        !tree.contains(initializer) || !isScalarLiteral(tree.node(initializer).kind)) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    auto declarationSourceSpan = bound.parsedModule().spanFor(declarator.range);
    auto initializerSourceSpan = bound.parsedModule().spanFor(tree.node(initializer).range);
    auto definitionTypeIndex = factIndex(facts.definitionTypes(), declaration.definition);
    auto patternFactIndex = factIndex(facts.patterns(), patternNode);
    auto nodeTypeIndex = factIndex(facts.nodeTypes(), initializer);
    auto literalIndex = factIndex(facts.literals(), initializer);
    auto signaturePosition = signatureIndex(signatures.definitions.asPtr(), declaration.definition);
    auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), declaration.definition);
    if (definitionTypeIndex == zc::none || patternFactIndex == zc::none ||
        nodeTypeIndex == zc::none || literalIndex == zc::none || signaturePosition == zc::none ||
        rootPosition == zc::none || declarationSourceSpan == zc::none ||
        initializerSourceSpan == zc::none) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::MissingRequiredFact, module,
                                          registries, index + 1);
    }
    size_t definitionTypeSlot = 0;
    size_t patternFactSlot = 0;
    size_t nodeTypeSlot = 0;
    size_t literalSlot = 0;
    size_t signatureSlot = 0;
    size_t rootSlot = 0;
    ZC_IF_SOME(value, definitionTypeIndex) { definitionTypeSlot = value; }
    ZC_IF_SOME(value, patternFactIndex) { patternFactSlot = value; }
    ZC_IF_SOME(value, nodeTypeIndex) { nodeTypeSlot = value; }
    ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
    ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
    ZC_IF_SOME(value, rootPosition) { rootSlot = value; }
    const auto& definitionType = facts.definitionTypes().entries()[definitionTypeSlot].value;
    const auto& patternFact = facts.patterns().entries()[patternFactSlot].value;
    const auto& nodeType = facts.nodeTypes().entries()[nodeTypeSlot].value;
    const auto& literalFact = facts.literals().entries()[literalSlot].value;
    const auto& signature = signatures.definitions[signatureSlot];
    const auto& root = signatures.roots[rootSlot];
    if (!signature.payload.variant().is<checker::signature::ValueSignature>() ||
        !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>()) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    const auto& valueSignature =
        signature.payload.variant().get<checker::signature::ValueSignature>();
    auto expectedVisibility = visibility(root.visibility);
    auto expectedLinkage = linkage(valueSignature);
    bool visibilityMatches = false;
    bool linkageMatches = false;
    bool declarationSpanMatches = false;
    bool initializerSpanMatches = false;
    ZC_IF_SOME(value, expectedVisibility) {
      visibilityMatches = sameVisibility(declaration.visibility, value);
    }
    ZC_IF_SOME(value, expectedLinkage) { linkageMatches = declaration.linkage == value; }
    ZC_IF_SOME(value, declarationSourceSpan) {
      declarationSpanMatches = sameSpan(declaration.sourceSpan, value);
    }
    ZC_IF_SOME(value, initializerSourceSpan) {
      initializerSpanMatches = sameSpan(expression.sourceSpan, value);
    }
    if (declaration.inferredType != definitionType || expression.type != nodeType ||
        pattern.type != patternFact.scrutineeType || patternFact.bindings.size() != 1 ||
        patternFact.bindings[0].binding != declaration.definition ||
        patternFact.bindings[0].type != pattern.type ||
        !sameConstant(expression.value, literalFact.literal, module, registries, semanticTypes) ||
        !sameSpan(expression.sourceSpan, literalFact.sourceSpan) ||
        !sameSpan(pattern.sourceSpan, sourceDefinition.source) || !declarationSpanMatches ||
        !initializerSpanMatches || !visibilityMatches || !linkageMatches ||
        signature.definition != declaration.definition ||
        signature.definitionKind != declaration.definitionKind ||
        !sameSpan(signature.declarationSpan, sourceDefinition.source) ||
        root.canonicalDefinition != declaration.definition || root.sourceModule != module ||
        valueSignature.type != declaration.declaredType ||
        declaration.declaredType != declaration.inferredType ||
        valueSignature.mutability != declaration.mutability || !valueSignature.hasInitializer ||
        patternFact.refinements.size() != 0 ||
        !patternFact.constructor.variant().is<checker::checked::WildcardPattern>() ||
        !patternFact.reachable || patternFact.guardMayRaise != zc::none) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }

    auto constantIndex = factIndex(facts.constants(), declaration.definition);
    if (declaration.definitionKind == identity::DefinitionKind::Constant) {
      if (constantIndex == zc::none || declaration.constantValue == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t constantSlot = 0;
      ZC_IF_SOME(value, constantIndex) { constantSlot = value; }
      bool same = false;
      ZC_IF_SOME(value, declaration.constantValue) {
        same = sameConstant(value, facts.constants().entries()[constantSlot].value.value, module,
                            registries, semanticTypes);
      }
      bool signatureConstantMatches = false;
      ZC_IF_SOME(value, valueSignature.constantValue) {
        signatureConstantMatches =
            sameConstant(value, facts.constants().entries()[constantSlot].value.value, module,
                         registries, semanticTypes);
      }
      const auto& constantFact = facts.constants().entries()[constantSlot].value;
      if (!same || !signatureConstantMatches || constantFact.expression != initializer ||
          constantFact.type != declaration.inferredType || constantFact.dependencies.size() != 0) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
    } else if (constantIndex != zc::none || declaration.constantValue != zc::none ||
               valueSignature.constantValue != zc::none) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::AdditionalFact, module, registries,
                                          index + 1);
    }
  }

  uint32_t nextFunction = static_cast<uint32_t>(declarationCount * 3 + 1);
  for (size_t sourceIndex = 0; sourceIndex < functionCount; ++sourceIndex) {
    const auto index = static_cast<uint32_t>(sourceIndex);
    const auto& function = candidate.impl->functions[index];
    const auto& block = candidate.impl->blocks[index];
    const auto& returnStatement = candidate.impl->returns[index];
    const uint32_t expectedFunction = nextFunction;
    bool hasSequentialDestination = false;
    for (const auto& local : candidate.impl->locals) {
      if (local.node == hirId(expectedFunction + 4)) {
        hasSequentialDestination = true;
        break;
      }
    }
    if (block.statements.size() == 3 && hasSequentialDestination) {
      auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
      if (sourceDefinitionIndex == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t definitionSlot = 0;
      ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
      const auto& sourceDefinition = definitions.definitions()[definitionSlot];
      const auto& tree = bound.tree();
      if (!hasExecutableBody(sourceDefinition, definitions) ||
          !definitionBelongsToModule(sourceDefinition, definitions) ||
          sourceDefinition.record.kind() != identity::DefinitionKind::Function ||
          !sourceDefinition.site.value().is<binder::DeclarationDefinitionSite>() ||
          !tree.contains(sourceDefinition.node)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      auto sourceShape = functionReturnShape(tree, tree.node(sourceDefinition.node));
      if (sourceShape == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      FunctionReturnShape source{};
      ZC_IF_SOME(value, sourceShape) { source = value; }
      zc::Maybe<const HirLocalBinding&> sourceLocal;
      zc::Maybe<const HirLocalBinding&> destinationLocal;
      zc::Maybe<const HirScalarLiteralExpression&> literal;
      zc::Maybe<const HirNominalAggregateExpression&> aggregate;
      zc::Maybe<const HirLocalReferenceExpression&> initializerReference;
      zc::Maybe<const HirLocalReferenceExpression&> returnReference;
      for (const auto& local : candidate.impl->locals) {
        if (local.node == hirId(expectedFunction + 2)) {
          if (sourceLocal != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          sourceLocal = local;
        }
        if (local.node == hirId(expectedFunction + 4)) {
          if (destinationLocal != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          destinationLocal = local;
        }
      }
      for (const auto& expression : candidate.impl->expressions) {
        if (expression.node != hirId(expectedFunction + 3)) continue;
        if (literal != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::AdditionalFact, module, registries,
                                              index + 1);
        }
        literal = expression;
      }
      for (const auto& expression : candidate.impl->aggregates) {
        if (expression.node != hirId(expectedFunction + 3)) continue;
        if (aggregate != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::AdditionalFact, module, registries,
                                              index + 1);
        }
        aggregate = expression;
      }
      for (const auto& reference : candidate.impl->localReferences) {
        if (reference.node == hirId(expectedFunction + 5)) {
          if (initializerReference != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          initializerReference = reference;
        }
        if (reference.node == hirId(expectedFunction + 7)) {
          if (returnReference != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          returnReference = reference;
        }
      }
      auto sourceBinding = resolvedOwnerLocal(bound.bindings(), source.destinationLocalInitializer);
      auto destinationBinding = ownerLocalBindingForPattern(definitions, source.localPattern, tree);
      const bool sourceIsLiteral = isScalarLiteral(tree.node(source.sourceLocalInitializer).kind);
      const bool sourceIsAggregate =
          tree.node(source.sourceLocalInitializer).kind == ast::SyntaxKind::StructLiteralExpr;
      auto literalIndex = factIndex(facts.literals(), source.sourceLocalInitializer);
      auto aggregateIndex = factIndex(facts.aggregates(), source.sourceLocalInitializer);
      auto sourceTypeIndex = factIndex(facts.nodeTypes(), source.sourceLocalInitializer);
      auto initializerTypeIndex = factIndex(facts.nodeTypes(), source.destinationLocalInitializer);
      auto returnTypeIndex = factIndex(facts.nodeTypes(), source.value);
      auto sourcePatternSpan =
          bound.parsedModule().spanFor(tree.node(source.sourceLocalPattern).range);
      auto destinationPatternSpan =
          bound.parsedModule().spanFor(tree.node(source.localPattern).range);
      auto sourceInitializerSpan =
          bound.parsedModule().spanFor(tree.node(source.sourceLocalInitializer).range);
      auto destinationInitializerSpan =
          bound.parsedModule().spanFor(tree.node(source.destinationLocalInitializer).range);
      auto returnSpan = bound.parsedModule().spanFor(tree.node(source.returnStatement).range);
      auto returnValueSpan = bound.parsedModule().spanFor(tree.node(source.value).range);
      if (!source.isSequentialLocalReturn || sourceLocal == zc::none ||
          destinationLocal == zc::none || ((literal == zc::none) == (aggregate == zc::none)) ||
          (!sourceIsLiteral && !sourceIsAggregate) ||
          (sourceIsLiteral && literalIndex == zc::none) ||
          (sourceIsAggregate && aggregateIndex == zc::none) || initializerReference == zc::none ||
          returnReference == zc::none || sourceBinding == zc::none ||
          destinationBinding == zc::none || sourceBinding == destinationBinding ||
          sourceTypeIndex == zc::none || initializerTypeIndex == zc::none ||
          returnTypeIndex == zc::none || sourcePatternSpan == zc::none ||
          destinationPatternSpan == zc::none || sourceInitializerSpan == zc::none ||
          destinationInitializerSpan == zc::none || returnSpan == zc::none ||
          returnValueSpan == zc::none ||
          !ownerLocalMatches(definitions, ZC_ASSERT_NONNULL(sourceBinding),
                             source.sourceLocalPattern, tree) ||
          !ownerLocalMatches(definitions, ZC_ASSERT_NONNULL(destinationBinding),
                             source.localPattern, tree)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t literalSlot = 0;
      size_t aggregateSlot = 0;
      size_t sourceTypeSlot = 0;
      size_t initializerTypeSlot = 0;
      size_t returnTypeSlot = 0;
      ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
      ZC_IF_SOME(value, aggregateIndex) { aggregateSlot = value; }
      ZC_IF_SOME(value, sourceTypeIndex) { sourceTypeSlot = value; }
      ZC_IF_SOME(value, initializerTypeIndex) { initializerTypeSlot = value; }
      ZC_IF_SOME(value, returnTypeIndex) { returnTypeSlot = value; }
      const auto sourceType = facts.nodeTypes().entries()[sourceTypeSlot].value;
      const auto initializerType = facts.nodeTypes().entries()[initializerTypeSlot].value;
      const auto returnType = facts.nodeTypes().entries()[returnTypeSlot].value;
      const auto& sourceLocalValue = ZC_ASSERT_NONNULL(sourceLocal);
      const auto& destinationLocalValue = ZC_ASSERT_NONNULL(destinationLocal);
      const auto& initializerReferenceValue = ZC_ASSERT_NONNULL(initializerReference);
      const auto& returnReferenceValue = ZC_ASSERT_NONNULL(returnReference);
      auto signaturePosition = signatureIndex(signatures.definitions.asPtr(), function.definition);
      auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), function.definition);
      auto bodySpan = bound.parsedModule().spanFor(tree.node(source.body).range);
      if (signaturePosition == zc::none || rootPosition == zc::none || bodySpan == zc::none ||
          !tree.contains(sourceDefinition.node) ||
          tree.node(sourceDefinition.node).kind != ast::SyntaxKind::FunctionDecl) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t signatureSlot = 0;
      size_t rootSlot = 0;
      ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
      ZC_IF_SOME(value, rootPosition) { rootSlot = value; }
      const auto& signature = signatures.definitions[signatureSlot];
      const auto& root = signatures.roots[rootSlot];
      auto expectedVisibility = visibility(root.visibility);
      if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
          !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>() ||
          expectedVisibility == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      const auto& callable =
          signature.payload.variant().get<checker::signature::CallableSignature>();
      auto expectedLinkage = linkage(callable);
      if (expectedLinkage == zc::none || signature.definition != function.definition ||
          signature.definitionKind != identity::DefinitionKind::Function ||
          root.canonicalDefinition != function.definition || root.sourceModule != module ||
          callable.receiver != zc::none || callable.raises != zc::none ||
          callable.success != function.resultType ||
          !sameSpan(signature.declarationSpan, sourceDefinition.source) ||
          !sameSpan(function.sourceSpan, sourceDefinition.source) ||
          !sameVisibility(function.visibility, ZC_ASSERT_NONNULL(expectedVisibility)) ||
          function.linkage != ZC_ASSERT_NONNULL(expectedLinkage) ||
          !sameSpan(block.sourceSpan, ZC_ASSERT_NONNULL(bodySpan)) ||
          function.parameters.size() != callable.parameters.size()) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      for (size_t parameterIndex = 0; parameterIndex < function.parameters.size();
           ++parameterIndex) {
        const auto& parameter = function.parameters[parameterIndex];
        const auto& sourceParameter = callable.parameters[parameterIndex];
        if (parameter.key != sourceParameter.parameter || parameter.type != sourceParameter.type ||
            sourceParameter.hasDefault || !typeExists(parameter.type, semanticTypes)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
      }
      if (function.node != hirId(expectedFunction) || block.node != hirId(expectedFunction + 1) ||
          function.body != block.node || sourceLocalValue.node != hirId(expectedFunction + 2) ||
          sourceLocalValue.local != hirLocalId(1) ||
          sourceLocalValue.initializer != hirId(expectedFunction + 3) ||
          destinationLocalValue.node != hirId(expectedFunction + 4) ||
          destinationLocalValue.local != hirLocalId(2) ||
          destinationLocalValue.initializer != hirId(expectedFunction + 5) ||
          returnStatement.node != hirId(expectedFunction + 6) ||
          returnStatement.value != hirId(expectedFunction + 7) ||
          block.statements[0] != sourceLocalValue.node ||
          block.statements[1] != destinationLocalValue.node ||
          block.statements[2] != returnStatement.node ||
          initializerReferenceValue.local != sourceLocalValue.local ||
          initializerReferenceValue.type != sourceType ||
          initializerReferenceValue.category != HirValueCategory::Place ||
          returnReferenceValue.local != (source.sequentialReturnUsesSource
                                             ? sourceLocalValue.local
                                             : destinationLocalValue.local) ||
          returnReferenceValue.type != sourceType ||
          returnReferenceValue.category != HirValueCategory::Place ||
          sourceLocalValue.type != sourceType || destinationLocalValue.type != sourceType ||
          function.resultType != sourceType || returnStatement.resultType != sourceType ||
          initializerType != sourceType || returnType != sourceType ||
          !sameSpan(sourceLocalValue.sourceSpan, ZC_ASSERT_NONNULL(sourcePatternSpan)) ||
          !sameSpan(ZC_ASSERT_NONNULL(sourceLocalValue.initializerSpan),
                    ZC_ASSERT_NONNULL(sourceInitializerSpan)) ||
          !sameSpan(destinationLocalValue.sourceSpan, ZC_ASSERT_NONNULL(destinationPatternSpan)) ||
          !sameSpan(ZC_ASSERT_NONNULL(destinationLocalValue.initializerSpan),
                    ZC_ASSERT_NONNULL(destinationInitializerSpan)) ||
          !sameSpan(initializerReferenceValue.sourceSpan,
                    ZC_ASSERT_NONNULL(destinationInitializerSpan)) ||
          !sameSpan(returnReferenceValue.sourceSpan, ZC_ASSERT_NONNULL(returnValueSpan)) ||
          !sameSpan(returnStatement.sourceSpan, ZC_ASSERT_NONNULL(returnSpan)) ||
          !typeExists(sourceType, semanticTypes)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      ZC_IF_SOME(value, literal) {
        const auto& sourceLiteral = facts.literals().entries()[literalSlot].value;
        if (!sourceIsLiteral || value.node != hirId(expectedFunction + 3) ||
            value.type != sourceType || value.category != HirValueCategory::Value ||
            sourceLiteral.type != sourceType ||
            !sameConstant(value.value, sourceLiteral.literal, module, registries, semanticTypes) ||
            !sameSpan(value.sourceSpan, ZC_ASSERT_NONNULL(sourceInitializerSpan))) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
      }
      ZC_IF_SOME(value, aggregate) {
        const auto& checkedAggregate = facts.aggregates().entries()[aggregateSlot].value;
        if (!sourceIsAggregate || value.node != hirId(expectedFunction + 3) ||
            value.type != sourceType || value.category != HirValueCategory::Value ||
            checkedAggregate.node != source.sourceLocalInitializer ||
            !checkedAggregate.kind.variant().is<checker::checked::NominalAggregate>() ||
            checkedAggregate.kind.variant().get<checker::checked::NominalAggregate>().definition !=
                value.definition ||
            checkedAggregate.resultType != value.type ||
            !sameSpan(checkedAggregate.sourceSpan, ZC_ASSERT_NONNULL(sourceInitializerSpan)) ||
            checkedAggregate.elements.size() != value.elements.size()) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        for (size_t elementIndex = 0; elementIndex < value.elements.size(); ++elementIndex) {
          const auto& checkedElement = checkedAggregate.elements[elementIndex];
          const auto& element = value.elements[elementIndex];
          auto elementLiteral = factIndex(facts.literals(), checkedElement.sourceNode);
          if (checkedElement.field == zc::none || checkedElement.index != elementIndex ||
              checkedElement.sourceType != checkedElement.destinationType ||
              checkedElement.adjustment != zc::none || elementLiteral == zc::none ||
              ZC_ASSERT_NONNULL(checkedElement.field) != element.field ||
              checkedElement.destinationType != element.type) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          size_t elementSlot = 0;
          ZC_IF_SOME(item, elementLiteral) { elementSlot = item; }
          const auto& checkedLiteral = facts.literals().entries()[elementSlot].value;
          if (checkedLiteral.type != element.type ||
              !sameConstant(checkedLiteral.literal, element.value, module, registries,
                            semanticTypes) ||
              !sameSpan(checkedLiteral.sourceSpan, element.sourceSpan)) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
      }
      nextFunction += 8;
      continue;
    }
    zc::Maybe<const HirLocalBinding&> localBinding;
    for (const auto& local : candidate.impl->locals) {
      if (local.node != hirId(expectedFunction + 2)) continue;
      if (localBinding != zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::AdditionalFact, module, registries,
                                            index + 1);
      }
      localBinding = local;
    }
    const bool returnsLocal = localBinding != zc::none;
    bool localHasInitializer = false;
    HirNodeId materializedNode;
    ZC_IF_SOME(local, localBinding) {
      ZC_IF_SOME(initializer, local.initializer) {
        materializedNode = initializer;
        localHasInitializer = true;
      }
    }
    size_t functionLocalWriteCount = 0;
    if (returnsLocal && block.statements.size() >= 2) {
      functionLocalWriteCount = block.statements.size() - 2;
    }
    zc::Maybe<const HirLocalWriteStatement&> localWrite;
    if (returnsLocal) {
      const auto expectedWrite = hirId(expectedFunction + (localHasInitializer ? 4 : 3));
      for (const auto& write : candidate.impl->localWrites) {
        if (write.node != expectedWrite) continue;
        if (localWrite != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::AdditionalFact, module, registries,
                                              index + 1);
        }
        localWrite = write;
      }
    }
    const bool hasLocalWrite = functionLocalWriteCount != 0;
    const auto returnNode =
        hirId(expectedFunction +
              (returnsLocal ? (localHasInitializer ? 4 : 3) + functionLocalWriteCount * 2 : 2));
    const auto valueNode =
        hirId(expectedFunction +
              (returnsLocal ? (localHasInitializer ? 5 : 4) + functionLocalWriteCount * 2 : 3));
    if (!localHasInitializer) {
      materializedNode = hasLocalWrite ? hirId(expectedFunction + 4) : valueNode;
    }
    zc::Maybe<const HirScalarLiteralExpression&> literalExpression;
    zc::Maybe<const HirDirectCallExpression&> directCall;
    for (const auto& expression : candidate.impl->expressions) {
      if (expression.node != materializedNode) continue;
      if (literalExpression != zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::AdditionalFact, module, registries,
                                            index + 1);
      }
      literalExpression = expression;
    }
    for (const auto& call : candidate.impl->calls) {
      if (call.node != materializedNode) continue;
      if (directCall != zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::AdditionalFact, module, registries,
                                            index + 1);
      }
      directCall = call;
    }
    zc::Maybe<const HirScalarLiteralExpression&> writeLiteral;
    if (hasLocalWrite) {
      for (const auto& expression : candidate.impl->expressions) {
        if (expression.node != hirId(expectedFunction + (localHasInitializer ? 5 : 4))) continue;
        if (writeLiteral != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::AdditionalFact, module, registries,
                                              index + 1);
        }
        writeLiteral = expression;
      }
    }
    bool writesMatchBlock = true;
    if (hasLocalWrite) {
      for (size_t writeIndex = 0; writeIndex < functionLocalWriteCount; ++writeIndex) {
        const auto expectedWrite =
            hirId(expectedFunction + (localHasInitializer ? 4 : 3) + writeIndex * 2);
        bool found = false;
        for (const auto& write : candidate.impl->localWrites) {
          if (write.node != expectedWrite) continue;
          if (found) writesMatchBlock = false;
          found = true;
        }
        if (!found || block.statements[writeIndex + 1] != expectedWrite) {
          writesMatchBlock = false;
        }
      }
    }
    zc::Maybe<const HirLocalReferenceExpression&> localReference;
    for (const auto& reference : candidate.impl->localReferences) {
      if (reference.node != valueNode) continue;
      if (localReference != zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::AdditionalFact, module, registries,
                                            index + 1);
      }
      localReference = reference;
    }
    zc::Maybe<const HirLocalBorrowExpression&> localBorrow;
    for (const auto& borrow : candidate.impl->localBorrows) {
      if (borrow.node != valueNode) continue;
      if (localBorrow != zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::AdditionalFact, module, registries,
                                            index + 1);
      }
      localBorrow = borrow;
    }
    zc::Maybe<const HirReceiverCallExpression&> receiverCall;
    const auto receiverReferenceNode = hirId(valueNode.ordinal());
    const auto receiverCallNode = hirId(valueNode.ordinal() + 1);
    for (const auto& call : candidate.impl->receiverCalls) {
      if (call.node != receiverCallNode) continue;
      if (receiverCall != zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::AdditionalFact, module, registries,
                                            index + 1);
      }
      receiverCall = call;
    }
    if (receiverCall != zc::none) {
      zc::Maybe<const HirNominalAggregateExpression&> receiverAggregate;
      for (const auto& aggregate : candidate.impl->aggregates) {
        if (aggregate.node != materializedNode) continue;
        if (receiverAggregate != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::AdditionalFact, module, registries,
                                              index + 1);
        }
        receiverAggregate = aggregate;
      }
      if (!returnsLocal || !localHasInitializer || hasLocalWrite || directCall != zc::none ||
          localBinding == zc::none || localReference == zc::none || literalExpression != zc::none ||
          receiverAggregate == zc::none || function.node != hirId(expectedFunction) ||
          block.node != hirId(expectedFunction + 1) ||
          ZC_ASSERT_NONNULL(localBinding).node != hirId(expectedFunction + 2) ||
          ZC_ASSERT_NONNULL(localBinding).initializer != hirId(expectedFunction + 3) ||
          ZC_ASSERT_NONNULL(localBinding).initializerSpan == zc::none ||
          ZC_ASSERT_NONNULL(receiverAggregate).node != hirId(expectedFunction + 3) ||
          ZC_ASSERT_NONNULL(receiverAggregate).type != ZC_ASSERT_NONNULL(localBinding).type ||
          ZC_ASSERT_NONNULL(receiverAggregate).category != HirValueCategory::Value ||
          returnStatement.node != hirId(expectedFunction + 4) ||
          ZC_ASSERT_NONNULL(localReference).node != receiverReferenceNode ||
          returnStatement.value != ZC_ASSERT_NONNULL(receiverCall).node ||
          ZC_ASSERT_NONNULL(receiverCall).receiver != receiverReferenceNode ||
          function.body != block.node || block.statements.size() != 2 ||
          block.statements[0] != ZC_ASSERT_NONNULL(localBinding).node ||
          block.statements[1] != returnStatement.node ||
          ZC_ASSERT_NONNULL(localReference).local != ZC_ASSERT_NONNULL(localBinding).local ||
          ZC_ASSERT_NONNULL(localReference).type != ZC_ASSERT_NONNULL(localBinding).type ||
          ZC_ASSERT_NONNULL(localReference).category != HirValueCategory::Place ||
          ZC_ASSERT_NONNULL(receiverCall).receiverSourceType !=
              ZC_ASSERT_NONNULL(localBinding).type ||
          ZC_ASSERT_NONNULL(receiverCall).resultType != function.resultType ||
          ZC_ASSERT_NONNULL(receiverCall).receiverMode != checker::checked::ReceiverMode::Mutable ||
          ZC_ASSERT_NONNULL(receiverCall).receiverAdjustments.size() != 1 ||
          ZC_ASSERT_NONNULL(receiverCall).receiverAdjustments[0] !=
              checker::checked::ReceiverAdjustmentStep::BorrowMutable ||
          !typeExists(ZC_ASSERT_NONNULL(localBinding).type, semanticTypes) ||
          !typeExists(function.resultType, semanticTypes)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      auto receiverParameter = semanticTypes.get(ZC_ASSERT_NONNULL(receiverCall).receiverType);
      if (!receiverParameter.is<type::SemanticTypeLookup>() ||
          !receiverParameter.get<type::SemanticTypeLookup>()
               .data()
               .is<type::semantic::ReferenceTypeData>() ||
          receiverParameter.get<type::SemanticTypeLookup>()
                  .data()
                  .get<type::semantic::ReferenceTypeData>()
                  .mutability != type::semantic::Mutability::Mutable ||
          receiverParameter.get<type::SemanticTypeLookup>()
                  .data()
                  .get<type::semantic::ReferenceTypeData>()
                  .referent != ZC_ASSERT_NONNULL(receiverCall).receiverSourceType) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }

      auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
      auto signaturePosition = signatureIndex(signatures.definitions.asPtr(), function.definition);
      auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), function.definition);
      if (sourceDefinitionIndex == zc::none || signaturePosition == zc::none ||
          rootPosition == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t definitionSlot = 0;
      size_t signatureSlot = 0;
      size_t rootSlot = 0;
      ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
      ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
      ZC_IF_SOME(value, rootPosition) { rootSlot = value; }
      const auto& sourceDefinition = definitions.definitions()[definitionSlot];
      const auto& signature = signatures.definitions[signatureSlot];
      const auto& root = signatures.roots[rootSlot];
      const auto& tree = bound.tree();
      if (!hasExecutableBody(sourceDefinition, definitions) ||
          !definitionBelongsToModule(sourceDefinition, definitions) ||
          sourceDefinition.record.kind() != identity::DefinitionKind::Function ||
          !sourceDefinition.site.value().is<binder::DeclarationDefinitionSite>() ||
          !tree.contains(sourceDefinition.node) ||
          tree.node(sourceDefinition.node).kind != ast::SyntaxKind::FunctionDecl ||
          !signature.payload.variant().is<checker::signature::CallableSignature>() ||
          !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>()) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      const auto& callable =
          signature.payload.variant().get<checker::signature::CallableSignature>();
      auto expectedVisibility = visibility(root.visibility);
      auto expectedLinkage = linkage(callable);
      auto sourceShape = functionReturnShape(tree, tree.node(sourceDefinition.node));
      if (expectedVisibility == zc::none || expectedLinkage == zc::none ||
          sourceShape == zc::none || signature.definition != function.definition ||
          signature.definitionKind != identity::DefinitionKind::Function ||
          root.canonicalDefinition != function.definition || root.sourceModule != module ||
          callable.receiver != zc::none || callable.raises != zc::none ||
          callable.success != function.resultType ||
          !sameSpan(signature.declarationSpan, sourceDefinition.source) ||
          !sameSpan(function.sourceSpan, sourceDefinition.source) ||
          !sameVisibility(function.visibility, ZC_ASSERT_NONNULL(expectedVisibility)) ||
          function.linkage != ZC_ASSERT_NONNULL(expectedLinkage) ||
          function.parameters.size() != callable.parameters.size()) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      FunctionReturnShape source{};
      ZC_IF_SOME(value, sourceShape) { source = value; }
      if (!source.returnsLocal || !source.returnsReceiverCall ||
          source.localInitializer == zc::none || source.localWrites.size != 0 ||
          !tree.contains(source.value) ||
          tree.node(source.value).kind != ast::SyntaxKind::CallExpression) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      ast::NodeId initializer;
      ZC_IF_SOME(value, source.localInitializer) { initializer = value; }
      const auto& sourceCall = tree.node(source.value);
      const ast::NodeId calleeNode(sourceCall.payload.words[ast::kCallExpressionCalleeWord]);
      const ast::NodeList arguments{sourceCall.payload.words[ast::kCallExpressionArgsFirstWord],
                                    sourceCall.payload.words[ast::kCallExpressionArgsSizeWord]};
      if (!tree.contains(initializer) ||
          tree.node(initializer).kind != ast::SyntaxKind::StructLiteralExpr ||
          !tree.contains(calleeNode) ||
          tree.node(calleeNode).kind != ast::SyntaxKind::MemberExpression ||
          static_cast<ast::MemberAccessKind>(
              tree.node(calleeNode).payload.words[ast::kMemberExpressionAccessWord]) !=
              ast::MemberAccessKind::Dot ||
          !tree.contains(arguments)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      const ast::NodeId receiverNode(
          tree.node(calleeNode).payload.words[ast::kMemberExpressionObjectWord]);
      auto ownerBinding = ownerLocalBindingForPattern(definitions, source.localPattern, tree);
      auto receiverBinding = resolvedOwnerLocal(bound.bindings(), receiverNode);
      auto initializerTypeIndex = factIndex(facts.nodeTypes(), initializer);
      auto initializerAggregateIndex = factIndex(facts.aggregates(), initializer);
      auto receiverTypeIndex = factIndex(facts.nodeTypes(), receiverNode);
      auto calleeTypeIndex = factIndex(facts.nodeTypes(), calleeNode);
      auto memberIndex = factIndex(facts.members(), calleeNode);
      auto checkedCallIndex = factIndex(facts.calls(), source.value);
      auto callKey = checkedNodeKey(tree, bound.parsedModule(), source.value);
      auto callSpan = bound.parsedModule().spanFor(sourceCall.range);
      auto receiverSpan = bound.parsedModule().spanFor(tree.node(receiverNode).range);
      auto initializerSpan = bound.parsedModule().spanFor(tree.node(initializer).range);
      if (!tree.contains(receiverNode) ||
          tree.node(receiverNode).kind != ast::SyntaxKind::IdentExpr || ownerBinding == zc::none ||
          receiverBinding == zc::none || ownerBinding != receiverBinding ||
          initializerTypeIndex == zc::none || initializerAggregateIndex == zc::none ||
          receiverTypeIndex == zc::none || calleeTypeIndex == zc::none || memberIndex == zc::none ||
          checkedCallIndex == zc::none || callKey == zc::none || callSpan == zc::none ||
          receiverSpan == zc::none || initializerSpan == zc::none ||
          !ownerLocalMatches(definitions, ZC_ASSERT_NONNULL(ownerBinding), source.localPattern,
                             tree)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      auto dispatchIndex = dispatchFactIndex(candidate.impl->checkedModule.dispatchFacts().facts(),
                                             ZC_ASSERT_NONNULL(callKey));
      if (dispatchIndex == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t initializerTypeSlot = 0;
      size_t initializerAggregateSlot = 0;
      size_t receiverTypeSlot = 0;
      size_t calleeTypeSlot = 0;
      size_t memberSlot = 0;
      size_t checkedCallSlot = 0;
      size_t dispatchSlot = 0;
      ZC_IF_SOME(value, initializerTypeIndex) { initializerTypeSlot = value; }
      ZC_IF_SOME(value, initializerAggregateIndex) { initializerAggregateSlot = value; }
      ZC_IF_SOME(value, receiverTypeIndex) { receiverTypeSlot = value; }
      ZC_IF_SOME(value, calleeTypeIndex) { calleeTypeSlot = value; }
      ZC_IF_SOME(value, memberIndex) { memberSlot = value; }
      ZC_IF_SOME(value, checkedCallIndex) { checkedCallSlot = value; }
      ZC_IF_SOME(value, dispatchIndex) { dispatchSlot = value; }
      const auto& checkedInitializer = facts.aggregates().entries()[initializerAggregateSlot].value;
      const auto& member = facts.members().entries()[memberSlot].value;
      const auto& invocation = facts.calls().entries()[checkedCallSlot].value.invocation;
      const auto& selected = invocation.selected.variant();
      const auto& dispatch = candidate.impl->checkedModule.dispatchFacts().facts()[dispatchSlot];
      const auto& target = dispatch.fact.target.variant();
      const auto& transform = dispatch.fact.resultTransform.variant();
      bool dispatchOwnerMatches = false;
      ZC_IF_SOME(owner, dispatch.owner) { dispatchOwnerMatches = owner == function.definition; }
      if (ZC_ASSERT_NONNULL(receiverAggregate).type !=
              facts.nodeTypes().entries()[initializerTypeSlot].value ||
          checkedInitializer.node != initializer ||
          checkedInitializer.resultType != ZC_ASSERT_NONNULL(localBinding).type ||
          !sameSpan(checkedInitializer.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan)) ||
          !sameSpan(ZC_ASSERT_NONNULL(localBinding).sourceSpan,
                    ZC_ASSERT_NONNULL(
                        bound.parsedModule().spanFor(tree.node(source.localPattern).range))) ||
          !sameSpan(ZC_ASSERT_NONNULL(ZC_ASSERT_NONNULL(localBinding).initializerSpan),
                    ZC_ASSERT_NONNULL(initializerSpan)) ||
          !sameSpan(ZC_ASSERT_NONNULL(localReference).sourceSpan,
                    ZC_ASSERT_NONNULL(receiverSpan)) ||
          !selected.is<checker::checked::ConcreteMethodCallable>() ||
          !target.is<checker::dispatch::ConcreteMethodTarget>() ||
          !transform.is<checker::dispatch::IdentityResultTransform>() ||
          selected.get<checker::checked::ConcreteMethodCallable>().method != member.member ||
          target.get<checker::dispatch::ConcreteMethodTarget>().method != member.member ||
          member.node != calleeNode ||
          member.receiverType != facts.nodeTypes().entries()[receiverTypeSlot].value ||
          member.memberType != facts.nodeTypes().entries()[calleeTypeSlot].value ||
          member.adjustment != zc::none ||
          ZC_ASSERT_NONNULL(receiverCall).callee != member.member ||
          ZC_ASSERT_NONNULL(receiverCall).calleeType != member.memberType ||
          invocation.calleeType != ZC_ASSERT_NONNULL(receiverCall).calleeType ||
          invocation.successType != function.resultType ||
          invocation.resultType != function.resultType || invocation.receiver == zc::none ||
          invocation.receiverMode == zc::none || invocation.receiverAdjustment == zc::none ||
          invocation.arguments.size() != arguments.size || invocation.substitutions != zc::none ||
          invocation.witnesses != zc::none || invocation.raises != zc::none ||
          !dispatchOwnerMatches || dispatch.fact.receiver == zc::none ||
          dispatch.fact.arguments.size() != arguments.size ||
          dispatch.fact.successType != function.resultType ||
          dispatch.fact.resultType != function.resultType ||
          dispatch.fact.substitutions != zc::none || dispatch.fact.witnesses != zc::none ||
          dispatch.fact.raises != zc::none ||
          !sameSpan(facts.calls().entries()[checkedCallSlot].value.sourceSpan,
                    ZC_ASSERT_NONNULL(callSpan)) ||
          !sameSpan(dispatch.fact.sourceSpan, ZC_ASSERT_NONNULL(callSpan)) ||
          !sameSpan(ZC_ASSERT_NONNULL(receiverCall).sourceSpan, ZC_ASSERT_NONNULL(callSpan))) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      ZC_IF_SOME(receiver, invocation.receiver) {
        ZC_IF_SOME(mode, invocation.receiverMode) {
          ZC_IF_SOME(adjustment, invocation.receiverAdjustment) {
            if (receiver.sourceNode != receiverNode ||
                receiver.sourceType != ZC_ASSERT_NONNULL(receiverCall).receiverSourceType ||
                receiver.parameterType != ZC_ASSERT_NONNULL(receiverCall).receiverType ||
                receiver.adjustment != zc::none ||
                mode != ZC_ASSERT_NONNULL(receiverCall).receiverMode ||
                adjustment.source != ZC_ASSERT_NONNULL(receiverCall).receiverSourceType ||
                adjustment.destination != ZC_ASSERT_NONNULL(receiverCall).receiverType ||
                adjustment.steps.size() != 1 ||
                adjustment.steps[0] != checker::checked::ReceiverAdjustmentStep::BorrowMutable) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
          }
        }
      }
      const auto argumentNodes = tree.list(arguments);
      for (size_t argumentIndex = 0; argumentIndex < argumentNodes.size(); ++argumentIndex) {
        const auto argument = argumentNodes[argumentIndex];
        auto argumentTypeIndex = factIndex(facts.nodeTypes(), argument);
        auto literalIndex = factIndex(facts.literals(), argument);
        auto argumentSpan = bound.parsedModule().spanFor(tree.node(argument).range);
        if (!tree.contains(argument) || !isScalarLiteral(tree.node(argument).kind) ||
            argumentTypeIndex == zc::none || literalIndex == zc::none || argumentSpan == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t argumentTypeSlot = 0;
        size_t literalSlot = 0;
        ZC_IF_SOME(value, argumentTypeIndex) { argumentTypeSlot = value; }
        ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
        const auto& checkedArgument = invocation.arguments[argumentIndex];
        const auto& hirArgument = ZC_ASSERT_NONNULL(receiverCall).arguments[argumentIndex];
        const auto& literal = facts.literals().entries()[literalSlot].value;
        const auto argumentType = facts.nodeTypes().entries()[argumentTypeSlot].value;
        if (checkedArgument.sourceNode != argument || checkedArgument.sourceType != argumentType ||
            checkedArgument.parameterType != argumentType ||
            checkedArgument.adjustment != zc::none || literal.node != argument ||
            literal.type != argumentType || hirArgument.type != argumentType ||
            !sameConstant(hirArgument.value, literal.literal, module, registries, semanticTypes) ||
            !sameSpan(literal.sourceSpan, ZC_ASSERT_NONNULL(argumentSpan)) ||
            !sameSpan(hirArgument.sourceSpan, ZC_ASSERT_NONNULL(argumentSpan))) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
      }
      nextFunction += 7;
      continue;
    }
    zc::Maybe<const HirNominalAggregateExpression&> aggregateExpression;
    for (const auto& aggregate : candidate.impl->aggregates) {
      if (aggregate.node != materializedNode) continue;
      if (aggregateExpression != zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::AdditionalFact, module, registries,
                                            index + 1);
      }
      aggregateExpression = aggregate;
    }
    zc::Maybe<const HirLocalFieldProjectionExpression&> localFieldProjection;
    for (const auto& projection : candidate.impl->localFieldProjections) {
      if (projection.node != valueNode) continue;
      if (localFieldProjection != zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::AdditionalFact, module, registries,
                                            index + 1);
      }
      localFieldProjection = projection;
    }
    zc::Maybe<const HirParameterReferenceExpression&> parameterReference;
    for (const auto& reference : candidate.impl->parameterReferences) {
      const auto expectedReferenceNode = returnsLocal ? materializedNode : valueNode;
      if (reference.node != expectedReferenceNode) continue;
      if (parameterReference != zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::AdditionalFact, module, registries,
                                            index + 1);
      }
      parameterReference = reference;
    }
    zc::Maybe<const HirParameterReborrowExpression&> parameterReborrow;
    for (const auto& reborrow : candidate.impl->parameterReborrows) {
      if (reborrow.node != valueNode) continue;
      if (parameterReborrow != zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::AdditionalFact, module, registries,
                                            index + 1);
      }
      parameterReborrow = reborrow;
    }
    if (localFieldProjection != zc::none && !localHasInitializer && !hasLocalWrite) {
      const bool initializesField = hasLocalWrite;
      if (aggregateExpression != zc::none || localBinding == zc::none ||
          localReference != zc::none || (!initializesField && literalExpression != zc::none) ||
          directCall != zc::none || parameterReference != zc::none ||
          (initializesField &&
           (functionLocalWriteCount != 1 || localWrite == zc::none || writeLiteral == zc::none ||
            ZC_ASSERT_NONNULL(localWrite).field == zc::none ||
            ZC_ASSERT_NONNULL(localWrite).kind != HirLocalWriteKind::Initialize)) ||
          function.node.ordinal() != expectedFunction ||
          block.node.ordinal() != expectedFunction + 1 ||
          ZC_ASSERT_NONNULL(localBinding).node != hirId(expectedFunction + 2) ||
          ZC_ASSERT_NONNULL(localBinding).initializer != zc::none ||
          returnStatement.node != hirId(expectedFunction + 3 + functionLocalWriteCount * 2) ||
          ZC_ASSERT_NONNULL(localFieldProjection).node !=
              hirId(expectedFunction + 4 + functionLocalWriteCount * 2) ||
          function.body != block.node || block.statements.size() != functionLocalWriteCount + 2 ||
          block.statements[0] != ZC_ASSERT_NONNULL(localBinding).node ||
          block.statements[block.statements.size() - 1] != returnStatement.node ||
          returnStatement.value != ZC_ASSERT_NONNULL(localFieldProjection).node ||
          function.resultType != returnStatement.resultType ||
          ZC_ASSERT_NONNULL(localFieldProjection).local != ZC_ASSERT_NONNULL(localBinding).local ||
          ZC_ASSERT_NONNULL(localFieldProjection).receiverType !=
              ZC_ASSERT_NONNULL(localBinding).type ||
          ZC_ASSERT_NONNULL(localFieldProjection).type != function.resultType ||
          ZC_ASSERT_NONNULL(localFieldProjection).category != HirValueCategory::Place ||
          !typeExists(ZC_ASSERT_NONNULL(localBinding).type, semanticTypes) ||
          !typeExists(function.resultType, semanticTypes)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
      if (sourceDefinitionIndex == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t definitionSlot = 0;
      ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
      const auto& sourceDefinition = definitions.definitions()[definitionSlot];
      const auto& tree = bound.tree();
      if (!hasExecutableBody(sourceDefinition, definitions) ||
          sourceDefinition.record.kind() != identity::DefinitionKind::Function ||
          !tree.contains(sourceDefinition.node)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      auto sourceShape = functionReturnShape(tree, tree.node(sourceDefinition.node));
      if (sourceShape == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      FunctionReturnShape source{};
      ZC_IF_SOME(value, sourceShape) { source = value; }
      auto ownerBinding = resolvedOwnerLocal(bound.bindings(), source.localReference);
      auto memberIndex = factIndex(facts.members(), source.value);
      auto placeIndex = factIndex(facts.places(), source.value);
      auto returnType = factIndex(facts.nodeTypes(), source.value);
      auto projectionSpan = bound.parsedModule().spanFor(tree.node(source.value).range);
      if (!source.returnsLocal || !source.returnsLocalField ||
          source.localInitializer != zc::none ||
          source.localWrites.size != functionLocalWriteCount || ownerBinding == zc::none ||
          memberIndex == zc::none || placeIndex == zc::none || returnType == zc::none ||
          projectionSpan == zc::none ||
          !ownerLocalMatches(definitions, ZC_ASSERT_NONNULL(ownerBinding), source.localPattern,
                             tree)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t memberSlot = 0;
      size_t placeSlot = 0;
      size_t returnTypeSlot = 0;
      ZC_IF_SOME(value, memberIndex) { memberSlot = value; }
      ZC_IF_SOME(value, placeIndex) { placeSlot = value; }
      ZC_IF_SOME(value, returnType) { returnTypeSlot = value; }
      const auto& checkedMember = facts.members().entries()[memberSlot].value;
      const auto& checkedPlace = facts.places().entries()[placeSlot].value;
      const auto& checkedRoot = checkedPlace.root.variant();
      if (checkedMember.node != source.value ||
          checkedMember.receiverType != ZC_ASSERT_NONNULL(localBinding).type ||
          checkedMember.member != ZC_ASSERT_NONNULL(localFieldProjection).field ||
          checkedMember.memberType != ZC_ASSERT_NONNULL(localFieldProjection).type ||
          checkedMember.adjustment != zc::none ||
          !checkedRoot.is<checker::checked::OwnerLocalPlaceRoot>() ||
          checkedRoot.get<checker::checked::OwnerLocalPlaceRoot>().binding !=
              ZC_ASSERT_NONNULL(ownerBinding) ||
          checkedPlace.projections.size() != 1 ||
          !checkedPlace.projections[0].variant().is<checker::checked::FieldProjection>() ||
          checkedPlace.projections[0].variant().get<checker::checked::FieldProjection>().field !=
              checkedMember.member ||
          checkedPlace.type != ZC_ASSERT_NONNULL(localFieldProjection).type ||
          !checkedPlace.movable ||
          facts.nodeTypes().entries()[returnTypeSlot].value != function.resultType ||
          !sameSpan(ZC_ASSERT_NONNULL(localFieldProjection).sourceSpan,
                    ZC_ASSERT_NONNULL(projectionSpan))) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      if (initializesField) {
        const auto expectedWrite = hirId(expectedFunction + 3);
        const auto expectedLiteral = hirId(expectedFunction + 4);
        auto sourceStatement = statementItem(tree, tree.list(source.localWrites)[0]);
        if (sourceStatement == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        ast::NodeId statement;
        ZC_IF_SOME(value, sourceStatement) { statement = value; }
        const ast::NodeId assignment(
            tree.node(statement).payload.words[ast::kExpressionStatementExpressionWord]);
        if (tree.node(statement).kind != ast::SyntaxKind::ExpressionStatement ||
            !tree.contains(assignment) ||
            tree.node(assignment).kind != ast::SyntaxKind::AssignmentExpr) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        const ast::NodeId target(tree.node(assignment).payload.words[ast::kAssignmentExprLhsWord]);
        const ast::NodeId value(tree.node(assignment).payload.words[ast::kAssignmentExprRhsWord]);
        auto assignmentType = factIndex(facts.nodeTypes(), assignment);
        auto targetType = factIndex(facts.nodeTypes(), target);
        auto valueType = factIndex(facts.nodeTypes(), value);
        auto valueLiteral = factIndex(facts.literals(), value);
        auto writeMember = factIndex(facts.members(), target);
        auto writePlace = factIndex(facts.places(), target);
        auto assignmentSpan = bound.parsedModule().spanFor(tree.node(assignment).range);
        auto valueSpan = bound.parsedModule().spanFor(tree.node(value).range);
        ast::NodeId targetReference = target;
        if (tree.contains(target) && tree.node(target).kind == ast::SyntaxKind::MemberExpression) {
          targetReference =
              ast::NodeId(tree.node(target).payload.words[ast::kMemberExpressionObjectWord]);
        }
        auto targetBinding = resolvedOwnerLocal(bound.bindings(), targetReference);
        if (!tree.contains(target) || !tree.contains(value) ||
            tree.node(target).kind != ast::SyntaxKind::MemberExpression ||
            assignmentType == zc::none || targetType == zc::none || valueType == zc::none ||
            valueLiteral == zc::none || writeMember == zc::none || writePlace == zc::none ||
            assignmentSpan == zc::none || valueSpan == zc::none || targetBinding == zc::none ||
            targetBinding != ZC_ASSERT_NONNULL(ownerBinding)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t assignmentSlot = 0;
        size_t targetSlot = 0;
        size_t valueSlot = 0;
        size_t literalSlot = 0;
        size_t memberSlot = 0;
        size_t placeSlot = 0;
        ZC_IF_SOME(slot, assignmentType) { assignmentSlot = slot; }
        ZC_IF_SOME(slot, targetType) { targetSlot = slot; }
        ZC_IF_SOME(slot, valueType) { valueSlot = slot; }
        ZC_IF_SOME(slot, valueLiteral) { literalSlot = slot; }
        ZC_IF_SOME(slot, writeMember) { memberSlot = slot; }
        ZC_IF_SOME(slot, writePlace) { placeSlot = slot; }
        const auto& member = facts.members().entries()[memberSlot].value;
        const auto& place = facts.places().entries()[placeSlot].value;
        const auto& root = place.root.variant();
        if (block.statements[1] != expectedWrite ||
            ZC_ASSERT_NONNULL(localWrite).node != expectedWrite ||
            ZC_ASSERT_NONNULL(writeLiteral).node != expectedLiteral ||
            ZC_ASSERT_NONNULL(localWrite).local != ZC_ASSERT_NONNULL(localBinding).local ||
            ZC_ASSERT_NONNULL(localWrite).field != ZC_ASSERT_NONNULL(localFieldProjection).field ||
            ZC_ASSERT_NONNULL(localWrite).value != expectedLiteral ||
            ZC_ASSERT_NONNULL(writeLiteral).type != ZC_ASSERT_NONNULL(localWrite).type ||
            ZC_ASSERT_NONNULL(writeLiteral).category != HirValueCategory::Value ||
            facts.nodeTypes().entries()[assignmentSlot].value !=
                ZC_ASSERT_NONNULL(localWrite).type ||
            facts.nodeTypes().entries()[targetSlot].value != ZC_ASSERT_NONNULL(localWrite).type ||
            facts.nodeTypes().entries()[valueSlot].value != ZC_ASSERT_NONNULL(localWrite).type ||
            facts.literals().entries()[literalSlot].value.type !=
                ZC_ASSERT_NONNULL(localWrite).type ||
            member.node != target || member.member != ZC_ASSERT_NONNULL(localWrite).field ||
            member.memberType != ZC_ASSERT_NONNULL(localWrite).type || !place.mutablePlace ||
            !root.is<checker::checked::OwnerLocalPlaceRoot>() ||
            root.get<checker::checked::OwnerLocalPlaceRoot>().binding !=
                ZC_ASSERT_NONNULL(ownerBinding) ||
            place.projections.size() != 1 ||
            !place.projections[0].variant().is<checker::checked::FieldProjection>() ||
            place.projections[0].variant().get<checker::checked::FieldProjection>().field !=
                ZC_ASSERT_NONNULL(localWrite).field ||
            place.type != ZC_ASSERT_NONNULL(localWrite).type ||
            !sameSpan(ZC_ASSERT_NONNULL(localWrite).sourceSpan,
                      ZC_ASSERT_NONNULL(assignmentSpan)) ||
            !sameSpan(ZC_ASSERT_NONNULL(localWrite).valueSpan, ZC_ASSERT_NONNULL(valueSpan)) ||
            !sameSpan(ZC_ASSERT_NONNULL(writeLiteral).sourceSpan, ZC_ASSERT_NONNULL(valueSpan))) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        nextFunction += 7;
      } else {
        nextFunction += 5;
      }
      continue;
    }
    if (aggregateExpression != zc::none && localFieldProjection == zc::none &&
        localReference != zc::none) {
      if (localBinding == zc::none || literalExpression != zc::none || directCall != zc::none ||
          parameterReference != zc::none || hasLocalWrite || !localHasInitializer ||
          function.node.ordinal() != expectedFunction ||
          block.node.ordinal() != expectedFunction + 1 ||
          ZC_ASSERT_NONNULL(localBinding).node != hirId(expectedFunction + 2) ||
          ZC_ASSERT_NONNULL(localBinding).initializer != hirId(expectedFunction + 3) ||
          ZC_ASSERT_NONNULL(aggregateExpression).node != hirId(expectedFunction + 3) ||
          returnStatement.node != hirId(expectedFunction + 4) ||
          ZC_ASSERT_NONNULL(localReference).node != hirId(expectedFunction + 5) ||
          function.body != block.node || block.statements.size() != 2 ||
          block.statements[0] != ZC_ASSERT_NONNULL(localBinding).node ||
          block.statements[1] != returnStatement.node ||
          returnStatement.value != ZC_ASSERT_NONNULL(localReference).node ||
          function.resultType != returnStatement.resultType ||
          ZC_ASSERT_NONNULL(localReference).local != ZC_ASSERT_NONNULL(localBinding).local ||
          ZC_ASSERT_NONNULL(localReference).type != function.resultType ||
          ZC_ASSERT_NONNULL(localReference).category != HirValueCategory::Place ||
          ZC_ASSERT_NONNULL(aggregateExpression).type != ZC_ASSERT_NONNULL(localBinding).type ||
          ZC_ASSERT_NONNULL(aggregateExpression).category != HirValueCategory::Value ||
          !typeExists(ZC_ASSERT_NONNULL(localBinding).type, semanticTypes) ||
          !typeExists(function.resultType, semanticTypes)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
      if (sourceDefinitionIndex == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t definitionSlot = 0;
      ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
      const auto& sourceDefinition = definitions.definitions()[definitionSlot];
      const auto& tree = bound.tree();
      if (!hasExecutableBody(sourceDefinition, definitions) ||
          sourceDefinition.record.kind() != identity::DefinitionKind::Function ||
          !tree.contains(sourceDefinition.node)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      auto sourceShape = functionReturnShape(tree, tree.node(sourceDefinition.node));
      if (sourceShape == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      FunctionReturnShape source{};
      ZC_IF_SOME(value, sourceShape) { source = value; }
      ast::NodeId initializer;
      ZC_IF_SOME(value, source.localInitializer) { initializer = value; }
      auto ownerBinding = resolvedOwnerLocal(bound.bindings(), source.localReference);
      auto aggregateIndex = factIndex(facts.aggregates(), initializer);
      auto initializerType = factIndex(facts.nodeTypes(), initializer);
      auto returnType = factIndex(facts.nodeTypes(), source.value);
      auto aggregateSpan = bound.parsedModule().spanFor(tree.node(initializer).range);
      auto returnValueSpan = bound.parsedModule().spanFor(tree.node(source.value).range);
      if (!source.returnsLocal || source.returnsLocalField || source.localWrites.size != 0 ||
          ownerBinding == zc::none || aggregateIndex == zc::none || initializerType == zc::none ||
          returnType == zc::none || aggregateSpan == zc::none || returnValueSpan == zc::none ||
          !ownerLocalMatches(definitions, ZC_ASSERT_NONNULL(ownerBinding), source.localPattern,
                             tree)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t aggregateSlot = 0;
      size_t initializerTypeSlot = 0;
      size_t returnTypeSlot = 0;
      ZC_IF_SOME(value, aggregateIndex) { aggregateSlot = value; }
      ZC_IF_SOME(value, initializerType) { initializerTypeSlot = value; }
      ZC_IF_SOME(value, returnType) { returnTypeSlot = value; }
      const auto& checkedAggregate = facts.aggregates().entries()[aggregateSlot].value;
      if (checkedAggregate.node != initializer ||
          !checkedAggregate.kind.variant().is<checker::checked::NominalAggregate>() ||
          checkedAggregate.kind.variant().get<checker::checked::NominalAggregate>().definition !=
              ZC_ASSERT_NONNULL(aggregateExpression).definition ||
          checkedAggregate.resultType != ZC_ASSERT_NONNULL(aggregateExpression).type ||
          checkedAggregate.resultType != facts.nodeTypes().entries()[initializerTypeSlot].value ||
          !sameSpan(checkedAggregate.sourceSpan, ZC_ASSERT_NONNULL(aggregateSpan)) ||
          facts.nodeTypes().entries()[returnTypeSlot].value != function.resultType ||
          !sameSpan(ZC_ASSERT_NONNULL(localReference).sourceSpan,
                    ZC_ASSERT_NONNULL(returnValueSpan))) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      if (checkedAggregate.elements.size() !=
          ZC_ASSERT_NONNULL(aggregateExpression).elements.size()) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      for (size_t elementIndex = 0; elementIndex < checkedAggregate.elements.size();
           ++elementIndex) {
        const auto& checkedElement = checkedAggregate.elements[elementIndex];
        const auto& aggregateElement =
            ZC_ASSERT_NONNULL(aggregateExpression).elements[elementIndex];
        auto literalIndex = factIndex(facts.literals(), checkedElement.sourceNode);
        if (checkedElement.field == zc::none || checkedElement.index != elementIndex ||
            checkedElement.sourceType != checkedElement.destinationType ||
            checkedElement.adjustment != zc::none || literalIndex == zc::none ||
            ZC_ASSERT_NONNULL(checkedElement.field) != aggregateElement.field ||
            checkedElement.destinationType != aggregateElement.type) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        size_t literalSlot = 0;
        ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
        const auto& literal = facts.literals().entries()[literalSlot].value;
        if (literal.type != aggregateElement.type ||
            !sameConstant(literal.literal, aggregateElement.value, module, registries,
                          semanticTypes) ||
            !sameSpan(literal.sourceSpan, aggregateElement.sourceSpan)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
      }
      nextFunction += 6;
      continue;
    }
    if (aggregateExpression != zc::none) {
      const bool aggregateFieldOverwrite = hasLocalWrite;
      if (aggregateExpression == zc::none || localFieldProjection == zc::none ||
          localBinding == zc::none || localReference != zc::none || literalExpression != zc::none ||
          directCall != zc::none || parameterReference != zc::none || !localHasInitializer ||
          (aggregateFieldOverwrite && functionLocalWriteCount == 0) ||
          function.node.ordinal() != expectedFunction ||
          block.node.ordinal() != expectedFunction + 1 ||
          ZC_ASSERT_NONNULL(localBinding).node != hirId(expectedFunction + 2) ||
          ZC_ASSERT_NONNULL(localBinding).initializer != hirId(expectedFunction + 3) ||
          ZC_ASSERT_NONNULL(aggregateExpression).node != hirId(expectedFunction + 3) ||
          returnStatement.node != hirId(expectedFunction + 4 + functionLocalWriteCount * 2) ||
          ZC_ASSERT_NONNULL(localFieldProjection).node !=
              hirId(expectedFunction + 5 + functionLocalWriteCount * 2) ||
          function.body != block.node || block.statements.size() != functionLocalWriteCount + 2 ||
          block.statements[0] != ZC_ASSERT_NONNULL(localBinding).node ||
          block.statements[functionLocalWriteCount + 1] != returnStatement.node ||
          returnStatement.value != ZC_ASSERT_NONNULL(localFieldProjection).node ||
          function.resultType != returnStatement.resultType ||
          ZC_ASSERT_NONNULL(localFieldProjection).local != ZC_ASSERT_NONNULL(localBinding).local ||
          ZC_ASSERT_NONNULL(localFieldProjection).receiverType !=
              ZC_ASSERT_NONNULL(localBinding).type ||
          ZC_ASSERT_NONNULL(localFieldProjection).type != function.resultType ||
          ZC_ASSERT_NONNULL(localFieldProjection).category != HirValueCategory::Place ||
          ZC_ASSERT_NONNULL(aggregateExpression).type != ZC_ASSERT_NONNULL(localBinding).type ||
          ZC_ASSERT_NONNULL(aggregateExpression).category != HirValueCategory::Value ||
          !typeExists(ZC_ASSERT_NONNULL(localBinding).type, semanticTypes) ||
          !typeExists(function.resultType, semanticTypes)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
      if (sourceDefinitionIndex == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t definitionSlot = 0;
      ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
      const auto& sourceDefinition = definitions.definitions()[definitionSlot];
      const auto& tree = bound.tree();
      if (!hasExecutableBody(sourceDefinition, definitions) ||
          sourceDefinition.record.kind() != identity::DefinitionKind::Function ||
          !tree.contains(sourceDefinition.node)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      auto sourceShape = functionReturnShape(tree, tree.node(sourceDefinition.node));
      if (sourceShape == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      FunctionReturnShape source{};
      ZC_IF_SOME(value, sourceShape) { source = value; }
      ast::NodeId initializer;
      ZC_IF_SOME(value, source.localInitializer) { initializer = value; }
      auto ownerBinding = resolvedOwnerLocal(bound.bindings(), source.localReference);
      auto aggregateIndex = factIndex(facts.aggregates(), initializer);
      auto memberIndex = factIndex(facts.members(), source.value);
      auto placeIndex = factIndex(facts.places(), source.value);
      auto initializerType = factIndex(facts.nodeTypes(), initializer);
      auto returnType = factIndex(facts.nodeTypes(), source.value);
      auto aggregateSpan = bound.parsedModule().spanFor(tree.node(initializer).range);
      auto projectionSpan = bound.parsedModule().spanFor(tree.node(source.value).range);
      if (!source.returnsLocal || !source.returnsLocalField ||
          source.localWrites.size != functionLocalWriteCount || ownerBinding == zc::none ||
          aggregateIndex == zc::none || memberIndex == zc::none || placeIndex == zc::none ||
          initializerType == zc::none || returnType == zc::none || aggregateSpan == zc::none ||
          projectionSpan == zc::none ||
          !ownerLocalMatches(definitions, ZC_ASSERT_NONNULL(ownerBinding), source.localPattern,
                             tree)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t aggregateSlot = 0;
      size_t memberSlot = 0;
      size_t placeSlot = 0;
      size_t initializerTypeSlot = 0;
      size_t returnTypeSlot = 0;
      ZC_IF_SOME(value, aggregateIndex) { aggregateSlot = value; }
      ZC_IF_SOME(value, memberIndex) { memberSlot = value; }
      ZC_IF_SOME(value, placeIndex) { placeSlot = value; }
      ZC_IF_SOME(value, initializerType) { initializerTypeSlot = value; }
      ZC_IF_SOME(value, returnType) { returnTypeSlot = value; }
      const auto& checkedAggregate = facts.aggregates().entries()[aggregateSlot].value;
      const auto& checkedMember = facts.members().entries()[memberSlot].value;
      const auto& checkedPlace = facts.places().entries()[placeSlot].value;
      const auto& checkedRoot = checkedPlace.root.variant();
      if (checkedAggregate.node != initializer ||
          !checkedAggregate.kind.variant().is<checker::checked::NominalAggregate>() ||
          checkedAggregate.kind.variant().get<checker::checked::NominalAggregate>().definition !=
              ZC_ASSERT_NONNULL(aggregateExpression).definition ||
          checkedAggregate.resultType != ZC_ASSERT_NONNULL(aggregateExpression).type ||
          checkedAggregate.resultType != facts.nodeTypes().entries()[initializerTypeSlot].value ||
          !sameSpan(checkedAggregate.sourceSpan, ZC_ASSERT_NONNULL(aggregateSpan)) ||
          checkedMember.node != source.value ||
          checkedMember.receiverType != ZC_ASSERT_NONNULL(localBinding).type ||
          checkedMember.member != ZC_ASSERT_NONNULL(localFieldProjection).field ||
          checkedMember.memberType != ZC_ASSERT_NONNULL(localFieldProjection).type ||
          checkedMember.adjustment != zc::none ||
          !checkedRoot.is<checker::checked::OwnerLocalPlaceRoot>() ||
          checkedRoot.get<checker::checked::OwnerLocalPlaceRoot>().binding !=
              ZC_ASSERT_NONNULL(ownerBinding) ||
          checkedPlace.projections.size() != 1 ||
          !checkedPlace.projections[0].variant().is<checker::checked::FieldProjection>() ||
          checkedPlace.projections[0].variant().get<checker::checked::FieldProjection>().field !=
              checkedMember.member ||
          checkedPlace.type != ZC_ASSERT_NONNULL(localFieldProjection).type ||
          !checkedPlace.movable ||
          facts.nodeTypes().entries()[returnTypeSlot].value != function.resultType ||
          !sameSpan(ZC_ASSERT_NONNULL(localFieldProjection).sourceSpan,
                    ZC_ASSERT_NONNULL(projectionSpan))) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      if (checkedAggregate.elements.size() !=
          ZC_ASSERT_NONNULL(aggregateExpression).elements.size()) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      for (size_t elementIndex = 0; elementIndex < checkedAggregate.elements.size();
           ++elementIndex) {
        const auto& checkedElement = checkedAggregate.elements[elementIndex];
        const auto& aggregateElement =
            ZC_ASSERT_NONNULL(aggregateExpression).elements[elementIndex];
        auto literalIndex = factIndex(facts.literals(), checkedElement.sourceNode);
        if (checkedElement.field == zc::none || checkedElement.index != elementIndex ||
            checkedElement.sourceType != checkedElement.destinationType ||
            checkedElement.adjustment != zc::none || literalIndex == zc::none ||
            ZC_ASSERT_NONNULL(checkedElement.field) != aggregateElement.field ||
            checkedElement.destinationType != aggregateElement.type) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        size_t literalSlot = 0;
        ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
        const auto& literal = facts.literals().entries()[literalSlot].value;
        if (literal.type != aggregateElement.type ||
            !sameConstant(literal.literal, aggregateElement.value, module, registries,
                          semanticTypes) ||
            !sameSpan(literal.sourceSpan, aggregateElement.sourceSpan)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
      }
      if (aggregateFieldOverwrite) {
        for (size_t writeIndex = 0; writeIndex < functionLocalWriteCount; ++writeIndex) {
          auto sourceStatement = statementItem(tree, tree.list(source.localWrites)[writeIndex]);
          if (sourceStatement == zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::MissingRequiredFact, module,
                                                registries, index + 1);
          }
          ast::NodeId statement;
          ZC_IF_SOME(value, sourceStatement) { statement = value; }
          const ast::NodeId assignment(
              tree.node(statement).payload.words[ast::kExpressionStatementExpressionWord]);
          if (tree.node(statement).kind != ast::SyntaxKind::ExpressionStatement ||
              !tree.contains(assignment) ||
              tree.node(assignment).kind != ast::SyntaxKind::AssignmentExpr) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          const ast::NodeId target(
              tree.node(assignment).payload.words[ast::kAssignmentExprLhsWord]);
          const ast::NodeId value(tree.node(assignment).payload.words[ast::kAssignmentExprRhsWord]);
          auto assignmentType = factIndex(facts.nodeTypes(), assignment);
          auto targetType = factIndex(facts.nodeTypes(), target);
          auto valueType = factIndex(facts.nodeTypes(), value);
          auto valueLiteral = factIndex(facts.literals(), value);
          auto writeMember = factIndex(facts.members(), target);
          auto writePlace = factIndex(facts.places(), target);
          auto assignmentSpan = bound.parsedModule().spanFor(tree.node(assignment).range);
          auto valueSpan = bound.parsedModule().spanFor(tree.node(value).range);
          const auto expectedWrite = hirId(expectedFunction + 4 + writeIndex * 2);
          const auto expectedLiteral = hirId(expectedWrite.ordinal() + 1);
          zc::Maybe<const HirLocalWriteStatement&> write;
          zc::Maybe<const HirScalarLiteralExpression&> literal;
          for (const auto& candidateWrite : candidate.impl->localWrites) {
            if (candidateWrite.node != expectedWrite) continue;
            if (write != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            write = candidateWrite;
          }
          for (const auto& expression : candidate.impl->expressions) {
            if (expression.node != expectedLiteral) continue;
            if (literal != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            literal = expression;
          }
          if (!tree.contains(target) || !tree.contains(value) ||
              tree.node(target).kind != ast::SyntaxKind::MemberExpression ||
              assignmentType == zc::none || targetType == zc::none || valueType == zc::none ||
              valueLiteral == zc::none || writeMember == zc::none || writePlace == zc::none ||
              assignmentSpan == zc::none || valueSpan == zc::none || write == zc::none ||
              literal == zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::MissingRequiredFact, module,
                                                registries, index + 1);
          }
          size_t assignmentSlot = 0;
          size_t targetSlot = 0;
          size_t valueSlot = 0;
          size_t literalSlot = 0;
          size_t memberSlot = 0;
          size_t placeSlot = 0;
          ZC_IF_SOME(slot, assignmentType) { assignmentSlot = slot; }
          ZC_IF_SOME(slot, targetType) { targetSlot = slot; }
          ZC_IF_SOME(slot, valueType) { valueSlot = slot; }
          ZC_IF_SOME(slot, valueLiteral) { literalSlot = slot; }
          ZC_IF_SOME(slot, writeMember) { memberSlot = slot; }
          ZC_IF_SOME(slot, writePlace) { placeSlot = slot; }
          const auto& member = facts.members().entries()[memberSlot].value;
          const auto& place = facts.places().entries()[placeSlot].value;
          const auto& root = place.root.variant();
          const auto& writeValue = ZC_ASSERT_NONNULL(write);
          const auto& literalValue = ZC_ASSERT_NONNULL(literal);
          if (writeValue.local != ZC_ASSERT_NONNULL(localBinding).local ||
              writeValue.field == zc::none || writeValue.kind != HirLocalWriteKind::Overwrite ||
              writeValue.value != literalValue.node || literalValue.type != writeValue.type ||
              literalValue.category != HirValueCategory::Value ||
              facts.nodeTypes().entries()[assignmentSlot].value != writeValue.type ||
              facts.nodeTypes().entries()[targetSlot].value != writeValue.type ||
              facts.nodeTypes().entries()[valueSlot].value != writeValue.type ||
              facts.literals().entries()[literalSlot].value.type != writeValue.type ||
              member.node != target || member.member != ZC_ASSERT_NONNULL(writeValue.field) ||
              member.memberType != writeValue.type || !place.mutablePlace ||
              !root.is<checker::checked::OwnerLocalPlaceRoot>() ||
              root.get<checker::checked::OwnerLocalPlaceRoot>().binding !=
                  ZC_ASSERT_NONNULL(ownerBinding) ||
              place.projections.size() != 1 ||
              !place.projections[0].variant().is<checker::checked::FieldProjection>() ||
              place.projections[0].variant().get<checker::checked::FieldProjection>().field !=
                  ZC_ASSERT_NONNULL(writeValue.field) ||
              place.type != writeValue.type ||
              !sameSpan(writeValue.sourceSpan, ZC_ASSERT_NONNULL(assignmentSpan)) ||
              !sameSpan(writeValue.valueSpan, ZC_ASSERT_NONNULL(valueSpan)) ||
              !sameSpan(literalValue.sourceSpan, ZC_ASSERT_NONNULL(valueSpan))) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
      }
      nextFunction += 6 + functionLocalWriteCount * 2;
      continue;
    }
    const bool uninitializedLocal = returnsLocal && !localHasInitializer && !hasLocalWrite;
    const bool returnsParameter = parameterReference != zc::none && !returnsLocal;
    const bool returnsParameterReborrow = parameterReborrow != zc::none && !returnsLocal;
    const bool returnsLocalAliasReborrow = parameterReborrow != zc::none && returnsLocal;
    const bool initializesFromParameter = parameterReference != zc::none && returnsLocal;
    if (returnsParameterReborrow) {
      if (parameterReference != zc::none || literalExpression != zc::none ||
          directCall != zc::none || localReference != zc::none ||
          localFieldProjection != zc::none || aggregateExpression != zc::none || hasLocalWrite ||
          function.node.ordinal() != expectedFunction ||
          block.node.ordinal() != expectedFunction + 1 || returnStatement.node != returnNode ||
          function.body != block.node || block.statements.size() != 1 ||
          block.statements[0] != returnStatement.node || returnStatement.value != valueNode ||
          function.resultType != returnStatement.resultType ||
          ZC_ASSERT_NONNULL(parameterReborrow).type != function.resultType) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
    }
    if ((!uninitializedLocal && !returnsParameter && !returnsParameterReborrow &&
         !initializesFromParameter &&
         (literalExpression == zc::none) == (directCall == zc::none)) ||
        (uninitializedLocal && (literalExpression != zc::none || directCall != zc::none)) ||
        (returnsParameter &&
         (returnsLocal || literalExpression != zc::none || directCall != zc::none)) ||
        (initializesFromParameter &&
         (literalExpression != zc::none || directCall != zc::none ||
          (localReference == zc::none && !returnsLocalAliasReborrow))) ||
        (hasLocalWrite && (returnsLocal == false || writeLiteral == zc::none ||
                           (localHasInitializer &&
                            ZC_ASSERT_NONNULL(localWrite).kind != HirLocalWriteKind::Overwrite) ||
                           (!localHasInitializer && ZC_ASSERT_NONNULL(localWrite).kind !=
                                                        HirLocalWriteKind::Initialize))) ||
        (returnsLocal && !returnsLocalAliasReborrow && localReference == zc::none &&
         localFieldProjection == zc::none && localBorrow == zc::none) ||
        function.node.ordinal() != expectedFunction ||
        block.node.ordinal() != expectedFunction + 1 || returnStatement.node != returnNode ||
        function.body != block.node ||
        block.statements.size() != (returnsLocal ? functionLocalWriteCount + 2 : 1) ||
        !writesMatchBlock ||
        block.statements[block.statements.size() - 1] != returnStatement.node ||
        returnStatement.value != valueNode || function.resultType != returnStatement.resultType ||
        !typeExists(function.resultType, semanticTypes)) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }

    auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
    auto signaturePosition = signatureIndex(signatures.definitions.asPtr(), function.definition);
    auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), function.definition);
    if (sourceDefinitionIndex == zc::none || signaturePosition == zc::none ||
        rootPosition == zc::none) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::MissingRequiredFact, module,
                                          registries, index + 1);
    }
    size_t definitionSlot = 0;
    size_t signatureSlot = 0;
    size_t rootSlot = 0;
    ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
    ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
    ZC_IF_SOME(value, rootPosition) { rootSlot = value; }
    const auto& sourceDefinition = definitions.definitions()[definitionSlot];
    const auto& tree = bound.tree();
    if (!hasExecutableBody(sourceDefinition, definitions) ||
        !definitionBelongsToModule(sourceDefinition, definitions) ||
        sourceDefinition.record.kind() != identity::DefinitionKind::Function ||
        !sourceDefinition.site.value().is<binder::DeclarationDefinitionSite>() ||
        !tree.contains(sourceDefinition.node) ||
        tree.node(sourceDefinition.node).kind != ast::SyntaxKind::FunctionDecl) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    auto sourceShape = functionReturnShape(tree, tree.node(sourceDefinition.node));
    if (sourceShape == zc::none) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::MissingRequiredFact, module,
                                          registries, index + 1);
    }
    FunctionReturnShape source{};
    ZC_IF_SOME(value, sourceShape) { source = value; }
    auto bodySpan = bound.parsedModule().spanFor(tree.node(source.body).range);
    auto returnSpan = bound.parsedModule().spanFor(tree.node(source.returnStatement).range);
    if (bodySpan == zc::none || returnSpan == zc::none || source.returnsLocal != returnsLocal) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::MissingRequiredFact, module,
                                          registries, index + 1);
    }
    if ((source.unsafeBlock != zc::none) != (function.unsafeBlock != zc::none)) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    const auto sourceReturnValueNode = source.value;
    ast::NodeId sourceValueNode = sourceReturnValueNode;
    ZC_IF_SOME(initializer, source.localInitializer) { sourceValueNode = initializer; }
    if (source.localWrites.size != functionLocalWriteCount) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    for (size_t writeIndex = 0; writeIndex < source.localWrites.size; ++writeIndex) {
      auto sourceStatement = statementItem(tree, tree.list(source.localWrites)[writeIndex]);
      if (sourceStatement == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      ast::NodeId sourceStatementNode;
      ZC_IF_SOME(value, sourceStatement) { sourceStatementNode = value; }
      const ast::NodeId sourceWrite(
          tree.node(sourceStatementNode).payload.words[ast::kExpressionStatementExpressionWord]);
      const ast::NodeId sourceTarget(
          tree.node(sourceWrite).payload.words[ast::kAssignmentExprLhsWord]);
      const ast::NodeId sourceWriteValue(
          tree.node(sourceWrite).payload.words[ast::kAssignmentExprRhsWord]);
      auto sourceWriteSpan = bound.parsedModule().spanFor(tree.node(sourceWrite).range);
      auto sourceValueSpan = bound.parsedModule().spanFor(tree.node(sourceWriteValue).range);
      auto assignmentType = factIndex(facts.nodeTypes(), sourceWrite);
      auto targetType = factIndex(facts.nodeTypes(), sourceTarget);
      auto valueType = factIndex(facts.nodeTypes(), sourceWriteValue);
      auto valueLiteral = factIndex(facts.literals(), sourceWriteValue);
      ast::NodeId sourceTargetReference = sourceTarget;
      if (tree.contains(sourceTarget) &&
          tree.node(sourceTarget).kind == ast::SyntaxKind::MemberExpression) {
        sourceTargetReference =
            ast::NodeId(tree.node(sourceTarget).payload.words[ast::kMemberExpressionObjectWord]);
      }
      auto targetBinding = resolvedOwnerLocal(bound.bindings(), sourceTargetReference);
      ast::NodeId sourceReturnReference = sourceReturnValueNode;
      if (tree.node(sourceReturnValueNode).kind == ast::SyntaxKind::MemberExpression) {
        sourceReturnReference = ast::NodeId(
            tree.node(sourceReturnValueNode).payload.words[ast::kMemberExpressionObjectWord]);
      }
      auto returnBinding = resolvedOwnerLocal(bound.bindings(), sourceReturnReference);
      if (sourceWriteSpan == zc::none || sourceValueSpan == zc::none ||
          assignmentType == zc::none || targetType == zc::none || valueType == zc::none ||
          valueLiteral == zc::none || targetBinding == zc::none || returnBinding == zc::none ||
          targetBinding != returnBinding) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t assignmentSlot = 0;
      size_t targetSlot = 0;
      size_t valueSlot = 0;
      size_t literalSlot = 0;
      ZC_IF_SOME(value, assignmentType) { assignmentSlot = value; }
      ZC_IF_SOME(value, targetType) { targetSlot = value; }
      ZC_IF_SOME(value, valueType) { valueSlot = value; }
      ZC_IF_SOME(value, valueLiteral) { literalSlot = value; }
      const auto expectedWrite =
          hirId(expectedFunction + (localHasInitializer ? 4 : 3) + writeIndex * 2);
      const auto expectedValue = hirId(expectedWrite.ordinal() + 1);
      zc::Maybe<const HirLocalWriteStatement&> write;
      zc::Maybe<const HirScalarLiteralExpression&> literal;
      for (const auto& candidateWrite : candidate.impl->localWrites) {
        if (candidateWrite.node != expectedWrite) continue;
        if (write != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::AdditionalFact, module, registries,
                                              index + 1);
        }
        write = candidateWrite;
      }
      for (const auto& expression : candidate.impl->expressions) {
        if (expression.node != expectedValue) continue;
        if (literal != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::AdditionalFact, module, registries,
                                              index + 1);
        }
        literal = expression;
      }
      if (write == zc::none || literal == zc::none || localBinding == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      ZC_IF_SOME(writeValue, write) {
        ZC_IF_SOME(literalValue, literal) {
          ZC_IF_SOME(local, localBinding) {
            const auto& assignmentFact = facts.nodeTypes().entries()[assignmentSlot].value;
            const auto& targetFact = facts.nodeTypes().entries()[targetSlot].value;
            const auto& valueFact = facts.nodeTypes().entries()[valueSlot].value;
            const auto& literalFact = facts.literals().entries()[literalSlot].value;
            bool firstFieldWrite = writeValue.field != zc::none;
            if (firstFieldWrite) {
              for (const auto& previous : candidate.impl->localWrites) {
                if (previous.node.ordinal() >= expectedWrite.ordinal() ||
                    previous.field != writeValue.field) {
                  continue;
                }
                firstFieldWrite = false;
                break;
              }
            }
            const auto expectedKind =
                !localHasInitializer &&
                        (writeValue.field != zc::none ? firstFieldWrite : writeIndex == 0)
                    ? HirLocalWriteKind::Initialize
                    : HirLocalWriteKind::Overwrite;
            if (writeValue.local != local.local ||
                (writeValue.field == zc::none && writeValue.type != local.type) ||
                writeValue.value != literalValue.node || literalValue.type != writeValue.type ||
                literalValue.category != HirValueCategory::Value ||
                assignmentFact != writeValue.type || targetFact != writeValue.type ||
                valueFact != writeValue.type || literalFact.type != writeValue.type ||
                writeValue.kind != expectedKind ||
                !sameConstant(literalValue.value, literalFact.literal, module, registries,
                              semanticTypes) ||
                !sameSpan(writeValue.sourceSpan, ZC_ASSERT_NONNULL(sourceWriteSpan)) ||
                !sameSpan(writeValue.valueSpan, ZC_ASSERT_NONNULL(sourceValueSpan)) ||
                !sameSpan(literalValue.sourceSpan, ZC_ASSERT_NONNULL(sourceValueSpan)) ||
                !sameSpan(literalFact.sourceSpan, ZC_ASSERT_NONNULL(sourceValueSpan))) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
          }
        }
      }
    }
    if (localFieldProjection != zc::none && !localHasInitializer && hasLocalWrite) {
      nextFunction += static_cast<uint32_t>(5 + functionLocalWriteCount * 2);
      continue;
    }
    const auto& signature = signatures.definitions[signatureSlot];
    const auto& root = signatures.roots[rootSlot];
    if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
        !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>()) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    const auto& callable = signature.payload.variant().get<checker::signature::CallableSignature>();
    auto expectedVisibility = visibility(root.visibility);
    auto expectedLinkage = linkage(callable);
    bool visibilityMatches = false;
    bool linkageMatches = false;
    bool bodySpanMatches = false;
    bool returnSpanMatches = false;
    ZC_IF_SOME(value, expectedVisibility) {
      visibilityMatches = sameVisibility(function.visibility, value);
    }
    ZC_IF_SOME(value, expectedLinkage) { linkageMatches = function.linkage == value; }
    ZC_IF_SOME(value, bodySpan) { bodySpanMatches = sameSpan(block.sourceSpan, value); }
    ZC_IF_SOME(value, returnSpan) {
      returnSpanMatches = sameSpan(returnStatement.sourceSpan, value);
    }
    if (signature.definition != function.definition ||
        signature.definitionKind != identity::DefinitionKind::Function ||
        root.canonicalDefinition != function.definition || root.sourceModule != module ||
        callable.receiver != zc::none || callable.raises != zc::none ||
        callable.success != function.resultType ||
        !sameSpan(function.sourceSpan, sourceDefinition.source) ||
        !sameSpan(signature.declarationSpan, sourceDefinition.source) || !visibilityMatches ||
        !linkageMatches || !bodySpanMatches || !returnSpanMatches) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }

    const ast::NodeId parameterListNode(
        tree.node(sourceDefinition.node).payload.words[ast::kFunctionDeclParamsIdWord]);
    if (!tree.contains(parameterListNode) ||
        tree.node(parameterListNode).kind != ast::SyntaxKind::FunctionParameterList) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    const auto& parameterList = tree.node(parameterListNode);
    const ast::NodeList parameterNodes{
        parameterList.payload.words[ast::kFunctionParameterListParamsFirstWord],
        parameterList.payload.words[ast::kFunctionParameterListParamsSizeWord]};
    if (!tree.contains(parameterNodes) ||
        function.parameters.size() != callable.parameters.size() ||
        function.parameters.size() != parameterNodes.size) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::MissingRequiredFact, module,
                                          registries, index + 1);
    }
    for (size_t parameterIndex = 0; parameterIndex < function.parameters.size(); ++parameterIndex) {
      const auto parameterNode = tree.list(parameterNodes)[parameterIndex];
      if (!tree.contains(parameterNode) ||
          tree.node(parameterNode).kind != ast::SyntaxKind::FunctionParameterDecl) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      auto parameterSpan = bound.parsedModule().spanFor(tree.node(parameterNode).range);
      const auto& parameter = function.parameters[parameterIndex];
      const auto& signatureParameter = callable.parameters[parameterIndex];
      if (parameterSpan == zc::none || parameter.key != signatureParameter.parameter ||
          parameter.type != signatureParameter.type || signatureParameter.hasDefault ||
          !typeExists(parameter.type, semanticTypes) ||
          !sameSpan(parameter.sourceSpan, ZC_ASSERT_NONNULL(parameterSpan))) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
    }

    if (returnsLocalAliasReborrow) {
      ast::NodeId initializer;
      ZC_IF_SOME(value, source.localInitializer) { initializer = value; }
      auto alias = reborrowReference(tree, sourceReturnValueNode);
      if (source.localInitializer == zc::none || alias == zc::none ||
          parameterReference == zc::none || literalExpression != zc::none ||
          directCall != zc::none || localReference != zc::none ||
          localFieldProjection != zc::none || aggregateExpression != zc::none || hasLocalWrite ||
          function.node.ordinal() != expectedFunction ||
          block.node.ordinal() != expectedFunction + 1 || returnStatement.node != returnNode ||
          function.body != block.node || block.statements.size() != 2 ||
          block.statements[0] != ZC_ASSERT_NONNULL(localBinding).node ||
          block.statements[1] != returnStatement.node || returnStatement.value != valueNode ||
          function.resultType != returnStatement.resultType ||
          ZC_ASSERT_NONNULL(localBinding).node != hirId(expectedFunction + 2) ||
          ZC_ASSERT_NONNULL(localBinding).initializer != hirId(expectedFunction + 3) ||
          ZC_ASSERT_NONNULL(localBinding).local != hirLocalId(1) ||
          ZC_ASSERT_NONNULL(parameterReference).node != hirId(expectedFunction + 3) ||
          ZC_ASSERT_NONNULL(parameterReborrow).node != valueNode ||
          ZC_ASSERT_NONNULL(parameterReborrow).sourceAlias == zc::none ||
          ZC_ASSERT_NONNULL(parameterReborrow).type != function.resultType ||
          ZC_ASSERT_NONNULL(parameterReborrow).sourceType != ZC_ASSERT_NONNULL(localBinding).type) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      ZC_IF_SOME(sourceAlias, ZC_ASSERT_NONNULL(parameterReborrow).sourceAlias) {
        if (sourceAlias != ZC_ASSERT_NONNULL(localBinding).local) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
      }
      auto aliasBinding = resolvedOwnerLocal(bound.bindings(), ZC_ASSERT_NONNULL(alias));
      auto parameterBinding = resolvedCallableParameter(bound.bindings(), initializer);
      auto initializerType = factIndex(facts.nodeTypes(), initializer);
      auto aliasType = factIndex(facts.nodeTypes(), ZC_ASSERT_NONNULL(alias));
      auto returnType = factIndex(facts.nodeTypes(), sourceReturnValueNode);
      auto patternSpan = bound.parsedModule().spanFor(tree.node(source.localPattern).range);
      auto initializerSpan = bound.parsedModule().spanFor(tree.node(initializer).range);
      auto returnSpan = bound.parsedModule().spanFor(tree.node(sourceReturnValueNode).range);
      if (aliasBinding == zc::none || parameterBinding == zc::none || initializerType == zc::none ||
          aliasType == zc::none || returnType == zc::none || patternSpan == zc::none ||
          initializerSpan == zc::none || returnSpan == zc::none ||
          !ownerLocalMatches(definitions, ZC_ASSERT_NONNULL(aliasBinding), source.localPattern,
                             tree)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t initializerSlot = 0;
      size_t aliasSlot = 0;
      size_t returnSlot = 0;
      ZC_IF_SOME(value, initializerType) { initializerSlot = value; }
      ZC_IF_SOME(value, aliasType) { aliasSlot = value; }
      ZC_IF_SOME(value, returnType) { returnSlot = value; }
      auto parameterAuthority = registries.callableParameter(ZC_ASSERT_NONNULL(parameterBinding));
      if (parameterAuthority == zc::none ||
          facts.nodeTypes().entries()[initializerSlot].value !=
              ZC_ASSERT_NONNULL(localBinding).type ||
          facts.nodeTypes().entries()[aliasSlot].value != ZC_ASSERT_NONNULL(localBinding).type ||
          facts.nodeTypes().entries()[returnSlot].value != function.resultType ||
          ZC_ASSERT_NONNULL(parameterReference).type != ZC_ASSERT_NONNULL(localBinding).type ||
          ZC_ASSERT_NONNULL(parameterReference).category != HirValueCategory::Place ||
          !sameSpan(ZC_ASSERT_NONNULL(localBinding).sourceSpan, ZC_ASSERT_NONNULL(patternSpan)) ||
          !sameSpan(ZC_ASSERT_NONNULL(ZC_ASSERT_NONNULL(localBinding).initializerSpan),
                    ZC_ASSERT_NONNULL(initializerSpan)) ||
          !sameSpan(ZC_ASSERT_NONNULL(parameterReference).sourceSpan,
                    ZC_ASSERT_NONNULL(initializerSpan)) ||
          !sameSpan(ZC_ASSERT_NONNULL(parameterReborrow).sourceSpan,
                    ZC_ASSERT_NONNULL(returnSpan))) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      ZC_IF_SOME(parameter, parameterAuthority) {
        if (ZC_ASSERT_NONNULL(parameterReborrow).parameter != parameter.key()) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
      }
      nextFunction += 6;
      continue;
    }

    if (returnsLocal && source.localInitializer == zc::none && !hasLocalWrite) {
      auto returnTypeIndex = factIndex(facts.nodeTypes(), sourceReturnValueNode);
      auto binding = resolvedOwnerLocal(bound.bindings(), sourceReturnValueNode);
      auto patternSpan = bound.parsedModule().spanFor(tree.node(source.localPattern).range);
      auto referenceSpan = bound.parsedModule().spanFor(tree.node(sourceReturnValueNode).range);
      if (returnTypeIndex == zc::none || binding == zc::none || patternSpan == zc::none ||
          referenceSpan == zc::none ||
          !ownerLocalMatches(definitions, ZC_ASSERT_NONNULL(binding), source.localPattern, tree)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      ZC_IF_SOME(local, localBinding) {
        ZC_IF_SOME(reference, localReference) {
          size_t returnTypeSlot = 0;
          ZC_IF_SOME(value, returnTypeIndex) { returnTypeSlot = value; }
          if (local.node != hirId(expectedFunction + 2) || local.initializer != zc::none ||
              local.initializerSpan != zc::none || local.local != hirLocalId(1) ||
              reference.local != local.local || reference.type != local.type ||
              reference.category != HirValueCategory::Place || block.statements[0] != local.node ||
              local.type != function.resultType ||
              facts.nodeTypes().entries()[returnTypeSlot].value != local.type ||
              !sameSpan(local.sourceSpan, ZC_ASSERT_NONNULL(patternSpan)) ||
              !sameSpan(reference.sourceSpan, ZC_ASSERT_NONNULL(referenceSpan))) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
      }
      nextFunction += 5;
      continue;
    }

    if (returnsLocal && source.localInitializer == zc::none && localFieldProjection == zc::none) {
      auto returnTypeIndex = factIndex(facts.nodeTypes(), sourceReturnValueNode);
      auto binding = resolvedOwnerLocal(bound.bindings(), sourceReturnValueNode);
      auto patternSpan = bound.parsedModule().spanFor(tree.node(source.localPattern).range);
      auto referenceSpan = bound.parsedModule().spanFor(tree.node(sourceReturnValueNode).range);
      if (returnTypeIndex == zc::none || binding == zc::none || patternSpan == zc::none ||
          referenceSpan == zc::none ||
          !ownerLocalMatches(definitions, ZC_ASSERT_NONNULL(binding), source.localPattern, tree)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      ZC_IF_SOME(local, localBinding) {
        ZC_IF_SOME(reference, localReference) {
          size_t returnTypeSlot = 0;
          ZC_IF_SOME(value, returnTypeIndex) { returnTypeSlot = value; }
          if (local.node != hirId(expectedFunction + 2) || local.initializer != zc::none ||
              local.initializerSpan != zc::none || local.local != hirLocalId(1) ||
              reference.local != local.local || reference.type != local.type ||
              reference.category != HirValueCategory::Place || block.statements[0] != local.node ||
              local.type != function.resultType ||
              facts.nodeTypes().entries()[returnTypeSlot].value != local.type ||
              !sameSpan(local.sourceSpan, ZC_ASSERT_NONNULL(patternSpan)) ||
              !sameSpan(reference.sourceSpan, ZC_ASSERT_NONNULL(referenceSpan))) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          for (size_t writeIndex = 0; writeIndex < functionLocalWriteCount; ++writeIndex) {
            const auto expectedWrite = hirId(expectedFunction + 3 + writeIndex * 2);
            const auto expectedValue = hirId(expectedFunction + 4 + writeIndex * 2);
            auto write = zc::Maybe<const HirLocalWriteStatement&>();
            auto literal = zc::Maybe<const HirScalarLiteralExpression&>();
            for (const auto& candidateWrite : candidate.impl->localWrites) {
              if (candidateWrite.node == expectedWrite) { write = candidateWrite; }
            }
            for (const auto& expression : candidate.impl->expressions) {
              if (expression.node == expectedValue) { literal = expression; }
            }
            if (write == zc::none || literal == zc::none ||
                ZC_ASSERT_NONNULL(write).kind != (writeIndex == 0 ? HirLocalWriteKind::Initialize
                                                                  : HirLocalWriteKind::Overwrite) ||
                ZC_ASSERT_NONNULL(write).local != local.local ||
                ZC_ASSERT_NONNULL(write).type != local.type ||
                ZC_ASSERT_NONNULL(write).value != expectedValue ||
                ZC_ASSERT_NONNULL(literal).node != expectedValue ||
                ZC_ASSERT_NONNULL(literal).type != local.type ||
                ZC_ASSERT_NONNULL(literal).category != HirValueCategory::Value ||
                block.statements[writeIndex + 1] != expectedWrite) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
          }
        }
      }
      nextFunction += static_cast<uint32_t>(5 + functionLocalWriteCount * 2);
      continue;
    }

    if (returnsLocal && source.returnsLocalBorrow) {
      ast::NodeId localInitializer;
      ZC_IF_SOME(value, source.localInitializer) { localInitializer = value; }
      auto initializerTypeIndex = factIndex(facts.nodeTypes(), localInitializer);
      auto returnTypeIndex = factIndex(facts.nodeTypes(), sourceReturnValueNode);
      auto binding = resolvedOwnerLocal(bound.bindings(), source.localReference);
      auto patternSpan = bound.parsedModule().spanFor(tree.node(source.localPattern).range);
      auto initializerSpan = bound.parsedModule().spanFor(tree.node(localInitializer).range);
      auto referenceSpan = bound.parsedModule().spanFor(tree.node(sourceReturnValueNode).range);
      if (initializerTypeIndex == zc::none || returnTypeIndex == zc::none || binding == zc::none ||
          patternSpan == zc::none || initializerSpan == zc::none || referenceSpan == zc::none ||
          !ownerLocalMatches(definitions, ZC_ASSERT_NONNULL(binding), source.localPattern, tree)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      ZC_IF_SOME(local, localBinding) {
        ZC_IF_SOME(borrow, localBorrow) {
          size_t initializerTypeSlot = 0;
          size_t returnTypeSlot = 0;
          ZC_IF_SOME(value, initializerTypeIndex) { initializerTypeSlot = value; }
          ZC_IF_SOME(value, returnTypeIndex) { returnTypeSlot = value; }
          const auto operation = static_cast<ast::UnaryOperatorKind>(
              tree.node(source.value).payload.words[ast::kUnaryExpressionOpWord]);
          const auto expectedMutability = operation == ast::UnaryOperatorKind::Ref
                                              ? type::semantic::Mutability::Const
                                              : type::semantic::Mutability::Mutable;
          if (local.node != hirId(expectedFunction + 2) || local.initializer == zc::none ||
              local.initializerSpan == zc::none ||
              local.initializer != hirId(expectedFunction + 3) || local.local != hirLocalId(1) ||
              borrow.local != local.local || borrow.sourceType != local.type ||
              borrow.type != function.resultType || borrow.mutability != expectedMutability ||
              block.statements[0] != local.node ||
              facts.nodeTypes().entries()[initializerTypeSlot].value != local.type ||
              facts.nodeTypes().entries()[returnTypeSlot].value != function.resultType ||
              !sameSpan(local.sourceSpan, ZC_ASSERT_NONNULL(patternSpan)) ||
              !sameSpan(ZC_ASSERT_NONNULL(local.initializerSpan),
                        ZC_ASSERT_NONNULL(initializerSpan)) ||
              !sameSpan(borrow.sourceSpan, ZC_ASSERT_NONNULL(referenceSpan))) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          ZC_IF_SOME(expression, literalExpression) {
            auto literalIndex = factIndex(facts.literals(), localInitializer);
            if (literalIndex == zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::MissingRequiredFact, module,
                                                  registries, index + 1);
            }
            size_t literalSlot = 0;
            ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
            if (expression.type != local.type || expression.category != HirValueCategory::Value ||
                !sameSpan(expression.sourceSpan, ZC_ASSERT_NONNULL(local.initializerSpan)) ||
                facts.literals().entries()[literalSlot].value.type != local.type ||
                !sameConstant(expression.value,
                              facts.literals().entries()[literalSlot].value.literal, module,
                              registries, semanticTypes)) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
          }
        }
      }
      nextFunction += 6;
      continue;
    }

    if (returnsLocal && localFieldProjection == zc::none) {
      ast::NodeId localInitializer;
      ZC_IF_SOME(value, source.localInitializer) { localInitializer = value; }
      auto initializerTypeIndex = factIndex(facts.nodeTypes(), localInitializer);
      auto returnTypeIndex = factIndex(facts.nodeTypes(), sourceReturnValueNode);
      auto binding = resolvedOwnerLocal(bound.bindings(), sourceReturnValueNode);
      auto patternSpan = bound.parsedModule().spanFor(tree.node(source.localPattern).range);
      auto initializerSpan = bound.parsedModule().spanFor(tree.node(localInitializer).range);
      auto referenceSpan = bound.parsedModule().spanFor(tree.node(sourceReturnValueNode).range);
      if (initializerTypeIndex == zc::none || returnTypeIndex == zc::none || binding == zc::none ||
          patternSpan == zc::none || initializerSpan == zc::none || referenceSpan == zc::none ||
          !ownerLocalMatches(definitions, ZC_ASSERT_NONNULL(binding), source.localPattern, tree)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      ZC_IF_SOME(local, localBinding) {
        ZC_IF_SOME(reference, localReference) {
          size_t initializerTypeSlot = 0;
          size_t returnTypeSlot = 0;
          ZC_IF_SOME(value, initializerTypeIndex) { initializerTypeSlot = value; }
          ZC_IF_SOME(value, returnTypeIndex) { returnTypeSlot = value; }
          if (local.node != hirId(expectedFunction + 2) || local.initializer == zc::none ||
              local.initializerSpan == zc::none ||
              local.initializer != hirId(expectedFunction + 3) || local.local != hirLocalId(1) ||
              reference.local != local.local || reference.type != local.type ||
              reference.category != HirValueCategory::Place || block.statements[0] != local.node ||
              local.type != function.resultType ||
              facts.nodeTypes().entries()[initializerTypeSlot].value != local.type ||
              facts.nodeTypes().entries()[returnTypeSlot].value != local.type ||
              !sameSpan(local.sourceSpan, ZC_ASSERT_NONNULL(patternSpan)) ||
              !sameSpan(ZC_ASSERT_NONNULL(local.initializerSpan),
                        ZC_ASSERT_NONNULL(initializerSpan)) ||
              !sameSpan(reference.sourceSpan, ZC_ASSERT_NONNULL(referenceSpan))) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          ZC_IF_SOME(expression, literalExpression) {
            auto literalIndex = factIndex(facts.literals(), localInitializer);
            if (literalIndex == zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::MissingRequiredFact, module,
                                                  registries, index + 1);
            }
            size_t literalSlot = 0;
            ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
            if (expression.type != local.type || expression.category != HirValueCategory::Value ||
                !sameSpan(expression.sourceSpan, ZC_ASSERT_NONNULL(local.initializerSpan)) ||
                facts.literals().entries()[literalSlot].value.type != local.type ||
                !sameConstant(expression.value,
                              facts.literals().entries()[literalSlot].value.literal, module,
                              registries, semanticTypes)) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  registries, index + 1);
            }
          }
        }
      }
    }

    auto sourceValueSpan = bound.parsedModule().spanFor(tree.node(sourceValueNode).range);
    auto nodeTypeIndex = factIndex(facts.nodeTypes(), sourceValueNode);
    if (sourceValueSpan == zc::none || nodeTypeIndex == zc::none) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::MissingRequiredFact, module,
                                          registries, index + 1);
    }
    size_t nodeTypeSlot = 0;
    ZC_IF_SOME(value, nodeTypeIndex) { nodeTypeSlot = value; }
    const auto& sourceType = facts.nodeTypes().entries()[nodeTypeSlot].value;
    if (sourceType != function.resultType) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }

    ZC_IF_SOME(reborrow, parameterReborrow) {
      const auto& source = tree.node(sourceValueNode);
      if (source.kind != ast::SyntaxKind::UnaryExpression ||
          (static_cast<ast::UnaryOperatorKind>(source.payload.words[ast::kUnaryExpressionOpWord]) !=
               ast::UnaryOperatorKind::Ref &&
           static_cast<ast::UnaryOperatorKind>(source.payload.words[ast::kUnaryExpressionOpWord]) !=
               ast::UnaryOperatorKind::RefMut) ||
          !sameSpan(reborrow.sourceSpan, ZC_ASSERT_NONNULL(sourceValueSpan))) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      const ast::NodeId dereference(source.payload.words[ast::kUnaryExpressionOperandWord]);
      if (!tree.contains(dereference) ||
          tree.node(dereference).kind != ast::SyntaxKind::UnaryExpression ||
          static_cast<ast::UnaryOperatorKind>(
              tree.node(dereference).payload.words[ast::kUnaryExpressionOpWord]) !=
              ast::UnaryOperatorKind::Deref) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      const ast::NodeId parameter(
          tree.node(dereference).payload.words[ast::kUnaryExpressionOperandWord]);
      auto parameterBinding = resolvedCallableParameter(bound.bindings(), parameter);
      auto parameterTypeIndex = factIndex(facts.nodeTypes(), parameter);
      auto dereferenceTypeIndex = factIndex(facts.nodeTypes(), dereference);
      if (!tree.contains(parameter) || tree.node(parameter).kind != ast::SyntaxKind::IdentExpr ||
          parameterBinding == zc::none || parameterTypeIndex == zc::none ||
          dereferenceTypeIndex == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t parameterTypeSlot = 0;
      size_t dereferenceTypeSlot = 0;
      ZC_IF_SOME(value, parameterTypeIndex) { parameterTypeSlot = value; }
      ZC_IF_SOME(value, dereferenceTypeIndex) { dereferenceTypeSlot = value; }
      const auto parameterType = facts.nodeTypes().entries()[parameterTypeSlot].value;
      const auto referentType = facts.nodeTypes().entries()[dereferenceTypeSlot].value;
      const auto operation =
          static_cast<ast::UnaryOperatorKind>(source.payload.words[ast::kUnaryExpressionOpWord]);
      const auto expectedMutability = operation == ast::UnaryOperatorKind::Ref
                                          ? type::semantic::Mutability::Const
                                          : type::semantic::Mutability::Mutable;
      auto sourceLookup = semanticTypes.get(parameterType);
      if (!sourceLookup.is<type::SemanticTypeLookup>() ||
          !sourceLookup.get<type::SemanticTypeLookup>()
               .data()
               .is<type::semantic::ReferenceTypeData>() ||
          sourceLookup.get<type::SemanticTypeLookup>()
                  .data()
                  .get<type::semantic::ReferenceTypeData>()
                  .mutability != expectedMutability ||
          sourceLookup.get<type::SemanticTypeLookup>()
                  .data()
                  .get<type::semantic::ReferenceTypeData>()
                  .referent != referentType ||
          reborrow.sourceType != parameterType || reborrow.type != sourceType ||
          reborrow.mutability != expectedMutability) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      ZC_IF_SOME(handle, parameterBinding) {
        auto authority = registries.callableParameter(handle);
        if (authority == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        ZC_IF_SOME(entry, authority) {
          bool found = false;
          for (const auto& candidateParameter : function.parameters) {
            if (candidateParameter.key == entry.key() && candidateParameter.type == parameterType) {
              found = true;
            }
          }
          if (!found || reborrow.parameter != entry.key()) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
      }
      nextFunction += 4;
      continue;
    }

    ZC_IF_SOME(reference, parameterReference) {
      auto parameter = resolvedCallableParameter(bound.bindings(), sourceValueNode);
      if (tree.node(sourceValueNode).kind != ast::SyntaxKind::IdentExpr || parameter == zc::none ||
          reference.type != function.resultType || reference.category != HirValueCategory::Place ||
          !sameSpan(reference.sourceSpan, ZC_ASSERT_NONNULL(sourceValueSpan))) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      ZC_IF_SOME(handle, parameter) {
        auto authority = registries.callableParameter(handle);
        if (authority == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        ZC_IF_SOME(entry, authority) {
          bool found = false;
          for (const auto& candidateParameter : function.parameters) {
            if (candidateParameter.key == entry.key() &&
                candidateParameter.type == reference.type) {
              found = true;
            }
          }
          if (!found || reference.parameter != entry.key()) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
      }
      if (returnsParameter) {
        nextFunction += 4;
        continue;
      }
    }

    ZC_IF_SOME(expression, literalExpression) {
      auto literalIndex = factIndex(facts.literals(), sourceValueNode);
      bool valueSpanMatches = false;
      ZC_IF_SOME(value, sourceValueSpan) {
        valueSpanMatches = sameSpan(expression.sourceSpan, value);
      }
      if (!isScalarLiteral(tree.node(sourceValueNode).kind) || literalIndex == zc::none ||
          expression.type != function.resultType ||
          expression.category != HirValueCategory::Value || !valueSpanMatches) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t literalSlot = 0;
      ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
      const auto& literal = facts.literals().entries()[literalSlot].value;
      if (literal.type != function.resultType ||
          !sameConstant(expression.value, literal.literal, module, registries, semanticTypes) ||
          !sameSpan(expression.sourceSpan, literal.sourceSpan)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      if (source.unsafeBlock != zc::none) {
        const auto expectedUnsafeBlock = hirId(expectedFunction + 4);
        if (function.unsafeBlock != expectedUnsafeBlock) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        zc::Maybe<const HirUnsafeBlockExpression&> unsafeBlock;
        for (const auto& candidate : candidate.impl->unsafeBlocks) {
          if (candidate.node != expectedUnsafeBlock) continue;
          if (unsafeBlock != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          unsafeBlock = candidate;
        }
        if (unsafeBlock == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        ZC_IF_SOME(block, unsafeBlock) {
          auto sourceUnsafeSpan =
              bound.parsedModule().spanFor(tree.node(ZC_ASSERT_NONNULL(source.unsafeBlock)).range);
          if (block.body != valueNode || block.type != function.resultType ||
              sourceUnsafeSpan == zc::none ||
              !sameSpan(block.sourceSpan, ZC_ASSERT_NONNULL(sourceUnsafeSpan))) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
        nextFunction += 5;
      } else {
        nextFunction +=
            returnsLocal
                ? static_cast<uint32_t>((localHasInitializer ? 6 : 5) + functionLocalWriteCount * 2)
                : 4;
      }
      continue;
    }

    ZC_IF_SOME(call, directCall) {
      const auto& sourceCall = tree.node(sourceValueNode);
      const ast::NodeId calleeNode(sourceCall.payload.words[ast::kCallExpressionCalleeWord]);
      const ast::NodeList typeArguments{
          sourceCall.payload.words[ast::kCallExpressionTypeArgsFirstWord],
          sourceCall.payload.words[ast::kCallExpressionTypeArgsSizeWord]};
      const ast::NodeList arguments{sourceCall.payload.words[ast::kCallExpressionArgsFirstWord],
                                    sourceCall.payload.words[ast::kCallExpressionArgsSizeWord]};
      auto calleeTypeIndex = factIndex(facts.nodeTypes(), calleeNode);
      auto checkedCallIndex = factIndex(facts.calls(), sourceValueNode);
      auto callKey = checkedNodeKey(tree, bound.parsedModule(), sourceValueNode);
      auto callee = resolvedDefinition(bound.bindings(), calleeNode);
      if (sourceCall.kind != ast::SyntaxKind::CallExpression || !tree.contains(calleeNode) ||
          tree.node(calleeNode).kind != ast::SyntaxKind::IdentExpr ||
          !tree.contains(typeArguments) || !tree.contains(arguments) || !typeArguments.empty() ||
          call.arguments.size() != arguments.size || calleeTypeIndex == zc::none ||
          checkedCallIndex == zc::none || callKey == zc::none || callee == zc::none ||
          call.node != materializedNode || call.resultType != function.resultType ||
          !typeExists(call.calleeType, semanticTypes)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      auto dispatchIndex = dispatchFactIndex(candidate.impl->checkedModule.dispatchFacts().facts(),
                                             ZC_ASSERT_NONNULL(callKey));
      if (dispatchIndex == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t calleeTypeSlot = 0;
      size_t checkedCallSlot = 0;
      size_t dispatchSlot = 0;
      ZC_IF_SOME(value, calleeTypeIndex) { calleeTypeSlot = value; }
      ZC_IF_SOME(value, checkedCallIndex) { checkedCallSlot = value; }
      ZC_IF_SOME(value, dispatchIndex) { dispatchSlot = value; }
      const auto& checkedCall = facts.calls().entries()[checkedCallSlot].value;
      const auto& invocation = checkedCall.invocation;
      const auto& selected = invocation.selected.variant();
      const auto& dispatch = candidate.impl->checkedModule.dispatchFacts().facts()[dispatchSlot];
      const auto& target = dispatch.fact.target.variant();
      const auto& transform = dispatch.fact.resultTransform.variant();
      bool callSpanMatches = false;
      bool dispatchSpanMatches = false;
      bool hirSpanMatches = false;
      bool dispatchOwnerMatches = false;
      ZC_IF_SOME(value, sourceValueSpan) {
        callSpanMatches = sameSpan(checkedCall.sourceSpan, value);
        dispatchSpanMatches = sameSpan(dispatch.fact.sourceSpan, value);
        hirSpanMatches = sameSpan(call.sourceSpan, value);
      }
      ZC_IF_SOME(owner, dispatch.owner) { dispatchOwnerMatches = owner == function.definition; }
      if (call.callee != ZC_ASSERT_NONNULL(callee) ||
          call.calleeType != facts.nodeTypes().entries()[calleeTypeSlot].value ||
          !selected.is<checker::checked::DirectCallable>() ||
          selected.get<checker::checked::DirectCallable>().callee != call.callee ||
          invocation.calleeType != call.calleeType ||
          invocation.successType != function.resultType ||
          invocation.resultType != function.resultType || invocation.receiver != zc::none ||
          invocation.receiverMode != zc::none || invocation.receiverAdjustment != zc::none ||
          invocation.arguments.size() != arguments.size || invocation.substitutions != zc::none ||
          invocation.witnesses != zc::none || invocation.raises != zc::none || !callSpanMatches ||
          !dispatchOwnerMatches || !target.is<checker::dispatch::DirectTarget>() ||
          target.get<checker::dispatch::DirectTarget>().callee != call.callee ||
          !transform.is<checker::dispatch::IdentityResultTransform>() ||
          dispatch.fact.receiver != zc::none || dispatch.fact.arguments.size() != arguments.size ||
          dispatch.fact.successType != function.resultType ||
          dispatch.fact.resultType != function.resultType ||
          dispatch.fact.substitutions != zc::none || dispatch.fact.witnesses != zc::none ||
          dispatch.fact.raises != zc::none || !dispatchSpanMatches || !hirSpanMatches) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      const auto argumentNodes = tree.list(arguments);
      for (size_t argumentIndex = 0; argumentIndex < argumentNodes.size(); ++argumentIndex) {
        const auto argument = argumentNodes[argumentIndex];
        auto argumentTypeIndex = factIndex(facts.nodeTypes(), argument);
        auto literalIndex = factIndex(facts.literals(), argument);
        auto argumentKey = checkedNodeKey(tree, bound.parsedModule(), argument);
        auto argumentSpan = bound.parsedModule().spanFor(tree.node(argument).range);
        if (!tree.contains(argument) || !isScalarLiteral(tree.node(argument).kind) ||
            argumentTypeIndex == zc::none || literalIndex == zc::none || argumentKey == zc::none ||
            argumentSpan == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t argumentTypeSlot = 0;
        size_t literalSlot = 0;
        ZC_IF_SOME(value, argumentTypeIndex) { argumentTypeSlot = value; }
        ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
        const auto& checkedArgument = invocation.arguments[argumentIndex];
        const auto& dispatchArgument = dispatch.fact.arguments[argumentIndex];
        const auto& hirArgument = call.arguments[argumentIndex];
        const auto& literal = facts.literals().entries()[literalSlot].value;
        const auto argumentType = facts.nodeTypes().entries()[argumentTypeSlot].value;
        if (checkedArgument.sourceNode != argument || checkedArgument.sourceType != argumentType ||
            checkedArgument.parameterType != argumentType ||
            checkedArgument.adjustment != zc::none ||
            !sameNodeKey(dispatchArgument.sourceNode, ZC_ASSERT_NONNULL(argumentKey)) ||
            dispatchArgument.sourceType != argumentType ||
            dispatchArgument.parameterType != argumentType ||
            dispatchArgument.adjustment != zc::none || literal.node != argument ||
            literal.type != argumentType || hirArgument.type != argumentType ||
            !sameConstant(hirArgument.value, literal.literal, module, registries, semanticTypes) ||
            !sameSpan(literal.sourceSpan, ZC_ASSERT_NONNULL(argumentSpan)) ||
            !sameSpan(hirArgument.sourceSpan, ZC_ASSERT_NONNULL(argumentSpan))) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
      }
    }
    nextFunction +=
        returnsLocal
            ? static_cast<uint32_t>((localHasInitializer ? 6 : 5) + functionLocalWriteCount * 2)
            : 4;
  }

  auto retainedBoundModule = candidate.impl->checkedModule.retainAdmittedBoundModule();
  auto retainedIdentities = candidate.impl->checkedModule.retainIdentityAuthority();
  const auto& checkedRepository = candidate.impl->checkedModule.checkedRepository();
  auto borrowEvidenceCapability = candidate.impl->checkedModule.borrowEvidenceCapability();
  const auto& semanticTypeStore = candidate.impl->checkedModule.semanticTypes();
  auto impl = zc::heap<VerifiedHirModule::Impl>(
      zc::mv(candidate.impl->checkedModule), zc::mv(retainedBoundModule),
      zc::mv(retainedIdentities), checkedRepository, zc::mv(borrowEvidenceCapability),
      semanticTypeStore, zc::mv(candidate.impl->declarations), zc::mv(candidate.impl->functions),
      zc::mv(candidate.impl->blocks), zc::mv(candidate.impl->returns),
      zc::mv(candidate.impl->patterns), zc::mv(candidate.impl->expressions),
      zc::mv(candidate.impl->aggregates), zc::mv(candidate.impl->locals),
      zc::mv(candidate.impl->localWrites), zc::mv(candidate.impl->localReferences),
      zc::mv(candidate.impl->localFieldProjections), zc::mv(candidate.impl->parameterReferences),
      zc::mv(candidate.impl->parameterIndexes), zc::mv(candidate.impl->parameterReborrows),
      zc::mv(candidate.impl->localBorrows), zc::mv(candidate.impl->calls),
      zc::mv(candidate.impl->receiverCalls), zc::mv(candidate.impl->unsafeBlocks));
  return ir::IrOperationResult<VerifiedHirModule>::verified(VerifiedHirModule(zc::mv(impl)));
}

}  // namespace zomlang::compiler::hir
