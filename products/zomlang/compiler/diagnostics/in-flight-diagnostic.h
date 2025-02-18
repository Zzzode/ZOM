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

namespace zomlang {
namespace compiler {
namespace diagnostics {

struct FixIt;

class Diagnostic;
class DiagnosticEngine;

class InFlightDiagnostic {
public:
  InFlightDiagnostic(DiagnosticEngine& engine, Diagnostic&& diag);
  ~InFlightDiagnostic();

  InFlightDiagnostic(InFlightDiagnostic&& other) noexcept = default;
  InFlightDiagnostic& operator=(InFlightDiagnostic&& other) noexcept = delete;

  ZC_DISALLOW_COPY(InFlightDiagnostic);

  void emit();

  /// Add methods to modify the diagnostic, for example, add fix-its
  InFlightDiagnostic& addFixIt(zc::Own<FixIt> fixit);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace diagnostics
}  // namespace compiler
}  // namespace zomlang