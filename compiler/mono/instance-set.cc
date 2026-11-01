// Copyright (c) 2026 Zode.Z. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/mono/instance-set.h"

#include "zc/core/debug.h"
#include "compiler/identity/canonical/canonical-encoder.h"
#include "compiler/identity/crypto/sha256.h"
#include "compiler/ownership/overlay/ownership-checked-mir.h"

namespace zomlang::compiler::mono {

MonoInstanceSetRevisionId MonoInstanceSetRevisionId::fromDigest(
    const identity::Sha256Digest& digest) noexcept {
  return MonoInstanceSetRevisionId(digest);
}

zc::Maybe<zc::Array<uint8_t>> MonoInstanceSetRevisionCodec::encodeFramed(
    const identity::Sha256Digest& contextFingerprint, zc::ArrayPtr<const uint8_t> expandedModuleKey,
    const identity::Sha256Digest& builtRevision,
    const identity::Sha256Digest& eventOverlayRevision,
    const identity::Sha256Digest& factsRevision,
    zc::ArrayPtr<const zc::Array<uint8_t>> sortedIdentityFrames,
    zc::ArrayPtr<const zc::Array<uint8_t>> canonicalBodies) {
  if (expandedModuleKey.size() == 0 || sortedIdentityFrames.size() != canonicalBodies.size()) {
    return zc::none;
  }
  identity::CanonicalEncoder encoder;
  constexpr char domain[] = "zom.mono-instance-set-revision";
  for (size_t index = 0; index + 1 < sizeof(domain); ++index) {
    encoder.encodeUint8(static_cast<uint8_t>(domain[index]));
  }
  encoder.encodeUint8(0x00);
  encoder.encodeDigest(contextFingerprint);
  encoder.encodeByteString(expandedModuleKey);
  encoder.encodeDigest(builtRevision);
  encoder.encodeDigest(eventOverlayRevision);
  encoder.encodeDigest(factsRevision);
  encoder.encodeSequenceSize(sortedIdentityFrames.size());
  for (size_t index = 0; index < sortedIdentityFrames.size(); ++index) {
    if (sortedIdentityFrames[index].size() == 0 || canonicalBodies[index].size() == 0) {
      return zc::none;
    }
    encoder.encodeByteString(sortedIdentityFrames[index].asPtr());
    encoder.encodeByteString(canonicalBodies[index].asPtr());
  }
  return encoder.finish();
}

zc::Maybe<MonoInstanceSetRevisionId> MonoInstanceSetRevisionCodec::computeFramed(
    const identity::Sha256Digest& contextFingerprint, zc::ArrayPtr<const uint8_t> expandedModuleKey,
    const identity::Sha256Digest& builtRevision,
    const identity::Sha256Digest& eventOverlayRevision,
    const identity::Sha256Digest& factsRevision,
    zc::ArrayPtr<const zc::Array<uint8_t>> sortedIdentityFrames,
    zc::ArrayPtr<const zc::Array<uint8_t>> canonicalBodies) {
  auto bytes = encodeFramed(contextFingerprint, expandedModuleKey, builtRevision,
                            eventOverlayRevision, factsRevision, sortedIdentityFrames,
                            canonicalBodies);
  if (bytes == zc::none) { return zc::none; }
  auto digest = identity::sha256(ZC_ASSERT_NONNULL(bytes).asPtr());
  ZC_IF_SOME(value, digest) { return MonoInstanceSetRevisionId::fromDigest(value); }
  return zc::none;
}

MonoInstanceSetCandidate::MonoInstanceSetCandidate(zc::Vector<MonoInstance>&& instances,
                                                   MonoInstanceSetRevisionId revision) noexcept
    : instances(zc::mv(instances)), revision(revision) {}

struct VerifiedMonoInstanceSet::Impl final {
  Impl(identity::SemanticContextBrand semanticContext, identity::ModuleId module,
       mir::MirRevisionId builtRevision, ownership::EventOverlayRevision eventOverlayRevision,
       ownership::facts::FactsRevision factsRevision, MonoInstanceSetRevisionId revision,
       zc::Vector<MonoInstance>&& instances) noexcept
      : semanticContext(semanticContext),
        module(module),
        builtRevision(builtRevision),
        eventOverlayRevision(zc::mv(eventOverlayRevision)),
        factsRevision(zc::mv(factsRevision)),
        revision(revision),
        instances(zc::mv(instances)) {}

  identity::SemanticContextBrand semanticContext;
  identity::ModuleId module;
  mir::MirRevisionId builtRevision;
  ownership::EventOverlayRevision eventOverlayRevision;
  ownership::facts::FactsRevision factsRevision;
  MonoInstanceSetRevisionId revision;
  zc::Vector<MonoInstance> instances;
};

VerifiedMonoInstanceSet::VerifiedMonoInstanceSet(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
VerifiedMonoInstanceSet::~VerifiedMonoInstanceSet() noexcept(false) = default;
VerifiedMonoInstanceSet::VerifiedMonoInstanceSet(VerifiedMonoInstanceSet&&) noexcept = default;
VerifiedMonoInstanceSet& VerifiedMonoInstanceSet::operator=(VerifiedMonoInstanceSet&&) noexcept =
    default;

identity::SemanticContextBrand VerifiedMonoInstanceSet::semanticContext() const noexcept {
  return impl->semanticContext;
}
identity::ModuleId VerifiedMonoInstanceSet::module() const noexcept { return impl->module; }
const mir::MirRevisionId& VerifiedMonoInstanceSet::builtRevision() const noexcept {
  return impl->builtRevision;
}
const ownership::EventOverlayRevision& VerifiedMonoInstanceSet::eventOverlayRevision() const
    noexcept {
  return impl->eventOverlayRevision;
}
const ownership::facts::FactsRevision& VerifiedMonoInstanceSet::factsRevision() const noexcept {
  return impl->factsRevision;
}
const MonoInstanceSetRevisionId& VerifiedMonoInstanceSet::revision() const noexcept {
  return impl->revision;
}
zc::ArrayPtr<const MonoInstance> VerifiedMonoInstanceSet::instances() const noexcept {
  return impl->instances.asPtr();
}

VerifiedMonoInstanceSet publishMonoInstanceSet(
    identity::SemanticContextBrand semanticContext, identity::ModuleId module,
    mir::MirRevisionId builtRevision, ownership::EventOverlayRevision eventOverlayRevision,
    ownership::facts::FactsRevision factsRevision, MonoInstanceSetRevisionId revision,
    zc::Vector<MonoInstance>&& instances) noexcept {
  auto impl = zc::heap<VerifiedMonoInstanceSet::Impl>(
      semanticContext, module, builtRevision, zc::mv(eventOverlayRevision), zc::mv(factsRevision),
      revision, zc::mv(instances));
  return VerifiedMonoInstanceSet(zc::mv(impl));
}

}  // namespace zomlang::compiler::mono
