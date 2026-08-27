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

#include "compiler/ownership/facts/capture.h"

#include "compiler/ir/ir-failure.h"

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
/// Capture verification does not need identity expansion; this resolver
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
  ZC_IREQUIRE(fallback != zc::none, "Capture failure fallback must be legal");
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
bool inputsMatch(const VerifiedMovePaths& movePaths, const mir::VerifiedBuiltMir& builtMir,
                 const VerifiedOwnershipEventOverlay& overlay) {
  return movePaths.semanticContext() == builtMir.semanticContext() &&
         movePaths.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         movePaths.module() == builtMir.module() &&
         movePaths.builtRevision().digest() == builtMir.revision().digest() &&
         movePaths.overlayRevision().digest() == overlay.revision().digest();
}

/// \brief Derives the capture inventory for the admitted subset.
///
/// Closures are not yet admitted by surface admission, so no MIR function can
/// construct a closure and the derived inventory is always empty. The
/// derivation activates here once closure construction reaches MIR.
zc::Vector<CaptureFact> derive() { return zc::Vector<CaptureFact>{}; }

}  // namespace

// ---------------------------------------------------------------------------
// CaptureCandidate
// ---------------------------------------------------------------------------

CaptureCandidate::CaptureCandidate(
    identity::SemanticContextBrand semanticContext,
    identity::ContextFingerprint&& contextFingerprint, identity::ModuleId module,
    mir::MirRevisionId builtRevision, OwnershipEventOverlayRevision overlayRevision,
    driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision,
    zc::Vector<CaptureFact>&& captures) noexcept
    : semanticContext(semanticContext),
      contextFingerprint(zc::mv(contextFingerprint)),
      module(module),
      builtRevision(builtRevision),
      overlayRevision(overlayRevision),
      borrowEvidenceRevision(borrowEvidenceRevision),
      captures(zc::mv(captures)) {}

// ---------------------------------------------------------------------------
// VerifiedCaptureFacts
// ---------------------------------------------------------------------------

struct VerifiedCaptureFacts::Impl final {
  explicit Impl(CaptureCandidate&& candidate) noexcept : candidate(zc::mv(candidate)) {}

  CaptureCandidate candidate;
};

VerifiedCaptureFacts::VerifiedCaptureFacts(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
VerifiedCaptureFacts::VerifiedCaptureFacts(VerifiedCaptureFacts&&) noexcept = default;
VerifiedCaptureFacts& VerifiedCaptureFacts::operator=(VerifiedCaptureFacts&&) noexcept = default;
VerifiedCaptureFacts::~VerifiedCaptureFacts() noexcept(false) = default;

identity::SemanticContextBrand VerifiedCaptureFacts::semanticContext() const noexcept {
  return impl->candidate.semanticContext;
}
const identity::ContextFingerprint& VerifiedCaptureFacts::contextFingerprint() const noexcept {
  return impl->candidate.contextFingerprint;
}
identity::ModuleId VerifiedCaptureFacts::module() const noexcept { return impl->candidate.module; }
const mir::MirRevisionId& VerifiedCaptureFacts::builtRevision() const noexcept {
  return impl->candidate.builtRevision;
}
const OwnershipEventOverlayRevision& VerifiedCaptureFacts::overlayRevision() const noexcept {
  return impl->candidate.overlayRevision;
}
const driver::borrow_evidence::BorrowEvidenceRevision&
VerifiedCaptureFacts::borrowEvidenceRevision() const noexcept {
  return impl->candidate.borrowEvidenceRevision;
}
zc::ArrayPtr<const CaptureFact> VerifiedCaptureFacts::captures() const noexcept {
  return impl->candidate.captures.asPtr();
}

// ---------------------------------------------------------------------------
// CaptureBuilder
// ---------------------------------------------------------------------------

ir::IrOperationResult<CaptureCandidate> CaptureBuilder::build(
    const VerifiedMovePaths& movePaths, const mir::VerifiedBuiltMir& builtMir,
    const VerifiedOwnershipEventOverlay& overlay) {
  const auto identities = builtMir.retainIdentityAuthority();
  if (!inputsMatch(movePaths, builtMir, overlay)) {
    return reject<CaptureCandidate>(builtMir, identities, ir::IrFailureKind::InputRevisionMismatch,
                                    0);
  }
  return ir::IrOperationResult<CaptureCandidate>::verified(CaptureCandidate(
      builtMir.semanticContext(), builtMir.contextFingerprint().clone(), builtMir.module(),
      builtMir.revision(), overlay.revision(), builtMir.borrowEvidenceRevision(), derive()));
}

// ---------------------------------------------------------------------------
// CaptureVerifier
// ---------------------------------------------------------------------------

ir::IrOperationResult<VerifiedCaptureFacts> CaptureVerifier::verify(
    CaptureCandidate&& candidate, const VerifiedMovePaths& movePaths,
    const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay) {
  const auto identities = builtMir.retainIdentityAuthority();
  if (candidate.semanticContext != builtMir.semanticContext() ||
      candidate.contextFingerprint.digest() != builtMir.contextFingerprint().digest() ||
      candidate.module != builtMir.module() ||
      candidate.builtRevision.digest() != builtMir.revision().digest() ||
      candidate.overlayRevision.digest() != overlay.revision().digest() ||
      candidate.borrowEvidenceRevision.digest() != builtMir.borrowEvidenceRevision().digest() ||
      !inputsMatch(movePaths, builtMir, overlay)) {
    return reject<VerifiedCaptureFacts>(builtMir, identities,
                                        ir::IrFailureKind::InputRevisionMismatch, 0);
  }
  // Independently reconstruct the expected inventory. The admitted subset
  // admits no closures, so the expected inventory is empty and any candidate
  // row is an invalid ownership proof.
  auto expected = derive();
  if (candidate.captures.size() != expected.size()) {
    return reject<VerifiedCaptureFacts>(builtMir, identities,
                                        ir::IrFailureKind::InvalidOwnershipProof, 1);
  }
  return ir::IrOperationResult<VerifiedCaptureFacts>::verified(
      VerifiedCaptureFacts(zc::heap<VerifiedCaptureFacts::Impl>(zc::mv(candidate))));
}

}  // namespace zomlang::compiler::ownership::facts
