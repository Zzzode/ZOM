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

#include "compiler/driver/package/trusted-runtime-elf.h"

#include "compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::driver::package {
namespace {

constexpr uint32_t kSectionSymbolTable = 2;
constexpr uint32_t kSectionStringTable = 3;
constexpr uint32_t kSectionRela = 4;
constexpr uint32_t kSectionNoBits = 8;
constexpr uint32_t kSectionRel = 9;
constexpr uint32_t kSectionDynamicSymbolTable = 11;
constexpr uint32_t kSectionInitArray = 14;
constexpr uint32_t kSectionFiniArray = 15;
constexpr uint32_t kSectionPreinitArray = 16;
constexpr uint16_t kUndefinedSection = 0;
constexpr uint16_t kAbsoluteSection = 0xfff1;
constexpr uint16_t kCommonSection = 0xfff2;
constexpr uint32_t kProgramLoad = 1;
constexpr uint32_t kProgramInterpreter = 3;
constexpr uint32_t kProgramGnuStack = 0x6474e551;
constexpr uint32_t kProgramFlagExecute = 0x01;
constexpr uint32_t kProgramFlagWrite = 0x02;
constexpr uint32_t kTargetNoteType = 0x5a4f4d01;
constexpr uint32_t kSectionNote = 7;

struct ElfSection final {
  uint32_t type;
  uint64_t offset;
  uint64_t size;
  uint32_t link;
  uint32_t info;
  uint64_t entrySize;
};

bool isSymbolTable(uint32_t type) {
  return type == kSectionSymbolTable || type == kSectionDynamicSymbolTable;
}

bool rangeFits(uint64_t offset, uint64_t size, size_t total) {
  return offset <= total && size <= total - offset;
}

bool sameBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  return left == right;
}

uint64_t align4(uint64_t value) {
  if (value > UINT64_MAX - 3) { return UINT64_MAX; }
  return (value + 3) & ~uint64_t{3};
}

zc::Maybe<uint16_t> readUint16(zc::ArrayPtr<const uint8_t> bytes, uint64_t offset) {
  if (!rangeFits(offset, 2, bytes.size())) { return zc::none; }
  return static_cast<uint16_t>(bytes[offset]) |
         static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8U);
}

zc::Maybe<uint32_t> readUint32(zc::ArrayPtr<const uint8_t> bytes, uint64_t offset) {
  if (!rangeFits(offset, 4, bytes.size())) { return zc::none; }
  uint32_t result = 0;
  for (uint32_t index = 0; index < 4; ++index) {
    result |= static_cast<uint32_t>(bytes[offset + index]) << (index * 8U);
  }
  return result;
}

zc::Maybe<uint64_t> readUint64(zc::ArrayPtr<const uint8_t> bytes, uint64_t offset) {
  if (!rangeFits(offset, 8, bytes.size())) { return zc::none; }
  uint64_t result = 0;
  for (uint32_t index = 0; index < 8; ++index) {
    result |= static_cast<uint64_t>(bytes[offset + index]) << (index * 8U);
  }
  return result;
}

zc::Maybe<zc::Array<uint8_t>> readString(zc::ArrayPtr<const uint8_t> bytes,
                                         const ElfSection& strings, uint32_t offset) {
  if (offset >= strings.size || !rangeFits(strings.offset, strings.size, bytes.size())) {
    return zc::none;
  }
  const uint64_t start = strings.offset + offset;
  uint64_t end = start;
  while (end < strings.offset + strings.size && bytes[end] != 0) { ++end; }
  if (end == strings.offset + strings.size) { return zc::none; }
  zc::Vector<uint8_t> result(end - start);
  result.addAll(bytes.slice(start, end));
  return result.releaseAsArray();
}

zc::Maybe<TrustedRuntimeSymbolKindTag> symbolKind(uint8_t native) {
  if (native <= 6) { return static_cast<TrustedRuntimeSymbolKindTag>(native + 1); }
  if (native >= 10 && native <= 12) { return TrustedRuntimeSymbolKindTag::OsSpecific; }
  if (native >= 13 && native <= 15) { return TrustedRuntimeSymbolKindTag::ProcessorSpecific; }
  return zc::none;
}

