// Copyright (c) 2025 Zode.Z. All rights reserved
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

#include "zc/core/io.h"
#include "zc/core/string.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/diagnostics/consoling-diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {
namespace diagnostics {

namespace {

class CountingDiagnosticConsumer final : public DiagnosticConsumer {
public:
  size_t diagnosticCount = 0;

  void handleDiagnostic(const source::SourceManager&, const Diagnostic&) override {
    ++diagnosticCount;
  }
};

}  // namespace

ZC_TEST("DiagnosticTest.BasicDiagnosticReporting") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  auto consumer = zc::heap<ConsolingDiagnosticConsumer>();
  diagnosticEngine->addConsumer(zc::mv(consumer));

  auto bufferId = sourceManager->addMemBufferCopy(zc::StringPtr("let x = ;").asBytes(), "test.zom");
  auto loc = sourceManager->getLocFromExternalSource("test.zom", 1, 1);

  diagnosticEngine->diagnose<DiagID::InvalidCharacter>(loc);
  ZC_EXPECT(diagnosticEngine->hasErrors());
}

ZC_TEST("DiagnosticTest.TypeCheckerDiagnosticIdsAreStable") {
  ZC_EXPECT(static_cast<uint32_t>(DiagID::DynGenericMethod) == 4331);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::DynSelfReturn) == 4332);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::DynMoveSelf) == 4333);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::DynUnassociatedType) == 4334);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::DynStaticMethod) == 4335);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::DynGatNotAllowed) == 4336);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::DynUnsizedParameter) == 4337);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::DynSuperNotObjectSafe) == 4338);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::TypeCheckerTypeMismatch) == 4410);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::CannotUnifyTypes) == 4411);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::InfiniteType) == 4412);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::CheckerInvalidCast) == 4416);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::CannotInferTypeParameter) == 4420);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::CannotInferNullInitializer) == 4421);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ExplicitTypeArgumentCountMismatch) == 4423);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ConflictingImpl) == 4430);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::OperatorTraitSignatureMismatch) == 4432);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::AmbiguousAssociatedTypeProjection) == 4434);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::NoAssociatedTypeProjection) == 4433);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::CheckerNonExhaustiveMatch) == 4440);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::CheckerUnreachableMatchArm) == 4442);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::CannotMutateImmutableVariable) == 4450);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ErrorPropagateOutsideRaises) == 4460);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ErrorUnwrapNonUnion) == 4461);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::TypeMismatch) >= 3000);
}

ZC_TEST("DiagnosticTest.MultipleDiagnostics") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  auto consumer = zc::heap<ConsolingDiagnosticConsumer>();
  diagnosticEngine->addConsumer(zc::mv(consumer));

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::StringPtr("invalid code").asBytes(), "test.zom");
  auto loc = sourceManager->getLocFromExternalSource("test.zom", 1, 1);

  diagnosticEngine->diagnose<DiagID::InvalidCharacter>(loc);
  diagnosticEngine->diagnose<DiagID::UnterminatedString>(loc);
  ZC_EXPECT(diagnosticEngine->hasErrors());
}

ZC_TEST("DiagnosticTest.DeduplicatesSameIdAtSameLocation") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  auto consumer = zc::heap<CountingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  diagnosticEngine->addConsumer(zc::mv(consumer));

  sourceManager->addMemBufferCopy(zc::StringPtr("x").asBytes(), "test.zom");
  auto loc = sourceManager->getLocFromExternalSource("test.zom", 1, 1);

  diagnosticEngine->diagnose<DiagID::InvalidCharacter>(loc);
  diagnosticEngine->diagnose<DiagID::InvalidCharacter>(loc);

  ZC_EXPECT(consumerPtr->diagnosticCount == 1);
  ZC_EXPECT(diagnosticEngine->errorCount() == 1);
}

ZC_TEST("DiagnosticTest.DefaultErrorBudgetStopsAfterOneHundredErrors") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  auto consumer = zc::heap<CountingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  diagnosticEngine->addConsumer(zc::mv(consumer));

  sourceManager->addMemBufferCopy(
      zc::StringPtr("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                    "xxxxxxxxxxxxxxxxxxxxxxxxxx")
          .asBytes(),
      "test.zom");
  auto loc = sourceManager->getLocFromExternalSource("test.zom", 1, 1);

  for (size_t i = 0; i < 105; ++i) {
    diagnosticEngine->diagnose<DiagID::InvalidCharacter>(
        loc.getAdvancedLoc(static_cast<unsigned>(i)));
  }

  ZC_EXPECT(consumerPtr->diagnosticCount == 100);
  ZC_EXPECT(diagnosticEngine->errorCount() == 100);
}

ZC_TEST("DiagnosticTest.DiagnosticConsumer") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  auto consumer = zc::heap<ConsolingDiagnosticConsumer>();
  diagnosticEngine->addConsumer(zc::mv(consumer));

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::StringPtr("code with error").asBytes(), "test.zom");
  auto loc = sourceManager->getLocFromExternalSource("test.zom", 1, 1);

  diagnosticEngine->diagnose<DiagID::InvalidCharacter>(loc);
  ZC_EXPECT(diagnosticEngine->hasErrors());
}

ZC_TEST("DiagnosticTest.SourceLocationReporting") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  auto consumer = zc::heap<ConsolingDiagnosticConsumer>();
  diagnosticEngine->addConsumer(zc::mv(consumer));

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::StringPtr("line1\nline2\nline3").asBytes(), "test.zom");
  auto loc = sourceManager->getLocFromExternalSource("test.zom", 1, 1);

  diagnosticEngine->diagnose<DiagID::InvalidCharacter>(loc);
  ZC_EXPECT(diagnosticEngine->hasErrors());
}

ZC_TEST("DiagnosticTest.NoErrors") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  auto consumer = zc::heap<ConsolingDiagnosticConsumer>();
  diagnosticEngine->addConsumer(zc::mv(consumer));

  ZC_EXPECT(!diagnosticEngine->hasErrors());
}

}  // namespace diagnostics
}  // namespace compiler
}  // namespace zomlang
