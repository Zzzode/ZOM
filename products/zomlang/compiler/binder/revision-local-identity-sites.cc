#include "zomlang/compiler/binder/revision-local-identity-sites.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::binder {
namespace {

constexpr zc::StringPtr kDefinitionSitesDomain = "zom.revision-local-definition-sites.v1"_zc;
constexpr zc::StringPtr kImplementationSitesDomain =
    "zom.revision-local-implementation-sites.v1"_zc;
constexpr uint64_t kMaximumSites = 1024 * 1024;
constexpr uint64_t kMaximumOccurrenceBytes = 128 * 1024;
constexpr uint64_t kMaximumEncodedBytes = 64 * 1024 * 1024;

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

bool containsDefinition(const NamedDefinitionInventory& inventory,
                        const identity::DefinitionKey& key) {
  for (const auto& entry : inventory.entries()) {
    if (entry.key() == key) { return true; }
  }
  return false;
}

bool containsImplementation(const NamedImplementationInventory& inventory,
                            const identity::ImplKey& key) {
  for (const auto& candidate : inventory.keys()) {
    if (candidate == key) { return true; }
  }
  return false;
}

template <typename Site, typename KeyBytes>
void sortSites(zc::Vector<Site>& sites, KeyBytes&& keyBytes) {
  for (size_t index = 1; index < sites.size(); ++index) {
    auto current = zc::mv(sites[index]);
    size_t insertion = index;
    while (insertion != 0 &&
           compareBytes(keyBytes(current).asPtr(), keyBytes(sites[insertion - 1]).asPtr()) < 0) {
      sites[insertion] = zc::mv(sites[insertion - 1]);
      --insertion;
    }
    sites[insertion] = zc::mv(current);
  }
}

bool validDefinitionSites(const identity::ModuleKey& module,
                          const identity::SourceFileKey& source,
                          const NamedDefinitionInventory& inventory,
                          zc::ArrayPtr<const RevisionLocalDefinitionSite> sites) {
  for (size_t index = 0; index < sites.size(); ++index) {
    const auto& site = sites[index];
    if (!site.node() || !sameModule(site.site().module(), module) ||
        !site.site().source().sameAs(source) || site.byteStart() > site.byteEnd() ||
        !containsDefinition(inventory, site.definition())) {
      return false;
    }
    for (size_t prior = 0; prior < index; ++prior) {
      if (sites[prior].node() == site.node()) { return false; }
    }
    if (index != 0) {
      const auto previous = sites[index - 1].site().encode();
      const auto current = site.site().encode();
      if (compareBytes(previous.asPtr(), current.asPtr()) >= 0) { return false; }
    }
  }
  return true;
}

bool validImplementationSites(const identity::ModuleKey& module,
                              const identity::SourceFileKey& source,
                              const NamedImplementationInventory& inventory,
                              zc::ArrayPtr<const RevisionLocalImplementationSite> sites) {
  for (size_t index = 0; index < sites.size(); ++index) {
    const auto& site = sites[index];
    const auto& occurrence = site.occurrence();
    if (!site.node() || !sameModule(occurrence.site().module(), module) ||
        !occurrence.site().source().sameAs(source) || site.byteStart() > site.byteEnd() ||
        !containsImplementation(inventory, occurrence.implementation())) {
      return false;
    }
    for (size_t prior = 0; prior < index; ++prior) {
      if (sites[prior].node() == site.node()) { return false; }
    }
    if (index != 0) {
      const auto previous = sites[index - 1].occurrence().site().encode();
      const auto current = occurrence.site().encode();
      if (compareBytes(previous.asPtr(), current.asPtr()) >= 0) { return false; }
    }
  }
  return true;
}

}  // namespace

RevisionLocalDefinitionSite::RevisionLocalDefinitionSite(
    ast::NodeId node, identity::DefinitionKey&& definition, IdentitySyntaxSiteKey&& site,
    uint64_t byteStart, uint64_t byteEnd) noexcept
    : nodeField(node),
      definitionField(zc::mv(definition)),
      siteField(zc::mv(site)),
      byteStartField(byteStart),
      byteEndField(byteEnd) {}

zc::Maybe<RevisionLocalDefinitionSite> RevisionLocalDefinitionSite::from(
    ast::NodeId node, identity::DefinitionKey&& definition, IdentitySyntaxSiteKey&& site,
    uint64_t byteStart, uint64_t byteEnd) {
  if (!node || byteStart > byteEnd || !site.source().belongsTo(site.module().crate())) {
    return zc::none;
  }
  return RevisionLocalDefinitionSite(node, zc::mv(definition), zc::mv(site), byteStart, byteEnd);
}

RevisionLocalDefinitionSite RevisionLocalDefinitionSite::clone() const {
  return RevisionLocalDefinitionSite(nodeField, definitionField.clone(), siteField.clone(),
                                     byteStartField, byteEndField);
}
ast::NodeId RevisionLocalDefinitionSite::node() const noexcept { return nodeField; }
const identity::DefinitionKey& RevisionLocalDefinitionSite::definition() const noexcept {
  return definitionField;
}
const IdentitySyntaxSiteKey& RevisionLocalDefinitionSite::site() const noexcept {
  return siteField;
}
uint64_t RevisionLocalDefinitionSite::byteStart() const noexcept { return byteStartField; }
uint64_t RevisionLocalDefinitionSite::byteEnd() const noexcept { return byteEndField; }

