#include "zomlang/compiler/driver/named-identity-inventory-query.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/ast/generated/node-schema.h"
#include "zomlang/compiler/binder/definition-inventory.h"
#include "zomlang/compiler/binder/parsed-module.h"
#include "zomlang/compiler/binder/stable-identity-candidate-producer.h"
#include "zomlang/compiler/binder/stable-identity-candidate-verifier.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/parser/parse-source-query.h"

namespace zomlang::compiler::driver::incremental_binding_query {
namespace {

constexpr zc::StringPtr kRejectedDomain = "zom.named-identity-inventory-rejected"_zc;
constexpr uint64_t kMaximumParseFailureBytes = 64 * 1024 * 1024;

enum class RejectedKind : uint8_t {
  ParseRejected = 0x01,
  ConstantExpressionNotAllowed = 0x02,
  DuplicateGenericParameter = 0x03
};

struct LoadedIdentitySource final {
  identity::ModuleKey module;
  binder::CanonicalParsedModule parsed;
  ast::NodeId moduleNode;
};

query::QueryKindContract inventoryContract(zc::StringPtr domain) {
  auto contract = query::QueryKindContract::derived(domain, query::ReuseClass::Semantic,
                                                    query::RetentionClass::Retained);
  return zc::mv(ZC_REQUIRE_NONNULL(contract));
}

query::QueryKindContract siteContract(zc::StringPtr domain) {
  auto contract = query::QueryKindContract::derived(domain, query::ReuseClass::RevisionLocal,
                                                    query::RetentionClass::Evictable);
  return zc::mv(ZC_REQUIRE_NONNULL(contract));
}

zc::Maybe<identity::ModuleKey> decodeModule(const StableModuleQueryKey& key) {
  identity::CanonicalDecoder decoder(key.canonicalModuleBytes());
  auto module = identity::ModuleKey::decodeCanonical(decoder);
  if (module == zc::none || !decoder.finished()) { return zc::none; }
  return zc::mv(ZC_ASSERT_NONNULL(module));
}

zc::Maybe<ast::NodeId> selectModuleNode(const ast::Tree& tree, const identity::ModuleKey& module) {
  if (!tree.contains(tree.root()) || tree.node(tree.root()).kind != ast::SyntaxKind::SourceFile ||
      module.path().size() == 0) {
    return zc::none;
  }
  const auto& source = tree.node(tree.root());
  const ast::NodeId declaration(source.payload.words[ast::kSourceFileModuleWord]);
  if (!declaration) { return ast::NodeId(); }
  if (!tree.contains(declaration) ||
      tree.node(declaration).kind != ast::SyntaxKind::ModuleDeclaration) {
    return zc::none;
  }
  const auto& syntax = tree.node(declaration);
  const auto form = static_cast<ast::ModuleDeclarationForm>(
      syntax.payload.words[ast::kModuleDeclarationFormWord]);
  if (form != ast::ModuleDeclarationForm::RootDeclaration &&
      form != ast::ModuleDeclarationForm::InlineRoot) {
    return zc::none;
  }
  auto name = identity::ModulePathSegment::fromSource(
      tree.ident(ast::IdentId(syntax.payload.words[ast::kModuleDeclarationDeclaredNameWord])));
  if (name == zc::none || ZC_ASSERT_NONNULL(name) != module.path().back()) { return zc::none; }
  return declaration;
}

zc::Array<uint8_t> encodeRejected(RejectedKind kind, zc::ArrayPtr<const uint8_t> payload = {}) {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(kRejectedDomain.asBytes());
  encoder.encodeUint8(static_cast<uint8_t>(kind));
  encoder.encodeByteString(payload);
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> decodeRejected(zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto domain = decoder.decodeByteString(kRejectedDomain.size());
  auto rawKind = decoder.decodeUint8();
  auto payload = decoder.decodeByteString(kMaximumParseFailureBytes);
  if (domain == zc::none || rawKind == zc::none || payload == zc::none || !decoder.finished() ||
      ZC_ASSERT_NONNULL(domain).asPtr() != kRejectedDomain.asBytes()) {
    return zc::none;
  }
  const auto kind = static_cast<RejectedKind>(ZC_ASSERT_NONNULL(rawKind));
  if (kind == RejectedKind::ParseRejected) {
    if (parser::ParseRejected::decodeCanonical(ZC_ASSERT_NONNULL(payload).asPtr()) == zc::none) {
      return zc::none;
    }
  } else if ((kind != RejectedKind::ConstantExpressionNotAllowed &&
              kind != RejectedKind::DuplicateGenericParameter) ||
             ZC_ASSERT_NONNULL(payload).size() != 0) {
    return zc::none;
  }
  return zc::heapArray<uint8_t>(bytes);
}

zc::Array<uint8_t> encodeRejected(const binder::StableIdentityCandidateSourceFailure& failure) {
  switch (failure.kind) {
    case binder::StableIdentityCandidateSourceFailureKind::ConstantExpressionNotAllowed:
      return encodeRejected(RejectedKind::ConstantExpressionNotAllowed);
    case binder::StableIdentityCandidateSourceFailureKind::DuplicateGenericParameter:
      return encodeRejected(RejectedKind::DuplicateGenericParameter);
  }
  ZC_UNREACHABLE;
}

query::TypedQueryResult<LoadedIdentitySource> loadIdentitySource(query::QueryContext& context,
                                                                 const StableModuleQueryKey& key) {
  auto module = decodeModule(key);
  if (module == zc::none) {
    return query::TypedQueryResult<LoadedIdentitySource>::runtimeFailure(
        query::QueryRuntimeFailure::InvalidKeyEncoding);
  }
  auto selected = context.get<SelectedModuleSourceInput>(key);
  if (selected.isRuntimeFailure()) {
    return query::TypedQueryResult<LoadedIdentitySource>::runtimeFailure(selected.runtimeFailure());
  }
  if (selected.kind() == query::QueryValueKind::Absence) {
    return query::TypedQueryResult<LoadedIdentitySource>::absence();
  }
  if (selected.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<LoadedIdentitySource>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  auto sourceKey = identity::source_query::StableSourceQueryKey::decodeBounded(
      selected.value().canonicalSourceBytes());
  if (sourceKey == zc::none) {
    return query::TypedQueryResult<LoadedIdentitySource>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto parsed = context.get<parser::ParseSourceQuery>(ZC_ASSERT_NONNULL(sourceKey));
  if (parsed.isRuntimeFailure()) {
    return query::TypedQueryResult<LoadedIdentitySource>::runtimeFailure(parsed.runtimeFailure());
  }
  if (parsed.kind() == query::QueryValueKind::SemanticFailure) {
    return query::TypedQueryResult<LoadedIdentitySource>::semanticFailure(
        encodeRejected(RejectedKind::ParseRejected, parsed.semanticFailureBytes()));
  }
  if (parsed.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<LoadedIdentitySource>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  auto canonical = binder::CanonicalParsedModule::fromQueryResult(parsed.value().clone());
  if (canonical == zc::none) {
    return query::TypedQueryResult<LoadedIdentitySource>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto moduleNode =
      selectModuleNode(ZC_ASSERT_NONNULL(canonical).tree(), ZC_ASSERT_NONNULL(module));
  if (moduleNode == zc::none) {
    return query::TypedQueryResult<LoadedIdentitySource>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  return query::TypedQueryResult<LoadedIdentitySource>::value(
      LoadedIdentitySource{zc::mv(ZC_ASSERT_NONNULL(module)), zc::mv(ZC_ASSERT_NONNULL(canonical)),
                           ZC_ASSERT_NONNULL(moduleNode)});
}

zc::Maybe<binder::NamedDefinitionInventory> producedDefinitionInventory(
    const identity::ModuleKey& module, const binder::StableIdentityCandidateInventory& production) {
  zc::Vector<identity::DefinitionIdentityAuthority> authorities;
  for (const auto& candidate : production.candidates()) {
    if (candidate.kind() != binder::PreAdmissionIdentityKind::Definition) { continue; }
    auto record = candidate.definitionRecord();
    if (record == zc::none) { return zc::none; }
    zc::Maybe<identity::OverloadHeaderAuthority> overload;
    ZC_IF_SOME(value, candidate.overloadHeader()) { overload = value.clone(); }
    ZC_IF_SOME(value, record) {
      auto authority = identity::DefinitionIdentityAuthority::from(value.clone(), zc::mv(overload));
      if (authority == zc::none) { return zc::none; }
      authorities.add(zc::mv(ZC_ASSERT_NONNULL(authority)));
    }
  }
  return binder::NamedDefinitionInventory::fromVerified(module, authorities.asPtr());
}

zc::Maybe<binder::NamedImplementationInventory> producedImplementationInventory(
    const identity::ModuleKey& module, const binder::StableIdentityCandidateInventory& production) {
  zc::Vector<identity::ImplIdentityAuthority> authorities;
  for (const auto& candidate : production.candidates()) {
    if (candidate.kind() != binder::PreAdmissionIdentityKind::Implementation) { continue; }
    auto record = candidate.implRecord();
    if (record == zc::none) { return zc::none; }
    ZC_IF_SOME(value, record) {
      authorities.add(identity::ImplIdentityAuthority::from(value.clone()));
    }
  }
  return binder::NamedImplementationInventory::fromVerified(module, authorities.asPtr());
}

zc::Maybe<binder::NamedDefinitionInventory> reconstructedDefinitionInventory(
    const identity::ModuleKey& module,
    const binder::VerifiedStableIdentityCandidateInventory& reconstruction) {
  zc::Vector<identity::DefinitionIdentityAuthority> authorities(reconstruction.definitions.size());
  for (const auto& candidate : reconstruction.definitions) {
    authorities.add(candidate.authority.clone());
  }
  return binder::NamedDefinitionInventory::fromVerified(module, authorities.asPtr());
}

zc::Maybe<binder::NamedImplementationInventory> reconstructedImplementationInventory(
    const identity::ModuleKey& module,
    const binder::VerifiedStableIdentityCandidateInventory& reconstruction) {
  zc::Vector<identity::ImplIdentityAuthority> authorities(reconstruction.implementations.size());
  for (const auto& candidate : reconstruction.implementations) {
    authorities.add(candidate.authority.clone());
  }
  return binder::NamedImplementationInventory::fromVerified(module, authorities.asPtr());
}

zc::Maybe<binder::RevisionLocalDefinitionSites> producedDefinitionSites(
    const identity::ModuleKey& module, const identity::SourceFileKey& source,
    const binder::NamedDefinitionInventory& inventory,
    const binder::StableIdentityCandidateInventory& production) {
  zc::Vector<binder::RevisionLocalDefinitionSite> sites(production.definitionSites().size());
  for (const auto& definition : production.definitionSites()) {
    auto site = binder::RevisionLocalDefinitionSite::from(
        definition.node, definition.key.clone(), definition.site.clone(),
        definition.source.byteStart(), definition.source.byteEnd());
    if (site == zc::none) { return zc::none; }
    sites.add(zc::mv(ZC_ASSERT_NONNULL(site)));
  }
  return binder::RevisionLocalDefinitionSites::fromVerified(module, source, inventory,
                                                            zc::mv(sites));
}

zc::Maybe<binder::RevisionLocalImplementationSites> producedImplementationSites(
    const identity::ModuleKey& module, const identity::SourceFileKey& source,
    const binder::NamedImplementationInventory& inventory,
    const binder::StableIdentityCandidateInventory& production) {
  zc::Vector<binder::RevisionLocalImplementationSite> sites(
      production.implementationSites().size());
  for (const auto& implementation : production.implementationSites()) {
    auto site = binder::RevisionLocalImplementationSite::from(
        implementation.node,
        binder::ImplSourceOccurrenceKey::from(implementation.key.clone(),
                                              implementation.site.clone()),
        implementation.source.byteStart(), implementation.source.byteEnd());
    if (site == zc::none) { return zc::none; }
    sites.add(zc::mv(ZC_ASSERT_NONNULL(site)));
  }
  return binder::RevisionLocalImplementationSites::fromVerified(module, source, inventory,
                                                                zc::mv(sites));
}

zc::Maybe<binder::RevisionLocalDefinitionSites> reconstructedDefinitionSites(
    const identity::ModuleKey& module, const identity::SourceFileKey& source,
    const binder::NamedDefinitionInventory& inventory,
    const binder::VerifiedStableIdentityCandidateInventory& reconstruction) {
  zc::Vector<binder::RevisionLocalDefinitionSite> sites(reconstruction.definitions.size());
  for (const auto& definition : reconstruction.definitions) {
    auto site = binder::RevisionLocalDefinitionSite::from(
        definition.node, definition.authority.key().clone(), definition.site.clone(),
        definition.source.byteStart(), definition.source.byteEnd());
    if (site == zc::none) { return zc::none; }
    sites.add(zc::mv(ZC_ASSERT_NONNULL(site)));
  }
  return binder::RevisionLocalDefinitionSites::fromVerified(module, source, inventory,
                                                            zc::mv(sites));
}

zc::Maybe<binder::RevisionLocalImplementationSites> reconstructedImplementationSites(
    const identity::ModuleKey& module, const identity::SourceFileKey& source,
    const binder::NamedImplementationInventory& inventory,
    const binder::VerifiedStableIdentityCandidateInventory& reconstruction) {
  zc::Vector<binder::RevisionLocalImplementationSite> sites(reconstruction.implementations.size());
  for (const auto& implementation : reconstruction.implementations) {
    auto site = binder::RevisionLocalImplementationSite::from(
        implementation.node,
        binder::ImplSourceOccurrenceKey::from(implementation.authority.key().clone(),
                                              implementation.site.clone()),
        implementation.source.byteStart(), implementation.source.byteEnd());
    if (site == zc::none) { return zc::none; }
    sites.add(zc::mv(ZC_ASSERT_NONNULL(site)));
  }
  return binder::RevisionLocalImplementationSites::fromVerified(module, source, inventory,
                                                                zc::mv(sites));
}

template <typename Value>
query::TypedQueryResult<Value> propagateLoadFailure(
    const query::TypedQueryResult<LoadedIdentitySource>& loaded) {
  if (loaded.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(loaded.runtimeFailure());
  }
  if (loaded.kind() == query::QueryValueKind::Absence) {
    return query::TypedQueryResult<Value>::absence();
  }
  if (loaded.kind() == query::QueryValueKind::SemanticFailure) {
    return query::TypedQueryResult<Value>::semanticFailure(
        zc::heapArray<uint8_t>(loaded.semanticFailureBytes()));
  }
  return query::TypedQueryResult<Value>::runtimeFailure(
      query::QueryRuntimeFailure::InvariantViolation);
}

bool verifyLoadFailure(const query::TypedQueryResult<LoadedIdentitySource>& loaded,
                       query::QueryValueKind actualKind,
                       zc::ArrayPtr<const uint8_t> actualFailure) {
  if (loaded.isRuntimeFailure()) { return false; }
  if (loaded.kind() == query::QueryValueKind::Absence) {
    return actualKind == query::QueryValueKind::Absence;
  }
  if (loaded.kind() == query::QueryValueKind::SemanticFailure) {
    return actualKind == query::QueryValueKind::SemanticFailure &&
           decodeRejected(actualFailure) != zc::none &&
           loaded.semanticFailureBytes() == actualFailure;
  }
  return false;
}

}  // namespace

zc::StringPtr NamedDefinitionInventoryQuery::domain() {
  return "zom.query.named-definition-inventory"_zc;
}

query::QueryKindContract NamedDefinitionInventoryQuery::contract() {
  return inventoryContract(domain());
}

zc::Array<uint8_t> NamedDefinitionInventoryQuery::encodeKey(const Key& key) {
  return zc::heapArray<uint8_t>(key.canonicalModuleBytes());
}

zc::Maybe<NamedDefinitionInventoryQuery::Key> NamedDefinitionInventoryQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return StableModuleQueryKey::fromVerified(
      ZC_ASSERT_NONNULL([&]() -> zc::Maybe<identity::ModuleKey> {
        identity::CanonicalDecoder decoder(bytes);
        auto module = identity::ModuleKey::decodeCanonical(decoder);
        if (module == zc::none || !decoder.finished()) { return zc::none; }
        return zc::mv(ZC_ASSERT_NONNULL(module));
      }()));
}

zc::Array<uint8_t> NamedDefinitionInventoryQuery::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<NamedDefinitionInventoryQuery::Value> NamedDefinitionInventoryQuery::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return binder::NamedDefinitionInventory::decodeCanonical(bytes);
}

query::TypedQueryResult<NamedDefinitionInventoryQuery::Value>
NamedDefinitionInventoryQuery::provide(query::QueryContext& context, const Key& key) {
  auto loaded = loadIdentitySource(context, key);
  if (loaded.kind() != query::QueryValueKind::Value || loaded.isRuntimeFailure()) {
    return propagateLoadFailure<Value>(loaded);
  }
  const auto& source = loaded.value();
  auto production = binder::StableIdentityCandidateProducer::produce(source.parsed, source.module,
                                                                     source.moduleNode);
  auto verification = binder::StableIdentityCandidateVerifier::verify(
      source.parsed, source.module, source.moduleNode, production);
  if (verification.is<binder::StableIdentityCandidateSourceFailure>()) {
    return query::TypedQueryResult<Value>::semanticFailure(
        encodeRejected(verification.get<binder::StableIdentityCandidateSourceFailure>()));
  }
  if (!verification.is<binder::VerifiedStableIdentityCandidateInventory>() ||
      !production.is<binder::StableIdentityCandidateInventory>()) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto inventory = producedDefinitionInventory(
      source.module, production.get<binder::StableIdentityCandidateInventory>());
  if (inventory == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(inventory)));
}

bool NamedDefinitionInventoryQuery::verify(query::QueryContext& context, const Key& key,
                                           const query::TypedQueryResult<Value>& result) {
  auto loaded = loadIdentitySource(context, key);
  if (loaded.kind() != query::QueryValueKind::Value || loaded.isRuntimeFailure()) {
    return verifyLoadFailure(loaded, result.kind(), result.semanticFailureBytes());
  }
  const auto& source = loaded.value();
  auto reconstruction = binder::StableIdentityCandidateVerifier::reconstruct(
      source.parsed, source.module, source.moduleNode);
  if (reconstruction.is<binder::StableIdentityCandidateSourceFailure>()) {
    return result.kind() == query::QueryValueKind::SemanticFailure &&
           result.semanticFailureBytes() ==
               encodeRejected(reconstruction.get<binder::StableIdentityCandidateSourceFailure>())
                   .asPtr();
  }
  if (result.kind() != query::QueryValueKind::Value ||
      !reconstruction.is<binder::VerifiedStableIdentityCandidateInventory>()) {
    return false;
  }
  auto expected = reconstructedDefinitionInventory(
      source.module, reconstruction.get<binder::VerifiedStableIdentityCandidateInventory>());
  return expected != zc::none && ZC_ASSERT_NONNULL(expected).sameAs(result.value());
}

zc::StringPtr NamedImplementationInventoryQuery::domain() {
  return "zom.query.named-implementation-inventory"_zc;
}

query::QueryKindContract NamedImplementationInventoryQuery::contract() {
  return inventoryContract(domain());
}

zc::Array<uint8_t> NamedImplementationInventoryQuery::encodeKey(const Key& key) {
  return NamedDefinitionInventoryQuery::encodeKey(key);
}

zc::Maybe<NamedImplementationInventoryQuery::Key> NamedImplementationInventoryQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return NamedDefinitionInventoryQuery::decodeKey(bytes);
}

zc::Array<uint8_t> NamedImplementationInventoryQuery::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<NamedImplementationInventoryQuery::Value> NamedImplementationInventoryQuery::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return binder::NamedImplementationInventory::decodeCanonical(bytes);
}

query::TypedQueryResult<NamedImplementationInventoryQuery::Value>
NamedImplementationInventoryQuery::provide(query::QueryContext& context, const Key& key) {
  auto loaded = loadIdentitySource(context, key);
  if (loaded.kind() != query::QueryValueKind::Value || loaded.isRuntimeFailure()) {
    return propagateLoadFailure<Value>(loaded);
  }
  const auto& source = loaded.value();
  auto production = binder::StableIdentityCandidateProducer::produce(source.parsed, source.module,
                                                                     source.moduleNode);
  auto verification = binder::StableIdentityCandidateVerifier::verify(
      source.parsed, source.module, source.moduleNode, production);
  if (verification.is<binder::StableIdentityCandidateSourceFailure>()) {
    return query::TypedQueryResult<Value>::semanticFailure(
        encodeRejected(verification.get<binder::StableIdentityCandidateSourceFailure>()));
  }
  if (!verification.is<binder::VerifiedStableIdentityCandidateInventory>() ||
      !production.is<binder::StableIdentityCandidateInventory>()) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto inventory = producedImplementationInventory(
      source.module, production.get<binder::StableIdentityCandidateInventory>());
  if (inventory == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(inventory)));
}

bool NamedImplementationInventoryQuery::verify(query::QueryContext& context, const Key& key,
                                               const query::TypedQueryResult<Value>& result) {
  auto loaded = loadIdentitySource(context, key);
  if (loaded.kind() != query::QueryValueKind::Value || loaded.isRuntimeFailure()) {
    return verifyLoadFailure(loaded, result.kind(), result.semanticFailureBytes());
  }
  const auto& source = loaded.value();
  auto reconstruction = binder::StableIdentityCandidateVerifier::reconstruct(
      source.parsed, source.module, source.moduleNode);
  if (reconstruction.is<binder::StableIdentityCandidateSourceFailure>()) {
    return result.kind() == query::QueryValueKind::SemanticFailure &&
           result.semanticFailureBytes() ==
               encodeRejected(reconstruction.get<binder::StableIdentityCandidateSourceFailure>())
                   .asPtr();
  }
  if (result.kind() != query::QueryValueKind::Value ||
      !reconstruction.is<binder::VerifiedStableIdentityCandidateInventory>()) {
    return false;
  }
  auto expected = reconstructedImplementationInventory(
      source.module, reconstruction.get<binder::VerifiedStableIdentityCandidateInventory>());
  return expected != zc::none && ZC_ASSERT_NONNULL(expected).sameAs(result.value());
}

zc::StringPtr RevisionLocalDefinitionSitesQuery::domain() {
  return "zom.query.revision-local-definition-sites"_zc;
}

query::QueryKindContract RevisionLocalDefinitionSitesQuery::contract() {
  return siteContract(domain());
}

zc::Array<uint8_t> RevisionLocalDefinitionSitesQuery::encodeKey(const Key& key) {
  return NamedDefinitionInventoryQuery::encodeKey(key);
}

zc::Maybe<RevisionLocalDefinitionSitesQuery::Key> RevisionLocalDefinitionSitesQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return NamedDefinitionInventoryQuery::decodeKey(bytes);
}

