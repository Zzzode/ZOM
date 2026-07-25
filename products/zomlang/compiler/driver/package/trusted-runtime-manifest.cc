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

#include "zomlang/compiler/driver/package/trusted-runtime-manifest.h"

#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::driver::package {
namespace {

zc::Array<uint8_t> copyBytes(zc::ArrayPtr<const uint8_t> input) {
  zc::Vector<uint8_t> output(input.size());
  output.addAll(input);
  return output.releaseAsArray();
}

identity::Sha256Digest digest(zc::ArrayPtr<const uint8_t> input) {
  auto result = identity::sha256(input);
  ZC_IF_SOME(value, result) { return value; }
  ZC_UNREACHABLE;
}

template <typename Value>
bool sortUnique(zc::Vector<Value>& values) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    const auto currentBytes = current.encode();
    size_t insertion = index;
    while (insertion > 0 && currentBytes.asPtr() < values[insertion - 1].encode().asPtr()) {
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
  for (size_t index = 1; index < values.size(); ++index) {
    if (values[index - 1].encode().asPtr() == values[index].encode().asPtr()) { return false; }
  }
  return true;
}

bool validKind(TrustedRuntimeSymbolKindTag tag, uint8_t native) {
  if (tag >= TrustedRuntimeSymbolKindTag::NoType && tag <= TrustedRuntimeSymbolKindTag::Tls) {
    return native == 0;
  }
  if (tag == TrustedRuntimeSymbolKindTag::OsSpecific) { return native >= 10 && native <= 12; }
  if (tag == TrustedRuntimeSymbolKindTag::ProcessorSpecific) {
    return native >= 13 && native <= 15;
  }
  return false;
}

bool validBinding(TrustedRuntimeSymbolBindingTag tag, uint8_t native) {
  if (tag >= TrustedRuntimeSymbolBindingTag::Local && tag <= TrustedRuntimeSymbolBindingTag::Weak) {
    return native == 0;
  }
  if (tag == TrustedRuntimeSymbolBindingTag::OsSpecific) { return native >= 10 && native <= 12; }
  if (tag == TrustedRuntimeSymbolBindingTag::ProcessorSpecific) {
    return native >= 13 && native <= 15;
  }
  return false;
}

bool validVisibility(TrustedRuntimeSymbolVisibility value) {
  return value >= TrustedRuntimeSymbolVisibility::Default &&
         value <= TrustedRuntimeSymbolVisibility::Protected;
}

bool validSection(TrustedRuntimeSymbolSectionTag tag, uint32_t ordinal) {
  if (tag >= TrustedRuntimeSymbolSectionTag::Undefined &&
      tag <= TrustedRuntimeSymbolSectionTag::Common) {
    return ordinal == 0;
  }
  return tag == TrustedRuntimeSymbolSectionTag::Section && ordinal != 0;
}

bool validName(zc::ArrayPtr<const uint8_t> name) {
  if (name.size() == 0) { return false; }
  for (const auto value : name) {
    if (value == 0 || value < 0x20 || value == 0x7f) { return false; }
  }
  return true;
}

bool encodeSymbolName(identity::CanonicalEncoder& encoder, TrustedRuntimeSymbolNameTag tag,
                      zc::ArrayPtr<const uint8_t> name) {
  if (tag == TrustedRuntimeSymbolNameTag::Unnamed) {
    if (name.size() != 0) { return false; }
    encoder.encodeUint8(static_cast<uint8_t>(tag));
    return true;
  }
  if (tag == TrustedRuntimeSymbolNameTag::Named) {
    if (!validName(name)) { return false; }
    encoder.encodeUint8(static_cast<uint8_t>(tag));
    encoder.encodeByteString(name);
    return true;
  }
  return false;
}

template <typename Value>
identity::Sha256Digest manifestDigest(TrustedRuntimeManifestKind kind,
                                      zc::ArrayPtr<const Value> values) {
  zc::Vector<zc::Array<uint8_t>> records(values.size());
  for (const auto& value : values) { records.add(value.encode()); }
  return digestTrustedRuntimeManifestFrames(kind, records);
}

bool hasSymbol(zc::ArrayPtr<const TrustedRuntimeSymbolRecord> symbols,
               const TrustedRuntimeSymbolId& id) {
  for (const auto& symbol : symbols) {
    if (symbol.id() == id) { return true; }
  }
  return false;
}

zc::Maybe<size_t> findSymbol(zc::ArrayPtr<const TrustedRuntimeSymbolRecord> symbols,
                             const TrustedRuntimeSymbolId& id) {
  for (size_t index = 0; index < symbols.size(); ++index) {
    if (symbols[index].id() == id) { return index; }
  }
  return zc::none;
}

template <typename Value>
bool sameRecords(zc::ArrayPtr<const Value> left, zc::ArrayPtr<const Value> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].encode().asPtr() != right[index].encode().asPtr()) { return false; }
  }
  return true;
}