RevisionLocalDefinitionSites::RevisionLocalDefinitionSites(
    zc::Vector<RevisionLocalDefinitionSite>&& sites) noexcept
    : siteFields(zc::mv(sites)) {}

zc::Maybe<RevisionLocalDefinitionSites> RevisionLocalDefinitionSites::fromVerified(
    const identity::ModuleKey& module, const identity::SourceFileKey& source,
    const NamedDefinitionInventory& inventory,
    zc::Vector<RevisionLocalDefinitionSite>&& sites) {
  if (sites.size() > kMaximumSites) { return zc::none; }
  sortSites(sites, [](const RevisionLocalDefinitionSite& site) { return site.site().encode(); });
  if (!validDefinitionSites(module, source, inventory, sites.asPtr())) { return zc::none; }
  RevisionLocalDefinitionSites result(zc::mv(sites));
  if (result.encodeCanonical().size() > kMaximumEncodedBytes) { return zc::none; }
  return zc::mv(result);
}

zc::Maybe<RevisionLocalDefinitionSites> RevisionLocalDefinitionSites::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumEncodedBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto domain = decoder.decodeByteString(kDefinitionSitesDomain.size());
  auto count = decoder.decodeSequenceSize(kMaximumSites);
  if (domain == zc::none || count == zc::none ||
      ZC_ASSERT_NONNULL(domain).asPtr() != kDefinitionSitesDomain.asBytes()) {
    return zc::none;
  }
  zc::Vector<RevisionLocalDefinitionSite> sites(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto node = decoder.decodeUint32();
    auto digest = decoder.decodeDigest();
    auto site = IdentitySyntaxSiteKey::decodeCanonical(decoder);
    auto byteStart = decoder.decodeUint64();
    auto byteEnd = decoder.decodeUint64();
    if (node == zc::none || digest == zc::none || site == zc::none || byteStart == zc::none ||
        byteEnd == zc::none) {
      return zc::none;
    }
    auto key = identity::DefinitionKey::fromBytes(ZC_ASSERT_NONNULL(digest).bytes());
    if (key == zc::none) { return zc::none; }
    auto entry = RevisionLocalDefinitionSite::from(
        ast::NodeId(ZC_ASSERT_NONNULL(node)), zc::mv(ZC_ASSERT_NONNULL(key)),
        zc::mv(ZC_ASSERT_NONNULL(site)), ZC_ASSERT_NONNULL(byteStart),
        ZC_ASSERT_NONNULL(byteEnd));
    if (entry == zc::none) { return zc::none; }
    ZC_IF_SOME(value, entry) {
      if (sites.size() != 0) {
        const auto previous = sites.back().site().encode();
        const auto current = value.site().encode();
        if (compareBytes(previous.asPtr(), current.asPtr()) >= 0) { return zc::none; }
        for (const auto& prior : sites) {
          if (prior.node() == value.node()) { return zc::none; }
        }
      }
      sites.add(zc::mv(value));
    }
  }
  if (!decoder.finished()) { return zc::none; }
  return RevisionLocalDefinitionSites(zc::mv(sites));
}

RevisionLocalDefinitionSites RevisionLocalDefinitionSites::clone() const {
  zc::Vector<RevisionLocalDefinitionSite> sites(siteFields.size());
  for (const auto& site : siteFields) { sites.add(site.clone()); }
  return RevisionLocalDefinitionSites(zc::mv(sites));
}
zc::ArrayPtr<const RevisionLocalDefinitionSite> RevisionLocalDefinitionSites::entries() const {
  return siteFields.asPtr();
}
zc::Array<uint8_t> RevisionLocalDefinitionSites::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(kDefinitionSitesDomain.asBytes());
  encoder.encodeSequenceSize(siteFields.size());
  for (const auto& site : siteFields) {
    encoder.encodeUint32(site.node().value);
    site.definition().encode(encoder);
    site.site().encode(encoder);
    encoder.encodeUint64(site.byteStart());
    encoder.encodeUint64(site.byteEnd());
  }
  return encoder.finish();
}
bool RevisionLocalDefinitionSites::sameAs(const RevisionLocalDefinitionSites& other) const {
  return encodeCanonical().asPtr() == other.encodeCanonical().asPtr();
}

RevisionLocalImplementationSite::RevisionLocalImplementationSite(
    ast::NodeId node, ImplSourceOccurrenceKey&& occurrence, uint64_t byteStart,
    uint64_t byteEnd) noexcept
    : nodeField(node),
      occurrenceField(zc::mv(occurrence)),
      byteStartField(byteStart),
      byteEndField(byteEnd) {}

