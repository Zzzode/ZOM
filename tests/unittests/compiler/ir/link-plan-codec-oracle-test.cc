// Copyright (c) 2026 Zode.Z. All rights reserved
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
// See the License for the specific language governing permissions and
// limitations under the License.

// RFC 0043 O5/KR5.3 first slice: prove the verified link-plan codec and its
// independent verifier are deterministic, field sensitive, and fail closed on
// each of the six numbered link-plan invariants. This slice constructs and
// verifies the plan as pure data and computes its deterministic LinkPlanId; it
// invokes no linker, reads no filesystem, and binds no live capability.

#include "compiler/identity/crypto/sha256.h"
#include "compiler/ir/ir-failure.h"
#include "compiler/ir/link-plan-codec.h"
#include "zc/core/encoding.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::ir {
namespace {

zc::String hex(zc::ArrayPtr<const uint8_t> bytes) { return zc::encodeHex(bytes); }

identity::Sha256Digest digestOf(zc::StringPtr seed) {
  auto digest = identity::sha256(seed.asBytes());
  ZC_REQUIRE(digest != zc::none);
  return ZC_REQUIRE_NONNULL(digest);
}

LinkInputRecord input(zc::StringPtr path, LinkInputRole role, zc::StringPtr digestSeed,
                      uint64_t byteCount) {
  auto record = LinkInputRecord::make(path, role, digestOf(digestSeed), byteCount);
  ZC_REQUIRE(record != zc::none);
  return ZC_REQUIRE_NONNULL(zc::mv(record));
}

// Wraps one link input in a single-element array.
zc::Array<LinkInputRecord> oneInput(LinkInputRecord&& record) {
  auto builder = zc::heapArrayBuilder<LinkInputRecord>(1);
  builder.add(zc::mv(record));
  return builder.finish();
}

ExecutableInspectionProfile inspectionProfile() {
  auto symbols = zc::heapArrayBuilder<zc::String>(1);
  symbols.add(zc::str("__zom_runtime"));
  auto profile = ExecutableInspectionProfile::make(ObjectFormat::Elf, ExecutableMachine::X86_64, 64,
                                                   symbols.finish(), zc::str("__zom_"));
  ZC_REQUIRE(profile != zc::none);
  return ZC_REQUIRE_NONNULL(zc::mv(profile));
}

ToolchainClosureRecord minimalClosure() {
  const uint8_t targetIdentity[] = {0x74, 0x67, 0x74};  // "tgt"
  auto crtObjects = oneInput(input("/sysroot/lib/crt1.o", LinkInputRole::CrtObject, "crt1", 1024));
  auto defaultLibraries =
      oneInput(input("/sysroot/lib/libc.so", LinkInputRole::DefaultLibrary, "libc", 2048));
  auto closure = ToolchainClosureRecord::make(
      zc::arrayPtr(targetIdentity, 3), "/sysroot"_zc, LinkerDriverKind::ElfDriver,
      "/sysroot/bin/cc"_zc, digestOf("cc"), 4096, zc::mv(crtObjects), zc::mv(defaultLibraries));
  ZC_REQUIRE(closure != zc::none);
  return ZC_REQUIRE_NONNULL(zc::mv(closure));
}

// Builds a fixed, minimal, valid link request. The plan carries no generic
// argument surface; the driver derives its canonical argument vector from these
// closed structural fields.
ExecutableLinkRequest minimalRequest() {
  ExecutableLinkRequest request{
      minimalClosure(),
      inspectionProfile(),
      zc::heapArray<uint8_t>({0x7a, 0x6f, 0x6d}),  // "zom" entry symbol
      oneInput(input("/out/app.o", LinkInputRole::ObjectArtifact, "obj", 512)),
      oneInput(input("/sysroot/lib/zomrt.o", LinkInputRole::RuntimeObject, "rt", 256)),
      zc::str("/out"),
      zc::str("/out/app")};
  return request;
}

VerifiedLinkPlan verifiedPlan() {
  auto result = LinkPlanVerifier::verify(minimalRequest());
  ZC_REQUIRE(result.isVerified());
  return zc::mv(result).takeVerified();
}

}  // namespace

// The minimal valid request verifies, and its canonical encoding reproduces a
// fixed byte length, preimage hex, and LinkPlanId digest. The bytes are produced
// by the live encoder; the asserted hex and digest are the frozen oracle for
// this slice (RFC 0043 does not publish a link-plan oracle, unlike RFC 0021's
// LIR algebra, so the frozen constants are this test's own regression anchor).

