#include "zomlang/compiler/binder/owner-body-syntax.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::binder {

namespace owner_body_syntax_detail {

struct ModuleBodyOwnersData final {
  ModuleBodyOwnersData(identity::ModuleKey&& owningModule,
                       zc::Vector<StableBodyOwnerKey>&& owners) noexcept
      : owningModule(zc::mv(owningModule)), owners(zc::mv(owners)) {}

  identity::ModuleKey owningModule;
  zc::Vector<StableBodyOwnerKey> owners;
};

struct OwnerBodySyntaxData final {
  OwnerBodySyntaxData(StableBodyOwnerKey&& owner, identity::ModuleKey&& owningModule,
                      ModuleBodySyntax&& syntax) noexcept
      : owner(zc::mv(owner)), owningModule(zc::mv(owningModule)), syntax(zc::mv(syntax)) {}

  StableBodyOwnerKey owner;
  identity::ModuleKey owningModule;
  ModuleBodySyntax syntax;
};

struct OwnerBodyProvenanceData final {
  OwnerBodyProvenanceData(StableBodyOwnerKey&& owner, ModuleBodyProvenance&& provenance) noexcept
      : owner(zc::mv(owner)), provenance(zc::mv(provenance)) {}

  StableBodyOwnerKey owner;
  ModuleBodyProvenance provenance;
};

}  // namespace owner_body_syntax_detail

