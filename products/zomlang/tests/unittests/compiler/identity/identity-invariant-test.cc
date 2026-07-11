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

#include "zc/ztest/test.h"
#include "zomlang/compiler/diagnostics/diagnostic-info.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang::compiler::identity {
namespace {

IdentityInvariant fact(IdentityInvariantKind kind, IdentityAllocationPhase phase,
                       uint8_t structuralByte, IdentityApiSite apiSite, uint32_t ordinal) {
  zc::Maybe<zc::Array<uint8_t>> structural = zc::heapArray<uint8_t>({structuralByte});
  zc::Maybe<UnbrandedSourceRange> noRange;
  auto value = IdentityInvariant::from(kind, phase, zc::mv(structural), zc::mv(noRange),
                                       apiSite, ordinal);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid identity invariant test input was rejected");
}

uint8_t structuralByte(const IdentityInvariant& invariant) {
  ZC_IF_SOME(bytes, invariant.structuralInputKey()) {
    ZC_REQUIRE(bytes.size() == 1);
    return bytes[0];
  }
  ZC_FAIL_REQUIRE("identity invariant test fact has no structural key");
}

}  // namespace

ZC_TEST("Identity invariant rejects unknown closed values") {
  zc::Maybe<zc::Array<uint8_t>> noStructural;
  zc::Maybe<UnbrandedSourceRange> noRange;
  ZC_EXPECT(IdentityInvariant::from(
                static_cast<IdentityInvariantKind>(0xff), IdentityAllocationPhase::Context,
                zc::mv(noStructural), zc::mv(noRange), IdentityApiSite::ContextBrandIssue, 0) ==
            zc::none);
}

ZC_TEST("Identity invariant collector sorts complete structured facts") {
  IdentityInvariantCollector collector;
  collector.add(fact(IdentityInvariantKind::DuplicateCanonicalKey,
                     IdentityAllocationPhase::Package, 0x02,
                     IdentityApiSite::RegistryMutation, 2));
  collector.add(fact(IdentityInvariantKind::DuplicateCanonicalKey,
                     IdentityAllocationPhase::Package, 0x01,
                     IdentityApiSite::RegistryMutation, 1));
  collector.add(fact(IdentityInvariantKind::InvalidHandle, IdentityAllocationPhase::Registry,
                     0xff, IdentityApiSite::HandleLookup, 0));
  collector.sort();

  auto facts = collector.facts();
  ZC_REQUIRE(facts.size() == 3);
  ZC_EXPECT(facts[0].phase() == IdentityAllocationPhase::Registry);
  ZC_EXPECT(structuralByte(facts[1]) == 0x01);
  ZC_EXPECT(structuralByte(facts[2]) == 0x02);
}

ZC_TEST("Identity diagnostics use registered fatal entries and preserve full facts") {
  using diagnostics::DiagID;
  using diagnostics::DiagnosticTraits;
  ZC_EXPECT(identityDiagnosticId(IdentityInvariantKind::InvalidHandle) ==
            DiagID::IdentityInvalidHandle);
  ZC_EXPECT(identityDiagnosticId(IdentityInvariantKind::NonCanonicalEncoding) ==
            DiagID::IdentityNonCanonicalEncoding);
  ZC_EXPECT(DiagnosticTraits<DiagID::IdentityInvalidHandle>::severity ==
            diagnostics::DiagSeverity::kFatal);
  ZC_EXPECT(DiagnosticTraits<DiagID::IdentityInvalidHandle>::argCount == 1);

  IdentityInvariantCollector collector;
  collector.add(fact(IdentityInvariantKind::DuplicateCanonicalKey,
                     IdentityAllocationPhase::Package, 0x01,
                     IdentityApiSite::PackageFreeze, 0));
  collector.add(fact(IdentityInvariantKind::DuplicateCanonicalKey,
                     IdentityAllocationPhase::Package, 0x01,
                     IdentityApiSite::PackageFreeze, 1));
  collector.add(fact(IdentityInvariantKind::InvalidHandle, IdentityAllocationPhase::Registry,
                     0x01, IdentityApiSite::HandleLookup, 2));
  collector.sort();
  auto groups = groupIdentityInvariants(collector.facts());
  ZC_REQUIRE(collector.facts().size() == 3);
  ZC_REQUIRE(groups.size() == 2);
  ZC_EXPECT(groups[0].diagnosticId() == DiagID::IdentityInvalidHandle);
  ZC_EXPECT(groups[0].occurrenceCount() == 1);
  ZC_EXPECT(groups[1].diagnosticId() == DiagID::IdentityDuplicateCanonicalKey);
  ZC_EXPECT(groups[1].occurrenceCount() == 2);

  source::SourceManager sourceManager;
  diagnostics::DiagnosticEngine engine(sourceManager);
  emitIdentityDiagnosticGroups(engine, groups.asPtr());
  ZC_EXPECT(engine.errorCount() == 2);
}

}  // namespace zomlang::compiler::identity
