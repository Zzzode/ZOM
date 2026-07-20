#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/identity/definition-key.h"

namespace zomlang::compiler::binder {

/// \brief One stable named-definition key and its complete RFC 0018 identity record.
class NamedDefinitionInventoryEntry final {
public:
  NamedDefinitionInventoryEntry(NamedDefinitionInventoryEntry&&) noexcept = default;
  NamedDefinitionInventoryEntry& operator=(NamedDefinitionInventoryEntry&&) noexcept = default;
  ZC_DISALLOW_COPY(NamedDefinitionInventoryEntry);

  ZC_NODISCARD NamedDefinitionInventoryEntry clone() const;
  ZC_NODISCARD const identity::DefinitionKey& key() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> canonicalRecord() const ZC_LIFETIMEBOUND;

private:
  NamedDefinitionInventoryEntry(identity::DefinitionKey&& key,
                                zc::Array<uint8_t>&& canonicalRecord) noexcept;

  identity::DefinitionKey keyField;
  zc::Array<uint8_t> canonicalRecordField;

  friend class NamedDefinitionInventory;
};

/// \brief Canonical semantic active named-definition inventory for one module.
class NamedDefinitionInventory final {
public:
  NamedDefinitionInventory(NamedDefinitionInventory&&) noexcept = default;
  NamedDefinitionInventory& operator=(NamedDefinitionInventory&&) noexcept = default;
  ZC_DISALLOW_COPY(NamedDefinitionInventory);

  ZC_NODISCARD static zc::Maybe<NamedDefinitionInventory> fromVerified(
      const identity::ModuleKey& module,
      zc::ArrayPtr<const identity::DefinitionIdentityAuthority> authorities);
  ZC_NODISCARD static zc::Maybe<NamedDefinitionInventory> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD NamedDefinitionInventory clone() const;
  ZC_NODISCARD zc::ArrayPtr<const NamedDefinitionInventoryEntry> entries() const
      ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  ZC_NODISCARD bool sameAs(const NamedDefinitionInventory& other) const;

private:
  explicit NamedDefinitionInventory(
      zc::Vector<NamedDefinitionInventoryEntry>&& entries) noexcept;

  zc::Vector<NamedDefinitionInventoryEntry> entryFields;
};

/// \brief Canonical semantic active implementation-key inventory for one module.
class NamedImplementationInventory final {
public:
  NamedImplementationInventory(NamedImplementationInventory&&) noexcept = default;
  NamedImplementationInventory& operator=(NamedImplementationInventory&&) noexcept = default;
  ZC_DISALLOW_COPY(NamedImplementationInventory);

  ZC_NODISCARD static zc::Maybe<NamedImplementationInventory> fromVerified(
      const identity::ModuleKey& module,
      zc::ArrayPtr<const identity::ImplIdentityAuthority> authorities);
  ZC_NODISCARD static zc::Maybe<NamedImplementationInventory> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD NamedImplementationInventory clone() const;
  ZC_NODISCARD zc::ArrayPtr<const identity::ImplKey> keys() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  ZC_NODISCARD bool sameAs(const NamedImplementationInventory& other) const;

private:
  explicit NamedImplementationInventory(zc::Vector<identity::ImplKey>&& keys) noexcept;

  zc::Vector<identity::ImplKey> keyFields;
};

}  // namespace zomlang::compiler::binder
