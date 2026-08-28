// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
// Licensed under the MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "zc/core/subprocess.h"

#include "zc/core/debug.h"
#include "zc/core/io.h"
#include "zc/core/vector.h"

#if !_WIN32
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace zc {

// =======================================================================================
// SubprocessResult

struct SubprocessResult::Impl {
  bool spawned;

  // Valid when spawned.
  SubprocessOutput output;

  // Valid when !spawned.
  SubprocessSpawnFailure spawnFailure;
  int spawnErrno;
};

SubprocessResult::SubprocessResult(zc::Own<Impl> implParam) : impl(zc::mv(implParam)) {}

SubprocessResult SubprocessResult::forOutput(SubprocessOutput&& output) {
  auto impl = zc::heap<Impl>();
  impl->spawned = true;
  impl->output = zc::mv(output);
  impl->spawnFailure = SubprocessSpawnFailure::SystemError;
  impl->spawnErrno = 0;
  return SubprocessResult(zc::mv(impl));
}

SubprocessResult SubprocessResult::forSpawnFailure(SubprocessSpawnFailure reason, int systemErrno) {
  auto impl = zc::heap<Impl>();
  impl->spawned = false;
  impl->output = SubprocessOutput{};
  impl->spawnFailure = reason;
  impl->spawnErrno = systemErrno;
  return SubprocessResult(zc::mv(impl));
}

SubprocessResult::SubprocessResult(SubprocessResult&&) noexcept = default;
SubprocessResult& SubprocessResult::operator=(SubprocessResult&&) noexcept = default;
SubprocessResult::~SubprocessResult() noexcept(false) = default;

bool SubprocessResult::spawned() const { return impl->spawned; }

const SubprocessOutput& SubprocessResult::output() const {
  ZC_IREQUIRE(impl->spawned, "SubprocessResult::output() on a spawn failure");
  return impl->output;
}

SubprocessSpawnFailure SubprocessResult::spawnFailure() const {
  ZC_IREQUIRE(!impl->spawned, "SubprocessResult::spawnFailure() on a spawned process");
  return impl->spawnFailure;
}

int SubprocessResult::spawnErrno() const {
  ZC_IREQUIRE(!impl->spawned, "SubprocessResult::spawnErrno() on a spawned process");
  return impl->spawnErrno;
}

// =======================================================================================
// SubprocessCommand

namespace {

// The default stdout/stderr capture cap: 1 MiB per stream.
constexpr size_t kDefaultCaptureLimit = 1u << 20;

struct EnvEntry {
  String name;
  String value;
};

}  // namespace

struct SubprocessCommand::Impl {
  String program;
  Maybe<String> argv0Override;
  Vector<String> arguments;
  Maybe<String> workingDirectory;
  SubprocessEnvPolicy envPolicy = SubprocessEnvPolicy::Inherit;
  Vector<EnvEntry> envEntries;
  size_t captureLimit = kDefaultCaptureLimit;
};

SubprocessCommand::SubprocessCommand(StringPtr program) : impl(zc::heap<Impl>()) {
  impl->program = heapString(program);
}

SubprocessCommand::SubprocessCommand(SubprocessCommand&&) noexcept = default;
SubprocessCommand& SubprocessCommand::operator=(SubprocessCommand&&) noexcept = default;
SubprocessCommand::~SubprocessCommand() noexcept(false) = default;

SubprocessCommand& SubprocessCommand::arg(StringPtr value) {
  impl->arguments.add(heapString(value));
  return *this;
}

SubprocessCommand& SubprocessCommand::args(ArrayPtr<const StringPtr> values) {
  for (const StringPtr& value : values) { impl->arguments.add(heapString(value)); }
  return *this;
}

SubprocessCommand& SubprocessCommand::argv0(StringPtr value) {
  impl->argv0Override = heapString(value);
  return *this;
}

SubprocessCommand& SubprocessCommand::cwd(StringPtr directory) {
  impl->workingDirectory = heapString(directory);
  return *this;
}

SubprocessCommand& SubprocessCommand::envPolicy(SubprocessEnvPolicy policy) {
  impl->envPolicy = policy;
  return *this;
}

SubprocessCommand& SubprocessCommand::env(StringPtr name, StringPtr value) {
  impl->envEntries.add(EnvEntry{heapString(name), heapString(value)});
  return *this;
}

SubprocessCommand& SubprocessCommand::captureLimit(size_t bytes) {
  impl->captureLimit = bytes;
  return *this;
}

#if !_WIN32

namespace {

// Classify a spawn-time errno reported by the child through the exec-error pipe.
SubprocessSpawnFailure classifySpawnErrno(int error) {
  switch (error) {
    case ENOENT:
    case ENOTDIR:
      return SubprocessSpawnFailure::ProgramNotFound;
    case EACCES:
    case EPERM:
      return SubprocessSpawnFailure::PermissionDenied;
    default:
      return SubprocessSpawnFailure::SystemError;
  }
}

// A live capture buffer that drains one pipe read-end up to a cap.
struct CaptureBuffer {
  Vector<byte> bytes;
  bool truncated = false;
  size_t limit;

  explicit CaptureBuffer(size_t limit) : limit(limit) {}

  // Append up to `count` bytes from `src`, honoring the cap.
  void append(const byte* src, size_t count) {
    size_t room = limit > bytes.size() ? limit - bytes.size() : 0;
    size_t take = zc::min(room, count);
    for (size_t i = 0; i < take; ++i) { bytes.add(src[i]); }
    if (take < count) { truncated = true; }
  }

  Array<byte> release() { return bytes.releaseAsArray(); }
};

// Build a null-terminated argv array for execv. argv[0] is `argv0`, followed by
// each entry of `arguments`. The returned array owns the pointed-at C strings
// via `storage`.
Array<char*> buildArgv(StringPtr argv0, ArrayPtr<const String> arguments, Vector<String>& storage) {
  storage.add(heapString(argv0));
  for (const String& argument : arguments) { storage.add(heapString(argument)); }

  auto argv = heapArray<char*>(storage.size() + 1);
  for (size_t i = 0; i < storage.size(); ++i) { argv[i] = storage[i].begin(); }
  argv[storage.size()] = nullptr;
  return argv;
}

// Build a null-terminated envp array under the Empty policy from the explicit
// allow-list. The returned array owns the "NAME=VALUE" strings via `storage`.
Array<char*> buildEnvp(ArrayPtr<const EnvEntry> envEntries, Vector<String>& storage) {
  for (const EnvEntry& entry : envEntries) { storage.add(str(entry.name, "=", entry.value)); }
  auto envp = heapArray<char*>(storage.size() + 1);
  for (size_t i = 0; i < storage.size(); ++i) { envp[i] = storage[i].begin(); }
  envp[storage.size()] = nullptr;
  return envp;
}

// True when `name` is the variable name of the "NAME=VALUE" entry `entry`.
bool entryHasName(const char* entry, StringPtr name) {
  size_t i = 0;
  for (; i < name.size(); ++i) {
    if (entry[i] == '\0' || entry[i] != name[i]) { return false; }
  }
  return entry[i] == '=';
}

// Build a null-terminated envp under the Inherit policy: the parent environment
// with each explicit `env()` entry overriding a matching name or appended if
// new. The returned array owns the merged "NAME=VALUE" strings via `storage`.
Array<char*> buildInheritEnvp(ArrayPtr<const EnvEntry> envEntries, Vector<String>& storage) {
  // `environ` is the process environment declared by <unistd.h>.
  // Copy every inherited entry, substituting an override when its name matches.
  for (char** cursor = environ; cursor != nullptr && *cursor != nullptr; ++cursor) {
    bool overridden = false;
    for (const EnvEntry& entry : envEntries) {
      if (entryHasName(*cursor, entry.name)) {
        storage.add(str(entry.name, "=", entry.value));
        overridden = true;
        break;
      }
    }
    if (!overridden) { storage.add(heapString(*cursor)); }
  }
  // Append overrides whose name was not present in the parent environment.
  for (const EnvEntry& entry : envEntries) {
    bool present = false;
    for (char** cursor = environ; cursor != nullptr && *cursor != nullptr; ++cursor) {
      if (entryHasName(*cursor, entry.name)) {
        present = true;
        break;
      }
    }
    if (!present) { storage.add(str(entry.name, "=", entry.value)); }
  }
  auto envp = heapArray<char*>(storage.size() + 1);
  for (size_t i = 0; i < storage.size(); ++i) { envp[i] = storage[i].begin(); }
  envp[storage.size()] = nullptr;
  return envp;
}

// True when `text` contains an interior NUL, which would silently truncate a C
// string passed to the exec family.
bool containsInteriorNul(StringPtr text) {
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\0') { return true; }
  }
  return false;
}

}  // namespace

