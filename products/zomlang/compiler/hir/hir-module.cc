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

// One initializer kind admitted for a sequential local binding. A reference is
// discriminated further by whether it names an earlier local or a parameter. A
// primitive binary operation carries two operands (see PendingSequentialBinary).
enum class SequentialInitializerKind : uint8_t {
  Literal,
  Aggregate,
  LocalReference,
  ParameterReference,
  PrimitiveBinary
};

// One operand of a sequential primitive-binary initializer. Exactly one payload
// is populated per kind: `literal` for a scalar literal, `parameter` for a
// parameter reference, `referencedLocal` (zero-based index into the enclosing
// body's bindings) for a reference to an earlier local, and a nested one-level
// primitive binary (`a + b * c`) whose own two operands are classified into
// `nestedLeft` / `nestedRight` (each a literal, parameter, or earlier local).
enum class SequentialBinaryOperandKind : uint8_t {
  Literal,
  ParameterReference,
  LocalReference,
  NestedBinary
};

struct PendingSequentialBinaryLeafOperand final {
  SequentialBinaryOperandKind kind;
  identity::SemanticTypeId type;
  identity::SourceSpan sourceSpan;
  zc::Maybe<checker::checked::CanonicalConstValue> literal;
  zc::Maybe<identity::CallableParameterKey> parameter;
  size_t referencedLocal;
};

struct PendingSequentialBinaryOperand final {
  SequentialBinaryOperandKind kind;
  identity::SemanticTypeId type;
  identity::SourceSpan sourceSpan;
  zc::Maybe<checker::checked::CanonicalConstValue> literal;
  zc::Maybe<identity::CallableParameterKey> parameter;
  size_t referencedLocal;
  // Populated only for NestedBinary: the inner operation and its two leaf
  // operands. The inner binary shares this operand's `sourceSpan`/`type` and its
  // node id is this operand's node id.
  zc::Maybe<checker::PrimitiveOperation> nestedOperation;
  zc::Maybe<PendingSequentialBinaryLeafOperand> nestedLeft;
  zc::Maybe<PendingSequentialBinaryLeafOperand> nestedRight;
};

// One materialized binding in a sequential N-local body. Exactly one payload is
// populated per initializer kind: `literal` for a scalar literal, `aggregate`
// for a closed nominal aggregate, `parameter` for a parameter reference, and
// `referencedLocal` for a reference to an earlier local (zero-based index). A
// `PrimitiveBinary` binding instead populates `operation`, `leftOperand`, and
// `rightOperand`. All bindings share the function result type.
struct PendingSequentialBinding final {
  identity::SemanticTypeId type;
  identity::SourceSpan patternSpan;
  identity::SourceSpan initializerSpan;
  SequentialInitializerKind kind;
  zc::Maybe<checker::checked::CanonicalConstValue> literal;
  zc::Maybe<HirNominalAggregateExpression> aggregate;
  zc::Maybe<identity::CallableParameterKey> parameter;
  size_t referencedLocal;
  zc::Maybe<checker::PrimitiveOperation> operation;
  identity::SemanticTypeId operandType;
  zc::Maybe<PendingSequentialBinaryOperand> leftOperand;
  zc::Maybe<PendingSequentialBinaryOperand> rightOperand;
};

struct PendingSequentialLocalReturn final {
  zc::Vector<PendingSequentialBinding> bindings;
  identity::SemanticTypeId type;
  // The returned place: a parameter (`returnParameter` populated) or one of the
  // declared locals (`returnLocal` holds its zero-based index).
  zc::Maybe<identity::CallableParameterKey> returnParameter;
  size_t returnLocal;
  identity::SourceSpan returnValueSpan;
};

// One conditional arm carries either a scalar literal value or a reference to a
// function parameter. Exactly one of the two Maybe fields is populated; the arm
// kind is discriminated by which one is set.
// Returns true for the six relational comparison operators of same-typed
// scalars that the conditional-condition lowering supports.
bool isScalarComparisonOperation(checker::PrimitiveOperation operation) {
  switch (operation) {
    case checker::PrimitiveOperation::Eq:
    case checker::PrimitiveOperation::Ne:
    case checker::PrimitiveOperation::Lt:
    case checker::PrimitiveOperation::Le:
    case checker::PrimitiveOperation::Gt:
    case checker::PrimitiveOperation::Ge:
      return true;
    default:
      return false;
  }
}

// Returns true when the syntactic binary operator is one of the six relational
// comparisons supported as a conditional condition. Strict identity operators
// and every arithmetic, bitwise, or logical operator are excluded.
bool isRelationalBinaryOperator(ast::BinaryOperatorKind syntax) {
  ZC_IF_SOME(kind, checker::OperatorKind::fromBinary(syntax)) {
    const auto& variant = kind.variant();
    return variant.is<checker::PrimitiveOperation>() &&
           isScalarComparisonOperation(variant.get<checker::PrimitiveOperation>());
  }
  return false;
}

// Returns true for the twelve arithmetic and bitwise binary operators of
// same-typed scalars. Unlike a comparison, the result is the operand type, not
// bool; the logical short-circuit operators (`&&` / `||`) are excluded.
bool isScalarArithmeticOperation(checker::PrimitiveOperation operation) {
  switch (operation) {
    case checker::PrimitiveOperation::Add:
    case checker::PrimitiveOperation::Sub:
    case checker::PrimitiveOperation::Mul:
    case checker::PrimitiveOperation::Div:
    case checker::PrimitiveOperation::Rem:
    case checker::PrimitiveOperation::Pow:
    case checker::PrimitiveOperation::Shl:
    case checker::PrimitiveOperation::Shr:
    case checker::PrimitiveOperation::UShr:
    case checker::PrimitiveOperation::BitAnd:
    case checker::PrimitiveOperation::BitOr:
    case checker::PrimitiveOperation::BitXor:
      return true;
    default:
      return false;
  }
}

// Returns true when the syntactic binary operator is a relational comparison or
// an arithmetic/bitwise operator, i.e. a primitive binary operation lowerable in
// return position. Strict identity and the logical short-circuit operators are
// excluded.
bool isPrimitiveBinaryOperator(ast::BinaryOperatorKind syntax) {
  ZC_IF_SOME(kind, checker::OperatorKind::fromBinary(syntax)) {
    const auto& variant = kind.variant();
    if (!variant.is<checker::PrimitiveOperation>()) return false;
    const auto operation = variant.get<checker::PrimitiveOperation>();
    return isScalarComparisonOperation(operation) || isScalarArithmeticOperation(operation);
  }
  return false;
}

struct PendingConditionalArm final {
  zc::Maybe<checker::checked::CanonicalConstValue> literal;
  zc::Maybe<HirParameterReferenceExpression> parameter;
  identity::SemanticTypeId type;
  identity::SourceSpan sourceSpan;
};

// One mutable-local write value. It is either a scalar literal (`literal`
// populated) or a reference to a function parameter lowered to a place operand
// (`parameter` populated). Exactly one of the two is populated; the value kind
// is discriminated by which, mirroring the literal-XOR-parameter shape of a
// conditional arm. A literal write lowers to a `MirOperand::constant`; a
// parameter write lowers to a copy/move place-use of the caller's parameter
// local, exactly like the return-of-parameter path.
struct PendingLocalWriteValue final {
  zc::Maybe<checker::checked::CanonicalConstValue> literal;
  zc::Maybe<HirParameterReferenceExpression> parameter;
};

// One relational-comparison condition holds its two operands, the shared operand
// type, the produced bool type, and the selected comparison operator. Each
// operand is either a scalar literal or a parameter reference (the same
// literal-XOR-parameter shape as a conditional arm); at least one is a
// parameter.
struct PendingEqualityCondition final {
  PendingConditionalArm left;
  PendingConditionalArm right;
  identity::SemanticTypeId operandType;
  identity::SemanticTypeId type;
  checker::PrimitiveOperation operation;
  identity::SourceSpan sourceSpan;
};

// One conditional condition is either a bare bool parameter reference or an
// `a == b` equality comparison of two parameter references. Exactly one of the
// two Maybe fields is populated; the condition kind is discriminated by which.
struct PendingConditionalCondition final {
  zc::Maybe<HirParameterReferenceExpression> parameter;
  zc::Maybe<PendingEqualityCondition> equality;
};

struct PendingConditionalReturn final {
  PendingConditionalCondition condition;
  PendingConditionalArm thenArm;
  PendingConditionalArm elseArm;
  identity::SourceSpan conditionalSpan;
};

