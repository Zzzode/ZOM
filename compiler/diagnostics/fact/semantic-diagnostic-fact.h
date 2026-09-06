// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/diagnostics/fact/diagnostic-fact.h"
#include "compiler/identity/key/source-key.h"
#include "zc/core/one-of.h"

namespace zomlang::compiler::diagnostics {

/// \brief One typed related source site before semantic fact construction.
struct SemanticDiagnosticRelatedSite final {
  DiagnosticSecondaryRole role;
  zc::Maybe<DiagID> code;
  identity::SourceSpan span;
  zc::Vector<zc::String> arguments;
};

/// \brief One semantic fact and every revision-local provenance entry it requires.
struct SemanticDiagnosticFactProjection final {
  DiagnosticFact fact;
  zc::Vector<SourceDiagnosticProvenanceEntry> provenance;
};

/// \brief Canonical fact and provenance roots projected by one semantic producer.
struct SemanticDiagnosticFactBatch final {
  zc::Vector<DiagnosticFact> facts;
  zc::Vector<SourceDiagnosticProvenanceEntry> provenance;
};

/// \brief Fail-closed batch projection failures before request-local collection.
enum class SemanticDiagnosticBatchProjectionFailure : uint8_t {
  InvalidModuleIdentity = 0x01,
  InvalidOwnerIdentity = 0x02,
  InvalidFactProjection = 0x03,
};

using SemanticDiagnosticBatchProjectionResult =
    zc::OneOf<SemanticDiagnosticFactBatch, SemanticDiagnosticBatchProjectionFailure>;

enum class SemanticDiagnosticProjectionFailure : uint8_t {
  InvalidOwner = 0x01,
  InvalidOccurrence = 0x02,
  InvalidPrimary = 0x03,
  InvalidRelated = 0x04,
  InvalidFact = 0x05,
};

using SemanticDiagnosticProjectionResult =
    zc::OneOf<SemanticDiagnosticFactProjection, SemanticDiagnosticProjectionFailure>;

/// \brief Constructs one canonical semantic fact without presentation or source-manager state.
ZC_NODISCARD SemanticDiagnosticProjectionResult projectSemanticDiagnosticFact(
    const identity::ModuleKey& module, SemanticDiagnosticDomain domain,
    zc::ArrayPtr<const uint8_t> canonicalOwner, zc::ArrayPtr<const uint32_t> eventPath, DiagID code,
    zc::Vector<zc::String>&& arguments, const identity::SourceSpan& primary,
    zc::Vector<SemanticDiagnosticRelatedSite>&& related);

}  // namespace zomlang::compiler::diagnostics
