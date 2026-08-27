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

#include "compiler/ownership/facts/region-outlives.h"

#include "compiler/ir/ir-diagnostic-adapter.h"

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
  ZC_IREQUIRE(fallback != zc::none, "Region outlives failure fallback must be legal");
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

// ---------------------------------------------------------------------------
// Outlives derivation
// ---------------------------------------------------------------------------

/// \brief Orders two outlives facts canonically by `from` then `to`.
bool lessOutlives(const RegionOutlivesFact& left, const RegionOutlivesFact& right) noexcept {
  if (left.from != right.from) return left.from < right.from;
  return left.to < right.to;
}

/// \brief Sorts outlives facts into canonical (from, to) order via insertion sort.
void sortOutlives(zc::Vector<RegionOutlivesFact>& outlives) {
  for (size_t index = 1; index < outlives.size(); ++index) {
    auto current = zc::mv(outlives[index]);
    size_t insertion = index;
    while (insertion > 0 && lessOutlives(current, outlives[insertion - 1])) {
      outlives[insertion] = zc::mv(outlives[insertion - 1]);
      --insertion;
    }
    outlives[insertion] = zc::mv(current);
  }
}

/// \brief Returns true when every point in `subset` is also in `superset`.
bool isSubset(zc::ArrayPtr<const OwnershipPoint> subset,
              zc::ArrayPtr<const OwnershipPoint> superset) {
  for (const auto& point : subset) {
    bool found = false;
    for (const auto& candidate : superset) {
      if (point == candidate) {
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  return true;
}

/// \brief Sorts region keys into canonical order via insertion sort.
void sortRegions(zc::Vector<RegionKey>& regions) {
  for (size_t index = 1; index < regions.size(); ++index) {
    auto current = zc::mv(regions[index]);
    size_t insertion = index;
    while (insertion > 0 && current < regions[insertion - 1]) {
      regions[insertion] = zc::mv(regions[insertion - 1]);
      --insertion;
    }
    regions[insertion] = zc::mv(current);
  }
}

zc::Vector<RegionOutlivesFact> deriveOutlives(zc::ArrayPtr<const RegionMembership> memberships) {
  // Collect the distinct regions present in the membership inventory.
  zc::Vector<RegionKey> regions;
  for (const auto& membership : memberships) {
    bool present = false;
    for (const auto& existing : regions) {
      if (existing == membership.region) {
        present = true;
        break;
      }
    }
    if (!present) regions.add(membership.region.clone());
  }
  sortRegions(regions);

  // Build the live-point set of each region. The admitted subset carries only
  // a handful of points per region, so a linear scan per membership suffices.
  zc::Vector<zc::Vector<OwnershipPoint>> livePoints;
  livePoints.resize(regions.size());
  for (const auto& membership : memberships) {
    for (size_t index = 0; index < regions.size(); ++index) {
      if (regions[index] == membership.region) {
        livePoints[index].add(membership.point);
        break;
      }
    }
  }

  // Emit R1 outlives R2 for every distinct pair where points(R2) is a subset
  // of points(R1). Self-pairs are omitted: the primitive relation relates
  // distinct regions, and reflexivity is the closure's concern (RFC 0007).
  zc::Vector<RegionOutlivesFact> result;
  for (size_t from = 0; from < regions.size(); ++from) {
    for (size_t to = 0; to < regions.size(); ++to) {
      if (from == to) continue;
      if (isSubset(livePoints[to].asPtr(), livePoints[from].asPtr())) {
        result.add(RegionOutlivesFact{regions[from].clone(), regions[to].clone()});
      }
    }
  }
  sortOutlives(result);
  return result;
}

// ---------------------------------------------------------------------------
// Lineage comparison
// ---------------------------------------------------------------------------

bool sameOutlives(zc::ArrayPtr<const RegionOutlivesFact> left,
                  zc::ArrayPtr<const RegionOutlivesFact> right) {
  if (left.size() != right.size()) return false;
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].from != right[index].from || left[index].to != right[index].to) {
      return false;
    }
  }
  return true;
}

bool inputsMatch(const VerifiedRegionMemberships& memberships,
                 const mir::VerifiedBuiltMir& builtMir,
                 const VerifiedOwnershipEventOverlay& overlay) {
  return memberships.semanticContext() == builtMir.semanticContext() &&
         memberships.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         memberships.module() == builtMir.module() &&
         memberships.builtRevision().digest() == builtMir.revision().digest() &&
         memberships.overlayRevision().digest() == overlay.revision().digest() &&
         memberships.borrowEvidenceRevision().digest() ==
             builtMir.borrowEvidenceRevision().digest();
}

}  // namespace

