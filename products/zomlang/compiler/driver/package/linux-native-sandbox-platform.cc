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

#include "zomlang/compiler/driver/package/linux-native-sandbox.h"

#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))
#include <fcntl.h>
#include <linux/filter.h>
#include <linux/memfd.h>
#include <linux/sched.h>
#include <linux/seccomp.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstdio>

#include "zc/core/exception.h"
#include "zc/core/filesystem.h"
#include "zc/core/io.h"
#include "zomlang/compiler/driver/package/linux-sandbox-policy.h"

namespace zomlang::compiler::driver::package {
namespace {

constexpr int kRequestDescriptor = 3;
constexpr int kResponseDescriptor = 4;
constexpr int kInputDescriptor = 5;
constexpr int kOutputDescriptor = 6;
constexpr int kExecutableDescriptor = 7;

class NoThrowFd final {
public:
  NoThrowFd() noexcept = default;
  explicit NoThrowFd(int value) noexcept : value(value) {}
  ~NoThrowFd() noexcept { reset(); }
  NoThrowFd(NoThrowFd&& other) noexcept : value(other.release()) {}
  NoThrowFd& operator=(NoThrowFd&& other) noexcept {
    reset(other.release());
    return *this;
  }
  ZC_DISALLOW_COPY(NoThrowFd);

  int get() const noexcept { return value; }
  bool valid() const noexcept { return value >= 0; }
  int release() noexcept {
    const int result = value;
    value = -1;
    return result;
  }
  bool closeChecked() noexcept {
    if (!valid()) { return true; }
    const int descriptor = release();
    return close(descriptor) == 0;
  }
  void reset(int replacement = -1) noexcept {
    if (valid()) { (void)close(release()); }
    value = replacement;
  }

private:
  int value = -1;
};

class FixedFreshDirectory final : public FreshSourceDirectory {
public:
  FixedFreshDirectory(zc::Own<const zc::Directory>&& parent, zc::Path&& path,
                      zc::Own<const zc::Directory>&& root) noexcept
      : parent(zc::mv(parent)), path(zc::mv(path)), rootValue(zc::mv(root)) {}
  ~FixedFreshDirectory() noexcept override { (void)finish(); }

  const zc::Directory& root() const override { return *rootValue; }

  zc::Maybe<MaterializationIssue> finish() override {
    if (finished) { return zc::none; }
    finished = true;
    try {
      rootValue = nullptr;
      if (!parent->tryRemove(path)) { return MaterializationIssue::SnapshotCleanupFailed; }
      return zc::none;
    } catch (const zc::Exception&) { return MaterializationIssue::SnapshotCleanupFailed; }
  }

private:
  zc::Own<const zc::Directory> parent;
  zc::Path path;
  zc::Own<const zc::Directory> rootValue;
  bool finished = false;
};

class FixedFreshDirectoryFactory final : public FreshSourceDirectoryFactory {
public:
  FixedFreshDirectoryFactory(const zc::Directory& parent, zc::Path&& path)
      : parent(parent.clone()), path(zc::mv(path)) {}

  FreshSourceDirectoryResult create() override {
    if (used) { return MaterializationIssue::FreshDirectoryCreateFailed; }
    used = true;
    try {
      auto owner = parent->clone();
      auto root = owner->openSubdir(path, zc::WriteMode::CREATE | zc::WriteMode::PRIVATE);
      zc::Own<FreshSourceDirectory> result =
          zc::heap<FixedFreshDirectory>(zc::mv(owner), path.clone(), zc::mv(root));
      return zc::mv(result);
    } catch (const zc::Exception&) { return MaterializationIssue::FreshDirectoryCreateFailed; }
  }

private:
  zc::Own<const zc::Directory> parent;
  zc::Path path;
  bool used = false;
};

class SandboxTree final {
public:
  SandboxTree(zc::Own<const zc::Directory>&& parent, zc::Path&& path,
              zc::Own<const zc::Directory>&& root, zc::String&& absolutePath) noexcept
      : parent(zc::mv(parent)),
        path(zc::mv(path)),
        rootValue(zc::mv(root)),
        absolutePathValue(zc::mv(absolutePath)) {}
  ~SandboxTree() noexcept { (void)finish(); }
  ZC_DISALLOW_COPY_AND_MOVE(SandboxTree);

