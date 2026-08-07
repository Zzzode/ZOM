// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/checker/inference-recovery-context.h"

#include "zc/ztest/test.h"
#include "zomlang/tests/unittests/compiler/checker/checker-authority-test-fixture.h"

namespace zomlang::compiler::checker::inference {
namespace {

using namespace tests::checker_fixture;

bool sameBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index] != right[index]) { return false; }
  }
  return true;
}

class RecoveryFixture final {
public:
  RecoveryFixture() : session("class RecoveryOwner {}\n"_zc), context(session.semanticContext()) {}

  identity::SourceSpan span(uint64_t start, uint64_t end) const { return session.span(start, end); }

  zc::Own<InferenceRecoveryContext> makeContext(InferenceRecoveryIssueBudget budget = {}) {
    auto created = InferenceRecoveryContext::create(
        session.identityAuthority(), session.brands(), session.source(),
        InferenceOwner::callableBody(session.owner()), budget);
    ZC_REQUIRE(created.is<zc::Own<InferenceRecoveryContext>>());
    return zc::mv(created).get<zc::Own<InferenceRecoveryContext>>();
  }

  zc::Own<InferenceRecoveryContext> makeInitializerContext() {
    auto created = InferenceRecoveryContext::create(
        session.identityAuthority(), session.brands(), session.source(),
        InferenceOwner::initializer(session.owner()), {});
    ZC_REQUIRE(created.is<zc::Own<InferenceRecoveryContext>>());
    return zc::mv(created).get<zc::Own<InferenceRecoveryContext>>();
  }

  zc::Own<InferenceRecoveryContext> makeSignatureGroupContext() {
    zc::Vector<identity::DefId> members;
    members.add(session.owner());
    auto created = InferenceRecoveryContext::create(
        session.identityAuthority(), session.brands(), session.source(),
        InferenceOwner::signatureGroup(session.module(), zc::mv(members)), {});
    ZC_REQUIRE(created.is<zc::Own<InferenceRecoveryContext>>());
    return zc::mv(created).get<zc::Own<InferenceRecoveryContext>>();
  }

  static checked::CheckerEmitterOrdinal ordinal(uint32_t site) {
    return checked::CheckerEmitterOrdinal{
        static_cast<uint8_t>(checked::CheckerDiagnosticStage::Body), 0, site, 0};
  }

  CheckerAuthoritySession session;
  identity::SemanticContextBrand context;
};

checked::TypeErrorId requireIssued(TypeErrorIssueResult&& result) {
  ZC_REQUIRE(result.is<checked::TypeErrorId>());
  return result.get<checked::TypeErrorId>();
}

checked::FrozenRecoveryLedger requireRecovered(InferenceRecoveryFinishResult&& result) {
  ZC_REQUIRE(result.is<InferenceRecoveryRecovered>());
  return zc::mv(result).get<InferenceRecoveryRecovered>().ledger;
}

}  // namespace

ZC_TEST("InferenceRecoveryContext issues unique branded roots and freezes one ledger") {
  RecoveryFixture fixture;
  auto context = fixture.makeContext();
  const auto first =
      requireIssued(context->issueRoot(RecoveryFixture::ordinal(1), ast::NodeId(1),
                                       fixture.span(0, 1), RecoveryClass::TypeMismatch));
  const auto second =
      requireIssued(context->issueRoot(RecoveryFixture::ordinal(2), ast::NodeId(2),
                                       fixture.span(1, 2), RecoveryClass::InvalidOperation));
  ZC_EXPECT(first != second);
  ZC_EXPECT(first.belongsTo(context->issuer()));
  ZC_EXPECT(second.belongsTo(context->issuer()));
  auto reused = context->reuse(first);
  ZC_REQUIRE(reused.is<checked::TypeErrorId>());
  ZC_EXPECT(reused.get<checked::TypeErrorId>() == first);
  auto ledger = requireRecovered(context->finish());
  ZC_EXPECT(ledger.semanticContext() == fixture.context);
  ZC_EXPECT(ledger.issuer() == context->issuer());
  ZC_EXPECT(ledger.contains(first));
  ZC_EXPECT(ledger.contains(second));
  ZC_REQUIRE(ledger.idAt(0) != zc::none);
  ZC_REQUIRE(ledger.idAt(1) != zc::none);
  ZC_IF_SOME(value, ledger.idAt(0)) { ZC_EXPECT(value == first); }
  ZC_IF_SOME(value, ledger.idAt(1)) { ZC_EXPECT(value == second); }

  auto initializer = fixture.makeInitializerContext();
  ZC_EXPECT(initializer->finish().is<InferenceRecoverySolved>());
  auto signatureGroup = fixture.makeSignatureGroupContext();
  ZC_EXPECT(signatureGroup->finish().is<InferenceRecoverySolved>());
}