zc::Maybe<TrustedRuntimeSymbolBindingTag> symbolBinding(uint8_t native) {
  if (native <= 2) { return static_cast<TrustedRuntimeSymbolBindingTag>(native + 1); }
  if (native >= 10 && native <= 12) { return TrustedRuntimeSymbolBindingTag::OsSpecific; }
  if (native >= 13 && native <= 15) { return TrustedRuntimeSymbolBindingTag::ProcessorSpecific; }
  return zc::none;
}

zc::Maybe<TrustedRuntimeSymbolVisibility> symbolVisibility(uint8_t native) {
  if ((native & 0xfcU) != 0 || native > 3) { return zc::none; }
  return static_cast<TrustedRuntimeSymbolVisibility>(native + 1);
}

zc::Maybe<TrustedRuntimeSymbolSectionTag> symbolSection(uint16_t native, uint32_t sectionCount) {
  if (native == kUndefinedSection) { return TrustedRuntimeSymbolSectionTag::Undefined; }
  if (native == kAbsoluteSection) { return TrustedRuntimeSymbolSectionTag::Absolute; }
  if (native == kCommonSection) { return TrustedRuntimeSymbolSectionTag::Common; }
  if (native > 0 && native < sectionCount) { return TrustedRuntimeSymbolSectionTag::Section; }
  return zc::none;
}

zc::Maybe<ElfSection> readSection(zc::ArrayPtr<const uint8_t> bytes, uint64_t sectionOffset) {
  auto type = readUint32(bytes, sectionOffset + 4);
  auto offset = readUint64(bytes, sectionOffset + 24);
  auto size = readUint64(bytes, sectionOffset + 32);
  auto link = readUint32(bytes, sectionOffset + 40);
  auto info = readUint32(bytes, sectionOffset + 44);
  auto entrySize = readUint64(bytes, sectionOffset + 56);
  if (type == zc::none || offset == zc::none || size == zc::none || link == zc::none ||
      info == zc::none || entrySize == zc::none) {
    return zc::none;
  }
  ZC_IF_SOME(typeValue, type) {
    ZC_IF_SOME(offsetValue, offset) {
      ZC_IF_SOME(sizeValue, size) {
        ZC_IF_SOME(linkValue, link) {
          ZC_IF_SOME(infoValue, info) {
            ZC_IF_SOME(entrySizeValue, entrySize) {
              if (typeValue != kSectionNoBits && !rangeFits(offsetValue, sizeValue, bytes.size())) {
                return zc::none;
              }
              return ElfSection{typeValue, offsetValue, sizeValue,
                                linkValue, infoValue,   entrySizeValue};
            }
          }
        }
      }
    }
  }
  return zc::none;
}

