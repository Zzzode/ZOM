#include "zomlang/compiler/binder/identity/named-identity-inventory.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/identity/canonical/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::binder {
namespace {

constexpr zc::StringPtr kDefinitionInventoryDomain = "zom.named-definition-inventory"_zc;
constexpr zc::StringPtr kImplementationInventoryDomain = "zom.named-implementation-inventory"_zc;
constexpr uint64_t kMaximumInventoryEntries = 1024 * 1024;
constexpr uint64_t kMaximumIdentityRecordBytes = 4 * 1024 * 1024;
constexpr uint64_t kMaximumInventoryBytes = 64 * 1024 * 1024;

int compareBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] < right[index]) return -1;
    if (left[index] > right[index]) return 1;
  }
  if (left.size() < right.size()) return -1;
  if (left.size() > right.size()) return 1;
  return 0;
}

bool sameModule(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

void sortDefinitions(zc::Vector<NamedDefinitionInventoryInput>& inputs) {
  for (size_t index = 1; index < inputs.size(); ++index) {
    auto current = zc::mv(inputs[index]);
    size_t insertion = index;
    while (insertion != 0 && compareBytes(current.authority.key().bytes(),
                                          inputs[insertion - 1].authority.key().bytes()) < 0) {
      inputs[insertion] = zc::mv(inputs[insertion - 1]);
      --insertion;
    }
    inputs[insertion] = zc::mv(current);
  }
}

void sortImplementations(zc::Vector<identity::ImplIdentityAuthority>& authorities) {
  for (size_t index = 1; index < authorities.size(); ++index) {
    auto current = zc::mv(authorities[index]);
    size_t insertion = index;
    while (insertion != 0 &&
           compareBytes(current.key().bytes(), authorities[insertion - 1].key().bytes()) < 0) {
      authorities[insertion] = zc::mv(authorities[insertion - 1]);
      --insertion;
    }
    authorities[insertion] = zc::mv(current);
  }
}

}  // namespace

NamedDefinitionInventoryEntry::NamedDefinitionInventoryEntry(
    identity::DefinitionKey&& key, identity::DefinitionIdentityRecord&& record,
    DefinitionBodyDisposition bodyDisposition) noexcept
    : keyField(zc::mv(key)), recordField(zc::mv(record)), bodyDispositionField(bodyDisposition) {}

NamedDefinitionInventoryEntry NamedDefinitionInventoryEntry::clone() const {
  return NamedDefinitionInventoryEntry(keyField.clone(), recordField.clone(), bodyDispositionField);
}

const identity::DefinitionKey& NamedDefinitionInventoryEntry::key() const noexcept {
  return keyField;
}

const identity::DefinitionIdentityRecord& NamedDefinitionInventoryEntry::record() const noexcept {
  return recordField;
}

DefinitionBodyDisposition NamedDefinitionInventoryEntry::bodyDisposition() const noexcept {
  return bodyDispositionField;
}

NamedDefinitionInventory::NamedDefinitionInventory(
    zc::Vector<NamedDefinitionInventoryEntry>&& entries) noexcept
    : entryFields(zc::mv(entries)) {}

zc::Maybe<NamedDefinitionInventory> NamedDefinitionInventory::fromVerified(
    const identity::ModuleKey& module, zc::ArrayPtr<const NamedDefinitionInventoryInput> inputs) {
  if (inputs.size() > kMaximumInventoryEntries) { return zc::none; }
  zc::Vector<NamedDefinitionInventoryInput> sorted(inputs.size());
  for (const auto& input : inputs) {
    if (!input.authority.verify() || !sameModule(input.authority.record().module(), module) ||
        !isStableBindingValue(input.bodyDisposition)) {
      return zc::none;
    }
    sorted.add(NamedDefinitionInventoryInput{input.authority.clone(), input.bodyDisposition});
  }
  sortDefinitions(sorted);

  zc::Vector<NamedDefinitionInventoryEntry> entries(sorted.size());
  for (size_t index = 0; index < sorted.size(); ++index) {
    const auto& input = sorted[index];
    if (index != 0 && sorted[index - 1].authority.key() == input.authority.key()) {
      if (!sorted[index - 1].authority.sameRecordAs(input.authority) ||
          sorted[index - 1].bodyDisposition != input.bodyDisposition) {
        return zc::none;
      }
      continue;
    }
    entries.add(NamedDefinitionInventoryEntry(
        input.authority.key().clone(), input.authority.record().clone(), input.bodyDisposition));
  }
  NamedDefinitionInventory result(zc::mv(entries));
  if (result.encodeCanonical().size() > kMaximumInventoryBytes) { return zc::none; }
  return zc::mv(result);
}

