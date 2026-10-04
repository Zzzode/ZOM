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
  // True when this PrimitiveBinary binding is the desugared form of a unary
  // operation. The right operand is a synthetic constant derived by the builder
  // from the operation and type; the tally subtracts it from the literal count.
  bool isUnaryDesugar = false;
  // Populated for Ternary: the condition is a bool parameter reference
  // (`ternaryConditionParameter` set), a reference to an earlier local
  // (`ternaryConditionIsLocal` true, `ternaryConditionLocal` its zero-based
  // index), or a bool literal (`ternaryConditionIsLiteral` true,
  // `ternaryConditionLiteral` set). Both branches are scalar literals carried
  // as canonical values.
  zc::Maybe<identity::CallableParameterKey> ternaryConditionParameter;
  bool ternaryConditionIsLocal = false;
  size_t ternaryConditionLocal = 0;
  bool ternaryConditionIsLiteral = false;
  identity::SemanticTypeId ternaryConditionType;
  zc::Maybe<checker::checked::CanonicalConstValue> ternaryConditionLiteral;
  zc::Maybe<checker::checked::CanonicalConstValue> ternaryThenLiteral;
  zc::Maybe<checker::checked::CanonicalConstValue> ternaryElseLiteral;
  zc::Maybe<identity::SourceSpan> ternaryConditionSpan;
  zc::Maybe<identity::SourceSpan> ternaryThenSpan;
  zc::Maybe<identity::SourceSpan> ternaryElseSpan;
  // True when this Ternary binding is normalized from a match expression. The
  // match carries two extra pattern literals (true/false) with node-type and
  // literal facts that the count equations must account for.
  bool isMatchExpr = false;
  // True when the match expression additionally carries a default (wildcard)
  // arm. The default arm body is dropped during normalization, but the
  // checker still produces node-type and literal facts for it, which the
  // count equations must credit.
  bool matchHasDefaultArm = false;
};

struct PendingSequentialLocalReturn final {
  zc::Vector<PendingSequentialBinding> bindings;
  identity::SemanticTypeId type;
  // The returned place: a parameter (`returnParameter` populated) or one of the
  // declared locals (`returnLocal` holds its zero-based index).
  zc::Maybe<identity::CallableParameterKey> returnParameter;
  size_t returnLocal;
  identity::SourceSpan returnValueSpan;
  // Dead-erase slice: bindings skipped by the dead-binding filter. The checker
  // produces facts for every binding, so the count validation must add these
  // back to the expected counts. The HIR lowering only sees `bindings`.
  zc::Vector<SequentialLocalBinding> deadBindings;
};

// Forward declaration to break the circular by-value dependency with
// PendingConditionalArmBinary.
struct PendingConditionalArm;

// One primitive binary operation embedded in a conditional arm. The two
// operands reuse the literal-XOR-parameter-XOR-local arm shape; the operation
// is a checked scalar arithmetic call. A binary arm lowers to an Arithmetic
// rvalue exactly like the primitive-binary initializer path. The operands are
// heap-owned to break the otherwise-circular by-value dependency with
// PendingConditionalArm.
struct PendingConditionalArmBinary final {
  zc::Own<PendingConditionalArm> left;
  zc::Own<PendingConditionalArm> right;
  identity::SemanticTypeId operandType;
  checker::PrimitiveOperation operation;
};

// One conditional arm carries a scalar literal value, a reference to a
// function parameter, a reference to the function's user local, or a primitive
// binary operation over two such leaves. Exactly one of `literal`, `parameter`,
// `local`, and `binary` is populated; the arm kind is discriminated by which
// one is set. The local alternative is reachable only from a binary write
// operand (`x = x + 1`); conditional and comparison arms never populate it.
// `binary` is declared last so the pre-existing five-field aggregate
// initializers (literal, parameter, local, type, sourceSpan) keep compiling
// with a defaulted none.
struct PendingConditionalArm final {
  zc::Maybe<checker::checked::CanonicalConstValue> literal;
  zc::Maybe<HirParameterReferenceExpression> parameter;
  zc::Maybe<HirLocalReferenceExpression> local;
  identity::SemanticTypeId type;
  identity::SourceSpan sourceSpan;
  zc::Maybe<PendingConditionalArmBinary> binary = zc::none;
};

