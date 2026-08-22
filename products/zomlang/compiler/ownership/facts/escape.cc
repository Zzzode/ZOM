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

#include "zomlang/compiler/ownership/facts/escape.h"

#include "zomlang/compiler/ir/ir-failure.h"

namespace zomlang::compiler::ownership::facts {
namespace {

identity::IdentityInvariant invalidIdentity(identity::IdentityAllocationPhase phase,
                                            uint32_t ordinal) {
  zc::Maybe<zc::Array<uint8_t>> noKey;
  zc::Maybe<identity::UnbrandedSourceRange> noRange;
  auto invariant = identity::IdentityInvariant::from(
      identity::IdentityInvariantKind::InvalidHandle, phase, zc::mv(noKey), zc::mv(noRange),
      identity::IdentityApiSite::HandleLookup, ordinal);
  ZC_IF_SOME(value, invariant) { return zc::mv(value); }
  ZC_UNREACHABLE
}

/// \brief Identity resolver that rejects all expansion requests.
///
/// Escape verification does not need identity expansion; this resolver
/// satisfies the IrFailureFactory contract without retaining authority.
class AuthorityIdentityResolver final : public ir::IrFailureIdentityResolver {
public:
  explicit AuthorityIdentityResolver(const checker::CheckerIdentityAuthority& identities) noexcept
      : identities(identities) {}

  ir::ExpandedIrIdentityResult expand(identity::ModuleId module) const override {
    auto key = identities.module(module);
    if (key == zc::none) {
      return ir::RejectedIrIdentityValue{
          invalidIdentity(identity::IdentityAllocationPhase::Module, 0)};
    }
    ZC_IF_SOME(value, key) {
      auto expanded = ir::ExpandedIrIdentity::from(value.key().encode());
      ZC_IF_SOME(bytes, expanded) { return ir::ExpandedIrIdentityValue{zc::mv(bytes)}; }
    }
    return ir::RejectedIrIdentityValue{
        invalidIdentity(identity::IdentityAllocationPhase::Encoding, 0)};
  }

  ir::ExpandedIrIdentityResult expand(identity::DefId definition) const override {
    auto key = identities.definition(definition);
    if (key == zc::none) {
      return ir::RejectedIrIdentityValue{
          invalidIdentity(identity::IdentityAllocationPhase::Definition, 0)};
    }
    ZC_IF_SOME(value, key) {
      auto expanded = ir::ExpandedIrIdentity::from(value.key().encode());
      ZC_IF_SOME(bytes, expanded) { return ir::ExpandedIrIdentityValue{zc::mv(bytes)}; }
    }
    return ir::RejectedIrIdentityValue{
        invalidIdentity(identity::IdentityAllocationPhase::Encoding, 0)};
  }

