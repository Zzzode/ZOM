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
#include "zc/core/vector.h"
#include "zomlang/compiler/source/location.h"

namespace zomlang {
namespace compiler {

namespace source {
class SourceManager;
}

namespace diagnostics {

class DiagnosticState {
public:
  DiagnosticState();
  DiagnosticState(DiagnosticState&&) noexcept = default;

  ZC_DISALLOW_COPY(DiagnosticState);

  DiagnosticState& operator=(DiagnosticState&&) noexcept = default;

  bool getShowDiagnosticsAfterFatalError() const { return showDiagnosticsAfterFatalError; }
  void setShowDiagnosticsAfterFatalError(bool value) { showDiagnosticsAfterFatalError = value; }

  bool getSuppressWarnings() const { return suppressWarnings; }
  void setSuppressWarnings(bool value) { suppressWarnings = value; }

  void ignoreDiagnostic(uint32_t diag_id);
  bool isDiagnosticIgnored(uint32_t diag_id) const;

  bool getHadAnyError() const { return hadAnyError; }
  void setHadAnyError() { hadAnyError = true; }

  static source::CharSourceRange toCharSourceRange(const source::SourceManager& sm,
                                                   source::SourceRange range);
  static char extractCharAfter(const source::SourceManager& sm, source::SourceLoc loc);

private:
  bool showDiagnosticsAfterFatalError = false;
  bool suppressWarnings = false;
  bool hadAnyError = false;
  zc::Vector<bool> ignoredDiagnostics;

  /// Assume 1000 diagnostic ids
  static constexpr uint32_t kNumDiags = 1000;
};

}  // namespace diagnostics
}  // namespace compiler
}  // namespace zomlang
