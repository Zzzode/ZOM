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

#include "compiler/ir/executable-inspector.h"

namespace zomlang::compiler::ir {
namespace {

bool rangeFits(size_t offset, size_t size, size_t total) {
  return offset <= total && size <= total - offset;
}

zc::Maybe<uint16_t> u16le(zc::ArrayPtr<const uint8_t> bytes, size_t offset) {
  if (!rangeFits(offset, 2, bytes.size())) return zc::none;
  return static_cast<uint16_t>(bytes[offset]) |
         static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8);
}

zc::Maybe<uint32_t> u32le(zc::ArrayPtr<const uint8_t> bytes, size_t offset) {
  if (!rangeFits(offset, 4, bytes.size())) return zc::none;
  uint32_t result = 0;
  for (size_t index = 0; index < 4; ++index) {
    result |= static_cast<uint32_t>(bytes[offset + index]) << static_cast<uint32_t>(index * 8);
  }
  return result;
}

zc::Maybe<uint64_t> u64le(zc::ArrayPtr<const uint8_t> bytes, size_t offset) {
  if (!rangeFits(offset, 8, bytes.size())) return zc::none;
  uint64_t result = 0;
  for (size_t index = 0; index < 8; ++index) {
    result |= static_cast<uint64_t>(bytes[offset + index]) << static_cast<uint32_t>(index * 8);
  }
  return result;
}

// Extracts the NUL-terminated symbol name at `offset` within the string table.
// Returns none when the offset is out of range or the name is not NUL-terminated
// inside the table, which the caller must treat as a malformed image rather than
// silently skipping the symbol. A zero offset into a table whose first byte is
// NUL is a valid empty name, not a malformed record.
zc::Maybe<zc::ArrayPtr<const uint8_t>> symbolName(zc::ArrayPtr<const uint8_t> strings,
                                                  uint32_t offset) {
  if (offset >= strings.size()) return zc::none;
  for (size_t index = offset; index < strings.size(); ++index) {
    if (strings[index] == 0) return strings.slice(offset, index);
  }
  return zc::none;
}

bool bytesEqual(zc::ArrayPtr<const uint8_t> a, zc::ArrayPtr<const uint8_t> b) {
  if (a.size() != b.size()) return false;
  for (size_t index = 0; index < a.size(); ++index) {
    if (a[index] != b[index]) return false;
  }
  return true;
}

bool bytesStartWith(zc::ArrayPtr<const uint8_t> value, zc::ArrayPtr<const uint8_t> prefix) {
  if (value.size() < prefix.size()) return false;
  for (size_t index = 0; index < prefix.size(); ++index) {
    if (value[index] != prefix[index]) return false;
  }
  return true;
}

struct SymbolRequirements final {
  zc::ArrayPtr<const uint8_t> entry;
  zc::ArrayPtr<const zc::String> runtime;
  uint32_t entryCount = 0;
  zc::Vector<uint32_t> runtimeCount;

  SymbolRequirements(zc::ArrayPtr<const uint8_t> entry, zc::ArrayPtr<const zc::String> runtime)
      : entry(entry), runtime(runtime) {
    for (size_t index = 0; index < runtime.size(); ++index) runtimeCount.add(0);
  }

  void observe(zc::ArrayPtr<const uint8_t> name) {
    if (bytesEqual(name, entry)) ++entryCount;
    for (size_t index = 0; index < runtime.size(); ++index) {
      if (bytesEqual(name, runtime[index].asBytes())) ++runtimeCount[index];
    }
  }