  const zc::Directory& root() const { return *rootValue; }
  zc::StringPtr absolutePath() const { return absolutePathValue; }
  bool finish() noexcept {
    if (finished) { return true; }
    try {
      rootValue = nullptr;
      if (!parent->tryRemove(path)) { return false; }
      finished = true;
      return true;
    } catch (...) { return false; }
  }

private:
  zc::Own<const zc::Directory> parent;
  zc::Path path;
  zc::Own<const zc::Directory> rootValue;
  zc::String absolutePathValue;
  bool finished = false;
};

zc::String joinPath(zc::StringPtr parent, zc::StringPtr child) {
  return parent == "/"_zc ? zc::str(parent, child) : zc::str(parent, "/"_zc, child);
}

bool validAbsoluteParent(zc::StringPtr path) {
  if (path.size() == 0 || path[0] != '/' || (path.size() > 1 && path[path.size() - 1] == '/')) {
    return false;
  }
  for (const auto byte : path.asBytes()) {
    if (byte == 0) { return false; }
  }
  return true;
}

bool writeAll(int descriptor, zc::ArrayPtr<const uint8_t> bytes) {
  size_t offset = 0;
  while (offset < bytes.size()) {
    const ssize_t count = write(descriptor, bytes.begin() + offset, bytes.size() - offset);
    if (count > 0) {
      offset += static_cast<size_t>(count);
      continue;
    }
    if (count < 0 && errno == EINTR) { continue; }
    return false;
  }
  return true;
}

bool writeTextFile(zc::StringPtr path, zc::StringPtr value) {
  NoThrowFd descriptor(open(path.cStr(), O_WRONLY | O_CLOEXEC));
  return descriptor.valid() && writeAll(descriptor.get(), value.asBytes()) &&
         descriptor.closeChecked();
}

bool readTextFile(zc::StringPtr path, zc::ArrayPtr<uint8_t> buffer, size_t& size) {
  NoThrowFd descriptor(open(path.cStr(), O_RDONLY | O_CLOEXEC));
  if (!descriptor.valid()) { return false; }
  size = 0;
  while (size < buffer.size()) {
    const ssize_t count = read(descriptor.get(), buffer.begin() + size, buffer.size() - size);
    if (count > 0) {
      size += static_cast<size_t>(count);
      continue;
    }
    if (count == 0) { return descriptor.closeChecked(); }
    if (errno == EINTR) { continue; }
    return false;
  }
  return false;
}

zc::Maybe<uint64_t> fieldValue(zc::ArrayPtr<const uint8_t> text, zc::StringPtr field) {
  for (size_t index = 0; index + field.size() < text.size(); ++index) {
    if (index != 0 && text[index - 1] != '\n') { continue; }
    bool match = true;
    for (size_t byte = 0; byte < field.size(); ++byte) {
      if (text[index + byte] != static_cast<uint8_t>(field[byte])) {
        match = false;
        break;
      }
    }
    if (!match || text[index + field.size()] != ' ') { continue; }
    uint64_t value = 0;
    size_t cursor = index + field.size() + 1;
    if (cursor == text.size() || text[cursor] < '0' || text[cursor] > '9') { return zc::none; }
    while (cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9') {
      const uint8_t digit = text[cursor] - '0';
      if (value > (UINT64_MAX - digit) / 10) { return zc::none; }
      value = value * 10 + digit;
      ++cursor;
    }
    return value;
  }
  return zc::none;
}

LinuxSandboxArchitecture hostArchitecture() {
#if defined(__x86_64__)
  return LinuxSandboxArchitecture::X86_64;
#else
  return LinuxSandboxArchitecture::AArch64;
#endif
}

zc::Array<sock_filter> nativeFilter(LinuxSandboxFilterPhase phase) {
  auto generated = generateLinuxSandboxFilter(hostArchitecture(), phase);
  zc::Vector<sock_filter> result(generated.size());
  for (const auto& instruction : generated) {
    result.add(sock_filter{instruction.code, instruction.jumpTrue, instruction.jumpFalse,
                           instruction.operand});
  }
  return result.releaseAsArray();
}

bool duplicateFixed(int source, int target) {
  const int safe = fcntl(source, F_DUPFD_CLOEXEC, 8);
  if (safe < 0) { return false; }
  const bool success = dup3(safe, target, 0) == target;
  (void)close(safe);
  return success;
}

bool duplicateFixedCloseOnExec(int source, int target) {
  const int safe = fcntl(source, F_DUPFD_CLOEXEC, 9);
  if (safe < 0) { return false; }
  const bool success = dup3(safe, target, O_CLOEXEC) == target;
  (void)close(safe);
  return success;
}

[[noreturn]] void childFailureOn(int descriptor) {
  const uint8_t failure = 1;
  (void)write(descriptor, &failure, 1);
  _exit(127);
}

[[noreturn]] void childFailure() { childFailureOn(8); }

class NativeLinuxSandboxPlatform final : public LinuxNativeSandboxPlatform {
public:
  NativeLinuxSandboxPlatform(VerifiedBuildScriptExecutable&& executable,
                             zc::Own<DigestVerifiedSourceSnapshot>&& inputCopy,
                             zc::Own<SandboxTree>&& tree, zc::String&& cgroupPath,
                             zc::Own<FreshSourceDirectoryFactory>&& outputSnapshotFactory)
      : executable(zc::mv(executable)),
        inputCopy(zc::mv(inputCopy)),
        tree(zc::mv(tree)),
        cgroupPath(zc::mv(cgroupPath)),
        outputSnapshotFactory(zc::mv(outputSnapshotFactory)),
        bootstrapFilter(nativeFilter(LinuxSandboxFilterPhase::Bootstrap)),
        inputPath(joinPath(this->tree->absolutePath(), "input"_zc)),
        outputPath(joinPath(this->tree->absolutePath(), "output"_zc)),
        rootPath(joinPath(this->tree->absolutePath(), "rootfs"_zc)),
        rootInputPath(joinPath(rootPath, "input"_zc)),
        rootOutputPath(joinPath(rootPath, "out"_zc)),
        rootOldPath(joinPath(rootPath, ".old"_zc)) {}

