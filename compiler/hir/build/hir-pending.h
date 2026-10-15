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
  /// Source AST nodes threaded to the HIR side-table so the per-node
  /// correspondence validator can anchor each pooled node to its checked fact.
  ast::NodeId declarationNode;
  ast::NodeId patternNode;
  ast::NodeId initializerNode{};
};

struct PendingSequentialBinaryLeafOperand final {
  SequentialBinaryOperandKind kind;
  identity::SemanticTypeId type;
  identity::SourceSpan sourceSpan;
  zc::Maybe<checker::checked::CanonicalConstValue> literal;
  zc::Maybe<identity::CallableParameterKey> parameter;
  size_t referencedLocal;
  /// The AST node this operand was lowered from, threaded to the HIR
  /// side-table so the per-node correspondence validator can anchor it.
  ast::NodeId sourceNode{};
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
  /// The AST node this operand was lowered from.
  ast::NodeId sourceNode{};
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
  /// The AST VariableDeclarator and initializer expression nodes, threaded to
  /// the HIR side-table.
  ast::NodeId declaratorNode{};
  ast::NodeId initializerNode{};
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
  /// The AST BinaryExpr node this operation was lowered from.
  ast::NodeId sourceNode{};
};

// One conditional arm carries a scalar literal value, a reference to a
// function parameter, a reference to the function's user local, a primitive
// binary operation over two such leaves, a field projection off a local, or
// a positional field extraction off a parameter (enum tuple-variant payload
// extraction). Exactly one of `literal`, `parameter`, `local`, `binary`,
// `fieldProjection`, and `parameterFieldProjection` is populated; the arm
// kind is discriminated by which one is set.
// `binary`, `fieldProjection`, and `parameterFieldProjection` are declared
// last so the pre-existing five-field aggregate initializers (literal,
// parameter, local, type, sourceSpan) keep compiling with defaulted nones.
struct PendingConditionalArmFieldProjection final {
  HirLocalId local;
  identity::DefId field;
  identity::SemanticTypeId receiverType;
};

// One positional field extraction off a function parameter, used for enum
// tuple-variant destructure arms. `fieldIndex` is the zero-based aggregate
// field index (1 for the payload of a single-payload tuple variant; the tag
// is at index 0). `field` carries the enum definition's DefId for fact
// correlation; the MIR builder uses `fieldIndex` for positional extraction.
struct PendingConditionalArmParameterFieldProjection final {
  identity::CallableParameterKey parameter;
  identity::SemanticTypeId receiverType;
  identity::DefId field;
  identity::SemanticTypeId type;
  uint32_t fieldIndex;
};

struct PendingConditionalArm final {
  zc::Maybe<checker::checked::CanonicalConstValue> literal;
  zc::Maybe<HirParameterReferenceExpression> parameter;
  zc::Maybe<HirLocalReferenceExpression> local;
  identity::SemanticTypeId type;
  identity::SourceSpan sourceSpan;
  zc::Maybe<PendingConditionalArmBinary> binary = zc::none;
  /// The AST node this arm was lowered from (literal, identifier, or binary
  /// expression), threaded to the HIR side-table.
  ast::NodeId sourceNode{};
  zc::Maybe<PendingConditionalArmFieldProjection> fieldProjection = zc::none;
  zc::Maybe<PendingConditionalArmParameterFieldProjection> parameterFieldProjection = zc::none;
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
  // True when this binary write is the desugared form of a prefix or postfix
  // increment/decrement (`++x` / `x++` -> `x = x + 1`). The UnaryExpression or
  // PostfixExpression node replaces the assignment plus binary nodes (two
  // node-type facts instead of five) and the synthetic literal 1 has no
  // checked literal fact, so the digest equations subtract one per desugared
  // write.
  bool isIncrementDesugar = false;
  // True when this binary write is the desugared form of a compound
  // assignment (`x += 1` -> `x = x + 1`). The AssignmentExpr node carries
  // the target and value node-type facts (three instead of five) and the
  // binary operation has no checked call fact, so the digest equations
  // subtract two node-type facts and one call fact per desugared write.
  bool isCompoundAssignmentDesugar = false;
  /// The AST node this binary write was lowered from (AssignmentExpr,
  /// UnaryExpression, or PostfixExpression), threaded to the HIR side-table.
  ast::NodeId sourceNode{};
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
  /// The AST BinaryExpr node this comparison was lowered from. Empty for
  /// synthetic comparisons (match-equality, unary desugar).
  ast::NodeId sourceNode{};
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
  /// The AST BinaryExpr node of the guard comparison. The conjunction
  /// (BitAnd) is synthetic and carries no AST node.
  ast::NodeId sourceNode{};
};

