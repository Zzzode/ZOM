// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/ast/tree.h"
#include "compiler/checker/operator-kind.h"
#include "compiler/hir/hir-module.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::hir {
namespace detail {

// One initializer kind admitted for a sequential local binding. A reference is
// discriminated further by whether it names an earlier local or a parameter. A
// primitive binary operation carries two operands (see PendingSequentialBinary).
enum class SequentialInitializerKind : uint8_t {
  Literal,
  Aggregate,
  LocalReference,
  ParameterReference,
  PrimitiveBinary,
  PrimitiveUnary,
  Cast,
  Ternary,
  EnumVariant,
  EnumVariantConstruction,
  FoldedStringLength
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
  // Populated for PrimitiveUnary: the desugared binary operation and the real
  // operand classification (parameter or earlier local). The synthetic operand
  // has no AST node and is derived by the builder from the operation + type.
  zc::Maybe<checker::PrimitiveOperation> unaryOperation;
  zc::Maybe<SequentialBinaryOperand> unaryOperand;
  // Populated for Cast: the inner expression node of the `as` cast. The cast
  // target type is derived from the initializer's node-type fact.
  ast::NodeId castInnerNode;
  // Populated for Ternary: the condition, then-branch, and else-branch AST
  // nodes. The condition is a bool reference or a bool literal; both branches
  // are scalar literals of the same type. `ternaryConditionIsLocal` is true
  // when the condition names an earlier local (rather than a parameter);
  // `ternaryConditionIsLiteral` is true when the condition is a bool literal.
  ast::NodeId ternaryCondNode;
  ast::NodeId ternaryThenNode;
  ast::NodeId ternaryElseNode;
  bool ternaryConditionIsLocal = false;
  bool ternaryConditionIsLiteral = false;
  // True when this Ternary binding is normalized from a boolean match
  // expression that carries a default (wildcard) arm. The default arm body is
  // semantically unreachable for bool (true+false cover all bool values) and is
  // dropped during normalization, but the checker still produces node-type and
  // literal facts for it, which the count equations must credit.
  bool matchHasDefaultArm = false;
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

// K leading scalar-local bindings followed by one comparison conditional
// return. Each binding initializer is a scalar literal, a parameter copy, or a
// copy of an earlier binding (only the reference part of
// SequentialLocalBinding is populated); the trailing IfStmt is a relational
// comparison whose arms each return a scalar literal.
struct LeadingLocalConditionalShape final {
  ast::NodeId body;
  ast::NodeId ifStatement;
  zc::Vector<SequentialLocalBinding> bindings;
};

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
  // Single-statement free-function shape: `return <ordinary-parameter>.<field>;`
  // reading one field of a by-value struct parameter with one field projection.
  bool returnsParameterField = false;
  // Single-statement inherent-method shape: `return this.<field>;` read through
  // the implicit shared receiver parameter with one field projection.
  bool returnsReceiverField = false;
  // Single-statement inherent-method shape: `return this.<method>();` forwarding
  // the implicit shared receiver parameter to a zero-argument method of the same
  // owner. The call node is `value`; the callee member is derived from it.
  bool returnsReceiverSelfCall = false;
  // Two-statement mutating-method shape: `this.<field> = <scalar literal>;`
  // followed by `return this.<field>;` through the mutable receiver. The write
  // statement, its assignment, and the written literal nodes are carried for
  // the builder; the returned field uses `value` and returnsReceiverField.
  bool writesReceiverField = false;
  ast::NodeId receiverWriteStatement;
  ast::NodeId receiverWriteAssignment;
  ast::NodeId receiverWriteValue;
  bool returnsLocalReborrow = false;
  // Sequential-local shape: N leading `let id: T = <literal | aggregate |
  // identifier>;` statements followed by `return <identifier>;`. Per-binding
  // detail is derived from the block node on demand via sequentialLocalShape so
  // this struct stays copyable; only the discriminator is stored here.
  bool isSequentialLocalReturn = false;
  bool returnsReceiverCall = false;
  // Two-statement by-value struct call shape: a leading aggregate-initialized
  // local passed as the sole argument of a returned direct free-function call:
  // `let p: P = P { .. }; return f(p);`.
  bool returnsDirectAggregateCall = false;
  // Two-statement scalar-local call shape: a leading i32 local initialized from
  // an integer literal passed as the sole argument of a returned direct
  // free-function call: `let a: i32 = 21; return f(a);`.
  bool returnsDirectScalarLocalCall = false;
  bool returnsLocalBorrow = false;
  zc::Maybe<ast::NodeId> unsafeBlock;
  bool isConditional = false;
  // Leading scalar-local bindings plus a trailing comparison conditional:
  // `let id = <literal | parameter | earlier local>; ...; if (a CMP b) { return
  // <literal>; } else { return <literal>; }` with one or more leading bindings.
  // Per-binding layout is derived on demand via leadingLocalConditionalShape.
  bool isLeadingLocalConditional = false;
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
  // When one comparison operand is itself a one-level arithmetic binary (e.g.
  // `a + 1 < 5`), the corresponding flag is set and these fields carry the
  // arithmetic operator's two leaf operands. The HIR builder synthesizes one
  // leading local binding for the arithmetic result, then compares that local.
  bool conditionLeftIsNestedArithmetic = false;
  bool conditionRightIsNestedArithmetic = false;
  ast::NodeId nestedArithmeticLeft;
  ast::NodeId nestedArithmeticRight;
  bool nestedArithmeticLeftIsLiteral = false;
  bool nestedArithmeticRightIsLiteral = false;
  // When the condition is a unary `!x` expression, the condition node is a
  // UnaryExpression and this holds its operand. The operand is an IdentExpr
  // parameter reference or a scalar literal. The HIR builder desugars the
  // unary to an equivalent equality comparison, reusing the comparison
  // condition path.
  bool conditionIsUnary = false;
  ast::NodeId conditionUnaryOperand;
  bool conditionUnaryOperandIsLiteral = false;
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
  // When the body is a single `return <UnaryExpression>` of one scalar operand,
  // `shape.value` is the UnaryExpression node and these hold its operand. The
  // operand is an IdentExpr parameter reference or a scalar literal. The
  // selected unary operator comes from the checked call fact, not from the
  // shape. The HIR builder desugars the unary operation to an equivalent binary
  // operation, reusing the comparison-return materialization path.
  bool returnsUnary = false;
  ast::NodeId unaryOperand;
  bool unaryOperandIsLiteral = false;
  // When the body is a single `return "a" + "b"` of two string literals, the
  // body checker folds the concatenation to a string constant at compile time.
  // `shape.value` is the BinaryExpr node; the builder looks up its checked
  // literal fact and lowers the return to a scalar literal, reusing the
  // string-literal-return path.
  bool returnsFoldedStringConcat = false;
  // When the body is a single `return <float-literal> as <integer-primitive>;`,
  // the body checker folds the cast to an integer constant at compile time.
  // `shape.value` is the CastExpression node; the builder looks up its checked
  // literal fact and lowers the return to a scalar literal, reusing the
  // literal-return path.
  bool returnsFoldedFloatCast = false;
  // Single-statement shared-receiver method shape:
  // `return this.<field> OP <scalar literal>;` (or the mirrored operand order)
  // reading the field through the implicit receiver with one field projection.
  // There are no ordinary parameters. The non-field operand must be a scalar
  // literal; parameter and nested operands keep their own shapes.
  bool returnsReceiverFieldArithmetic = false;
  bool isLoop = false;
  ast::NodeId loopCondition{};
  ast::NodeId loopStatement{};
  // Loop-body composite shape: a leading `mut` local declaration, an admitted
  // `while` whose body writes that local, and a trailing local return. Reuses
  // the mut-local fields (localPattern, localInitializer, localWrites,
  // localReference, returnsLocal) and adds only the loop discriminator, the loop
  // condition node, and the loop statement node.
  bool isLoopBody = false;
  // Loop-body composite: when the loop body's trailing statement is an unlabeled
  // `break;` or `continue;`, these record its AST node (empty otherwise). At
  // most one is set; both are empty when the body ends with a write, so the
  // back-edge falls through to the header. `localWrites` covers only the write
  // prefix, excluding the trailing break/continue.
  ast::NodeId loopBodyBreak{};
  ast::NodeId loopBodyContinue{};
  // For-loop shape: a C-style `for (let id = <lit>; <ident> <cmp> <lit>;
  // <ident> = <binary>) {}` followed by a scalar return. The for-loop
  // desugars to a leading local binding plus a loop whose condition is the
  // comparison and whose body is the update write. The init, cond, update,
  // and body AST nodes are carried for the builder.
  bool isForLoop = false;
  ast::NodeId forLoopInit{};
  ast::NodeId forLoopCond{};
  ast::NodeId forLoopUpdate{};
  ast::NodeId forLoopBody{};
  // The ForStmt node itself, carried for the loop source span.
  ast::NodeId forLoopStatement{};
  // For-loop accumulator shape: N leading scalar `mut` accumulator locals, a
  // C-style `for` loop whose body writes each accumulator, and a trailing
  // `return <accumulator-local>;`. Reuses the for-loop fields. The accumulator
  // patterns, their scalar-literal initializers, and the body write AST nodes
  // are carried for the builder. The return names the first accumulator.
  bool isForLoopAccumulator = false;
  zc::Vector<ast::NodeId> forLoopAccumulatorPatterns;
  zc::Vector<ast::NodeId> forLoopAccumulatorInitializers;
  zc::Vector<ast::NodeId> forLoopBodyWrites;
  // For-loop accumulator composite: when the loop body's trailing statement is
  // an unlabeled `break;` or `continue;`, these record its AST node (empty
  // otherwise). At most one is set; both are empty when the body ends with a
  // write, so the back-edge falls through to the header. `forLoopBodyWrites`
  // covers only the write prefix, excluding the trailing break/continue.
  ast::NodeId forLoopBodyBreak{};
  ast::NodeId forLoopBodyContinue{};
  // For-loop accumulator if-guarded break: when the loop body's first statement
  // is `if (<cond>) { break; }`, this records the condition comparison AST node
  // (empty otherwise). The guard evaluates before the accumulator writes in
  // source order, so the builder resolves it to a break-condition comparison
  // and the MIR builder lowers a guard block. `forLoopBodyWrites` covers only
  // the write suffix, excluding the leading guarded break.
  ast::NodeId forLoopBodyBreakCondition{};
  // Nested for-loop accumulator shape: N leading scalar `mut` accumulator
  // locals, an outer C-style `for` loop whose sole body statement is an inner
  // C-style `for` loop that writes each accumulator, and a trailing
  // `return <accumulator-local>;`. The outer loop reuses the for-loop fields
  // (forLoopInit, forLoopCond, forLoopUpdate, forLoopBody, forLoopStatement);
  // the inner loop nodes are carried here. The accumulator patterns,
  // initializers, and body writes reuse the forLoopAccumulator* fields.
  bool isNestedForLoopAccumulator = false;
  ast::NodeId nestedForLoopInnerInit{};
  ast::NodeId nestedForLoopInnerCond{};
  ast::NodeId nestedForLoopInnerUpdate{};
  ast::NodeId nestedForLoopInnerBody{};
  ast::NodeId nestedForLoopInnerStatement{};
  // Void mutating-method shape: the sole statement is
  // `this.<field> = <ordinary-parameter>;` with no return. The callable result
  // is Unit; there is no return statement or return value. The write statement,
  // its assignment, and the parameter RHS reuse receiverWriteStatement,
  // receiverWriteAssignment, and receiverWriteValue.
  bool isVoidBody = false;
  // True in a void body when the sole write RHS is an ordinary parameter
  // reference (IdentExpr) rather than a scalar literal. Resolving it to the
  // parameter is a builder decision; shape only records the structure.
  bool voidWriteValueIsParameter = false;
  // Function shape: a leading aggregate-initialized owner local, one discarded
  // receiver-call expression statement on that local, and a trailing
  // `return <owner-local>.<method>(...);`. This slice admits exactly one
  // intermediate statement (three statements total). The statement node is
  // carried; the trailing return routes through returnsReceiverCall.
  bool hasDiscardedReceiverCallStatement = false;
  ast::NodeId discardedCallStatement;
  // Match-return shape: a single `match (b) { when true => return <lit>; when
  // false => return <lit>; }` statement. The scrutinee is a bare identifier
  // (a bool parameter reference); the two arms tail-return scalar literals.
  // One arm may instead be a `default` (wildcard) arm covering the remaining
  // bool value. The HIR builder lowers this to the same conditional path as a
  // bare-parameter `if`, reusing `condition` (the scrutinee),
  // `thenReturnValue` (the true arm's return value), and `elseReturnValue`
  // (the false arm's). `matchStatement` carries the MatchStmt node for the
  // conditional source span.
  bool isMatchReturn = false;
  ast::NodeId matchStatement;
  // True when the match-return shape has a default (wildcard) arm instead of
  // a second literal arm. The default arm produces one fewer pattern-literal
  // fact, which the count equations must subtract.
  bool matchHasDefaultArm = false;
  // True when the match-return shape is an integer match with one literal
  // pattern arm and one default arm. The HIR builder lowers this to the
  // equality conditional path, synthesizing `scrutinee == literal` without
  // an AST BinaryExpr node. `matchEqualityLiteral` carries the pattern
  // literal node so the builder can read its checked literal fact.
  bool isMatchEquality = false;
  ast::NodeId matchEqualityLiteral{};
  // True when the match-return shape is an integer match with two or more
  // literal pattern arms and one default arm. The HIR builder lowers this to
  // a chained conditional: `if scrutinee == lit0 then val0 else if scrutinee
  // == lit1 then val1 ... else defaultValue`. Each entry in
  // `matchChainedLiterals` carries the pattern literal node (for the builder
  // to read its checked literal fact) and the corresponding entry in
  // `matchChainedThenValues` carries that arm's return value.
  // `matchChainedElseValue` carries the default arm's return value.
  bool isMatchChainedEquality = false;
  zc::Vector<ast::NodeId> matchChainedLiterals;
  zc::Vector<ast::NodeId> matchChainedThenValues;
  ast::NodeId matchChainedElseValue{};
  // True when the match-return shape is an enum match with N (>= 2)
  // unit-variant pattern arms. Two arms lower to the equality conditional
  // path; three or more lower to the chained conditional path with the last
  // arm as the else branch. `matchEqualityLiteral` carries the first
  // EnumPattern node (two-arm case) so the builder can read its checked
  // literal fact (the variant discriminant); `matchChainedLiterals` carries
  // the first N-1 EnumPattern nodes (chained case).
  bool isMatchEnum = false;
  // True when the match-return shape has a guard on the literal (then) arm.
  // The guard is a single binary expression whose one operand is a bare
  // identifier (a parameter reference) and whose other operand is a scalar
  // literal. The HIR builder lowers the match to a conjunctive condition
  // (scrutinee AND guard) keeping the four-block diamond CFG, so the guard
  // node is carried for the builder to read its checked call and node-type
  // facts. Only bool-literal matches admit a guard; integer matches do not.
  bool hasMatchGuard = false;
  ast::NodeId matchGuard{};
};

zc::Maybe<ast::NodeId> statementItem(const ast::Tree& tree, ast::NodeId statement);

zc::Maybe<ast::NodeId> reborrowReference(const ast::Tree& tree, ast::NodeId expression);

// Classifies a function body as the sequential N-local shape: N (>= 2) leading
// `let id: T = <literal | aggregate | identifier>;` statements followed by a
// single `return <identifier>;`. Each identifier initializer names an earlier
// local or a parameter; the return names one of the locals or a parameter.
// Returns none for any other body. The result is recomputed from the AST wher-
// ever the per-binding layout is needed, keeping FunctionReturnShape copyable
// and guaranteeing the producer and verifiers derive one identical layout.
zc::Maybe<SequentialLocalShape> sequentialLocalShape(const ast::Tree& tree, ast::NodeId body);

// Computes the backward-liveness dead set for a sequential-local shape. A
// binding is live when it is the returned local or is referenced (directly or
// transitively) by a live binding's initializer. Every other binding is dead:
// it is written but never read, so skipping it during lowering changes no
// observable value. The returned vector has one entry per shape binding.
zc::Vector<bool> deadSequentialBindings(const ast::Tree& tree, const SequentialLocalShape& shape);

// Restricts `dead` to the erasure cone: a dead binding is removed only when its
// initializer is one of `erasedInitializers` (an admitted dead-erase coercion
// site) or it is referenced (transitively) by a removed binding. Unrelated dead
// scalar locals stay in the shape so ordinary sequential-local bodies lower
// unchanged. A removed binding that is still referenced by a kept binding is
// kept instead (and its references with it), so the remap is always
// well-defined; such a shape is rejected downstream by the LIR slice. The
// returned vector has one entry per shape binding.
zc::Vector<bool> erasureRelatedDeadBindings(const ast::Tree& tree,
                                            const SequentialLocalShape& shape,
                                            const zc::Vector<bool>& dead,
                                            const zc::Vector<ast::NodeId>& erasedInitializers);

// Returns a copy of `shape` with the bindings flagged in `removed` dropped and
// every binding-index field (LocalReference, binary/unary/ternary operands,
// returnsLocal) remapped to the filtered binding order. A kept binding never
// references a removed one, so the remap is always well-defined. The caller
// must reject the result when it carries fewer than two bindings, since the
// MIR sequential-local-return classifier requires at least two statements.
SequentialLocalShape filterDeadSequentialBindings(const SequentialLocalShape& shape,
                                                  const zc::Vector<bool>& removed);

// Classifies a function body as K (>= 1) leading scalar `let` bindings followed
// by one explicit-else `if` whose relational comparison condition reads
// identifier or literal operands and whose two arms tail-return scalar
// literals. Binding initializers are scalar literals, parameter references, or
// references to earlier bindings. Returns none for every other body.
zc::Maybe<LeadingLocalConditionalShape> leadingLocalConditionalShape(const ast::Tree& tree,
                                                                     ast::NodeId body);

zc::Maybe<FunctionReturnShape> functionReturnShape(const ast::Tree& tree,
                                                   const ast::Node& function);

}  // namespace detail
}  // namespace zomlang::compiler::hir
