// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/diagnostics/text/diagnostic-text.h"

namespace zomlang::compiler::diagnostics {
namespace {

bool isContinuation(uint8_t value) { return (value & 0xc0U) == 0x80U; }

char upperHex(uint8_t value) {
  return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('A' + value - 10);
}

void appendScalarEscape(zc::Vector<char>& output, uint32_t value) {
  output.addAll("\\u{"_zc);
  char digits[6];
  size_t count = 0;
  do {
    digits[count++] = upperHex(static_cast<uint8_t>(value & 0x0fU));
    value >>= 4U;
  } while (value != 0);
  while (count != 0) { output.add(digits[--count]); }
  output.add('}');
}

bool isQuoted(char value, DiagnosticQuote quote) {
  switch (quote) {
    case DiagnosticQuote::None:
      return false;
    case DiagnosticQuote::Single:
      return value == '\'';
    case DiagnosticQuote::Double:
      return value == '"';
    case DiagnosticQuote::Backtick:
      return value == '`';
  }
  ZC_UNREACHABLE
}

}  // namespace

zc::Maybe<DecodedDiagnosticScalar> decodeDiagnosticScalar(zc::ArrayPtr<const zc::byte> source,
                                                          size_t offset) {
  if (offset >= source.size()) return zc::none;
  const auto first = static_cast<uint8_t>(source[offset]);
  if (first < 0x80U) return DecodedDiagnosticScalar{first, 1};
  if (first >= 0xc2U && first <= 0xdfU && offset + 1 < source.size()) {
    const auto second = static_cast<uint8_t>(source[offset + 1]);
    if (isContinuation(second)) {
      return DecodedDiagnosticScalar{
          static_cast<uint32_t>(((first & 0x1fU) << 6U) | (second & 0x3fU)), 2};
    }
  }
  if (first >= 0xe0U && first <= 0xefU && offset + 2 < source.size()) {
    const auto second = static_cast<uint8_t>(source[offset + 1]);
    const auto third = static_cast<uint8_t>(source[offset + 2]);
    const bool secondValid = isContinuation(second) && (first != 0xe0U || second >= 0xa0U) &&
                             (first != 0xedU || second <= 0x9fU);
    if (secondValid && isContinuation(third)) {
      return DecodedDiagnosticScalar{
          static_cast<uint32_t>(((first & 0x0fU) << 12U) | ((second & 0x3fU) << 6U) |
                                (third & 0x3fU)),
          3};
    }
  }
  if (first >= 0xf0U && first <= 0xf4U && offset + 3 < source.size()) {
    const auto second = static_cast<uint8_t>(source[offset + 1]);
    const auto third = static_cast<uint8_t>(source[offset + 2]);
    const auto fourth = static_cast<uint8_t>(source[offset + 3]);
    const bool secondValid = isContinuation(second) && (first != 0xf0U || second >= 0x90U) &&
                             (first != 0xf4U || second <= 0x8fU);
    if (secondValid && isContinuation(third) && isContinuation(fourth)) {
      return DecodedDiagnosticScalar{
          static_cast<uint32_t>(((first & 0x07U) << 18U) | ((second & 0x3fU) << 12U) |
                                ((third & 0x3fU) << 6U) | (fourth & 0x3fU)),
          4};
    }
  }
  return zc::none;
}

void appendDiagnosticByteEscape(zc::Vector<char>& output, uint8_t value) {
  output.addAll("\\x"_zc);
  output.add(upperHex(static_cast<uint8_t>(value >> 4U)));
  output.add(upperHex(static_cast<uint8_t>(value & 0x0fU)));
}

void appendDiagnosticScalar(zc::Vector<char>& output, uint32_t value, DiagnosticQuote quote) {
  if (value >= 0x20U && value <= 0x7eU) {
    const auto character = static_cast<char>(value);
    if (character == '\\' || isQuoted(character, quote)) output.add('\\');
    output.add(character);
    return;
  }
  appendScalarEscape(output, value);
}

zc::String escapeDiagnosticText(zc::ArrayPtr<const zc::byte> bytes, DiagnosticQuote quote,
                                size_t maximumScalars) {
  zc::Vector<char> output;
  size_t offset = 0;
  size_t count = 0;
  while (offset < bytes.size() && count < maximumScalars) {
    ZC_IF_SOME(decoded, decodeDiagnosticScalar(bytes, offset)) {
      appendDiagnosticScalar(output, decoded.value, quote);
      offset += decoded.length;
    } else {
      appendDiagnosticByteEscape(output, static_cast<uint8_t>(bytes[offset++]));
    }
    ++count;
  }
  if (offset != bytes.size()) output.addAll("..."_zc);
  return zc::str(output.releaseAsArray());
}

}  // namespace zomlang::compiler::diagnostics