zc::Array<uint8_t> RevisionLocalDefinitionSitesQuery::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<RevisionLocalDefinitionSitesQuery::Value> RevisionLocalDefinitionSitesQuery::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return binder::RevisionLocalDefinitionSites::decodeCanonical(bytes);
}

query::TypedQueryResult<RevisionLocalDefinitionSitesQuery::Value>
RevisionLocalDefinitionSitesQuery::provide(query::QueryContext& context, const Key& key) {
  auto loaded = loadIdentitySource(context, key);
  if (loaded.kind() != query::QueryValueKind::Value || loaded.isRuntimeFailure()) {
    return propagateLoadFailure<Value>(loaded);
  }
  auto named = context.get<NamedDefinitionInventoryQuery>(key);
  if (named.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(named.runtimeFailure());
  }
  if (named.kind() == query::QueryValueKind::SemanticFailure) {
    return query::TypedQueryResult<Value>::semanticFailure(
        zc::heapArray<uint8_t>(named.semanticFailureBytes()));
  }
  if (named.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  const auto& source = loaded.value();
  auto production = binder::StableIdentityCandidateProducer::produce(source.parsed, source.module,
                                                                     source.moduleNode);
  auto verification = binder::StableIdentityCandidateVerifier::verify(
      source.parsed, source.module, source.moduleNode, production);
  if (verification.is<binder::StableIdentityCandidateSourceFailure>()) {
    return query::TypedQueryResult<Value>::semanticFailure(
        encodeRejected(verification.get<binder::StableIdentityCandidateSourceFailure>()));
  }
  if (!verification.is<binder::VerifiedStableIdentityCandidateInventory>() ||
      !production.is<binder::StableIdentityCandidateInventory>()) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto sites = producedDefinitionSites(source.module, source.parsed.source(), named.value(),
                                       production.get<binder::StableIdentityCandidateInventory>());
  if (sites == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(sites)));
}

