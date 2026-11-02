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

// Explicit i64 instantiation in direct-return position. The inferred literal
// form stays rejected (the literal defaults to i32 and cannot bootstrap from
// the return context); only the explicit type argument admits i64.
constexpr char kIdentityI64Program[] =
    "fn identity<T>(value: T) -> T {\n"
    "    return value;\n"
    "}\n"
    "fn entry() -> i64 {\n"
    "    return identity<i64>(42);\n"
    "}\n";

// Two parameter-free roots instantiate identity at two distinct scalar types;
// the i64 instance needs an explicit type argument while i32 is inferred from
// the literal.
constexpr char kTwoDistinctInstancesProgram[] =
    "fn identity<T>(value: T) -> T {\n"
    "    return value;\n"
    "}\n"
    "fn entry() -> i32 {\n"
    "    return identity(42);\n"
    "}\n"
    "fn other() -> i64 {\n"
    "    return identity<i64>(7);\n"
    "}\n";

// Generic integer binary: the template body is one arithmetic rvalue over two
// same-key type-parameter parameters. Its substituted i32 instance body lowers
// to the one-block arithmetic callee shape with two parameters.
constexpr char kAddProgram[] =
    "fn add<T>(a: T, b: T) -> T {\n"
    "    return a + b;\n"
    "}\n"
    "fn entry() -> i32 {\n"
    "    return add(40, 2);\n"
    "}\n";

// Two parameter-free roots instantiate identity at the same type, so both call
// edges name one shared InstanceId and the joined module emits the instance
// body exactly once.
constexpr char kTwoRootsSharedInstanceProgram[] =
    "fn identity<T>(value: T) -> T {\n"
    "    return value;\n"
    "}\n"
    "fn entry() -> i32 {\n"
    "    return identity(42);\n"
    "}\n"
    "fn other() -> i32 {\n"
    "    return identity(7);\n"
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

struct ResolvedUnit {
  const mir::MirFunction* caller;
  const mir::MirFunction* body;
  ir::InstanceId instance;
  zc::ArrayPtr<const uint8_t> frame;
};

// Finds every source caller whose entry Call edge names an Instance identity
// and resolves each edge to its verified instance body and identity frame.
zc::Maybe<zc::Vector<ResolvedUnit>> resolveInstanceCallUnits(
    const mir::MirPipelineFixture& fixture) {
  const auto& session = fixture.sessionRef();
  zc::Vector<ResolvedUnit> resolved;
  for (const auto& function : fixture.builtMir().functions()) {
    if (function.blocks.size() == 0 ||
        function.blocks[0].terminator.kind() != mir::MirTerminatorKind::Call) {
      continue;
    }
    const auto& edge = function.blocks[0].terminator.callValue();
    if (edge.callee.kind() != mir::MirCallableIdentityKind::Instance) { continue; }
    const auto instanceId = edge.callee.instanceValue();
    const mir::MirFunction* body = nullptr;
    zc::ArrayPtr<const uint8_t> frame;
    for (const auto& monoSet : session.getVerifiedMonoInstanceSets()) {
      for (const auto& instance : monoSet.instances()) {
        if (instance.identity == instanceId) {
          body = &instance.body;
          frame = instance.identityFrame.asPtr();
          break;
        }
      }
      if (body != nullptr) { break; }
    }
    if (body == nullptr) { return zc::none; }
    resolved.add(ResolvedUnit{&function, body, instanceId, frame});
  }
  if (resolved.size() == 0) { return zc::none; }
  return zc::mv(resolved);
}

// Builds the presented MIR/identity/frame arrays in the joined LIR module
// layout: callers first in unit order, then distinct instance bodies in first-
// reference order.
struct PresentedPlan {
  zc::Array<const mir::MirFunction*> mir;
  zc::Vector<mir::MirCallableIdentity> identities;
  zc::Vector<zc::ArrayPtr<const uint8_t>> frames;
};

