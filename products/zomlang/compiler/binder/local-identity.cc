// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/local-identity.h"

#include "zc/core/one-of.h"
#include "zomlang/compiler/identity/canonical/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::binder {
namespace {

constexpr char kOwnerLocalBindingDomain[] = "zom.owner-local-binding";
constexpr char kAnonymousOwnerLocalDomain[] = "zom.anonymous-owner-local";
constexpr uint64_t kMaximumLocalIdentityBytes = 64 * 1024;
constexpr uint64_t kMaximumLocalSyntaxPathComponents = 4096;

bool isOwnerLocalBindingKind(OwnerLocalBindingKind value) noexcept {
  switch (value) {
    case OwnerLocalBindingKind::CallableParameter:
    case OwnerLocalBindingKind::GenericParameter:
    case OwnerLocalBindingKind::Local:
    case OwnerLocalBindingKind::PatternBinding:
      return true;
  }
  return false;
}

bool isOwnerLocalBindingNamespace(OwnerLocalBindingNamespace value) noexcept {
  switch (value) {
    case OwnerLocalBindingNamespace::Value:
    case OwnerLocalBindingNamespace::Type:
    case OwnerLocalBindingNamespace::Module:
    case OwnerLocalBindingNamespace::Label:
    case OwnerLocalBindingNamespace::Attribute:
      return true;
  }
  return false;
}

bool bindingKindMatchesNamespace(OwnerLocalBindingKind kind,
                                 OwnerLocalBindingNamespace nameSpace) noexcept {
  switch (kind) {
    case OwnerLocalBindingKind::CallableParameter:
    case OwnerLocalBindingKind::Local:
    case OwnerLocalBindingKind::PatternBinding:
      return nameSpace == OwnerLocalBindingNamespace::Value;
    case OwnerLocalBindingKind::GenericParameter:
      return nameSpace == OwnerLocalBindingNamespace::Type;
  }
  return false;
}

bool isAnonymousOwnerLocalRole(AnonymousOwnerLocalRole value) noexcept {
  switch (value) {
    case AnonymousOwnerLocalRole::Closure:
    case AnonymousOwnerLocalRole::FunctionExpression:
      return true;
  }
  return false;
}

template <size_t Size>
void encodeDomain(identity::CanonicalEncoder& encoder, const char (&domain)[Size]) {
  for (size_t index = 0; index + 1 < Size; ++index) {
    encoder.encodeUint8(static_cast<uint8_t>(domain[index]));
  }
  encoder.encodeUint8(0);
}

template <size_t Size>
bool decodeDomain(identity::CanonicalDecoder& decoder, const char (&domain)[Size]) {
  for (size_t index = 0; index + 1 < Size; ++index) {
    auto byte = decoder.decodeUint8();
    if (byte == zc::none) { return false; }
    ZC_IF_SOME(value, byte) {
      if (value != static_cast<uint8_t>(domain[index])) { return false; }
    }
  }
  auto terminator = decoder.decodeUint8();
  if (terminator == zc::none) { return false; }
  ZC_IF_SOME(value, terminator) { return value == 0; }
  ZC_UNREACHABLE
}

zc::Maybe<LocalSyntaxPath> decodeLocalSyntaxPath(identity::CanonicalDecoder& decoder) {
  auto count = decoder.decodeSequenceSize(kMaximumLocalSyntaxPathComponents);
  if (count == zc::none) { return zc::none; }

  zc::Vector<uint32_t> components;
  ZC_IF_SOME(value, count) {
    if (value == 0) { return zc::none; }
    for (uint64_t index = 0; index < value; ++index) {
      auto component = decoder.decodeUint32();
      if (component == zc::none) { return zc::none; }
      ZC_IF_SOME(admitted, component) { components.add(admitted); }
    }
  }
  return LocalSyntaxPath::from(zc::mv(components));
}

zc::Maybe<StableBodyOwnerKey> decodeStableBodyOwner(identity::CanonicalDecoder& decoder) {
  auto tag = decoder.decodeUint8();
  if (tag == zc::none) { return zc::none; }
  ZC_IF_SOME(value, tag) {
    if (value == static_cast<uint8_t>(StableBodyOwnerKind::Module)) {
      auto module = identity::ModuleKey::decodeCanonical(decoder);
      ZC_IF_SOME(admitted, module) { return StableBodyOwnerKey::module(zc::mv(admitted)); }
      return zc::none;
    }
    if (value == static_cast<uint8_t>(StableBodyOwnerKind::Definition)) {
      auto bytes = decoder.decodeBytes(32);
      ZC_IF_SOME(admittedBytes, bytes) {
        auto key = identity::DefinitionKey::fromBytes(admittedBytes.asPtr());
        ZC_IF_SOME(admittedKey, key) { return StableBodyOwnerKey::definition(zc::mv(admittedKey)); }
      }
    }
  }
  return zc::none;
}

}  // namespace

