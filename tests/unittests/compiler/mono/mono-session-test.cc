// Copyright (c) 2026 Zode.Z. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zc/ztest/test.h"
#include "tests/unittests/compiler/mir/mir-pipeline-fixture.h"

namespace zomlang::compiler::mono {
namespace {

constexpr char kIdentityProgram[] =
    "fn identity<T>(value: T) -> T {\n"
    "    return value;\n"
    "}\n"
    "fn entry() -> i32 {\n"
    "    return identity(42);\n"
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

}  // namespace zomlang::compiler::mono
