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

#include "compiler/ir/linker-invocation.h"
#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/filesystem.h"
#include "zc/core/string.h"

namespace zomlang::compiler::ir {

/// \brief The closed reason a linker invocation failed to produce an executable.
///
/// RFC 0043 "Linker Driver Invocation": an unrecognized driver result, a nonzero
/// exit status, or a missing output rejects the operation. Each reason names one
/// concrete cause; a success carries no reason.
enum class LinkerInvocationFailure : uint8_t {
  /// The linker driver program could not be spawned (not found or not
  /// executable).
  DriverNotSpawnable = 0x01,

  /// The linker driver was terminated by a signal rather than exiting.
  DriverSignaled = 0x02,

  /// The linker driver exited with a nonzero status.
  DriverExitedNonZero = 0x03,

  /// The linker driver exited successfully but produced no output file at the
  /// planned path.
  OutputMissing = 0x04,
};

/// \brief The result of invoking a linker driver for a link plan.
///
/// On success it carries the bytes of the produced executable read back from the
/// output path. On failure it carries the closed reason plus, when the driver
/// ran, the captured exit code and stderr for diagnosis.
class LinkerInvocationResult final {
public:
  static LinkerInvocationResult forExecutable(zc::Array<uint8_t>&& executableBytes);
  static LinkerInvocationResult forFailure(LinkerInvocationFailure reason, int exitCode,
                                           zc::Array<uint8_t>&& capturedStderr);

  LinkerInvocationResult(LinkerInvocationResult&&) noexcept = default;
  LinkerInvocationResult& operator=(LinkerInvocationResult&&) noexcept = default;
  ZC_DISALLOW_COPY(LinkerInvocationResult);
  ~LinkerInvocationResult() noexcept = default;

  /// \brief True when the linker produced the planned executable.
  ZC_NODISCARD bool ok() const noexcept { return okValue; }

  /// \brief The produced executable bytes. Requires ok().
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> executableBytes() const noexcept {
    return executableBytesValue.asPtr();
  }

  /// \brief The failure reason. Requires !ok().
  ZC_NODISCARD LinkerInvocationFailure failure() const noexcept { return failureValue; }

  /// \brief The driver exit code, valid on a DriverExitedNonZero failure.
  ZC_NODISCARD int exitCode() const noexcept { return exitCodeValue; }

  /// \brief The captured driver stderr, valid on a failure where the driver ran.
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> capturedStderr() const noexcept {
    return capturedStderrValue.asPtr();
  }

private:
  LinkerInvocationResult(bool ok, zc::Array<uint8_t>&& executableBytes,
                         LinkerInvocationFailure failure, int exitCode,
                         zc::Array<uint8_t>&& capturedStderr) noexcept
      : okValue(ok),
        executableBytesValue(zc::mv(executableBytes)),
        failureValue(failure),
        exitCodeValue(exitCode),
        capturedStderrValue(zc::mv(capturedStderr)) {}

  bool okValue;
  zc::Array<uint8_t> executableBytesValue;
  LinkerInvocationFailure failureValue;
  int exitCodeValue;
  zc::Array<uint8_t> capturedStderrValue;
};

/// \brief Invokes a linker driver for a plan and reads back the produced output.
///
/// RFC 0043 "Linker Driver Invocation": spawns the invocation's driver program
/// with its exact argument vector through the shell-free child-process
/// primitive - no shell, no glob expansion, no inherited search variables - then
/// classifies the result. A spawn failure, a signal, a nonzero exit, or a
/// missing output rejects the operation. On success it reads the produced
/// executable at `outputRelativePath` under `outputDir` and returns its bytes.
///
/// This is the InvokeLinker step that binds the argv expansion (LinkerInvocation)
/// to the process primitive; it does not itself verify the executable format or
/// publish a manifest (those are separate slices). The environment is exactly
/// the invocation's explicit environment; the parent environment is never
/// inherited.
///
/// \param invocation The canonical driver invocation to run.
/// \param outputDir The directory the produced executable is read from.
/// \param outputRelativePath The executable's path relative to `outputDir`.
/// \return The produced executable bytes, or the first violated failure reason.
ZC_NODISCARD LinkerInvocationResult invokeLinker(const LinkerInvocation& invocation,
                                                 const zc::ReadableDirectory& outputDir,
                                                 zc::StringPtr outputRelativePath);

}  // namespace zomlang::compiler::ir
