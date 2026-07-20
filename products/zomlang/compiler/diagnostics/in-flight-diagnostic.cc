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
#include "zomlang/compiler/diagnostics/diagnostic-emitter.h"
#include "zomlang/compiler/diagnostics/diagnostic.h"
#include "zomlang/compiler/source/location.h"

namespace zomlang {
namespace compiler {
namespace diagnostics {

// ================================================================================
// InflightDiagnostic::Impl

struct InFlightDiagnostic::Impl {
  Impl(DiagnosticEmitter& emitter, Diagnostic&& diag, zc::SourceLocation emitterLocation)
      : emitter(emitter), diag(zc::mv(diag)), emitterLocation(emitterLocation), emitted(false) {}

  DiagnosticEmitter& emitter;
  Diagnostic diag;
  zc::SourceLocation emitterLocation;
  bool emitted;
};

// ================================================================================
// InFlightDiagnostic

InFlightDiagnostic::InFlightDiagnostic(DiagnosticEmitter& emitter, Diagnostic&& diag,
                                       zc::SourceLocation emitterLocation)
    : impl(zc::heap<Impl>(emitter, zc::mv(diag), emitterLocation)) {}

InFlightDiagnostic::InFlightDiagnostic(InFlightDiagnostic&& other) noexcept = default;

InFlightDiagnostic::~InFlightDiagnostic() {
  if (!impl->emitted) { emit(); }
}

void InFlightDiagnostic::emit() {
  if (!impl->emitted) {
    impl->emitter.emitDiagnostic(impl->diag, impl->emitterLocation);
    impl->emitted = true;
  }
}

InFlightDiagnostic& InFlightDiagnostic::addFixIt(zc::Own<FixIt> fixit) {
  impl->diag.addFixIt(zc::mv(fixit));
  return *this;
}

InFlightDiagnostic& InFlightDiagnostic::addRange(const source::CharSourceRange& range) {
  impl->diag.addRange(range);
  return *this;
}

InFlightDiagnostic& InFlightDiagnostic::addChild(zc::Own<Diagnostic> child) {
  impl->diag.addChildDiagnostic(zc::mv(child));
  return *this;
}

}  // namespace diagnostics
}  // namespace compiler
}  // namespace zomlang
