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

#include "zc/ztest/test.h"

namespace zomlang::compiler::ir {
namespace {

void put16(zc::ArrayPtr<uint8_t> bytes, size_t offset, uint16_t value) {
  bytes[offset] = static_cast<uint8_t>(value);
  bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void put32(zc::ArrayPtr<uint8_t> bytes, size_t offset, uint32_t value) {
  for (size_t index = 0; index < 4; ++index) {
    bytes[offset + index] = static_cast<uint8_t>(value >> static_cast<uint32_t>(index * 8));
  }
}

void put64(zc::ArrayPtr<uint8_t> bytes, size_t offset, uint64_t value) {
  for (size_t index = 0; index < 8; ++index) {
    bytes[offset + index] = static_cast<uint8_t>(value >> static_cast<uint32_t>(index * 8));
  }
}

zc::Array<zc::String> runtimeSymbols(ObjectFormat format) {
  auto result = zc::heapArrayBuilder<zc::String>(1);
  // The required runtime symbol is recorded in the target's raw symbol-table
  // spelling: ELF `__zom_runtime`, Mach-O's leading-underscore `___zom_runtime`.
  result.add(format == ObjectFormat::Elf ? zc::str("__zom_runtime") : zc::str("___zom_runtime"));
  return result.finish();
}

zc::StringPtr runtimeDomain(ObjectFormat format) {
  return format == ObjectFormat::Elf ? "__zom_"_zc : "___zom_"_zc;
}

ExecutableInspectionProfile profile(ObjectFormat format, ExecutableMachine machine) {
  return ZC_REQUIRE_NONNULL(ExecutableInspectionProfile::make(
      format, machine, 64, runtimeSymbols(format), zc::str(runtimeDomain(format))));
}

zc::Array<uint8_t> elfFixture() {
  auto bytes = zc::heapArray<uint8_t>(352, uint8_t{0});
  bytes[0] = 0x7f;
  bytes[1] = 'E';
  bytes[2] = 'L';
  bytes[3] = 'F';
  bytes[4] = 2;
  bytes[5] = 1;
  bytes[6] = 1;
  put16(bytes, 16, 2);
  put16(bytes, 18, 62);
  put64(bytes, 40, 64);
  put16(bytes, 52, 64);
  put16(bytes, 58, 64);
  put16(bytes, 60, 3);

  const size_t symtab = 64 + 64;
  put32(bytes, symtab + 4, 2);
  put64(bytes, symtab + 24, 256);
  put64(bytes, symtab + 32, 72);
  put32(bytes, symtab + 40, 2);
  put64(bytes, symtab + 56, 24);

  const size_t strtab = 64 + 128;
  put32(bytes, strtab + 4, 3);
  put64(bytes, strtab + 24, 328);
  put64(bytes, strtab + 32, 19);

  put32(bytes, 256 + 24, 1);
  bytes[256 + 24 + 4] = 0x10;
  put16(bytes, 256 + 24 + 6, 1);
  put32(bytes, 256 + 48, 5);
  bytes[256 + 48 + 4] = 0x10;
  put16(bytes, 256 + 48 + 6, 1);

  constexpr char strings[] = "\0zom\0__zom_runtime\0";
  for (size_t index = 0; index < sizeof(strings) - 1; ++index) {
    bytes[328 + index] = static_cast<uint8_t>(strings[index]);
  }
  return bytes;
}

zc::Array<uint8_t> machOFixture() {
  auto bytes = zc::heapArray<uint8_t>(120, uint8_t{0});
  put32(bytes, 0, 0xfeedfacf);
  put32(bytes, 4, 0x01000007);
  put32(bytes, 12, 2);
  put32(bytes, 16, 1);
  put32(bytes, 20, 24);
  put32(bytes, 32, 2);
  put32(bytes, 36, 24);
  put32(bytes, 40, 56);
  put32(bytes, 44, 2);
  put32(bytes, 48, 88);
  put32(bytes, 52, 21);

  // Entry `_zom` at string offset 1; runtime `___zom_runtime` at offset 6, both
  // in the Mach-O raw (leading-underscore) spelling.
  put32(bytes, 56, 1);
  bytes[60] = 0x0f;
  bytes[61] = 1;
  put32(bytes, 72, 6);
  bytes[76] = 0x0f;
  bytes[77] = 1;

  constexpr char strings[] = "\0_zom\0___zom_runtime\0";
  for (size_t index = 0; index < sizeof(strings) - 1; ++index) {
    bytes[88 + index] = static_cast<uint8_t>(strings[index]);
  }
  return bytes;
}

constexpr uint8_t entryBytes[] = {'z', 'o', 'm'};
// The caller passes the raw logical entry name for every target; the inspector
// projects it into the Mach-O leading-underscore spelling (`zom` -> `_zom`)
// before matching the fixture's `_zom` nlist entry.
constexpr uint8_t machOEntryBytes[] = {'z', 'o', 'm'};
constexpr uint8_t machOPreMangledEntryBytes[] = {'_', 'z', 'o', 'm'};

}  // namespace

ZC_TEST("Executable inspector accepts a bounded ELF64 image with required symbols") {
  auto bytes = elfFixture();
  auto result = ExecutableImageInspector::inspect(
      bytes.asPtr(), profile(ObjectFormat::Elf, ExecutableMachine::X86_64),
      zc::arrayPtr(entryBytes, 3));
  ZC_EXPECT(result == zc::none);
}

ZC_TEST("Executable inspector rejects ELF ABI symbol and range mismatches") {
  auto bytes = elfFixture();
  put16(bytes, 18, 183);
  ZC_EXPECT(ExecutableImageInspector::inspect(
                bytes.asPtr(), profile(ObjectFormat::Elf, ExecutableMachine::X86_64),
                zc::arrayPtr(entryBytes, 3)) == ExecutableInspectionFailure::AbiMismatch);

  bytes = elfFixture();
  bytes[328 + 5] = 'x';
  ZC_EXPECT(ExecutableImageInspector::inspect(
                bytes.asPtr(), profile(ObjectFormat::Elf, ExecutableMachine::X86_64),
                zc::arrayPtr(entryBytes, 3)) == ExecutableInspectionFailure::MissingRequiredSymbol);

  bytes = elfFixture();
  put64(bytes, 64 + 64 + 24, 1000);
  ZC_EXPECT(ExecutableImageInspector::inspect(
                bytes.asPtr(), profile(ObjectFormat::Elf, ExecutableMachine::X86_64),
                zc::arrayPtr(entryBytes, 3)) == ExecutableInspectionFailure::MalformedImage);
}

ZC_TEST("Executable inspector accepts a bounded Mach-O64 image with required symbols") {
  auto bytes = machOFixture();
  auto result = ExecutableImageInspector::inspect(
      bytes.asPtr(), profile(ObjectFormat::MachO, ExecutableMachine::X86_64),
      zc::arrayPtr(machOEntryBytes, 3));
  ZC_EXPECT(result == zc::none);
}

ZC_TEST("Executable inspector rejects a pre-mangled Mach-O entry name") {
  // The inspector owns the Mach-O leading-underscore projection, so callers must
  // pass the raw logical entry name. A caller that pre-mangles to `_zom` would
  // have it projected to `__zom`, which no longer matches the fixture's `_zom`
  // nlist entry: the contract rejects a double-underscore rather than silently
  // accepting a pre-mangled input.
  auto bytes = machOFixture();
  ZC_EXPECT(ExecutableImageInspector::inspect(
                bytes.asPtr(), profile(ObjectFormat::MachO, ExecutableMachine::X86_64),
                zc::arrayPtr(machOPreMangledEntryBytes, 4)) ==
            ExecutableInspectionFailure::MissingRequiredSymbol);
}

ZC_TEST("Executable inspector rejects Mach-O ABI symbol and command mismatches") {
  auto bytes = machOFixture();
  put32(bytes, 4, 0x0100000c);
  ZC_EXPECT(ExecutableImageInspector::inspect(
                bytes.asPtr(), profile(ObjectFormat::MachO, ExecutableMachine::X86_64),
                zc::arrayPtr(machOEntryBytes, 3)) == ExecutableInspectionFailure::AbiMismatch);

  bytes = machOFixture();
  bytes[88 + 6] = 'x';
  ZC_EXPECT(ExecutableImageInspector::inspect(
                bytes.asPtr(), profile(ObjectFormat::MachO, ExecutableMachine::X86_64),
                zc::arrayPtr(machOEntryBytes, 3)) ==
            ExecutableInspectionFailure::MissingRequiredSymbol);

  bytes = machOFixture();
  put32(bytes, 36, 0xffff);
  ZC_EXPECT(ExecutableImageInspector::inspect(
                bytes.asPtr(), profile(ObjectFormat::MachO, ExecutableMachine::X86_64),
                zc::arrayPtr(machOEntryBytes, 3)) == ExecutableInspectionFailure::MalformedImage);
}

ZC_TEST("Executable inspector rejects a duplicated required symbol in ELF") {
  // A self-contained ELF64 image whose symbol table defines the entry once and
  // the runtime symbol twice, so the failure is a genuine duplicate rather than
  // strtab corruption. Symtab holds 4 nlist entries (sh_size 96) at file offset
  // 256; strtab data is relocated to 384 to clear the enlarged symbol table.
  auto bytes = zc::heapArray<uint8_t>(416, uint8_t{0});
  bytes[0] = 0x7f;
  bytes[1] = 'E';
  bytes[2] = 'L';
  bytes[3] = 'F';
  bytes[4] = 2;
  bytes[5] = 1;
  bytes[6] = 1;
  put16(bytes, 16, 2);
  put16(bytes, 18, 62);
  put64(bytes, 40, 64);
  put16(bytes, 52, 64);
  put16(bytes, 58, 64);
  put16(bytes, 60, 3);

  const size_t symtab = 64 + 64;
  put32(bytes, symtab + 4, 2);
  put64(bytes, symtab + 24, 256);
  put64(bytes, symtab + 32, 96);
  put32(bytes, symtab + 40, 2);
  put64(bytes, symtab + 56, 24);

  const size_t strtab = 64 + 128;
  put32(bytes, strtab + 4, 3);
  put64(bytes, strtab + 24, 384);
  put64(bytes, strtab + 32, 19);

  // symbol[1] = entry "zom" (name 1); symbol[2] and symbol[3] = "__zom_runtime"
  // (name 5), a defined duplicate.
  put32(bytes, 256 + 24, 1);
  bytes[256 + 24 + 4] = 0x10;
  put16(bytes, 256 + 24 + 6, 1);
  put32(bytes, 256 + 48, 5);
  bytes[256 + 48 + 4] = 0x10;
  put16(bytes, 256 + 48 + 6, 1);
  put32(bytes, 256 + 72, 5);
  bytes[256 + 72 + 4] = 0x10;
  put16(bytes, 256 + 72 + 6, 1);

  constexpr char strings[] = "\0zom\0__zom_runtime\0";
  for (size_t index = 0; index < sizeof(strings) - 1; ++index) {
    bytes[384 + index] = static_cast<uint8_t>(strings[index]);
  }
  ZC_EXPECT(ExecutableImageInspector::inspect(bytes.asPtr(),
                                              profile(ObjectFormat::Elf, ExecutableMachine::X86_64),
                                              zc::arrayPtr(entryBytes, 3)) ==
            ExecutableInspectionFailure::DuplicateRequiredSymbol);
}

ZC_TEST("Executable inspector rejects a duplicated required symbol in Mach-O") {
  auto bytes = machOFixture();
  // Enlarge the symbol table to three nlist entries and add a second defined
  // "___zom_runtime" (name offset 6), leaving the entry and runtime both present.
  bytes = zc::heapArray<uint8_t>(128, uint8_t{0});
  put32(bytes, 0, 0xfeedfacf);
  put32(bytes, 4, 0x01000007);
  put32(bytes, 12, 2);
  put32(bytes, 16, 1);
  put32(bytes, 20, 24);
  put32(bytes, 32, 2);
  put32(bytes, 36, 24);
  put32(bytes, 40, 56);
  put32(bytes, 44, 3);
  put32(bytes, 48, 104);
  put32(bytes, 52, 21);

  put32(bytes, 56, 1);
  bytes[60] = 0x0f;
  bytes[61] = 1;
  put32(bytes, 72, 6);
  bytes[76] = 0x0f;
  bytes[77] = 1;
  put32(bytes, 88, 6);
  bytes[92] = 0x0f;
  bytes[93] = 1;

  constexpr char strings[] = "\0_zom\0___zom_runtime\0";
  for (size_t index = 0; index < sizeof(strings) - 1; ++index) {
    bytes[104 + index] = static_cast<uint8_t>(strings[index]);
  }
  ZC_EXPECT(ExecutableImageInspector::inspect(
                bytes.asPtr(), profile(ObjectFormat::MachO, ExecutableMachine::X86_64),
                zc::arrayPtr(machOEntryBytes, 3)) ==
            ExecutableInspectionFailure::DuplicateRequiredSymbol);
}

ZC_TEST("Executable inspector maps a recognized wrong-format magic to AbiMismatch") {
  // (a) ELF profile, Mach-O magic: a recognizable other supported format is a
  // format mismatch (InvalidAbi), not a malformed image.
  auto bytes = elfFixture();
  put32(bytes, 0, 0xfeedfacf);
  ZC_EXPECT(ExecutableImageInspector::inspect(
                bytes.asPtr(), profile(ObjectFormat::Elf, ExecutableMachine::X86_64),
                zc::arrayPtr(entryBytes, 3)) == ExecutableInspectionFailure::AbiMismatch);

  // (a) Mach-O profile, ELF magic: symmetric.
  bytes = machOFixture();
  bytes[0] = 0x7f;
  bytes[1] = 'E';
  bytes[2] = 'L';
  bytes[3] = 'F';
  ZC_EXPECT(ExecutableImageInspector::inspect(
                bytes.asPtr(), profile(ObjectFormat::MachO, ExecutableMachine::X86_64),
                zc::arrayPtr(entryBytes, 3)) == ExecutableInspectionFailure::AbiMismatch);
}

ZC_TEST("Executable inspector maps same-format wrong bitness or endianness to AbiMismatch") {
  // (b) ELF profile, ELF32 ident (EI_CLASS=1): correct format, wrong ABI shape.
  auto bytes = elfFixture();
  bytes[4] = 1;
  ZC_EXPECT(ExecutableImageInspector::inspect(
                bytes.asPtr(), profile(ObjectFormat::Elf, ExecutableMachine::X86_64),
                zc::arrayPtr(entryBytes, 3)) == ExecutableInspectionFailure::AbiMismatch);

  // (b) ELF profile, big-endian ident (EI_DATA=2): the initial matrix is
  // little-endian only, so a big-endian ELF is an ABI shape mismatch.
  bytes = elfFixture();
  bytes[5] = 2;
  ZC_EXPECT(ExecutableImageInspector::inspect(
                bytes.asPtr(), profile(ObjectFormat::Elf, ExecutableMachine::X86_64),
                zc::arrayPtr(entryBytes, 3)) == ExecutableInspectionFailure::AbiMismatch);

  // (b) Mach-O profile, 32-bit Mach-O magic: correct format, wrong bitness.
  bytes = machOFixture();
  put32(bytes, 0, 0xfeedface);
  ZC_EXPECT(ExecutableImageInspector::inspect(
                bytes.asPtr(), profile(ObjectFormat::MachO, ExecutableMachine::X86_64),
                zc::arrayPtr(entryBytes, 3)) == ExecutableInspectionFailure::AbiMismatch);
}

ZC_TEST("Executable inspector keeps unrecognizable bytes as MalformedImage") {
  // (c) Neither ELF nor Mach-O magic: still a malformed image (InvalidFact),
  // never misclassified as an ABI mismatch.
  auto bytes = elfFixture();
  bytes[0] = 0xde;
  bytes[1] = 0xad;
  bytes[2] = 0xbe;
  bytes[3] = 0xef;
  ZC_EXPECT(ExecutableImageInspector::inspect(
                bytes.asPtr(), profile(ObjectFormat::Elf, ExecutableMachine::X86_64),
                zc::arrayPtr(entryBytes, 3)) == ExecutableInspectionFailure::MalformedImage);

  // (c) A recognizable ELF64 little-endian ident but a truncated header remains
  // malformed, not an ABI mismatch.
  bytes = elfFixture();
  auto truncated = zc::heapArray<uint8_t>(bytes.slice(0, 32));
  ZC_EXPECT(ExecutableImageInspector::inspect(
                truncated.asPtr(), profile(ObjectFormat::Elf, ExecutableMachine::X86_64),
                zc::arrayPtr(entryBytes, 3)) == ExecutableInspectionFailure::MalformedImage);
}

ZC_TEST("Executable inspector rejects an out-of-range ELF symbol name offset") {
  // The required symbols are all present, but a defined symbol carries a name
  // offset past the string table. It must fail malformed, not be silently
  // skipped. Symbol[1] (entry "zom") gets an out-of-range st_name.
  auto bytes = elfFixture();
  put32(bytes, 256 + 24, 4096);
  ZC_EXPECT(ExecutableImageInspector::inspect(
                bytes.asPtr(), profile(ObjectFormat::Elf, ExecutableMachine::X86_64),
                zc::arrayPtr(entryBytes, 3)) == ExecutableInspectionFailure::MalformedImage);
}

ZC_TEST("Executable inspector rejects an out-of-range Mach-O symbol name offset") {
  auto bytes = machOFixture();
  put32(bytes, 56, 4096);
  ZC_EXPECT(ExecutableImageInspector::inspect(
                bytes.asPtr(), profile(ObjectFormat::MachO, ExecutableMachine::X86_64),
                zc::arrayPtr(machOEntryBytes, 3)) == ExecutableInspectionFailure::MalformedImage);
}

ZC_TEST("Executable inspector rejects an undefined ELF ZOM runtime reference") {
  // All required symbols are defined, but symbol[0] is an undefined (SHN_UNDEF)
  // global whose name is in the runtime ABI domain (__zom_runtime at offset 5):
  // an unresolved runtime reference.
  auto bytes = elfFixture();
  put32(bytes, 256 + 0, 5);
  bytes[256 + 0 + 4] = 0x10;     // global binding
  put16(bytes, 256 + 0 + 6, 0);  // SHN_UNDEF
  ZC_EXPECT(ExecutableImageInspector::inspect(bytes.asPtr(),
                                              profile(ObjectFormat::Elf, ExecutableMachine::X86_64),
                                              zc::arrayPtr(entryBytes, 3)) ==
            ExecutableInspectionFailure::UnresolvedRuntimeReference);
}

ZC_TEST("Executable inspector allows an undefined non-domain ELF import") {
  // An undefined global whose name (the entry "zom" at offset 1) is not in the
  // runtime ABI domain models an ordinary C library import and is allowed; the
  // defined required symbols still resolve.
  auto bytes = elfFixture();
  put32(bytes, 256 + 0, 1);
  bytes[256 + 0 + 4] = 0x10;
  put16(bytes, 256 + 0 + 6, 0);
  ZC_EXPECT(ExecutableImageInspector::inspect(bytes.asPtr(),
                                              profile(ObjectFormat::Elf, ExecutableMachine::X86_64),
                                              zc::arrayPtr(entryBytes, 3)) == zc::none);
}

ZC_TEST("Executable inspector rejects an undefined Mach-O ZOM runtime reference") {
  // A three-nlist image: defined entry `_zom` and runtime `___zom_runtime`, plus
  // an extra undefined external `___zom_missing` (offset 21) in the runtime ABI
  // domain - an unresolved runtime reference, with the required defs intact.
  auto bytes = zc::heapArray<uint8_t>(144, uint8_t{0});
  put32(bytes, 0, 0xfeedfacf);
  put32(bytes, 4, 0x01000007);
  put32(bytes, 12, 2);
  put32(bytes, 16, 1);
  put32(bytes, 20, 24);
  put32(bytes, 32, 2);
  put32(bytes, 36, 24);
  put32(bytes, 40, 56);
  put32(bytes, 44, 3);
  put32(bytes, 48, 104);
  put32(bytes, 52, 36);

  put32(bytes, 56, 1);  // defined entry _zom
  bytes[60] = 0x0f;
  bytes[61] = 1;
  put32(bytes, 72, 6);  // defined runtime ___zom_runtime
  bytes[76] = 0x0f;
  bytes[77] = 1;
  put32(bytes, 88, 21);  // undefined ___zom_missing (domain hit)
  bytes[92] = 0x01;
  bytes[93] = 0;

  constexpr char strings[] = "\0_zom\0___zom_runtime\0___zom_missing\0";
  for (size_t index = 0; index < sizeof(strings) - 1; ++index) {
    bytes[104 + index] = static_cast<uint8_t>(strings[index]);
  }
  ZC_EXPECT(ExecutableImageInspector::inspect(
                bytes.asPtr(), profile(ObjectFormat::MachO, ExecutableMachine::X86_64),
                zc::arrayPtr(machOEntryBytes, 3)) ==
            ExecutableInspectionFailure::UnresolvedRuntimeReference);
}

ZC_TEST("Executable inspector allows an undefined non-domain Mach-O import") {
  // A three-nlist image: defined entry `_zom` and runtime `___zom_runtime`, plus
  // an extra undefined external `_printf` (offset 21) that is not in the runtime
  // ABI domain - an ordinary C library import, allowed.
  auto bytes = zc::heapArray<uint8_t>(136, uint8_t{0});
  put32(bytes, 0, 0xfeedfacf);
  put32(bytes, 4, 0x01000007);
  put32(bytes, 12, 2);
  put32(bytes, 16, 1);
  put32(bytes, 20, 24);
  put32(bytes, 32, 2);
  put32(bytes, 36, 24);
  put32(bytes, 40, 56);
  put32(bytes, 44, 3);
  put32(bytes, 48, 104);
  put32(bytes, 52, 29);

  put32(bytes, 56, 1);  // defined entry _zom
  bytes[60] = 0x0f;
  bytes[61] = 1;
  put32(bytes, 72, 6);  // defined runtime ___zom_runtime
  bytes[76] = 0x0f;
  bytes[77] = 1;
  put32(bytes, 88, 21);  // undefined _printf (non-domain import)
  bytes[92] = 0x01;
  bytes[93] = 0;

  constexpr char strings[] = "\0_zom\0___zom_runtime\0_printf\0";
  for (size_t index = 0; index < sizeof(strings) - 1; ++index) {
    bytes[104 + index] = static_cast<uint8_t>(strings[index]);
  }
  ZC_EXPECT(ExecutableImageInspector::inspect(
                bytes.asPtr(), profile(ObjectFormat::MachO, ExecutableMachine::X86_64),
                zc::arrayPtr(machOEntryBytes, 3)) == zc::none);
}

ZC_TEST("Executable inspector rejects a bad name offset on an irrelevant ELF symbol") {
  // Symbol[0] is a LOCAL (non-candidate) symbol with an out-of-range st_name.
  // Every record's name must be bounded and NUL-terminated regardless of role,
  // so this is a malformed image, not a skippable record.
  auto bytes = elfFixture();
  put32(bytes, 256 + 0, 4096);
  bytes[256 + 0 + 4] = 0x00;  // local binding, not a match candidate
  put16(bytes, 256 + 0 + 6, 1);
  ZC_EXPECT(ExecutableImageInspector::inspect(
                bytes.asPtr(), profile(ObjectFormat::Elf, ExecutableMachine::X86_64),
                zc::arrayPtr(entryBytes, 3)) == ExecutableInspectionFailure::MalformedImage);
}

ZC_TEST("Executable inspector rejects a bad name offset on an irrelevant Mach-O symbol") {
  auto bytes = machOFixture();
  put32(bytes, 56, 4096);
  bytes[60] = 0x0e;  // not external, N_SECT: a non-candidate record
  bytes[61] = 1;
  ZC_EXPECT(ExecutableImageInspector::inspect(
                bytes.asPtr(), profile(ObjectFormat::MachO, ExecutableMachine::X86_64),
                zc::arrayPtr(machOEntryBytes, 3)) == ExecutableInspectionFailure::MalformedImage);
}

}  // namespace zomlang::compiler::ir