namespace local_identity_detail {

struct LocalSyntaxPathData final {
  explicit LocalSyntaxPathData(zc::Vector<uint32_t>&& components) noexcept
      : components(zc::mv(components)) {}

  zc::Vector<uint32_t> components;
};

struct StableBodyOwnerKeyData final {
  explicit StableBodyOwnerKeyData(identity::ModuleKey&& owner) noexcept : owner(zc::mv(owner)) {}
  explicit StableBodyOwnerKeyData(identity::DefinitionKey&& owner) noexcept
      : owner(zc::mv(owner)) {}

  zc::OneOf<identity::ModuleKey, identity::DefinitionKey> owner;
};

struct OwnerLocalBindingKeyData final {
  OwnerLocalBindingKeyData(StableBodyOwnerKey&& owner, LocalSyntaxPath&& path,
                           OwnerLocalBindingNamespace nameSpace, OwnerLocalBindingKind kind,
                           identity::DeclaredDefinitionName&& name) noexcept
      : owner(zc::mv(owner)),
        path(zc::mv(path)),
        nameSpace(nameSpace),
        kind(kind),
        name(zc::mv(name)) {}

  StableBodyOwnerKey owner;
  LocalSyntaxPath path;
  OwnerLocalBindingNamespace nameSpace;
  OwnerLocalBindingKind kind;
  identity::DeclaredDefinitionName name;
};

struct AnonymousOwnerLocalKeyData final {
  AnonymousOwnerLocalKeyData(StableBodyOwnerKey&& owner, LocalSyntaxPath&& path,
                             AnonymousOwnerLocalRole role) noexcept
      : owner(zc::mv(owner)), path(zc::mv(path)), role(role) {}

  StableBodyOwnerKey owner;
  LocalSyntaxPath path;
  AnonymousOwnerLocalRole role;
};

}  // namespace local_identity_detail

LocalSyntaxPath::LocalSyntaxPath(
    zc::Own<local_identity_detail::LocalSyntaxPathData>&& impl) noexcept
    : impl(zc::mv(impl)) {}
LocalSyntaxPath::~LocalSyntaxPath() noexcept(false) = default;
LocalSyntaxPath::LocalSyntaxPath(LocalSyntaxPath&&) noexcept = default;
LocalSyntaxPath& LocalSyntaxPath::operator=(LocalSyntaxPath&&) noexcept = default;

zc::Maybe<LocalSyntaxPath> LocalSyntaxPath::from(zc::Vector<uint32_t>&& components) {
  if (components.size() == 0 || components.size() > kMaximumLocalSyntaxPathComponents) {
    return zc::none;
  }
  return LocalSyntaxPath(zc::heap<local_identity_detail::LocalSyntaxPathData>(zc::mv(components)));
}

zc::Maybe<LocalSyntaxPath> LocalSyntaxPath::decodeCanonical(zc::ArrayPtr<const uint8_t> encoded) {
  identity::CanonicalDecoder decoder(encoded);
  auto path = decodeLocalSyntaxPath(decoder);
  if (path == zc::none || !decoder.finished()) { return zc::none; }
  return zc::mv(path);
}

LocalSyntaxPath LocalSyntaxPath::clone() const {
  zc::Vector<uint32_t> components;
  components.addAll(impl->components);
  return LocalSyntaxPath(zc::heap<local_identity_detail::LocalSyntaxPathData>(zc::mv(components)));
}

zc::ArrayPtr<const uint32_t> LocalSyntaxPath::components() const noexcept {
  return impl->components.asPtr();
}

void LocalSyntaxPath::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeSequenceSize(impl->components.size());
  for (uint32_t component : impl->components) { encoder.encodeUint32(component); }
}

zc::Array<uint8_t> LocalSyntaxPath::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

