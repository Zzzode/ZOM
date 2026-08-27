// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/stable/stable-binding-diagnostic-fact.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/binder/stable/stable-binding-codec.h"
#include "zomlang/compiler/binder/stable/stable-binding-facts.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::binder {
namespace {

template <typename T>
T require(zc::Maybe<T>&& value) {
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid stable diagnostic fixture");
}

identity::DeclaredDefinitionName declaredName(zc::StringPtr value) {
  return require(identity::DeclaredDefinitionName::fromCanonical(value));
}

LocalSyntaxPath localPath(uint32_t component = 1) {
  zc::Vector<uint32_t> components;
  components.add(component);
  return require(LocalSyntaxPath::from(zc::mv(components)));
}

IdentitySyntaxSiteKey identitySite(uint32_t component) {
  zc::Vector<uint32_t> path;
  path.add(component);
  return require(IdentitySyntaxSiteKey::from(tests::test_identity_detail::module(),
                                             tests::test_identity_detail::source(), zc::mv(path)));
}

StableBindingTargetKey target(uint32_t ordinal) {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(tests::test_identity_detail::scalar<identity::ModulePathSegment>(
      ordinal == 1 ? "left"_zc : "right"_zc));
  auto module =
      require(identity::ModuleKey::from(tests::test_identity_detail::crate(), zc::mv(path)));
  return StableBindingTargetKey::module(zc::mv(module));
}

bool bytesLess(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] != right[index]) { return left[index] < right[index]; }
  }
  return left.size() < right.size();
}

StableFailedLookupOutcome namespaceMismatch() {
  zc::Vector<Namespace> values;
  values.add(Namespace::Type);
  auto sequence = require(StableBindingSequenceBuilder<Namespace>::fromNonEmpty(zc::mv(values)));
  return StableFailedLookupOutcome::namespaceMismatch(zc::mv(sequence));
}

StableFailedLookupOutcome ambiguous() {
  zc::Vector<StableBindingTargetKey> values;
  auto left = target(1);
  auto right = target(2);
  auto leftBytes = StableBindingCodec<StableBindingTargetKey>::encode(left);
  auto rightBytes = StableBindingCodec<StableBindingTargetKey>::encode(right);
  if (bytesLess(leftBytes.asPtr(), rightBytes.asPtr())) {
    values.add(zc::mv(left));
    values.add(zc::mv(right));
  } else {
    values.add(zc::mv(right));
    values.add(zc::mv(left));
  }
  auto sequence =
      require(StableBindingSequenceBuilder<StableBindingTargetKey>::fromNonEmpty(zc::mv(values)));
  return require(StableFailedLookupOutcome::ambiguous(zc::mv(sequence)));
}

StableFailedLookupFact lookup(StableFailedLookupOutcome&& outcome,
                              Namespace nameSpace = Namespace::Value) {
  return require(StableFailedLookupFact::from(
      BinderQueryOwner::module(tests::test_identity_detail::module()), localPath(), nameSpace,
      declaredName("name"_zc), zc::mv(outcome)));
}

void expectRoundTrip(const diagnostics::DiagnosticFact& fact) {
  zc::Vector<diagnostics::DiagnosticFact> facts;
  facts.add(fact.clone());
  auto encoded = require(encodeStableBindingDiagnosticFacts(facts.asPtr()));
  auto decoded = require(decodeStableBindingDiagnosticFacts(encoded.asPtr()));
  ZC_REQUIRE(decoded.size() == 1);
  ZC_EXPECT(decoded[0] == fact);
}

}  // namespace

