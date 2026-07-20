#include "zomlang/compiler/binder/named-identity-inventory.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/sha256.h"

namespace zomlang::compiler::binder {
namespace {

constexpr zc::StringPtr kDefinitionInventoryDomain = "zom.named-definition-inventory.v1"_zc;
constexpr zc::StringPtr kImplementationInventoryDomain =
    "zom.named-implementation-inventory.v1"_zc;
constexpr zc::StringPtr kDefinitionKeyDomain = "zom.named-item-header.v0"_zc;
constexpr uint64_t kMaximumInventoryEntries = 1024 * 1024;
constexpr uint64_t kMaximumDefinitionRecordBytes = 4 * 1024 * 1024;
constexpr uint64_t kMaximumInventoryBytes = 64 * 1024 * 1024;

int compareBytes(zc::ArrayPtr<const uint8_t> left,
                 zc::ArrayPtr<const uint8_t> right) noexcept {
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

bool definitionRecordMatchesKey(const identity::DefinitionKey& key,
                                zc::ArrayPtr<const uint8_t> record) {
  identity::Sha256Hasher hasher;
  const uint8_t separator = 0;
  if (!hasher.update(kDefinitionKeyDomain.asBytes()) ||
      !hasher.update(zc::arrayPtr(separator)) || !hasher.update(record)) {
    return false;
  }
  auto digest = hasher.finish();
  return digest != zc::none && ZC_ASSERT_NONNULL(digest).bytes() == key.bytes();
}

void sortDefinitions(zc::Vector<identity::DefinitionIdentityAuthority>& authorities) {
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
    identity::DefinitionKey&& key, zc::Array<uint8_t>&& canonicalRecord) noexcept
    : keyField(zc::mv(key)), canonicalRecordField(zc::mv(canonicalRecord)) {}

NamedDefinitionInventoryEntry NamedDefinitionInventoryEntry::clone() const {
  return NamedDefinitionInventoryEntry(keyField.clone(),
                                       zc::heapArray<uint8_t>(canonicalRecordField.asPtr()));
}

const identity::DefinitionKey& NamedDefinitionInventoryEntry::key() const noexcept {
  return keyField;
}

zc::ArrayPtr<const uint8_t> NamedDefinitionInventoryEntry::canonicalRecord() const {
  return canonicalRecordField.asPtr();
}

NamedDefinitionInventory::NamedDefinitionInventory(
    zc::Vector<NamedDefinitionInventoryEntry>&& entries) noexcept
    : entryFields(zc::mv(entries)) {}

zc::Maybe<NamedDefinitionInventory> NamedDefinitionInventory::fromVerified(
    const identity::ModuleKey& module,
    zc::ArrayPtr<const identity::DefinitionIdentityAuthority> authorities) {
  if (authorities.size() > kMaximumInventoryEntries) { return zc::none; }
  zc::Vector<identity::DefinitionIdentityAuthority> sorted(authorities.size());
  for (const auto& authority : authorities) {
    if (!authority.verify() || !sameModule(authority.record().module(), module)) {
      return zc::none;
    }
    sorted.add(authority.clone());
  }
  sortDefinitions(sorted);

  zc::Vector<NamedDefinitionInventoryEntry> entries(sorted.size());
  for (size_t index = 0; index < sorted.size(); ++index) {
    const auto& authority = sorted[index];
    if (index != 0 && sorted[index - 1].key() == authority.key()) {
      if (!sorted[index - 1].sameRecordAs(authority)) { return zc::none; }
      continue;
    }
    entries.add(NamedDefinitionInventoryEntry(authority.key().clone(),
                                              authority.record().encode()));
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
  zc::Vector<NamedDefinitionInventoryEntry> entries(
      static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto digest = decoder.decodeDigest();
    auto record = decoder.decodeByteString(kMaximumDefinitionRecordBytes);
    if (digest == zc::none || record == zc::none) { return zc::none; }
    auto key = identity::DefinitionKey::fromBytes(ZC_ASSERT_NONNULL(digest).bytes());
    if (key == zc::none ||
        !definitionRecordMatchesKey(ZC_ASSERT_NONNULL(key),
                                    ZC_ASSERT_NONNULL(record).asPtr())) {
      return zc::none;
    }
    ZC_IF_SOME(keyValue, key) {
      if (entries.size() != 0 &&
          compareBytes(entries.back().key().bytes(), keyValue.bytes()) >= 0) {
        return zc::none;
      }
      entries.add(NamedDefinitionInventoryEntry(zc::mv(keyValue),
                                                zc::mv(ZC_ASSERT_NONNULL(record))));
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
    encoder.encodeByteString(entry.canonicalRecord());
  }
  return encoder.finish();
}

bool NamedDefinitionInventory::sameAs(const NamedDefinitionInventory& other) const {
  return encodeCanonical().asPtr() == other.encodeCanonical().asPtr();
}

NamedImplementationInventory::NamedImplementationInventory(
    zc::Vector<identity::ImplKey>&& keys) noexcept
    : keyFields(zc::mv(keys)) {}

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

  zc::Vector<identity::ImplKey> keys(sorted.size());
  for (size_t index = 0; index < sorted.size(); ++index) {
    if (index != 0 && sorted[index - 1].key() == sorted[index].key()) {
      if (!sorted[index - 1].sameRecordAs(sorted[index])) { return zc::none; }
      continue;
    }
    keys.add(sorted[index].key().clone());
  }
  NamedImplementationInventory result(zc::mv(keys));
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
  zc::Vector<identity::ImplKey> keys(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto digest = decoder.decodeDigest();
    if (digest == zc::none) { return zc::none; }
    auto key = identity::ImplKey::fromBytes(ZC_ASSERT_NONNULL(digest).bytes());
    if (key == zc::none) { return zc::none; }
    ZC_IF_SOME(keyValue, key) {
      if (keys.size() != 0 && compareBytes(keys.back().bytes(), keyValue.bytes()) >= 0) {
        return zc::none;
      }
      keys.add(zc::mv(keyValue));
    }
  }
  if (!decoder.finished()) { return zc::none; }
  return NamedImplementationInventory(zc::mv(keys));
}

NamedImplementationInventory NamedImplementationInventory::clone() const {
  zc::Vector<identity::ImplKey> keys(keyFields.size());
  for (const auto& key : keyFields) { keys.add(key.clone()); }
  return NamedImplementationInventory(zc::mv(keys));
}

zc::ArrayPtr<const identity::ImplKey> NamedImplementationInventory::keys() const {
  return keyFields.asPtr();
}

zc::Array<uint8_t> NamedImplementationInventory::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(kImplementationInventoryDomain.asBytes());
  encoder.encodeSequenceSize(keyFields.size());
  for (const auto& key : keyFields) { key.encode(encoder); }
  return encoder.finish();
}

bool NamedImplementationInventory::sameAs(const NamedImplementationInventory& other) const {
  return encodeCanonical().asPtr() == other.encodeCanonical().asPtr();
}

}  // namespace zomlang::compiler::binder
