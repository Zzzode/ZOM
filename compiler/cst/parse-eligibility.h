// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include <cstdint>

#include "compiler/cst/lexeme-codec.h"
#include "compiler/cst/recovery-codec.h"
#include "zc/core/common.h"

namespace zomlang::compiler::cst {

/// \brief The closed reason a recoverable parse result is ineligible for
/// verified AST construction.
///
/// RFC 0023 "Recoverable Parsing" (L561-563): `ParseSyntaxVerifier` constructs
/// the immutable `ast::Tree` only when the recovery sequence is empty, no
/// `Invalid` lexeme exists, no error-severity parser diagnostic exists, byte
/// coverage and digest verification succeed, and the tree satisfies the RFC 0002
/// schema. This names the first violated condition among the ones decidable from
/// the lexeme stream, the recovery sequence, and the parser-diagnostic summary.
enum class ParseIneligibilityReason : uint8_t {
  /// The recovery sequence bound to the stream is not the one being verified.
  RecoveryStreamMismatch = 0x01,
  /// A recovery element is present (the parse recovered from malformed syntax).
  RecoveryPresent = 0x02,
  /// An `Invalid` lexeme is present in the stream.
  InvalidLexemePresent = 0x03,
  /// At least one error-severity parser diagnostic was produced.
  ParserErrorPresent = 0x04,
};

/// \brief The closed result of the parse-eligibility decision.
///
/// Exactly one of `AcceptClean` (the recovery-free, error-free, fully-covered
/// stream is eligible for RFC 0002 schema verification and AST construction) or
/// the first violated `ParseIneligibilityReason`.
class ParseEligibility final {
public:
  ZC_NODISCARD static ParseEligibility acceptClean() noexcept {
    return ParseEligibility(true, ParseIneligibilityReason::RecoveryPresent);
  }
  ZC_NODISCARD static ParseEligibility ineligible(ParseIneligibilityReason reason) noexcept {
    return ParseEligibility(false, reason);
  }

  ZC_NODISCARD bool isEligible() const noexcept { return eligibleValue; }
  /// \brief The violated condition; valid only when `!isEligible()`.
  ZC_NODISCARD ParseIneligibilityReason reason() const noexcept { return reasonValue; }

private:
  ParseEligibility(bool eligible, ParseIneligibilityReason reason) noexcept
      : eligibleValue(eligible), reasonValue(reason) {}

  bool eligibleValue;
  ParseIneligibilityReason reasonValue;
};

/// \brief Decides whether a recoverable parse result is eligible for verified
/// AST construction.
///
/// RFC 0023 "Recoverable Parsing": `ParseSyntaxVerifier` accepts a parse only
/// when the recovery sequence is empty, no `Invalid` lexeme exists, no
/// error-severity parser diagnostic exists, and byte coverage plus digest
/// verification succeed. The last condition is already discharged by the
/// existence of `stream` (a `VerifiedLexemeStream` is constructed only after the
/// partition and content-digest checks pass), so this gate decides the first
/// three, in RFC order, and reports the first violated condition. The remaining
/// RFC 0002 schema check runs over the constructed `ast::Tree` and is a later
/// slice.
///
/// The recovery sequence must be the one verified against `stream`; a sequence
/// bound to a different `LexemeStreamId` is rejected as a stream mismatch before
/// any content decision.
///
/// \param stream The verified lexeme stream (byte coverage and digest proven).
/// \param recovery The recovery sequence verified against that stream.
/// \param errorSeverityParserDiagnosticCount The number of error-severity (or
///        fatal) parser diagnostics the recoverable parse produced.
/// \return `AcceptClean`, or the first violated `ParseIneligibilityReason`.
ZC_NODISCARD ParseEligibility parseEligibility(const VerifiedLexemeStream& stream,
                                               const VerifiedRecoverySequence& recovery,
                                               uint64_t errorSeverityParserDiagnosticCount);

}  // namespace zomlang::compiler::cst
