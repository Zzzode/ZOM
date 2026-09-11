// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/hir/hir-module.h"
#include "compiler/hir/hir-shape.h"

namespace zomlang::compiler::hir {

namespace detail {

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
struct PendingConditionalArm final {
  zc::Maybe<checker::checked::CanonicalConstValue> literal;
  zc::Maybe<HirParameterReferenceExpression> parameter;
  identity::SemanticTypeId type;
  identity::SourceSpan sourceSpan;
};

// One mutable-local write value. It is a scalar literal (`literal` populated), a
// reference to a function parameter lowered to a place operand (`parameter`
// populated), or a primitive binary operation (`binary` populated). Exactly one
// of the three is populated; the value kind is discriminated by which,
// generalizing the literal-XOR-parameter shape to a third binary alternative. A
// literal write lowers to a `MirOperand::constant`; a parameter write lowers to
// a copy/move place-use of the caller's parameter local; a binary write lowers
// to an Arithmetic or Comparison rvalue exactly like the primitive-binary
// initializer path. A binary write's two operands are each a scalar literal or a
// parameter reference (the literal-XOR-parameter shape of a conditional arm),
// with at least one parameter.
struct PendingLocalWriteBinary final {
  PendingConditionalArm left;
  PendingConditionalArm right;
  identity::SemanticTypeId operandType;
  identity::SemanticTypeId type;
  checker::PrimitiveOperation operation;
  identity::SourceSpan sourceSpan;
};

struct PendingLocalWriteValue final {
  zc::Maybe<checker::checked::CanonicalConstValue> literal;
  zc::Maybe<HirParameterReferenceExpression> parameter;
  zc::Maybe<PendingLocalWriteBinary> binary;
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

// One admitted `while` loop whose non-empty body writes a mutable local, fused
// with the leading mut-local declaration and the trailing local return. The
// declaration, writes, and return reuse the flat mut-local pending fields
// (`local`, `localWrites`, `localWriteValues`, `localReference`); this carrier
// adds only the loop condition parameter reference and the loop/body spans. The
// loop body writes are materialized as the loop statement's body node ids, and
// the function body block is `[local, loop, return]`.
struct PendingLoopBodyReturn final {
  HirParameterReferenceExpression condition;
  identity::SourceSpan loopSpan;
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
  // Populated for the loop-body composite shape (a leading mut-local, a `while`
  // whose body writes it, and a local return). The declaration, writes, and
  // return reuse the flat mut-local fields; this holds only the loop condition
  // parameter reference and the loop span.
  zc::Maybe<PendingLoopBodyReturn> loopBodyReturn;
};

}  // namespace detail
}  // namespace zomlang::compiler::hir
