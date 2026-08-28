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

#pragma once

#include <stdint.h>

#include "zc/core/array.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"

ZC_BEGIN_HEADER

namespace zc {

// A generic, shell-free child-process primitive. This is the base mechanism a
// higher layer (for example the RFC 0043 link driver) builds on to invoke an
// external program with an explicit argument vector. It deliberately owns only
// the generic mechanism - argv/cwd/env/stdio/termination and a structured
// result - and knows nothing about linkers, compilers, or any specific tool.
//
// Design follows Rust's std::process::Command: the program is named separately
// from its arguments, arguments are never re-parsed, and no shell is ever
// interposed. There is intentionally no "run this string in a shell" entry
// point: quoting bugs and injection are impossible when the argument vector is
// passed to execv verbatim.

// How the child's environment is populated.
enum class SubprocessEnvPolicy : uint8_t {
  // The child inherits the parent's environment unchanged. Used when the caller
  // has no environment-hardening requirement.
  Inherit = 0,

  // The child sees only the variables explicitly added via
  // SubprocessCommand::env. The parent's environment is not inherited. This is
  // the allow-list mode a security-conscious driver selects.
  Empty = 1,
};

// Why a spawn attempt failed before the child produced any exit status. These
// are the frozen, exhaustive failure reasons a caller switches on.
enum class SubprocessSpawnFailure : uint8_t {
  // The executable named by the program path does not exist.
  ProgramNotFound = 0,

  // The program path exists but is not executable by this process.
  PermissionDenied = 1,

  // Any other spawn-time operating-system failure (fork/pipe/exec errno not one
  // of the above). The raw errno is carried alongside for diagnosis.
  SystemError = 2,
};

// How the child terminated.
enum class SubprocessTerminationKind : uint8_t {
  // The child ran to completion and returned an exit status; `code` is valid.
  Exited = 0,

  // The child was terminated by a signal; `signal` is valid.
  Signaled = 1,
};

// The structured, frozen result of a completed child process.
struct SubprocessOutput {
  // How the child terminated.
  SubprocessTerminationKind terminationKind;

  // The exit code, valid only when terminationKind == Exited. In [0, 255].
  int code;

  // The terminating signal number, valid only when terminationKind == Signaled.
  int signal;

  // The bytes the child wrote to stdout, truncated to the configured capture
  // cap. `stdoutTruncated` records whether truncation occurred.
  Array<byte> capturedStdout;
  bool stdoutTruncated;

  // The bytes the child wrote to stderr, truncated to the configured capture
  // cap. `stderrTruncated` records whether truncation occurred.
  Array<byte> capturedStderr;
  bool stderrTruncated;

  // Convenience: true when the child exited normally with code 0.
  bool succeeded() const {
    return terminationKind == SubprocessTerminationKind::Exited && code == 0;
  }
};

// The result of attempting to run a command: either a completed process's
// structured output, or a spawn failure that prevented the child from starting.
class SubprocessResult {
public:
  static SubprocessResult forOutput(SubprocessOutput&& output);
  static SubprocessResult forSpawnFailure(SubprocessSpawnFailure reason, int systemErrno);

  SubprocessResult(SubprocessResult&&) noexcept;
  SubprocessResult& operator=(SubprocessResult&&) noexcept;
  ZC_DISALLOW_COPY(SubprocessResult);
  ~SubprocessResult() noexcept(false);

  // True when the child was spawned and produced a termination status. When
  // false, the spawn failed and spawnFailure()/spawnErrno() are valid.
  bool spawned() const;

  // The completed child's structured output. Requires spawned().
  const SubprocessOutput& output() const;

  // The reason the spawn failed. Requires !spawned().
  SubprocessSpawnFailure spawnFailure() const;

  // The raw errno captured at spawn failure. Requires !spawned().
  int spawnErrno() const;

private:
  struct Impl;
  zc::Own<Impl> impl;

  explicit SubprocessResult(zc::Own<Impl> impl);
};

// A shell-free command to execute: a program path, an explicit argument vector,
// an optional working directory, and an environment policy with optional
// explicit variables. Configure it with the fluent setters, then call run().
class SubprocessCommand {
public:
  // Construct a command for `program`. `program` is the path passed to the exec
  // family verbatim; it is NOT searched through a shell. argv[0] defaults to
  // `program` and can be overridden with argv0().
  explicit SubprocessCommand(StringPtr program);

  SubprocessCommand(SubprocessCommand&&) noexcept;
  SubprocessCommand& operator=(SubprocessCommand&&) noexcept;
  ZC_DISALLOW_COPY(SubprocessCommand);
  ~SubprocessCommand() noexcept(false);

  // Append one argument to the child's argument vector (argv[1], argv[2], ...).
  // The value is passed through verbatim; no globbing, splitting, or variable
  // expansion is performed.
  SubprocessCommand& arg(StringPtr value);

  // Append several arguments in order.
  SubprocessCommand& args(ArrayPtr<const StringPtr> values);

  // Override argv[0], which otherwise equals `program`.
  SubprocessCommand& argv0(StringPtr value);

  // Run the child with this working directory. Defaults to the parent's.
  SubprocessCommand& cwd(StringPtr directory);

  // Select the environment policy (Inherit or Empty). Defaults to Inherit.
  SubprocessCommand& envPolicy(SubprocessEnvPolicy policy);

  // Add or override a single environment variable in the child. Under the Empty
  // policy this builds the allow-list; under Inherit it overrides one entry.
  SubprocessCommand& env(StringPtr name, StringPtr value);

  // Cap the number of stdout/stderr bytes captured. Output beyond the cap is
  // discarded and the corresponding truncated flag is set. Defaults to 1 MiB.
  SubprocessCommand& captureLimit(size_t bytes);

  // Spawn the child, feed it an empty stdin, capture stdout/stderr up to the
  // cap, wait for termination, and return the structured result. Never runs a
  // shell and never throws for an ordinary spawn failure - that is reported via
  // SubprocessResult::spawnFailure().
  SubprocessResult run();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace zc

ZC_END_HEADER
