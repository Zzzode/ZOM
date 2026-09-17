// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/mir/build/mir-fn-builder.h"
#include "compiler/mir/built-mir.h"
#include "zc/core/array.h"
#include "zc/core/common.h"

namespace zomlang::compiler::checker {
class CheckerIdentityAuthority;
namespace marker {
class MarkerProofEngine;
}
}  // namespace zomlang::compiler::checker

namespace zomlang::compiler::mir {

/// \brief One recursively lowered function plus its sorted owner key.
///
/// Mirrors the legacy PendingMirFunction pair: the builder owns the function and
/// the canonical owner bytes the module-level sort uses.
struct RecursiveFunctionProduct final {
  MirFunction function;
  zc::Array<uint8_t> ownerKey;
};

/// \brief Lowers one function through the recursive destination-driven builder
/// when it matches the current Phase-3 legality set.
///
/// The accepted shapes are the bare single-block returns the legacy rail emits:
/// a scalar literal return (`fun f(...) -> T { return <literal>; }`, no locals,
/// Return(Constant)), a parameter return (`fun f(p0..pN-1) -> R { return pK;
/// }`, leading parameter locals, Return(copy/move place-use)), a single
/// scalar-initialized user local returned by place-use (`let x = <literal>;
/// return x;`, one UserLocal, StorageLive plus an Initialize Assign), its
/// one literal-overwrite extension (`mut x = <lit>; x = <lit>; return x;`, an
/// additional Overwrite Assign), and sequential N-local bodies with N>=2 plain
/// initializers (`let a = <lit/param/local>; ... return <local-or-param>;`,
/// parameter locals followed by one UserLocal, StorageLive plus Initialize
/// Assign per binding). The predicates replicate the legacy per-shape gates
/// exactly; unsafe-tail, aggregate, primitive-binary, nested-operand, and every
/// other shape return none so the caller delegates them to the legacy builder
/// unchanged.
///
/// \return the lowered function when one strict shape matched and lowered, else
/// none to delegate back to the legacy construction path.
zc::Maybe<RecursiveFunctionProduct> tryBuildRecursiveFunction(
    const hir::HirFunctionDeclaration& declaration, const hir::HirBlockStatement& block,
    const hir::VerifiedHirModule& hirModule, const checker::CheckerIdentityAuthority& identities,
    checker::marker::MarkerProofEngine& proofs, identity::DefId copyMarker);

}  // namespace zomlang::compiler::mir
