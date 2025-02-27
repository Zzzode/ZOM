// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/lexer/token.h"

namespace zomlang {
namespace compiler {
namespace parser {

class Parser {
public:
  Parser(diagnostics::DiagnosticEngine& diagnosticEngine, uint64_t bufferId) noexcept;
  ~Parser() noexcept(false) = default;

  ZC_DISALLOW_COPY_AND_MOVE(Parser);

  void parse(zc::ArrayPtr<const lexer::Token> tokens);

private:
  class Impl;
  zc::Own<Impl> impl;
};

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
