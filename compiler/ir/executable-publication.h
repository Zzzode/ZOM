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
#include "zc/core/filesystem.h"
#include "zc/core/string.h"

namespace zomlang::compiler::ir {

/// \brief Checks that produced executable bytes carry the expected object-format
/// magic.
///
/// RFC 0043 "Executable Verification And Publication": the linker result is
/// accepted only after an independent verifier checks the output format; it does
/// not infer safety from a successful linker exit status. This is the first,
/// format-magic slice of that verifier: it matches the leading bytes against the
/// object format the plan targets (ELF: 0x7F 'E' 'L' 'F'; Mach-O: the 32/64-bit
/// little/big-endian magics). It reads no filesystem and spawns no process.
///
/// \param executableBytes The bytes the linker produced.
/// \param expectedFormat The object format the link plan targets.
/// \return true when the leading magic matches `expectedFormat`.
ZC_NODISCARD bool inspectExecutableFormat(zc::ArrayPtr<const uint8_t> executableBytes,
                                          ObjectFormat expectedFormat);

/// \brief The closed reason an executable publication attempt failed.
///
/// Publication is a transaction: any failure removes the temporary outputs and
/// leaves the final directory untouched. Each reason names one concrete cause.
enum class ExecutablePublicationFailure : uint8_t {
  /// The executable or manifest destination already exists; existing final paths
  /// are never replaced.
  DestinationExists = 0x01,

  /// A temporary output could not be written, synced, or renamed.
  WriteFailed = 0x02,
};

/// \brief The result of an executable publication attempt.
class ExecutablePublicationResult final {
public:
  static ExecutablePublicationResult success() noexcept {
    return ExecutablePublicationResult(true, ExecutablePublicationFailure::WriteFailed);
  }
  static ExecutablePublicationResult failure(ExecutablePublicationFailure reason) noexcept {
    return ExecutablePublicationResult(false, reason);
  }

  ZC_NODISCARD bool ok() const noexcept { return okValue; }
  /// \brief The failure reason; valid only when `!ok()`.
  ZC_NODISCARD ExecutablePublicationFailure failure() const noexcept { return failureValue; }

private:
  ExecutablePublicationResult(bool ok, ExecutablePublicationFailure reason) noexcept
      : okValue(ok), failureValue(reason) {}

  bool okValue;
  ExecutablePublicationFailure failureValue;
};

/// \brief Atomically publishes an executable and its `.zom-artifact` manifest.
///
/// RFC 0043 "Executable Verification And Publication": the executable and
/// manifest are written to sibling temporary names, synced, then renamed into
/// their final destinations; existing final paths are never replaced. A failure
/// removes all temporary files and leaves the final directory unchanged. Both
/// destinations are relative paths under `outputDir`.
///
/// This slice refuses if either final path already exists, writes both temp
/// files via the directory's atomic replace-with-commit, and commits the
/// executable then the manifest. It does not itself compute digests or build the
/// manifest bytes; the caller supplies the exact bytes to publish.
///
/// \param outputDir The final output directory.
/// \param executablePath The executable's path relative to `outputDir`.
/// \param manifestPath The manifest's path relative to `outputDir`.
/// \param executableBytes The exact executable bytes to publish.
/// \param manifestBytes The exact manifest bytes to publish.
/// \return success, or the first violated publication reason.
ZC_NODISCARD ExecutablePublicationResult publishExecutable(
    const zc::Directory& outputDir, zc::StringPtr executablePath, zc::StringPtr manifestPath,
    zc::ArrayPtr<const uint8_t> executableBytes, zc::ArrayPtr<const uint8_t> manifestBytes);

}  // namespace zomlang::compiler::ir