// One call condition: a direct free-function call whose bool result drives the
// conditional. The HIR builder materializes the call as a
// HirDirectCallExpression; the MIR builder evaluates it into a temporary
// before the SwitchInt terminator.
struct PendingCallCondition final {
  HirDirectCallExpression call;
  identity::SourceSpan sourceSpan;
  /// The AST CallExpression node this condition was lowered from.
  ast::NodeId sourceNode{};
};

// One conditional condition is either a bare bool parameter reference, an
// `a == b` equality comparison of two parameter references, a conjunctive
// condition (bool parameter AND comparison), or a direct call whose bool
// result drives the conditional. Exactly one of the four Maybe fields is
// populated; the condition kind is discriminated by which.
struct PendingConditionalCondition final {
  zc::Maybe<HirParameterReferenceExpression> parameter;
  zc::Maybe<PendingEqualityCondition> equality;
  zc::Maybe<PendingConjunctiveCondition> conjunctive;
  zc::Maybe<PendingCallCondition> call;
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
  /// The AST IfStmt (or MatchStmt) node this conditional was lowered from.
  ast::NodeId conditionalNode{};
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
  /// The AST MatchStmt node this chain was lowered from.
  ast::NodeId matchNode{};
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
  /// The AST node this operand was lowered from.
  ast::NodeId sourceNode{};
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
  /// The AST VariableDeclarator and initializer expression nodes.
  ast::NodeId declaratorNode{};
  ast::NodeId initializerNode{};
};

// K leading scalar-local bindings followed by one comparison conditional with
// two arms. The comparison operands may name the leading locals; the bool
// result drives the conditional exactly like the sole-if equality shape. Each
// arm is a scalar literal, a bare identifier (parameter or local reference),
// or a one-level binary expression with leaf operands, carried as a
// PendingConditionalArm so the materialization and MIR lowering reuse the same
// arm abstraction as the bare conditional path.
struct PendingLeadingLocalConditionalReturn final {
  zc::Vector<PendingLeadingLocalBinding> bindings;
  PendingLeadingConditionOperand left;
  PendingLeadingConditionOperand right;
  identity::SemanticTypeId operandType;
  identity::SemanticTypeId conditionType;
  checker::PrimitiveOperation operation;
  identity::SourceSpan conditionSpan;
  PendingConditionalArm thenArm;
  PendingConditionalArm elseArm;
  identity::SemanticTypeId resultType;
  identity::SourceSpan thenSpan;
  identity::SourceSpan elseSpan;
  identity::SourceSpan returnSpan;
  bool isUnaryDesugar;
  /// True when the condition was a bare bool identifier desugared to `c == true`.
  /// Unlike the unary case, the checker produces no call fact for the condition,
  /// so the calls count equation must subtract one.
  bool isBoolReferenceDesugar = false;
  /// The AST IfStmt node this conditional was lowered from.
  ast::NodeId conditionalNode{};
};

