// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/driver/core/diagnostic-projector.h"

namespace zomlang::compiler::driver::core_library_query {

zc::Maybe<CoreOperationalFailureKind> coreOperationalFailure(
    query::QueryRuntimeFailure failure) noexcept {
  switch (failure) {
    case query::QueryRuntimeFailure::AllocationFailure:
      return CoreOperationalFailureKind::AllocationFailed;
    case query::QueryRuntimeFailure::Cancelled:
      return CoreOperationalFailureKind::Cancelled;
    case query::QueryRuntimeFailure::UnregisteredKind:
    case query::QueryRuntimeFailure::InvalidKeyEncoding:
    case query::QueryRuntimeFailure::MissingInput:
    case query::QueryRuntimeFailure::ProviderRejected:
    case query::QueryRuntimeFailure::VerifierRejected:
    case query::QueryRuntimeFailure::Cycle:
    case query::QueryRuntimeFailure::FingerprintCollision:
    case query::QueryRuntimeFailure::InvariantViolation:
    case query::QueryRuntimeFailure::FinalSealRequired:
    case query::QueryRuntimeFailure::FinalSealMismatch:
      return zc::none;
  }
  ZC_UNREACHABLE
}

zc::StringPtr coreOperationalFailureDisplay(CoreOperationalFailureKind failure) noexcept {
  switch (failure) {
    case CoreOperationalFailureKind::AllocationFailed:
      return "allocation-failed"_zc;
    case CoreOperationalFailureKind::Cancelled:
      return "cancelled"_zc;
  }
  ZC_UNREACHABLE
}

basic::CompilerIncidentDescriptor CoreDiagnosticProjector::descriptor(
    CoreIncidentPhase phase, uint32_t kind, CoreIncidentProducer producer) noexcept {
  return basic::CompilerIncidentDescriptor(
      basic::CompilerIncidentDomain::Driver,
      basic::CompilerIncidentPhaseKey(static_cast<uint32_t>(phase)),
      basic::CompilerIncidentKindKey(kind),
      basic::CompilerIncidentProducerKey(static_cast<uint32_t>(producer)), 1);
}

basic::CompilerIncidentDescriptor CoreDiagnosticProjector::project(
    source::core::CoreDistributionAdmissionInvariantKind kind, CoreIncidentPhase phase,
    CoreIncidentProducer producer) noexcept {
  return descriptor(phase, 0x0100U + static_cast<uint32_t>(kind), producer);
}

basic::CompilerIncidentDescriptor CoreDiagnosticProjector::project(
    CoreDistributionInputPreparationInvariantKind kind) noexcept {
  return descriptor(CoreIncidentPhase::InputPreparation, 0x0200U + static_cast<uint32_t>(kind),
                    CoreIncidentProducer::InputTransaction);
}

basic::CompilerIncidentDescriptor CoreDiagnosticProjector::project(
    query::InputTransactionFailure failure) noexcept {
  return descriptor(CoreIncidentPhase::InputCommit, 0x0300U + static_cast<uint32_t>(failure),
                    CoreIncidentProducer::InputTransaction);
}

basic::CompilerIncidentDescriptor CoreDiagnosticProjector::project(
    query::QueryRuntimeFailure failure) noexcept {
  return descriptor(CoreIncidentPhase::QueryEvaluation, 0x0400U + static_cast<uint32_t>(failure),
                    CoreIncidentProducer::QueryRuntime);
}

basic::CompilerIncidentDescriptor CoreDiagnosticProjector::project(
    CoreRoleSeedFailureKind kind) noexcept {
  return descriptor(CoreIncidentPhase::QueryEvaluation, 0x0500U + static_cast<uint32_t>(kind),
                    CoreIncidentProducer::QueryRuntime);
}

basic::CompilerIncidentDescriptor CoreDiagnosticProjector::project(
    CoreSessionInvariantKind kind) noexcept {
  return descriptor(CoreIncidentPhase::SessionIntegration, 0x0600U + static_cast<uint32_t>(kind),
                    CoreIncidentProducer::Session);
}

}  // namespace zomlang::compiler::driver::core_library_query
