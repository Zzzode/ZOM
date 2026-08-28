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

}  // namespace zomlang::compiler::ir
