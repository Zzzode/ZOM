// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
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

/// \brief Explicit admission limits shared by diagnostic fact encoding and decoding.
struct DiagnosticFactCodecLimits final {
  uint64_t maximumFacts;
  uint64_t maximumEncodedBytes;
  uint64_t maximumSourceByteOffset;
};

/// \brief Sorts facts by complete structural occurrence and assigns duplicate ordinals.
ZC_NODISCARD zc::Vector<DiagnosticFact> canonicalizeDiagnosticFacts(
    zc::Vector<DiagnosticFact>&& facts);

/// \brief Validates and encodes a strictly canonical diagnostic fact sequence.
/// \param outputResource Optional resource that must outlive the returned byte array.
/// \param facts Canonically ordered fact sequence.
/// \param limits Complete count, byte, and source-offset admission limits.
ZC_NODISCARD zc::Maybe<zc::Array<uint8_t>> encodeDiagnosticFacts(
    zc::Maybe<zc::MemoryResource&> outputResource, zc::ArrayPtr<const DiagnosticFact> facts,
    DiagnosticFactCodecLimits limits);

/// \brief Strictly decodes and validates canonical diagnostic facts.
/// \param resultResource Optional resource that must outlive the returned facts and nested values.
/// \param encoded Complete canonical payload.
/// \param limits Complete count, byte, and source-offset admission limits.
ZC_NODISCARD zc::Maybe<zc::Vector<DiagnosticFact>> decodeDiagnosticFacts(
    zc::Maybe<zc::MemoryResource&> resultResource, zc::ArrayPtr<const uint8_t> encoded,
    DiagnosticFactCodecLimits limits);

/// \brief Returns whether a diagnostic is owned by lexing or parsing.
ZC_NODISCARD bool isSourceSyntaxDiagnostic(DiagID code) noexcept;

}  // namespace zomlang::compiler::diagnostics
