// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/driver/core/signature.h"

#include "compiler/ast/generated/node-payload.h"
#include "compiler/checker/checker-identity-authority.h"
#include "compiler/driver/core/query.h"
#include "compiler/driver/query/module-graph/materialized-module-graph-query.h"
#include "compiler/identity/canonical/canonical-decoder.h"
#include "compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::driver::core {

bool isInitialMarkerInterface(const binder::NamedItemSyntax& syntax) {
  const auto& detached = syntax.detachedSyntax();
  if (detached.rootCount() != 1 || detached.nodes().size() != 2) { return false; }
  const auto& interface = detached.nodes()[0];
  const auto& members = detached.nodes()[1];
  if (interface.syntaxKind() != ast::SyntaxKind::InterfaceDecl ||
      members.syntaxKind() != ast::SyntaxKind::ClassMemberList || interface.childCount() != 1 ||
      members.childCount() != 0) {
    return false;
  }
  auto typeParameters = interface.childField(1);
  auto inheritedInterfaces = interface.childField(2);
  auto memberList = interface.childField(3);
  auto memberItems = members.childField(1);
  return typeParameters != zc::none && inheritedInterfaces != zc::none && memberList != zc::none &&
         memberItems != zc::none && !ZC_ASSERT_NONNULL(typeParameters).present &&
         !ZC_ASSERT_NONNULL(inheritedInterfaces).present && ZC_ASSERT_NONNULL(memberList).present &&
         ZC_ASSERT_NONNULL(memberList).firstChildOrdinal == 0 &&
         ZC_ASSERT_NONNULL(memberList).childCount == 1 && ZC_ASSERT_NONNULL(memberItems).present &&
         ZC_ASSERT_NONNULL(memberItems).firstChildOrdinal == 0 &&
         ZC_ASSERT_NONNULL(memberItems).childCount == 0;
}

namespace {

bool hasNoInitialBodies(const module_graph_query::VerifiedBoundModule& bound) {
  if (bound.definitions().impls().size() != 0 ||
      bound.definitions().genericParameters().size() != 0 ||
      bound.definitions().callableParameters().size() != 0) {
    return false;
  }
  for (const auto& lease : bound.ownerBodyLeases()) {
    const auto& body = lease.capability();
    if (body.materializedOwnerLocalBindings().size() != 0 ||
        body.materializedAnonymousEntities().size() != 0 ||
        body.materializedResolutions().size() != 0 || body.materializedSelfTypes().size() != 0 ||
        body.materializedThisBindings().size() != 0 ||
        body.materializedShadowTargets().size() != 0 || body.materializedLabels().size() != 0 ||
        body.materializedControlTransfers().size() != 0 ||
        body.materializedClosureFreeVariables().size() != 0 ||
        body.materializedExplicitClosureCaptures().size() != 0 ||
        body.materializedFailedLookups().size() != 0 ||
        body.materializedDeferredMembers().size() != 0) {
      return false;
    }
  }
  return true;
}

bool exportsExactlyRoles(const binder::VerifiedExportSurface& surface,
                         const core_library_query::VerifiedCoreRoleSeed& seed) {
  if (surface.exports().size() != seed.roles().size()) { return false; }
  for (const auto& role : seed.roles()) {
    size_t matches = 0;
    for (const auto& entry : surface.exports()) {
      const auto& target = entry.canonicalTarget.value();
      if (!target.is<binder::DefinitionBindingTarget>() ||
          target.get<binder::DefinitionBindingTarget>().definition != role.definition) {
        continue;
      }
      if (!entry.exported || !entry.visibility.value().is<binder::ExternalVisibility>()) {
        return false;
      }
      ++matches;
    }
    if (matches != 1) { return false; }
  }
  return true;
}

bool sameSource(const binder::ImportBindingFact& import,
                const core_library_query::VerifiedCoreBootstrapModuleInterface& source) {
  return import.sourceModule == source.boundModuleLease().capability().definitions().module() &&
         import.sourceRevision.digest() ==
             source.boundModuleLease().capability().bindingSurface().revision().digest();
}

zc::Array<uint8_t> frame(zc::StringPtr domain, zc::ArrayPtr<const uint8_t> payload) {
  auto result = zc::heapArray<uint8_t>(domain.size() + 1 + payload.size());
  size_t cursor = 0;
  for (const auto byte : domain.asBytes()) { result[cursor++] = byte; }
  result[cursor++] = 0;
  for (const auto byte : payload) { result[cursor++] = byte; }
  return result;
}

zc::Maybe<const binder::MaterializedDefinitionInventoryEntry&> findDefinition(
    const module_graph_query::VerifiedBoundModule& bound, identity::DefId definition) {
  zc::Maybe<const binder::MaterializedDefinitionInventoryEntry&> result;
  for (const auto& candidate : bound.definitions().definitions()) {
    if (candidate.definition != definition) { continue; }
    if (result != zc::none) { return zc::none; }
    result = candidate;
  }
  return result;
}

bool decodeEmptySequence(identity::CanonicalDecoder& decoder) {
  auto size = decoder.decodeSequenceSize(0);
  return size != zc::none && ZC_ASSERT_NONNULL(size) == 0;
}

zc::Array<uint8_t> encodeTypeFreeInterfaceSignatureRecord(const identity::DefinitionKey& definition,
                                                          const identity::SourceFileKey& source,
                                                          uint64_t byteStart, uint64_t byteEnd) {
  identity::CanonicalEncoder encoder;
  definition.encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(identity::DefinitionKind::Interface));
  encoder.encodeUint8(0x01);
  encoder.encodeSequenceSize(0);
  encoder.encodeSequenceSize(0);
  encoder.encodeUint8(0x03);
  encoder.encodeSequenceSize(0);
  encoder.encodeSequenceSize(0);
  encoder.encodeSequenceSize(0);
  encoder.encodeSequenceSize(0);
  encoder.encodeBool(true);
  encoder.encodeSequenceSize(0);
  source.encode(encoder);
  encoder.encodeUint64(byteStart);
  encoder.encodeUint64(byteEnd);
  return encoder.finish();
}

}  // namespace

