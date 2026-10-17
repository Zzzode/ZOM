// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/mir/build/hir-node-resolver.h"

#include "compiler/hir/hir-internal.h"

namespace zomlang::compiler::mir {

using hir::HirNodeId;
using hir::VerifiedHirModule;

namespace {

/// Linear-scan helper: returns true if any record in the pool has the given
/// node id.
template <typename Record>
bool poolContains(zc::ArrayPtr<const Record> records, HirNodeId node) {
  for (const auto& record : records) {
    if (record.node == node) return true;
  }
  return false;
}

}  // namespace

zc::Maybe<HirNodeKind> lookupHirNodeKind(const VerifiedHirModule& module, HirNodeId node) {
  if (poolContains(module.functions(), node)) return HirNodeKind::Function;
  if (poolContains(module.blocks(), node)) return HirNodeKind::Block;
  if (poolContains(module.returns(), node)) return HirNodeKind::Return;
  if (poolContains(module.expressions(), node)) return HirNodeKind::ScalarLiteral;
  if (poolContains(module.aggregates(), node)) return HirNodeKind::Aggregate;
  if (poolContains(module.errorUnions(), node)) return HirNodeKind::ErrorUnion;
  if (poolContains(module.locals(), node)) return HirNodeKind::LocalBinding;
  if (poolContains(module.localWrites(), node)) return HirNodeKind::LocalWrite;
  if (poolContains(module.localReferences(), node)) return HirNodeKind::LocalReference;
  if (poolContains(module.localFieldProjections(), node)) return HirNodeKind::LocalFieldProjection;
  if (poolContains(module.parameterFieldProjections(), node))
    return HirNodeKind::ParameterFieldProjection;
  if (poolContains(module.parameterFieldWrites(), node)) return HirNodeKind::ParameterFieldWrite;
  if (poolContains(module.rawPointerWrites(), node)) return HirNodeKind::RawPointerWrite;
  if (poolContains(module.parameterReferences(), node)) return HirNodeKind::ParameterReference;
  if (poolContains(module.parameterReborrows(), node)) return HirNodeKind::ParameterReborrow;
  if (poolContains(module.localBorrows(), node)) return HirNodeKind::LocalBorrow;
  if (poolContains(module.calls(), node)) return HirNodeKind::DirectCall;
  if (poolContains(module.receiverCalls(), node)) return HirNodeKind::ReceiverCall;
  if (poolContains(module.unsafeBlocks(), node)) return HirNodeKind::UnsafeBlock;
  if (poolContains(module.primitiveBinaryOperations(), node)) return HirNodeKind::PrimitiveBinary;
  if (poolContains(module.conditionals(), node)) return HirNodeKind::Conditional;
  if (poolContains(module.loops(), node)) return HirNodeKind::Loop;
  return zc::none;
}

}  // namespace zomlang::compiler::mir
