#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/binder/stable-binding-facts.h"
#include "zomlang/compiler/identity/key/definition-key.h"

namespace zomlang::compiler::binder {

/// \brief One verified definition authority paired with its selected syntax body disposition.
struct NamedDefinitionInventoryInput final {
  identity::DefinitionIdentityAuthority authority;
  DefinitionBodyDisposition bodyDisposition;
};

/// \brief One stable named-definition key and its complete RFC 0018 identity record.
class NamedDefinitionInventoryEntry final {
public:
  NamedDefinitionInventoryEntry(NamedDefinitionInventoryEntry&&) noexcept = default;
  NamedDefinitionInventoryEntry& operator=(NamedDefinitionInventoryEntry&&) noexcept = default;
  ZC_DISALLOW_COPY(NamedDefinitionInventoryEntry);

  ZC_NODISCARD NamedDefinitionInventoryEntry clone() const;
  ZC_NODISCARD const identity::DefinitionKey& key() const noexcept;
  ZC_NODISCARD const identity::DefinitionIdentityRecord& record() const noexcept;
  ZC_NODISCARD DefinitionBodyDisposition bodyDisposition() const noexcept;

private:
  NamedDefinitionInventoryEntry(identity::DefinitionKey&& key,
                                identity::DefinitionIdentityRecord&& record,
                                DefinitionBodyDisposition bodyDisposition) noexcept;

  identity::DefinitionKey keyField;
  identity::DefinitionIdentityRecord recordField;
  DefinitionBodyDisposition bodyDispositionField;

  friend class NamedDefinitionInventory;
};

/// \brief Canonical semantic active named-definition inventory for one module.
class NamedDefinitionInventory final {
public:
  NamedDefinitionInventory(NamedDefinitionInventory&&) noexcept = default;
  NamedDefinitionInventory& operator=(NamedDefinitionInventory&&) noexcept = default;
  ZC_DISALLOW_COPY(NamedDefinitionInventory);

  ZC_NODISCARD static zc::Maybe<NamedDefinitionInventory> fromVerified(
      const identity::ModuleKey& module, zc::ArrayPtr<const NamedDefinitionInventoryInput> inputs);
  ZC_NODISCARD static zc::Maybe<NamedDefinitionInventory> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD NamedDefinitionInventory clone() const;
  ZC_NODISCARD zc::ArrayPtr<const NamedDefinitionInventoryEntry> entries() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  ZC_NODISCARD bool sameAs(const NamedDefinitionInventory& other) const;

private:
  explicit NamedDefinitionInventory(zc::Vector<NamedDefinitionInventoryEntry>&& entries) noexcept;

  zc::Vector<NamedDefinitionInventoryEntry> entryFields;
};

/// \brief One stable implementation key and its complete RFC 0018 identity record.
class NamedImplementationInventoryEntry final {
public:
  NamedImplementationInventoryEntry(NamedImplementationInventoryEntry&&) noexcept = default;
  NamedImplementationInventoryEntry& operator=(NamedImplementationInventoryEntry&&) noexcept =
      default;
  ZC_DISALLOW_COPY(NamedImplementationInventoryEntry);

  ZC_NODISCARD NamedImplementationInventoryEntry clone() const;
  ZC_NODISCARD const identity::ImplKey& key() const noexcept;
  ZC_NODISCARD const identity::ImplIdentityRecord& record() const noexcept;

private:
  NamedImplementationInventoryEntry(identity::ImplKey&& key,
                                    identity::ImplIdentityRecord&& record) noexcept;

  identity::ImplKey keyField;
  identity::ImplIdentityRecord recordField;

  friend class NamedImplementationInventory;
};

/// \brief Canonical semantic active implementation inventory for one module.
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
  ZC_NODISCARD zc::ArrayPtr<const NamedImplementationInventoryEntry> entries() const
      ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  ZC_NODISCARD bool sameAs(const NamedImplementationInventory& other) const;

private:
  explicit NamedImplementationInventory(
      zc::Vector<NamedImplementationInventoryEntry>&& entries) noexcept;

  zc::Vector<NamedImplementationInventoryEntry> entryFields;
};

}  // namespace zomlang::compiler::binder