bool LocalSyntaxPath::operator==(const LocalSyntaxPath& other) const noexcept {
  return impl->components.asPtr() == other.impl->components.asPtr();
}

StableBodyOwnerKey::StableBodyOwnerKey(
    zc::Own<local_identity_detail::StableBodyOwnerKeyData>&& impl) noexcept
    : impl(zc::mv(impl)) {}
StableBodyOwnerKey::~StableBodyOwnerKey() noexcept(false) = default;
StableBodyOwnerKey::StableBodyOwnerKey(StableBodyOwnerKey&&) noexcept = default;
StableBodyOwnerKey& StableBodyOwnerKey::operator=(StableBodyOwnerKey&&) noexcept = default;

StableBodyOwnerKey StableBodyOwnerKey::module(identity::ModuleKey&& key) {
  return StableBodyOwnerKey(zc::heap<local_identity_detail::StableBodyOwnerKeyData>(zc::mv(key)));
}

StableBodyOwnerKey StableBodyOwnerKey::definition(identity::DefinitionKey&& key) {
  return StableBodyOwnerKey(zc::heap<local_identity_detail::StableBodyOwnerKeyData>(zc::mv(key)));
}

zc::Maybe<StableBodyOwnerKey> StableBodyOwnerKey::decodeCanonical(
    zc::ArrayPtr<const uint8_t> encoded) {
  if (encoded.size() > kMaximumLocalIdentityBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(encoded);
  auto value = decodeStableBodyOwner(decoder);
  if (!decoder.finished()) { return zc::none; }
  return value;
}

StableBodyOwnerKey StableBodyOwnerKey::clone() const {
  ZC_IF_SOME(moduleOwner, impl->owner.tryGet<identity::ModuleKey>()) {
    return module(moduleOwner.clone());
  }
  return definition(impl->owner.get<identity::DefinitionKey>().clone());
}

StableBodyOwnerKind StableBodyOwnerKey::kind() const noexcept {
  return impl->owner.is<identity::ModuleKey>() ? StableBodyOwnerKind::Module
                                               : StableBodyOwnerKind::Definition;
}

zc::Maybe<const identity::ModuleKey&> StableBodyOwnerKey::moduleKey() const noexcept {
  return impl->owner.tryGet<identity::ModuleKey>();
}

zc::Maybe<const identity::DefinitionKey&> StableBodyOwnerKey::definitionKey() const noexcept {
  return impl->owner.tryGet<identity::DefinitionKey>();
}

void StableBodyOwnerKey::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(kind()));
  ZC_IF_SOME(moduleOwner, impl->owner.tryGet<identity::ModuleKey>()) {
    moduleOwner.encode(encoder);
    return;
  }
  impl->owner.get<identity::DefinitionKey>().encode(encoder);
}

zc::Array<uint8_t> StableBodyOwnerKey::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

bool StableBodyOwnerKey::operator==(const StableBodyOwnerKey& other) const {
  if (kind() != other.kind()) { return false; }
  if (kind() == StableBodyOwnerKind::Definition) {
    return impl->owner.get<identity::DefinitionKey>() ==
           other.impl->owner.get<identity::DefinitionKey>();
  }
  return impl->owner.get<identity::ModuleKey>().encode().asPtr() ==
         other.impl->owner.get<identity::ModuleKey>().encode().asPtr();
}

OwnerLocalBindingKey::OwnerLocalBindingKey(
    zc::Own<local_identity_detail::OwnerLocalBindingKeyData>&& impl) noexcept
    : impl(zc::mv(impl)) {}
OwnerLocalBindingKey::~OwnerLocalBindingKey() noexcept(false) = default;
OwnerLocalBindingKey::OwnerLocalBindingKey(OwnerLocalBindingKey&&) noexcept = default;
OwnerLocalBindingKey& OwnerLocalBindingKey::operator=(OwnerLocalBindingKey&&) noexcept = default;

zc::Maybe<OwnerLocalBindingKey> OwnerLocalBindingKey::from(
    StableBodyOwnerKey&& owner, LocalSyntaxPath&& path, OwnerLocalBindingNamespace nameSpace,
    OwnerLocalBindingKind kind, identity::DeclaredDefinitionName&& name) {
  if (!isOwnerLocalBindingNamespace(nameSpace) || !isOwnerLocalBindingKind(kind) ||
      !bindingKindMatchesNamespace(kind, nameSpace)) {
    return zc::none;
  }
  return OwnerLocalBindingKey(zc::heap<local_identity_detail::OwnerLocalBindingKeyData>(
      zc::mv(owner), zc::mv(path), nameSpace, kind, zc::mv(name)));
}