ZC_TEST("InferenceRecoveryContext canonicalizes joins independently of brands and input order") {
  RecoveryFixture firstFixture;
  auto firstContext = firstFixture.makeContext();
  const auto firstA =
      requireIssued(firstContext->issueRoot(RecoveryFixture::ordinal(1), ast::NodeId(1),
                                            firstFixture.span(0, 1), RecoveryClass::TypeMismatch));
  const auto firstB = requireIssued(firstContext->issueRoot(RecoveryFixture::ordinal(2),
                                                            ast::NodeId(2), firstFixture.span(1, 2),
                                                            RecoveryClass::FailedInference));
  const checked::TypeErrorId firstInputs[] = {firstB, firstA};
  checked::CheckedNodeKey firstParent{1, 3, firstFixture.span(2, 3)};
  auto firstSelected =
      requireIssued(firstContext->join(ast::NodeId(3), firstParent, zc::arrayPtr(firstInputs)));
  ZC_EXPECT(firstSelected == firstA);
  checked::CheckedNodeKey firstEarlierParent{1, 4, firstFixture.span(0, 1)};
  ZC_EXPECT(requireIssued(firstContext->join(ast::NodeId(4), firstEarlierParent,
                                             zc::arrayPtr(firstInputs))) == firstA);
  auto firstLedger = requireRecovered(firstContext->finish());

  RecoveryFixture secondFixture;
  auto secondContext = secondFixture.makeContext();
  const auto secondA = requireIssued(
      secondContext->issueRoot(RecoveryFixture::ordinal(1), ast::NodeId(1),
                               secondFixture.span(0, 1), RecoveryClass::TypeMismatch));
  const auto secondB = requireIssued(
      secondContext->issueRoot(RecoveryFixture::ordinal(2), ast::NodeId(2),
                               secondFixture.span(1, 2), RecoveryClass::FailedInference));
  const checked::TypeErrorId secondInputs[] = {secondA, secondB};
  checked::CheckedNodeKey secondParent{1, 3, secondFixture.span(2, 3)};
  checked::CheckedNodeKey secondEarlierParent{1, 4, secondFixture.span(0, 1)};
  ZC_EXPECT(requireIssued(secondContext->join(ast::NodeId(4), secondEarlierParent,
                                              zc::arrayPtr(secondInputs))) == secondA);
  auto secondSelected =
      requireIssued(secondContext->join(ast::NodeId(3), secondParent, zc::arrayPtr(secondInputs)));
  ZC_EXPECT(secondSelected == secondA);
  auto secondLedger = requireRecovered(secondContext->finish());

  ZC_EXPECT(firstLedger.issuer() != secondLedger.issuer());
  ZC_EXPECT(sameBytes(firstLedger.canonicalRecord(), secondLedger.canonicalRecord()));
}

