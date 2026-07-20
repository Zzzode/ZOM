// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/binder/binding-input.h"
#include "zomlang/compiler/binder/binding-metadata.h"
#include "zomlang/compiler/identity/identity-invariant.h"

namespace zomlang::compiler::diagnostics {
class DiagnosticEngine;
}

namespace zomlang::compiler::binder {

/// \brief Verified binding metadata and export surface published as one capability.
struct VerifiedBindingOutput final {
  VerifiedBindingOutput(VerifiedBindingMetadata&& metadata,
                        VerifiedExportSurface&& surface) noexcept;
  VerifiedBindingOutput(VerifiedBindingOutput&&) noexcept = default;
  VerifiedBindingOutput& operator=(VerifiedBindingOutput&&) noexcept = default;
  ZC_DISALLOW_COPY(VerifiedBindingOutput);
  VerifiedBindingMetadata metadata;
  VerifiedExportSurface surface;
};

/// \brief Source-level binding rejection with deterministically ordered failures.
class SourceRejected final {
public:
  SourceRejected(SourceRejected&&) noexcept = default;
  SourceRejected& operator=(SourceRejected&&) noexcept = default;
  ZC_DISALLOW_COPY(SourceRejected);
  ZC_NODISCARD zc::ArrayPtr<const BindingFailureRef> failures() const noexcept;

private:
  explicit SourceRejected(zc::Vector<BindingFailureRef>&& failures) noexcept;
  zc::Vector<BindingFailureRef> failureValues;
  friend class BindingVerifier;
};

using BindingVerificationFailureValue = zc::OneOf<identity::IdentityInvariant, BinderInvariantFact>;

/// \brief One closed internal invariant failure from binding production or verification.
struct BindingVerificationFailure final {
  explicit BindingVerificationFailure(BindingVerificationFailureValue&& value) noexcept;
  BindingVerificationFailure(BindingVerificationFailure&&) noexcept = default;
  BindingVerificationFailure& operator=(BindingVerificationFailure&&) noexcept = default;
  ZC_DISALLOW_COPY(BindingVerificationFailure);
  BindingVerificationFailureValue value;
};

/// \brief Internal binding rejection that cannot publish semantic facts.
class InvariantRejected final {
public:
  InvariantRejected(InvariantRejected&&) noexcept = default;
  InvariantRejected& operator=(InvariantRejected&&) noexcept = default;
  ZC_DISALLOW_COPY(InvariantRejected);
  ZC_NODISCARD static InvariantRejected single(BindingVerificationFailure&& failure);
  ZC_NODISCARD zc::ArrayPtr<const BindingVerificationFailure> failures() const noexcept;

private:
  explicit InvariantRejected(zc::Vector<BindingVerificationFailure>&& failures) noexcept;
  zc::Vector<BindingVerificationFailure> failureValues;
};

using BindingVerificationResult =
    zc::OneOf<VerifiedBindingOutput, SourceRejected, InvariantRejected>;

/// \brief Builds and structurally verifies one admitted module's binding facts.
/// \param input Complete verified syntax, identity, inventory, and module-graph input.
/// \param diagnostics Typed source-diagnostic sink used only by binding production.
/// \return One closed publication or rejection result.
ZC_NODISCARD BindingVerificationResult runBinding(const VerifiedBindingInput& input,
                                                  diagnostics::DiagnosticEngine& diagnostics);

}  // namespace zomlang::compiler::binder
