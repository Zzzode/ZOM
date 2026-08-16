// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstddef>
#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::diagnostics {

enum class DiagnosticQuote : uint8_t { None, Single, Double, Backtick };

struct DecodedDiagnosticScalar final {
  uint32_t value;
  uint8_t length;
};

/// \brief Decode one valid Unicode scalar without accepting overlong or surrogate encodings.
ZC_NODISCARD zc::Maybe<DecodedDiagnosticScalar> decodeDiagnosticScalar(
    zc::ArrayPtr<const zc::byte> source, size_t offset);

/// \brief Append an escaped byte that cannot be represented as a valid Unicode scalar.
void appendDiagnosticByteEscape(zc::Vector<char>& output, uint8_t value);

/// \brief Append one scalar using deterministic ASCII-only diagnostic escaping.
void appendDiagnosticScalar(zc::Vector<char>& output, uint32_t value, DiagnosticQuote quote);

/// \brief Escape arbitrary bytes and truncate only at scalar boundaries.
ZC_NODISCARD zc::String escapeDiagnosticText(zc::ArrayPtr<const zc::byte> bytes,
                                             DiagnosticQuote quote = DiagnosticQuote::None,
                                             size_t maximumScalars = static_cast<size_t>(-1));

}  // namespace zomlang::compiler::diagnostics
