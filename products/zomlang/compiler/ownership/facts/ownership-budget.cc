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

#include "zomlang/compiler/ownership/facts/ownership-budget.h"

#include <cstdint>
#include <limits>

namespace zomlang::compiler::ownership::facts {
namespace {

/// \brief Multiplies two uint64_t values, returning none on overflow.
zc::Maybe<uint64_t> checkedMul(uint64_t a, uint64_t b) noexcept {
  if (a == 0 || b == 0) return uint64_t{0};
  if (a > std::numeric_limits<uint64_t>::max() / b) return zc::none;
  return a * b;
}

/// \brief Multiplies three uint64_t values, returning none on overflow.
zc::Maybe<uint64_t> checkedMul3(uint64_t a, uint64_t b, uint64_t c) noexcept {
  auto ab = checkedMul(a, b);
  uint64_t value = ZC_UNWRAP_OR_RETURN(ab, zc::none);
  return checkedMul(value, c);
}

/// \brief Computes the resource-alternative bound K from the factor vector.
///
/// K = 4^Ccast * product(b in B, 1 + Ab + Ab*Cb) *
///     product(q in Q, 1 + Aq + Aq*Cq)
///
/// Returns none if the computation overflows uint64_t.
zc::Maybe<uint64_t> computeResourceBound(const OwnershipBudgetFactors& factors) noexcept {
  uint64_t k = 1;
  // 4^Ccast
  for (uint64_t i = 0; i < factors.castCarriers; ++i) {
    auto next = checkedMul(k, 4);
    k = ZC_UNWRAP_OR_RETURN(next, zc::none);
  }
  // product(b in B, 1 + Ab + Ab*Cb)
  for (size_t i = 0; i < factors.dropCarrierCounts.size(); ++i) {
    const uint64_t ab = factors.dropCarrierCounts[i];
    const uint64_t cb =
        i < factors.dropDischargeCounts.size() ? factors.dropDischargeCounts[i] : 0;
    auto abCb = checkedMul(ab, cb);
    uint64_t product = ZC_UNWRAP_OR_RETURN(abCb, zc::none);
    auto next = checkedMul(k, 1 + ab + product);
    k = ZC_UNWRAP_OR_RETURN(next, zc::none);
  }
  // product(q in Q, 1 + Aq + Aq*Cq)
  for (size_t i = 0; i < factors.linearCarrierCounts.size(); ++i) {
    const uint64_t aq = factors.linearCarrierCounts[i];
    const uint64_t cq =
        i < factors.linearConsumptionCounts.size() ? factors.linearConsumptionCounts[i] : 0;
    auto aqCq = checkedMul(aq, cq);
    uint64_t product = ZC_UNWRAP_OR_RETURN(aqCq, zc::none);
    auto next = checkedMul(k, 1 + aq + product);
    k = ZC_UNWRAP_OR_RETURN(next, zc::none);
  }
  return k;
}

}  // namespace

OwnershipBudgetCheckResult OwnershipBudgetVerifier::check(
    const OwnershipBudgetFactors& factors,
    const OwnershipBudgetCounters& counters) noexcept {
  const uint64_t p = factors.cutpointCount();
  const uint64_t m = factors.movePaths;
  const uint64_t d = factors.lossCauses;
  const uint64_t l = factors.loans;
  const uint64_t r = factors.regions;
  const uint64_t v = factors.referenceValues;
  const uint64_t o = factors.referenceOrigins;
  const uint64_t araw = factors.rawProvenanceCarriers;
  const uint64_t u = factors.rawOrigins;

  OwnershipBudgetCheckResult result;

  auto check = [&](uint64_t counter, zc::Maybe<uint64_t> bound, zc::StringPtr name) {
    if (!result.withinBudget) return;
    if (bound == zc::none) {
      result.withinBudget = false;
      result.exceededCounter = name;
      return;
    }
    ZC_IF_SOME(value, bound) {
      if (counter > value) {
        result.withinBudget = false;
        result.exceededCounter = name;
      }
    }
  };

  // Initialization: 3*P*M state bits, P*M*D loss-cause memberships.
  check(counters.initStateBits, checkedMul3(3, p, m), "initStateBits");
  check(counters.initLossCauseMemberships, checkedMul3(p, m, d), "initLossCauseMemberships");

  // Loans: 3*P*L phase bits, P*L*L suspending children, L*O source origins, L*L parents.
  check(counters.loanPhaseBits, checkedMul3(3, p, l), "loanPhaseBits");
  check(counters.loanSuspendingChildren, checkedMul3(p, l, l), "loanSuspendingChildren");
  check(counters.loanSourceOrigins, checkedMul(l, o), "loanSourceOrigins");
  check(counters.loanParents, checkedMul(l, l), "loanParents");

  // References: P*M*V definitions, P*M*V*O origins.
  check(counters.referenceDefinitions, checkedMul3(p, m, v), "referenceDefinitions");
  auto pmv = checkedMul3(p, m, v);
  if (pmv == zc::none) {
    check(counters.referenceOrigins, zc::none, "referenceOrigins");
  } else {
    ZC_IF_SOME(pmvValue, pmv) {
      check(counters.referenceOrigins, checkedMul(pmvValue, o), "referenceOrigins");
    }
  }

  // Regions: P*R memberships, P*V value liveness, R*R outlives.
  check(counters.regionMemberships, checkedMul(p, r), "regionMemberships");
  check(counters.regionValueLiveness, checkedMul(p, v), "regionValueLiveness");
  check(counters.regionOutlives, checkedMul(r, r), "regionOutlives");

  // Resources: P*K alternatives, sum(Ab*Ab) drop transitions, sum(Aq*Aq) linear transitions.
  auto k = computeResourceBound(factors);
  if (k == zc::none) {
    check(counters.resourceAlternatives, zc::none, "resourceAlternatives");
  } else {
    ZC_IF_SOME(kValue, k) {
      check(counters.resourceAlternatives, checkedMul(p, kValue), "resourceAlternatives");
    }
  }

  // Drop transitions: sum(Ab * Ab).
  uint64_t dropTransitionBound = 0;
  bool dropOverflow = false;
  for (uint64_t ab : factors.dropCarrierCounts) {
    auto sq = checkedMul(ab, ab);
    if (sq == zc::none) {
      dropOverflow = true;
      break;
    }
    ZC_IF_SOME(sqValue, sq) { dropTransitionBound += sqValue; }
  }
  check(counters.dropTransitions,
        dropOverflow ? zc::none : zc::Maybe<uint64_t>(dropTransitionBound), "dropTransitions");

  // Linear transitions: sum(Aq * Aq).
  uint64_t linearTransitionBound = 0;
  bool linearOverflow = false;
  for (uint64_t aq : factors.linearCarrierCounts) {
    auto sq = checkedMul(aq, aq);
    if (sq == zc::none) {
      linearOverflow = true;
      break;
    }
    ZC_IF_SOME(sqValue, sq) { linearTransitionBound += sqValue; }
  }
  check(counters.linearTransitions,
        linearOverflow ? zc::none : zc::Maybe<uint64_t>(linearTransitionBound),
        "linearTransitions");

  // Raw provenance: P*M*Araw reaching carriers, Araw*Araw predecessors, Araw*U origins.
  check(counters.rawReachingCarriers, checkedMul3(p, m, araw), "rawReachingCarriers");
  check(counters.rawPredecessors, checkedMul(araw, araw), "rawPredecessors");
  check(counters.rawOriginMemberships, checkedMul(araw, u), "rawOriginMemberships");

  // Place conflicts: M*(M-1)/2 pairs.
  if (m > 0) {
    auto pairs = checkedMul(m, m - 1);
    if (pairs == zc::none) {
      check(counters.placeConflictPairs, zc::none, "placeConflictPairs");
    } else {
      ZC_IF_SOME(pairsValue, pairs) {
        check(counters.placeConflictPairs, pairsValue / 2, "placeConflictPairs");
      }
    }
  }

  return result;
}

}  // namespace zomlang::compiler::ownership::facts
