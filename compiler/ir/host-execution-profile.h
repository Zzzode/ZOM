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

#include "compiler/ir/target-registry.h"
#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::ir {

/// \brief One execution environment described by the five RFC 0043 host-execution
/// dimensions.
///
/// RFC 0043 "Host Execution": `zomc run` compares the artifact target with a host
/// execution profile constructed from the same target registry authority; the
/// operating system, CPU architecture, object format, pointer width, and required
/// execution ABI capabilities must all match. This value type models exactly
/// those five dimensions for one side of that comparison (the artifact side and
/// the host side share the shape). It has no public aggregate initializer; the
/// validating factory is the only way to build one.
///
/// ABI capabilities are the closed set of execution features an environment
/// provides (host) or requires (artifact); they are retained as a strictly
/// ascending set of opaque capability-name bytes so this value carries a
/// canonical, order-independent capability comparison.
class HostExecutionProfile final {
public:
  HostExecutionProfile(HostExecutionProfile&&) noexcept = default;
  HostExecutionProfile& operator=(HostExecutionProfile&&) noexcept = default;
  ZC_DISALLOW_COPY(HostExecutionProfile);
  ~HostExecutionProfile() noexcept = default;

  /// \brief Builds a validated execution profile.
  /// \param operatingSystem Non-empty canonical operating-system component.
  /// \param cpuArchitecture Non-empty canonical CPU architecture component.
  /// \param objectFormat The executable object format.
  /// \param pointerWidthBits The target pointer width in bits (non-zero).
  /// \param abiCapabilities A strictly ascending, duplicate-free set of ABI
  ///        capability names.
  /// \return none when a required component is empty, the pointer width is zero,
  ///         or the ABI capability set is not strictly ascending.
  ZC_NODISCARD static zc::Maybe<HostExecutionProfile> make(zc::StringPtr operatingSystem,
                                                           zc::StringPtr cpuArchitecture,
                                                           ObjectFormat objectFormat,
                                                           uint32_t pointerWidthBits,
                                                           zc::Array<zc::String>&& abiCapabilities);

  ZC_NODISCARD zc::StringPtr operatingSystem() const noexcept { return operatingSystemValue; }
  ZC_NODISCARD zc::StringPtr cpuArchitecture() const noexcept { return cpuArchitectureValue; }
  ZC_NODISCARD ObjectFormat objectFormat() const noexcept { return objectFormatValue; }
  ZC_NODISCARD uint32_t pointerWidthBits() const noexcept { return pointerWidthValue; }
  ZC_NODISCARD zc::ArrayPtr<const zc::String> abiCapabilities() const noexcept {
    return abiCapabilityValues.asPtr();
  }

  ZC_NODISCARD HostExecutionProfile clone() const;

private:
  HostExecutionProfile(zc::String&& operatingSystem, zc::String&& cpuArchitecture,
                       ObjectFormat objectFormat, uint32_t pointerWidthBits,
                       zc::Array<zc::String>&& abiCapabilities) noexcept
      : operatingSystemValue(zc::mv(operatingSystem)),
        cpuArchitectureValue(zc::mv(cpuArchitecture)),
        objectFormatValue(objectFormat),
        pointerWidthValue(pointerWidthBits),
        abiCapabilityValues(zc::mv(abiCapabilities)) {}

  zc::String operatingSystemValue;
  zc::String cpuArchitectureValue;
  ObjectFormat objectFormatValue;
  uint32_t pointerWidthValue;
  zc::Array<zc::String> abiCapabilityValues;
};

/// \brief The closed reason an artifact is incompatible with a host profile.
///
/// RFC 0043 "Host Execution": a mismatch on any of the five dimensions rejects
/// execution with the existing target-selection diagnostic and spawns no process.
/// This names the exact dimension that failed.
enum class HostMismatchReason : uint8_t {
  OperatingSystem = 0x01,
  CpuArchitecture = 0x02,
  ObjectFormatKind = 0x03,
  PointerWidth = 0x04,
  AbiCapability = 0x05,
};

/// \brief The closed result of a host-execution compatibility comparison.
///
/// Exactly one of `Compatible` (the artifact may be executed on the host) or a
/// mismatch naming the first violated dimension.
class HostCompatibility final {
public:
  ZC_NODISCARD static HostCompatibility compatible() noexcept {
    return HostCompatibility(true, HostMismatchReason::OperatingSystem);
  }
  ZC_NODISCARD static HostCompatibility mismatch(HostMismatchReason reason) noexcept {
    return HostCompatibility(false, reason);
  }

  ZC_NODISCARD bool isCompatible() const noexcept { return compatibleValue; }
  /// \brief The violated dimension; valid only when `!isCompatible()`.
  ZC_NODISCARD HostMismatchReason reason() const noexcept { return reasonValue; }

private:
  HostCompatibility(bool compatible, HostMismatchReason reason) noexcept
      : compatibleValue(compatible), reasonValue(reason) {}

  bool compatibleValue;
  HostMismatchReason reasonValue;
};

/// \brief Decides whether `artifact` may execute on `host`.
///
/// RFC 0043 "Host Execution": the operating system, CPU architecture, object
/// format, and pointer width must be equal, and the host must provide every ABI
/// capability the artifact requires (the host set is a superset of the artifact
/// set). The dimensions are checked in RFC declaration order, and the first
/// violated dimension is reported. This is a pure comparison; it spawns no
/// process, reads no filesystem, and never falls back to emulation.
ZC_NODISCARD HostCompatibility runCompatibility(const HostExecutionProfile& artifact,
                                                const HostExecutionProfile& host);

}  // namespace zomlang::compiler::ir