struct PendingLoopReturn final {
  HirParameterReferenceExpression condition;
  checker::checked::CanonicalConstValue returnLiteral;
  identity::SemanticTypeId returnType;
  identity::SourceSpan loopSpan;
  identity::SourceSpan returnValueSpan;
  /// The AST WhileStmt node this loop was lowered from.
  ast::NodeId loopNode{};
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
  /// The AST WhileStmt node this loop was lowered from.
  ast::NodeId loopNode{};
};

// One admitted loop-body return with a comparison condition: a leading `mut`
// local declaration, a while or do-while loop whose body writes that local and
// whose condition is a binary comparison over the local and a scalar literal
// (or a bare identifier), and a trailing local return. The declaration, body
// writes, and return reuse the flat mut-local pending fields (`local`,
// `localWrites`, `localWriteValues`, `localReference`); this carrier adds the
// resolved comparison condition and the loop/body spans. A while loop
// desugars to a condition-before-body reducible four-block CFG; a do-while
// loop desugars to a body-before-condition reducible four-block CFG. Every HIR
// expression carries an empty HirNodeId; the lowering function allocates the
// node ids.
struct PendingComparisonLoopBodyReturn final {
  // The loop condition comparison, resolved to a primitive binary expression.
  // The left operand is a reference to the accumulator local; the right
  // operand is a scalar literal.
  HirPrimitiveBinaryExpression condition;
  // The comparison's left operand: a place reference to the accumulator local.
  HirLocalReferenceExpression conditionLeft;
  // The comparison's right operand: a scalar literal.
  HirScalarLiteralExpression conditionRight;
  // The loop's source span.
  identity::SourceSpan loopSpan;
  // A trailing unlabeled `break;` or `continue;` source span, carried so the
  // materialized `HirLoopStatement` can select the body block terminator. At
  // most one is set; both are none for a write-only body.
  zc::Maybe<identity::SourceSpan> breakSpan;
  zc::Maybe<identity::SourceSpan> continueSpan;
  /// True when the loop is a do-while (body-before-condition CFG); false for
  /// a while loop (condition-before-body CFG).
  bool isDoWhile = false;
  /// The AST WhileStmt or DoWhileStatement node this loop was lowered from.
  ast::NodeId loopNode{};
  /// The AST ReturnStmt node of the trailing local return.
  ast::NodeId returnNode{};
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
  /// The AST ForStmt node this loop was lowered from.
  ast::NodeId loopNode{};
  /// The AST ReturnStmt node of the trailing scalar return.
  ast::NodeId returnNode{};
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
  /// The AST VariableDeclarator node of the accumulator binding.
  ast::NodeId bindingNode{};
  /// The AST AssignmentExpr node of the body write.
  ast::NodeId writeNode{};
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
  /// The AST ForStmt node this loop was lowered from.
  ast::NodeId loopNode{};
  /// The AST ReturnStmt node of the trailing scalar return.
  ast::NodeId returnNode{};
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
  /// The AST outer ForStmt node.
  ast::NodeId outerLoopNode{};
  /// The AST inner ForStmt node.
  ast::NodeId innerLoopNode{};
  /// The AST ReturnStmt node of the trailing scalar return.
  ast::NodeId returnNode{};
  /// The inner loop's trailing break source span, when present.
  zc::Maybe<identity::SourceSpan> innerBreakSpan;
  /// The inner loop's trailing continue source span, when present.
  zc::Maybe<identity::SourceSpan> innerContinueSpan;
  /// True when the inner trailing break targets the outer loop (labeled break).
  bool innerBreakTargetsEnclosing = false;
  /// True when the inner trailing continue targets the outer loop (labeled continue).
  bool innerContinueTargetsEnclosing = false;
};