  // A required symbol must be defined exactly once. Absent fails
  // MissingRequiredSymbol; a second defining occurrence (same table, or across
  // SYMTAB and DYNSYM, or a duplicate Mach-O definition) fails
  // DuplicateRequiredSymbol per RFC 0043's duplicate-required-symbol contract.
  zc::Maybe<ExecutableInspectionFailure> resolve() const {
    if (entryCount == 0) return ExecutableInspectionFailure::MissingRequiredSymbol;
    if (entryCount > 1) return ExecutableInspectionFailure::DuplicateRequiredSymbol;
    for (const uint32_t count : runtimeCount) {
      if (count == 0) return ExecutableInspectionFailure::MissingRequiredSymbol;
      if (count > 1) return ExecutableInspectionFailure::DuplicateRequiredSymbol;
    }
    return zc::none;
  }
};

/// \brief A leading magic recognised as one of the supported object formats.
enum class RecognizedFormat : uint8_t { Elf, MachO, Unknown };

RecognizedFormat recognizeFormat(zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() >= 4 && bytes[0] == 0x7f && bytes[1] == 'E' && bytes[2] == 'L' &&
      bytes[3] == 'F') {
    return RecognizedFormat::Elf;
  }
  const auto magic = u32le(bytes, 0);
  if (magic != zc::none) {
    switch (ZC_REQUIRE_NONNULL(magic)) {
      case 0xfeedfacf:  // Mach-O 64-bit little-endian
      case 0xfeedface:  // Mach-O 32-bit little-endian
      case 0xcffaedfe:  // Mach-O 64-bit big-endian
      case 0xcefaedfe:  // Mach-O 32-bit big-endian
      case 0xcafebabe:  // Mach-O fat big-endian
      case 0xbebafeca:  // Mach-O fat little-endian
        return RecognizedFormat::MachO;
      default:
        break;
    }
  }
  return RecognizedFormat::Unknown;
}

// A leading magic that identifies a different supported object format is a
// format mismatch (InvalidAbi); unrecognisable or structurally broken bytes
// remain a malformed image (InvalidFact).
ExecutableInspectionFailure formatFailure(zc::ArrayPtr<const uint8_t> bytes,
                                          RecognizedFormat expected) {
  return recognizeFormat(bytes) == RecognizedFormat::Unknown || recognizeFormat(bytes) == expected
             ? ExecutableInspectionFailure::MalformedImage
             : ExecutableInspectionFailure::AbiMismatch;
}

