// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/checker/body-checker.h"

#include <cstdint>

#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/binder/definition-inventory.h"
#include "zomlang/compiler/checker/inference-recovery-context.h"
#include "zomlang/compiler/checker/operator-kind.h"
#include "zomlang/compiler/checker/scalar-literal-facts.h"

namespace zomlang::compiler::checker::body {
namespace {

using checked::CheckedFactGroup;

enum class BodyProductionKind : uint8_t {
  NullLiteral = 0x01,
  BoolLiteral = 0x02,
  IntLiteral = 0x03,
  FloatLiteral = 0x04,
  BigIntLiteral = 0x05,
  StringLiteral = 0x06,
  UnitLiteral = 0x07,
  CharacterLiteral = 0x08,
  NoSubstitutionTemplateLiteral = 0x09,
  Unsupported = 0x0a
};

struct BodyProductionSite final {
  ast::NodeId node;
  checked::CheckedNodeKey key;
  CheckedFactGroup primaryGroup;
  BodyProductionKind production;
};

checked::CheckedNodeKey cloneNodeKey(const checked::CheckedNodeKey& key) {
  return checked::CheckedNodeKey{key.syntaxKind, key.schemaPreorder, key.sourceSpan.clone()};
}

checked::CheckedFactsInvariantRejected rejectInvariant(
    signature::CheckerInvariantKind kind, identity::ModuleId module, uint32_t ordinal,
    zc::Maybe<identity::DefId>&& owner = zc::none, zc::Maybe<ast::NodeId>&& node = zc::none,
    zc::Maybe<identity::SourceSpan>&& span = zc::none,
    zc::Vector<uint32_t>&& structuralFieldPath = zc::Vector<uint32_t>()) {
  zc::Maybe<identity::Sha256Digest> noExpected;
  zc::Maybe<identity::Sha256Digest> noActual;
  zc::Vector<signature::CheckerVerificationFailure> failures;
  failures.add(signature::CheckerVerificationFailure(signature::CheckerInvariantFact{
      kind, signature::CheckerInvariantStage::Body, module, zc::mv(owner), zc::mv(node),
      zc::mv(span), zc::mv(structuralFieldPath), zc::mv(noExpected), zc::mv(noActual), ordinal}));
  return checked::CheckedFactsInvariantRejected{zc::mv(failures)};
}

checked::CheckedFactsInvariantRejected rejectRecoveryInvariant(
    inference::InferenceRecoveryRejected&& rejection) {
  zc::Vector<signature::CheckerVerificationFailure> failures;
  failures.add(zc::mv(rejection.failure));
  return checked::CheckedFactsInvariantRejected{zc::mv(failures)};
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

zc::Maybe<identity::DefId> initializerOwner(
    const driver::module_graph_query::CheckerBoundModuleView& boundModule,
    ast::NodeId initializer) {
  const auto& tree = boundModule.tree();
  const auto inventory = binder::DefinitionInventory::collect(tree);
  for (const auto& definition : boundModule.definitions().definitions()) {
    ZC_IF_SOME(binding, patternBindingSite(inventory, definition)) {
      if (!tree.contains(binding.introducer) ||
          tree.node(binding.introducer).kind != ast::SyntaxKind::VariableDeclarator) {
        continue;
      }
      const auto& declarator = tree.node(binding.introducer);
      if (ast::NodeId(declarator.payload.words[ast::kVariableDeclaratorInitWord]) == initializer) {
        return definition.definition;
      }
    }
  }
  return zc::none;
}

bool subtreeContains(const ast::Tree& tree, ast::NodeId root, ast::NodeId target) {
  bool contains = false;
  ast::visitTreePreOrder(tree, root, [&](ast::NodeId node, const ast::Node&) {
    if (node == target) contains = true;
  });
  return contains;
}

zc::Maybe<identity::DefId> returnValueOwner(
    const driver::module_graph_query::CheckerBoundModuleView& boundModule, ast::NodeId value) {
  const auto& tree = boundModule.tree();
  bool isReturnValue = false;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId, const ast::Node& syntax) {
    if (syntax.kind == ast::SyntaxKind::ReturnStmt &&
        ast::NodeId(syntax.payload.words[ast::kReturnStmtValueWord]) == value) {
      isReturnValue = true;
    }
  });
  if (!isReturnValue) return zc::none;

