// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/identity/brand.h"
#include "zomlang/compiler/identity/canonical-scalar.h"
#include "zomlang/compiler/identity/definition-key.h"
#include "zomlang/compiler/identity/handle.h"
#include "zomlang/compiler/identity/source-key.h"

namespace zomlang::compiler::binder {

namespace local_identity_detail {
struct LocalSyntaxPathData;
struct StableBodyOwnerKeyData;
struct OwnerLocalBindingKeyData;
struct AnonymousOwnerLocalKeyData;
}  // namespace local_identity_detail

/// \brief Non-empty structural child-index path rooted at one stable semantic body.
class LocalSyntaxPath final {
public:
  ~LocalSyntaxPath() noexcept(false);
  LocalSyntaxPath(LocalSyntaxPath&&) noexcept;
  LocalSyntaxPath& operator=(LocalSyntaxPath&&) noexcept;
  ZC_DISALLOW_COPY(LocalSyntaxPath);

  /// \brief Admits a non-empty bounded sequence of structural child indices.
  ZC_NODISCARD static zc::Maybe<LocalSyntaxPath> from(zc::Vector<uint32_t>&& components);
  /// \brief Decodes one complete bounded canonical local syntax path.
  ZC_NODISCARD static zc::Maybe<LocalSyntaxPath> decodeCanonical(
      zc::ArrayPtr<const uint8_t> encoded);
  ZC_NODISCARD LocalSyntaxPath clone() const;
  ZC_NODISCARD zc::ArrayPtr<const uint32_t> components() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;
  bool operator==(const LocalSyntaxPath& other) const noexcept;
  bool operator!=(const LocalSyntaxPath& other) const noexcept { return !(*this == other); }

private:
  explicit LocalSyntaxPath(zc::Own<local_identity_detail::LocalSyntaxPathData>&& impl) noexcept;

  zc::Own<local_identity_detail::LocalSyntaxPathData> impl;
};

/// \brief Closed canonical stable owner of one semantic body.
enum class StableBodyOwnerKind : uint8_t { Module = 0x01, Definition = 0x02 };

/// \brief Edit-stable module-or-definition owner used by Binder body queries and values.
class StableBodyOwnerKey final {
public:
  ~StableBodyOwnerKey() noexcept(false);
  StableBodyOwnerKey(StableBodyOwnerKey&&) noexcept;
  StableBodyOwnerKey& operator=(StableBodyOwnerKey&&) noexcept;
  ZC_DISALLOW_COPY(StableBodyOwnerKey);

  /// \brief Constructs the module-initializer body owner alternative.
  ZC_NODISCARD static StableBodyOwnerKey module(identity::ModuleKey&& key);
  /// \brief Constructs the stable named-definition body owner alternative.
  ZC_NODISCARD static StableBodyOwnerKey definition(identity::DefinitionKey&& key);
  /// \brief Decodes one complete canonical stable-body-owner record.
  ZC_NODISCARD static zc::Maybe<StableBodyOwnerKey> decodeCanonical(
      zc::ArrayPtr<const uint8_t> encoded);
  ZC_NODISCARD StableBodyOwnerKey clone() const;
  ZC_NODISCARD StableBodyOwnerKind kind() const noexcept;
  ZC_NODISCARD zc::Maybe<const identity::ModuleKey&> moduleKey() const noexcept;
  ZC_NODISCARD zc::Maybe<const identity::DefinitionKey&> definitionKey() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;
  bool operator==(const StableBodyOwnerKey& other) const;
  bool operator!=(const StableBodyOwnerKey& other) const { return !(*this == other); }

private:
  explicit StableBodyOwnerKey(
      zc::Own<local_identity_detail::StableBodyOwnerKeyData>&& impl) noexcept;

  zc::Own<local_identity_detail::StableBodyOwnerKeyData> impl;
};

/// \brief Closed Binder-local declaration kinds that never enter the global definition registry.
enum class OwnerLocalBindingKind : uint8_t {
  CallableParameter = 0x0f,
  GenericParameter = 0x10,
  Local = 0x13,
  PatternBinding = 0x14
};

/// \brief Binder namespace tags retained by owner-local binding values.
enum class OwnerLocalBindingNamespace : uint8_t {
  Value = 0x01,
  Type = 0x02,
  Module = 0x03,
  Label = 0x04,
  Attribute = 0x05
};

/// \brief Body-value identity for one binding beneath a stable body owner.
class OwnerLocalBindingKey final {
public:
  ~OwnerLocalBindingKey() noexcept(false);
  OwnerLocalBindingKey(OwnerLocalBindingKey&&) noexcept;
  OwnerLocalBindingKey& operator=(OwnerLocalBindingKey&&) noexcept;
  ZC_DISALLOW_COPY(OwnerLocalBindingKey);