  ir::ExpandedIrIdentityResult expand(ir::InstanceId) const override {
    return ir::RejectedIrIdentityValue{
        invalidIdentity(identity::IdentityAllocationPhase::Definition, 0)};
  }

private:
  const checker::CheckerIdentityAuthority& identities;
};

template <typename Result>
ir::IrOperationResult<Result> reject(const mir::VerifiedBuiltMir& builtMir,
                                     const checker::CheckerIdentityAuthority& identities,
                                     ir::IrFailureKind kind, uint32_t ordinal) {
  identity::DefId definition;
  if (builtMir.functions().size() != 0) definition = builtMir.functions()[0].owner;
  AuthorityIdentityResolver resolver(identities);
  auto fallback = ir::IrFailureFallbackContext::from(ir::IrFailurePhase::OwnershipProofValidation,
                                                     ir::IrFailureOwner::definition(definition));
  ZC_IREQUIRE(fallback != zc::none, "Escape failure fallback must be legal");
  zc::Maybe<ir::IrFailureSite> noSite;
  zc::Maybe<identity::SourceSpan> noSpan;
  zc::Vector<uint32_t> noPath;
  auto descriptor = ir::IrFailureDescriptor::decoded(
      ir::IrRejectedBranch::IrInvariantRejected, ir::IrFailurePhase::OwnershipProofValidation, kind,
      ir::IrFailureOwner::definition(definition), zc::mv(noSite), ir::IrFailureDetail::none(),
      zc::mv(noSpan), zc::mv(noPath), ordinal);
  ZC_IF_SOME(fallbackValue, fallback) {
    auto admitted = ir::IrFailureFactory::admit(zc::mv(descriptor), fallbackValue, resolver);
    if (admitted.is<ir::IdentityRejectedIrFailureDescriptor>()) {
      zc::Vector<identity::IdentityInvariant> failures;
      failures.add(zc::mv(admitted).get<ir::IdentityRejectedIrFailureDescriptor>().failure);
      auto sorted = ir::SortedIdentityInvariantFacts::from(zc::mv(failures));
      ZC_IF_SOME(values, sorted) {
        return ir::IrOperationResult<Result>::identityInvariantRejected(zc::mv(values));
      }
      ZC_UNREACHABLE
    }
    zc::Vector<ir::IrFailureFact> failures;
    failures.add(zc::mv(admitted).get<ir::AcceptedIrFailureDescriptor>().fact);
    auto sorted = ir::SortedIrInvariantFailureFacts::from(zc::mv(failures));
    ZC_IF_SOME(values, sorted) {
      return ir::IrOperationResult<Result>::irInvariantRejected(zc::mv(values));
    }
  }
  ZC_UNREACHABLE
}

/// \brief Checks that all verified inputs share the same lineage.
bool inputsMatch(const VerifiedFlow& flow, const VerifiedLoanFacts& loans,
                 const VerifiedReferenceDefinitions& references,
                 const mir::VerifiedBuiltMir& builtMir,
                 const VerifiedOwnershipEventOverlay& overlay) {
  return flow.semanticContext() == builtMir.semanticContext() &&
         flow.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         flow.module() == builtMir.module() &&
         flow.builtRevision().digest() == builtMir.revision().digest() &&
         flow.overlayRevision().digest() == overlay.revision().digest() &&
         loans.semanticContext() == builtMir.semanticContext() &&
         references.semanticContext() == builtMir.semanticContext();
}

}  // namespace

// ---------------------------------------------------------------------------
// EscapeCandidate
// ---------------------------------------------------------------------------

EscapeCandidate::EscapeCandidate(
    identity::SemanticContextBrand semanticContext,
    identity::ContextFingerprint&& contextFingerprint, identity::ModuleId module,
    mir::MirRevisionId builtRevision, OwnershipEventOverlayRevision overlayRevision,
    driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision,
    zc::Vector<EscapeFact>&& escapes) noexcept
    : semanticContext(semanticContext),
      contextFingerprint(zc::mv(contextFingerprint)),
      module(module),
      builtRevision(builtRevision),
      overlayRevision(overlayRevision),
      borrowEvidenceRevision(borrowEvidenceRevision),
      escapes(zc::mv(escapes)) {}

// ---------------------------------------------------------------------------
// VerifiedEscapeFacts
// ---------------------------------------------------------------------------

struct VerifiedEscapeFacts::Impl final {
  explicit Impl(EscapeCandidate&& candidate) noexcept : candidate(zc::mv(candidate)) {}

