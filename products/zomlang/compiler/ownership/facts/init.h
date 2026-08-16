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

#pragma once

#include <cstdint>

#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ownership/facts/flow.h"
#include "zomlang/compiler/ownership/facts/paths.h"

namespace zomlang::compiler::ownership::facts {

/// \brief Three-bit initialization lattice state for one local at one MIR point.
struct InitializationState final {
  bool storageLive = false;
  bool mayBeInitialized = false;
  bool mustBeInitialized = false;

  ZC_NODISCARD static constexpr InitializationState dead() noexcept { return {}; }
  ZC_NODISCARD static constexpr InitializationState uninitialized() noexcept {
    return InitializationState{true, false, false};
  }
  ZC_NODISCARD static constexpr InitializationState initialized() noexcept {
    return InitializationState{true, true, true};
  }
  constexpr bool operator==(InitializationState other) const noexcept {
    return storageLive == other.storageLive && mayBeInitialized == other.mayBeInitialized &&
           mustBeInitialized == other.mustBeInitialized;
  }
  constexpr bool operator!=(InitializationState other) const noexcept { return !(*this == other); }
};

/// \brief The current-subset reason a root local is unavailable at one MIR point.
enum class InitializationLossKind : uint8_t {
  NeverInitialized = 0x01,
  Moved = 0x02,
  Deinitialized = 0x03,
  StorageEnded = 0x04,
};

/// \brief One event-anchored loss of availability for a root move path.
struct InitializationLossCause final {
  InitializationLossKind kind;
  MirEventKey event;
  mir::MirLocalId local;
};

/// \brief One root move-path state at one control-flow point in a function fact inventory.
struct InitializationFact final {
  MirPoint point;
  MovePathKey key;
  InitializationState state;
  zc::Vector<InitializationLossCause> lossCauses;
};

/// \brief Complete initialization-state inventory for one current-subset MIR function.
struct InitializationFunction final {
  identity::DefId owner;
  zc::Vector<InitializationFact> facts;
};

/// \brief One source-visible root-local use rejected before initialization facts publish.
enum class InitializationSourceFailureKind : uint8_t {
  UseAfterMove = 0x01,
  UninitializedPlaceUse = 0x02,
};

/// \brief One unavailable-state cause retained for a source-visible root-local use.
struct InitializationSourceFailureCause final {
  InitializationLossKind kind;
  MirEventKey event;
  identity::SourceSpan span;
};

/// \brief One source-visible root-local use rejected before initialization facts publish.
struct InitializationSourceFailure final {
  InitializationSourceFailureKind kind;
  identity::DefId owner;
  MirEventKey primary;
  identity::SourceSpan useSpan;
  MovePathKey place;
  zc::Vector<InitializationSourceFailureCause> unavailableCauses;
  uint32_t traversalOrdinal;
};

/// \brief Canonical ordering for source initialization failures.
struct InitializationSourceFailureOrdering final {
  ZC_NODISCARD static bool less(const InitializationSourceFailure& left,
                                const InitializationSourceFailure& right) noexcept;
};

/// \brief Successful source-use validation over independently verified initialization facts.
struct InitializationSourceAccepted final {};

/// \brief Ownership-specific source result for the initialization precheck.
///
/// This result is deliberately separate from RFC 0010 feature-boundary results:
/// ownership source rejections are inputs to RFC 0013 ownership analysis and
/// are legal only at ownership proof validation.
class InitializationSourceVerificationResult final {
public:
  using SourceFailures = ir::SortedSourceFailureFacts<InitializationSourceFailure,
                                                      InitializationSourceFailureOrdering>;

  InitializationSourceVerificationResult(InitializationSourceVerificationResult&&) noexcept =
      default;
  InitializationSourceVerificationResult& operator=(
      InitializationSourceVerificationResult&&) noexcept = default;
  ZC_DISALLOW_COPY(InitializationSourceVerificationResult);

  ZC_NODISCARD bool isVerified() const noexcept { return value.is<Verified>(); }
  ZC_NODISCARD bool isSourceRejected() const noexcept { return value.is<SourceRejected>(); }
  ZC_NODISCARD bool isIdentityInvariantRejected() const noexcept {
    return value.is<ir::IdentityInvariantRejectedIrOperation>();
  }
  ZC_NODISCARD bool isIrInvariantRejected() const noexcept {
    return value.is<ir::IrInvariantRejectedIrOperation>();
  }
  ZC_NODISCARD InitializationSourceAccepted&& takeVerified() && {
    return zc::mv(value.get<Verified>().value);
  }
  ZC_NODISCARD SourceFailures&& takeSourceFailures() && {
    return zc::mv(value.get<SourceRejected>().failures);
  }
  ZC_NODISCARD ir::SortedIdentityInvariantFacts&& takeIdentityFailures() && {
    return zc::mv(value.get<ir::IdentityInvariantRejectedIrOperation>().failures);
  }
  ZC_NODISCARD ir::SortedIrInvariantFailureFacts&& takeInvariantFailures() && {
    return zc::mv(value.get<ir::IrInvariantRejectedIrOperation>().failures);
  }

private:
  struct Verified final {
    InitializationSourceAccepted value;
  };
  struct SourceRejected final {
    SourceFailures failures;
  };