bool matchesInitialSurface(core_library_query::CoreBootstrapModuleSurface surface,
                           const module_graph_query::VerifiedBoundModule& bound,
                           const core_library_query::VerifiedCoreRoleSeed& seed) {
  if (!hasNoInitialBodies(bound)) { return false; }
  switch (surface) {
    case core_library_query::CoreBootstrapModuleSurface::Root:
      return bound.definitions().definitions().size() == 0 &&
             bound.bindings().imports().size() == 0 && bound.bindingSurface().exports().size() == 0;
    case core_library_query::CoreBootstrapModuleSurface::Marker:
      return bound.module().encode().asPtr() ==
                 seed.markerBoundModuleLease().capability().module().encode().asPtr() &&
             bound.definitions().definitions().size() == seed.roles().size() &&
             exportsExactlyRoles(bound.bindingSurface(), seed);
    case core_library_query::CoreBootstrapModuleSurface::Prelude:
      return bound.definitions().definitions().size() == 0 &&
             exportsExactlyRoles(bound.bindingSurface(), seed);
  }
  return false;
}

struct TypeFreeInterfaceSignatureRecord::Impl final {
  Impl(identity::DefinitionKey&& definition, identity::SourceFileKey&& source, uint64_t byteStart,
       uint64_t byteEnd) noexcept
      : definition(zc::mv(definition)),
        source(zc::mv(source)),
        byteStart(byteStart),
        byteEnd(byteEnd) {}

  identity::DefinitionKey definition;
  identity::SourceFileKey source;
  uint64_t byteStart;
  uint64_t byteEnd;
};

