// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/stable-binding-diagnostic-fact.h"

#include "zomlang/compiler/binder/stable-binding-facts.h"
#include "zomlang/compiler/diagnostics/diagnostic-info.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::binder {

class StableBindingDiagnosticFactCodecAccess final {
public:
  static zc::Maybe<diagnostics::DiagnosticOccurrenceKey> occurrence(
      identity::ModuleKey&& module, identity::SourceFileKey&& source,
      diagnostics::BinderDiagnosticProducer producer, zc::Maybe<zc::Array<uint8_t>>&& semanticOwner,
      diagnostics::BinderDiagnosticEmitter emitter, zc::Vector<uint32_t>&& syntaxPath) {
    return diagnostics::DiagnosticOccurrenceKey::binder(zc::mv(module), zc::mv(source), producer,
                                                        zc::mv(semanticOwner), emitter,
                                                        zc::mv(syntaxPath));
  }

  static zc::Maybe<diagnostics::DiagnosticProvenanceKey> provenance(
      identity::ModuleKey&& module, identity::SourceFileKey&& source,
      zc::Maybe<zc::Array<uint8_t>>&& semanticOwner, diagnostics::BinderDiagnosticEmitter emitter,
      zc::Vector<uint32_t>&& syntaxPath) {
    return diagnostics::DiagnosticProvenanceKey::binderModuleSite(
        zc::mv(module), zc::mv(source), zc::mv(semanticOwner), emitter, zc::mv(syntaxPath));
  }
};

namespace {

constexpr zc::StringPtr kBinderIdentifierArgumentsDomain =
    "zom.diagnostic.arguments.binder-identifier"_zc;
constexpr zc::StringPtr kBinderNamespaceArgumentsDomain =
    "zom.diagnostic.arguments.binder-namespace"_zc;
constexpr diagnostics::DiagnosticFactCodecLimits kStableBindingDiagnosticLimits{
    .maximumFacts = 4096,
    .maximumEncodedBytes = 64 * 1024 * 1024,
    .maximumProvenanceComponentsPerKey = 64,
    .maximumArgumentBytesPerRecord = 64 * 1024 * 1024,
    .maximumSecondaryPerFact = 128,
};

bool containsError(zc::ArrayPtr<const diagnostics::DiagnosticFact> facts) {
  for (const auto& fact : facts) {
    if (diagnostics::getDiagnosticInfo(fact.code()).severity >= diagnostics::DiagSeverity::kError) {
      return true;
    }
  }
  return false;
}

int comparePath(zc::ArrayPtr<const uint32_t> left, zc::ArrayPtr<const uint32_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] < right[index]) { return -1; }
    if (left[index] > right[index]) { return 1; }
  }
  if (left.size() < right.size()) { return -1; }
  if (left.size() > right.size()) { return 1; }
  return 0;
}

zc::Vector<uint32_t> clonePath(zc::ArrayPtr<const uint32_t> path) {
  zc::Vector<uint32_t> result(path.size());
  result.addAll(path);
  return result;
}

bool sameModuleAndSource(const IdentitySyntaxSiteKey& left, const IdentitySyntaxSiteKey& right) {
  return left.module().encode().asPtr() == right.module().encode().asPtr() &&
         left.source().sameAs(right.source());
}

zc::Maybe<diagnostics::DiagnosticOccurrenceKey> occurrence(
    const IdentitySyntaxSiteKey& site, diagnostics::IdentityDiagnosticEmitter emitter) {
  return diagnostics::DiagnosticOccurrenceKey::identityAdmission(
      site.module().clone(), site.source().clone(), clonePath(site.moduleSyntaxPath()), emitter);
}

zc::Maybe<diagnostics::DiagnosticProvenanceKey> provenance(const IdentitySyntaxSiteKey& site) {
  return diagnostics::DiagnosticProvenanceKey::identitySyntaxSite(
      site.module().clone(), site.source().clone(), clonePath(site.moduleSyntaxPath()));
}

zc::StringPtr namespaceName(Namespace nameSpace) {
  switch (nameSpace) {
    case Namespace::Value:
      return "value"_zc;
    case Namespace::Type:
      return "type"_zc;
    case Namespace::Module:
      return "module"_zc;
    case Namespace::Label:
      return "label"_zc;
    case Namespace::Attribute:
      return "attribute"_zc;
  }
  ZC_UNREACHABLE;
}

struct LookupDiagnosticKeys final {
  diagnostics::DiagnosticOccurrenceKey occurrence;
  diagnostics::DiagnosticProvenanceKey provenance;
};

