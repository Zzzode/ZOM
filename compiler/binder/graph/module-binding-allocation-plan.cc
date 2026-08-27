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
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#include "compiler/binder/graph/module-binding-allocation-plan.h"

#include <cstdint>

#include "zc/core/vector.h"
#include "compiler/binder/stable/stable-binding-codec.h"

namespace zomlang::compiler::binder {
namespace {

bool sameModule(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

zc::Maybe<ModuleBindingAllocationPlan> buildProviderPlan(
    const BoundModuleSkeleton& skeleton, zc::ArrayPtr<const BoundOwnerBody> bodies) {
  const auto owners = skeleton.bodyOwners().values();
  if (bodies.size() != owners.size() || skeleton.scopes().values().size() > UINT32_MAX ||
      skeleton.implementationOccurrences().values().size() > UINT32_MAX) {
    return zc::none;
  }

  uint32_t scopeCursor = static_cast<uint32_t>(skeleton.scopes().values().size());
  uint32_t ownerLocalCursor = 0;
  uint32_t anonymousCursor = 0;
  uint32_t labelCursor = 0;
  zc::Vector<OwnerAllocationRange> ranges(owners.size());
  for (const auto& owner : owners) {
    zc::Maybe<size_t> matchingIndex;
    for (size_t index = 0; index < bodies.size(); ++index) {
      const auto& body = bodies[index];
      if (body.owner() != owner) { continue; }
      if (matchingIndex != zc::none) { return zc::none; }
      matchingIndex = index;
    }
    if (matchingIndex == zc::none) { return zc::none; }
    const auto& matchingBody = bodies[ZC_ASSERT_NONNULL(matchingIndex)];
    if (!sameModule(skeleton.module(), matchingBody.owner().module()) ||
        matchingBody.scopes().values().size() > UINT32_MAX ||
        matchingBody.bindings().values().size() > UINT32_MAX ||
        matchingBody.labels().values().size() > UINT32_MAX ||
        static_cast<uint64_t>(matchingBody.closures().values().size()) +
                matchingBody.explicitClosureCaptures().values().size() >
            UINT32_MAX) {
      return zc::none;
    }
    const uint32_t scopeCount = static_cast<uint32_t>(matchingBody.scopes().values().size());
    const uint32_t ownerLocalCount = static_cast<uint32_t>(matchingBody.bindings().values().size());
    const uint32_t anonymousCount =
        static_cast<uint32_t>(matchingBody.closures().values().size() +
                              matchingBody.explicitClosureCaptures().values().size());
    const uint32_t labelCount = static_cast<uint32_t>(matchingBody.labels().values().size());
    auto range = OwnerAllocationRange::from(owner.clone(), scopeCursor, scopeCount,
                                            ownerLocalCursor, ownerLocalCount, anonymousCursor,
                                            anonymousCount, labelCursor, labelCount);
    if (range == zc::none) { return zc::none; }
    ranges.add(zc::mv(ZC_ASSERT_NONNULL(range)));
    if (static_cast<uint64_t>(scopeCursor) + scopeCount > UINT32_MAX ||
        static_cast<uint64_t>(ownerLocalCursor) + ownerLocalCount > UINT32_MAX ||
        static_cast<uint64_t>(anonymousCursor) + anonymousCount > UINT32_MAX ||
        static_cast<uint64_t>(labelCursor) + labelCount > UINT32_MAX) {
      return zc::none;
    }
    scopeCursor += scopeCount;
    ownerLocalCursor += ownerLocalCount;
    anonymousCursor += anonymousCount;
    labelCursor += labelCount;
  }
  auto admittedRanges = StableBindingSequenceBuilder<OwnerAllocationRange>::from(zc::mv(ranges));
  if (admittedRanges == zc::none) { return zc::none; }
  return ModuleBindingAllocationPlan::from(
      skeleton.module().clone(), static_cast<uint32_t>(skeleton.scopes().values().size()),
      static_cast<uint32_t>(skeleton.implementationOccurrences().values().size()),
      zc::mv(ZC_ASSERT_NONNULL(admittedRanges)));
}

zc::Maybe<ModuleBindingAllocationPlan> buildVerifierPlan(
    const BoundModuleSkeleton& skeleton, zc::ArrayPtr<const BoundOwnerBody> bodies) {
  const auto canonicalOwners = skeleton.bodyOwners().values();
  if (canonicalOwners.size() != bodies.size() || skeleton.scopes().values().size() > UINT32_MAX ||
      skeleton.implementationOccurrences().values().size() > UINT32_MAX) {
    return zc::none;
  }

  const uint32_t skeletonScopeCount = static_cast<uint32_t>(skeleton.scopes().values().size());
  const uint32_t implementationOccurrenceCount =
      static_cast<uint32_t>(skeleton.implementationOccurrences().values().size());
  uint32_t nextScope = skeletonScopeCount;
  uint32_t nextOwnerLocal = 0;
  uint32_t nextAnonymous = 0;
  uint32_t nextLabel = 0;
  zc::Vector<OwnerAllocationRange> expectedRanges(canonicalOwners.size());
  for (const auto& canonicalOwner : canonicalOwners) {
    zc::Maybe<size_t> selectedIndex;
    for (size_t index = 0; index < bodies.size(); ++index) {
      const auto& candidate = bodies[index];
      if (candidate.owner() != canonicalOwner) { continue; }
      if (selectedIndex != zc::none) { return zc::none; }
      selectedIndex = index;
    }
    if (selectedIndex == zc::none) { return zc::none; }
    const auto& selected = bodies[ZC_ASSERT_NONNULL(selectedIndex)];
    if (!sameModule(selected.owner().module(), skeleton.module()) ||
        selected.scopes().values().size() > UINT32_MAX ||
        selected.bindings().values().size() > UINT32_MAX ||
        selected.labels().values().size() > UINT32_MAX ||
        static_cast<uint64_t>(selected.closures().values().size()) +
                selected.explicitClosureCaptures().values().size() >
            UINT32_MAX) {
      return zc::none;
    }
    const uint32_t scopes = static_cast<uint32_t>(selected.scopes().values().size());
    const uint32_t ownerLocals = static_cast<uint32_t>(selected.bindings().values().size());
    const uint32_t anonymous = static_cast<uint32_t>(
        selected.closures().values().size() + selected.explicitClosureCaptures().values().size());
    const uint32_t labels = static_cast<uint32_t>(selected.labels().values().size());
    if (static_cast<uint64_t>(nextScope) + scopes > UINT32_MAX ||
        static_cast<uint64_t>(nextOwnerLocal) + ownerLocals > UINT32_MAX ||
        static_cast<uint64_t>(nextAnonymous) + anonymous > UINT32_MAX ||
        static_cast<uint64_t>(nextLabel) + labels > UINT32_MAX) {
      return zc::none;
    }
    auto range =
        OwnerAllocationRange::from(canonicalOwner.clone(), nextScope, scopes, nextOwnerLocal,
                                   ownerLocals, nextAnonymous, anonymous, nextLabel, labels);
    if (range == zc::none) { return zc::none; }
    expectedRanges.add(zc::mv(ZC_ASSERT_NONNULL(range)));
    nextScope += scopes;
    nextOwnerLocal += ownerLocals;
    nextAnonymous += anonymous;
    nextLabel += labels;
  }
  auto canonicalRanges =
      StableBindingSequenceBuilder<OwnerAllocationRange>::from(zc::mv(expectedRanges));
  if (canonicalRanges == zc::none) { return zc::none; }
  return ModuleBindingAllocationPlan::from(skeleton.module().clone(), skeletonScopeCount,
                                           implementationOccurrenceCount,
                                           zc::mv(ZC_ASSERT_NONNULL(canonicalRanges)));
}

}  // namespace

zc::Maybe<ModuleBindingAllocationPlan> ModuleBindingAllocationPlanner::from(
    const BoundModuleSkeleton& skeleton, zc::ArrayPtr<const BoundOwnerBody> bodies) {
  return buildProviderPlan(skeleton, bodies);
}

bool ModuleBindingAllocationPlanner::verify(const BoundModuleSkeleton& skeleton,
                                            zc::ArrayPtr<const BoundOwnerBody> bodies,
                                            const ModuleBindingAllocationPlan& plan) {
  auto expected = buildVerifierPlan(skeleton, bodies);
  return expected != zc::none && ZC_ASSERT_NONNULL(expected) == plan;
}

}  // namespace zomlang::compiler::binder