  zc::Maybe<identity::DefId> result;
  size_t bestDepth = 0;
  for (const auto& definition : boundModule.definitions().definitions()) {
    if (definition.record.kind() != identity::DefinitionKind::Function ||
        !subtreeContains(tree, definition.node, value)) {
      continue;
    }
    const size_t depth = definition.record.owners().size();
    if (result == zc::none || depth > bestDepth) {
      result = definition.definition;
      bestDepth = depth;
    } else if (depth == bestDepth) {
      return zc::none;
    }
  }
  return result;
}

zc::Maybe<identity::SemanticTypeId> callableSuccess(const signature::VerifiedSignatureFacts& facts,
                                                    identity::DefId callable) {
  zc::Maybe<identity::SemanticTypeId> result;
  for (const auto& semanticSignature : facts.signatures()) {
    if (semanticSignature.definition != callable) continue;
    if (result != zc::none ||
        !semanticSignature.payload.variant().is<signature::CallableSignature>()) {
      return zc::none;
    }
    result = semanticSignature.payload.variant().get<signature::CallableSignature>().success;
  }
  return result;
}

zc::Maybe<uint32_t> definitionPreorder(
    const driver::module_graph_query::CheckerBoundModuleView& boundModule,
    identity::DefId definition) {
  ast::NodeId declaration;
  bool foundDefinition = false;
  for (const auto& candidate : boundModule.definitions().definitions()) {
    if (candidate.definition != definition) continue;
    if (foundDefinition) return zc::none;
    declaration = candidate.node;
    foundDefinition = true;
  }
  if (!foundDefinition) return zc::none;
  uint32_t preorder = 0;
  zc::Maybe<uint32_t> result;
  ast::visitTreePreOrder(boundModule.tree(), boundModule.tree().root(),
                         [&](ast::NodeId node, const ast::Node&) {
                           if (node == declaration) result = preorder;
                           ++preorder;
                         });
  return result;
}

checked::CheckedFactsSourceRejected rejectReturnTypeMismatch(const BodyProductionSite& site,
                                                             uint32_t ownerPreorder,
                                                             identity::SemanticTypeId expected,
                                                             identity::SemanticTypeId actual) {
  zc::Maybe<identity::SemanticIdentifier> noExpectedAlias;
  zc::Maybe<identity::SemanticIdentifier> noActualAlias;
  zc::Vector<checked::CheckerDisplayArgument> arguments;
  arguments.add(
      checked::CheckerDisplayArgument(checked::TypeDisplayArg{expected, zc::mv(noExpectedAlias)}));
  arguments.add(
      checked::CheckerDisplayArgument(checked::TypeDisplayArg{actual, zc::mv(noActualAlias)}));
  zc::Vector<checked::CheckerNoteRef> notes;
  zc::Maybe<checked::TypeErrorId> noRecovery;
  zc::Vector<checked::CheckerFailureRef> failures;
  failures.add(checked::CheckerFailureRef{
      checked::CheckerErrorId::TypeCheckerTypeMismatch(), checked::CheckerDiagnosticStage::Body,
      site.node, site.key.sourceSpan.clone(), zc::mv(arguments), zc::mv(notes),
      checked::CheckerDiagnosticProducer::Inference,
      checked::CheckerRecoveryPolicy(
          checked::CreateRootRecoveryPolicy{checked::CheckerRecoveryClass::TypeMismatch, true}),
      checked::CheckerEmitterOrdinal{static_cast<uint8_t>(checked::CheckerDiagnosticStage::Body),
                                     ownerPreorder, site.key.schemaPreorder, 0},
      zc::mv(noRecovery)});
  return checked::CheckedFactsSourceRejected{zc::mv(failures),
                                             zc::Vector<checked::CheckerAdvisoryRef>(),
                                             zc::Vector<checked::FrozenRecoveryLedger>()};
}

zc::OneOf<checked::CheckedFactsSourceRejected, checked::CheckedFactsInvariantRejected>
attachRecoveryLedger(checked::CheckedFactsSourceRejected&& rejection,
                     const BodyCheckingInput& input,
                     const identity::RegistryBrandIssuer& factStoreBrands) {
  if (rejection.failures.size() != 1 || rejection.recoveryLedgers.size() != 0) {
    return rejectInvariant(signature::CheckerInvariantKind::InferenceLifecycle,
                           input.boundModule.module(), 0);
  }
  auto& failure = rejection.failures[0];
  auto initializer = initializerOwner(input.boundModule, failure.primaryNode);
  auto callable = returnValueOwner(input.boundModule, failure.primaryNode);
  if ((initializer == zc::none) == (callable == zc::none) ||
      !failure.recoveryPolicy.variant().is<checked::CreateRootRecoveryPolicy>()) {
    return rejectInvariant(signature::CheckerInvariantKind::InferenceLifecycle,
                           input.boundModule.module(), failure.emitterOrdinal.siteSchemaPreorder,
                           zc::none, failure.primaryNode, failure.primarySpan.clone());
  }

  identity::DefId ownerDefinition;
  bool initializerOwnerKind = false;
  ZC_IF_SOME(definition, initializer) {
    ownerDefinition = definition;
    initializerOwnerKind = true;
  }
  ZC_IF_SOME(definition, callable) { ownerDefinition = definition; }
  auto created = inference::InferenceRecoveryContext::create(
      input.identities, factStoreBrands, input.boundModule.parsedModule().source(),
      initializerOwnerKind ? inference::InferenceOwner::initializer(ownerDefinition)
                           : inference::InferenceOwner::callableBody(ownerDefinition));
  if (created.is<inference::InferenceRecoveryRejected>()) {
    return rejectRecoveryInvariant(zc::mv(created).get<inference::InferenceRecoveryRejected>());
  }
  auto context = zc::mv(created).get<zc::Own<inference::InferenceRecoveryContext>>();
  auto issued = context->issueRoot(failure.emitterOrdinal, failure.primaryNode, failure.primarySpan,
                                   inference::RecoveryClass::FailedInference);
  if (issued.is<inference::InferenceRecoveryRejected>()) {
    return rejectRecoveryInvariant(zc::mv(issued).get<inference::InferenceRecoveryRejected>());
  }
  failure.recovery = zc::mv(issued).get<checked::TypeErrorId>();
  auto finished = context->finish();
  if (finished.is<inference::InferenceRecoveryRejected>()) {
    return rejectRecoveryInvariant(zc::mv(finished).get<inference::InferenceRecoveryRejected>());
  }
  if (!finished.is<inference::InferenceRecoveryRecovered>()) {
    return rejectInvariant(signature::CheckerInvariantKind::InferenceLifecycle,
                           input.boundModule.module(), failure.emitterOrdinal.siteSchemaPreorder);
  }
  rejection.recoveryLedgers.add(
      zc::mv(finished).get<inference::InferenceRecoveryRecovered>().ledger);
  return zc::mv(rejection);
}

zc::Vector<uint32_t> factPath(CheckedFactGroup group) {
  zc::Vector<uint32_t> path;
  path.add(static_cast<uint32_t>(group));
  return path;
}

bool isPattern(ast::SyntaxKind kind) noexcept {
  switch (kind) {
    case ast::SyntaxKind::RestPattern:
    case ast::SyntaxKind::LiteralPattern:
    case ast::SyntaxKind::IsPattern:
    case ast::SyntaxKind::WildcardPattern:
    case ast::SyntaxKind::BindingPattern:
    case ast::SyntaxKind::IdentifierPattern:
    case ast::SyntaxKind::TuplePattern:
    case ast::SyntaxKind::StructPattern:
    case ast::SyntaxKind::PatternProperty:
    case ast::SyntaxKind::ArrayPattern:
    case ast::SyntaxKind::ExpressionPattern:
    case ast::SyntaxKind::EnumPattern:
      return true;
    default:
      return false;
  }
}

bool isExpression(ast::SyntaxKind kind) noexcept {
  switch (kind) {
    case ast::SyntaxKind::UnsafeBlockExpr:
    case ast::SyntaxKind::ErrorDefaultExpr:
    case ast::SyntaxKind::NullCoalesceExpr:
    case ast::SyntaxKind::IsExpression:
    case ast::SyntaxKind::NullLiteral:
    case ast::SyntaxKind::BoolLiteral:
    case ast::SyntaxKind::IntLiteral:
    case ast::SyntaxKind::FloatLiteralExpr:
    case ast::SyntaxKind::BigIntLiteral:
    case ast::SyntaxKind::StringLiteralExpr:
    case ast::SyntaxKind::ArrayLiteral:
    case ast::SyntaxKind::TupleLiteral:
    case ast::SyntaxKind::UnitLiteral:
    case ast::SyntaxKind::CharacterLiteralExpr:
    case ast::SyntaxKind::NoSubstitutionTemplateLiteralExpr:
    case ast::SyntaxKind::ThisExpr:
    case ast::SyntaxKind::IdentExpr:
    case ast::SyntaxKind::CallExpression:
    case ast::SyntaxKind::BinaryExpr:
    case ast::SyntaxKind::ConditionalExpr:
    case ast::SyntaxKind::AssignmentExpr:
    case ast::SyntaxKind::CommaExpr:
    case ast::SyntaxKind::MemberExpression:
    case ast::SyntaxKind::IndexExpression:
    case ast::SyntaxKind::NewExpression:
    case ast::SyntaxKind::FunctionExpression:
    case ast::SyntaxKind::ImportCallExpression:
    case ast::SyntaxKind::ObjectLiteralExpr:
    case ast::SyntaxKind::TemplateLiteralExpr:
    case ast::SyntaxKind::TypeOfExpression:
    case ast::SyntaxKind::UnaryExpression:
    case ast::SyntaxKind::PostfixExpression:
    case ast::SyntaxKind::CastExpression:
    case ast::SyntaxKind::LambdaExpression:
    case ast::SyntaxKind::SpawnExpression:
    case ast::SyntaxKind::StructLiteralExpr:
    case ast::SyntaxKind::SuperExpr:
      return true;
    default:
      return false;
  }
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

void addNodeRequirement(zc::Vector<checked::NodeFactRequirement>& requirements,
                        CheckedFactGroup group, ast::NodeId node,
                        const checked::CheckedNodeKey& key) {
  requirements.add(checked::NodeFactRequirement{group, node, cloneNodeKey(key)});
}

bool hasCapture(zc::ArrayPtr<const checked::CaptureFactRequirement> requirements,
                const checked::CaptureKey& key) {
  for (const auto& requirement : requirements) {
    if (requirement.key == key) return true;
  }
  return false;
}

bool addOperatorRequirements(const ast::Node& syntax, ast::NodeId node,
                             const checked::CheckedNodeKey& key,
                             zc::Vector<checked::NodeFactRequirement>& requirements) {
  if (syntax.kind == ast::SyntaxKind::UnaryExpression) {
    auto operation = OperatorKind::fromUnary(
        static_cast<ast::UnaryOperatorKind>(syntax.payload.words[ast::kUnaryExpressionOpWord]));
    if (operation == zc::none) return false;
    addNodeRequirement(requirements, CheckedFactGroup::Call, node, key);
    return true;
  }
  if (syntax.kind == ast::SyntaxKind::BinaryExpr) {
    auto operation = OperatorKind::fromBinary(
        static_cast<ast::BinaryOperatorKind>(syntax.payload.words[ast::kBinaryExprOpWord]));
    if (operation == zc::none) return false;
    addNodeRequirement(requirements, CheckedFactGroup::Call, node, key);
    return true;
  }
  if (syntax.kind == ast::SyntaxKind::PostfixExpression) {
    auto operation = OperatorKind::fromPostfix(
        static_cast<ast::PostfixOperatorKind>(syntax.payload.words[ast::kPostfixExpressionOpWord]));
    if (operation == zc::none) return false;
    ZC_IF_SOME(value, operation) {
      if (value.variant().is<checker::ErrorOperatorKind>()) {
        addNodeRequirement(requirements, CheckedFactGroup::ErrorUnionShape, node, key);
        addNodeRequirement(requirements, CheckedFactGroup::ErrorOperator, node, key);
      } else {
        addNodeRequirement(requirements, CheckedFactGroup::Call, node, key);
      }
    }
    return true;
  }
  if (syntax.kind == ast::SyntaxKind::AssignmentExpr) {
    auto operation = OperatorKind::fromAssignment(
        static_cast<ast::AssignmentOperatorKind>(syntax.payload.words[ast::kAssignmentExprOpWord]));
    if (operation == zc::none) return false;
    ZC_IF_SOME(value, operation) {
      if (value.variant().is<CompoundAssignmentOperation>()) {
        addNodeRequirement(requirements, CheckedFactGroup::CompoundAssignment, node, key);
      }
    }
    return true;
  }
  return false;
}

template <typename Map>
Map emptyFactMap() {
  zc::Vector<typename Map::Entry> entries;
  return Map::fromEntries(zc::mv(entries));
}

template <typename Entry, typename Key>
zc::Maybe<const Entry&> factEntry(zc::ArrayPtr<Entry> entries, const Key& key) {
  for (const auto& entry : entries) {
    if (entry.key == key) return entry;
  }
  return zc::none;
}

zc::Maybe<const BodyProductionSite&> productionSite(zc::ArrayPtr<const BodyProductionSite> sites,
                                                    ast::NodeId node) {
  for (const auto& site : sites) {
    if (site.node == node) return site;
  }
  return zc::none;
}

size_t requirementCount(zc::ArrayPtr<const checked::NodeFactRequirement> requirements,
                        CheckedFactGroup group) noexcept {
  size_t count = 0;
  for (const auto& requirement : requirements) {
    if (requirement.group == group) ++count;
  }
  return count;
}

bool hasDefinitionRequirement(zc::ArrayPtr<const checked::DefinitionFactRequirement> requirements,
                              identity::DefId definition) noexcept {
  for (const auto& requirement : requirements) {
    if (requirement.definition == definition) return true;
  }
  return false;
}

size_t definitionRequirementCount(
    zc::ArrayPtr<const checked::DefinitionFactRequirement> requirements,
    CheckedFactGroup group) noexcept {
  size_t count = 0;
  for (const auto& requirement : requirements) {
    if (requirement.group == group) ++count;
  }
  return count;
}

zc::Maybe<checked::PatternFactMap::Entry> identifierPatternFact(
    const BodyProductionSite& site, identity::DefId definition,
    identity::SemanticTypeId semanticType) {
  zc::Vector<checked::PatternBindingFact> bindings;
  bindings.add(checked::PatternBindingFact{definition, semanticType});
  zc::Vector<checked::PatternRefinementFact> refinements;
  zc::Maybe<identity::SemanticTypeId> noGuard;

  return checked::PatternFactMap::Entry{
      site.node,
      checked::CheckedPatternFact{site.node, semanticType,
                                  checked::PatternConstructor(checked::WildcardPattern{}),
                                  zc::mv(bindings), zc::mv(refinements), true, zc::mv(noGuard)},
      zc::Array<uint8_t>()};
}

checked::ConstantFactMap::Entry scalarConstantFact(identity::DefId definition,
                                                   ast::NodeId expression,
                                                   const checked::LiteralFactMap::Entry& literal) {
  using DependencyMap = checked::ImmutableFactMap<identity::DefId, identity::Sha256Digest>;
  zc::Vector<DependencyMap::Entry> dependencyEntries;
  auto dependencies = DependencyMap::fromEntries(zc::mv(dependencyEntries));
  return checked::ConstantFactMap::Entry{
      definition,
      checked::ConstantEvaluationFact{definition, expression, literal.value.literal.clone(),
                                      literal.value.type, zc::mv(dependencies),
                                      identity::Sha256Digest()},
      zc::Array<uint8_t>()};
}

bool hasCompleteFamilyCoverage(zc::ArrayPtr<const BodyFactFamilyCoverage> coverage) {
  if (coverage.size() != 22) return false;
  for (size_t index = 0; index < coverage.size(); ++index) {
    if (static_cast<uint8_t>(coverage[index].group) != index + 1) return false;
  }
  return true;
}

}  // namespace

struct VerifiedBodyFactRequirementInventory::Impl final {
  Impl(identity::SemanticContextBrand semanticContext, identity::ModuleId module,
       const identity::Sha256Digest& sourceContentDigest,
       const binder::ParsedModuleReceipt& parsedModuleReceipt,
       zc::Vector<BodyFactFamilyCoverage>&& familyCoverage,
       zc::Vector<checked::NodeFactRequirement>&& nodeRequirements,
       zc::Vector<checked::DefinitionFactRequirement>&& definitionRequirements,
       zc::Vector<checked::CaptureFactRequirement>&& captureRequirements,
       zc::Vector<BodyProductionSite>&& productionSites)
      : semanticContextValue(semanticContext),
        moduleValue(module),
        sourceContentDigestValue(sourceContentDigest),
        parsedModuleReceiptValue(parsedModuleReceipt),
        familyCoverageValues(zc::mv(familyCoverage)),
        nodeRequirementValues(zc::mv(nodeRequirements)),
        definitionRequirementValues(zc::mv(definitionRequirements)),
        captureRequirementValues(zc::mv(captureRequirements)),
        productionSiteValues(zc::mv(productionSites)) {}

