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

#include "compiler/ir/toolchain-discovery.h"

#include "compiler/identity/crypto/sha256.h"
#include "zc/core/debug.h"

namespace zomlang::compiler::ir {

// =======================================================================================
// ToolchainDiscoveryResult

ToolchainDiscoveryResult ToolchainDiscoveryResult::forClosure(ToolchainClosureRecord&& closure) {
  return ToolchainDiscoveryResult(zc::mv(closure));
}

ToolchainDiscoveryResult ToolchainDiscoveryResult::forFailure(ToolchainDiscoveryFailure reason) {
  return ToolchainDiscoveryResult(reason);
}

const ToolchainClosureRecord& ToolchainDiscoveryResult::closure() const {
  ZC_IREQUIRE(closureValue != zc::none, "ToolchainDiscoveryResult::closure() on a failure");
  return ZC_REQUIRE_NONNULL(closureValue);
}

// =======================================================================================
// discoverToolchain

namespace {

// Reads a file's bytes under the search root, or none if it does not exist. A
// present-but-empty file returns an empty array (distinguished from a missing
// file, which returns none).
zc::Maybe<zc::Array<zc::byte>> tryReadFile(const zc::ReadableDirectory& root,
                                           zc::StringPtr relativePath) {
  ZC_IF_SOME(file, root.tryOpenFile(zc::Path::parse(relativePath))) { return file->readAllBytes(); }
  return zc::none;
}

// Digests `bytes` into a validated input record with `role` and `recordedPath`,
// or none if the digest cannot be computed or the record is rejected.
zc::Maybe<LinkInputRecord> makeInputRecord(zc::StringPtr recordedPath, LinkInputRole role,
                                           zc::ArrayPtr<const zc::byte> bytes) {
  ZC_IF_SOME(digest, identity::sha256(bytes.asBytes())) {
    return LinkInputRecord::make(recordedPath, role, digest, bytes.size());
  }
  return zc::none;
}

// Derives the normalized absolute recorded path for a file from the sysroot and
// its sysroot-relative path, so the recorded/executed path always names the same
// object that was read and digested. The relative path must be non-empty and
// must not begin with '/'.
zc::Maybe<zc::String> deriveRecordedPath(zc::StringPtr sysroot, zc::StringPtr relativePath) {
  if (relativePath.size() == 0 || relativePath[0] == '/') { return zc::none; }
  return zc::str(sysroot, "/", relativePath);
}

}  // namespace

ToolchainDiscoveryResult discoverToolchain(const zc::ReadableDirectory& searchRoot,
                                           const ToolchainSearchSpec& spec) {
  // Fail-closed spec validation: the target identity, sysroot, and linker
  // relative path must be present before we touch the filesystem.
  if (spec.targetSpecificationIdentity.size() == 0 || spec.sysroot.size() == 0 ||
      spec.linkerRelativePath.size() == 0) {
    return ToolchainDiscoveryResult::forFailure(ToolchainDiscoveryFailure::MalformedSpec);
  }

  // The linker's recorded absolute path is DERIVED from the sysroot and its
  // relative path, never supplied independently, so the digested bytes and the
  // executed program are the same object by construction.
  zc::Maybe<zc::String> linkerRecorded = deriveRecordedPath(spec.sysroot, spec.linkerRelativePath);
  if (linkerRecorded == zc::none) {
    return ToolchainDiscoveryResult::forFailure(ToolchainDiscoveryFailure::MalformedSpec);
  }

  // Resolve and digest the linker driver program from the same relative path.
  zc::Maybe<zc::Array<zc::byte>> linkerBytes = tryReadFile(searchRoot, spec.linkerRelativePath);
  if (linkerBytes == zc::none) {
    return ToolchainDiscoveryResult::forFailure(ToolchainDiscoveryFailure::LinkerNotFound);
  }
  zc::Array<zc::byte> linkerContent = ZC_REQUIRE_NONNULL(zc::mv(linkerBytes));
  if (linkerContent.size() == 0) {
    return ToolchainDiscoveryResult::forFailure(ToolchainDiscoveryFailure::EmptyInput);
  }
  zc::Maybe<identity::Sha256Digest> linkerDigest = identity::sha256(linkerContent.asBytes());
  if (linkerDigest == zc::none) {
    return ToolchainDiscoveryResult::forFailure(ToolchainDiscoveryFailure::DigestFailed);
  }

  // Resolve and digest every CRT object and default library, sorting each into
  // its role bucket. The spec's declared order is preserved within a role. Each
  // recorded path is derived from the same relative path that was read.
  zc::Vector<LinkInputRecord> crtObjects;
  zc::Vector<LinkInputRecord> defaultLibraries;
  for (const ToolchainSearchInput& input : spec.inputs) {
    if (input.role != LinkInputRole::CrtObject && input.role != LinkInputRole::DefaultLibrary) {
      return ToolchainDiscoveryResult::forFailure(ToolchainDiscoveryFailure::InvalidInputRole);
    }

    zc::Maybe<zc::String> recordedPath = deriveRecordedPath(spec.sysroot, input.relativePath);
    if (recordedPath == zc::none) {
      return ToolchainDiscoveryResult::forFailure(ToolchainDiscoveryFailure::MalformedSpec);
    }

    zc::Maybe<zc::Array<zc::byte>> bytes = tryReadFile(searchRoot, input.relativePath);
    if (bytes == zc::none) {
      return ToolchainDiscoveryResult::forFailure(ToolchainDiscoveryFailure::InputNotFound);
    }
    zc::Array<zc::byte> content = ZC_REQUIRE_NONNULL(zc::mv(bytes));
    if (content.size() == 0) {
      return ToolchainDiscoveryResult::forFailure(ToolchainDiscoveryFailure::EmptyInput);
    }

    zc::Maybe<LinkInputRecord> record =
        makeInputRecord(ZC_REQUIRE_NONNULL(recordedPath), input.role, content);
    if (record == zc::none) {
      return ToolchainDiscoveryResult::forFailure(ToolchainDiscoveryFailure::DigestFailed);
    }
    if (input.role == LinkInputRole::CrtObject) {
      crtObjects.add(ZC_REQUIRE_NONNULL(zc::mv(record)));
    } else {
      defaultLibraries.add(ZC_REQUIRE_NONNULL(zc::mv(record)));
    }
  }

  // Final closed check: assemble the validated closure. `make` enforces the
  // absolute-path, non-empty, and role-consistency invariants once more.
  zc::Maybe<ToolchainClosureRecord> closure = ToolchainClosureRecord::make(
      spec.targetSpecificationIdentity.asPtr(), spec.sysroot, spec.linkerKind,
      ZC_REQUIRE_NONNULL(linkerRecorded), ZC_REQUIRE_NONNULL(zc::mv(linkerDigest)),
      linkerContent.size(), crtObjects.releaseAsArray(), defaultLibraries.releaseAsArray());
  if (closure == zc::none) {
    return ToolchainDiscoveryResult::forFailure(ToolchainDiscoveryFailure::ClosureRejected);
  }
  return ToolchainDiscoveryResult::forClosure(ZC_REQUIRE_NONNULL(zc::mv(closure)));
}

// =======================================================================================
// verifyClosureMatchesHostFormat

bool verifyClosureMatchesHostFormat(const ToolchainClosureRecord& closure,
                                    ObjectFormat hostObjectFormat) {
  switch (hostObjectFormat) {
    case ObjectFormat::Elf:
      return closure.linkerKind() == LinkerDriverKind::ElfDriver;
    case ObjectFormat::MachO:
      return closure.linkerKind() == LinkerDriverKind::MachODriver;
    case ObjectFormat::Coff:
    case ObjectFormat::Wasm:
      // No driver family is defined for these formats yet; fail closed.
      return false;
  }
  return false;
}

}  // namespace zomlang::compiler::ir
