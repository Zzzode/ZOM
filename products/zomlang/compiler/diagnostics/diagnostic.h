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

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/lexer/token.h"
#include "zomlang/compiler/source/location.h"

namespace zomlang {
namespace compiler {

namespace source {
class SourceLoc;
}

namespace diagnostics {

struct FixIt {
  source::CharSourceRange range;
  zc::String replacementText;
};

using DiagnosticArgument = zc::OneOf<zc::StringPtr,  // Str
                                     lexer::Token    // Token
                                     >;

class Diagnostic {
public:
  template <typename... Args>
  explicit Diagnostic(const DiagID id, const source::SourceLoc loc, Args&&... args)
      : id(id), location(loc) {
    (diagnosticArgs.add(zc::fwd<Args>(args)), ...);
  }
  ~Diagnostic();

  Diagnostic(Diagnostic&& other) noexcept = default;
  Diagnostic& operator=(Diagnostic&& other) noexcept = default;

  ZC_DISALLOW_COPY(Diagnostic);

  ZC_NODISCARD DiagID getId() const;
  ZC_NODISCARD const zc::Vector<zc::Own<Diagnostic>>& getChildDiagnostics() const;
  ZC_NODISCARD const zc::Vector<zc::Own<FixIt>>& getFixIts() const;
  ZC_NODISCARD const source::SourceLoc& getLoc() const;
  ZC_NODISCARD zc::ArrayPtr<const DiagnosticArgument> getArgs() const;
  ZC_NODISCARD zc::ArrayPtr<const source::CharSourceRange> getRanges() const;

  void addChildDiagnostic(zc::Own<Diagnostic> child);
  void addFixIt(zc::Own<FixIt> fixIt);

private:
  DiagID id;
  source::SourceLoc location;
  zc::Vector<DiagnosticArgument> diagnosticArgs;
  zc::Vector<zc::Own<Diagnostic>> childDiagnostics;
  zc::Vector<zc::Own<FixIt>> fixIts;
  zc::Vector<source::CharSourceRange> ranges;
};

}  // namespace diagnostics
}  // namespace compiler
}  // namespace zomlang