zc::Maybe<NamedDefinitionInventory> NamedDefinitionInventory::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumInventoryBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto domain = decoder.decodeByteString(kDefinitionInventoryDomain.size());
  auto count = decoder.decodeSequenceSize(kMaximumInventoryEntries);
  if (domain == zc::none || count == zc::none ||
      ZC_ASSERT_NONNULL(domain).asPtr() != kDefinitionInventoryDomain.asBytes()) {
    return zc::none;
  }
  zc::Vector<NamedDefinitionInventoryEntry> entries(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto digest = decoder.decodeDigest();
    auto recordBytes = decoder.decodeByteString(kMaximumIdentityRecordBytes);
    auto disposition = decoder.decodeUint8();
    if (digest == zc::none || recordBytes == zc::none || disposition == zc::none) {
      return zc::none;
    }
    auto key = identity::DefinitionKey::fromBytes(ZC_ASSERT_NONNULL(digest).bytes());
    auto record =
        identity::DefinitionIdentityRecord::decodeCanonical(ZC_ASSERT_NONNULL(recordBytes).asPtr());
    const auto bodyDisposition =
        static_cast<DefinitionBodyDisposition>(ZC_ASSERT_NONNULL(disposition));
    if (key == zc::none || record == zc::none || !isStableBindingValue(bodyDisposition)) {
      return zc::none;
    }
    ZC_IF_SOME(keyValue, key) {
      if (entries.size() != 0 &&
          compareBytes(entries.back().key().bytes(), keyValue.bytes()) >= 0) {
        return zc::none;
      }
      ZC_IF_SOME(recordValue, record) {
        if (identity::DefinitionKey::compute(recordValue) != keyValue ||
            (entries.size() != 0 &&
             !sameModule(entries[0].record().module(), recordValue.module()))) {
          return zc::none;
        }
        entries.add(
            NamedDefinitionInventoryEntry(zc::mv(keyValue), zc::mv(recordValue), bodyDisposition));
      }
    }
  }
  if (!decoder.finished()) { return zc::none; }
  return NamedDefinitionInventory(zc::mv(entries));
}

NamedDefinitionInventory NamedDefinitionInventory::clone() const {
  zc::Vector<NamedDefinitionInventoryEntry> entries(entryFields.size());
  for (const auto& entry : entryFields) { entries.add(entry.clone()); }
  return NamedDefinitionInventory(zc::mv(entries));
}

zc::ArrayPtr<const NamedDefinitionInventoryEntry> NamedDefinitionInventory::entries() const {
  return entryFields.asPtr();
}

zc::Array<uint8_t> NamedDefinitionInventory::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(kDefinitionInventoryDomain.asBytes());
  encoder.encodeSequenceSize(entryFields.size());
  for (const auto& entry : entryFields) {
    entry.key().encode(encoder);
    auto record = entry.record().encode();
    encoder.encodeByteString(record.asPtr());
    encoder.encodeUint8(static_cast<uint8_t>(entry.bodyDisposition()));
  }
  return encoder.finish();
}

bool NamedDefinitionInventory::sameAs(const NamedDefinitionInventory& other) const {
  return encodeCanonical().asPtr() == other.encodeCanonical().asPtr();
}

NamedImplementationInventoryEntry::NamedImplementationInventoryEntry(
    identity::ImplKey&& key, identity::ImplIdentityRecord&& record) noexcept
    : keyField(zc::mv(key)), recordField(zc::mv(record)) {}

NamedImplementationInventoryEntry NamedImplementationInventoryEntry::clone() const {
  return NamedImplementationInventoryEntry(keyField.clone(), recordField.clone());
}

const identity::ImplKey& NamedImplementationInventoryEntry::key() const noexcept {
  return keyField;
}

const identity::ImplIdentityRecord& NamedImplementationInventoryEntry::record() const noexcept {
  return recordField;
}

