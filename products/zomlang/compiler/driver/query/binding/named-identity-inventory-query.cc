#include "zomlang/compiler/driver/query/binding/named-identity-inventory-query.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/ast/generated/node-accessors.h"
#include "zomlang/compiler/ast/generated/node-schema.h"
#include "zomlang/compiler/binder/metadata/definition-inventory.h"
#include "zomlang/compiler/binder/graph/parsed-module.h"
#include "zomlang/compiler/binder/stable/stable-binding-codec.h"
#include "zomlang/compiler/binder/stable/stable-binding-diagnostic-fact.h"
#include "zomlang/compiler/binder/stable/candidate/producer.h"
#include "zomlang/compiler/binder/stable/candidate/verifier.h"
#include "zomlang/compiler/diagnostics/toolchain/module-root-argument.h"
#include "zomlang/compiler/driver/query/module-graph/module-graph-query-input.h"
#include "zomlang/compiler/identity/canonical/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"
#include "zomlang/compiler/parser/query/parse-source-query.h"

namespace zomlang::compiler::driver::incremental_binding_query {
namespace {

constexpr zc::StringPtr kIdentitySyntaxSiteInventoryWitnessDomain =
    "zom.query.identity-syntax-site-inventory-witness"_zc;
constexpr zc::StringPtr kStableIdentityAdmissionWitnessDomain =
    "zom.query.stable-identity-admission-witness"_zc;

struct LoadedIdentitySource final {
  identity::ModuleKey module;
  binder::CanonicalParsedModule parsed;
  ast::NodeId moduleNode;
};

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
      form != ast::ModuleDeclarationForm::InlineRoot && form != ast::ModuleDeclarationForm::Alias) {
    return zc::none;
  }
  auto name = identity::ModulePathSegment::fromSource(
      tree.ident(ast::IdentId(syntax.payload.words[ast::kModuleDeclarationDeclaredNameWord])));
  if (name == zc::none) { return zc::none; }
  ZC_IF_SOME(value, name) {
    if (value != module.path().back()) {
      zc::Vector<identity::ModulePathSegment> declaredPath;
      declaredPath.add(value.clone());
      if (diagnostics::ModuleRootArgument::fromCanonicalPath(zc::mv(declaredPath)) ==
          zc::none) {
        return zc::none;
      }
    }
  }
  return declaration;
}

