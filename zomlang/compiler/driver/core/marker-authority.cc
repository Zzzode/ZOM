// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/core/marker-authority.h"

#include "zomlang/compiler/identity/canonical/canonical-encoder.h"
#include "zomlang/compiler/identity/crypto/sha256.h"

namespace zomlang::compiler::driver::core {
namespace {

constexpr zc::StringPtr kPolicyDomain = "zom.core-marker-policy"_zc;
constexpr zc::StringPtr kShapeDomain = "zom.core-marker-shape-inventory"_zc;
constexpr zc::StringPtr kRegistryDomain = "zom.core-marker-policy-registry"_zc;
constexpr zc::StringPtr kAuthorityDomain = "zom.standard-marker-authority"_zc;

zc::Array<uint8_t> frame(zc::StringPtr domain, zc::ArrayPtr<const uint8_t> payload) {
  auto result = zc::heapArray<uint8_t>(domain.size() + 1 + payload.size());
  size_t cursor = 0;
  for (const auto byte : domain.asBytes()) { result[cursor++] = byte; }
  result[cursor++] = 0;
  for (const auto byte : payload) { result[cursor++] = byte; }
  return result;
}

bool rolesAreCanonical(zc::ArrayPtr<const CoreMarkerRole> roles) {
  return roles.size() == 2 && roles[0].role == source::core::CoreSemanticRole::Copy &&
         roles[1].role == source::core::CoreSemanticRole::Linear &&
         roles[0].definition != roles[1].definition && roles[0].resolved != roles[1].resolved;
}

bool shapesAreCanonical(zc::ArrayPtr<const CoreMarkerShapeEntry> shapes) {
  return shapes.size() == 2 && shapes[0].role == source::core::CoreSemanticRole::Copy &&
         shapes[1].role == source::core::CoreSemanticRole::Linear &&
         shapes[0].definition != shapes[1].definition &&
         shapes[0].shape == checker::signature::InterfaceMarkerShape::ClosedMarker &&
         shapes[1].shape == checker::signature::InterfaceMarkerShape::ClosedMarker;
}

zc::Array<uint8_t> encodeShapeEntry(const CoreMarkerShapeEntry& entry) {
  identity::CanonicalEncoder encoder;
  encoder.encodeUint8(static_cast<uint8_t>(entry.role));
  entry.definition.encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(entry.shape));
  return encoder.finish();
}

bool sameRolesAndShapes(zc::ArrayPtr<const CoreMarkerRole> roles,
                        zc::ArrayPtr<const CoreMarkerShapeEntry> shapes) {
  if (!rolesAreCanonical(roles) || !shapesAreCanonical(shapes)) { return false; }
  for (size_t index = 0; index < roles.size(); ++index) {
    if (roles[index].role != shapes[index].role ||
        roles[index].definition != shapes[index].definition) {
      return false;
    }
  }
  return true;
}

zc::Maybe<identity::DefinitionKey> roleDefinition(zc::ArrayPtr<const CoreMarkerRole> roles,
                                                  source::core::CoreSemanticRole role) {
  for (const auto& entry : roles) {
    if (entry.role == role) { return entry.definition.clone(); }
  }
  return zc::none;
}

bool policyEntriesAreCanonical(zc::ArrayPtr<const CoreMarkerPolicyEntry> entries,
                               zc::ArrayPtr<const CoreMarkerShapeEntry> shapes) {
  return entries.size() == 1 && entries[0].role == source::core::CoreSemanticRole::Copy &&
         shapesAreCanonical(shapes) && entries[0].definition == shapes[0].definition;
}

zc::Array<uint8_t> encodePolicyEntry(const CoreMarkerPolicyEntry& entry) {
  identity::CanonicalEncoder encoder;
  encoder.encodeUint8(static_cast<uint8_t>(entry.role));
  entry.definition.encode(encoder);
  encoder.encodeByteString(entry.policy.encodeCanonical().asPtr());
  return encoder.finish();
}

zc::Array<uint8_t> encodeTemplatePolicy(const source::core::CoreMarkerPolicyTemplate& policy) {
  identity::CanonicalEncoder encoder;
  policy.encode(encoder);
  return encoder.finish();
}

template <typename Entry>
zc::Vector<Entry> cloneEntries(zc::ArrayPtr<const Entry> entries) {
  zc::Vector<Entry> result(entries.size());
  for (const auto& entry : entries) { result.add(entry.clone()); }
  return result;
}

}  // namespace