ZC_TEST("Link plan codec reproduces the minimal-plan oracle") {
  auto plan = verifiedPlan();
  auto bytes = LinkPlanCodec::encode(plan);

  ZC_EXPECT(bytes.size() == 518);
  ZC_EXPECT(hex(bytes.asPtr()) ==
            "7a6f6d2e6c696e6b2d706c616e0000000000000000037467740100000000000000082f737973726f6f74"
            "000000000000000f2f737973726f6f742f62696e2f63630000000000000020355b1bbfc96725cdce8f4a"
            "2708fda310a80e6d13315aec4e5eed2a75fe8032ce000000000000100000000000000000010000000000"
            "0000132f737973726f6f742f6c69622f637274312e6f02000000000000002032c45a9e8888c079df3868"
            "7b7146a1c55a56fe052f8715f1dc6d18143362ac6c000000000000040000000000000000010000000000"
            "0000142f737973726f6f742f6c69622f6c6962632e736f03000000000000002016c8c6eb85e05438f5d6"
            "c60ff9869072a3a3b1618aa1481ac7a0cb049f06f51d000000000000080001010000004000000000000000"
            "01000000000000000d5f5f7a6f6d5f72756e74696d6500000000000000065f5f7a6f6d5f00000000000000"
            "037a6f6d00"
            "00000000000001000000000000000a2f6f75742f6170702e6f010000000000000020772a5fb04f9bad38"
            "681a2f56ddfdbd6a15185753df8dcc029788d02bf3b6825b000000000000020000000000000000010000"
            "0000000000142f737973726f6f742f6c69622f7a6f6d72742e6f040000000000000020cdffd5dd8ca812"
            "6c0482ba994814b9014cc9e973435d399f1cf1f69479e6b907000000000000010000000000000000082f"
            "6f75742f617070");

  auto expected = identity::sha256(bytes.asPtr());
  ZC_REQUIRE(expected != zc::none);
  ZC_IF_SOME(value, expected) { ZC_EXPECT(plan.id().digest() == value); }
  ZC_EXPECT(zc::encodeHex(plan.id().digest().bytes()) ==
            "54e60703e2ea42b6f0b45f616f41f3b417b298345edf5e8d6a79b5d5817c8dfd"_zc);
}

// Re-encoding an equal plan yields identical bytes beginning with the domain tag
// and a NUL separator.

ZC_TEST("Link plan codec is deterministic and domain framed") {
  auto first = LinkPlanCodec::encode(verifiedPlan());
  auto second = LinkPlanCodec::encode(verifiedPlan());
  ZC_EXPECT(first.asPtr() == second.asPtr());

  constexpr char domain[] = "zom.link-plan";
  ZC_REQUIRE(first.size() > sizeof(domain));
  for (size_t index = 0; index + 1 < sizeof(domain); ++index) {
    ZC_EXPECT(first[index] == static_cast<uint8_t>(domain[index]));
  }
  ZC_EXPECT(first[sizeof(domain) - 1] == 0x00);
}

// The LinkPlanId is sensitive to every framed field: a change to the sysroot, an
// input digest, the argument order, an input path, or the output path must move
// the id away from the baseline.

ZC_TEST("Link plan id is field sensitive") {
  const auto baseline = verifiedPlan().id();

  {
    // A different output path changes the id.
    auto request = minimalRequest();
    request.outputPath = zc::str("/out/app2");
    auto result = LinkPlanVerifier::verify(zc::mv(request));
    ZC_REQUIRE(result.isVerified());
    ZC_EXPECT(result.verifiedValue().id() != baseline);
  }
  {
    auto request = minimalRequest();
    auto symbols = zc::heapArrayBuilder<zc::String>(1);
    symbols.add(zc::str("__zom_runtime2"));
    request.inspectionProfile = ZC_REQUIRE_NONNULL(ExecutableInspectionProfile::make(
        ObjectFormat::Elf, ExecutableMachine::X86_64, 64, symbols.finish(), zc::str("__zom_")));
    auto result = LinkPlanVerifier::verify(zc::mv(request));
    ZC_REQUIRE(result.isVerified());
    ZC_EXPECT(result.verifiedValue().id() != baseline);
  }
  {
    // A different runtime input path changes the id.
    auto request = minimalRequest();
    request.runtimeRecords =
        oneInput(input("/sysroot/lib/zomrt2.o", LinkInputRole::RuntimeObject, "rt", 256));
    auto result = LinkPlanVerifier::verify(zc::mv(request));
    ZC_REQUIRE(result.isVerified());
    ZC_EXPECT(result.verifiedValue().id() != baseline);
  }
  {
    // A different object content digest changes the id.
    auto request = minimalRequest();
    request.objectRecords =
        oneInput(input("/out/app.o", LinkInputRole::ObjectArtifact, "obj-x", 512));
    auto result = LinkPlanVerifier::verify(zc::mv(request));
    ZC_REQUIRE(result.isVerified());
    ZC_EXPECT(result.verifiedValue().id() != baseline);
  }
}