template <typename Context>
query::TypedQueryResult<LoadedIdentitySource> loadIdentitySource(Context& context,
                                                                 const StableModuleQueryKey& key) {
  auto module = decodeModule(key);
  if (module == zc::none) {
    return query::TypedQueryResult<LoadedIdentitySource>::runtimeFailure(
        query::QueryRuntimeFailure::InvalidKeyEncoding);
  }
  auto selected = context.template get<module_graph_query::SelectedModuleSourceQuery>(
      ZC_ASSERT_NONNULL(module));
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
  auto sourceKey = identity::source_query::StableSourceQueryKey::fromVerified(selected.value());
  if (sourceKey == zc::none) {
    return query::TypedQueryResult<LoadedIdentitySource>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto parsed =
      context.template getCapability<parser::ParseSourceQuery>(ZC_ASSERT_NONNULL(sourceKey));
  if (parsed.isRuntimeRejected()) {
    return query::TypedQueryResult<LoadedIdentitySource>::runtimeFailure(parsed.runtimeFailure());
  }
  if (parsed.isSourceRejected()) {
    using Contract =
        query::CapabilityFailureContract<parser::ParseSourceQuery,
                                         query::SourceRejection<diagnostics::DiagnosticFact>>;
    return query::TypedQueryResult<LoadedIdentitySource>::semanticFailure(
        Contract::encode(parsed.diagnostics()));
  }
  if (!parsed.isPublished()) {
    return query::TypedQueryResult<LoadedIdentitySource>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  auto canonical =
      binder::CanonicalParsedModule::fromQueryResult(parsed.lease().capability().clone());
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

zc::Maybe<binder::VerifiedStableIdentityCandidateInventory> verifiedIdentityCandidates(
    const LoadedIdentitySource& source) {
  auto production =
      binder::CandidateProducer::produce(source.parsed, source.module, source.moduleNode);
  auto verification = binder::CandidateVerifier::verify(source.parsed, source.module,
                                                        source.moduleNode, production);
  if (!verification.is<binder::VerifiedStableIdentityCandidateInventory>()) { return zc::none; }
  return zc::mv(verification.get<binder::VerifiedStableIdentityCandidateInventory>());
}

zc::Maybe<binder::NamedDefinitionInventory> providerDefinitionInventory(
    const LoadedIdentitySource& source, const binder::StableIdentityAdmission& admission) {
  auto verified = verifiedIdentityCandidates(source);
  if (verified == zc::none ||
      ZC_ASSERT_NONNULL(verified).definitions.size() != admission.definitions().size()) {
    return zc::none;
  }
  const auto& tree = source.parsed.tree();
  zc::Vector<binder::NamedDefinitionInventoryInput> inputs(admission.definitions().size());
  for (size_t index = 0; index < admission.definitions().size(); ++index) {
    const auto& definition = admission.definitions()[index];
    if (!tree.contains(definition.node) ||
        !definition.site.key().source().sameAs(admission.source())) {
      return zc::none;
    }
    for (size_t prior = 0; prior < index; ++prior) {
      if (admission.definitions()[prior].node == definition.node ||
          admission.definitions()[prior].site.key().sameAs(definition.site.key())) {
        return zc::none;
      }
    }
    zc::Maybe<const binder::VerifiedStableDefinitionCandidate&> matched;
    for (const auto& candidate : ZC_ASSERT_NONNULL(verified).definitions) {
      if (candidate.node != definition.node) { continue; }
      if (matched != zc::none) { return zc::none; }
      matched = candidate;
    }
    if (matched == zc::none ||
        !ZC_ASSERT_NONNULL(matched).authority.sameRecordAs(definition.authority) ||
        ZC_ASSERT_NONNULL(matched).authority.key() != definition.authority.key() ||
        !ZC_ASSERT_NONNULL(matched).site.sameAs(definition.site.key()) ||
        ZC_ASSERT_NONNULL(matched).source.byteStart() != definition.site.range().byteStart() ||
        ZC_ASSERT_NONNULL(matched).source.byteEnd() != definition.site.range().byteEnd()) {
      return zc::none;
    }

    const auto& syntax = tree.node(definition.node);
    auto disposition = binder::DefinitionBodyDisposition::NoExecutableBody;
    switch (syntax.kind) {
      case ast::SyntaxKind::FunctionDecl: {
        const ast::NodeId body(syntax.payload.words[ast::kFunctionDeclBodyWord]);
        if (!body || !tree.contains(body) || tree.node(body).kind != ast::SyntaxKind::BlockStmt) {
          return zc::none;
        }
        disposition = binder::DefinitionBodyDisposition::ExecutableBody;
        break;
      }
      case ast::SyntaxKind::ConstructorDecl: {
        const ast::NodeId body(syntax.payload.words[ast::kConstructorDeclBodyWord]);
        if (!body || !tree.contains(body) || tree.node(body).kind != ast::SyntaxKind::BlockStmt) {
          return zc::none;
        }
        disposition = binder::DefinitionBodyDisposition::ExecutableBody;
        break;
      }
      case ast::SyntaxKind::DestructorDecl: {
        const ast::NodeId body(syntax.payload.words[ast::kDestructorDeclBodyWord]);
        if (!body || !tree.contains(body) || tree.node(body).kind != ast::SyntaxKind::BlockStmt) {
          return zc::none;
        }
        disposition = binder::DefinitionBodyDisposition::ExecutableBody;
        break;
      }
      case ast::SyntaxKind::MethodDecl: {
        const ast::NodeId body(syntax.payload.words[ast::kMethodDeclBodyWord]);
        if (body && (!tree.contains(body) || tree.node(body).kind != ast::SyntaxKind::BlockStmt)) {
          return zc::none;
        }
        if (body) { disposition = binder::DefinitionBodyDisposition::ExecutableBody; }
        break;
      }
      case ast::SyntaxKind::FieldDecl:
      case ast::SyntaxKind::ClassConstDecl: {
        const uint32_t initializerWord = syntax.kind == ast::SyntaxKind::FieldDecl
                                             ? ast::kFieldDeclInitWord
                                             : ast::kClassConstDeclInitWord;
        const ast::NodeId initializer(syntax.payload.words[initializerWord]);
        if (initializer) {
          if (!tree.contains(initializer)) { return zc::none; }
          const auto kind = tree.node(initializer).kind;
          if (!ast::isLiteralExprKind(kind) && !ast::isExprKind(kind) &&
              kind != ast::SyntaxKind::UnsafeBlockExpr) {
            return zc::none;
          }
          disposition = binder::DefinitionBodyDisposition::ExecutableBody;
        }
        break;
      }
      default:
        break;
    }
    inputs.add(binder::NamedDefinitionInventoryInput{definition.authority.clone(), disposition});
  }
  return binder::NamedDefinitionInventory::fromVerified(admission.module(), inputs.asPtr());
}

zc::Maybe<binder::NamedDefinitionInventory> verifierDefinitionInventory(
    const LoadedIdentitySource& source, const binder::StableIdentityAdmission& admission) {
  auto verified = verifiedIdentityCandidates(source);
  if (verified == zc::none ||
      ZC_ASSERT_NONNULL(verified).definitions.size() != admission.definitions().size()) {
    return zc::none;
  }
  const auto& tree = source.parsed.tree();
  zc::Vector<binder::NamedDefinitionInventoryInput> inputs(admission.definitions().size());
  for (size_t index = 0; index < admission.definitions().size(); ++index) {
    const auto& definition = admission.definitions()[index];
    if (!tree.contains(definition.node) ||
        !definition.site.key().source().sameAs(admission.source())) {
      return zc::none;
    }
    for (size_t prior = 0; prior < index; ++prior) {
      if (admission.definitions()[prior].node == definition.node ||
          admission.definitions()[prior].site.key().sameAs(definition.site.key())) {
        return zc::none;
      }
    }
    zc::Maybe<const binder::VerifiedStableDefinitionCandidate&> matched;
    for (const auto& candidate : ZC_ASSERT_NONNULL(verified).definitions) {
      if (candidate.node != definition.node) { continue; }
      if (matched != zc::none) { return zc::none; }
      matched = candidate;
    }
    if (matched == zc::none ||
        !ZC_ASSERT_NONNULL(matched).authority.sameRecordAs(definition.authority) ||
        ZC_ASSERT_NONNULL(matched).authority.key() != definition.authority.key() ||
        !ZC_ASSERT_NONNULL(matched).site.sameAs(definition.site.key()) ||
        ZC_ASSERT_NONNULL(matched).source.byteStart() != definition.site.range().byteStart() ||
        ZC_ASSERT_NONNULL(matched).source.byteEnd() != definition.site.range().byteEnd()) {
      return zc::none;
    }

    const auto& syntax = tree.node(definition.node);
    auto disposition = binder::DefinitionBodyDisposition::NoExecutableBody;
    switch (syntax.kind) {
      case ast::SyntaxKind::FunctionDecl: {
        const ast::NodeId body(syntax.payload.words[ast::kFunctionDeclBodyWord]);
        if (!body || !tree.contains(body) || tree.node(body).kind != ast::SyntaxKind::BlockStmt) {
          return zc::none;
        }
        disposition = binder::DefinitionBodyDisposition::ExecutableBody;
        break;
      }
      case ast::SyntaxKind::ConstructorDecl: {
        const ast::NodeId body(syntax.payload.words[ast::kConstructorDeclBodyWord]);
        if (!body || !tree.contains(body) || tree.node(body).kind != ast::SyntaxKind::BlockStmt) {
          return zc::none;
        }
        disposition = binder::DefinitionBodyDisposition::ExecutableBody;
        break;
      }
      case ast::SyntaxKind::DestructorDecl: {
        const ast::NodeId body(syntax.payload.words[ast::kDestructorDeclBodyWord]);
        if (!body || !tree.contains(body) || tree.node(body).kind != ast::SyntaxKind::BlockStmt) {
          return zc::none;
        }
        disposition = binder::DefinitionBodyDisposition::ExecutableBody;
        break;
      }
      case ast::SyntaxKind::MethodDecl: {
        const ast::NodeId body(syntax.payload.words[ast::kMethodDeclBodyWord]);
        if (body && (!tree.contains(body) || tree.node(body).kind != ast::SyntaxKind::BlockStmt)) {
          return zc::none;
        }
        if (body) { disposition = binder::DefinitionBodyDisposition::ExecutableBody; }
        break;
      }
      case ast::SyntaxKind::FieldDecl:
      case ast::SyntaxKind::ClassConstDecl: {
        const uint32_t initializerWord = syntax.kind == ast::SyntaxKind::FieldDecl
                                             ? ast::kFieldDeclInitWord
                                             : ast::kClassConstDeclInitWord;
        const ast::NodeId initializer(syntax.payload.words[initializerWord]);
        if (initializer) {
          if (!tree.contains(initializer)) { return zc::none; }
          const auto kind = tree.node(initializer).kind;
          if (!ast::isLiteralExprKind(kind) && !ast::isExprKind(kind) &&
              kind != ast::SyntaxKind::UnsafeBlockExpr) {
            return zc::none;
          }
          disposition = binder::DefinitionBodyDisposition::ExecutableBody;
        }
        break;
      }
      default:
        break;
    }
    inputs.add(binder::NamedDefinitionInventoryInput{definition.authority.clone(), disposition});
  }
  return binder::NamedDefinitionInventory::fromVerified(admission.module(), inputs.asPtr());
}

zc::Maybe<binder::NamedImplementationInventory> admittedImplementationInventory(
    const binder::StableIdentityAdmission& admission) {
  zc::Vector<identity::ImplIdentityAuthority> authorities(admission.implementations().size());
  for (const auto& implementation : admission.implementations()) {
    authorities.add(implementation.authority.clone());
  }
  return binder::NamedImplementationInventory::fromVerified(admission.module(),
                                                            authorities.asPtr());
}

zc::Maybe<binder::RevisionLocalDefinitionSites> admittedDefinitionSites(
    const binder::StableIdentityAdmission& admission,
    const binder::NamedDefinitionInventory& inventory) {
  zc::Vector<binder::RevisionLocalDefinitionSite> sites(admission.definitions().size());
  for (const auto& definition : admission.definitions()) {
    auto site = binder::RevisionLocalDefinitionSite::from(
        definition.node, definition.authority.key().clone(), definition.site.key().clone(),
        definition.site.range().byteStart(), definition.site.range().byteEnd());
    if (site == zc::none) { return zc::none; }
    sites.add(zc::mv(ZC_ASSERT_NONNULL(site)));
  }
  return binder::RevisionLocalDefinitionSites::fromVerified(admission.module(), admission.source(),
                                                            inventory, zc::mv(sites));
}

zc::Maybe<binder::RevisionLocalImplementationSites> admittedImplementationSites(
    const binder::StableIdentityAdmission& admission,
    const binder::NamedImplementationInventory& inventory) {
  zc::Vector<binder::RevisionLocalImplementationSite> sites(admission.implementations().size());
  for (const auto& implementation : admission.implementations()) {
    auto site = binder::RevisionLocalImplementationSite::from(
        implementation.node,
        binder::ImplSourceOccurrenceKey::from(implementation.authority.key().clone(),
                                              implementation.site.key().clone()),
        implementation.site.range().byteStart(), implementation.site.range().byteEnd());
    if (site == zc::none) { return zc::none; }
    sites.add(zc::mv(ZC_ASSERT_NONNULL(site)));
  }
  return binder::RevisionLocalImplementationSites::fromVerified(
      admission.module(), admission.source(), inventory, zc::mv(sites));
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

template <typename Descriptor>
query::CapabilityProviderResult<Descriptor> propagateLoadCapabilityFailure(
    const query::TypedQueryResult<LoadedIdentitySource>& loaded, const StableModuleQueryKey& key) {
  if (loaded.isRuntimeFailure()) {
    return query::CapabilityProviderResult<Descriptor>::runtimeRejected(loaded.runtimeFailure());
  }
  if (loaded.kind() == query::QueryValueKind::Absence) {
    auto module = decodeModule(key);
    if (module == zc::none) {
      return query::CapabilityProviderResult<Descriptor>::runtimeRejected(
          query::QueryRuntimeFailure::InvalidKeyEncoding);
    }
    zc::Maybe<binder::LocalSyntaxPath> noPath;
    auto failure = binder::BinderKeyFailure::from(
        binder::BinderKeyFailureKind::MissingSelectedModuleSource,
        binder::BinderQueryOwner::module(zc::mv(ZC_ASSERT_NONNULL(module))), zc::mv(noPath));
    if (failure == zc::none) {
      return query::CapabilityProviderResult<Descriptor>::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    return query::CapabilityProviderResult<Descriptor>::template keyRejected<
        binder::BinderKeyFailure>(zc::mv(ZC_ASSERT_NONNULL(failure)));
  }
  if (loaded.kind() == query::QueryValueKind::SemanticFailure) {
    using Contract =
        query::CapabilityFailureContract<Descriptor,
                                         query::SourceRejection<diagnostics::DiagnosticFact>>;
    auto diagnostics = Contract::decode(loaded.semanticFailureBytes());
    if (diagnostics == zc::none) {
      return query::CapabilityProviderResult<Descriptor>::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    return query::CapabilityProviderResult<Descriptor>::template sourceRejected<
        diagnostics::DiagnosticFact>(zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
  }
  return query::CapabilityProviderResult<Descriptor>::runtimeRejected(
      query::QueryRuntimeFailure::InvariantViolation);
}

template <typename TargetDescriptor, typename SourceDescriptor>
query::CapabilityProviderResult<TargetDescriptor> forwardSourceRejection(
    const query::CapabilityDemandResult<SourceDescriptor>& source) {
  using SourceContract =
      query::CapabilityFailureContract<SourceDescriptor,
                                       query::SourceRejection<diagnostics::DiagnosticFact>>;
  using TargetContract =
      query::CapabilityFailureContract<TargetDescriptor,
                                       query::SourceRejection<diagnostics::DiagnosticFact>>;
  auto diagnostics = TargetContract::decode(SourceContract::encode(source.diagnostics()).asPtr());
  if (diagnostics == zc::none) {
    return query::CapabilityProviderResult<TargetDescriptor>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::CapabilityProviderResult<TargetDescriptor>::template sourceRejected<
      diagnostics::DiagnosticFact>(zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
}

template <typename TargetDescriptor, typename SourceDescriptor>
query::CapabilityProviderResult<TargetDescriptor> forwardKeyRejection(
    const query::CapabilityDemandResult<SourceDescriptor>& source) {
  return query::CapabilityProviderResult<TargetDescriptor>::template keyRejected<
      binder::BinderKeyFailure>(source.keyFailure().clone());
}

bool verifyLoadFailure(const query::TypedQueryResult<LoadedIdentitySource>& loaded,
                       query::QueryValueKind actualKind,
                       zc::ArrayPtr<const uint8_t> actualFailure) {
  if (loaded.isRuntimeFailure()) { return false; }
  if (loaded.kind() == query::QueryValueKind::Absence) {
    return actualKind == query::QueryValueKind::Absence;
  }
  if (loaded.kind() == query::QueryValueKind::SemanticFailure) {
    auto decoded = binder::decodeStableBindingDiagnosticFacts(actualFailure);
    return actualKind == query::QueryValueKind::SemanticFailure && decoded != zc::none &&
           loaded.semanticFailureBytes() == actualFailure;
  }
  return false;
}

void encodeSourceSpan(identity::CanonicalEncoder& encoder, const identity::SourceSpan& source) {
  source.encode(encoder);
}

zc::Array<uint8_t> encodeIdentitySyntaxSiteInventoryWitness(
    const binder::IdentitySyntaxSiteInventory& inventory) {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(kIdentitySyntaxSiteInventoryWitnessDomain.asBytes());
  encoder.encodeUint8(0);
  inventory.module().encode(encoder);
  inventory.source().encode(encoder);
  encoder.encodeDigest(inventory.sourceDigest());
  encoder.encodeSequenceSize(inventory.entries().size());
  for (const auto& entry : inventory.entries()) {
    entry.site.key().encode(encoder);
    encoder.encodeUint32(entry.schemaPreorderOrdinal);
    encodeSourceSpan(encoder, entry.site.range());
  }
  return encoder.finish();
}

void encodeDefinitionAuthority(identity::CanonicalEncoder& encoder,
                               const identity::DefinitionIdentityAuthority& authority) {
  authority.record().encode(encoder);
  ZC_IF_SOME(overload, authority.overloadHeaderAuthority()) {
    encoder.encodeSome();
    overload.digest().encode(encoder);
    overload.header().encode(encoder);
  } else {
    encoder.encodeNone();
  }
}

zc::Array<uint8_t> encodeStableIdentityAdmissionWitness(
    const binder::StableIdentityAdmission& admission) {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(kStableIdentityAdmissionWitnessDomain.asBytes());
  encoder.encodeUint8(0);
  admission.module().encode(encoder);
  admission.source().encode(encoder);
  encoder.encodeDigest(admission.sourceDigest());
  encoder.encodeSequenceSize(admission.definitions().size());
  for (const auto& definition : admission.definitions()) {
    encoder.encodeUint32(definition.schemaPreorderOrdinal);
    encodeDefinitionAuthority(encoder, definition.authority);
    definition.site.key().encode(encoder);
    encodeSourceSpan(encoder, definition.site.range());
  }
  encoder.encodeSequenceSize(admission.implementations().size());
  for (const auto& implementation : admission.implementations()) {
    encoder.encodeUint32(implementation.schemaPreorderOrdinal);
    implementation.authority.record().encode(encoder);
    implementation.site.key().encode(encoder);
    encodeSourceSpan(encoder, implementation.site.range());
  }
  return encoder.finish();
}

template <typename Descriptor>
query::CapabilityProviderResult<Descriptor> sourceRejectionFromFact(
    diagnostics::DiagnosticFact&& fact) {
  zc::Vector<diagnostics::DiagnosticFact> facts;
  facts.add(zc::mv(fact));
  auto encoded = binder::encodeStableBindingDiagnosticFacts(facts.asPtr());
  if (encoded == zc::none) {
    return query::CapabilityProviderResult<Descriptor>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  using Contract =
      query::CapabilityFailureContract<Descriptor,
                                       query::SourceRejection<diagnostics::DiagnosticFact>>;
  auto diagnostics = Contract::decode(ZC_ASSERT_NONNULL(encoded).asPtr());
  if (diagnostics == zc::none) {
    return query::CapabilityProviderResult<Descriptor>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::CapabilityProviderResult<Descriptor>::template sourceRejected<
      diagnostics::DiagnosticFact>(zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
}

zc::Maybe<diagnostics::DiagnosticFact> identityAdmissionDiagnostic(
    const binder::CanonicalParsedModule& parsed, const binder::IdentitySyntaxSiteInventory& sites,
    const binder::StableIdentityCandidateSourceFailure& failure) {
  auto primary = binder::IdentitySyntaxSiteInventoryVerifier::resolve(parsed, sites, failure.node,
                                                                      failure.source);
  if (primary == zc::none) { return zc::none; }
  switch (failure.kind) {
    case binder::StableIdentityCandidateSourceFailureKind::ConstantExpressionNotAllowed:
      if (failure.previousNode != zc::none || failure.previous != zc::none ||
          failure.identifier != zc::none) {
        return zc::none;
      }
      return binder::StableBindingDiagnosticFactFactory::constantExpressionNotAllowed(
          ZC_ASSERT_NONNULL(primary));
    case binder::StableIdentityCandidateSourceFailureKind::DuplicateGenericParameter: {
      if (failure.previousNode == zc::none || failure.previous == zc::none ||
          failure.identifier == zc::none) {
        return zc::none;
      }
      auto previous = binder::IdentitySyntaxSiteInventoryVerifier::resolve(
          parsed, sites, ZC_ASSERT_NONNULL(failure.previousNode),
          ZC_ASSERT_NONNULL(failure.previous));
      if (previous == zc::none) { return zc::none; }
      auto arguments = binder::BinderIdentifierDiagnosticArguments::from(
          ZC_ASSERT_NONNULL(failure.identifier).clone());
      return binder::StableBindingDiagnosticFactFactory::duplicateGenericParameter(
          ZC_ASSERT_NONNULL(primary), ZC_ASSERT_NONNULL(previous), arguments);
    }
  }
  ZC_UNREACHABLE;
}

zc::Maybe<diagnostics::DiagnosticFact> definitionRedeclarationDiagnostic(
    zc::ArrayPtr<const binder::VerifiedStableDefinitionCandidate> definitions,
    const binder::StableDefinitionRedeclaration& redeclaration) {
  if (redeclaration.first >= definitions.size() || redeclaration.duplicate >= definitions.size()) {
    return zc::none;
  }
  const auto& duplicate = definitions[redeclaration.duplicate];
  const auto& previous = definitions[redeclaration.first];
  return binder::StableBindingDiagnosticFactFactory::definitionRedeclaration(
      duplicate.site, previous.site, static_cast<diagnostics::DiagID>(redeclaration.diagnostic),
      duplicate.authority.record().name());
}

}  // namespace

zc::Array<uint8_t> IdentitySyntaxSiteInventoryQuery::encodeKey(const Key& key) {
  return NamedDefinitionInventoryQuery::encodeKey(key);
}

zc::Maybe<IdentitySyntaxSiteInventoryQuery::Key> IdentitySyntaxSiteInventoryQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return NamedDefinitionInventoryQuery::decodeKey(bytes);
}

query::CapabilityProviderResult<IdentitySyntaxSiteInventoryQuery>
IdentitySyntaxSiteInventoryQuery::provide(
    query::CapabilityQueryContext<IdentitySyntaxSiteInventoryQuery>& context, const Key& key) {
  auto loaded = loadIdentitySource(context, key);
  if (loaded.kind() != query::QueryValueKind::Value || loaded.isRuntimeFailure()) {
    return propagateLoadCapabilityFailure<IdentitySyntaxSiteInventoryQuery>(loaded, key);
  }
  const auto& source = loaded.value();
  auto inventory = binder::IdentitySyntaxSiteInventoryProducer::produce(
      source.parsed, source.module, source.moduleNode);
  if (inventory == zc::none) {
    return query::CapabilityProviderResult<IdentitySyntaxSiteInventoryQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto candidate = zc::heap<Capability>(zc::mv(ZC_ASSERT_NONNULL(inventory)));
  auto witness =
      query::CapabilityCandidateContract<IdentitySyntaxSiteInventoryQuery>::encode(*candidate);
  return query::CapabilityProviderResult<IdentitySyntaxSiteInventoryQuery>::candidate(
      zc::mv(candidate), zc::mv(witness));
}

zc::Maybe<zc::Array<uint8_t>> IdentitySyntaxSiteInventoryQuery::verify(
    query::CapabilityQueryContext<IdentitySyntaxSiteInventoryQuery>& context, const Key& key,
    const Capability& candidate) {
  auto loaded = loadIdentitySource(context, key);
  if (loaded.kind() != query::QueryValueKind::Value || loaded.isRuntimeFailure()) {
    return zc::none;
  }
  const auto& source = loaded.value();
  if (!binder::IdentitySyntaxSiteInventoryVerifier::verify(source.parsed, source.module,
                                                           source.moduleNode, candidate)) {
    return zc::none;
  }
  return encodeIdentitySyntaxSiteInventoryWitness(candidate);
}

zc::Array<uint8_t> StableIdentityAdmissionQuery::encodeKey(const Key& key) {
  return NamedDefinitionInventoryQuery::encodeKey(key);
}

zc::Maybe<StableIdentityAdmissionQuery::Key> StableIdentityAdmissionQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return NamedDefinitionInventoryQuery::decodeKey(bytes);
}

query::CapabilityProviderResult<StableIdentityAdmissionQuery> StableIdentityAdmissionQuery::provide(
    query::CapabilityQueryContext<StableIdentityAdmissionQuery>& context, const Key& key) {
  auto loaded = loadIdentitySource(context, key);
  if (loaded.kind() != query::QueryValueKind::Value || loaded.isRuntimeFailure()) {
    return propagateLoadCapabilityFailure<StableIdentityAdmissionQuery>(loaded, key);
  }
  auto siteInventory = context.getCapability<IdentitySyntaxSiteInventoryQuery>(key);
  if (siteInventory.isRuntimeRejected()) {
    return query::CapabilityProviderResult<StableIdentityAdmissionQuery>::runtimeRejected(
        siteInventory.runtimeFailure());
  }
  if (siteInventory.isKeyRejected()) {
    return forwardKeyRejection<StableIdentityAdmissionQuery>(siteInventory);
  }
  if (siteInventory.isSourceRejected()) {
    return forwardSourceRejection<StableIdentityAdmissionQuery>(siteInventory);
  }
  if (!siteInventory.isPublished()) {
    return query::CapabilityProviderResult<StableIdentityAdmissionQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  const auto& source = loaded.value();
  auto production =
      binder::CandidateProducer::produce(source.parsed, source.module, source.moduleNode);
  auto reconstructed = binder::CandidateVerifier::verify(source.parsed, source.module,
                                                         source.moduleNode, production);
  if (reconstructed.is<binder::StableIdentityCandidateInvariant>()) {
    return query::CapabilityProviderResult<StableIdentityAdmissionQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  if (reconstructed.is<binder::VerifiedStableIdentityCandidateInventory>()) {
    const auto& definitions =
        reconstructed.get<binder::VerifiedStableIdentityCandidateInventory>().definitions;
    auto redeclarations =
        binder::CandidateVerifier::findDefinitionRedeclarations(definitions.asPtr());
    if (!redeclarations.is<zc::Vector<binder::StableDefinitionRedeclaration>>()) {
      return query::CapabilityProviderResult<StableIdentityAdmissionQuery>::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    const auto& rows = redeclarations.get<zc::Vector<binder::StableDefinitionRedeclaration>>();
    if (!rows.empty()) {
      auto fact = definitionRedeclarationDiagnostic(definitions.asPtr(), rows[0]);
      if (fact == zc::none) {
        return query::CapabilityProviderResult<StableIdentityAdmissionQuery>::runtimeRejected(
            query::QueryRuntimeFailure::InvariantViolation);
      }
      return sourceRejectionFromFact<StableIdentityAdmissionQuery>(zc::mv(ZC_ASSERT_NONNULL(fact)));
    }
  }
  auto admission = binder::StableIdentityAdmissionVerifier::verify(
      source.parsed, source.module, source.moduleNode, siteInventory.lease().capability(),
      production);
  if (admission.is<binder::StableIdentityCandidateSourceFailure>()) {
    auto fact =
        identityAdmissionDiagnostic(source.parsed, siteInventory.lease().capability(),
                                    admission.get<binder::StableIdentityCandidateSourceFailure>());
    if (fact == zc::none) {
      return query::CapabilityProviderResult<StableIdentityAdmissionQuery>::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    return sourceRejectionFromFact<StableIdentityAdmissionQuery>(zc::mv(ZC_ASSERT_NONNULL(fact)));
  }
  if (!admission.is<binder::StableIdentityAdmission>()) {
    return query::CapabilityProviderResult<StableIdentityAdmissionQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto candidate = zc::heap<Capability>(zc::mv(admission.get<binder::StableIdentityAdmission>()));
  auto witness =
      query::CapabilityCandidateContract<StableIdentityAdmissionQuery>::encode(*candidate);
  return query::CapabilityProviderResult<StableIdentityAdmissionQuery>::candidate(zc::mv(candidate),
                                                                                  zc::mv(witness));
}

zc::Maybe<zc::Array<uint8_t>> StableIdentityAdmissionQuery::verify(
    query::CapabilityQueryContext<StableIdentityAdmissionQuery>& context, const Key& key,
    const Capability& candidate) {
  auto loaded = loadIdentitySource(context, key);
  if (loaded.kind() != query::QueryValueKind::Value || loaded.isRuntimeFailure()) {
    return zc::none;
  }
  auto siteInventory = context.getCapability<IdentitySyntaxSiteInventoryQuery>(key);
  if (!siteInventory.isPublished()) { return zc::none; }
  const auto& source = loaded.value();
  auto production =
      binder::CandidateProducer::produce(source.parsed, source.module, source.moduleNode);
  auto admission = binder::StableIdentityAdmissionVerifier::verify(
      source.parsed, source.module, source.moduleNode, siteInventory.lease().capability(),
      production);
  if (!admission.is<binder::StableIdentityAdmission>() ||
      !(admission.get<binder::StableIdentityAdmission>() == candidate)) {
    return zc::none;
  }
  return encodeStableIdentityAdmissionWitness(candidate);
}

zc::Array<uint8_t> NamedDefinitionInventoryQuery::encodeKey(const Key& key) {
  return zc::heapArray<uint8_t>(key.canonicalModuleBytes());
}

zc::Maybe<NamedDefinitionInventoryQuery::Key> NamedDefinitionInventoryQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto module = identity::ModuleKey::decodeCanonical(decoder);
  if (module == zc::none || !decoder.finished()) { return zc::none; }
  return StableModuleQueryKey::fromVerified(ZC_ASSERT_NONNULL(module));
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
  auto admission = context.getCapability<StableIdentityAdmissionQuery>(key);
  if (admission.isRuntimeRejected()) {
    return query::TypedQueryResult<Value>::runtimeFailure(admission.runtimeFailure());
  }
  if (admission.isKeyRejected()) { return query::TypedQueryResult<Value>::absence(); }
  if (admission.isSourceRejected()) {
    auto encoded = binder::encodeStableBindingDiagnosticFacts(admission.diagnostics().values());
    if (encoded == zc::none) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    return query::TypedQueryResult<Value>::semanticFailure(zc::mv(ZC_ASSERT_NONNULL(encoded)));
  }
  if (!admission.isPublished()) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto inventory = providerDefinitionInventory(loaded.value(), admission.lease().capability());
  if (inventory == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(inventory)));
}

bool NamedDefinitionInventoryQuery::verify(query::QueryContext& context, const Key& key,
                                           const query::TypedQueryResult<Value>& result) {
  auto loaded = loadIdentitySource(context, key);
  if (loaded.isRuntimeFailure()) { return false; }
  if (loaded.kind() == query::QueryValueKind::Absence) {
    return result.kind() == query::QueryValueKind::Absence;
  }
  if (loaded.kind() == query::QueryValueKind::SemanticFailure) {
    return result.kind() == query::QueryValueKind::SemanticFailure &&
           result.semanticFailureBytes() == loaded.semanticFailureBytes();
  }
  if (loaded.kind() != query::QueryValueKind::Value) { return false; }
  auto admission = context.getCapability<StableIdentityAdmissionQuery>(key);
  if (admission.isRuntimeRejected()) { return false; }
  if (admission.isKeyRejected()) { return result.kind() == query::QueryValueKind::Absence; }
  if (admission.isSourceRejected()) {
    auto encoded = binder::encodeStableBindingDiagnosticFacts(admission.diagnostics().values());
    return result.kind() == query::QueryValueKind::SemanticFailure && encoded != zc::none &&
           result.semanticFailureBytes() == ZC_ASSERT_NONNULL(encoded).asPtr();
  }
  if (!admission.isPublished() || result.kind() != query::QueryValueKind::Value) { return false; }
  auto expected = verifierDefinitionInventory(loaded.value(), admission.lease().capability());
  return expected != zc::none && ZC_ASSERT_NONNULL(expected).sameAs(result.value());
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
  auto admission = context.getCapability<StableIdentityAdmissionQuery>(key);
  if (admission.isRuntimeRejected()) {
    return query::TypedQueryResult<Value>::runtimeFailure(admission.runtimeFailure());
  }
  if (admission.isKeyRejected()) { return query::TypedQueryResult<Value>::absence(); }
  if (admission.isSourceRejected()) {
    auto encoded = binder::encodeStableBindingDiagnosticFacts(admission.diagnostics().values());
    if (encoded == zc::none) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    return query::TypedQueryResult<Value>::semanticFailure(zc::mv(ZC_ASSERT_NONNULL(encoded)));
  }
  if (!admission.isPublished()) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto inventory = admittedImplementationInventory(admission.lease().capability());
  if (inventory == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(inventory)));
}

bool NamedImplementationInventoryQuery::verify(query::QueryContext& context, const Key& key,
                                               const query::TypedQueryResult<Value>& result) {
  auto admission = context.getCapability<StableIdentityAdmissionQuery>(key);
  if (admission.isRuntimeRejected()) { return false; }
  if (admission.isKeyRejected()) { return result.kind() == query::QueryValueKind::Absence; }
  if (admission.isSourceRejected()) {
    auto encoded = binder::encodeStableBindingDiagnosticFacts(admission.diagnostics().values());
    return result.kind() == query::QueryValueKind::SemanticFailure && encoded != zc::none &&
           result.semanticFailureBytes() == ZC_ASSERT_NONNULL(encoded).asPtr();
  }
  if (!admission.isPublished() || result.kind() != query::QueryValueKind::Value) { return false; }
  auto expected = admittedImplementationInventory(admission.lease().capability());
  return expected != zc::none && ZC_ASSERT_NONNULL(expected).sameAs(result.value());
}

zc::Array<uint8_t> RevisionLocalDefinitionSitesQuery::encodeKey(const Key& key) {
  return NamedDefinitionInventoryQuery::encodeKey(key);
}

zc::Maybe<RevisionLocalDefinitionSitesQuery::Key> RevisionLocalDefinitionSitesQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return NamedDefinitionInventoryQuery::decodeKey(bytes);
}

query::CapabilityProviderResult<RevisionLocalDefinitionSitesQuery>
RevisionLocalDefinitionSitesQuery::provide(
    query::CapabilityQueryContext<RevisionLocalDefinitionSitesQuery>& context, const Key& key) {
  auto loaded = loadIdentitySource(context, key);
  if (loaded.kind() != query::QueryValueKind::Value || loaded.isRuntimeFailure()) {
    return propagateLoadCapabilityFailure<RevisionLocalDefinitionSitesQuery>(loaded, key);
  }
  auto admission = context.getCapability<StableIdentityAdmissionQuery>(key);
  if (admission.isRuntimeRejected()) {
    return query::CapabilityProviderResult<RevisionLocalDefinitionSitesQuery>::runtimeRejected(
        admission.runtimeFailure());
  }
  if (admission.isKeyRejected()) {
    return forwardKeyRejection<RevisionLocalDefinitionSitesQuery>(admission);
  }
  if (admission.isSourceRejected()) {
    return forwardSourceRejection<RevisionLocalDefinitionSitesQuery>(admission);
  }
  if (!admission.isPublished()) {
    return query::CapabilityProviderResult<RevisionLocalDefinitionSitesQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto named = context.get<NamedDefinitionInventoryQuery>(key);
  if (named.isRuntimeFailure()) {
    return query::CapabilityProviderResult<RevisionLocalDefinitionSitesQuery>::runtimeRejected(
        named.runtimeFailure());
  }
  if (named.kind() == query::QueryValueKind::SemanticFailure) {
    using Contract =
        query::CapabilityFailureContract<RevisionLocalDefinitionSitesQuery,
                                         query::SourceRejection<diagnostics::DiagnosticFact>>;
    auto diagnostics = Contract::decode(named.semanticFailureBytes());
    if (diagnostics == zc::none) {
      return query::CapabilityProviderResult<RevisionLocalDefinitionSitesQuery>::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    return query::CapabilityProviderResult<RevisionLocalDefinitionSitesQuery>::sourceRejected<
        diagnostics::DiagnosticFact>(zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
  }
  if (named.kind() != query::QueryValueKind::Value) {
    return query::CapabilityProviderResult<RevisionLocalDefinitionSitesQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto sites = admittedDefinitionSites(admission.lease().capability(), named.value());
  if (sites == zc::none) {
    return query::CapabilityProviderResult<RevisionLocalDefinitionSitesQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto candidate = zc::heap<Capability>(zc::mv(ZC_ASSERT_NONNULL(sites)));
  auto stableWitness =
      query::CapabilityCandidateContract<RevisionLocalDefinitionSitesQuery>::encode(*candidate);
  return query::CapabilityProviderResult<RevisionLocalDefinitionSitesQuery>::candidate(
      zc::mv(candidate), zc::mv(stableWitness));
}

zc::Maybe<zc::Array<uint8_t>> RevisionLocalDefinitionSitesQuery::verify(
    query::CapabilityQueryContext<RevisionLocalDefinitionSitesQuery>& context, const Key& key,
    const Capability& candidate) {
  auto loaded = loadIdentitySource(context, key);
  if (loaded.kind() != query::QueryValueKind::Value || loaded.isRuntimeFailure()) {
    return zc::none;
  }
  auto admission = context.getCapability<StableIdentityAdmissionQuery>(key);
  if (!admission.isPublished()) { return zc::none; }
  auto named = context.get<NamedDefinitionInventoryQuery>(key);
  if (named.isRuntimeFailure() || named.kind() != query::QueryValueKind::Value) { return zc::none; }
  auto expected = admittedDefinitionSites(admission.lease().capability(), named.value());
  if (expected == zc::none || !ZC_ASSERT_NONNULL(expected).sameAs(candidate)) { return zc::none; }
  auto witness = candidate.encodeCanonical();
  auto decoded = binder::RevisionLocalDefinitionSites::decodeCanonical(witness.asPtr());
  if (decoded == zc::none || !ZC_ASSERT_NONNULL(decoded).sameAs(candidate)) { return zc::none; }
  return zc::mv(witness);
}

zc::Array<uint8_t> RevisionLocalImplementationSitesQuery::encodeKey(const Key& key) {
  return NamedDefinitionInventoryQuery::encodeKey(key);
}

zc::Maybe<RevisionLocalImplementationSitesQuery::Key>
RevisionLocalImplementationSitesQuery::decodeKey(zc::ArrayPtr<const uint8_t> bytes) {
  return NamedDefinitionInventoryQuery::decodeKey(bytes);
}

query::CapabilityProviderResult<RevisionLocalImplementationSitesQuery>
RevisionLocalImplementationSitesQuery::provide(
    query::CapabilityQueryContext<RevisionLocalImplementationSitesQuery>& context, const Key& key) {
  auto loaded = loadIdentitySource(context, key);
  if (loaded.kind() != query::QueryValueKind::Value || loaded.isRuntimeFailure()) {
    return propagateLoadCapabilityFailure<RevisionLocalImplementationSitesQuery>(loaded, key);
  }
  auto admission = context.getCapability<StableIdentityAdmissionQuery>(key);
  if (admission.isRuntimeRejected()) {
    return query::CapabilityProviderResult<RevisionLocalImplementationSitesQuery>::runtimeRejected(
        admission.runtimeFailure());
  }
  if (admission.isKeyRejected()) {
    return forwardKeyRejection<RevisionLocalImplementationSitesQuery>(admission);
  }
  if (admission.isSourceRejected()) {
    return forwardSourceRejection<RevisionLocalImplementationSitesQuery>(admission);
  }
  if (!admission.isPublished()) {
    return query::CapabilityProviderResult<RevisionLocalImplementationSitesQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto named = context.get<NamedImplementationInventoryQuery>(key);
  if (named.isRuntimeFailure()) {
    return query::CapabilityProviderResult<RevisionLocalImplementationSitesQuery>::runtimeRejected(
        named.runtimeFailure());
  }
  if (named.kind() == query::QueryValueKind::SemanticFailure) {
    using Contract =
        query::CapabilityFailureContract<RevisionLocalImplementationSitesQuery,
                                         query::SourceRejection<diagnostics::DiagnosticFact>>;
    auto diagnostics = Contract::decode(named.semanticFailureBytes());
    if (diagnostics == zc::none) {
      return query::CapabilityProviderResult<RevisionLocalImplementationSitesQuery>::
          runtimeRejected(query::QueryRuntimeFailure::InvariantViolation);
    }
    return query::CapabilityProviderResult<RevisionLocalImplementationSitesQuery>::sourceRejected<
        diagnostics::DiagnosticFact>(zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
  }
  if (named.kind() != query::QueryValueKind::Value) {
    return query::CapabilityProviderResult<RevisionLocalImplementationSitesQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto sites = admittedImplementationSites(admission.lease().capability(), named.value());
  if (sites == zc::none) {
    return query::CapabilityProviderResult<RevisionLocalImplementationSitesQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto candidate = zc::heap<Capability>(zc::mv(ZC_ASSERT_NONNULL(sites)));
  auto stableWitness =
      query::CapabilityCandidateContract<RevisionLocalImplementationSitesQuery>::encode(*candidate);
  return query::CapabilityProviderResult<RevisionLocalImplementationSitesQuery>::candidate(
      zc::mv(candidate), zc::mv(stableWitness));
}

zc::Maybe<zc::Array<uint8_t>> RevisionLocalImplementationSitesQuery::verify(
    query::CapabilityQueryContext<RevisionLocalImplementationSitesQuery>& context, const Key& key,
    const Capability& candidate) {
  auto loaded = loadIdentitySource(context, key);
  if (loaded.kind() != query::QueryValueKind::Value || loaded.isRuntimeFailure()) {
    return zc::none;
  }
  auto admission = context.getCapability<StableIdentityAdmissionQuery>(key);
  if (!admission.isPublished()) { return zc::none; }
  auto named = context.get<NamedImplementationInventoryQuery>(key);
  if (named.isRuntimeFailure() || named.kind() != query::QueryValueKind::Value) { return zc::none; }
  auto expected = admittedImplementationSites(admission.lease().capability(), named.value());
  if (expected == zc::none || !ZC_ASSERT_NONNULL(expected).sameAs(candidate)) { return zc::none; }
  auto witness = candidate.encodeCanonical();
  auto decoded = binder::RevisionLocalImplementationSites::decodeCanonical(witness.asPtr());
  if (decoded == zc::none || !ZC_ASSERT_NONNULL(decoded).sameAs(candidate)) { return zc::none; }
  return zc::mv(witness);
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
  auto admission = context.getCapability<StableIdentityAdmissionQuery>(key);
  if (admission.isRuntimeRejected()) {
    return query::TypedQueryResult<Value>::runtimeFailure(admission.runtimeFailure());
  }
  if (admission.isKeyRejected()) { return query::TypedQueryResult<Value>::absence(); }
  if (admission.isSourceRejected()) {
    using Contract =
        query::CapabilityFailureContract<StableIdentityAdmissionQuery,
                                         query::SourceRejection<diagnostics::DiagnosticFact>>;
    return query::TypedQueryResult<Value>::semanticFailure(
        Contract::encode(admission.diagnostics()));
  }
  if (!admission.isPublished()) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  const auto& source = loaded.value();
  auto projection = binder::ModuleBodySyntaxProducer::produce(
      source.parsed, source.module, source.moduleNode, admission.lease().capability());
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
  auto admission = context.getCapability<StableIdentityAdmissionQuery>(key);
  if (admission.isRuntimeRejected()) { return false; }
  if (admission.isKeyRejected()) { return result.kind() == query::QueryValueKind::Absence; }
  if (admission.isSourceRejected()) {
    using Contract =
        query::CapabilityFailureContract<StableIdentityAdmissionQuery,
                                         query::SourceRejection<diagnostics::DiagnosticFact>>;
    const auto failure = Contract::encode(admission.diagnostics());
    return result.kind() == query::QueryValueKind::SemanticFailure &&
           result.semanticFailureBytes() == failure.asPtr();
  }
  if (result.kind() != query::QueryValueKind::Value || !admission.isPublished()) { return false; }
  const auto& source = loaded.value();
  auto expected = binder::ModuleBodySyntaxVerifier::reconstruct(
      source.parsed, source.module, source.moduleNode, admission.lease().capability());
  return expected.is<binder::ModuleBodySyntaxProjection>() &&
         expected.get<binder::ModuleBodySyntaxProjection>().syntax == result.value();
}

zc::Array<uint8_t> ModuleBodyProvenanceQuery::encodeKey(const Key& key) {
  return NamedDefinitionInventoryQuery::encodeKey(key);
}

zc::Maybe<ModuleBodyProvenanceQuery::Key> ModuleBodyProvenanceQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return NamedDefinitionInventoryQuery::decodeKey(bytes);
}

query::CapabilityProviderResult<ModuleBodyProvenanceQuery> ModuleBodyProvenanceQuery::provide(
    query::CapabilityQueryContext<ModuleBodyProvenanceQuery>& context, const Key& key) {
  auto loaded = loadIdentitySource(context, key);
  if (loaded.kind() != query::QueryValueKind::Value || loaded.isRuntimeFailure()) {
    return propagateLoadCapabilityFailure<ModuleBodyProvenanceQuery>(loaded, key);
  }
  auto admission = context.getCapability<StableIdentityAdmissionQuery>(key);
  if (admission.isRuntimeRejected()) {
    return query::CapabilityProviderResult<ModuleBodyProvenanceQuery>::runtimeRejected(
        admission.runtimeFailure());
  }
  if (admission.isKeyRejected()) {
    return forwardKeyRejection<ModuleBodyProvenanceQuery>(admission);
  }
  if (admission.isSourceRejected()) {
    return forwardSourceRejection<ModuleBodyProvenanceQuery>(admission);
  }
  if (!admission.isPublished()) {
    return query::CapabilityProviderResult<ModuleBodyProvenanceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto syntax = context.get<ModuleBodySyntaxQuery>(key);
  if (syntax.isRuntimeFailure()) {
    return query::CapabilityProviderResult<ModuleBodyProvenanceQuery>::runtimeRejected(
        syntax.runtimeFailure());
  }
  if (syntax.kind() != query::QueryValueKind::Value) {
    return query::CapabilityProviderResult<ModuleBodyProvenanceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  const auto& source = loaded.value();
  auto projection = binder::ModuleBodySyntaxProducer::produce(
      source.parsed, source.module, source.moduleNode, admission.lease().capability());
  if (!projection.is<binder::ModuleBodySyntaxProjection>() ||
      projection.get<binder::ModuleBodySyntaxProjection>().syntax != syntax.value()) {
    return query::CapabilityProviderResult<ModuleBodyProvenanceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto provenance = zc::mv(projection.get<binder::ModuleBodySyntaxProjection>().provenance);
  auto candidate = zc::heap<Capability>(zc::mv(provenance));
  auto stableWitness =
      query::CapabilityCandidateContract<ModuleBodyProvenanceQuery>::encode(*candidate);
  return query::CapabilityProviderResult<ModuleBodyProvenanceQuery>::candidate(
      zc::mv(candidate), zc::mv(stableWitness));
}

zc::Maybe<zc::Array<uint8_t>> ModuleBodyProvenanceQuery::verify(
    query::CapabilityQueryContext<ModuleBodyProvenanceQuery>& context, const Key& key,
    const Capability& candidate) {
  auto loaded = loadIdentitySource(context, key);
  if (loaded.kind() != query::QueryValueKind::Value || loaded.isRuntimeFailure()) {
    return zc::none;
  }
  auto admission = context.getCapability<StableIdentityAdmissionQuery>(key);
  if (!admission.isPublished()) { return zc::none; }
  auto syntax = context.get<ModuleBodySyntaxQuery>(key);
  if (syntax.isRuntimeFailure() || syntax.kind() != query::QueryValueKind::Value) {
    return zc::none;
  }
  const auto& source = loaded.value();
  auto expected = binder::ModuleBodySyntaxVerifier::reconstruct(
      source.parsed, source.module, source.moduleNode, admission.lease().capability());
  if (!expected.is<binder::ModuleBodySyntaxProjection>() ||
      expected.get<binder::ModuleBodySyntaxProjection>().syntax != syntax.value() ||
      expected.get<binder::ModuleBodySyntaxProjection>().provenance != candidate) {
    return zc::none;
  }
  auto witness = candidate.encodeCanonical();
  auto decoded = binder::ModuleBodyProvenance::decodeCanonical(witness.asPtr());
  if (decoded == zc::none || ZC_ASSERT_NONNULL(decoded) != candidate) { return zc::none; }
  return zc::mv(witness);
}

enum class BinderCapabilityFailureReconstruction : uint8_t {
  IdentitySiteInventory = 0x01,
  StableIdentityAdmission = 0x02,
  StableIdentityDependent = 0x03
};

bool sameSourceRejection(zc::ArrayPtr<const uint8_t> expected,
                         zc::ArrayPtr<const diagnostics::DiagnosticFact> actual) {
  auto encoded = binder::encodeStableBindingDiagnosticFacts(actual);
  return encoded != zc::none && expected == ZC_ASSERT_NONNULL(encoded).asPtr();
}

template <typename Descriptor>
query::CapabilityRejectionCheck verifyBinderSourceRejection(
    query::CapabilityQueryContext<Descriptor>& context, const typename Descriptor::Key& key,
    zc::ArrayPtr<const diagnostics::DiagnosticFact> diagnostics,
    BinderCapabilityFailureReconstruction reconstruction) {
  auto loaded = loadIdentitySource(context, key);
  if (loaded.isRuntimeFailure() || loaded.kind() == query::QueryValueKind::Absence) {
    return query::CapabilityRejectionCheck::Rejected;
  }
  if (loaded.kind() == query::QueryValueKind::SemanticFailure) {
    return sameSourceRejection(loaded.semanticFailureBytes(), diagnostics)
               ? query::CapabilityRejectionCheck::Verified
               : query::CapabilityRejectionCheck::Rejected;
  }
  if (loaded.kind() != query::QueryValueKind::Value ||
      reconstruction == BinderCapabilityFailureReconstruction::IdentitySiteInventory) {
    return query::CapabilityRejectionCheck::Rejected;
  }
  if (reconstruction == BinderCapabilityFailureReconstruction::StableIdentityDependent) {
    auto admission = context.template getCapability<StableIdentityAdmissionQuery>(key);
    if (!admission.isSourceRejected()) { return query::CapabilityRejectionCheck::Rejected; }
    auto expected = binder::encodeStableBindingDiagnosticFacts(admission.diagnostics().values());
    auto actual = binder::encodeStableBindingDiagnosticFacts(diagnostics);
    return expected != zc::none && actual != zc::none &&
                   ZC_ASSERT_NONNULL(expected).asPtr() == ZC_ASSERT_NONNULL(actual).asPtr()
               ? query::CapabilityRejectionCheck::Verified
               : query::CapabilityRejectionCheck::Rejected;
  }
  auto sites = context.template getCapability<IdentitySyntaxSiteInventoryQuery>(key);
  if (!sites.isPublished()) {
    if (!sites.isSourceRejected()) { return query::CapabilityRejectionCheck::Rejected; }
    auto expected = binder::encodeStableBindingDiagnosticFacts(sites.diagnostics().values());
    auto actual = binder::encodeStableBindingDiagnosticFacts(diagnostics);
    return expected != zc::none && actual != zc::none &&
                   ZC_ASSERT_NONNULL(expected).asPtr() == ZC_ASSERT_NONNULL(actual).asPtr()
               ? query::CapabilityRejectionCheck::Verified
               : query::CapabilityRejectionCheck::Rejected;
  }
  const auto& source = loaded.value();
  auto verification =
      binder::CandidateVerifier::reconstruct(source.parsed, source.module, source.moduleNode);
  if (verification.template is<binder::VerifiedStableIdentityCandidateInventory>()) {
    const auto& definitions =
        verification.template get<binder::VerifiedStableIdentityCandidateInventory>().definitions;
    auto redeclarations =
        binder::CandidateVerifier::findDefinitionRedeclarations(definitions.asPtr());
    if (!redeclarations.template is<zc::Vector<binder::StableDefinitionRedeclaration>>()) {
      return query::CapabilityRejectionCheck::Rejected;
    }
    const auto& rows =
        redeclarations.template get<zc::Vector<binder::StableDefinitionRedeclaration>>();
    if (rows.empty()) { return query::CapabilityRejectionCheck::Rejected; }
    auto fact = definitionRedeclarationDiagnostic(definitions.asPtr(), rows[0]);
    if (fact == zc::none) { return query::CapabilityRejectionCheck::Rejected; }
    zc::Vector<diagnostics::DiagnosticFact> expectedFacts;
    expectedFacts.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
    auto expected = binder::encodeStableBindingDiagnosticFacts(expectedFacts.asPtr());
    auto actual = binder::encodeStableBindingDiagnosticFacts(diagnostics);
    return expected != zc::none && actual != zc::none &&
                   ZC_ASSERT_NONNULL(expected).asPtr() == ZC_ASSERT_NONNULL(actual).asPtr()
               ? query::CapabilityRejectionCheck::Verified
               : query::CapabilityRejectionCheck::Rejected;
  }
  if (!verification.template is<binder::StableIdentityCandidateSourceFailure>()) {
    return query::CapabilityRejectionCheck::Rejected;
  }
  auto fact = identityAdmissionDiagnostic(
      source.parsed, sites.lease().capability(),
      verification.template get<binder::StableIdentityCandidateSourceFailure>());
  if (fact == zc::none) { return query::CapabilityRejectionCheck::Rejected; }
  zc::Vector<diagnostics::DiagnosticFact> expectedFacts;
  expectedFacts.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
  auto expected = binder::encodeStableBindingDiagnosticFacts(expectedFacts.asPtr());
  auto actual = binder::encodeStableBindingDiagnosticFacts(diagnostics);
  return expected != zc::none && actual != zc::none &&
                 ZC_ASSERT_NONNULL(expected).asPtr() == ZC_ASSERT_NONNULL(actual).asPtr()
             ? query::CapabilityRejectionCheck::Verified
             : query::CapabilityRejectionCheck::Rejected;
}

template <typename Descriptor>
query::CapabilityRejectionCheck verifyBinderKeyRejection(
    query::CapabilityQueryContext<Descriptor>& context, const typename Descriptor::Key& key,
    const binder::BinderKeyFailure& failure, BinderCapabilityFailureReconstruction reconstruction) {
  auto loaded = loadIdentitySource(context, key);
  if (loaded.isRuntimeFailure() || loaded.kind() == query::QueryValueKind::SemanticFailure) {
    return query::CapabilityRejectionCheck::Rejected;
  }
  if (loaded.kind() == query::QueryValueKind::Absence) {
    auto module = decodeModule(key);
    if (module == zc::none) { return query::CapabilityRejectionCheck::Rejected; }
    zc::Maybe<binder::LocalSyntaxPath> noPath;
    auto expected = binder::BinderKeyFailure::from(
        binder::BinderKeyFailureKind::MissingSelectedModuleSource,
        binder::BinderQueryOwner::module(zc::mv(ZC_ASSERT_NONNULL(module))), zc::mv(noPath));
    return expected != zc::none && ZC_ASSERT_NONNULL(expected) == failure
               ? query::CapabilityRejectionCheck::Verified
               : query::CapabilityRejectionCheck::Rejected;
  }
  if (loaded.kind() != query::QueryValueKind::Value ||
      reconstruction == BinderCapabilityFailureReconstruction::IdentitySiteInventory) {
    return query::CapabilityRejectionCheck::Rejected;
  }
  if (reconstruction == BinderCapabilityFailureReconstruction::StableIdentityAdmission) {
    auto sites = context.template getCapability<IdentitySyntaxSiteInventoryQuery>(key);
    return sites.isKeyRejected() && sites.keyFailure() == failure
               ? query::CapabilityRejectionCheck::Verified
               : query::CapabilityRejectionCheck::Rejected;
  }
  auto admission = context.template getCapability<StableIdentityAdmissionQuery>(key);
  return admission.isKeyRejected() && admission.keyFailure() == failure
             ? query::CapabilityRejectionCheck::Verified
             : query::CapabilityRejectionCheck::Rejected;
}

}  // namespace zomlang::compiler::driver::incremental_binding_query

namespace zomlang::compiler::query {
namespace {

template <typename Capability>
zc::Maybe<zc::Own<Capability>> decodeCanonicalCandidate(zc::ArrayPtr<const uint8_t> bytes) {
  auto candidate = Capability::decodeCanonical(bytes);
  if (candidate == zc::none || ZC_ASSERT_NONNULL(candidate).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::heap<Capability>(zc::mv(ZC_ASSERT_NONNULL(candidate)));
}

template <typename Contract>
zc::Array<uint8_t> encodeSourceSequence(const typename Contract::Sequence& diagnostics) {
  auto encoded = binder::encodeStableBindingDiagnosticFacts(diagnostics.values());
  return zc::mv(ZC_ASSERT_NONNULL(encoded));
}

zc::Array<uint8_t> encodeBinderKeyFailure(const binder::BinderKeyFailure& failure) {
  return binder::StableBindingCodec<binder::BinderKeyFailure>::encode(failure);
}

zc::Maybe<binder::BinderKeyFailure> decodeBinderKeyFailure(zc::ArrayPtr<const uint8_t> bytes) {
  return binder::StableBindingCodec<binder::BinderKeyFailure>::decode(bytes);
}

}  // namespace

#define ZOM_DEFINE_BINDER_FAILURE_CONTRACTS(DescriptorName, Reconstruction)                        \
  zc::Array<uint8_t>                                                                               \
  CapabilityFailureContract<DescriptorName, SourceRejection<diagnostics::DiagnosticFact>>::encode( \
      const Sequence& diagnostics) {                                                               \
    using Contract =                                                                               \
        CapabilityFailureContract<DescriptorName, SourceRejection<diagnostics::DiagnosticFact>>;   \
    return encodeSourceSequence<Contract>(diagnostics);                                            \
  }                                                                                                \
  zc::Maybe<CapabilityFailureContract<DescriptorName,                                              \
                                      SourceRejection<diagnostics::DiagnosticFact>>::Sequence>     \
  CapabilityFailureContract<DescriptorName, SourceRejection<diagnostics::DiagnosticFact>>::decode( \
      zc::ArrayPtr<const uint8_t> bytes) {                                                         \
    auto facts = binder::decodeStableBindingDiagnosticFacts(bytes);                                \
    if (facts == zc::none) { return zc::none; }                                                    \
    return Sequence(zc::mv(ZC_ASSERT_NONNULL(facts)));                                             \
  }                                                                                                \
  CapabilityRejectionCheck                                                                         \
  CapabilityFailureContract<DescriptorName, SourceRejection<diagnostics::DiagnosticFact>>::verify( \
      CapabilityQueryContext<DescriptorName>& context, const DescriptorName::Key& key,             \
      const Sequence& diagnostics) {                                                               \
    return driver::incremental_binding_query::verifyBinderSourceRejection(                         \
        context, key, diagnostics.values(), Reconstruction);                                       \
  }                                                                                                \
  zc::Array<uint8_t>                                                                               \
  CapabilityFailureContract<DescriptorName, KeyRejection<binder::BinderKeyFailure>>::encode(       \
      const binder::BinderKeyFailure& failure) {                                                   \
    return encodeBinderKeyFailure(failure);                                                        \
  }                                                                                                \
  zc::Maybe<binder::BinderKeyFailure>                                                              \
  CapabilityFailureContract<DescriptorName, KeyRejection<binder::BinderKeyFailure>>::decode(       \
      zc::ArrayPtr<const uint8_t> bytes) {                                                         \
    return decodeBinderKeyFailure(bytes);                                                          \
  }                                                                                                \
  CapabilityRejectionCheck                                                                         \
  CapabilityFailureContract<DescriptorName, KeyRejection<binder::BinderKeyFailure>>::verify(       \
      CapabilityQueryContext<DescriptorName>& context, const DescriptorName::Key& key,             \
      const binder::BinderKeyFailure& failure) {                                                   \
    return driver::incremental_binding_query::verifyBinderKeyRejection(context, key, failure,      \
                                                                       Reconstruction);            \
  }

using IdentitySiteInventoryDescriptor =
    driver::incremental_binding_query::IdentitySyntaxSiteInventoryQuery;
using StableIdentityAdmissionDescriptor =
    driver::incremental_binding_query::StableIdentityAdmissionQuery;
using DefinitionSitesDescriptor =
    driver::incremental_binding_query::RevisionLocalDefinitionSitesQuery;
using ImplementationSitesDescriptor =
    driver::incremental_binding_query::RevisionLocalImplementationSitesQuery;
using ModuleBodyProvenanceDescriptor = driver::incremental_binding_query::ModuleBodyProvenanceQuery;

StableWitnessBytes CapabilityCandidateContract<IdentitySiteInventoryDescriptor>::encode(
    const IdentitySiteInventoryDescriptor::Capability& candidate) {
  return StableWitnessBytes(
      driver::incremental_binding_query::encodeIdentitySyntaxSiteInventoryWitness(candidate));
}
zc::Maybe<zc::Own<IdentitySiteInventoryDescriptor::Capability>> CapabilityCandidateContract<
    IdentitySiteInventoryDescriptor>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  return zc::none;
}

StableWitnessBytes CapabilityCandidateContract<StableIdentityAdmissionDescriptor>::encode(
    const StableIdentityAdmissionDescriptor::Capability& candidate) {
  return StableWitnessBytes(
      driver::incremental_binding_query::encodeStableIdentityAdmissionWitness(candidate));
}
zc::Maybe<zc::Own<StableIdentityAdmissionDescriptor::Capability>> CapabilityCandidateContract<
    StableIdentityAdmissionDescriptor>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  return zc::none;
}

#define ZOM_DEFINE_CANONICAL_BINDER_CANDIDATE(DescriptorName, CapabilityType)              \
  StableWitnessBytes CapabilityCandidateContract<DescriptorName>::encode(                  \
      const DescriptorName::Capability& candidate) {                                       \
    return StableWitnessBytes(candidate.encodeCanonical());                                \
  }                                                                                        \
  zc::Maybe<zc::Own<DescriptorName::Capability>>                                           \
  CapabilityCandidateContract<DescriptorName>::decode(zc::ArrayPtr<const uint8_t> bytes) { \
    return decodeCanonicalCandidate<CapabilityType>(bytes);                                \
  }

ZOM_DEFINE_BINDER_FAILURE_CONTRACTS(
    IdentitySiteInventoryDescriptor,
    driver::incremental_binding_query::BinderCapabilityFailureReconstruction::IdentitySiteInventory)
ZOM_DEFINE_BINDER_FAILURE_CONTRACTS(
    StableIdentityAdmissionDescriptor,
    driver::incremental_binding_query::BinderCapabilityFailureReconstruction::
        StableIdentityAdmission)
ZOM_DEFINE_CANONICAL_BINDER_CANDIDATE(DefinitionSitesDescriptor,
                                      binder::RevisionLocalDefinitionSites)
ZOM_DEFINE_BINDER_FAILURE_CONTRACTS(
    DefinitionSitesDescriptor, driver::incremental_binding_query::
                                   BinderCapabilityFailureReconstruction::StableIdentityDependent)
ZOM_DEFINE_CANONICAL_BINDER_CANDIDATE(ImplementationSitesDescriptor,
                                      binder::RevisionLocalImplementationSites)
ZOM_DEFINE_BINDER_FAILURE_CONTRACTS(
    ImplementationSitesDescriptor,
    driver::incremental_binding_query::BinderCapabilityFailureReconstruction::
        StableIdentityDependent)
ZOM_DEFINE_CANONICAL_BINDER_CANDIDATE(ModuleBodyProvenanceDescriptor, binder::ModuleBodyProvenance)
ZOM_DEFINE_BINDER_FAILURE_CONTRACTS(
    ModuleBodyProvenanceDescriptor,
    driver::incremental_binding_query::BinderCapabilityFailureReconstruction::
        StableIdentityDependent)

#undef ZOM_DEFINE_CANONICAL_BINDER_CANDIDATE
#undef ZOM_DEFINE_BINDER_FAILURE_CONTRACTS

}  // namespace zomlang::compiler::query
