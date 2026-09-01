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

// RFC 0043 O5/KR5.4 slice: prove the host-execution compatibility comparison is
// a pure, total decision over the five RFC 0043 host dimensions (operating
// system, CPU architecture, object format, pointer width, and required ABI
// capabilities). It spawns no process, reads no filesystem, and never falls back
// to emulation; it is the gate `zomc run` applies before the (still blocked)
// process spawn.

#include "compiler/ir/host-execution-profile.h"

#include "compiler/ir/target-registry.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::ir {
namespace {

zc::Array<zc::String> noCapabilities() { return zc::Array<zc::String>(); }

zc::Array<zc::String> oneCapability(zc::StringPtr a) {
  zc::Vector<zc::String> result(1);
  result.add(zc::str(a));
  return result.releaseAsArray();
}

zc::Array<zc::String> twoCapabilities(zc::StringPtr a, zc::StringPtr b) {
  zc::Vector<zc::String> result(2);
  result.add(zc::str(a));
  result.add(zc::str(b));
  return result.releaseAsArray();
}

HostExecutionProfile profile(zc::StringPtr os, zc::StringPtr arch, ObjectFormat format,
                             uint32_t pointerWidth, zc::Array<zc::String>&& caps) {
  auto value = HostExecutionProfile::make(os, arch, format, pointerWidth, zc::mv(caps));
  ZC_REQUIRE(value != zc::none);
  return ZC_REQUIRE_NONNULL(zc::mv(value));
}

// The canonical host: linux / x86_64 / ELF / 64-bit with two ABI capabilities.
HostExecutionProfile linuxHost() {
  return profile("linux"_zc, "x86_64"_zc, ObjectFormat::Elf, 64,
                 twoCapabilities("sse2"_zc, "sysv"_zc));
}

// An artifact identical to the host is executable.
ZC_TEST("Host compatibility accepts an identical artifact") {
  auto result = runCompatibility(linuxHost(), linuxHost());
  ZC_EXPECT(result.isCompatible());
}

// An artifact requiring a subset of the host's ABI capabilities is executable.
ZC_TEST("Host compatibility accepts an ABI capability subset") {
  auto artifact = profile("linux"_zc, "x86_64"_zc, ObjectFormat::Elf, 64, oneCapability("sysv"_zc));
  auto result = runCompatibility(artifact, linuxHost());
  ZC_EXPECT(result.isCompatible());

  auto empty = profile("linux"_zc, "x86_64"_zc, ObjectFormat::Elf, 64, noCapabilities());
  ZC_EXPECT(runCompatibility(empty, linuxHost()).isCompatible());
}

// A differing operating system is an OperatingSystem mismatch.
ZC_TEST("Host compatibility rejects an operating-system mismatch") {
  auto artifact = profile("darwin"_zc, "x86_64"_zc, ObjectFormat::Elf, 64,
                          twoCapabilities("sse2"_zc, "sysv"_zc));
  auto result = runCompatibility(artifact, linuxHost());
  ZC_REQUIRE(!result.isCompatible());
  ZC_EXPECT(result.reason() == HostMismatchReason::OperatingSystem);
}

// A differing CPU architecture is a CpuArchitecture mismatch.
ZC_TEST("Host compatibility rejects a cpu-architecture mismatch") {
  auto artifact = profile("linux"_zc, "aarch64"_zc, ObjectFormat::Elf, 64,
                          twoCapabilities("sse2"_zc, "sysv"_zc));
  auto result = runCompatibility(artifact, linuxHost());
  ZC_REQUIRE(!result.isCompatible());
  ZC_EXPECT(result.reason() == HostMismatchReason::CpuArchitecture);
}

// A differing object format is an ObjectFormatKind mismatch.
ZC_TEST("Host compatibility rejects an object-format mismatch") {
  auto artifact = profile("linux"_zc, "x86_64"_zc, ObjectFormat::MachO, 64,
                          twoCapabilities("sse2"_zc, "sysv"_zc));
  auto result = runCompatibility(artifact, linuxHost());
  ZC_REQUIRE(!result.isCompatible());
  ZC_EXPECT(result.reason() == HostMismatchReason::ObjectFormatKind);
}

// A differing pointer width is a PointerWidth mismatch.
ZC_TEST("Host compatibility rejects a pointer-width mismatch") {
  auto artifact = profile("linux"_zc, "x86_64"_zc, ObjectFormat::Elf, 32,
                          twoCapabilities("sse2"_zc, "sysv"_zc));
  auto result = runCompatibility(artifact, linuxHost());
  ZC_REQUIRE(!result.isCompatible());
  ZC_EXPECT(result.reason() == HostMismatchReason::PointerWidth);
}

// An ABI capability the host does not provide is an AbiCapability mismatch.
ZC_TEST("Host compatibility rejects a missing ABI capability") {
  auto artifact = profile("linux"_zc, "x86_64"_zc, ObjectFormat::Elf, 64,
                          twoCapabilities("avx512"_zc, "sysv"_zc));
  auto result = runCompatibility(artifact, linuxHost());
  ZC_REQUIRE(!result.isCompatible());
  ZC_EXPECT(result.reason() == HostMismatchReason::AbiCapability);
}

// The factory rejects an empty operating system or architecture, a zero pointer
// width, and a non-ascending ABI capability set.
ZC_TEST("Host execution profile factory rejects malformed input") {
  ZC_EXPECT(HostExecutionProfile::make(""_zc, "x86_64"_zc, ObjectFormat::Elf, 64,
                                       noCapabilities()) == zc::none);
  ZC_EXPECT(HostExecutionProfile::make("linux"_zc, ""_zc, ObjectFormat::Elf, 64,
                                       noCapabilities()) == zc::none);
  ZC_EXPECT(HostExecutionProfile::make("linux"_zc, "x86_64"_zc, ObjectFormat::Elf, 0,
                                       noCapabilities()) == zc::none);
  ZC_EXPECT(HostExecutionProfile::make("linux"_zc, "x86_64"_zc, ObjectFormat::Elf, 64,
                                       twoCapabilities("sysv"_zc, "sse2"_zc)) == zc::none);
  ZC_EXPECT(HostExecutionProfile::make("linux"_zc, "x86_64"_zc, ObjectFormat::Elf, 64,
                                       twoCapabilities("sysv"_zc, "sysv"_zc)) == zc::none);
}

// G2 fix: the artifact profile must come from the artifact's own inspection
// facts and target triple, not the running host, so a genuine cross-target
// artifact is rejected. On this x86-64 Linux host, an AArch64 artifact (an
// aarch64 triple with an AArch64 inspection machine) is a real CpuArchitecture
// mismatch against the current host. Before the fix, both profiles were built
// from the host, so this comparison was tautologically compatible.
ExecutableInspectionProfile inspectionOf(ObjectFormat format, ExecutableMachine machine,
                                         uint32_t pointerWidth) {
  auto value = ExecutableInspectionProfile::make(format, machine, pointerWidth,
                                                 zc::Array<zc::String>(), zc::str("__zom_"));
  ZC_REQUIRE(value != zc::none);
  return ZC_REQUIRE_NONNULL(zc::mv(value));
}

ZC_TEST("Artifact profile from an aarch64 triple mismatches an x86-64 host") {
  auto artifact = artifactExecutionProfileFromInspection(
      "aarch64-unknown-linux-gnu"_zc,
      inspectionOf(ObjectFormat::Elf, ExecutableMachine::AArch64, 64));
  ZC_REQUIRE(artifact != zc::none);
  auto host = currentHostExecutionProfile();
  ZC_REQUIRE(host != zc::none);
  auto result =
      runCompatibility(ZC_REQUIRE_NONNULL(zc::mv(artifact)), ZC_REQUIRE_NONNULL(zc::mv(host)));
  ZC_REQUIRE(!result.isCompatible());
  ZC_EXPECT(result.reason() == HostMismatchReason::CpuArchitecture);
}

// A same-target artifact (x86-64 triple + x86-64 inspection machine) is
// compatible with the x86-64 Linux host that produced it.
ZC_TEST("Artifact profile from an x86-64 triple matches the x86-64 host") {
  auto artifact = artifactExecutionProfileFromInspection(
      "x86_64-unknown-linux-gnu"_zc,
      inspectionOf(ObjectFormat::Elf, ExecutableMachine::X86_64, 64));
  ZC_REQUIRE(artifact != zc::none);
  auto host = currentHostExecutionProfile();
  ZC_REQUIRE(host != zc::none);
  ZC_EXPECT(runCompatibility(ZC_REQUIRE_NONNULL(zc::mv(artifact)), ZC_REQUIRE_NONNULL(zc::mv(host)))
                .isCompatible());
}

// The artifact profile factory fails closed on a malformed triple, an
// unsupported operating system or architecture, and a triple architecture that
// disagrees with the inspection machine.
ZC_TEST("Artifact profile from inspection fails closed on malformed or inconsistent input") {
  const auto x86 = ObjectFormat::Elf;
  // Fewer than three fields (arch-vendor-os) is malformed.
  ZC_EXPECT(artifactExecutionProfileFromInspection(
                "x86_64-unknown"_zc, inspectionOf(x86, ExecutableMachine::X86_64, 64)) == zc::none);
  // An empty field is malformed.
  ZC_EXPECT(artifactExecutionProfileFromInspection(
                "x86_64--linux-gnu"_zc, inspectionOf(x86, ExecutableMachine::X86_64, 64)) ==
            zc::none);
  // An unsupported architecture is rejected.
  ZC_EXPECT(artifactExecutionProfileFromInspection(
                "riscv64-unknown-linux-gnu"_zc, inspectionOf(x86, ExecutableMachine::X86_64, 64)) ==
            zc::none);
  // An unsupported operating system is rejected.
  ZC_EXPECT(artifactExecutionProfileFromInspection(
                "x86_64-unknown-freebsd"_zc, inspectionOf(x86, ExecutableMachine::X86_64, 64)) ==
            zc::none);
  // A triple architecture that disagrees with the inspection machine is rejected
  // (x86_64 triple but the machine says AArch64, and vice versa).
  ZC_EXPECT(artifactExecutionProfileFromInspection(
                "x86_64-unknown-linux-gnu"_zc, inspectionOf(x86, ExecutableMachine::AArch64, 64)) ==
            zc::none);
  ZC_EXPECT(artifactExecutionProfileFromInspection(
                "aarch64-unknown-linux-gnu"_zc, inspectionOf(x86, ExecutableMachine::X86_64, 64)) ==
            zc::none);
}

}  // namespace
}  // namespace zomlang::compiler::ir
