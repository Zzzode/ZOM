// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "compiler/ast/tree.h"
#include "compiler/hir/build/hir-pending.h"
#include "compiler/hir/hir-module.h"
#include "compiler/hir/hir-node-id.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::hir {
namespace detail {

/// \brief Per-function append-only HIR construction context (RFC 0048).
///
/// Mirrors the rustc THIR-to-MIR function builder: one monotonic node-id
/// allocator plus the record pools the recursive lowering arms append into.
/// Each destination-driven lowering arm allocates its node id at entry and
/// recursively lowers its operands, so node ids follow source preorder without
/// fixed strides. Pools are wired in as each family arm needs them; every pool
/// reference stays non-null for the whole materialization pass.
class HirFnCtx final {
public:
  HirFnCtx(uint32_t& nextNode, zc::Vector<HirFunctionDeclaration>& functions,
           zc::Vector<HirBlockStatement>& blocks, zc::Vector<HirReturnStatement>& returns,
           zc::Vector<HirScalarLiteralExpression>& expressions,
           zc::Vector<HirParameterReferenceExpression>& parameterReferences,
           zc::Vector<HirLocalBinding>& locals, zc::Vector<HirLocalWriteStatement>& localWrites,
           zc::Vector<HirLocalReferenceExpression>& localReferences,
           zc::Vector<HirPrimitiveBinaryExpression>& primitiveBinaryOperations,
           zc::Vector<HirNominalAggregateExpression>& aggregates,
           zc::Vector<HirLocalFieldProjectionExpression>& localFieldProjections,
           zc::Vector<HirParameterFieldProjectionExpression>& parameterFieldProjections,
           zc::Vector<HirParameterFieldWriteStatement>& parameterFieldWrites,
           zc::Vector<HirUnsafeBlockExpression>& unsafeBlocks,
           zc::Vector<HirParameterReborrowExpression>& parameterReborrows,
           zc::Vector<HirLocalBorrowExpression>& localBorrows,
           zc::Vector<HirDirectCallExpression>& calls,
           zc::Vector<HirReceiverCallExpression>& receiverCalls,
           zc::Vector<HirConditionalExpression>& conditionals, zc::Vector<HirLoopStatement>& loops,
           zc::Vector<ast::NodeId>& sourceNodes) noexcept;

  /// \brief Allocates the next deterministic source-preorder node id and
  /// records its source AST node in the side-table. A default-constructed
  /// ast::NodeId (value 0) marks a synthetic node with no AST correspondence.
  HirNodeId allocNode(ast::NodeId source);

  void addFunction(HirFunctionDeclaration declaration);
  void addBlock(HirBlockStatement block);
  void addReturn(HirReturnStatement statement);
  void addExpression(HirScalarLiteralExpression expression);
  void addParameterReference(HirParameterReferenceExpression reference);
  void addLocal(HirLocalBinding local);
  void addLocalWrite(HirLocalWriteStatement write);
  void addLocalReference(HirLocalReferenceExpression reference);
  void addPrimitiveBinary(HirPrimitiveBinaryExpression operation);
  void addAggregate(HirNominalAggregateExpression aggregate);
  void addLocalFieldProjection(HirLocalFieldProjectionExpression projection);
  void addParameterFieldProjection(HirParameterFieldProjectionExpression projection);
  void addParameterFieldWrite(HirParameterFieldWriteStatement write);
  void addUnsafeBlock(HirUnsafeBlockExpression block);
  void addParameterReborrow(HirParameterReborrowExpression reborrow);
  void addLocalBorrow(HirLocalBorrowExpression borrow);
  void addDirectCall(HirDirectCallExpression call);
  void addReceiverCall(HirReceiverCallExpression call);
  void addConditional(HirConditionalExpression conditional);
  void addLoop(HirLoopStatement loop);

  /// \brief Lowers one scalar literal-or-parameter arm leaf into its
  /// destination id. Used by every binary operand and condition arm.
  void lowerArmLeaf(HirNodeId destination, const PendingConditionalArm& leaf);

