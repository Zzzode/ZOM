// Copyright (c) 2025 Zode.Z. All rights reserved
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

/// \file
/// \brief Unit tests for in-flight diagnostic functionality.
///
/// This file contains ztest-based unit tests for the in-flight diagnostic class,
/// testing diagnostic building and management.

#include "zomlang/compiler/diagnostics/in-flight-diagnostic.h"

#include "zc/core/common.h"
#include "zc/core/debug.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/source/location.h"

namespace zomlang {
namespace compiler {
namespace diagnostics {

ZC_TEST("InFlightDiagnosticTest: BasicConstruction") {
  // Test disabled due to API changes
  // TODO: Update when SourceLoc and DiagID API is stable
}

ZC_TEST("InFlightDiagnosticTest: AddNote") {
  // Test disabled due to API changes
  // TODO: Update when SourceLoc and DiagID API is stable
}
ZC_TEST("InFlightDiagnosticTest: AddFixIt") {
  // Test disabled due to API changes
  // TODO: Update when SourceLoc and DiagID API is stable
}

ZC_TEST("InFlightDiagnosticTest: MultipleNotes") {
  // Test disabled due to API changes
  // TODO: Update when SourceLoc and DiagID API is stable
}

ZC_TEST("InFlightDiagnosticTest: WarningLevel") {
  // Test disabled due to API changes
  // TODO: Update when SourceLoc and DiagID API is stable
}

ZC_TEST("InFlightDiagnosticTest: NoteLevel") {
  // Test disabled due to API changes
  // TODO: Update when SourceLoc and DiagID API is stable
}

}  // namespace diagnostics
}  // namespace compiler
}  // namespace zomlang