bool observedHasUnknownSymbol(const TrustedRuntimeManifestSet& declared,
                              const TrustedRuntimeManifestSet& observed) {
  for (const auto& symbol : observed.symbols()) {
    if (!hasSymbol(declared.symbols(), symbol.id())) { return true; }
  }
  return false;
}

bool observedHasUnknownRelocation(const TrustedRuntimeManifestSet& declared,
                                  const TrustedRuntimeManifestSet& observed) {
  for (const auto& relocation : observed.relocations()) {
    bool found = false;
    const auto bytes = relocation.encode();
    for (const auto& expected : declared.relocations()) {
      if (bytes.asPtr() == expected.encode().asPtr()) {
        found = true;
        break;
      }
    }
    if (!found) { return true; }
  }
  return false;
}

}  // namespace

identity::Sha256Digest digestTrustedRuntimeManifestFrames(
    TrustedRuntimeManifestKind kind, zc::ArrayPtr<const zc::Array<uint8_t>> records) {
  zc::StringPtr domain;
  switch (kind) {
    case TrustedRuntimeManifestKind::Symbols:
      domain = "zom.build-runtime-symbols"_zc;
      break;
    case TrustedRuntimeManifestKind::Relocations:
      domain = "zom.build-runtime-relocations"_zc;
      break;
    case TrustedRuntimeManifestKind::Operations:
      domain = "zom.build-runtime-operations"_zc;
      break;
  }
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(records.size());
  for (const auto& record : records) { encoder.encodeByteString(record); }
  auto framed = encoder.finish();
  zc::Vector<uint8_t> preimage(domain.size() + 1 + framed.size());
  preimage.addAll(domain.asBytes());
  preimage.add(0);
  preimage.addAll(framed);
  return digest(preimage);
}

zc::OneOf<zc::Array<uint8_t>, TrustedRuntimeInvariantIssue> encodeTrustedRuntimeSymbolName(
    TrustedRuntimeSymbolNameTag tag, zc::ArrayPtr<const uint8_t> name) {
  identity::CanonicalEncoder encoder;
  if (!encodeSymbolName(encoder, tag, name)) {
    return TrustedRuntimeInvariantIssue::InvalidManifestRecord;
  }
  return encoder.finish();
}

zc::OneOf<identity::Sha256Digest, TrustedRuntimeInvariantIssue> digestTrustedRuntimeSymbolName(
    TrustedRuntimeSymbolNameTag tag, zc::ArrayPtr<const uint8_t> name) {
  auto encoded = encodeTrustedRuntimeSymbolName(tag, name);
  if (encoded.is<TrustedRuntimeInvariantIssue>()) {
    return encoded.get<TrustedRuntimeInvariantIssue>();
  }
  constexpr auto domain = "zom.build-runtime-symbol-name"_zc;
  const auto& tagged = encoded.get<zc::Array<uint8_t>>();
  zc::Vector<uint8_t> preimage(domain.size() + 1 + tagged.size());
  preimage.addAll(domain.asBytes());
  preimage.add(0);
  preimage.addAll(tagged);
  return digest(preimage);
}