// One admitted for-in loop accumulator return: N leading scalar `mut`
// accumulator locals, a `for (let x in [<lit>, ...]) { <accumulator> =
// <accumulator> <bin> x; ... }` loop, and a trailing
// `return <accumulator-local>;`. The loop is compile-time unrolled: each array
// element literal is substituted for the binding in the body write template,
// producing N*M sequential writes. The accumulator locals, their initializers,
// the body write template (binary operation and types), the element literals,
// and the return reference are carried here. Every HIR expression carries an
// empty HirNodeId; the lowering function allocates the node ids.
struct PendingForInLoopAccumulatorReturn final {
  // One accumulator: its local binding, scalar literal initializer, and AST
  // binding node for the per-node correspondence validator.
  struct Accumulator final {
    HirLocalBinding local;
    HirScalarLiteralExpression initLiteral;
    ast::NodeId bindingNode{};
  };
  // The N accumulator locals and their initializers.
  zc::Vector<Accumulator> accumulators;
  // The body write template: an arithmetic binary over the accumulator and the
  // for-in binding. The lowering function substitutes each element literal for
  // the binding, producing one write per element per accumulator.
  checker::PrimitiveOperation operation;
  identity::SemanticTypeId operandType;
  identity::SemanticTypeId resultType;
  identity::SourceSpan writeSpan;
  identity::SourceSpan valueSpan;
  identity::SourceSpan leftSpan;
  // The M array element literals, one per unrolled iteration.
  zc::Vector<HirScalarLiteralExpression> elements;
  // The return value: a place reference to the first accumulator local.
  HirLocalReferenceExpression returnReference;
  /// The AST ForInStatement node.
  ast::NodeId loopNode{};
  /// The AST ReturnStmt node of the trailing scalar return.
  ast::NodeId returnNode{};
  /// The AST body write AssignmentExpr nodes, one per accumulator, for the
  /// per-node correspondence validator.
  zc::Vector<ast::NodeId> bodyWriteNodes;
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
  /// The AST MemberExpression node of the field projection.
  ast::NodeId fieldNode{};
  /// The AST literal node of the non-field operand.
  ast::NodeId literalNode{};
  /// The AST BinaryExpr node of the arithmetic operation.
  ast::NodeId binaryNode{};
};

// One dead scalar-let binding in a void-let-body destructor. The local binding
// and its scalar-literal value are carried for the lowering function; the dead
// local is folded away by the LIR builder.
struct PendingVoidLetBinding final {
  HirLocalBinding local;
  checker::checked::CanonicalConstValue literal;
};

// Three-statement dyn field-access shape: an aggregate-initialized local with
// per-field erasure (`let zoo = Zoo { animal: Dog { .. } };`), a field-read
// local (`let a: dyn I = zoo.animal;`), and a trailing method-call return
// (`return a.speak();`). The aggregate carries a concrete-to-existential
// erasure on its dyn-typed field; the field read copies the fat-pointer
// carrier into the second local; the method call dispatches indirectly
// through the vtable.
struct PendingDynFieldAccessReturn final {
  HirLocalBinding aggregateLocal;
  HirNominalAggregateExpression aggregate;
  HirLocalBinding fieldLocal;
  HirLocalFieldProjectionExpression fieldProjection;
  HirLocalReferenceExpression receiverReference;
  HirReceiverCallExpression receiverCall;
};

// ===== PendingFunctionDeclaration shape variants =====
//
// Each variant carries only the fields its shape needs. The variant itself
// is the shape discriminant — no field-presence checks are needed to
// determine which lowering helper to call. The X-macro list below is the
// single source of truth for the variant set; the OneOf type alias and
// any future visitor dispatch are generated from it.

struct EmptyVoidBody {};

struct VoidLetBody {
  zc::Vector<PendingVoidLetBinding> voidLetBindings;
};

struct DestructorFieldWriteBody {};

struct DestructorMethodCallBody {};

struct VoidMutatingMethod {
  HirParameterFieldWriteStatement parameterFieldWrite;
  HirParameterReferenceExpression parameterFieldWriteParameter;
};

struct ScalarLiteralReturn {
  checker::checked::CanonicalConstValue literal;
};