NamedImplementationInventory::NamedImplementationInventory(
    zc::Vector<NamedImplementationInventoryEntry>&& entries) noexcept
    : entryFields(zc::mv(entries)) {}

zc::Maybe<NamedImplementationInventory> NamedImplementationInventory::fromVerified(
    const identity::ModuleKey& module,
    zc::ArrayPtr<const identity::ImplIdentityAuthority> authorities) {
  if (authorities.size() > kMaximumInventoryEntries) { return zc::none; }
  zc::Vector<identity::ImplIdentityAuthority> sorted(authorities.size());
  for (const auto& authority : authorities) {
    if (!authority.verify() || !sameModule(authority.record().module(), module)) {
      return zc::none;
    }
    sorted.add(authority.clone());
  }
  sortImplementations(sorted);

  zc::Vector<NamedImplementationInventoryEntry> entries(sorted.size());
  for (size_t index = 0; index < sorted.size(); ++index) {
    if (index != 0 && sorted[index - 1].key() == sorted[index].key()) {
      if (!sorted[index - 1].sameRecordAs(sorted[index])) { return zc::none; }
      continue;
    }
    entries.add(NamedImplementationInventoryEntry(sorted[index].key().clone(),
                                                  sorted[index].record().clone()));
  }
  NamedImplementationInventory result(zc::mv(entries));
  if (result.encodeCanonical().size() > kMaximumInventoryBytes) { return zc::none; }
  return zc::mv(result);
}

zc::Maybe<NamedImplementationInventory> NamedImplementationInventory::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumInventoryBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto domain = decoder.decodeByteString(kImplementationInventoryDomain.size());
  auto count = decoder.decodeSequenceSize(kMaximumInventoryEntries);
  if (domain == zc::none || count == zc::none ||
      ZC_ASSERT_NONNULL(domain).asPtr() != kImplementationInventoryDomain.asBytes()) {
    return zc::none;
  }
  zc::Vector<NamedImplementationInventoryEntry> entries(
      static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto digest = decoder.decodeDigest();
    auto recordBytes = decoder.decodeByteString(kMaximumIdentityRecordBytes);
    if (digest == zc::none || recordBytes == zc::none) { return zc::none; }
    auto key = identity::ImplKey::fromBytes(ZC_ASSERT_NONNULL(digest).bytes());
    auto record =
        identity::ImplIdentityRecord::decodeCanonical(ZC_ASSERT_NONNULL(recordBytes).asPtr());
    if (key == zc::none || record == zc::none) { return zc::none; }
    ZC_IF_SOME(keyValue, key) {
      if (entries.size() != 0 &&
          compareBytes(entries.back().key().bytes(), keyValue.bytes()) >= 0) {
        return zc::none;
      }
      ZC_IF_SOME(recordValue, record) {
        if (identity::ImplKey::compute(recordValue) != keyValue) { return zc::none; }
        if (entries.size() != 0 &&
            !sameModule(entries[0].record().module(), recordValue.module())) {
          return zc::none;
        }
        entries.add(NamedImplementationInventoryEntry(zc::mv(keyValue), zc::mv(recordValue)));
      }
    }
  }
  if (!decoder.finished()) { return zc::none; }
  return NamedImplementationInventory(zc::mv(entries));
}

NamedImplementationInventory NamedImplementationInventory::clone() const {
  zc::Vector<NamedImplementationInventoryEntry> entries(entryFields.size());
  for (const auto& entry : entryFields) { entries.add(entry.clone()); }
  return NamedImplementationInventory(zc::mv(entries));
}

zc::ArrayPtr<const NamedImplementationInventoryEntry> NamedImplementationInventory::entries()
    const {
  return entryFields.asPtr();
}

zc::Array<uint8_t> NamedImplementationInventory::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(kImplementationInventoryDomain.asBytes());
  encoder.encodeSequenceSize(entryFields.size());
  for (const auto& entry : entryFields) {
    entry.key().encode(encoder);
    auto record = entry.record().encode();
    encoder.encodeByteString(record.asPtr());
  }
  return encoder.finish();
}

bool NamedImplementationInventory::sameAs(const NamedImplementationInventory& other) const {
  return encodeCanonical().asPtr() == other.encodeCanonical().asPtr();
}

}  // namespace zomlang::compiler::binder