bool RevisionLocalDefinitionSitesQuery::verify(query::QueryContext& context, const Key& key,
                                               const query::TypedQueryResult<Value>& result) {
  auto loaded = loadIdentitySource(context, key);
  if (loaded.kind() != query::QueryValueKind::Value || loaded.isRuntimeFailure()) {
    return verifyLoadFailure(loaded, result.kind(), result.semanticFailureBytes());
  }
  auto named = context.get<NamedDefinitionInventoryQuery>(key);
  if (named.isRuntimeFailure()) { return false; }
  if (named.kind() == query::QueryValueKind::SemanticFailure) {
    return result.kind() == query::QueryValueKind::SemanticFailure &&
           named.semanticFailureBytes() == result.semanticFailureBytes();
  }
  if (named.kind() != query::QueryValueKind::Value) { return false; }
  const auto& source = loaded.value();
  auto reconstruction = binder::StableIdentityCandidateVerifier::reconstruct(
      source.parsed, source.module, source.moduleNode);
  if (reconstruction.is<binder::StableIdentityCandidateSourceFailure>()) {
    return result.kind() == query::QueryValueKind::SemanticFailure &&
           result.semanticFailureBytes() ==
               encodeRejected(reconstruction.get<binder::StableIdentityCandidateSourceFailure>())
                   .asPtr();
  }
  if (result.kind() != query::QueryValueKind::Value ||
      !reconstruction.is<binder::VerifiedStableIdentityCandidateInventory>()) {
    return false;
  }
  auto expected = reconstructedDefinitionSites(
      source.module, source.parsed.source(), named.value(),
      reconstruction.get<binder::VerifiedStableIdentityCandidateInventory>());
  return expected != zc::none && ZC_ASSERT_NONNULL(expected).sameAs(result.value());
}

