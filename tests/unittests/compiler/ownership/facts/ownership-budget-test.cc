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

#include "compiler/ownership/facts/ownership-budget.h"

#include "zc/ztest/test.h"

namespace zomlang::compiler::ownership::facts {
namespace {

/// \brief Builds factors with small but nonzero values for every dimension.
OwnershipBudgetFactors makeSmallFactors() {
  OwnershipBudgetFactors factors;
  factors.eventSlots = 2;
  factors.cfgPoints = 3;
  factors.movePaths = 4;
  factors.lossCauses = 2;
  factors.loans = 3;
  factors.regions = 2;
  factors.referenceValues = 3;
  factors.referenceOrigins = 2;
  factors.castCarriers = 1;
  factors.dropCarrierCounts.add(2);
  factors.dropCarrierCounts.add(3);
  factors.dropDischargeCounts.add(1);
  factors.dropDischargeCounts.add(1);
  factors.linearCarrierCounts.add(2);
  factors.linearConsumptionCounts.add(1);
  factors.rawProvenanceCarriers = 2;
  factors.rawOrigins = 2;
  return factors;
}

/// \brief Builds counters that are all zero (trivially within budget).
OwnershipBudgetCounters makeZeroCounters() { return OwnershipBudgetCounters{}; }

// ---------------------------------------------------------------------------
// Happy path
// ---------------------------------------------------------------------------

ZC_TEST("OwnershipBudgetTest.ZeroCountersAreWithinBudget") {
  auto factors = makeSmallFactors();
  auto counters = makeZeroCounters();
  auto result = OwnershipBudgetVerifier::check(factors, counters);
  ZC_EXPECT(result.withinBudget);
  ZC_EXPECT(result.exceededCounter == zc::none);
}

ZC_TEST("OwnershipBudgetTest.CountersAtExactBoundAreWithinBudget") {
  auto factors = makeSmallFactors();
  // P = cfgPoints + 2 * eventSlots = 3 + 4 = 7
  const uint64_t p = factors.cutpointCount();
  ZC_EXPECT(p == 7);

  OwnershipBudgetCounters counters;
  counters.initStateBits = 3 * p * factors.movePaths;                              // 3 * 7 * 4 = 84
  counters.initLossCauseMemberships = p * factors.movePaths * factors.lossCauses;  // 7*4*2=56
  counters.loanPhaseBits = 3 * p * factors.loans;                                  // 3*7*3 = 63
  counters.loanSuspendingChildren = p * factors.loans * factors.loans;             // 7*3*3=63
  counters.loanSourceOrigins = factors.loans * factors.referenceOrigins;           // 3*2=6
  counters.loanParents = factors.loans * factors.loans;                            // 3*3=9
  counters.referenceDefinitions = p * factors.movePaths * factors.referenceValues;  // 7*4*3=84
  counters.referenceOrigins =
      p * factors.movePaths * factors.referenceValues * factors.referenceOrigins;  // 7*4*3*2=168
  counters.regionMemberships = p * factors.regions;                                // 7*2=14
  counters.regionValueLiveness = p * factors.referenceValues;                      // 7*3=21
  counters.regionOutlives = factors.regions * factors.regions;                     // 2*2=4
  // K = 4^1 * (1+2+2*1) * (1+3+3*1) * (1+2+2*1) = 4 * 5 * 7 * 5 = 700
  counters.resourceAlternatives = p * 700;   // 7*700=4900
  counters.dropTransitions = 2 * 2 + 3 * 3;  // 4+9=13
  counters.linearTransitions = 2 * 2;        // 4
  counters.rawReachingCarriers = p * factors.movePaths * factors.rawProvenanceCarriers;  // 7*4*2=56
  counters.rawPredecessors =
      factors.rawProvenanceCarriers * factors.rawProvenanceCarriers;                   // 2*2=4
  counters.rawOriginMemberships = factors.rawProvenanceCarriers * factors.rawOrigins;  // 2*2=4
  counters.placeConflictPairs = factors.movePaths * (factors.movePaths - 1) / 2;       // 4*3/2=6

  auto result = OwnershipBudgetVerifier::check(factors, counters);
  ZC_EXPECT(result.withinBudget);
  ZC_EXPECT(result.exceededCounter == zc::none);
}

// ---------------------------------------------------------------------------
// Cutpoint count
// ---------------------------------------------------------------------------

ZC_TEST("OwnershipBudgetTest.CutpointCountIsCfgPointsPlusTwoEventSlots") {
  OwnershipBudgetFactors factors;
  factors.cfgPoints = 5;
  factors.eventSlots = 3;
  ZC_EXPECT(factors.cutpointCount() == 11);
}

ZC_TEST("OwnershipBudgetTest.CutpointCountWithZeroEventSlots") {
  OwnershipBudgetFactors factors;
  factors.cfgPoints = 5;
  factors.eventSlots = 0;
  ZC_EXPECT(factors.cutpointCount() == 5);
}

// ---------------------------------------------------------------------------
// Individual counter violations
// ---------------------------------------------------------------------------

ZC_TEST("OwnershipBudgetTest.InitStateBitsExceedingBoundFails") {
  auto factors = makeSmallFactors();
  OwnershipBudgetCounters counters;
  counters.initStateBits = 3 * factors.cutpointCount() * factors.movePaths + 1;
  auto result = OwnershipBudgetVerifier::check(factors, counters);
  ZC_EXPECT(!result.withinBudget);
  ZC_IF_SOME(name, result.exceededCounter) { ZC_EXPECT(name == "initStateBits"); }
}

ZC_TEST("OwnershipBudgetTest.LoanPhaseBitsExceedingBoundFails") {
  auto factors = makeSmallFactors();
  OwnershipBudgetCounters counters;
  counters.loanPhaseBits = 3 * factors.cutpointCount() * factors.loans + 1;
  auto result = OwnershipBudgetVerifier::check(factors, counters);
  ZC_EXPECT(!result.withinBudget);
  ZC_IF_SOME(name, result.exceededCounter) { ZC_EXPECT(name == "loanPhaseBits"); }
}

ZC_TEST("OwnershipBudgetTest.ResourceAlternativesExceedingBoundFails") {
  auto factors = makeSmallFactors();
  OwnershipBudgetCounters counters;
  counters.resourceAlternatives = UINT64_MAX;
  auto result = OwnershipBudgetVerifier::check(factors, counters);
  ZC_EXPECT(!result.withinBudget);
  ZC_IF_SOME(name, result.exceededCounter) { ZC_EXPECT(name == "resourceAlternatives"); }
}

ZC_TEST("OwnershipBudgetTest.PlaceConflictPairsExceedingBoundFails") {
  auto factors = makeSmallFactors();
  OwnershipBudgetCounters counters;
  counters.placeConflictPairs = factors.movePaths * (factors.movePaths - 1) / 2 + 1;
  auto result = OwnershipBudgetVerifier::check(factors, counters);
  ZC_EXPECT(!result.withinBudget);
  ZC_IF_SOME(name, result.exceededCounter) { ZC_EXPECT(name == "placeConflictPairs"); }
}

ZC_TEST("OwnershipBudgetTest.RawReachingCarriersExceedingBoundFails") {
  auto factors = makeSmallFactors();
  OwnershipBudgetCounters counters;
  counters.rawReachingCarriers =
      factors.cutpointCount() * factors.movePaths * factors.rawProvenanceCarriers + 1;
  auto result = OwnershipBudgetVerifier::check(factors, counters);
  ZC_EXPECT(!result.withinBudget);
  ZC_IF_SOME(name, result.exceededCounter) { ZC_EXPECT(name == "rawReachingCarriers"); }
}

// ---------------------------------------------------------------------------
// Overflow handling
// ---------------------------------------------------------------------------

ZC_TEST("OwnershipBudgetTest.OverflowInBoundComputationFails") {
  OwnershipBudgetFactors factors;
  factors.eventSlots = 1;
  factors.cfgPoints = 1;
  factors.movePaths = UINT64_MAX;
  factors.loans = 1;
  factors.regions = 1;
  factors.referenceValues = 1;
  factors.referenceOrigins = 1;
  factors.castCarriers = 0;
  factors.dropCarrierCounts.add(1);
  factors.dropDischargeCounts.add(0);
  factors.linearCarrierCounts.add(1);
  factors.linearConsumptionCounts.add(0);
  factors.rawProvenanceCarriers = 1;
  factors.rawOrigins = 1;

  auto counters = makeZeroCounters();
  auto result = OwnershipBudgetVerifier::check(factors, counters);
  // initStateBits bound = 3 * P * M overflows → first check fails
  ZC_EXPECT(!result.withinBudget);
  ZC_IF_SOME(name, result.exceededCounter) { ZC_EXPECT(name == "initStateBits"); }
}

ZC_TEST("OwnershipBudgetTest.OverflowInResourceBoundFails") {
  OwnershipBudgetFactors factors;
  factors.eventSlots = 0;
  factors.cfgPoints = 1;
  factors.movePaths = 1;
  factors.loans = 1;
  factors.regions = 1;
  factors.referenceValues = 1;
  factors.referenceOrigins = 1;
  factors.castCarriers = 40;  // 4^40 overflows uint64_t
  factors.dropCarrierCounts.add(1);
  factors.dropDischargeCounts.add(0);
  factors.linearCarrierCounts.add(1);
  factors.linearConsumptionCounts.add(0);
  factors.rawProvenanceCarriers = 1;
  factors.rawOrigins = 1;

  auto counters = makeZeroCounters();
  auto result = OwnershipBudgetVerifier::check(factors, counters);
  // All checks before resourceAlternatives pass with zero counters, but K
  // overflows so resourceAlternatives fails.
  ZC_EXPECT(!result.withinBudget);
  ZC_IF_SOME(name, result.exceededCounter) { ZC_EXPECT(name == "resourceAlternatives"); }
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

ZC_TEST("OwnershipBudgetTest.AllZeroFactorsAreWithinBudget") {
  OwnershipBudgetFactors factors;  // all zeros
  auto counters = makeZeroCounters();
  auto result = OwnershipBudgetVerifier::check(factors, counters);
  ZC_EXPECT(result.withinBudget);
}

ZC_TEST("OwnershipBudgetTest.EmptyDropAndLinearVectorsAreWithinBudget") {
  OwnershipBudgetFactors factors;
  factors.eventSlots = 1;
  factors.cfgPoints = 1;
  factors.movePaths = 1;
  factors.loans = 1;
  factors.regions = 1;
  factors.referenceValues = 1;
  factors.referenceOrigins = 1;
  factors.castCarriers = 0;
  // dropCarrierCounts and linearCarrierCounts are empty
  factors.rawProvenanceCarriers = 1;
  factors.rawOrigins = 1;

  auto counters = makeZeroCounters();
  auto result = OwnershipBudgetVerifier::check(factors, counters);
  ZC_EXPECT(result.withinBudget);
}

ZC_TEST("OwnershipBudgetTest.FirstViolationIsReported") {
  auto factors = makeSmallFactors();
  OwnershipBudgetCounters counters;
  // Violate initStateBits (first check) and loanPhaseBits (later check).
  counters.initStateBits = UINT64_MAX;
  counters.loanPhaseBits = UINT64_MAX;
  auto result = OwnershipBudgetVerifier::check(factors, counters);
  ZC_EXPECT(!result.withinBudget);
  ZC_IF_SOME(name, result.exceededCounter) { ZC_EXPECT(name == "initStateBits"); }
}

}  // namespace
}  // namespace zomlang::compiler::ownership::facts