void TrustedRuntimeSymbolId::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeUint32(objectOrdinal);
  encoder.encodeUint32(symbolTableSectionOrdinal);
  encoder.encodeUint32(symbolIndex);
}

bool TrustedRuntimeSymbolId::operator==(const TrustedRuntimeSymbolId& other) const noexcept {
  return objectOrdinal == other.objectOrdinal &&
         symbolTableSectionOrdinal == other.symbolTableSectionOrdinal &&
         symbolIndex == other.symbolIndex;
}

TrustedRuntimeSymbolRecord::TrustedRuntimeSymbolRecord(
    TrustedRuntimeSymbolId id, bool named, zc::Array<uint8_t>&& name,
    TrustedRuntimeSymbolKindTag kind, uint8_t nativeKind, TrustedRuntimeSymbolBindingTag binding,
    uint8_t nativeBinding, TrustedRuntimeSymbolVisibility visibility,
    TrustedRuntimeSymbolSectionTag section, uint32_t sectionOrdinal, uint64_t byteSize) noexcept
    : idValue(id),
      namedValue(named),
      nameValue(zc::mv(name)),
      kindValue(kind),
      nativeKindValue(nativeKind),
      bindingValue(binding),
      nativeBindingValue(nativeBinding),
      visibilityValue(visibility),
      sectionValue(section),
      sectionOrdinalValue(sectionOrdinal),
      byteSizeValue(byteSize) {}

TrustedRuntimeSymbolRecord TrustedRuntimeSymbolRecord::unnamed(
    TrustedRuntimeSymbolId id, TrustedRuntimeSymbolKindTag kind, uint8_t nativeKind,
    TrustedRuntimeSymbolBindingTag binding, uint8_t nativeBinding,
    TrustedRuntimeSymbolVisibility visibility, TrustedRuntimeSymbolSectionTag section,
    uint32_t sectionOrdinal, uint64_t byteSize) {
  zc::Array<uint8_t> name;
  return TrustedRuntimeSymbolRecord(id, false, zc::mv(name), kind, nativeKind, binding,
                                    nativeBinding, visibility, section, sectionOrdinal, byteSize);
}

TrustedRuntimeSymbolRecord TrustedRuntimeSymbolRecord::named(
    TrustedRuntimeSymbolId id, zc::Array<uint8_t>&& name, TrustedRuntimeSymbolKindTag kind,
    uint8_t nativeKind, TrustedRuntimeSymbolBindingTag binding, uint8_t nativeBinding,
    TrustedRuntimeSymbolVisibility visibility, TrustedRuntimeSymbolSectionTag section,
    uint32_t sectionOrdinal, uint64_t byteSize) {
  return TrustedRuntimeSymbolRecord(id, true, zc::mv(name), kind, nativeKind, binding,
                                    nativeBinding, visibility, section, sectionOrdinal, byteSize);
}

TrustedRuntimeSymbolRecord TrustedRuntimeSymbolRecord::clone() const {
  return TrustedRuntimeSymbolRecord(
      idValue, namedValue, copyBytes(nameValue), kindValue, nativeKindValue, bindingValue,
      nativeBindingValue, visibilityValue, sectionValue, sectionOrdinalValue, byteSizeValue);
}

const TrustedRuntimeSymbolId& TrustedRuntimeSymbolRecord::id() const noexcept { return idValue; }

bool TrustedRuntimeSymbolRecord::isNamed() const noexcept { return namedValue; }

zc::ArrayPtr<const uint8_t> TrustedRuntimeSymbolRecord::name() const noexcept { return nameValue; }

TrustedRuntimeSymbolKindTag TrustedRuntimeSymbolRecord::kind() const noexcept { return kindValue; }