zc::StringPtr RevisionLocalImplementationSitesQuery::domain() {
  return "zom.query.revision-local-implementation-sites"_zc;
}

query::QueryKindContract RevisionLocalImplementationSitesQuery::contract() {
  return siteContract(domain());
}

zc::Array<uint8_t> RevisionLocalImplementationSitesQuery::encodeKey(const Key& key) {
  return NamedDefinitionInventoryQuery::encodeKey(key);
}

zc::Maybe<RevisionLocalImplementationSitesQuery::Key>
RevisionLocalImplementationSitesQuery::decodeKey(zc::ArrayPtr<const uint8_t> bytes) {
  return NamedDefinitionInventoryQuery::decodeKey(bytes);
}

zc::Array<uint8_t> RevisionLocalImplementationSitesQuery::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<RevisionLocalImplementationSitesQuery::Value>
RevisionLocalImplementationSitesQuery::decodeValue(zc::ArrayPtr<const uint8_t> bytes) {
  return binder::RevisionLocalImplementationSites::decodeCanonical(bytes);
}

query::TypedQueryResult<RevisionLocalImplementationSitesQuery::Value>
RevisionLocalImplementationSitesQuery::provide(query::QueryContext& context, const Key& key) {
  auto loaded = loadIdentitySource(context, key);
  if (loaded.kind() != query::QueryValueKind::Value || loaded.isRuntimeFailure()) {
    return propagateLoadFailure<Value>(loaded);
  }
  auto named = context.get<NamedImplementationInventoryQuery>(key);
  if (named.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(named.runtimeFailure());
  }
  if (named.kind() == query::QueryValueKind::SemanticFailure) {
    return query::TypedQueryResult<Value>::semanticFailure(
        zc::heapArray<uint8_t>(named.semanticFailureBytes()));
  }
  if (named.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  const auto& source = loaded.value();
  auto production = binder::StableIdentityCandidateProducer::produce(source.parsed, source.module,
                                                                     source.moduleNode);
  auto verification = binder::StableIdentityCandidateVerifier::verify(
      source.parsed, source.module, source.moduleNode, production);
  if (verification.is<binder::StableIdentityCandidateSourceFailure>()) {
    return query::TypedQueryResult<Value>::semanticFailure(
        encodeRejected(verification.get<binder::StableIdentityCandidateSourceFailure>()));
  }
  if (!verification.is<binder::VerifiedStableIdentityCandidateInventory>() ||
      !production.is<binder::StableIdentityCandidateInventory>()) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto sites =
      producedImplementationSites(source.module, source.parsed.source(), named.value(),
                                  production.get<binder::StableIdentityCandidateInventory>());
  if (sites == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(sites)));
}

