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

#include "zomlang/compiler/driver/package/trusted-runtime-elf.h"

#include "zc/ztest/test.h"

namespace zomlang::compiler::driver::package {
namespace {

void put16(zc::ArrayPtr<uint8_t> bytes, size_t offset, uint16_t value) {
  bytes[offset] = static_cast<uint8_t>(value);
  bytes[offset + 1] = static_cast<uint8_t>(value >> 8U);
}

void put32(zc::ArrayPtr<uint8_t> bytes, size_t offset, uint32_t value) {
  for (uint32_t index = 0; index < 4; ++index) {
    bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8U));
  }
}

void put64(zc::ArrayPtr<uint8_t> bytes, size_t offset, uint64_t value) {
  for (uint32_t index = 0; index < 8; ++index) {
    bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8U));
  }
}

void section(zc::ArrayPtr<uint8_t> bytes, uint32_t ordinal, uint32_t type, uint64_t offset,
             uint64_t size, uint32_t link = 0, uint32_t info = 0, uint64_t entrySize = 0) {
  const size_t base = 64 + ordinal * 64;
  put32(bytes, base + 4, type);
  put64(bytes, base + 24, offset);
  put64(bytes, base + 32, size);
  put32(bytes, base + 40, link);
  put32(bytes, base + 44, info);
  put64(bytes, base + 56, entrySize);
}

uint32_t putString(zc::ArrayPtr<uint8_t> bytes, size_t base, size_t& cursor, zc::StringPtr value) {
  const uint32_t offset = static_cast<uint32_t>(cursor - base);
  for (const auto byte : value.asBytes()) { bytes[cursor++] = byte; }
  bytes[cursor++] = 0;
  return offset;
}

void putSymbol(zc::ArrayPtr<uint8_t> bytes, size_t tableOffset, uint32_t index, uint32_t nameOffset,
               uint8_t info, uint8_t visibility, uint16_t sectionOrdinal, uint64_t byteSize) {
  const size_t base = tableOffset + index * 24;
  put32(bytes, base, nameOffset);
  bytes[base + 4] = info;
  bytes[base + 5] = visibility;
  put16(bytes, base + 6, sectionOrdinal);
  put64(bytes, base + 16, byteSize);
}

zc::Array<uint8_t> elf(uint16_t machine = 62, uint32_t extraSectionType = 0) {
  constexpr size_t stringOffset = 384;
  constexpr size_t symbolOffset = 390;
  constexpr size_t textOffset = 438;
  constexpr size_t relocationOffset = 454;
  zc::Vector<uint8_t> bytes(478);
  for (size_t index = 0; index < 478; ++index) { bytes.add(0); }
  bytes[0] = 0x7f;
  bytes[1] = 'E';
  bytes[2] = 'L';
  bytes[3] = 'F';
  bytes[4] = 2;
  bytes[5] = 1;
  bytes[6] = 1;
  put16(bytes, 16, 1);
  put16(bytes, 18, machine);
  put32(bytes, 20, 1);
  put64(bytes, 40, 64);
  put16(bytes, 52, 64);
  put16(bytes, 58, 64);
  put16(bytes, 60, 5);

  section(bytes, 0, 0, 0, 0);
  section(bytes, 1, 3, stringOffset, 6);
  section(bytes, 2, 2, symbolOffset, 48, 1, 1, 24);
  section(bytes, 3, extraSectionType == 0 ? 1 : extraSectionType, textOffset, 16);
  section(bytes, 4, 4, relocationOffset, 24, 2, 3, 24);

  bytes[stringOffset] = 0;
  bytes[stringOffset + 1] = 'f';
  bytes[stringOffset + 2] = 'u';
  bytes[stringOffset + 3] = 'n';
  bytes[stringOffset + 4] = 'c';
  bytes[stringOffset + 5] = 0;

  put32(bytes, symbolOffset + 24, 1);
  bytes[symbolOffset + 28] = 0x12;
  put16(bytes, symbolOffset + 30, 3);
  put64(bytes, symbolOffset + 40, 16);

  put64(bytes, relocationOffset, 0);
  put64(bytes, relocationOffset + 8, (uint64_t{1} << 32U) | 1U);
  put64(bytes, relocationOffset + 16, 4);
  return bytes.releaseAsArray();
}

