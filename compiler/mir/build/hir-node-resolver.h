// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "compiler/hir/hir-module.h"
#include "compiler/hir/hir-node-id.h"
#include "zc/core/common.h"

namespace zomlang::compiler::mir {

/// \brief Identifies the record kind of an HIR node.
///
/// The HIR module stores records in ~20 separate pools, each indexed by
/// HirNodeId. A recursive walker must dispatch on node kind for every node
/// it visits. This enum provides a single kind tag, resolved by
/// lookupHirNodeKind below via linear scan over the pools.
enum class HirNodeKind : uint8_t {
  Function,
  Block,
  Return,
  ScalarLiteral,
  Aggregate,
  ErrorUnion,
  LocalBinding,
  LocalWrite,
  LocalReference,
  LocalFieldProjection,
  ParameterFieldProjection,
  ParameterFieldWrite,
  RawPointerWrite,
  ParameterReference,
  ParameterReborrow,
  LocalBorrow,
  DirectCall,
  ReceiverCall,
  UnsafeBlock,
  PrimitiveBinary,
  Conditional,
  Loop,
};

/// \brief Resolves the record kind of an HIR node by linear scan over the
/// module's pools. Returns none if the node is not found in any pool.
///
/// This is O(n) per lookup (where n is the total record count) but
/// acceptable for a builder; a hash map can be added later if needed.
zc::Maybe<HirNodeKind> lookupHirNodeKind(const hir::VerifiedHirModule& module,
                                          hir::HirNodeId node);

}  // namespace zomlang::compiler::mir
