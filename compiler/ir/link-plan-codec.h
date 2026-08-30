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

#include "compiler/identity/crypto/sha256.h"
#include "compiler/ir/ir-failure.h"
#include "compiler/ir/target-registry.h"
#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::ir {

/// \brief The linker driver family a toolchain closure binds.
///
/// RFC 0043 "Linker Driver Invocation": the selected target authority names
/// exactly one driver alternative per object format.
enum class LinkerDriverKind : uint8_t {
  ElfDriver = 0x01,
  MachODriver = 0x02,
};

enum class ExecutableMachine : uint8_t {
  X86_64 = 0x01,
  AArch64 = 0x02,
};

/// \brief Closed target facts an executable inspector must prove from output
///        bytes rather than infer from an opaque target identity or the host.
class ExecutableInspectionProfile final {
public:
  ExecutableInspectionProfile(ExecutableInspectionProfile&&) noexcept = default;
  ExecutableInspectionProfile& operator=(ExecutableInspectionProfile&&) noexcept = default;
  ZC_DISALLOW_COPY(ExecutableInspectionProfile);
  ~ExecutableInspectionProfile() noexcept = default;

  ZC_NODISCARD static zc::Maybe<ExecutableInspectionProfile> make(
      ObjectFormat objectFormat, ExecutableMachine machine, uint32_t pointerWidthBits,
      zc::Array<zc::String>&& requiredRuntimeSymbols, zc::String&& runtimeReferenceDomain);

  ZC_NODISCARD ObjectFormat objectFormat() const noexcept { return objectFormatValue; }
  ZC_NODISCARD ExecutableMachine machine() const noexcept { return machineValue; }
  ZC_NODISCARD uint32_t pointerWidthBits() const noexcept { return pointerWidthValue; }
  ZC_NODISCARD zc::ArrayPtr<const zc::String> requiredRuntimeSymbols() const noexcept {
    return requiredRuntimeSymbolValues.asPtr();
  }
  /// \return The target-specific raw symbol-table prefix of the ZOM runtime ABI
  ///         (e.g. `__zom_` on ELF). Any undefined symbol whose raw name begins
  ///         with this prefix is an unresolved runtime reference and fails
  ///         inspection; a non-prefixed external import (a C library symbol) is
  ///         allowed. Never empty.
  ZC_NODISCARD zc::StringPtr runtimeReferenceDomain() const noexcept {
    return runtimeReferenceDomainValue;
  }

private:
  ExecutableInspectionProfile(ObjectFormat objectFormat, ExecutableMachine machine,
                              uint32_t pointerWidthBits,
                              zc::Array<zc::String>&& requiredRuntimeSymbols,
                              zc::String&& runtimeReferenceDomain) noexcept
      : objectFormatValue(objectFormat),
        machineValue(machine),
        pointerWidthValue(pointerWidthBits),
        requiredRuntimeSymbolValues(zc::mv(requiredRuntimeSymbols)),
        runtimeReferenceDomainValue(zc::mv(runtimeReferenceDomain)) {}

  ObjectFormat objectFormatValue;
  ExecutableMachine machineValue;
  uint32_t pointerWidthValue;
  zc::Array<zc::String> requiredRuntimeSymbolValues;
  zc::String runtimeReferenceDomainValue;
};

/// \brief The role a link input plays in the plan.
///
/// RFC 0043 sorts and deduplicates records by their complete canonical keys and
/// forbids a duplicate path, symbol, or role; the role is part of that key.
enum class LinkInputRole : uint8_t {
  ObjectArtifact = 0x01,
  CrtObject = 0x02,
  DefaultLibrary = 0x03,
  RuntimeObject = 0x04,
};

/// \brief One immutable, digest-recorded file input to the link plan.
///
/// RFC 0043 requires every file input to be an already verified, immutable
/// artifact with a recorded digest and byte count. This record carries exactly
/// that: a normalized path, its role, its content digest, and its byte count.
/// It has no public aggregate initializer; `make` validates and is the only way
/// to build one.
class LinkInputRecord final {
public:
  LinkInputRecord(LinkInputRecord&&) noexcept = default;
  LinkInputRecord& operator=(LinkInputRecord&&) noexcept = default;
  ZC_DISALLOW_COPY(LinkInputRecord);
  ~LinkInputRecord() noexcept = default;