zc::Maybe<OwnerLocalBindingKey> OwnerLocalBindingKey::decodeCanonical(
    zc::ArrayPtr<const uint8_t> encoded) {
  if (encoded.size() > kMaximumLocalIdentityBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(encoded);
  if (!decodeDomain(decoder, kOwnerLocalBindingDomain)) { return zc::none; }

  auto owner = decodeStableBodyOwner(decoder);
  auto path = decodeLocalSyntaxPath(decoder);
  auto nameSpace = decoder.decodeUint8();
  auto kind = decoder.decodeUint8();
  auto name = identity::DeclaredDefinitionName::decodeCanonical(decoder);
  if (owner == zc::none || path == zc::none || nameSpace == zc::none || kind == zc::none ||
      name == zc::none || !decoder.finished()) {
    return zc::none;
  }

  ZC_IF_SOME(admittedOwner, owner) {
    ZC_IF_SOME(admittedPath, path) {
      ZC_IF_SOME(admittedNamespace, nameSpace) {
        ZC_IF_SOME(admittedKind, kind) {
          ZC_IF_SOME(admittedName, name) {
            return from(zc::mv(admittedOwner), zc::mv(admittedPath),
                        static_cast<OwnerLocalBindingNamespace>(admittedNamespace),
                        static_cast<OwnerLocalBindingKind>(admittedKind), zc::mv(admittedName));
          }
        }
      }
    }
  }
  ZC_UNREACHABLE
}

OwnerLocalBindingKey OwnerLocalBindingKey::clone() const {
  return OwnerLocalBindingKey(zc::heap<local_identity_detail::OwnerLocalBindingKeyData>(
      impl->owner.clone(), impl->path.clone(), impl->nameSpace, impl->kind, impl->name.clone()));
}

const StableBodyOwnerKey& OwnerLocalBindingKey::owner() const noexcept { return impl->owner; }
const LocalSyntaxPath& OwnerLocalBindingKey::path() const noexcept { return impl->path; }
OwnerLocalBindingNamespace OwnerLocalBindingKey::nameSpace() const noexcept {
  return impl->nameSpace;
}
OwnerLocalBindingKind OwnerLocalBindingKey::kind() const noexcept { return impl->kind; }
const identity::DeclaredDefinitionName& OwnerLocalBindingKey::name() const noexcept {
  return impl->name;
}

void OwnerLocalBindingKey::encode(identity::CanonicalEncoder& encoder) const {
  encodeDomain(encoder, kOwnerLocalBindingDomain);
  impl->owner.encode(encoder);
  impl->path.encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(impl->nameSpace));
  encoder.encodeUint8(static_cast<uint8_t>(impl->kind));
  impl->name.encode(encoder);
}

zc::Array<uint8_t> OwnerLocalBindingKey::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

bool OwnerLocalBindingKey::operator==(const OwnerLocalBindingKey& other) const {
  return impl->owner == other.impl->owner && impl->path == other.impl->path &&
         impl->nameSpace == other.impl->nameSpace && impl->kind == other.impl->kind &&
         impl->name == other.impl->name;
}

AnonymousOwnerLocalKey::AnonymousOwnerLocalKey(
    zc::Own<local_identity_detail::AnonymousOwnerLocalKeyData>&& impl) noexcept
    : impl(zc::mv(impl)) {}
AnonymousOwnerLocalKey::~AnonymousOwnerLocalKey() noexcept(false) = default;
AnonymousOwnerLocalKey::AnonymousOwnerLocalKey(AnonymousOwnerLocalKey&&) noexcept = default;
AnonymousOwnerLocalKey& AnonymousOwnerLocalKey::operator=(AnonymousOwnerLocalKey&&) noexcept =
    default;

zc::Maybe<AnonymousOwnerLocalKey> AnonymousOwnerLocalKey::from(StableBodyOwnerKey&& owner,
                                                               LocalSyntaxPath&& path,
                                                               AnonymousOwnerLocalRole role) {
  if (!isAnonymousOwnerLocalRole(role)) { return zc::none; }
  return AnonymousOwnerLocalKey(zc::heap<local_identity_detail::AnonymousOwnerLocalKeyData>(
      zc::mv(owner), zc::mv(path), role));
}