PresentedPlan buildPresentedPlan(zc::ArrayPtr<const ResolvedUnit> units) {
  PresentedPlan plan;
  zc::Vector<ir::InstanceId> distinctInstances;
  for (const auto& unit : units) {
    bool seen = false;
    for (const auto& identity : distinctInstances) {
      if (identity == unit.instance) {
        seen = true;
        break;
      }
    }
    if (!seen) { distinctInstances.add(unit.instance); }
  }
  plan.mir = zc::heapArray<const mir::MirFunction*>(units.size() + distinctInstances.size());
  size_t index = 0;
  for (const auto& unit : units) {
    plan.mir[index] = unit.caller;
    plan.identities.add(mir::MirCallableIdentity::source(unit.caller->owner));
    plan.frames.add(zc::ArrayPtr<const uint8_t>{});
    ++index;
  }
  for (const auto& identity : distinctInstances) {
    for (const auto& unit : units) {
      if (unit.instance == identity) {
        plan.mir[index] = unit.body;
        plan.identities.add(mir::MirCallableIdentity::instance(identity));
        plan.frames.add(unit.frame);
        ++index;
        break;
      }
    }
  }
  return plan;
}

// Exercises the full instance-call lowering + presented-identity validation
// for one single-caller program: resolves the source caller with the entry
// Instance edge, pairs it with the verified instance body, lowers the joined
// monomorphic module, and proves it validates with explicit identities but
// fails closed Source-only.
void expectInstanceCallLowersAndValidates(zc::StringPtr program) {
  mir::MirPipelineFixture fixture(program);
  const auto& session = fixture.sessionRef();
  const auto types = session.getSemanticTypeStore();
  ZC_REQUIRE(types != zc::none);

  auto resolved = resolveInstanceCallUnits(fixture);
  ZC_REQUIRE(resolved != zc::none);
  const auto& units = ZC_ASSERT_NONNULL(resolved);
  ZC_EXPECT(units.size() == 1);
  const auto& resolvedUnit = units[0];

  zc::Vector<lir::MonoCallUnit> lirUnits;
  lirUnits.add(lir::MonoCallUnit{resolvedUnit.caller, resolvedUnit.body,
                                 mir::MirCallableIdentity::instance(resolvedUnit.instance),
                                 resolvedUnit.frame});
  auto module =
      lir::MirToLirLowering::lowerMonomorphicCallModule(lirUnits.asPtr(), ZC_ASSERT_NONNULL(types));
  ZC_REQUIRE(module != zc::none);
  const auto lirModule = zc::mv(ZC_ASSERT_NONNULL(module));
  ZC_EXPECT(lirModule.functions().size() == 2);
  ZC_EXPECT(lirModule.functions()[1].symbolName().startsWith("zom.mono."));

  // Present [caller, instance body] with [Source, Instance] identities in LIR
  // order; the Instance call edge must validate against the instance body.
  auto presented = buildPresentedPlan(units.asPtr());
  ZC_EXPECT(lir::TranslationValidator::validate(presented.mir.asPtr(), presented.identities.asPtr(),
                                                presented.frames.asPtr(), lirModule,
                                                ZC_ASSERT_NONNULL(types)) == zc::none);

  // Presenting Source-only identities for the same module must fail closed:
  // the Instance edge can never match a Source owner.
  ZC_EXPECT(lir::TranslationValidator::validate(presented.mir.asPtr(), lirModule,
                                                ZC_ASSERT_NONNULL(types)) != zc::none);
}

}  // namespace

ZC_TEST("MonoSession.instanceCallLowersAndValidatesUnderInstanceIdentity") {
  expectInstanceCallLowersAndValidates(kIdentityProgram);
}

ZC_TEST("MonoSession.initializerInstanceCallLowersAndValidatesUnderInstanceIdentity") {
  expectInstanceCallLowersAndValidates(kIdentityInitializerProgram);
}

ZC_TEST("MonoSession.i64InstanceCallLowersAndValidatesUnderInstanceIdentity") {
  expectInstanceCallLowersAndValidates(kIdentityI64Program);
}

ZC_TEST("MonoSession.genericArithmeticInstanceLowersAndValidatesUnderInstanceIdentity") {
  expectInstanceCallLowersAndValidates(kAddProgram);
}