  /// \brief Builds a validated input record.
  /// \return none for a non-normalized-absolute path or a zero byte count.
  ZC_NODISCARD static zc::Maybe<LinkInputRecord> make(zc::StringPtr normalizedPath,
                                                      LinkInputRole role,
                                                      const identity::Sha256Digest& digest,
                                                      uint64_t byteCount);

  ZC_NODISCARD zc::StringPtr path() const noexcept { return pathValue; }
  ZC_NODISCARD LinkInputRole role() const noexcept { return roleValue; }
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept { return digestValue; }
  ZC_NODISCARD uint64_t byteCount() const noexcept { return byteCountValue; }

  /// \brief Orders records by the complete canonical key (role, path, digest).
  ZC_NODISCARD int compareCanonical(const LinkInputRecord& other) const noexcept;

private:
  LinkInputRecord(zc::String&& path, LinkInputRole role, const identity::Sha256Digest& digest,
                  uint64_t byteCount) noexcept
      : pathValue(zc::mv(path)), roleValue(role), digestValue(digest), byteCountValue(byteCount) {}

  zc::String pathValue;
  LinkInputRole roleValue;
  identity::Sha256Digest digestValue;
  uint64_t byteCountValue;
};

/// \brief The immutable per-target toolchain closure record.
///
/// RFC 0043 "Toolchain Discovery Record": one immutable record per selected
/// target binding the closure's filesystem inputs. It is supplied, not ambient:
/// `make` fails closed on an empty sysroot, an empty or non-absolute linker
/// path, or a zero linker byte count. The closure's ordered CRT objects and
/// default libraries are carried as `LinkInputRecord`s (roles `CrtObject` /
/// `DefaultLibrary`) so they share the input canonical-key discipline.
class ToolchainClosureRecord final {
public:
  ToolchainClosureRecord(ToolchainClosureRecord&&) noexcept = default;
  ToolchainClosureRecord& operator=(ToolchainClosureRecord&&) noexcept = default;
  ZC_DISALLOW_COPY(ToolchainClosureRecord);
  ~ToolchainClosureRecord() noexcept = default;

  /// \brief Builds a validated toolchain closure.
  /// \param targetSpecificationIdentity Non-empty canonical target identity bytes.
  /// \param sysroot One normalized absolute target root directory.
  /// \param linkerKind The single driver alternative for the target format.
  /// \param linkerPath Normalized absolute driver program path.
  /// \param linkerDigest The driver program's recorded content digest.
  /// \param linkerByteCount The driver program's recorded byte count.
  /// \param crtObjects Ordered, deduplicated startup/finalization objects.
  /// \param defaultLibraries Ordered, deduplicated default system libraries.
  /// \return none when any required field is empty, non-absolute, or zero, or
  ///         when a CRT/library record does not carry its stated role.
  ZC_NODISCARD static zc::Maybe<ToolchainClosureRecord> make(
      zc::ArrayPtr<const uint8_t> targetSpecificationIdentity, zc::StringPtr sysroot,
      LinkerDriverKind linkerKind, zc::StringPtr linkerPath,
      const identity::Sha256Digest& linkerDigest, uint64_t linkerByteCount,
      zc::Array<LinkInputRecord>&& crtObjects, zc::Array<LinkInputRecord>&& defaultLibraries);

  ZC_NODISCARD zc::ArrayPtr<const uint8_t> targetSpecificationIdentity() const noexcept {
    return targetIdentityValue.asPtr();
  }
  ZC_NODISCARD zc::StringPtr sysroot() const noexcept { return sysrootValue; }
  ZC_NODISCARD LinkerDriverKind linkerKind() const noexcept { return linkerKindValue; }
  ZC_NODISCARD zc::StringPtr linkerPath() const noexcept { return linkerPathValue; }
  ZC_NODISCARD const identity::Sha256Digest& linkerDigest() const noexcept {
    return linkerDigestValue;
  }
  ZC_NODISCARD uint64_t linkerByteCount() const noexcept { return linkerByteCountValue; }
  ZC_NODISCARD zc::ArrayPtr<const LinkInputRecord> crtObjects() const noexcept {
    return crtObjectRecords.asPtr();
  }
  ZC_NODISCARD zc::ArrayPtr<const LinkInputRecord> defaultLibraries() const noexcept {
    return defaultLibraryRecords.asPtr();
  }

private:
  ToolchainClosureRecord(zc::Array<uint8_t>&& targetIdentity, zc::String&& sysroot,
                         LinkerDriverKind linkerKind, zc::String&& linkerPath,
                         const identity::Sha256Digest& linkerDigest, uint64_t linkerByteCount,
                         zc::Array<LinkInputRecord>&& crtObjects,
                         zc::Array<LinkInputRecord>&& defaultLibraries) noexcept
      : targetIdentityValue(zc::mv(targetIdentity)),
        sysrootValue(zc::mv(sysroot)),
        linkerKindValue(linkerKind),
        linkerPathValue(zc::mv(linkerPath)),
        linkerDigestValue(linkerDigest),
        linkerByteCountValue(linkerByteCount),
        crtObjectRecords(zc::mv(crtObjects)),
        defaultLibraryRecords(zc::mv(defaultLibraries)) {}