CoreMarkerRole CoreMarkerRole::clone() const { return {role, definition.clone(), resolved}; }

CoreMarkerShapeEntry CoreMarkerShapeEntry::clone() const {
  return {role, definition.clone(), shape};
}

CoreResolvedMarkerReferenceRule CoreResolvedMarkerReferenceRule::clone() const {
  zc::Maybe<identity::DefinitionKey> required;
  ZC_IF_SOME(value, requiredMarker) { required = value.clone(); }
  return {mutability, kind, zc::mv(required)};
}

struct CoreResolvedMarkerPolicy::Impl final {
  Impl(zc::Vector<source::core::CoreMarkerStructuralSubject>&& subjects,
       zc::Vector<type::semantic::PrimitiveKind>&& primitives,
       zc::Vector<CoreResolvedMarkerReferenceRule>&& references,
       zc::Vector<type::semantic::Mutability>&& pointers) noexcept
      : subjects(zc::mv(subjects)),
        primitives(zc::mv(primitives)),
        references(zc::mv(references)),
        pointers(zc::mv(pointers)) {}

  zc::Vector<source::core::CoreMarkerStructuralSubject> subjects;
  zc::Vector<type::semantic::PrimitiveKind> primitives;
  zc::Vector<CoreResolvedMarkerReferenceRule> references;
  zc::Vector<type::semantic::Mutability> pointers;
};