  identity::SemanticContextBrand semanticContextValue;
  identity::ModuleId moduleValue;
  identity::Sha256Digest sourceContentDigestValue;
  binder::ParsedModuleReceipt parsedModuleReceiptValue;
  zc::Vector<BodyFactFamilyCoverage> familyCoverageValues;
  zc::Vector<checked::NodeFactRequirement> nodeRequirementValues;
  zc::Vector<checked::DefinitionFactRequirement> definitionRequirementValues;
  zc::Vector<checked::CaptureFactRequirement> captureRequirementValues;
  zc::Vector<BodyProductionSite> productionSiteValues;
};

VerifiedBodyFactRequirementInventory::VerifiedBodyFactRequirementInventory(
    zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
VerifiedBodyFactRequirementInventory::~VerifiedBodyFactRequirementInventory() noexcept(false) =
    default;
VerifiedBodyFactRequirementInventory::VerifiedBodyFactRequirementInventory(
    VerifiedBodyFactRequirementInventory&&) noexcept = default;
VerifiedBodyFactRequirementInventory& VerifiedBodyFactRequirementInventory::operator=(
    VerifiedBodyFactRequirementInventory&&) noexcept = default;
identity::SemanticContextBrand VerifiedBodyFactRequirementInventory::semanticContext()
    const noexcept {
  return impl->semanticContextValue;
}
identity::ModuleId VerifiedBodyFactRequirementInventory::module() const noexcept {
  return impl->moduleValue;
}
const identity::Sha256Digest& VerifiedBodyFactRequirementInventory::sourceContentDigest()
    const noexcept {
  return impl->sourceContentDigestValue;
}
const binder::ParsedModuleReceipt& VerifiedBodyFactRequirementInventory::parsedModuleReceipt()
    const noexcept {
  return impl->parsedModuleReceiptValue;
}
zc::ArrayPtr<const BodyFactFamilyCoverage> VerifiedBodyFactRequirementInventory::familyCoverage()
    const noexcept {
  return impl->familyCoverageValues.asPtr();
}
zc::ArrayPtr<const checked::NodeFactRequirement>
VerifiedBodyFactRequirementInventory::nodeRequirements() const noexcept {
  return impl->nodeRequirementValues.asPtr();
}
zc::ArrayPtr<const checked::DefinitionFactRequirement>
VerifiedBodyFactRequirementInventory::definitionRequirements() const noexcept {
  return impl->definitionRequirementValues.asPtr();
}
zc::ArrayPtr<const checked::CaptureFactRequirement>
VerifiedBodyFactRequirementInventory::captureRequirements() const noexcept {
  return impl->captureRequirementValues.asPtr();
}

BodyFactRequirementInventoryBuildResult BodyFactRequirementInventoryBuilder::build(
    const driver::module_graph_query::CheckerBoundModuleView& boundModule) {
  const auto& tree = boundModule.tree();
  const auto& parsedModule = boundModule.parsedModule();
  if (!boundModule.semanticContext().isValid() || !tree.contains(tree.root()) ||
      &tree != &parsedModule.tree()) {
    return rejectInvariant(signature::CheckerInvariantKind::InputReceiptMismatch,
                           boundModule.module(), 0);
  }

  zc::Vector<BodyFactFamilyCoverage> coverage;
  coverage.add(
      BodyFactFamilyCoverage{CheckedFactGroup::NodeType, BodyFactRequirementOrigin::Syntax});
  coverage.add(
      BodyFactFamilyCoverage{CheckedFactGroup::DefinitionType, BodyFactRequirementOrigin::Binding});
  coverage.add(
      BodyFactFamilyCoverage{CheckedFactGroup::Literal, BodyFactRequirementOrigin::Syntax});
  coverage.add(
      BodyFactFamilyCoverage{CheckedFactGroup::Constant, BodyFactRequirementOrigin::Binding});
  coverage.add(
      BodyFactFamilyCoverage{CheckedFactGroup::Aggregate, BodyFactRequirementOrigin::Syntax});
  coverage.add(
      BodyFactFamilyCoverage{CheckedFactGroup::Place, BodyFactRequirementOrigin::SemanticAnalysis});
  coverage.add(BodyFactFamilyCoverage{CheckedFactGroup::Coercion,
                                      BodyFactRequirementOrigin::SemanticAnalysis});
  coverage.add(BodyFactFamilyCoverage{CheckedFactGroup::Cast, BodyFactRequirementOrigin::Syntax});
  coverage.add(BodyFactFamilyCoverage{CheckedFactGroup::Call, BodyFactRequirementOrigin::Syntax});
  coverage.add(BodyFactFamilyCoverage{CheckedFactGroup::CompoundAssignment,
                                      BodyFactRequirementOrigin::Syntax});
  coverage.add(BodyFactFamilyCoverage{CheckedFactGroup::Member, BodyFactRequirementOrigin::Syntax});
  coverage.add(BodyFactFamilyCoverage{CheckedFactGroup::Index, BodyFactRequirementOrigin::Syntax});
  coverage.add(
      BodyFactFamilyCoverage{CheckedFactGroup::Pattern, BodyFactRequirementOrigin::Syntax});
  coverage.add(BodyFactFamilyCoverage{CheckedFactGroup::ObservedOperation,
                                      BodyFactRequirementOrigin::SemanticAnalysis});
  coverage.add(
      BodyFactFamilyCoverage{CheckedFactGroup::Capture, BodyFactRequirementOrigin::Binding});
  coverage.add(BodyFactFamilyCoverage{CheckedFactGroup::MarkerObligation,
                                      BodyFactRequirementOrigin::SemanticAnalysis});
  coverage.add(
      BodyFactFamilyCoverage{CheckedFactGroup::Exhaustiveness, BodyFactRequirementOrigin::Syntax});
  coverage.add(BodyFactFamilyCoverage{CheckedFactGroup::UnsafeOperation,
                                      BodyFactRequirementOrigin::SemanticAnalysis});
  coverage.add(BodyFactFamilyCoverage{CheckedFactGroup::Projection,
                                      BodyFactRequirementOrigin::SemanticAnalysis});
  coverage.add(BodyFactFamilyCoverage{CheckedFactGroup::Obligation,
                                      BodyFactRequirementOrigin::SemanticAnalysis});
  coverage.add(BodyFactFamilyCoverage{CheckedFactGroup::ErrorUnionShape,
                                      BodyFactRequirementOrigin::SemanticAnalysis});
  coverage.add(
      BodyFactFamilyCoverage{CheckedFactGroup::ErrorOperator, BodyFactRequirementOrigin::Syntax});

  zc::Vector<checked::NodeFactRequirement> nodeRequirements;
  zc::Vector<checked::DefinitionFactRequirement> definitionRequirements;
  zc::Vector<checked::CaptureFactRequirement> captureRequirements;
  zc::Vector<BodyProductionSite> productionSites;
  zc::Maybe<checked::CheckedFactsInvariantRejected> failure;
  uint32_t schemaPreorder = 0;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    if (failure != zc::none) return;
    const uint32_t ordinal = schemaPreorder++;
    if (syntax.kind == ast::SyntaxKind::Unknown) {
      failure = rejectInvariant(signature::CheckerInvariantKind::InvalidFact, boundModule.module(),
                                ordinal, zc::none, node);
      return;
    }
    const bool bodyNode = isExpression(syntax.kind) || isPattern(syntax.kind) ||
                          syntax.kind == ast::SyntaxKind::MatchStmt ||
                          syntax.kind == ast::SyntaxKind::SuspendStatement;
    if (!bodyNode) return;
    auto span = parsedModule.spanFor(syntax.range);
    if (span == zc::none) {
      failure = rejectInvariant(signature::CheckerInvariantKind::InputReceiptMismatch,
                                boundModule.module(), ordinal, zc::none, node);
      return;
    }
    ZC_IF_SOME(sourceSpan, span) {
      checked::CheckedNodeKey key{static_cast<uint32_t>(syntax.kind), ordinal, sourceSpan.clone()};
      if (isExpression(syntax.kind)) {
        addNodeRequirement(nodeRequirements, CheckedFactGroup::NodeType, node, key);
      }
      if (isScalarLiteral(syntax.kind)) {
        addNodeRequirement(nodeRequirements, CheckedFactGroup::Literal, node, key);
      } else if (syntax.kind == ast::SyntaxKind::ArrayLiteral ||
                 syntax.kind == ast::SyntaxKind::TupleLiteral ||
                 syntax.kind == ast::SyntaxKind::ObjectLiteralExpr ||
                 syntax.kind == ast::SyntaxKind::StructLiteralExpr ||
                 syntax.kind == ast::SyntaxKind::NewExpression) {
        addNodeRequirement(nodeRequirements, CheckedFactGroup::Aggregate, node, key);
      } else if (syntax.kind == ast::SyntaxKind::CastExpression) {
        addNodeRequirement(nodeRequirements, CheckedFactGroup::Cast, node, key);
      } else if (syntax.kind == ast::SyntaxKind::CallExpression ||
                 syntax.kind == ast::SyntaxKind::ImportCallExpression) {
        addNodeRequirement(nodeRequirements, CheckedFactGroup::Call, node, key);
      } else if (syntax.kind == ast::SyntaxKind::MemberExpression) {
        addNodeRequirement(nodeRequirements, CheckedFactGroup::Member, node, key);
      } else if (syntax.kind == ast::SyntaxKind::IndexExpression) {
        addNodeRequirement(nodeRequirements, CheckedFactGroup::Call, node, key);
        addNodeRequirement(nodeRequirements, CheckedFactGroup::Index, node, key);
      } else if (isPattern(syntax.kind)) {
        addNodeRequirement(nodeRequirements, CheckedFactGroup::Pattern, node, key);
      } else if (syntax.kind == ast::SyntaxKind::MatchStmt) {
        addNodeRequirement(nodeRequirements, CheckedFactGroup::Exhaustiveness, node, key);
      } else if (syntax.kind == ast::SyntaxKind::SuspendStatement) {
        addNodeRequirement(nodeRequirements, CheckedFactGroup::ObservedOperation, node, key);
      } else if (syntax.kind == ast::SyntaxKind::NullCoalesceExpr) {
        const OperatorKind operation(PrimitiveOperation::NullCoalesce);
        if (operation.variant().is<PrimitiveOperation>()) {
          addNodeRequirement(nodeRequirements, CheckedFactGroup::Call, node, key);
        }
      } else if (syntax.kind == ast::SyntaxKind::UnaryExpression ||
                 syntax.kind == ast::SyntaxKind::BinaryExpr ||
                 syntax.kind == ast::SyntaxKind::PostfixExpression ||
                 syntax.kind == ast::SyntaxKind::AssignmentExpr) {
        if (!addOperatorRequirements(syntax, node, key, nodeRequirements)) {
          failure =
              rejectInvariant(signature::CheckerInvariantKind::InvalidFact, boundModule.module(),
                              ordinal, zc::none, node, sourceSpan.clone());
          return;
        }
      }
      BodyProductionKind production = BodyProductionKind::Unsupported;
      switch (syntax.kind) {
        case ast::SyntaxKind::NullLiteral:
          production = BodyProductionKind::NullLiteral;
          break;
        case ast::SyntaxKind::BoolLiteral:
          production = BodyProductionKind::BoolLiteral;
          break;
        case ast::SyntaxKind::IntLiteral:
          production = BodyProductionKind::IntLiteral;
          break;
        case ast::SyntaxKind::FloatLiteralExpr:
          production = BodyProductionKind::FloatLiteral;
          break;
        case ast::SyntaxKind::BigIntLiteral:
          production = BodyProductionKind::BigIntLiteral;
          break;
        case ast::SyntaxKind::StringLiteralExpr:
          production = BodyProductionKind::StringLiteral;
          break;
        case ast::SyntaxKind::UnitLiteral:
          production = BodyProductionKind::UnitLiteral;
          break;
        case ast::SyntaxKind::CharacterLiteralExpr:
          production = BodyProductionKind::CharacterLiteral;
          break;
        case ast::SyntaxKind::NoSubstitutionTemplateLiteralExpr:
          production = BodyProductionKind::NoSubstitutionTemplateLiteral;
          break;
        default:
          break;
      }
      CheckedFactGroup primary = CheckedFactGroup::NodeType;
      if (isPattern(syntax.kind)) primary = CheckedFactGroup::Pattern;
      if (syntax.kind == ast::SyntaxKind::MatchStmt) primary = CheckedFactGroup::Exhaustiveness;
      if (syntax.kind == ast::SyntaxKind::SuspendStatement) {
        primary = CheckedFactGroup::ObservedOperation;
      }
      productionSites.add(BodyProductionSite{node, cloneNodeKey(key), primary, production});
    }
  });
  ZC_IF_SOME(rejection, failure) { return zc::mv(rejection); }

