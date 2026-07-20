// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"

namespace zomlang::compiler::diagnostics {

enum class SourceDiagnosticPhase : uint8_t { Lex = 1, Parse = 2 };

struct DiagnosticFactRange final {
  uint64_t byteStart = 0;
  uint64_t byteEnd = 0;
  bool isTokenRange = false;

  bool operator==(const DiagnosticFactRange& other) const noexcept = default;
};

struct DiagnosticFixItFact final {
  DiagnosticFactRange range;
  zc::String replacementText;

  ZC_NODISCARD DiagnosticFixItFact clone() const;
  bool operator==(const DiagnosticFixItFact& other) const noexcept;
};

struct SecondaryDiagnosticFact final {
  DiagID code = static_cast<DiagID>(0);
  uint64_t primaryByteOffset = 0;
  zc::Vector<zc::String> arguments;
  zc::Vector<DiagnosticFactRange> ranges;

  ZC_NODISCARD SecondaryDiagnosticFact clone() const;
  bool operator==(const SecondaryDiagnosticFact& other) const noexcept;
};

/// \brief One owned, source-relative, query-safe diagnostic occurrence.
struct DiagnosticFact final {
  SourceDiagnosticPhase phase = SourceDiagnosticPhase::Parse;
  zc::String emitterFile;
  zc::String emitterFunction;
  uint32_t emitterLine = 0;
  uint32_t emitterColumn = 0;
  uint32_t occurrenceOrdinal = 0;
  DiagID code = static_cast<DiagID>(0);
  uint64_t primaryByteOffset = 0;
  zc::Vector<zc::String> arguments;
  zc::Vector<DiagnosticFactRange> ranges;
  zc::Vector<DiagnosticFixItFact> fixIts;
  zc::Vector<SecondaryDiagnosticFact> secondary;

  ZC_NODISCARD DiagnosticFact clone() const;
  bool operator==(const DiagnosticFact& other) const noexcept;
};

/// \brief Sorts facts by complete structural occurrence and assigns duplicate ordinals.
ZC_NODISCARD zc::Vector<DiagnosticFact> canonicalizeDiagnosticFacts(
    zc::Vector<DiagnosticFact>&& facts);

/// \brief Encodes a strictly canonical source-diagnostic fact sequence.
ZC_NODISCARD zc::Array<uint8_t> encodeDiagnosticFacts(
    zc::ArrayPtr<const DiagnosticFact> facts);

/// \brief Strictly decodes and validates source-relative diagnostic facts.
ZC_NODISCARD zc::Maybe<zc::Vector<DiagnosticFact>> decodeDiagnosticFacts(
    zc::ArrayPtr<const uint8_t> encoded, uint64_t sourceByteLength);

/// \brief Returns whether a diagnostic is owned by lexing or parsing.
ZC_NODISCARD bool isSourceSyntaxDiagnostic(DiagID code) noexcept;

}  // namespace zomlang::compiler::diagnostics