  /// \brief Allocates and lowers one conditional arm, returning the node id of
  /// its value. A leaf arm (literal, parameter, or local reference) allocates
  /// one node; a binary arm allocates three (left operand, right operand,
  /// binary) and returns the binary node id as the arm's value.
  HirNodeId lowerArmValue(const PendingConditionalArm& arm);

private:
  uint32_t* nextNode;
  zc::Vector<HirFunctionDeclaration>* functions;
  zc::Vector<HirBlockStatement>* blocks;
  zc::Vector<HirReturnStatement>* returns;
  zc::Vector<HirScalarLiteralExpression>* expressions;
  zc::Vector<HirParameterReferenceExpression>* parameterReferences;
  zc::Vector<HirLocalBinding>* locals;
  zc::Vector<HirLocalWriteStatement>* localWrites;
  zc::Vector<HirLocalReferenceExpression>* localReferences;
  zc::Vector<HirPrimitiveBinaryExpression>* primitiveBinaryOperations;
  zc::Vector<HirNominalAggregateExpression>* aggregates;
  zc::Vector<HirLocalFieldProjectionExpression>* localFieldProjections;
  zc::Vector<HirParameterFieldProjectionExpression>* parameterFieldProjections;
  zc::Vector<HirParameterFieldWriteStatement>* parameterFieldWrites;
  zc::Vector<HirUnsafeBlockExpression>* unsafeBlocks;
  zc::Vector<HirParameterReborrowExpression>* parameterReborrows;
  zc::Vector<HirLocalBorrowExpression>* localBorrows;
  zc::Vector<HirDirectCallExpression>* calls;
  zc::Vector<HirReceiverCallExpression>* receiverCalls;
  zc::Vector<HirConditionalExpression>* conditionals;
  zc::Vector<HirLoopStatement>* loops;
  zc::Vector<ast::NodeId>* sourceNodes;
};

/// \brief Lowers one tagged scalar-return function through the recursive
/// destination-driven driver: function -> block -> return -> scalar expression.
/// The pending record is consumed exactly once; move-only fields are moved.
void lowerScalarReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

/// \brief Lowers one single initialized-local function:
/// `let x: T = <literal | parameter>; return x;` through the recursive driver:
/// function -> block -> (local binding, return) with the initializer lowered
/// into the binding's destination and the returned place lowered into the
/// return destination.
void lowerLocalReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

/// \brief Lowers one sequential N-local body (`N >= 2` leading let bindings
/// followed by a parameter/local return) through the recursive driver. Covers
/// literal, aggregate, parameter/local-reference, and primitive-binary
/// initializers, including a one-level nested binary operand (`a + b * c`).
void lowerSequentialLocalReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

/// \brief Lowers one local field-projection return whose local is initialized
/// by a nominal aggregate: `let cell = T {...}; return cell.field;`.
void lowerAggregateFieldProjectionFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

/// \brief Lowers one shared-receiver field-read method body:
/// `fn m(this) -> T { return this.field; }` through the recursive driver:
/// function -> block -> return -> parameter field projection. The receiver
/// moves into the function header; the projection keys on its parameter key.
void lowerReceiverFieldReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

/// \brief Lowers one mutating-receiver write-read method body:
/// `fn m(this) -> T { this.field = <scalar>; return this.field; }`. Six node
/// ids in source preorder: function, body, write, write value literal, return,
/// returned parameter field projection.
void lowerReceiverFieldWriteReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

/// \brief Lowers one mutable-local write body: one initialized mut local, one
/// or more non-field scalar/parameter/binary writes, and a local-reference
/// return (`mut x: T = <leaf>; x = <value>; ..; return x;`).
void lowerLocalWriteFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

/// \brief Lowers one if/else conditional return through the recursive driver:
/// the condition is a bare bool parameter reference or an `a CMP b` comparison
/// of two literal/parameter operands, and each branch returns a scalar literal
/// or parameter. Node strides: seven ids for a parameter condition, nine for a
/// comparison condition.
void lowerConditionalReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

/// \brief Lowers a chained integer match return (N literal arms plus one
/// default arm) through the recursive destination-driven driver. Each literal
/// arm synthesizes an equality comparison (scrutinee == literal) without an
/// AST BinaryExpr node; the arms nest as a chain of conditional expressions.
/// Node stride is 4 + 5N.
void lowerChainedConditionalReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

/// \brief Lowers one empty-body `while` loop followed by a scalar return
/// through the recursive driver. Node stride: function, body, condition
/// parameter reference, return literal, loop, return (six ids).
void lowerLoopReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

/// \brief Lowers one loop-body composite through the recursive driver: one
/// initialized mut local, an admitted `while` whose body writes that local
/// (literal/parameter/binary write values), and a local-reference return. The
/// loop condition and loop statement allocate in a trailing region after the
/// flat mut-local ids, matching the generic materializer.
void lowerLoopBodyReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx);

}  // namespace detail
}  // namespace zomlang::compiler::hir
