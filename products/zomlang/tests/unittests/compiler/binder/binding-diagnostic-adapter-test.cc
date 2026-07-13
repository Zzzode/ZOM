// Copyright (c) 2026 Zode.Z. All rights reserved
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

#include "zomlang/compiler/binder/binding-diagnostic-adapter.h"

#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/diagnostics/diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang::compiler::binder {
namespace {

class CapturingDiagnosticConsumer final : public diagnostics::DiagnosticConsumer {
public:
  zc::Vector<diagnostics::DiagID> ids;
  zc::Vector<source::SourceLoc> locations;
  zc::Vector<size_t> argumentCounts;

  void handleDiagnostic(const source::SourceManager&,
                        const diagnostics::Diagnostic& diagnostic) override {
    ids.add(diagnostic.getId());
    locations.add(diagnostic.getLoc());
    argumentCounts.add(diagnostic.getArgs().size());
  }
};

}  // namespace

ZC_TEST("BindingDiagnosticAdapter.EmitsZeroArgumentControlTransferFailures") {
  source::SourceManager sources;
  const auto buffer = sources.addMemBufferCopy("break; continue;"_zcb, "main.zom");
  const auto breakLoc = sources.getLocForBufferStart(buffer);
  const auto continueLoc = breakLoc.getAdvancedLoc(7);
  diagnostics::DiagnosticEngine diagnostics(sources);
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  const auto& captured = *consumer;
  diagnostics.addConsumer(zc::mv(consumer));

  ZC_EXPECT(BindingDiagnosticAdapter::emitControlTransferFailure(
      diagnostics, BinderDiagnosticCode::BreakTargetNotFound, breakLoc));
  ZC_EXPECT(BindingDiagnosticAdapter::emitControlTransferFailure(
      diagnostics, BinderDiagnosticCode::ContinueTargetNotFound, continueLoc));

  ZC_REQUIRE(captured.ids.size() == 2);
  ZC_REQUIRE(captured.locations.size() == 2);
  ZC_REQUIRE(captured.argumentCounts.size() == 2);
  ZC_EXPECT(captured.ids[0] == diagnostics::DiagID::BreakTargetNotFound);
  ZC_EXPECT(captured.ids[1] == diagnostics::DiagID::ContinueTargetNotFound);
  ZC_EXPECT(captured.locations[0] == breakLoc);
  ZC_EXPECT(captured.locations[1] == continueLoc);
  ZC_EXPECT(captured.argumentCounts[0] == 0);
  ZC_EXPECT(captured.argumentCounts[1] == 0);
  ZC_EXPECT(diagnostics.errorCount() == 2);
}

ZC_TEST("BindingDiagnosticAdapter.RejectsUnsupportedControlTransferCodes") {
  source::SourceManager sources;
  const auto buffer = sources.addMemBufferCopy("break;"_zcb, "main.zom");
  const auto primary = sources.getLocForBufferStart(buffer);
  diagnostics::DiagnosticEngine diagnostics(sources);
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  const auto& captured = *consumer;
  diagnostics.addConsumer(zc::mv(consumer));

  const BinderDiagnosticCode rejected[] = {
      BinderDiagnosticCode::UndefinedIdentifier,
      BinderDiagnosticCode::RedeclareVariable,
      BinderDiagnosticCode::PreviousDeclarationHere,
  };
  for (const auto code : rejected) {
    ZC_EXPECT(!BindingDiagnosticAdapter::emitControlTransferFailure(diagnostics, code, primary));
  }

  ZC_EXPECT(captured.ids.empty());
  ZC_EXPECT(captured.locations.empty());
  ZC_EXPECT(captured.argumentCounts.empty());
  ZC_EXPECT(diagnostics.errorCount() == 0);
}

}  // namespace zomlang::compiler::binder