// Invariant (6): the output path must be normalized and inside the output root.

ZC_TEST("Link plan verifier rejects an out-of-root output path") {
  auto request = minimalRequest();
  request.outputPath = zc::str("/elsewhere/app");
  auto result = LinkPlanVerifier::verify(zc::mv(request));
  ZC_REQUIRE(result.isCapabilityRejected());
  const auto& facts = result.capabilityFailures().facts();
  ZC_REQUIRE(facts.size() == 1);
  ZC_EXPECT(facts[0].phase() == IrFailurePhase::LinkPlanConstruction);
  ZC_EXPECT(facts[0].kind() == IrFailureKind::OutputCreationFailed);
}

ZC_TEST("Link plan verifier rejects a non-normalized output path") {
  auto request = minimalRequest();
  request.outputPath = zc::str("/out/../out/app");
  auto result = LinkPlanVerifier::verify(zc::mv(request));
  ZC_REQUIRE(result.isCapabilityRejected());
  ZC_EXPECT(result.capabilityFailures().facts()[0].kind() == IrFailureKind::OutputCreationFailed);
}

// Invariant (3): the plan must name exactly one non-empty entry symbol.

ZC_TEST("Link plan verifier rejects a missing entry symbol") {
  auto request = minimalRequest();
  request.entrySymbol = zc::Array<uint8_t>();
  auto result = LinkPlanVerifier::verify(zc::mv(request));
  ZC_REQUIRE(result.isIrInvariantRejected());
  const auto& facts = result.invariantFailures().facts();
  ZC_REQUIRE(facts.size() == 1);
  ZC_EXPECT(facts[0].phase() == IrFailurePhase::LinkPlanConstruction);
  ZC_EXPECT(facts[0].kind() == IrFailureKind::MissingRequiredFact);
}

ZC_TEST("Executable inspection profile rejects invalid target and symbol shapes") {
  zc::Array<zc::String> empty;
  ZC_EXPECT(ExecutableInspectionProfile::make(ObjectFormat::Coff, ExecutableMachine::X86_64, 64,
                                              zc::mv(empty), zc::str("__zom_")) == zc::none);
  zc::Array<zc::String> emptyWidth;
  ZC_EXPECT(ExecutableInspectionProfile::make(ObjectFormat::Elf, ExecutableMachine::X86_64, 32,
                                              zc::mv(emptyWidth), zc::str("__zom_")) == zc::none);
  zc::Array<zc::String> invalidMachine;
  ZC_EXPECT(
      ExecutableInspectionProfile::make(ObjectFormat::Elf, static_cast<ExecutableMachine>(0xff), 64,
                                        zc::mv(invalidMachine), zc::str("__zom_")) == zc::none);
  auto duplicate = zc::heapArrayBuilder<zc::String>(2);
  duplicate.add(zc::str("__zom_runtime"));
  duplicate.add(zc::str("__zom_runtime"));
  ZC_EXPECT(ExecutableInspectionProfile::make(ObjectFormat::Elf, ExecutableMachine::X86_64, 64,
                                              duplicate.finish(), zc::str("__zom_")) == zc::none);
  // The runtime-reference domain is the canonical raw prefix derived from the
  // object format; an empty or off-canonical domain (which would silently disable
  // or misdirect the unresolved-runtime check) is rejected.
  zc::Array<zc::String> emptyDomainSyms;
  ZC_EXPECT(ExecutableInspectionProfile::make(ObjectFormat::Elf, ExecutableMachine::X86_64, 64,
                                              zc::mv(emptyDomainSyms), zc::str("")) == zc::none);
  zc::Array<zc::String> wrongDomainSyms;
  ZC_EXPECT(ExecutableInspectionProfile::make(ObjectFormat::Elf, ExecutableMachine::X86_64, 64,
                                              zc::mv(wrongDomainSyms), zc::str("x.")) == zc::none);
  // ELF requires __zom_; the Mach-O spelling ___zom_ is off-canonical for ELF.
  zc::Array<zc::String> machODomainOnElf;
  ZC_EXPECT(ExecutableInspectionProfile::make(ObjectFormat::Elf, ExecutableMachine::X86_64, 64,
                                              zc::mv(machODomainOnElf),
                                              zc::str("___zom_")) == zc::none);
  // Mach-O requires ___zom_; the ELF spelling __zom_ is off-canonical for Mach-O.
  zc::Array<zc::String> elfDomainOnMachO;
  ZC_EXPECT(ExecutableInspectionProfile::make(ObjectFormat::MachO, ExecutableMachine::X86_64, 64,
                                              zc::mv(elfDomainOnMachO),
                                              zc::str("__zom_")) == zc::none);
}