  zc::Array<uint8_t> targetIdentityValue;
  zc::String sysrootValue;
  LinkerDriverKind linkerKindValue;
  zc::String linkerPathValue;
  identity::Sha256Digest linkerDigestValue;
  uint64_t linkerByteCountValue;
  zc::Array<LinkInputRecord> crtObjectRecords;
  zc::Array<LinkInputRecord> defaultLibraryRecords;
};

/// \brief The domain-separated immutable identity of one verified link plan.
///
/// Computed by `LinkPlanCodec` as SHA-256 over the plan's canonical preimage and
/// compared by digest. See RFC 0043 "Inputs And Link Plan" (`LinkPlanId`).
class LinkPlanId final {
public:
  constexpr LinkPlanId() noexcept = default;

  ZC_NODISCARD static LinkPlanId fromDigest(const identity::Sha256Digest& digest) noexcept {
    return LinkPlanId(digest);
  }
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept { return value; }

  bool operator==(const LinkPlanId& other) const noexcept { return value == other.value; }
  bool operator!=(const LinkPlanId& other) const noexcept { return !(*this == other); }

private:
  explicit LinkPlanId(const identity::Sha256Digest& digest) noexcept : value(digest) {}

  identity::Sha256Digest value;
};

/// \brief A link plan whose canonical invariants an independent verifier proved.
///
/// RFC 0043 "Inputs And Link Plan": the verified plan stores the target
/// specification identity, toolchain identity, entry identity, ordered object
/// records, ordered runtime records, normalized linker argument records, the
/// normalized output request, and a `LinkPlanId`. It has no public aggregate
/// initializer; only `LinkPlanVerifier::verify` constructs one, so a plan cannot
/// be reconstructed from raw paths or CLI text.
///
/// This foundation slice models and verifies the plan as pure data and computes
/// its deterministic identity. It does not invoke a linker, read the filesystem,
/// or bind a live target-registry capability; those are later RFC 0043 slices.
class VerifiedLinkPlan final {
public:
  VerifiedLinkPlan(VerifiedLinkPlan&&) noexcept = default;
  VerifiedLinkPlan& operator=(VerifiedLinkPlan&&) noexcept = default;
  ZC_DISALLOW_COPY(VerifiedLinkPlan);
  ~VerifiedLinkPlan() noexcept = default;

  ZC_NODISCARD zc::ArrayPtr<const uint8_t> targetSpecificationIdentity() const noexcept {
    return closureValue.targetSpecificationIdentity();
  }
  ZC_NODISCARD const ToolchainClosureRecord& toolchainClosure() const noexcept {
    return closureValue;
  }
  ZC_NODISCARD const ExecutableInspectionProfile& inspectionProfile() const noexcept {
    return inspectionProfileValue;
  }
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> entrySymbol() const noexcept {
    return entrySymbolValue.asPtr();
  }
  ZC_NODISCARD zc::ArrayPtr<const LinkInputRecord> objectRecords() const noexcept {
    return objectRecordValues.asPtr();
  }
  ZC_NODISCARD zc::ArrayPtr<const LinkInputRecord> runtimeRecords() const noexcept {
    return runtimeRecordValues.asPtr();
  }
  ZC_NODISCARD zc::StringPtr outputPath() const noexcept { return outputPathValue; }
  ZC_NODISCARD const LinkPlanId& id() const noexcept { return idValue; }

private:
  friend class LinkPlanVerifier;