  /// \brief Admits one closed owner-local binding record.
  ZC_NODISCARD static zc::Maybe<OwnerLocalBindingKey> from(StableBodyOwnerKey&& owner,
                                                           LocalSyntaxPath&& path,
                                                           OwnerLocalBindingNamespace nameSpace,
                                                           OwnerLocalBindingKind kind,
                                                           identity::DeclaredDefinitionName&& name);
  /// \brief Decodes one complete bounded canonical owner-local-binding record.
  ZC_NODISCARD static zc::Maybe<OwnerLocalBindingKey> decodeCanonical(
      zc::ArrayPtr<const uint8_t> encoded);
  ZC_NODISCARD OwnerLocalBindingKey clone() const;
  ZC_NODISCARD const StableBodyOwnerKey& owner() const noexcept;
  ZC_NODISCARD const LocalSyntaxPath& path() const noexcept;
  ZC_NODISCARD OwnerLocalBindingNamespace nameSpace() const noexcept;
  ZC_NODISCARD OwnerLocalBindingKind kind() const noexcept;
  ZC_NODISCARD const identity::DeclaredDefinitionName& name() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;
  bool operator==(const OwnerLocalBindingKey& other) const;
  bool operator!=(const OwnerLocalBindingKey& other) const { return !(*this == other); }

private:
  explicit OwnerLocalBindingKey(
      zc::Own<local_identity_detail::OwnerLocalBindingKeyData>&& impl) noexcept;

  zc::Own<local_identity_detail::OwnerLocalBindingKeyData> impl;
};

/// \brief Closed anonymous syntax roles within one stable body owner.
enum class AnonymousOwnerLocalRole : uint8_t { Closure = 0x01, FunctionExpression = 0x02 };

/// \brief Body-value identity for anonymous callable syntax beneath a stable body owner.
class AnonymousOwnerLocalKey final {
public:
  ~AnonymousOwnerLocalKey() noexcept(false);
  AnonymousOwnerLocalKey(AnonymousOwnerLocalKey&&) noexcept;
  AnonymousOwnerLocalKey& operator=(AnonymousOwnerLocalKey&&) noexcept;
  ZC_DISALLOW_COPY(AnonymousOwnerLocalKey);

  /// \brief Admits one closed anonymous owner-local record.
  ZC_NODISCARD static zc::Maybe<AnonymousOwnerLocalKey> from(StableBodyOwnerKey&& owner,
                                                             LocalSyntaxPath&& path,
                                                             AnonymousOwnerLocalRole role);
  /// \brief Decodes one complete bounded canonical anonymous-owner-local record.
  ZC_NODISCARD static zc::Maybe<AnonymousOwnerLocalKey> decodeCanonical(
      zc::ArrayPtr<const uint8_t> encoded);
  ZC_NODISCARD AnonymousOwnerLocalKey clone() const;
  ZC_NODISCARD const StableBodyOwnerKey& owner() const noexcept;
  ZC_NODISCARD const LocalSyntaxPath& path() const noexcept;
  ZC_NODISCARD AnonymousOwnerLocalRole role() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;
  bool operator==(const AnonymousOwnerLocalKey& other) const;
  bool operator!=(const AnonymousOwnerLocalKey& other) const { return !(*this == other); }

private:
  explicit AnonymousOwnerLocalKey(
      zc::Own<local_identity_detail::AnonymousOwnerLocalKeyData>&& impl) noexcept;

  zc::Own<local_identity_detail::AnonymousOwnerLocalKeyData> impl;
};

class ModuleLocalIdentityAllocator;

/// \brief Revision-local materialization handle for one owner-local binding.
class OwnerLocalBindingId final {
public:
  constexpr OwnerLocalBindingId() noexcept = default;

  ZC_NODISCARD constexpr bool isValid() const noexcept {
    return context.isValid() && moduleValue.belongsTo(context);
  }
  ZC_NODISCARD constexpr bool belongsTo(identity::SemanticContextBrand expected) const noexcept {
    return isValid() && context == expected;
  }
  ZC_NODISCARD constexpr bool belongsTo(identity::ModuleId expected) const noexcept {
    return isValid() && moduleValue == expected;
  }
  ZC_NODISCARD constexpr uint32_t index() const noexcept { return slot; }
  constexpr bool operator==(OwnerLocalBindingId other) const noexcept {
    return context == other.context && moduleValue == other.moduleValue && slot == other.slot;
  }
  constexpr bool operator!=(OwnerLocalBindingId other) const noexcept { return !(*this == other); }

private:
  constexpr OwnerLocalBindingId(identity::SemanticContextBrand owner, identity::ModuleId module,
                                uint32_t value) noexcept
      : context(owner), moduleValue(module), slot(value) {}

  identity::SemanticContextBrand context;
  identity::ModuleId moduleValue;
  uint32_t slot = 0;

  friend class ModuleLocalIdentityAllocator;
};

/// \brief Revision-local materialization handle for one anonymous owner-local entity.
class AnonymousOwnerLocalId final {
public:
  constexpr AnonymousOwnerLocalId() noexcept = default;