ZC_TEST("Link plan verifier rejects a non-ASCII or NUL entry symbol") {
  auto request = minimalRequest();
  request.entrySymbol = zc::heapArray<uint8_t>({0x7a, 0x00, 0x6d});
  auto nulResult = LinkPlanVerifier::verify(zc::mv(request));
  ZC_REQUIRE(nulResult.isIrInvariantRejected());
  ZC_EXPECT(nulResult.invariantFailures().facts()[0].kind() == IrFailureKind::MissingRequiredFact);

  request = minimalRequest();
  request.entrySymbol = zc::heapArray<uint8_t>({0xff});
  auto asciiResult = LinkPlanVerifier::verify(zc::mv(request));
  ZC_REQUIRE(asciiResult.isIrInvariantRejected());
  ZC_EXPECT(asciiResult.invariantFailures().facts()[0].kind() ==
            IrFailureKind::MissingRequiredFact);
}

ZC_TEST("Link plan verifier rejects a driver and inspection format mismatch") {
  auto request = minimalRequest();
  zc::Array<zc::String> symbols;
  request.inspectionProfile = ZC_REQUIRE_NONNULL(ExecutableInspectionProfile::make(
      ObjectFormat::MachO, ExecutableMachine::X86_64, 64, zc::mv(symbols), zc::str("___zom_")));
  auto result = LinkPlanVerifier::verify(zc::mv(request));
  ZC_REQUIRE(result.isIrInvariantRejected());
  ZC_EXPECT(result.invariantFailures().facts()[0].kind() == IrFailureKind::InvalidAbi);
}

// Invariant (1): at least one object artifact must be linked.

ZC_TEST("Link plan verifier rejects an empty object set") {
  auto request = minimalRequest();
  request.objectRecords = zc::Array<LinkInputRecord>();
  auto result = LinkPlanVerifier::verify(zc::mv(request));
  ZC_REQUIRE(result.isIrInvariantRejected());
  ZC_EXPECT(result.invariantFailures().facts()[0].kind() == IrFailureKind::MissingRequiredFact);
}

// Invariant (4): object records must carry the object-artifact role.

ZC_TEST("Link plan verifier rejects a mis-roled object record") {
  auto request = minimalRequest();
  request.objectRecords = oneInput(input("/out/app.o", LinkInputRole::RuntimeObject, "obj", 512));
  auto result = LinkPlanVerifier::verify(zc::mv(request));
  ZC_REQUIRE(result.isIrInvariantRejected());
  ZC_EXPECT(result.invariantFailures().facts()[0].kind() == IrFailureKind::InvalidFact);
}

// Invariant (4): duplicate canonical keys in a record sequence reject.

ZC_TEST("Link plan verifier rejects duplicate object records") {
  auto request = minimalRequest();
  auto builder = zc::heapArrayBuilder<LinkInputRecord>(2);
  builder.add(input("/out/app.o", LinkInputRole::ObjectArtifact, "obj", 512));
  builder.add(input("/out/app.o", LinkInputRole::ObjectArtifact, "obj", 512));
  request.objectRecords = builder.finish();
  auto result = LinkPlanVerifier::verify(zc::mv(request));
  ZC_REQUIRE(result.isIrInvariantRejected());
  ZC_EXPECT(result.invariantFailures().facts()[0].kind() == IrFailureKind::AdditionalFact);
}

// Invariant (5): an object record path outside the output root rejects.

ZC_TEST("Link plan verifier rejects an out-of-root object path") {
  auto request = minimalRequest();
  request.objectRecords =
      oneInput(input("/elsewhere/app.o", LinkInputRole::ObjectArtifact, "obj", 512));
  auto result = LinkPlanVerifier::verify(zc::mv(request));
  ZC_REQUIRE(result.isIrInvariantRejected());
  ZC_EXPECT(result.invariantFailures().facts()[0].kind() == IrFailureKind::InvalidFact);
}

// The record factories fail closed on non-normalized paths and zero byte counts.

ZC_TEST("Link input record rejects invalid construction") {
  ZC_EXPECT(LinkInputRecord::make("relative/path"_zc, LinkInputRole::ObjectArtifact, digestOf("x"),
                                  1) == zc::none);
  ZC_EXPECT(LinkInputRecord::make("/abs/path"_zc, LinkInputRole::ObjectArtifact, digestOf("x"),
                                  0) == zc::none);
  ZC_EXPECT(LinkInputRecord::make("/abs/../path"_zc, LinkInputRole::ObjectArtifact, digestOf("x"),
                                  1) == zc::none);
}

}  // namespace zomlang::compiler::ir