CoreResolvedMarkerPolicy::CoreResolvedMarkerPolicy(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
CoreResolvedMarkerPolicy::~CoreResolvedMarkerPolicy() noexcept(false) = default;
CoreResolvedMarkerPolicy::CoreResolvedMarkerPolicy(CoreResolvedMarkerPolicy&&) noexcept = default;
CoreResolvedMarkerPolicy& CoreResolvedMarkerPolicy::operator=(CoreResolvedMarkerPolicy&&) noexcept =
    default;

zc::Maybe<CoreResolvedMarkerPolicy> CoreResolvedMarkerPolicy::from(
    const source::core::CoreMarkerPolicyTemplate& policy,
    zc::ArrayPtr<const CoreMarkerRole> roles) {
  if (!rolesAreCanonical(roles) ||
      encodeTemplatePolicy(policy).asPtr() != encodeTemplatePolicy(policy.clone()).asPtr()) {
    return zc::none;
  }
  zc::Vector<CoreResolvedMarkerReferenceRule> references(policy.referenceRules().size());
  for (const auto& reference : policy.referenceRules()) {
    auto requiredRole = reference.rule.requiredRole();
    if (reference.rule.kind() == source::core::CoreMarkerReferenceTemplateRuleKind::Unconditional) {
      if (requiredRole != zc::none) { return zc::none; }
      references.add(
          CoreResolvedMarkerReferenceRule{reference.mutability, reference.rule.kind(), zc::none});
      continue;
    }
    if (reference.rule.kind() != source::core::CoreMarkerReferenceTemplateRuleKind::Requires ||
        requiredRole == zc::none) {
      return zc::none;
    }
    auto required = roleDefinition(roles, ZC_ASSERT_NONNULL(requiredRole));
    if (required == zc::none) { return zc::none; }
    references.add(CoreResolvedMarkerReferenceRule{reference.mutability, reference.rule.kind(),
                                                   zc::mv(ZC_ASSERT_NONNULL(required))});
  }
  zc::Vector<source::core::CoreMarkerStructuralSubject> subjects(
      policy.structuralSubjects().size());
  for (const auto subject : policy.structuralSubjects()) { subjects.add(subject); }
  zc::Vector<type::semantic::PrimitiveKind> primitives(policy.builtinPrimitives().size());
  for (const auto primitive : policy.builtinPrimitives()) { primitives.add(primitive); }
  zc::Vector<type::semantic::Mutability> pointers(policy.rawPointerMutabilities().size());
  for (const auto pointer : policy.rawPointerMutabilities()) { pointers.add(pointer); }
  return CoreResolvedMarkerPolicy(
      zc::heap<Impl>(zc::mv(subjects), zc::mv(primitives), zc::mv(references), zc::mv(pointers)));
}

CoreResolvedMarkerPolicy CoreResolvedMarkerPolicy::clone() const {
  zc::Vector<CoreResolvedMarkerReferenceRule> references(impl->references.size());
  for (const auto& reference : impl->references) { references.add(reference.clone()); }
  zc::Vector<source::core::CoreMarkerStructuralSubject> subjects(impl->subjects.size());
  for (const auto subject : impl->subjects) { subjects.add(subject); }
  zc::Vector<type::semantic::PrimitiveKind> primitives(impl->primitives.size());
  for (const auto primitive : impl->primitives) { primitives.add(primitive); }
  zc::Vector<type::semantic::Mutability> pointers(impl->pointers.size());
  for (const auto pointer : impl->pointers) { pointers.add(pointer); }
  return CoreResolvedMarkerPolicy(
      zc::heap<Impl>(zc::mv(subjects), zc::mv(primitives), zc::mv(references), zc::mv(pointers)));
}

zc::ArrayPtr<const source::core::CoreMarkerStructuralSubject>
CoreResolvedMarkerPolicy::structuralSubjects() const noexcept {
  return impl->subjects.asPtr();
}
zc::ArrayPtr<const type::semantic::PrimitiveKind> CoreResolvedMarkerPolicy::builtinPrimitives()
    const noexcept {
  return impl->primitives.asPtr();
}
zc::ArrayPtr<const CoreResolvedMarkerReferenceRule> CoreResolvedMarkerPolicy::referenceRules()
    const noexcept {
  return impl->references.asPtr();
}
zc::ArrayPtr<const type::semantic::Mutability> CoreResolvedMarkerPolicy::rawPointerMutabilities()
    const noexcept {
  return impl->pointers.asPtr();
}

zc::Array<uint8_t> CoreResolvedMarkerPolicy::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(structuralSubjects().size());
  for (const auto subject : structuralSubjects()) {
    encoder.encodeUint8(static_cast<uint8_t>(subject));
  }
  encoder.encodeSequenceSize(builtinPrimitives().size());
  for (const auto primitive : builtinPrimitives()) {
    encoder.encodeUint8(static_cast<uint8_t>(primitive));
  }
  encoder.encodeSequenceSize(referenceRules().size());
  for (const auto& reference : referenceRules()) {
    encoder.encodeUint8(static_cast<uint8_t>(reference.mutability));
    encoder.encodeUint8(static_cast<uint8_t>(reference.kind));
    encoder.encodeBool(reference.requiredMarker != zc::none);
    ZC_IF_SOME(marker, reference.requiredMarker) { marker.encode(encoder); }
  }
  encoder.encodeSequenceSize(rawPointerMutabilities().size());
  for (const auto pointer : rawPointerMutabilities()) {
    encoder.encodeUint8(static_cast<uint8_t>(pointer));
  }
  return frame(kPolicyDomain, encoder.finish().asPtr());
}

CoreMarkerPolicyEntry CoreMarkerPolicyEntry::clone() const {
  return {role, definition.clone(), policy.clone()};
}

#define ZOM_CORE_REVISION_IMPL(Type)                                                            \
  Type::Type(const identity::Sha256Digest& digest) noexcept : digestValue(digest) {}            \
  Type Type::fromDigest(const identity::Sha256Digest& digest) noexcept { return Type(digest); } \
  Type Type::clone() const noexcept { return Type(digestValue); }                               \
  const identity::Sha256Digest& Type::digest() const noexcept { return digestValue; }