  ZC_NODISCARD static InitializationSourceVerificationResult verified(
      InitializationSourceAccepted&& value) noexcept {
    return InitializationSourceVerificationResult(Verified{zc::mv(value)});
  }
  ZC_NODISCARD static InitializationSourceVerificationResult sourceRejected(
      SourceFailures&& failures) noexcept {
    return InitializationSourceVerificationResult(SourceRejected{zc::mv(failures)});
  }
  ZC_NODISCARD static InitializationSourceVerificationResult identityInvariantRejected(
      ir::SortedIdentityInvariantFacts&& failures) noexcept {
    return InitializationSourceVerificationResult(
        ir::IdentityInvariantRejectedIrOperation{zc::mv(failures)});
  }
  ZC_NODISCARD static InitializationSourceVerificationResult irInvariantRejected(
      ir::SortedIrInvariantFailureFacts&& failures) noexcept {
    return InitializationSourceVerificationResult(
        ir::IrInvariantRejectedIrOperation{zc::mv(failures)});
  }

  explicit InitializationSourceVerificationResult(Verified&& result) noexcept
      : value(zc::mv(result)) {}
  explicit InitializationSourceVerificationResult(SourceRejected&& result) noexcept
      : value(zc::mv(result)) {}
  explicit InitializationSourceVerificationResult(
      ir::IdentityInvariantRejectedIrOperation&& result) noexcept
      : value(zc::mv(result)) {}
  explicit InitializationSourceVerificationResult(
      ir::IrInvariantRejectedIrOperation&& result) noexcept
      : value(zc::mv(result)) {}

  zc::OneOf<Verified, SourceRejected, ir::IdentityInvariantRejectedIrOperation,
            ir::IrInvariantRejectedIrOperation>
      value;

  friend class InitializationSourceVerifier;
};

/// \brief Untrusted initialization inventory awaiting independent reconstruction.
class InitializationCandidate final {
public:
  InitializationCandidate(identity::SemanticContextBrand semanticContext,
                          identity::SemanticContextFingerprint&& contextFingerprint,
                          identity::ModuleId module, mir::MirRevisionId builtRevision,
                          OwnershipEventOverlayRevision overlayRevision,
                          zc::Vector<InitializationFunction>&& functions) noexcept;
  InitializationCandidate(InitializationCandidate&&) noexcept = default;
  InitializationCandidate& operator=(InitializationCandidate&&) noexcept = delete;
  ZC_DISALLOW_COPY(InitializationCandidate);

  identity::SemanticContextBrand semanticContext;
  identity::SemanticContextFingerprint contextFingerprint;
  identity::ModuleId module;
  mir::MirRevisionId builtRevision;
  OwnershipEventOverlayRevision overlayRevision;
  zc::Vector<InitializationFunction> functions;
};

/// \brief Immutable initialization facts bound to Built MIR, event overlay, and move paths.
class VerifiedInitializationFacts final {
public:
  ~VerifiedInitializationFacts() noexcept(false);
  VerifiedInitializationFacts(VerifiedInitializationFacts&&) noexcept;
  VerifiedInitializationFacts& operator=(VerifiedInitializationFacts&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedInitializationFacts);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::SemanticContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const mir::MirRevisionId& builtRevision() const noexcept;
  ZC_NODISCARD const OwnershipEventOverlayRevision& overlayRevision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const InitializationFunction> functions() const noexcept;

private:
  struct Impl;
  explicit VerifiedInitializationFacts(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class InitializationVerifier;
};

/// \brief Independently validates source uses against verified initialization facts.
class InitializationSourceVerifier final {
public:
  ZC_NODISCARD static InitializationSourceVerificationResult verify(
      const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay,
      const VerifiedInitializationFacts& initialization);

private:
  ZC_NODISCARD static InitializationSourceVerificationResult reject(
      const mir::VerifiedBuiltMir& builtMir, const checker::CheckerIdentityAuthority& identities,
      ir::IrFailureKind kind, uint32_t ordinal);
};

/// \brief Derives current-subset initialization facts from verified MIR inputs.
class InitializationBuilder final {
public:
  ZC_NODISCARD static ir::IrOperationResult<InitializationCandidate> build(
      const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay,
      const VerifiedFlow& flow, const VerifiedMovePaths& movePaths);
};

/// \brief Independently validates and publishes initialization facts.
class InitializationVerifier final {
public:
  ZC_NODISCARD static ir::IrOperationResult<VerifiedInitializationFacts> verify(
      InitializationCandidate&& candidate, const mir::VerifiedBuiltMir& builtMir,
      const VerifiedOwnershipEventOverlay& overlay, const VerifiedFlow& flow,
      const VerifiedMovePaths& movePaths);
};

}  // namespace zomlang::compiler::ownership::facts
