// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstdint>

#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::ownership::facts {

/// \brief Symbolic factor vector for the RFC 0007 operational budget.
///
/// The finite complete resource-alternative bound is
/// `K = 4^Ccast * product(b in B, 1 + Ab + Ab*Cb) *
///      product(q in Q, 1 + Aq + Aq*Cq)`.
///
/// K is never materialized as a fixed-width integer. Instead, the factor
/// components are retained and per-analysis bounds are derived component-wise.
/// All factors come from verified finite inventories.
struct OwnershipBudgetFactors final {
  /// Reachable ownership event slots.
  uint64_t eventSlots = 0;
  /// Reachable MIR CFG points.
  uint64_t cfgPoints = 0;
  /// CFG edges.
  uint64_t cfgEdges = 0;
  /// Move paths (M).
  uint64_t movePaths = 0;
  /// Distinct initialization-loss causes (D).
  uint64_t lossCauses = 0;
  /// Loans (L).
  uint64_t loans = 0;
  /// Regions (R).
  uint64_t regions = 0;
  /// Reference-value definitions (V).
  uint64_t referenceValues = 0;
  /// Reference origins (O).
  uint64_t referenceOrigins = 0;
  /// Drop obligations (B).
  uint64_t dropObligations = 0;
  /// Linear obligations (Q).
  uint64_t linearObligations = 0;
  /// Checked-cast carriers (Ccast).
  uint64_t castCarriers = 0;
  /// Verified place-carrier counts per drop obligation (Ab).
  zc::Vector<uint64_t> dropCarrierCounts;
  /// Discharge counts per drop obligation (Cb).
  zc::Vector<uint64_t> dropDischargeCounts;
  /// Verified carrier counts per linear obligation (Aq).
  zc::Vector<uint64_t> linearCarrierCounts;
  /// Consumption counts per linear obligation (Cq).
  zc::Vector<uint64_t> linearConsumptionCounts;
  /// Raw provenance carriers (Araw).
  uint64_t rawProvenanceCarriers = 0;
  /// Raw origins (U).
  uint64_t rawOrigins = 0;

  /// \brief Returns P = Pcfg + 2 * X, the number of reachable OwnershipPoint cutpoints.
  ZC_NODISCARD constexpr uint64_t cutpointCount() const noexcept {
    return cfgPoints + 2 * eventSlots;
  }
};

/// \brief Per-analysis monotone counters for the ownership solver.
///
/// Each counter tracks the number of memberships the solver has added in one
/// analysis pass. A counter exceeding its component-wise derived bound is
/// `InvalidOwnershipProof`, because it proves a non-monotone transfer,
/// duplicate queue admission, or corrupted inventory.
struct OwnershipBudgetCounters final {
  /// Initialization state bits (bound: 3 * P * M).
  uint64_t initStateBits = 0;
  /// Initialization loss-cause memberships (bound: P * M * D).
  uint64_t initLossCauseMemberships = 0;
  /// Loan phase bits (bound: 3 * P * L).
  uint64_t loanPhaseBits = 0;
  /// Loan suspending-child memberships (bound: P * L * L).
  uint64_t loanSuspendingChildren = 0;
  /// Loan source-origin memberships (bound: L * O).
  uint64_t loanSourceOrigins = 0;
  /// Loan parent memberships (bound: L * L).
  uint64_t loanParents = 0;
  /// Reference reaching-definition memberships (bound: P * M * V).
  uint64_t referenceDefinitions = 0;
  /// Reference origin memberships (bound: P * M * V * O).
  uint64_t referenceOrigins = 0;
  /// Region general memberships (bound: P * R).
  uint64_t regionMemberships = 0;
  /// Region explicit value-liveness memberships (bound: P * V).
  uint64_t regionValueLiveness = 0;
  /// Region outlives-closure memberships (bound: R * R).
  uint64_t regionOutlives = 0;
  /// Drop/linear complete-alternative memberships (bound: P * K).
  uint64_t resourceAlternatives = 0;
  /// Drop transition memberships (bound: sum(Ab * Ab)).
  uint64_t dropTransitions = 0;
  /// Linear carrier transition memberships (bound: sum(Aq * Aq)).
  uint64_t linearTransitions = 0;
  /// Raw provenance reaching-carrier memberships (bound: P * M * Araw).
  uint64_t rawReachingCarriers = 0;
  /// Raw provenance predecessor memberships (bound: Araw * Araw).
  uint64_t rawPredecessors = 0;
  /// Raw provenance origin memberships (bound: Araw * U).
  uint64_t rawOriginMemberships = 0;
  /// Place-conflict pairs examined (bound: M * (M - 1) / 2).
  uint64_t placeConflictPairs = 0;
};

/// \brief Result of a budget verification check.
struct OwnershipBudgetCheckResult final {
  /// \brief True when every counter is within its component-wise bound.
  bool withinBudget = true;
  /// \brief The name of the first counter that exceeded its bound, if any.
  zc::Maybe<zc::StringPtr> exceededCounter;
};

/// \brief Verifies ownership solver counters against the RFC 0007 operational budget.
///
/// The verifier derives per-analysis bounds from the symbolic factor vector
/// and checks each counter against its bound. The resource-alternative bound K
/// is never materialized; instead, the resource-alternative counter is checked
/// against a conservative upper bound derived from the individual factors.
///
/// Bound arithmetic uses checked uint64_t multiplication. If a bound
/// computation overflows, the inventories are impossibly large and the check
/// fails.
class OwnershipBudgetVerifier final {
public:
  /// \brief Checks counters against the component-wise derived bounds.
  /// \param factors The symbolic factor vector from verified inventories.
  /// \param counters The per-analysis monotone solver counters.
  /// \return A result indicating whether every counter is within budget.
  ZC_NODISCARD static OwnershipBudgetCheckResult check(
      const OwnershipBudgetFactors& factors, const OwnershipBudgetCounters& counters) noexcept;
};

}  // namespace zomlang::compiler::ownership::facts