ZOM_CORE_REVISION_IMPL(CoreMarkerShapeInventoryRevision)
ZOM_CORE_REVISION_IMPL(CoreMarkerPolicyRegistryRevision)
ZOM_CORE_REVISION_IMPL(CoreStandardMarkerAuthorityRevision)

#undef ZOM_CORE_REVISION_IMPL

struct VerifiedCoreMarkerShapeInventory::Impl final {
  Impl(identity::SemanticContextBrand context, identity::ContextFingerprint&& fingerprint,
       identity::CoreSemanticContextFingerprint&& coreContext,
       const identity::Sha256Digest& distribution, const identity::Sha256Digest& roleSeed,
       CoreMarkerShapeInventoryRevision revision,
       zc::Vector<CoreMarkerShapeEntry>&& shapes) noexcept
      : context(context),
        fingerprint(zc::mv(fingerprint)),
        coreContext(zc::mv(coreContext)),
        distribution(distribution),
        roleSeed(roleSeed),
        revision(zc::mv(revision)),
        shapes(zc::mv(shapes)) {}
  identity::SemanticContextBrand context;
  identity::ContextFingerprint fingerprint;
  identity::CoreSemanticContextFingerprint coreContext;
  identity::Sha256Digest distribution;
  identity::Sha256Digest roleSeed;
  CoreMarkerShapeInventoryRevision revision;
  zc::Vector<CoreMarkerShapeEntry> shapes;
};

VerifiedCoreMarkerShapeInventory::VerifiedCoreMarkerShapeInventory(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
VerifiedCoreMarkerShapeInventory::~VerifiedCoreMarkerShapeInventory() noexcept(false) = default;
VerifiedCoreMarkerShapeInventory::VerifiedCoreMarkerShapeInventory(
    VerifiedCoreMarkerShapeInventory&&) noexcept = default;
VerifiedCoreMarkerShapeInventory& VerifiedCoreMarkerShapeInventory::operator=(
    VerifiedCoreMarkerShapeInventory&&) noexcept = default;

zc::Maybe<VerifiedCoreMarkerShapeInventory> VerifiedCoreMarkerShapeInventory::from(
    identity::SemanticContextBrand context, identity::ContextFingerprint&& fingerprint,
    identity::CoreSemanticContextFingerprint&& coreContext,
    const identity::Sha256Digest& distribution, const identity::Sha256Digest& roleSeed,
    zc::Vector<CoreMarkerShapeEntry>&& shapes) {
  if (!context.isValid() || !shapesAreCanonical(shapes.asPtr())) { return zc::none; }
  identity::CanonicalEncoder encoder;
  encoder.encodeDigest(coreContext.digest());
  encoder.encodeDigest(roleSeed);
  encoder.encodeSequenceSize(shapes.size());
  for (const auto& shape : shapes) { encoder.encodeByteString(encodeShapeEntry(shape).asPtr()); }
  auto digest = identity::sha256(frame(kShapeDomain, encoder.finish().asPtr()).asPtr());
  if (digest == zc::none) { return zc::none; }
  return VerifiedCoreMarkerShapeInventory(zc::heap<Impl>(
      context, zc::mv(fingerprint), zc::mv(coreContext), distribution, roleSeed,
      CoreMarkerShapeInventoryRevision::fromDigest(ZC_ASSERT_NONNULL(digest)), zc::mv(shapes)));
}

VerifiedCoreMarkerShapeInventory VerifiedCoreMarkerShapeInventory::clone() const {
  return VerifiedCoreMarkerShapeInventory(zc::heap<Impl>(
      impl->context, impl->fingerprint.clone(), impl->coreContext.clone(), impl->distribution,
      impl->roleSeed, impl->revision.clone(), cloneEntries(impl->shapes.asPtr())));
}
identity::SemanticContextBrand VerifiedCoreMarkerShapeInventory::context() const noexcept {
  return impl->context;
}
const identity::ContextFingerprint& VerifiedCoreMarkerShapeInventory::fingerprint()
    const noexcept {
  return impl->fingerprint;
}
const identity::CoreSemanticContextFingerprint& VerifiedCoreMarkerShapeInventory::coreContext()
    const noexcept {
  return impl->coreContext;
}
const identity::Sha256Digest& VerifiedCoreMarkerShapeInventory::distribution() const noexcept {
  return impl->distribution;
}
const identity::Sha256Digest& VerifiedCoreMarkerShapeInventory::roleSeedRevision() const noexcept {
  return impl->roleSeed;
}
zc::ArrayPtr<const CoreMarkerShapeEntry> VerifiedCoreMarkerShapeInventory::shapes() const noexcept {
  return impl->shapes.asPtr();
}
const CoreMarkerShapeInventoryRevision& VerifiedCoreMarkerShapeInventory::revision()
    const noexcept {
  return impl->revision;
}
zc::Array<uint8_t> VerifiedCoreMarkerShapeInventory::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeDigest(fingerprint().digest());
  encoder.encodeDigest(coreContext().digest());
  encoder.encodeDigest(distribution());
  encoder.encodeDigest(roleSeedRevision());
  encoder.encodeDigest(revision().digest());
  encoder.encodeSequenceSize(shapes().size());
  for (const auto& shape : shapes()) { encoder.encodeByteString(encodeShapeEntry(shape).asPtr()); }
  return frame(kShapeDomain, encoder.finish().asPtr());
}

