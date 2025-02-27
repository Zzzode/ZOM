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

#include "zomlang/compiler/diagnostics/in-flight-diagnostic.h"

#include "zc/core/common.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic.h"

namespace zomlang {
namespace compiler {
namespace diagnostics {

// ================================================================================
// InflightDiagnostic::Impl

struct InFlightDiagnostic::Impl {
  Impl(DiagnosticEngine& engine, Diagnostic&& diag)
      : engine(engine), diag(zc::mv(diag)), emitted(false) {}

  DiagnosticEngine& engine;
  Diagnostic diag;
  bool emitted;
};

// ================================================================================
// InFlightDiagnostic

InFlightDiagnostic::InFlightDiagnostic(DiagnosticEngine& engine, Diagnostic&& diag)
    : impl(zc::heap<Impl>(engine, zc::mv(diag))) {}

InFlightDiagnostic::~InFlightDiagnostic() {
  if (!impl->emitted) { emit(); }
}

void InFlightDiagnostic::emit() {
  if (!impl->emitted) {
    impl->engine.emit(impl->diag);
    impl->emitted = true;
  }
}

InFlightDiagnostic& InFlightDiagnostic::addFixIt(zc::Own<FixIt> fixit) {
  impl->diag.addFixIt(zc::mv(fixit));
  return *this;
}

}  // namespace diagnostics
}  // namespace compiler
}  // namespace zomlang