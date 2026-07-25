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

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zomlang/compiler/driver/package/build-script-runtime.h"
#include "zomlang/compiler/driver/package/trusted-runtime-elf.h"

namespace zomlang::compiler::driver::package {

enum class LinuxNativeSandboxState : uint8_t {
  SettingUp = 0x01,
  Running = 0x02,
  Exited = 0x03,
  Finished = 0x04,
};

struct LinuxSandboxResourcePlan final {
  uint64_t cpuSoftSeconds;
  uint64_t cpuHardSeconds;
  uint64_t wallMilliseconds;
  uint64_t memoryBytes;
  uint32_t fileDescriptorCount;
  uint32_t processCount;
};

/// \brief Exact rlimit/cgroup values derived from an already verified limit key.
ZC_NODISCARD LinuxSandboxResourcePlan
linuxSandboxResourcePlan(const BuildScriptLimitKey& limits) noexcept;

struct LinuxSandboxExitObservation final {
  bool wallTimerReadable = false;
  bool cgroupMemoryEvent = false;
  bool cpuLimitSignal = false;
  bool seccompTrap = false;
  zc::Maybe<BuildScriptIssue> responseIssue;
  bool childExitedSuccessfully = false;
};

/// \brief Applies the RFC 0012 post-reap failure classification priority.
ZC_NODISCARD zc::Maybe<BuildScriptIssue> classifyLinuxSandboxExit(
    const LinuxSandboxExitObservation& observation) noexcept;

/// \brief OS boundary used by the sandbox state machine and fault-injection tests.
class LinuxNativeSandboxPlatform {
public:
  virtual ~LinuxNativeSandboxPlatform() noexcept = default;
  ZC_NODISCARD virtual zc::Maybe<BuildScriptIssue> preflight() = 0;
  ZC_NODISCARD virtual bool createNamespaces() = 0;
  ZC_NODISCARD virtual bool createPrivateTrees() = 0;
  ZC_NODISCARD virtual bool createCgroup() = 0;
  ZC_NODISCARD virtual bool applyResourceLimits(const BuildScriptLimitKey& limits) = 0;
  ZC_NODISCARD virtual bool spawnWithSeccomp() = 0;
  ZC_NODISCARD virtual BuildScriptRunResult execute(const BuildScriptRequestFrame& request) = 0;
  ZC_NODISCARD virtual bool killAndReapChild() noexcept = 0;
  ZC_NODISCARD virtual bool closeNamespaceDescriptors() noexcept = 0;
  ZC_NODISCARD virtual bool removeCgroup() noexcept = 0;
  ZC_NODISCARD virtual bool removePrivateTrees() noexcept = 0;
};

using LinuxNativeSandboxCreateResult =
    zc::OneOf<zc::Own<BuildScriptSandboxAdapter>, BuildScriptIssue>;

/// \brief Move-only Linux sandbox with explicit idempotent resource teardown.
class LinuxNativeSandbox final : public BuildScriptSandboxAdapter {
private:
  struct Impl;

public:
  ZC_NODISCARD static LinuxNativeSandboxCreateResult create(
      zc::Own<LinuxNativeSandboxPlatform>&& platform, const BuildScriptLimitKey& limits);
  ~LinuxNativeSandbox() noexcept override;
  LinuxNativeSandbox(LinuxNativeSandbox&&) noexcept;
  LinuxNativeSandbox& operator=(LinuxNativeSandbox&&) noexcept;
  ZC_DISALLOW_COPY(LinuxNativeSandbox);

  ZC_NODISCARD BuildScriptRunResult execute(const BuildScriptRequestFrame& request) override;
  ZC_NODISCARD zc::Maybe<BuildScriptIssue> finish() override;
  ZC_NODISCARD LinuxNativeSandboxState state() const noexcept;

  explicit LinuxNativeSandbox(zc::Own<Impl>&& impl) noexcept;

private:
  zc::Own<Impl> impl;
};

/// \brief Returns whether the current build host can ever admit LinuxNativeSandbox.
ZC_NODISCARD bool linuxNativeSandboxHostSupported() noexcept;
/// \brief Verifies every required host-kernel primitive without acquiring sandbox resources.
ZC_NODISCARD zc::Maybe<BuildScriptIssue> preflightLinuxNativeSandboxHost() noexcept;

/// \brief Creates the production Linux platform from verified image and source capabilities.
/// \param executable Digest- and target-verified static PIE image.
/// \param inputs Owning digest-verified input snapshot copied into the private tree.
/// \param sandboxParent Compiler-owned parent for private run directories.
/// \param sandboxParentAbsolutePath Absolute OS path naming sandboxParent.
/// \param sandboxName Unique canonical leaf owned by this sandbox instance.
/// \param cgroupParentAbsolutePath Writable cgroup v2 parent delegated to the compiler.
/// \param outputSnapshotFactory Factory receiving the final two-pass verified output snapshot.
/// \return Production platform or a fail-closed setup issue.
ZC_NODISCARD zc::OneOf<zc::Own<LinuxNativeSandboxPlatform>, BuildScriptIssue>
createProductionLinuxNativeSandboxPlatform(
    VerifiedBuildScriptExecutable&& executable, const DigestVerifiedSourceSnapshot& inputs,
    const zc::Directory& sandboxParent, zc::StringPtr sandboxParentAbsolutePath,
    identity::CanonicalPathSegment&& sandboxName, zc::StringPtr cgroupParentAbsolutePath,
    zc::Own<FreshSourceDirectoryFactory>&& outputSnapshotFactory);

}  // namespace zomlang::compiler::driver::package