struct UnsafeScalarReturn {
  checker::checked::CanonicalConstValue literal;
};

struct ArrayIndexFoldReturn {
  checker::checked::CanonicalConstValue literal;
};

struct ArrayLengthFoldReturn {
  checker::checked::CanonicalConstValue literal;
};

struct LocalReturn {
  HirLocalBinding local;
  zc::Maybe<checker::checked::CanonicalConstValue> literal;
  zc::Maybe<HirParameterReferenceExpression> parameterReference;
  HirLocalReferenceExpression localReference;
};

struct LocalWriteReturn {
  HirLocalBinding local;
  zc::Maybe<checker::checked::CanonicalConstValue> literal;
  zc::Maybe<HirParameterReferenceExpression> parameterReference;
  zc::Vector<HirLocalWriteStatement> localWrites;
  zc::Vector<PendingLocalWriteValue> localWriteValues;
  HirLocalReferenceExpression localReference;
};

struct LocalFieldProjectionReturn {
  HirLocalBinding local;
  zc::Maybe<HirNominalAggregateExpression> aggregate;
  zc::Vector<HirLocalWriteStatement> localWrites;
  zc::Vector<PendingLocalWriteValue> localWriteValues;
  HirLocalFieldProjectionExpression localFieldProjection;
};

struct LocalBorrowReturn {
  HirLocalBinding local;
  zc::Maybe<checker::checked::CanonicalConstValue> literal;
  zc::Maybe<HirParameterReferenceExpression> parameterReference;
  HirLocalBorrowExpression localBorrow;
};

struct ParameterReferenceReturn {
  HirParameterReferenceExpression parameterReference;
};

struct ParameterIndexReturn {
  HirParameterIndexExpression parameterIndex;
};

struct ParameterReborrowReturn {
  HirParameterReborrowExpression parameterReborrow;
};

struct LocalAliasReborrowReturn {
  HirLocalBinding local;
  HirParameterReferenceExpression parameterReference;
  HirParameterReborrowExpression parameterReborrow;
};

struct RawDereferenceReturn {
  HirParameterReferenceExpression rawDereferenceSource;
};

struct RawPointerWriteReturn {
  HirRawPointerWriteStatement rawDereferenceWrite;
  identity::SourceSpan writeUnsafeBlockSpan;
  checker::checked::CanonicalConstValue rawDereferenceWriteLiteral;
  HirParameterReferenceExpression rawDereferenceSource;
};

struct AggregateReturn {
  HirNominalAggregateExpression aggregate;
};

struct EnumConstructionReturn {
  HirNominalAggregateExpression aggregate;
};

struct ExistentialReturn {
  HirNominalAggregateExpression aggregate;
  HirErasureRecord returnErasure;
};

struct AggregateFieldProjectionReturn {
  HirNominalAggregateExpression aggregate;
  HirLocalBinding local;
  HirLocalFieldProjectionExpression localFieldProjection;
  HirLocalReferenceExpression localReference;
};

struct DirectScalarLocalCallReturn {
  HirDirectCallExpression call;
  checker::checked::CanonicalConstValue literal;
  HirLocalBinding local;
};

struct DirectAggregateCallReturn {
  HirDirectCallExpression call;
  HirNominalAggregateExpression aggregate;
  HirLocalBinding local;
};

struct DirectExistentialCallReturn {
  HirDirectCallExpression call;
  HirDirectCallExpression initializerCall;
  HirLocalBinding local;
};

struct DirectCallReturn {
  HirDirectCallExpression call;
};

struct ErrorPropagateCallReturn {
  HirDirectCallExpression call;
};

struct ErrorUnwrapCallReturn {
  HirDirectCallExpression call;
  /// Computed during dispatch (not construction) via internPrimitiveType.
  zc::Maybe<identity::SemanticTypeId> errorUnwrapTagType;
  zc::Maybe<identity::SemanticTypeId> errorUnwrapBoolType;
};

