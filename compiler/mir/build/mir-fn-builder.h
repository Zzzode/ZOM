// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "compiler/mir/built-mir.h"
#include "zc/core/common.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::mir {
namespace detail {

/// \brief Per-function append-only recursive MIR construction context (RFC 0048 Phase 3).
///
/// Mirrors the rustc THIR-to-MIR function builder and the HIR-side HirFnCtx: one
/// monotonic dense allocator for 1-based local, block, and source-scope ids, an
/// append-only statement cursor over the block currently being built, and a
/// terminator set exactly once per block. MIR stays place-based (no block
/// parameters, no PHIs); values flow through slot locals.
///
/// This first slice wires only the pools the scalar/parameter return arms need:
/// one root source scope, parameter locals, one entry block, and a Return
/// terminator. The generic local/block allocators are already in place so later
/// families (function result, temporaries, multi-block control flow) add arms
/// without changing the allocation model.
class MirFnCtx final {
public:
  MirFnCtx() = default;
  ZC_DISALLOW_COPY(MirFnCtx);

  /// \brief Allocates the function root source scope with no parent.
  MirSourceScopeId pushRootScope(identity::SourceSpan sourceSpan);

  /// \brief Declares the next dense local on the given source scope.
  MirLocalId declareLocal(MirLocalKind kind, identity::SemanticTypeId type, MirSourceScopeId scope,
                          identity::SourceSpan sourceSpan);

  /// \brief Opens the next dense append-only basic block on the given scope.
  MirBlockId beginBlock(MirSourceScopeId scope);

  /// \brief Appends one statement to the current open, unterminated block.
  void appendStatement(MirStatement statement);

  /// \brief Sets the current block terminator exactly once and closes the block.
  void terminateBlock(MirTerminator terminator);

  /// \brief Assembles the finalized MirFunction, consuming every allocation.
  MirFunction finish(identity::DefId owner, MirFunctionKind kind,
                     identity::DefinitionKind sourceDefinitionKind,
                     identity::SemanticTypeId resultType, identity::SourceSpan sourceSpan);

private:
  struct PendingBlock final {
    MirBlockId id;
    MirSourceScopeId sourceScope;
    zc::Vector<MirStatement> statements;
    zc::Maybe<MirTerminator> terminator;
  };

  uint32_t nextLocal = 1;
  uint32_t nextBlock = 1;
  uint32_t nextScope = 1;
  zc::Vector<MirSourceScope> scopes;
  zc::Vector<MirLocalDeclaration> locals;
  zc::Vector<PendingBlock> blocks;
};

}  // namespace detail
}  // namespace zomlang::compiler::mir
