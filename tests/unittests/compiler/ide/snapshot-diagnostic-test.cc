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
// See the License for the specific language governing permissions and
// limitations under the License.

// RFC 0023 "IDE Semantic Snapshots" Authority Rails: prove SnapshotDiagnostic is
// an IDE-safe projection that derives severity from the authoritative diagnostic
// table, retains the resolved byte range, and owns copies of the message
// arguments. It composes as pure data; it touches no query, session, or fact.

#include "compiler/ide/snapshot-diagnostic.h"

#include "compiler/diagnostics/core/diagnostic-ids.h"
#include "zc/core/array.h"
#include "zc/core/string.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::ide {
namespace {

zc::Array<zc::String> noArguments() { return zc::heapArray<zc::String>(0); }

zc::Array<zc::String> oneArgument(zc::StringPtr value) {
  auto result = zc::heapArray<zc::String>(1);
  result[0] = zc::heapString(value);
  return result;
}

zc::Array<zc::String> twoArguments(zc::StringPtr first, zc::StringPtr second) {
  auto result = zc::heapArray<zc::String>(2);
  result[0] = zc::heapString(first);
  result[1] = zc::heapString(second);
  return result;
}

ZC_TEST("SnapshotDiagnostic derives warning severity from the diagnostic code") {
  auto empty = noArguments();
  auto projected = SnapshotDiagnostic::project(diagnostics::DiagID::CheckerUnreachableMatchArm, 10,
                                               24, true, empty.asPtr());
  ZC_EXPECT(projected.code() == diagnostics::DiagID::CheckerUnreachableMatchArm);
  ZC_EXPECT(projected.severity() == diagnostics::DiagSeverity::kWarning);
  ZC_EXPECT(projected.byteStart() == 10);
  ZC_EXPECT(projected.byteEnd() == 24);
  ZC_EXPECT(projected.isTokenRange());
  ZC_EXPECT(projected.arguments().size() == 0);
}

ZC_TEST("SnapshotDiagnostic derives error severity from the diagnostic code") {
  auto empty = noArguments();
  auto projected = SnapshotDiagnostic::project(diagnostics::DiagID::InvalidCharacter, 0, 1, false,
                                               empty.asPtr());
  ZC_EXPECT(projected.severity() == diagnostics::DiagSeverity::kError);
  ZC_EXPECT(!projected.isTokenRange());
  ZC_EXPECT(projected.byteStart() == 0);
  ZC_EXPECT(projected.byteEnd() == 1);
}

ZC_TEST("SnapshotDiagnostic owns copies of the message arguments") {
  auto arguments = twoArguments("first"_zc, "second"_zc);
  auto projected = SnapshotDiagnostic::project(diagnostics::DiagID::ImportModuleNotFound, 3, 9,
                                               true, arguments.asPtr());
  ZC_EXPECT(projected.arguments().size() == 2);
  ZC_EXPECT(projected.arguments()[0] == "first"_zc);
  ZC_EXPECT(projected.arguments()[1] == "second"_zc);
  // Mutating the source arguments must not affect the owned projection.
  arguments[0] = zc::heapString("mutated"_zc);
  ZC_EXPECT(projected.arguments()[0] == "first"_zc);
}

ZC_TEST("SnapshotDiagnostic clone reproduces an equal projection") {
  auto arguments = oneArgument("only"_zc);
  auto projected = SnapshotDiagnostic::project(diagnostics::DiagID::CheckerUnreachableMatchArm, 5,
                                               8, false, arguments.asPtr());
  auto copy = projected.clone();
  ZC_EXPECT(copy == projected);
  ZC_EXPECT(copy.arguments()[0] == "only"_zc);
}

ZC_TEST("SnapshotDiagnostic equality distinguishes every projected field") {
  auto empty = noArguments();
  auto base = SnapshotDiagnostic::project(diagnostics::DiagID::CheckerUnreachableMatchArm, 5, 8,
                                          true, empty.asPtr());
  // Different code.
  ZC_EXPECT(base != SnapshotDiagnostic::project(diagnostics::DiagID::InvalidCharacter, 5, 8, true,
                                                empty.asPtr()));
  // Different range.
  ZC_EXPECT(base != SnapshotDiagnostic::project(diagnostics::DiagID::CheckerUnreachableMatchArm, 6,
                                                8, true, empty.asPtr()));
  ZC_EXPECT(base != SnapshotDiagnostic::project(diagnostics::DiagID::CheckerUnreachableMatchArm, 5,
                                                9, true, empty.asPtr()));
  // Different token-range flag.
  ZC_EXPECT(base != SnapshotDiagnostic::project(diagnostics::DiagID::CheckerUnreachableMatchArm, 5,
                                                8, false, empty.asPtr()));
  // Different arguments.
  auto oneArg = oneArgument("x"_zc);
  ZC_EXPECT(base != SnapshotDiagnostic::project(diagnostics::DiagID::CheckerUnreachableMatchArm, 5,
                                                8, true, oneArg.asPtr()));
  // Same everything.
  ZC_EXPECT(base == SnapshotDiagnostic::project(diagnostics::DiagID::CheckerUnreachableMatchArm, 5,
                                                8, true, empty.asPtr()));
}

}  // namespace
}  // namespace zomlang::compiler::ide