zc::Array<uint8_t> inventoryElf() {
  constexpr size_t stringOffset = 448;
  constexpr size_t symbolOffset = 544;
  constexpr size_t textOffset = 832;
  constexpr size_t tlsOffset = 864;
  constexpr size_t relocationOffset = 880;
  zc::Vector<uint8_t> bytes(944);
  for (size_t index = 0; index < 944; ++index) { bytes.add(0); }
  bytes[0] = 0x7f;
  bytes[1] = 'E';
  bytes[2] = 'L';
  bytes[3] = 'F';
  bytes[4] = 2;
  bytes[5] = 1;
  bytes[6] = 1;
  put16(bytes, 16, 1);
  put16(bytes, 18, 62);
  put32(bytes, 20, 1);
  put64(bytes, 40, 64);
  put16(bytes, 52, 64);
  put16(bytes, 58, 64);
  put16(bytes, 60, 6);

  size_t stringCursor = stringOffset + 1;
  const auto absolute = putString(bytes, stringOffset, stringCursor, "absolute"_zc);
  const auto common = putString(bytes, stringOffset, stringCursor, "common"_zc);
  const auto duplicate = putString(bytes, stringOffset, stringCursor, "duplicate"_zc);
  const auto file = putString(bytes, stringOffset, stringCursor, "runtime-file"_zc);
  const auto external = putString(bytes, stringOffset, stringCursor, "external"_zc);
  const auto osKind = putString(bytes, stringOffset, stringCursor, "os-kind"_zc);
  const auto processorKind = putString(bytes, stringOffset, stringCursor, "processor-kind"_zc);
  const auto allocate = putString(bytes, stringOffset, stringCursor, "allocate"_zc);

  section(bytes, 0, 0, 0, 0);
  section(bytes, 1, 3, stringOffset, stringCursor - stringOffset);
  section(bytes, 2, 2, symbolOffset, 11 * 24, 1, 7, 24);
  section(bytes, 3, 1, textOffset, 32);
  section(bytes, 4, 1, tlsOffset, 16);
  section(bytes, 5, 4, relocationOffset, 2 * 24, 2, 3, 24);

  putSymbol(bytes, symbolOffset, 1, 0, 0x03, 0, 3, 0);
  putSymbol(bytes, symbolOffset, 2, absolute, 0x01, 2, 0xfff1, 8);
  putSymbol(bytes, symbolOffset, 3, common, 0x05, 0, 0xfff2, 8);
  putSymbol(bytes, symbolOffset, 4, duplicate, 0x00, 1, 3, 0);
  putSymbol(bytes, symbolOffset, 5, duplicate, 0x06, 3, 4, 8);
  putSymbol(bytes, symbolOffset, 6, file, 0x04, 0, 0xfff1, 0);
  putSymbol(bytes, symbolOffset, 7, external, 0x10, 0, 0, 0);
  putSymbol(bytes, symbolOffset, 8, osKind, 0x1a, 0, 3, 16);
  putSymbol(bytes, symbolOffset, 9, processorKind, 0x1d, 0, 3, 16);
  putSymbol(bytes, symbolOffset, 10, allocate, 0x12, 0, 3, 16);

  put64(bytes, relocationOffset, 0);
  put64(bytes, relocationOffset + 8, (uint64_t{4} << 32U) | 1U);
  put64(bytes, relocationOffset + 16, 0);
  put64(bytes, relocationOffset + 24, 8);
  put64(bytes, relocationOffset + 32, (uint64_t{1} << 32U) | 2U);
  put64(bytes, relocationOffset + 40, static_cast<uint64_t>(-4));
  return bytes.releaseAsArray();
}

zc::Vector<TrustedRuntimeOperationRecord> operations() {
  zc::Vector<TrustedRuntimeOperationRecord> result;
  result.add(TrustedRuntimeOperationRecord{TrustedRuntimeOperation::Allocate, {0, 2, 1}});
  return result;
}

zc::Array<TrustedRuntimeSymbolId> requiredOperations() {
  zc::Vector<TrustedRuntimeSymbolId> result;
  result.add(TrustedRuntimeSymbolId{0, 2, 1});
  return result.releaseAsArray();
}

zc::Vector<TrustedRuntimeOperationRecord> inventoryOperations() {
  zc::Vector<TrustedRuntimeOperationRecord> result;
  result.add(TrustedRuntimeOperationRecord{TrustedRuntimeOperation::Allocate, {0, 2, 10}});
  return result;
}

zc::Array<TrustedRuntimeSymbolId> requiredInventoryOperations() {
  zc::Vector<TrustedRuntimeSymbolId> result;
  result.add(TrustedRuntimeSymbolId{0, 2, 10});
  return result.releaseAsArray();
}

identity::Sha256Digest digest(zc::ArrayPtr<const uint8_t> bytes) {
  auto result = identity::sha256(bytes);
  ZC_IF_SOME(value, result) { return value; }
  ZC_FAIL_REQUIRE("ELF digest fixture failed");
}

}  // namespace

