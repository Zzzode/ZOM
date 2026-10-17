// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/checker/body/marker-proof.h"
#include "compiler/checker/checker-identity-authority.h"
#include "compiler/hir/hir-module.h"
#include "compiler/mir/build/mir-fn-builder.h"
#include "compiler/mir/built-mir.h"
#include "zc/core/common.h"

namespace zomlang::compiler::mir {

/// \brief Entry point for the recursive MIR builder (RFC 0048 Phase 3).
///
/// Lowers one HIR function to a MirFunction by walking the HIR tree
/// structurally: function -> block -> statements -> expressions. Uses
/// MirFnCtx for dense allocation and the destination-driven API
/// (exprIntoDest / asTemp / asOperand) for expression lowering.
///
/// Returns none if the function uses a construct the recursive builder
/// cannot yet lower (the caller falls back to the shape strategy table).
[[nodiscard]] zc::Maybe<MirFunction> buildRecursiveFunction(
    const hir::VerifiedHirModule& hirModule, const hir::HirFunctionDeclaration& declaration,
    const checker::CheckerIdentityAuthority& identities, checker::marker::MarkerProofEngine& proofs,
    zc::Maybe<identity::DefId> copyMarker);

}  // namespace zomlang::compiler::mir