zc::Maybe<LookupDiagnosticKeys> lookupKeys(const identity::SourceFileKey& source,
                                           const StableFailedLookupFact& lookup) {
  zc::Maybe<const identity::ModuleKey&> module;
  diagnostics::BinderDiagnosticProducer producer =
      diagnostics::BinderDiagnosticProducer::BindModuleSkeleton;
  zc::Maybe<zc::Array<uint8_t>> owner;
  const auto& queryOwner = lookup.owner().value();
  if (queryOwner.is<BinderModuleQueryOwner>()) {
    module = queryOwner.get<BinderModuleQueryOwner>().module;
  } else if (queryOwner.is<BinderBodyQueryOwner>()) {
    const auto& body = queryOwner.get<BinderBodyQueryOwner>().body;
    module = body.module();
    producer = diagnostics::BinderDiagnosticProducer::BindOwnerBody;
    owner = body.owner().encode();
  } else {
    return zc::none;
  }
  if (module == zc::none || !source.belongsTo(ZC_ASSERT_NONNULL(module).crate())) {
    return zc::none;
  }
  zc::Maybe<zc::Array<uint8_t>> occurrenceOwner;
  ZC_IF_SOME(bytes, owner) { occurrenceOwner = zc::heapArray<uint8_t>(bytes.asPtr()); }
  auto occurrence = StableBindingDiagnosticFactCodecAccess::occurrence(
      ZC_ASSERT_NONNULL(module).clone(), source.clone(), producer, zc::mv(occurrenceOwner),
      diagnostics::BinderDiagnosticEmitter::Lookup, clonePath(lookup.usePath().components()));
  auto provenance = StableBindingDiagnosticFactCodecAccess::provenance(
      ZC_ASSERT_NONNULL(module).clone(), source.clone(), zc::mv(owner),
      diagnostics::BinderDiagnosticEmitter::Lookup, clonePath(lookup.usePath().components()));
  if (occurrence == zc::none || provenance == zc::none) { return zc::none; }
  return LookupDiagnosticKeys{zc::mv(ZC_ASSERT_NONNULL(occurrence)),
                              zc::mv(ZC_ASSERT_NONNULL(provenance))};
}

zc::Maybe<diagnostics::DiagnosticFact> identifierLookupFact(const identity::SourceFileKey& source,
                                                            const StableFailedLookupFact& lookup,
                                                            diagnostics::DiagID code) {
  auto arguments = BinderIdentifierDiagnosticArguments::from(lookup.name().clone());
  auto encodedArguments = arguments.encodeCanonical();
  auto decodedArguments =
      BinderIdentifierDiagnosticArguments::decodeCanonical(encodedArguments.asPtr());
  auto keys = lookupKeys(source, lookup);
  if (decodedArguments == zc::none || ZC_ASSERT_NONNULL(decodedArguments) != arguments ||
      keys == zc::none) {
    return zc::none;
  }
  zc::Vector<zc::String> displayArguments;
  displayArguments.add(zc::str(arguments.identifier().text()));
  return diagnostics::DiagnosticFact::from(
      zc::mv(ZC_ASSERT_NONNULL(keys).occurrence), code, zc::mv(displayArguments),
      zc::mv(ZC_ASSERT_NONNULL(keys).provenance), zc::Vector<diagnostics::DiagnosticSecondary>());
}

}  // namespace

BinderIdentifierDiagnosticArguments::BinderIdentifierDiagnosticArguments(
    identity::DeclaredDefinitionName&& identifier) noexcept
    : identifierField(zc::mv(identifier)) {}

BinderIdentifierDiagnosticArguments BinderIdentifierDiagnosticArguments::from(
    identity::DeclaredDefinitionName&& identifier) {
  return BinderIdentifierDiagnosticArguments(zc::mv(identifier));
}

BinderIdentifierDiagnosticArguments BinderIdentifierDiagnosticArguments::clone() const {
  return from(identifierField.clone());
}

const identity::DeclaredDefinitionName& BinderIdentifierDiagnosticArguments::identifier()
    const noexcept {
  return identifierField;
}

zc::Array<uint8_t> BinderIdentifierDiagnosticArguments::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(kBinderIdentifierArgumentsDomain.asBytes());
  encoder.encodeUint8(0);
  identifierField.encode(encoder);
  return encoder.finish();
}

zc::Maybe<BinderIdentifierDiagnosticArguments> BinderIdentifierDiagnosticArguments::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto domain = decoder.decodeByteString(kBinderIdentifierArgumentsDomain.size());
  auto separator = decoder.decodeUint8();
  auto identifier = identity::DeclaredDefinitionName::decodeCanonical(decoder);
  if (domain == zc::none || separator == zc::none || identifier == zc::none ||
      !decoder.finished() ||
      ZC_ASSERT_NONNULL(domain).asPtr() != kBinderIdentifierArgumentsDomain.asBytes() ||
      ZC_ASSERT_NONNULL(separator) != 0) {
    return zc::none;
  }
  auto result = from(zc::mv(ZC_ASSERT_NONNULL(identifier)));
  if (result.encodeCanonical().asPtr() != bytes) { return zc::none; }
  return result;
}