TrustedRuntimeSymbolBindingTag TrustedRuntimeSymbolRecord::binding() const noexcept {
  return bindingValue;
}

TrustedRuntimeSymbolSectionTag TrustedRuntimeSymbolRecord::section() const noexcept {
  return sectionValue;
}

void TrustedRuntimeSymbolRecord::encode(identity::CanonicalEncoder& encoder) const {
  idValue.encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(namedValue ? TrustedRuntimeSymbolNameTag::Named
                                                      : TrustedRuntimeSymbolNameTag::Unnamed));
  if (namedValue) { encoder.encodeByteString(nameValue); }
  encoder.encodeUint8(static_cast<uint8_t>(kindValue));
  if (kindValue == TrustedRuntimeSymbolKindTag::OsSpecific ||
      kindValue == TrustedRuntimeSymbolKindTag::ProcessorSpecific) {
    encoder.encodeUint8(nativeKindValue);
  }
  encoder.encodeUint8(static_cast<uint8_t>(bindingValue));
  if (bindingValue == TrustedRuntimeSymbolBindingTag::OsSpecific ||
      bindingValue == TrustedRuntimeSymbolBindingTag::ProcessorSpecific) {
    encoder.encodeUint8(nativeBindingValue);
  }
  encoder.encodeUint8(static_cast<uint8_t>(visibilityValue));
  encoder.encodeUint8(static_cast<uint8_t>(sectionValue));
  if (sectionValue == TrustedRuntimeSymbolSectionTag::Section) {
    encoder.encodeUint32(sectionOrdinalValue);
  }
  encoder.encodeUint64(byteSizeValue);
}

zc::Array<uint8_t> TrustedRuntimeSymbolRecord::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

void TrustedRuntimeRelocationRecord::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeUint32(objectOrdinal);
  encoder.encodeUint32(sectionOrdinal);
  encoder.encodeUint64(byteOffset);
  encoder.encodeUint32(kind);
  target.encode(encoder);
  encoder.encodeUint64(static_cast<uint64_t>(addend));
}

zc::Array<uint8_t> TrustedRuntimeRelocationRecord::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

void TrustedRuntimeOperationRecord::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(operation));
  symbol.encode(encoder);
}

zc::Array<uint8_t> TrustedRuntimeOperationRecord::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

TrustedRuntimeManifestSet::TrustedRuntimeManifestSet(
    zc::Vector<TrustedRuntimeSymbolRecord>&& symbols,
    zc::Vector<TrustedRuntimeRelocationRecord>&& relocations,
    zc::Vector<TrustedRuntimeOperationRecord>&& operations,
    const identity::Sha256Digest& symbolDigest, const identity::Sha256Digest& relocationDigest,
    const identity::Sha256Digest& operationDigest) noexcept
    : symbolValues(zc::mv(symbols)),
      relocationValues(zc::mv(relocations)),
      operationValues(zc::mv(operations)),
      symbolDigestValue(symbolDigest),
      relocationDigestValue(relocationDigest),
      operationDigestValue(operationDigest) {}

