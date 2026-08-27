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

#if defined(__linux__)
#include <fcntl.h>
#include <linux/filter.h>
#include <linux/magic.h>
#include <linux/openat2.h>
#include <linux/seccomp.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace zomlang::compiler::driver::package {

LinuxSandboxResourcePlan linuxSandboxResourcePlan(const BuildScriptLimitKey& limits) noexcept {
  const auto& values = limits.values();
  const uint64_t softSeconds = values.cpuMilliseconds / 1'000;
  return LinuxSandboxResourcePlan{softSeconds,
                                  softSeconds + 1,
                                  values.wallMilliseconds,
                                  values.memoryBytes,
                                  values.fileDescriptorCount,
                                  1};
}

zc::Maybe<BuildScriptIssue> classifyLinuxSandboxExit(
    const LinuxSandboxExitObservation& observation) noexcept {
  if (observation.wallTimerReadable) { return BuildScriptIssue::WallLimit; }
  if (observation.cgroupMemoryEvent) { return BuildScriptIssue::MemoryLimit; }
  if (observation.cpuLimitSignal) { return BuildScriptIssue::CpuLimit; }
  if (observation.seccompTrap) { return BuildScriptIssue::SeccompPolicyViolation; }
  ZC_IF_SOME(issue, observation.responseIssue) { return issue; }
  if (!observation.childExitedSuccessfully) { return BuildScriptIssue::ExecutionFailed; }
  return zc::none;
}

struct LinuxNativeSandbox::Impl final {
  explicit Impl(zc::Own<LinuxNativeSandboxPlatform>&& platform) noexcept
      : platform(zc::mv(platform)) {}
  ~Impl() noexcept { finish(false); }

  zc::Own<LinuxNativeSandboxPlatform> platform;
  LinuxNativeSandboxState state = LinuxNativeSandboxState::SettingUp;
  bool namespacesOwned = false;
  bool treesOwned = false;
  bool cgroupOwned = false;
  bool childOwned = false;

  zc::Maybe<BuildScriptIssue> finish(bool report) noexcept {
    bool failed = false;
    if (childOwned && platform->killAndReapChild()) {
      childOwned = false;
    } else if (childOwned) {
      if (report) { return BuildScriptIssue::SandboxTeardownFailed; }
      return zc::none;
    }
    if (namespacesOwned && platform->closeNamespaceDescriptors()) {
      namespacesOwned = false;
    } else if (namespacesOwned) {
      failed = true;
    }
    if (cgroupOwned && platform->removeCgroup()) {
      cgroupOwned = false;
    } else if (cgroupOwned) {
      failed = true;
    }
    if (treesOwned && platform->removePrivateTrees()) {
      treesOwned = false;
    } else if (treesOwned) {
      failed = true;
    }
    if (!childOwned && !namespacesOwned && !cgroupOwned && !treesOwned) {
      state = LinuxNativeSandboxState::Finished;
    }
    if (failed && report) { return BuildScriptIssue::SandboxTeardownFailed; }
    return zc::none;
  }
};

LinuxNativeSandbox::LinuxNativeSandbox(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}

LinuxNativeSandboxCreateResult LinuxNativeSandbox::create(
    zc::Own<LinuxNativeSandboxPlatform>&& platform, const BuildScriptLimitKey& limits) {
  auto state = zc::heap<Impl>(zc::mv(platform));
  ZC_IF_SOME(issue, state->platform->preflight()) { return issue; }
  if (!state->platform->createNamespaces()) { return BuildScriptIssue::SandboxSetupFailed; }
  state->namespacesOwned = true;
  if (!state->platform->createPrivateTrees()) {
    state->finish(false);
    return BuildScriptIssue::SandboxSetupFailed;
  }
  state->treesOwned = true;
  if (!state->platform->createCgroup()) {
    state->finish(false);
    return BuildScriptIssue::SandboxSetupFailed;
  }
  state->cgroupOwned = true;
  if (!state->platform->applyResourceLimits(limits) || !state->platform->spawnWithSeccomp()) {
    state->finish(false);
    return BuildScriptIssue::SandboxSetupFailed;
  }
  state->childOwned = true;
  state->state = LinuxNativeSandboxState::Running;
  zc::Own<BuildScriptSandboxAdapter> result = zc::heap<LinuxNativeSandbox>(zc::mv(state));
  return zc::mv(result);
}

