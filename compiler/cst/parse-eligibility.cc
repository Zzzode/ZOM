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

#include "compiler/cst/parse-eligibility.h"

namespace zomlang::compiler::cst {

ParseEligibility parseEligibility(const VerifiedLexemeStream& stream,
                                  const VerifiedRecoverySequence& recovery,
                                  uint64_t errorSeverityParserDiagnosticCount) {
  // The recovery sequence must be the one verified against this stream.
  if (recovery.lexemeStreamId() != stream.id()) {
    return ParseEligibility::ineligible(ParseIneligibilityReason::RecoveryStreamMismatch);
  }

  // (1) The recovery sequence must be empty.
  if (recovery.elements().size() != 0) {
    return ParseEligibility::ineligible(ParseIneligibilityReason::RecoveryPresent);
  }

  // (2) No Invalid lexeme may be present.
  for (const auto& lexeme : stream.lexemes()) {
    if (lexeme.tag() == CstLexemeTag::Invalid) {
      return ParseEligibility::ineligible(ParseIneligibilityReason::InvalidLexemePresent);
    }
  }

  // (3) No error-severity parser diagnostic may exist.
  if (errorSeverityParserDiagnosticCount != 0) {
    return ParseEligibility::ineligible(ParseIneligibilityReason::ParserErrorPresent);
  }

  // (4) Byte coverage and digest verification are already discharged by the
  // existence of the VerifiedLexemeStream. The RFC 0002 schema check over the
  // constructed ast::Tree is a later slice.
  return ParseEligibility::acceptClean();
}

}  // namespace zomlang::compiler::cst