zc::OneOf<TrustedRuntimeManifestSet, TrustedRuntimeInvariantIssue>
TrustedRuntimeManifestSet::verify(
    zc::ArrayPtr<const uint32_t> sectionCounts, zc::Vector<TrustedRuntimeSymbolRecord>&& symbols,
    zc::Vector<TrustedRuntimeRelocationRecord>&& relocations,
    zc::Vector<TrustedRuntimeOperationRecord>&& operations,
    zc::ArrayPtr<const TrustedRuntimeSymbolId> requiredOperationSymbols,
    bool hasUnexpectedInitializer) {
  if (sectionCounts.size() == 0) { return TrustedRuntimeInvariantIssue::EmptyObjectSet; }
  if (hasUnexpectedInitializer) { return TrustedRuntimeInvariantIssue::UnexpectedInitializer; }
  for (const auto& symbol : symbols) {
    if (symbol.id().objectOrdinal >= sectionCounts.size() ||
        symbol.id().symbolTableSectionOrdinal == 0 ||
        symbol.id().symbolTableSectionOrdinal >= sectionCounts[symbol.id().objectOrdinal] ||
        !validKind(symbol.kindValue, symbol.nativeKindValue) ||
        !validBinding(symbol.bindingValue, symbol.nativeBindingValue) ||
        !validVisibility(symbol.visibilityValue) ||
        !validSection(symbol.sectionValue, symbol.sectionOrdinalValue) ||
        (symbol.namedValue && !validName(symbol.nameValue)) ||
        (!symbol.namedValue && symbol.nameValue.size() != 0)) {
      return TrustedRuntimeInvariantIssue::InvalidManifestRecord;
    }
    if (symbol.binding() == TrustedRuntimeSymbolBindingTag::Weak ||
        symbol.binding() == TrustedRuntimeSymbolBindingTag::OsSpecific ||
        symbol.binding() == TrustedRuntimeSymbolBindingTag::ProcessorSpecific) {
      return TrustedRuntimeInvariantIssue::WeakFallback;
    }
    if (symbol.sectionValue == TrustedRuntimeSymbolSectionTag::Section &&
        symbol.sectionOrdinalValue >= sectionCounts[symbol.id().objectOrdinal]) {
      return TrustedRuntimeInvariantIssue::InvalidManifestRecord;
    }
    if (symbol.sectionValue == TrustedRuntimeSymbolSectionTag::Undefined &&
        symbol.byteSizeValue != 0) {
      return TrustedRuntimeInvariantIssue::InvalidManifestRecord;
    }
  }
  if (!sortUnique(symbols)) { return TrustedRuntimeInvariantIssue::InvalidManifestRecord; }
  for (size_t index = 0; index < symbols.size(); ++index) {
    const auto& symbol = symbols[index];
    if (index > 0 && symbols[index - 1].id() == symbol.id()) {
      return TrustedRuntimeInvariantIssue::InvalidManifestRecord;
    }
    if (symbol.bindingValue == TrustedRuntimeSymbolBindingTag::Global && symbol.namedValue) {
      for (size_t previous = 0; previous < index; ++previous) {
        if (symbols[previous].bindingValue == TrustedRuntimeSymbolBindingTag::Global &&
            symbols[previous].namedValue && symbols[previous].nameValue == symbol.nameValue) {
          return TrustedRuntimeInvariantIssue::InvalidManifestRecord;
        }
      }
    }
  }
  if (!sortUnique(relocations)) { return TrustedRuntimeInvariantIssue::InvalidManifestRecord; }
  for (const auto& relocation : relocations) {
    if (relocation.objectOrdinal >= sectionCounts.size() || relocation.sectionOrdinal == 0 ||
        relocation.sectionOrdinal >= sectionCounts[relocation.objectOrdinal] ||
        !hasSymbol(symbols, relocation.target)) {
      return TrustedRuntimeInvariantIssue::InvalidManifestRecord;
    }
  }
  for (size_t index = 1; index < relocations.size(); ++index) {
    if (relocations[index - 1].objectOrdinal == relocations[index].objectOrdinal &&
        relocations[index - 1].sectionOrdinal == relocations[index].sectionOrdinal &&
        relocations[index - 1].byteOffset == relocations[index].byteOffset) {
      return TrustedRuntimeInvariantIssue::InvalidManifestRecord;
    }
  }
  if (!sortUnique(operations)) { return TrustedRuntimeInvariantIssue::InvalidManifestRecord; }
  for (size_t index = 0; index < requiredOperationSymbols.size(); ++index) {
    for (size_t other = index + 1; other < requiredOperationSymbols.size(); ++other) {
      if (requiredOperationSymbols[index] == requiredOperationSymbols[other]) {
        return TrustedRuntimeInvariantIssue::InvalidManifestRecord;
      }
    }
  }
  for (size_t index = 0; index < operations.size(); ++index) {
    const auto& operation = operations[index];
    auto symbolIndex = findSymbol(symbols, operation.symbol);
    if (operation.operation < TrustedRuntimeOperation::Allocate ||
        operation.operation > TrustedRuntimeOperation::Exit || symbolIndex == zc::none) {
      return TrustedRuntimeInvariantIssue::InvalidManifestRecord;
    }
    ZC_IF_SOME(targetIndex, symbolIndex) {
      if (symbols[targetIndex].kind() != TrustedRuntimeSymbolKindTag::Function ||
          symbols[targetIndex].section() != TrustedRuntimeSymbolSectionTag::Section) {
        return TrustedRuntimeInvariantIssue::InvalidManifestRecord;
      }
    }
    for (size_t previous = 0; previous < index; ++previous) {
      if (operations[previous].symbol == operation.symbol) {
        return TrustedRuntimeInvariantIssue::OperationManifestMismatch;
      }
    }
    bool required = false;
    for (const auto& requiredSymbol : requiredOperationSymbols) {
      if (requiredSymbol == operation.symbol) {
        required = true;
        break;
      }
    }
    if (!required) { return TrustedRuntimeInvariantIssue::OperationManifestMismatch; }
  }
  for (const auto& requiredSymbol : requiredOperationSymbols) {
    bool classified = false;
    for (const auto& operation : operations) {
      if (operation.symbol == requiredSymbol) {
        classified = true;
        break;
      }
    }
    if (!classified) { return TrustedRuntimeInvariantIssue::OperationManifestMismatch; }
  }
  const auto symbolDigest =
      manifestDigest<TrustedRuntimeSymbolRecord>(TrustedRuntimeManifestKind::Symbols, symbols);
  const auto relocationDigest = manifestDigest<TrustedRuntimeRelocationRecord>(
      TrustedRuntimeManifestKind::Relocations, relocations);
  const auto operationDigest = manifestDigest<TrustedRuntimeOperationRecord>(
      TrustedRuntimeManifestKind::Operations, operations);
  return TrustedRuntimeManifestSet(zc::mv(symbols), zc::mv(relocations), zc::mv(operations),
                                   symbolDigest, relocationDigest, operationDigest);
}

