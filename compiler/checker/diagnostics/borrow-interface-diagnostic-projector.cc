// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/checker/diagnostics/borrow-interface-diagnostic-projector.h"

#include "zc/core/debug.h"

namespace zomlang::compiler::checker::borrow {
namespace {

diagnostics::DiagID diagnosticId(BorrowSignatureFailureKind kind) {
  using diagnostics::DiagID;
  switch (kind) {
    case BorrowSignatureFailureKind::AmbiguousDirectResult:
      return DiagID::BorrowOutputRegionAmbiguous;
    case BorrowSignatureFailureKind::UnexpressibleResult:
      return DiagID::BorrowOutputRegionUnexpressible;
    case BorrowSignatureFailureKind::UnverifiedExternContract:
      return DiagID::BorrowExternContractUnverified;
  }
  ZC_UNREACHABLE
}

bool appendProjection(diagnostics::SemanticDiagnosticFactBatch& batch,
                      diagnostics::SemanticDiagnosticProjectionResult&& result) {
  if (!result.is<diagnostics::SemanticDiagnosticFactProjection>()) return false;
  auto projected = zc::mv(result).get<diagnostics::SemanticDiagnosticFactProjection>();
  batch.facts.add(zc::mv(projected.fact));
  for (auto& entry : projected.provenance) { batch.provenance.add(zc::mv(entry)); }
  return true;
}

}  // namespace

diagnostics::SemanticDiagnosticBatchProjectionResult projectBorrowSignatureFailures(
    const CheckerIdentityAuthority& identities,
    zc::ArrayPtr<const BorrowSignatureFailure> failures) {
  diagnostics::SemanticDiagnosticFactBatch batch;
  for (const auto& failure : failures) {
    auto callable = identities.definition(failure.callable);
    if (callable == zc::none) {
      return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidOwnerIdentity;
    }
    ZC_IF_SOME(callableEntry, callable) {
      auto module = identities.module(callableEntry.record().module());
      if (module == zc::none) {
        return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidModuleIdentity;
      }
      auto owner = callableEntry.key().encode();
      zc::Vector<uint32_t> path;
      path.add(static_cast<uint32_t>(failure.kind));
      path.add(failure.traversalOrdinal);
      zc::Vector<zc::String> arguments;
      zc::Vector<diagnostics::SemanticDiagnosticRelatedSite> related;
      related.add(diagnostics::SemanticDiagnosticRelatedSite{
          diagnostics::DiagnosticSecondaryRole::Highlight, zc::none,
          failure.declarationSpan.clone(), zc::Vector<zc::String>()});
      ZC_IF_SOME(moduleEntry, module) {
        if (!appendProjection(
                batch,
                diagnostics::projectSemanticDiagnosticFact(
                    moduleEntry.key(), diagnostics::SemanticDiagnosticDomain::BorrowInterface,
                    owner.asPtr(), path.asPtr(), diagnosticId(failure.kind), zc::mv(arguments),
                    failure.primarySpan, zc::mv(related)))) {
          return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
        }
      }
    }
  }
  return batch;
}

}  // namespace zomlang::compiler::checker::borrow