bool BinderIdentifierDiagnosticArguments::operator==(
    const BinderIdentifierDiagnosticArguments& other) const noexcept {
  return identifierField == other.identifierField;
}

BinderNamespaceDiagnosticArguments::BinderNamespaceDiagnosticArguments(
    identity::DeclaredDefinitionName&& identifier, Namespace expectedNamespace) noexcept
    : identifierField(zc::mv(identifier)), expectedNamespaceField(expectedNamespace) {}

zc::Maybe<BinderNamespaceDiagnosticArguments> BinderNamespaceDiagnosticArguments::from(
    identity::DeclaredDefinitionName&& identifier, Namespace expectedNamespace) {
  if (expectedNamespace < Namespace::Value || expectedNamespace > Namespace::Attribute) {
    return zc::none;
  }
  return BinderNamespaceDiagnosticArguments(zc::mv(identifier), expectedNamespace);
}

BinderNamespaceDiagnosticArguments BinderNamespaceDiagnosticArguments::clone() const {
  return ZC_ASSERT_NONNULL(from(identifierField.clone(), expectedNamespaceField));
}

const identity::DeclaredDefinitionName& BinderNamespaceDiagnosticArguments::identifier()
    const noexcept {
  return identifierField;
}

Namespace BinderNamespaceDiagnosticArguments::expectedNamespace() const noexcept {
  return expectedNamespaceField;
}

zc::Array<uint8_t> BinderNamespaceDiagnosticArguments::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(kBinderNamespaceArgumentsDomain.asBytes());
  encoder.encodeUint8(0);
  identifierField.encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(expectedNamespaceField));
  return encoder.finish();
}

zc::Maybe<BinderNamespaceDiagnosticArguments> BinderNamespaceDiagnosticArguments::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto domain = decoder.decodeByteString(kBinderNamespaceArgumentsDomain.size());
  auto separator = decoder.decodeUint8();
  auto identifier = identity::DeclaredDefinitionName::decodeCanonical(decoder);
  auto expectedNamespace = decoder.decodeUint8();
  if (domain == zc::none || separator == zc::none || identifier == zc::none ||
      expectedNamespace == zc::none || !decoder.finished() ||
      ZC_ASSERT_NONNULL(domain).asPtr() != kBinderNamespaceArgumentsDomain.asBytes() ||
      ZC_ASSERT_NONNULL(separator) != 0) {
    return zc::none;
  }
  auto result = from(zc::mv(ZC_ASSERT_NONNULL(identifier)),
                     static_cast<Namespace>(ZC_ASSERT_NONNULL(expectedNamespace)));
  if (result == zc::none || ZC_ASSERT_NONNULL(result).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(result);
}

bool BinderNamespaceDiagnosticArguments::operator==(
    const BinderNamespaceDiagnosticArguments& other) const noexcept {
  return identifierField == other.identifierField &&
         expectedNamespaceField == other.expectedNamespaceField;
}

zc::Maybe<diagnostics::DiagnosticFact> StableBindingDiagnosticFactFactory::missingLookup(
    const identity::SourceFileKey& source, const StableFailedLookupFact& lookup) {
  if (!lookup.outcome().value().is<StableMissingLookupOutcome>()) { return zc::none; }
  return identifierLookupFact(source, lookup, diagnostics::DiagID::UndefinedIdentifier);
}

zc::Maybe<diagnostics::DiagnosticFact> StableBindingDiagnosticFactFactory::namespaceMismatchLookup(
    const identity::SourceFileKey& source, const StableFailedLookupFact& lookup) {
  if (!lookup.outcome().value().is<StableNamespaceMismatchLookupOutcome>()) { return zc::none; }
  auto arguments =
      BinderNamespaceDiagnosticArguments::from(lookup.name().clone(), lookup.nameSpace());
  if (arguments == zc::none) { return zc::none; }
  auto encodedArguments = ZC_ASSERT_NONNULL(arguments).encodeCanonical();
  auto decodedArguments =
      BinderNamespaceDiagnosticArguments::decodeCanonical(encodedArguments.asPtr());
  auto keys = lookupKeys(source, lookup);
  if (decodedArguments == zc::none ||
      ZC_ASSERT_NONNULL(decodedArguments) != ZC_ASSERT_NONNULL(arguments) || keys == zc::none) {
    return zc::none;
  }
  zc::Vector<zc::String> displayArguments;
  displayArguments.add(zc::str(ZC_ASSERT_NONNULL(arguments).identifier().text()));
  displayArguments.add(zc::str(namespaceName(ZC_ASSERT_NONNULL(arguments).expectedNamespace())));
  return diagnostics::DiagnosticFact::from(
      zc::mv(ZC_ASSERT_NONNULL(keys).occurrence), diagnostics::DiagID::SymbolNamespaceMismatch,
      zc::mv(displayArguments), zc::mv(ZC_ASSERT_NONNULL(keys).provenance),
      zc::Vector<diagnostics::DiagnosticSecondary>());
}