bool RevisionLocalImplementationSitesQuery::verify(query::QueryContext& context, const Key& key,
                                                   const query::TypedQueryResult<Value>& result) {
  auto loaded = loadIdentitySource(context, key);
  if (loaded.kind() != query::QueryValueKind::Value || loaded.isRuntimeFailure()) {
    return verifyLoadFailure(loaded, result.kind(), result.semanticFailureBytes());
  }
  auto named = context.get<NamedImplementationInventoryQuery>(key);
  if (named.isRuntimeFailure()) { return false; }
  if (named.kind() == query::QueryValueKind::SemanticFailure) {
    return result.kind() == query::QueryValueKind::SemanticFailure &&
           named.semanticFailureBytes() == result.semanticFailureBytes();
  }
  if (named.kind() != query::QueryValueKind::Value) { return false; }
  const auto& source = loaded.value();
  auto reconstruction = binder::StableIdentityCandidateVerifier::reconstruct(
      source.parsed, source.module, source.moduleNode);
  if (reconstruction.is<binder::StableIdentityCandidateSourceFailure>()) {
    return result.kind() == query::QueryValueKind::SemanticFailure &&
           result.semanticFailureBytes() ==
               encodeRejected(reconstruction.get<binder::StableIdentityCandidateSourceFailure>())
                   .asPtr();
  }
  if (result.kind() != query::QueryValueKind::Value ||
      !reconstruction.is<binder::VerifiedStableIdentityCandidateInventory>()) {
    return false;
  }
  auto expected = reconstructedImplementationSites(
      source.module, source.parsed.source(), named.value(),
      reconstruction.get<binder::VerifiedStableIdentityCandidateInventory>());
  return expected != zc::none && ZC_ASSERT_NONNULL(expected).sameAs(result.value());
}

