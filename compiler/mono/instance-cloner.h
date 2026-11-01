// Copyright (c) 2026 Zode.Z. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/function.h"
#include "compiler/identity/diagnostics/identity-invariant.h"
#include "compiler/mir/built-mir.h"
#include "compiler/type/type-substitution.h"

namespace zomlang::compiler::type {
class SemanticTypeStore;
}  // namespace zomlang::compiler::type

namespace zomlang::compiler::mono {

/// \brief Deep-clones one verified post-elaboration MIR template without sharing
/// move-only state with the source.
/// \param templateFunction Verified executable MIR function body.
/// \return An independent clone.
ZC_NODISCARD mir::MirFunction cloneTemplateFunction(const mir::MirFunction& templateFunction);

/// \brief Rewrites every SemanticTypeId carrier of one cloned MIR body through
/// the canonical substitution, interning ground results in the mutable store.
///
/// Every slot listed by the RFC 0056 substitution walk is covered: function
/// result, locals, place roots/results and every projection input/result,
/// constants, and all rvalue carrier types. Unmapped ground types pass through
/// unchanged; a residual TypeParameter leaf is rewritten only when the
/// substitution names its key.
/// \param function Cloned function body rewritten in place.
/// \param substitution Ordered canonical type substitutions.
/// \param semanticTypes Mutable session type store receiving interned grounds.
/// \param substitutedNodeCount Accumulates the count of type carrier slots
/// rewritten across calls, for the instance-set node budget.
/// \return Nothing on success, or the identity invariant reported by the
/// canonical substitution walker.
ZC_NODISCARD zc::Maybe<identity::IdentityInvariant> substituteFunctionTypes(
    mir::MirFunction& function,
    zc::ArrayPtr<const type::semantic::TypeSubstitution> substitution,
    type::SemanticTypeStore& semanticTypes, uint64_t& substitutedNodeCount);

/// \brief Invokes callback for every SemanticTypeId carrier slot of one body.
///
/// Covers the RFC 0056 full node walk: function result, locals, place roots,
/// place results, every projection input/result, constant types, and every
/// rvalue-carried type. The same slot inventory is used by the builder's node
/// budget pre-count and the verifier's carrier predicates.
void forEachFunctionTypeSlot(
    const mir::MirFunction& function,
    zc::FunctionParam<void(identity::SemanticTypeId)> callback) noexcept;

/// \brief Counts the type carrier slots in one body using the full node walk.
ZC_NODISCARD uint64_t countFunctionTypeCarriers(const mir::MirFunction& function) noexcept;

}  // namespace zomlang::compiler::mono