bool decodeSymbols(uint32_t objectOrdinal, zc::ArrayPtr<const uint8_t> bytes,
                   zc::ArrayPtr<const ElfSection> sections,
                   zc::Vector<TrustedRuntimeSymbolRecord>& output) {
  for (uint32_t sectionOrdinal = 1; sectionOrdinal < sections.size(); ++sectionOrdinal) {
    const auto& table = sections[sectionOrdinal];
    if (!isSymbolTable(table.type)) { continue; }
    if (table.entrySize != 24 || table.size % table.entrySize != 0 ||
        table.link >= sections.size() || sections[table.link].type != kSectionStringTable) {
      return false;
    }
    const auto& strings = sections[table.link];
    const uint64_t count = table.size / table.entrySize;
    if (count == 0 || count > UINT32_MAX || table.info > count || strings.size == 0 ||
        bytes[strings.offset] != 0) {
      return false;
    }
    for (uint32_t symbolIndex = 0; symbolIndex < count; ++symbolIndex) {
      const uint64_t offset = table.offset + symbolIndex * table.entrySize;
      auto nameOffset = readUint32(bytes, offset);
      if (nameOffset == zc::none || !rangeFits(offset + 4, 20, bytes.size())) { return false; }
      const uint8_t info = bytes[offset + 4];
      const uint8_t other = bytes[offset + 5];
      auto section = readUint16(bytes, offset + 6);
      auto byteValue = readUint64(bytes, offset + 8);
      auto byteSize = readUint64(bytes, offset + 16);
      auto kind = symbolKind(info & 0x0fU);
      auto binding = symbolBinding(info >> 4U);
      auto visibility = symbolVisibility(other);
      if (section == zc::none || byteValue == zc::none || byteSize == zc::none ||
          kind == zc::none || binding == zc::none || visibility == zc::none) {
        return false;
      }
      ZC_IF_SOME(sectionValue, section) {
        auto sectionTag = symbolSection(sectionValue, sections.size());
        if (sectionTag == zc::none) { return false; }
        ZC_IF_SOME(nameOffsetValue, nameOffset) {
          auto name = readString(bytes, strings, nameOffsetValue);
          if (name == zc::none) { return false; }
          ZC_IF_SOME(nameValue, name) {
            ZC_IF_SOME(kindValue, kind) {
              ZC_IF_SOME(bindingValue, binding) {
                ZC_IF_SOME(visibilityValue, visibility) {
                  ZC_IF_SOME(sectionTagValue, sectionTag) {
                    ZC_IF_SOME(byteValueValue, byteValue) {
                      ZC_IF_SOME(byteSizeValue, byteSize) {
                        if ((symbolIndex < table.info) !=
                                (bindingValue == TrustedRuntimeSymbolBindingTag::Local) ||
                            (kindValue == TrustedRuntimeSymbolKindTag::Section &&
                             nameValue.size() != 0) ||
                            (symbolIndex == 0 &&
                             (nameOffsetValue != 0 || info != 0 || other != 0 ||
                              sectionValue != 0 || byteValueValue != 0 || byteSizeValue != 0))) {
                          return false;
                        }
                        const TrustedRuntimeSymbolId id{objectOrdinal, sectionOrdinal, symbolIndex};
                        const uint32_t ordinarySection =
                            sectionTagValue == TrustedRuntimeSymbolSectionTag::Section
                                ? sectionValue
                                : 0;
                        const uint8_t storedKind =
                            kindValue == TrustedRuntimeSymbolKindTag::OsSpecific ||
                                    kindValue == TrustedRuntimeSymbolKindTag::ProcessorSpecific
                                ? info & 0x0fU
                                : 0;
                        const uint8_t storedBinding =
                            bindingValue == TrustedRuntimeSymbolBindingTag::OsSpecific ||
                                    bindingValue ==
                                        TrustedRuntimeSymbolBindingTag::ProcessorSpecific
                                ? info >> 4U
                                : 0;
                        if (nameValue.size() == 0) {
                          output.add(TrustedRuntimeSymbolRecord::unnamed(
                              id, kindValue, storedKind, bindingValue, storedBinding,
                              visibilityValue, sectionTagValue, ordinarySection, byteSizeValue));
                        } else {
                          output.add(TrustedRuntimeSymbolRecord::named(
                              id, zc::mv(nameValue), kindValue, storedKind, bindingValue,
                              storedBinding, visibilityValue, sectionTagValue, ordinarySection,
                              byteSizeValue));
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  return true;
}

bool decodeRelocations(uint32_t objectOrdinal, zc::ArrayPtr<const uint8_t> bytes,
                       zc::ArrayPtr<const ElfSection> sections,
                       zc::Vector<TrustedRuntimeRelocationRecord>& output) {
  for (uint32_t relocationSection = 1; relocationSection < sections.size(); ++relocationSection) {
    const auto& table = sections[relocationSection];
    if (table.type == kSectionRel) { return false; }
    if (table.type != kSectionRela) { continue; }
    if (table.entrySize != 24 || table.size % table.entrySize != 0 ||
        table.link >= sections.size() || !isSymbolTable(sections[table.link].type) ||
        table.info == 0 || table.info >= sections.size()) {
      return false;
    }
    const auto& symbols = sections[table.link];
    if (symbols.entrySize != 24 || symbols.size % symbols.entrySize != 0) { return false; }
    const uint64_t symbolCount = symbols.size / symbols.entrySize;
    const uint64_t count = table.size / table.entrySize;
    for (uint64_t index = 0; index < count; ++index) {
      const uint64_t entry = table.offset + index * table.entrySize;
      auto byteOffset = readUint64(bytes, entry);
      auto info = readUint64(bytes, entry + 8);
      auto addend = readUint64(bytes, entry + 16);
      if (byteOffset == zc::none || info == zc::none || addend == zc::none) { return false; }
      ZC_IF_SOME(byteOffsetValue, byteOffset) {
        ZC_IF_SOME(infoValue, info) {
          const uint64_t symbolIndex = infoValue >> 32U;
          if (symbolIndex >= symbolCount || symbolIndex > UINT32_MAX ||
              byteOffsetValue >= sections[table.info].size) {
            return false;
          }
          ZC_IF_SOME(addendValue, addend) {
            output.add(TrustedRuntimeRelocationRecord{
                objectOrdinal,
                table.info,
                byteOffsetValue,
                static_cast<uint32_t>(infoValue),
                {objectOrdinal, table.link, static_cast<uint32_t>(symbolIndex)},
                static_cast<int64_t>(addendValue)});
          }
        }
      }
    }
  }
  return true;
}

bool hasExactTargetNote(zc::ArrayPtr<const uint8_t> bytes, zc::ArrayPtr<const ElfSection> sections,
                        zc::ArrayPtr<const uint8_t> expectedDescriptor) {
  bool found = false;
  constexpr uint8_t noteName[] = {'Z', 'O', 'M', 0};
  for (const auto& section : sections) {
    if (section.type != kSectionNote) { continue; }
    uint64_t cursor = section.offset;
    const uint64_t end = section.offset + section.size;
    while (cursor < end) {
      auto nameSize = readUint32(bytes, cursor);
      auto descriptorSize = readUint32(bytes, cursor + 4);
      auto type = readUint32(bytes, cursor + 8);
      if (nameSize == zc::none || descriptorSize == zc::none || type == zc::none) { return false; }
      ZC_IF_SOME(nameSizeValue, nameSize) {
        ZC_IF_SOME(descriptorSizeValue, descriptorSize) {
          ZC_IF_SOME(typeValue, type) {
            const uint64_t paddedName = align4(nameSizeValue);
            const uint64_t paddedDescriptor = align4(descriptorSizeValue);
            if (paddedName == UINT64_MAX || paddedDescriptor == UINT64_MAX || end - cursor < 12 ||
                paddedName > end - cursor - 12 ||
                paddedDescriptor > end - cursor - 12 - paddedName) {
              return false;
            }
            const uint64_t nameOffset = cursor + 12;
            const uint64_t descriptorOffset = nameOffset + paddedName;
            if (typeValue == kTargetNoteType && nameSizeValue == zc::size(noteName) &&
                sameBytes(bytes.slice(nameOffset, nameOffset + nameSizeValue),
                          zc::arrayPtr(noteName))) {
              if (found || descriptorSizeValue != expectedDescriptor.size() ||
                  !sameBytes(bytes.slice(descriptorOffset, descriptorOffset + descriptorSizeValue),
                             expectedDescriptor)) {
                return false;
              }
              found = true;
            }
            cursor = descriptorOffset + paddedDescriptor;
          }
        }
      }
    }
  }
  return found;
}

bool isVerifiedStaticPie(zc::ArrayPtr<const uint8_t> bytes,
                         const RegisteredTargetSelection& target) {
  if (bytes.size() < 64 || bytes[0] != 0x7f || bytes[1] != 'E' || bytes[2] != 'L' ||
      bytes[3] != 'F' || bytes[4] != 2 || bytes[5] != 1 || bytes[6] != 1) {
    return false;
  }
  auto type = readUint16(bytes, 16);
  auto machine = readUint16(bytes, 18);
  auto version = readUint32(bytes, 20);
  auto entry = readUint64(bytes, 24);
  auto programOffset = readUint64(bytes, 32);
  auto sectionOffset = readUint64(bytes, 40);
  auto headerSize = readUint16(bytes, 52);
  auto programEntrySize = readUint16(bytes, 54);
  auto programCount = readUint16(bytes, 56);
  auto sectionEntrySize = readUint16(bytes, 58);
  auto sectionCount = readUint16(bytes, 60);
  if (type == zc::none || machine == zc::none || version == zc::none || entry == zc::none ||
      programOffset == zc::none || sectionOffset == zc::none || headerSize == zc::none ||
      programEntrySize == zc::none || programCount == zc::none || sectionEntrySize == zc::none ||
      sectionCount == zc::none) {
    return false;
  }
  ZC_IF_SOME(typeValue, type) {
    ZC_IF_SOME(machineValue, machine) {
      ZC_IF_SOME(versionValue, version) {
        ZC_IF_SOME(entryValue, entry) {
          ZC_IF_SOME(programOffsetValue, programOffset) {
            ZC_IF_SOME(sectionOffsetValue, sectionOffset) {
              ZC_IF_SOME(headerSizeValue, headerSize) {
                ZC_IF_SOME(programEntrySizeValue, programEntrySize) {
                  ZC_IF_SOME(programCountValue, programCount) {
                    ZC_IF_SOME(sectionEntrySizeValue, sectionEntrySize) {
                      ZC_IF_SOME(sectionCountValue, sectionCount) {
                        const auto& projection = target.semanticProjection();
                        const uint16_t expectedMachine =
                            projection.architecture() == "x86_64"_zc    ? 62
                            : projection.architecture() == "aarch64"_zc ? 183
                                                                        : 0;
                        if (typeValue != 3 || expectedMachine == 0 ||
                            machineValue != expectedMachine || projection.pointerWidth() != 64 ||
                            projection.endianness() != identity::Endianness::Little ||
                            versionValue != 1 || entryValue == 0 || headerSizeValue != 64 ||
                            programEntrySizeValue != 56 || programCountValue == 0 ||
                            sectionEntrySizeValue != 64 || sectionCountValue == 0 ||
                            !rangeFits(programOffsetValue,
                                       static_cast<uint64_t>(programCountValue) * 56,
                                       bytes.size()) ||
                            !rangeFits(sectionOffsetValue,
                                       static_cast<uint64_t>(sectionCountValue) * 64,
                                       bytes.size())) {
                          return false;
                        }
                        bool executableEntry = false;
                        for (uint32_t index = 0; index < programCountValue; ++index) {
                          const uint64_t offset = programOffsetValue + index * 56;
                          auto programType = readUint32(bytes, offset);
                          auto flags = readUint32(bytes, offset + 4);
                          auto fileOffset = readUint64(bytes, offset + 8);
                          auto virtualAddress = readUint64(bytes, offset + 16);
                          auto fileSize = readUint64(bytes, offset + 32);
                          auto memorySize = readUint64(bytes, offset + 40);
                          if (programType == zc::none || flags == zc::none ||
                              fileOffset == zc::none || virtualAddress == zc::none ||
                              fileSize == zc::none || memorySize == zc::none) {
                            return false;
                          }
                          ZC_IF_SOME(programTypeValue, programType) {
                            ZC_IF_SOME(flagsValue, flags) {
                              ZC_IF_SOME(fileOffsetValue, fileOffset) {
                                ZC_IF_SOME(virtualAddressValue, virtualAddress) {
                                  ZC_IF_SOME(fileSizeValue, fileSize) {
                                    ZC_IF_SOME(memorySizeValue, memorySize) {
                                      if (programTypeValue == kProgramInterpreter ||
                                          ((flagsValue & kProgramFlagExecute) != 0 &&
                                           (flagsValue & kProgramFlagWrite) != 0) ||
                                          (programTypeValue == kProgramGnuStack &&
                                           (flagsValue & kProgramFlagExecute) != 0) ||
                                          fileSizeValue > memorySizeValue ||
                                          !rangeFits(fileOffsetValue, fileSizeValue,
                                                     bytes.size())) {
                                        return false;
                                      }
                                      if (programTypeValue == kProgramLoad &&
                                          (flagsValue & kProgramFlagExecute) != 0 &&
                                          entryValue >= virtualAddressValue &&
                                          entryValue - virtualAddressValue < memorySizeValue) {
                                        executableEntry = true;
                                      }
                                    }
                                  }
                                }
                              }
                            }
                          }
                        }
                        if (!executableEntry) { return false; }
                        zc::Vector<ElfSection> sections(sectionCountValue);
                        for (uint32_t index = 0; index < sectionCountValue; ++index) {
                          auto decoded = readSection(bytes, sectionOffsetValue + index * 64);
                          if (decoded == zc::none) { return false; }
                          ZC_IF_SOME(section, decoded) {
                            if ((index == 0 && section.type != 0) ||
                                section.type == kSectionInitArray ||
                                section.type == kSectionFiniArray ||
                                section.type == kSectionPreinitArray) {
                              return false;
                            }
                            sections.add(section);
                          }
                        }
                        identity::CanonicalEncoder encoder;
                        target.encode(encoder);
                        return hasExactTargetNote(bytes, sections, encoder.finish());
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  return false;
}

}  // namespace

struct VerifiedBuildScriptExecutable::Impl final {
  Impl(BuildScriptExecutableKey&& key, zc::Array<uint8_t>&& bytes) noexcept
      : key(zc::mv(key)), bytes(zc::mv(bytes)) {}
  BuildScriptExecutableKey key;
  zc::Array<uint8_t> bytes;
};

VerifiedBuildScriptExecutable::VerifiedBuildScriptExecutable(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}

zc::OneOf<VerifiedBuildScriptExecutable, BuildScriptIssue> VerifiedBuildScriptExecutable::verify(
    BuildScriptExecutableKey&& key, zc::Array<uint8_t>&& imageBytes) {
  auto actualDigest = identity::sha256(imageBytes);
  if (actualDigest == zc::none) { return BuildScriptIssue::ExecutableIdentityMismatch; }
  ZC_IF_SOME(digest, actualDigest) {
    if (digest != key.imageDigest() || !isVerifiedStaticPie(imageBytes, key.target())) {
      return BuildScriptIssue::ExecutableIdentityMismatch;
    }
  }
  return VerifiedBuildScriptExecutable(zc::heap<Impl>(zc::mv(key), zc::mv(imageBytes)));
}

VerifiedBuildScriptExecutable::~VerifiedBuildScriptExecutable() noexcept = default;
VerifiedBuildScriptExecutable::VerifiedBuildScriptExecutable(
    VerifiedBuildScriptExecutable&&) noexcept = default;
VerifiedBuildScriptExecutable& VerifiedBuildScriptExecutable::operator=(
    VerifiedBuildScriptExecutable&&) noexcept = default;

const BuildScriptExecutableKey& VerifiedBuildScriptExecutable::key() const noexcept {
  return impl->key;
}

zc::ArrayPtr<const uint8_t> VerifiedBuildScriptExecutable::bytes() const noexcept {
  return impl->bytes;
}

DecodedTrustedRuntimeElfObject::DecodedTrustedRuntimeElfObject(
    uint32_t sectionCount, TrustedRuntimeElfArchitecture architecture,
    zc::Vector<TrustedRuntimeSymbolRecord>&& symbols,
    zc::Vector<TrustedRuntimeRelocationRecord>&& relocations,
    bool hasUnexpectedInitializer) noexcept
    : sectionCountValue(sectionCount),
      architectureValue(architecture),
      symbolValues(zc::mv(symbols)),
      relocationValues(zc::mv(relocations)),
      unexpectedInitializerValue(hasUnexpectedInitializer) {}

uint32_t DecodedTrustedRuntimeElfObject::sectionCount() const noexcept { return sectionCountValue; }

TrustedRuntimeElfArchitecture DecodedTrustedRuntimeElfObject::architecture() const noexcept {
  return architectureValue;
}

bool DecodedTrustedRuntimeElfObject::hasUnexpectedInitializer() const noexcept {
  return unexpectedInitializerValue;
}

zc::Vector<TrustedRuntimeSymbolRecord> DecodedTrustedRuntimeElfObject::releaseSymbols() {
  return zc::mv(symbolValues);
}

zc::Vector<TrustedRuntimeRelocationRecord> DecodedTrustedRuntimeElfObject::releaseRelocations() {
  return zc::mv(relocationValues);
}

TrustedRuntimeElfDecodeResult TrustedRuntimeElfDecoder::decode(
    uint32_t objectOrdinal, zc::ArrayPtr<const uint8_t> objectBytes) {
  if (objectBytes.size() < 64 || objectBytes[0] != 0x7f || objectBytes[1] != 'E' ||
      objectBytes[2] != 'L' || objectBytes[3] != 'F' || objectBytes[4] != 2 ||
      objectBytes[5] != 1 || objectBytes[6] != 1) {
    return TrustedRuntimeInvariantIssue::InvalidManifestRecord;
  }
  auto type = readUint16(objectBytes, 16);
  auto machine = readUint16(objectBytes, 18);
  auto version = readUint32(objectBytes, 20);
  auto sectionOffset = readUint64(objectBytes, 40);
  auto headerSize = readUint16(objectBytes, 52);
  auto sectionEntrySize = readUint16(objectBytes, 58);
  auto sectionCount = readUint16(objectBytes, 60);
  if (type == zc::none || machine == zc::none || version == zc::none || sectionOffset == zc::none ||
      headerSize == zc::none || sectionEntrySize == zc::none || sectionCount == zc::none) {
    return TrustedRuntimeInvariantIssue::InvalidManifestRecord;
  }
  ZC_IF_SOME(typeValue, type) {
    ZC_IF_SOME(machineValue, machine) {
      ZC_IF_SOME(versionValue, version) {
        ZC_IF_SOME(sectionOffsetValue, sectionOffset) {
          ZC_IF_SOME(headerSizeValue, headerSize) {
            ZC_IF_SOME(sectionEntrySizeValue, sectionEntrySize) {
              ZC_IF_SOME(sectionCountValue, sectionCount) {
                if (typeValue != 1 || versionValue != 1 || headerSizeValue != 64 ||
                    sectionEntrySizeValue != 64 || sectionCountValue == 0 ||
                    !rangeFits(sectionOffsetValue, static_cast<uint64_t>(sectionCountValue) * 64,
                               objectBytes.size())) {
                  return TrustedRuntimeInvariantIssue::InvalidManifestRecord;
                }
                TrustedRuntimeElfArchitecture architecture;
                if (machineValue == 62) {
                  architecture = TrustedRuntimeElfArchitecture::X86_64;
                } else if (machineValue == 183) {
                  architecture = TrustedRuntimeElfArchitecture::AArch64;
                } else {
                  return TrustedRuntimeInvariantIssue::InvalidManifestRecord;
                }
                zc::Vector<ElfSection> sections(sectionCountValue);
                bool initializer = false;
                for (uint32_t index = 0; index < sectionCountValue; ++index) {
                  auto sectionValue = readSection(objectBytes, sectionOffsetValue + index * 64);
                  if (sectionValue == zc::none) {
                    return TrustedRuntimeInvariantIssue::InvalidManifestRecord;
                  }
                  ZC_IF_SOME(section, sectionValue) {
                    if (index == 0 && section.type != 0) {
                      return TrustedRuntimeInvariantIssue::InvalidManifestRecord;
                    }
                    initializer = initializer || section.type == kSectionInitArray ||
                                  section.type == kSectionFiniArray ||
                                  section.type == kSectionPreinitArray;
                    sections.add(section);
                  }
                }
                zc::Vector<TrustedRuntimeSymbolRecord> symbols;
                zc::Vector<TrustedRuntimeRelocationRecord> relocations;
                if (!decodeSymbols(objectOrdinal, objectBytes, sections, symbols) ||
                    !decodeRelocations(objectOrdinal, objectBytes, sections, relocations)) {
                  return TrustedRuntimeInvariantIssue::InvalidManifestRecord;
                }
                return DecodedTrustedRuntimeElfObject(sectionCountValue, architecture,
                                                      zc::mv(symbols), zc::mv(relocations),
                                                      initializer);
              }
            }
          }
        }
      }
    }
  }
  return TrustedRuntimeInvariantIssue::InvalidManifestRecord;
}

zc::OneOf<TrustedRuntimeManifestSet, TrustedRuntimeInvariantIssue>
TrustedRuntimeElfDecoder::decodeManifest(
    zc::ArrayPtr<const zc::Array<uint8_t>> objectBytes,
    zc::Vector<TrustedRuntimeOperationRecord>&& operations,
    zc::ArrayPtr<const TrustedRuntimeSymbolId> requiredOperationSymbols) {
  if (objectBytes.size() == 0) { return TrustedRuntimeInvariantIssue::EmptyObjectSet; }
  if (objectBytes.size() > UINT32_MAX) {
    return TrustedRuntimeInvariantIssue::InvalidManifestRecord;
  }
  zc::Vector<uint32_t> sectionCounts(objectBytes.size());
  zc::Vector<TrustedRuntimeSymbolRecord> symbols;
  zc::Vector<TrustedRuntimeRelocationRecord> relocations;
  bool initializer = false;
  zc::Maybe<TrustedRuntimeElfArchitecture> architecture;
  for (uint32_t index = 0; index < objectBytes.size(); ++index) {
    auto decoded = decode(index, objectBytes[index]);
    if (decoded.is<TrustedRuntimeInvariantIssue>()) {
      return decoded.get<TrustedRuntimeInvariantIssue>();
    }
    auto& object = decoded.get<DecodedTrustedRuntimeElfObject>();
    ZC_IF_SOME(firstArchitecture, architecture) {
      if (firstArchitecture != object.architecture()) {
        return TrustedRuntimeInvariantIssue::InvalidManifestRecord;
      }
    }
    else { architecture = object.architecture(); }
    sectionCounts.add(object.sectionCount());
    initializer = initializer || object.hasUnexpectedInitializer();
    auto objectSymbols = object.releaseSymbols();
    for (auto& symbol : objectSymbols) { symbols.add(zc::mv(symbol)); }
    auto objectRelocations = object.releaseRelocations();
    relocations.addAll(objectRelocations);
  }
  return TrustedRuntimeManifestSet::verify(sectionCounts, zc::mv(symbols), zc::mv(relocations),
                                           zc::mv(operations), requiredOperationSymbols,
                                           initializer);
}

zc::OneOf<TrustedBuildRuntimeKey, TrustedRuntimeInvariantIssue> TrustedRuntimeElfDecoder::verifyKey(
    zc::StringPtr expectedRuntimeAbi, zc::StringPtr runtimeAbi,
    zc::Vector<zc::Array<uint8_t>>&& objectBytes,
    zc::Vector<identity::Sha256Digest>&& declaredObjectDigests,
    TrustedRuntimeManifestSet&& declaredManifest,
    zc::Vector<TrustedRuntimeOperationRecord>&& observedOperations,
    zc::ArrayPtr<const TrustedRuntimeSymbolId> requiredOperationSymbols) {
  auto observed = decodeManifest(objectBytes, zc::mv(observedOperations), requiredOperationSymbols);
  if (observed.is<TrustedRuntimeInvariantIssue>()) {
    return observed.get<TrustedRuntimeInvariantIssue>();
  }
  auto evidence = TrustedRuntimeVerificationEvidence::verify(
      zc::mv(declaredObjectDigests), zc::mv(declaredManifest),
      observed.get<TrustedRuntimeManifestSet>());
  if (evidence.is<TrustedRuntimeInvariantIssue>()) {
    return evidence.get<TrustedRuntimeInvariantIssue>();
  }
  return TrustedBuildRuntimeKey::verifyEvidence(
      expectedRuntimeAbi, runtimeAbi, zc::mv(objectBytes),
      zc::mv(evidence.get<TrustedRuntimeVerificationEvidence>()));
}

}  // namespace zomlang::compiler::driver::package