zc::Vector<binder::ModuleBodyDefinitionBoundaryInput> definitionBoundaries(
    const binder::CanonicalParsedModule& parsed, ast::NodeId moduleNode,
    const binder::RevisionLocalDefinitionSites& sites) {
  const auto inventory = binder::DefinitionInventory::collect(parsed.tree());
  const ast::NodeId inventoryModuleNode =
      moduleNode == parsed.tree().root() ? ast::NodeId() : moduleNode;
  zc::Vector<binder::ModuleBodyDefinitionBoundaryInput> result(sites.entries().size());
  for (const auto& site : sites.entries()) {
    bool boundary = false;
    for (const auto& definition : inventory.definitions()) {
      if (definition.node == site.node() && definition.moduleNode == inventoryModuleNode &&
          definition.site.value().is<binder::DeclarationDefinitionSite>()) {
        boundary = true;
        break;
      }
    }
    if (!boundary) { continue; }
    result.add(binder::ModuleBodyDefinitionBoundaryInput{site.node(), site.definition().clone()});
  }
  return result;
}

zc::Vector<binder::ModuleBodyImplementationBoundaryInput> implementationBoundaries(
    const binder::RevisionLocalImplementationSites& sites) {
  zc::Vector<binder::ModuleBodyImplementationBoundaryInput> result(sites.entries().size());
  for (const auto& site : sites.entries()) {
    result.add(
        binder::ModuleBodyImplementationBoundaryInput{site.node(), site.occurrence().clone()});
  }
  return result;
}

zc::StringPtr ModuleBodySyntaxQuery::domain() { return "zom.query.module-body-syntax"_zc; }

query::QueryKindContract ModuleBodySyntaxQuery::contract() {
  auto contract = query::QueryKindContract::derived(domain(), query::ReuseClass::Semantic,
                                                    query::RetentionClass::Evictable);
  return zc::mv(ZC_REQUIRE_NONNULL(contract));
}

zc::Array<uint8_t> ModuleBodySyntaxQuery::encodeKey(const Key& key) {
  return NamedDefinitionInventoryQuery::encodeKey(key);
}

