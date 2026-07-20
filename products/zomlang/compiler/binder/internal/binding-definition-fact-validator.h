// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zomlang/compiler/binder/internal/binding-verifier.h"

namespace zomlang::compiler::binder {

/// \brief Closed result of subordinate Definition-domain fact validation.
enum class BindingDefinitionFactValidationResult : uint8_t {
  Valid,
  MissingRequiredResolution,
  InvalidBindingFact
};

/// \brief Validates generic, callable, and owner-local facts against frozen authority.
/// \return The exact structural classification without invoking producer algorithms.
ZC_NODISCARD BindingDefinitionFactValidationResult verifyBindingDefinitionFacts(
    const VerifiedBindingInput& input, const BindingMetadataCandidate& candidate);

}  // namespace zomlang::compiler::binder