TypeFreeInterfaceSignatureRecord::TypeFreeInterfaceSignatureRecord(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
TypeFreeInterfaceSignatureRecord::~TypeFreeInterfaceSignatureRecord() noexcept(false) = default;
TypeFreeInterfaceSignatureRecord::TypeFreeInterfaceSignatureRecord(
    TypeFreeInterfaceSignatureRecord&&) noexcept = default;
TypeFreeInterfaceSignatureRecord& TypeFreeInterfaceSignatureRecord::operator=(
    TypeFreeInterfaceSignatureRecord&&) noexcept = default;

zc::Maybe<TypeFreeInterfaceSignatureRecord> TypeFreeInterfaceSignatureRecord::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto definitionDigest = decoder.decodeDigest();
  if (definitionDigest == zc::none) { return zc::none; }
  auto definition = identity::DefinitionKey::fromBytes(ZC_ASSERT_NONNULL(definitionDigest).bytes());
  auto kind = decoder.decodeUint8();
  auto scope = decoder.decodeUint8();
  if (definition == zc::none || kind == zc::none || scope == zc::none ||
      ZC_ASSERT_NONNULL(kind) != static_cast<uint8_t>(identity::DefinitionKind::Interface) ||
      ZC_ASSERT_NONNULL(scope) != 0x01 || !decodeEmptySequence(decoder) ||
      !decodeEmptySequence(decoder)) {
    return zc::none;
  }
  auto payload = decoder.decodeUint8();
  if (payload == zc::none || ZC_ASSERT_NONNULL(payload) != 0x03 || !decodeEmptySequence(decoder) ||
      !decodeEmptySequence(decoder) || !decodeEmptySequence(decoder) ||
      !decodeEmptySequence(decoder)) {
    return zc::none;
  }
  auto markerOnly = decoder.decodeBool();
  if (markerOnly == zc::none || !ZC_ASSERT_NONNULL(markerOnly) || !decodeEmptySequence(decoder)) {
    return zc::none;
  }
  auto source = identity::SourceFileKey::decodeCanonical(decoder);
  auto byteStart = decoder.decodeUint64();
  auto byteEnd = decoder.decodeUint64();
  if (source == zc::none || byteStart == zc::none || byteEnd == zc::none || !decoder.finished()) {
    return zc::none;
  }
  auto record = TypeFreeInterfaceSignatureRecord(
      zc::heap<Impl>(zc::mv(ZC_ASSERT_NONNULL(definition)), zc::mv(ZC_ASSERT_NONNULL(source)),
                     ZC_ASSERT_NONNULL(byteStart), ZC_ASSERT_NONNULL(byteEnd)));
  return record.encodeCanonical().asPtr() == bytes
             ? zc::Maybe<TypeFreeInterfaceSignatureRecord>(zc::mv(record))
             : zc::none;
}

TypeFreeInterfaceSignatureRecord TypeFreeInterfaceSignatureRecord::clone() const {
  return TypeFreeInterfaceSignatureRecord(zc::heap<Impl>(
      impl->definition.clone(), impl->source.clone(), impl->byteStart, impl->byteEnd));
}
const identity::DefinitionKey& TypeFreeInterfaceSignatureRecord::definition() const noexcept {
  return impl->definition;
}
const identity::SourceFileKey& TypeFreeInterfaceSignatureRecord::source() const noexcept {
  return impl->source;
}
uint64_t TypeFreeInterfaceSignatureRecord::byteStart() const noexcept { return impl->byteStart; }
uint64_t TypeFreeInterfaceSignatureRecord::byteEnd() const noexcept { return impl->byteEnd; }
zc::Array<uint8_t> TypeFreeInterfaceSignatureRecord::encodeCanonical() const {
  return encodeTypeFreeInterfaceSignatureRecord(definition(), source(), byteStart(), byteEnd());
}

struct VerifiedCoreSignatureFacts::Impl final {
  Impl(core_library_query::CoreBootstrapModuleSurface surface,
       zc::Vector<CoreSignatureFact>&& facts) noexcept
      : surface(surface), facts(zc::mv(facts)) {}

  core_library_query::CoreBootstrapModuleSurface surface;
  zc::Vector<CoreSignatureFact> facts;
};