namespace {

// Lowers a multi-caller program through the joined N-unit slice and validates
// its presented [callers..., distinct instances...] module. Returns the lowered
// module for per-test structural assertions.
lir::Module expectJoinedPlanLowersAndValidates(zc::StringPtr program, size_t expectedCallerCount,
                                               size_t expectedMonoSetSize) {
  mir::MirPipelineFixture fixture(program);
  const auto& session = fixture.sessionRef();
  const auto types = session.getSemanticTypeStore();
  ZC_REQUIRE(types != zc::none);

  const auto monoSets = session.getVerifiedMonoInstanceSets();
  ZC_REQUIRE(monoSets.size() == 1);
  ZC_EXPECT(monoSets[0].instances().size() == expectedMonoSetSize);

  auto resolved = resolveInstanceCallUnits(fixture);
  ZC_REQUIRE(resolved != zc::none);
  const auto& units = ZC_ASSERT_NONNULL(resolved);
  ZC_EXPECT(units.size() == expectedCallerCount);

  zc::Vector<lir::MonoCallUnit> lirUnits;
  for (size_t index = 0; index < units.size(); ++index) {
    lirUnits.add(lir::MonoCallUnit{units[index].caller, units[index].body,
                                   mir::MirCallableIdentity::instance(units[index].instance),
                                   units[index].frame});
  }
  auto module =
      lir::MirToLirLowering::lowerMonomorphicCallModule(lirUnits.asPtr(), ZC_ASSERT_NONNULL(types));
  ZC_REQUIRE(module != zc::none);
  auto lirModule = zc::mv(ZC_ASSERT_NONNULL(module));

  auto presented = buildPresentedPlan(units.asPtr());
  ZC_EXPECT(lir::TranslationValidator::validate(presented.mir.asPtr(), presented.identities.asPtr(),
                                                presented.frames.asPtr(), lirModule,
                                                ZC_ASSERT_NONNULL(types)) == zc::none);
  ZC_EXPECT(lir::TranslationValidator::validate(presented.mir.asPtr(), lirModule,
                                                ZC_ASSERT_NONNULL(types)) != zc::none);
  return zc::mv(lirModule);
}

}  // namespace

ZC_TEST("MonoSession.twoDistinctInstancesJoinOneModuleWithPositionalEdges") {
  auto module = expectJoinedPlanLowersAndValidates(kTwoDistinctInstancesProgram,
                                                   /*expectedCallerCount=*/2,
                                                   /*expectedMonoSetSize=*/2);
  // [entry caller, other caller, i32 instance, i64 instance].
  ZC_EXPECT(module.functions().size() == 4);
  ZC_EXPECT(module.functions()[0].symbolName() == "zom.module_init");
  ZC_EXPECT(module.functions()[1].symbolName() == "zom.standalone_1");
  ZC_EXPECT(module.functions()[2].symbolName().startsWith("zom.mono."));
  ZC_EXPECT(module.functions()[3].symbolName().startsWith("zom.mono."));
  ZC_EXPECT(module.functions()[2].symbolName() != module.functions()[3].symbolName());
  const auto& firstBlocks = module.functions()[0].blocks();
  const auto& secondBlocks = module.functions()[1].blocks();
  ZC_EXPECT(firstBlocks.size() == 2 && secondBlocks.size() == 2);
  ZC_EXPECT(firstBlocks[0].terminator().kind() == lir::TerminatorKind::Call);
  ZC_EXPECT(secondBlocks[0].terminator().kind() == lir::TerminatorKind::Call);
  ZC_EXPECT(firstBlocks[0].terminator().calleeIndex() == 2);
  ZC_EXPECT(secondBlocks[0].terminator().calleeIndex() == 3);
}

ZC_TEST("MonoSession.twoRootsShareOneEmittedInstanceBody") {
  auto module = expectJoinedPlanLowersAndValidates(kTwoRootsSharedInstanceProgram,
                                                   /*expectedCallerCount=*/2,
                                                   /*expectedMonoSetSize=*/1);
  // [entry caller, other caller, shared instance].
  ZC_EXPECT(module.functions().size() == 3);
  ZC_EXPECT(module.functions()[0].symbolName() == "zom.module_init");
  ZC_EXPECT(module.functions()[1].symbolName() == "zom.standalone_1");
  ZC_EXPECT(module.functions()[2].symbolName().startsWith("zom.mono."));
  const auto& firstBlocks = module.functions()[0].blocks();
  const auto& secondBlocks = module.functions()[1].blocks();
  ZC_EXPECT(firstBlocks.size() == 2 && secondBlocks.size() == 2);
  ZC_EXPECT(firstBlocks[0].terminator().kind() == lir::TerminatorKind::Call);
  ZC_EXPECT(secondBlocks[0].terminator().kind() == lir::TerminatorKind::Call);
  ZC_EXPECT(firstBlocks[0].terminator().calleeIndex() == 2);
  ZC_EXPECT(secondBlocks[0].terminator().calleeIndex() == 2);
}

}  // namespace zomlang::compiler::mono