ZC_TEST("InferenceRecoveryContext rejects foreign recovery and issuance exhaustion") {
  RecoveryFixture fixture;
  auto firstContext = fixture.makeContext();
  auto secondContext = fixture.makeContext({1});
  const auto foreign =
      requireIssued(firstContext->issueRoot(RecoveryFixture::ordinal(1), ast::NodeId(1),
                                            fixture.span(0, 1), RecoveryClass::TypeMismatch));
  auto foreignReuse = secondContext->reuse(foreign);
  ZC_REQUIRE(foreignReuse.is<InferenceRecoveryRejected>());
  ZC_EXPECT(foreignReuse.get<InferenceRecoveryRejected>().invariant ==
            InferenceRecoveryInvariant::ForeignRecovery);

  auto boundedContext = fixture.makeContext({1});
  requireIssued(boundedContext->issueRoot(RecoveryFixture::ordinal(1), ast::NodeId(1),
                                          fixture.span(0, 1), RecoveryClass::TypeMismatch));
  auto exhausted = boundedContext->issueRoot(RecoveryFixture::ordinal(2), ast::NodeId(2),
                                             fixture.span(1, 2), RecoveryClass::InvalidOperation);
  ZC_REQUIRE(exhausted.is<InferenceRecoveryRejected>());
  ZC_EXPECT(exhausted.get<InferenceRecoveryRejected>().invariant ==
            InferenceRecoveryInvariant::ErrorIdSpaceExhausted);

  requireRecovered(firstContext->finish());
}

ZC_TEST("InferenceRecoveryContext rejects duplicate root ordinals without reusing a slot") {
  RecoveryFixture fixture;
  auto context = fixture.makeContext();
  requireIssued(context->issueRoot(RecoveryFixture::ordinal(1), ast::NodeId(1), fixture.span(0, 1),
                                   RecoveryClass::TypeMismatch));
  auto duplicate = context->issueRoot(RecoveryFixture::ordinal(1), ast::NodeId(2),
                                      fixture.span(1, 2), RecoveryClass::FailedInference);
  ZC_REQUIRE(duplicate.is<InferenceRecoveryRejected>());
  ZC_EXPECT(duplicate.get<InferenceRecoveryRejected>().invariant ==
            InferenceRecoveryInvariant::DuplicateRootOrdinal);
}

ZC_TEST("InferenceRecoveryContext closes exactly once and rejects every post-freeze operation") {
  RecoveryFixture fixture;
  auto context = fixture.makeContext();
  const auto recovery =
      requireIssued(context->issueRoot(RecoveryFixture::ordinal(1), ast::NodeId(1),
                                       fixture.span(0, 1), RecoveryClass::TypeMismatch));
  requireRecovered(context->finish());

  auto issueAfterFinish = context->issueRoot(RecoveryFixture::ordinal(2), ast::NodeId(2),
                                             fixture.span(1, 2), RecoveryClass::InvalidOperation);
  ZC_REQUIRE(issueAfterFinish.is<InferenceRecoveryRejected>());
  ZC_EXPECT(issueAfterFinish.get<InferenceRecoveryRejected>().invariant ==
            InferenceRecoveryInvariant::ContextClosed);
  auto reuseAfterFinish = context->reuse(recovery);
  ZC_REQUIRE(reuseAfterFinish.is<InferenceRecoveryRejected>());
  ZC_EXPECT(reuseAfterFinish.get<InferenceRecoveryRejected>().invariant ==
            InferenceRecoveryInvariant::ContextClosed);
  auto secondFinish = context->finish();
  ZC_REQUIRE(secondFinish.is<InferenceRecoveryRejected>());
  ZC_EXPECT(secondFinish.get<InferenceRecoveryRejected>().invariant ==
            InferenceRecoveryInvariant::ContextClosed);
}

ZC_TEST("InferenceRecoveryContext closes an empty context without fabricating a ledger") {
  RecoveryFixture fixture;
  auto context = fixture.makeContext();
  auto finished = context->finish();
  ZC_REQUIRE(finished.is<InferenceRecoverySolved>());
}

}  // namespace zomlang::compiler::checker::inference