struct VerifiedCoreMarkerPolicyRegistry::Impl final {
  Impl(identity::SemanticContextBrand context, identity::ContextFingerprint&& fingerprint,
       identity::CoreSemanticContextFingerprint&& coreContext,
       const identity::Sha256Digest& distribution, const identity::Sha256Digest& roleSeed,
       const identity::Sha256Digest& templateRevision,
       CoreMarkerShapeInventoryRevision shapeRevision, CoreMarkerPolicyRegistryRevision revision,
       zc::Vector<CoreMarkerPolicyEntry>&& entries) noexcept
      : context(context),
        fingerprint(zc::mv(fingerprint)),
        coreContext(zc::mv(coreContext)),
        distribution(distribution),
        roleSeed(roleSeed),
        templateRevision(templateRevision),
        shapeRevision(zc::mv(shapeRevision)),
        revision(zc::mv(revision)),
        entries(zc::mv(entries)) {}
  identity::SemanticContextBrand context;
  identity::ContextFingerprint fingerprint;
  identity::CoreSemanticContextFingerprint coreContext;
  identity::Sha256Digest distribution;
  identity::Sha256Digest roleSeed;
  identity::Sha256Digest templateRevision;
  CoreMarkerShapeInventoryRevision shapeRevision;
  CoreMarkerPolicyRegistryRevision revision;
  zc::Vector<CoreMarkerPolicyEntry> entries;
};