  ZC_NODISCARD constexpr bool isValid() const noexcept {
    return context.isValid() && moduleValue.belongsTo(context);
  }
  ZC_NODISCARD constexpr bool belongsTo(identity::SemanticContextBrand expected) const noexcept {
    return isValid() && context == expected;
  }
  ZC_NODISCARD constexpr bool belongsTo(identity::ModuleId expected) const noexcept {
    return isValid() && moduleValue == expected;
  }
  ZC_NODISCARD constexpr uint32_t index() const noexcept { return slot; }
  constexpr bool operator==(AnonymousOwnerLocalId other) const noexcept {
    return context == other.context && moduleValue == other.moduleValue && slot == other.slot;
  }
  constexpr bool operator!=(AnonymousOwnerLocalId other) const noexcept {
    return !(*this == other);
  }

private:
  constexpr AnonymousOwnerLocalId(identity::SemanticContextBrand owner, identity::ModuleId module,
                                  uint32_t value) noexcept
      : context(owner), moduleValue(module), slot(value) {}

  identity::SemanticContextBrand context;
  identity::ModuleId moduleValue;
  uint32_t slot = 0;

  friend class ModuleLocalIdentityAllocator;
};

/// \brief Revision-local materialization handle for one implementation source occurrence.
class ImplOccurrenceId final {
public:
  constexpr ImplOccurrenceId() noexcept = default;

  ZC_NODISCARD constexpr bool isValid() const noexcept {
    return context.isValid() && moduleValue.belongsTo(context);
  }
  ZC_NODISCARD constexpr bool belongsTo(identity::SemanticContextBrand expected) const noexcept {
    return isValid() && context == expected;
  }
  ZC_NODISCARD constexpr bool belongsTo(identity::ModuleId expected) const noexcept {
    return isValid() && moduleValue == expected;
  }
  constexpr bool operator==(ImplOccurrenceId other) const noexcept {
    return context == other.context && moduleValue == other.moduleValue && slot == other.slot;
  }
  constexpr bool operator!=(ImplOccurrenceId other) const noexcept { return !(*this == other); }

private:
  constexpr ImplOccurrenceId(identity::SemanticContextBrand owner, identity::ModuleId module,
                             uint32_t value) noexcept
      : context(owner), moduleValue(module), slot(value) {}

  identity::SemanticContextBrand context;
  identity::ModuleId moduleValue;
  uint32_t slot = 0;

  friend class ModuleLocalIdentityAllocator;
};

/// \brief Failure returned while validating one module-local dense handle sequence.
enum class ModuleLocalIdentityFailure : uint8_t {
  None,
  InvalidHandle,
  ForeignContext,
  ForeignModule,
  SlotOutOfRange,
  NonDenseSlot
};

/// \brief Sole dense issuer and sequence validator for one context-and-module materialization.
class ModuleLocalIdentityAllocator final {
public:
  ~ModuleLocalIdentityAllocator() noexcept(false);
  ModuleLocalIdentityAllocator(ModuleLocalIdentityAllocator&&) noexcept;
  ModuleLocalIdentityAllocator& operator=(ModuleLocalIdentityAllocator&&) noexcept;
  ZC_DISALLOW_COPY(ModuleLocalIdentityAllocator);

  /// \brief Creates an allocator only when the module belongs to the supplied context.
  ZC_NODISCARD static zc::Maybe<ModuleLocalIdentityAllocator> create(
      identity::SemanticContextBrand context, identity::ModuleId module);
  /// \brief Issues the next dense owner-local binding slot, or none at uint32 exhaustion.
  ZC_NODISCARD zc::Maybe<OwnerLocalBindingId> allocateOwnerLocalBinding();
  /// \brief Advances over a checked dense owner-local prefix without constructing handles.
  ZC_NODISCARD bool skipOwnerLocalBindings(uint32_t count) noexcept;
  /// \brief Issues the next dense anonymous owner-local slot, or none at uint32 exhaustion.
  ZC_NODISCARD zc::Maybe<AnonymousOwnerLocalId> allocateAnonymousOwnerLocal();
  /// \brief Advances over a checked dense anonymous owner-local prefix without constructing
  /// handles.
  ZC_NODISCARD bool skipAnonymousOwnerLocals(uint32_t count) noexcept;
  /// \brief Issues the next dense implementation-occurrence slot, or none at uint32 exhaustion.
  ZC_NODISCARD zc::Maybe<ImplOccurrenceId> allocateImplOccurrence();
  /// \brief Validates context, module, range, and exact dense position.
  ZC_NODISCARD ModuleLocalIdentityFailure validate(OwnerLocalBindingId id,
                                                   uint32_t expectedDenseSlot) const noexcept;
  /// \brief Validates context, module, range, and exact dense position.
  ZC_NODISCARD ModuleLocalIdentityFailure validate(AnonymousOwnerLocalId id,
                                                   uint32_t expectedDenseSlot) const noexcept;
  /// \brief Validates context, module, range, and exact dense position.
  ZC_NODISCARD ModuleLocalIdentityFailure validate(ImplOccurrenceId id,
                                                   uint32_t expectedDenseSlot) const noexcept;
  ZC_NODISCARD uint64_t ownerLocalBindingCount() const noexcept;
  ZC_NODISCARD uint64_t anonymousOwnerLocalCount() const noexcept;
  ZC_NODISCARD uint64_t implOccurrenceCount() const noexcept;

private:
  struct Impl;
  explicit ModuleLocalIdentityAllocator(zc::Own<Impl>&& impl) noexcept;

  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::binder
