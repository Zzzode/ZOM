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

#include "compiler/ir/host-execution-profile.h"

#include "zc/core/vector.h"

namespace zomlang::compiler::ir {
namespace {

// Orders two capability names by their raw bytes; returns <0, 0, or >0.
int compareBytes(zc::StringPtr left, zc::StringPtr right) {
  const auto leftBytes = left.asBytes();
  const auto rightBytes = right.asBytes();
  const size_t shared = leftBytes.size() < rightBytes.size() ? leftBytes.size() : rightBytes.size();
  for (size_t index = 0; index < shared; ++index) {
    if (leftBytes[index] != rightBytes[index]) {
      return leftBytes[index] < rightBytes[index] ? -1 : 1;
    }
  }
  if (leftBytes.size() != rightBytes.size()) {
    return leftBytes.size() < rightBytes.size() ? -1 : 1;
  }
  return 0;
}

// Splits a canonical `arch-vendor-os-env` triple on '-'. Returns none unless the
// triple has at least the arch, vendor, and os fields and no empty field, so a
// malformed triple never yields a partial parse.
zc::Maybe<zc::Vector<zc::String>> splitTriple(zc::StringPtr triple) {
  zc::Vector<zc::String> fields;
  const auto bytes = triple.asBytes();
  size_t start = 0;
  for (size_t index = 0; index <= bytes.size(); ++index) {
    if (index == bytes.size() || bytes[index] == '-') {
      if (index == start) { return zc::none; }  // empty field: malformed
      zc::Vector<char> field(index - start + 1);
      for (size_t byte = start; byte < index; ++byte) { field.add(static_cast<char>(bytes[byte])); }
      field.add('\0');
      fields.add(zc::str(zc::StringPtr(field.begin())));
      start = index + 1;
    }
  }
  if (fields.size() < 3) { return zc::none; }  // need at least arch-vendor-os
  return zc::mv(fields);
}

}  // namespace

zc::Maybe<HostExecutionProfile> HostExecutionProfile::make(
    zc::StringPtr operatingSystem, zc::StringPtr cpuArchitecture, ObjectFormat objectFormat,
    uint32_t pointerWidthBits, zc::Array<zc::String>&& abiCapabilities) {
  if (operatingSystem.size() == 0 || cpuArchitecture.size() == 0 || pointerWidthBits == 0) {
    return zc::none;
  }
  // The ABI capability set must be strictly ascending (sorted, duplicate-free).
  for (size_t index = 1; index < abiCapabilities.size(); ++index) {
    if (compareBytes(abiCapabilities[index - 1], abiCapabilities[index]) >= 0) { return zc::none; }
  }
  return HostExecutionProfile(zc::str(operatingSystem), zc::str(cpuArchitecture), objectFormat,
                              pointerWidthBits, zc::mv(abiCapabilities));
}

HostExecutionProfile HostExecutionProfile::clone() const {
  zc::Vector<zc::String> capabilities(abiCapabilityValues.size());
  for (const auto& capability : abiCapabilityValues) { capabilities.add(zc::str(capability)); }
  return HostExecutionProfile(zc::str(operatingSystemValue), zc::str(cpuArchitectureValue),
                              objectFormatValue, pointerWidthValue, capabilities.releaseAsArray());
}

HostCompatibility runCompatibility(const HostExecutionProfile& artifact,
                                   const HostExecutionProfile& host) {
  if (compareBytes(artifact.operatingSystem(), host.operatingSystem()) != 0) {
    return HostCompatibility::mismatch(HostMismatchReason::OperatingSystem);
  }
  if (compareBytes(artifact.cpuArchitecture(), host.cpuArchitecture()) != 0) {
    return HostCompatibility::mismatch(HostMismatchReason::CpuArchitecture);
  }
  if (artifact.objectFormat() != host.objectFormat()) {
    return HostCompatibility::mismatch(HostMismatchReason::ObjectFormatKind);
  }
  if (artifact.pointerWidthBits() != host.pointerWidthBits()) {
    return HostCompatibility::mismatch(HostMismatchReason::PointerWidth);
  }
  // The host must provide every ABI capability the artifact requires. Both sets
  // are strictly ascending, so a linear merge decides the superset relation.
  const auto required = artifact.abiCapabilities();
  const auto provided = host.abiCapabilities();
  size_t hostIndex = 0;
  for (const auto& capability : required) {
    while (hostIndex < provided.size() && compareBytes(provided[hostIndex], capability) < 0) {
      ++hostIndex;
    }
    if (hostIndex >= provided.size() || compareBytes(provided[hostIndex], capability) != 0) {
      return HostCompatibility::mismatch(HostMismatchReason::AbiCapability);
    }
    ++hostIndex;
  }
  return HostCompatibility::compatible();
}

zc::Maybe<HostExecutionProfile> artifactExecutionProfileFromInspection(
    zc::StringPtr triple, const ExecutableInspectionProfile& inspection) {
  auto fields = splitTriple(triple);
  ZC_IF_SOME(parts, fields) {
    const zc::StringPtr architecture = parts[0];
    // The operating system is the third canonical field (arch-vendor-os-env).
    const zc::StringPtr operatingSystem = parts[2];

    // The machine, object format, and pointer width come solely from the
    // artifact's own inspection facts; no host-derived value is substituted.
    const bool machineIsAArch64 = inspection.machine() == ExecutableMachine::AArch64;

    // The triple architecture must be a supported architecture AND consistent
    // with the inspected executable machine; any disagreement fails closed
    // rather than trusting one side over the other.
    bool architectureSupported = false;
    if (compareBytes(architecture, "x86_64"_zc) == 0) {
      architectureSupported = !machineIsAArch64;
    } else if (compareBytes(architecture, "aarch64"_zc) == 0) {
      architectureSupported = machineIsAArch64;
    }
    if (!architectureSupported) { return zc::none; }

    // Only operating systems this compiler can describe are accepted.
    if (compareBytes(operatingSystem, "linux"_zc) != 0 &&
        compareBytes(operatingSystem, "darwin"_zc) != 0) {
      return zc::none;
    }

    // The artifact requires no additional execution ABI capability in the
    // current scalar slice, so the capability set is empty by construction.
    return HostExecutionProfile::make(operatingSystem, architecture, inspection.objectFormat(),
                                      inspection.pointerWidthBits(), zc::Array<zc::String>());
  }
  return zc::none;
}

zc::Maybe<HostExecutionProfile> currentHostExecutionProfile() {
#if defined(__x86_64__)
  const zc::StringPtr architecture = "x86_64"_zc;
#elif defined(__aarch64__) || defined(__arm64__)
  const zc::StringPtr architecture = "aarch64"_zc;
#else
  return zc::none;  // unsupported host architecture: fail closed
#endif

#if defined(__linux__)
  const zc::StringPtr operatingSystem = "linux"_zc;
  const ObjectFormat objectFormat = ObjectFormat::Elf;
#elif defined(__APPLE__)
  const zc::StringPtr operatingSystem = "darwin"_zc;
  const ObjectFormat objectFormat = ObjectFormat::MachO;
#else
  return zc::none;  // unsupported host operating system: fail closed
#endif

  // The host advertises no execution ABI capability in the current scalar slice,
  // so the capability set is empty.
  return HostExecutionProfile::make(operatingSystem, architecture, objectFormat,
                                    static_cast<uint32_t>(sizeof(void*) * 8),
                                    zc::Array<zc::String>());
}

}  // namespace zomlang::compiler::ir