ZC_TEST("DiagnosticFactTest.BinderLookupRejectsExactMappingMutations") {
  auto identifier = BinderIdentifierDiagnosticArguments::from(declaredName("name"_zc));
  auto identifierBytes = identifier.encodeCanonical();
  ZC_EXPECT(require(BinderIdentifierDiagnosticArguments::decodeCanonical(
                identifierBytes.asPtr())) == identifier);

  auto namespaceArguments =
      require(BinderNamespaceDiagnosticArguments::from(declaredName("name"_zc), Namespace::Value));
  auto namespaceBytes = namespaceArguments.encodeCanonical();
  ZC_EXPECT(require(BinderNamespaceDiagnosticArguments::decodeCanonical(namespaceBytes.asPtr())) ==
            namespaceArguments);
  namespaceBytes[namespaceBytes.size() - 1] = 0xff;
  ZC_EXPECT(BinderNamespaceDiagnosticArguments::decodeCanonical(namespaceBytes.asPtr()) ==
            zc::none);

  auto missing = lookup(StableFailedLookupOutcome::missing());
  auto missingFact = require(StableBindingDiagnosticFactFactory::missingLookup(
      tests::test_identity_detail::source(), missing));
  ZC_EXPECT(missingFact.code() == diagnostics::DiagID::UndefinedIdentifier);
  ZC_EXPECT(missingFact.occurrence().binderProducer() ==
            diagnostics::BinderDiagnosticProducer::BindModuleSkeleton);
  ZC_EXPECT(missingFact.occurrence().binderEmitter() ==
            diagnostics::BinderDiagnosticEmitter::Lookup);
  ZC_EXPECT(!missingFact.occurrence().hasBinderSemanticOwner());
  expectRoundTrip(missingFact);

  auto mismatch = lookup(namespaceMismatch());
  auto mismatchFact = require(StableBindingDiagnosticFactFactory::namespaceMismatchLookup(
      tests::test_identity_detail::source(), mismatch));
  ZC_EXPECT(mismatchFact.code() == diagnostics::DiagID::SymbolNamespaceMismatch);
  ZC_EXPECT(mismatchFact.arguments().size() == 2);
  expectRoundTrip(mismatchFact);

  auto ambiguousLookup = lookup(ambiguous());
  auto ambiguousFact = require(StableBindingDiagnosticFactFactory::ambiguousLookup(
      tests::test_identity_detail::source(), ambiguousLookup));
  ZC_EXPECT(ambiguousFact.code() == diagnostics::DiagID::AmbiguousIdentifier);
  ZC_EXPECT(ambiguousFact.arguments().size() == 1);
  expectRoundTrip(ambiguousFact);

  ZC_EXPECT(StableBindingDiagnosticFactFactory::ambiguousLookup(
                tests::test_identity_detail::source(), missing) == zc::none);
}

ZC_TEST("DiagnosticFactTest.IdentityAdmissionRejectsExactMappingMutations") {
  auto constant =
      require(StableBindingDiagnosticFactFactory::constantExpressionNotAllowed(identitySite(1)));
  ZC_EXPECT(constant.code() == diagnostics::DiagID::ConstantExpressionNotAllowed);
  ZC_EXPECT(constant.arguments().size() == 0);
  ZC_EXPECT(constant.secondary().size() == 0);
  expectRoundTrip(constant);

  auto arguments = BinderIdentifierDiagnosticArguments::from(declaredName("T"_zc));
  auto duplicate = require(StableBindingDiagnosticFactFactory::duplicateGenericParameter(
      identitySite(2), identitySite(1), arguments));
  ZC_EXPECT(duplicate.code() == diagnostics::DiagID::DuplicateIdentifier);
  ZC_REQUIRE(duplicate.secondary().size() == 1);
  ZC_EXPECT(duplicate.secondary()[0].code() == diagnostics::DiagID::PreviousDeclarationHere);
  ZC_EXPECT(duplicate.secondary()[0].role() ==
            diagnostics::DiagnosticSecondaryRole::PreviousDeclaration);
  expectRoundTrip(duplicate);

  ZC_EXPECT(StableBindingDiagnosticFactFactory::duplicateGenericParameter(
                identitySite(1), identitySite(2), arguments) == zc::none);
}

}  // namespace zomlang::compiler::binder
