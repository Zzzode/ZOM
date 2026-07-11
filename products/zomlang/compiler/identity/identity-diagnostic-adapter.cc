// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/identity/identity-diagnostic-adapter.h"

#include "zc/core/string.h"
#include "zomlang/compiler/diagnostics/diagnostic.h"

namespace zomlang::compiler::identity {
namespace {

bool sameRange(zc::Maybe<const UnbrandedSourceRange&> left,
               zc::Maybe<const UnbrandedSourceRange&> right) {
  if (left == zc::none) { return right == zc::none; }
  if (right == zc::none) { return false; }
  ZC_IF_SOME(leftValue, left) {
    ZC_IF_SOME(rightValue, right) {
      const auto leftBytes = leftValue.encode();
      const auto rightBytes = rightValue.encode();
      return leftBytes.asPtr() == rightBytes.asPtr();
    }
  }
  ZC_UNREACHABLE
}

}  // namespace

IdentityDiagnosticGroup::IdentityDiagnosticGroup(
    diagnostics::DiagID diagnosticId, zc::Maybe<UnbrandedSourceRange>&& diagnosticRange,
    uint64_t occurrenceCount) noexcept
    : idValue(diagnosticId), rangeValue(zc::mv(diagnosticRange)), countValue(occurrenceCount) {}

diagnostics::DiagID IdentityDiagnosticGroup::diagnosticId() const noexcept { return idValue; }
zc::Maybe<const UnbrandedSourceRange&> IdentityDiagnosticGroup::diagnosticRange() const {
  ZC_IF_SOME(value, rangeValue) { return value; }
  return zc::none;
}
uint64_t IdentityDiagnosticGroup::occurrenceCount() const noexcept { return countValue; }

diagnostics::DiagID identityDiagnosticId(IdentityInvariantKind kind) {
  using diagnostics::DiagID;
  switch (kind) {
    case IdentityInvariantKind::InvalidHandle:
      return DiagID::IdentityInvalidHandle;
    case IdentityInvariantKind::ForeignContext:
      return DiagID::IdentityForeignContext;
    case IdentityInvariantKind::ForeignRegistry:
      return DiagID::IdentityForeignRegistry;
    case IdentityInvariantKind::SlotOutOfRange:
      return DiagID::IdentitySlotOutOfRange;
    case IdentityInvariantKind::AncestorMismatch:
      return DiagID::IdentityAncestorMismatch;
    case IdentityInvariantKind::InvalidSourceRange:
      return DiagID::IdentityInvalidSourceRange;
    case IdentityInvariantKind::DuplicateCanonicalKey:
      return DiagID::IdentityDuplicateCanonicalKey;
    case IdentityInvariantKind::InvalidClosedValue:
      return DiagID::IdentityInvalidClosedValue;
    case IdentityInvariantKind::PostFreezeMutation:
      return DiagID::IdentityPostFreezeMutation;
    case IdentityInvariantKind::BrandExhausted:
      return DiagID::IdentityBrandExhausted;
    case IdentityInvariantKind::DuplicateSingletonStore:
      return DiagID::IdentityDuplicateSingletonStore;
    case IdentityInvariantKind::NonCanonicalEncoding:
      return DiagID::IdentityNonCanonicalEncoding;
  }
  ZC_UNREACHABLE
}

zc::Vector<IdentityDiagnosticGroup> groupIdentityInvariants(
    zc::ArrayPtr<const IdentityInvariant> facts) {
  zc::Vector<IdentityDiagnosticGroup> groups;
  for (const auto& fact : facts) {
    const auto id = identityDiagnosticId(fact.kind());
    if (groups.size() != 0 && groups.back().diagnosticId() == id &&
        sameRange(groups.back().diagnosticRange(), fact.diagnosticRange())) {
      ++groups.back().countValue;
      continue;
    }

    zc::Maybe<UnbrandedSourceRange> range;
    ZC_IF_SOME(value, fact.diagnosticRange()) { range = value.clone(); }
    groups.add(IdentityDiagnosticGroup(id, zc::mv(range), 1));
  }
  return groups;
}

void emitIdentityDiagnosticGroups(
    diagnostics::DiagnosticEngine& engine, zc::ArrayPtr<const IdentityDiagnosticGroup> groups,
    zc::Maybe<const IdentityDiagnosticLocationResolver&> locationResolver) {
  for (const auto& group : groups) {
    source::SourceLoc location;
    ZC_IF_SOME(range, group.diagnosticRange()) {
      ZC_IF_SOME(resolver, locationResolver) {
        ZC_IF_SOME(resolved, resolver.resolve(range)) { location = resolved; }
      }
    }
    engine.emit(diagnostics::Diagnostic(group.diagnosticId(), location,
                                        zc::str(group.occurrenceCount())));
  }
}

}  // namespace zomlang::compiler::identity
