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
// See the License for the specific language governing permissions and limitations under
// the License.

#include "compiler/driver/package/linux-sandbox-policy.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "compiler/identity/crypto/sha256.h"

namespace zomlang::compiler::driver::package {
namespace {

constexpr uint32_t kAllow = 0x7fff0000;
constexpr uint32_t kTrap = 0x00030000;

struct SeccompInput final {
  uint32_t syscall;
  uint32_t architecture;
  uint64_t arguments[6] = {};
};

uint32_t loadWord(const SeccompInput& input, uint32_t offset) {
  if (offset == 0) { return input.syscall; }
  if (offset == 4) { return input.architecture; }
  ZC_REQUIRE(offset >= 16 && offset < 16 + 6 * 8);
  const uint32_t argument = (offset - 16) / 8;
  const bool high = (offset - 16) % 8 == 4;
  return static_cast<uint32_t>(input.arguments[argument] >> (high ? 32U : 0U));
}

uint32_t evaluate(zc::ArrayPtr<const LinuxSandboxBpfInstruction> program,
                  const SeccompInput& input) {
  uint32_t accumulator = 0;
  for (size_t pc = 0; pc < program.size(); ++pc) {
    const auto& instruction = program[pc];
    switch (instruction.code) {
      case 0x20:
        accumulator = loadWord(input, instruction.operand);
        break;
      case 0x15:
        pc += accumulator == instruction.operand ? instruction.jumpTrue : instruction.jumpFalse;
        break;
      case 0x25:
        pc += accumulator > instruction.operand ? instruction.jumpTrue : instruction.jumpFalse;
        break;
      case 0x35:
        pc += accumulator >= instruction.operand ? instruction.jumpTrue : instruction.jumpFalse;
        break;
      case 0x45:
        pc +=
            (accumulator & instruction.operand) != 0 ? instruction.jumpTrue : instruction.jumpFalse;
        break;
      case 0x06:
        return instruction.operand;
      default:
        ZC_FAIL_REQUIRE("unknown classic BPF instruction in fixture");
    }
  }
  ZC_FAIL_REQUIRE("seccomp fixture fell off the end of its program");
}

uint32_t auditArchitecture(LinuxSandboxArchitecture architecture) {
  return architecture == LinuxSandboxArchitecture::X86_64 ? 0xc000003e : 0xc00000b7;
}

uint32_t number(LinuxSandboxArchitecture architecture, uint32_t x86, uint32_t arm) {
  return architecture == LinuxSandboxArchitecture::X86_64 ? x86 : arm;
}

SeccompInput input(LinuxSandboxArchitecture architecture, uint32_t syscall) {
  return SeccompInput{syscall, auditArchitecture(architecture), {}};
}

}  // namespace

ZC_TEST("Linux sandbox runtime filter is architecture-bound and default-trap") {
  const LinuxSandboxArchitecture architectures[] = {LinuxSandboxArchitecture::X86_64,
                                                    LinuxSandboxArchitecture::AArch64};
  for (const auto architecture : architectures) {
    auto program = generateLinuxSandboxFilter(architecture, LinuxSandboxFilterPhase::Runtime);
    auto exitCall = input(architecture, number(architecture, 60, 93));
    ZC_EXPECT(evaluate(program, exitCall) == kAllow);
    auto socketCall = input(architecture, number(architecture, 41, 198));
    ZC_EXPECT(evaluate(program, socketCall) == kTrap);
    exitCall.architecture = auditArchitecture(architecture == LinuxSandboxArchitecture::X86_64
                                                  ? LinuxSandboxArchitecture::AArch64
                                                  : LinuxSandboxArchitecture::X86_64);
    ZC_EXPECT(evaluate(program, exitCall) == kTrap);
  }

  auto x86 = generateLinuxSandboxFilter(LinuxSandboxArchitecture::X86_64,
                                        LinuxSandboxFilterPhase::Runtime);
  auto x32 = input(LinuxSandboxArchitecture::X86_64, 0x40000000U | 60U);
  ZC_EXPECT(evaluate(x86, x32) == kTrap);
}

ZC_TEST("Linux sandbox runtime filter enforces fixed descriptor and memory arguments") {
  const LinuxSandboxArchitecture architectures[] = {LinuxSandboxArchitecture::X86_64,
                                                    LinuxSandboxArchitecture::AArch64};
  for (const auto architecture : architectures) {
    auto program = generateLinuxSandboxFilter(architecture, LinuxSandboxFilterPhase::Runtime);
    auto readRequest = input(architecture, number(architecture, 0, 63));
    readRequest.arguments[0] = 3;
    ZC_EXPECT(evaluate(program, readRequest) == kAllow);
    readRequest.arguments[0] = 4;
    ZC_EXPECT(evaluate(program, readRequest) == kTrap);
    readRequest.arguments[0] = 0;
    ZC_EXPECT(evaluate(program, readRequest) == kAllow);
    readRequest.arguments[0] = 8;
    ZC_EXPECT(evaluate(program, readRequest) == kTrap);

    auto writeResponse = input(architecture, number(architecture, 1, 64));
    writeResponse.arguments[0] = 4;
    ZC_EXPECT(evaluate(program, writeResponse) == kAllow);
    writeResponse.arguments[0] = 3;
    ZC_EXPECT(evaluate(program, writeResponse) == kTrap);
    writeResponse.arguments[0] = 0;
    ZC_EXPECT(evaluate(program, writeResponse) == kAllow);

    auto closeFile = input(architecture, number(architecture, 3, 57));
    closeFile.arguments[0] = 0;
    ZC_EXPECT(evaluate(program, closeFile) == kAllow);
    closeFile.arguments[0] = 7;
    ZC_EXPECT(evaluate(program, closeFile) == kAllow);
    closeFile.arguments[0] = 8;
    ZC_EXPECT(evaluate(program, closeFile) == kTrap);

    auto statFile = input(architecture, number(architecture, 5, 80));
    statFile.arguments[0] = 0;
    ZC_EXPECT(evaluate(program, statFile) == kAllow);
    statFile.arguments[0] = 8;
    ZC_EXPECT(evaluate(program, statFile) == kTrap);

    auto open = input(architecture, 437);
    open.arguments[0] = 5;
    ZC_EXPECT(evaluate(program, open) == kAllow);
    open.arguments[0] = 6;
    ZC_EXPECT(evaluate(program, open) == kAllow);
    open.arguments[0] = 7;
    ZC_EXPECT(evaluate(program, open) == kTrap);

    auto map = input(architecture, number(architecture, 9, 222));
    map.arguments[2] = 3;
    map.arguments[3] = 0x22;
    map.arguments[4] = UINT64_MAX;
    ZC_EXPECT(evaluate(program, map) == kAllow);
    map.arguments[4] = UINT32_MAX;
    ZC_EXPECT(evaluate(program, map) == kTrap);
    map.arguments[4] = UINT64_MAX;
    map.arguments[2] = 7;
    ZC_EXPECT(evaluate(program, map) == kTrap);
  }
}

ZC_TEST("Linux sandbox bootstrap filter admits one constrained executable transition") {
  const LinuxSandboxArchitecture architectures[] = {LinuxSandboxArchitecture::X86_64,
                                                    LinuxSandboxArchitecture::AArch64};
  for (const auto architecture : architectures) {
    auto bootstrap = generateLinuxSandboxFilter(architecture, LinuxSandboxFilterPhase::Bootstrap);
    auto runtime = generateLinuxSandboxFilter(architecture, LinuxSandboxFilterPhase::Runtime);
    auto exec = input(architecture, number(architecture, 322, 281));
    exec.arguments[0] = 7;
    exec.arguments[4] = 0x1000;
    ZC_EXPECT(evaluate(bootstrap, exec) == kAllow);
    ZC_EXPECT(evaluate(runtime, exec) == kTrap);
    exec.arguments[0] = 6;
    ZC_EXPECT(evaluate(bootstrap, exec) == kTrap);

    auto seccomp = input(architecture, number(architecture, 317, 277));
    seccomp.arguments[0] = 1;
    seccomp.arguments[1] = 0;
    ZC_EXPECT(evaluate(bootstrap, seccomp) == kAllow);
    ZC_EXPECT(evaluate(runtime, seccomp) == kTrap);
    seccomp.arguments[1] = 1;
    ZC_EXPECT(evaluate(bootstrap, seccomp) == kTrap);

    auto setupWrite = input(architecture, number(architecture, 1, 64));
    setupWrite.arguments[0] = 8;
    ZC_EXPECT(evaluate(bootstrap, setupWrite) == kAllow);
    ZC_EXPECT(evaluate(runtime, setupWrite) == kTrap);
  }
}

ZC_TEST("Linux sandbox generated filter bytes have stable drift hashes") {
  const LinuxSandboxArchitecture architectures[] = {LinuxSandboxArchitecture::X86_64,
                                                    LinuxSandboxArchitecture::AArch64};
  const LinuxSandboxFilterPhase phases[] = {LinuxSandboxFilterPhase::Bootstrap,
                                            LinuxSandboxFilterPhase::Runtime};
  const zc::StringPtr expected[][2] = {
      {"98dbe2df91eedb4cd8c805e239558cde4deabd3ab0d37dbda75e1d6dad22466e"_zc,
       "0a3a652806873f8018139385f0d52614e45c744045560cb16dec84198a81b0e7"_zc},
      {"bc73026ef01827a882538ffd7405b1bbda5ff3ace08d15c06cbdbe80d9f4033b"_zc,
       "e5442959710b82f7cef71c704b98feb1855d5cc444f60815277a1adcaa70df4f"_zc},
  };
  for (size_t architectureIndex = 0; architectureIndex < zc::size(architectures);
       ++architectureIndex) {
    for (size_t phaseIndex = 0; phaseIndex < zc::size(phases); ++phaseIndex) {
      const auto architecture = architectures[architectureIndex];
      const auto phase = phases[phaseIndex];
      auto encoded = encodeLinuxSandboxFilter(generateLinuxSandboxFilter(architecture, phase));
      auto digest = identity::sha256(encoded);
      ZC_REQUIRE(digest != zc::none);
      ZC_IF_SOME(value, digest) {
        ZC_EXPECT(zc::encodeHex(value.bytes()) == expected[architectureIndex][phaseIndex]);
      }
    }
  }
}

}  // namespace zomlang::compiler::driver::package