zc::Maybe<ModuleBodySyntaxQuery::Key> ModuleBodySyntaxQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return NamedDefinitionInventoryQuery::decodeKey(bytes);
}

zc::Array<uint8_t> ModuleBodySyntaxQuery::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<ModuleBodySyntaxQuery::Value> ModuleBodySyntaxQuery::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return binder::ModuleBodySyntax::decodeCanonical(bytes);
}

query::TypedQueryResult<ModuleBodySyntaxQuery::Value> ModuleBodySyntaxQuery::provide(
    query::QueryContext& context, const Key& key) {
  auto loaded = loadIdentitySource(context, key);
  if (loaded.kind() != query::QueryValueKind::Value || loaded.isRuntimeFailure()) {
    return propagateLoadFailure<Value>(loaded);
  }
  auto definitions = context.get<NamedDefinitionInventoryQuery>(key);
  auto implementations = context.get<NamedImplementationInventoryQuery>(key);
  auto definitionSites = context.get<RevisionLocalDefinitionSitesQuery>(key);
  auto implementationSites = context.get<RevisionLocalImplementationSitesQuery>(key);
  if (definitions.isRuntimeFailure() || implementations.isRuntimeFailure() ||
      definitionSites.isRuntimeFailure() || implementationSites.isRuntimeFailure()) {
    const auto failure = definitions.isRuntimeFailure()       ? definitions.runtimeFailure()
                         : implementations.isRuntimeFailure() ? implementations.runtimeFailure()
                         : definitionSites.isRuntimeFailure()
                             ? definitionSites.runtimeFailure()
                             : implementationSites.runtimeFailure();
    return query::TypedQueryResult<Value>::runtimeFailure(failure);
  }
  if (definitions.kind() == query::QueryValueKind::SemanticFailure) {
    return query::TypedQueryResult<Value>::semanticFailure(
        zc::heapArray<uint8_t>(definitions.semanticFailureBytes()));
  }
  if (implementations.kind() == query::QueryValueKind::SemanticFailure) {
    return query::TypedQueryResult<Value>::semanticFailure(
        zc::heapArray<uint8_t>(implementations.semanticFailureBytes()));
  }
  if (definitionSites.kind() == query::QueryValueKind::SemanticFailure) {
    return query::TypedQueryResult<Value>::semanticFailure(
        zc::heapArray<uint8_t>(definitionSites.semanticFailureBytes()));
  }
  if (implementationSites.kind() == query::QueryValueKind::SemanticFailure) {
    return query::TypedQueryResult<Value>::semanticFailure(
        zc::heapArray<uint8_t>(implementationSites.semanticFailureBytes()));
  }
  if (definitions.kind() != query::QueryValueKind::Value ||
      implementations.kind() != query::QueryValueKind::Value ||
      definitionSites.kind() != query::QueryValueKind::Value ||
      implementationSites.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  const auto& source = loaded.value();
  auto definitionInputs =
      definitionBoundaries(source.parsed, source.moduleNode, definitionSites.value());
  auto implementationInputs = implementationBoundaries(implementationSites.value());
  auto projection = binder::ModuleBodySyntaxProducer::produce(
      source.parsed, source.module, source.moduleNode, definitionInputs.asPtr(),
      implementationInputs.asPtr());
  if (!projection.is<binder::ModuleBodySyntaxProjection>()) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<Value>::value(
      zc::mv(projection.get<binder::ModuleBodySyntaxProjection>().syntax));
}

bool ModuleBodySyntaxQuery::verify(query::QueryContext& context, const Key& key,
                                   const query::TypedQueryResult<Value>& result) {
  auto loaded = loadIdentitySource(context, key);
  if (loaded.kind() != query::QueryValueKind::Value || loaded.isRuntimeFailure()) {
    return verifyLoadFailure(loaded, result.kind(), result.semanticFailureBytes());
  }
  auto definitions = context.get<NamedDefinitionInventoryQuery>(key);
  auto implementations = context.get<NamedImplementationInventoryQuery>(key);
  auto definitionSites = context.get<RevisionLocalDefinitionSitesQuery>(key);
  auto implementationSites = context.get<RevisionLocalImplementationSitesQuery>(key);
  if (definitions.isRuntimeFailure() || implementations.isRuntimeFailure() ||
      definitionSites.isRuntimeFailure() || implementationSites.isRuntimeFailure()) {
    return false;
  }
  for (const auto failure :
       {definitions.semanticFailureBytes(), implementations.semanticFailureBytes(),
        definitionSites.semanticFailureBytes(), implementationSites.semanticFailureBytes()}) {
    if (failure.size() != 0) {
      return result.kind() == query::QueryValueKind::SemanticFailure &&
             result.semanticFailureBytes() == failure;
    }
  }
  if (result.kind() != query::QueryValueKind::Value ||
      definitions.kind() != query::QueryValueKind::Value ||
      implementations.kind() != query::QueryValueKind::Value ||
      definitionSites.kind() != query::QueryValueKind::Value ||
      implementationSites.kind() != query::QueryValueKind::Value) {
    return false;
  }
  const auto& source = loaded.value();
  auto definitionInputs =
      definitionBoundaries(source.parsed, source.moduleNode, definitionSites.value());
  auto implementationInputs = implementationBoundaries(implementationSites.value());
  auto expected = binder::ModuleBodySyntaxVerifier::reconstruct(
      source.parsed, source.module, source.moduleNode, definitionInputs.asPtr(),
      implementationInputs.asPtr());
  return expected.is<binder::ModuleBodySyntaxProjection>() &&
         expected.get<binder::ModuleBodySyntaxProjection>().syntax == result.value();
}

zc::StringPtr ModuleBodyProvenanceQuery::domain() { return "zom.query.module-body-provenance"_zc; }

query::QueryKindContract ModuleBodyProvenanceQuery::contract() { return siteContract(domain()); }

zc::Array<uint8_t> ModuleBodyProvenanceQuery::encodeKey(const Key& key) {
  return NamedDefinitionInventoryQuery::encodeKey(key);
}

zc::Maybe<ModuleBodyProvenanceQuery::Key> ModuleBodyProvenanceQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return NamedDefinitionInventoryQuery::decodeKey(bytes);
}

