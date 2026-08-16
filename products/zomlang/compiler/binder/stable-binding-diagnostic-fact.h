// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/binder/binding-metadata.h"
#include "zomlang/compiler/binder/identity-pre-admission.h"
#include "zomlang/compiler/diagnostics/fact/diagnostic-fact.h"
#include "zomlang/compiler/identity/canonical/canonical-scalar.h"

namespace zomlang::compiler::binder {

class StableFailedLookupFact;

/// \brief Canonical Binder diagnostic payload for one declared identifier.
class BinderIdentifierDiagnosticArguments final {
public:
  BinderIdentifierDiagnosticArguments(BinderIdentifierDiagnosticArguments&&) noexcept = default;
  BinderIdentifierDiagnosticArguments& operator=(BinderIdentifierDiagnosticArguments&&) noexcept =
      default;
  ZC_DISALLOW_COPY(BinderIdentifierDiagnosticArguments);

  ZC_NODISCARD static BinderIdentifierDiagnosticArguments from(
      identity::DeclaredDefinitionName&& identifier);
  ZC_NODISCARD BinderIdentifierDiagnosticArguments clone() const;
  ZC_NODISCARD const identity::DeclaredDefinitionName& identifier() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  ZC_NODISCARD static zc::Maybe<BinderIdentifierDiagnosticArguments> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  bool operator==(const BinderIdentifierDiagnosticArguments& other) const noexcept;

private:
  explicit BinderIdentifierDiagnosticArguments(
      identity::DeclaredDefinitionName&& identifier) noexcept;
  identity::DeclaredDefinitionName identifierField;
};

/// \brief Canonical Binder diagnostic payload for one identifier and expected namespace.
class BinderNamespaceDiagnosticArguments final {
public:
  BinderNamespaceDiagnosticArguments(BinderNamespaceDiagnosticArguments&&) noexcept = default;
  BinderNamespaceDiagnosticArguments& operator=(BinderNamespaceDiagnosticArguments&&) noexcept =
      default;
  ZC_DISALLOW_COPY(BinderNamespaceDiagnosticArguments);

  ZC_NODISCARD static zc::Maybe<BinderNamespaceDiagnosticArguments> from(
      identity::DeclaredDefinitionName&& identifier, Namespace expectedNamespace);
  ZC_NODISCARD BinderNamespaceDiagnosticArguments clone() const;
  ZC_NODISCARD const identity::DeclaredDefinitionName& identifier() const noexcept;
  ZC_NODISCARD Namespace expectedNamespace() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  ZC_NODISCARD static zc::Maybe<BinderNamespaceDiagnosticArguments> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  bool operator==(const BinderNamespaceDiagnosticArguments& other) const noexcept;

private:
  BinderNamespaceDiagnosticArguments(identity::DeclaredDefinitionName&& identifier,
                                     Namespace expectedNamespace) noexcept;
  identity::DeclaredDefinitionName identifierField;
  Namespace expectedNamespaceField;
};

/// \brief Constructs the only stable-identity source diagnostics admitted by Binder.
class StableBindingDiagnosticFactFactory final {
public:
  ZC_NODISCARD static zc::Maybe<diagnostics::DiagnosticFact> missingLookup(
      const identity::SourceFileKey& source, const StableFailedLookupFact& lookup);
  ZC_NODISCARD static zc::Maybe<diagnostics::DiagnosticFact> namespaceMismatchLookup(
      const identity::SourceFileKey& source, const StableFailedLookupFact& lookup);
  ZC_NODISCARD static zc::Maybe<diagnostics::DiagnosticFact> ambiguousLookup(
      const identity::SourceFileKey& source, const StableFailedLookupFact& lookup);
  ZC_NODISCARD static zc::Maybe<diagnostics::DiagnosticFact> constantExpressionNotAllowed(
      const IdentitySyntaxSiteKey& primary);
  ZC_NODISCARD static zc::Maybe<diagnostics::DiagnosticFact> duplicateGenericParameter(
      const IdentitySyntaxSiteKey& duplicate, const IdentitySyntaxSiteKey& previous,
      const BinderIdentifierDiagnosticArguments& arguments);
  ZC_NODISCARD static zc::Maybe<diagnostics::DiagnosticFact> definitionRedeclaration(
      const IdentitySyntaxSiteKey& duplicate, const IdentitySyntaxSiteKey& previous,
      diagnostics::DiagID diagnostic, zc::StringPtr name);
};

/// \brief Encodes one canonical nonempty Binder source-diagnostic sequence.
ZC_NODISCARD zc::Maybe<zc::Array<uint8_t>> encodeStableBindingDiagnosticFacts(
    zc::ArrayPtr<const diagnostics::DiagnosticFact> facts);

/// \brief Decodes and canonicalizes one nonempty Binder source-diagnostic sequence.
ZC_NODISCARD zc::Maybe<zc::Vector<diagnostics::DiagnosticFact>> decodeStableBindingDiagnosticFacts(
    zc::ArrayPtr<const uint8_t> bytes);

}  // namespace zomlang::compiler::binder
