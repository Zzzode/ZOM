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

#include "zc/core/vector.h"

namespace zomlang::compiler::driver::package {
namespace {

constexpr uint16_t kLoadWordAbsolute = 0x20;
constexpr uint16_t kJumpEqual = 0x15;
constexpr uint16_t kJumpGreater = 0x25;
constexpr uint16_t kJumpGreaterEqual = 0x35;
constexpr uint16_t kJumpBitsSet = 0x45;
constexpr uint16_t kReturn = 0x06;
constexpr uint32_t kSeccompAllow = 0x7fff0000;
constexpr uint32_t kSeccompTrap = 0x00030000;
constexpr uint32_t kAuditX86_64 = 0xc000003e;
constexpr uint32_t kAuditAArch64 = 0xc00000b7;
constexpr uint32_t kX32SyscallBit = 0x40000000;
constexpr uint32_t kSyscallOffset = 0;
constexpr uint32_t kArchitectureOffset = 4;
constexpr uint32_t kArgumentOffset = 16;

enum class ArgumentPolicy : uint8_t {
  None,
  Execveat,
  Seccomp,
  Read,
  Write,
  BootstrapWrite,
  Close,
  Fstat,
  Mmap,
  Mprotect,
  Openat2,
};

struct SyscallRule final {
  uint32_t number;
  ArgumentPolicy policy;
};

uint32_t argumentLowOffset(uint32_t index) { return kArgumentOffset + index * 8; }

void add(zc::Vector<LinuxSandboxBpfInstruction>& output, uint16_t code, uint8_t jumpTrue,
         uint8_t jumpFalse, uint32_t operand) {
  output.add(LinuxSandboxBpfInstruction{code, jumpTrue, jumpFalse, operand});
}

void trap(zc::Vector<LinuxSandboxBpfInstruction>& output) {
  add(output, kReturn, 0, 0, kSeccompTrap);
}

void allow(zc::Vector<LinuxSandboxBpfInstruction>& output) {
  add(output, kReturn, 0, 0, kSeccompAllow);
}

void requireEqual(zc::Vector<LinuxSandboxBpfInstruction>& output, uint32_t argument,
                  uint32_t value) {
  add(output, kLoadWordAbsolute, 0, 0, argumentLowOffset(argument));
  add(output, kJumpEqual, 1, 0, value);
  trap(output);
}

void requireEqualHigh(zc::Vector<LinuxSandboxBpfInstruction>& output, uint32_t argument,
                      uint32_t value) {
  add(output, kLoadWordAbsolute, 0, 0, argumentLowOffset(argument) + 4);
  add(output, kJumpEqual, 1, 0, value);
  trap(output);
}

void requireNoBits(zc::Vector<LinuxSandboxBpfInstruction>& output, uint32_t argument,
                   uint32_t bits) {
  add(output, kLoadWordAbsolute, 0, 0, argumentLowOffset(argument));
  add(output, kJumpBitsSet, 0, 1, bits);
  trap(output);
}

void requireValueOrRange(zc::Vector<LinuxSandboxBpfInstruction>& output, uint32_t argument,
                         uint32_t value, uint32_t minimum, uint32_t maximum) {
  add(output, kLoadWordAbsolute, 0, 0, argumentLowOffset(argument));
  add(output, kJumpEqual, 4, 0, value);
  add(output, kJumpGreaterEqual, 1, 0, minimum);
  trap(output);
  add(output, kJumpGreater, 0, 1, maximum);
  trap(output);
}

void requireThreeValues(zc::Vector<LinuxSandboxBpfInstruction>& output, uint32_t argument,
                        uint32_t first, uint32_t second, uint32_t third) {
  add(output, kLoadWordAbsolute, 0, 0, argumentLowOffset(argument));
  add(output, kJumpEqual, 3, 0, first);
  add(output, kJumpEqual, 2, 0, second);
  add(output, kJumpEqual, 1, 0, third);
  trap(output);
}

zc::Vector<LinuxSandboxBpfInstruction> policyBlock(ArgumentPolicy policy) {
  zc::Vector<LinuxSandboxBpfInstruction> result;
  switch (policy) {
    case ArgumentPolicy::None:
      break;
    case ArgumentPolicy::Execveat:
      requireEqual(result, 0, 7);
      requireEqual(result, 4, 0x1000);
      break;
    case ArgumentPolicy::Seccomp:
      requireEqual(result, 0, 1);
      requireEqual(result, 1, 0);
      break;
    case ArgumentPolicy::Read:
      requireValueOrRange(result, 0, 3, 0, 0);
      break;
    case ArgumentPolicy::Write:
      requireValueOrRange(result, 0, 4, 0, 0);
      break;
    case ArgumentPolicy::BootstrapWrite:
      requireThreeValues(result, 0, 0, 4, 8);
      break;
    case ArgumentPolicy::Close:
      requireValueOrRange(result, 0, 0, 3, 7);
      break;
    case ArgumentPolicy::Fstat:
      requireEqual(result, 0, 0);
      break;
    case ArgumentPolicy::Mmap:
      requireNoBits(result, 2, 0x4);
      requireEqual(result, 3, 0x22);
      requireEqual(result, 4, UINT32_MAX);
      requireEqualHigh(result, 4, UINT32_MAX);
      break;
    case ArgumentPolicy::Mprotect:
      requireNoBits(result, 2, 0x4);
      break;
    case ArgumentPolicy::Openat2:
      requireValueOrRange(result, 0, 5, 6, 6);
      break;
  }
  allow(result);
  return result;
}

zc::Array<SyscallRule> rules(LinuxSandboxArchitecture architecture, LinuxSandboxFilterPhase phase) {
  zc::Vector<SyscallRule> result;
  if (phase == LinuxSandboxFilterPhase::Bootstrap) {
    result.add(SyscallRule{architecture == LinuxSandboxArchitecture::X86_64 ? 322U : 281U,
                           ArgumentPolicy::Execveat});
    result.add(SyscallRule{architecture == LinuxSandboxArchitecture::X86_64 ? 317U : 277U,
                           ArgumentPolicy::Seccomp});
  }
  const uint32_t numbersX86[] = {0, 1, 3, 5, 9, 10, 11, 12, 14, 15, 60, 231, 437};
  const uint32_t numbersArm[] = {63, 64, 57, 80, 222, 226, 215, 214, 135, 139, 93, 94, 437};
  const ArgumentPolicy policies[] = {
      ArgumentPolicy::Read,    ArgumentPolicy::Write, ArgumentPolicy::Close,
      ArgumentPolicy::Fstat,   ArgumentPolicy::Mmap,  ArgumentPolicy::Mprotect,
      ArgumentPolicy::None,    ArgumentPolicy::None,  ArgumentPolicy::None,
      ArgumentPolicy::None,    ArgumentPolicy::None,  ArgumentPolicy::None,
      ArgumentPolicy::Openat2,
  };
  for (size_t index = 0; index < zc::size(policies); ++index) {
    auto policy = policies[index];
    if (phase == LinuxSandboxFilterPhase::Bootstrap && policy == ArgumentPolicy::Write) {
      policy = ArgumentPolicy::BootstrapWrite;
    }
    result.add(SyscallRule{
        architecture == LinuxSandboxArchitecture::X86_64 ? numbersX86[index] : numbersArm[index],
        policy});
  }
  return result.releaseAsArray();
}

void appendUint16(zc::Vector<uint8_t>& output, uint16_t value) {
  output.add(static_cast<uint8_t>(value >> 8U));
  output.add(static_cast<uint8_t>(value));
}

void appendUint32(zc::Vector<uint8_t>& output, uint32_t value) {
  output.add(static_cast<uint8_t>(value >> 24U));
  output.add(static_cast<uint8_t>(value >> 16U));
  output.add(static_cast<uint8_t>(value >> 8U));
  output.add(static_cast<uint8_t>(value));
}

}  // namespace

zc::Array<LinuxSandboxBpfInstruction> generateLinuxSandboxFilter(
    LinuxSandboxArchitecture architecture, LinuxSandboxFilterPhase phase) {
  zc::Vector<LinuxSandboxBpfInstruction> result;
  add(result, kLoadWordAbsolute, 0, 0, kArchitectureOffset);
  add(result, kJumpEqual, 1, 0,
      architecture == LinuxSandboxArchitecture::X86_64 ? kAuditX86_64 : kAuditAArch64);
  trap(result);
  add(result, kLoadWordAbsolute, 0, 0, kSyscallOffset);
  if (architecture == LinuxSandboxArchitecture::X86_64) {
    add(result, kJumpBitsSet, 0, 1, kX32SyscallBit);
    trap(result);
  }
  for (const auto& rule : rules(architecture, phase)) {
    auto block = policyBlock(rule.policy);
    ZC_IREQUIRE(block.size() <= UINT8_MAX,
                "one seccomp syscall policy must fit a classic BPF jump");
    add(result, kJumpEqual, 0, static_cast<uint8_t>(block.size()), rule.number);
    result.addAll(block);
  }
  trap(result);
  return result.releaseAsArray();
}

zc::Array<uint8_t> encodeLinuxSandboxFilter(
    zc::ArrayPtr<const LinuxSandboxBpfInstruction> instructions) {
  zc::Vector<uint8_t> result(instructions.size() * 8);
  for (const auto& instruction : instructions) {
    appendUint16(result, instruction.code);
    result.add(instruction.jumpTrue);
    result.add(instruction.jumpFalse);
    appendUint32(result, instruction.operand);
  }
  return result.releaseAsArray();
}

}  // namespace zomlang::compiler::driver::package