  VerifiedLinkPlan(ToolchainClosureRecord&& closure,
                   ExecutableInspectionProfile&& inspectionProfile,
                   zc::Array<uint8_t>&& entrySymbol, zc::Array<LinkInputRecord>&& objectRecords,
                   zc::Array<LinkInputRecord>&& runtimeRecords, zc::String&& outputPath,
                   const LinkPlanId& id) noexcept
      : closureValue(zc::mv(closure)),
        inspectionProfileValue(zc::mv(inspectionProfile)),
        entrySymbolValue(zc::mv(entrySymbol)),
        objectRecordValues(zc::mv(objectRecords)),
        runtimeRecordValues(zc::mv(runtimeRecords)),
        outputPathValue(zc::mv(outputPath)),
        idValue(id) {}

  ToolchainClosureRecord closureValue;
  ExecutableInspectionProfile inspectionProfileValue;
  zc::Array<uint8_t> entrySymbolValue;
  zc::Array<LinkInputRecord> objectRecordValues;
  zc::Array<LinkInputRecord> runtimeRecordValues;
  zc::String outputPathValue;
  LinkPlanId idValue;
};

/// \brief The unverified request an independent verifier turns into a plan.
///
/// RFC 0043 "Inputs And Link Plan": `ExecutableLinkRequest` is private to the
/// final package compilation path and cannot be decoded from CLI text. This
/// foundation slice models exactly the fields the verifier proves; the live
/// session-owned request bound to a `VerifiedObjectArtifact` and a
/// `TargetRegistryCapability` is a later slice.
struct ExecutableLinkRequest final {
  ToolchainClosureRecord closure;
  ExecutableInspectionProfile inspectionProfile;
  zc::Array<uint8_t> entrySymbol;
  zc::Array<LinkInputRecord> objectRecords;
  zc::Array<LinkInputRecord> runtimeRecords;
  zc::String outputRoot;
  zc::String outputPath;
};

/// \brief Canonical codec for the verified link plan.
///
/// The preimage is a domain-separated, length-framed encoding:
///   ASCII("zom.link-plan") 0x00
///   Frame(targetSpecificationIdentity)
///   uint8(linkerKind) Frame(sysroot) Frame(linkerPath)
///     Frame(linkerDigest) uint64(linkerByteCount)
///   EncodeInputSequence(closure.crtObjects)
///   EncodeInputSequence(closure.defaultLibraries)
///   uint8(objectFormat) uint8(machine) uint32(pointerWidthBits)
///   uint64(requiredRuntimeSymbolCount) [Frame(symbol)...]
///   Frame(runtimeReferenceDomain)
///   Frame(entrySymbol)
///   EncodeInputSequence(objectRecords)
///   EncodeInputSequence(runtimeRecords)
///   Frame(outputPath)
/// `Frame` is a big-endian uint64 byte length followed by the exact bytes; a
/// `LinkInputRecord` frame is `Frame(path) uint8(role) Frame(digest)
/// uint64(byteCount)`. Every sequence is prefixed by a big-endian uint64 count.
class LinkPlanCodec final {
public:
  /// \brief Encodes a verified plan to its canonical preimage bytes.
  ZC_NODISCARD static zc::Array<uint8_t> encode(const VerifiedLinkPlan& plan);
  /// \brief Computes the plan's `LinkPlanId` (SHA-256 of the preimage).
  ZC_NODISCARD static LinkPlanId computeId(const VerifiedLinkPlan& plan);
};

/// \brief The independent verifier that constructs a `VerifiedLinkPlan`.
///
/// RFC 0043 "Inputs And Link Plan": the request constructs a plan only after an
/// independent verifier proves the six numbered invariants. Rejection consumes
/// the request and publishes neither a plan nor a partial executable. Each
/// rejection maps to an RFC 0043 failure row under `LinkPlanConstruction`.
class LinkPlanVerifier final {
public:
  /// \brief Verifies the request and, on success, constructs the plan.
  /// \param request The link request, consumed on every branch.
  /// \return A verified plan, or an RFC 0010 `LinkPlanConstruction` rejection.
  ZC_NODISCARD static IrOperationResult<VerifiedLinkPlan> verify(ExecutableLinkRequest&& request);
};

}  // namespace zomlang::compiler::ir