zc::Maybe<diagnostics::DiagnosticFact> StableBindingDiagnosticFactFactory::ambiguousLookup(
    const identity::SourceFileKey& source, const StableFailedLookupFact& lookup) {
  if (!lookup.outcome().value().is<StableAmbiguousLookupOutcome>()) { return zc::none; }
  return identifierLookupFact(source, lookup, diagnostics::DiagID::AmbiguousIdentifier);
}

zc::Maybe<diagnostics::DiagnosticFact>
StableBindingDiagnosticFactFactory::constantExpressionNotAllowed(
    const IdentitySyntaxSiteKey& primary) {
  auto occurrenceValue =
      occurrence(primary, diagnostics::IdentityDiagnosticEmitter::ConstantExpressionNotAllowed);
  auto primaryValue = provenance(primary);
  if (occurrenceValue == zc::none || primaryValue == zc::none) { return zc::none; }
  return diagnostics::DiagnosticFact::from(
      zc::mv(ZC_ASSERT_NONNULL(occurrenceValue)), diagnostics::DiagID::ConstantExpressionNotAllowed,
      zc::Vector<zc::String>(), zc::mv(ZC_ASSERT_NONNULL(primaryValue)),
      zc::Vector<diagnostics::DiagnosticSecondary>());
}

zc::Maybe<diagnostics::DiagnosticFact>
StableBindingDiagnosticFactFactory::duplicateGenericParameter(
    const IdentitySyntaxSiteKey& duplicate, const IdentitySyntaxSiteKey& previous,
    const BinderIdentifierDiagnosticArguments& arguments) {
  if (!sameModuleAndSource(duplicate, previous) ||
      comparePath(previous.moduleSyntaxPath(), duplicate.moduleSyntaxPath()) >= 0) {
    return zc::none;
  }
  auto occurrenceValue =
      occurrence(duplicate, diagnostics::IdentityDiagnosticEmitter::DuplicateGenericParameter);
  auto primaryValue = provenance(duplicate);
  auto previousValue = provenance(previous);
  if (occurrenceValue == zc::none || primaryValue == zc::none || previousValue == zc::none) {
    return zc::none;
  }
  auto previousSecondary = diagnostics::DiagnosticSecondary::previousDeclaration(
      diagnostics::DiagID::PreviousDeclarationHere, zc::mv(ZC_ASSERT_NONNULL(previousValue)));
  if (previousSecondary == zc::none) { return zc::none; }
  zc::Vector<zc::String> factArguments;
  factArguments.add(zc::str(arguments.identifier().text()));
  zc::Vector<diagnostics::DiagnosticSecondary> secondary;
  secondary.add(zc::mv(ZC_ASSERT_NONNULL(previousSecondary)));
  return diagnostics::DiagnosticFact::from(
      zc::mv(ZC_ASSERT_NONNULL(occurrenceValue)), diagnostics::DiagID::DuplicateIdentifier,
      zc::mv(factArguments), zc::mv(ZC_ASSERT_NONNULL(primaryValue)), zc::mv(secondary));
}

zc::Maybe<zc::Array<uint8_t>> encodeStableBindingDiagnosticFacts(
    zc::ArrayPtr<const diagnostics::DiagnosticFact> facts) {
  if (facts.size() == 0 || !containsError(facts)) { return zc::none; }
  return diagnostics::encodeDiagnosticFacts(zc::none, facts, kStableBindingDiagnosticLimits);
}

zc::Maybe<zc::Vector<diagnostics::DiagnosticFact>> decodeStableBindingDiagnosticFacts(
    zc::ArrayPtr<const uint8_t> bytes) {
  auto facts = diagnostics::decodeDiagnosticFacts(zc::none, bytes, kStableBindingDiagnosticLimits);
  if (facts == zc::none || ZC_ASSERT_NONNULL(facts).size() == 0 ||
      !containsError(ZC_ASSERT_NONNULL(facts).asPtr())) {
    return zc::none;
  }
  auto canonical = diagnostics::encodeDiagnosticFacts(zc::none, ZC_ASSERT_NONNULL(facts).asPtr(),
                                                      kStableBindingDiagnosticLimits);
  if (canonical == zc::none || ZC_ASSERT_NONNULL(canonical).asPtr() != bytes) { return zc::none; }
  return zc::mv(facts);
}

}  // namespace zomlang::compiler::binder
