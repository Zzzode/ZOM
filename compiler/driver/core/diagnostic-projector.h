// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "compiler/basic/incident/compiler-incident.h"
#include "compiler/driver/core/query.h"
#include "compiler/driver/core/role-seed-failure.h"
#include "compiler/query/query-types.h"
#include "compiler/source/core-source-admission.h"
#include "zc/core/common.h"

namespace zomlang::compiler::driver::core_library_query {

enum class CoreIncidentPhase : uint16_t {
  DistributionAdmission = 0x0201,
  InputPreparation = 0x0202,
  InputCommit = 0x0203,
  QueryEvaluation = 0x0204,
  SessionIntegration = 0x0205,
};

enum class CoreIncidentProducer : uint16_t {
  SourceAdmission = 0x0201,
  SourceCatalog = 0x0202,
  InputTransaction = 0x0203,
  QueryRuntime = 0x0204,
  Session = 0x0205,
};

/// \brief Closed session-local invariant failures around core installation and publication.
enum class CoreSessionInvariantKind : uint8_t {
  InvalidInstallationState = 0x01,
  CompilationOptionsRejected = 0x02,
  PackageRootProjectionRejected = 0x03,
  PackageGraphProjectionRejected = 0x04,
  AmbiguousPackageRoot = 0x05,
  CoreCrateProjectionRejected = 0x06,
  CompilationRootUnavailable = 0x07,
  ConsumerSearchRootsRejected = 0x08,
  CoreSearchRootRejected = 0x09,
  CoreSearchRootsRejected = 0x0a,
  ContextAuthorityRejected = 0x0b,
  TransactionPreparationRejected = 0x0c,
  ProjectionCatalogMismatch = 0x0d,
  ProjectionSnapshotMismatch = 0x0e,
  SourceRegistrationRejected = 0x0f,
  CoreGraphRejected = 0x10,
  CorePreludeRejected = 0x11,
  CoreRoleAuthorityRejected = 0x12,
  CoreAuthorityRejected = 0x13,
  CoreInterfaceRejected = 0x14,
  CoreLibraryRejected = 0x15,
};

/// \brief Operational core-query failures that callers may report or retry.
enum class CoreOperationalFailureKind : uint8_t {
  AllocationFailed = 0x01,
  Cancelled = 0x02,
};

ZC_NODISCARD zc::Maybe<CoreOperationalFailureKind> coreOperationalFailure(
    query::QueryRuntimeFailure failure) noexcept;
ZC_NODISCARD zc::StringPtr coreOperationalFailureDisplay(
    CoreOperationalFailureKind failure) noexcept;

/// \brief Projects owner-local core invariants to registered compiler incidents.
class CoreDiagnosticProjector final {
public:
  ZC_NODISCARD static basic::CompilerIncidentDescriptor project(
      source::core::CoreDistributionAdmissionInvariantKind kind, CoreIncidentPhase phase,
      CoreIncidentProducer producer) noexcept;
  ZC_NODISCARD static basic::CompilerIncidentDescriptor project(
      CoreDistributionInputPreparationInvariantKind kind) noexcept;
  ZC_NODISCARD static basic::CompilerIncidentDescriptor project(
      query::InputTransactionFailure failure) noexcept;
  ZC_NODISCARD static basic::CompilerIncidentDescriptor project(
      query::QueryRuntimeFailure failure) noexcept;
  ZC_NODISCARD static basic::CompilerIncidentDescriptor project(
      CoreRoleSeedFailureKind kind) noexcept;
  ZC_NODISCARD static basic::CompilerIncidentDescriptor project(
      CoreSessionInvariantKind kind) noexcept;

private:
  ZC_NODISCARD static basic::CompilerIncidentDescriptor descriptor(
      CoreIncidentPhase phase, uint32_t kind, CoreIncidentProducer producer) noexcept;
};

}  // namespace zomlang::compiler::driver::core_library_query