  for (const auto& definition : boundModule.bindings().definitions()) {
    switch (definition.kind) {
      case identity::DefinitionKind::Parameter:
      case identity::DefinitionKind::Constant:
      case identity::DefinitionKind::Static:
      case identity::DefinitionKind::Local:
      case identity::DefinitionKind::PatternBinding:
      case identity::DefinitionKind::Closure:
        definitionRequirements.add(checked::DefinitionFactRequirement{
            CheckedFactGroup::DefinitionType, definition.identity});
        if (definition.kind == identity::DefinitionKind::Constant) {
          definitionRequirements.add(
              checked::DefinitionFactRequirement{CheckedFactGroup::Constant, definition.identity});
        }
        break;
      default:
        break;
    }
  }
  for (const auto& closure : boundModule.bindings().closureFreeVariables()) {
    for (const auto& variable : closure.variables) {
      checked::CaptureKey key{closure.closure.clone(), variable.target.clone()};
      if (!hasCapture(captureRequirements.asPtr(), key)) {
        captureRequirements.add(checked::CaptureFactRequirement{zc::mv(key)});
      }
    }
  }
  for (const auto& closure : boundModule.bindings().explicitClosureCaptures()) {
    for (const auto& capture : closure.captures) {
      checked::CaptureKey key{closure.closure.clone(), capture.target.clone()};
      if (!hasCapture(captureRequirements.asPtr(), key)) {
        captureRequirements.add(checked::CaptureFactRequirement{zc::mv(key)});
      }
    }
  }

