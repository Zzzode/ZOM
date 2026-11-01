// Copyright (c) 2026 Zode.Z. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "compiler/identity/brand.h"
#include "compiler/identity/semantic/context-fingerprint.h"
#include "compiler/ir/diagnostics/ir-failure.h"
#include "compiler/ir/ir-identity.h"
#include "compiler/mir/built-mir.h"
#include "compiler/ownership/facts/ownership-facts-revision.h"
#include "compiler/ownership/overlay/ownership-event-overlay.h"

namespace zomlang::compiler::ownership {
class CheckedMir;
}  // namespace zomlang::compiler::ownership

namespace zomlang::compiler::mono {

/// \brief Fixed instance-count budget for one module's fixpoint expansion.
///
/// With the slice-1 admitted carrier set of two scalars and no
/// generic-to-generic edges, the reachable count is bounded by twice the
/// number of generic definitions; the fixed limit only guards resource use.
constexpr uint64_t kMonomorphizationInstanceBudget = 256;

/// \brief Fixed budget on type carrier slots rewritten across all instances.
constexpr uint64_t kMonomorphizationSubstitutionNodeBudget = 1ull << 20;

/// \brief Domain-separated immutable revision of one verified instance set.
class MonoInstanceSetRevisionId final {
public:
  constexpr MonoInstanceSetRevisionId() noexcept = default;

  ZC_NODISCARD static MonoInstanceSetRevisionId fromDigest(
      const identity::Sha256Digest& digest) noexcept;
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept { return digestValue; }

private:
  explicit MonoInstanceSetRevisionId(const identity::Sha256Digest& digest) noexcept
      : digestValue(digest) {}

  identity::Sha256Digest digestValue;
};

/// \brief Canonical revision framing for the RFC 0056 instance-set artifact.
///
/// Separate from MirRevisionCodec: the frame folds the context fingerprint,
/// expanded module key, the post-elaboration lineage triple, and every
/// instance as one length-framed expanded identity followed by its canonical
/// substituted body, in byte-sorted identity order.
class MonoInstanceSetRevisionCodec final {
public:
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encodeFramed(
      const identity::Sha256Digest& contextFingerprint,
      zc::ArrayPtr<const uint8_t> expandedModuleKey,
      const identity::Sha256Digest& builtRevision,
      const identity::Sha256Digest& eventOverlayRevision,
      const identity::Sha256Digest& factsRevision,
      zc::ArrayPtr<const zc::Array<uint8_t>> sortedIdentityFrames,
      zc::ArrayPtr<const zc::Array<uint8_t>> canonicalBodies);
  ZC_NODISCARD static zc::Maybe<MonoInstanceSetRevisionId> computeFramed(
      const identity::Sha256Digest& contextFingerprint,
      zc::ArrayPtr<const uint8_t> expandedModuleKey,
      const identity::Sha256Digest& builtRevision,
      const identity::Sha256Digest& eventOverlayRevision,
      const identity::Sha256Digest& factsRevision,
      zc::ArrayPtr<const zc::Array<uint8_t>> sortedIdentityFrames,
      zc::ArrayPtr<const zc::Array<uint8_t>> canonicalBodies);
};

/// \brief One monomorphized instance paired with its identity and canonical bytes.
struct MonoInstance final {
  ir::InstanceId identity;
  mir::MirFunction body;
  /// \brief Length-framed canonical instance identity, also the final sort key.
  zc::Array<uint8_t> identityFrame;
  /// \brief Canonical encoding of the substituted body, re-derived by the verifier.
  zc::Array<uint8_t> canonicalBody;
  /// \brief Source span of the root call edge that first demanded this instance.
  identity::SourceSpan callSiteSpan;
};

/// \brief Untrusted fixpoint output admitted only by the independent verifier.
class MonoInstanceSetCandidate final {
public:
  MonoInstanceSetCandidate(zc::Vector<MonoInstance>&& instances,
                           MonoInstanceSetRevisionId revision) noexcept;
  MonoInstanceSetCandidate(MonoInstanceSetCandidate&&) noexcept = default;
  MonoInstanceSetCandidate& operator=(MonoInstanceSetCandidate&&) noexcept = delete;
  ZC_DISALLOW_COPY(MonoInstanceSetCandidate);

  zc::Vector<MonoInstance> instances;
  MonoInstanceSetRevisionId revision;
};

/// \brief Immutable verified monomorphized instance set for one module.
///
/// Published per module in the same checkSources transaction as executable
/// MIR verification; an empty set is a valid published artifact for a module
/// with zero Instance edges.
class VerifiedMonoInstanceSet final {
public:
  ~VerifiedMonoInstanceSet() noexcept(false);
  VerifiedMonoInstanceSet(VerifiedMonoInstanceSet&&) noexcept;
  VerifiedMonoInstanceSet& operator=(VerifiedMonoInstanceSet&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedMonoInstanceSet);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const mir::MirRevisionId& builtRevision() const noexcept;
  ZC_NODISCARD const ownership::EventOverlayRevision& eventOverlayRevision() const noexcept;
  ZC_NODISCARD const ownership::facts::FactsRevision& factsRevision() const noexcept;
  ZC_NODISCARD const MonoInstanceSetRevisionId& revision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const MonoInstance> instances() const noexcept;

private:
  struct Impl;
  explicit VerifiedMonoInstanceSet(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend VerifiedMonoInstanceSet publishMonoInstanceSet(
      identity::SemanticContextBrand semanticContext, identity::ModuleId module,
      mir::MirRevisionId builtRevision, ownership::EventOverlayRevision eventOverlayRevision,
      ownership::facts::FactsRevision factsRevision, MonoInstanceSetRevisionId revision,
      zc::Vector<MonoInstance>&& instances) noexcept;
};

/// \brief Internal publication entry point defined beside the artifact Impl.
ZC_NODISCARD VerifiedMonoInstanceSet publishMonoInstanceSet(
    identity::SemanticContextBrand semanticContext, identity::ModuleId module,
    mir::MirRevisionId builtRevision, ownership::EventOverlayRevision eventOverlayRevision,
    ownership::facts::FactsRevision factsRevision, MonoInstanceSetRevisionId revision,
    zc::Vector<MonoInstance>&& instances) noexcept;

}  // namespace zomlang::compiler::mono
