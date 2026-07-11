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
  auto loc = sourceManager->getLocForOffset(bufferId, 0);

  diagnosticEngine->diagnose<DiagID::InvalidCharacter>(loc);
  ZC_EXPECT(diagnosticEngine->hasErrors());
}

ZC_TEST("DiagnosticTest.TypeCheckerDiagnosticIdsAreStable") {
  ZC_EXPECT(static_cast<uint32_t>(DiagID::DynGenericMethod) == 4001);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::DynSelfReturn) == 4002);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::DynMoveSelf) == 4003);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::DynUnassociatedType) == 4004);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::DynStaticMethod) == 4005);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::DynGatNotAllowed) == 4006);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::DynUnsizedParameter) == 4007);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::DynSuperNotObjectSafe) == 4008);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::TypeCheckerTypeMismatch) == 4009);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::CannotUnifyTypes) == 4010);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::InfiniteType) == 4011);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::CheckerInvalidCast) == 4013);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::CannotInferTypeParameter) == 4014);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::CannotInferNullInitializer) == 4015);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ExplicitTypeArgumentCountMismatch) == 4016);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ConflictingImpl) == 4017);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::OperatorTraitSignatureMismatch) == 4019);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::AmbiguousAssociatedTypeProjection) == 4021);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::NoAssociatedTypeProjection) == 4020);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::CheckerNonExhaustiveMatch) == 4022);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::CheckerUnreachableMatchArm) == 4023);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::CannotMutateImmutableVariable) == 4024);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ErrorPropagateOutsideRaises) == 4025);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ErrorUnwrapNonUnion) == 4026);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::UndefinedIdentifier) == 3001);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::SymbolNamespaceMismatch) == 3002);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::RedeclareVariable) == 3003);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::RedeclareParameter) == 3004);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::RedeclareFunction) == 3005);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::RedeclareClass) == 3006);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::RedeclareInterface) == 3007);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::RedeclareEnum) == 3008);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::RedeclareTypeAlias) == 3009);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::DuplicateIdentifier) == 3010);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::CircularImport) == 3011);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ImportModuleNotFound) == 3012);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ImportMemberNotFound) == 3013);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::CircularReexport) == 3014);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ReexportModuleNotFound) == 3015);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ReexportMemberNotFound) == 3016);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::UndeclaredValue) == 4027);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::InvalidBinaryOperands) == 4028);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::InvalidComparisonOperands) == 4029);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::CannotDereferenceType) == 4030);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::PostfixUpdateRequiresNumeric) == 4031);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ErrorPropagateNonUnion) == 4032);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ErrorUnionEmpty) == 4033);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::UnsupportedExplicitTypeArgument) == 4034);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ExplicitTypeArgumentsRequireGenericCallee) == 4035);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::CallArgumentCountMismatch) == 4036);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::MemberNotFound) == 4037);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::IndexRequiresInteger) == 4038);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::TupleIndexRequiresIntegerLiteral) == 4039);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::TupleIndexOutOfBounds) == 4040);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::CannotIndexType) == 4041);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::UnsupportedCastTarget) == 4042);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::RawPointerCastRequiresUnsafe) == 4043);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::InvalidDynUpcast) == 4044);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ConditionMustBeBool) == 4045);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::MissingReturnValue) == 4046);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::UnsupportedStructLiteralTarget) == 4047);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::UnknownStructField) == 4048);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::MissingStructField) == 4049);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ArrayElementTypeMismatch) == 4050);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::MatchGuardMustBeBool) == 4051);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::RecursiveTypeAliasCycle) == 4052);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::UnsupportedTypeExpression) == 4053);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::OrphanImpl) == 4054);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::DynDuplicateAssociatedTypeBinding) == 4055);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::UseAfterMove) == 4056);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ValueMovedHere) == 4057);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::MutableBorrowConflicts) == 4058);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::SharedBorrowConflicts) == 4059);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::BorrowOriginHere) == 4060);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::BorrowDoesNotLiveLongEnough) == 4061);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::BorrowReferentHere) == 4062);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::LinearNotConsumed) == 4063);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::LinearInitializedHere) == 4064);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::LinearConsumedTwice) == 4065);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::LinearFirstConsumedHere) == 4066);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ScopedTaskBorrowEscapes) == 4067);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ScopedTaskReferentHere) == 4068);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::RawPointerBoundaryRequiresUnsafe) == 4069);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::MoveOutOfBorrow) == 4070);
}

ZC_TEST("DiagnosticTest.IrDiagnosticIdsAreStable") {
  ZC_EXPECT(static_cast<uint32_t>(DiagID::IrUnsupportedSourceShape) == 6001);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::IrUnsupportedExpression) == 6002);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::IrUnknownTargetLayout) == 6003);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::IrCrossSourceCallUnsupported) == 6004);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::IrSingleSourceRequired) == 6005);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::PanicUnwindUnsupported) == 6006);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::BinaryEmissionUnavailable) == 6007);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::IrOutputCreationFailed) == 6008);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::IrLoweringInvariantViolation) == 9901);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::IrCheckedInputMissing) == 9902);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::IrDumpInvariantViolation) == 9903);
  ZC_EXPECT(DiagnosticTraits<DiagID::IrLoweringInvariantViolation>::argCount == 3);
  ZC_EXPECT(DiagnosticTraits<DiagID::IrDumpInvariantViolation>::argCount == 7);
}

ZC_TEST("DiagnosticTest.IdentityDiagnosticIdsAreStable") {
  ZC_EXPECT(static_cast<uint32_t>(DiagID::IdentityInvalidHandle) == 9910);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::IdentityForeignContext) == 9911);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::IdentityForeignRegistry) == 9912);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::IdentitySlotOutOfRange) == 9913);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::IdentityAncestorMismatch) == 9914);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::IdentityInvalidSourceRange) == 9915);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::IdentityDuplicateCanonicalKey) == 9916);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::IdentityInvalidClosedValue) == 9917);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::IdentityPostFreezeMutation) == 9918);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::IdentityBrandExhausted) == 9919);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::IdentityDuplicateSingletonStore) == 9920);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::IdentityNonCanonicalEncoding) == 9921);
  ZC_EXPECT(DiagnosticTraits<DiagID::IdentityNonCanonicalEncoding>::argCount == 1);
}

ZC_TEST("DiagnosticTest.MultipleDiagnostics") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  auto consumer = zc::heap<ConsolingDiagnosticConsumer>();
  diagnosticEngine->addConsumer(zc::mv(consumer));

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::StringPtr("invalid code").asBytes(), "test.zom");
  auto loc = sourceManager->getLocForOffset(bufferId, 0);

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

  auto bufferId = sourceManager->addMemBufferCopy(zc::StringPtr("x").asBytes(), "test.zom");
  auto loc = sourceManager->getLocForOffset(bufferId, 0);

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

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::StringPtr("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                    "xxxxxxxxxxxxxxxxxxxxxxxxxx")
          .asBytes(),
      "test.zom");
  auto loc = sourceManager->getLocForOffset(bufferId, 0);

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
  auto loc = sourceManager->getLocForOffset(bufferId, 0);

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
  auto loc = sourceManager->getLocForOffset(bufferId, 0);

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