zc::Maybe<AnonymousOwnerLocalKey> AnonymousOwnerLocalKey::decodeCanonical(
    zc::ArrayPtr<const uint8_t> encoded) {
  if (encoded.size() > kMaximumLocalIdentityBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(encoded);
  if (!decodeDomain(decoder, kAnonymousOwnerLocalDomain)) { return zc::none; }

  auto owner = decodeStableBodyOwner(decoder);
  auto path = decodeLocalSyntaxPath(decoder);
  auto role = decoder.decodeUint8();
  if (owner == zc::none || path == zc::none || role == zc::none || !decoder.finished()) {
    return zc::none;
  }

  ZC_IF_SOME(admittedOwner, owner) {
    ZC_IF_SOME(admittedPath, path) {
      ZC_IF_SOME(admittedRole, role) {
        return from(zc::mv(admittedOwner), zc::mv(admittedPath),
                    static_cast<AnonymousOwnerLocalRole>(admittedRole));
      }
    }
  }
  ZC_UNREACHABLE
}

AnonymousOwnerLocalKey AnonymousOwnerLocalKey::clone() const {
  return AnonymousOwnerLocalKey(zc::heap<local_identity_detail::AnonymousOwnerLocalKeyData>(
      impl->owner.clone(), impl->path.clone(), impl->role));
}

const StableBodyOwnerKey& AnonymousOwnerLocalKey::owner() const noexcept { return impl->owner; }
const LocalSyntaxPath& AnonymousOwnerLocalKey::path() const noexcept { return impl->path; }
AnonymousOwnerLocalRole AnonymousOwnerLocalKey::role() const noexcept { return impl->role; }

void AnonymousOwnerLocalKey::encode(identity::CanonicalEncoder& encoder) const {
  encodeDomain(encoder, kAnonymousOwnerLocalDomain);
  impl->owner.encode(encoder);
  impl->path.encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(impl->role));
}

zc::Array<uint8_t> AnonymousOwnerLocalKey::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

bool AnonymousOwnerLocalKey::operator==(const AnonymousOwnerLocalKey& other) const {
  return impl->owner == other.impl->owner && impl->path == other.impl->path &&
         impl->role == other.impl->role;
}

struct ModuleLocalIdentityAllocator::Impl final {
  Impl(identity::SemanticContextBrand context, identity::ModuleId module) noexcept
      : context(context), module(module) {}

  identity::SemanticContextBrand context;
  identity::ModuleId module;
  uint64_t ownerLocalBindingCount = 0;
  uint64_t anonymousOwnerLocalCount = 0;
  uint64_t implOccurrenceCount = 0;
};