zc::ArrayPtr<const TrustedRuntimeSymbolRecord> TrustedRuntimeManifestSet::symbols() const noexcept {
  return symbolValues;
}

zc::ArrayPtr<const TrustedRuntimeRelocationRecord> TrustedRuntimeManifestSet::relocations()
    const noexcept {
  return relocationValues;
}

zc::ArrayPtr<const TrustedRuntimeOperationRecord> TrustedRuntimeManifestSet::operations()
    const noexcept {
  return operationValues;
}

const identity::Sha256Digest& TrustedRuntimeManifestSet::symbolDigest() const noexcept {
  return symbolDigestValue;
}

const identity::Sha256Digest& TrustedRuntimeManifestSet::relocationDigest() const noexcept {
  return relocationDigestValue;
}

const identity::Sha256Digest& TrustedRuntimeManifestSet::operationDigest() const noexcept {
  return operationDigestValue;
}

TrustedRuntimeVerificationEvidence::TrustedRuntimeVerificationEvidence(
    zc::Vector<identity::Sha256Digest>&& objectDigests, const identity::Sha256Digest& symbolDigest,
    const identity::Sha256Digest& relocationDigest,
    const identity::Sha256Digest& operationDigest) noexcept
    : objectDigestValues(zc::mv(objectDigests)),
      symbolDigestValue(symbolDigest),
      relocationDigestValue(relocationDigest),
      operationDigestValue(operationDigest) {}

