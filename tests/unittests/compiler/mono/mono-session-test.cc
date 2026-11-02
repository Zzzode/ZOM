// Copyright (c) 2026 Zode.Z. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/lir/mir-to-lir.h"
#include "compiler/lir/verify/translation-validator.h"
#include "compiler/mir/built-mir.h"
#include "tests/unittests/compiler/mir/mir-pipeline-fixture.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::mono {
namespace {

constexpr char kIdentityProgram[] =
    "fn identity<T>(value: T) -> T {\n"
    "    return value;\n"
    "}\n"
    "fn entry() -> i32 {\n"
    "    return identity(42);\n"
    "}\n";

// Initializer position: the monomorphized instance call writes the local that
// is then returned (the LIR trailing-count-one initializer shape).
constexpr char kIdentityInitializerProgram[] =
    "fn identity<T>(value: T) -> T {\n"
    "    return value;\n"
    "}\n"
    "fn entry() -> i32 {\n"
    "    let y = identity(42);\n"
    "    return y;\n"
    "}\n";

}  // namespace

ZC_TEST("MonoSession.publishesEmptyVerifiedInstanceSetForModuleWithNoInstanceEdges") {
  mir::MirPipelineFixture fixture("let value = 0;"_zc);
  const auto monoSets = fixture.sessionRef().getVerifiedMonoInstanceSets();
  ZC_REQUIRE(monoSets.size() == 1);
  ZC_EXPECT(monoSets[0].instances().size() == 0);
  // The empty-set revision is reproducible across independent checkSources
  // builds.
  const auto firstRevision = monoSets[0].revision().digest();
  ZC_EXPECT(firstRevision.bytes().size() == 32);

  mir::MirPipelineFixture rebuilt("let value = 0;"_zc);
  const auto rebuiltSets = rebuilt.sessionRef().getVerifiedMonoInstanceSets();
  ZC_REQUIRE(rebuiltSets.size() == 1);
  ZC_EXPECT(rebuiltSets[0].revision().digest() == firstRevision);
}

ZC_TEST("MonoSession.publishesOneInstanceForAdmittedGenericIdentityCall") {
  mir::MirPipelineFixture fixture(kIdentityProgram);
  const auto monoSets = fixture.sessionRef().getVerifiedMonoInstanceSets();
  ZC_REQUIRE(monoSets.size() == 1);
  ZC_EXPECT(monoSets[0].instances().size() == 1);
  const auto& instance = monoSets[0].instances()[0];
  ZC_EXPECT(instance.identity.isValid());
  ZC_EXPECT(instance.canonicalBody.size() > 0);
  ZC_EXPECT(instance.identityFrame.size() > 0);
  ZC_EXPECT(instance.body.blocks.size() >= 1);

  // Independent rebuilds reproduce the Built MIR, overlay, facts, and instance
  // set revisions despite the new Instance edge bytes (RFC 0056 determinism).
  const auto checkedModules = fixture.sessionRef().getOwnershipCheckedMirModules();
  ZC_REQUIRE(checkedModules.size() == 1);
  const auto builtDigest = checkedModules[0].builtRevision().digest();
  const auto overlayDigest = checkedModules[0].eventOverlayRevision().digest();
  const auto factsDigest = checkedModules[0].factsRevision().digest();
  const auto monoDigest = monoSets[0].revision().digest();

  mir::MirPipelineFixture rebuilt(kIdentityProgram);
  const auto rebuiltChecked = rebuilt.sessionRef().getOwnershipCheckedMirModules();
  const auto rebuiltMono = rebuilt.sessionRef().getVerifiedMonoInstanceSets();
  ZC_REQUIRE(rebuiltChecked.size() == 1 && rebuiltMono.size() == 1);
  ZC_EXPECT(rebuiltChecked[0].builtRevision().digest() == builtDigest);
  ZC_EXPECT(rebuiltChecked[0].eventOverlayRevision().digest() == overlayDigest);
  ZC_EXPECT(rebuiltChecked[0].factsRevision().digest() == factsDigest);
  ZC_EXPECT(rebuiltMono[0].revision().digest() == monoDigest);
}

namespace {

// Exercises the full instance-call lowering + presented-identity validation
// for one program: resolves the source caller with the entry Instance edge,
// pairs it with the verified instance body, lowers the monomorphic module, and
// proves it validates with explicit identities but fails closed Source-only.
void expectInstanceCallLowersAndValidates(zc::StringPtr program) {
  mir::MirPipelineFixture fixture(program);
  const auto& session = fixture.sessionRef();
  const auto types = session.getSemanticTypeStore();
  ZC_REQUIRE(types != zc::none);

  const auto functions = fixture.builtMir().functions();
  const mir::MirFunction* caller = nullptr;
  zc::Maybe<ir::InstanceId> calleeInstance;
  for (const auto& function : functions) {
    if (function.blocks.size() == 0 ||
        function.blocks[0].terminator.kind() != mir::MirTerminatorKind::Call) {
      continue;
    }
    const auto& edge = function.blocks[0].terminator.callValue();
    if (edge.callee.kind() == mir::MirCallableIdentityKind::Instance) {
      caller = &function;
      calleeInstance = edge.callee.instanceValue();
      break;
    }
  }
  ZC_REQUIRE(caller != nullptr);
  ZC_REQUIRE(calleeInstance != zc::none);

  const auto monoSets = session.getVerifiedMonoInstanceSets();
  ZC_REQUIRE(monoSets.size() == 1 && monoSets[0].instances().size() == 1);
  const auto& instance = monoSets[0].instances()[0];
  ZC_EXPECT(instance.identity == ZC_ASSERT_NONNULL(calleeInstance));

  auto module = lir::MirToLirLowering::lowerMonomorphicCallModuleWithArgument(
      *caller, instance.body, instance.identity, instance.identityFrame.asPtr(),
      ZC_ASSERT_NONNULL(types));
  ZC_REQUIRE(module != zc::none);
  const auto lirModule = zc::mv(ZC_ASSERT_NONNULL(module));
  ZC_EXPECT(lirModule.functions().size() == 2);
  ZC_EXPECT(lirModule.functions()[1].symbolName().startsWith("zom.mono."));

  // Present [caller, instance body] with [Source, Instance] identities in LIR
  // order; the Instance call edge must validate against the instance body.
  auto presented = zc::heapArray<const mir::MirFunction*>(2);
  presented[0] = caller;
  presented[1] = &instance.body;
  zc::Vector<mir::MirCallableIdentity> identities;
  identities.add(mir::MirCallableIdentity::source(caller->owner));
  identities.add(mir::MirCallableIdentity::instance(instance.identity));
  ZC_EXPECT(lir::TranslationValidator::validate(presented.asPtr(), identities.asPtr(), lirModule,
                                                ZC_ASSERT_NONNULL(types)) == zc::none);

  // Presenting Source-only identities for the same module must fail closed:
  // the Instance edge can never match a Source owner.
  ZC_EXPECT(lir::TranslationValidator::validate(presented.asPtr(), lirModule,
                                                ZC_ASSERT_NONNULL(types)) != zc::none);
}

}  // namespace

ZC_TEST("MonoSession.instanceCallLowersAndValidatesUnderInstanceIdentity") {
  expectInstanceCallLowersAndValidates(kIdentityProgram);
}

ZC_TEST("MonoSession.initializerInstanceCallLowersAndValidatesUnderInstanceIdentity") {
  expectInstanceCallLowersAndValidates(kIdentityInitializerProgram);
}

}  // namespace zomlang::compiler::mono