ModuleLocalIdentityAllocator::ModuleLocalIdentityAllocator(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
ModuleLocalIdentityAllocator::~ModuleLocalIdentityAllocator() noexcept(false) = default;
ModuleLocalIdentityAllocator::ModuleLocalIdentityAllocator(
    ModuleLocalIdentityAllocator&&) noexcept = default;
ModuleLocalIdentityAllocator& ModuleLocalIdentityAllocator::operator=(
    ModuleLocalIdentityAllocator&&) noexcept = default;

zc::Maybe<ModuleLocalIdentityAllocator> ModuleLocalIdentityAllocator::create(
    identity::SemanticContextBrand context, identity::ModuleId module) {
  if (!context.isValid() || !module.belongsTo(context)) { return zc::none; }
  return ModuleLocalIdentityAllocator(zc::heap<Impl>(context, module));
}

zc::Maybe<OwnerLocalBindingId> ModuleLocalIdentityAllocator::allocateOwnerLocalBinding() {
  if (impl->ownerLocalBindingCount >= static_cast<uint64_t>(0xffffffffu)) { return zc::none; }
  const auto slot = static_cast<uint32_t>(impl->ownerLocalBindingCount);
  ++impl->ownerLocalBindingCount;
  return OwnerLocalBindingId(impl->context, impl->module, slot);
}

bool ModuleLocalIdentityAllocator::skipOwnerLocalBindings(uint32_t count) noexcept {
  if (impl->ownerLocalBindingCount > static_cast<uint64_t>(0xffffffffu) ||
      static_cast<uint64_t>(count) >
          static_cast<uint64_t>(0xffffffffu) - impl->ownerLocalBindingCount) {
    return false;
  }
  impl->ownerLocalBindingCount += count;
  return true;
}

zc::Maybe<AnonymousOwnerLocalId> ModuleLocalIdentityAllocator::allocateAnonymousOwnerLocal() {
  if (impl->anonymousOwnerLocalCount >= static_cast<uint64_t>(0xffffffffu)) { return zc::none; }
  const auto slot = static_cast<uint32_t>(impl->anonymousOwnerLocalCount);
  ++impl->anonymousOwnerLocalCount;
  return AnonymousOwnerLocalId(impl->context, impl->module, slot);
}

bool ModuleLocalIdentityAllocator::skipAnonymousOwnerLocals(uint32_t count) noexcept {
  if (impl->anonymousOwnerLocalCount > static_cast<uint64_t>(0xffffffffu) ||
      static_cast<uint64_t>(count) >
          static_cast<uint64_t>(0xffffffffu) - impl->anonymousOwnerLocalCount) {
    return false;
  }
  impl->anonymousOwnerLocalCount += count;
  return true;
}

zc::Maybe<ImplOccurrenceId> ModuleLocalIdentityAllocator::allocateImplOccurrence() {
  if (impl->implOccurrenceCount >= static_cast<uint64_t>(0xffffffffu)) { return zc::none; }
  const auto slot = static_cast<uint32_t>(impl->implOccurrenceCount);
  ++impl->implOccurrenceCount;
  return ImplOccurrenceId(impl->context, impl->module, slot);
}

ModuleLocalIdentityFailure ModuleLocalIdentityAllocator::validate(
    OwnerLocalBindingId id, uint32_t expectedDenseSlot) const noexcept {
  if (!id.isValid()) { return ModuleLocalIdentityFailure::InvalidHandle; }
  if (!id.belongsTo(impl->context)) { return ModuleLocalIdentityFailure::ForeignContext; }
  if (!id.belongsTo(impl->module)) { return ModuleLocalIdentityFailure::ForeignModule; }
  if (id.slot >= impl->ownerLocalBindingCount) {
    return ModuleLocalIdentityFailure::SlotOutOfRange;
  }
  if (id.slot != expectedDenseSlot) { return ModuleLocalIdentityFailure::NonDenseSlot; }
  return ModuleLocalIdentityFailure::None;
}

ModuleLocalIdentityFailure ModuleLocalIdentityAllocator::validate(
    AnonymousOwnerLocalId id, uint32_t expectedDenseSlot) const noexcept {
  if (!id.isValid()) { return ModuleLocalIdentityFailure::InvalidHandle; }
  if (!id.belongsTo(impl->context)) { return ModuleLocalIdentityFailure::ForeignContext; }
  if (!id.belongsTo(impl->module)) { return ModuleLocalIdentityFailure::ForeignModule; }
  if (id.slot >= impl->anonymousOwnerLocalCount) {
    return ModuleLocalIdentityFailure::SlotOutOfRange;
  }
  if (id.slot != expectedDenseSlot) { return ModuleLocalIdentityFailure::NonDenseSlot; }
  return ModuleLocalIdentityFailure::None;
}

ModuleLocalIdentityFailure ModuleLocalIdentityAllocator::validate(
    ImplOccurrenceId id, uint32_t expectedDenseSlot) const noexcept {
  if (!id.isValid()) { return ModuleLocalIdentityFailure::InvalidHandle; }
  if (!id.belongsTo(impl->context)) { return ModuleLocalIdentityFailure::ForeignContext; }
  if (!id.belongsTo(impl->module)) { return ModuleLocalIdentityFailure::ForeignModule; }
  if (id.slot >= impl->implOccurrenceCount) { return ModuleLocalIdentityFailure::SlotOutOfRange; }
  if (id.slot != expectedDenseSlot) { return ModuleLocalIdentityFailure::NonDenseSlot; }
  return ModuleLocalIdentityFailure::None;
}

uint64_t ModuleLocalIdentityAllocator::ownerLocalBindingCount() const noexcept {
  return impl->ownerLocalBindingCount;
}

uint64_t ModuleLocalIdentityAllocator::anonymousOwnerLocalCount() const noexcept {
  return impl->anonymousOwnerLocalCount;
}

uint64_t ModuleLocalIdentityAllocator::implOccurrenceCount() const noexcept {
  return impl->implOccurrenceCount;
}

}  // namespace zomlang::compiler::binder
