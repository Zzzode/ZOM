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

#include "compiler/ir/ir-failure.h"
#include "compiler/ir/link-plan-codec.h"
#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/filesystem.h"

namespace zomlang::compiler::ir {

/// \brief A linked executable produced from a verified link plan.
///
/// Carries the bytes of the executable the linker driver wrote at the plan's
/// output path, read back after a successful, verified invocation. It is only
/// constructed by `linkExecutable` on the success path.
class VerifiedLinkedExecutable final {
public:
  VerifiedLinkedExecutable(VerifiedLinkedExecutable&&) noexcept = default;
  VerifiedLinkedExecutable& operator=(VerifiedLinkedExecutable&&) noexcept = default;
  ZC_DISALLOW_COPY(VerifiedLinkedExecutable);
  ~VerifiedLinkedExecutable() noexcept = default;

  explicit VerifiedLinkedExecutable(zc::Array<uint8_t>&& bytes) noexcept
      : bytesValue(zc::mv(bytes)) {}

  /// \brief The produced executable bytes.
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> bytes() const noexcept { return bytesValue.asPtr(); }

private:
  zc::Array<uint8_t> bytesValue;
};

/// \brief Invokes the linker driver for a verified plan and reads back the output.
///
/// RFC 0043 "Linker Driver Invocation": the InvokeLinker step. Its entire input
/// is the `VerifiedLinkPlan` plus the filesystem root the plan's absolute paths
/// resolve against - there is no forkable independent argv, working directory, or
/// output path. It:
///
///   1. re-verifies the driver program on disk still matches the closure's
///      recorded digest and byte count (rejecting a file replaced after
///      discovery) before spawning;
///   2. rejects a pre-existing file at the plan's output path (no stale output
///      is ever accepted as a link result);
///   3. expands the plan to the canonical argv and spawns the driver through the
///      shell-free child-process primitive with an explicit environment;
///   4. on a spawn failure, a signal, a nonzero exit, or a missing/unchanged
///      output, removes any partial output it created and rejects; and
///   5. on success, reads back the produced executable and returns it.
///
/// Every rejection is an RFC 0010 `IrOperationResult` failure under
/// `IrFailurePhase::LinkerInvocation`; the driver-digest mismatch maps to
/// `InputRevisionMismatch` (the verified input changed), and every other linker
/// failure maps to `InvalidFact` (the linker produced no valid result).
///
/// \param plan The verified link plan; the sole source of argv, driver, and output.
/// \param filesystemRoot The directory the plan's normalized absolute paths
///        resolve against (the disk root in production).
/// \return The verified linked executable, or a LinkerInvocation-phase rejection.
ZC_NODISCARD IrOperationResult<VerifiedLinkedExecutable> linkExecutable(
    const VerifiedLinkPlan& plan, const zc::Directory& filesystemRoot);

}  // namespace zomlang::compiler::ir
