// Copyright (c) 2024-2025 Zode.Z. All rights reserved
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
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstddef>

#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zomlang/compiler/lexer/token.h"
#include "zomlang/compiler/parser/token-cursor.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {

namespace diagnostics {
class DiagnosticEngine;
}

namespace basic {
struct LangOptions;
class StringPool;
}  // namespace basic

namespace parser {

/// \brief Shared parser state for source, diagnostics, and token access.
class ParserContext {
public:
  ParserContext(const source::SourceManager& sourceMgr,
                diagnostics::DiagnosticEngine& diagnosticEngine, const source::BufferId& bufferId);
  ParserContext(const source::SourceManager& sourceMgr,
                diagnostics::DiagnosticEngine& diagnosticEngine, const basic::LangOptions& langOpts,
                basic::StringPool& stringPool, const source::BufferId& bufferId);

  ZC_DISALLOW_COPY_AND_MOVE(ParserContext);

  /// \brief Reset token access to a freshly lexed stream.
  void resetTokens(zc::ArrayPtr<const lexer::Token> tokens);

  /// \brief Return a cursor positioned at the given absolute token index.
  ZC_NODISCARD TokenCursor cursorAt(size_t index) const;

  /// \brief Return the currently buffered non-EOF token count without forcing EOF.
  ZC_NODISCARD size_t bufferedTokenLimit() const;

  /// \brief Return the token at an absolute token index.
  ZC_NODISCARD const lexer::Token& tokenAt(size_t index) const;

  /// \brief Return the token kind at an absolute token index.
  ZC_NODISCARD ast::SyntaxKind kindAt(size_t index) const;

  /// \brief Return the diagnostic location at index, clamped to EOF.
  ZC_NODISCARD source::SourceLoc diagnosticLoc(size_t index) const;

  /// \brief Return the source range covering the half-open token range.
  ZC_NODISCARD source::SourceRange rangeFor(size_t start, size_t end) const;

  /// \brief Return the current source file identifier.
  ZC_NODISCARD zc::StringPtr fileIdentifier() const;

  /// \brief Return the shared diagnostic engine.
  ZC_NODISCARD diagnostics::DiagnosticEngine& diagnostics() const;

  /// \brief Return the shared source manager.
  ZC_NODISCARD const source::SourceManager& sourceManager() const;

  /// \brief Return the parsed buffer id.
  ZC_NODISCARD const source::BufferId& sourceBuffer() const;

  /// \brief Return the current parser error count.
  ZC_NODISCARD size_t errorCount() const;

  /// \brief Deep-copy parser token data for the Parser capability boundary.
  ZC_NODISCARD zc::Array<ParsedTokenRange> copyBufferedTokenRanges() const;

private:
  const source::SourceManager& sourceMgr;
  diagnostics::DiagnosticEngine& diagnosticEngine;
  source::BufferId bufferId;
  mutable TokenStream stream;
};

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