zc::OneOf<TrustedRuntimeVerificationEvidence, TrustedRuntimeInvariantIssue>
TrustedRuntimeVerificationEvidence::verify(
    zc::Vector<identity::Sha256Digest>&& declaredObjectDigests,
    TrustedRuntimeManifestSet&& declared, const TrustedRuntimeManifestSet& observed) {
  if (declaredObjectDigests.empty()) { return TrustedRuntimeInvariantIssue::EmptyObjectSet; }
  for (size_t index = 0; index < declaredObjectDigests.size(); ++index) {
    for (size_t other = index + 1; other < declaredObjectDigests.size(); ++other) {
      if (declaredObjectDigests[index] == declaredObjectDigests[other]) {
        return TrustedRuntimeInvariantIssue::DuplicateObjectDigest;
      }
    }
  }
  if (!sameRecords(declared.symbols(), observed.symbols())) {
    if (observedHasUnknownSymbol(declared, observed)) {
      return TrustedRuntimeInvariantIssue::UnmanifestedSymbol;
    }
    return TrustedRuntimeInvariantIssue::SymbolManifestMismatch;
  }
  if (!sameRecords(declared.relocations(), observed.relocations())) {
    if (observedHasUnknownRelocation(declared, observed)) {
      return TrustedRuntimeInvariantIssue::UnmanifestedRelocation;
    }
    return TrustedRuntimeInvariantIssue::RelocationManifestMismatch;
  }
  if (!sameRecords(declared.operations(), observed.operations())) {
    return TrustedRuntimeInvariantIssue::OperationManifestMismatch;
  }
  return TrustedRuntimeVerificationEvidence(zc::mv(declaredObjectDigests), declared.symbolDigest(),
                                            declared.relocationDigest(),
                                            declared.operationDigest());
}

zc::ArrayPtr<const identity::Sha256Digest> TrustedRuntimeVerificationEvidence::objectDigests()
    const noexcept {
  return objectDigestValues;
}

const identity::Sha256Digest& TrustedRuntimeVerificationEvidence::symbolDigest() const noexcept {
  return symbolDigestValue;
}

const identity::Sha256Digest& TrustedRuntimeVerificationEvidence::relocationDigest()
    const noexcept {
  return relocationDigestValue;
}

const identity::Sha256Digest& TrustedRuntimeVerificationEvidence::operationDigest() const noexcept {
  return operationDigestValue;
}

zc::OneOf<TrustedBuildRuntimeKey, TrustedRuntimeInvariantIssue>
TrustedBuildRuntimeKey::verifyEvidence(zc::StringPtr expectedRuntimeAbi, zc::StringPtr runtimeAbi,
                                       zc::Vector<zc::Array<uint8_t>>&& objectBytes,
                                       TrustedRuntimeVerificationEvidence&& evidence) {
  if (runtimeAbi != expectedRuntimeAbi) { return TrustedRuntimeInvariantIssue::RuntimeAbiMismatch; }
  if (objectBytes.empty()) { return TrustedRuntimeInvariantIssue::EmptyObjectSet; }
  if (objectBytes.size() != evidence.objectDigests().size()) {
    return TrustedRuntimeInvariantIssue::ObjectDigestMismatch;
  }
  zc::Vector<identity::Sha256Digest> objectDigests(objectBytes.size());
  for (size_t index = 0; index < objectBytes.size(); ++index) {
    const auto actual = digest(objectBytes[index]);
    if (actual != evidence.objectDigests()[index]) {
      return TrustedRuntimeInvariantIssue::ObjectDigestMismatch;
    }
    for (const auto& previous : objectDigests) {
      if (previous == actual) { return TrustedRuntimeInvariantIssue::DuplicateObjectDigest; }
    }
    objectDigests.add(actual);
  }
  return TrustedBuildRuntimeKey(copyBytes(runtimeAbi.asBytes()), zc::mv(objectDigests),
                                evidence.symbolDigest(), evidence.relocationDigest(),
                                evidence.operationDigest());
}

}  // namespace zomlang::compiler::driver::package