zc::Maybe<RevisionLocalImplementationSite> RevisionLocalImplementationSite::from(
    ast::NodeId node, ImplSourceOccurrenceKey&& occurrence, uint64_t byteStart,
    uint64_t byteEnd) {
  if (!node || byteStart > byteEnd) { return zc::none; }
  return RevisionLocalImplementationSite(node, zc::mv(occurrence), byteStart, byteEnd);
}

RevisionLocalImplementationSite RevisionLocalImplementationSite::clone() const {
  return RevisionLocalImplementationSite(nodeField, occurrenceField.clone(), byteStartField,
                                         byteEndField);
}
ast::NodeId RevisionLocalImplementationSite::node() const noexcept { return nodeField; }
const ImplSourceOccurrenceKey& RevisionLocalImplementationSite::occurrence() const noexcept {
  return occurrenceField;
}
uint64_t RevisionLocalImplementationSite::byteStart() const noexcept { return byteStartField; }
uint64_t RevisionLocalImplementationSite::byteEnd() const noexcept { return byteEndField; }

RevisionLocalImplementationSites::RevisionLocalImplementationSites(
    zc::Vector<RevisionLocalImplementationSite>&& sites) noexcept
    : siteFields(zc::mv(sites)) {}

zc::Maybe<RevisionLocalImplementationSites> RevisionLocalImplementationSites::fromVerified(
    const identity::ModuleKey& module, const identity::SourceFileKey& source,
    const NamedImplementationInventory& inventory,
    zc::Vector<RevisionLocalImplementationSite>&& sites) {
  if (sites.size() > kMaximumSites) { return zc::none; }
  sortSites(sites, [](const RevisionLocalImplementationSite& site) {
    return site.occurrence().site().encode();
  });
  if (!validImplementationSites(module, source, inventory, sites.asPtr())) { return zc::none; }
  RevisionLocalImplementationSites result(zc::mv(sites));
  if (result.encodeCanonical().size() > kMaximumEncodedBytes) { return zc::none; }
  return zc::mv(result);
}

zc::Maybe<RevisionLocalImplementationSites> RevisionLocalImplementationSites::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumEncodedBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto domain = decoder.decodeByteString(kImplementationSitesDomain.size());
  auto count = decoder.decodeSequenceSize(kMaximumSites);
  if (domain == zc::none || count == zc::none ||
      ZC_ASSERT_NONNULL(domain).asPtr() != kImplementationSitesDomain.asBytes()) {
    return zc::none;
  }
  zc::Vector<RevisionLocalImplementationSite> sites(
      static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto node = decoder.decodeUint32();
    auto occurrenceBytes = decoder.decodeByteString(kMaximumOccurrenceBytes);
    auto byteStart = decoder.decodeUint64();
    auto byteEnd = decoder.decodeUint64();
    if (node == zc::none || occurrenceBytes == zc::none || byteStart == zc::none ||
        byteEnd == zc::none) {
      return zc::none;
    }
    auto occurrence =
        ImplSourceOccurrenceKey::decodeCanonical(ZC_ASSERT_NONNULL(occurrenceBytes).asPtr());
    if (occurrence == zc::none) { return zc::none; }
    auto entry = RevisionLocalImplementationSite::from(
        ast::NodeId(ZC_ASSERT_NONNULL(node)), zc::mv(ZC_ASSERT_NONNULL(occurrence)),
        ZC_ASSERT_NONNULL(byteStart), ZC_ASSERT_NONNULL(byteEnd));
    if (entry == zc::none) { return zc::none; }
    ZC_IF_SOME(value, entry) {
      if (sites.size() != 0) {
        const auto previous = sites.back().occurrence().site().encode();
        const auto current = value.occurrence().site().encode();
        if (compareBytes(previous.asPtr(), current.asPtr()) >= 0) { return zc::none; }
        for (const auto& prior : sites) {
          if (prior.node() == value.node()) { return zc::none; }
        }
      }
      sites.add(zc::mv(value));
    }
  }
  if (!decoder.finished()) { return zc::none; }
  return RevisionLocalImplementationSites(zc::mv(sites));
}

RevisionLocalImplementationSites RevisionLocalImplementationSites::clone() const {
  zc::Vector<RevisionLocalImplementationSite> sites(siteFields.size());
  for (const auto& site : siteFields) { sites.add(site.clone()); }
  return RevisionLocalImplementationSites(zc::mv(sites));
}
zc::ArrayPtr<const RevisionLocalImplementationSite>
RevisionLocalImplementationSites::entries() const {
  return siteFields.asPtr();
}
zc::Array<uint8_t> RevisionLocalImplementationSites::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(kImplementationSitesDomain.asBytes());
  encoder.encodeSequenceSize(siteFields.size());
  for (const auto& site : siteFields) {
    encoder.encodeUint32(site.node().value);
    encoder.encodeByteString(site.occurrence().encode().asPtr());
    encoder.encodeUint64(site.byteStart());
    encoder.encodeUint64(site.byteEnd());
  }
  return encoder.finish();
}
bool RevisionLocalImplementationSites::sameAs(
    const RevisionLocalImplementationSites& other) const {
  return encodeCanonical().asPtr() == other.encodeCanonical().asPtr();
}

}  // namespace zomlang::compiler::binder
