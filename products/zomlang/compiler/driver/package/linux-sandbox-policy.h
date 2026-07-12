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

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"

namespace zomlang::compiler::driver::package {

enum class LinuxSandboxArchitecture : uint8_t { X86_64 = 0x01, AArch64 = 0x02 };
enum class LinuxSandboxFilterPhase : uint8_t { Bootstrap = 0x01, Runtime = 0x02 };

/// \brief Architecture-neutral representation of one classic seccomp BPF instruction.
struct LinuxSandboxBpfInstruction final {
  uint16_t code;
  uint8_t jumpTrue;
  uint8_t jumpFalse;
  uint32_t operand;
};

/// \brief Generates the exact default-trap LinuxNativeSandboxV1 seccomp program.
ZC_NODISCARD zc::Array<LinuxSandboxBpfInstruction> generateLinuxSandboxFilter(
    LinuxSandboxArchitecture architecture, LinuxSandboxFilterPhase phase);

/// \brief Canonically encodes generated instructions for checked-in drift hashes.
ZC_NODISCARD zc::Array<uint8_t> encodeLinuxSandboxFilter(
    zc::ArrayPtr<const LinuxSandboxBpfInstruction> instructions);

}  // namespace zomlang::compiler::driver::package