RegionOutlivesCandidate::RegionOutlivesCandidate(
    identity::SemanticContextBrand semanticContext,
    identity::ContextFingerprint&& contextFingerprint, identity::ModuleId module,
    mir::MirRevisionId builtRevision, OwnershipEventOverlayRevision overlayRevision,
    driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision,
    zc::Vector<RegionOutlivesFact>&& outlives) noexcept
    : semanticContext(semanticContext),
      contextFingerprint(zc::mv(contextFingerprint)),
      module(module),
      builtRevision(builtRevision),
      overlayRevision(overlayRevision),
      borrowEvidenceRevision(borrowEvidenceRevision),
      outlives(zc::mv(outlives)) {}

struct VerifiedRegionOutlives::Impl final {
  explicit Impl(RegionOutlivesCandidate&& candidate) noexcept : candidate(zc::mv(candidate)) {}
  RegionOutlivesCandidate candidate;
};

VerifiedRegionOutlives::VerifiedRegionOutlives(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
VerifiedRegionOutlives::~VerifiedRegionOutlives() noexcept(false) = default;
VerifiedRegionOutlives::VerifiedRegionOutlives(VerifiedRegionOutlives&&) noexcept = default;
VerifiedRegionOutlives& VerifiedRegionOutlives::operator=(VerifiedRegionOutlives&&) noexcept =
    default;
identity::SemanticContextBrand VerifiedRegionOutlives::semanticContext() const noexcept {
  return impl->candidate.semanticContext;
}
const identity::ContextFingerprint& VerifiedRegionOutlives::contextFingerprint() const noexcept {
  return impl->candidate.contextFingerprint;
}
identity::ModuleId VerifiedRegionOutlives::module() const noexcept {
  return impl->candidate.module;
}
const mir::MirRevisionId& VerifiedRegionOutlives::builtRevision() const noexcept {
  return impl->candidate.builtRevision;
}
const OwnershipEventOverlayRevision& VerifiedRegionOutlives::overlayRevision() const noexcept {
  return impl->candidate.overlayRevision;
}
const driver::borrow_evidence::BorrowEvidenceRevision&
VerifiedRegionOutlives::borrowEvidenceRevision() const noexcept {
  return impl->candidate.borrowEvidenceRevision;
}
zc::ArrayPtr<const RegionOutlivesFact> VerifiedRegionOutlives::outlives() const noexcept {
  return impl->candidate.outlives.asPtr();
}

zc::Vector<RegionOutlivesFact> RegionOutlivesBuilder::derive(
    zc::ArrayPtr<const RegionMembership> memberships) {
  return deriveOutlives(memberships);
}

ir::IrOperationResult<RegionOutlivesCandidate> RegionOutlivesBuilder::build(
    const VerifiedRegionMemberships& memberships, const mir::VerifiedBuiltMir& builtMir,
    const VerifiedOwnershipEventOverlay& overlay) {
  const auto identities = builtMir.retainIdentityAuthority();
  if (!inputsMatch(memberships, builtMir, overlay)) {
    return reject<RegionOutlivesCandidate>(builtMir, identities,
                                           ir::IrFailureKind::InputRevisionMismatch, 0);
  }
  auto outlives = deriveOutlives(memberships.memberships());
  return ir::IrOperationResult<RegionOutlivesCandidate>::verified(
      RegionOutlivesCandidate(builtMir.semanticContext(), builtMir.contextFingerprint().clone(),
                              builtMir.module(), builtMir.revision(), overlay.revision(),
                              builtMir.borrowEvidenceRevision(), zc::mv(outlives)));
}

ir::IrOperationResult<VerifiedRegionOutlives> RegionOutlivesVerifier::verify(
    RegionOutlivesCandidate&& candidate, const VerifiedRegionMemberships& memberships,
    const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay) {
  const auto identities = builtMir.retainIdentityAuthority();
  if (candidate.semanticContext != builtMir.semanticContext() ||
      candidate.contextFingerprint.digest() != builtMir.contextFingerprint().digest() ||
      candidate.module != builtMir.module() ||
      candidate.builtRevision.digest() != builtMir.revision().digest() ||
      candidate.overlayRevision.digest() != overlay.revision().digest() ||
      candidate.borrowEvidenceRevision.digest() != builtMir.borrowEvidenceRevision().digest() ||
      !inputsMatch(memberships, builtMir, overlay)) {
    return reject<VerifiedRegionOutlives>(builtMir, identities,
                                          ir::IrFailureKind::InputRevisionMismatch, 0);
  }
  auto expected = deriveOutlives(memberships.memberships());
  if (!sameOutlives(candidate.outlives.asPtr(), expected.asPtr())) {
    return reject<VerifiedRegionOutlives>(builtMir, identities,
                                          ir::IrFailureKind::InvalidOwnershipProof, 1);
  }
  return ir::IrOperationResult<VerifiedRegionOutlives>::verified(
      VerifiedRegionOutlives(zc::heap<VerifiedRegionOutlives::Impl>(zc::mv(candidate))));
}

}  // namespace zomlang::compiler::ownership::facts
