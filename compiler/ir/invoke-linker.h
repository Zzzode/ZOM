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
#include "zc/core/string.h"

namespace zomlang::compiler::ir {

/// \brief A transaction-private, re-verified snapshot of every link input plus
///        the rewritten driver invocation that consumes it.
///
/// RFC 0043 D3b defeats the input side of the link TOCTOU. `linkExecutable`
/// re-reads the driver by pathname just before spawning, but the driver then
/// re-opens each input (and, without this, itself) by pathname at exec time, so
/// an inode swapped between verification and exec would be linked. This
/// capability closes that window:
///
///   1. Every input the plan names - the driver, the closure CRT objects and
///      default libraries, and the plan's object and runtime records - is read
///      from the source filesystem, verified against the plan's recorded digest
///      and byte count, and copied into a fresh, process-private snapshot
///      directory whose per-file names are derived from the input's canonical
///      role and index (never a source basename, which could collide across
///      distinct source paths).
///   2. After every input is snapshotted, each snapshot copy is re-opened and
///      re-verified against the plan from the snapshot handle, proving the bytes
///      that were written are the bytes the plan proved.
///   3. Only then is the rewritten argument vector built once, pointing every
///      input token at its snapshot path, and the driver snapshot re-opened
///      read-only for execution by descriptor. There is never a half-built
///      invocation: the factory returns this capability only after all inputs
///      verify, or a rejection with the private tree already removed.
///
/// The driver snapshot's descriptor is borrowed from a `ReadableFile` this
/// object owns, so the owner must outlive the `SubprocessCommand::run` that
/// execs it. The destructor removes the entire private snapshot tree; on the
/// success path the caller keeps this object alive across the spawn so the
/// descriptor stays valid, and the tree is removed once the linker process has
/// been awaited and this object is dropped.
class PreparedLinkInputs final {
public:
  PreparedLinkInputs(PreparedLinkInputs&&) noexcept = default;
  PreparedLinkInputs& operator=(PreparedLinkInputs&&) noexcept = default;
  ZC_DISALLOW_COPY(PreparedLinkInputs);
  ~PreparedLinkInputs() noexcept(false);

  /// \brief Snapshots and re-verifies every input, returning the prepared
  ///        capability or a LinkerInvocation-phase rejection.
  ///
  /// \param plan The verified link plan naming every input and the driver.
  /// \param filesystemRoot The directory the plan's absolute paths resolve
  ///        against (the disk root in production; must expose real descriptors).
  /// \return The prepared inputs, or a rejection with the private tree removed.
  ZC_NODISCARD static IrOperationResult<PreparedLinkInputs> prepare(
      const VerifiedLinkPlan& plan, const zc::Directory& filesystemRoot);

  /// \brief The borrowed descriptor of the driver snapshot, valid while this
  ///        object is alive. The exec targets exactly this open object.
  ZC_NODISCARD int driverDescriptor() const;

  /// \brief The driver snapshot's absolute path (the default `argv[0]`; not used
  ///        to resolve the executable, which runs by descriptor).
  ZC_NODISCARD zc::StringPtr program() const noexcept;

  /// \brief The rewritten argument vector; every input token names a snapshot
  ///        path. `argv[0]` is the driver snapshot path.
  ZC_NODISCARD zc::ArrayPtr<const zc::String> argv() const noexcept;

  /// \brief The working directory for the driver (the output file's parent).
  ZC_NODISCARD zc::StringPtr workingDirectory() const noexcept;

  /// \brief The explicit environment as flattened (name, value) pairs; empty in
  ///        the current closure shape.
  ZC_NODISCARD zc::ArrayPtr<const zc::String> environment() const noexcept;

private:
  struct Impl;
  zc::Own<Impl> impl;

  explicit PreparedLinkInputs(zc::Own<Impl> impl) noexcept;
};

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
///   1. snapshots and re-verifies every plan input (driver, closure CRT objects
///      and default libraries, object and runtime records) into a transaction-
///      private directory via `PreparedLinkInputs::prepare`, defeating the input
///      TOCTOU: the driver runs by the snapshot descriptor and every input token
///      names a snapshot path, so no source inode swapped after verification can
///      be linked;
///   2. rejects a pre-existing file at the plan's output path (no stale output
///      is ever accepted as a link result);
///   3. spawns the driver by descriptor through the shell-free child-process
///      primitive with an explicit empty environment;
///   4. on a spawn failure, a signal, a nonzero exit, or a missing/unchanged
///      output, removes any partial output it created and rejects; and
///   5. on success, reads back the produced executable and returns it.
///
/// Every rejection is an RFC 0010 `IrOperationResult` failure under
/// `IrFailurePhase::LinkerInvocation`; an input-revision change (a driver or
/// input whose bytes no longer match the plan) maps to `InputRevisionMismatch`,
/// and every other linker failure maps to `OutputCreationFailed` (capability) or
/// `InvalidFact`. The private snapshot tree is removed on every path.
///
/// \param plan The verified link plan; the sole source of inputs, driver, and output.
/// \param filesystemRoot The directory the plan's normalized absolute paths
///        resolve against (the disk root in production; must expose real
///        descriptors, so an in-memory root fails closed).
/// \return The verified linked executable, or a LinkerInvocation-phase rejection.
ZC_NODISCARD IrOperationResult<VerifiedLinkedExecutable> linkExecutable(
    const VerifiedLinkPlan& plan, const zc::Directory& filesystemRoot);

}  // namespace zomlang::compiler::ir
