// Copyright (c) 2026 Zode.Zang. All rights reserved
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

// RFC 0053: structural and translation verification findings admit through the
// IR failure algebra at IrFailurePhase::LirVerification and project to the
// internal incident rail, rather than being flattened to an operational string.

#include "compiler/lir/verify/lir-verification-failure.h"

#include "zc/core/array.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::lir {
namespace {

ZC_TEST("LIR structural finding projects to an admitted LirVerification incident") {
  LirVerificationFinding finding{LirVerificationFaultKind::DanglingBlockTarget, 1, 2, 0};
  auto rejected = projectLirVerificationIncident(finding);
  ZC_REQUIRE(rejected != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(rejected).incidents.size() == 1);
}

ZC_TEST("LIR translation finding projects to an admitted LirVerification incident") {
  TranslationFinding finding{TranslationFaultKind::EdgeTargetMismatch, 1, 2, 2, 0};
  auto rejected = projectLirVerificationIncident(finding);
  ZC_REQUIRE(rejected != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(rejected).incidents.size() == 1);
}

ZC_TEST("Both verifier stages project through the same compiler-defect incident kind") {
  // The incident rail is a privacy-preserving (domain, phase, kind, producer)
  // shape. Structural and translation verification are both LirVerification /
  // InvalidFact compiler self-consistency defects; the finer fault tag stays in
  // the structured finding, not the user-facing incident. Both must admit.
  LirVerificationFinding structural{LirVerificationFaultKind::DanglingBlockTarget, 1, 2, 0};
  TranslationFinding translation{TranslationFaultKind::EdgeTargetMismatch, 1, 2, 2, 0};
  auto rejectedStructural = projectLirVerificationIncident(structural);
  auto rejectedTranslation = projectLirVerificationIncident(translation);
  ZC_REQUIRE(rejectedStructural != zc::none && rejectedTranslation != zc::none);
  const auto& structuralDescriptor =
      ZC_ASSERT_NONNULL(rejectedStructural).incidents.descriptors()[0];
  const auto& translationDescriptor =
      ZC_ASSERT_NONNULL(rejectedTranslation).incidents.descriptors()[0];
  ZC_EXPECT(structuralDescriptor.phase() == translationDescriptor.phase());
  ZC_EXPECT(structuralDescriptor.kind() == translationDescriptor.kind());
}

ZC_TEST("Repeated findings of one kind dedupe to one incident descriptor") {
  LirVerificationFinding first{LirVerificationFaultKind::DanglingBlockTarget, 1, 2, 0};
  LirVerificationFinding second{LirVerificationFaultKind::DanglingBlockTarget, 1, 7, 0};
  basic::BoundedIncidentSet merged;
  auto rejectedFirst = projectLirVerificationIncident(first);
  auto rejectedSecond = projectLirVerificationIncident(second);
  ZC_REQUIRE(rejectedFirst != zc::none && rejectedSecond != zc::none);
  ZC_EXPECT(merged.merge(ZC_ASSERT_NONNULL(rejectedFirst).incidents));
  (void)merged.merge(ZC_ASSERT_NONNULL(rejectedSecond).incidents);
  ZC_EXPECT(merged.size() == 1);
}

}  // namespace
}  // namespace zomlang::compiler::lir
