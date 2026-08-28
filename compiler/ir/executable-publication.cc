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

#include "compiler/ir/executable-publication.h"

#include "zc/core/debug.h"

namespace zomlang::compiler::ir {

namespace {

// True when `bytes` begins with the exact `magic` sequence.
bool startsWithMagic(zc::ArrayPtr<const uint8_t> bytes, zc::ArrayPtr<const uint8_t> magic) {
  if (bytes.size() < magic.size()) { return false; }
  for (size_t i = 0; i < magic.size(); ++i) {
    if (bytes[i] != magic[i]) { return false; }
  }
  return true;
}

}  // namespace

bool inspectExecutableFormat(zc::ArrayPtr<const uint8_t> executableBytes,
                             ObjectFormat expectedFormat) {
  switch (expectedFormat) {
    case ObjectFormat::Elf: {
      // ELF: 0x7F 'E' 'L' 'F'.
      const uint8_t elf[] = {0x7f, 0x45, 0x4c, 0x46};
      return startsWithMagic(executableBytes, zc::arrayPtr(elf, 4));
    }
    case ObjectFormat::MachO: {
      // Mach-O 32/64-bit, little- and big-endian magics.
      const uint8_t machO32Le[] = {0xce, 0xfa, 0xed, 0xfe};
      const uint8_t machO64Le[] = {0xcf, 0xfa, 0xed, 0xfe};
      const uint8_t machO32Be[] = {0xfe, 0xed, 0xfa, 0xce};
      const uint8_t machO64Be[] = {0xfe, 0xed, 0xfa, 0xcf};
      return startsWithMagic(executableBytes, zc::arrayPtr(machO32Le, 4)) ||
             startsWithMagic(executableBytes, zc::arrayPtr(machO64Le, 4)) ||
             startsWithMagic(executableBytes, zc::arrayPtr(machO32Be, 4)) ||
             startsWithMagic(executableBytes, zc::arrayPtr(machO64Be, 4));
    }
    case ObjectFormat::Coff:
    case ObjectFormat::Wasm:
      // No executable format check is defined for these targets yet; fail closed.
      return false;
  }
  return false;
}

ExecutablePublicationResult publishExecutable(
    const zc::Directory& outputDir, zc::StringPtr executablePath, zc::StringPtr manifestPath,
    zc::ArrayPtr<const uint8_t> executableBytes, zc::ArrayPtr<const uint8_t> manifestBytes,
    ExecutablePublicationInjectedFailure injectedFailure) {
  zc::Path executable = zc::Path::parse(executablePath);
  zc::Path manifest = zc::Path::parse(manifestPath);

  // Existing final paths are never replaced.
  if (outputDir.exists(executable) || outputDir.exists(manifest)) {
    return ExecutablePublicationResult::failure(ExecutablePublicationFailure::DestinationExists);
  }

  // Stage both outputs as synced temporaries BEFORE committing either, so a
  // write/sync fault fails with nothing committed. A Replacer that is destroyed
  // without commit() removes its temporary, so an early return needs no manual
  // cleanup here.
  bool executableCommitted = false;
  try {
    auto executableReplacer =
        outputDir.replaceFile(executable, zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT);
    executableReplacer->get().writeAll(executableBytes);
    executableReplacer->get().sync();

    auto manifestReplacer =
        outputDir.replaceFile(manifest, zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT);
    manifestReplacer->get().writeAll(manifestBytes);
    manifestReplacer->get().sync();

    // Both temporaries are in place. Commit the executable first, then the
    // manifest, so a manifest never refers to an absent executable.
    executableReplacer->commit();
    executableCommitted = true;

    // A failure after the executable commit must roll it back so the transaction
    // is all-or-neither, not best-effort.
    if (injectedFailure == ExecutablePublicationInjectedFailure::AfterExecutableCommit) {
      throw zc::Exception(zc::Exception::Type::FAILED, __FILE__, __LINE__,
                          zc::heapString("injected failure after executable commit"));
    }

    manifestReplacer->commit();

    if (injectedFailure == ExecutablePublicationInjectedFailure::AfterBothCommits) {
      throw zc::Exception(zc::Exception::Type::FAILED, __FILE__, __LINE__,
                          zc::heapString("injected failure after both commits"));
    }

    outputDir.sync();
    return ExecutablePublicationResult::success();
  } catch (const zc::Exception&) {
    // Roll back every final path this call may have committed, restoring the
    // pre-call directory state. tryRemove ignores an absent path, so removing a
    // never-committed manifest is a no-op.
    if (executableCommitted) {
      outputDir.tryRemove(executable);
      outputDir.tryRemove(manifest);
    }
    return ExecutablePublicationResult::failure(ExecutablePublicationFailure::WriteFailed);
  }
}

}  // namespace zomlang::compiler::ir
