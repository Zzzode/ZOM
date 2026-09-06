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
// table, carries a resolved source range, and owns
// copies of the message arguments. It composes as pure data; it touches no
// query, session, or fact.

#include "compiler/ide/snapshot/snapshot-diagnostic.h"

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

SnapshotDiagnosticLocation sourceLocation(SnapshotRange range, uint8_t identity = 0x01) {
  const uint8_t bytes[] = {identity};
  return SnapshotDiagnosticLocation::source(zc::heapArray<uint8_t>(bytes), range);
}

SnapshotDiagnostic project(diagnostics::DiagID code, SnapshotRange range,
                           zc::ArrayPtr<const zc::String> arguments) {
  return SnapshotDiagnostic::project(code, sourceLocation(range), arguments,
                                     zc::Array<SnapshotDiagnosticRelated>());
}

ZC_TEST("SnapshotDiagnostic derives warning severity and carries a resolved range") {
  auto empty = noArguments();
  auto projected = project(diagnostics::DiagID::CheckerUnreachableMatchArm,
                           SnapshotRange{10, 24, true}, empty.asPtr());
  ZC_EXPECT(projected.code() == diagnostics::DiagID::CheckerUnreachableMatchArm);
  ZC_EXPECT(projected.severity() == diagnostics::DiagSeverity::kWarning);
  ZC_EXPECT(projected.primary().range().byteStart == 10);
  ZC_EXPECT(projected.primary().range().byteEnd == 24);
  ZC_EXPECT(projected.primary().range().isTokenRange);
  ZC_EXPECT(projected.arguments().size() == 0);
}

ZC_TEST("SnapshotDiagnostic derives error severity from the diagnostic code") {
  auto empty = noArguments();
  auto projected =
      project(diagnostics::DiagID::InvalidCharacter, SnapshotRange{0, 1, false}, empty.asPtr());
  ZC_EXPECT(projected.severity() == diagnostics::DiagSeverity::kError);
  ZC_EXPECT(!projected.primary().range().isTokenRange);
  ZC_EXPECT(projected.primary().range().byteStart == 0);
  ZC_EXPECT(projected.primary().range().byteEnd == 1);
}

ZC_TEST("SnapshotDiagnostic owns copies of the message arguments") {
  auto arguments = twoArguments("first"_zc, "second"_zc);
  auto projected = project(diagnostics::DiagID::ImportModuleNotFound, SnapshotRange{3, 9, true},
                           arguments.asPtr());
  ZC_EXPECT(projected.arguments().size() == 2);
  ZC_EXPECT(projected.arguments()[0] == "first"_zc);
  ZC_EXPECT(projected.arguments()[1] == "second"_zc);
  // Mutating the source arguments must not affect the owned projection.
  arguments[0] = zc::heapString("mutated"_zc);
  ZC_EXPECT(projected.arguments()[0] == "first"_zc);
}

ZC_TEST("SnapshotDiagnostic clone reproduces an equal projection") {
  auto arguments = oneArgument("only"_zc);
  auto projected = project(diagnostics::DiagID::CheckerUnreachableMatchArm,
                           SnapshotRange{5, 8, false}, arguments.asPtr());
  auto copy = projected.clone();
  ZC_EXPECT(copy == projected);
  ZC_EXPECT(copy.arguments()[0] == "only"_zc);
}

ZC_TEST("SnapshotDiagnostic equality distinguishes every projected field") {
  auto empty = noArguments();
  auto base = project(diagnostics::DiagID::CheckerUnreachableMatchArm, SnapshotRange{5, 8, true},
                      empty.asPtr());
  // Different code.
  ZC_EXPECT(base != project(diagnostics::DiagID::InvalidCharacter, SnapshotRange{5, 8, true},
                            empty.asPtr()));
  // Different range.
  ZC_EXPECT(base != project(diagnostics::DiagID::CheckerUnreachableMatchArm,
                            SnapshotRange{6, 8, true}, empty.asPtr()));
  // Different token-range flag.
  ZC_EXPECT(base != project(diagnostics::DiagID::CheckerUnreachableMatchArm,
                            SnapshotRange{5, 8, false}, empty.asPtr()));
  // Different arguments.
  auto oneArg = oneArgument("x"_zc);
  ZC_EXPECT(base != project(diagnostics::DiagID::CheckerUnreachableMatchArm,
                            SnapshotRange{5, 8, true}, oneArg.asPtr()));
  // Same everything.
  ZC_EXPECT(base == project(diagnostics::DiagID::CheckerUnreachableMatchArm,
                            SnapshotRange{5, 8, true}, empty.asPtr()));
}

ZC_TEST("SnapshotDiagnostic preserves complete related information") {
  zc::Vector<SnapshotDiagnosticRelated> related(3);
  related.add(
      SnapshotDiagnosticRelated::highlight(sourceLocation(SnapshotRange{2, 5, true}, 0x02)));
  auto noteArguments = oneArgument("name"_zc);
  related.add(SnapshotDiagnosticRelated::note(diagnostics::DiagID::ExpectedToken,
                                              sourceLocation(SnapshotRange{8, 8, false}, 0x03),
                                              noteArguments.asPtr()));
  related.add(SnapshotDiagnosticRelated::previousDeclaration(SnapshotDiagnosticLocation::document(
      zc::heapArray<uint8_t>({0x04}), SnapshotRange{13, 13, false})));
  auto empty = noArguments();
  auto projected = SnapshotDiagnostic::project(diagnostics::DiagID::InvalidCharacter,
                                               sourceLocation(SnapshotRange{1, 1, false}),
                                               empty.asPtr(), related.releaseAsArray());

  ZC_REQUIRE(projected.related().size() == 3);
  ZC_EXPECT(projected.related()[0].role() == SnapshotDiagnosticRelatedRole::Highlight);
  ZC_EXPECT(projected.related()[0].code() == zc::none);
  ZC_EXPECT(projected.related()[0].location().resourceKind() ==
            SnapshotDiagnosticResourceKind::Source);
  ZC_EXPECT(projected.related()[1].role() == SnapshotDiagnosticRelatedRole::Note);
  ZC_EXPECT(projected.related()[1].code() == diagnostics::DiagID::ExpectedToken);
  ZC_EXPECT(projected.related()[1].arguments()[0] == "name"_zc);
  ZC_EXPECT(projected.related()[2].role() == SnapshotDiagnosticRelatedRole::PreviousDeclaration);
  ZC_EXPECT(projected.related()[2].code() == diagnostics::DiagID::PreviousDeclarationHere);
  ZC_EXPECT(projected.related()[2].location().resourceKind() ==
            SnapshotDiagnosticResourceKind::Document);

  auto copy = projected.clone();
  ZC_EXPECT(copy == projected);
  ZC_EXPECT(copy.related()[1].location().resourceIdentityBytes()[0] == 0x03);
}

}  // namespace
}  // namespace zomlang::compiler::ide
