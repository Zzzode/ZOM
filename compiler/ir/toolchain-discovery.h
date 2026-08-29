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

#include "compiler/ir/link-plan-codec.h"
#include "compiler/ir/target-registry.h"
#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/filesystem.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::ir {

/// \brief A directory capability bound to its canonical absolute identity.
///
/// RFC 0043 toolchain discovery must not take a directory capability and a
/// sysroot path string as two independent inputs: nothing would prove the bytes
/// read from the capability are the bytes named by the recorded path. A
/// `VerifiedSysroot` binds the two together - the open directory it reads from
/// AND the canonical absolute path every recorded input path is derived from -
/// so "the object read" and "the path recorded" cannot diverge by construction.
///
/// It is move-only and owns the open directory capability. The only constructor
/// is the validating `open` factory.
class VerifiedSysroot final {
public:
  VerifiedSysroot(VerifiedSysroot&&) noexcept = default;
  VerifiedSysroot& operator=(VerifiedSysroot&&) noexcept = default;
  ZC_DISALLOW_COPY(VerifiedSysroot);
  ~VerifiedSysroot() noexcept = default;

  /// \brief Opens the sysroot directory at `canonicalAbsolutePath` under `root`.
  ///
  /// \param root The filesystem root the canonical path is resolved under.
  /// \param canonicalAbsolutePath A normalized absolute path (begins with '/',
  ///        no '.'/'..'/empty segment); it is both opened and retained as the
  ///        identity every recorded input path derives from.
  /// \return The bound capability, or none when the path is not a normalized
  ///         absolute path or the directory cannot be opened.
  ZC_NODISCARD static zc::Maybe<VerifiedSysroot> open(const zc::ReadableDirectory& root,
                                                      zc::StringPtr canonicalAbsolutePath);

  /// \brief The canonical absolute identity every recorded path derives from.
  ZC_NODISCARD zc::StringPtr identity() const noexcept { return identityValue; }

  /// \brief The bound directory capability discovery reads inputs through.
  ZC_NODISCARD const zc::ReadableDirectory& directory() const noexcept { return *directoryValue; }

private:
  VerifiedSysroot(zc::String&& identity, zc::Own<const zc::ReadableDirectory>&& directory) noexcept
      : identityValue(zc::mv(identity)), directoryValue(zc::mv(directory)) {}

  zc::String identityValue;
  zc::Own<const zc::ReadableDirectory> directoryValue;
};

/// \brief One explicitly-supplied file the discovery step must resolve and digest.
///
/// RFC 0043 forbids ambient toolchain discovery: every input is named up front
/// with a role and a single path relative to the trusted sysroot. That one
/// relative path drives BOTH the read/digest and the recorded absolute path
/// (derived as sysroot + "/" + relativePath), so the digested bytes and the
/// path recorded into the closure always name the same object. No PATH search,
/// no environment probing, no implicit default sysroot, and no caller-supplied
/// independent recorded path.
struct ToolchainSearchInput final {
  /// The role this file plays in the produced closure. Only `CrtObject` and
  /// `DefaultLibrary` are accepted here; the linker driver is named separately.
  LinkInputRole role;

  /// The file's path relative to the sysroot. It is read and digested from the
  /// search root, and the recorded absolute path is derived from it.
  zc::String relativePath;
};

/// \brief The complete, explicit description of a toolchain to resolve.
///
/// This is supplied by the caller (a target authority), never inferred from the
/// host environment. `discover` reads only what this names through the
/// `VerifiedSysroot` capability, and every recorded absolute path is derived
/// from that capability's identity plus a relative path, never supplied
/// independently.
struct ToolchainSearchSpec final {
  /// Canonical target specification identity bytes (non-empty).
  zc::Array<uint8_t> targetSpecificationIdentity;

  /// The single driver family for the target object format.
  LinkerDriverKind linkerKind;

  /// The linker driver program's path relative to the sysroot. It is read and
  /// digested through the `VerifiedSysroot` capability; the recorded absolute
  /// path the spawn step executes is derived as `sysroot.identity() + "/" + this`.
  zc::String linkerRelativePath;

  /// Ordered startup/finalization objects and default libraries to resolve.
  zc::Array<ToolchainSearchInput> inputs;
};