struct PendingLoopReturn final {
  HirParameterReferenceExpression condition;
  checker::checked::CanonicalConstValue returnLiteral;
  identity::SemanticTypeId returnType;
  identity::SourceSpan loopSpan;
  identity::SourceSpan returnValueSpan;
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
  zc::Vector<PendingLocalWriteValue> localWriteValues;
  zc::Maybe<HirLocalReferenceExpression> localReference;
  zc::Maybe<HirLocalFieldProjectionExpression> localFieldProjection;
  zc::Maybe<HirParameterReferenceExpression> parameterReference;
  zc::Maybe<HirParameterIndexExpression> parameterIndex;
  zc::Maybe<HirParameterReborrowExpression> parameterReborrow;
  zc::Maybe<HirLocalBorrowExpression> localBorrow;
  zc::Maybe<PendingSequentialLocalReturn> sequentialLocalReturn;
  zc::Maybe<identity::SourceSpan> unsafeBlockSpan;
  zc::Array<uint8_t> orderingKey;
  zc::Maybe<PendingConditionalReturn> conditionalReturn;
  zc::Maybe<PendingLoopReturn> loopReturn;
  // Populated when the body is `return <a CMP b>`: the comparison result flows
  // straight into the Return terminator (no conditional). Reuses the equality
  // operand/operator carrier since the operand shape is identical.
  zc::Maybe<PendingEqualityCondition> comparisonReturn;
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
  // Sequential-local shape: N leading `let id: T = <literal | aggregate |
  // identifier>;` statements followed by `return <identifier>;`. Per-binding
  // detail is derived from the block node on demand via sequentialLocalShape so
  // this struct stays copyable; only the discriminator is stored here.
  bool isSequentialLocalReturn = false;
  bool returnsReceiverCall = false;
  bool returnsLocalBorrow = false;
  zc::Maybe<ast::NodeId> unsafeBlock;
  bool isConditional = false;
  ast::NodeId condition;
  // When the condition is an `a CMP b` relational comparison, the condition node
  // is a BinaryExpr and these hold its two operands. Each operand is either an
  // IdentExpr parameter reference or a scalar literal; the two literal flags
  // record which. At least one operand is a parameter.
  bool conditionIsEquality = false;
  ast::NodeId conditionLeft;
  ast::NodeId conditionRight;
  bool conditionLeftIsLiteral = false;
  bool conditionRightIsLiteral = false;
  ast::NodeId thenReturnValue;
  ast::NodeId elseReturnValue;
  // When the body is a single `return <BinaryExpr comparison>` of two same-typed
  // scalar operands, `shape.value` is the BinaryExpr node and these hold its two
  // operands. Each operand is an IdentExpr parameter reference or a scalar
  // literal; at least one is a parameter. The selected relational operator comes
  // from the checked comparison call fact, not from the shape.
  bool returnsComparison = false;
  ast::NodeId comparisonLeft;
  ast::NodeId comparisonRight;
  bool comparisonLeftIsLiteral = false;
  bool comparisonRightIsLiteral = false;
  bool isLoop = false;
  ast::NodeId loopCondition{};
  ast::NodeId loopStatement{};
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

// One classified operand of a sequential primitive-binary initializer. `node` is
// the AST operand node; the kind discriminates a scalar literal, a parameter
// reference, a reference to an earlier local (with `referencedLocal` its
// zero-based binding index), or a nested one-level primitive binary. A nested
// operand additionally carries its inner operation and the two classified leaf
// operands (each a literal, parameter, or earlier local -- never a further
// binary, which keeps two-level nesting unsupported).
struct SequentialBinaryLeafOperand final {
  ast::NodeId node;
  SequentialBinaryOperandKind kind;
  size_t referencedLocal = 0;
};

struct SequentialBinaryOperand final {
  ast::NodeId node;
  SequentialBinaryOperandKind kind;
  size_t referencedLocal = 0;
  zc::Maybe<checker::PrimitiveOperation> nestedOperation;
  zc::Maybe<SequentialBinaryLeafOperand> nestedLeft;
  zc::Maybe<SequentialBinaryLeafOperand> nestedRight;
};

struct SequentialLocalBinding final {
  ast::NodeId declarator;
  ast::NodeId pattern;
  ast::NodeId initializer;
  SequentialInitializerKind initializerKind;
  // Populated for LocalReference: the zero-based index of the earlier binding it
  // references. Unused for the other kinds.
  size_t referencedLocal = 0;
  // Populated for PrimitiveBinary: the two operand classifications.
  zc::Maybe<SequentialBinaryOperand> leftOperand;
  zc::Maybe<SequentialBinaryOperand> rightOperand;
};

struct SequentialLocalShape final {
  ast::NodeId body;
  ast::NodeId returnStatement;
  ast::NodeId returnValue;
  zc::Vector<SequentialLocalBinding> bindings;
  // The zero-based index of the returned local, or none when the return names a
  // parameter.
  zc::Maybe<size_t> returnsLocal;
};

// Classifies a function body as the sequential N-local shape: N (>= 2) leading
// `let id: T = <literal | aggregate | identifier>;` statements followed by a
// single `return <identifier>;`. Each identifier initializer names an earlier
// local or a parameter; the return names one of the locals or a parameter.
// Returns none for any other body. The result is recomputed from the AST wher-
// ever the per-binding layout is needed, keeping FunctionReturnShape copyable
// and guaranteeing the producer and verifiers derive one identical layout.
zc::Maybe<SequentialLocalShape> sequentialLocalShape(const ast::Tree& tree, ast::NodeId body) {
  if (!tree.contains(body) || tree.node(body).kind != ast::SyntaxKind::BlockStmt) return zc::none;
  const auto& block = tree.node(body);
  const ast::NodeList statements{block.payload.words[ast::kBlockStmtStmtsFirstWord],
                                 block.payload.words[ast::kBlockStmtStmtsSizeWord]};
  if (!tree.contains(statements) || statements.size < 3) return zc::none;
  const size_t bindingCount = statements.size - 1;
  SequentialLocalShape shape{};
  shape.body = body;
  for (size_t index = 0; index < bindingCount; ++index) {
    auto declaratorNode = localDeclarator(tree, tree.list(statements)[index]);
    if (declaratorNode == zc::none) return zc::none;
    ast::NodeId declarator;
    ZC_IF_SOME(value, declaratorNode) { declarator = value; }
    const ast::NodeId pattern(
        tree.node(declarator).payload.words[ast::kVariableDeclaratorPatternWord]);
    const ast::NodeId initializer(
        tree.node(declarator).payload.words[ast::kVariableDeclaratorInitWord]);
    if (!tree.contains(initializer)) return zc::none;
    SequentialInitializerKind kind;
    size_t referencedLocal = 0;
    zc::Maybe<SequentialBinaryOperand> leftOperand;
    zc::Maybe<SequentialBinaryOperand> rightOperand;
    if (isScalarLiteral(tree.node(initializer).kind)) {
      kind = SequentialInitializerKind::Literal;
    } else if (tree.node(initializer).kind == ast::SyntaxKind::StructLiteralExpr) {
      kind = SequentialInitializerKind::Aggregate;
    } else if (tree.node(initializer).kind == ast::SyntaxKind::IdentExpr) {
      kind = SequentialInitializerKind::ParameterReference;
      for (size_t earlier = 0; earlier < index; ++earlier) {
        if (matchesLocalReference(tree, shape.bindings[earlier].pattern, initializer)) {
          kind = SequentialInitializerKind::LocalReference;
          referencedLocal = earlier;
          break;
        }
      }
    } else if (tree.node(initializer).kind == ast::SyntaxKind::BinaryExpr &&
               isPrimitiveBinaryOperator(static_cast<ast::BinaryOperatorKind>(
                   tree.node(initializer).payload.words[ast::kBinaryExprOpWord]))) {
      // A primitive binary operation whose operands are each a scalar literal, a
      // parameter reference, a reference to an earlier local, or (for at most one
      // operand) a nested one-level primitive binary, with at least one reference
      // or nested operand. Each leaf operand is classified against the earlier
      // bindings the same way an identifier initializer is.
      const ast::NodeId binaryLeft(tree.node(initializer).payload.words[ast::kBinaryExprLhsWord]);
      const ast::NodeId binaryRight(tree.node(initializer).payload.words[ast::kBinaryExprRhsWord]);
      if (!tree.contains(binaryLeft) || !tree.contains(binaryRight)) return zc::none;
      // Classifies a leaf operand: a scalar literal, a parameter reference, or a
      // reference to an earlier local. A leaf operand is never a binary, so this
      // keeps two-level nesting unsupported.
      auto classifyLeaf = [&](ast::NodeId operandNode) -> zc::Maybe<SequentialBinaryLeafOperand> {
        if (isScalarLiteral(tree.node(operandNode).kind)) {
          return SequentialBinaryLeafOperand{operandNode, SequentialBinaryOperandKind::Literal, 0};
        }
        if (tree.node(operandNode).kind != ast::SyntaxKind::IdentExpr) return zc::none;
        for (size_t earlier = 0; earlier < index; ++earlier) {
          if (matchesLocalReference(tree, shape.bindings[earlier].pattern, operandNode)) {
            return SequentialBinaryLeafOperand{
                operandNode, SequentialBinaryOperandKind::LocalReference, earlier};
          }
        }
        return SequentialBinaryLeafOperand{operandNode,
                                           SequentialBinaryOperandKind::ParameterReference, 0};
      };
      auto classifyOperand = [&](ast::NodeId operandNode) -> zc::Maybe<SequentialBinaryOperand> {
        if (tree.node(operandNode).kind == ast::SyntaxKind::BinaryExpr &&
            isPrimitiveBinaryOperator(static_cast<ast::BinaryOperatorKind>(
                tree.node(operandNode).payload.words[ast::kBinaryExprOpWord]))) {
          // A nested one-level primitive binary. Its two leaf operands are
          // classified; at least one must be a reference and neither may be a
          // further binary.
          const ast::NodeId nestedLeft(
              tree.node(operandNode).payload.words[ast::kBinaryExprLhsWord]);
          const ast::NodeId nestedRight(
              tree.node(operandNode).payload.words[ast::kBinaryExprRhsWord]);
          if (!tree.contains(nestedLeft) || !tree.contains(nestedRight)) return zc::none;
          auto innerLeft = classifyLeaf(nestedLeft);
          auto innerRight = classifyLeaf(nestedRight);
          if (innerLeft == zc::none || innerRight == zc::none) return zc::none;
          bool innerLeftLiteral = false;
          bool innerRightLiteral = false;
          ZC_IF_SOME(value, innerLeft) {
            innerLeftLiteral = value.kind == SequentialBinaryOperandKind::Literal;
          }
          ZC_IF_SOME(value, innerRight) {
            innerRightLiteral = value.kind == SequentialBinaryOperandKind::Literal;
          }
          if (innerLeftLiteral && innerRightLiteral) return zc::none;
          zc::Maybe<checker::PrimitiveOperation> nestedOperation;
          ZC_IF_SOME(kind, checker::OperatorKind::fromBinary(static_cast<ast::BinaryOperatorKind>(
                               tree.node(operandNode).payload.words[ast::kBinaryExprOpWord]))) {
            if (kind.variant().is<checker::PrimitiveOperation>()) {
              nestedOperation = kind.variant().get<checker::PrimitiveOperation>();
            }
          }
          if (nestedOperation == zc::none) return zc::none;
          return SequentialBinaryOperand{operandNode,
                                         SequentialBinaryOperandKind::NestedBinary,
                                         0,
                                         zc::mv(nestedOperation),
                                         zc::mv(innerLeft),
                                         zc::mv(innerRight)};
        }
        auto leaf = classifyLeaf(operandNode);
        if (leaf == zc::none) return zc::none;
        SequentialBinaryOperand operand{};
        ZC_IF_SOME(value, leaf) {
          operand.node = value.node;
          operand.kind = value.kind;
          operand.referencedLocal = value.referencedLocal;
        }
        return operand;
      };
      // At most one operand may be a nested binary in this slice.
      const bool leftIsNested = tree.node(binaryLeft).kind == ast::SyntaxKind::BinaryExpr;
      const bool rightIsNested = tree.node(binaryRight).kind == ast::SyntaxKind::BinaryExpr;
      if (leftIsNested && rightIsNested) return zc::none;
      auto left = classifyOperand(binaryLeft);
      auto right = classifyOperand(binaryRight);
      if (left == zc::none || right == zc::none) return zc::none;
      bool leftIsLiteral = false;
      bool rightIsLiteral = false;
      ZC_IF_SOME(value, left) {
        leftIsLiteral = value.kind == SequentialBinaryOperandKind::Literal;
      }
      ZC_IF_SOME(value, right) {
        rightIsLiteral = value.kind == SequentialBinaryOperandKind::Literal;
      }
      // At least one operand must be a reference or a nested binary; a
      // literal-vs-literal operation has no place to lower.
      if (leftIsLiteral && rightIsLiteral) return zc::none;
      kind = SequentialInitializerKind::PrimitiveBinary;
      leftOperand = zc::mv(left);
      rightOperand = zc::mv(right);
    } else {
      return zc::none;
    }
    shape.bindings.add(SequentialLocalBinding{declarator, pattern, initializer, kind,
                                              referencedLocal, zc::mv(leftOperand),
                                              zc::mv(rightOperand)});
  }
  auto returnItem = statementItem(tree, tree.list(statements)[statements.size - 1]);
  if (returnItem == zc::none) return zc::none;
  ast::NodeId returnNode;
  ZC_IF_SOME(value, returnItem) { returnNode = value; }
  if (tree.node(returnNode).kind != ast::SyntaxKind::ReturnStmt) return zc::none;
  ast::NodeId returnValue(tree.node(returnNode).payload.words[ast::kReturnStmtValueWord]);
  if (!tree.contains(returnValue)) return zc::none;
  // Unwrap an unsafe-block-wrapped return so `return unsafe { x }` classifies the
  // same as `return x`; the unsafe boundary itself is retained by the outer
  // FunctionReturnShape via its own unsafe-block detection.
  if (tree.node(returnValue).kind == ast::SyntaxKind::UnsafeBlockExpr) {
    const ast::NodeId unsafeBody(
        tree.node(returnValue).payload.words[ast::kUnsafeBlockExprBodyWord]);
    if (!tree.contains(unsafeBody) || tree.node(unsafeBody).kind != ast::SyntaxKind::BlockStmt) {
      return zc::none;
    }
    const auto& unsafeBodyNode = tree.node(unsafeBody);
    const ast::NodeList unsafeStatements{
        unsafeBodyNode.payload.words[ast::kBlockStmtStmtsFirstWord],
        unsafeBodyNode.payload.words[ast::kBlockStmtStmtsSizeWord]};
    if (!tree.contains(unsafeStatements) || unsafeStatements.empty()) return zc::none;
    auto innerItem = statementItem(tree, tree.list(unsafeStatements)[unsafeStatements.size - 1]);
    if (innerItem == zc::none) return zc::none;
    ast::NodeId innerStatement;
    ZC_IF_SOME(value, innerItem) { innerStatement = value; }
    if (tree.node(innerStatement).kind != ast::SyntaxKind::ExpressionStatement) return zc::none;
    returnValue = ast::NodeId(
        tree.node(innerStatement).payload.words[ast::kExpressionStatementExpressionWord]);
    if (!tree.contains(returnValue)) return zc::none;
  }
  if (tree.node(returnValue).kind != ast::SyntaxKind::IdentExpr) return zc::none;
  shape.returnStatement = returnNode;
  shape.returnValue = returnValue;
  for (size_t index = 0; index < shape.bindings.size(); ++index) {
    if (matchesLocalReference(tree, shape.bindings[index].pattern, returnValue)) {
      shape.returnsLocal = index;
      break;
    }
  }
  return shape;
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
  if (statements.size == 1) {
    auto conditionalItem = statementItem(tree, tree.list(statements)[0]);
    if (conditionalItem != zc::none) {
      ast::NodeId conditionalStmt;
      ZC_IF_SOME(value, conditionalItem) { conditionalStmt = value; }
      if (tree.node(conditionalStmt).kind == ast::SyntaxKind::IfStmt) {
        const auto& ifNode = tree.node(conditionalStmt);
        const ast::NodeId thenStmt(ifNode.payload.words[ast::kIfStmtThenStmtWord]);
        const ast::NodeId elseStmt(ifNode.payload.words[ast::kIfStmtElseStmtWord]);
        if (tree.contains(thenStmt) && tree.contains(elseStmt) &&
            tree.node(thenStmt).kind == ast::SyntaxKind::BlockStmt &&
            tree.node(elseStmt).kind == ast::SyntaxKind::BlockStmt) {
          auto branchReturnValue = [&](ast::NodeId branch) -> zc::Maybe<ast::NodeId> {
            const auto& branchNode = tree.node(branch);
            const ast::NodeList branchStmts{branchNode.payload.words[ast::kBlockStmtStmtsFirstWord],
                                            branchNode.payload.words[ast::kBlockStmtStmtsSizeWord]};
            if (!tree.contains(branchStmts) || branchStmts.empty()) return zc::none;
            auto tail = statementItem(tree, tree.list(branchStmts)[branchStmts.size - 1]);
            if (tail == zc::none) return zc::none;
            ast::NodeId tailStmt;
            ZC_IF_SOME(value, tail) { tailStmt = value; }
            if (tree.node(tailStmt).kind != ast::SyntaxKind::ReturnStmt) return zc::none;
            const ast::NodeId returnValue(
                tree.node(tailStmt).payload.words[ast::kReturnStmtValueWord]);
            if (!tree.contains(returnValue)) return zc::none;
            return returnValue;
          };
          auto thenValue = branchReturnValue(thenStmt);
          auto elseValue = branchReturnValue(elseStmt);
          if (thenValue != zc::none && elseValue != zc::none) {
            ast::NodeId thenNode;
            ast::NodeId elseNode;
            ZC_IF_SOME(value, thenValue) { thenNode = value; }
            ZC_IF_SOME(value, elseValue) { elseNode = value; }
            const ast::NodeId condition(ifNode.payload.words[ast::kIfStmtCondWord]);
            FunctionReturnShape shape{};
            shape.body = body;
            shape.returnStatement = conditionalStmt;
            shape.value = conditionalStmt;
            shape.isConditional = true;
            shape.condition = condition;
            shape.thenReturnValue = thenNode;
            shape.elseReturnValue = elseNode;
            // Detect the relational-comparison condition: a comparison
            // BinaryExpr for one of the six relational operators whose operands
            // are each an IdentExpr parameter reference or a scalar literal, with
            // at least one parameter operand. A bare identifier condition keeps
            // the parameter-reference lowering.
            if (tree.contains(condition) &&
                tree.node(condition).kind == ast::SyntaxKind::BinaryExpr &&
                isRelationalBinaryOperator(static_cast<ast::BinaryOperatorKind>(
                    tree.node(condition).payload.words[ast::kBinaryExprOpWord]))) {
              const ast::NodeId left(tree.node(condition).payload.words[ast::kBinaryExprLhsWord]);
              const ast::NodeId right(tree.node(condition).payload.words[ast::kBinaryExprRhsWord]);
              if (!tree.contains(left) || !tree.contains(right)) return zc::none;
              const bool leftIdent = tree.node(left).kind == ast::SyntaxKind::IdentExpr;
              const bool rightIdent = tree.node(right).kind == ast::SyntaxKind::IdentExpr;
              const bool leftLiteral = isScalarLiteral(tree.node(left).kind);
              const bool rightLiteral = isScalarLiteral(tree.node(right).kind);
              if ((!leftIdent && !leftLiteral) || (!rightIdent && !rightLiteral) ||
                  (!leftIdent && !rightIdent)) {
                return zc::none;
              }
              shape.conditionIsEquality = true;
              shape.conditionLeft = left;
              shape.conditionRight = right;
              shape.conditionLeftIsLiteral = !leftIdent;
              shape.conditionRightIsLiteral = !rightIdent;
            }
            return shape;
          }
        }
      }
    }
  }
  auto returnStatement = statementItem(tree, tree.list(statements)[statements.size - 1]);
  if (returnStatement == zc::none) return zc::none;
  ZC_IF_SOME(statement, returnStatement) {
    if (tree.node(statement).kind != ast::SyntaxKind::ReturnStmt) return zc::none;
  }
  ast::NodeId returnNode;
  ZC_IF_SOME(statement, returnStatement) { returnNode = statement; }
  ast::NodeId value(tree.node(returnNode).payload.words[ast::kReturnStmtValueWord]);
  if (!tree.contains(value)) return zc::none;
  if (statements.size == 2) {
    // Admitted loop shape: a leading `while` loop with a bare identifier
    // condition and an empty body, followed by a scalar return.
    auto leadingItem = statementItem(tree, tree.list(statements)[0]);
    if (leadingItem != zc::none) {
      ast::NodeId leadingStmt;
      ZC_IF_SOME(item, leadingItem) { leadingStmt = item; }
      if (tree.node(leadingStmt).kind == ast::SyntaxKind::WhileStmt) {
        const auto& loop = tree.node(leadingStmt);
        const ast::NodeId loopCondition(loop.payload.words[ast::kWhileStmtCondWord]);
        const ast::NodeId loopBody(loop.payload.words[ast::kWhileStmtBodyWord]);
        if (!tree.contains(loopCondition) ||
            tree.node(loopCondition).kind != ast::SyntaxKind::IdentExpr ||
            !tree.contains(loopBody) || tree.node(loopBody).kind != ast::SyntaxKind::BlockStmt ||
            !isScalarLiteral(tree.node(value).kind)) {
          return zc::none;
        }
        const auto& loopBlock = tree.node(loopBody);
        const ast::NodeList loopStatements{loopBlock.payload.words[ast::kBlockStmtStmtsFirstWord],
                                           loopBlock.payload.words[ast::kBlockStmtStmtsSizeWord]};
        if (!tree.contains(loopStatements) || !loopStatements.empty()) return zc::none;
        FunctionReturnShape shape{};
        shape.body = body;
        shape.returnStatement = returnNode;
        shape.value = value;
        shape.isLoop = true;
        shape.loopCondition = loopCondition;
        shape.loopStatement = leadingStmt;
        return shape;
      }
    }
  }
  zc::Maybe<ast::NodeId> unsafeBlock;
  if (tree.node(value).kind == ast::SyntaxKind::UnsafeBlockExpr) {
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
    // The scalar-return and parameter-reborrow paths lower unsafe blocks for
    // single-statement shapes; other single-statement inner expressions keep
    // the shape but drop the unsafe-block marker.
    if (statements.size != 1 || isScalarLiteral(tree.node(innerValue).kind) ||
        reborrowReference(tree, innerValue) != zc::none) {
      unsafeBlock = value;
    }
    value = innerValue;
  }
  if (statements.size == 1) {
    // A single `return <BinaryExpr>` returns the operation result directly. The
    // BinaryExpr is one of the six relational comparisons (result bool) or one of
    // the twelve arithmetic/bitwise operators (result operand type) over two
    // operands, each an IdentExpr parameter reference or a scalar literal, with
    // at least one parameter. Which operators are supported is a checker
    // decision; the shape only requires the primitive-binary structure.
    if (tree.contains(value) && tree.node(value).kind == ast::SyntaxKind::BinaryExpr &&
        isPrimitiveBinaryOperator(static_cast<ast::BinaryOperatorKind>(
            tree.node(value).payload.words[ast::kBinaryExprOpWord]))) {
      const ast::NodeId left(tree.node(value).payload.words[ast::kBinaryExprLhsWord]);
      const ast::NodeId right(tree.node(value).payload.words[ast::kBinaryExprRhsWord]);
      if (!tree.contains(left) || !tree.contains(right)) return zc::none;
      const bool leftIdent = tree.node(left).kind == ast::SyntaxKind::IdentExpr;
      const bool rightIdent = tree.node(right).kind == ast::SyntaxKind::IdentExpr;
      const bool leftLiteral = isScalarLiteral(tree.node(left).kind);
      const bool rightLiteral = isScalarLiteral(tree.node(right).kind);
      if ((!leftIdent && !leftLiteral) || (!rightIdent && !rightLiteral) ||
          (!leftIdent && !rightIdent)) {
        return zc::none;
      }
      FunctionReturnShape shape{};
      shape.body = body;
      shape.returnStatement = returnNode;
      shape.value = value;
      shape.returnsComparison = true;
      shape.comparisonLeft = left;
      shape.comparisonRight = right;
      shape.comparisonLeftIsLiteral = !leftIdent;
      shape.comparisonRightIsLiteral = !rightIdent;
      shape.unsafeBlock = zc::mv(unsafeBlock);
      return shape;
    }
    FunctionReturnShape shape{};
    shape.body = body;
    shape.returnStatement = returnNode;
    shape.value = value;
    shape.unsafeBlock = zc::mv(unsafeBlock);
    return shape;
  }
  {
    auto sequential = sequentialLocalShape(tree, body);
    if (sequential != zc::none) {
      FunctionReturnShape shape{};
      shape.body = body;
      shape.returnStatement = returnNode;
      shape.value = value;
      shape.returnsLocal = true;
      shape.localReference = value;
      shape.isSequentialLocalReturn = true;
      shape.unsafeBlock = zc::mv(unsafeBlock);
      return shape;
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
    // A local borrow requires the referent to be initialized at the borrow point.
    if (localBorrow != zc::none) return zc::none;
    FunctionReturnShape shape{};
    shape.body = body;
    shape.returnStatement = returnNode;
    shape.value = value;
    shape.localPattern = pattern;
    shape.returnsLocal = true;
    shape.localReference = localReference;
    shape.returnsLocalField = returnsLocalField;
    shape.returnsLocalReborrow = reborrow != zc::none;
    shape.returnsReceiverCall = returnsReceiverCall;
    shape.returnsLocalBorrow = localBorrow != zc::none;
    shape.unsafeBlock = zc::mv(unsafeBlock);
    return shape;
  }
  if (tree.contains(initializer) && !isScalarLiteral(tree.node(initializer).kind) &&
      tree.node(initializer).kind != ast::SyntaxKind::CallExpression &&
      tree.node(initializer).kind != ast::SyntaxKind::IdentExpr &&
      tree.node(initializer).kind != ast::SyntaxKind::StructLiteralExpr) {
    return zc::none;
  }
  if (statements.size == 2) {
    FunctionReturnShape shape{};
    shape.body = body;
    shape.returnStatement = returnNode;
    shape.value = value;
    shape.localPattern = pattern;
    shape.localInitializer = initializer;
    shape.returnsLocal = true;
    shape.localReference = localReference;
    shape.returnsLocalField = returnsLocalField;
    shape.returnsLocalReborrow = reborrow != zc::none;
    shape.returnsReceiverCall = returnsReceiverCall;
    shape.returnsLocalBorrow = localBorrow != zc::none;
    shape.unsafeBlock = zc::mv(unsafeBlock);
    return shape;
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
      if (!tree.contains(target) || !tree.contains(writeValue)) return zc::none;
      // A scalar-local write value is a scalar literal or an identifier reference
      // (a parameter, resolved downstream); a field write value stays
      // literal-only in this slice.
      const bool identValue = tree.node(writeValue).kind == ast::SyntaxKind::IdentExpr;
      if (!isScalarLiteral(tree.node(writeValue).kind) && !identValue) return zc::none;
      if (tree.node(target).kind == ast::SyntaxKind::IdentExpr) continue;
      if (identValue) return zc::none;
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
  FunctionReturnShape shape{};
  shape.body = body;
  shape.returnStatement = returnNode;
  shape.value = value;
  shape.localPattern = pattern;
  shape.localInitializer = zc::mv(localInitializer);
  shape.localWrites = ast::NodeList{statements.first + 1, statements.size - 2};
  shape.returnsLocal = true;
  shape.localReference = localReference;
  shape.returnsLocalField = returnsLocalField;
  shape.returnsLocalReborrow = reborrow != zc::none;
  shape.returnsReceiverCall = returnsReceiverCall;
  shape.returnsLocalBorrow = localBorrow != zc::none;
  shape.unsafeBlock = zc::mv(unsafeBlock);
  return shape;
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
       zc::Vector<HirUnsafeBlockExpression>&& unsafeBlocks,
       zc::Vector<HirPrimitiveBinaryExpression>&& primitiveBinaryOperations,
       zc::Vector<HirConditionalExpression>&& conditionals,
       zc::Vector<HirLoopStatement>&& loops) noexcept
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
        unsafeBlocks(zc::mv(unsafeBlocks)),
        primitiveBinaryOperations(zc::mv(primitiveBinaryOperations)),
        conditionals(zc::mv(conditionals)),
        loops(zc::mv(loops)) {}

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
  zc::Vector<HirPrimitiveBinaryExpression> primitiveBinaryOperations;
  zc::Vector<HirConditionalExpression> conditionals;
  zc::Vector<HirLoopStatement> loops;
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
       zc::Vector<HirUnsafeBlockExpression>&& unsafeBlocks,
       zc::Vector<HirPrimitiveBinaryExpression>&& primitiveBinaryOperations,
       zc::Vector<HirConditionalExpression>&& conditionals,
       zc::Vector<HirLoopStatement>&& loops) noexcept
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
        unsafeBlocks(zc::mv(unsafeBlocks)),
        primitiveBinaryOperations(zc::mv(primitiveBinaryOperations)),
        conditionals(zc::mv(conditionals)),
        loops(zc::mv(loops)) {
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
  zc::Vector<HirPrimitiveBinaryExpression> primitiveBinaryOperations;
  zc::Vector<HirConditionalExpression> conditionals;
  zc::Vector<HirLoopStatement> loops;
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

zc::ArrayPtr<const HirConditionalExpression> VerifiedHirModule::conditionals() const noexcept {
  return impl->conditionals.asPtr();
}

zc::ArrayPtr<const HirPrimitiveBinaryExpression> VerifiedHirModule::primitiveBinaryOperations()
    const noexcept {
  return impl->primitiveBinaryOperations.asPtr();
}

zc::ArrayPtr<const HirLoopStatement> VerifiedHirModule::loops() const noexcept {
  return impl->loops.asPtr();
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
    append(output, " nargs="_zc);
    append(output, zc::str(call.arguments.size()));
    for (size_t index = 0; index < call.arguments.size(); ++index) {
      const auto& argument = call.arguments[index];
      auto argumentType = impl->semanticTypes.get(argument.type);
      if (!argumentType.is<type::SemanticTypeLookup>()) return zc::none;
      append(output, " arg"_zc);
      append(output, zc::str(index));
      append(output, "-type="_zc);
      append(output, zc::encodeHex(argumentType.get<type::SemanticTypeLookup>().key().bytes()));
      // An argument is a constant or a parameter reference; encode a discriminating
      // tag so the two shapes never collide in the canonical text.
      ZC_IF_SOME(value, argument.value) {
        append(output, " arg"_zc);
        append(output, zc::str(index));
        append(output, "-const="_zc);
        auto encoded = checker::signature::SignatureFactsCanonicalCodec::
            encodeCanonicalConstValueFromAuthority(value, impl->module, impl->identities,
                                                   impl->semanticTypes);
        if (encoded == zc::none) return zc::none;
        ZC_IF_SOME(bytes, encoded) { append(output, zc::encodeHex(bytes.asPtr())); }
      }
      ZC_IF_SOME(parameter, argument.parameter) {
        append(output, " arg"_zc);
        append(output, zc::str(index));
        append(output, "-param="_zc);
        append(output, zc::encodeHex(parameter.encode().asPtr()));
      }
    }
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
  for (const auto& equality : impl->primitiveBinaryOperations) {
    auto resultType = impl->semanticTypes.get(equality.type);
    if (!resultType.is<type::SemanticTypeLookup>()) return zc::none;
    append(output, "equality h"_zc);
    append(output, zc::str(equality.node.ordinal()));
    append(output, " left=h"_zc);
    append(output, zc::str(equality.left.ordinal()));
    append(output, " right=h"_zc);
    append(output, zc::str(equality.right.ordinal()));
    append(output, " type="_zc);
    append(output, zc::encodeHex(resultType.get<type::SemanticTypeLookup>().key().bytes()));
    append(output, "\n"_zc);
  }
  for (const auto& conditional : impl->conditionals) {
    auto resultType = impl->semanticTypes.get(conditional.type);
    if (!resultType.is<type::SemanticTypeLookup>()) return zc::none;
    append(output, "conditional h"_zc);
    append(output, zc::str(conditional.node.ordinal()));
    append(output, " condition=h"_zc);
    append(output, zc::str(conditional.condition.ordinal()));
    append(output, " then=h"_zc);
    append(output, zc::str(conditional.thenReturnValue.ordinal()));
    append(output, " else=h"_zc);
    append(output, zc::str(conditional.elseReturnValue.ordinal()));
    append(output, " type="_zc);
    append(output, zc::encodeHex(resultType.get<type::SemanticTypeLookup>().key().bytes()));
    append(output, "\n"_zc);
  }
  for (const auto& loop : impl->loops) {
    auto resultType = impl->semanticTypes.get(loop.type);
    if (!resultType.is<type::SemanticTypeLookup>()) return zc::none;
    append(output, "loop h"_zc);
    append(output, zc::str(loop.node.ordinal()));
    append(output, " condition=h"_zc);
    append(output, zc::str(loop.condition.ordinal()));
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
      if (shape.isConditional) {
        // Conditional shape: if/else where both branches return either a scalar
        // literal or a bare parameter reference. Arms may differ in kind.
        auto conditionTypeIndex = factIndex(facts.nodeTypes(), shape.condition);
        auto thenTypeIndex = factIndex(facts.nodeTypes(), shape.thenReturnValue);
        auto elseTypeIndex = factIndex(facts.nodeTypes(), shape.elseReturnValue);
        auto bodySpan = bound.parsedModule().spanFor(tree.node(shape.body).range);
        auto returnSpan = bound.parsedModule().spanFor(tree.node(shape.returnStatement).range);
        auto valueSpan = bound.parsedModule().spanFor(tree.node(shape.value).range);
        auto conditionSpan = bound.parsedModule().spanFor(tree.node(shape.condition).range);
        auto thenSpan = bound.parsedModule().spanFor(tree.node(shape.thenReturnValue).range);
        auto elseSpan = bound.parsedModule().spanFor(tree.node(shape.elseReturnValue).range);
        if (conditionTypeIndex == zc::none || thenTypeIndex == zc::none ||
            elseTypeIndex == zc::none || bodySpan == zc::none || returnSpan == zc::none ||
            valueSpan == zc::none || conditionSpan == zc::none || thenSpan == zc::none ||
            elseSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        const auto& signature = signatures.definitions[signatureSlot];
        const auto& root = signatures.roots[rootSlot];
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
        size_t thenTypeSlot = 0;
        size_t elseTypeSlot = 0;
        ZC_IF_SOME(index, thenTypeIndex) { thenTypeSlot = index; }
        ZC_IF_SOME(index, elseTypeIndex) { elseTypeSlot = index; }
        const auto thenType = facts.nodeTypes().entries()[thenTypeSlot].value;
        const auto elseType = facts.nodeTypes().entries()[elseTypeSlot].value;
        if (thenType != callable.success || elseType != callable.success) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        // Resolve the condition: either a bare bool parameter reference or an
        // `a == b` equality comparison of two same-typed scalar parameters. The
        // equality form consumes the checker Eq call fact.
        HirVisibility visibilityValue = HirVisibility::external();
        HirLinkage linkageValue = HirLinkage::Internal;
        ZC_IF_SOME(value, functionVisibility) { visibilityValue = zc::mv(value); }
        ZC_IF_SOME(value, functionLinkage) { linkageValue = value; }
        identity::SourceSpan bodySpanValue = definition.source.clone();
        identity::SourceSpan returnSpanValue = definition.source.clone();
        identity::SourceSpan valueSpanValue = definition.source.clone();
        ZC_IF_SOME(value, bodySpan) { bodySpanValue = value.clone(); }
        ZC_IF_SOME(value, returnSpan) { returnSpanValue = value.clone(); }
        ZC_IF_SOME(value, valueSpan) { valueSpanValue = value.clone(); }
        zc::Array<uint8_t> orderingKey = definition.key.encode();
        size_t conditionTypeSlot = 0;
        ZC_IF_SOME(index, conditionTypeIndex) { conditionTypeSlot = index; }
        const auto conditionType = facts.nodeTypes().entries()[conditionTypeSlot].value;
        PendingConditionalCondition pendingCondition;
        if (shape.conditionIsEquality) {
          // The comparison condition consumes the checked relational call fact
          // whose two arguments are the left and right operands. Each operand is
          // either a parameter reference or a scalar literal; at least one is a
          // parameter, from which the shared operand type is derived.
          auto leftTypeIndex = factIndex(facts.nodeTypes(), shape.conditionLeft);
          auto rightTypeIndex = factIndex(facts.nodeTypes(), shape.conditionRight);
          auto callIndex = factIndex(facts.calls(), shape.condition);
          auto leftSpan = bound.parsedModule().spanFor(tree.node(shape.conditionLeft).range);
          auto rightSpan = bound.parsedModule().spanFor(tree.node(shape.conditionRight).range);
          if (leftTypeIndex == zc::none || rightTypeIndex == zc::none || callIndex == zc::none ||
              leftSpan == zc::none || rightSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t leftTypeSlot = 0;
          size_t rightTypeSlot = 0;
          size_t callSlot = 0;
          ZC_IF_SOME(index, leftTypeIndex) { leftTypeSlot = index; }
          ZC_IF_SOME(index, rightTypeIndex) { rightTypeSlot = index; }
          ZC_IF_SOME(index, callIndex) { callSlot = index; }
          const auto operandType = facts.nodeTypes().entries()[leftTypeSlot].value;
          const auto rightType = facts.nodeTypes().entries()[rightTypeSlot].value;
          const auto& callFact = facts.calls().entries()[callSlot].value;
          const auto& call = callFact.invocation;
          const auto& selected = call.selected.variant();
          // Both operand type facts must share the primitive scalar type and the
          // comparison call fact must match the relational-comparison contract
          // exactly for one of the six supported operators.
          if (operandType != rightType || callFact.node != shape.condition ||
              !selected.is<checker::checked::PrimitiveCallable>() ||
              !isScalarComparisonOperation(
                  selected.get<checker::checked::PrimitiveCallable>().operation) ||
              call.calleeType != operandType || call.receiver != zc::none ||
              call.receiverMode != zc::none || call.receiverAdjustment != zc::none ||
              call.arguments.size() != 2 || call.arguments[0].sourceNode != shape.conditionLeft ||
              call.arguments[0].sourceType != operandType ||
              call.arguments[1].sourceNode != shape.conditionRight ||
              call.arguments[1].sourceType != operandType || call.successType != conditionType ||
              call.resultType != conditionType || call.substitutions != zc::none ||
              call.witnesses != zc::none || call.raises != zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          // Build one operand: a parameter operand resolves to a parameter
          // reference; a literal operand consumes its checked literal fact. Both
          // operands must share the derived operand type.
          bool operandRejected = false;
          auto buildOperand =
              [&](ast::NodeId operandNode, bool isLiteral,
                  const identity::SourceSpan& operandSpan) -> zc::Maybe<PendingConditionalArm> {
            if (!isLiteral) {
              auto parameter = resolvedCallableParameter(bound.bindings(), operandNode);
              if (parameter == zc::none) {
                operandRejected = true;
                return zc::none;
              }
              identity::CallableParameterId handle;
              ZC_IF_SOME(value, parameter) { handle = value; }
              auto authority = registries.callableParameter(handle);
              if (authority == zc::none) {
                operandRejected = true;
                return zc::none;
              }
              zc::Maybe<PendingConditionalArm> built;
              ZC_IF_SOME(entry, authority) {
                bool matches = false;
                for (const auto& parameterCandidate : parameters) {
                  if (parameterCandidate.key == entry.key() &&
                      parameterCandidate.type == operandType) {
                    matches = true;
                  }
                }
                if (!matches) {
                  operandRejected = true;
                } else {
                  auto reference =
                      HirParameterReferenceExpression{HirNodeId(), entry.key().clone(), operandType,
                                                      HirValueCategory::Place, operandSpan.clone()};
                  built = PendingConditionalArm{zc::none, zc::mv(reference), operandType,
                                                operandSpan.clone()};
                }
              }
              return built;
            }
            auto literalIndex = factIndex(facts.literals(), operandNode);
            if (literalIndex == zc::none) {
              operandRejected = true;
              return zc::none;
            }
            size_t literalSlot = 0;
            ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
            const auto& literalFact = facts.literals().entries()[literalSlot].value;
            return PendingConditionalArm{literalFact.literal.clone(), zc::none, operandType,
                                         operandSpan.clone()};
          };
          auto leftOperand = buildOperand(shape.conditionLeft, shape.conditionLeftIsLiteral,
                                          ZC_ASSERT_NONNULL(leftSpan));
          auto rightOperand = buildOperand(shape.conditionRight, shape.conditionRightIsLiteral,
                                           ZC_ASSERT_NONNULL(rightSpan));
          if (operandRejected || leftOperand == zc::none || rightOperand == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          pendingCondition.equality = PendingEqualityCondition{
              zc::mv(ZC_ASSERT_NONNULL(leftOperand)),
              zc::mv(ZC_ASSERT_NONNULL(rightOperand)),
              operandType,
              conditionType,
              selected.get<checker::checked::PrimitiveCallable>().operation,
              ZC_ASSERT_NONNULL(conditionSpan).clone()};
        } else {
          auto conditionParameter = resolvedCallableParameter(bound.bindings(), shape.condition);
          if (conditionParameter == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          identity::CallableParameterId conditionHandle;
          ZC_IF_SOME(value, conditionParameter) { conditionHandle = value; }
          auto conditionAuthority = registries.callableParameter(conditionHandle);
          if (conditionAuthority == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          ZC_IF_SOME(entry, conditionAuthority) {
            pendingCondition.parameter = HirParameterReferenceExpression{
                HirNodeId(), entry.key().clone(), conditionType, HirValueCategory::Place,
                ZC_ASSERT_NONNULL(conditionSpan).clone()};
          }
        }
        // Build one conditional arm from its return-value node. A scalar-literal
        // arm carries the checked literal fact; a bare-identifier arm resolves to
        // a function parameter reference (no literal fact is required for it).
        bool armRejected = false;
        auto buildArm =
            [&](ast::NodeId armNode, identity::SemanticTypeId armType,
                const identity::SourceSpan& armSpan) -> zc::Maybe<PendingConditionalArm> {
          if (tree.node(armNode).kind == ast::SyntaxKind::IdentExpr) {
            auto parameter = resolvedCallableParameter(bound.bindings(), armNode);
            if (parameter == zc::none) {
              armRejected = true;
              return zc::none;
            }
            identity::CallableParameterId handle;
            ZC_IF_SOME(value, parameter) { handle = value; }
            auto authority = registries.callableParameter(handle);
            if (authority == zc::none) {
              armRejected = true;
              return zc::none;
            }
            zc::Maybe<PendingConditionalArm> built;
            ZC_IF_SOME(entry, authority) {
              bool matches = false;
              for (const auto& parameterCandidate : parameters) {
                if (parameterCandidate.key == entry.key() && parameterCandidate.type == armType) {
                  matches = true;
                }
              }
              if (!matches) {
                armRejected = true;
              } else {
                auto reference =
                    HirParameterReferenceExpression{HirNodeId(), entry.key().clone(), armType,
                                                    HirValueCategory::Place, armSpan.clone()};
                built =
                    PendingConditionalArm{zc::none, zc::mv(reference), armType, armSpan.clone()};
              }
            }
            return built;
          }
          auto literalIndex = factIndex(facts.literals(), armNode);
          if (literalIndex == zc::none) {
            armRejected = true;
            return zc::none;
          }
          size_t literalSlot = 0;
          ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
          const auto& literalFact = facts.literals().entries()[literalSlot].value;
          return PendingConditionalArm{literalFact.literal.clone(), zc::none, armType,
                                       armSpan.clone()};
        };
        auto thenArm = buildArm(shape.thenReturnValue, thenType, ZC_ASSERT_NONNULL(thenSpan));
        auto elseArm = buildArm(shape.elseReturnValue, elseType, ZC_ASSERT_NONNULL(elseSpan));
        if (armRejected || thenArm == zc::none || elseArm == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        {
          auto conditionalReturn =
              PendingConditionalReturn{zc::mv(pendingCondition), zc::mv(ZC_ASSERT_NONNULL(thenArm)),
                                       zc::mv(ZC_ASSERT_NONNULL(elseArm)), valueSpanValue.clone()};
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
                                                          zc::none,
                                                          zc::none,
                                                          zc::mv(orderingKey),
                                                          zc::mv(conditionalReturn),
                                                          zc::none,
                                                          zc::none});
        }
        continue;
      }
      if (shape.isLoop) {
        // Loop shape: a `while` with a bool parameter condition and empty body,
        // followed by a scalar return.
        auto conditionTypeIndex = factIndex(facts.nodeTypes(), shape.loopCondition);
        auto returnTypeIndex = factIndex(facts.nodeTypes(), shape.value);
        auto returnLiteralIndex = factIndex(facts.literals(), shape.value);
        auto bodySpan = bound.parsedModule().spanFor(tree.node(shape.body).range);
        auto returnSpan = bound.parsedModule().spanFor(tree.node(shape.returnStatement).range);
        auto valueSpan = bound.parsedModule().spanFor(tree.node(shape.value).range);
        auto conditionSpan = bound.parsedModule().spanFor(tree.node(shape.loopCondition).range);
        auto loopSpan = bound.parsedModule().spanFor(tree.node(shape.loopStatement).range);
        if (conditionTypeIndex == zc::none || returnTypeIndex == zc::none ||
            returnLiteralIndex == zc::none || bodySpan == zc::none || returnSpan == zc::none ||
            valueSpan == zc::none || conditionSpan == zc::none || loopSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        const auto& signature = signatures.definitions[signatureSlot];
        const auto& root = signatures.roots[rootSlot];
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
        size_t returnTypeSlot = 0;
        size_t returnLiteralSlot = 0;
        ZC_IF_SOME(index, returnTypeIndex) { returnTypeSlot = index; }
        ZC_IF_SOME(index, returnLiteralIndex) { returnLiteralSlot = index; }
        const auto returnType = facts.nodeTypes().entries()[returnTypeSlot].value;
        const auto& returnLiteral = facts.literals().entries()[returnLiteralSlot].value;
        if (returnType != callable.success) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        auto conditionParameter = resolvedCallableParameter(bound.bindings(), shape.loopCondition);
        if (conditionParameter == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        identity::CallableParameterId conditionHandle;
        ZC_IF_SOME(value, conditionParameter) { conditionHandle = value; }
        auto conditionAuthority = registries.callableParameter(conditionHandle);
        if (conditionAuthority == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        HirVisibility visibilityValue = HirVisibility::external();
        HirLinkage linkageValue = HirLinkage::Internal;
        ZC_IF_SOME(value, functionVisibility) { visibilityValue = zc::mv(value); }
        ZC_IF_SOME(value, functionLinkage) { linkageValue = value; }
        identity::SourceSpan bodySpanValue = definition.source.clone();
        identity::SourceSpan returnSpanValue = definition.source.clone();
        identity::SourceSpan valueSpanValue = definition.source.clone();
        ZC_IF_SOME(value, bodySpan) { bodySpanValue = value.clone(); }
        ZC_IF_SOME(value, returnSpan) { returnSpanValue = value.clone(); }
        ZC_IF_SOME(value, valueSpan) { valueSpanValue = value.clone(); }
        zc::Array<uint8_t> orderingKey = definition.key.encode();
        size_t conditionTypeSlot = 0;
        ZC_IF_SOME(index, conditionTypeIndex) { conditionTypeSlot = index; }
        const auto conditionType = facts.nodeTypes().entries()[conditionTypeSlot].value;
        ZC_IF_SOME(entry, conditionAuthority) {
          auto conditionRef = HirParameterReferenceExpression{
              HirNodeId(), entry.key().clone(), conditionType, HirValueCategory::Place,
              ZC_ASSERT_NONNULL(conditionSpan).clone()};
          auto loopReturn =
              PendingLoopReturn{zc::mv(conditionRef), returnLiteral.literal.clone(), returnType,
                                ZC_ASSERT_NONNULL(loopSpan).clone(), valueSpanValue.clone()};
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
                                                          zc::none,
                                                          zc::none,
                                                          zc::mv(orderingKey),
                                                          zc::none,
                                                          zc::mv(loopReturn),
                                                          zc::none});
        }
        continue;
      }
      if (shape.returnsComparison) {
        // Comparison-return shape: `return <a CMP b>`. The comparison result is
        // the function's bool return value. This consumes the same checked
        // relational call fact as the equality-conditional condition, but the
        // result flows into the Return terminator rather than a SwitchInt.
        auto nodeTypeIndex = factIndex(facts.nodeTypes(), shape.value);
        auto leftTypeIndex = factIndex(facts.nodeTypes(), shape.comparisonLeft);
        auto rightTypeIndex = factIndex(facts.nodeTypes(), shape.comparisonRight);
        auto callIndex = factIndex(facts.calls(), shape.value);
        auto bodySpan = bound.parsedModule().spanFor(tree.node(shape.body).range);
        auto returnSpan = bound.parsedModule().spanFor(tree.node(shape.returnStatement).range);
        auto valueSpan = bound.parsedModule().spanFor(tree.node(shape.value).range);
        auto leftSpan = bound.parsedModule().spanFor(tree.node(shape.comparisonLeft).range);
        auto rightSpan = bound.parsedModule().spanFor(tree.node(shape.comparisonRight).range);
        if (nodeTypeIndex == zc::none || leftTypeIndex == zc::none || rightTypeIndex == zc::none ||
            callIndex == zc::none || bodySpan == zc::none || returnSpan == zc::none ||
            valueSpan == zc::none || leftSpan == zc::none || rightSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        const auto& signature = signatures.definitions[signatureSlot];
        const auto& root = signatures.roots[rootSlot];
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
        size_t nodeTypeSlot = 0;
        ZC_IF_SOME(index, nodeTypeIndex) { nodeTypeSlot = index; }
        const auto resultType = facts.nodeTypes().entries()[nodeTypeSlot].value;
        if (functionVisibility == zc::none || functionLinkage == zc::none ||
            signature.definitionKind != identity::DefinitionKind::Function ||
            root.sourceModule != module || root.canonicalDefinition != definition.definition ||
            callable.receiver != zc::none || callable.raises != zc::none ||
            callable.success != resultType ||
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
        size_t leftTypeSlot = 0;
        size_t rightTypeSlot = 0;
        size_t callSlot = 0;
        ZC_IF_SOME(index, leftTypeIndex) { leftTypeSlot = index; }
        ZC_IF_SOME(index, rightTypeIndex) { rightTypeSlot = index; }
        ZC_IF_SOME(index, callIndex) { callSlot = index; }
        const auto operandType = facts.nodeTypes().entries()[leftTypeSlot].value;
        const auto rightType = facts.nodeTypes().entries()[rightTypeSlot].value;
        const auto& callFact = facts.calls().entries()[callSlot].value;
        const auto& call = callFact.invocation;
        const auto& selected = call.selected.variant();
        // The call fact must match the primitive-binary contract exactly for one
        // of the supported operators. A comparison produces the bool return type;
        // an arithmetic/bitwise operator produces the operand type, so for
        // arithmetic the function return type (resultType) equals operandType.
        const auto selectedOperation =
            selected.is<checker::checked::PrimitiveCallable>()
                ? zc::Maybe<checker::PrimitiveOperation>(
                      selected.get<checker::checked::PrimitiveCallable>().operation)
                : zc::Maybe<checker::PrimitiveOperation>(zc::none);
        bool operationSupported = false;
        ZC_IF_SOME(op, selectedOperation) {
          operationSupported = isScalarComparisonOperation(op) ||
                               (isScalarArithmeticOperation(op) && resultType == operandType);
        }
        if (operandType != rightType || callFact.node != shape.value || !operationSupported ||
            call.calleeType != operandType || call.receiver != zc::none ||
            call.receiverMode != zc::none || call.receiverAdjustment != zc::none ||
            call.arguments.size() != 2 || call.arguments[0].sourceNode != shape.comparisonLeft ||
            call.arguments[0].sourceType != operandType ||
            call.arguments[1].sourceNode != shape.comparisonRight ||
            call.arguments[1].sourceType != operandType || call.successType != resultType ||
            call.resultType != resultType || call.substitutions != zc::none ||
            call.witnesses != zc::none || call.raises != zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        HirVisibility visibilityValue = HirVisibility::external();
        HirLinkage linkageValue = HirLinkage::Internal;
        ZC_IF_SOME(value, functionVisibility) { visibilityValue = zc::mv(value); }
        ZC_IF_SOME(value, functionLinkage) { linkageValue = value; }
        identity::SourceSpan bodySpanValue = definition.source.clone();
        identity::SourceSpan returnSpanValue = definition.source.clone();
        identity::SourceSpan valueSpanValue = definition.source.clone();
        ZC_IF_SOME(value, bodySpan) { bodySpanValue = value.clone(); }
        ZC_IF_SOME(value, returnSpan) { returnSpanValue = value.clone(); }
        ZC_IF_SOME(value, valueSpan) { valueSpanValue = value.clone(); }
        zc::Array<uint8_t> orderingKey = definition.key.encode();
        // Build one comparison operand: a parameter operand resolves to a
        // parameter reference; a literal operand consumes its checked literal
        // fact. Both operands share the derived operand type.
        bool operandRejected = false;
        auto buildOperand =
            [&](ast::NodeId operandNode, bool isLiteral,
                const identity::SourceSpan& operandSpan) -> zc::Maybe<PendingConditionalArm> {
          if (!isLiteral) {
            auto parameter = resolvedCallableParameter(bound.bindings(), operandNode);
            if (parameter == zc::none) {
              operandRejected = true;
              return zc::none;
            }
            identity::CallableParameterId handle;
            ZC_IF_SOME(value, parameter) { handle = value; }
            auto authority = registries.callableParameter(handle);
            if (authority == zc::none) {
              operandRejected = true;
              return zc::none;
            }
            zc::Maybe<PendingConditionalArm> built;
            ZC_IF_SOME(entry, authority) {
              bool matches = false;
              for (const auto& parameterCandidate : parameters) {
                if (parameterCandidate.key == entry.key() &&
                    parameterCandidate.type == operandType) {
                  matches = true;
                }
              }
              if (!matches) {
                operandRejected = true;
              } else {
                auto reference =
                    HirParameterReferenceExpression{HirNodeId(), entry.key().clone(), operandType,
                                                    HirValueCategory::Place, operandSpan.clone()};
                built = PendingConditionalArm{zc::none, zc::mv(reference), operandType,
                                              operandSpan.clone()};
              }
            }
            return built;
          }
          auto literalIndex = factIndex(facts.literals(), operandNode);
          if (literalIndex == zc::none) {
            operandRejected = true;
            return zc::none;
          }
          size_t literalSlot = 0;
          ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
          const auto& literalFact = facts.literals().entries()[literalSlot].value;
          return PendingConditionalArm{literalFact.literal.clone(), zc::none, operandType,
                                       operandSpan.clone()};
        };
        auto leftOperand = buildOperand(shape.comparisonLeft, shape.comparisonLeftIsLiteral,
                                        ZC_ASSERT_NONNULL(leftSpan));
        auto rightOperand = buildOperand(shape.comparisonRight, shape.comparisonRightIsLiteral,
                                         ZC_ASSERT_NONNULL(rightSpan));
        if (operandRejected || leftOperand == zc::none || rightOperand == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        auto comparisonReturn =
            PendingEqualityCondition{zc::mv(ZC_ASSERT_NONNULL(leftOperand)),
                                     zc::mv(ZC_ASSERT_NONNULL(rightOperand)),
                                     operandType,
                                     resultType,
                                     selected.get<checker::checked::PrimitiveCallable>().operation,
                                     ZC_ASSERT_NONNULL(valueSpan).clone()};
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
                                                        zc::none,
                                                        zc::none,
                                                        zc::mv(orderingKey),
                                                        zc::none,
                                                        zc::none,
                                                        zc::mv(comparisonReturn)});
        continue;
      }
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
      zc::Vector<PendingLocalWriteValue> localWriteValues;
      zc::Maybe<HirLocalReferenceExpression> localReference;
      zc::Maybe<HirLocalFieldProjectionExpression> localFieldProjection;
      zc::Maybe<HirParameterReferenceExpression> parameterReference;
      zc::Maybe<HirParameterIndexExpression> parameterIndex;
      zc::Maybe<HirParameterReborrowExpression> parameterReborrow;
      zc::Maybe<HirLocalBorrowExpression> localBorrow;
      if (shape.isSequentialLocalReturn) {
        auto sequentialShapeMaybe = sequentialLocalShape(tree, shape.body);
        if (sequentialShapeMaybe == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        SequentialLocalShape sequentialShape{};
        ZC_IF_SOME(value, sequentialShapeMaybe) { sequentialShape = zc::mv(value); }
        const auto returnTypeIndex = factIndex(facts.nodeTypes(), sequentialShape.returnValue);
        if (returnTypeIndex == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t returnTypeSlot = 0;
        ZC_IF_SOME(index, returnTypeIndex) { returnTypeSlot = index; }
        const auto sequentialType = facts.nodeTypes().entries()[returnTypeSlot].value;
        if (sequentialType != callable.success ||
            !typeExists(sequentialType, checkedModule.semanticTypes())) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        zc::Vector<PendingSequentialBinding> pendingBindings;
        zc::Vector<binder::OwnerLocalBindingId> localBindingIds;
        bool rejected = false;
        for (size_t bindingIndex = 0; bindingIndex < sequentialShape.bindings.size();
             ++bindingIndex) {
          const auto& binding = sequentialShape.bindings[bindingIndex];
          auto ownerBinding =
              ownerLocalBindingForPattern(bound.definitions(), binding.pattern, tree);
          auto patternSpan = bound.parsedModule().spanFor(tree.node(binding.pattern).range);
          auto initializerSpan = bound.parsedModule().spanFor(tree.node(binding.initializer).range);
          auto initializerTypeIndex = factIndex(facts.nodeTypes(), binding.initializer);
          if (ownerBinding == zc::none || patternSpan == zc::none || initializerSpan == zc::none ||
              initializerTypeIndex == zc::none ||
              !ownerLocalMatches(bound.definitions(), ZC_ASSERT_NONNULL(ownerBinding),
                                 binding.pattern, tree)) {
            rejected = true;
            break;
          }
          // Every binding shares the function result type in this slice.
          size_t initializerTypeSlot = 0;
          ZC_IF_SOME(index, initializerTypeIndex) { initializerTypeSlot = index; }
          if (facts.nodeTypes().entries()[initializerTypeSlot].value != sequentialType) {
            rejected = true;
            break;
          }
          // Locals must be distinct bindings.
          for (const auto existing : localBindingIds) {
            if (existing == ZC_ASSERT_NONNULL(ownerBinding)) rejected = true;
          }
          if (rejected) break;
          localBindingIds.add(ZC_ASSERT_NONNULL(ownerBinding));
          zc::Maybe<checker::checked::CanonicalConstValue> bindingLiteral;
          zc::Maybe<HirNominalAggregateExpression> bindingAggregate;
          zc::Maybe<identity::CallableParameterKey> bindingParameter;
          zc::Maybe<checker::PrimitiveOperation> bindingOperation;
          identity::SemanticTypeId bindingOperandType = sequentialType;
          zc::Maybe<PendingSequentialBinaryOperand> bindingLeftOperand;
          zc::Maybe<PendingSequentialBinaryOperand> bindingRightOperand;
          if (binding.initializerKind == SequentialInitializerKind::Literal) {
            auto literalIndex = factIndex(facts.literals(), binding.initializer);
            if (literalIndex == zc::none) {
              rejected = true;
              break;
            }
            size_t literalSlot = 0;
            ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
            const auto& literalFact = facts.literals().entries()[literalSlot].value;
            if (literalFact.type != sequentialType ||
                !sameSpan(literalFact.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan))) {
              rejected = true;
              break;
            }
            bindingLiteral = literalFact.literal.clone();
          } else if (binding.initializerKind == SequentialInitializerKind::Aggregate) {
            auto aggregateIndex = factIndex(facts.aggregates(), binding.initializer);
            if (aggregateIndex == zc::none) {
              rejected = true;
              break;
            }
            size_t aggregateSlot = 0;
            ZC_IF_SOME(index, aggregateIndex) { aggregateSlot = index; }
            const auto& sourceAggregate = facts.aggregates().entries()[aggregateSlot].value;
            if (sourceAggregate.node != binding.initializer ||
                !sourceAggregate.kind.variant().is<checker::checked::NominalAggregate>() ||
                sourceAggregate.resultType != sequentialType ||
                !sameSpan(sourceAggregate.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan))) {
              rejected = true;
              break;
            }
            zc::Vector<HirNominalAggregateElement> elements;
            for (const auto& sourceElement : sourceAggregate.elements) {
              if (sourceElement.field == zc::none ||
                  sourceElement.sourceType != sourceElement.destinationType ||
                  sourceElement.adjustment != zc::none) {
                rejected = true;
                break;
              }
              auto elementLiteral = factIndex(facts.literals(), sourceElement.sourceNode);
              auto elementSpan =
                  bound.parsedModule().spanFor(tree.node(sourceElement.sourceNode).range);
              if (elementLiteral == zc::none || elementSpan == zc::none) {
                rejected = true;
                break;
              }
              size_t elementSlot = 0;
              ZC_IF_SOME(index, elementLiteral) { elementSlot = index; }
              const auto& literalFact = facts.literals().entries()[elementSlot].value;
              if (literalFact.type != sourceElement.destinationType ||
                  !sameSpan(literalFact.sourceSpan, ZC_ASSERT_NONNULL(elementSpan))) {
                rejected = true;
                break;
              }
              ZC_IF_SOME(field, sourceElement.field) {
                elements.add(HirNominalAggregateElement{field, sourceElement.destinationType,
                                                        literalFact.literal.clone(),
                                                        ZC_ASSERT_NONNULL(elementSpan).clone()});
              }
            }
            if (rejected) break;
            bindingAggregate = HirNominalAggregateExpression{
                HirNodeId(),
                sourceAggregate.kind.variant().get<checker::checked::NominalAggregate>().definition,
                sequentialType,
                zc::mv(elements),
                HirValueCategory::Value,
                ZC_ASSERT_NONNULL(initializerSpan).clone()};
          } else if (binding.initializerKind == SequentialInitializerKind::LocalReference) {
            // A reference to an earlier local must resolve to that owner binding.
            auto referenceBinding = resolvedOwnerLocal(bound.bindings(), binding.initializer);
            if (referenceBinding == zc::none ||
                ZC_ASSERT_NONNULL(referenceBinding) != localBindingIds[binding.referencedLocal]) {
              rejected = true;
              break;
            }
          } else if (binding.initializerKind == SequentialInitializerKind::PrimitiveBinary) {
            // A primitive binary initializer: validate its checked call fact and
            // resolve each operand to a literal, a parameter, or an earlier local.
            auto callIndex = factIndex(facts.calls(), binding.initializer);
            if (callIndex == zc::none || binding.leftOperand == zc::none ||
                binding.rightOperand == zc::none) {
              rejected = true;
              break;
            }
            size_t callSlot = 0;
            ZC_IF_SOME(index, callIndex) { callSlot = index; }
            const auto& callFact = facts.calls().entries()[callSlot].value;
            const auto& call = callFact.invocation;
            const auto& selected = call.selected.variant();
            if (!selected.is<checker::checked::PrimitiveCallable>()) {
              rejected = true;
              break;
            }
            const auto operation = selected.get<checker::checked::PrimitiveCallable>().operation;
            const bool comparison = isScalarComparisonOperation(operation);
            const bool arithmetic = isScalarArithmeticOperation(operation);
            // The operand type is the shared argument type; a comparison yields
            // the (bool) result while an arithmetic operator yields the operand
            // type. The binding type is always the function result type.
            const auto binaryOperandType =
                call.arguments.size() == 2 ? call.arguments[0].sourceType : sequentialType;
            const bool operationSupported =
                comparison || (arithmetic && sequentialType == binaryOperandType);
            const ast::NodeId binaryLeft(
                tree.node(binding.initializer).payload.words[ast::kBinaryExprLhsWord]);
            const ast::NodeId binaryRight(
                tree.node(binding.initializer).payload.words[ast::kBinaryExprRhsWord]);
            if (!operationSupported || callFact.node != binding.initializer ||
                call.calleeType != binaryOperandType || call.receiver != zc::none ||
                call.receiverMode != zc::none || call.receiverAdjustment != zc::none ||
                call.arguments.size() != 2 || call.arguments[0].sourceNode != binaryLeft ||
                call.arguments[0].sourceType != binaryOperandType ||
                call.arguments[1].sourceNode != binaryRight ||
                call.arguments[1].sourceType != binaryOperandType ||
                call.successType != sequentialType || call.resultType != sequentialType ||
                call.substitutions != zc::none || call.witnesses != zc::none ||
                call.raises != zc::none) {
              rejected = true;
              break;
            }
            // Resolve each classified operand into a materializable operand. A
            // leaf resolver builds a literal/parameter/local operand; the operand
            // resolver additionally handles a nested one-level binary by
            // validating its own checked call fact keyed on the operand node.
            auto resolveLeaf = [&](const SequentialBinaryLeafOperand& leaf,
                                   identity::SemanticTypeId leafType)
                -> zc::Maybe<PendingSequentialBinaryLeafOperand> {
              auto leafSpan = bound.parsedModule().spanFor(tree.node(leaf.node).range);
              if (leafSpan == zc::none) return zc::none;
              if (leaf.kind == SequentialBinaryOperandKind::Literal) {
                auto leafLiteral = factIndex(facts.literals(), leaf.node);
                if (leafLiteral == zc::none) return zc::none;
                size_t leafLiteralSlot = 0;
                ZC_IF_SOME(index, leafLiteral) { leafLiteralSlot = index; }
                const auto& literalFact = facts.literals().entries()[leafLiteralSlot].value;
                if (literalFact.type != leafType) return zc::none;
                zc::Maybe<checker::checked::CanonicalConstValue> literalValue =
                    literalFact.literal.clone();
                zc::Maybe<identity::CallableParameterKey> noParameter;
                return PendingSequentialBinaryLeafOperand{SequentialBinaryOperandKind::Literal,
                                                          leafType,
                                                          ZC_ASSERT_NONNULL(leafSpan).clone(),
                                                          zc::mv(literalValue),
                                                          zc::mv(noParameter),
                                                          0};
              }
              if (leaf.kind == SequentialBinaryOperandKind::LocalReference) {
                if (leaf.referencedLocal >= localBindingIds.size()) return zc::none;
                auto referenceBinding = resolvedOwnerLocal(bound.bindings(), leaf.node);
                if (referenceBinding == zc::none ||
                    ZC_ASSERT_NONNULL(referenceBinding) != localBindingIds[leaf.referencedLocal]) {
                  return zc::none;
                }
                zc::Maybe<checker::checked::CanonicalConstValue> noLiteral;
                zc::Maybe<identity::CallableParameterKey> noParameter;
                return PendingSequentialBinaryLeafOperand{
                    SequentialBinaryOperandKind::LocalReference,
                    leafType,
                    ZC_ASSERT_NONNULL(leafSpan).clone(),
                    zc::mv(noLiteral),
                    zc::mv(noParameter),
                    leaf.referencedLocal};
              }
              auto parameterHandle = resolvedCallableParameter(bound.bindings(), leaf.node);
              if (parameterHandle == zc::none) return zc::none;
              zc::Maybe<identity::CallableParameterKey> resolvedKey;
              ZC_IF_SOME(handle, parameterHandle) {
                auto authority = registries.callableParameter(handle);
                ZC_IF_SOME(entry, authority) {
                  for (const auto& parameter : parameters) {
                    if (parameter.key == entry.key() && parameter.type == leafType) {
                      resolvedKey = entry.key().clone();
                    }
                  }
                }
              }
              if (resolvedKey == zc::none) return zc::none;
              zc::Maybe<checker::checked::CanonicalConstValue> noLiteral;
              return PendingSequentialBinaryLeafOperand{
                  SequentialBinaryOperandKind::ParameterReference,
                  leafType,
                  ZC_ASSERT_NONNULL(leafSpan).clone(),
                  zc::mv(noLiteral),
                  zc::mv(resolvedKey),
                  0};
            };
            auto resolveOperand = [&](const SequentialBinaryOperand& operand)
                -> zc::Maybe<PendingSequentialBinaryOperand> {
              auto operandSpan = bound.parsedModule().spanFor(tree.node(operand.node).range);
              if (operandSpan == zc::none) return zc::none;
              zc::Maybe<checker::PrimitiveOperation> noNestedOperation;
              zc::Maybe<PendingSequentialBinaryLeafOperand> noNestedLeft;
              zc::Maybe<PendingSequentialBinaryLeafOperand> noNestedRight;
              if (operand.kind == SequentialBinaryOperandKind::NestedBinary) {
                // A nested one-level binary carries its own checked call fact keyed
                // on the operand node; validate it as a same-typed arithmetic (or
                // comparison agreeing on type) binary of two same-typed leaves.
                auto nestedCallIndex = factIndex(facts.calls(), operand.node);
                if (nestedCallIndex == zc::none || operand.nestedLeft == zc::none ||
                    operand.nestedRight == zc::none || operand.nestedOperation == zc::none) {
                  return zc::none;
                }
                size_t nestedCallSlot = 0;
                ZC_IF_SOME(index, nestedCallIndex) { nestedCallSlot = index; }
                const auto& nestedFact = facts.calls().entries()[nestedCallSlot].value;
                const auto& nestedCall = nestedFact.invocation;
                const auto& nestedSelected = nestedCall.selected.variant();
                if (!nestedSelected.is<checker::checked::PrimitiveCallable>()) return zc::none;
                const auto nestedOp =
                    nestedSelected.get<checker::checked::PrimitiveCallable>().operation;
                const bool nestedComparison = isScalarComparisonOperation(nestedOp);
                const bool nestedArithmetic = isScalarArithmeticOperation(nestedOp);
                const auto nestedOperandType = nestedCall.arguments.size() == 2
                                                   ? nestedCall.arguments[0].sourceType
                                                   : binaryOperandType;
                // The nested result feeds the parent operand slot, so its result
                // type must equal the parent operand type; a comparison result
                // (bool) can only agree when the parent operand type is bool.
                const bool nestedSupported =
                    (nestedArithmetic && nestedOperandType == binaryOperandType) ||
                    (nestedComparison && binaryOperandType == nestedCall.resultType);
                const ast::NodeId nestedLeftNode(
                    tree.node(operand.node).payload.words[ast::kBinaryExprLhsWord]);
                const ast::NodeId nestedRightNode(
                    tree.node(operand.node).payload.words[ast::kBinaryExprRhsWord]);
                if (!nestedSupported || nestedOp != ZC_ASSERT_NONNULL(operand.nestedOperation) ||
                    nestedFact.node != operand.node || nestedCall.calleeType != nestedOperandType ||
                    nestedCall.receiver != zc::none || nestedCall.receiverMode != zc::none ||
                    nestedCall.receiverAdjustment != zc::none || nestedCall.arguments.size() != 2 ||
                    nestedCall.arguments[0].sourceNode != nestedLeftNode ||
                    nestedCall.arguments[0].sourceType != nestedOperandType ||
                    nestedCall.arguments[1].sourceNode != nestedRightNode ||
                    nestedCall.arguments[1].sourceType != nestedOperandType ||
                    nestedCall.resultType != binaryOperandType ||
                    nestedCall.substitutions != zc::none || nestedCall.witnesses != zc::none ||
                    nestedCall.raises != zc::none) {
                  return zc::none;
                }
                zc::Maybe<PendingSequentialBinaryLeafOperand> resolvedNestedLeft;
                zc::Maybe<PendingSequentialBinaryLeafOperand> resolvedNestedRight;
                ZC_IF_SOME(leaf, operand.nestedLeft) {
                  resolvedNestedLeft = resolveLeaf(leaf, nestedOperandType);
                }
                ZC_IF_SOME(leaf, operand.nestedRight) {
                  resolvedNestedRight = resolveLeaf(leaf, nestedOperandType);
                }
                if (resolvedNestedLeft == zc::none || resolvedNestedRight == zc::none) {
                  return zc::none;
                }
                zc::Maybe<checker::checked::CanonicalConstValue> noLiteral;
                zc::Maybe<identity::CallableParameterKey> noParameter;
                zc::Maybe<checker::PrimitiveOperation> nestedOperation = nestedOp;
                return PendingSequentialBinaryOperand{SequentialBinaryOperandKind::NestedBinary,
                                                      binaryOperandType,
                                                      ZC_ASSERT_NONNULL(operandSpan).clone(),
                                                      zc::mv(noLiteral),
                                                      zc::mv(noParameter),
                                                      0,
                                                      zc::mv(nestedOperation),
                                                      zc::mv(resolvedNestedLeft),
                                                      zc::mv(resolvedNestedRight)};
              }
              if (operand.kind == SequentialBinaryOperandKind::Literal) {
                auto operandLiteral = factIndex(facts.literals(), operand.node);
                if (operandLiteral == zc::none) return zc::none;
                size_t operandLiteralSlot = 0;
                ZC_IF_SOME(index, operandLiteral) { operandLiteralSlot = index; }
                const auto& literalFact = facts.literals().entries()[operandLiteralSlot].value;
                if (literalFact.type != binaryOperandType) return zc::none;
                zc::Maybe<checker::checked::CanonicalConstValue> literalValue =
                    literalFact.literal.clone();
                zc::Maybe<identity::CallableParameterKey> noParameter;
                return PendingSequentialBinaryOperand{SequentialBinaryOperandKind::Literal,
                                                      binaryOperandType,
                                                      ZC_ASSERT_NONNULL(operandSpan).clone(),
                                                      zc::mv(literalValue),
                                                      zc::mv(noParameter),
                                                      0,
                                                      zc::mv(noNestedOperation),
                                                      zc::mv(noNestedLeft),
                                                      zc::mv(noNestedRight)};
              }
              if (operand.kind == SequentialBinaryOperandKind::LocalReference) {
                if (operand.referencedLocal >= localBindingIds.size()) return zc::none;
                auto referenceBinding = resolvedOwnerLocal(bound.bindings(), operand.node);
                if (referenceBinding == zc::none || ZC_ASSERT_NONNULL(referenceBinding) !=
                                                        localBindingIds[operand.referencedLocal]) {
                  return zc::none;
                }
                zc::Maybe<checker::checked::CanonicalConstValue> noLiteral;
                zc::Maybe<identity::CallableParameterKey> noParameter;
                return PendingSequentialBinaryOperand{SequentialBinaryOperandKind::LocalReference,
                                                      binaryOperandType,
                                                      ZC_ASSERT_NONNULL(operandSpan).clone(),
                                                      zc::mv(noLiteral),
                                                      zc::mv(noParameter),
                                                      operand.referencedLocal,
                                                      zc::mv(noNestedOperation),
                                                      zc::mv(noNestedLeft),
                                                      zc::mv(noNestedRight)};
              }
              auto parameterHandle = resolvedCallableParameter(bound.bindings(), operand.node);
              if (parameterHandle == zc::none) return zc::none;
              zc::Maybe<identity::CallableParameterKey> resolvedKey;
              ZC_IF_SOME(handle, parameterHandle) {
                auto authority = registries.callableParameter(handle);
                ZC_IF_SOME(entry, authority) {
                  for (const auto& parameter : parameters) {
                    if (parameter.key == entry.key() && parameter.type == binaryOperandType) {
                      resolvedKey = entry.key().clone();
                    }
                  }
                }
              }
              if (resolvedKey == zc::none) return zc::none;
              zc::Maybe<checker::checked::CanonicalConstValue> noLiteral;
              return PendingSequentialBinaryOperand{SequentialBinaryOperandKind::ParameterReference,
                                                    binaryOperandType,
                                                    ZC_ASSERT_NONNULL(operandSpan).clone(),
                                                    zc::mv(noLiteral),
                                                    zc::mv(resolvedKey),
                                                    0,
                                                    zc::mv(noNestedOperation),
                                                    zc::mv(noNestedLeft),
                                                    zc::mv(noNestedRight)};
            };
            ZC_IF_SOME(leftClassified, binding.leftOperand) {
              ZC_IF_SOME(rightClassified, binding.rightOperand) {
                auto resolvedLeft = resolveOperand(leftClassified);
                auto resolvedRight = resolveOperand(rightClassified);
                if (resolvedLeft == zc::none || resolvedRight == zc::none) {
                  rejected = true;
                } else {
                  bindingOperation = operation;
                  bindingOperandType = binaryOperandType;
                  bindingLeftOperand = zc::mv(resolvedLeft);
                  bindingRightOperand = zc::mv(resolvedRight);
                }
              }
            }
            if (rejected) break;
          } else {
            // A reference to a parameter must resolve to a declared parameter.
            auto parameterHandle = resolvedCallableParameter(bound.bindings(), binding.initializer);
            if (parameterHandle == zc::none) {
              rejected = true;
              break;
            }
            zc::Maybe<identity::CallableParameterKey> resolvedKey;
            ZC_IF_SOME(handle, parameterHandle) {
              auto authority = registries.callableParameter(handle);
              ZC_IF_SOME(entry, authority) {
                for (const auto& parameter : parameters) {
                  if (parameter.key == entry.key() && parameter.type == sequentialType) {
                    resolvedKey = entry.key().clone();
                  }
                }
              }
            }
            if (resolvedKey == zc::none) {
              rejected = true;
              break;
            }
            bindingParameter = zc::mv(resolvedKey);
          }
          pendingBindings.add(PendingSequentialBinding{
              sequentialType, ZC_ASSERT_NONNULL(patternSpan).clone(),
              ZC_ASSERT_NONNULL(initializerSpan).clone(), binding.initializerKind,
              zc::mv(bindingLiteral), zc::mv(bindingAggregate), zc::mv(bindingParameter),
              binding.referencedLocal, zc::mv(bindingOperation), bindingOperandType,
              zc::mv(bindingLeftOperand), zc::mv(bindingRightOperand)});
        }
        if (rejected) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        // The returned value names a parameter or one of the declared locals.
        zc::Maybe<identity::CallableParameterKey> returnParameter;
        size_t returnLocal = 0;
        ZC_IF_SOME(index, sequentialShape.returnsLocal) { returnLocal = index; }
        if (sequentialShape.returnsLocal == zc::none) {
          auto parameterHandle =
              resolvedCallableParameter(bound.bindings(), sequentialShape.returnValue);
          if (parameterHandle == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          ZC_IF_SOME(handle, parameterHandle) {
            auto authority = registries.callableParameter(handle);
            ZC_IF_SOME(entry, authority) {
              for (const auto& parameter : parameters) {
                if (parameter.key == entry.key() && parameter.type == sequentialType) {
                  returnParameter = entry.key().clone();
                }
              }
            }
          }
          if (returnParameter == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
        }
        auto returnValueSpan =
            bound.parsedModule().spanFor(tree.node(sequentialShape.returnValue).range);
        if (returnValueSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        PendingSequentialLocalReturn sequential{zc::mv(pendingBindings), sequentialType,
                                                zc::mv(returnParameter), returnLocal,
                                                ZC_ASSERT_NONNULL(returnValueSpan).clone()};
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
                                                        zc::mv(unsafeBlockSpan),
                                                        zc::mv(orderingKey),
                                                        zc::none,
                                                        zc::none,
                                                        zc::none});
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
          auto assignmentSpan = bound.parsedModule().spanFor(tree.node(write).range);
          auto valueSpan = bound.parsedModule().spanFor(tree.node(writeValue).range);
          // A write value is a scalar literal or an identifier reference (a
          // parameter, resolved below). Only a literal write consumes a checked
          // literal fact; a reference write consumes none.
          const bool referenceValue = tree.node(writeValue).kind == ast::SyntaxKind::IdentExpr;
          auto literalIndex = referenceValue ? zc::none : factIndex(facts.literals(), writeValue);
          ast::NodeId targetReference = target;
          if (tree.node(target).kind == ast::SyntaxKind::MemberExpression) {
            targetReference =
                ast::NodeId(tree.node(target).payload.words[ast::kMemberExpressionObjectWord]);
          }
          auto targetBinding = resolvedOwnerLocal(bound.bindings(), targetReference);
          auto returnBinding = resolvedOwnerLocal(bound.bindings(), shape.localReference);
          if (assignmentTypeIndex == zc::none || targetTypeIndex == zc::none ||
              valueTypeIndex == zc::none || (!referenceValue && literalIndex == zc::none) ||
              assignmentSpan == zc::none || valueSpan == zc::none || targetBinding == zc::none ||
              returnBinding == zc::none || targetBinding != returnBinding) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t assignmentSlot = 0;
          size_t targetSlot = 0;
          size_t valueSlot = 0;
          ZC_IF_SOME(index, assignmentTypeIndex) { assignmentSlot = index; }
          ZC_IF_SOME(index, targetTypeIndex) { targetSlot = index; }
          ZC_IF_SOME(index, valueTypeIndex) { valueSlot = index; }
          const auto& assignmentType = facts.nodeTypes().entries()[assignmentSlot].value;
          const auto& targetType = facts.nodeTypes().entries()[targetSlot].value;
          const auto& valueType = facts.nodeTypes().entries()[valueSlot].value;
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
              valueType != writeType) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          // Build the per-write value: a scalar literal consumes its checked
          // literal fact; a parameter reference resolves the parameter and
          // matches its type, exactly like the return-of-parameter path.
          PendingLocalWriteValue writeValueRecord;
          if (referenceValue) {
            auto parameter = resolvedCallableParameter(bound.bindings(), writeValue);
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
                  if (candidate.key == entry.key() && candidate.type == writeType) {
                    matches = true;
                  }
                }
                if (!matches) {
                  return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                       ir::IrFailureKind::InvalidFact, module,
                                                       registries, ordinal + 2);
                }
                writeValueRecord.parameter = HirParameterReferenceExpression{
                    HirNodeId(), entry.key().clone(), writeType, HirValueCategory::Place,
                    ZC_ASSERT_NONNULL(valueSpan).clone()};
              }
            }
          } else {
            size_t literalSlot = 0;
            ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
            const auto& sourceLiteral = facts.literals().entries()[literalSlot].value;
            if (sourceLiteral.type != writeType ||
                !sameSpan(sourceLiteral.sourceSpan, ZC_ASSERT_NONNULL(valueSpan))) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            writeValueRecord.literal = sourceLiteral.literal.clone();
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
          localWriteValues.add(zc::mv(writeValueRecord));
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
                  callArguments.add(
                      HirDirectCallArgument{argumentType, literal.literal.clone(),
                                            zc::Maybe<identity::CallableParameterKey>(),
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
            auto argumentKey = checkedNodeKey(tree, bound.parsedModule(), argument);
            auto argumentSpan = bound.parsedModule().spanFor(tree.node(argument).range);
            const bool isLiteralArgument =
                tree.contains(argument) && isScalarLiteral(tree.node(argument).kind);
            const bool isParameterArgument =
                tree.contains(argument) && tree.node(argument).kind == ast::SyntaxKind::IdentExpr;
            if ((!isLiteralArgument && !isParameterArgument) || argumentTypeIndex == zc::none ||
                argumentKey == zc::none || argumentSpan == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t argumentTypeSlot = 0;
            ZC_IF_SOME(value, argumentTypeIndex) { argumentTypeSlot = value; }
            const auto& checkedArgument = invocation.arguments[index];
            const auto& dispatchArgument = dispatch.fact.arguments[index];
            const auto argumentType = facts.nodeTypes().entries()[argumentTypeSlot].value;
            if (checkedArgument.sourceNode != argument ||
                checkedArgument.sourceType != argumentType ||
                checkedArgument.parameterType != argumentType ||
                checkedArgument.adjustment != zc::none ||
                !sameNodeKey(dispatchArgument.sourceNode, ZC_ASSERT_NONNULL(argumentKey)) ||
                dispatchArgument.sourceType != argumentType ||
                dispatchArgument.parameterType != argumentType ||
                dispatchArgument.adjustment != zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            if (isLiteralArgument) {
              auto literalIndex = factIndex(facts.literals(), argument);
              if (literalIndex == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              size_t literalSlot = 0;
              ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
              const auto& literal = facts.literals().entries()[literalSlot].value;
              if (literal.node != argument || literal.type != argumentType ||
                  !sameSpan(literal.sourceSpan, ZC_ASSERT_NONNULL(argumentSpan))) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::InvalidFact, module,
                                                     registries, ordinal + 2);
              }
              zc::Maybe<identity::CallableParameterKey> noParameter;
              callArguments.add(HirDirectCallArgument{argumentType, literal.literal.clone(),
                                                      zc::mv(noParameter),
                                                      ZC_ASSERT_NONNULL(argumentSpan).clone()});
              continue;
            }
            auto parameter = resolvedCallableParameter(bound.bindings(), argument);
            zc::Maybe<identity::CallableParameterKey> parameterKey;
            ZC_IF_SOME(handle, parameter) {
              auto authority = registries.callableParameter(handle);
              ZC_IF_SOME(entry, authority) {
                for (const auto& candidate : parameters) {
                  if (candidate.key == entry.key() && candidate.type == argumentType) {
                    parameterKey = entry.key().clone();
                  }
                }
              }
            }
            if (parameterKey == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            zc::Maybe<checker::checked::CanonicalConstValue> noValue;
            callArguments.add(HirDirectCallArgument{argumentType, zc::mv(noValue),
                                                    zc::mv(parameterKey),
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
                                                      zc::mv(localWriteValues),
                                                      zc::mv(localReference),
                                                      zc::mv(localFieldProjection),
                                                      zc::mv(parameterReference),
                                                      zc::mv(parameterIndex),
                                                      zc::mv(parameterReborrow),
                                                      zc::mv(localBorrow),
                                                      zc::none,
                                                      zc::mv(unsafeBlockSpan),
                                                      zc::mv(orderingKey),
                                                      zc::none,
                                                      zc::none,
                                                      zc::none});
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
  size_t directCallLiteralArgumentCount = 0;
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
  size_t conditionalCount = 0;
  size_t equalityConditionalCount = 0;
  size_t conditionalLiteralArmCount = 0;
  // Number of comparison-condition operands that are scalar literals rather than
  // parameter references. Each such operand adds one scalar-literal expression
  // and one literal fact beyond the arm literals.
  size_t equalityLiteralOperandCount = 0;
  size_t loopCount = 0;
  // Per-function excess of literal facts a sequential N-local body carries over
  // the single-literal baseline the shared literal equation grants each
  // function: sum of (literal-bearing bindings - 1). Zero for the former
  // two-local literal or aggregate source, so existing bodies keep exact counts.
  // Per-function excess of literal facts a sequential N-local body carries over
  // the single-literal baseline the shared literal equation grants each
  // function: sum of (literal-bearing operands - 1). Signed because an all-binary
  // body with reference operands carries zero literal-bearing operands, so the
  // per-function term is negative. Zero for the former two-local literal or
  // aggregate source, so existing bodies keep exact counts.
  int64_t sequentialLiteralAdjustment = 0;
  // Total primitive-binary initializers across sequential bodies. Each carries
  // one call fact and one dispatch fact and two operand node types beyond the
  // per-binding local node type.
  size_t sequentialBinaryCount = 0;
  // Comparison-return functions (`return <a CMP b>`) and, of their two operands,
  // the count that are scalar literals rather than parameter references.
  size_t comparisonReturnCount = 0;
  size_t comparisonReturnLiteralOperandCount = 0;
  for (const auto& function : pendingFunctions) {
    const bool hasSequentialLocalReturn = function.sequentialLocalReturn != zc::none;
    if (hasSequentialLocalReturn) {
      ZC_IF_SOME(sequential, function.sequentialLocalReturn) {
        // Each binding declares one local; its initializer node carries a node
        // type fact, counted by localReturnCount (+1 per binding, matching the
        // N node types plus the return value node covered by the per-function
        // baseline). Aggregate bindings add their element literals; a primitive
        // binary binding adds its two operand nodes (node types) plus a call and
        // dispatch fact. The shared literal equation grants exactly one literal
        // per function; a sequential body instead carries L literal-initializer
        // + A aggregate-initializer + K binary-literal-operand "literal-bearing"
        // slots, so sequentialLiteralAdjustment records the per-function excess
        // (L + A + K - 1). Parameter- and local-reference operands carry no
        // literal. This term is zero for the former two-local literal/aggregate
        // source (L + A == 1), so existing bodies keep their exact counts.
        int64_t literalBearingSlots = 0;
        for (const auto& binding : sequential.bindings) {
          ++localReturnCount;
          switch (binding.kind) {
            case SequentialInitializerKind::Literal:
              ++literalBearingSlots;
              break;
            case SequentialInitializerKind::Aggregate:
              ++aggregateCount;
              ++literalBearingSlots;
              ZC_IF_SOME(aggregate, binding.aggregate) {
                aggregateElementCount += aggregate.elements.size();
              }
              break;
            case SequentialInitializerKind::LocalReference:
            case SequentialInitializerKind::ParameterReference:
              break;
            case SequentialInitializerKind::PrimitiveBinary:
              ++sequentialBinaryCount;
              for (const auto* operand : {&binding.leftOperand, &binding.rightOperand}) {
                ZC_IF_SOME(value, *operand) {
                  if (value.kind == SequentialBinaryOperandKind::Literal) ++literalBearingSlots;
                  if (value.kind == SequentialBinaryOperandKind::NestedBinary) {
                    // A nested operand is itself a primitive binary carrying its
                    // own call/dispatch fact and two leaf operands; count it as an
                    // additional binary and tally its literal leaves.
                    ++sequentialBinaryCount;
                    for (const auto* leaf : {&value.nestedLeft, &value.nestedRight}) {
                      ZC_IF_SOME(leafValue, *leaf) {
                        if (leafValue.kind == SequentialBinaryOperandKind::Literal) {
                          ++literalBearingSlots;
                        }
                      }
                    }
                  }
                }
              }
              break;
          }
        }
        sequentialLiteralAdjustment += literalBearingSlots - 1;
      }
      if (function.unsafeBlockSpan != zc::none) ++unsafeBlockCount;
      continue;
    }
    if (function.conditionalReturn != zc::none) {
      ++conditionalCount;
      ZC_IF_SOME(conditional, function.conditionalReturn) {
        ZC_IF_SOME(equality, conditional.condition.equality) {
          ++equalityConditionalCount;
          for (const auto* operand : {&equality.left, &equality.right}) {
            if (operand->literal != zc::none) { ++equalityLiteralOperandCount; }
          }
        }
        for (const auto* arm : {&conditional.thenArm, &conditional.elseArm}) {
          if (arm->literal != zc::none) { ++conditionalLiteralArmCount; }
        }
      }
      continue;
    }
    if (function.loopReturn != zc::none) {
      ++loopCount;
      continue;
    }
    if (function.comparisonReturn != zc::none) {
      ++comparisonReturnCount;
      ZC_IF_SOME(comparison, function.comparisonReturn) {
        for (const auto* operand : {&comparison.left, &comparison.right}) {
          if (operand->literal != zc::none) { ++comparisonReturnLiteralOperandCount; }
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
      for (const auto& argument : call.arguments) {
        if (argument.value != zc::none) ++directCallLiteralArgumentCount;
      }
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
          function.localWrites.size() != function.localWriteValues.size()) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::MissingRequiredFact, module,
                                             registries, 1);
      }
      localWriteCount += function.localWrites.size();
      for (const auto& write : function.localWrites) {
        if (write.field != zc::none) ++localFieldWriteCount;
      }
      // A write whose value is a parameter reference materializes an entry in
      // parameterReferences and no literal, so it contributes to the same
      // parameterReferenceCount balance the top-level parameter reference does.
      // A literal write materializes a literal expression and no parameter
      // reference. Every write still contributes localWriteCount to the literals
      // equation; a reference write's -parameterReferenceCount term cancels it.
      for (const auto& value : function.localWriteValues) {
        if (value.parameter != zc::none) ++parameterReferenceCount;
      }
    } else if (function.localWriteValues.size() != 0) {
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
          receiverCallArgumentCount + localBorrowCount + unsafeBlockCount + conditionalCount * 2 +
          equalityConditionalCount * 2 + loopCount + comparisonReturnCount * 2 +
          sequentialBinaryCount * 2) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::AdditionalFact, module, registries, 1);
  }
  if (facts.definitionTypes().size() != pending.size()) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::AdditionalFact, module, registries, 2);
  }
  if (static_cast<int64_t>(facts.literals().size()) !=
      static_cast<int64_t>(
          pending.size() + pendingFunctions.size() - directCallCount - aggregateCount -
          uninitializedLocalReturnCount - parameterReferenceCount - parameterReborrowCount +
          localAliasReborrowCount + localWriteCount + aggregateElementCount +
          directCallLiteralArgumentCount + receiverCallArgumentCount + conditionalLiteralArmCount +
          equalityLiteralOperandCount - conditionalCount + comparisonReturnLiteralOperandCount -
          comparisonReturnCount) +
          sequentialLiteralAdjustment) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::AdditionalFact, module, registries, 3);
  }
  if (facts.calls().size() != directCallCount + receiverCallCount + parameterIndexCount +
                                  equalityConditionalCount + comparisonReturnCount +
                                  sequentialBinaryCount ||
      checkedModule.dispatchFacts().facts().size() !=
          directCallCount + receiverCallCount + parameterIndexCount + equalityConditionalCount +
              comparisonReturnCount + sequentialBinaryCount) {
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
  zc::Vector<HirPrimitiveBinaryExpression> primitiveBinaryOperations;
  zc::Vector<HirConditionalExpression> conditionals;
  zc::Vector<HirLoopStatement> loops;
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
      // Sequential N-local fixed-id layout, relative to the function id F:
      //   F+0 function, F+1 body block; then each binding i (0-based) consumes,
      //   in order, its localNode and initializerNode, plus (only when the
      //   initializer is a primitive binary) two operand nodes, plus (when an
      //   operand is itself a nested primitive binary) two inner leaf-operand
      //   nodes for that operand. So a non-binary binding is width 2, a binary
      //   binding is width 4, and a binary binding with one nested operand is
      //   width 6. After the last binding: returnNode, returnValueNode, and an
      //   optional unsafeBlock. The materializer and the HIR verifier both derive
      //   this from the same per-binding kinds, so their node counts always agree.
      const size_t bindingCount = sequential.bindings.size();
      zc::Vector<HirNodeId> localNodeIds;
      zc::Vector<HirNodeId> initializerNodeIds;
      zc::Vector<zc::Maybe<HirNodeId>> leftOperandIds;
      zc::Vector<zc::Maybe<HirNodeId>> rightOperandIds;
      zc::Vector<zc::Maybe<HirNodeId>> leftNestedLeafLeftIds;
      zc::Vector<zc::Maybe<HirNodeId>> leftNestedLeafRightIds;
      zc::Vector<zc::Maybe<HirNodeId>> rightNestedLeafLeftIds;
      zc::Vector<zc::Maybe<HirNodeId>> rightNestedLeafRightIds;
      for (size_t index = 0; index < bindingCount; ++index) {
        localNodeIds.add(hirId(next++));
        initializerNodeIds.add(hirId(next++));
        zc::Maybe<HirNodeId> leftOperandId;
        zc::Maybe<HirNodeId> rightOperandId;
        zc::Maybe<HirNodeId> leftNestedLeafLeftId;
        zc::Maybe<HirNodeId> leftNestedLeafRightId;
        zc::Maybe<HirNodeId> rightNestedLeafLeftId;
        zc::Maybe<HirNodeId> rightNestedLeafRightId;
        if (sequential.bindings[index].kind == SequentialInitializerKind::PrimitiveBinary) {
          leftOperandId = hirId(next++);
          rightOperandId = hirId(next++);
          ZC_IF_SOME(left, sequential.bindings[index].leftOperand) {
            if (left.kind == SequentialBinaryOperandKind::NestedBinary) {
              leftNestedLeafLeftId = hirId(next++);
              leftNestedLeafRightId = hirId(next++);
            }
          }
          ZC_IF_SOME(right, sequential.bindings[index].rightOperand) {
            if (right.kind == SequentialBinaryOperandKind::NestedBinary) {
              rightNestedLeafLeftId = hirId(next++);
              rightNestedLeafRightId = hirId(next++);
            }
          }
        }
        leftOperandIds.add(zc::mv(leftOperandId));
        rightOperandIds.add(zc::mv(rightOperandId));
        leftNestedLeafLeftIds.add(zc::mv(leftNestedLeafLeftId));
        leftNestedLeafRightIds.add(zc::mv(leftNestedLeafRightId));
        rightNestedLeafLeftIds.add(zc::mv(rightNestedLeafLeftId));
        rightNestedLeafRightIds.add(zc::mv(rightNestedLeafRightId));
      }
      const auto returnId = hirId(next++);
      const auto returnValueId = hirId(next++);
      zc::Maybe<HirNodeId> unsafeBlockId;
      if (value.unsafeBlockSpan != zc::none) {
        unsafeBlockId = hirId(next++);
        unsafeBlocks.add(HirUnsafeBlockExpression{
            ZC_ASSERT_NONNULL(unsafeBlockId), returnValueId, value.resultType,
            ZC_ASSERT_NONNULL(value.unsafeBlockSpan).clone()});
      }
      functions.add(HirFunctionDeclaration{functionId, value.definition, value.resultType,
                                           zc::mv(value.parameters), value.visibility.clone(),
                                           value.linkage, value.declarationSpan.clone(), bodyId,
                                           zc::mv(unsafeBlockId)});
      zc::Vector<HirNodeId> statements;
      for (const auto localNodeId : localNodeIds) { statements.add(localNodeId); }
      statements.add(returnId);
      blocks.add(HirBlockStatement{bodyId, zc::mv(statements), value.bodySpan.clone()});
      returns.add(
          HirReturnStatement{returnId, value.resultType, returnValueId, value.returnSpan.clone()});
      // Materializes one binary operand at its node id: a literal into
      // expressions, a parameter reference into parameterReferences, a reference
      // to an earlier local into localReferences, or a nested one-level primitive
      // binary into primitiveBinaryOperations (its own two leaf operands are
      // materialized at the supplied leaf node ids).
      auto materializeBinaryLeaf = [&](HirNodeId leafId, PendingSequentialBinaryLeafOperand& leaf) {
        switch (leaf.kind) {
          case SequentialBinaryOperandKind::Literal:
            ZC_IF_SOME(literal, leaf.literal) {
              expressions.add(HirScalarLiteralExpression{leafId, leaf.type, literal.clone(),
                                                         HirValueCategory::Value,
                                                         leaf.sourceSpan.clone()});
            }
            break;
          case SequentialBinaryOperandKind::ParameterReference:
            ZC_IF_SOME(parameter, leaf.parameter) {
              parameterReferences.add(HirParameterReferenceExpression{
                  leafId, parameter.clone(), leaf.type, HirValueCategory::Place,
                  leaf.sourceSpan.clone()});
            }
            break;
          case SequentialBinaryOperandKind::LocalReference:
            localReferences.add(HirLocalReferenceExpression{
                leafId, hirLocalId(static_cast<uint32_t>(leaf.referencedLocal + 1)), leaf.type,
                HirValueCategory::Place, leaf.sourceSpan.clone()});
            break;
          case SequentialBinaryOperandKind::NestedBinary:
            // A leaf is never a nested binary; two-level nesting is unsupported.
            break;
        }
      };
      auto materializeBinaryOperand = [&](HirNodeId operandId,
                                          PendingSequentialBinaryOperand& operand,
                                          zc::Maybe<HirNodeId> nestedLeafLeftId,
                                          zc::Maybe<HirNodeId> nestedLeafRightId) {
        switch (operand.kind) {
          case SequentialBinaryOperandKind::Literal:
            ZC_IF_SOME(literal, operand.literal) {
              expressions.add(HirScalarLiteralExpression{operandId, operand.type, literal.clone(),
                                                         HirValueCategory::Value,
                                                         operand.sourceSpan.clone()});
            }
            break;
          case SequentialBinaryOperandKind::ParameterReference:
            ZC_IF_SOME(parameter, operand.parameter) {
              parameterReferences.add(HirParameterReferenceExpression{
                  operandId, parameter.clone(), operand.type, HirValueCategory::Place,
                  operand.sourceSpan.clone()});
            }
            break;
          case SequentialBinaryOperandKind::LocalReference:
            localReferences.add(HirLocalReferenceExpression{
                operandId, hirLocalId(static_cast<uint32_t>(operand.referencedLocal + 1)),
                operand.type, HirValueCategory::Place, operand.sourceSpan.clone()});
            break;
          case SequentialBinaryOperandKind::NestedBinary: {
            HirNodeId leafLeftId;
            HirNodeId leafRightId;
            ZC_IF_SOME(id, nestedLeafLeftId) { leafLeftId = id; }
            ZC_IF_SOME(id, nestedLeafRightId) { leafRightId = id; }
            ZC_IF_SOME(leaf, operand.nestedLeft) { materializeBinaryLeaf(leafLeftId, leaf); }
            ZC_IF_SOME(leaf, operand.nestedRight) { materializeBinaryLeaf(leafRightId, leaf); }
            ZC_IF_SOME(operation, operand.nestedOperation) {
              primitiveBinaryOperations.add(HirPrimitiveBinaryExpression{
                  operandId, leafLeftId, leafRightId, operand.type, operand.type,
                  HirValueCategory::Value, operation, operand.sourceSpan.clone()});
            }
            break;
          }
        }
      };
      for (size_t index = 0; index < bindingCount; ++index) {
        auto& binding = sequential.bindings[index];
        const auto localNodeId = localNodeIds[index];
        const auto initializerNodeId = initializerNodeIds[index];
        switch (binding.kind) {
          case SequentialInitializerKind::Literal:
            ZC_IF_SOME(literal, binding.literal) {
              expressions.add(HirScalarLiteralExpression{initializerNodeId, binding.type,
                                                         literal.clone(), HirValueCategory::Value,
                                                         binding.initializerSpan.clone()});
            }
            break;
          case SequentialInitializerKind::Aggregate:
            ZC_IF_SOME(aggregate, binding.aggregate) {
              aggregates.add(HirNominalAggregateExpression{
                  initializerNodeId, aggregate.definition, aggregate.type,
                  zc::mv(aggregate.elements), aggregate.category, aggregate.sourceSpan.clone()});
            }
            break;
          case SequentialInitializerKind::LocalReference:
            localReferences.add(HirLocalReferenceExpression{
                initializerNodeId, hirLocalId(static_cast<uint32_t>(binding.referencedLocal + 1)),
                binding.type, HirValueCategory::Place, binding.initializerSpan.clone()});
            break;
          case SequentialInitializerKind::ParameterReference:
            ZC_IF_SOME(parameter, binding.parameter) {
              parameterReferences.add(HirParameterReferenceExpression{
                  initializerNodeId, parameter.clone(), binding.type, HirValueCategory::Place,
                  binding.initializerSpan.clone()});
            }
            break;
          case SequentialInitializerKind::PrimitiveBinary: {
            HirNodeId leftOperandId;
            HirNodeId rightOperandId;
            ZC_IF_SOME(id, leftOperandIds[index]) { leftOperandId = id; }
            ZC_IF_SOME(id, rightOperandIds[index]) { rightOperandId = id; }
            ZC_IF_SOME(left, binding.leftOperand) {
              materializeBinaryOperand(leftOperandId, left, leftNestedLeafLeftIds[index],
                                       leftNestedLeafRightIds[index]);
            }
            ZC_IF_SOME(right, binding.rightOperand) {
              materializeBinaryOperand(rightOperandId, right, rightNestedLeafLeftIds[index],
                                       rightNestedLeafRightIds[index]);
            }
            ZC_IF_SOME(operation, binding.operation) {
              primitiveBinaryOperations.add(HirPrimitiveBinaryExpression{
                  initializerNodeId, leftOperandId, rightOperandId, binding.operandType,
                  binding.type, HirValueCategory::Value, operation,
                  binding.initializerSpan.clone()});
            }
            break;
          }
        }
        locals.add(HirLocalBinding{localNodeId, hirLocalId(static_cast<uint32_t>(index + 1)),
                                   binding.type, initializerNodeId, binding.patternSpan.clone(),
                                   binding.initializerSpan.clone()});
      }
      // The returned value references a parameter or one of the declared locals.
      ZC_IF_SOME(parameter, sequential.returnParameter) {
        parameterReferences.add(HirParameterReferenceExpression{
            returnValueId, parameter.clone(), sequential.type, HirValueCategory::Place,
            sequential.returnValueSpan.clone()});
      }
      if (sequential.returnParameter == zc::none) {
        localReferences.add(HirLocalReferenceExpression{
            returnValueId, hirLocalId(static_cast<uint32_t>(sequential.returnLocal + 1)),
            sequential.type, HirValueCategory::Place, sequential.returnValueSpan.clone()});
      }
      continue;
    }
    ZC_IF_SOME(conditional, value.conditionalReturn) {
      // Conditional materialization fixed-id layout, relative to the function id.
      //
      // Parameter condition (7 nodes):
      //   +0 function, +1 body block, +2 condition parameter reference,
      //   +3 then value, +4 else value, +5 conditional, +6 return.
      // Equality condition `a == b` (9 nodes):
      //   +0 function, +1 body block, +2 left operand reference,
      //   +3 right operand reference, +4 equality comparison, +5 then value,
      //   +6 else value, +7 conditional, +8 return.
      // Each arm value is emitted either as a scalar literal into expressions or,
      // for a parameter arm, as a parameter reference into parameterReferences;
      // the arm node ordinal is unchanged either way.
      auto materializeArm = [&](HirNodeId armId, PendingConditionalArm& arm) {
        ZC_IF_SOME(reference, arm.parameter) {
          parameterReferences.add(
              HirParameterReferenceExpression{armId, reference.parameter.clone(), arm.type,
                                              HirValueCategory::Place, arm.sourceSpan.clone()});
        }
        ZC_IF_SOME(literal, arm.literal) {
          expressions.add(HirScalarLiteralExpression{
              armId, arm.type, literal.clone(), HirValueCategory::Value, arm.sourceSpan.clone()});
        }
      };
      // Materialize one comparison operand: a parameter operand becomes a
      // parameter reference into parameterReferences, a literal operand becomes a
      // scalar literal into expressions. The node ordinal is fixed either way, so
      // the equality node's left/right ids remain node-kind-agnostic.
      auto materializeOperand = [&](HirNodeId operandId, PendingConditionalArm& operand) {
        ZC_IF_SOME(reference, operand.parameter) {
          parameterReferences.add(
              HirParameterReferenceExpression{operandId, reference.parameter.clone(), operand.type,
                                              HirValueCategory::Place, operand.sourceSpan.clone()});
        }
        ZC_IF_SOME(literal, operand.literal) {
          expressions.add(HirScalarLiteralExpression{operandId, operand.type, literal.clone(),
                                                     HirValueCategory::Value,
                                                     operand.sourceSpan.clone()});
        }
      };
      ZC_IF_SOME(equality, conditional.condition.equality) {
        const auto leftId = hirId(next++);
        const auto rightId = hirId(next++);
        const auto equalityId = hirId(next++);
        const auto thenValueId = hirId(next++);
        const auto elseValueId = hirId(next++);
        const auto conditionalId = hirId(next++);
        const auto returnId = hirId(next++);
        functions.add(HirFunctionDeclaration{functionId, value.definition, value.resultType,
                                             zc::mv(value.parameters), value.visibility.clone(),
                                             value.linkage, value.declarationSpan.clone(), bodyId,
                                             zc::none});
        zc::Vector<HirNodeId> statements;
        statements.add(returnId);
        blocks.add(HirBlockStatement{bodyId, zc::mv(statements), value.bodySpan.clone()});
        returns.add(HirReturnStatement{returnId, value.resultType, conditionalId,
                                       value.returnSpan.clone()});
        materializeOperand(leftId, equality.left);
        materializeOperand(rightId, equality.right);
        primitiveBinaryOperations.add(HirPrimitiveBinaryExpression{
            equalityId, leftId, rightId, equality.operandType, equality.type,
            HirValueCategory::Value, equality.operation, equality.sourceSpan.clone()});
        materializeArm(thenValueId, conditional.thenArm);
        materializeArm(elseValueId, conditional.elseArm);
        conditionals.add(HirConditionalExpression{
            conditionalId, equalityId, thenValueId, elseValueId, value.resultType,
            HirValueCategory::Value, conditional.conditionalSpan.clone()});
        continue;
      }
      const auto conditionId = hirId(next++);
      const auto thenValueId = hirId(next++);
      const auto elseValueId = hirId(next++);
      const auto conditionalId = hirId(next++);
      const auto returnId = hirId(next++);
      functions.add(HirFunctionDeclaration{functionId, value.definition, value.resultType,
                                           zc::mv(value.parameters), value.visibility.clone(),
                                           value.linkage, value.declarationSpan.clone(), bodyId,
                                           zc::none});
      zc::Vector<HirNodeId> statements;
      statements.add(returnId);
      blocks.add(HirBlockStatement{bodyId, zc::mv(statements), value.bodySpan.clone()});
      returns.add(
          HirReturnStatement{returnId, value.resultType, conditionalId, value.returnSpan.clone()});
      ZC_IF_SOME(parameter, conditional.condition.parameter) {
        parameterReferences.add(HirParameterReferenceExpression{
            conditionId, parameter.parameter.clone(), parameter.type, parameter.category,
            parameter.sourceSpan.clone()});
      }
      materializeArm(thenValueId, conditional.thenArm);
      materializeArm(elseValueId, conditional.elseArm);
      conditionals.add(HirConditionalExpression{
          conditionalId, conditionId, thenValueId, elseValueId, value.resultType,
          HirValueCategory::Value, conditional.conditionalSpan.clone()});
      continue;
    }
    ZC_IF_SOME(loop, value.loopReturn) {
      // Loop materialization fixed-id layout, relative to the function id:
      //   +0 function, +1 body block, +2 condition parameter reference,
      //   +3 return value literal, +4 loop statement, +5 return statement.
      // The body block holds two statements: the loop statement then the return.
      const auto conditionId = hirId(next++);
      const auto returnValueId = hirId(next++);
      const auto loopId = hirId(next++);
      const auto returnId = hirId(next++);
      functions.add(HirFunctionDeclaration{functionId, value.definition, value.resultType,
                                           zc::mv(value.parameters), value.visibility.clone(),
                                           value.linkage, value.declarationSpan.clone(), bodyId,
                                           zc::none});
      zc::Vector<HirNodeId> statements;
      statements.add(loopId);
      statements.add(returnId);
      blocks.add(HirBlockStatement{bodyId, zc::mv(statements), value.bodySpan.clone()});
      returns.add(
          HirReturnStatement{returnId, value.resultType, returnValueId, value.returnSpan.clone()});
      parameterReferences.add(HirParameterReferenceExpression{
          conditionId, loop.condition.parameter.clone(), loop.condition.type,
          loop.condition.category, loop.condition.sourceSpan.clone()});
      expressions.add(
          HirScalarLiteralExpression{returnValueId, loop.returnType, loop.returnLiteral.clone(),
                                     HirValueCategory::Value, loop.returnValueSpan.clone()});
      loops.add(HirLoopStatement{loopId, conditionId, loop.condition.type, HirValueCategory::Place,
                                 loop.loopSpan.clone()});
      continue;
    }
    ZC_IF_SOME(comparison, value.comparisonReturn) {
      // Comparison-return materialization fixed-id layout, relative to the
      // function id (6 nodes):
      //   +0 function, +1 body block, +2 left operand, +3 right operand,
      //   +4 comparison, +5 return statement.
      // The return value is the comparison node directly (no conditional). Each
      // operand is a scalar literal into expressions or, for a parameter operand,
      // a parameter reference into parameterReferences; the ordinal is fixed
      // either way. This is smaller than the equality-conditional shape (which
      // additionally materializes then/else arms and a conditional node).
      auto materializeOperand = [&](HirNodeId operandId, PendingConditionalArm& operand) {
        ZC_IF_SOME(reference, operand.parameter) {
          parameterReferences.add(
              HirParameterReferenceExpression{operandId, reference.parameter.clone(), operand.type,
                                              HirValueCategory::Place, operand.sourceSpan.clone()});
        }
        ZC_IF_SOME(literal, operand.literal) {
          expressions.add(HirScalarLiteralExpression{operandId, operand.type, literal.clone(),
                                                     HirValueCategory::Value,
                                                     operand.sourceSpan.clone()});
        }
      };
      const auto leftId = hirId(next++);
      const auto rightId = hirId(next++);
      const auto comparisonId = hirId(next++);
      const auto returnId = hirId(next++);
      functions.add(HirFunctionDeclaration{functionId, value.definition, value.resultType,
                                           zc::mv(value.parameters), value.visibility.clone(),
                                           value.linkage, value.declarationSpan.clone(), bodyId,
                                           zc::none});
      zc::Vector<HirNodeId> statements;
      statements.add(returnId);
      blocks.add(HirBlockStatement{bodyId, zc::mv(statements), value.bodySpan.clone()});
      returns.add(
          HirReturnStatement{returnId, value.resultType, comparisonId, value.returnSpan.clone()});
      materializeOperand(leftId, comparison.left);
      materializeOperand(rightId, comparison.right);
      primitiveBinaryOperations.add(HirPrimitiveBinaryExpression{
          comparisonId, leftId, rightId, comparison.operandType, comparison.type,
          HirValueCategory::Value, comparison.operation, comparison.sourceSpan.clone()});
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
    if (value.localWrites.size() != value.localWriteValues.size() ||
        writeIds.size() != value.localWrites.size() ||
        writeValueIds.size() != value.localWrites.size()) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::MissingRequiredFact, module,
                                           registries, 1);
    }
    for (size_t index = 0; index < value.localWrites.size(); ++index) {
      const auto& write = value.localWrites[index];
      const auto& writeValue = value.localWriteValues[index];
      // A literal write materializes a scalar literal expression at the write's
      // value node; a parameter write materializes a parameter reference at the
      // same node id. The node id stride (two per write) is identical for both,
      // so downstream fixed-id derivations do not shift.
      ZC_IF_SOME(literal, writeValue.literal) {
        expressions.add(HirScalarLiteralExpression{writeValueIds[index], write.type,
                                                   literal.clone(), HirValueCategory::Value,
                                                   write.valueSpan.clone()});
      }
      ZC_IF_SOME(parameter, writeValue.parameter) {
        parameterReferences.add(HirParameterReferenceExpression{
            writeValueIds[index], parameter.parameter.clone(), write.type, parameter.category,
            parameter.sourceSpan.clone()});
      }
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
        zc::Maybe<checker::checked::CanonicalConstValue> value;
        ZC_IF_SOME(constant, argument.value) { value = constant.clone(); }
        zc::Maybe<identity::CallableParameterKey> parameter;
        ZC_IF_SOME(key, argument.parameter) { parameter = key.clone(); }
        arguments.add(HirDirectCallArgument{argument.type, zc::mv(value), zc::mv(parameter),
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
        zc::Maybe<checker::checked::CanonicalConstValue> value;
        ZC_IF_SOME(constant, argument.value) { value = constant.clone(); }
        zc::Maybe<identity::CallableParameterKey> parameter;
        ZC_IF_SOME(key, argument.parameter) { parameter = key.clone(); }
        arguments.add(HirDirectCallArgument{argument.type, zc::mv(value), zc::mv(parameter),
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
      zc::mv(localBorrows), zc::mv(calls), zc::mv(receiverCalls), zc::mv(unsafeBlocks),
      zc::mv(primitiveBinaryOperations), zc::mv(conditionals), zc::mv(loops));
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
  size_t directCallLiteralArgumentCount = 0;
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
  const auto conditionalCount = candidate.impl->conditionals.size();
  // equalityConditionalCount is derived below, after the sequential-binary tally,
  // because the primitiveBinaryOperations vector pools conditional/comparison
  // binaries with sequential-local binary initializers.
  const auto loopCount = candidate.impl->loops.size();
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
    for (const auto& argument : call.arguments) {
      if (argument.value != zc::none) ++directCallLiteralArgumentCount;
    }
  }
  for (const auto& call : candidate.impl->receiverCalls) {
    receiverCallArgumentCount += call.arguments.size();
  }
  size_t uninitializedLocalReturnCount = 0;
  for (const auto& local : candidate.impl->locals) {
    if (local.initializer == zc::none) ++uninitializedLocalReturnCount;
  }
  // Sequential N-local corrections. The shared count equations assume one value
  // node and one local reference per function local; a sequential body instead
  // produces one value node per literal/aggregate/parameter-reference
  // initializer plus a parameter return, and one local reference per
  // local-reference initializer plus a local return. These tallies re-derive the
  // true contributions from the candidate HIR so the localReferences,
  // expressions, and literals equations balance for any N. All corrections are
  // zero for the former two-local literal or aggregate source.
  size_t sequentialLiteralInitializers = 0;
  size_t sequentialAggregateInitializers = 0;
  size_t sequentialParameterInitializers = 0;
  size_t sequentialLocalInitializers = 0;
  size_t sequentialLocalCount = 0;
  size_t sequentialFunctionCount = 0;
  size_t sequentialParameterReturns = 0;
  size_t sequentialLocalReturns = 0;
  // Sequential primitive-binary bindings and, of their operands, the counts that
  // are scalar literals, parameter references, and earlier-local references.
  size_t sequentialBinaryCount = 0;
  size_t sequentialBinaryLiteralOperands = 0;
  size_t sequentialBinaryParameterOperands = 0;
  size_t sequentialBinaryLocalOperands = 0;
  {
    const auto& tree = bound.tree();
    for (const auto& functionDeclaration : candidate.impl->functions) {
      auto sourceDefinitionIndex = definitionIndex(definitions, functionDeclaration.definition);
      if (sourceDefinitionIndex == zc::none) continue;
      size_t definitionSlot = 0;
      ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
      const auto& sourceDefinition = definitions.definitions()[definitionSlot];
      if (!tree.contains(sourceDefinition.node)) continue;
      auto shape = functionReturnShape(tree, tree.node(sourceDefinition.node));
      bool sequential = false;
      ast::NodeId sourceBody;
      ZC_IF_SOME(value, shape) {
        sequential = value.isSequentialLocalReturn;
        sourceBody = value.body;
      }
      if (!sequential) continue;
      auto sequentialShape = sequentialLocalShape(tree, sourceBody);
      ZC_IF_SOME(value, sequentialShape) {
        ++sequentialFunctionCount;
        sequentialLocalCount += value.bindings.size();
        for (const auto& binding : value.bindings) {
          switch (binding.initializerKind) {
            case SequentialInitializerKind::Literal:
              ++sequentialLiteralInitializers;
              break;
            case SequentialInitializerKind::Aggregate:
              ++sequentialAggregateInitializers;
              break;
            case SequentialInitializerKind::ParameterReference:
              ++sequentialParameterInitializers;
              break;
            case SequentialInitializerKind::LocalReference:
              ++sequentialLocalInitializers;
              break;
            case SequentialInitializerKind::PrimitiveBinary:
              ++sequentialBinaryCount;
              for (const auto* operand : {&binding.leftOperand, &binding.rightOperand}) {
                ZC_IF_SOME(value, *operand) {
                  switch (value.kind) {
                    case SequentialBinaryOperandKind::Literal:
                      ++sequentialBinaryLiteralOperands;
                      break;
                    case SequentialBinaryOperandKind::ParameterReference:
                      ++sequentialBinaryParameterOperands;
                      break;
                    case SequentialBinaryOperandKind::LocalReference:
                      ++sequentialBinaryLocalOperands;
                      break;
                    case SequentialBinaryOperandKind::NestedBinary:
                      // A nested operand is a second primitive binary carrying its
                      // own call/dispatch fact and two leaf operands; count it as
                      // an additional binary and tally its leaves the same way.
                      ++sequentialBinaryCount;
                      for (const auto* leaf : {&value.nestedLeft, &value.nestedRight}) {
                        ZC_IF_SOME(leafValue, *leaf) {
                          switch (leafValue.kind) {
                            case SequentialBinaryOperandKind::Literal:
                              ++sequentialBinaryLiteralOperands;
                              break;
                            case SequentialBinaryOperandKind::ParameterReference:
                              ++sequentialBinaryParameterOperands;
                              break;
                            case SequentialBinaryOperandKind::LocalReference:
                              ++sequentialBinaryLocalOperands;
                              break;
                            case SequentialBinaryOperandKind::NestedBinary:
                              break;
                          }
                        }
                      }
                      break;
                  }
                }
              }
              break;
          }
        }
        if (value.returnsLocal == zc::none) {
          ++sequentialParameterReturns;
        } else {
          ++sequentialLocalReturns;
        }
      }
    }
  }
  // The materialized primitive-binary operations pool three sources: conditional
  // conditions, comparison-return values, and sequential-local binary
  // initializers. Restore equalityConditionalCount to only the first two so the
  // pooled comparison/conditional equation terms stay exact; sequential binaries
  // are balanced by explicit sequentialBinaryCount terms below.
  const auto equalityConditionalCount =
      candidate.impl->primitiveBinaryOperations.size() - sequentialBinaryCount;
  // localReferences: sequential locals contribute N to the localReturnCount
  // baseline but only L_loc + R_loc + binary-local-operand actual local
  // references. Signed because binary local operands can exceed the shortfall.
  const int64_t sequentialLocalReferenceCorrection =
      static_cast<int64_t>(sequentialLocalCount) -
      static_cast<int64_t>(sequentialLocalInitializers + sequentialLocalReturns +
                           sequentialBinaryLocalOperands);
  // expressions and literals: each sequential function contributes one baseline
  // value node minus its aggregate and parameter-reference credits, but the true
  // scalar-literal count is L_lit plus any binary literal operands. Both
  // equations need the same correction. Signed because an all-reference binary
  // body carries fewer literals than the per-function baseline grants.
  const int64_t sequentialLiteralCorrection =
      static_cast<int64_t>(sequentialLiteralInitializers + sequentialAggregateInitializers +
                           sequentialParameterInitializers + sequentialParameterReturns +
                           sequentialBinaryLiteralOperands + sequentialBinaryParameterOperands) -
      static_cast<int64_t>(sequentialFunctionCount);
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
          directCallCount + receiverCallCount + equalityConditionalCount + sequentialBinaryCount ||
      !noUnsupportedFacts(facts) || candidate.impl->patterns.size() != declarationCount ||
      static_cast<int64_t>(candidate.impl->localReferences.size() + localFieldProjectionCount +
                           localAliasReborrowCount + localBorrowCount) +
              sequentialLocalReferenceCorrection !=
          static_cast<int64_t>(localReturnCount) ||
      parameterReferenceCount + parameterIndexCount + parameterReborrowCount >
          functionCount + localAliasReborrowCount + conditionalCount * 2 +
              equalityConditionalCount * 2 + sequentialParameterInitializers +
              sequentialParameterReturns + sequentialBinaryParameterOperands ||
      candidate.impl->blocks.size() != functionCount ||
      candidate.impl->returns.size() != functionCount ||
      static_cast<int64_t>(candidate.impl->expressions.size()) !=
          static_cast<int64_t>(declarationCount + functionCount - directCallCount - aggregateCount -
                               uninitializedLocalReturnCount - parameterReferenceCount -
                               parameterReborrowCount + localAliasReborrowCount + localWriteCount +
                               conditionalCount * 2 + equalityConditionalCount + loopCount) +
              sequentialLiteralCorrection ||
      executableDefinitions != declarationCount + functionCount ||
      facts.definitionTypes().size() != declarationCount ||
      facts.nodeTypes().size() !=
          declarationCount + functionCount + directCallCount + receiverCallCount * 2 +
              localReturnCount - uninitializedLocalReturnCount + localWriteCount * 3 +
              aggregateElementCount + localFieldProjectionCount + localFieldWriteCount +
              parameterIndexCount * 2 + parameterReborrowCount * 2 + directCallArgumentCount +
              receiverCallArgumentCount + localBorrowCount + unsafeBlockCount +
              conditionalCount * 2 + equalityConditionalCount * 2 + loopCount +
              sequentialBinaryCount * 2 ||
      static_cast<int64_t>(facts.literals().size()) !=
          static_cast<int64_t>(declarationCount + functionCount - directCallCount - aggregateCount -
                               uninitializedLocalReturnCount - parameterReferenceCount -
                               parameterReborrowCount + localAliasReborrowCount + localWriteCount +
                               aggregateElementCount + directCallLiteralArgumentCount +
                               receiverCallArgumentCount + conditionalCount * 2 +
                               equalityConditionalCount + loopCount) +
              sequentialLiteralCorrection ||
      facts.calls().size() != directCallCount + receiverCallCount + parameterIndexCount +
                                  equalityConditionalCount + sequentialBinaryCount ||
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
    // Sequential N-local shape: recompute the source classification and verify
    // the materialized bindings against it and against checked facts. Each
    // binding consumes its localNode then its initializerNode, plus two operand
    // nodes when the initializer is a primitive binary; after the last binding
    // come returnNode, returnValueNode, and an optional unsafe block. The
    // materializer and this verifier derive the same per-binding widths, so the
    // ordinals agree.
    bool isSequentialShape = false;
    {
      auto sourceDefinitionIndex = definitionIndex(definitions, function.definition);
      if (sourceDefinitionIndex != zc::none) {
        size_t definitionSlot = 0;
        ZC_IF_SOME(value, sourceDefinitionIndex) { definitionSlot = value; }
        const auto& sourceDefinition = definitions.definitions()[definitionSlot];
        const auto& tree = bound.tree();
        if (tree.contains(sourceDefinition.node)) {
          auto probe = functionReturnShape(tree, tree.node(sourceDefinition.node));
          ZC_IF_SOME(value, probe) { isSequentialShape = value.isSequentialLocalReturn; }
        }
      }
    }
    if (isSequentialShape) {
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
      auto sequentialShapeMaybe = zc::Maybe<SequentialLocalShape>(zc::none);
      // Recompute the classified source shape directly from the function body.
      auto functionShape = functionReturnShape(tree, tree.node(sourceDefinition.node));
      if (functionShape == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      ast::NodeId sourceBody;
      ZC_IF_SOME(value, functionShape) { sourceBody = value.body; }
      sequentialShapeMaybe = sequentialLocalShape(tree, sourceBody);
      if (sequentialShapeMaybe == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      SequentialLocalShape source{};
      ZC_IF_SOME(value, sequentialShapeMaybe) { source = zc::mv(value); }
      const size_t bindingCount = source.bindings.size();
      // Signature and function metadata.
      auto signaturePosition = signatureIndex(signatures.definitions.asPtr(), function.definition);
      auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), function.definition);
      auto bodySpan = bound.parsedModule().spanFor(tree.node(sourceBody).range);
      if (signaturePosition == zc::none || rootPosition == zc::none || bodySpan == zc::none ||
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
      auto returnTypeIndex = factIndex(facts.nodeTypes(), source.returnValue);
      if (returnTypeIndex == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      size_t returnTypeSlot = 0;
      ZC_IF_SOME(value, returnTypeIndex) { returnTypeSlot = value; }
      const auto sequentialType = facts.nodeTypes().entries()[returnTypeSlot].value;
      if (expectedLinkage == zc::none || signature.definition != function.definition ||
          signature.definitionKind != identity::DefinitionKind::Function ||
          root.canonicalDefinition != function.definition || root.sourceModule != module ||
          callable.receiver != zc::none || callable.raises != zc::none ||
          callable.success != function.resultType || function.resultType != sequentialType ||
          !typeExists(sequentialType, semanticTypes) ||
          !sameSpan(signature.declarationSpan, sourceDefinition.source) ||
          !sameSpan(function.sourceSpan, sourceDefinition.source) ||
          !sameVisibility(function.visibility, ZC_ASSERT_NONNULL(expectedVisibility)) ||
          function.linkage != ZC_ASSERT_NONNULL(expectedLinkage) ||
          !sameSpan(block.sourceSpan, ZC_ASSERT_NONNULL(bodySpan)) ||
          function.node != hirId(expectedFunction) || block.node != hirId(expectedFunction + 1) ||
          function.body != block.node || function.parameters.size() != callable.parameters.size() ||
          block.statements.size() != bindingCount + 1) {
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
      // Per-binding node width: 2 (local + initializer) plus 2 operand nodes for
      // a primitive-binary initializer, plus 2 more inner leaf-operand nodes for
      // each operand that is itself a nested one-level primitive binary. The
      // return statement follows the last binding's nodes. This mirrors the
      // materializer's id allocation exactly.
      auto bindingWidth = [&](const SequentialLocalBinding& binding) -> uint32_t {
        if (binding.initializerKind != SequentialInitializerKind::PrimitiveBinary) return 2u;
        uint32_t width = 4u;
        for (const auto* operand : {&binding.leftOperand, &binding.rightOperand}) {
          ZC_IF_SOME(value, *operand) {
            if (value.kind == SequentialBinaryOperandKind::NestedBinary) width += 2u;
          }
        }
        return width;
      };
      uint32_t bindingNodeSpan = 0;
      for (const auto& binding : source.bindings) bindingNodeSpan += bindingWidth(binding);
      const uint32_t returnNodeOrdinal = expectedFunction + 2 + bindingNodeSpan;
      // Verify the return statement structure.
      auto returnSpan = bound.parsedModule().spanFor(tree.node(source.returnStatement).range);
      auto returnValueSpan = bound.parsedModule().spanFor(tree.node(source.returnValue).range);
      if (returnStatement.node != hirId(returnNodeOrdinal) ||
          returnStatement.value != hirId(returnNodeOrdinal + 1) ||
          returnStatement.resultType != sequentialType || returnSpan == zc::none ||
          returnValueSpan == zc::none ||
          !sameSpan(returnStatement.sourceSpan, ZC_ASSERT_NONNULL(returnSpan))) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      // Verify each binding: its local node, initializer node, type, spans, and
      // the checked fact for its initializer.
      bool bindingsValid = true;
      zc::Vector<binder::OwnerLocalBindingId> localBindingIds;
      uint32_t bindingOrdinal = expectedFunction + 2;
      for (size_t bindingIndex = 0; bindingIndex < bindingCount && bindingsValid; ++bindingIndex) {
        const auto& binding = source.bindings[bindingIndex];
        const uint32_t localNodeOrdinal = bindingOrdinal;
        const uint32_t initializerNodeOrdinal = localNodeOrdinal + 1;
        bindingOrdinal += bindingWidth(binding);
        zc::Maybe<const HirLocalBinding&> localBinding;
        for (const auto& local : candidate.impl->locals) {
          if (local.node != hirId(localNodeOrdinal)) continue;
          if (localBinding != zc::none) { bindingsValid = false; }
          localBinding = local;
        }
        if (localBinding == zc::none || block.statements[bindingIndex] != hirId(localNodeOrdinal)) {
          bindingsValid = false;
          break;
        }
        const auto& localValue = ZC_ASSERT_NONNULL(localBinding);
        auto patternSpan = bound.parsedModule().spanFor(tree.node(binding.pattern).range);
        auto initializerSpan = bound.parsedModule().spanFor(tree.node(binding.initializer).range);
        auto ownerBinding = ownerLocalBindingForPattern(definitions, binding.pattern, tree);
        if (patternSpan == zc::none || initializerSpan == zc::none || ownerBinding == zc::none ||
            localValue.local != hirLocalId(static_cast<uint32_t>(bindingIndex + 1)) ||
            localValue.initializer != hirId(initializerNodeOrdinal) ||
            localValue.type != sequentialType ||
            !sameSpan(localValue.sourceSpan, ZC_ASSERT_NONNULL(patternSpan)) ||
            localValue.initializerSpan == zc::none ||
            !sameSpan(ZC_ASSERT_NONNULL(localValue.initializerSpan),
                      ZC_ASSERT_NONNULL(initializerSpan)) ||
            !ownerLocalMatches(definitions, ZC_ASSERT_NONNULL(ownerBinding), binding.pattern,
                               tree)) {
          bindingsValid = false;
          break;
        }
        for (const auto existing : localBindingIds) {
          if (existing == ZC_ASSERT_NONNULL(ownerBinding)) bindingsValid = false;
        }
        if (!bindingsValid) break;
        localBindingIds.add(ZC_ASSERT_NONNULL(ownerBinding));
        // Every binding initializer node carries the function result type.
        auto initializerTypeIndex = factIndex(facts.nodeTypes(), binding.initializer);
        if (initializerTypeIndex == zc::none) {
          bindingsValid = false;
          break;
        }
        size_t initializerTypeSlot = 0;
        ZC_IF_SOME(value, initializerTypeIndex) { initializerTypeSlot = value; }
        if (facts.nodeTypes().entries()[initializerTypeSlot].value != sequentialType) {
          bindingsValid = false;
          break;
        }
        if (binding.initializerKind == SequentialInitializerKind::Literal) {
          zc::Maybe<const HirScalarLiteralExpression&> literal;
          for (const auto& expression : candidate.impl->expressions) {
            if (expression.node != hirId(initializerNodeOrdinal)) continue;
            if (literal != zc::none) bindingsValid = false;
            literal = expression;
          }
          auto literalIndex = factIndex(facts.literals(), binding.initializer);
          if (!bindingsValid || literal == zc::none || literalIndex == zc::none) {
            bindingsValid = false;
            break;
          }
          size_t literalSlot = 0;
          ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
          const auto& literalFact = facts.literals().entries()[literalSlot].value;
          const auto& literalValue = ZC_ASSERT_NONNULL(literal);
          if (literalValue.type != sequentialType ||
              literalValue.category != HirValueCategory::Value ||
              literalFact.type != sequentialType ||
              !sameConstant(literalValue.value, literalFact.literal, module, registries,
                            semanticTypes) ||
              !sameSpan(literalValue.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan))) {
            bindingsValid = false;
            break;
          }
        } else if (binding.initializerKind == SequentialInitializerKind::Aggregate) {
          zc::Maybe<const HirNominalAggregateExpression&> aggregate;
          for (const auto& expression : candidate.impl->aggregates) {
            if (expression.node != hirId(initializerNodeOrdinal)) continue;
            if (aggregate != zc::none) bindingsValid = false;
            aggregate = expression;
          }
          auto aggregateIndex = factIndex(facts.aggregates(), binding.initializer);
          if (!bindingsValid || aggregate == zc::none || aggregateIndex == zc::none) {
            bindingsValid = false;
            break;
          }
          size_t aggregateSlot = 0;
          ZC_IF_SOME(value, aggregateIndex) { aggregateSlot = value; }
          const auto& checkedAggregate = facts.aggregates().entries()[aggregateSlot].value;
          const auto& aggregateValue = ZC_ASSERT_NONNULL(aggregate);
          if (aggregateValue.type != sequentialType ||
              aggregateValue.category != HirValueCategory::Value ||
              checkedAggregate.node != binding.initializer ||
              !checkedAggregate.kind.variant().is<checker::checked::NominalAggregate>() ||
              checkedAggregate.kind.variant()
                      .get<checker::checked::NominalAggregate>()
                      .definition != aggregateValue.definition ||
              checkedAggregate.resultType != aggregateValue.type ||
              !sameSpan(checkedAggregate.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan)) ||
              checkedAggregate.elements.size() != aggregateValue.elements.size()) {
            bindingsValid = false;
            break;
          }
          for (size_t elementIndex = 0; elementIndex < aggregateValue.elements.size();
               ++elementIndex) {
            const auto& checkedElement = checkedAggregate.elements[elementIndex];
            const auto& element = aggregateValue.elements[elementIndex];
            auto elementLiteral = factIndex(facts.literals(), checkedElement.sourceNode);
            if (checkedElement.field == zc::none || checkedElement.index != elementIndex ||
                checkedElement.sourceType != checkedElement.destinationType ||
                checkedElement.adjustment != zc::none || elementLiteral == zc::none ||
                ZC_ASSERT_NONNULL(checkedElement.field) != element.field ||
                checkedElement.destinationType != element.type) {
              bindingsValid = false;
              break;
            }
            size_t elementSlot = 0;
            ZC_IF_SOME(item, elementLiteral) { elementSlot = item; }
            const auto& checkedLiteral = facts.literals().entries()[elementSlot].value;
            if (checkedLiteral.type != element.type ||
                !sameConstant(checkedLiteral.literal, element.value, module, registries,
                              semanticTypes) ||
                !sameSpan(checkedLiteral.sourceSpan, element.sourceSpan)) {
              bindingsValid = false;
              break;
            }
          }
        } else if (binding.initializerKind == SequentialInitializerKind::LocalReference) {
          zc::Maybe<const HirLocalReferenceExpression&> reference;
          for (const auto& localReference : candidate.impl->localReferences) {
            if (localReference.node != hirId(initializerNodeOrdinal)) continue;
            if (reference != zc::none) bindingsValid = false;
            reference = localReference;
          }
          auto referenceBinding = resolvedOwnerLocal(bound.bindings(), binding.initializer);
          if (!bindingsValid || reference == zc::none || referenceBinding == zc::none ||
              binding.referencedLocal >= localBindingIds.size() ||
              ZC_ASSERT_NONNULL(referenceBinding) != localBindingIds[binding.referencedLocal] ||
              ZC_ASSERT_NONNULL(reference).local !=
                  hirLocalId(static_cast<uint32_t>(binding.referencedLocal + 1)) ||
              ZC_ASSERT_NONNULL(reference).type != sequentialType ||
              ZC_ASSERT_NONNULL(reference).category != HirValueCategory::Place ||
              !sameSpan(ZC_ASSERT_NONNULL(reference).sourceSpan,
                        ZC_ASSERT_NONNULL(initializerSpan))) {
            bindingsValid = false;
            break;
          }
        } else if (binding.initializerKind == SequentialInitializerKind::ParameterReference) {
          zc::Maybe<const HirParameterReferenceExpression&> reference;
          for (const auto& parameterReference : candidate.impl->parameterReferences) {
            if (parameterReference.node != hirId(initializerNodeOrdinal)) continue;
            if (reference != zc::none) bindingsValid = false;
            reference = parameterReference;
          }
          auto parameterHandle = resolvedCallableParameter(bound.bindings(), binding.initializer);
          if (!bindingsValid || reference == zc::none || parameterHandle == zc::none) {
            bindingsValid = false;
            break;
          }
          bool parameterMatches = false;
          ZC_IF_SOME(handle, parameterHandle) {
            auto authority = registries.callableParameter(handle);
            ZC_IF_SOME(entry, authority) {
              parameterMatches = ZC_ASSERT_NONNULL(reference).parameter == entry.key();
            }
          }
          if (!parameterMatches || ZC_ASSERT_NONNULL(reference).type != sequentialType ||
              ZC_ASSERT_NONNULL(reference).category != HirValueCategory::Place ||
              !sameSpan(ZC_ASSERT_NONNULL(reference).sourceSpan,
                        ZC_ASSERT_NONNULL(initializerSpan))) {
            bindingsValid = false;
            break;
          }
        } else {
          // PrimitiveBinary: the initializer node is a HirPrimitiveBinaryExpression
          // referencing its two operand nodes (left at +1, right at +2). Each
          // operand materializes as a literal, parameter reference, or reference
          // to an earlier local, and its checked call fact keys on the initializer
          // node. This mirrors the return-position comparison verification.
          const uint32_t leftOperandOrdinal = initializerNodeOrdinal + 1;
          const uint32_t rightOperandOrdinal = initializerNodeOrdinal + 2;
          zc::Maybe<const HirPrimitiveBinaryExpression&> binary;
          for (const auto& operation : candidate.impl->primitiveBinaryOperations) {
            if (operation.node != hirId(initializerNodeOrdinal)) continue;
            if (binary != zc::none) bindingsValid = false;
            binary = operation;
          }
          auto callIndex = factIndex(facts.calls(), binding.initializer);
          if (!bindingsValid || binary == zc::none || callIndex == zc::none ||
              binding.leftOperand == zc::none || binding.rightOperand == zc::none) {
            bindingsValid = false;
            break;
          }
          const auto& binaryValue = ZC_ASSERT_NONNULL(binary);
          size_t callSlot = 0;
          ZC_IF_SOME(value, callIndex) { callSlot = value; }
          const auto& callFact = facts.calls().entries()[callSlot].value;
          const auto& call = callFact.invocation;
          const auto& selected = call.selected.variant();
          if (!selected.is<checker::checked::PrimitiveCallable>()) {
            bindingsValid = false;
            break;
          }
          const auto operation = selected.get<checker::checked::PrimitiveCallable>().operation;
          const bool comparison = isScalarComparisonOperation(operation);
          const bool arithmetic = isScalarArithmeticOperation(operation);
          const auto binaryOperandType = binaryValue.operandType;
          if ((!comparison && !arithmetic) || binaryValue.node != hirId(initializerNodeOrdinal) ||
              binaryValue.left != hirId(leftOperandOrdinal) ||
              binaryValue.right != hirId(rightOperandOrdinal) ||
              binaryValue.type != sequentialType ||
              binaryValue.category != HirValueCategory::Value ||
              binaryValue.operation != operation ||
              !sameSpan(binaryValue.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan)) ||
              (arithmetic && sequentialType != binaryOperandType) ||
              callFact.node != binding.initializer || call.calleeType != binaryOperandType ||
              call.receiver != zc::none || call.receiverMode != zc::none ||
              call.receiverAdjustment != zc::none || call.arguments.size() != 2 ||
              call.arguments[0].sourceType != binaryOperandType ||
              call.arguments[1].sourceType != binaryOperandType ||
              call.successType != sequentialType || call.resultType != sequentialType ||
              call.substitutions != zc::none || call.witnesses != zc::none ||
              call.raises != zc::none) {
            bindingsValid = false;
            break;
          }
          // Verify one leaf operand node (literal, parameter, or earlier local)
          // at a fixed ordinal against its expected operand type.
          auto verifyLeaf = [&](uint32_t leafOrdinal, const SequentialBinaryLeafOperand& leaf,
                                identity::SemanticTypeId leafType) -> bool {
            auto leafSpan = bound.parsedModule().spanFor(tree.node(leaf.node).range);
            if (leafSpan == zc::none) return false;
            if (leaf.kind == SequentialBinaryOperandKind::Literal) {
              zc::Maybe<const HirScalarLiteralExpression&> literal;
              for (const auto& expression : candidate.impl->expressions) {
                if (expression.node != hirId(leafOrdinal)) continue;
                if (literal != zc::none) return false;
                literal = expression;
              }
              auto operandLiteral = factIndex(facts.literals(), leaf.node);
              if (literal == zc::none || operandLiteral == zc::none) return false;
              size_t literalSlot = 0;
              ZC_IF_SOME(value, operandLiteral) { literalSlot = value; }
              const auto& literalFact = facts.literals().entries()[literalSlot].value;
              const auto& literalValue = ZC_ASSERT_NONNULL(literal);
              return literalValue.type == leafType &&
                     literalValue.category == HirValueCategory::Value &&
                     literalFact.type == leafType &&
                     sameConstant(literalValue.value, literalFact.literal, module, registries,
                                  semanticTypes) &&
                     sameSpan(literalValue.sourceSpan, ZC_ASSERT_NONNULL(leafSpan));
            }
            if (leaf.kind == SequentialBinaryOperandKind::LocalReference) {
              zc::Maybe<const HirLocalReferenceExpression&> reference;
              for (const auto& localReference : candidate.impl->localReferences) {
                if (localReference.node != hirId(leafOrdinal)) continue;
                if (reference != zc::none) return false;
                reference = localReference;
              }
              auto referenceBinding = resolvedOwnerLocal(bound.bindings(), leaf.node);
              if (reference == zc::none || referenceBinding == zc::none ||
                  leaf.referencedLocal >= localBindingIds.size() ||
                  ZC_ASSERT_NONNULL(referenceBinding) != localBindingIds[leaf.referencedLocal]) {
                return false;
              }
              const auto& referenceValue = ZC_ASSERT_NONNULL(reference);
              return referenceValue.local ==
                         hirLocalId(static_cast<uint32_t>(leaf.referencedLocal + 1)) &&
                     referenceValue.type == leafType &&
                     referenceValue.category == HirValueCategory::Place &&
                     sameSpan(referenceValue.sourceSpan, ZC_ASSERT_NONNULL(leafSpan));
            }
            zc::Maybe<const HirParameterReferenceExpression&> reference;
            for (const auto& parameterReference : candidate.impl->parameterReferences) {
              if (parameterReference.node != hirId(leafOrdinal)) continue;
              if (reference != zc::none) return false;
              reference = parameterReference;
            }
            auto parameterHandle = resolvedCallableParameter(bound.bindings(), leaf.node);
            if (reference == zc::none || parameterHandle == zc::none) return false;
            bool parameterMatches = false;
            ZC_IF_SOME(handle, parameterHandle) {
              auto authority = registries.callableParameter(handle);
              ZC_IF_SOME(entry, authority) {
                parameterMatches = ZC_ASSERT_NONNULL(reference).parameter == entry.key();
              }
            }
            const auto& referenceValue = ZC_ASSERT_NONNULL(reference);
            return parameterMatches && referenceValue.type == leafType &&
                   referenceValue.category == HirValueCategory::Place &&
                   sameSpan(referenceValue.sourceSpan, ZC_ASSERT_NONNULL(leafSpan));
          };
          // Verify each operand node against its classification. `leafOrdinal` is
          // the first of the two node ids reserved for a nested operand's inner
          // leaves; it is only consumed for a nested binary.
          auto verifyOperand = [&](uint32_t operandOrdinal, uint32_t leafOrdinal,
                                   const SequentialBinaryOperand& operand) -> bool {
            auto operandSpan = bound.parsedModule().spanFor(tree.node(operand.node).range);
            if (operandSpan == zc::none) return false;
            if (operand.kind == SequentialBinaryOperandKind::NestedBinary) {
              // The nested operand node is its own HirPrimitiveBinaryExpression
              // whose inner leaves are at leafOrdinal / leafOrdinal + 1, with its
              // own checked call fact keyed on the operand node.
              if (operand.nestedLeft == zc::none || operand.nestedRight == zc::none ||
                  operand.nestedOperation == zc::none) {
                return false;
              }
              zc::Maybe<const HirPrimitiveBinaryExpression&> nested;
              for (const auto& operation : candidate.impl->primitiveBinaryOperations) {
                if (operation.node != hirId(operandOrdinal)) continue;
                if (nested != zc::none) return false;
                nested = operation;
              }
              auto nestedCallIndex = factIndex(facts.calls(), operand.node);
              if (nested == zc::none || nestedCallIndex == zc::none) return false;
              size_t nestedCallSlot = 0;
              ZC_IF_SOME(value, nestedCallIndex) { nestedCallSlot = value; }
              const auto& nestedFact = facts.calls().entries()[nestedCallSlot].value;
              const auto& nestedCall = nestedFact.invocation;
              const auto& nestedSelected = nestedCall.selected.variant();
              if (!nestedSelected.is<checker::checked::PrimitiveCallable>()) return false;
              const auto nestedOp =
                  nestedSelected.get<checker::checked::PrimitiveCallable>().operation;
              const bool nestedComparison = isScalarComparisonOperation(nestedOp);
              const bool nestedArithmetic = isScalarArithmeticOperation(nestedOp);
              const auto& nestedValue = ZC_ASSERT_NONNULL(nested);
              const auto nestedOperandType = nestedValue.operandType;
              const ast::NodeId nestedLeftNode(
                  tree.node(operand.node).payload.words[ast::kBinaryExprLhsWord]);
              const ast::NodeId nestedRightNode(
                  tree.node(operand.node).payload.words[ast::kBinaryExprRhsWord]);
              if ((!nestedComparison && !nestedArithmetic) ||
                  nestedValue.node != hirId(operandOrdinal) ||
                  nestedValue.left != hirId(leafOrdinal) ||
                  nestedValue.right != hirId(leafOrdinal + 1) ||
                  nestedValue.type != binaryOperandType ||
                  nestedValue.category != HirValueCategory::Value ||
                  nestedValue.operation != nestedOp ||
                  nestedOp != ZC_ASSERT_NONNULL(operand.nestedOperation) ||
                  !sameSpan(nestedValue.sourceSpan, ZC_ASSERT_NONNULL(operandSpan)) ||
                  (nestedArithmetic && binaryOperandType != nestedOperandType) ||
                  nestedFact.node != operand.node || nestedCall.calleeType != nestedOperandType ||
                  nestedCall.receiver != zc::none || nestedCall.receiverMode != zc::none ||
                  nestedCall.receiverAdjustment != zc::none || nestedCall.arguments.size() != 2 ||
                  nestedCall.arguments[0].sourceNode != nestedLeftNode ||
                  nestedCall.arguments[0].sourceType != nestedOperandType ||
                  nestedCall.arguments[1].sourceNode != nestedRightNode ||
                  nestedCall.arguments[1].sourceType != nestedOperandType ||
                  nestedCall.resultType != binaryOperandType ||
                  nestedCall.substitutions != zc::none || nestedCall.witnesses != zc::none ||
                  nestedCall.raises != zc::none) {
                return false;
              }
              bool leavesValid = false;
              ZC_IF_SOME(leftLeaf, operand.nestedLeft) {
                ZC_IF_SOME(rightLeaf, operand.nestedRight) {
                  leavesValid = verifyLeaf(leafOrdinal, leftLeaf, nestedOperandType) &&
                                verifyLeaf(leafOrdinal + 1, rightLeaf, nestedOperandType);
                }
              }
              return leavesValid;
            }
            SequentialBinaryLeafOperand leaf{operand.node, operand.kind, operand.referencedLocal};
            return verifyLeaf(operandOrdinal, leaf, binaryOperandType);
          };
          // The nested operand's inner leaves occupy the two node ids after both
          // operand ids; only one operand may be nested, so the leaf window is
          // shared.
          const uint32_t nestedLeafOrdinal = rightOperandOrdinal + 1;
          bool operandsValid = false;
          ZC_IF_SOME(left, binding.leftOperand) {
            ZC_IF_SOME(right, binding.rightOperand) {
              operandsValid = verifyOperand(leftOperandOrdinal, nestedLeafOrdinal, left) &&
                              verifyOperand(rightOperandOrdinal, nestedLeafOrdinal, right) &&
                              call.arguments[0].sourceNode == left.node &&
                              call.arguments[1].sourceNode == right.node;
            }
          }
          if (!operandsValid) {
            bindingsValid = false;
            break;
          }
        }
      }
      if (!bindingsValid) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      // Verify the returned place: a parameter or one of the declared locals.
      if (source.returnsLocal != zc::none) {
        size_t returnLocal = 0;
        ZC_IF_SOME(value, source.returnsLocal) { returnLocal = value; }
        zc::Maybe<const HirLocalReferenceExpression&> returnReference;
        for (const auto& localReference : candidate.impl->localReferences) {
          if (localReference.node != hirId(returnNodeOrdinal + 1)) continue;
          if (returnReference != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          returnReference = localReference;
        }
        if (returnReference == zc::none ||
            ZC_ASSERT_NONNULL(returnReference).local !=
                hirLocalId(static_cast<uint32_t>(returnLocal + 1)) ||
            ZC_ASSERT_NONNULL(returnReference).type != sequentialType ||
            ZC_ASSERT_NONNULL(returnReference).category != HirValueCategory::Place ||
            !sameSpan(ZC_ASSERT_NONNULL(returnReference).sourceSpan,
                      ZC_ASSERT_NONNULL(returnValueSpan))) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
      } else {
        zc::Maybe<const HirParameterReferenceExpression&> returnReference;
        for (const auto& parameterReference : candidate.impl->parameterReferences) {
          if (parameterReference.node != hirId(returnNodeOrdinal + 1)) continue;
          if (returnReference != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          returnReference = parameterReference;
        }
        auto parameterHandle = resolvedCallableParameter(bound.bindings(), source.returnValue);
        bool parameterMatches = false;
        if (returnReference != zc::none) {
          ZC_IF_SOME(handle, parameterHandle) {
            auto authority = registries.callableParameter(handle);
            ZC_IF_SOME(entry, authority) {
              parameterMatches = ZC_ASSERT_NONNULL(returnReference).parameter == entry.key();
            }
          }
        }
        if (returnReference == zc::none || parameterHandle == zc::none || !parameterMatches ||
            ZC_ASSERT_NONNULL(returnReference).type != sequentialType ||
            ZC_ASSERT_NONNULL(returnReference).category != HirValueCategory::Place ||
            !sameSpan(ZC_ASSERT_NONNULL(returnReference).sourceSpan,
                      ZC_ASSERT_NONNULL(returnValueSpan))) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
      }
      // Unsafe-block boundary sits immediately after the return value node.
      FunctionReturnShape returnShape{};
      ZC_IF_SOME(value, functionShape) { returnShape = value; }
      if ((returnShape.unsafeBlock != zc::none) != (function.unsafeBlock != zc::none)) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      if (returnShape.unsafeBlock != zc::none) {
        const auto expectedUnsafeBlock = hirId(returnNodeOrdinal + 2);
        if (function.unsafeBlock != expectedUnsafeBlock) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        zc::Maybe<const HirUnsafeBlockExpression&> unsafeBlock;
        for (const auto& candidateBlock : candidate.impl->unsafeBlocks) {
          if (candidateBlock.node != expectedUnsafeBlock) continue;
          if (unsafeBlock != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          unsafeBlock = candidateBlock;
        }
        if (unsafeBlock == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        ZC_IF_SOME(unsafe, unsafeBlock) {
          auto sourceUnsafeSpan = bound.parsedModule().spanFor(
              tree.node(ZC_ASSERT_NONNULL(returnShape.unsafeBlock)).range);
          if (unsafe.body != returnStatement.value || unsafe.type != function.resultType ||
              sourceUnsafeSpan == zc::none ||
              !sameSpan(unsafe.sourceSpan, ZC_ASSERT_NONNULL(sourceUnsafeSpan))) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
        nextFunction += bindingNodeSpan + 5;
      } else {
        nextFunction += bindingNodeSpan + 4;
      }
      continue;
    }
    // Conditional shape: if/else with scalar returns in both branches.
    {
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
      if (source.isConditional) {
        auto signaturePosition =
            signatureIndex(signatures.definitions.asPtr(), function.definition);
        auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), function.definition);
        if (signaturePosition == zc::none || rootPosition == zc::none) {
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
            function.parameters.size() != callable.parameters.size()) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        for (size_t parameterIndex = 0; parameterIndex < function.parameters.size();
             ++parameterIndex) {
          const auto& parameter = function.parameters[parameterIndex];
          const auto& sourceParameter = callable.parameters[parameterIndex];
          if (parameter.key != sourceParameter.parameter ||
              parameter.type != sourceParameter.type || sourceParameter.hasDefault ||
              !typeExists(parameter.type, semanticTypes)) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
        auto bodySpan = bound.parsedModule().spanFor(tree.node(source.body).range);
        auto returnSpan = bound.parsedModule().spanFor(tree.node(source.returnStatement).range);
        auto conditionSpan = bound.parsedModule().spanFor(tree.node(source.condition).range);
        auto thenSpan = bound.parsedModule().spanFor(tree.node(source.thenReturnValue).range);
        auto elseSpan = bound.parsedModule().spanFor(tree.node(source.elseReturnValue).range);
        if (bodySpan == zc::none || returnSpan == zc::none || conditionSpan == zc::none ||
            thenSpan == zc::none || elseSpan == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        auto conditionTypeIndex = factIndex(facts.nodeTypes(), source.condition);
        auto thenTypeIndex = factIndex(facts.nodeTypes(), source.thenReturnValue);
        auto elseTypeIndex = factIndex(facts.nodeTypes(), source.elseReturnValue);
        const bool thenIsParameter =
            tree.node(source.thenReturnValue).kind == ast::SyntaxKind::IdentExpr;
        const bool elseIsParameter =
            tree.node(source.elseReturnValue).kind == ast::SyntaxKind::IdentExpr;
        auto thenLiteralIndex =
            thenIsParameter ? zc::none : factIndex(facts.literals(), source.thenReturnValue);
        auto elseLiteralIndex =
            elseIsParameter ? zc::none : factIndex(facts.literals(), source.elseReturnValue);
        if (conditionTypeIndex == zc::none || thenTypeIndex == zc::none ||
            elseTypeIndex == zc::none || (!thenIsParameter && thenLiteralIndex == zc::none) ||
            (!elseIsParameter && elseLiteralIndex == zc::none)) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t conditionTypeSlot = 0;
        size_t thenTypeSlot = 0;
        size_t elseTypeSlot = 0;
        ZC_IF_SOME(value, conditionTypeIndex) { conditionTypeSlot = value; }
        ZC_IF_SOME(value, thenTypeIndex) { thenTypeSlot = value; }
        ZC_IF_SOME(value, elseTypeIndex) { elseTypeSlot = value; }
        const auto conditionType = facts.nodeTypes().entries()[conditionTypeSlot].value;
        const auto thenType = facts.nodeTypes().entries()[thenTypeSlot].value;
        const auto elseType = facts.nodeTypes().entries()[elseTypeSlot].value;
        if (thenType != function.resultType || elseType != function.resultType) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        // The condition is either a bare bool parameter reference or an `a == b`
        // equality comparison of two same-typed scalar parameters. The two forms
        // materialize different node counts, so the arm/conditional/return
        // offsets and the nextFunction increment differ between them.
        const bool conditionIsEquality = source.conditionIsEquality;
        const uint32_t thenOffset = conditionIsEquality ? 5 : 3;
        const uint32_t elseOffset = conditionIsEquality ? 6 : 4;
        const uint32_t conditionalOffset = conditionIsEquality ? 7 : 5;
        const uint32_t returnOffset = conditionIsEquality ? 8 : 6;
        const uint32_t functionNodeCount = conditionIsEquality ? 9 : 7;
        // Common arm verification: each arm value node resolves to a
        // scalar-literal expression or a parameter reference depending on its AST
        // kind. Locate the matching materialized node and cross-check it.
        auto verifyArm = [&](bool isParameter, ast::NodeId armSourceNode, HirNodeId armId,
                             identity::SemanticTypeId armType, zc::Maybe<size_t> literalSlotIndex,
                             const identity::SourceSpan& armSpan) -> bool {
          if (isParameter) {
            auto parameter = resolvedCallableParameter(bound.bindings(), armSourceNode);
            if (parameter == zc::none) return false;
            identity::CallableParameterId handle;
            ZC_IF_SOME(value, parameter) { handle = value; }
            auto authority = registries.callableParameter(handle);
            if (authority == zc::none) return false;
            zc::Maybe<const HirParameterReferenceExpression&> reference;
            for (const auto& candidateReference : candidate.impl->parameterReferences) {
              if (candidateReference.node != armId) continue;
              if (reference != zc::none) return false;
              reference = candidateReference;
            }
            bool ok = false;
            ZC_IF_SOME(referenceValue, reference) {
              ZC_IF_SOME(authorityValue, authority) {
                ok = referenceValue.parameter == authorityValue.key() &&
                     referenceValue.type == armType &&
                     referenceValue.category == HirValueCategory::Place &&
                     sameSpan(referenceValue.sourceSpan, armSpan);
              }
            }
            return ok;
          }
          if (literalSlotIndex == zc::none) return false;
          size_t literalSlot = 0;
          ZC_IF_SOME(value, literalSlotIndex) { literalSlot = value; }
          const auto& literalFact = facts.literals().entries()[literalSlot].value;
          zc::Maybe<const HirScalarLiteralExpression&> expressionValue;
          for (const auto& expression : candidate.impl->expressions) {
            if (expression.node != armId) continue;
            if (expressionValue != zc::none) return false;
            expressionValue = expression;
          }
          bool ok = false;
          ZC_IF_SOME(expression, expressionValue) {
            ok = expression.type == armType && expression.category == HirValueCategory::Value &&
                 sameConstant(expression.value, literalFact.literal, module, registries,
                              semanticTypes) &&
                 sameSpan(expression.sourceSpan, armSpan);
          }
          return ok;
        };
        zc::Maybe<const HirConditionalExpression&> conditionalValue;
        for (const auto& candidate : candidate.impl->conditionals) {
          if (candidate.node != hirId(expectedFunction + conditionalOffset)) continue;
          if (conditionalValue != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          conditionalValue = candidate;
        }
        if (conditionalValue == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        if (!verifyArm(thenIsParameter, source.thenReturnValue,
                       hirId(expectedFunction + thenOffset), thenType, thenLiteralIndex,
                       ZC_ASSERT_NONNULL(thenSpan)) ||
            !verifyArm(elseIsParameter, source.elseReturnValue,
                       hirId(expectedFunction + elseOffset), elseType, elseLiteralIndex,
                       ZC_ASSERT_NONNULL(elseSpan))) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        // Verify the shared function/block/return skeleton and arm node ids.
        bool skeletonOk = false;
        ZC_IF_SOME(conditional, conditionalValue) {
          skeletonOk = function.node == hirId(expectedFunction) &&
                       block.node == hirId(expectedFunction + 1) && function.body == block.node &&
                       block.statements.size() == 1 &&
                       block.statements[0] == returnStatement.node &&
                       returnStatement.node == hirId(expectedFunction + returnOffset) &&
                       returnStatement.value == conditional.node &&
                       returnStatement.resultType == function.resultType &&
                       conditional.thenReturnValue == hirId(expectedFunction + thenOffset) &&
                       conditional.elseReturnValue == hirId(expectedFunction + elseOffset) &&
                       conditional.type == function.resultType &&
                       conditional.category == HirValueCategory::Value &&
                       sameSpan(block.sourceSpan, ZC_ASSERT_NONNULL(bodySpan)) &&
                       sameSpan(returnStatement.sourceSpan, ZC_ASSERT_NONNULL(returnSpan)) &&
                       sameSpan(conditional.sourceSpan, ZC_ASSERT_NONNULL(returnSpan)) &&
                       typeExists(function.resultType, semanticTypes) &&
                       typeExists(conditionType, semanticTypes);
        }
        if (!skeletonOk) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        if (conditionIsEquality) {
          // Cross-check the two operand nodes and the comparison node against the
          // checked relational call fact. Each operand is a parameter reference
          // or a scalar literal; the shared operand type comes from the fact.
          auto leftTypeIndex = factIndex(facts.nodeTypes(), source.conditionLeft);
          auto rightTypeIndex = factIndex(facts.nodeTypes(), source.conditionRight);
          auto callIndex = factIndex(facts.calls(), source.condition);
          auto leftSpan = bound.parsedModule().spanFor(tree.node(source.conditionLeft).range);
          auto rightSpan = bound.parsedModule().spanFor(tree.node(source.conditionRight).range);
          if (leftTypeIndex == zc::none || rightTypeIndex == zc::none || callIndex == zc::none ||
              leftSpan == zc::none || rightSpan == zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::MissingRequiredFact, module,
                                                registries, index + 1);
          }
          size_t leftTypeSlot = 0;
          size_t rightTypeSlot = 0;
          size_t callSlot = 0;
          ZC_IF_SOME(value, leftTypeIndex) { leftTypeSlot = value; }
          ZC_IF_SOME(value, rightTypeIndex) { rightTypeSlot = value; }
          ZC_IF_SOME(value, callIndex) { callSlot = value; }
          const auto operandType = facts.nodeTypes().entries()[leftTypeSlot].value;
          const auto rightType = facts.nodeTypes().entries()[rightTypeSlot].value;
          const auto& callFact = facts.calls().entries()[callSlot].value;
          const auto& call = callFact.invocation;
          const auto& selected = call.selected.variant();
          if (operandType != rightType || callFact.node != source.condition ||
              !selected.is<checker::checked::PrimitiveCallable>() ||
              !isScalarComparisonOperation(
                  selected.get<checker::checked::PrimitiveCallable>().operation) ||
              call.calleeType != operandType || call.receiver != zc::none ||
              call.receiverMode != zc::none || call.receiverAdjustment != zc::none ||
              call.arguments.size() != 2 || call.arguments[0].sourceNode != source.conditionLeft ||
              call.arguments[0].sourceType != operandType ||
              call.arguments[1].sourceNode != source.conditionRight ||
              call.arguments[1].sourceType != operandType || call.successType != conditionType ||
              call.resultType != conditionType || call.substitutions != zc::none ||
              call.witnesses != zc::none || call.raises != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          // Verify one comparison operand: a parameter operand resolves to a
          // parameter reference at the fixed node id; a literal operand resolves
          // to a scalar-literal expression matching its checked literal fact.
          auto verifyOperand = [&](bool isLiteral, ast::NodeId operandSourceNode,
                                   HirNodeId operandId,
                                   const identity::SourceSpan& operandSpan) -> bool {
            if (!isLiteral) {
              auto parameter = resolvedCallableParameter(bound.bindings(), operandSourceNode);
              if (parameter == zc::none) return false;
              identity::CallableParameterId handle;
              ZC_IF_SOME(value, parameter) { handle = value; }
              auto authority = registries.callableParameter(handle);
              if (authority == zc::none) return false;
              zc::Maybe<const HirParameterReferenceExpression&> reference;
              for (const auto& candidateReference : candidate.impl->parameterReferences) {
                if (candidateReference.node != operandId) continue;
                if (reference != zc::none) return false;
                reference = candidateReference;
              }
              bool ok = false;
              ZC_IF_SOME(referenceValue, reference) {
                ZC_IF_SOME(authorityValue, authority) {
                  ok = referenceValue.parameter == authorityValue.key() &&
                       referenceValue.type == operandType &&
                       referenceValue.category == HirValueCategory::Place &&
                       sameSpan(referenceValue.sourceSpan, operandSpan);
                }
              }
              return ok;
            }
            auto literalIndex = factIndex(facts.literals(), operandSourceNode);
            if (literalIndex == zc::none) return false;
            size_t literalSlot = 0;
            ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
            const auto& literalFact = facts.literals().entries()[literalSlot].value;
            zc::Maybe<const HirScalarLiteralExpression&> expressionValue;
            for (const auto& expression : candidate.impl->expressions) {
              if (expression.node != operandId) continue;
              if (expressionValue != zc::none) return false;
              expressionValue = expression;
            }
            bool ok = false;
            ZC_IF_SOME(expression, expressionValue) {
              ok = expression.type == operandType &&
                   expression.category == HirValueCategory::Value &&
                   sameConstant(expression.value, literalFact.literal, module, registries,
                                semanticTypes) &&
                   sameSpan(expression.sourceSpan, operandSpan);
            }
            return ok;
          };
          if (!verifyOperand(source.conditionLeftIsLiteral, source.conditionLeft,
                             hirId(expectedFunction + 2), ZC_ASSERT_NONNULL(leftSpan)) ||
              !verifyOperand(source.conditionRightIsLiteral, source.conditionRight,
                             hirId(expectedFunction + 3), ZC_ASSERT_NONNULL(rightSpan))) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          zc::Maybe<const HirPrimitiveBinaryExpression&> equalityValue;
          for (const auto& equality : candidate.impl->primitiveBinaryOperations) {
            if (equality.node != hirId(expectedFunction + 4)) continue;
            if (equalityValue != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            equalityValue = equality;
          }
          if (equalityValue == zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::MissingRequiredFact, module,
                                                registries, index + 1);
          }
          bool equalityOk = false;
          ZC_IF_SOME(equality, equalityValue) {
            ZC_IF_SOME(conditional, conditionalValue) {
              equalityOk = equality.left == hirId(expectedFunction + 2) &&
                           equality.right == hirId(expectedFunction + 3) &&
                           equality.operandType == operandType && equality.type == conditionType &&
                           equality.category == HirValueCategory::Value &&
                           equality.operation ==
                               selected.get<checker::checked::PrimitiveCallable>().operation &&
                           sameSpan(equality.sourceSpan, ZC_ASSERT_NONNULL(conditionSpan)) &&
                           conditional.condition == equality.node &&
                           typeExists(operandType, semanticTypes);
            }
          }
          if (!equalityOk) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        } else {
          auto conditionParameter = resolvedCallableParameter(bound.bindings(), source.condition);
          if (conditionParameter == zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          identity::CallableParameterId conditionHandle;
          ZC_IF_SOME(value, conditionParameter) { conditionHandle = value; }
          auto conditionAuthority = registries.callableParameter(conditionHandle);
          if (conditionAuthority == zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::MissingRequiredFact, module,
                                                registries, index + 1);
          }
          zc::Maybe<const HirParameterReferenceExpression&> conditionReference;
          for (const auto& reference : candidate.impl->parameterReferences) {
            if (reference.node != hirId(expectedFunction + 2)) continue;
            if (conditionReference != zc::none) {
              return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                  ir::IrFailureKind::AdditionalFact, module,
                                                  registries, index + 1);
            }
            conditionReference = reference;
          }
          if (conditionReference == zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::MissingRequiredFact, module,
                                                registries, index + 1);
          }
          bool conditionOk = false;
          ZC_IF_SOME(conditional, conditionalValue) {
            ZC_IF_SOME(conditionRef, conditionReference) {
              ZC_IF_SOME(authority, conditionAuthority) {
                conditionOk = conditional.condition == conditionRef.node &&
                              conditionRef.parameter == authority.key() &&
                              conditionRef.type == conditionType &&
                              conditionRef.category == HirValueCategory::Place &&
                              sameSpan(conditionRef.sourceSpan, ZC_ASSERT_NONNULL(conditionSpan));
              }
            }
          }
          if (!conditionOk) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
        nextFunction += functionNodeCount;
        continue;
      }
    }
    // Comparison-return shape: `return <a CMP b>`. Fixed-id layout, relative to
    // the function id (6 nodes): +0 function, +1 body block, +2 left operand,
    // +3 right operand, +4 comparison, +5 return. The materializer emits the
    // same six nodes, so this verifier's offsets and nextFunction increment must
    // agree with it exactly.
    {
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
      if (source.returnsComparison) {
        auto signaturePosition =
            signatureIndex(signatures.definitions.asPtr(), function.definition);
        auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), function.definition);
        if (signaturePosition == zc::none || rootPosition == zc::none) {
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
            function.parameters.size() != callable.parameters.size()) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        for (size_t parameterIndex = 0; parameterIndex < function.parameters.size();
             ++parameterIndex) {
          const auto& parameter = function.parameters[parameterIndex];
          const auto& sourceParameter = callable.parameters[parameterIndex];
          if (parameter.key != sourceParameter.parameter ||
              parameter.type != sourceParameter.type || sourceParameter.hasDefault ||
              !typeExists(parameter.type, semanticTypes)) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
        auto bodySpan = bound.parsedModule().spanFor(tree.node(source.body).range);
        auto returnSpan = bound.parsedModule().spanFor(tree.node(source.returnStatement).range);
        auto valueSpan = bound.parsedModule().spanFor(tree.node(source.value).range);
        auto leftSpan = bound.parsedModule().spanFor(tree.node(source.comparisonLeft).range);
        auto rightSpan = bound.parsedModule().spanFor(tree.node(source.comparisonRight).range);
        auto nodeTypeIndex = factIndex(facts.nodeTypes(), source.value);
        auto leftTypeIndex = factIndex(facts.nodeTypes(), source.comparisonLeft);
        auto rightTypeIndex = factIndex(facts.nodeTypes(), source.comparisonRight);
        auto callIndex = factIndex(facts.calls(), source.value);
        if (bodySpan == zc::none || returnSpan == zc::none || valueSpan == zc::none ||
            leftSpan == zc::none || rightSpan == zc::none || nodeTypeIndex == zc::none ||
            leftTypeIndex == zc::none || rightTypeIndex == zc::none || callIndex == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t nodeTypeSlot = 0;
        size_t leftTypeSlot = 0;
        size_t rightTypeSlot = 0;
        size_t callSlot = 0;
        ZC_IF_SOME(value, nodeTypeIndex) { nodeTypeSlot = value; }
        ZC_IF_SOME(value, leftTypeIndex) { leftTypeSlot = value; }
        ZC_IF_SOME(value, rightTypeIndex) { rightTypeSlot = value; }
        ZC_IF_SOME(value, callIndex) { callSlot = value; }
        const auto resultType = facts.nodeTypes().entries()[nodeTypeSlot].value;
        const auto operandType = facts.nodeTypes().entries()[leftTypeSlot].value;
        const auto rightType = facts.nodeTypes().entries()[rightTypeSlot].value;
        const auto& callFact = facts.calls().entries()[callSlot].value;
        const auto& call = callFact.invocation;
        const auto& selected = call.selected.variant();
        // A comparison return produces a bool result; an arithmetic/bitwise
        // return produces the operand type (resultType == operandType). Accept
        // either family for the supported operators.
        const bool operationSupported =
            selected.is<checker::checked::PrimitiveCallable>() &&
            (isScalarComparisonOperation(
                 selected.get<checker::checked::PrimitiveCallable>().operation) ||
             (isScalarArithmeticOperation(
                  selected.get<checker::checked::PrimitiveCallable>().operation) &&
              resultType == operandType));
        if (resultType != function.resultType || operandType != rightType ||
            callFact.node != source.value || !operationSupported ||
            call.calleeType != operandType || call.receiver != zc::none ||
            call.receiverMode != zc::none || call.receiverAdjustment != zc::none ||
            call.arguments.size() != 2 || call.arguments[0].sourceNode != source.comparisonLeft ||
            call.arguments[0].sourceType != operandType ||
            call.arguments[1].sourceNode != source.comparisonRight ||
            call.arguments[1].sourceType != operandType || call.successType != resultType ||
            call.resultType != resultType || call.substitutions != zc::none ||
            call.witnesses != zc::none || call.raises != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        // Verify one comparison operand at its fixed node id: a parameter operand
        // resolves to a parameter reference; a literal operand resolves to a
        // scalar-literal expression matching its checked literal fact.
        auto verifyOperand = [&](bool isLiteral, ast::NodeId operandSourceNode, HirNodeId operandId,
                                 const identity::SourceSpan& operandSpan) -> bool {
          if (!isLiteral) {
            auto parameter = resolvedCallableParameter(bound.bindings(), operandSourceNode);
            if (parameter == zc::none) return false;
            identity::CallableParameterId handle;
            ZC_IF_SOME(value, parameter) { handle = value; }
            auto authority = registries.callableParameter(handle);
            if (authority == zc::none) return false;
            zc::Maybe<const HirParameterReferenceExpression&> reference;
            for (const auto& candidateReference : candidate.impl->parameterReferences) {
              if (candidateReference.node != operandId) continue;
              if (reference != zc::none) return false;
              reference = candidateReference;
            }
            bool ok = false;
            ZC_IF_SOME(referenceValue, reference) {
              ZC_IF_SOME(authorityValue, authority) {
                ok = referenceValue.parameter == authorityValue.key() &&
                     referenceValue.type == operandType &&
                     referenceValue.category == HirValueCategory::Place &&
                     sameSpan(referenceValue.sourceSpan, operandSpan);
              }
            }
            return ok;
          }
          auto literalIndex = factIndex(facts.literals(), operandSourceNode);
          if (literalIndex == zc::none) return false;
          size_t literalSlot = 0;
          ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
          const auto& literalFact = facts.literals().entries()[literalSlot].value;
          zc::Maybe<const HirScalarLiteralExpression&> expressionValue;
          for (const auto& expression : candidate.impl->expressions) {
            if (expression.node != operandId) continue;
            if (expressionValue != zc::none) return false;
            expressionValue = expression;
          }
          bool ok = false;
          ZC_IF_SOME(expression, expressionValue) {
            ok = expression.type == operandType && expression.category == HirValueCategory::Value &&
                 sameConstant(expression.value, literalFact.literal, module, registries,
                              semanticTypes) &&
                 sameSpan(expression.sourceSpan, operandSpan);
          }
          return ok;
        };
        if (!verifyOperand(source.comparisonLeftIsLiteral, source.comparisonLeft,
                           hirId(expectedFunction + 2), ZC_ASSERT_NONNULL(leftSpan)) ||
            !verifyOperand(source.comparisonRightIsLiteral, source.comparisonRight,
                           hirId(expectedFunction + 3), ZC_ASSERT_NONNULL(rightSpan))) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        zc::Maybe<const HirPrimitiveBinaryExpression&> comparisonValue;
        for (const auto& comparison : candidate.impl->primitiveBinaryOperations) {
          if (comparison.node != hirId(expectedFunction + 4)) continue;
          if (comparisonValue != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          comparisonValue = comparison;
        }
        if (comparisonValue == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        bool skeletonOk = false;
        ZC_IF_SOME(comparison, comparisonValue) {
          skeletonOk = function.node == hirId(expectedFunction) &&
                       block.node == hirId(expectedFunction + 1) && function.body == block.node &&
                       block.statements.size() == 1 &&
                       block.statements[0] == returnStatement.node &&
                       returnStatement.node == hirId(expectedFunction + 5) &&
                       returnStatement.value == comparison.node &&
                       returnStatement.resultType == function.resultType &&
                       comparison.left == hirId(expectedFunction + 2) &&
                       comparison.right == hirId(expectedFunction + 3) &&
                       comparison.operandType == operandType && comparison.type == resultType &&
                       comparison.category == HirValueCategory::Value &&
                       comparison.operation ==
                           selected.get<checker::checked::PrimitiveCallable>().operation &&
                       sameSpan(block.sourceSpan, ZC_ASSERT_NONNULL(bodySpan)) &&
                       sameSpan(returnStatement.sourceSpan, ZC_ASSERT_NONNULL(returnSpan)) &&
                       sameSpan(comparison.sourceSpan, ZC_ASSERT_NONNULL(valueSpan)) &&
                       typeExists(function.resultType, semanticTypes) &&
                       typeExists(operandType, semanticTypes);
        }
        if (!skeletonOk) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        nextFunction += 6;
        continue;
      }
    }
    // Loop shape: a `while` with a bool parameter condition and empty body,
    // followed by a scalar return.
    {
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
      if (source.isLoop) {
        auto signaturePosition =
            signatureIndex(signatures.definitions.asPtr(), function.definition);
        auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), function.definition);
        if (signaturePosition == zc::none || rootPosition == zc::none) {
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
            function.parameters.size() != callable.parameters.size()) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        for (size_t parameterIndex = 0; parameterIndex < function.parameters.size();
             ++parameterIndex) {
          const auto& parameter = function.parameters[parameterIndex];
          const auto& sourceParameter = callable.parameters[parameterIndex];
          if (parameter.key != sourceParameter.parameter ||
              parameter.type != sourceParameter.type || sourceParameter.hasDefault ||
              !typeExists(parameter.type, semanticTypes)) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
        }
        auto bodySpan = bound.parsedModule().spanFor(tree.node(source.body).range);
        auto returnSpan = bound.parsedModule().spanFor(tree.node(source.returnStatement).range);
        auto conditionSpan = bound.parsedModule().spanFor(tree.node(source.loopCondition).range);
        auto loopSpan = bound.parsedModule().spanFor(tree.node(source.loopStatement).range);
        auto valueSpan = bound.parsedModule().spanFor(tree.node(source.value).range);
        if (bodySpan == zc::none || returnSpan == zc::none || conditionSpan == zc::none ||
            loopSpan == zc::none || valueSpan == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        auto conditionTypeIndex = factIndex(facts.nodeTypes(), source.loopCondition);
        auto returnTypeIndex = factIndex(facts.nodeTypes(), source.value);
        auto returnLiteralIndex = factIndex(facts.literals(), source.value);
        if (conditionTypeIndex == zc::none || returnTypeIndex == zc::none ||
            returnLiteralIndex == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t conditionTypeSlot = 0;
        size_t returnTypeSlot = 0;
        size_t returnLiteralSlot = 0;
        ZC_IF_SOME(value, conditionTypeIndex) { conditionTypeSlot = value; }
        ZC_IF_SOME(value, returnTypeIndex) { returnTypeSlot = value; }
        ZC_IF_SOME(value, returnLiteralIndex) { returnLiteralSlot = value; }
        const auto conditionType = facts.nodeTypes().entries()[conditionTypeSlot].value;
        const auto returnType = facts.nodeTypes().entries()[returnTypeSlot].value;
        const auto& returnLiteral = facts.literals().entries()[returnLiteralSlot].value;
        if (returnType != function.resultType) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        auto conditionParameter = resolvedCallableParameter(bound.bindings(), source.loopCondition);
        if (conditionParameter == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        identity::CallableParameterId conditionHandle;
        ZC_IF_SOME(value, conditionParameter) { conditionHandle = value; }
        auto conditionAuthority = registries.callableParameter(conditionHandle);
        if (conditionAuthority == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        zc::Maybe<const HirLoopStatement&> loopValue;
        for (const auto& candidateLoop : candidate.impl->loops) {
          if (candidateLoop.node != hirId(expectedFunction + 4)) continue;
          if (loopValue != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          loopValue = candidateLoop;
        }
        zc::Maybe<const HirParameterReferenceExpression&> conditionReference;
        for (const auto& reference : candidate.impl->parameterReferences) {
          if (reference.node != hirId(expectedFunction + 2)) continue;
          if (conditionReference != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          conditionReference = reference;
        }
        zc::Maybe<const HirScalarLiteralExpression&> returnValue;
        for (const auto& expression : candidate.impl->expressions) {
          if (expression.node != hirId(expectedFunction + 3)) continue;
          if (returnValue != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::AdditionalFact, module,
                                                registries, index + 1);
          }
          returnValue = expression;
        }
        if (loopValue == zc::none || conditionReference == zc::none || returnValue == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        ZC_IF_SOME(loop, loopValue) {
          ZC_IF_SOME(conditionRef, conditionReference) {
            ZC_IF_SOME(returnLit, returnValue) {
              ZC_IF_SOME(authority, conditionAuthority) {
                if (function.node != hirId(expectedFunction) ||
                    block.node != hirId(expectedFunction + 1) || function.body != block.node ||
                    block.statements.size() != 2 || block.statements[0] != loop.node ||
                    block.statements[1] != returnStatement.node ||
                    returnStatement.node != hirId(expectedFunction + 5) ||
                    returnStatement.value != returnLit.node ||
                    returnStatement.resultType != function.resultType ||
                    loop.condition != conditionRef.node || loop.type != conditionType ||
                    loop.category != HirValueCategory::Place ||
                    conditionRef.parameter != authority.key() ||
                    conditionRef.type != conditionType ||
                    conditionRef.category != HirValueCategory::Place ||
                    returnLit.type != returnType || returnLit.category != HirValueCategory::Value ||
                    !sameConstant(returnLit.value, returnLiteral.literal, module, registries,
                                  semanticTypes) ||
                    !sameSpan(block.sourceSpan, ZC_ASSERT_NONNULL(bodySpan)) ||
                    !sameSpan(returnStatement.sourceSpan, ZC_ASSERT_NONNULL(returnSpan)) ||
                    !sameSpan(loop.sourceSpan, ZC_ASSERT_NONNULL(loopSpan)) ||
                    !sameSpan(conditionRef.sourceSpan, ZC_ASSERT_NONNULL(conditionSpan)) ||
                    !sameSpan(returnLit.sourceSpan, ZC_ASSERT_NONNULL(valueSpan)) ||
                    !typeExists(function.resultType, semanticTypes) ||
                    !typeExists(conditionType, semanticTypes)) {
                  return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      registries, index + 1);
                }
              }
            }
          }
        }
        nextFunction += 6;
        continue;
      }
    }
    // number of extra HIR nodes the unsafe block contributes (0 when absent, 1
    // when present and valid), or zc::none when present but invalid.
    auto verifyUnsafeBlock = [&](const FunctionReturnShape& source, uint32_t baseIncrement,
                                 HirNodeId bodyNode) -> zc::Maybe<uint32_t> {
      if (source.unsafeBlock == zc::none) return zc::Maybe<uint32_t>(0u);
      const auto expectedUnsafeBlock = hirId(expectedFunction + baseIncrement);
      if (function.unsafeBlock != expectedUnsafeBlock) return zc::none;
      zc::Maybe<const HirUnsafeBlockExpression&> unsafeBlock;
      for (const auto& candidateBlock : candidate.impl->unsafeBlocks) {
        if (candidateBlock.node != expectedUnsafeBlock) continue;
        if (unsafeBlock != zc::none) return zc::none;
        unsafeBlock = candidateBlock;
      }
      if (unsafeBlock == zc::none) return zc::none;
      ZC_IF_SOME(block, unsafeBlock) {
        auto sourceUnsafeSpan = bound.parsedModule().spanFor(
            bound.tree().node(ZC_ASSERT_NONNULL(source.unsafeBlock)).range);
        if (block.body != bodyNode || block.type != function.resultType ||
            sourceUnsafeSpan == zc::none ||
            !sameSpan(block.sourceSpan, ZC_ASSERT_NONNULL(sourceUnsafeSpan))) {
          return zc::none;
        }
      }
      return zc::Maybe<uint32_t>(1u);
    };
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
    zc::Maybe<const HirParameterReferenceExpression&> writeParameter;
    if (hasLocalWrite) {
      const auto expectedValue = hirId(expectedFunction + (localHasInitializer ? 5 : 4));
      for (const auto& expression : candidate.impl->expressions) {
        if (expression.node != expectedValue) continue;
        if (writeLiteral != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::AdditionalFact, module, registries,
                                              index + 1);
        }
        writeLiteral = expression;
      }
      for (const auto& reference : candidate.impl->parameterReferences) {
        if (reference.node != expectedValue) continue;
        if (writeParameter != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::AdditionalFact, module, registries,
                                              index + 1);
        }
        writeParameter = reference;
      }
    }
    // The first write's value is a scalar literal or a parameter reference,
    // never both. Downstream branches consume whichever is populated.
    const bool hasFirstWriteValue = writeLiteral != zc::none || writeParameter != zc::none;
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
        bool constantMatches = false;
        ZC_IF_SOME(value, hirArgument.value) {
          constantMatches = sameConstant(value, literal.literal, module, registries, semanticTypes);
        }
        if (checkedArgument.sourceNode != argument || checkedArgument.sourceType != argumentType ||
            checkedArgument.parameterType != argumentType ||
            checkedArgument.adjustment != zc::none || literal.node != argument ||
            literal.type != argumentType || hirArgument.type != argumentType ||
            hirArgument.value == zc::none || hirArgument.parameter != zc::none ||
            !constantMatches || !sameSpan(literal.sourceSpan, ZC_ASSERT_NONNULL(argumentSpan)) ||
            !sameSpan(hirArgument.sourceSpan, ZC_ASSERT_NONNULL(argumentSpan))) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
      }
      const auto baseIncrement = static_cast<uint32_t>(7);
      auto unsafeExtra = verifyUnsafeBlock(source, baseIncrement, receiverCallNode);
      if (unsafeExtra == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      nextFunction += baseIncrement + ZC_ASSERT_NONNULL(unsafeExtra);
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
        const auto baseIncrement = static_cast<uint32_t>(initializesField ? 7 : 5);
        auto unsafeExtra = verifyUnsafeBlock(source, baseIncrement, valueNode);
        if (unsafeExtra == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        nextFunction += baseIncrement + ZC_ASSERT_NONNULL(unsafeExtra);
      } else {
        const auto baseIncrement = static_cast<uint32_t>(5);
        auto unsafeExtra = verifyUnsafeBlock(source, baseIncrement, valueNode);
        if (unsafeExtra == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        nextFunction += baseIncrement + ZC_ASSERT_NONNULL(unsafeExtra);
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
      const auto baseIncrement = static_cast<uint32_t>(6);
      auto unsafeExtra = verifyUnsafeBlock(source, baseIncrement, valueNode);
      if (unsafeExtra == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      nextFunction += baseIncrement + ZC_ASSERT_NONNULL(unsafeExtra);
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
      const auto baseIncrement = static_cast<uint32_t>(6 + functionLocalWriteCount * 2);
      auto unsafeExtra = verifyUnsafeBlock(source, baseIncrement, valueNode);
      if (unsafeExtra == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      nextFunction += baseIncrement + ZC_ASSERT_NONNULL(unsafeExtra);
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
        (hasLocalWrite && (returnsLocal == false || !hasFirstWriteValue ||
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
      // A reference write value has no literal fact; a literal write value does.
      const bool sourceReferenceValue =
          tree.contains(sourceWriteValue) &&
          tree.node(sourceWriteValue).kind == ast::SyntaxKind::IdentExpr;
      auto valueLiteral =
          sourceReferenceValue ? zc::none : factIndex(facts.literals(), sourceWriteValue);
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
          (!sourceReferenceValue && valueLiteral == zc::none) || targetBinding == zc::none ||
          returnBinding == zc::none || targetBinding != returnBinding) {
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
      zc::Maybe<const HirParameterReferenceExpression&> parameter;
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
      for (const auto& reference : candidate.impl->parameterReferences) {
        if (reference.node != expectedValue) continue;
        if (parameter != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::AdditionalFact, module, registries,
                                              index + 1);
        }
        parameter = reference;
      }
      // Exactly one materialized value must match the write's kind: a literal
      // write materializes a scalar literal, a reference write a parameter
      // reference. The two are mutually exclusive.
      if (write == zc::none || localBinding == zc::none ||
          (sourceReferenceValue ? (parameter == zc::none || literal != zc::none)
                                : (literal == zc::none || parameter != zc::none))) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::MissingRequiredFact, module,
                                            registries, index + 1);
      }
      ZC_IF_SOME(writeValue, write) {
        ZC_IF_SOME(local, localBinding) {
          const auto& assignmentFact = facts.nodeTypes().entries()[assignmentSlot].value;
          const auto& targetFact = facts.nodeTypes().entries()[targetSlot].value;
          const auto& valueFact = facts.nodeTypes().entries()[valueSlot].value;
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
              writeValue.value != expectedValue || assignmentFact != writeValue.type ||
              targetFact != writeValue.type || valueFact != writeValue.type ||
              writeValue.kind != expectedKind ||
              !sameSpan(writeValue.sourceSpan, ZC_ASSERT_NONNULL(sourceWriteSpan)) ||
              !sameSpan(writeValue.valueSpan, ZC_ASSERT_NONNULL(sourceValueSpan))) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          if (sourceReferenceValue) {
            // A reference write value is a parameter reference: it carries the
            // write's type, is a place category, and shares the value node id.
            ZC_IF_SOME(parameterValue, parameter) {
              if (parameterValue.type != writeValue.type ||
                  parameterValue.category != HirValueCategory::Place ||
                  !sameSpan(parameterValue.sourceSpan, ZC_ASSERT_NONNULL(sourceValueSpan))) {
                return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                    ir::IrFailureKind::InvalidFact, module,
                                                    registries, index + 1);
              }
            }
          } else {
            const auto& literalFact = facts.literals().entries()[literalSlot].value;
            ZC_IF_SOME(literalValue, literal) {
              if (literalValue.type != writeValue.type ||
                  literalValue.category != HirValueCategory::Value ||
                  literalFact.type != writeValue.type ||
                  !sameConstant(literalValue.value, literalFact.literal, module, registries,
                                semanticTypes) ||
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
    }
    if (localFieldProjection != zc::none && !localHasInitializer && hasLocalWrite) {
      const auto baseIncrement = static_cast<uint32_t>(5 + functionLocalWriteCount * 2);
      auto unsafeExtra = verifyUnsafeBlock(source, baseIncrement, valueNode);
      if (unsafeExtra == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      nextFunction += baseIncrement + ZC_ASSERT_NONNULL(unsafeExtra);
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
      const auto baseIncrement = static_cast<uint32_t>(6);
      auto unsafeExtra = verifyUnsafeBlock(source, baseIncrement, valueNode);
      if (unsafeExtra == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      nextFunction += baseIncrement + ZC_ASSERT_NONNULL(unsafeExtra);
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
      const auto baseIncrement = static_cast<uint32_t>(5);
      auto unsafeExtra = verifyUnsafeBlock(source, baseIncrement, valueNode);
      if (unsafeExtra == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      nextFunction += baseIncrement + ZC_ASSERT_NONNULL(unsafeExtra);
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
      const auto baseIncrement = static_cast<uint32_t>(5 + functionLocalWriteCount * 2);
      auto unsafeExtra = verifyUnsafeBlock(source, baseIncrement, valueNode);
      if (unsafeExtra == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      nextFunction += baseIncrement + ZC_ASSERT_NONNULL(unsafeExtra);
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
      const auto baseIncrement = static_cast<uint32_t>(6);
      auto unsafeExtra = verifyUnsafeBlock(source, baseIncrement, valueNode);
      if (unsafeExtra == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      nextFunction += baseIncrement + ZC_ASSERT_NONNULL(unsafeExtra);
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
      const auto& sourceNode = tree.node(sourceValueNode);
      if (sourceNode.kind != ast::SyntaxKind::UnaryExpression ||
          (static_cast<ast::UnaryOperatorKind>(
               sourceNode.payload.words[ast::kUnaryExpressionOpWord]) !=
               ast::UnaryOperatorKind::Ref &&
           static_cast<ast::UnaryOperatorKind>(
               sourceNode.payload.words[ast::kUnaryExpressionOpWord]) !=
               ast::UnaryOperatorKind::RefMut) ||
          !sameSpan(reborrow.sourceSpan, ZC_ASSERT_NONNULL(sourceValueSpan))) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      const ast::NodeId dereference(sourceNode.payload.words[ast::kUnaryExpressionOperandWord]);
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
      const auto operation = static_cast<ast::UnaryOperatorKind>(
          sourceNode.payload.words[ast::kUnaryExpressionOpWord]);
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
      const auto baseIncrement = 4;
      auto unsafeExtra = verifyUnsafeBlock(source, baseIncrement, valueNode);
      if (unsafeExtra == zc::none) {
        return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                            ir::IrFailureKind::InvalidFact, module, registries,
                                            index + 1);
      }
      nextFunction += baseIncrement + ZC_ASSERT_NONNULL(unsafeExtra);
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
      if (source.unsafeBlock != zc::none && !returnsLocal) {
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
        const auto baseIncrement =
            returnsLocal
                ? static_cast<uint32_t>((localHasInitializer ? 6 : 5) + functionLocalWriteCount * 2)
                : 4;
        auto unsafeExtra = verifyUnsafeBlock(source, baseIncrement, valueNode);
        if (unsafeExtra == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        nextFunction += baseIncrement + ZC_ASSERT_NONNULL(unsafeExtra);
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
        auto argumentKey = checkedNodeKey(tree, bound.parsedModule(), argument);
        auto argumentSpan = bound.parsedModule().spanFor(tree.node(argument).range);
        const bool isLiteralArgument =
            tree.contains(argument) && isScalarLiteral(tree.node(argument).kind);
        const bool isParameterArgument =
            tree.contains(argument) && tree.node(argument).kind == ast::SyntaxKind::IdentExpr;
        if ((!isLiteralArgument && !isParameterArgument) || argumentTypeIndex == zc::none ||
            argumentKey == zc::none || argumentSpan == zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::MissingRequiredFact, module,
                                              registries, index + 1);
        }
        size_t argumentTypeSlot = 0;
        ZC_IF_SOME(value, argumentTypeIndex) { argumentTypeSlot = value; }
        const auto& checkedArgument = invocation.arguments[argumentIndex];
        const auto& dispatchArgument = dispatch.fact.arguments[argumentIndex];
        const auto& hirArgument = call.arguments[argumentIndex];
        const auto argumentType = facts.nodeTypes().entries()[argumentTypeSlot].value;
        if (checkedArgument.sourceNode != argument || checkedArgument.sourceType != argumentType ||
            checkedArgument.parameterType != argumentType ||
            checkedArgument.adjustment != zc::none ||
            !sameNodeKey(dispatchArgument.sourceNode, ZC_ASSERT_NONNULL(argumentKey)) ||
            dispatchArgument.sourceType != argumentType ||
            dispatchArgument.parameterType != argumentType ||
            dispatchArgument.adjustment != zc::none || hirArgument.type != argumentType ||
            !sameSpan(hirArgument.sourceSpan, ZC_ASSERT_NONNULL(argumentSpan))) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        if (isLiteralArgument) {
          auto literalIndex = factIndex(facts.literals(), argument);
          if (literalIndex == zc::none || hirArgument.value == zc::none ||
              hirArgument.parameter != zc::none) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          size_t literalSlot = 0;
          ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
          const auto& literal = facts.literals().entries()[literalSlot].value;
          bool constantMatches = false;
          ZC_IF_SOME(value, hirArgument.value) {
            constantMatches =
                sameConstant(value, literal.literal, module, registries, semanticTypes);
          }
          if (literal.node != argument || literal.type != argumentType || !constantMatches ||
              !sameSpan(literal.sourceSpan, ZC_ASSERT_NONNULL(argumentSpan))) {
            return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                                ir::IrFailureKind::InvalidFact, module, registries,
                                                index + 1);
          }
          continue;
        }
        // Parameter-reference argument: the HIR must carry the parameter key that
        // the argument node resolves to and no constant.
        if (hirArgument.parameter == zc::none || hirArgument.value != zc::none) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
        auto parameter = resolvedCallableParameter(bound.bindings(), argument);
        bool parameterMatches = false;
        ZC_IF_SOME(handle, parameter) {
          auto authority = registries.callableParameter(handle);
          ZC_IF_SOME(entry, authority) {
            ZC_IF_SOME(key, hirArgument.parameter) { parameterMatches = key == entry.key(); }
          }
        }
        if (!parameterMatches) {
          return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                              ir::IrFailureKind::InvalidFact, module, registries,
                                              index + 1);
        }
      }
    }
    const auto baseIncrement =
        returnsLocal
            ? static_cast<uint32_t>((localHasInitializer ? 6 : 5) + functionLocalWriteCount * 2)
            : 4;
    auto unsafeExtra = verifyUnsafeBlock(source, baseIncrement, valueNode);
    if (unsafeExtra == zc::none) {
      return rejectHir<VerifiedHirModule>(ir::IrFailurePhase::HirVerification,
                                          ir::IrFailureKind::InvalidFact, module, registries,
                                          index + 1);
    }
    nextFunction += baseIncrement + ZC_ASSERT_NONNULL(unsafeExtra);
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
      zc::mv(candidate.impl->receiverCalls), zc::mv(candidate.impl->unsafeBlocks),
      zc::mv(candidate.impl->primitiveBinaryOperations), zc::mv(candidate.impl->conditionals),
      zc::mv(candidate.impl->loops));
  return ir::IrOperationResult<VerifiedHirModule>::verified(VerifiedHirModule(zc::mv(impl)));
}

}  // namespace zomlang::compiler::hir