// One mutable-local write value. It is a scalar literal (`literal` populated), a
// reference to a function parameter lowered to a place operand (`parameter`
// populated), or a primitive binary operation (`binary` populated). Exactly one
// of the three is populated; the value kind is discriminated by which,
// generalizing the literal-XOR-parameter shape to a third binary alternative. A
// literal write lowers to a `MirOperand::constant`; a parameter write lowers to
// a copy/move place-use of the caller's parameter local; a binary write lowers
// to an Arithmetic or Comparison rvalue exactly like the primitive-binary
// initializer path. A binary write's two operands are each a scalar literal, a
// parameter reference, or a reference to the written user local (the
// literal-XOR-parameter shape of a conditional arm, generalized with a local
// alternative), with at least one non-literal operand.
struct PendingLocalWriteBinary final {
  PendingConditionalArm left;
  PendingConditionalArm right;
  identity::SemanticTypeId operandType;
  identity::SemanticTypeId type;
  checker::PrimitiveOperation operation;
  identity::SourceSpan sourceSpan;
  // True when this binary write is the desugared form of a postfix
  // increment/decrement (`x++` -> `x = x + 1`). The PostfixExpression node
  // replaces the assignment plus binary nodes (two node-type facts instead of
  // five) and the synthetic literal 1 has no checked literal fact, so the
  // digest equations subtract one per desugared write.
  bool isPostfixDesugar = false;
  // True when this binary write is the desugared form of a compound
  // assignment (`x += 1` -> `x = x + 1`). The AssignmentExpr node carries
  // the target and value node-type facts (three instead of five) and the
  // binary operation has no checked call fact, so the digest equations
  // subtract two node-type facts and one call fact per desugared write.
  bool isCompoundAssignmentDesugar = false;
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
  // True when this condition desugars a unary operation (`-x` -> `0 - x`,
  // `+x` -> `x + 0`, `~x` -> `x ^ -1`, `!x` -> `x == false`). The synthetic
  // operand has no AST node and therefore no checker-produced node-type fact,
  // so the nodeTypes counting equation subtracts one per unary return.
  bool isUnaryDesugar = false;
};

// One conjunctive condition: a bare bool parameter ANDed with a relational
// comparison. The match-guard shape lowers to this: the scrutinee is the bool
// parameter and the guard is the comparison. The HIR builder materializes the
// comparison as a primitive binary and the conjunction as a second primitive
// binary (BitAnd), keeping the four-block diamond CFG.
struct PendingConjunctiveCondition final {
  HirParameterReferenceExpression parameter;
  PendingEqualityCondition guard;
  identity::SourceSpan sourceSpan;
};

// One conditional condition is either a bare bool parameter reference, an
// `a == b` equality comparison of two parameter references, or a conjunctive
// condition (bool parameter AND comparison). Exactly one of the three Maybe
// fields is populated; the condition kind is discriminated by which.
struct PendingConditionalCondition final {
  zc::Maybe<HirParameterReferenceExpression> parameter;
  zc::Maybe<PendingEqualityCondition> equality;
  zc::Maybe<PendingConjunctiveCondition> conjunctive;
};

struct PendingConditionalReturn final {
  PendingConditionalCondition condition;
  PendingConditionalArm thenArm;
  PendingConditionalArm elseArm;
  identity::SourceSpan conditionalSpan;
  // True when the conditional return was classified from a two-arm boolean
  // match statement. The match statement and its scrutinee expression produce
  // two extra node-type facts the count equations must credit.
  bool isMatchReturn = false;
  // True when the match-return has a default (wildcard) arm instead of a
  // second literal arm. The default arm produces one fewer pattern-literal
  // fact, which the count equations must subtract.
  bool hasDefaultArm = false;
  // True when the match-return is an integer match with one literal pattern
  // arm and one default arm. The equality comparison is synthetic (no AST
  // BinaryExpr node), so the checker produces no call fact or
  // comparison-result node-type fact for it. The count equations subtract
  // the phantom call, the phantom comparison-result node-types, and the
  // double-counted pattern literal.
  bool isMatchEquality = false;
  // True when the match-return has a guard on the literal (then) arm. The
  // guard is a real AST BinaryExpr with checker-produced call, node-type, and
  // literal facts; the conjunction (BitAnd) is synthetic (no AST node). The
  // count equations credit the guard facts and subtract the phantom
  // conjunction call and comparison-result node-types.
  bool hasMatchGuard = false;
};