VerifiedCoreMarkerPolicyRegistry::VerifiedCoreMarkerPolicyRegistry(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
VerifiedCoreMarkerPolicyRegistry::~VerifiedCoreMarkerPolicyRegistry() noexcept(false) = default;
VerifiedCoreMarkerPolicyRegistry::VerifiedCoreMarkerPolicyRegistry(
    VerifiedCoreMarkerPolicyRegistry&&) noexcept = default;
VerifiedCoreMarkerPolicyRegistry& VerifiedCoreMarkerPolicyRegistry::operator=(
    VerifiedCoreMarkerPolicyRegistry&&) noexcept = default;

zc::Maybe<VerifiedCoreMarkerPolicyRegistry> VerifiedCoreMarkerPolicyRegistry::from(
    identity::SemanticContextBrand context, identity::ContextFingerprint&& fingerprint,
    identity::CoreSemanticContextFingerprint&& coreContext,
    const identity::Sha256Digest& distribution, const identity::Sha256Digest& roleSeed,
    const identity::Sha256Digest& templateRevision, const VerifiedCoreMarkerShapeInventory& shapes,
    zc::Vector<CoreMarkerPolicyEntry>&& entries) {
  if (!context.isValid() || shapes.context() != context ||
      shapes.fingerprint().digest() != fingerprint.digest() ||
      shapes.coreContext().digest() != coreContext.digest() ||
      shapes.distribution() != distribution || shapes.roleSeedRevision() != roleSeed ||
      !policyEntriesAreCanonical(entries.asPtr(), shapes.shapes())) {
    return zc::none;
  }
  identity::CanonicalEncoder encoder;
  encoder.encodeDigest(coreContext.digest());
  encoder.encodeDigest(roleSeed);
  encoder.encodeDigest(templateRevision);
  encoder.encodeDigest(shapes.revision().digest());
  encoder.encodeSequenceSize(entries.size());
  for (const auto& entry : entries) { encoder.encodeByteString(encodePolicyEntry(entry).asPtr()); }
  auto digest = identity::sha256(frame(kRegistryDomain, encoder.finish().asPtr()).asPtr());
  if (digest == zc::none) { return zc::none; }
  return VerifiedCoreMarkerPolicyRegistry(zc::heap<Impl>(
      context, zc::mv(fingerprint), zc::mv(coreContext), distribution, roleSeed, templateRevision,
      shapes.revision().clone(),
      CoreMarkerPolicyRegistryRevision::fromDigest(ZC_ASSERT_NONNULL(digest)), zc::mv(entries)));
}

VerifiedCoreMarkerPolicyRegistry VerifiedCoreMarkerPolicyRegistry::clone() const {
  return VerifiedCoreMarkerPolicyRegistry(zc::heap<Impl>(
      impl->context, impl->fingerprint.clone(), impl->coreContext.clone(), impl->distribution,
      impl->roleSeed, impl->templateRevision, impl->shapeRevision.clone(), impl->revision.clone(),
      cloneEntries(impl->entries.asPtr())));
}
identity::SemanticContextBrand VerifiedCoreMarkerPolicyRegistry::context() const noexcept {
  return impl->context;
}
const identity::ContextFingerprint& VerifiedCoreMarkerPolicyRegistry::fingerprint()
    const noexcept {
  return impl->fingerprint;
}
const identity::CoreSemanticContextFingerprint& VerifiedCoreMarkerPolicyRegistry::coreContext()
    const noexcept {
  return impl->coreContext;
}
const identity::Sha256Digest& VerifiedCoreMarkerPolicyRegistry::distribution() const noexcept {
  return impl->distribution;
}
const identity::Sha256Digest& VerifiedCoreMarkerPolicyRegistry::roleSeedRevision() const noexcept {
  return impl->roleSeed;
}
const identity::Sha256Digest& VerifiedCoreMarkerPolicyRegistry::templateRevision() const noexcept {
  return impl->templateRevision;
}
const CoreMarkerShapeInventoryRevision& VerifiedCoreMarkerPolicyRegistry::shapeRevision()
    const noexcept {
  return impl->shapeRevision;
}
zc::ArrayPtr<const CoreMarkerPolicyEntry> VerifiedCoreMarkerPolicyRegistry::entries()
    const noexcept {
  return impl->entries.asPtr();
}
const CoreMarkerPolicyRegistryRevision& VerifiedCoreMarkerPolicyRegistry::revision()
    const noexcept {
  return impl->revision;
}
zc::Array<uint8_t> VerifiedCoreMarkerPolicyRegistry::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeDigest(fingerprint().digest());
  encoder.encodeDigest(coreContext().digest());
  encoder.encodeDigest(distribution());
  encoder.encodeDigest(roleSeedRevision());
  encoder.encodeDigest(templateRevision());
  encoder.encodeDigest(shapeRevision().digest());
  encoder.encodeDigest(revision().digest());
  encoder.encodeSequenceSize(entries().size());
  for (const auto& entry : entries()) {
    encoder.encodeByteString(encodePolicyEntry(entry).asPtr());
  }
  return frame(kRegistryDomain, encoder.finish().asPtr());
}

