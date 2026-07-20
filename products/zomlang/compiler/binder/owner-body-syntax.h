#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/binder/local-identity.h"
#include "zomlang/compiler/binder/module-body-syntax.h"
#include "zomlang/compiler/identity/source-key.h"

namespace zomlang::compiler::binder {

namespace owner_body_syntax_detail {
struct ModuleBodyOwnersData;
struct OwnerBodySyntaxData;
struct OwnerBodyProvenanceData;
}  // namespace owner_body_syntax_detail

/// \brief Complete canonical stable-body-owner inventory for one active module.
class ModuleBodyOwners final {
public:
  ~ModuleBodyOwners() noexcept(false);
  ModuleBodyOwners(ModuleBodyOwners&&) noexcept;
  ModuleBodyOwners& operator=(ModuleBodyOwners&&) noexcept;
  ZC_DISALLOW_COPY(ModuleBodyOwners);

  /// \brief Admits one complete owner inventory in strict canonical byte order.
  /// \param owningModule Active module owned by this inventory.
  /// \param owners Complete owner sequence with the matching module owner first.
  /// \return The admitted inventory, or none for malformed order, duplication, or ownership.
  ZC_NODISCARD static zc::Maybe<ModuleBodyOwners> from(identity::ModuleKey&& owningModule,
                                                       zc::Vector<StableBodyOwnerKey>&& owners);
  /// \brief Decodes one complete bounded canonical owner inventory.
  ZC_NODISCARD static zc::Maybe<ModuleBodyOwners> decodeCanonical(
      zc::ArrayPtr<const uint8_t> encoded);
  /// \brief Returns an independent deep copy of this value.
  ZC_NODISCARD ModuleBodyOwners clone() const;
  /// \brief Returns the module whose complete owner inventory is stored.
  ZC_NODISCARD const identity::ModuleKey& owningModule() const noexcept;
  /// \brief Returns owners in strict complete-canonical-byte order.
  ZC_NODISCARD zc::ArrayPtr<const StableBodyOwnerKey> owners() const ZC_LIFETIMEBOUND;
  /// \brief Encodes the complete versioned canonical record.
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  bool operator==(const ModuleBodyOwners& other) const;
  bool operator!=(const ModuleBodyOwners& other) const { return !(*this == other); }

private:
  explicit ModuleBodyOwners(
      zc::Own<owner_body_syntax_detail::ModuleBodyOwnersData>&& impl) noexcept;

  zc::Own<owner_body_syntax_detail::ModuleBodyOwnersData> impl;
};

/// \brief Closed detached semantic syntax for one admitted stable body owner.
class OwnerBodySyntax final {
public:
  ~OwnerBodySyntax() noexcept(false);
  OwnerBodySyntax(OwnerBodySyntax&&) noexcept;
  OwnerBodySyntax& operator=(OwnerBodySyntax&&) noexcept;
  ZC_DISALLOW_COPY(OwnerBodySyntax);

  /// \brief Admits one structurally closed owner-body syntax record.
  /// \param owner Stable body owner represented by the detached syntax.
  /// \param owningModule Active module containing the owner.
  /// \param syntax Detached syntax already projected for this owner.
  /// \return The admitted record, or none for an inconsistent structural record.
  ZC_NODISCARD static zc::Maybe<OwnerBodySyntax> from(StableBodyOwnerKey&& owner,
                                                      identity::ModuleKey&& owningModule,
                                                      ModuleBodySyntax&& syntax);
  /// \brief Decodes one complete bounded canonical owner-body syntax record.
  ZC_NODISCARD static zc::Maybe<OwnerBodySyntax> decodeCanonical(
      zc::ArrayPtr<const uint8_t> encoded);
  /// \brief Returns an independent deep copy of this value.
  ZC_NODISCARD OwnerBodySyntax clone() const;
  /// \brief Returns the stable body owner represented by this record.
  ZC_NODISCARD const StableBodyOwnerKey& owner() const noexcept;
  /// \brief Returns the active module containing the owner.
  ZC_NODISCARD const identity::ModuleKey& owningModule() const noexcept;
  /// \brief Returns the closed detached syntax projected for this owner.
  ZC_NODISCARD const ModuleBodySyntax& detachedSyntax() const noexcept;
  /// \brief Encodes the complete versioned canonical record.
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  bool operator==(const OwnerBodySyntax& other) const;
  bool operator!=(const OwnerBodySyntax& other) const { return !(*this == other); }

private:
  explicit OwnerBodySyntax(zc::Own<owner_body_syntax_detail::OwnerBodySyntaxData>&& impl) noexcept;

  zc::Own<owner_body_syntax_detail::OwnerBodySyntaxData> impl;
};

/// \brief Revision-local provenance projected for one admitted stable body owner.
class OwnerBodyProvenance final {
public:
  ~OwnerBodyProvenance() noexcept(false);
  OwnerBodyProvenance(OwnerBodyProvenance&&) noexcept;
  OwnerBodyProvenance& operator=(OwnerBodyProvenance&&) noexcept;
  ZC_DISALLOW_COPY(OwnerBodyProvenance);

  /// \brief Admits one owner and its already projected revision-local provenance.
  ZC_NODISCARD static zc::Maybe<OwnerBodyProvenance> from(StableBodyOwnerKey&& owner,
                                                          ModuleBodyProvenance&& provenance);
  /// \brief Decodes one complete bounded canonical owner-body provenance record.
  ZC_NODISCARD static zc::Maybe<OwnerBodyProvenance> decodeCanonical(
      zc::ArrayPtr<const uint8_t> encoded);
  /// \brief Returns an independent deep copy of this value.
  ZC_NODISCARD OwnerBodyProvenance clone() const;
  /// \brief Returns the stable body owner represented by this record.
  ZC_NODISCARD const StableBodyOwnerKey& owner() const noexcept;
  /// \brief Returns the exact projected revision-local provenance.
  ZC_NODISCARD const ModuleBodyProvenance& detachedProvenance() const noexcept;
  /// \brief Encodes the complete versioned canonical record.
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  bool operator==(const OwnerBodyProvenance& other) const;
  bool operator!=(const OwnerBodyProvenance& other) const { return !(*this == other); }

private:
  explicit OwnerBodyProvenance(
      zc::Own<owner_body_syntax_detail::OwnerBodyProvenanceData>&& impl) noexcept;

  zc::Own<owner_body_syntax_detail::OwnerBodyProvenanceData> impl;
};

}  // namespace zomlang::compiler::binder