// A chained conditional return lowers a match with two or more integer literal
// arms and one default arm to a nested conditional chain:
// `if scrutinee == lit0 then val0 else if scrutinee == lit1 then val1 ... else
// defaultValue`. Each entry pairs a synthetic equality condition (scrutinee ==
// literal, no AST BinaryExpr node) with that arm's return value. The else arm
// carries the default arm's return value.
struct PendingChainedConditionalReturn final {
  struct Entry final {
    PendingEqualityCondition condition;
    PendingConditionalArm thenArm;
  };
  zc::Vector<Entry> entries;
  PendingConditionalArm elseArm;
  identity::SourceSpan matchSpan;
};

// One comparison-condition operand in a leading-local conditional body: a
// scalar literal, a parameter place reference, or a reference to one of the
// leading locals (`referencedLocal` is its zero-based binding index when
// `isLocal` is set).
struct PendingLeadingConditionOperand final {
  identity::SemanticTypeId type;
  identity::SourceSpan sourceSpan;
  zc::Maybe<checker::checked::CanonicalConstValue> literal;
  zc::Maybe<identity::CallableParameterKey> parameter;
  size_t referencedLocal = 0;
  bool isLocal = false;
};

// One leading scalar-local binding in a leading-local conditional body. The
// initializer is a scalar literal, a parameter copy, or a copy of an earlier
// leading local (`referencedLocal` is its zero-based binding index). A
// PrimitiveBinary binding instead populates `arithmeticOperation`,
// `arithmeticLeft`, and `arithmeticRight`; its two leaf operands are each a
// scalar literal or a parameter reference.
struct PendingLeadingLocalBinding final {
  identity::SemanticTypeId type;
  identity::SourceSpan patternSpan;
  identity::SourceSpan initializerSpan;
  SequentialInitializerKind kind;
  zc::Maybe<checker::checked::CanonicalConstValue> literal;
  zc::Maybe<identity::CallableParameterKey> parameter;
  size_t referencedLocal;
  zc::Maybe<checker::PrimitiveOperation> arithmeticOperation;
  zc::Maybe<PendingLeadingConditionOperand> arithmeticLeft;
  zc::Maybe<PendingLeadingConditionOperand> arithmeticRight;
};