struct DirectCallInitializerReturn {
  HirDirectCallExpression call;
  HirLocalBinding local;
  HirLocalReferenceExpression localReference;
};

struct ReceiverCallReturn {
  HirReceiverCallExpression receiverCall;
  HirLocalBinding local;
  zc::Maybe<HirNominalAggregateExpression> aggregate;
  zc::Maybe<checker::checked::CanonicalConstValue> literal;
  HirLocalReferenceExpression localReference;
};

struct ParameterReceiverCallReturn {
  HirReceiverCallExpression receiverCall;
  identity::CallableParameterKey receiverCallParameterKey;
  identity::SourceSpan receiverCallParameterSpan;
};

struct ReceiverSelfCallReturn {
  HirReceiverCallExpression receiverSelfCall;
};

struct DiscardedReceiverCall {
  HirReceiverCallExpression statementReceiverCall;
  HirLocalReferenceExpression statementReceiverReference;
  HirReceiverCallExpression receiverCall;
  HirLocalBinding local;
  HirNominalAggregateExpression aggregate;
  HirLocalReferenceExpression localReference;
};

struct DynFieldAccessReturn {
  PendingDynFieldAccessReturn dynFieldAccessReturn;
};

struct ReceiverFieldReturn {
  HirParameterFieldProjectionExpression parameterFieldProjection;
};

struct ByValueParameterFieldReturn {
  HirParameterFieldProjectionExpression parameterFieldProjection;
};

struct ReceiverFieldWriteReturn {
  HirParameterFieldProjectionExpression parameterFieldProjection;
  HirParameterFieldWriteStatement parameterFieldWrite;
  checker::checked::CanonicalConstValue parameterFieldWriteLiteral;
};

struct ReceiverFieldArithmeticReturn {
  PendingReceiverFieldArithmetic receiverFieldArithmetic;
};

struct ConditionalReturn {
  PendingConditionalReturn conditionalReturn;
};

struct ChainedConditionalReturn {
  PendingChainedConditionalReturn chainedConditionalReturn;
};

struct LeadingLocalConditionalReturn {
  PendingLeadingLocalConditionalReturn leadingLocalConditionalReturn;
};

struct LoopReturn {
  PendingLoopReturn loopReturn;
};

struct LoopBodyReturn {
  PendingLoopBodyReturn loopBodyReturn;
  HirLocalBinding local;
  zc::Maybe<checker::checked::CanonicalConstValue> literal;
  zc::Maybe<HirParameterReferenceExpression> parameterReference;
  zc::Vector<HirLocalWriteStatement> localWrites;
  zc::Vector<PendingLocalWriteValue> localWriteValues;
  HirLocalReferenceExpression localReference;
};

struct ComparisonLoopBodyReturn {
  PendingComparisonLoopBodyReturn comparisonLoopBodyReturn;
  HirLocalBinding local;
  zc::Maybe<checker::checked::CanonicalConstValue> literal;
  zc::Maybe<HirParameterReferenceExpression> parameterReference;
  zc::Vector<HirLocalWriteStatement> localWrites;
  zc::Vector<PendingLocalWriteValue> localWriteValues;
  HirLocalReferenceExpression localReference;
};

struct ComparisonReturn {
  PendingEqualityCondition comparisonReturn;
};

struct PrimitiveUnaryReturn {
  PendingEqualityCondition comparisonReturn;
  HirLocalBinding local;
  zc::Maybe<checker::checked::CanonicalConstValue> literal;
};

struct ForLoopReturn {
  PendingForLoopReturn forLoopReturn;
  checker::checked::CanonicalConstValue literal;
};

struct ForLoopAccumulatorReturn {
  PendingForLoopAccumulatorReturn forLoopAccumulatorReturn;
};

struct NestedForLoopAccumulatorReturn {
  PendingNestedForLoopAccumulatorReturn nestedForLoopAccumulatorReturn;
};