  ~NativeLinuxSandboxPlatform() noexcept override {
    (void)killAndReapChild();
    (void)closeNamespaceDescriptors();
    (void)removeCgroup();
    (void)removePrivateTrees();
  }
  ZC_DISALLOW_COPY_AND_MOVE(NativeLinuxSandboxPlatform);

  zc::Maybe<BuildScriptIssue> preflight() override { return preflightLinuxNativeSandboxHost(); }

  bool createNamespaces() override {
    namespaceStageReady = true;
    return true;
  }

  bool createPrivateTrees() override {
    if (!namespaceStageReady) { return false; }
    executableDescriptor.reset(static_cast<int>(
        syscall(SYS_memfd_create, "zom-build-script", MFD_CLOEXEC | MFD_ALLOW_SEALING)));
    if (!executableDescriptor.valid() ||
        !writeAll(executableDescriptor.get(), executable.bytes()) ||
        fchmod(executableDescriptor.get(), 0500) != 0 ||
        fcntl(executableDescriptor.get(), F_ADD_SEALS,
              F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL) != 0) {
      return false;
    }
    inputDescriptor.reset(open(inputPath.cStr(), O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    outputDescriptor.reset(
        open(outputPath.cStr(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    rootDescriptor.reset(open(rootPath.cStr(), O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    return inputDescriptor.valid() && outputDescriptor.valid() && rootDescriptor.valid();
  }

  bool createCgroup() override {
    if (mkdir(cgroupPath.cStr(), 0700) != 0) { return false; }
    cgroupOwned = true;
    return true;
  }

  bool applyResourceLimits(const BuildScriptLimitKey& limits) override {
    const auto& values = limits.values();
    limitValues = values;
    resourcePlan = linuxSandboxResourcePlan(limits);
    limitsReady = true;
    return writeTextFile(joinPath(cgroupPath, "memory.max"_zc), zc::str(values.memoryBytes)) &&
           writeTextFile(joinPath(cgroupPath, "pids.max"_zc), "1"_zc);
  }

  bool spawnWithSeccomp() override {
    if (!limitsReady || !namespaceStageReady || !cgroupOwned || !executableDescriptor.valid() ||
        !inputDescriptor.valid() || !outputDescriptor.valid() || !rootDescriptor.valid()) {
      return false;
    }
    int requestPipe[2] = {-1, -1};
    int responsePipe[2] = {-1, -1};
    int synchronizePipe[2] = {-1, -1};
    int setupPipe[2] = {-1, -1};
    if (pipe2(requestPipe, O_CLOEXEC) != 0 || pipe2(responsePipe, O_CLOEXEC) != 0 ||
        pipe2(synchronizePipe, O_CLOEXEC) != 0 || pipe2(setupPipe, O_CLOEXEC) != 0) {
      closePipe(requestPipe);
      closePipe(responsePipe);
      closePipe(synchronizePipe);
      closePipe(setupPipe);
      return false;
    }
    requestRead.reset(requestPipe[0]);
    requestWrite.reset(requestPipe[1]);
    responseRead.reset(responsePipe[0]);
    responseWrite.reset(responsePipe[1]);
    synchronizeRead.reset(synchronizePipe[0]);
    synchronizeWrite.reset(synchronizePipe[1]);
    setupRead.reset(setupPipe[0]);
    setupWrite.reset(setupPipe[1]);

    timerDescriptor.reset(timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK));
    if (!timerDescriptor.valid()) { return false; }
    int kernelPidfd = -1;
    struct clone_args arguments{};
    arguments.flags = CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWNET | CLONE_PIDFD;
    arguments.pidfd = reinterpret_cast<uint64_t>(&kernelPidfd);
    arguments.exit_signal = SIGCHLD;
    const pid_t result = static_cast<pid_t>(syscall(SYS_clone3, &arguments, sizeof(arguments)));
    if (result < 0) { return false; }
    if (result == 0) { runChild(); }
    childPid = result;
    childRunning = true;
    pidDescriptor.reset(kernelPidfd);
    requestRead.reset();
    responseWrite.reset();
    synchronizeRead.reset();
    setupWrite.reset();

    if (!configureUserNamespace() ||
        !writeTextFile(joinPath(cgroupPath, "cgroup.procs"_zc), zc::str(childPid)) ||
        !armWallTimer() || !writeAll(synchronizeWrite.get(), "1"_zc.asBytes()) ||
        !synchronizeWrite.closeChecked()) {
      (void)killAndReapChild();
      return false;
    }
    uint8_t setupFailure = 0;
    ssize_t setupCount;
    do {
      setupCount = read(setupRead.get(), &setupFailure, 1);
    } while (setupCount < 0 && errno == EINTR);
    if (!setupRead.closeChecked() || setupCount != 0) {
      (void)killAndReapChild();
      return false;
    }
    const int flags = fcntl(responseRead.get(), F_GETFL, 0);
    if (flags < 0 || fcntl(responseRead.get(), F_SETFL, flags | O_NONBLOCK) != 0) {
      (void)killAndReapChild();
      return false;
    }
    return true;
  }

  BuildScriptRunResult execute(const BuildScriptRequestFrame& request) override {
    if (!childRunning || !requestWrite.valid()) { return BuildScriptIssue::ExecutionFailed; }
    if (!writeAll(requestWrite.get(), request.bytes()) || !requestWrite.closeChecked()) {
      (void)killAndReapChild();
      return BuildScriptIssue::ExecutionFailed;
    }

    zc::Vector<uint8_t> response;
    bool responseClosed = false;
    bool wallExpired = false;
    bool responseOverflow = false;
    int childStatus = 0;
    while (childRunning || !responseClosed) {
      struct pollfd descriptors[3] = {
          {timerDescriptor.get(), POLLIN, 0},
          {pidDescriptor.get(), POLLIN, 0},
          {responseRead.get(), static_cast<short>(POLLIN | POLLHUP), 0},
      };
      if (poll(descriptors, 3, -1) < 0) {
        if (errno == EINTR) { continue; }
        (void)killAndReapChild();
        return BuildScriptIssue::ExecutionFailed;
      }
      if ((descriptors[0].revents & POLLIN) != 0) {
        wallExpired = true;
        uint64_t expirations = 0;
        (void)read(timerDescriptor.get(), &expirations, sizeof(expirations));
        (void)writeTextFile(joinPath(cgroupPath, "cgroup.kill"_zc), "1"_zc);
        if (childRunning) { (void)kill(childPid, SIGKILL); }
      }
      if ((descriptors[2].revents & (POLLIN | POLLHUP)) != 0) {
        uint8_t chunk[65536];
        while (true) {
          const ssize_t count = read(responseRead.get(), chunk, sizeof(chunk));
          if (count > 0) {
            const size_t countValue = static_cast<size_t>(count);
            const uint64_t maximumFrame = limitValues.responseFrameBytes + 8;
            if (countValue > maximumFrame || response.size() > maximumFrame - countValue) {
              responseOverflow = true;
            } else if (!responseOverflow) {
              response.addAll(zc::arrayPtr(chunk, countValue));
            }
            continue;
          }
          if (count == 0) {
            responseClosed = true;
          } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            responseClosed = true;
          }
          if (count < 0 && errno == EINTR) { continue; }
          break;
        }
      }
      if ((descriptors[1].revents & POLLIN) != 0 && childRunning) {
        if (waitpid(childPid, &childStatus, 0) != childPid) {
          (void)killAndReapChild();
          return BuildScriptIssue::ExecutionFailed;
        }
        childRunning = false;
        childPid = -1;
      }
      if (!childRunning && responseClosed) { break; }
    }
    requestWrite.reset();
    responseRead.reset();

    LinuxSandboxExitObservation observation;
    observation.wallTimerReadable = wallExpired;
    observation.cgroupMemoryEvent = memoryLimitObserved();
    observation.cpuLimitSignal = cpuLimitObserved(childStatus);
    observation.seccompTrap = WIFSIGNALED(childStatus) && WTERMSIG(childStatus) == SIGSYS;
    observation.childExitedSuccessfully = WIFEXITED(childStatus) && WEXITSTATUS(childStatus) == 0;
    zc::Maybe<BuildScriptResponse> decodedResponse;
    if (responseOverflow) {
      observation.responseIssue = BuildScriptIssue::ResponseFrameLimit;
    } else {
      auto decoded = decodeBuildScriptResponse(response, limitKey());
      if (decoded.is<BuildScriptIssue>()) {
        observation.responseIssue = decoded.get<BuildScriptIssue>();
      } else {
        decodedResponse = zc::mv(decoded.get<BuildScriptResponse>());
      }
    }
    ZC_IF_SOME(issue, classifyLinuxSandboxExit(observation)) { return issue; }
    if (decodedResponse == zc::none) { return BuildScriptIssue::MalformedResponse; }

    SourceAdmissionLimits outputLimits;
    outputLimits.fileCount = limitValues.fileCount;
    outputLimits.singleFileBytes = limitValues.outputBytes;
    outputLimits.totalFileBytes = limitValues.outputBytes;
    const int duplicate = fcntl(outputDescriptor.get(), F_DUPFD_CLOEXEC, 0);
    if (duplicate < 0) { return BuildScriptIssue::OutputTreePolicyViolation; }
    auto outputDirectory = zc::newDiskReadableDirectory(zc::OwnFd(duplicate));
    SourceDirectoryMaterializer materializer(outputLimits);
    auto snapshot = materializer.materialize(*outputDirectory, *outputSnapshotFactory);
    if (snapshot.is<MaterializationIssue>()) { return BuildScriptIssue::OutputTreePolicyViolation; }
    ZC_IF_SOME(responseValue, decodedResponse) {
      return VerifiedBuildScriptRun::from(zc::mv(snapshot.get<DigestVerifiedSourceSnapshot>()),
                                          zc::mv(responseValue));
    }
    return BuildScriptIssue::MalformedResponse;
  }

  bool killAndReapChild() noexcept override {
    bool success = true;
    if (childRunning && childPid > 0) {
      (void)writeTextFile(joinPath(cgroupPath, "cgroup.kill"_zc), "1"_zc);
      if (kill(childPid, SIGKILL) != 0 && errno != ESRCH) { success = false; }
      int status = 0;
      while (waitpid(childPid, &status, 0) < 0) {
        if (errno == EINTR) { continue; }
        if (errno != ECHILD) { success = false; }
        break;
      }
    }
    childRunning = false;
    childPid = -1;
    return success;
  }

  bool closeNamespaceDescriptors() noexcept override {
    bool success = true;
    NoThrowFd* descriptors[] = {
        &requestRead,      &requestWrite,         &responseRead,
        &responseWrite,    &synchronizeRead,      &synchronizeWrite,
        &setupRead,        &setupWrite,           &pidDescriptor,
        &timerDescriptor,  &executableDescriptor, &inputDescriptor,
        &outputDescriptor, &rootDescriptor,
    };
    for (auto descriptor : descriptors) {
      if (!descriptor->closeChecked()) { success = false; }
    }
    namespaceStageReady = false;
    return success;
  }

  bool removeCgroup() noexcept override {
    if (!cgroupOwned) { return true; }
    if (rmdir(cgroupPath.cStr()) != 0) { return false; }
    cgroupOwned = false;
    return true;
  }

  bool removePrivateTrees() noexcept override {
    bool success = true;
    if (inputCopy.get() != nullptr) {
      try {
        if (inputCopy->finish() != zc::none) { success = false; }
        inputCopy = nullptr;
      } catch (...) { success = false; }
    }
    if (tree.get() != nullptr && !tree->finish()) { success = false; }
    if (success) {
      tree = nullptr;
      try {
        outputSnapshotFactory = nullptr;
      } catch (...) { success = false; }
    }
    return success;
  }

private:
  static void closePipe(int descriptors[2]) noexcept {
    if (descriptors[0] >= 0) { (void)close(descriptors[0]); }
    if (descriptors[1] >= 0) { (void)close(descriptors[1]); }
    descriptors[0] = -1;
    descriptors[1] = -1;
  }

  bool configureUserNamespace() {
    const auto processRoot = zc::str("/proc/"_zc, childPid, "/"_zc);
    if (!writeTextFile(zc::str(processRoot, "setgroups"_zc), "deny"_zc)) { return false; }
    return writeTextFile(zc::str(processRoot, "uid_map"_zc), zc::str("0 "_zc, getuid(), " 1"_zc)) &&
           writeTextFile(zc::str(processRoot, "gid_map"_zc), zc::str("0 "_zc, getgid(), " 1"_zc));
  }

  bool armWallTimer() {
    struct timespec now{};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) { return false; }
    const uint64_t seconds = limitValues.wallMilliseconds / 1000;
    const uint64_t nanoseconds = (limitValues.wallMilliseconds % 1000) * 1000000;
    struct itimerspec timer{};
    timer.it_value.tv_sec = now.tv_sec + static_cast<time_t>(seconds);
    timer.it_value.tv_nsec = now.tv_nsec + static_cast<long>(nanoseconds);
    if (timer.it_value.tv_nsec >= 1000000000) {
      ++timer.it_value.tv_sec;
      timer.it_value.tv_nsec -= 1000000000;
    }
    return timerfd_settime(timerDescriptor.get(), TFD_TIMER_ABSTIME, &timer, nullptr) == 0;
  }

  [[noreturn]] void runChild() {
    synchronizeWrite.reset();
    setupRead.reset();
    if (!duplicateFixedCloseOnExec(setupWrite.get(), 8)) { childFailureOn(setupWrite.get()); }
    setupWrite.reset();
    requestWrite.reset();
    responseRead.reset();
    pidDescriptor.reset();
    timerDescriptor.reset();
    uint8_t ready = 0;
    while (true) {
      const ssize_t count = read(synchronizeRead.get(), &ready, 1);
      if (count == 1) { break; }
      if (count < 0 && errno == EINTR) { continue; }
      childFailure();
    }
    synchronizeRead.reset();

    if (mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr) != 0 ||
        mount("tmpfs", rootPath.cStr(), "tmpfs", MS_NOSUID | MS_NODEV | MS_NOEXEC,
              "mode=0700"_zc.cStr()) != 0 ||
        mkdir(rootInputPath.cStr(), 0500) != 0 || mkdir(rootOutputPath.cStr(), 0700) != 0 ||
        mkdir(rootOldPath.cStr(), 0700) != 0 ||
        mount(inputPath.cStr(), rootInputPath.cStr(), nullptr, MS_BIND | MS_REC, nullptr) != 0 ||
        mount(nullptr, rootInputPath.cStr(), nullptr,
              MS_BIND | MS_REMOUNT | MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC, nullptr) != 0 ||
        mount(outputPath.cStr(), rootOutputPath.cStr(), nullptr, MS_BIND | MS_REC, nullptr) != 0 ||
        mount(nullptr, rootOutputPath.cStr(), nullptr,
              MS_BIND | MS_REMOUNT | MS_NOSUID | MS_NODEV | MS_NOEXEC, nullptr) != 0 ||
        chdir(rootPath.cStr()) != 0 || syscall(SYS_pivot_root, ".", ".old") != 0 ||
        chdir("/") != 0 || umount2("/.old", MNT_DETACH) != 0 || rmdir("/.old") != 0) {
      childFailure();
    }

    NoThrowFd runtimeInput(open("/input", O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    NoThrowFd runtimeOutput(open("/out", O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!runtimeInput.valid() || !runtimeOutput.valid() ||
        !duplicateFixed(requestRead.get(), kRequestDescriptor) ||
        !duplicateFixed(responseWrite.get(), kResponseDescriptor) ||
        !duplicateFixed(runtimeInput.get(), kInputDescriptor) ||
        !duplicateFixed(runtimeOutput.get(), kOutputDescriptor) ||
        !duplicateFixed(executableDescriptor.get(), kExecutableDescriptor)) {
      childFailure();
    }
    (void)close(0);
    (void)close(1);
    (void)close(2);
    if (syscall(SYS_close_range, 9U, UINT_MAX, 0U) != 0) { childFailure(); }

    struct rlimit cpuLimit{resourcePlan.cpuSoftSeconds, resourcePlan.cpuHardSeconds};
    struct rlimit descriptorLimit{resourcePlan.fileDescriptorCount,
                                  resourcePlan.fileDescriptorCount};
    if (setrlimit(RLIMIT_CPU, &cpuLimit) != 0 || setrlimit(RLIMIT_NOFILE, &descriptorLimit) != 0 ||
        prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
      childFailure();
    }
    struct sock_fprog program{static_cast<unsigned short>(bootstrapFilter.size()),
                              bootstrapFilter.begin()};
    if (syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0, &program) != 0) { childFailure(); }
    char name[] = "zom-build-script";
    char* arguments[] = {name, nullptr};
    char* environment[] = {nullptr};
    (void)syscall(SYS_execveat, kExecutableDescriptor, "", arguments, environment, AT_EMPTY_PATH);
    childFailure();
  }

  bool memoryLimitObserved() const {
    uint8_t buffer[4096];
    size_t size = 0;
    if (!readTextFile(joinPath(cgroupPath, "memory.events"_zc), zc::arrayPtr(buffer), size)) {
      return false;
    }
    auto value = fieldValue(zc::arrayPtr(buffer, size), "oom_kill"_zc);
    ZC_IF_SOME(count, value) { return count != 0; }
    return false;
  }

  bool cpuLimitObserved(int status) const {
    if (!WIFSIGNALED(status)) { return false; }
    if (WTERMSIG(status) == SIGXCPU) { return true; }
    if (WTERMSIG(status) != SIGKILL) { return false; }
    uint8_t buffer[4096];
    size_t size = 0;
    if (!readTextFile(joinPath(cgroupPath, "cpu.stat"_zc), zc::arrayPtr(buffer), size)) {
      return false;
    }
    auto value = fieldValue(zc::arrayPtr(buffer, size), "usage_usec"_zc);
    ZC_IF_SOME(microseconds, value) {
      return microseconds >= resourcePlan.cpuSoftSeconds * 1000000;
    }
    return false;
  }

  BuildScriptLimitKey limitKey() const {
    auto verified = BuildScriptLimitKey::verify(limitValues);
    ZC_IREQUIRE(verified.is<BuildScriptLimitKey>(), "stored sandbox limits must remain valid");
    return zc::mv(verified.get<BuildScriptLimitKey>());
  }

  VerifiedBuildScriptExecutable executable;
  zc::Own<DigestVerifiedSourceSnapshot> inputCopy;
  zc::Own<SandboxTree> tree;
  zc::String cgroupPath;
  zc::Own<FreshSourceDirectoryFactory> outputSnapshotFactory;
  zc::Array<sock_filter> bootstrapFilter;
  zc::String inputPath;
  zc::String outputPath;
  zc::String rootPath;
  zc::String rootInputPath;
  zc::String rootOutputPath;
  zc::String rootOldPath;
  BuildScriptLimitValues limitValues{};
  LinuxSandboxResourcePlan resourcePlan{};
  bool namespaceStageReady = false;
  bool cgroupOwned = false;
  bool limitsReady = false;
  bool childRunning = false;
  pid_t childPid = -1;
  NoThrowFd requestRead;
  NoThrowFd requestWrite;
  NoThrowFd responseRead;
  NoThrowFd responseWrite;
  NoThrowFd synchronizeRead;
  NoThrowFd synchronizeWrite;
  NoThrowFd setupRead;
  NoThrowFd setupWrite;
  NoThrowFd pidDescriptor;
  NoThrowFd timerDescriptor;
  NoThrowFd executableDescriptor;
  NoThrowFd inputDescriptor;
  NoThrowFd outputDescriptor;
  NoThrowFd rootDescriptor;
};

}  // namespace
}  // namespace zomlang::compiler::driver::package
#endif

namespace zomlang::compiler::driver::package {

zc::OneOf<zc::Own<LinuxNativeSandboxPlatform>, BuildScriptIssue>
createProductionLinuxNativeSandboxPlatform(
    VerifiedBuildScriptExecutable&& executable, const DigestVerifiedSourceSnapshot& inputs,
    const zc::Directory& sandboxParent, zc::StringPtr sandboxParentAbsolutePath,
    identity::CanonicalPathSegment&& sandboxName, zc::StringPtr cgroupParentAbsolutePath,
    zc::Own<FreshSourceDirectoryFactory>&& outputSnapshotFactory) {
#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))
  ZC_IF_SOME(issue, preflightLinuxNativeSandboxHost()) { return issue; }
  if (!validAbsoluteParent(sandboxParentAbsolutePath) ||
      !validAbsoluteParent(cgroupParentAbsolutePath) ||
      access(cgroupParentAbsolutePath.cStr(), R_OK | W_OK) != 0 ||
      outputSnapshotFactory.get() == nullptr) {
    return BuildScriptIssue::SandboxSetupFailed;
  }
  auto parentOwner = sandboxParent.clone();
  const zc::Path leaf(sandboxName.text());
  zc::Own<const zc::Directory> root;
  try {
    root = parentOwner->openSubdir(leaf, zc::WriteMode::CREATE | zc::WriteMode::PRIVATE);
  } catch (const zc::Exception&) { return BuildScriptIssue::SandboxSetupFailed; }
  auto absolutePath = joinPath(sandboxParentAbsolutePath, sandboxName.text());
  auto tree =
      zc::heap<SandboxTree>(zc::mv(parentOwner), leaf.clone(), zc::mv(root), zc::mv(absolutePath));

  FixedFreshDirectoryFactory inputFactory(tree->root(), zc::Path("input"_zc));
  auto inputCopy = inputs.materializeVerifiedCopy(inputFactory);
  if (inputCopy.is<MaterializationIssue>()) { return BuildScriptIssue::SandboxSetupFailed; }
  try {
    auto output = tree->root().openSubdir(zc::Path("output"_zc),
                                          zc::WriteMode::CREATE | zc::WriteMode::PRIVATE);
    auto rootfs = tree->root().openSubdir(zc::Path("rootfs"_zc),
                                          zc::WriteMode::CREATE | zc::WriteMode::PRIVATE);
    (void)output;
    (void)rootfs;
  } catch (const zc::Exception&) { return BuildScriptIssue::SandboxSetupFailed; }
  auto cgroupPath = joinPath(cgroupParentAbsolutePath, sandboxName.text());
  zc::Own<LinuxNativeSandboxPlatform> platform = zc::heap<NativeLinuxSandboxPlatform>(
      zc::mv(executable), zc::mv(inputCopy.get<zc::Own<DigestVerifiedSourceSnapshot>>()),
      zc::mv(tree), zc::mv(cgroupPath), zc::mv(outputSnapshotFactory));
  return zc::mv(platform);
#else
  (void)executable;
  (void)inputs;
  (void)sandboxParent;
  (void)sandboxParentAbsolutePath;
  (void)sandboxName;
  (void)cgroupParentAbsolutePath;
  (void)outputSnapshotFactory;
  return BuildScriptIssue::SandboxUnavailable;
#endif
}

}  // namespace zomlang::compiler::driver::package
