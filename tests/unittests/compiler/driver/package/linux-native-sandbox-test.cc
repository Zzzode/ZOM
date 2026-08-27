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

#include "compiler/driver/package/linux-native-sandbox.h"

#include "zc/ztest/test.h"

namespace zomlang::compiler::driver::package {
namespace {

enum class FaultStep : uint8_t {
  None,
  Preflight,
  Namespace,
  Trees,
  Cgroup,
  Limits,
  Spawn,
  Kill,
  Descriptors,
  RemoveCgroup,
  RemoveTrees,
};

struct PlatformTrace final {
  FaultStep fault = FaultStep::None;
  uint32_t namespaces = 0;
  uint32_t trees = 0;
  uint32_t cgroups = 0;
  uint32_t limits = 0;
  uint32_t spawns = 0;
  uint32_t kills = 0;
  uint32_t descriptorCloses = 0;
  uint32_t cgroupRemovals = 0;
  uint32_t treeRemovals = 0;
};

class FakeLinuxPlatform final : public LinuxNativeSandboxPlatform {
public:
  explicit FakeLinuxPlatform(PlatformTrace& trace) noexcept : trace(trace) {}
  ~FakeLinuxPlatform() noexcept override = default;

  zc::Maybe<BuildScriptIssue> preflight() override {
    if (trace.fault == FaultStep::Preflight) { return BuildScriptIssue::SandboxUnavailable; }
    return zc::none;
  }
  bool createNamespaces() override {
    ++trace.namespaces;
    return trace.fault != FaultStep::Namespace;
  }
  bool createPrivateTrees() override {
    ++trace.trees;
    return trace.fault != FaultStep::Trees;
  }
  bool createCgroup() override {
    ++trace.cgroups;
    return trace.fault != FaultStep::Cgroup;
  }
  bool applyResourceLimits(const BuildScriptLimitKey&) override {
    ++trace.limits;
    return trace.fault != FaultStep::Limits;
  }
  bool spawnWithSeccomp() override {
    ++trace.spawns;
    return trace.fault != FaultStep::Spawn;
  }
  BuildScriptRunResult execute(const BuildScriptRequestFrame&) override {
    return BuildScriptIssue::ExecutionFailed;
  }
  bool killAndReapChild() noexcept override {
    ++trace.kills;
    return consume(FaultStep::Kill);
  }
  bool closeNamespaceDescriptors() noexcept override {
    ++trace.descriptorCloses;
    return consume(FaultStep::Descriptors);
  }
  bool removeCgroup() noexcept override {
    ++trace.cgroupRemovals;
    return consume(FaultStep::RemoveCgroup);
  }
  bool removePrivateTrees() noexcept override {
    ++trace.treeRemovals;
    return consume(FaultStep::RemoveTrees);
  }

private:
  bool consume(FaultStep step) noexcept {
    if (trace.fault != step) { return true; }
    trace.fault = FaultStep::None;
    return false;
  }
  PlatformTrace& trace;
};

BuildScriptLimitKey limits() {
  auto result = BuildScriptLimitKey::verify(BuildScriptLimitKey::defaults());
  if (result.is<BuildScriptLimitKey>()) { return zc::mv(result.get<BuildScriptLimitKey>()); }
  ZC_FAIL_REQUIRE("default sandbox limits were rejected");
}

LinuxNativeSandboxCreateResult create(PlatformTrace& trace) {
  zc::Own<LinuxNativeSandboxPlatform> platform = zc::heap<FakeLinuxPlatform>(trace);
  auto buildLimits = limits();
  return LinuxNativeSandbox::create(zc::mv(platform), buildLimits);
}

BuildScriptRequestFrame request() {
  zc::Vector<identity::CanonicalRelativePath> inputs;
  zc::Vector<BuildScriptEnvironmentValue> environment;
  zc::Vector<identity::CanonicalRelativePath> outputs;
  auto buildLimits = limits();
  auto result = BuildScriptRequestFrame::encode(zc::mv(inputs), zc::mv(environment),
                                                zc::mv(outputs), buildLimits);
  if (result.is<BuildScriptRequestFrame>()) {
    return zc::mv(result.get<BuildScriptRequestFrame>());
  }
  ZC_FAIL_REQUIRE("empty request frame fixture was rejected");
}

}  // namespace

ZC_TEST("LinuxNativeSandbox fails closed on unsupported build hosts") {
#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))
  ZC_EXPECT(linuxNativeSandboxHostSupported());
#else
  ZC_EXPECT(!linuxNativeSandboxHostSupported());
  ZC_EXPECT(preflightLinuxNativeSandboxHost() == BuildScriptIssue::SandboxUnavailable);
#endif
}

ZC_TEST("LinuxNativeSandbox derives exact rlimit and cgroup values") {
  auto buildLimits = limits();
  const auto plan = linuxSandboxResourcePlan(buildLimits);
  ZC_EXPECT(plan.cpuSoftSeconds == 60);
  ZC_EXPECT(plan.cpuHardSeconds == 61);
  ZC_EXPECT(plan.wallMilliseconds == 120'000);
  ZC_EXPECT(plan.memoryBytes == 512U * 1024U * 1024U);
  ZC_EXPECT(plan.fileDescriptorCount == 16);
  ZC_EXPECT(plan.processCount == 1);
}

ZC_TEST("LinuxNativeSandbox classifies post-reap evidence in fixed priority order") {
  LinuxSandboxExitObservation observation;
  observation.wallTimerReadable = true;
  observation.cgroupMemoryEvent = true;
  observation.cpuLimitSignal = true;
  observation.seccompTrap = true;
  observation.responseIssue = BuildScriptIssue::MalformedResponse;
  ZC_EXPECT(classifyLinuxSandboxExit(observation) == BuildScriptIssue::WallLimit);
  observation.wallTimerReadable = false;
  ZC_EXPECT(classifyLinuxSandboxExit(observation) == BuildScriptIssue::MemoryLimit);
  observation.cgroupMemoryEvent = false;
  ZC_EXPECT(classifyLinuxSandboxExit(observation) == BuildScriptIssue::CpuLimit);
  observation.cpuLimitSignal = false;
  ZC_EXPECT(classifyLinuxSandboxExit(observation) == BuildScriptIssue::SeccompPolicyViolation);
  observation.seccompTrap = false;
  ZC_EXPECT(classifyLinuxSandboxExit(observation) == BuildScriptIssue::MalformedResponse);
  observation.responseIssue = zc::none;
  ZC_EXPECT(classifyLinuxSandboxExit(observation) == BuildScriptIssue::ExecutionFailed);
  observation.childExitedSuccessfully = true;
  ZC_EXPECT(classifyLinuxSandboxExit(observation) == zc::none);
}

ZC_TEST("LinuxNativeSandbox cleans every completed partial setup boundary") {
  const FaultStep faults[] = {FaultStep::Namespace, FaultStep::Trees, FaultStep::Cgroup,
                              FaultStep::Limits, FaultStep::Spawn};
  for (const auto fault : faults) {
    PlatformTrace trace;
    trace.fault = fault;
    auto result = create(trace);
    ZC_REQUIRE(result.is<BuildScriptIssue>());
    ZC_EXPECT(result.get<BuildScriptIssue>() == BuildScriptIssue::SandboxSetupFailed);
    const bool namespacesAcquired = fault != FaultStep::Namespace;
    const bool treesAcquired = namespacesAcquired && fault != FaultStep::Trees;
    const bool cgroupAcquired = treesAcquired && fault != FaultStep::Cgroup;
    ZC_EXPECT(trace.descriptorCloses == (namespacesAcquired ? 1U : 0U));
    ZC_EXPECT(trace.treeRemovals == (treesAcquired ? 1U : 0U));
    ZC_EXPECT(trace.cgroupRemovals == (cgroupAcquired ? 1U : 0U));
  }
}

ZC_TEST("LinuxNativeSandbox retries only the owner that failed at every teardown boundary") {
  const FaultStep faults[] = {FaultStep::Kill, FaultStep::Descriptors, FaultStep::RemoveCgroup,
                              FaultStep::RemoveTrees};
  for (const auto fault : faults) {
    PlatformTrace trace;
    auto result = create(trace);
    ZC_REQUIRE(result.is<zc::Own<BuildScriptSandboxAdapter>>());
    auto sandbox = zc::mv(result.get<zc::Own<BuildScriptSandboxAdapter>>());

    trace.fault = fault;
    auto first = sandbox->finish();
    ZC_REQUIRE(first != zc::none);
    ZC_IF_SOME(issue, first) { ZC_EXPECT(issue == BuildScriptIssue::SandboxTeardownFailed); }
    ZC_EXPECT(trace.kills == 1);
    ZC_EXPECT(trace.descriptorCloses == (fault == FaultStep::Kill ? 0U : 1U));
    ZC_EXPECT(trace.cgroupRemovals == (fault == FaultStep::Kill ? 0U : 1U));
    ZC_EXPECT(trace.treeRemovals == (fault == FaultStep::Kill ? 0U : 1U));

    ZC_EXPECT(sandbox->finish() == zc::none);
    ZC_EXPECT(trace.kills == (fault == FaultStep::Kill ? 2U : 1U));
    ZC_EXPECT(trace.descriptorCloses == (fault == FaultStep::Descriptors ? 2U : 1U));
    ZC_EXPECT(trace.cgroupRemovals == (fault == FaultStep::RemoveCgroup ? 2U : 1U));
    ZC_EXPECT(trace.treeRemovals == (fault == FaultStep::RemoveTrees ? 2U : 1U));

    ZC_EXPECT(sandbox->finish() == zc::none);
    ZC_EXPECT(trace.kills == (fault == FaultStep::Kill ? 2U : 1U));
    ZC_EXPECT(trace.descriptorCloses == (fault == FaultStep::Descriptors ? 2U : 1U));
    ZC_EXPECT(trace.cgroupRemovals == (fault == FaultStep::RemoveCgroup ? 2U : 1U));
    ZC_EXPECT(trace.treeRemovals == (fault == FaultStep::RemoveTrees ? 2U : 1U));
  }
}

ZC_TEST("LinuxNativeSandbox records an exited child before resource teardown") {
  PlatformTrace trace;
  auto result = create(trace);
  ZC_REQUIRE(result.is<zc::Own<BuildScriptSandboxAdapter>>());
  auto sandbox = zc::mv(result.get<zc::Own<BuildScriptSandboxAdapter>>());
  auto run = sandbox->execute(request());
  ZC_REQUIRE(run.is<BuildScriptIssue>());
  ZC_EXPECT(run.get<BuildScriptIssue>() == BuildScriptIssue::ExecutionFailed);
  ZC_EXPECT(sandbox->finish() == zc::none);
  ZC_EXPECT(trace.kills == 0);
  ZC_EXPECT(trace.descriptorCloses == 1);
  ZC_EXPECT(trace.cgroupRemovals == 1);
  ZC_EXPECT(trace.treeRemovals == 1);
}

}  // namespace zomlang::compiler::driver::package
