// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zomlang/compiler/binder/internal/binding-verifier.h"

namespace zomlang::compiler::binder {

/// \brief Encodes one candidate with stable fact-domain tags for test comparison.
ZC_NODISCARD zc::Maybe<zc::Array<uint8_t>> encodeBindingCandidate(
    const VerifiedBindingInput& input, const BindingMetadataCandidate& candidate);

/// \brief Encodes one requester-filtered export-surface sequence canonically.
ZC_NODISCARD zc::Maybe<zc::Array<uint8_t>> encodeBindingSurfaceMap(
    const VerifiedBindingInput& input, zc::ArrayPtr<const ExportSurfaceEntry> entries);

/// \brief Encodes one export-surface entry with canonical identity projection.
ZC_NODISCARD zc::Maybe<zc::Array<uint8_t>> encodeBindingSurfaceEntry(
    const VerifiedBindingInput& input, const ExportSurfaceEntry& entry);

}  // namespace zomlang::compiler::binder