struct ForInLoopAccumulatorReturn {
  PendingForInLoopAccumulatorReturn forInLoopAccumulatorReturn;
};

struct SequentialLocalReturn {
  PendingSequentialLocalReturn sequentialLocalReturn;
};

/// Catch-all shape for bodies that no specialized guard claims. The inline
/// generic materializer reads these fields to emit HIR nodes directly. Every
/// field combination that reaches the materializer lands here; new specialized
/// variants should be extracted as their guards land.
struct GenericShape {
  zc::Maybe<checker::checked::CanonicalConstValue> literal;
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
  zc::Maybe<PendingEqualityCondition> comparisonReturn;
  bool returnsErrorUnwrapCall = false;
};

/// The tagged union of all function body shapes.
using PendingFunctionShape = zc::OneOf<
    EmptyVoidBody, VoidLetBody, DestructorFieldWriteBody, DestructorMethodCallBody,
    VoidMutatingMethod, ScalarLiteralReturn, UnsafeScalarReturn, ArrayIndexFoldReturn,
    ArrayLengthFoldReturn, LocalReturn, LocalWriteReturn, LocalFieldProjectionReturn,
    LocalBorrowReturn, ParameterReferenceReturn, ParameterIndexReturn, ParameterReborrowReturn,
    LocalAliasReborrowReturn, RawDereferenceReturn, RawPointerWriteReturn, AggregateReturn,
    EnumConstructionReturn, ExistentialReturn, AggregateFieldProjectionReturn,
    DirectScalarLocalCallReturn, DirectAggregateCallReturn, DirectExistentialCallReturn,
    DirectCallReturn, ErrorPropagateCallReturn, ErrorUnwrapCallReturn, DirectCallInitializerReturn,
    ReceiverCallReturn, ParameterReceiverCallReturn, ReceiverSelfCallReturn, DiscardedReceiverCall,
    DynFieldAccessReturn, ReceiverFieldReturn, ByValueParameterFieldReturn,
    ReceiverFieldWriteReturn, ReceiverFieldArithmeticReturn, ConditionalReturn,
    ChainedConditionalReturn, LeadingLocalConditionalReturn, LoopReturn, LoopBodyReturn,
    ComparisonLoopBodyReturn, ComparisonReturn, PrimitiveUnaryReturn, ForLoopReturn,
    ForLoopAccumulatorReturn, ForInLoopAccumulatorReturn, NestedForLoopAccumulatorReturn,
    SequentialLocalReturn, GenericShape>;

/// A pending function declaration ready for HIR materialization.
///
/// The common header fields are set for every function. The `shape` variant
/// carries only the fields the specific body shape needs, replacing the
/// former 60+ Maybe fields that required field-presence checks at every
/// dispatch site.
struct PendingFunctionDeclaration final {
  identity::DefId definition;
  /// The AST node of the function/method declaration this pending record
  /// lowers. Threaded from the binder definition so the HIR side-table can
  /// anchor each function's node range to its source.
  ast::NodeId sourceNode{};
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
  zc::Array<uint8_t> orderingKey;
  /// The AST BlockStmt node of the function body, threaded to the HIR
  /// side-table so the per-node correspondence validator can anchor it.
  ast::NodeId bodyNode{};
  /// The AST ReturnStmt node of the trailing return. Empty for void bodies.
  ast::NodeId returnNode{};
  /// The AST expression node of the return value. Empty for void bodies.
  ast::NodeId returnValueNode{};
  /// Set when the return expression is wrapped in an admitted unsafe block.
  /// A source-context property shared by every shape, not shape-specific.
  zc::Maybe<identity::SourceSpan> unsafeBlockSpan;

  /// The body shape variant — discriminates which lowering helper to call
  /// and carries only the fields that shape needs.
  PendingFunctionShape shape;
};

}  // namespace detail
}  // namespace zomlang::compiler::hir
