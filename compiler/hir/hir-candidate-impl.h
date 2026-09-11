// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/hir/hir-module.h"

namespace zomlang::compiler::hir {

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

}  // namespace zomlang::compiler::hir