// K leading scalar-local bindings followed by one comparison conditional with
// two literal arms. The comparison operands may name the leading locals; the
// bool result drives the conditional exactly like the sole-if equality shape.
struct PendingLeadingLocalConditionalReturn final {
  zc::Vector<PendingLeadingLocalBinding> bindings;
  PendingLeadingConditionOperand left;
  PendingLeadingConditionOperand right;
  identity::SemanticTypeId operandType;
  identity::SemanticTypeId conditionType;
  checker::PrimitiveOperation operation;
  identity::SourceSpan conditionSpan;
  checker::checked::CanonicalConstValue thenLiteral;
  checker::checked::CanonicalConstValue elseLiteral;
  identity::SemanticTypeId resultType;
  identity::SourceSpan thenSpan;
  identity::SourceSpan elseSpan;
  identity::SourceSpan returnSpan;
  bool isUnaryDesugar;
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
//
// A trailing unlabeled `break;` or `continue;` is carried as its source span so
// the materialized `HirLoopStatement` can select the body block terminator (exit
// for break, header back-edge for continue/fallthrough). At most one is set.
struct PendingLoopBodyReturn final {
  HirParameterReferenceExpression condition;
  identity::SourceSpan loopSpan;
  zc::Maybe<identity::SourceSpan> breakSpan;
  zc::Maybe<identity::SourceSpan> continueSpan;
};

// One admitted C-style `for` loop return: `for (let id = <lit>; <ident> <cmp>
// <lit>; <ident> = <binary>) {}` followed by a scalar return. The for-loop
// desugars to a leading local binding plus a loop whose condition is the
// comparison and whose body is the update write. The init local, comparison
// condition, and update write are all resolved in the HIR builder and carried
// here for the lowering function. Every HIR expression carries an empty
// HirNodeId; the lowering function allocates the node ids. The operand
// expressions (init literal, comparison left/right, arithmetic left/right) are
// carried alongside their parent binary/write/local so the lowering function
// can materialize the operand nodes without reaching back into the AST.
struct PendingForLoopReturn final {
  // The loop condition comparison, resolved to a primitive binary expression.
  // The left operand is a reference to the init local; the right operand is a
  // scalar literal.
  HirPrimitiveBinaryExpression condition;
  // The comparison's left operand: a place reference to the init local.
  HirLocalReferenceExpression conditionLeft;
  // The comparison's right operand: a scalar literal.
  HirScalarLiteralExpression conditionRight;
  // The loop's source span.
  identity::SourceSpan loopSpan;
  // The init local binding (let declaration with scalar literal initializer).
  HirLocalBinding local;
  // The init local's scalar literal initializer.
  HirScalarLiteralExpression initLiteral;
  // The update write (assignment with arithmetic binary RHS).
  HirLocalWriteStatement write;
  // The update write's arithmetic binary value.
  HirPrimitiveBinaryExpression writeValue;
  // The arithmetic's left operand: a place reference to the init local.
  HirLocalReferenceExpression writeValueLeft;
  // The arithmetic's right operand: a scalar literal.
  HirScalarLiteralExpression writeValueRight;
  // A trailing unlabeled `break;` or `continue;` source span, carried so the
  // materialized `HirLoopStatement` can select the body block terminator (exit
  // for break, header back-edge for continue/fallthrough). At most one is set;
  // both are none for an empty body.
  zc::Maybe<identity::SourceSpan> breakSpan;
  zc::Maybe<identity::SourceSpan> continueSpan;
};

// One accumulator in a for-loop multi-write body: the local binding, its
// scalar-literal initializer, the body write assignment and its arithmetic
// binary value, the left operand (a place reference to the accumulator), and
// the right operand (a place reference to the loop init local or a scalar
// literal). Every HIR expression carries an empty HirNodeId; the lowering
// function allocates the node ids.
struct PendingForLoopAccumulator final {
  // The accumulator local binding (the leading `mut x = <lit>` declaration).
  HirLocalBinding local;
  // The accumulator local's scalar literal initializer.
  HirScalarLiteralExpression initLiteral;
  // The body write (`x = x + i` or `x = x + 1`), an arithmetic binary over
  // the accumulator and the loop init local or a scalar literal.
  HirLocalWriteStatement bodyWrite;
  // The body write's arithmetic binary value.
  HirPrimitiveBinaryExpression bodyWriteValue;
  // The body arithmetic's left operand: a place reference to the accumulator.
  HirLocalReferenceExpression bodyWriteLeft;
  // True when the body arithmetic's right operand is a scalar literal; false
  // when it is a place reference to the loop init local.
  bool bodyWriteRightIsLiteral = false;
  // The body arithmetic's right operand as a place reference to the init
  // local (populated when bodyWriteRightIsLiteral is false).
  zc::Maybe<HirLocalReferenceExpression> bodyWriteRight;
  // The body arithmetic's right operand as a scalar literal (populated when
  // bodyWriteRightIsLiteral is true).
  zc::Maybe<HirScalarLiteralExpression> bodyWriteRightLiteral;
};

// One admitted C-style `for` loop accumulator return: N leading scalar `mut`
// accumulator locals, a `for (let id = <lit>; <ident> <cmp> <lit>;
// <ident> = <binary>) { <accumulator> = <accumulator> <bin> <ident|lit>; ...
// }` loop, and a trailing `return <accumulator-local>;`. The for-loop
// init/cond/update mirror PendingForLoopReturn; the accumulator locals, their
// initializers, the body writes, and the return reference are carried here.
// Every HIR expression carries an empty HirNodeId; the lowering function
// allocates the node ids.
struct PendingForLoopAccumulatorReturn final {
  // The loop condition comparison, resolved to a primitive binary expression.
  // The left operand is a reference to the init local; the right operand is a
  // scalar literal.
  HirPrimitiveBinaryExpression condition;
  // The comparison's left operand: a place reference to the init local.
  HirLocalReferenceExpression conditionLeft;
  // The comparison's right operand: a scalar literal.
  HirScalarLiteralExpression conditionRight;
  // The loop's source span.
  identity::SourceSpan loopSpan;
  // The init local binding (let declaration with scalar literal initializer).
  HirLocalBinding local;
  // The init local's scalar literal initializer.
  HirScalarLiteralExpression initLiteral;
  // The update write (assignment with arithmetic binary RHS).
  HirLocalWriteStatement write;
  // The update write's arithmetic binary value.
  HirPrimitiveBinaryExpression writeValue;
  // The arithmetic's left operand: a place reference to the init local.
  HirLocalReferenceExpression writeValueLeft;
  // The arithmetic's right operand: a scalar literal.
  HirScalarLiteralExpression writeValueRight;
  // The N accumulator locals, their initializers, and their body writes.
  zc::Vector<PendingForLoopAccumulator> accumulators;
  // The return value: a place reference to the first accumulator local.
  HirLocalReferenceExpression returnReference;
  // A trailing unlabeled `break;` or `continue;` source span, carried so the
  // materialized `HirLoopStatement` can select the body block terminator (exit
  // for break, header back-edge for continue/fallthrough). At most one is set;
  // both are none when the body ends with a write.
  zc::Maybe<identity::SourceSpan> breakSpan;
  zc::Maybe<identity::SourceSpan> continueSpan;
  // The if-guarded break condition comparison, populated when the loop body
  // starts with `if (<cond>) { break; }`. The left operand is a place
  // reference to the init local; the right operand is a scalar literal. The
  // MIR builder lowers a guard block that evaluates this comparison and exits
  // the loop on true, before the accumulator writes. All three are empty when
  // the body has no guarded break.
  zc::Maybe<HirPrimitiveBinaryExpression> breakCondition;
  zc::Maybe<HirLocalReferenceExpression> breakConditionLeft;
  zc::Maybe<HirScalarLiteralExpression> breakConditionRight;
};

// One admitted nested C-style `for` loop accumulator return: N leading scalar
// `mut` accumulator locals, an outer `for` loop whose sole body statement is
// an inner `for` loop that writes each accumulator, and a trailing
// `return <accumulator-local>;`. The outer and inner loops each carry their
// own init/cond/update; the accumulator locals, their initializers, and their
// body writes (which live in the inner loop body) are carried here. Every HIR
// expression carries an empty HirNodeId; the lowering function allocates the
// node ids.
struct PendingNestedForLoopAccumulatorReturn final {
  // Outer loop condition comparison.
  HirPrimitiveBinaryExpression outerCondition;
  HirLocalReferenceExpression outerConditionLeft;
  HirScalarLiteralExpression outerConditionRight;
  identity::SourceSpan outerLoopSpan;
  // Outer loop init local binding and scalar literal initializer.
  HirLocalBinding outerLocal;
  HirScalarLiteralExpression outerInitLiteral;
  // Outer loop update write and its arithmetic binary value.
  HirLocalWriteStatement outerWrite;
  HirPrimitiveBinaryExpression outerWriteValue;
  HirLocalReferenceExpression outerWriteValueLeft;
  HirScalarLiteralExpression outerWriteValueRight;
  // Inner loop condition comparison.
  HirPrimitiveBinaryExpression innerCondition;
  HirLocalReferenceExpression innerConditionLeft;
  HirScalarLiteralExpression innerConditionRight;
  identity::SourceSpan innerLoopSpan;
  // Inner loop init local binding and scalar literal initializer.
  HirLocalBinding innerLocal;
  HirScalarLiteralExpression innerInitLiteral;
  // Inner loop update write and its arithmetic binary value.
  HirLocalWriteStatement innerWrite;
  HirPrimitiveBinaryExpression innerWriteValue;
  HirLocalReferenceExpression innerWriteValueLeft;
  HirScalarLiteralExpression innerWriteValueRight;
  // The N accumulator locals, their initializers, and their body writes.
  zc::Vector<PendingForLoopAccumulator> accumulators;
  // The return value: a place reference to the first accumulator local.
  HirLocalReferenceExpression returnReference;
};

// One receiver field-arithmetic return: `return this.<field> OP
// <literal>;`. The left operand is a projection of the implicit receiver
// parameter, the right operand is a scalar literal, and the primitive binary
// result flows into the return. The field projection is a place read; its type
// is the field type shared by both operands.
struct PendingReceiverFieldArithmetic final {
  HirParameterFieldProjectionExpression field;
  checker::checked::CanonicalConstValue literal;
  identity::SemanticTypeId operandType;
  checker::PrimitiveOperation operation;
  identity::SourceSpan literalSpan;
  identity::SourceSpan fieldSpan;
  identity::SourceSpan binarySpan;
  // Source operand order: true when the field projection is the binary's left
  // operand and the literal is the right operand.
  bool fieldIsLeft;
};

struct PendingFunctionDeclaration final {
  identity::DefId definition;
  identity::SemanticTypeId resultType;
  zc::Vector<HirParameter> parameters;
  // Set only for an inherent method: its implicit `this` receiver, kept
  // separate from the ordinary `parameters`.
  zc::Maybe<HirParameter> receiver;
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
  zc::Maybe<HirParameterFieldProjectionExpression> parameterFieldProjection;
  zc::Maybe<HirParameterReferenceExpression> parameterReference;
  zc::Maybe<HirParameterIndexExpression> parameterIndex;
  zc::Maybe<HirParameterReborrowExpression> parameterReborrow;
  zc::Maybe<HirLocalBorrowExpression> localBorrow;
  zc::Maybe<PendingSequentialLocalReturn> sequentialLocalReturn;
  zc::Maybe<identity::SourceSpan> unsafeBlockSpan;
  zc::Array<uint8_t> orderingKey;
  zc::Maybe<PendingConditionalReturn> conditionalReturn;
  zc::Maybe<PendingChainedConditionalReturn> chainedConditionalReturn;
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
  // Populated for the C-style for-loop shape: a `for (let id = <lit>;
  // <ident> <cmp> <lit>; <ident> = <binary>) {}` followed by a scalar
  // return. The init local, comparison condition, and update write are
  // carried here; the return literal reuses `literal`.
  zc::Maybe<PendingForLoopReturn> forLoopReturn;
  // Populated for the C-style for-loop accumulator shape: a leading scalar
  // `let` accumulator local, a `for` loop whose body writes that accumulator,
  // and a trailing `return <accumulator-local>;`. The accumulator local, the
  // for-loop init/cond/update, the body write, and the return reference are
  // carried here; there is no return literal.
  zc::Maybe<PendingForLoopAccumulatorReturn> forLoopAccumulatorReturn;
  // Populated for the nested for-loop accumulator shape: N leading scalar
  // `mut` accumulator locals, an outer `for` loop whose sole body statement
  // is an inner `for` loop that writes each accumulator, and a trailing
  // `return <accumulator-local>;`.
  zc::Maybe<PendingNestedForLoopAccumulatorReturn> nestedForLoopAccumulatorReturn;
  // Populated for the mutating-receiver write-read body: one Overwrite of a
  // field reached through the mutable receiver, plus its scalar literal value.
  zc::Maybe<HirParameterFieldWriteStatement> parameterFieldWrite;
  zc::Maybe<checker::checked::CanonicalConstValue> parameterFieldWriteLiteral;
  // Populated for the shared-receiver self-call body: `return this.method();`
  // forwards the implicit receiver parameter to a zero-argument method of the
  // same owner. Unlike `receiverCall` (an owner-local call with an aggregate
  // binding), the receiver is a parameter reference and there is no local or
  // aggregate carrier.
  zc::Maybe<HirReceiverCallExpression> receiverSelfCall;
  // Populated for the receiver field-arithmetic body:
  // `return this.<field> OP <literal>;` projects one field off the implicit
  // receiver and combines it with a scalar literal.
  zc::Maybe<PendingReceiverFieldArithmetic> receiverFieldArithmetic;
  // Populated for the discarded unit-resulting receiver-call statement
  // (`cell.set(42);`). Its node id is placed directly in the body block
  // statement list; there is no return or local reference tied to it.
  zc::Maybe<HirReceiverCallExpression> statementReceiverCall;
  // The discarded call's owner-local receiver reference. Its source node and
  // span are distinct from the trailing call's receiver; both borrow the same
  // HirLocalId.
  zc::Maybe<HirLocalReferenceExpression> statementReceiverReference;
  // Populated for the void method shape: the ordinary parameter referenced by
  // the sole receiver-field write RHS (`this.value = x;`). The write value
  // node names this pooled parameter reference; parameterFieldWriteLiteral is
  // unset.
  zc::Maybe<HirParameterReferenceExpression> parameterFieldWriteParameter;
  // True for the void method body (no HirReturnStatement is materialized).
  bool voidBody = false;
  // True when parameterFieldProjection reads a field of an ordinary by-value
  // struct parameter (no receiver dereference) rather than the implicit method
  // receiver. Selects the by-value MIR return builder.
  bool byValueParameterField = false;
  // Populated for K leading scalar-local bindings followed by one comparison
  // conditional with two literal arms.
  zc::Maybe<PendingLeadingLocalConditionalReturn> leadingLocalConditionalReturn;
  // True when the return value is a compile-time-folded string concatenation
  // (`return "a" + "b"`). The body checker emits a string-literal fact for the
  // binary node; the two operand StringLiteralExpr nodes each carry an extra
  // node-type fact beyond the per-function baseline.
  bool returnsFoldedStringConcat = false;
  // True when the return value is a compile-time-folded float-to-integer cast
  // (`return 1.5 as i32`). The body checker emits an integer-literal fact for
  // the cast node; the inner FloatLiteralExpr node carries an extra node-type
  // and literal fact beyond the per-function baseline.
  bool returnsFoldedFloatCast = false;
};

}  // namespace detail
}  // namespace zomlang::compiler::hir
