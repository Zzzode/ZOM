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

#include "zomlang/compiler/diagnostics/diagnostic.h"

#include "zc/core/common.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/source/location.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {
namespace diagnostics {

// ================================================================================
// Diagnostic

Diagnostic::~Diagnostic() = default;

DiagID Diagnostic::getId() const { return id; }
const zc::Vector<zc::Own<Diagnostic>>& Diagnostic::getChildDiagnostics() const {
  return childDiagnostics;
}
const zc::Vector<zc::Own<FixIt>>& Diagnostic::getFixIts() const { return fixIts; }
const source::SourceLoc& Diagnostic::getLoc() const { return location; }
zc::ArrayPtr<const DiagnosticArgument> Diagnostic::getArgs() const {
  return diagnosticArgs.asPtr();
}
zc::ArrayPtr<const source::CharSourceRange> Diagnostic::getRanges() const { return ranges.asPtr(); }

void Diagnostic::addChildDiagnostic(zc::Own<Diagnostic> child) {
  childDiagnostics.add(zc::mv(child));
}
void Diagnostic::addFixIt(zc::Own<FixIt> fixIt) { fixIts.add(zc::mv(fixIt)); }

}  // namespace diagnostics
}  // namespace compiler
}  // namespace zomlang