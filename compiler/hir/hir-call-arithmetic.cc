// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/hir/hir-call-arithmetic.h"

namespace zomlang::compiler::hir {
namespace detail {
namespace {

/// \brief Unique pool lookup returning none on zero or duplicate matches.
///
/// Mirrors the MIR builder's own record resolution, which returns none for a
/// duplicated record rather than resolving the first one. A duplicated record
/// must never be accepted by the predicate and then rejected (or silently
/// mis-resolved) by the lowering.
template <typename Record>
zc::Maybe<const Record&> uniqueRecordFor(zc::ArrayPtr<const Record> records, HirNodeId node) {
  zc::Maybe<const Record&> result;
  for (const auto& record : records) {
    if (record.node != node) continue;
    if (result != zc::none) return zc::none;
    result = record;
  }
  return result;
}

/// \brief True when the type is one of the integer primitives the LIR integer
/// carrier can express.
///
/// This is deliberately the LIR-admissible set, not "every integer": the LIR
/// width lookup has no arm for Isize/Usize, so admitting those here would admit
/// a body the backend then rejects.
bool isLirIntegerType(const type::SemanticTypeStore& semanticTypes, identity::SemanticTypeId type) {
  auto lookup = semanticTypes.get(type);
  if (!lookup.is<type::SemanticTypeLookup>()) return false;
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  if (!data.is<type::semantic::PrimitiveTypeData>()) return false;
  switch (data.get<type::semantic::PrimitiveTypeData>().kind) {
    case type::semantic::PrimitiveKind::I8:
    case type::semantic::PrimitiveKind::I16:
    case type::semantic::PrimitiveKind::I32:
    case type::semantic::PrimitiveKind::I64:
    case type::semantic::PrimitiveKind::U8:
    case type::semantic::PrimitiveKind::U16:
    case type::semantic::PrimitiveKind::U32:
    case type::semantic::PrimitiveKind::U64:
      return true;
    default:
      return false;
  }
}

/// \brief True when the operation maps to a LIR arithmetic operation.
///
/// Pow is excluded: the MIR operator mapping has an arm for it but the LIR
/// mapping does not, so admitting it here would admit a body the backend then
/// rejects. Every comparison operator is excluded for the same reason: this
/// body computes the result local with an arithmetic rvalue only.
bool isLirArithmeticOperation(checker::PrimitiveOperation operation) noexcept {
  switch (operation) {
    case checker::PrimitiveOperation::Add:
    case checker::PrimitiveOperation::Sub:
    case checker::PrimitiveOperation::Mul:
    case checker::PrimitiveOperation::Div:
    case checker::PrimitiveOperation::Rem:
    case checker::PrimitiveOperation::Shl:
    case checker::PrimitiveOperation::Shr:
    case checker::PrimitiveOperation::UShr:
    case checker::PrimitiveOperation::BitAnd:
    case checker::PrimitiveOperation::BitOr:
    case checker::PrimitiveOperation::BitXor:
    case checker::PrimitiveOperation::LogicalAnd:
    case checker::PrimitiveOperation::LogicalOr:
      return true;
    default:
      return false;
  }
}

/// \brief Highest local ordinal an arithmetic operand may name.
///
/// The computing binding is the third (zero-based index 2), and both leaf
/// builders admit a reference to a local at or before that index, so only
/// ordinals 1 (the call local) and 2 (the constant local) qualify. A reference
/// to the result local itself is excluded: no builder emits it.
constexpr uint32_t kHighestAdmittedOperandOrdinal = 2;

/// \brief True when one arithmetic operand is a leaf both the MIR strategy and
/// the LIR lowering emit.
///
/// The MIR leaf builder admits a same-typed scalar constant or a bare reference
/// to a local at or before the computing binding; the LIR arithmetic leaf
/// admits a constant or a place-use of exactly the call local and the constant
/// local. Anything else -- a nested binary, a field projection, a parameter --
/// returns none. The declaration has no parameters, so no parameter leaf can
/// occur.
bool isAdmittedArithmeticOperand(const CallArithmeticPools& pools,
                                 identity::SemanticTypeId operandType, HirNodeId operand) {
  auto literal = uniqueRecordFor(pools.expressions, operand);
  if (literal != zc::none) {
    const auto& value = ZC_ASSERT_NONNULL(literal);
    return value.type == operandType && value.category == HirValueCategory::Value;
  }
  auto reference = uniqueRecordFor(pools.localReferences, operand);
  if (reference == zc::none) return false;
  const auto& value = ZC_ASSERT_NONNULL(reference);
  return value.type == operandType && value.category == HirValueCategory::Place &&
         value.local.ordinal() >= 1 && value.local.ordinal() <= kHighestAdmittedOperandOrdinal;
}

}  // namespace

/// \brief True when the module is the caller/callee pair this body lowers in.
///
/// The lowering that emits this body is `lowerCallThenLocalArithmeticModule`, a
/// *pair* lowering dispatched only for a two-function module. A module with a
/// third function reaches the standalone ladder instead, which has no arm for a
/// two-block call body, and a callee outside the constant-return shape is
/// rejected by the pair lowering's own callee gate. Deciding the body alone
/// would therefore admit modules the next stage cannot lower, so the module
/// half is part of the same predicate: a body is admitted only together with a
/// module this lowering actually receives.
///
/// The checks mirror `lowerCallThenLocalArithmeticModule`'s callee gate: an
/// ordinary parameter-free module function returning the same type, whose body
/// is one block holding a single return of an integer literal.
bool moduleMatchesCallArithmetic(const CallArithmeticPools& pools,
                                 const HirFunctionDeclaration& caller,
                                 const HirDirectCallExpression& call) noexcept {
  if (pools.functions.size() != 2) return false;
  const HirFunctionDeclaration* callee = nullptr;
  for (const auto& candidate : pools.functions) {
    if (candidate.definition == caller.definition) continue;
    if (callee != nullptr) return false;
    callee = &candidate;
  }
  if (callee == nullptr || callee->definition != call.callee) return false;
  // The pair lowering emits the callee as a function, not a method, with no
  // parameters and the caller's own result type.
  if (callee->receiver != zc::none || callee->parameters.size() != 0 ||
      callee->unsafeBlock != zc::none || callee->writeUnsafeBlock != zc::none) {
    return false;
  }
  if (callee->resultType != caller.resultType) return false;
  // The callee body is exactly one block with exactly one statement: the
  // return of the integer constant. Any local, write, or further statement
  // puts the callee outside the constant-return shape.
  const HirBlockStatement* calleeBlock = nullptr;
  for (const auto& candidate : pools.blocks) {
    if (candidate.node != callee->body) continue;
    if (calleeBlock != nullptr) return false;
    calleeBlock = &candidate;
  }
  if (calleeBlock == nullptr || calleeBlock->statements.size() != 1) return false;
  auto calleeReturn = uniqueRecordFor(pools.returns, calleeBlock->statements[0]);
  if (calleeReturn == zc::none) return false;
  const auto& returned = ZC_ASSERT_NONNULL(calleeReturn);
  if (returned.resultType != callee->resultType) return false;
  auto literal = uniqueRecordFor(pools.expressions, returned.value);
  if (literal == zc::none) return false;
  const auto& value = ZC_ASSERT_NONNULL(literal);
  return value.type == callee->resultType && value.category == HirValueCategory::Value;
}

CallArithmeticPools CallArithmeticPools::of(const VerifiedHirModule& module) noexcept {
  return CallArithmeticPools{
      module.functions(), module.blocks(),
      module.returns(),   module.expressions(),
      module.locals(),    module.localReferences(),
      module.calls(),     module.primitiveBinaryOperations(),
  };
}

zc::Maybe<CallArithmeticParts> matchCallThenLocalArithmetic(
    const CallArithmeticPools& pools, const type::SemanticTypeStore& semanticTypes,
    const HirFunctionDeclaration& declaration, const HirBlockStatement& block) {
  // The declaration half: an ordinary, safe, parameter-free function whose body
  // is exactly the four statements below.
  if (declaration.receiver != zc::none || declaration.unsafeBlock != zc::none ||
      declaration.writeUnsafeBlock != zc::none || declaration.parameters.size() != 0 ||
      block.statements.size() != 4) {
    return zc::none;
  }
  // The three leading statements are the bindings, in order; the fourth is the
  // return. Every resolution must be unique.
  auto callLocal = uniqueRecordFor(pools.locals, block.statements[0]);
  auto constantLocal = uniqueRecordFor(pools.locals, block.statements[1]);
  auto resultLocal = uniqueRecordFor(pools.locals, block.statements[2]);
  auto returnStatement = uniqueRecordFor(pools.returns, block.statements[3]);
  if (callLocal == zc::none || constantLocal == zc::none || resultLocal == zc::none ||
      returnStatement == zc::none) {
    return zc::none;
  }
  const auto& callBinding = ZC_ASSERT_NONNULL(callLocal);
  const auto& constantBinding = ZC_ASSERT_NONNULL(constantLocal);
  const auto& resultBinding = ZC_ASSERT_NONNULL(resultLocal);
  const auto& returnValue = ZC_ASSERT_NONNULL(returnStatement);
  // The lowering maps the three declared locals to MIR ordinals 1..3 one to
  // one, so any other ordinal means the body is not this shape.
  if (callBinding.local.ordinal() != 1 || constantBinding.local.ordinal() != 2 ||
      resultBinding.local.ordinal() != 3) {
    return zc::none;
  }
  // Every binding is initialized and none carries an erasure: the lowering has
  // no erasure rvalue in this shape.
  if (callBinding.initializer == zc::none || constantBinding.initializer == zc::none ||
      resultBinding.initializer == zc::none || callBinding.erasure != zc::none ||
      constantBinding.erasure != zc::none || resultBinding.erasure != zc::none) {
    return zc::none;
  }
  HirNodeId callInitializer{};
  HirNodeId constantInitializer{};
  HirNodeId resultInitializer{};
  ZC_IF_SOME(value, callBinding.initializer) { callInitializer = value; }
  ZC_IF_SOME(value, constantBinding.initializer) { constantInitializer = value; }
  ZC_IF_SOME(value, resultBinding.initializer) { resultInitializer = value; }
  // The call binding's initializer node is the zero-argument direct call
  // itself; a zero-argument call allocates no argument nodes.
  auto call = uniqueRecordFor(pools.calls, callInitializer);
  auto constant = uniqueRecordFor(pools.expressions, constantInitializer);
  auto binary = uniqueRecordFor(pools.primitiveBinaryOperations, resultInitializer);
  auto returnReference = uniqueRecordFor(pools.localReferences, returnValue.value);
  if (call == zc::none || constant == zc::none || binary == zc::none ||
      returnReference == zc::none) {
    return zc::none;
  }
  const auto& callValue = ZC_ASSERT_NONNULL(call);
  const auto& constantValue = ZC_ASSERT_NONNULL(constant);
  const auto& binaryValue = ZC_ASSERT_NONNULL(binary);
  const auto& referenceValue = ZC_ASSERT_NONNULL(returnReference);
  // The four records describe exactly this binding list, and every one carries
  // the single shared type the declaration returns.
  if (callValue.node != callInitializer || callValue.arguments.size() != 0 ||
      callValue.resultType != callBinding.type || constantValue.node != constantInitializer ||
      constantValue.type != constantBinding.type ||
      constantValue.category != HirValueCategory::Value || binaryValue.node != resultInitializer ||
      binaryValue.type != resultBinding.type || binaryValue.operandType != resultBinding.type ||
      binaryValue.category != HirValueCategory::Value ||
      referenceValue.local != resultBinding.local || referenceValue.type != resultBinding.type ||
      referenceValue.category != HirValueCategory::Place ||
      callBinding.type != constantBinding.type || callBinding.type != resultBinding.type ||
      callBinding.type != declaration.resultType) {
    return zc::none;
  }
  // The operation and the carrier must both be ones the LIR lowering maps, and
  // both operands must be leaves the lowering emits.
  if (!isLirArithmeticOperation(binaryValue.operation) ||
      !isLirIntegerType(semanticTypes, resultBinding.type) ||
      !isAdmittedArithmeticOperand(pools, binaryValue.operandType, binaryValue.left) ||
      !isAdmittedArithmeticOperand(pools, binaryValue.operandType, binaryValue.right)) {
    return zc::none;
  }
  // The body is only ever emitted by the caller/callee pair lowering, so the
  // module must be the shape that lowering receives. Without this half a
  // module the pair lowering never sees would pass HIR and reach a rejection
  // message instead of a capability drain.
  if (!moduleMatchesCallArithmetic(pools, declaration, callValue)) { return zc::none; }
  return CallArithmeticParts{callBinding,   constantBinding, resultBinding, callValue,
                             constantValue, binaryValue,     returnValue,   binaryValue.operation};
}

}  // namespace detail
}  // namespace zomlang::compiler::hir