ZC_TEST("Trusted runtime ELF decoder inventories null symbols functions and relocations") {
  auto object = elf();
  auto decoded = TrustedRuntimeElfDecoder::decode(0, object);
  ZC_REQUIRE(decoded.is<DecodedTrustedRuntimeElfObject>());
  auto& value = decoded.get<DecodedTrustedRuntimeElfObject>();
  ZC_EXPECT(value.sectionCount() == 5);
  ZC_EXPECT(value.architecture() == TrustedRuntimeElfArchitecture::X86_64);
  ZC_EXPECT(!value.hasUnexpectedInitializer());
  auto symbols = value.releaseSymbols();
  auto relocations = value.releaseRelocations();
  ZC_REQUIRE(symbols.size() == 2);
  ZC_EXPECT(!symbols[0].isNamed());
  ZC_EXPECT(symbols[1].isNamed());
  ZC_EXPECT(symbols[1].name().asChars() == "func"_zc);
  ZC_REQUIRE(relocations.size() == 1);
  ZC_EXPECT(relocations[0].target == (TrustedRuntimeSymbolId{0, 2, 1}));

  zc::Vector<zc::Array<uint8_t>> objects;
  objects.add(elf());
  auto manifest =
      TrustedRuntimeElfDecoder::decodeManifest(objects, operations(), requiredOperations());
  ZC_REQUIRE(manifest.is<TrustedRuntimeManifestSet>());
  ZC_EXPECT(manifest.get<TrustedRuntimeManifestSet>().symbols().size() == 2);
  ZC_EXPECT(manifest.get<TrustedRuntimeManifestSet>().relocations().size() == 1);
  ZC_EXPECT(manifest.get<TrustedRuntimeManifestSet>().operations().size() == 1);
}

ZC_TEST("Trusted runtime ELF decoder preserves the complete positive inventory") {
  auto object = inventoryElf();
  auto decoded = TrustedRuntimeElfDecoder::decode(0, object);
  ZC_REQUIRE(decoded.is<DecodedTrustedRuntimeElfObject>());
  auto& value = decoded.get<DecodedTrustedRuntimeElfObject>();
  auto symbols = value.releaseSymbols();
  auto relocations = value.releaseRelocations();
  ZC_REQUIRE(symbols.size() == 11);
  ZC_EXPECT(symbols[0].kind() == TrustedRuntimeSymbolKindTag::NoType);
  ZC_EXPECT(symbols[1].kind() == TrustedRuntimeSymbolKindTag::Section);
  ZC_EXPECT(symbols[2].section() == TrustedRuntimeSymbolSectionTag::Absolute);
  ZC_EXPECT(symbols[3].section() == TrustedRuntimeSymbolSectionTag::Common);
  ZC_EXPECT(symbols[4].name().asChars() == "duplicate"_zc);
  ZC_EXPECT(symbols[5].name().asChars() == "duplicate"_zc);
  ZC_EXPECT(symbols[5].kind() == TrustedRuntimeSymbolKindTag::Tls);
  ZC_EXPECT(symbols[6].kind() == TrustedRuntimeSymbolKindTag::File);
  ZC_EXPECT(symbols[7].section() == TrustedRuntimeSymbolSectionTag::Undefined);
  ZC_EXPECT(symbols[8].kind() == TrustedRuntimeSymbolKindTag::OsSpecific);
  ZC_EXPECT(symbols[9].kind() == TrustedRuntimeSymbolKindTag::ProcessorSpecific);
  ZC_EXPECT(symbols[10].kind() == TrustedRuntimeSymbolKindTag::Function);
  ZC_REQUIRE(relocations.size() == 2);
  ZC_EXPECT(relocations[0].target == (TrustedRuntimeSymbolId{0, 2, 4}));
  ZC_EXPECT(relocations[1].target == (TrustedRuntimeSymbolId{0, 2, 1}));

  zc::Vector<zc::Array<uint8_t>> objects;
  objects.add(inventoryElf());
  auto manifest = TrustedRuntimeElfDecoder::decodeManifest(objects, inventoryOperations(),
                                                           requiredInventoryOperations());
  ZC_REQUIRE(manifest.is<TrustedRuntimeManifestSet>());
  ZC_EXPECT(manifest.get<TrustedRuntimeManifestSet>().symbols().size() == 11);
  ZC_EXPECT(manifest.get<TrustedRuntimeManifestSet>().relocations().size() == 2);
}

