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

#include "compiler/diagnostics/core/diagnostic-info.h"
#include "zc/core/string.h"
#include "zc/ztest/test.h"

namespace zomlang {
namespace compiler {
namespace diagnostics {

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
  ZC_EXPECT(static_cast<uint32_t>(DiagID::CannotCallNonFunction) == 4012);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::CheckerInvalidCast) == 4013);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::CannotInferTypeParameter) == 4014);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::CannotInferNullInitializer) == 4015);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ExplicitTypeArgumentCountMismatch) == 4016);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ConflictingImpl) == 4017);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::CheckerTraitNotImplemented) == 4018);
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
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ImportModuleAmbiguous) == 3023);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ReexportModuleAmbiguous) == 3024);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ModuleDeclarationNameMismatch) == 3026);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::BreakTargetNotFound) == 3020);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ContinueTargetNotFound) == 3021);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::InvalidBinaryOperands) == 4028);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::InvalidComparisonOperands) == 4029);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::CannotDereferenceType) == 4030);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::PostfixUpdateRequiresNumeric) == 4031);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ErrorPropagateNonUnion) == 4032);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ErrorUnionEmpty) == 4033);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ExplicitTypeArgumentsRequireGenericCallee) == 4035);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::CallArgumentCountMismatch) == 4036);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::MemberNotFound) == 4037);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::IndexRequiresInteger) == 4038);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::TupleIndexRequiresIntegerLiteral) == 4039);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::TupleIndexOutOfBounds) == 4040);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::CannotIndexType) == 4041);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::InvalidDynUpcast) == 4044);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ConditionMustBeBool) == 4045);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::MissingReturnValue) == 4046);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::AggregateLiteralTargetRequired) == 4047);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::UnknownStructField) == 4048);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::MissingStructField) == 4049);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ArrayElementTypeMismatch) == 4050);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::MatchGuardMustBeBool) == 4051);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::RecursiveTypeAliasCycle) == 4052);
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
  ZC_EXPECT(static_cast<uint32_t>(DiagID::RawPointerBoundaryRequiresUnsafe) == 4069);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::MoveOutOfBorrow) == 4070);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::PreviousImplHere) == 4071);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ObjectSafetyCauseHere) == 4072);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::AssociatedTypeCandidateHere) == 4073);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::PreviousAssociatedBindingHere) == 4074);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::AliasCycleMemberHere) == 4075);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::OperatorMethodDeclaredHere) == 4076);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::BodyLiteralOutOfRange) == 4077);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ConstantValueOutOfRange) == 4078);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ConstantExpressionNotAllowed) == 4079);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ConstantDependencyCycle) == 4080);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ConstantArithmeticFailure) == 4081);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::MarkerInterfaceRequiresBodylessImpl) == 4088);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::BehaviorInterfaceRequiresImplBody) == 4089);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::GenericMarkerInterfaceNotAllowed) == 4090);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::PositiveMarkerImplRequiresUnsafe) == 4091);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ExplicitImplConflictsWithBuiltinMarker) == 4092);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::UninitializedPlaceUse) == 4093);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::PlaceBecameUnavailableHere) == 4094);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ConcurrencySemanticsUnavailable) == 4095);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ControlFlowSemanticsUnavailable) == 4096);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::VoidReturnSemanticsUnavailable) == 4097);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ExpressionStatementSemanticsUnavailable) == 4098);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::FunctionBodySemanticsUnavailable) == 4099);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::BinaryOperatorSemanticsUnavailable) == 4103);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ModuleInitializerSemanticsUnavailable) == 4104);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ModuleStatementSemanticsUnavailable) == 4105);
}

ZC_TEST("DiagnosticTest.ReceiverParserDiagnosticIdsAreStable") {
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ReceiverMustBeFirstParameter) == 2093);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ReceiverDefaultNotAllowed) == 2094);
  ZC_EXPECT(static_cast<uint32_t>(DiagID::ReceiverNotAllowedHere) == 2095);
}