  return VerifiedBodyFactRequirementInventory(zc::heap<VerifiedBodyFactRequirementInventory::Impl>(
      boundModule.semanticContext(), boundModule.module(), parsedModule.contentDigest(),
      parsedModule.receipt(), zc::mv(coverage), zc::mv(nodeRequirements),
      zc::mv(definitionRequirements), zc::mv(captureRequirements), zc::mv(productionSites)));
}

struct BodyChecker::Impl final {};

BodyChecker::BodyChecker() : impl(zc::heap<Impl>()) {}
BodyChecker::~BodyChecker() noexcept(false) = default;

BodyCheckingResult BodyChecker::check(const BodyCheckingInput& input,
                                      const identity::RegistryBrandIssuer& factStoreBrands) {
  const auto context = input.boundModule.semanticContext();
  const auto module = input.boundModule.module();
  const auto& parsedModule = input.boundModule.parsedModule();
  if (!context.isValid() || input.signatureFacts.semanticContext() != context ||
      input.importedSignatures.semanticContext() != context ||
      input.coherence.semanticContext() != context ||
      input.identities.semanticContext() != context || input.semanticTypes.context() != context ||
      input.requirements.semanticContext() != context || input.signatureFacts.module() != module ||
      input.importedSignatures.requester() != module || input.requirements.module() != module ||
      input.boundModule.bindingSurface().revision().digest() !=
          input.signatureFacts.bindingSurfaceRevision().digest() ||
      input.boundModule.semanticFingerprint().digest() !=
          input.signatureFacts.contextFingerprint().digest() ||
      input.boundModule.semanticFingerprint().digest() !=
          input.importedSignatures.contextFingerprint().digest() ||
      input.boundModule.semanticFingerprint().digest() !=
          input.coherence.contextFingerprint().digest() ||
      parsedModule.contentDigest() != input.signatureFacts.sourceContentDigest() ||
      parsedModule.contentDigest() != input.requirements.sourceContentDigest() ||
      parsedModule.receipt().digest() != input.signatureFacts.parsedModuleReceipt().digest() ||
      parsedModule.receipt().digest() != input.requirements.parsedModuleReceipt().digest() ||
      !hasCompleteFamilyCoverage(input.requirements.familyCoverage())) {
    return rejectInvariant(signature::CheckerInvariantKind::InputReceiptMismatch, module, 0);
  }

  if (input.requirements.captureRequirements().size() != 0) {
    zc::Maybe<identity::DefId> closureOwner;
    ZC_IF_SOME(ownerKey,
               input.requirements.captureRequirements()[0].key.closure.owner().definitionKey()) {
      ZC_IF_SOME(owner, input.identities.definition(ownerKey)) { closureOwner = owner.handle(); }
    }
    return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module, 0,
                           zc::mv(closureOwner), zc::none, zc::none,
                           factPath(CheckedFactGroup::Capture));
  }

  zc::Vector<checked::NodeTypeMap::Entry> nodeTypes;
  zc::Vector<checked::DefinitionTypeMap::Entry> definitionTypes;
  zc::Vector<checked::LiteralFactMap::Entry> literals;
  zc::Vector<checked::ConstantFactMap::Entry> constants;
  zc::Vector<checked::PatternFactMap::Entry> patterns;
  for (const auto& site : input.requirements.impl->productionSiteValues) {
    if (site.production == BodyProductionKind::Unsupported) {
      if (input.boundModule.tree().node(site.node).kind == ast::SyntaxKind::IdentifierPattern) {
        continue;
      }
      return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                             site.key.schemaPreorder, zc::none, site.node,
                             site.key.sourceSpan.clone(), factPath(site.primaryGroup));
    }
    auto emitted = scalar_literal::FactEmitter::emit(scalar_literal::FactEmissionInput{
        context, module, input.boundModule.tree(), site.node, site.key,
        input.boundModule.parsedModule().source(), input.identities, input.semanticTypes});
    if (emitted.is<checked::CheckedFactsInvariantRejected>()) {
      return zc::mv(emitted).get<checked::CheckedFactsInvariantRejected>();
    }
    if (emitted.is<checked::CheckedFactsSourceRejected>()) {
      return attachRecoveryLedger(zc::mv(emitted).get<checked::CheckedFactsSourceRejected>(), input,
                                  factStoreBrands);
    }
    auto facts = zc::mv(emitted).get<scalar_literal::EmittedFacts>();
    auto callable = returnValueOwner(input.boundModule, site.node);
    ZC_IF_SOME(owner, callable) {
      auto expected = callableSuccess(input.signatureFacts, owner);
      auto ownerPreorder = definitionPreorder(input.boundModule, owner);
      if (expected == zc::none || ownerPreorder == zc::none) {
        return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                               site.key.schemaPreorder, owner, site.node,
                               site.key.sourceSpan.clone(), factPath(CheckedFactGroup::NodeType));
      }
      ZC_IF_SOME(expectedType, expected) {
        ZC_IF_SOME(ownerOrdinal, ownerPreorder) {
          if (facts.nodeType.value != expectedType) {
            return attachRecoveryLedger(
                rejectReturnTypeMismatch(site, ownerOrdinal, expectedType, facts.nodeType.value),
                input, factStoreBrands);
          }
        }
      }
    }
    nodeTypes.add(zc::mv(facts.nodeType));
    literals.add(zc::mv(facts.literal));
  }

  const auto& tree = input.boundModule.tree();
  const auto definitionInventory = binder::DefinitionInventory::collect(tree);
  for (const auto& definition : input.boundModule.definitions().definitions()) {
    if (!hasDefinitionRequirement(input.requirements.definitionRequirements(),
                                  definition.definition)) {
      continue;
    }
    auto bindingSite = patternBindingSite(definitionInventory, definition);
    if (bindingSite == zc::none) {
      return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module, 0,
                             definition.definition, definition.node, definition.source.clone(),
                             factPath(CheckedFactGroup::DefinitionType));
    }
    ZC_IF_SOME(site, bindingSite) {
      if (site.patternPath.size() != 0 || !tree.contains(site.introducer) ||
          tree.node(site.introducer).kind != ast::SyntaxKind::VariableDeclarator) {
        return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module, 0,
                               definition.definition, definition.node, definition.source.clone(),
                               factPath(CheckedFactGroup::Pattern));
      }
      const auto& declarator = tree.node(site.introducer);
      const ast::NodeId pattern(declarator.payload.words[ast::kVariableDeclaratorPatternWord]);
      const ast::NodeId annotation(declarator.payload.words[ast::kVariableDeclaratorTyWord]);
      const ast::NodeId initializer(declarator.payload.words[ast::kVariableDeclaratorInitWord]);
      if (!tree.contains(pattern) ||
          tree.node(pattern).kind != ast::SyntaxKind::IdentifierPattern ||
          tree.contains(annotation) || !tree.contains(initializer)) {
        return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module, 0,
                               definition.definition, site.introducer, definition.source.clone(),
                               factPath(CheckedFactGroup::DefinitionType));
      }
      auto initializerType = factEntry(nodeTypes.asPtr(), initializer);
      auto initializerLiteral = factEntry(literals.asPtr(), initializer);
      auto bindingProduction =
          productionSite(input.requirements.impl->productionSiteValues.asPtr(), pattern);
      if (initializerType == zc::none || initializerLiteral == zc::none ||
          bindingProduction == zc::none) {
        return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module, 0,
                               definition.definition, initializer, definition.source.clone(),
                               factPath(CheckedFactGroup::DefinitionType));
      }

      ZC_IF_SOME(typeEntry, initializerType) {
        definitionTypes.add(checked::DefinitionTypeMap::Entry{
            definition.definition, typeEntry.value, zc::Array<uint8_t>()});
        ZC_IF_SOME(patternSite, bindingProduction) {
          auto pattern = identifierPatternFact(patternSite, definition.definition, typeEntry.value);
          if (pattern == zc::none) {
            return rejectInvariant(signature::CheckerInvariantKind::CanonicalCodecMismatch, module,
                                   0, definition.definition, definition.node,
                                   definition.source.clone(), factPath(CheckedFactGroup::Pattern));
          }
          ZC_IF_SOME(value, pattern) { patterns.add(zc::mv(value)); }
        }
      }
      if (definition.record.kind() == identity::DefinitionKind::Constant) {
        ZC_IF_SOME(literalEntry, initializerLiteral) {
          constants.add(scalarConstantFact(definition.definition, initializer, literalEntry));
        }
      }
    }
  }

  for (const auto& requirement : input.requirements.nodeRequirements()) {
    if (requirement.group != CheckedFactGroup::NodeType &&
        requirement.group != CheckedFactGroup::Literal &&
        requirement.group != CheckedFactGroup::Pattern) {
      return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                             requirement.key.schemaPreorder, zc::none, requirement.node,
                             requirement.key.sourceSpan.clone(), factPath(requirement.group));
    }
  }
  if (nodeTypes.size() !=
          requirementCount(input.requirements.nodeRequirements(), CheckedFactGroup::NodeType) ||
      literals.size() !=
          requirementCount(input.requirements.nodeRequirements(), CheckedFactGroup::Literal) ||
      patterns.size() !=
          requirementCount(input.requirements.nodeRequirements(), CheckedFactGroup::Pattern) ||
      definitionTypes.size() !=
          definitionRequirementCount(input.requirements.definitionRequirements(),
                                     CheckedFactGroup::DefinitionType) ||
      constants.size() != definitionRequirementCount(input.requirements.definitionRequirements(),
                                                     CheckedFactGroup::Constant)) {
    return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module, 0);
  }
  auto substitutionBrand = factStoreBrands.issue();
  auto witnessBrand = factStoreBrands.issue();
  if (substitutionBrand == zc::none || witnessBrand == zc::none) {
    return rejectInvariant(signature::CheckerInvariantKind::InferenceLifecycle, module, 0);
  }
  zc::Maybe<checked::FrozenSubstitutionStore> substitutions;
  zc::Maybe<checked::FrozenWitnessStore> witnesses;
  ZC_IF_SOME(brand, substitutionBrand) {
    zc::Vector<checked::FrozenSubstitutionStore::Record> records;
    substitutions = checked::FrozenSubstitutionStore::from(context, brand, zc::mv(records));
  }
  ZC_IF_SOME(brand, witnessBrand) {
    zc::Vector<checked::FrozenWitnessStore::Record> records;
    witnesses = checked::FrozenWitnessStore::from(context, brand, zc::mv(records));
  }
  if (substitutions == zc::none || witnesses == zc::none) {
    return rejectInvariant(signature::CheckerInvariantKind::InferenceLifecycle, module, 0);
  }

  ZC_IF_SOME(substitutionStore, substitutions) {
    ZC_IF_SOME(witnessStore, witnesses) {
      checked::CheckedFactsCandidate candidate{
          context,
          input.boundModule.semanticFingerprint().clone(),
          module,
          parsedModule.contentDigest(),
          parsedModule.receipt(),
          input.signatureFacts.revision(),
          input.importedSignatures.revision(),
          input.coherence.revision(),
          input.semanticOptions,
          zc::mv(substitutionStore),
          zc::mv(witnessStore),
          checked::NodeTypeMap::fromEntries(zc::mv(nodeTypes)),
          checked::DefinitionTypeMap::fromEntries(zc::mv(definitionTypes)),
          checked::LiteralFactMap::fromEntries(zc::mv(literals)),
          checked::ConstantFactMap::fromEntries(zc::mv(constants)),
          emptyFactMap<checked::AggregateFactMap>(),
          emptyFactMap<checked::PlaceFactMap>(),
          emptyFactMap<checked::CoercionFactMap>(),
          emptyFactMap<checked::CastFactMap>(),
          emptyFactMap<checked::CallFactMap>(),
          emptyFactMap<checked::CompoundAssignmentFactMap>(),
          emptyFactMap<checked::MemberFactMap>(),
          emptyFactMap<checked::IndexFactMap>(),
          checked::PatternFactMap::fromEntries(zc::mv(patterns)),
          emptyFactMap<checked::ObservedOperationFactMap>(),
          emptyFactMap<checked::CaptureFactMap>(),
          emptyFactMap<checked::MarkerObligationFactMap>(),
          emptyFactMap<checked::ExhaustivenessFactMap>(),
          emptyFactMap<checked::UnsafeOperationFactMap>(),
          emptyFactMap<checked::ProjectionFactMap>(),
          emptyFactMap<checked::ObligationFactMap>(),
          emptyFactMap<checked::ErrorUnionShapeFactMap>(),
          emptyFactMap<checked::ErrorOperatorFactMap>(),
          zc::Vector<checked::FrozenRecoveryLedger>(),
          zc::Vector<checked::CheckerFailureRef>(),
          zc::Vector<checked::CheckerAdvisoryRef>()};

      zc::Vector<identity::DefId> importedDefinitions;
      for (const auto& importedModule : input.importedSignatures.modules()) {
        for (const auto& definition : importedModule.lookupDefinitions()) {
          bool duplicate = false;
          for (const auto existing : importedDefinitions) {
            if (existing == definition.definition) {
              duplicate = true;
              break;
            }
          }
          if (!duplicate) importedDefinitions.add(definition.definition);
        }
      }
      zc::Vector<identity::ImplId> coherentImpls(input.coherence.implHeads().size());
      for (const auto& implementation : input.coherence.implHeads()) {
        coherentImpls.add(implementation.impl);
      }
      const checked::CheckedFactsVerificationInput codecInput{
          context,
          input.boundModule.semanticFingerprint(),
          module,
          parsedModule.source(),
          parsedModule.contentDigest(),
          parsedModule.receipt(),
          input.signatureFacts.revision(),
          input.importedSignatures.revision(),
          input.coherence.revision(),
          input.semanticOptions,
          input.requirements.nodeRequirements(),
          input.requirements.definitionRequirements(),
          input.requirements.captureRequirements(),
          importedDefinitions.asPtr(),
          coherentImpls.asPtr(),
          candidate.sourceFailures.asPtr(),
          input.boundModule.definitions().ownerLocalBindings(),
          input.boundModule.definitions().anonymousEntities(),
          input.identities,
          input.semanticTypes};
      if (!checked::CheckedFactsCanonicalCodec::writeCanonicalRecords(candidate, codecInput)) {
        return rejectInvariant(signature::CheckerInvariantKind::CanonicalCodecMismatch, module, 0);
      }
      return zc::mv(candidate);
    }
  }
  ZC_UNREACHABLE
}

}  // namespace zomlang::compiler::checker::body