ZC_TEST("Trusted runtime ELF decoder rejects malformed class REL and relocation targets") {
  {
    auto object = elf();
    put32(object, 64 + 2 * 64 + 4, 11);
    auto result = TrustedRuntimeElfDecoder::decode(0, object);
    ZC_REQUIRE(result.is<DecodedTrustedRuntimeElfObject>());
  }
  {
    auto object = elf();
    object[390 + 24 + 4] = 0x02;
    auto result = TrustedRuntimeElfDecoder::decode(0, object);
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
  }
  {
    auto object = elf();
    put64(object, 390 + 16, 1);
    auto result = TrustedRuntimeElfDecoder::decode(0, object);
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
  }
  {
    auto object = elf();
    object[4] = 1;
    auto result = TrustedRuntimeElfDecoder::decode(0, object);
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
    ZC_EXPECT(result.get<TrustedRuntimeInvariantIssue>() ==
              TrustedRuntimeInvariantIssue::InvalidManifestRecord);
  }
  {
    auto object = elf();
    put32(object, 64 + 4 * 64 + 4, 9);
    auto result = TrustedRuntimeElfDecoder::decode(0, object);
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
  }
  {
    auto object = elf();
    put64(object, 454 + 8, (uint64_t{99} << 32U) | 1U);
    auto result = TrustedRuntimeElfDecoder::decode(0, object);
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
  }
}

ZC_TEST("Trusted runtime ELF manifest rejects initializer sections and mixed architectures") {
  {
    zc::Vector<zc::Array<uint8_t>> objects;
    objects.add(elf(62, 14));
    auto result =
        TrustedRuntimeElfDecoder::decodeManifest(objects, operations(), requiredOperations());
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
    ZC_EXPECT(result.get<TrustedRuntimeInvariantIssue>() ==
              TrustedRuntimeInvariantIssue::UnexpectedInitializer);
  }
  {
    zc::Vector<zc::Array<uint8_t>> objects;
    objects.add(elf(62));
    objects.add(elf(183));
    zc::Vector<TrustedRuntimeOperationRecord> noOperations;
    zc::Vector<TrustedRuntimeSymbolId> required;
    auto result = TrustedRuntimeElfDecoder::decodeManifest(objects, zc::mv(noOperations), required);
    ZC_REQUIRE(result.is<TrustedRuntimeInvariantIssue>());
    ZC_EXPECT(result.get<TrustedRuntimeInvariantIssue>() ==
              TrustedRuntimeInvariantIssue::InvalidManifestRecord);
  }
}

ZC_TEST("Trusted runtime ELF verifier constructs keys only from exact object and manifest bytes") {
  auto declaredObject = elf();
  zc::Vector<zc::Array<uint8_t>> declaredObjects;
  declaredObjects.add(elf());
  auto declared =
      TrustedRuntimeElfDecoder::decodeManifest(declaredObjects, operations(), requiredOperations());
  ZC_REQUIRE(declared.is<TrustedRuntimeManifestSet>());
  zc::Vector<identity::Sha256Digest> declaredDigests;
  declaredDigests.add(digest(declaredObject));
  zc::Vector<zc::Array<uint8_t>> actualObjects;
  actualObjects.add(elf());
  auto verified = TrustedRuntimeElfDecoder::verifyKey(
      "zom-v1"_zc, "zom-v1"_zc, zc::mv(actualObjects), zc::mv(declaredDigests),
      zc::mv(declared.get<TrustedRuntimeManifestSet>()), operations(), requiredOperations());
  ZC_EXPECT(verified.is<TrustedBuildRuntimeKey>());

  zc::Vector<zc::Array<uint8_t>> changedDeclaredObjects;
  changedDeclaredObjects.add(elf());
  auto changedDeclared = TrustedRuntimeElfDecoder::decodeManifest(
      changedDeclaredObjects, operations(), requiredOperations());
  ZC_REQUIRE(changedDeclared.is<TrustedRuntimeManifestSet>());
  auto changedObject = elf();
  changedObject[7] = 1;
  zc::Vector<zc::Array<uint8_t>> changedObjects;
  changedObjects.add(zc::mv(changedObject));
  zc::Vector<identity::Sha256Digest> unchangedDigests;
  unchangedDigests.add(digest(declaredObject));
  auto rejected = TrustedRuntimeElfDecoder::verifyKey(
      "zom-v1"_zc, "zom-v1"_zc, zc::mv(changedObjects), zc::mv(unchangedDigests),
      zc::mv(changedDeclared.get<TrustedRuntimeManifestSet>()), operations(), requiredOperations());
  ZC_REQUIRE(rejected.is<TrustedRuntimeInvariantIssue>());
  ZC_EXPECT(rejected.get<TrustedRuntimeInvariantIssue>() ==
            TrustedRuntimeInvariantIssue::ObjectDigestMismatch);
}

}  // namespace zomlang::compiler::driver::package