namespace detail = owner_body_syntax_detail;
namespace {

constexpr zc::StringPtr kModuleBodyOwnersDomain = "zom.module-body-owners.v1"_zc;
constexpr zc::StringPtr kOwnerBodySyntaxDomain = "zom.owner-body-syntax.v1"_zc;
constexpr zc::StringPtr kOwnerBodyProvenanceDomain = "zom.owner-body-provenance.v1"_zc;
constexpr uint64_t kMaximumOwnerCount = 1024 * 1024;
constexpr uint64_t kMaximumOwnerKeyBytes = 64 * 1024;
constexpr uint64_t kMaximumModuleBodyOwnersBytes = 64 * 1024 * 1024;
constexpr uint64_t kMaximumOwnerBodyValueBytes = 128 * 1024 * 1024;

int compareCanonicalBytes(zc::ArrayPtr<const uint8_t> left,
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

bool sameCanonicalEncoding(zc::ArrayPtr<const uint8_t> expected,
                           zc::ArrayPtr<const uint8_t> actual) noexcept {
  return expected == actual;
}

zc::Maybe<StableBodyOwnerKey> decodeOwner(zc::ArrayPtr<const uint8_t> encoded) {
  if (encoded.size() == 0 || encoded.size() > kMaximumOwnerKeyBytes) { return zc::none; }
  auto owner = StableBodyOwnerKey::decodeCanonical(encoded);
  if (owner == zc::none) { return zc::none; }
  ZC_IF_SOME(value, owner) {
    if (!sameCanonicalEncoding(encoded, value.encode().asPtr())) { return zc::none; }
    return zc::mv(value);
  }
  ZC_UNREACHABLE
}

}  // namespace

ModuleBodyOwners::ModuleBodyOwners(zc::Own<detail::ModuleBodyOwnersData>&& value) noexcept
    : impl(zc::mv(value)) {}
ModuleBodyOwners::~ModuleBodyOwners() noexcept(false) = default;
ModuleBodyOwners::ModuleBodyOwners(ModuleBodyOwners&&) noexcept = default;
ModuleBodyOwners& ModuleBodyOwners::operator=(ModuleBodyOwners&&) noexcept = default;

zc::Maybe<ModuleBodyOwners> ModuleBodyOwners::from(identity::ModuleKey&& owningModule,
                                                   zc::Vector<StableBodyOwnerKey>&& owners) {
  if (owners.size() == 0 || owners.size() > kMaximumOwnerCount) { return zc::none; }

  bool sawModuleOwner = false;
  for (size_t index = 0; index < owners.size(); ++index) {
    const auto encoded = owners[index].encode();
    if (encoded.size() == 0 || encoded.size() > kMaximumOwnerKeyBytes) { return zc::none; }
    if (index != 0) {
      const auto previous = owners[index - 1].encode();
      if (compareCanonicalBytes(previous.asPtr(), encoded.asPtr()) >= 0) { return zc::none; }
    }

    if (owners[index].kind() == StableBodyOwnerKind::Module) {
      if (sawModuleOwner || index != 0) { return zc::none; }
      ZC_IF_SOME(module, owners[index].moduleKey()) {
        if (!sameModule(module, owningModule)) { return zc::none; }
        sawModuleOwner = true;
      }
      if (!sawModuleOwner) { return zc::none; }
    } else if (index == 0) {
      return zc::none;
    }
  }
  if (!sawModuleOwner) { return zc::none; }

  ModuleBodyOwners result(
      zc::heap<detail::ModuleBodyOwnersData>(zc::mv(owningModule), zc::mv(owners)));
  if (result.encodeCanonical().size() > kMaximumModuleBodyOwnersBytes) { return zc::none; }
  return zc::mv(result);
}

zc::Maybe<ModuleBodyOwners> ModuleBodyOwners::decodeCanonical(zc::ArrayPtr<const uint8_t> encoded) {
  if (encoded.size() == 0 || encoded.size() > kMaximumModuleBodyOwnersBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(encoded);
  auto domain = decoder.decodeByteString(kModuleBodyOwnersDomain.size());
  auto owningModule = identity::ModuleKey::decodeCanonical(decoder);
  auto count = decoder.decodeSequenceSize(kMaximumOwnerCount);
  if (domain == zc::none || owningModule == zc::none || count == zc::none ||
      ZC_ASSERT_NONNULL(domain).asPtr() != kModuleBodyOwnersDomain.asBytes()) {
    return zc::none;
  }

  zc::Vector<StableBodyOwnerKey> owners(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto ownerBytes = decoder.decodeByteString(kMaximumOwnerKeyBytes);
    if (ownerBytes == zc::none) { return zc::none; }
    auto owner = decodeOwner(ZC_ASSERT_NONNULL(ownerBytes).asPtr());
    if (owner == zc::none) { return zc::none; }
    ZC_IF_SOME(value, owner) { owners.add(zc::mv(value)); }
  }
  if (!decoder.finished()) { return zc::none; }

  auto result = from(zc::mv(ZC_ASSERT_NONNULL(owningModule)), zc::mv(owners));
  if (result == zc::none) { return zc::none; }
  ZC_IF_SOME(value, result) {
    if (!sameCanonicalEncoding(encoded, value.encodeCanonical().asPtr())) { return zc::none; }
    return zc::mv(value);
  }
  ZC_UNREACHABLE
}

ModuleBodyOwners ModuleBodyOwners::clone() const {
  zc::Vector<StableBodyOwnerKey> owners(impl->owners.size());
  for (const auto& owner : impl->owners) { owners.add(owner.clone()); }
  auto result = from(impl->owningModule.clone(), zc::mv(owners));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
}

const identity::ModuleKey& ModuleBodyOwners::owningModule() const noexcept {
  return impl->owningModule;
}

zc::ArrayPtr<const StableBodyOwnerKey> ModuleBodyOwners::owners() const {
  return impl->owners.asPtr();
}

zc::Array<uint8_t> ModuleBodyOwners::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(kModuleBodyOwnersDomain.asBytes());
  impl->owningModule.encode(encoder);
  encoder.encodeSequenceSize(impl->owners.size());
  for (const auto& owner : impl->owners) {
    const auto encoded = owner.encode();
    encoder.encodeByteString(encoded.asPtr());
  }
  return encoder.finish();
}

bool ModuleBodyOwners::operator==(const ModuleBodyOwners& other) const {
  if (!sameModule(impl->owningModule, other.impl->owningModule) ||
      impl->owners.size() != other.impl->owners.size()) {
    return false;
  }
  for (size_t index = 0; index < impl->owners.size(); ++index) {
    if (impl->owners[index] != other.impl->owners[index]) { return false; }
  }
  return true;
}

OwnerBodySyntax::OwnerBodySyntax(zc::Own<detail::OwnerBodySyntaxData>&& value) noexcept
    : impl(zc::mv(value)) {}
OwnerBodySyntax::~OwnerBodySyntax() noexcept(false) = default;
OwnerBodySyntax::OwnerBodySyntax(OwnerBodySyntax&&) noexcept = default;
OwnerBodySyntax& OwnerBodySyntax::operator=(OwnerBodySyntax&&) noexcept = default;

zc::Maybe<OwnerBodySyntax> OwnerBodySyntax::from(StableBodyOwnerKey&& owner,
                                                 identity::ModuleKey&& owningModule,
                                                 ModuleBodySyntax&& syntax) {
  if (owner.kind() == StableBodyOwnerKind::Module) {
    bool matchingModule = false;
    ZC_IF_SOME(module, owner.moduleKey()) { matchingModule = sameModule(module, owningModule); }
    if (!matchingModule) { return zc::none; }
  } else if (syntax.rootCount() != 1) {
    return zc::none;
  }

  OwnerBodySyntax result(
      zc::heap<detail::OwnerBodySyntaxData>(zc::mv(owner), zc::mv(owningModule), zc::mv(syntax)));
  if (result.encodeCanonical().size() > kMaximumOwnerBodyValueBytes) { return zc::none; }
  return zc::mv(result);
}

zc::Maybe<OwnerBodySyntax> OwnerBodySyntax::decodeCanonical(zc::ArrayPtr<const uint8_t> encoded) {
  if (encoded.size() == 0 || encoded.size() > kMaximumOwnerBodyValueBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(encoded);
  auto domain = decoder.decodeByteString(kOwnerBodySyntaxDomain.size());
  auto ownerBytes = decoder.decodeByteString(kMaximumOwnerKeyBytes);
  auto owningModule = identity::ModuleKey::decodeCanonical(decoder);
  auto syntaxBytes = decoder.decodeByteString(kMaximumOwnerBodyValueBytes);
  if (domain == zc::none || ownerBytes == zc::none || owningModule == zc::none ||
      syntaxBytes == zc::none || !decoder.finished() ||
      ZC_ASSERT_NONNULL(domain).asPtr() != kOwnerBodySyntaxDomain.asBytes()) {
    return zc::none;
  }

  auto owner = decodeOwner(ZC_ASSERT_NONNULL(ownerBytes).asPtr());
  auto syntax = ModuleBodySyntax::decodeCanonical(ZC_ASSERT_NONNULL(syntaxBytes).asPtr());
  if (owner == zc::none || syntax == zc::none) { return zc::none; }
  auto result = from(zc::mv(ZC_ASSERT_NONNULL(owner)), zc::mv(ZC_ASSERT_NONNULL(owningModule)),
                     zc::mv(ZC_ASSERT_NONNULL(syntax)));
  if (result == zc::none) { return zc::none; }
  ZC_IF_SOME(value, result) {
    if (!sameCanonicalEncoding(encoded, value.encodeCanonical().asPtr())) { return zc::none; }
    return zc::mv(value);
  }
  ZC_UNREACHABLE
}

OwnerBodySyntax OwnerBodySyntax::clone() const {
  auto result = from(impl->owner.clone(), impl->owningModule.clone(), impl->syntax.clone());
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
}

const StableBodyOwnerKey& OwnerBodySyntax::owner() const noexcept { return impl->owner; }

const identity::ModuleKey& OwnerBodySyntax::owningModule() const noexcept {
  return impl->owningModule;
}

const ModuleBodySyntax& OwnerBodySyntax::detachedSyntax() const noexcept { return impl->syntax; }

zc::Array<uint8_t> OwnerBodySyntax::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(kOwnerBodySyntaxDomain.asBytes());
  const auto owner = impl->owner.encode();
  encoder.encodeByteString(owner.asPtr());
  impl->owningModule.encode(encoder);
  const auto syntax = impl->syntax.encodeCanonical();
  encoder.encodeByteString(syntax.asPtr());
  return encoder.finish();
}

bool OwnerBodySyntax::operator==(const OwnerBodySyntax& other) const {
  return impl->owner == other.impl->owner &&
         sameModule(impl->owningModule, other.impl->owningModule) &&
         impl->syntax == other.impl->syntax;
}

OwnerBodyProvenance::OwnerBodyProvenance(zc::Own<detail::OwnerBodyProvenanceData>&& value) noexcept
    : impl(zc::mv(value)) {}
OwnerBodyProvenance::~OwnerBodyProvenance() noexcept(false) = default;
OwnerBodyProvenance::OwnerBodyProvenance(OwnerBodyProvenance&&) noexcept = default;
OwnerBodyProvenance& OwnerBodyProvenance::operator=(OwnerBodyProvenance&&) noexcept = default;

zc::Maybe<OwnerBodyProvenance> OwnerBodyProvenance::from(StableBodyOwnerKey&& owner,
                                                         ModuleBodyProvenance&& provenance) {
  OwnerBodyProvenance result(
      zc::heap<detail::OwnerBodyProvenanceData>(zc::mv(owner), zc::mv(provenance)));
  if (result.encodeCanonical().size() > kMaximumOwnerBodyValueBytes) { return zc::none; }
  return zc::mv(result);
}

zc::Maybe<OwnerBodyProvenance> OwnerBodyProvenance::decodeCanonical(
    zc::ArrayPtr<const uint8_t> encoded) {
  if (encoded.size() == 0 || encoded.size() > kMaximumOwnerBodyValueBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(encoded);
  auto domain = decoder.decodeByteString(kOwnerBodyProvenanceDomain.size());
  auto ownerBytes = decoder.decodeByteString(kMaximumOwnerKeyBytes);
  auto provenanceBytes = decoder.decodeByteString(kMaximumOwnerBodyValueBytes);
  if (domain == zc::none || ownerBytes == zc::none || provenanceBytes == zc::none ||
      !decoder.finished() ||
      ZC_ASSERT_NONNULL(domain).asPtr() != kOwnerBodyProvenanceDomain.asBytes()) {
    return zc::none;
  }

  auto owner = decodeOwner(ZC_ASSERT_NONNULL(ownerBytes).asPtr());
  auto provenance =
      ModuleBodyProvenance::decodeCanonical(ZC_ASSERT_NONNULL(provenanceBytes).asPtr());
  if (owner == zc::none || provenance == zc::none) { return zc::none; }
  auto result = from(zc::mv(ZC_ASSERT_NONNULL(owner)), zc::mv(ZC_ASSERT_NONNULL(provenance)));
  if (result == zc::none) { return zc::none; }
  ZC_IF_SOME(value, result) {
    if (!sameCanonicalEncoding(encoded, value.encodeCanonical().asPtr())) { return zc::none; }
    return zc::mv(value);
  }
  ZC_UNREACHABLE
}

OwnerBodyProvenance OwnerBodyProvenance::clone() const {
  auto result = from(impl->owner.clone(), impl->provenance.clone());
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
}

const StableBodyOwnerKey& OwnerBodyProvenance::owner() const noexcept { return impl->owner; }

const ModuleBodyProvenance& OwnerBodyProvenance::detachedProvenance() const noexcept {
  return impl->provenance;
}

zc::Array<uint8_t> OwnerBodyProvenance::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(kOwnerBodyProvenanceDomain.asBytes());
  const auto owner = impl->owner.encode();
  encoder.encodeByteString(owner.asPtr());
  const auto provenance = impl->provenance.encodeCanonical();
  encoder.encodeByteString(provenance.asPtr());
  return encoder.finish();
}

bool OwnerBodyProvenance::operator==(const OwnerBodyProvenance& other) const {
  return impl->owner == other.impl->owner && impl->provenance == other.impl->provenance;
}

}  // namespace zomlang::compiler::binder