VerifiedCoreSignatureFacts::VerifiedCoreSignatureFacts(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
VerifiedCoreSignatureFacts::~VerifiedCoreSignatureFacts() noexcept(false) = default;
VerifiedCoreSignatureFacts::VerifiedCoreSignatureFacts(VerifiedCoreSignatureFacts&&) noexcept =
    default;
VerifiedCoreSignatureFacts& VerifiedCoreSignatureFacts::operator=(
    VerifiedCoreSignatureFacts&&) noexcept = default;

zc::Maybe<VerifiedCoreSignatureFacts> VerifiedCoreSignatureFacts::from(
    core_library_query::CoreBootstrapModuleSurface surface,
    const module_graph_query::VerifiedBoundModule& bound,
    const core_library_query::VerifiedCoreRoleSeed& seed,
    const checker::CheckerIdentityAuthority& identities) {
  if (!matchesInitialSurface(surface, bound, seed) ||
      bound.context() != identities.semanticContext() ||
      bound.fingerprint().digest() != identities.fingerprint().digest()) {
    return zc::none;
  }
  zc::Vector<CoreSignatureFact> facts;
  if (surface == core_library_query::CoreBootstrapModuleSurface::Marker) {
    facts = zc::Vector<CoreSignatureFact>(seed.roles().size());
    for (const auto& role : seed.roles()) {
      auto definition = findDefinition(bound, role.definition);
      auto identity = identities.definition(role.definition);
      if (definition == zc::none || identity == zc::none) { return zc::none; }
      ZC_IF_SOME(entry, definition) {
        ZC_IF_SOME(identityEntry, identity) {
          if (entry.key != role.key || entry.record.kind() != identity::DefinitionKind::Interface ||
              identityEntry.key() != role.key ||
              identityEntry.record().module().encode().asPtr() != bound.module().encode().asPtr()) {
            return zc::none;
          }
          checker::signature::SemanticSignature signature{
              role.definition,
              identity::DefinitionKind::Interface,
              checker::signature::SignatureScope(
                  checker::signature::ModuleDefinitionSignatureScope{}),
              zc::Vector<checker::signature::SignatureModifier>(),
              zc::Vector<checker::signature::NormalizedAttributeFact>(),
              checker::signature::SemanticSignaturePayload(checker::signature::InterfaceSignature{
                  zc::Vector<checker::signature::GenericParameterSignature>(),
                  zc::Vector<checker::signature::InterfaceInstantiation>(),
                  zc::Vector<identity::DefId>(), zc::Vector<identity::DefId>(), true,
                  zc::Vector<checker::signature::ObjectSafetyCause>()}),
              entry.source.clone()};
          auto canonical =
              checker::signature::SignatureFactsCanonicalCodec::encodeTypeFreeInterfaceSignature(
                  signature, bound.definitions().module(), identities);
          if (canonical == zc::none) { return zc::none; }
          auto decoded = TypeFreeInterfaceSignatureRecord::decodeCanonical(
              ZC_ASSERT_NONNULL(canonical).asPtr());
          if (decoded == zc::none || ZC_ASSERT_NONNULL(decoded).definition() != role.key ||
              !ZC_ASSERT_NONNULL(decoded).source().sameAs(entry.source.source()) ||
              ZC_ASSERT_NONNULL(decoded).byteStart() != entry.source.byteStart() ||
              ZC_ASSERT_NONNULL(decoded).byteEnd() != entry.source.byteEnd()) {
            return zc::none;
          }
          facts.add(CoreSignatureFact{role.role, zc::mv(signature),
                                      zc::mv(ZC_ASSERT_NONNULL(canonical))});
        }
      }
    }
  }
  return VerifiedCoreSignatureFacts(zc::heap<Impl>(surface, zc::mv(facts)));
}

core_library_query::CoreBootstrapModuleSurface VerifiedCoreSignatureFacts::surface()
    const noexcept {
  return impl->surface;
}

zc::ArrayPtr<const CoreSignatureFact> VerifiedCoreSignatureFacts::facts() const noexcept {
  return impl->facts.asPtr();
}

zc::Array<uint8_t> VerifiedCoreSignatureFacts::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeUint8(static_cast<uint8_t>(surface()));
  encoder.encodeSequenceSize(facts().size());
  for (const auto& fact : facts()) {
    encoder.encodeUint8(static_cast<uint8_t>(fact.role));
    encoder.encodeByteString(fact.canonical.asPtr());
  }
  return frame("zom.core-bootstrap-signature-facts"_zc, encoder.finish().asPtr());
}