/// \brief The closed reason a toolchain discovery attempt failed.
///
/// Discovery is fail-closed: any missing or malformed input rejects the whole
/// attempt and produces no closure. Each reason names one concrete cause.
enum class ToolchainDiscoveryFailure : uint8_t {
  /// The spec named an empty target identity, sysroot, or linker path.
  MalformedSpec = 0x01,

  /// The linker driver program named by the spec was not found under the root.
  LinkerNotFound = 0x02,

  /// A CRT object or default library named by the spec was not found.
  InputNotFound = 0x03,

  /// A resolved file was empty (zero bytes); an input must have content to be
  /// digested and linked.
  EmptyInput = 0x04,

  /// A search input carried a role other than `CrtObject` or `DefaultLibrary`.
  InvalidInputRole = 0x05,

  /// The digest of a resolved file could not be computed.
  DigestFailed = 0x06,

  /// The validated fields did not satisfy `ToolchainClosureRecord::make` (for
  /// example a non-absolute recorded path). This is the final closed check.
  ClosureRejected = 0x07,
};

/// \brief The result of a discovery attempt: a validated closure or a reason.
class ToolchainDiscoveryResult final {
public:
  static ToolchainDiscoveryResult forClosure(ToolchainClosureRecord&& closure);
  static ToolchainDiscoveryResult forFailure(ToolchainDiscoveryFailure reason);

  ToolchainDiscoveryResult(ToolchainDiscoveryResult&&) noexcept = default;
  ToolchainDiscoveryResult& operator=(ToolchainDiscoveryResult&&) noexcept = default;
  ZC_DISALLOW_COPY(ToolchainDiscoveryResult);
  ~ToolchainDiscoveryResult() noexcept = default;

  /// \brief True when a validated closure was produced.
  ZC_NODISCARD bool ok() const noexcept { return closureValue != zc::none; }

  /// \brief The produced closure. Requires ok().
  ZC_NODISCARD const ToolchainClosureRecord& closure() const;

  /// \brief The failure reason. Requires !ok().
  ZC_NODISCARD ToolchainDiscoveryFailure failure() const noexcept { return failureValue; }

private:
  explicit ToolchainDiscoveryResult(ToolchainClosureRecord&& closure) noexcept
      : closureValue(zc::mv(closure)), failureValue(ToolchainDiscoveryFailure::MalformedSpec) {}
  explicit ToolchainDiscoveryResult(ToolchainDiscoveryFailure reason) noexcept
      : failureValue(reason) {}

  zc::Maybe<ToolchainClosureRecord> closureValue;
  ToolchainDiscoveryFailure failureValue;
};

/// \brief Resolves an explicitly-specified toolchain into a verified closure.
///
/// RFC 0043 "Toolchain Discovery": reads exactly the files named by `spec`
/// through the `sysroot` capability, computes each file's SHA-256 digest and
/// byte count, and assembles a validated `ToolchainClosureRecord`. It never
/// searches PATH, reads the environment, or falls back to an ambient default; a
/// host that does not supply the named files fails closed with the first
/// violated reason. Every recorded absolute path is derived from
/// `sysroot.identity()`, so the bytes read and the path recorded name the same
/// object by construction.
///
/// \param sysroot The verified sysroot capability: the directory read from AND
///        the canonical identity recorded paths derive from.
/// \param spec The complete, explicit toolchain description.
/// \return A validated closure, or the first violated discovery reason.
ZC_NODISCARD ToolchainDiscoveryResult discoverToolchain(const VerifiedSysroot& sysroot,
                                                        const ToolchainSearchSpec& spec);

/// \brief Independently re-checks that a discovered closure fits the host.
///
/// This is a second, independent gate over `discoverToolchain`: it verifies the
/// closure's linker driver kind matches the object format the host executes
/// (ELF driver for an ELF host), rejecting a closure that resolved cleanly but
/// targets a different format than the running host. It reads no filesystem and
/// spawns no process.
///
/// \param closure The discovered closure to check.
/// \param hostObjectFormat The object format the running host executes.
/// \return true when the closure's driver family matches the host format.
ZC_NODISCARD bool verifyClosureMatchesHostFormat(const ToolchainClosureRecord& closure,
                                                 ObjectFormat hostObjectFormat);

}  // namespace zomlang::compiler::ir