zc::Maybe<ExecutableInspectionFailure> inspectElf(zc::ArrayPtr<const uint8_t> bytes,
                                                  const ExecutableInspectionProfile& profile,
                                                  zc::ArrayPtr<const uint8_t> entrySymbol) {
  constexpr size_t headerSize = 64;
  if (bytes.size() < 4 || bytes[0] != 0x7f || bytes[1] != 'E' || bytes[2] != 'L' ||
      bytes[3] != 'F') {
    return formatFailure(bytes, RecognizedFormat::Elf);
  }
  // A recognised ELF image: EI_CLASS (bitness) and EI_DATA (endianness) are ABI
  // shape, not structural corruption. The initial matrix is ELF64 little-endian,
  // so an ELF32 or big-endian ident is an ABI mismatch even when the rest of the
  // header cannot be parsed. EI_VERSION and a truncated 64-bit header remain
  // malformed.
  if (bytes.size() < 7) return ExecutableInspectionFailure::MalformedImage;
  if (bytes[4] != 2 || bytes[5] != 1) return ExecutableInspectionFailure::AbiMismatch;
  if (bytes.size() < headerSize || bytes[6] != 1) {
    return ExecutableInspectionFailure::MalformedImage;
  }
  const auto type = u16le(bytes, 16);
  const auto machine = u16le(bytes, 18);
  const auto sectionOffset = u64le(bytes, 40);
  const auto encodedHeaderSize = u16le(bytes, 52);
  const auto sectionEntrySize = u16le(bytes, 58);
  const auto sectionCount = u16le(bytes, 60);
  if (type == zc::none || machine == zc::none || sectionOffset == zc::none ||
      encodedHeaderSize == zc::none || sectionEntrySize == zc::none || sectionCount == zc::none ||
      (ZC_REQUIRE_NONNULL(type) != 2 && ZC_REQUIRE_NONNULL(type) != 3) ||
      ZC_REQUIRE_NONNULL(encodedHeaderSize) != headerSize ||
      ZC_REQUIRE_NONNULL(sectionEntrySize) < 64 || ZC_REQUIRE_NONNULL(sectionCount) == 0) {
    return ExecutableInspectionFailure::MalformedImage;
  }
  const uint16_t expectedMachine =
      profile.machine() == ExecutableMachine::X86_64 ? uint16_t{62} : uint16_t{183};
  if (ZC_REQUIRE_NONNULL(machine) != expectedMachine || profile.pointerWidthBits() != 64) {
    return ExecutableInspectionFailure::AbiMismatch;
  }
  const uint64_t tableSize = static_cast<uint64_t>(ZC_REQUIRE_NONNULL(sectionEntrySize)) *
                             ZC_REQUIRE_NONNULL(sectionCount);
  if (ZC_REQUIRE_NONNULL(sectionOffset) > SIZE_MAX || tableSize > SIZE_MAX ||
      !rangeFits(static_cast<size_t>(ZC_REQUIRE_NONNULL(sectionOffset)),
                 static_cast<size_t>(tableSize), bytes.size())) {
    return ExecutableInspectionFailure::MalformedImage;
  }

  SymbolRequirements requirements(entrySymbol, profile.requiredRuntimeSymbols());
  for (uint16_t sectionIndex = 0; sectionIndex < ZC_REQUIRE_NONNULL(sectionCount); ++sectionIndex) {
    const size_t section = static_cast<size_t>(ZC_REQUIRE_NONNULL(sectionOffset)) +
                           static_cast<size_t>(sectionIndex) * ZC_REQUIRE_NONNULL(sectionEntrySize);
    const auto sectionType = u32le(bytes, section + 4);
    if (sectionType == zc::none ||
        (ZC_REQUIRE_NONNULL(sectionType) != 2 && ZC_REQUIRE_NONNULL(sectionType) != 11)) {
      continue;
    }
    const auto symbolOffset = u64le(bytes, section + 24);
    const auto symbolSize = u64le(bytes, section + 32);
    const auto stringIndex = u32le(bytes, section + 40);
    const auto symbolEntrySize = u64le(bytes, section + 56);
    if (symbolOffset == zc::none || symbolSize == zc::none || stringIndex == zc::none ||
        symbolEntrySize == zc::none || ZC_REQUIRE_NONNULL(symbolEntrySize) < 24 ||
        ZC_REQUIRE_NONNULL(symbolSize) % ZC_REQUIRE_NONNULL(symbolEntrySize) != 0 ||
        ZC_REQUIRE_NONNULL(stringIndex) >= ZC_REQUIRE_NONNULL(sectionCount) ||
        ZC_REQUIRE_NONNULL(symbolOffset) > SIZE_MAX || ZC_REQUIRE_NONNULL(symbolSize) > SIZE_MAX ||
        !rangeFits(static_cast<size_t>(ZC_REQUIRE_NONNULL(symbolOffset)),
                   static_cast<size_t>(ZC_REQUIRE_NONNULL(symbolSize)), bytes.size())) {
      return ExecutableInspectionFailure::MalformedImage;
    }
    const size_t stringSection =
        static_cast<size_t>(ZC_REQUIRE_NONNULL(sectionOffset)) +
        static_cast<size_t>(ZC_REQUIRE_NONNULL(stringIndex)) * ZC_REQUIRE_NONNULL(sectionEntrySize);
    const auto stringType = u32le(bytes, stringSection + 4);
    const auto stringOffset = u64le(bytes, stringSection + 24);
    const auto stringSize = u64le(bytes, stringSection + 32);
    if (stringType == zc::none || ZC_REQUIRE_NONNULL(stringType) != 3 || stringOffset == zc::none ||
        stringSize == zc::none || ZC_REQUIRE_NONNULL(stringOffset) > SIZE_MAX ||
        ZC_REQUIRE_NONNULL(stringSize) > SIZE_MAX ||
        !rangeFits(static_cast<size_t>(ZC_REQUIRE_NONNULL(stringOffset)),
                   static_cast<size_t>(ZC_REQUIRE_NONNULL(stringSize)), bytes.size())) {
      return ExecutableInspectionFailure::MalformedImage;
    }
    const auto strings = bytes.slice(
        static_cast<size_t>(ZC_REQUIRE_NONNULL(stringOffset)),
        static_cast<size_t>(ZC_REQUIRE_NONNULL(stringOffset) + ZC_REQUIRE_NONNULL(stringSize)));
    const uint64_t count = ZC_REQUIRE_NONNULL(symbolSize) / ZC_REQUIRE_NONNULL(symbolEntrySize);
    for (uint64_t index = 0; index < count; ++index) {
      const size_t symbol = static_cast<size_t>(ZC_REQUIRE_NONNULL(symbolOffset) +
                                                index * ZC_REQUIRE_NONNULL(symbolEntrySize));
      const auto name = u32le(bytes, symbol);
      const auto sectionNumber = u16le(bytes, symbol + 6);
      if (name == zc::none || sectionNumber == zc::none) {
        return ExecutableInspectionFailure::MalformedImage;
      }
      // Every symbol table record's name offset is bounded and NUL-terminated
      // inside the string table before any binding/section classification; an
      // out-of-range or unterminated name is a malformed image regardless of
      // whether the symbol is a match candidate (offset 0 is a valid empty name).
      zc::Maybe<zc::ArrayPtr<const uint8_t>> resolvedName =
          symbolName(strings, ZC_REQUIRE_NONNULL(name));
      if (resolvedName == zc::none) return ExecutableInspectionFailure::MalformedImage;
      const zc::ArrayPtr<const uint8_t> symbolBytes = ZC_REQUIRE_NONNULL(resolvedName);
      const uint8_t binding = bytes[symbol + 4] >> 4;
      const bool globalOrWeak = binding == 1 || binding == 2;
      if (ZC_REQUIRE_NONNULL(sectionNumber) == 0) {
        // An undefined (SHN_UNDEF) global/weak symbol whose name is in the ZOM
        // runtime ABI domain is an unresolved runtime reference; a plain external
        // import (a C library symbol) is allowed.
        if (globalOrWeak &&
            bytesStartWith(symbolBytes, profile.runtimeReferenceDomain().asBytes())) {
          return ExecutableInspectionFailure::UnresolvedRuntimeReference;
        }
        continue;
      }
      if (!globalOrWeak) continue;
      requirements.observe(symbolBytes);
    }
  }
  return requirements.resolve();
}

