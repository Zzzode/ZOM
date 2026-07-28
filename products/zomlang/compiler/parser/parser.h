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

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/parser/token-snapshot.h"

namespace zomlang {
namespace compiler {

namespace basic {
class StringPool;
struct LangOptions;
}  // namespace basic

namespace diagnostics {
class SourceDiagnosticDraftBuffer;
}

namespace source {
class BufferId;
class SourceManager;
}  // namespace source

namespace parser {

/// \brief Recursive-descent parser facade that emits the schema-backed AST tree.
class Parser {
public:
  Parser(const source::SourceManager& sm, diagnostics::SourceDiagnosticDraftBuffer& diagnosticFacts,
         const basic::LangOptions& langOpts, basic::StringPool& stringPool,
         const source::BufferId& bufferId);
  ~Parser() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(Parser);

  /// \brief Parse the source file and return the syntax tree.
  zc::Maybe<ast::Tree> parse();

  /// \brief Return complete token provenance only after a successful parse.
  ZC_NODISCARD zc::Maybe<ParsedTokenSnapshot> takeTokenSnapshot();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