struct VerifiedCoreStandardMarkerAuthority::Impl final {
  Impl(identity::SemanticContextBrand context, identity::ContextFingerprint&& fingerprint,
       identity::CoreSemanticContextFingerprint&& coreContext,
       const identity::Sha256Digest& configurationRevision, CoreMarkerShapeInventoryRevision shape,
       CoreMarkerPolicyRegistryRevision policy, identity::ModuleKey&& prelude, identity::DefId copy,
       identity::DefId linear, CoreStandardMarkerAuthorityRevision revision) noexcept
      : context(context),
        fingerprint(zc::mv(fingerprint)),
        coreContext(zc::mv(coreContext)),
        configurationRevision(configurationRevision),
        shape(zc::mv(shape)),
        policy(zc::mv(policy)),
        prelude(zc::mv(prelude)),
        copy(copy),
        linear(linear),
        revision(zc::mv(revision)) {}
  identity::SemanticContextBrand context;
  identity::ContextFingerprint fingerprint;
  identity::CoreSemanticContextFingerprint coreContext;
  identity::Sha256Digest configurationRevision;
  CoreMarkerShapeInventoryRevision shape;
  CoreMarkerPolicyRegistryRevision policy;
  identity::ModuleKey prelude;
  identity::DefId copy;
  identity::DefId linear;
  CoreStandardMarkerAuthorityRevision revision;
};

VerifiedCoreStandardMarkerAuthority::VerifiedCoreStandardMarkerAuthority(
    zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
VerifiedCoreStandardMarkerAuthority::~VerifiedCoreStandardMarkerAuthority() noexcept(false) =
    default;
VerifiedCoreStandardMarkerAuthority::VerifiedCoreStandardMarkerAuthority(
    VerifiedCoreStandardMarkerAuthority&&) noexcept = default;
VerifiedCoreStandardMarkerAuthority& VerifiedCoreStandardMarkerAuthority::operator=(
    VerifiedCoreStandardMarkerAuthority&&) noexcept = default;

zc::Maybe<VerifiedCoreStandardMarkerAuthority> VerifiedCoreStandardMarkerAuthority::from(
    identity::SemanticContextBrand context, identity::ContextFingerprint&& fingerprint,
    identity::CoreSemanticContextFingerprint&& coreContext,
    const identity::Sha256Digest& configurationRevision,
    const VerifiedCoreMarkerShapeInventory& shapes,
    const VerifiedCoreMarkerPolicyRegistry& policies, identity::ModuleKey&& prelude,
    zc::Vector<CoreMarkerRole>&& roles) {
  if (!context.isValid() || !sameRolesAndShapes(roles.asPtr(), shapes.shapes()) ||
      shapes.context() != context || policies.context() != context ||
      shapes.fingerprint().digest() != fingerprint.digest() ||
      policies.fingerprint().digest() != fingerprint.digest() ||
      shapes.coreContext().digest() != coreContext.digest() ||
      policies.coreContext().digest() != coreContext.digest() ||
      policies.templateRevision() != configurationRevision ||
      policies.shapeRevision().digest() != shapes.revision().digest()) {
    return zc::none;
  }
  identity::CanonicalEncoder encoder;
  encoder.encodeDigest(coreContext.digest());
  encoder.encodeDigest(configurationRevision);
  encoder.encodeDigest(shapes.revision().digest());
  encoder.encodeDigest(policies.revision().digest());
  encoder.encodeByteString(prelude.encode().asPtr());
  roles[0].definition.encode(encoder);
  roles[1].definition.encode(encoder);
  auto digest = identity::sha256(frame(kAuthorityDomain, encoder.finish().asPtr()).asPtr());
  if (digest == zc::none) { return zc::none; }
  return VerifiedCoreStandardMarkerAuthority(
      zc::heap<Impl>(context, zc::mv(fingerprint), zc::mv(coreContext), configurationRevision,
                     shapes.revision().clone(), policies.revision().clone(), zc::mv(prelude),
                     roles[0].resolved, roles[1].resolved,
                     CoreStandardMarkerAuthorityRevision::fromDigest(ZC_ASSERT_NONNULL(digest))));
}

identity::SemanticContextBrand VerifiedCoreStandardMarkerAuthority::context() const noexcept {
  return impl->context;
}
const identity::ContextFingerprint& VerifiedCoreStandardMarkerAuthority::fingerprint()
    const noexcept {
  return impl->fingerprint;
}
const identity::CoreSemanticContextFingerprint& VerifiedCoreStandardMarkerAuthority::coreContext()
    const noexcept {
  return impl->coreContext;
}
const identity::Sha256Digest& VerifiedCoreStandardMarkerAuthority::configurationRevision()
    const noexcept {
  return impl->configurationRevision;
}
const CoreMarkerShapeInventoryRevision& VerifiedCoreStandardMarkerAuthority::shapeRevision()
    const noexcept {
  return impl->shape;
}
const CoreMarkerPolicyRegistryRevision& VerifiedCoreStandardMarkerAuthority::policyRevision()
    const noexcept {
  return impl->policy;
}
const identity::ModuleKey& VerifiedCoreStandardMarkerAuthority::prelude() const noexcept {
  return impl->prelude;
}
identity::DefId VerifiedCoreStandardMarkerAuthority::copy() const noexcept { return impl->copy; }
identity::DefId VerifiedCoreStandardMarkerAuthority::linear() const noexcept {
  return impl->linear;
}
const CoreStandardMarkerAuthorityRevision& VerifiedCoreStandardMarkerAuthority::revision()
    const noexcept {
  return impl->revision;
}
zc::Array<uint8_t> VerifiedCoreStandardMarkerAuthority::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeDigest(fingerprint().digest());
  encoder.encodeDigest(coreContext().digest());
  encoder.encodeDigest(configurationRevision());
  encoder.encodeDigest(shapeRevision().digest());
  encoder.encodeDigest(policyRevision().digest());
  encoder.encodeByteString(prelude().encode().asPtr());
  encoder.encodeDigest(revision().digest());
  return frame(kAuthorityDomain, encoder.finish().asPtr());
}