SubprocessResult SubprocessCommand::run() {
  // Fail closed on an interior NUL anywhere in the program, argv, cwd, or env:
  // the exec family takes C strings and would silently truncate at the NUL,
  // running a different program or passing a different argument than requested.
  if (containsInteriorNul(impl->program)) {
    return SubprocessResult::forSpawnFailure(SubprocessSpawnFailure::SystemError, EINVAL);
  }
  ZC_IF_SOME(value, impl->argv0Override) {
    if (containsInteriorNul(value)) {
      return SubprocessResult::forSpawnFailure(SubprocessSpawnFailure::SystemError, EINVAL);
    }
  }
  for (const String& argument : impl->arguments) {
    if (containsInteriorNul(argument)) {
      return SubprocessResult::forSpawnFailure(SubprocessSpawnFailure::SystemError, EINVAL);
    }
  }
  ZC_IF_SOME(value, impl->workingDirectory) {
    if (containsInteriorNul(value)) {
      return SubprocessResult::forSpawnFailure(SubprocessSpawnFailure::SystemError, EINVAL);
    }
  }
  for (const EnvEntry& entry : impl->envEntries) {
    if (containsInteriorNul(entry.name) || containsInteriorNul(entry.value)) {
      return SubprocessResult::forSpawnFailure(SubprocessSpawnFailure::SystemError, EINVAL);
    }
  }

  // Three pipes: child stdout, child stderr, and an exec-error channel the child
  // uses to report a failed exec (write-end is close-on-exec, so a successful
  // exec closes it silently and the parent reads EOF).
  int outPipe[2];
  int errPipe[2];
  int execPipe[2];
  ZC_SYSCALL(pipe(outPipe));
  ZC_SYSCALL(pipe(errPipe));
  ZC_SYSCALL(pipe(execPipe));

  // Mark the exec-error write end close-on-exec.
  ZC_SYSCALL(fcntl(execPipe[1], F_SETFD, FD_CLOEXEC));

  // Pre-build argv/envp before fork so no allocation happens in the child. The
  // child always execs through execve with an explicit environment: Empty uses
  // only the allow-list, Inherit merges the parent environment with the
  // overrides. So env() overrides take effect under both policies.
  StringPtr argv0 = impl->program;
  ZC_IF_SOME(override, impl->argv0Override) { argv0 = override; }
  Vector<String> argvStorage;
  Array<char*> argv = buildArgv(argv0, impl->arguments.asPtr(), argvStorage);
  Vector<String> envpStorage;
  Array<char*> envp = impl->envPolicy == SubprocessEnvPolicy::Empty
                          ? buildEnvp(impl->envEntries.asPtr(), envpStorage)
                          : buildInheritEnvp(impl->envEntries.asPtr(), envpStorage);

  const char* programPath = impl->program.cStr();
  const char* workingDir = nullptr;
  ZC_IF_SOME(directory, impl->workingDirectory) { workingDir = directory.cStr(); }

  pid_t pid;
  ZC_SYSCALL(pid = fork());

  if (pid == 0) {
    // Child. Only async-signal-safe operations from here to exec.
    ::close(outPipe[0]);
    ::close(errPipe[0]);
    ::close(execPipe[0]);

    auto reportAndDie = [&]() {
      int error = errno;
      ssize_t ignored = ::write(execPipe[1], &error, sizeof(error));
      (void)ignored;
      _exit(127);
    };

    // Redirect stdin from /dev/null, stdout/stderr to the pipes.
    int devNull = ::open("/dev/null", O_RDONLY);
    if (devNull < 0) { reportAndDie(); }
    if (::dup2(devNull, STDIN_FILENO) < 0) { reportAndDie(); }
    if (::dup2(outPipe[1], STDOUT_FILENO) < 0) { reportAndDie(); }
    if (::dup2(errPipe[1], STDERR_FILENO) < 0) { reportAndDie(); }
    ::close(devNull);
    ::close(outPipe[1]);
    ::close(errPipe[1]);

    if (workingDir != nullptr && ::chdir(workingDir) < 0) { reportAndDie(); }

    // Always exec with the explicit environment built above (Empty allow-list or
    // Inherit-merged-with-overrides), so env() overrides apply under both
    // policies.
    ::execve(programPath, argv.begin(), envp.begin());
    // Only reached if exec failed.
    reportAndDie();
    _exit(127);  // Unreachable, silences noreturn analysis.
  }

  // Parent.
  OwnFd outRead(outPipe[0]);
  OwnFd errRead(errPipe[0]);
  OwnFd execRead(execPipe[0]);
  ::close(outPipe[1]);
  ::close(errPipe[1]);
  ::close(execPipe[1]);

  // Read the exec-error channel first: any bytes here mean exec failed in the
  // child and no output/exit status is meaningful.
  int childExecErrno = 0;
  {
    ssize_t n = 0;
    size_t got = 0;
    while (got < sizeof(childExecErrno)) {
      n = ::read(execRead, reinterpret_cast<byte*>(&childExecErrno) + got,
                 sizeof(childExecErrno) - got);
      if (n <= 0) { break; }
      got += static_cast<size_t>(n);
    }
    if (got == 0) { childExecErrno = 0; }
  }

  if (childExecErrno != 0) {
    // Reap the short-lived child so it does not linger as a zombie.
    int status;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return SubprocessResult::forSpawnFailure(classifySpawnErrno(childExecErrno), childExecErrno);
  }

  // Drain stdout and stderr concurrently until both hit EOF, so a child that
  // fills one pipe while we read the other cannot deadlock us.
  CaptureBuffer outBuffer(impl->captureLimit);
  CaptureBuffer errBuffer(impl->captureLimit);
  bool outOpen = true;
  bool errOpen = true;
  byte scratch[4096];

  while (outOpen || errOpen) {
    struct pollfd fds[2];
    nfds_t nfds = 0;
    int outSlot = -1;
    int errSlot = -1;
    if (outOpen) {
      outSlot = static_cast<int>(nfds);
      fds[nfds].fd = outRead;
      fds[nfds].events = POLLIN;
      fds[nfds].revents = 0;
      ++nfds;
    }
    if (errOpen) {
      errSlot = static_cast<int>(nfds);
      fds[nfds].fd = errRead;
      fds[nfds].events = POLLIN;
      fds[nfds].revents = 0;
      ++nfds;
    }

    int ready = ::poll(fds, nfds, -1);
    if (ready < 0) {
      if (errno == EINTR) { continue; }
      ZC_FAIL_SYSCALL("poll", errno);
    }

    auto drain = [&](int slot, OwnFd& fd, CaptureBuffer& buffer, bool& open) {
      if (slot < 0) { return; }
      if ((fds[slot].revents & (POLLIN | POLLHUP | POLLERR)) == 0) { return; }
      ssize_t n = ::read(fd, scratch, sizeof(scratch));
      if (n > 0) {
        // Keep draining past the cap: bytes beyond the cap are discarded and the
        // truncated flag is set, but we never close the pipe on the child. The
        // capture cap must not alter the child's termination result (closing our
        // read end would deliver SIGPIPE and turn a valid child into a
        // DriverSignaled outcome). Bounding an unbounded producer is a separate
        // timeout/kill policy, not the capture cap's job.
        buffer.append(scratch, static_cast<size_t>(n));
      } else if (n == 0) {
        open = false;
      } else if (errno != EINTR && errno != EAGAIN) {
        open = false;
      }
    };

    drain(outSlot, outRead, outBuffer, outOpen);
    drain(errSlot, errRead, errBuffer, errOpen);
  }

  int status;
  while (::waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) { ZC_FAIL_SYSCALL("waitpid", errno); }
  }

  SubprocessOutput output{};
  if (WIFEXITED(status)) {
    output.terminationKind = SubprocessTerminationKind::Exited;
    output.code = WEXITSTATUS(status);
    output.signal = 0;
  } else if (WIFSIGNALED(status)) {
    output.terminationKind = SubprocessTerminationKind::Signaled;
    output.code = 0;
    output.signal = WTERMSIG(status);
  } else {
    // Neither exited nor signaled (should not happen for a blocking waitpid
    // without WUNTRACED/WCONTINUED); record as a zero exit to stay total.
    output.terminationKind = SubprocessTerminationKind::Exited;
    output.code = 0;
    output.signal = 0;
  }
  output.stdoutTruncated = outBuffer.truncated;
  output.stderrTruncated = errBuffer.truncated;
  output.capturedStdout = outBuffer.release();
  output.capturedStderr = errBuffer.release();

  return SubprocessResult::forOutput(zc::mv(output));
}

#else  // _WIN32

SubprocessResult SubprocessCommand::run() {
  ZC_UNIMPLEMENTED("SubprocessCommand::run is not yet implemented on Windows");
}

#endif  // !_WIN32

}  // namespace zc
