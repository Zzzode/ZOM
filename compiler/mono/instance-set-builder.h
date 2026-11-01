// Copyright (c) 2026 Zode.Z. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/common.h"
#include "zc/core/function.h"
#include "compiler/checker/facts/checked-facts-repository.h"
#include "compiler/checker/facts/signature-facts.h"
#include "compiler/checker/checker-identity-authority.h"
#include "compiler/identity/handle.h"
#include "compiler/ir/diagnostics/ir-failure.h"
#include "compiler/mir/built-mir.h"
#include "compiler/mono/instance-cloner.h"
#include "compiler/mono/instance-set.h"
#include "compiler/type/semantic-type-store.h"

namespace zomlang::compiler::ownership {
class CheckedMir;
}  // namespace zomlang::compiler::ownership

namespace zomlang::compiler::mono {

/// \brief Immutable inputs of one module's monomorphization pass.
///
/// The pass reads the post-elaboration executable MIR templates through the
/// checked MIR wrapper, canonical substitution and witness records through the
/// frozen-store record view, and interns substituted ground types in the
/// mutable session store. It never mutates source MIR.
struct MonomorphizationInput final {
  const ownership::CheckedMir& checkedMir;
  const checker::signature::VerifiedSignatureFacts& signatures;
  const checker::checked::CheckedFactsRecordView& records;
  const checker::CheckerIdentityAuthority& identities;
  type::SemanticTypeStore& semanticTypes;
  uint64_t instanceBudget = kMonomorphizationInstanceBudget;
  uint64_t substitutionNodeBudget = kMonomorphizationSubstitutionNodeBudget;
};

/// \brief Collects the reachable instance fixpoint from post-elaboration templates.
///
/// Worklist order is deterministic source function/block order; first sight of
/// an instance clones and substitutes its template. Recursive instantiation
/// selects ZOM6010 and the fixed budgets select ZOM6011. The output is
/// untrusted until MonoInstanceSetVerifier accepts it.
class MonoInstanceSetBuilder final {
public:
  ZC_NODISCARD static ir::IrOperationResult<MonoInstanceSetCandidate> build(
      const MonomorphizationInput& input);
};

/// \brief Sole publisher of immutable verified monomorphized instance sets.
///
/// Independently re-derives each identity frame, re-encodes every body, and
/// enforces the six RFC 0056 predicates: residual generic keys, admitted
/// carrier membership, ownership triviality, template lineage, edge keys and
/// arity closure, and identity ordering with fixed budgets.
class MonoInstanceSetVerifier final {
public:
  ZC_NODISCARD static ir::IrOperationResult<VerifiedMonoInstanceSet> verify(
      MonoInstanceSetCandidate&& candidate, const MonomorphizationInput& input);
};

}  // namespace zomlang::compiler::mono