zc::Maybe<ExecutableInspectionFailure> inspectMachO(zc::ArrayPtr<const uint8_t> bytes,
                                                    const ExecutableInspectionProfile& profile,
                                                    zc::ArrayPtr<const uint8_t> entrySymbol) {
  constexpr uint32_t magic64 = 0xfeedfacf;
  constexpr uint32_t executableType = 2;
  constexpr uint32_t symbolTableCommand = 2;
  const auto magic = u32le(bytes, 0);
  if (bytes.size() >= 4 && magic != zc::none) {
    switch (ZC_REQUIRE_NONNULL(magic)) {
      case 0xfeedface:  // 32-bit Mach-O little-endian: recognised format, wrong bitness
      case 0xcefaedfe:  // 32-bit Mach-O big-endian
      case 0xcffaedfe:  // 64-bit Mach-O big-endian: recognised format, wrong endianness
        return ExecutableInspectionFailure::AbiMismatch;
      default:
        break;
    }
  }
  if (bytes.size() < 32 || u32le(bytes, 0) != magic64) {
    return formatFailure(bytes, RecognizedFormat::MachO);
  }
  const auto cpu = u32le(bytes, 4);
  const auto fileType = u32le(bytes, 12);
  const auto commandCount = u32le(bytes, 16);
  const auto commandBytes = u32le(bytes, 20);
  if (cpu == zc::none || fileType == zc::none || commandCount == zc::none ||
      commandBytes == zc::none || ZC_REQUIRE_NONNULL(fileType) != executableType ||
      !rangeFits(32, ZC_REQUIRE_NONNULL(commandBytes), bytes.size())) {
    return ExecutableInspectionFailure::MalformedImage;
  }
  const uint32_t expectedCpu =
      profile.machine() == ExecutableMachine::X86_64 ? 0x01000007u : 0x0100000cu;
  if (ZC_REQUIRE_NONNULL(cpu) != expectedCpu || profile.pointerWidthBits() != 64) {
    return ExecutableInspectionFailure::AbiMismatch;
  }

  // The inspector owns the target-format entry projection: the link plan records
  // the entry point as its raw logical name (for example `zom` or `_start`), and
  // Mach-O prepends a single leading underscore to C-level symbols in the symbol
  // table (`zom` -> `_zom`). Project the raw entry into the Mach-O spelling once,
  // here, before matching. Required runtime symbols are NOT projected: the plan
  // already records them in the target's raw table spelling (ELF `__zom_...`,
  // Mach-O `___zom_...`), so a second prefix would double the underscore.
  auto projectedEntry = zc::heapArray<uint8_t>(entrySymbol.size() + 1);
  projectedEntry[0] = static_cast<uint8_t>('_');
  for (size_t index = 0; index < entrySymbol.size(); ++index) {
    projectedEntry[index + 1] = entrySymbol[index];
  }
  SymbolRequirements requirements(projectedEntry.asPtr(), profile.requiredRuntimeSymbols());
  bool foundSymbolTable = false;
  size_t commandOffset = 32;
  for (uint32_t index = 0; index < ZC_REQUIRE_NONNULL(commandCount); ++index) {
    const auto command = u32le(bytes, commandOffset);
    const auto commandSize = u32le(bytes, commandOffset + 4);
    if (command == zc::none || commandSize == zc::none || ZC_REQUIRE_NONNULL(commandSize) < 8 ||
        !rangeFits(commandOffset, ZC_REQUIRE_NONNULL(commandSize), bytes.size())) {
      return ExecutableInspectionFailure::MalformedImage;
    }
    if (ZC_REQUIRE_NONNULL(command) == symbolTableCommand) {
      if (foundSymbolTable || ZC_REQUIRE_NONNULL(commandSize) < 24) {
        return ExecutableInspectionFailure::MalformedImage;
      }
      foundSymbolTable = true;
      const auto symbolOffset = u32le(bytes, commandOffset + 8);
      const auto symbolCount = u32le(bytes, commandOffset + 12);
      const auto stringOffset = u32le(bytes, commandOffset + 16);
      const auto stringSize = u32le(bytes, commandOffset + 20);
      if (symbolOffset == zc::none || symbolCount == zc::none || stringOffset == zc::none ||
          stringSize == zc::none ||
          !rangeFits(ZC_REQUIRE_NONNULL(symbolOffset),
                     static_cast<size_t>(ZC_REQUIRE_NONNULL(symbolCount)) * 16, bytes.size()) ||
          !rangeFits(ZC_REQUIRE_NONNULL(stringOffset), ZC_REQUIRE_NONNULL(stringSize),
                     bytes.size())) {
        return ExecutableInspectionFailure::MalformedImage;
      }
      const auto strings =
          bytes.slice(ZC_REQUIRE_NONNULL(stringOffset),
                      ZC_REQUIRE_NONNULL(stringOffset) + ZC_REQUIRE_NONNULL(stringSize));
      for (uint32_t symbolIndex = 0; symbolIndex < ZC_REQUIRE_NONNULL(symbolCount); ++symbolIndex) {
        const size_t symbol = ZC_REQUIRE_NONNULL(symbolOffset) + symbolIndex * 16;
        const auto name = u32le(bytes, symbol);
        if (name == zc::none) return ExecutableInspectionFailure::MalformedImage;
        const uint8_t type = bytes[symbol + 4];
        const uint8_t section = bytes[symbol + 5];
        // Every nlist record's name offset is bounded and NUL-terminated inside
        // the string table before any type classification; an out-of-range or
        // unterminated n_strx is a malformed image regardless of the symbol's
        // role (offset 0 is a valid empty name).
        zc::Maybe<zc::ArrayPtr<const uint8_t>> resolvedName =
            symbolName(strings, ZC_REQUIRE_NONNULL(name));
        if (resolvedName == zc::none) return ExecutableInspectionFailure::MalformedImage;
        const zc::ArrayPtr<const uint8_t> symbolBytes = ZC_REQUIRE_NONNULL(resolvedName);
        if ((type & 0xe0) != 0) continue;          // STAB debug entry
        const bool external = (type & 0x01) != 0;  // N_EXT
        const uint8_t symbolType = type & 0x0e;    // N_TYPE
        if (symbolType == 0) {
          // N_UNDF: an undefined external symbol in the ZOM runtime ABI domain is
          // an unresolved runtime reference; a plain external import is allowed.
          if (external && bytesStartWith(symbolBytes, profile.runtimeReferenceDomain().asBytes())) {
            return ExecutableInspectionFailure::UnresolvedRuntimeReference;
          }
          continue;
        }
        if (!external || section == 0) continue;
        requirements.observe(symbolBytes);
      }
    }
    commandOffset += ZC_REQUIRE_NONNULL(commandSize);
  }
  if (commandOffset != 32 + ZC_REQUIRE_NONNULL(commandBytes) || !foundSymbolTable) {
    return ExecutableInspectionFailure::MalformedImage;
  }
  return requirements.resolve();
}

}  // namespace

zc::Maybe<ExecutableInspectionFailure> ExecutableImageInspector::inspect(
    zc::ArrayPtr<const uint8_t> bytes, const ExecutableInspectionProfile& profile,
    zc::ArrayPtr<const uint8_t> entrySymbol) {
  switch (profile.objectFormat()) {
    case ObjectFormat::Elf:
      return inspectElf(bytes, profile, entrySymbol);
    case ObjectFormat::MachO:
      return inspectMachO(bytes, profile, entrySymbol);
    case ObjectFormat::Coff:
    case ObjectFormat::Wasm:
      return ExecutableInspectionFailure::AbiMismatch;
  }
  return ExecutableInspectionFailure::MalformedImage;
}

}  // namespace zomlang::compiler::ir