struct VerifiedCoreImportedSignatureView::Impl final {
  Impl(identity::ModuleId requester, identity::ModuleKey&& requesterKey,
       identity::CoreSemanticContextFingerprint&& coreContext,
       zc::Vector<BootstrapInterfaceLease>&& sources) noexcept
      : requester(requester),
        requesterKey(zc::mv(requesterKey)),
        coreContext(zc::mv(coreContext)),
        sources(zc::mv(sources)) {}

  identity::ModuleId requester;
  identity::ModuleKey requesterKey;
  identity::CoreSemanticContextFingerprint coreContext;
  zc::Vector<BootstrapInterfaceLease> sources;
};

VerifiedCoreImportedSignatureView::VerifiedCoreImportedSignatureView(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
VerifiedCoreImportedSignatureView::~VerifiedCoreImportedSignatureView() noexcept(false) = default;
VerifiedCoreImportedSignatureView::VerifiedCoreImportedSignatureView(
    VerifiedCoreImportedSignatureView&&) noexcept = default;
VerifiedCoreImportedSignatureView& VerifiedCoreImportedSignatureView::operator=(
    VerifiedCoreImportedSignatureView&&) noexcept = default;

zc::Maybe<VerifiedCoreImportedSignatureView> VerifiedCoreImportedSignatureView::from(
    const module_graph_query::CheckerBoundModuleView& requester,
    const identity::CoreSemanticContextFingerprint& coreContext,
    zc::Vector<BootstrapInterfaceLease>&& sources) {
  const auto& bound = requester.boundModuleLease().capability();
  if (requester.semanticContext() != bound.context() ||
      requester.semanticFingerprint().digest() != bound.fingerprint().digest()) {
    return zc::none;
  }
  for (size_t index = 0; index < sources.size(); ++index) {
    const auto& source = sources[index].capability();
    if (source.context() != requester.semanticContext() ||
        source.fingerprint().digest() != requester.semanticFingerprint().digest() ||
        source.record().coreContext().digest() != coreContext.digest() ||
        source.boundModuleLease().capability().definitions().module() == requester.module()) {
      return zc::none;
    }
    for (size_t previous = 0; previous < index; ++previous) {
      if (sources[previous].capability().record().module().encode().asPtr() ==
          source.record().module().encode().asPtr()) {
        return zc::none;
      }
    }
  }

  const auto imports = requester.resolvedImports();
  if (sources.size() == 0) {
    if (imports.size() != 0) { return zc::none; }
  } else {
    if (sources.size() != 1 ||
        imports.size() != sources[0].capability().signatures().facts().size()) {
      return zc::none;
    }
    const auto& source = sources[0].capability();
    if (source.record().surface() != core_library_query::CoreBootstrapModuleSurface::Marker) {
      return zc::none;
    }
    for (const auto& fact : source.signatures().facts()) {
      size_t matches = 0;
      for (const auto& import : imports) {
        if (!sameSource(import, source) ||
            !import.canonicalTarget.value().is<binder::DefinitionBindingTarget>() ||
            import.canonicalTarget.value().get<binder::DefinitionBindingTarget>().definition !=
                fact.signature.definition) {
          continue;
        }
        ++matches;
      }
      if (matches != 1) { return zc::none; }
    }
  }
  return VerifiedCoreImportedSignatureView(zc::heap<Impl>(
      requester.module(), bound.module().clone(), coreContext.clone(), zc::mv(sources)));
}

identity::ModuleId VerifiedCoreImportedSignatureView::requester() const noexcept {
  return impl->requester;
}

zc::ArrayPtr<const VerifiedCoreImportedSignatureView::BootstrapInterfaceLease>
VerifiedCoreImportedSignatureView::sources() const noexcept {
  return impl->sources.asPtr();
}

zc::Array<uint8_t> VerifiedCoreImportedSignatureView::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(impl->requesterKey.encode().asPtr());
  encoder.encodeDigest(impl->coreContext.digest());
  encoder.encodeSequenceSize(sources().size());
  for (const auto& source : sources()) { encoder.encodeByteString(source.stableWitness()); }
  return frame("zom.core-bootstrap-imported-signature-view"_zc, encoder.finish().asPtr());
}

}  // namespace zomlang::compiler::driver::core
