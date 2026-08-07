// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/checker/inference-context.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/checker/inference-recovery-context.h"
#include "zomlang/compiler/type/semantic-type-data.h"
#include "zomlang/tests/unittests/compiler/checker/checker-authority-test-fixture.h"

namespace zomlang::compiler::checker::inference {
namespace {

using namespace tests::checker_fixture;

struct InferenceContextPair final {
  zc::Own<InferenceRecoveryContext> recovery;
  zc::Own<InferenceContext> inference;
};

class InferenceFixture final {
public:
  InferenceFixture() : session("class RecoveryOwner {}\n"_zc) {}

  identity::SemanticTypeId primitive(type::semantic::PrimitiveKind kind) {
    auto admitted = session.semanticTypes().canonicalizeClosed(
        type::semantic::TypeData(type::semantic::PrimitiveTypeData{kind}));
    ZC_REQUIRE(admitted.is<type::semantic::CanonicalTypeData>());
    auto interned =
        session.semanticTypes().intern(zc::mv(admitted).get<type::semantic::CanonicalTypeData>());
    ZC_REQUIRE(interned.is<type::SemanticTypeInterned>());
    return interned.get<type::SemanticTypeInterned>().id;
  }

  InferenceContextPair makeContext(InferenceContextBudget budget = {}) {
    auto recovery = makeRecoveryContext();
    auto issued = InferenceContextToken::issue(*recovery);
    ZC_REQUIRE(issued.is<InferenceContextToken>());
    auto token = zc::mv(issued).get<InferenceContextToken>();
    auto created = InferenceContext::create(zc::mv(token), budget);
    ZC_REQUIRE(created.is<zc::Own<InferenceContext>>());
    return InferenceContextPair{zc::mv(recovery), zc::mv(created).get<zc::Own<InferenceContext>>()};
  }

  zc::Own<InferenceRecoveryContext> makeRecoveryContext() {
    auto created = InferenceRecoveryContext::create(session.identityAuthority(), session.brands(),
                                                    session.source(),
                                                    InferenceOwner::callableBody(session.owner()));
    ZC_REQUIRE(created.is<zc::Own<InferenceRecoveryContext>>());
    return zc::mv(created).get<zc::Own<InferenceRecoveryContext>>();
  }

  identity::SourceSpan sourceSpan(uint64_t start, uint64_t end) const {
    return session.span(start, end);
  }

  static checked::CheckerEmitterOrdinal emitterOrdinal(uint32_t site) {
    return checked::CheckerEmitterOrdinal{
        static_cast<uint8_t>(checked::CheckerDiagnosticStage::Body), 0, site, 0};
  }

