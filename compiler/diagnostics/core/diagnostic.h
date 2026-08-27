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
#include "compiler/diagnostics/core/diagnostic-ids.h"
#include "compiler/source/location.h"

namespace zomlang {
namespace compiler {
namespace diagnostics {

/// Diagnostic argument
///
/// Owned and borrowed strings are retained as canonical text by source publication.
using DiagnosticArgument = zc::OneOf<zc::String, zc::StringPtr>;

class Diagnostic {
public:
  explicit Diagnostic(const DiagID id, const source::SourceLoc loc,
                      zc::Vector<DiagnosticArgument>&& args)
      : id(id), location(loc), diagnosticArgs(zc::mv(args)) {}

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
  ZC_NODISCARD const source::SourceLoc& getLoc() const;
  ZC_NODISCARD zc::ArrayPtr<const DiagnosticArgument> getArgs() const;
  ZC_NODISCARD zc::ArrayPtr<const source::CharSourceRange> getRanges() const;

  void addChildDiagnostic(zc::Own<Diagnostic> child);
  void addRange(const source::CharSourceRange& range);

private:
  DiagID id;
  source::SourceLoc location;
  zc::Vector<DiagnosticArgument> diagnosticArgs;
  zc::Vector<zc::Own<Diagnostic>> childDiagnostics;
  zc::Vector<source::CharSourceRange> ranges;
};

}  // namespace diagnostics
}  // namespace compiler
}  // namespace zomlang
