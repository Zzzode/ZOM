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
           zc::Vector<HirLocalBinding>& locals,
           zc::Vector<HirLocalReferenceExpression>& localReferences,
           zc::Vector<HirPrimitiveBinaryExpression>& primitiveBinaryOperations) noexcept;

  /// \brief Allocates the next deterministic source-preorder node id.
  HirNodeId allocNode();

  void addFunction(HirFunctionDeclaration declaration);
  void addBlock(HirBlockStatement block);
  void addReturn(HirReturnStatement statement);
  void addExpression(HirScalarLiteralExpression expression);
  void addParameterReference(HirParameterReferenceExpression reference);
  void addLocal(HirLocalBinding local);
  void addLocalReference(HirLocalReferenceExpression reference);
  void addPrimitiveBinary(HirPrimitiveBinaryExpression operation);

  /// \brief Lowers one scalar literal-or-parameter arm leaf into its
  /// destination id. Used by every binary operand and condition arm.
  void lowerArmLeaf(HirNodeId destination, const PendingConditionalArm& leaf);

private:
  uint32_t* nextNode;
  zc::Vector<HirFunctionDeclaration>* functions;
  zc::Vector<HirBlockStatement>* blocks;
  zc::Vector<HirReturnStatement>* returns;
  zc::Vector<HirScalarLiteralExpression>* expressions;
  zc::Vector<HirParameterReferenceExpression>* parameterReferences;
  zc::Vector<HirLocalBinding>* locals;
  zc::Vector<HirLocalReferenceExpression>* localReferences;
  zc::Vector<HirPrimitiveBinaryExpression>* primitiveBinaryOperations;
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

}  // namespace detail
}  // namespace zomlang::compiler::hir
