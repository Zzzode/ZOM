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
  // Loop-body composite shape: a leading `mut` local declaration, an admitted
  // `while` whose body writes that local, and a trailing local return. Reuses
  // the mut-local fields (localPattern, localInitializer, localWrites,
  // localReference, returnsLocal) and adds only the loop discriminator, the loop
  // condition node, and the loop statement node.
  bool isLoopBody = false;
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

zc::Maybe<FunctionReturnShape> functionReturnShape(const ast::Tree& tree,
                                                   const ast::Node& function);

}  // namespace detail
}  // namespace zomlang::compiler::hir