zc::Maybe<checker::signature::MarkerPolicyConfiguration> checkerConfig(
    const VerifiedCoreMarkerPolicyRegistry& policies) {
  zc::Vector<checker::signature::MarkerPolicyConfigurationEntry> entries(policies.entries().size());
  for (const auto& entry : policies.entries()) {
    zc::Vector<checker::signature::MarkerStructuralSubject> subjects(
        entry.policy.structuralSubjects().size());
    for (const auto subject : entry.policy.structuralSubjects()) {
      subjects.add(static_cast<checker::signature::MarkerStructuralSubject>(subject));
    }
    zc::Vector<type::semantic::PrimitiveKind> primitives(entry.policy.builtinPrimitives().size());
    for (const auto primitive : entry.policy.builtinPrimitives()) { primitives.add(primitive); }
    zc::Vector<checker::signature::MarkerPolicyReferenceConfiguration> references(
        entry.policy.referenceRules().size());
    for (const auto& rule : entry.policy.referenceRules()) {
      zc::Maybe<identity::DefinitionKey> required;
      if (rule.kind == source::core::CoreMarkerReferenceTemplateRuleKind::Requires) {
        if (rule.requiredMarker == zc::none) return zc::none;
        ZC_IF_SOME(value, rule.requiredMarker) { required = value.clone(); }
      } else if (rule.kind != source::core::CoreMarkerReferenceTemplateRuleKind::Unconditional ||
                 rule.requiredMarker != zc::none) {
        return zc::none;
      }
      references.add(
          checker::signature::MarkerPolicyReferenceConfiguration{rule.mutability, zc::mv(required)});
    }
    zc::Vector<type::semantic::Mutability> pointers(entry.policy.rawPointerMutabilities().size());
    for (const auto pointer : entry.policy.rawPointerMutabilities()) { pointers.add(pointer); }
    entries.add(checker::signature::MarkerPolicyConfigurationEntry{
        entry.definition.clone(), zc::mv(subjects), zc::mv(primitives), zc::mv(references),
        zc::mv(pointers)});
  }
  return checker::signature::MarkerPolicyConfiguration::from(zc::mv(entries));
}

}  // namespace zomlang::compiler::driver::core