  EscapeCandidate candidate;
};

VerifiedEscapeFacts::VerifiedEscapeFacts(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
VerifiedEscapeFacts::VerifiedEscapeFacts(VerifiedEscapeFacts&&) noexcept = default;
VerifiedEscapeFacts& VerifiedEscapeFacts::operator=(VerifiedEscapeFacts&&) noexcept = default;
VerifiedEscapeFacts::~VerifiedEscapeFacts() noexcept(false) = default;

identity::SemanticContextBrand VerifiedEscapeFacts::semanticContext() const noexcept {
  return impl->candidate.semanticContext;
}
const identity::ContextFingerprint& VerifiedEscapeFacts::contextFingerprint() const noexcept {
  return impl->candidate.contextFingerprint;
}
identity::ModuleId VerifiedEscapeFacts::module() const noexcept { return impl->candidate.module; }
const mir::MirRevisionId& VerifiedEscapeFacts::builtRevision() const noexcept {
  return impl->candidate.builtRevision;
}
const OwnershipEventOverlayRevision& VerifiedEscapeFacts::overlayRevision() const noexcept {
  return impl->candidate.overlayRevision;
}
const driver::borrow_evidence::BorrowEvidenceRevision& VerifiedEscapeFacts::borrowEvidenceRevision()
    const noexcept {
  return impl->candidate.borrowEvidenceRevision;
}
zc::ArrayPtr<const EscapeFact> VerifiedEscapeFacts::escapes() const noexcept {
  return impl->candidate.escapes.asPtr();
}

// ---------------------------------------------------------------------------
// EscapeBuilder
// ---------------------------------------------------------------------------

ir::IrOperationResult<EscapeCandidate> EscapeBuilder::build(
    const VerifiedFlow& flow, const VerifiedLoanFacts& loans,
    const VerifiedReferenceDefinitions& references,
    const VerifiedOwnershipResourceFacts& /*resources*/, const mir::VerifiedBuiltMir& builtMir,
    const VerifiedOwnershipEventOverlay& overlay) {
  const auto identities = builtMir.retainIdentityAuthority();
  if (!inputsMatch(flow, loans, references, builtMir, overlay)) {
    return reject<EscapeCandidate>(builtMir, identities, ir::IrFailureKind::InputRevisionMismatch,
                                   0);
  }
  // The current straight-line MIR subset admits no escape operands. The builder
  // returns an empty inventory; the verifier independently confirms emptiness.
  return ir::IrOperationResult<EscapeCandidate>::verified(
      EscapeCandidate(builtMir.semanticContext(), builtMir.contextFingerprint().clone(),
                      builtMir.module(), builtMir.revision(), overlay.revision(),
                      builtMir.borrowEvidenceRevision(), zc::Vector<EscapeFact>{}));
}

// ---------------------------------------------------------------------------
// EscapeVerifier
// ---------------------------------------------------------------------------

ir::IrOperationResult<VerifiedEscapeFacts> EscapeVerifier::verify(
    EscapeCandidate&& candidate, const VerifiedFlow& flow, const VerifiedLoanFacts& loans,
    const VerifiedReferenceDefinitions& references,
    const VerifiedOwnershipResourceFacts& /*resources*/, const mir::VerifiedBuiltMir& builtMir,
    const VerifiedOwnershipEventOverlay& overlay) {
  const auto identities = builtMir.retainIdentityAuthority();
  if (candidate.semanticContext != builtMir.semanticContext() ||
      candidate.contextFingerprint.digest() != builtMir.contextFingerprint().digest() ||
      candidate.module != builtMir.module() ||
      candidate.builtRevision.digest() != builtMir.revision().digest() ||
      candidate.overlayRevision.digest() != overlay.revision().digest() ||
      candidate.borrowEvidenceRevision.digest() != builtMir.borrowEvidenceRevision().digest() ||
      !inputsMatch(flow, loans, references, builtMir, overlay)) {
    return reject<VerifiedEscapeFacts>(builtMir, identities,
                                       ir::IrFailureKind::InputRevisionMismatch, 0);
  }
  // The current straight-line MIR subset admits no escape operands. An empty
  // inventory is the only valid result. A non-empty candidate is rejected.
  if (!candidate.escapes.empty()) {
    return reject<VerifiedEscapeFacts>(builtMir, identities,
                                       ir::IrFailureKind::InvalidOwnershipProof, 1);
  }
  return ir::IrOperationResult<VerifiedEscapeFacts>::verified(
      VerifiedEscapeFacts(zc::heap<VerifiedEscapeFacts::Impl>(zc::mv(candidate))));
}

}  // namespace zomlang::compiler::ownership::facts