LinuxNativeSandbox::~LinuxNativeSandbox() noexcept {
  if (impl.get() != nullptr) { impl->finish(false); }
}

LinuxNativeSandbox::LinuxNativeSandbox(LinuxNativeSandbox&&) noexcept = default;
LinuxNativeSandbox& LinuxNativeSandbox::operator=(LinuxNativeSandbox&&) noexcept = default;

BuildScriptRunResult LinuxNativeSandbox::execute(const BuildScriptRequestFrame& request) {
  if (impl->state != LinuxNativeSandboxState::Running || !impl->childOwned) {
    return BuildScriptIssue::ExecutionFailed;
  }
  auto result = impl->platform->execute(request);
  impl->childOwned = false;
  impl->state = LinuxNativeSandboxState::Exited;
  return result;
}

zc::Maybe<BuildScriptIssue> LinuxNativeSandbox::finish() { return impl->finish(true); }

LinuxNativeSandboxState LinuxNativeSandbox::state() const noexcept { return impl->state; }

bool linuxNativeSandboxHostSupported() noexcept {
#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))
  return true;
#else
  return false;
#endif
}

zc::Maybe<BuildScriptIssue> preflightLinuxNativeSandboxHost() noexcept {
#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))
  const char* namespacePaths[] = {"/proc/self/ns/user", "/proc/self/ns/mnt", "/proc/self/ns/pid",
                                  "/proc/self/ns/net"};
  for (const auto path : namespacePaths) {
    if (access(path, R_OK) != 0) { return BuildScriptIssue::SandboxUnavailable; }
  }

  struct statfs cgroupStatus{};
  if (statfs("/sys/fs/cgroup", &cgroupStatus) != 0 ||
      static_cast<uint64_t>(cgroupStatus.f_type) != static_cast<uint64_t>(CGROUP2_SUPER_MAGIC)) {
    return BuildScriptIssue::SandboxUnavailable;
  }

  errno = 0;
  (void)syscall(SYS_clone3, nullptr, 0);
  if (errno == ENOSYS) { return BuildScriptIssue::SandboxUnavailable; }

  uint32_t action = SECCOMP_RET_TRAP;
  if (syscall(SYS_seccomp, SECCOMP_GET_ACTION_AVAIL, 0, &action) != 0) {
    return BuildScriptIssue::SandboxUnavailable;
  }

  const int pidfd = static_cast<int>(syscall(SYS_pidfd_open, getpid(), 0));
  if (pidfd < 0) { return BuildScriptIssue::SandboxUnavailable; }
  if (close(pidfd) != 0) { return BuildScriptIssue::SandboxUnavailable; }

  const int timer = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
  if (timer < 0) { return BuildScriptIssue::SandboxUnavailable; }
  if (close(timer) != 0) { return BuildScriptIssue::SandboxUnavailable; }

  struct open_how how{};
  how.flags = O_RDONLY | O_CLOEXEC;
  errno = 0;
  const long openResult = syscall(SYS_openat2, AT_FDCWD, "", &how, sizeof(how));
  if (openResult >= 0) {
    if (close(static_cast<int>(openResult)) != 0) { return BuildScriptIssue::SandboxUnavailable; }
  } else if (errno == ENOSYS) {
    return BuildScriptIssue::SandboxUnavailable;
  }
  return zc::none;
#else
  return BuildScriptIssue::SandboxUnavailable;
#endif
}

}  // namespace zomlang::compiler::driver::package
