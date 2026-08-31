// Copyright (c) 2026 Zode.Z. All rights reserved
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
// See the License for the specific language governing permissions and limitations under
// the License.

#include "compiler/ide/snapshot-diagnostic.h"

#include "compiler/diagnostics/core/diagnostic-info.h"

namespace zomlang::compiler::ide {
namespace {

zc::Array<zc::String> copyArguments(zc::ArrayPtr<const zc::String> arguments) {
  auto copies = zc::heapArray<zc::String>(arguments.size());
  for (size_t index = 0; index < arguments.size(); ++index) {
    copies[index] = zc::heapString(arguments[index]);
  }
  return copies;
}

}  // namespace

SnapshotDiagnostic SnapshotDiagnostic::projectRanged(diagnostics::DiagID code, SnapshotRange range,
                                                     zc::ArrayPtr<const zc::String> arguments) {
  return SnapshotDiagnostic(code, diagnostics::getDiagnosticInfo(code).severity, range,
                            copyArguments(arguments));
}

SnapshotDiagnostic SnapshotDiagnostic::projectRangeless(diagnostics::DiagID code,
                                                        zc::ArrayPtr<const zc::String> arguments) {
  return SnapshotDiagnostic(code, diagnostics::getDiagnosticInfo(code).severity, zc::none,
                            copyArguments(arguments));
}

SnapshotDiagnostic SnapshotDiagnostic::clone() const {
  return SnapshotDiagnostic(codeValue, severityValue, rangeValue,
                            copyArguments(argumentValues.asPtr()));
}

bool SnapshotDiagnostic::operator==(const SnapshotDiagnostic& other) const noexcept {
  if (codeValue != other.codeValue || severityValue != other.severityValue ||
      rangeValue != other.rangeValue || argumentValues.size() != other.argumentValues.size()) {
    return false;
  }
  for (size_t index = 0; index < argumentValues.size(); ++index) {
    if (argumentValues[index] != other.argumentValues[index]) { return false; }
  }
  return true;
}

}  // namespace zomlang::compiler::ide
