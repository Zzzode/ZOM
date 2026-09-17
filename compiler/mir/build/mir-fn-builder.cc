// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/mir/build/mir-fn-builder.h"

#include "compiler/ir/diagnostics/ir-failure.h"

namespace zomlang::compiler::mir {
namespace detail {

MirSourceScopeId MirFnCtx::pushRootScope(identity::SourceSpan sourceSpan) {
  const auto id = MirSourceScopeId::fromOrdinal(nextScope);
  ZC_IF_SOME(value, id) {
    ++nextScope;
    zc::Maybe<MirSourceScopeId> noParent;
    scopes.add(MirSourceScope{value, zc::mv(noParent), zc::mv(sourceSpan)});
    return value;
  }
  ZC_UNREACHABLE
}

MirLocalId MirFnCtx::declareLocal(MirLocalKind kind, identity::SemanticTypeId type,
                                  MirSourceScopeId scope, identity::SourceSpan sourceSpan) {
  const auto id = MirLocalId::fromOrdinal(nextLocal);
  ZC_IF_SOME(value, id) {
    ++nextLocal;
    locals.add(MirLocalDeclaration{value, kind, type, scope, zc::mv(sourceSpan)});
    return value;
  }
  ZC_UNREACHABLE
}

MirBlockId MirFnCtx::beginBlock(MirSourceScopeId scope) {
  const auto id = MirBlockId::fromOrdinal(nextBlock);
  ZC_IF_SOME(value, id) {
    ++nextBlock;
    blocks.add(PendingBlock{value, scope, zc::Vector<MirStatement>{}, zc::none});
    return value;
  }
  ZC_UNREACHABLE
}

void MirFnCtx::appendStatement(MirStatement statement) {
  ZC_IREQUIRE(!blocks.empty(), "MIR statement requires an open block");
  ZC_IREQUIRE(blocks.back().terminator == zc::none, "MIR statement appended after terminator");
  blocks.back().statements.add(zc::mv(statement));
}

void MirFnCtx::terminateBlock(MirTerminator terminator) {
  ZC_IREQUIRE(!blocks.empty(), "MIR terminator requires an open block");
  ZC_IREQUIRE(blocks.back().terminator == zc::none, "MIR block terminated twice");
  blocks.back().terminator = zc::mv(terminator);
}

MirFunction MirFnCtx::finish(identity::DefId owner, MirFunctionKind kind,
                             identity::DefinitionKind sourceDefinitionKind,
                             identity::SemanticTypeId resultType, identity::SourceSpan sourceSpan) {
  zc::Vector<MirBasicBlock> built;
  for (auto& block : blocks) {
    ZC_IREQUIRE(block.terminator != zc::none, "MIR block finished without a terminator");
    built.add(MirBasicBlock{block.id, block.sourceScope, zc::mv(block.statements),
                            zc::mv(ZC_ASSERT_NONNULL(block.terminator))});
  }
  return MirFunction{
      owner,          kind,           sourceDefinitionKind, resultType, zc::mv(sourceSpan),
      zc::mv(scopes), zc::mv(locals), zc::mv(built)};
}

}  // namespace detail
}  // namespace zomlang::compiler::mir