  CheckerAuthoritySession session;
};

TypeVarId requireVariable(TypeVariableIssueResult&& result) {
  ZC_REQUIRE(result.is<TypeVarId>());
  return result.get<TypeVarId>();
}

void requireAccepted(InferenceConstraintResult&& result) {
  ZC_REQUIRE(result.is<InferenceConstraintAccepted>());
}

void requireSolved(InferenceSolveResult&& result) {
  ZC_REQUIRE(result.is<InferenceSolveComplete>());
}

void requireClosed(InferenceContextFinishResult&& result) {
  ZC_REQUIRE(result.is<InferenceContextClosed>());
}

void requireRecoveryClosed(InferenceRecoveryFinishResult&& result) {
  ZC_REQUIRE(result.is<InferenceRecoverySolved>() || result.is<InferenceRecoveryRecovered>());
}

checked::TypeErrorId requireRecovery(TypeErrorIssueResult&& result) {
  ZC_REQUIRE(result.is<checked::TypeErrorId>());
  return result.get<checked::TypeErrorId>();
}

}  // namespace

static_assert(__is_constructible(InferenceContextToken, InferenceContextToken&&));
static_assert(__is_assignable(InferenceContextToken&, InferenceContextToken&&));
static_assert(!__is_constructible(InferenceContextToken, const InferenceContextToken&));
static_assert(!__is_assignable(InferenceContextToken&, const InferenceContextToken&));
static_assert(__is_constructible(InferenceContext, InferenceContext&&));
static_assert(!__is_constructible(InferenceContext, const InferenceContext&));

ZC_TEST("InferenceContext consumes a move-only construction token exactly once") {
  InferenceFixture fixture;
  auto recovery = fixture.makeRecoveryContext();
  auto issued = InferenceContextToken::issue(*recovery);
  ZC_REQUIRE(issued.is<InferenceContextToken>());
  auto token = zc::mv(issued).get<InferenceContextToken>();
  ZC_EXPECT(token.isValid());

  auto first = InferenceContext::create(zc::mv(token));
  ZC_REQUIRE(first.is<zc::Own<InferenceContext>>());
  ZC_EXPECT(!token.isValid());
  auto second = InferenceContext::create(zc::mv(token));
  ZC_REQUIRE(second.is<InferenceContextRejected>());
  ZC_EXPECT(second.get<InferenceContextRejected>().invariant ==
            InferenceContextInvariant::TokenConsumed);
  auto context = zc::mv(first).get<zc::Own<InferenceContext>>();
  requireClosed(context->finish());
  requireRecoveryClosed(recovery->finish());
}

ZC_TEST("InferenceContext selects the smallest variable slot under ordinal FIFO equality") {
  InferenceFixture fixture;
  auto context = fixture.makeContext();
  const auto first = requireVariable(context.inference->issueVariable({1, 0}));
  const auto second = requireVariable(context.inference->issueVariable({1, 1}));
  const auto third = requireVariable(context.inference->issueVariable({2, 0}));
  const auto i32 = fixture.primitive(type::semantic::PrimitiveKind::I32);
  ZC_EXPECT(first.belongsTo(context.recovery->issuer()));

  requireAccepted(context.inference->addEquality(20, InferenceType::variable(third),
                                                 InferenceType::variable(second)));
  requireAccepted(context.inference->addEquality(10, InferenceType::variable(second),
                                                 InferenceType::variable(first)));
  requireAccepted(context.inference->addEquality(30, InferenceType::variable(first),
                                                 InferenceType::known(i32)));
  requireSolved(context.inference->solve());

  auto representative = context.inference->representative(third);
  ZC_REQUIRE(representative.is<TypeVarId>());
  ZC_EXPECT(representative.get<TypeVarId>() == first);
  auto materialized = context.inference->materialize(InferenceType::variable(third));
  ZC_REQUIRE(materialized.is<MaterializedInferenceType>());
  ZC_EXPECT(materialized.get<MaterializedInferenceType>().type == i32);
  requireClosed(context.inference->finish());
  requireRecoveryClosed(context.recovery->finish());
}

ZC_TEST("InferenceContext reports equality failure at ordinal FIFO position") {
  InferenceFixture fixture;
  auto context = fixture.makeContext();
  const auto variable = requireVariable(context.inference->issueVariable({1, 0}));
  const auto i32 = fixture.primitive(type::semantic::PrimitiveKind::I32);
  const auto i64 = fixture.primitive(type::semantic::PrimitiveKind::I64);
  requireAccepted(context.inference->addEquality(20, InferenceType::variable(variable),
                                                 InferenceType::known(i32)));
  requireAccepted(context.inference->addEquality(10, InferenceType::variable(variable),
                                                 InferenceType::known(i64)));
  auto solved = context.inference->solve();
  ZC_REQUIRE(solved.is<InferenceContextRejected>());
  ZC_EXPECT(solved.get<InferenceContextRejected>().invariant ==
            InferenceContextInvariant::SolverStateInvalid);
  ZC_EXPECT(solved.get<InferenceContextRejected>().ordinal == 20);
  auto finished = context.inference->finish();
  ZC_REQUIRE(finished.is<InferenceContextRejected>());
  ZC_EXPECT(finished.get<InferenceContextRejected>().ordinal == 20);
  requireRecoveryClosed(context.recovery->finish());
}

ZC_TEST("InferenceContext preserves recovery during variable materialization") {
  InferenceFixture fixture;
  auto context = fixture.makeContext();
  const auto error = requireRecovery(
      context.recovery->issueRoot(InferenceFixture::emitterOrdinal(1), ast::NodeId(1),
                                  fixture.sourceSpan(0, 1), RecoveryClass::FailedInference));
  const auto variable = requireVariable(context.inference->issueVariable({1, 0}));
  requireAccepted(context.inference->addEquality(1, InferenceType::variable(variable),
                                                 InferenceType::recovery(error)));
  requireSolved(context.inference->solve());

  auto materialized = context.inference->materialize(InferenceType::variable(variable));
  ZC_REQUIRE(materialized.is<SourceRejectedInferenceType>());
  ZC_EXPECT(materialized.get<SourceRejectedInferenceType>().error == error);
  requireClosed(context.inference->finish());
  auto recoveryFinished = context.recovery->finish();
  ZC_REQUIRE(recoveryFinished.is<InferenceRecoveryRecovered>());
}

ZC_TEST("InferenceContext finish rejects unresolved variables and pending work") {
  InferenceFixture fixture;
  auto unresolved = fixture.makeContext();
  requireVariable(unresolved.inference->issueVariable({1, 0}));
  auto unresolvedFinish = unresolved.inference->finish();
  ZC_REQUIRE(unresolvedFinish.is<InferenceContextRejected>());
  ZC_EXPECT(unresolvedFinish.get<InferenceContextRejected>().invariant ==
            InferenceContextInvariant::UnresolvedVariable);
  requireRecoveryClosed(unresolved.recovery->finish());

  auto pending = fixture.makeContext();
  const auto variable = requireVariable(pending.inference->issueVariable({1, 0}));
  const auto i32 = fixture.primitive(type::semantic::PrimitiveKind::I32);
  requireAccepted(pending.inference->addEquality(1, InferenceType::variable(variable),
                                                 InferenceType::known(i32)));
  auto pendingFinish = pending.inference->finish();
  ZC_REQUIRE(pendingFinish.is<InferenceContextRejected>());
  ZC_EXPECT(pendingFinish.get<InferenceContextRejected>().invariant ==
            InferenceContextInvariant::PendingWork);
  requireRecoveryClosed(pending.recovery->finish());
}

ZC_TEST("InferenceContext rejects foreign variables without crossing issuer lifetimes") {
  InferenceFixture fixture;
  auto first = fixture.makeContext();
  auto second = fixture.makeContext();
  const auto foreign = requireVariable(first.inference->issueVariable({1, 0}));
  const auto local = requireVariable(second.inference->issueVariable({1, 0}));
  auto result = second.inference->addEquality(1, InferenceType::variable(local),
                                              InferenceType::variable(foreign));
  ZC_REQUIRE(result.is<InferenceContextRejected>());
  ZC_EXPECT(result.get<InferenceContextRejected>().invariant ==
            InferenceContextInvariant::ForeignVariable);
  auto secondFinish = second.inference->finish();
  ZC_REQUIRE(secondFinish.is<InferenceContextRejected>());
  ZC_EXPECT(secondFinish.get<InferenceContextRejected>().invariant ==
            InferenceContextInvariant::ForeignVariable);
  requireRecoveryClosed(second.recovery->finish());

  const auto i32 = fixture.primitive(type::semantic::PrimitiveKind::I32);
  requireAccepted(
      first.inference->addEquality(1, InferenceType::variable(foreign), InferenceType::known(i32)));
  requireSolved(first.inference->solve());
  requireClosed(first.inference->finish());
  requireRecoveryClosed(first.recovery->finish());
}

ZC_TEST("InferenceContext destruction asserts when finish was omitted") {
  InferenceFixture fixture;
  auto context = fixture.makeContext();
  ZC_EXPECT_THROW_MESSAGE("InferenceContext destroyed before finish()",
                          [&]() { context.inference = nullptr; }());
  auto recoveryFinished = context.recovery->finish();
  ZC_REQUIRE(recoveryFinished.is<InferenceRecoveryRejected>());
  ZC_EXPECT(recoveryFinished.get<InferenceRecoveryRejected>().invariant ==
            InferenceRecoveryInvariant::UnclosedInferenceContext);
}

}  // namespace zomlang::compiler::checker::inference
