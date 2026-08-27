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

#include "zomlang/compiler/binder/diagnostics/binding-diagnostic-adapter.h"

#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/diagnostics/consumer/diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/core/diagnostic-engine.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

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

identity::ModuleId moduleIdentity() {
  using namespace tests::test_identity_detail;
  identity::SemanticContextFactory factory;
  auto context = factory.issue();
  ZC_REQUIRE(context != zc::none);
  ZC_IF_SOME(value, context) {
    auto authorities = identity::IdentityInternerSet::create(factory, value);
    ZC_REQUIRE(authorities != zc::none);
    ZC_IF_SOME(interner, authorities) {
      auto result = interner.internModule(value, module());
      ZC_REQUIRE(result.is<identity::ModuleId>());
      return result.get<identity::ModuleId>();
    }
  }
  ZC_UNREACHABLE;
}

}  // namespace

ZC_TEST("BindingDiagnosticAdapter.EmitsZeroArgumentControlTransferFailures") {
  source::SourceManager sources;
  const auto buffer = sources.addMemBufferCopy("break; continue; continue block;"_zcb, "main.zom");
  const auto breakLoc = sources.getLocForBufferStart(buffer);
  const auto continueLoc = breakLoc.getAdvancedLoc(7);
  const auto blockLoc = breakLoc.getAdvancedLoc(26);
  diagnostics::DiagnosticEngine diagnostics(sources);
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  const auto& captured = *consumer;
  diagnostics.addConsumer(zc::mv(consumer));

  ZC_EXPECT(BindingDiagnosticAdapter::emitControlTransferFailure(
      diagnostics, BinderDiagnosticCode::BreakTargetNotFound, breakLoc));
  ZC_EXPECT(BindingDiagnosticAdapter::emitControlTransferFailure(
      diagnostics, BinderDiagnosticCode::ContinueTargetNotFound, continueLoc));
  ZC_EXPECT(BindingDiagnosticAdapter::emitControlTransferFailure(
      diagnostics, BinderDiagnosticCode::ContinueTargetNotLoop, blockLoc));

  ZC_REQUIRE(captured.ids.size() == 3);
  ZC_REQUIRE(captured.locations.size() == 3);
  ZC_REQUIRE(captured.argumentCounts.size() == 3);
  ZC_EXPECT(captured.ids[0] == diagnostics::DiagID::BreakTargetNotFound);
  ZC_EXPECT(captured.ids[1] == diagnostics::DiagID::ContinueTargetNotFound);
  ZC_EXPECT(captured.ids[2] == diagnostics::DiagID::ContinueTargetNotLoop);
  ZC_EXPECT(captured.locations[0] == breakLoc);
  ZC_EXPECT(captured.locations[1] == continueLoc);
  ZC_EXPECT(captured.locations[2] == blockLoc);
  ZC_EXPECT(captured.argumentCounts[0] == 0);
  ZC_EXPECT(captured.argumentCounts[1] == 0);
  ZC_EXPECT(captured.argumentCounts[2] == 0);
  ZC_EXPECT(diagnostics.errorCount() == 3);
}

ZC_TEST("BindingDiagnosticAdapter.EmitsTypedMissingLabelFailure") {
  source::SourceManager sources;
  const auto buffer = sources.addMemBufferCopy("missing"_zcb, "main.zom");
  const auto primary = sources.getLocForBufferStart(buffer);
  diagnostics::DiagnosticEngine diagnostics(sources);
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  const auto& captured = *consumer;
  diagnostics.addConsumer(zc::mv(consumer));
  auto identifier = identity::SemanticIdentifier::fromSource("missing"_zc);
  ZC_REQUIRE(identifier != zc::none);
  ZC_IF_SOME(value, identifier) {
    ZC_EXPECT(BindingDiagnosticAdapter::emitLabelLookupFailure(
        diagnostics, BinderDiagnosticCode::UndefinedIdentifier, primary,
        VerifiedIdentifierArgument::from(value)));
  }
  ZC_REQUIRE(captured.ids.size() == 1);
  ZC_EXPECT(captured.ids[0] == diagnostics::DiagID::UndefinedIdentifier);
  ZC_EXPECT(captured.locations[0] == primary);
  ZC_EXPECT(captured.argumentCounts[0] == 1);
  ZC_EXPECT(diagnostics.errorCount() == 1);
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

ZC_TEST("BindingMetadata.GroupsAndEmitsEveryInvariantKind") {
  const auto module = moduleIdentity();
  const BinderInvariantKind kinds[] = {
      BinderInvariantKind::InvalidEmitterOrdinal,
      BinderInvariantKind::MissingRequiredResolution,
      BinderInvariantKind::MalformedScopeGraph,
      BinderInvariantKind::InvalidBindingFact,
      BinderInvariantKind::AliasCycle,
      BinderInvariantKind::MissingRequiredResolution,
  };
  const auto invariantKinds = zc::arrayPtr(kinds);
  zc::Vector<BinderInvariantFact> facts;
  for (size_t index = 0; index < invariantKinds.size(); ++index) {
    zc::Maybe<identity::UnbrandedSourceRange> noRange;
    facts.add(BinderInvariantFact{invariantKinds[index], module, zc::mv(noRange),
                                  BinderEmitterSite::ModuleSkeleton,
                                  static_cast<uint32_t>(invariantKinds.size() - index)});
  }

  auto groups = groupBinderInvariants(facts.asPtr());
  ZC_REQUIRE(groups != zc::none);
  ZC_IF_SOME(values, groups) {
    ZC_REQUIRE(values.size() == 5);
    ZC_EXPECT(values[0].diagnosticId() == diagnostics::DiagID::BinderMalformedScopeGraph);
    ZC_EXPECT(values[1].diagnosticId() == diagnostics::DiagID::BinderMissingRequiredResolution);
    ZC_EXPECT(values[1].occurrenceCount() == 2);
    ZC_EXPECT(values[2].diagnosticId() == diagnostics::DiagID::BinderAliasCycle);
    ZC_EXPECT(values[3].diagnosticId() == diagnostics::DiagID::BinderInvalidFact);
    ZC_EXPECT(values[4].diagnosticId() == diagnostics::DiagID::BinderInvalidEmitterOrdinal);
    for (const auto& group : values) { ZC_EXPECT(group.diagnosticRange() == zc::none); }

    source::SourceManager sources;
    diagnostics::DiagnosticEngine diagnostics(sources);
    auto consumer = zc::heap<CapturingDiagnosticConsumer>();
    const auto& captured = *consumer;
    diagnostics.addConsumer(zc::mv(consumer));
    emitBinderInvariantGroups(diagnostics, values.asPtr());
    ZC_REQUIRE(captured.ids.size() == values.size());
    for (size_t index = 0; index < values.size(); ++index) {
      ZC_EXPECT(captured.ids[index] == values[index].diagnosticId());
      ZC_EXPECT(captured.argumentCounts[index] == 1);
    }
  }
}

}  // namespace zomlang::compiler::binder