ZC_TEST("DiagnosticTest.ActiveLexerAndParserDiagnosticIdsAreStable") {
  struct Expected final {
    DiagID id;
    uint32_t code;
  };
  const Expected expected[] = {
      {DiagID::ThisCharCannotBeEscapedInARegularExpression, 2001},
      {DiagID::UnterminatedTemplateLiteral, 2004},
      {DiagID::AsteriskSlashExpected, 2006},
      {DiagID::UnexpectedEndOfText, 2007},
      {DiagID::InvalidOptionalChainFromNewExpression, 2009},
      {DiagID::OctalEscapeSequencesAndBackreferencesNotAllowed, 2028},
      {DiagID::DecimalEscapeSequencesAndBackreferencesNotAllowed, 2029},
      {DiagID::UnterminatedUnicodeEscapeSequence, 2039},
      {DiagID::DecimalsWithLeadingZerosAreNotAllowed, 2041},
      {DiagID::DigitExpected, 2042},
      {DiagID::ABigIntLiteralCannotUseExponentialNotation, 2043},
      {DiagID::ABigIntLiteralMustBeAnInteger, 2044},
      {DiagID::TypeParameterDeclarationExpected, 2070},
  };
  for (const auto& entry : expected) {
    const auto info = getDiagnosticInfo(entry.id);
    ZC_EXPECT(static_cast<uint32_t>(entry.id) == entry.code);
    ZC_EXPECT(info.id == entry.id);
    ZC_EXPECT(info.severity == DiagSeverity::kError);
  }
}

ZC_TEST("DiagnosticTest.ActiveBinderAndBorrowDiagnosticIdsAreStable") {
  struct Expected final {
    DiagID id;
    uint32_t code;
  };
  const Expected expected[] = {
      {DiagID::ContextualSelfOutsideType, 3025},
      {DiagID::BorrowOutputRegionAmbiguous, 4082},
      {DiagID::BorrowOutputRegionUnexpressible, 4083},
      {DiagID::BorrowExternContractUnverified, 4084},
  };
  for (const auto& entry : expected) {
    const auto info = getDiagnosticInfo(entry.id);
    ZC_EXPECT(static_cast<uint32_t>(entry.id) == entry.code);
    ZC_EXPECT(info.id == entry.id);
    ZC_EXPECT(info.severity == DiagSeverity::kError);
  }
}

ZC_TEST("DiagnosticTest.ActivePackageDiagnosticContractsAreStable") {
  struct Expected final {
    DiagID id;
    uint32_t code;
    DiagSeverity severity;
    uint32_t argCount;
  };
  const Expected expected[] = {
      {DiagID::PackageManifestInvalid, 7001, DiagSeverity::kError, 1},
      {DiagID::PreviousWorkspacePackageHere, 7093, DiagSeverity::kNote, 0},
  };
  for (const auto& entry : expected) {
    const auto info = getDiagnosticInfo(entry.id);
    ZC_EXPECT(static_cast<uint32_t>(entry.id) == entry.code);
    ZC_EXPECT(info.id == entry.id);
    ZC_EXPECT(info.severity == entry.severity);
    ZC_EXPECT(info.argCount == entry.argCount);
  }
}

ZC_TEST("DiagnosticTest.ControlTransferDiagnosticContractsAreStable") {
  const auto breakInfo = getDiagnosticInfo(DiagID::BreakTargetNotFound);
  ZC_EXPECT(breakInfo.id == DiagID::BreakTargetNotFound);
  ZC_EXPECT(breakInfo.severity == DiagSeverity::kError);
  ZC_EXPECT(breakInfo.message == "break requires an enclosing loop, match, or label"_zc);
  ZC_EXPECT(breakInfo.argCount == 0);

  const auto continueInfo = getDiagnosticInfo(DiagID::ContinueTargetNotFound);
  ZC_EXPECT(continueInfo.id == DiagID::ContinueTargetNotFound);
  ZC_EXPECT(continueInfo.severity == DiagSeverity::kError);
  ZC_EXPECT(continueInfo.message == "continue requires an enclosing loop or loop label"_zc);
  ZC_EXPECT(continueInfo.argCount == 0);
}

ZC_TEST("DiagnosticTest.ModuleGraphAmbiguityDiagnosticContractsAreStable") {
  const auto importInfo = getDiagnosticInfo(DiagID::ImportModuleAmbiguous);
  ZC_EXPECT(importInfo.id == DiagID::ImportModuleAmbiguous);
  ZC_EXPECT(importInfo.severity == DiagSeverity::kError);
  ZC_EXPECT(importInfo.message == "Import path resolves to multiple modules"_zc);
  ZC_EXPECT(importInfo.argCount == 0);

  const auto reexportInfo = getDiagnosticInfo(DiagID::ReexportModuleAmbiguous);
  ZC_EXPECT(reexportInfo.id == DiagID::ReexportModuleAmbiguous);
  ZC_EXPECT(reexportInfo.severity == DiagSeverity::kError);
  ZC_EXPECT(reexportInfo.message == "Re-export path resolves to multiple modules"_zc);
  ZC_EXPECT(reexportInfo.argCount == 0);
}

ZC_TEST("DiagnosticTest.BackendDiagnosticIdsAreStable") {
  ZC_EXPECT(static_cast<uint32_t>(DiagID::TargetCapabilityUnavailable) == 6009);
}

}  // namespace diagnostics
}  // namespace compiler
}  // namespace zomlang