zc::Array<uint8_t> ModuleBodyProvenanceQuery::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<ModuleBodyProvenanceQuery::Value> ModuleBodyProvenanceQuery::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return binder::ModuleBodyProvenance::decodeCanonical(bytes);
}

query::TypedQueryResult<ModuleBodyProvenanceQuery::Value> ModuleBodyProvenanceQuery::provide(
    query::QueryContext& context, const Key& key) {
  auto loaded = loadIdentitySource(context, key);
  if (loaded.kind() != query::QueryValueKind::Value || loaded.isRuntimeFailure()) {
    return propagateLoadFailure<Value>(loaded);
  }
  auto syntax = context.get<ModuleBodySyntaxQuery>(key);
  auto definitionSites = context.get<RevisionLocalDefinitionSitesQuery>(key);
  auto implementationSites = context.get<RevisionLocalImplementationSitesQuery>(key);
  if (syntax.isRuntimeFailure() || definitionSites.isRuntimeFailure() ||
      implementationSites.isRuntimeFailure()) {
    const auto failure = syntax.isRuntimeFailure() ? syntax.runtimeFailure()
                         : definitionSites.isRuntimeFailure()
                             ? definitionSites.runtimeFailure()
                             : implementationSites.runtimeFailure();
    return query::TypedQueryResult<Value>::runtimeFailure(failure);
  }
  if (syntax.kind() == query::QueryValueKind::SemanticFailure) {
    return query::TypedQueryResult<Value>::semanticFailure(
        zc::heapArray<uint8_t>(syntax.semanticFailureBytes()));
  }
  if (definitionSites.kind() == query::QueryValueKind::SemanticFailure) {
    return query::TypedQueryResult<Value>::semanticFailure(
        zc::heapArray<uint8_t>(definitionSites.semanticFailureBytes()));
  }
  if (implementationSites.kind() == query::QueryValueKind::SemanticFailure) {
    return query::TypedQueryResult<Value>::semanticFailure(
        zc::heapArray<uint8_t>(implementationSites.semanticFailureBytes()));
  }
  if (syntax.kind() != query::QueryValueKind::Value ||
      definitionSites.kind() != query::QueryValueKind::Value ||
      implementationSites.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  const auto& source = loaded.value();
  auto definitionInputs =
      definitionBoundaries(source.parsed, source.moduleNode, definitionSites.value());
  auto implementationInputs = implementationBoundaries(implementationSites.value());
  auto projection = binder::ModuleBodySyntaxProducer::produce(
      source.parsed, source.module, source.moduleNode, definitionInputs.asPtr(),
      implementationInputs.asPtr());
  if (!projection.is<binder::ModuleBodySyntaxProjection>() ||
      projection.get<binder::ModuleBodySyntaxProjection>().syntax != syntax.value()) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<Value>::value(
      zc::mv(projection.get<binder::ModuleBodySyntaxProjection>().provenance));
}

bool ModuleBodyProvenanceQuery::verify(query::QueryContext& context, const Key& key,
                                       const query::TypedQueryResult<Value>& result) {
  auto loaded = loadIdentitySource(context, key);
  if (loaded.kind() != query::QueryValueKind::Value || loaded.isRuntimeFailure()) {
    return verifyLoadFailure(loaded, result.kind(), result.semanticFailureBytes());
  }
  auto syntax = context.get<ModuleBodySyntaxQuery>(key);
  auto definitionSites = context.get<RevisionLocalDefinitionSitesQuery>(key);
  auto implementationSites = context.get<RevisionLocalImplementationSitesQuery>(key);
  if (syntax.isRuntimeFailure() || definitionSites.isRuntimeFailure() ||
      implementationSites.isRuntimeFailure()) {
    return false;
  }
  for (const auto failure : {syntax.semanticFailureBytes(), definitionSites.semanticFailureBytes(),
                             implementationSites.semanticFailureBytes()}) {
    if (failure.size() != 0) {
      return result.kind() == query::QueryValueKind::SemanticFailure &&
             result.semanticFailureBytes() == failure;
    }
  }
  if (result.kind() != query::QueryValueKind::Value ||
      syntax.kind() != query::QueryValueKind::Value ||
      definitionSites.kind() != query::QueryValueKind::Value ||
      implementationSites.kind() != query::QueryValueKind::Value) {
    return false;
  }
  const auto& source = loaded.value();
  auto definitionInputs =
      definitionBoundaries(source.parsed, source.moduleNode, definitionSites.value());
  auto implementationInputs = implementationBoundaries(implementationSites.value());
  auto expected = binder::ModuleBodySyntaxVerifier::reconstruct(
      source.parsed, source.module, source.moduleNode, definitionInputs.asPtr(),
      implementationInputs.asPtr());
  return expected.is<binder::ModuleBodySyntaxProjection>() &&
         expected.get<binder::ModuleBodySyntaxProjection>().syntax == syntax.value() &&
         expected.get<binder::ModuleBodySyntaxProjection>().provenance == result.value();
}

}  // namespace zomlang::compiler::driver::incremental_binding_query
