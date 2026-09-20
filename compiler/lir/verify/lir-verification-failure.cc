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

#include "compiler/lir/verify/lir-verification-failure.h"

#include <cstdint>

#include "compiler/ir/diagnostics/ir-diagnostic-projector.h"
#include "compiler/ir/diagnostics/ir-failure.h"

namespace zomlang::compiler::lir {
namespace {

namespace ir = compiler::ir;

// Resolver that expands nothing: the LIR verification fact is session-owned, so
// module/definition/instance expansion is never invoked.
class UnusedIdentityResolver final : public ir::IrFailureIdentityResolver {
public:
  ir::ExpandedIrIdentityResult expand(identity::ModuleId) const override {
    return rejected(identity::IdentityAllocationPhase::Module);
  }
  ir::ExpandedIrIdentityResult expand(identity::DefId) const override {
    return rejected(identity::IdentityAllocationPhase::Definition);
  }
  ir::ExpandedIrIdentityResult expand(ir::InstanceId) const override {
    return rejected(identity::IdentityAllocationPhase::Definition);
  }

private:
  static ir::ExpandedIrIdentityResult rejected(identity::IdentityAllocationPhase phase) {
    zc::Maybe<zc::Array<uint8_t>> noKey;
    zc::Maybe<identity::UnbrandedSourceRange> noRange;
    auto invariant = identity::IdentityInvariant::from(
        identity::IdentityInvariantKind::InvalidHandle, phase, zc::mv(noKey), zc::mv(noRange),
        identity::IdentityApiSite::HandleLookup, 0);
    ZC_IF_SOME(value, invariant) { return ir::RejectedIrIdentityValue{zc::mv(value)}; }
    ZC_UNREACHABLE
  }
};

// The LIR verification rail is compiler-internal; its session owner carries a
// fixed, domain-named context fingerprint, mirroring the standalone IR
// rejection builders that run outside a live semantic context.
identity::ContextFingerprint sessionContext() {
  auto digest = identity::sha256("zom.lir-verification"_zc.asBytes());
  ZC_IF_SOME(value, digest) {
    return identity::ContextFingerprint::fromCanonicalDigest(zc::mv(value));
  }
  ZC_UNREACHABLE
}

// Admit one session-owned LIR verification invariant at a stable traversal
// ordinal and project it to the internal incident rail.
zc::Maybe<RejectedLirVerification> project(uint32_t traversalOrdinal) noexcept {
  UnusedIdentityResolver resolver;
  auto fallback = ir::IrFailureFallbackContext::from(
      ir::IrFailurePhase::LirVerification, ir::IrFailureOwner::session(sessionContext().clone()));
  if (fallback == zc::none) { return zc::none; }
  zc::Maybe<ir::IrFailureSite> noSite;
  zc::Maybe<identity::SourceSpan> noSpan;
  zc::Vector<uint32_t> noPath;
  auto descriptor = ir::IrFailureDescriptor::decoded(
      ir::IrRejectedBranch::IrInvariantRejected, ir::IrFailurePhase::LirVerification,
      ir::IrFailureKind::InvalidFact, ir::IrFailureOwner::session(sessionContext().clone()),
      zc::mv(noSite), ir::IrFailureDetail::none(), zc::mv(noSpan), zc::mv(noPath),
      traversalOrdinal);
  auto admitted =
      ir::IrFailureFactory::admit(zc::mv(descriptor), ZC_ASSERT_NONNULL(fallback), resolver);
  if (!admitted.template is<ir::AcceptedIrFailureDescriptor>()) { return zc::none; }
  zc::Vector<ir::IrFailureFact> facts;
  facts.add(zc::mv(admitted).template get<ir::AcceptedIrFailureDescriptor>().fact);
  auto sorted = ir::SortedIrInvariantFailureFacts::from(zc::mv(facts));
  if (sorted == zc::none) { return zc::none; }
  auto incidents = ir::IrDiagnosticProjector::projectAll(ZC_ASSERT_NONNULL(sorted));
  ZC_IF_SOME(value, incidents) { return RejectedLirVerification{zc::mv(value)}; }
  return zc::none;
}

}  // namespace

zc::Maybe<RejectedLirVerification> projectLirVerificationIncident(
    const LirVerificationFinding& finding) noexcept {
  // A stable ordinal over the fault tag and location gives the incident a
  // deterministic fingerprint for a given defect.
  const uint32_t ordinal = 0x1000 + static_cast<uint32_t>(finding.fault) * 0x100 +
                           static_cast<uint32_t>(finding.blockOrdinal) * 0x10 +
                           static_cast<uint32_t>(finding.statementIndex);
  return project(ordinal);
}

zc::Maybe<RejectedLirVerification> projectLirVerificationIncident(
    const TranslationFinding& finding) noexcept {
  const uint32_t ordinal = 0x2000 + static_cast<uint32_t>(finding.fault) * 0x100 +
                           static_cast<uint32_t>(finding.mirBlockOrdinal) * 0x10 +
                           static_cast<uint32_t>(finding.statementIndex);
  return project(ordinal);
}

}  // namespace zomlang::compiler::lir
