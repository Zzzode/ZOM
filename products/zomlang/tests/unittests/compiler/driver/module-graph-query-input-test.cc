// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zc/core/encoding.h"
#include "zc/core/map.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/thread-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/binder/binding-input.h"
#include "zomlang/compiler/binder/module-dependency-requests.h"
#include "zomlang/compiler/diagnostics/source-diagnostic-draft-buffer.h"
#include "zomlang/compiler/driver/core-library-query-provider.h"
#include "zomlang/compiler/driver/module-dependency-provenance-query.h"
#include "zomlang/compiler/driver/module-graph-query.h"
#include "zomlang/compiler/driver/package/package-compilation-request.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/source-snapshot.h"
#include "zomlang/compiler/ir/target-registry.h"
#include "zomlang/compiler/parser/parser.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/tests/unittests/compiler/binder/parsed-module-query-test-fixture.h"
#include "zomlang/tests/unittests/compiler/driver/canonical-mutation-test-helpers.h"
#include "zomlang/tests/unittests/compiler/driver/core-library-test-fixture.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::driver::module_graph_query {
namespace {

namespace mutation = tests::canonical_mutation;

using tests::test_identity_detail::coreCrate;
using tests::test_identity_detail::crate;
using tests::test_identity_detail::digest;
using tests::test_identity_detail::package;
using tests::test_identity_detail::scalar;

identity::ModuleKey module(const identity::CrateKey& owner, zc::StringPtr first,
                           zc::StringPtr second = nullptr) {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(scalar<identity::ModulePathSegment>(first));
  if (second != nullptr) { path.add(scalar<identity::ModulePathSegment>(second)); }
  auto result = identity::ModuleKey::from(owner.clone(), zc::mv(path));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

bool sameCrate(const identity::CrateKey& left, const identity::CrateKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

bool sameModule(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

identity::PackageKey packageNamed(zc::StringPtr name) {
  zc::Vector<identity::CanonicalPathSegment> path;
  auto version = identity::ResolvedVersion::fromCanonical("0.0.0"_zc);
  zc::Vector<identity::FeatureName> features;
  auto sortedFeatures = identity::SortedFeatureSet::from(zc::mv(features));
  ZC_REQUIRE(version != zc::none);
  ZC_REQUIRE(sortedFeatures != zc::none);
  return identity::PackageKey::from(
      identity::CanonicalPackageSource::localPath(
          identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(path))),
      scalar<identity::PackageName>(name), zc::mv(ZC_REQUIRE_NONNULL(version)),
      zc::mv(ZC_REQUIRE_NONNULL(sortedFeatures)));
}

identity::CrateKey crateForPackage(const identity::CrateKey& model, identity::PackageKey&& owner,
                                   zc::StringPtr targetName) {
  auto result = identity::CrateKey::from(
      identity::CompilationUnitIdentity::userPackage(zc::mv(owner)),
      identity::CrateTargetKind::Library, scalar<identity::TargetName>(targetName),
      model.compilation().clone());
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::PackageDependencyEdgeKey packageEdge(const identity::PackageKey& consumer,
                                               const identity::PackageKey& provider,
                                               zc::StringPtr alias = "dependency"_zc) {
  auto result = identity::PackageDependencyEdgeKey::from(
      consumer.clone(), scalar<identity::DependencyAlias>(alias),
      identity::DependencyDomain::Target, provider.clone());
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::CrateDependencyEdgeKey crateEdge(const identity::PackageDependencyEdgeKey& origin,
                                           const identity::CrateKey& consumer,
                                           const identity::CrateKey& provider) {
  auto result = identity::CrateDependencyEdgeKey::from(
      identity::CrateDependencyOrigin::userPackage(origin.clone()), consumer.clone(),
      provider.clone());
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

bool encodedLess(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  const size_t common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    if (left[index] < right[index]) { return true; }
    if (left[index] > right[index]) { return false; }
  }
  return left.size() < right.size();
}

void sortEncoded(zc::Vector<zc::Array<uint8_t>>& values) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    size_t insertion = index;
    while (insertion != 0 && encodedLess(current.asPtr(), values[insertion - 1].asPtr())) {
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
}

zc::Array<uint8_t> packageGraphBytes(zc::Vector<zc::Array<uint8_t>>&& packages,
                                     zc::Vector<zc::Array<uint8_t>>&& resolvedEdges,
                                     zc::Vector<zc::Array<uint8_t>>&& selectedEdges,
                                     zc::Vector<zc::Array<uint8_t>>&& crates,
                                     zc::Vector<zc::Array<uint8_t>>&& crateEdges) {
  sortEncoded(packages);
  sortEncoded(resolvedEdges);
  sortEncoded(selectedEdges);
  sortEncoded(crates);
  sortEncoded(crateEdges);
  identity::CanonicalEncoder encoder;
  const auto encode = [&](const zc::Vector<zc::Array<uint8_t>>& values) {
    encoder.encodeSequenceSize(values.size());
    for (const auto& value : values) { encoder.encodeByteString(value.asPtr()); }
  };
  encode(packages);
  encode(resolvedEdges);
  encode(selectedEdges);
  encode(crates);
  encode(crateEdges);
  return encoder.finish();
}

struct WireRange final {
  size_t begin;
  size_t end;
};

uint64_t readWireUint64(zc::ArrayPtr<const uint8_t> bytes, size_t offset) {
  ZC_REQUIRE(offset <= bytes.size() && bytes.size() - offset >= sizeof(uint64_t));
  uint64_t result = 0;
  for (size_t index = 0; index < sizeof(uint64_t); ++index) {
    result = (result << 8U) | bytes[offset + index];
  }
  return result;
}

void writeWireUint64(zc::ArrayPtr<uint8_t> bytes, size_t offset, uint64_t value) {
  ZC_REQUIRE(offset <= bytes.size() && bytes.size() - offset >= sizeof(uint64_t));
  for (size_t index = 0; index < sizeof(uint64_t); ++index) {
    const auto shift = static_cast<uint32_t>((sizeof(uint64_t) - index - 1) * 8);
    bytes[offset + index] = static_cast<uint8_t>(value >> shift);
  }
}

WireRange consumeWireByteString(zc::ArrayPtr<const uint8_t> bytes, size_t& cursor) {
  const size_t begin = cursor;
  const uint64_t size = readWireUint64(bytes, cursor);
  cursor += sizeof(uint64_t);
  ZC_REQUIRE(size <= bytes.size() - cursor);
  cursor += static_cast<size_t>(size);
  return WireRange{begin, cursor};
}

WireRange consumeWireSequence(zc::ArrayPtr<const uint8_t> bytes, size_t& cursor) {
  const size_t begin = cursor;
  const uint64_t count = readWireUint64(bytes, cursor);
  cursor += sizeof(uint64_t);
  ZC_REQUIRE(count <= UINT32_MAX);
  for (uint64_t index = 0; index < count; ++index) {
    static_cast<void>(consumeWireByteString(bytes, cursor));
  }
  return WireRange{begin, cursor};
}

zc::Vector<WireRange> completeContextFieldRanges(zc::ArrayPtr<const uint8_t> bytes) {
  constexpr zc::StringPtr domain = "zom.input.complete-compilation-context-authority"_zc;
  ZC_REQUIRE(bytes.size() > domain.size() && bytes.slice(0, domain.size()) == domain.asBytes());
  ZC_REQUIRE(bytes[domain.size()] == 0);
  size_t cursor = domain.size() + 1;
  zc::Vector<WireRange> result;
  for (size_t index = 0; index < 4; ++index) { result.add(consumeWireByteString(bytes, cursor)); }
  for (size_t index = 0; index < 6; ++index) { result.add(consumeWireSequence(bytes, cursor)); }
  result.add(consumeWireByteString(bytes, cursor));
  ZC_REQUIRE(bytes.size() - cursor == 32);
  result.add(WireRange{cursor, bytes.size()});
  return result;
}

zc::Array<uint8_t> swapFirstTwoWireSequenceElements(zc::ArrayPtr<const uint8_t> bytes,
                                                    WireRange sequence) {
  size_t cursor = sequence.begin;
  ZC_REQUIRE(readWireUint64(bytes, cursor) >= 2);
  cursor += sizeof(uint64_t);
  const auto first = consumeWireByteString(bytes, cursor);
  const auto second = consumeWireByteString(bytes, cursor);
  ZC_REQUIRE(cursor <= sequence.end);
  zc::Vector<uint8_t> result(bytes.size());
  result.addAll(bytes.slice(0, first.begin));
  result.addAll(bytes.slice(second.begin, second.end));
  result.addAll(bytes.slice(first.begin, first.end));
  result.addAll(bytes.slice(second.end, bytes.size()));
  return result.releaseAsArray();
}

struct StructurePayloadWire final {
  mutation::WireRange context;
  mutation::SequenceRange projectedCoreCrates;
  mutation::SequenceRange catalogs;
  mutation::SequenceRange dependencySites;
  mutation::SequenceRange ancestries;
  mutation::SequenceRange catalogBuckets;
  mutation::SequenceRange searchRoots;
  mutation::SequenceRange aliases;
  mutation::SequenceRange preludes;
};

StructurePayloadWire structurePayloadWire(zc::ArrayPtr<const uint8_t> bytes) {
  constexpr zc::StringPtr domain = "zom.query.input-transaction.module-structure"_zc;
  size_t cursor = mutation::payloadOffset(bytes, domain);
  const auto context = mutation::consumeByteString(bytes, cursor);
  const auto projectedCoreCrates = mutation::consumeSequence(bytes, cursor);
  const auto catalogs = mutation::consumeSequence(bytes, cursor);
  const auto dependencySites = mutation::consumeSequence(bytes, cursor);
  const auto ancestries = mutation::consumeSequence(bytes, cursor);
  const auto catalogBuckets = mutation::consumeSequence(bytes, cursor);
  const auto searchRoots = mutation::consumeSequence(bytes, cursor);
  const auto aliases = mutation::consumeSequence(bytes, cursor, 2);
  const auto preludes = mutation::consumeSequence(bytes, cursor, 2);
  ZC_REQUIRE(cursor == bytes.size());
  return StructurePayloadWire{context,    projectedCoreCrates, catalogs,    dependencySites,
                              ancestries, catalogBuckets,      searchRoots, aliases,
                              preludes};
}

zc::Vector<identity::ModulePathSegment> cloneModulePath(
    zc::ArrayPtr<const identity::ModulePathSegment> path) {
  zc::Vector<identity::ModulePathSegment> result(path.size());
  for (const auto& segment : path) { result.add(segment.clone()); }
  return result;
}

zc::Vector<identity::ModulePathSegment> modulePath(zc::StringPtr first,
                                                   zc::StringPtr second = nullptr,
                                                   zc::StringPtr third = nullptr) {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(scalar<identity::ModulePathSegment>(first));
  if (second != nullptr) { path.add(scalar<identity::ModulePathSegment>(second)); }
  if (third != nullptr) { path.add(scalar<identity::ModulePathSegment>(third)); }
  return path;
}

identity::SourceFileKey localSource(const identity::CrateKey& owner, zc::StringPtr name) {
  zc::Vector<identity::CanonicalPathSegment> path;
  path.add(scalar<identity::CanonicalPathSegment>(name));
  return identity::SourceFileKey::from(
      owner.clone(), identity::SourceOriginKey::localFile(
                         identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(path))));
}

identity::SourceFileKey coreSource(const identity::CrateKey& owner, zc::StringPtr name) {
  zc::Vector<identity::CanonicalPathSegment> path;
  if (name != "core.zom"_zc) { path.add(scalar<identity::CanonicalPathSegment>("core"_zc)); }
  path.add(scalar<identity::CanonicalPathSegment>(name));
  return identity::SourceFileKey::from(
      owner.clone(),
      identity::SourceOriginKey::coreFile(identity::ToolchainUnitKey::core(),
                                          identity::CanonicalRelativePath::from(zc::mv(path))));
}

zc::Vector<identity::ModuleKey> ancestry(identity::ModuleKey&& requester,
                                         zc::Maybe<identity::ModuleKey>&& parent = zc::none) {
  zc::Vector<identity::ModuleKey> result;
  result.add(zc::mv(requester));
  ZC_IF_SOME(value, parent) { result.add(zc::mv(value)); }
  return result;
}

incremental_binding_query::CompilationRootSetQueryKey contextRoots(const identity::CrateKey& core) {
  zc::Vector<incremental_binding_query::CompilationRootKey> roots;
  auto user = incremental_binding_query::CompilationRootKey::userPackage(package());
  auto toolchain = incremental_binding_query::CompilationRootKey::toolchainCore(core);
  roots.add(zc::mv(ZC_REQUIRE_NONNULL(user)));
  roots.add(zc::mv(ZC_REQUIRE_NONNULL(toolchain)));
  auto result = incremental_binding_query::CompilationRootSetQueryKey::from(zc::mv(roots));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

package::RegisteredTargetProfileName profileName() {
  auto value = package::RegisteredTargetProfileName::from("module-graph-test"_zc);
  return zc::mv(ZC_REQUIRE_NONNULL(value));
}

ir::TargetRegistrySnapshot targetRegistry() {
  zc::Vector<ir::CanonicalTargetFeature> backendFeatures;
  auto specification = ir::CanonicalTargetSpec::from(
      "x-v-o-e"_zc, "e-p:64:64"_zc, "generic"_zc, zc::mv(backendFeatures), "a"_zc,
      ir::BackendPanicStrategy::Unwind, ir::ObjectFormat::Elf);
  ZC_REQUIRE(specification != zc::none);
  zc::Vector<identity::TargetFeatureName> semanticFeatures;
  zc::Vector<ir::CanonicalTargetSpec> specifications;
  specifications.add(zc::mv(ZC_REQUIRE_NONNULL(specification)));
  auto profile =
      ir::RegisteredTargetProfileRecord::from(profileName(), tests::test_identity_detail::target(),
                                              zc::mv(semanticFeatures), zc::mv(specifications));
  ZC_REQUIRE(profile != zc::none);
  zc::Vector<ir::RegisteredTargetProfileRecord> profiles;
  profiles.add(zc::mv(ZC_REQUIRE_NONNULL(profile)));
  auto registry = ir::TargetRegistrySnapshot::from(profileName(), zc::mv(profiles));
  return zc::mv(ZC_REQUIRE_NONNULL(registry));
}

package::RegisteredTargetSelection targetSelection(const ir::TargetRegistrySnapshot& registry) {
  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  auto selected =
      ZC_REQUIRE_NONNULL(service).select(zc::none, package::PackagePanicStrategy::Unwind);
  return zc::mv(ZC_REQUIRE_NONNULL(selected));
}

package::VerifiedPackageCompilationRequest packageRequest(
    const ir::TargetRegistrySnapshot& registry) {
  zc::Vector<identity::CanonicalPathSegment> path;
  path.add(scalar<identity::CanonicalPathSegment>("src"_zc));
  path.add(scalar<identity::CanonicalPathSegment>("test.zom"_zc));
  zc::Vector<package::VerifiedCompilationRoot> roots;
  roots.add(package::VerifiedCompilationRoot::from(
      package(), identity::CrateTargetKind::Library, scalar<identity::TargetName>("test"_zc), 2026,
      false, identity::CanonicalRelativePath::from(zc::mv(path))));
  auto request = package::VerifiedPackageCompilationRequest::from(
      zc::mv(roots), targetSelection(registry), targetSelection(registry),
      package::SelectedLanguageOptions{}, package::PackageLockMode::PreferLocked);
  return zc::mv(ZC_REQUIRE_NONNULL(request));
}

zc::Array<uint8_t> encodedTargetSelection() {
  identity::CanonicalEncoder encoder;
  encoder.encodeDigest(digest(0x71));
  encoder.encodeByteString("module-graph-test"_zc.asBytes());
  tests::test_identity_detail::target().encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(package::PackagePanicStrategy::Unwind));
  return encoder.finish();
}

identity::source_query::CanonicalCompilationOptions compilationOptions() {
  auto host = encodedTargetSelection();
  auto targetValue = encodedTargetSelection();
  auto options = identity::source_query::CanonicalCompilationOptions::fromCanonicalSelections(
      zc::mv(host), zc::mv(targetValue), true, false, true);
  return zc::mv(ZC_REQUIRE_NONNULL(options));
}

CompleteCompilationContextAuthority completeAuthorityForSinglePackage(
    const package::VerifiedPackageCompilationRequest& request, const identity::CrateKey& user,
    const identity::CrateKey& core,
    const identity::source_query::CanonicalCompilationOptions& options,
    const source::core::CoreDistributionInputRecord& distribution) {
  auto rootSet = incremental_binding_query::PackageRootSetKey::fromVerified(request);
  ZC_REQUIRE(rootSet != zc::none);
  zc::Vector<zc::Array<uint8_t>> packages;
  packages.add(package().encode());
  zc::Vector<zc::Array<uint8_t>> noResolvedEdges;
  zc::Vector<zc::Array<uint8_t>> noSelectedEdges;
  zc::Vector<zc::Array<uint8_t>> crates;
  crates.add(user.encode());
  zc::Vector<zc::Array<uint8_t>> noCrateEdges;
  auto graphBytes =
      packageGraphBytes(zc::mv(packages), zc::mv(noResolvedEdges), zc::mv(noSelectedEdges),
                        zc::mv(crates), zc::mv(noCrateEdges));
  auto graph = incremental_binding_query::PackageGraphInput::decodeValue(graphBytes.asPtr());
  ZC_REQUIRE(graph != zc::none);

  zc::Vector<identity::CrateKey> userRoots;
  userRoots.add(user.clone());
  zc::Vector<identity::CrateKey> coreRoots;
  coreRoots.add(core.clone());
  zc::Vector<CompilationOptionsEntry> optionEntries;
  optionEntries.add(CompilationOptionsEntry::from(user.clone(), options.clone()));
  optionEntries.add(CompilationOptionsEntry::from(core.clone(), options.clone()));

  zc::Vector<binder::ModuleSearchRoot> userEnvironment;
  zc::Vector<identity::CanonicalPathSegment> workspacePath;
  userEnvironment.add(binder::ModuleSearchRoot::workspace(
      user.clone(), identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(workspacePath))));
  auto userSearch = incremental_module_resolution_query::CanonicalModuleSearchRoots::fromVerified(
      user, userEnvironment.asPtr());
  auto coreRoot = binder::ModuleSearchRoot::toolchainCore(core.clone(), distribution.digest());
  ZC_REQUIRE(userSearch != zc::none);
  ZC_REQUIRE(coreRoot != zc::none);
  zc::Vector<binder::ModuleSearchRoot> coreEnvironment;
  coreEnvironment.add(zc::mv(ZC_REQUIRE_NONNULL(coreRoot)));
  auto coreSearch = incremental_module_resolution_query::CanonicalModuleSearchRoots::fromVerified(
      core, coreEnvironment.asPtr());
  ZC_REQUIRE(coreSearch != zc::none);
  zc::Vector<ModuleSearchRootsEntry> searchEntries;
  searchEntries.add(
      ModuleSearchRootsEntry::from(user.clone(), zc::mv(ZC_REQUIRE_NONNULL(userSearch))));
  searchEntries.add(
      ModuleSearchRootsEntry::from(core.clone(), zc::mv(ZC_REQUIRE_NONNULL(coreSearch))));

  const CompleteCompilationContextSources sources{
      request,           ZC_REQUIRE_NONNULL(rootSet), ZC_REQUIRE_NONNULL(graph), userRoots.asPtr(),
      coreRoots.asPtr(), optionEntries.asPtr(),       searchEntries.asPtr(),     distribution,
  };
  auto authority = CompleteCompilationContextAuthority::fromVerified(sources);
  return zc::mv(ZC_REQUIRE_NONNULL(authority));
}

struct ParsedSource final {
  explicit ParsedSource(zc::ArrayPtr<const uint8_t> text)
      : sources(zc::heap<source::SourceManager>()),
        buffer(sources->addMemBufferCopy(text, "module.zom")) {
    diagnostics::SourceDiagnosticDraftBuffer facts(*sources, buffer);
    parser::Parser parser(*sources, facts, options, strings, buffer);
    auto parsed = parser.parse();
    ZC_REQUIRE(parsed != zc::none);
    tree = zc::mv(ZC_REQUIRE_NONNULL(parsed));
    ZC_REQUIRE(!facts.hasErrors());
    tokens = parser.takeTokenSnapshot();
    ZC_REQUIRE(tokens != zc::none);
  }

  identity::ImmutableSourceSnapshot snapshot(const identity::SourceFileKey& source) const {
    auto value = identity::ImmutableSourceSnapshot::from(
        source.clone(), zc::heapArray<uint8_t>(sources->getEntireTextForBuffer(buffer)));
    return zc::mv(ZC_REQUIRE_NONNULL(value));
  }

  binder::VerifiedParsedModule verify(identity::SemanticContextBrand context,
                                      const identity::SemanticIdentityRegistrySet& registries,
                                      const identity::SourceFileKey& source) {
    auto retained = zc::mv(ZC_REQUIRE_NONNULL(tokens));
    return binder::test::requireVerifiedParsedSource(
        context, registries, snapshot(source), *sources, buffer, zc::mv(retained), zc::mv(tree));
  }

  zc::Own<source::SourceManager> sources;
  basic::LangOptions options;
  basic::StringPool strings;
  source::BufferId buffer;
  ast::Tree tree;
  zc::Maybe<parser::ParsedTokenSnapshot> tokens;
};

zc::Maybe<VerifiedModuleGraphInputTransaction> preparedTransaction(
    const VerifiedModuleGraphInputLedger& prior, bool includeMarker, bool withCycle = false,
    bool omitCoreProjection = false,
    zc::ArrayPtr<const VerifiedModuleGraphInputPayload> rejectedPayloads = nullptr) {
  auto userCrate = crate();
  auto core = coreCrate();
  auto userModule = module(userCrate, "test"_zc);
  auto coreRoot = module(core, "core"_zc);
  auto prelude = module(core, "core"_zc, "prelude"_zc);
  auto marker = module(core, "core"_zc, "marker"_zc);
  auto userFile = localSource(userCrate, "test.zom"_zc);
  auto coreFile = coreSource(core, "core.zom"_zc);
  auto preludeFile = coreSource(core, "prelude.zom"_zc);
  auto markerFile = coreSource(core, "marker.zom"_zc);
  auto distribution = core_library_test::admittedCoreDistribution();
  ZC_REQUIRE(distribution.snapshots().size() == 3);

  auto targetRegistryValue = targetRegistry();
  auto request = packageRequest(targetRegistryValue);
  auto verifiedOptions = identity::source_query::CanonicalCompilationOptions::fromVerified(request);
  ZC_REQUIRE(verifiedOptions != zc::none);
  auto options = zc::mv(ZC_REQUIRE_NONNULL(verifiedOptions));
  auto acceptedDistribution = source::core::initialCoreDistributionInput();
  ZC_REQUIRE(acceptedDistribution != zc::none);
  auto contextAuthority = completeAuthorityForSinglePackage(
      request, userCrate, core, options, ZC_REQUIRE_NONNULL(acceptedDistribution));
  zc::Vector<identity::CrateKey> consumers;
  consumers.add(userCrate.clone());
  auto preparedCore = core_library_query::VerifiedCoreDistributionInputTransaction::prepare(
      query::DatabaseRevision(), distribution, request, zc::mv(contextAuthority), options,
      consumers.asPtr());
  ZC_REQUIRE(preparedCore != zc::none);
  auto coreInputs = zc::mv(ZC_REQUIRE_NONNULL(preparedCore));
  ZC_REQUIRE(coreInputs.projections().size() == 1);
  ZC_REQUIRE(coreInputs.projections()[0].crate().encode().asPtr() == core.encode().asPtr());

  ParsedSource userParsedSource("module test;\n"_zc.asBytes());
  ParsedSource coreParsedSource(distribution.snapshots()[0].bytes());
  ParsedSource markerParsedSource(distribution.snapshots()[1].bytes());
  ParsedSource preludeParsedSource(distribution.snapshots()[2].bytes());
  identity::SemanticContextFactory contextFactory;
  auto context = contextFactory.issue();
  ZC_REQUIRE(context != zc::none);
  auto registryResult =
      identity::SemanticIdentityRegistrySet::create(contextFactory, ZC_REQUIRE_NONNULL(context));
  ZC_REQUIRE(registryResult != zc::none);
  auto registries = zc::mv(ZC_REQUIRE_NONNULL(registryResult));
  ZC_REQUIRE(registries.collectCompilationUnit(identity::CompilationUnitIdentity::userPackage(
                 package())) == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectCompilationUnit(identity::CompilationUnitIdentity::toolchain(
                 identity::ToolchainUnitKey::core())) == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeCompilationUnits() == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectCrate(userCrate.clone()) == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectCrate(core.clone()) == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeCrates() == identity::FrozenRegistryFailure::None);
  auto userSnapshot = userParsedSource.snapshot(userFile);
  auto coreSnapshot = coreParsedSource.snapshot(coreFile);
  auto markerSnapshot = markerParsedSource.snapshot(markerFile);
  auto preludeSnapshot = preludeParsedSource.snapshot(preludeFile);
  ZC_REQUIRE(registries.collectSourceFile(userSnapshot.clone()) ==
             identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectSourceFile(coreSnapshot.clone()) ==
             identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectSourceFile(markerSnapshot.clone()) ==
             identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectSourceFile(preludeSnapshot.clone()) ==
             identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeSourceFiles() == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectModule(userModule.clone()) == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectModule(coreRoot.clone()) == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectModule(marker.clone()) == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectModule(prelude.clone()) == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeModules() == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeStableIdentities() == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeGenericParameters() == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeCallableParameters() == identity::FrozenRegistryFailure::None);

  zc::Vector<binder::VerifiedParsedModule> parsedModules(4);
  parsedModules.add(userParsedSource.verify(ZC_REQUIRE_NONNULL(context), registries, userFile));
  parsedModules.add(coreParsedSource.verify(ZC_REQUIRE_NONNULL(context), registries, coreFile));
  parsedModules.add(markerParsedSource.verify(ZC_REQUIRE_NONNULL(context), registries, markerFile));
  parsedModules.add(
      preludeParsedSource.verify(ZC_REQUIRE_NONNULL(context), registries, preludeFile));
  const identity::ModuleKey* moduleKeys[] = {&userModule, &coreRoot, &marker, &prelude};
  const identity::SourceFileKey* sourceKeys[] = {&userFile, &coreFile, &markerFile, &preludeFile};
  zc::Vector<binder::ParsedModuleGraphInput> parsedInputs(4);
  for (size_t index = 0; index < 4; ++index) {
    auto handle = registries.modules().find(*moduleKeys[index]);
    ZC_REQUIRE(handle != zc::none);
    parsedInputs.add(
        binder::ParsedModuleGraphInput{ZC_REQUIRE_NONNULL(handle), parsedModules[index]});
  }

  zc::Vector<binder::ModuleSearchRoot> environmentRoots;
  zc::Vector<identity::CanonicalPathSegment> workspacePath;
  environmentRoots.add(binder::ModuleSearchRoot::workspace(
      userCrate.clone(), identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(workspacePath))));
  for (const auto& root : coreInputs.projections()[0].searchRoots().roots()) {
    environmentRoots.add(root.clone());
  }
  zc::Vector<binder::ModuleSourceSnapshotRevision> sourceRevisions;
  for (const auto& snapshot : registries.sourceSnapshots()) {
    sourceRevisions.add(
        binder::ModuleSourceSnapshotRevision(snapshot.source().clone(), snapshot.contentDigest()));
  }
  zc::Vector<binder::GeneratedModuleSourceRevision> generatedRevisions;
  zc::Vector<binder::ModuleDependencyAliasRoot> resolverAliases;
  resolverAliases.add(binder::ModuleDependencyAliasRoot(
      userCrate.clone(), scalar<identity::DependencyAlias>("core"_zc), coreRoot.clone()));
  zc::Vector<binder::RequesterModuleAncestryCandidate> ancestryCandidates;
  const auto addAncestry = [&](const identity::ModuleKey& requester,
                               zc::Maybe<const identity::ModuleKey&> parent) {
    auto chain = ancestry(requester.clone());
    ZC_IF_SOME(value, parent) { chain.add(value.clone()); }
    ancestryCandidates.add(
        binder::RequesterModuleAncestryCandidate(requester.clone(), zc::mv(chain)));
  };
  addAncestry(userModule, zc::none);
  addAncestry(coreRoot, zc::none);
  addAncestry(marker, coreRoot);
  addAncestry(prelude, coreRoot);
  zc::Vector<binder::StructuralModuleCatalogEntry> resolverCatalog;
  for (size_t index = 0; index < 4; ++index) {
    auto handle = registries.modules().find(*moduleKeys[index]);
    resolverCatalog.add(binder::StructuralModuleCatalogEntry(
        moduleKeys[index]->clone(), ZC_REQUIRE_NONNULL(handle), sourceKeys[index]->clone()));
  }
  auto frozenResolver = binder::StructuralModuleResolver::freeze(
      ZC_REQUIRE_NONNULL(context), registries,
      binder::ModuleResolutionEnvironmentRecord(zc::mv(environmentRoots), zc::mv(sourceRevisions),
                                                zc::mv(generatedRevisions), zc::mv(resolverAliases),
                                                zc::mv(ancestryCandidates)),
      zc::mv(resolverCatalog));
  ZC_REQUIRE(frozenResolver.is<binder::StructuralModuleResolver>());
  auto resolver = zc::mv(frozenResolver.get<binder::StructuralModuleResolver>());

  zc::Vector<binder::ModuleDependencyRequest> requests;
  zc::Vector<DetachedModuleDependencySiteSet> sites;
  for (size_t index = 0; index < parsedInputs.size(); ++index) {
    auto derived = binder::ModuleDependencyRequestDeriver::derive(
        parsedInputs[index].module, parsedInputs[index].parsedModule, resolver);
    ZC_REQUIRE(derived.is<zc::Vector<binder::ModuleDependencyRequest>>());
    auto moduleRequests = zc::mv(derived.get<zc::Vector<binder::ModuleDependencyRequest>>());
    zc::Vector<DetachedModuleDependencySite> detached;
    for (auto& request : moduleRequests) {
      DetachedModuleDependencySiteKind kind = DetachedModuleDependencySiteKind::Import;
      if (request.kind() == identity::ModuleDependencyKind::ForeignReexport) {
        kind = DetachedModuleDependencySiteKind::ForeignReexport;
      } else if (request.kind() == identity::ModuleDependencyKind::ModuleAlias) {
        kind = DetachedModuleDependencySiteKind::ModuleAlias;
      }
      for (const auto& syntax : request.syntaxSites()) {
        auto site = DetachedModuleDependencySite::from(
            kind, cloneModulePath(request.normalizedPath()), syntax.schemaPreorderOrdinal);
        detached.add(zc::mv(ZC_REQUIRE_NONNULL(site)));
      }
      requests.add(zc::mv(request));
    }
    if (withCycle && sameModule(*moduleKeys[index], coreRoot)) {
      zc::Vector<identity::ModulePathSegment> inventedPath;
      inventedPath.add(scalar<identity::ModulePathSegment>("core"_zc));
      inventedPath.add(scalar<identity::ModulePathSegment>("prelude"_zc));
      auto invented = DetachedModuleDependencySite::from(DetachedModuleDependencySiteKind::Import,
                                                         zc::mv(inventedPath), 999);
      detached.add(zc::mv(ZC_REQUIRE_NONNULL(invented)));
    }
    auto siteSet = DetachedModuleDependencySiteSet::from(
        moduleKeys[index]->clone(), sourceKeys[index]->clone(),
        parsedInputs[index].parsedModule.contentDigest(), zc::mv(detached));
    if (includeMarker || !sameModule(*moduleKeys[index], marker)) {
      sites.add(zc::mv(ZC_REQUIRE_NONNULL(siteSet)));
    }
  }

  zc::Vector<SelectedModuleRecord> userEntries;
  userEntries.add(SelectedModuleRecord(userModule.clone(), userFile.clone()));
  auto userCatalog = SelectedModuleCatalog::from(userCrate.clone(), zc::mv(userEntries));
  ZC_REQUIRE(userCatalog != zc::none);

  zc::Vector<SelectedModuleRecord> coreEntries;
  coreEntries.add(SelectedModuleRecord(coreRoot.clone(), coreFile.clone()));
  coreEntries.add(SelectedModuleRecord(prelude.clone(), preludeFile.clone()));
  if (includeMarker) { coreEntries.add(SelectedModuleRecord(marker.clone(), markerFile.clone())); }
  auto coreCatalog = SelectedModuleCatalog::from(core.clone(), zc::mv(coreEntries));
  ZC_REQUIRE(coreCatalog != zc::none);

  zc::Vector<SelectedModuleCatalog> catalogs;
  catalogs.add(zc::mv(ZC_REQUIRE_NONNULL(userCatalog)));
  catalogs.add(zc::mv(ZC_REQUIRE_NONNULL(coreCatalog)));

  zc::Vector<identity::RequesterModuleAncestry> ancestries;
  for (const auto& authorityAncestry : resolver.requesterAncestryInputs()) {
    if (includeMarker || !sameModule(authorityAncestry.requester(), marker)) {
      ancestries.add(authorityAncestry.clone());
    }
  }

  zc::TreeMap<zc::String, incremental_module_resolution_query::CanonicalModuleCatalogBucket>
      bucketMap;
  const auto addBucket = [&](const identity::CrateKey& owner,
                             zc::ArrayPtr<const identity::ModulePathSegment> path) {
    auto bucket = resolver.catalogPathBucketInput(owner, path);
    ZC_REQUIRE(bucket != zc::none);
    auto canonical =
        incremental_module_resolution_query::CanonicalModuleCatalogBucket::fromVerified(
            ZC_REQUIRE_NONNULL(bucket));
    auto encoded = canonical.key().encode();
    auto key = zc::encodeHex(encoded.asPtr());
    if (bucketMap.find(key) == zc::none) { bucketMap.insert(zc::mv(key), zc::mv(canonical)); }
  };
  for (const auto& entry : resolver.catalog()) { addBucket(entry.key.crate(), entry.key.path()); }
  for (const auto& request : requests) {
    zc::Maybe<const identity::RequesterModuleAncestry&> selectedAncestry;
    for (const auto& candidate : resolver.requesterAncestryInputs()) {
      if (sameModule(candidate.requester(), request.key().requester())) {
        selectedAncestry = candidate;
      }
    }
    ZC_REQUIRE(selectedAncestry != zc::none);
    for (const auto& ancestor : ZC_REQUIRE_NONNULL(selectedAncestry).ancestry()) {
      auto path = cloneModulePath(ancestor.path());
      for (const auto& segment : request.normalizedPath()) { path.add(segment.clone()); }
      addBucket(request.key().requester().crate(), path.asPtr());
    }
    addBucket(request.key().requester().crate(), request.normalizedPath());
    ZC_IF_SOME(alias, request.key().dependencyAlias()) {
      for (const auto& root : resolver.dependencyAliasRootInputs()) {
        if (!sameCrate(root.requester, request.key().requester().crate()) ||
            root.alias.text() != alias) {
          continue;
        }
        auto path = cloneModulePath(root.target.path());
        for (size_t index = 1; index < request.normalizedPath().size(); ++index) {
          path.add(request.normalizedPath()[index].clone());
        }
        addBucket(root.target.crate(), path.asPtr());
      }
    }
  }
  addBucket(core, prelude.path());
  zc::Vector<incremental_module_resolution_query::CanonicalModuleCatalogBucket> buckets(
      bucketMap.size());
  for (auto& entry : bucketMap) { buckets.add(zc::mv(entry.value)); }

  zc::Vector<incremental_module_resolution_query::CanonicalModuleSearchRoots> searchRoots;
  auto projectedUserRoots =
      incremental_module_resolution_query::CanonicalModuleSearchRoots::fromVerified(
          userCrate, resolver.searchRootInputs());
  ZC_REQUIRE(projectedUserRoots != zc::none);
  searchRoots.add(zc::mv(ZC_REQUIRE_NONNULL(projectedUserRoots)));
  searchRoots.add(coreInputs.projections()[0].searchRoots().clone());
  zc::Vector<ConfiguredDependencyAlias> aliases;
  zc::TreeMap<zc::String, ConfiguredDependencyAlias> aliasMap;
  for (const auto& request : requests) {
    auto alias = identity::DependencyAlias::fromCanonical(request.normalizedPath().front().text());
    if (alias == zc::none) { continue; }
    auto aliasKey = incremental_module_resolution_query::DependencyAliasRootQueryKey::from(
        request.key().requester().crate().clone(), ZC_REQUIRE_NONNULL(alias).clone());
    ZC_REQUIRE(aliasKey != zc::none);
    auto target = incremental_module_resolution_query::ExplicitModuleTarget::absent();
    for (const auto& root : resolver.dependencyAliasRootInputs()) {
      if (sameCrate(root.requester, request.key().requester().crate()) &&
          root.alias.text() == ZC_REQUIRE_NONNULL(alias).text()) {
        target =
            incremental_module_resolution_query::ExplicitModuleTarget::present(root.target.clone());
      }
    }
    auto encoded = ZC_REQUIRE_NONNULL(aliasKey).encode();
    auto key = zc::encodeHex(encoded.asPtr());
    if (aliasMap.find(key) == zc::none) {
      aliasMap.insert(zc::mv(key), ConfiguredDependencyAlias{zc::mv(ZC_REQUIRE_NONNULL(aliasKey)),
                                                             zc::mv(target)});
    }
  }
  for (auto& entry : aliasMap) { aliases.add(zc::mv(entry.value)); }
  zc::Vector<ConfiguredCratePrelude> preludes;
  preludes.add(ConfiguredCratePrelude{
      userCrate.clone(),
      incremental_module_resolution_query::ExplicitModuleTarget::present(prelude.clone())});
  preludes.add(ConfiguredCratePrelude{
      core.clone(), incremental_module_resolution_query::ExplicitModuleTarget::absent()});
  zc::Vector<identity::CrateKey> projectedCore;
  if (!omitCoreProjection) { projectedCore.add(core.clone()); }

  const ModuleGraphInputTransactionAuthority authority{request, coreInputs, resolver, registries,
                                                       parsedInputs.asPtr()};
  for (const auto& candidate : rejectedPayloads) {
    ZC_EXPECT(!VerifiedModuleGraphInputVerifier::verify(authority, candidate));
  }
  return VerifiedModuleGraphInputTransaction::prepare(
      authority, query::DatabaseRevision(), contextRoots(core), zc::mv(projectedCore),
      zc::mv(catalogs), zc::mv(sites), zc::mv(ancestries), zc::mv(buckets), zc::mv(searchRoots),
      zc::mv(aliases), zc::mv(preludes), prior);
}

class QueryTestSemanticContextResources final : public query::SemanticContextCapabilityResources {};

query::QueryDatabase database(basic::ThreadPool& scheduler) {
  auto resources = zc::heap<QueryTestSemanticContextResources>();
  auto arena = zc::arc<query::SemanticContextCapabilityArena>(zc::mv(resources));
  query::QueryDatabase result(scheduler, query::productionQueryDescriptorInventory(),
                              zc::mv(arena));
  ZC_REQUIRE(registerModuleGraphQueries(result));
  ZC_REQUIRE(
      incremental_module_resolution_query::registerIncrementalModuleResolutionQueries(result));
  ZC_REQUIRE(result.registerDescriptor<incremental_binding_query::UserPackageActiveSourcesInput>()
                 .isRegistered());
  ZC_REQUIRE(
      result.registerDescriptor<incremental_binding_query::ActiveSourcesQuery>().isRegistered());
  ZC_REQUIRE(
      result.registerDescriptor<identity::source_query::SourceSnapshotInput>().isRegistered());
  ZC_REQUIRE(core_library_query::registerCoreLibraryQueryProvider(result));
  ZC_REQUIRE(
      result.registerDescriptor<incremental_binding_query::ActiveCratesQuery>().isRegistered());
  ZC_REQUIRE(registerStableModuleGraphQueries(result));
  return result;
}

void stageSource(query::InputTransaction& transaction, const identity::SourceFileKey& source) {
  auto immutable = identity::ImmutableSourceSnapshot::from(
      source.clone(), zc::heapArray<uint8_t>("module test;\n"_zc.asBytes()));
  ZC_REQUIRE(immutable != zc::none);
  auto key = identity::source_query::StableSourceQueryKey::fromVerified(source);
  auto snapshot =
      identity::source_query::CanonicalSourceSnapshot::fromVerified(ZC_REQUIRE_NONNULL(immutable));
  ZC_REQUIRE(key != zc::none);
  ZC_REQUIRE(snapshot != zc::none);
  ZC_REQUIRE(transaction
                 .set<identity::source_query::SourceSnapshotInput>(ZC_REQUIRE_NONNULL(key),
                                                                   ZC_REQUIRE_NONNULL(snapshot))
                 .isApplied());
}

void stageUserActiveSource(query::InputTransaction& transaction,
                           const identity::SourceFileKey& source) {
  auto key = incremental_binding_query::StableCrateQueryKey::fromVerified(source.crate());
  auto stable = identity::source_query::StableSourceQueryKey::fromVerified(source);
  ZC_REQUIRE(key != zc::none);
  ZC_REQUIRE(stable != zc::none);
  zc::Vector<identity::source_query::StableSourceQueryKey> sources;
  sources.add(zc::mv(ZC_REQUIRE_NONNULL(stable)));
  auto value = incremental_binding_query::CanonicalSourceSet::from(zc::mv(sources));
  ZC_REQUIRE(value != zc::none);
  ZC_REQUIRE(transaction
                 .set<incremental_binding_query::UserPackageActiveSourcesInput>(
                     ZC_REQUIRE_NONNULL(key), ZC_REQUIRE_NONNULL(value))
                 .isApplied());
}

void stageCoreDistribution(query::InputTransaction& transaction) {
  auto distribution = core_library_test::admittedCoreDistribution();
  auto record = source::core::CoreDistributionInputRecord::from(
      distribution.record().clone(), distribution.distributionDigest(),
      distribution.policyTemplate().clone());
  ZC_REQUIRE(record != zc::none);
  ZC_REQUIRE(transaction
                 .set<core_library_query::CoreDistributionInput>(identity::ToolchainUnitKey::core(),
                                                                 ZC_REQUIRE_NONNULL(record))
                 .isApplied());
  auto core = coreCrate();
  for (const auto& admitted : distribution.snapshots()) {
    auto source = identity::SourceFileKey::from(
        core.clone(), identity::SourceOriginKey::coreFile(identity::ToolchainUnitKey::core(),
                                                          admitted.path().clone()));
    auto immutable = identity::ImmutableSourceSnapshot::from(
        source.clone(), zc::heapArray<uint8_t>(admitted.bytes()));
    auto key = identity::source_query::StableSourceQueryKey::fromVerified(source);
    auto snapshot = identity::source_query::CanonicalSourceSnapshot::fromVerified(
        ZC_REQUIRE_NONNULL(immutable));
    ZC_REQUIRE(key != zc::none);
    ZC_REQUIRE(snapshot != zc::none);
    ZC_REQUIRE(transaction
                   .set<identity::source_query::SourceSnapshotInput>(ZC_REQUIRE_NONNULL(key),
                                                                     ZC_REQUIRE_NONNULL(snapshot))
                   .isApplied());
  }
}

void stageBucket(query::InputTransaction& transaction, const identity::CrateKey& owner,
                 zc::Vector<identity::ModulePathSegment>&& path,
                 zc::Maybe<identity::ModuleKey>&& selected = zc::none) {
  auto key = identity::ModuleCatalogPathBucketKey::from(owner.clone(), zc::mv(path));
  ZC_REQUIRE(key != zc::none);
  auto bucket = selected == zc::none
                    ? identity::ModuleCatalogPathBucket::absent(ZC_REQUIRE_NONNULL(key).clone())
                    : identity::ModuleCatalogPathBucket::present(
                          ZC_REQUIRE_NONNULL(key).clone(), zc::mv(ZC_REQUIRE_NONNULL(selected)));
  ZC_REQUIRE(bucket != zc::none);
  auto canonical = incremental_module_resolution_query::CanonicalModuleCatalogBucket::fromVerified(
      ZC_REQUIRE_NONNULL(bucket));
  ZC_REQUIRE(transaction
                 .set<incremental_module_resolution_query::ModuleCatalogPathBucketInput>(
                     ZC_REQUIRE_NONNULL(key), canonical)
                 .isApplied());
}

}  // namespace

ZC_TEST("Selected module catalog has an exact canonical codec") {
  auto owner = crate();
  auto selectedModule = module(owner, "test"_zc);
  auto selectedSource = localSource(owner, "test.zom"_zc);
  zc::Vector<SelectedModuleRecord> entries;
  entries.add(SelectedModuleRecord(selectedModule.clone(), selectedSource.clone()));
  auto catalog = SelectedModuleCatalog::from(owner.clone(), zc::mv(entries));
  ZC_REQUIRE(catalog != zc::none);

  auto encoded = SelectedModuleCatalogInput::encodeValue(ZC_REQUIRE_NONNULL(catalog));
  auto decoded = SelectedModuleCatalogInput::decodeValue(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded).encodeCanonical().asPtr() == encoded.asPtr());
  ZC_EXPECT(SelectedModuleCatalogInput::decodeValue(encoded.slice(0, encoded.size() - 1)) ==
            zc::none);

  auto foreign = coreCrate();
  zc::Vector<SelectedModuleRecord> foreignEntries;
  foreignEntries.add(SelectedModuleRecord(selectedModule.clone(), selectedSource.clone()));
  ZC_EXPECT(SelectedModuleCatalog::from(zc::mv(foreign), zc::mv(foreignEntries)) == zc::none);
}

ZC_TEST("Detached dependency sites reject duplicate stable ordinals") {
  auto owner = crate();
  auto selectedModule = module(owner, "test"_zc);
  auto selectedSource = localSource(owner, "test.zom"_zc);
  zc::Vector<identity::ModulePathSegment> firstPath;
  firstPath.add(scalar<identity::ModulePathSegment>("one"_zc));
  zc::Vector<identity::ModulePathSegment> secondPath;
  secondPath.add(scalar<identity::ModulePathSegment>("two"_zc));
  auto first = DetachedModuleDependencySite::from(DetachedModuleDependencySiteKind::Import,
                                                  zc::mv(firstPath), 7);
  auto second = DetachedModuleDependencySite::from(DetachedModuleDependencySiteKind::ModuleAlias,
                                                   zc::mv(secondPath), 7);
  ZC_REQUIRE(first != zc::none);
  ZC_REQUIRE(second != zc::none);
  zc::Vector<DetachedModuleDependencySite> sites;
  sites.add(zc::mv(ZC_REQUIRE_NONNULL(first)));
  sites.add(zc::mv(ZC_REQUIRE_NONNULL(second)));
  ZC_EXPECT(DetachedModuleDependencySiteSet::from(zc::mv(selectedModule), zc::mv(selectedSource),
                                                  digest(0x62), zc::mv(sites)) == zc::none);
}

ZC_TEST("Module graph registration installs final dependency provenance") {
  basic::ThreadPool scheduler(2);
  auto queries = database(scheduler);
  auto result = queries.snapshot().getCapability<ModuleDependencyProvenanceQuery>(
      tests::test_identity_detail::module());
  ZC_REQUIRE(result.isRuntimeRejected());
  ZC_EXPECT(result.runtimeFailure() == query::QueryRuntimeFailure::FinalSealRequired);
}

ZC_TEST("Module graph input transaction commits its complete authority exactly once") {
  basic::ThreadPool scheduler(2);
  auto queries = database(scheduler);
  auto empty = VerifiedModuleGraphInputLedger::empty();
  auto first = preparedTransaction(empty, true);
  ZC_REQUIRE(first != zc::none);
  ZC_REQUIRE(ZC_REQUIRE_NONNULL(first).commit(queries).isCommitted());
  ZC_EXPECT(!ZC_REQUIRE_NONNULL(first).commit(queries).isCommitted());

  auto marker = module(coreCrate(), "core"_zc, "marker"_zc);
  auto present = queries.snapshot().probeInput<ModuleDependencySiteInput>(marker);
  ZC_REQUIRE(!present.isRuntimeFailure());
  ZC_REQUIRE(present.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(first).nextLedger().entries().size() != 0);
}

ZC_TEST("SessionInputTransactionTest.StructurePayloadRejectsCoverageAndOrderingMutations") {
  auto prior = VerifiedModuleGraphInputLedger::empty();
  auto prepared = preparedTransaction(prior, true);
  ZC_REQUIRE(prepared != zc::none);
  const auto& payload = ZC_REQUIRE_NONNULL(prepared).payload();
  auto encoded = payload.encodeCanonical();
  auto decoded = VerifiedModuleGraphInputPayload::decodeCanonical(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded) == payload);
  const auto wire = structurePayloadWire(encoded.asPtr());
  ZC_REQUIRE(wire.catalogs.count >= 2);
  ZC_REQUIRE(wire.dependencySites.count != 0);
  ZC_REQUIRE(wire.ancestries.count != 0);
  ZC_REQUIRE(wire.catalogBuckets.count != 0);
  ZC_REQUIRE(wire.searchRoots.count != 0);
  ZC_REQUIRE(wire.preludes.count != 0);
  zc::Vector<VerifiedModuleGraphInputPayload> authorityMutations;
  const auto retainForVerifier = [&](zc::Array<uint8_t>&& bytes) {
    auto candidate = VerifiedModuleGraphInputPayload::decodeCanonical(bytes.asPtr());
    if (candidate != zc::none) { authorityMutations.add(zc::mv(ZC_REQUIRE_NONNULL(candidate))); }
  };

  auto wrongDomain = mutation::flipByte(encoded.asPtr(), 0);
  ZC_EXPECT(VerifiedModuleGraphInputPayload::decodeCanonical(wrongDomain.asPtr()) == zc::none);

  retainForVerifier(mutation::flipPayloadByte(encoded.asPtr(), wire.context));
  retainForVerifier(mutation::flipPayloadByte(
      encoded.asPtr(), mutation::sequenceField(encoded.asPtr(), wire.projectedCoreCrates, 0, 0)));
  retainForVerifier(mutation::flipPayloadByte(
      encoded.asPtr(), mutation::sequenceField(encoded.asPtr(), wire.catalogs, 0, 0)));
  retainForVerifier(mutation::flipPayloadByte(
      encoded.asPtr(), mutation::sequenceField(encoded.asPtr(), wire.dependencySites, 0, 0)));
  retainForVerifier(mutation::flipPayloadByte(
      encoded.asPtr(), mutation::sequenceField(encoded.asPtr(), wire.ancestries, 0, 0)));
  retainForVerifier(mutation::flipPayloadByte(
      encoded.asPtr(), mutation::sequenceField(encoded.asPtr(), wire.catalogBuckets, 0, 0)));
  retainForVerifier(mutation::flipPayloadByte(
      encoded.asPtr(), mutation::sequenceField(encoded.asPtr(), wire.searchRoots, 0, 0)));
  auto changedAlias =
      wire.aliases.count == 0
          ? mutation::setSequenceCount(encoded.asPtr(), wire.aliases, 1)
          : mutation::flipPayloadByte(encoded.asPtr(),
                                      mutation::sequenceField(encoded.asPtr(), wire.aliases, 0, 0));
  retainForVerifier(zc::mv(changedAlias));
  retainForVerifier(mutation::flipPayloadByte(
      encoded.asPtr(), mutation::sequenceField(encoded.asPtr(), wire.preludes, 0, 1)));

  auto duplicate = mutation::duplicateFirstElement(encoded.asPtr(), wire.catalogs);
  ZC_EXPECT(VerifiedModuleGraphInputPayload::decodeCanonical(duplicate.asPtr()) == zc::none);
  auto reordered = mutation::swapFirstTwoElements(encoded.asPtr(), wire.catalogs);
  ZC_EXPECT(VerifiedModuleGraphInputPayload::decodeCanonical(reordered.asPtr()) == zc::none);
  auto changedCoverage = mutation::removeFirstElement(encoded.asPtr(), wire.dependencySites);
  retainForVerifier(zc::mv(changedCoverage));
  auto excessiveCount = mutation::setSequenceCount(encoded.asPtr(), wire.catalogs, UINT64_MAX);
  ZC_EXPECT(VerifiedModuleGraphInputPayload::decodeCanonical(excessiveCount.asPtr()) == zc::none);
  auto excessiveBytes = mutation::setByteStringSize(encoded.asPtr(), wire.context, UINT64_MAX);
  ZC_EXPECT(VerifiedModuleGraphInputPayload::decodeCanonical(excessiveBytes.asPtr()) == zc::none);

  auto trailing = mutation::withTrailingByte(encoded.asPtr());
  ZC_EXPECT(VerifiedModuleGraphInputPayload::decodeCanonical(trailing.asPtr()) == zc::none);
  ZC_EXPECT(VerifiedModuleGraphInputPayload::decodeCanonical(
                encoded.asPtr().slice(0, encoded.size() - 1)) == zc::none);

  ZC_REQUIRE(authorityMutations.size() != 0);
  auto independentlyChecked =
      preparedTransaction(prior, true, false, false, authorityMutations.asPtr());
  ZC_REQUIRE(independentlyChecked != zc::none);
}

ZC_TEST("Independent module graph input verifier rejects incomplete core roots") {
  auto prior = VerifiedModuleGraphInputLedger::empty();
  ZC_EXPECT(preparedTransaction(prior, true, false, true) == zc::none);
}

ZC_TEST("Independent module graph input verifier rejects an omitted module family") {
  auto prior = VerifiedModuleGraphInputLedger::empty();
  ZC_EXPECT(preparedTransaction(prior, false) == zc::none);
}

ZC_TEST("Derived module queries project the sole catalog through prelude resolution") {
  basic::ThreadPool scheduler(2);
  auto queries = database(scheduler);
  auto empty = VerifiedModuleGraphInputLedger::empty();
  auto graphInputs = preparedTransaction(empty, true);
  ZC_REQUIRE(graphInputs != zc::none);
  ZC_REQUIRE(ZC_REQUIRE_NONNULL(graphInputs).commit(queries).isCommitted());

  auto userCrate = crate();
  auto userModule = module(userCrate, "test"_zc);
  auto userSource = localSource(userCrate, "test.zom"_zc);
  auto sourceInputs = queries.beginInputTransaction(queries.snapshot().revision());
  ZC_REQUIRE(sourceInputs.isOpened());
  auto sourceTransaction = zc::mv(sourceInputs).takeTransaction();
  stageSource(sourceTransaction, userSource);
  stageUserActiveSource(sourceTransaction, userSource);
  ZC_REQUIRE(sourceTransaction.commit().isCommitted());

  auto snapshot = queries.snapshot();
  auto selected = snapshot.get<SelectedModuleSourceQuery>(userModule);
  ZC_REQUIRE(!selected.isRuntimeFailure());
  ZC_REQUIRE(selected.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(selected.value().sameAs(userSource));

  auto active = snapshot.get<ActiveModulesQuery>(userCrate);
  ZC_REQUIRE(!active.isRuntimeFailure());
  ZC_REQUIRE(active.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(active.value().modules().size() == 1);
  ZC_EXPECT(active.value().modules()[0].encode().asPtr() == userModule.encode().asPtr());

  auto sites = snapshot.get<ModuleDependencySitesQuery>(userModule);
  ZC_REQUIRE(!sites.isRuntimeFailure());
  ZC_REQUIRE(sites.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(sites.value().sites().size() == 0);

  auto requests = snapshot.get<ModuleDependencyRequestsQuery>(userModule);
  ZC_REQUIRE(!requests.isRuntimeFailure());
  ZC_REQUIRE(requests.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(requests.value().requests().size() == 1);
  ZC_EXPECT(requests.value().requests()[0].dependencyKind() ==
            identity::ModuleDependencyKind::Prelude);

  auto dependencies = snapshot.get<ModuleDependenciesQuery>(userModule);
  ZC_REQUIRE(!dependencies.isRuntimeFailure());
  ZC_REQUIRE(dependencies.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(dependencies.value().dependencies().size() == 1);
  ZC_EXPECT(dependencies.value().dependencies()[0].encode().asPtr() ==
            module(coreCrate(), "core"_zc, "prelude"_zc).encode().asPtr());
}

ZC_TEST("Stable graph and independent SCC queries cover the complete core root") {
  basic::ThreadPool scheduler(2);
  auto queries = database(scheduler);
  auto empty = VerifiedModuleGraphInputLedger::empty();
  auto graphInputs = preparedTransaction(empty, true);
  ZC_REQUIRE(graphInputs != zc::none);
  ZC_REQUIRE(ZC_REQUIRE_NONNULL(graphInputs).commit(queries).isCommitted());
  auto sourceInputs = queries.beginInputTransaction(queries.snapshot().revision());
  ZC_REQUIRE(sourceInputs.isOpened());
  auto sourceTransaction = zc::mv(sourceInputs).takeTransaction();
  stageCoreDistribution(sourceTransaction);
  ZC_REQUIRE(sourceTransaction.commit().isCommitted());

  auto roots =
      incremental_binding_query::CompilationRootSetQueryKey::singletonToolchainCore(coreCrate());
  ZC_REQUIRE(roots != zc::none);
  auto snapshot = queries.snapshot();
  auto graph = snapshot.get<ModuleGraphQuery>(ZC_REQUIRE_NONNULL(roots));
  ZC_REQUIRE(!graph.isRuntimeFailure());
  ZC_REQUIRE(graph.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(graph.value().modules().size() == 3);
  ZC_EXPECT(graph.value().edges().size() == 1);

  auto scc = snapshot.get<ModuleGraphSccQuery>(ZC_REQUIRE_NONNULL(roots));
  ZC_REQUIRE(!scc.isRuntimeFailure());
  ZC_REQUIRE(scc.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(scc.value().components().size() == 3);
  ZC_EXPECT(!scc.value().hasCycle(graph.value()));

  auto graphCodec = ModuleGraphRecord::decodeCanonical(graph.value().encodeCanonical().asPtr());
  auto sccCodec = ModuleGraphSccRecord::decodeCanonical(scc.value().encodeCanonical().asPtr());
  ZC_EXPECT(graphCodec != zc::none);
  ZC_EXPECT(sccCodec != zc::none);

  auto core = coreCrate();
  auto contextualCore =
      core_library_query::ContextualCoreCrateKey::from(contextRoots(core), core.clone());
  ZC_REQUIRE(contextualCore != zc::none);
  auto coreGraph =
      snapshot.get<core_library_query::CoreModuleGraphQuery>(ZC_REQUIRE_NONNULL(contextualCore));
  ZC_REQUIRE(!coreGraph.isRuntimeFailure());
  ZC_REQUIRE(coreGraph.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(coreGraph.value().core().encode().asPtr() == core.encode().asPtr());
  ZC_EXPECT(coreGraph.value().modules().size() == 3);
  ZC_EXPECT(coreGraph.value().edges().size() == 1);
  auto expectedCoreContext = identity::CoreSemanticContextFingerprint::compute(core);
  ZC_REQUIRE(expectedCoreContext != zc::none);
  ZC_EXPECT(coreGraph.value().coreContext().digest() ==
            ZC_REQUIRE_NONNULL(expectedCoreContext).digest());
  auto coreGraphCodec = core_library_query::CoreModuleGraphRecord::decodeCanonical(
      coreGraph.value().encodeCanonical().asPtr());
  ZC_REQUIRE(coreGraphCodec != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(coreGraphCodec).revision() == coreGraph.value().revision());
}

ZC_TEST("Nested dependency failure globally precedes an earlier outside edge") {
  basic::ThreadPool scheduler(2);
  auto queries = database(scheduler);
  auto empty = VerifiedModuleGraphInputLedger::empty();
  auto graphInputs = preparedTransaction(empty, true);
  ZC_REQUIRE(graphInputs != zc::none);
  ZC_REQUIRE(ZC_REQUIRE_NONNULL(graphInputs).commit(queries).isCommitted());
  auto sourceInputs = queries.beginInputTransaction(queries.snapshot().revision());
  ZC_REQUIRE(sourceInputs.isOpened());
  auto sourceTransaction = zc::mv(sourceInputs).takeTransaction();
  stageCoreDistribution(sourceTransaction);
  ZC_REQUIRE(sourceTransaction.commit().isCommitted());

  auto core = coreCrate();
  auto earlier = module(core, "core"_zc);
  auto later = module(core, "core"_zc, "marker"_zc);
  auto snapshot = queries.snapshot();
  auto earlierSource = snapshot.get<SelectedModuleSourceQuery>(earlier);
  auto laterSource = snapshot.get<SelectedModuleSourceQuery>(later);
  ZC_REQUIRE(earlierSource.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(laterSource.kind() == query::QueryValueKind::Value);
  auto earlierStable =
      identity::source_query::StableSourceQueryKey::fromVerified(earlierSource.value());
  auto laterStable =
      identity::source_query::StableSourceQueryKey::fromVerified(laterSource.value());
  ZC_REQUIRE(earlierStable != zc::none);
  ZC_REQUIRE(laterStable != zc::none);
  auto earlierSnapshot =
      snapshot.get<identity::source_query::SourceSnapshotInput>(ZC_REQUIRE_NONNULL(earlierStable));
  auto laterSnapshot =
      snapshot.get<identity::source_query::SourceSnapshotInput>(ZC_REQUIRE_NONNULL(laterStable));
  ZC_REQUIRE(earlierSnapshot.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(laterSnapshot.kind() == query::QueryValueKind::Value);

  auto mutation = queries.beginInputTransaction(queries.snapshot().revision());
  ZC_REQUIRE(mutation.isOpened());
  {
    auto transaction = zc::mv(mutation).takeTransaction();
    auto outsidePath = modulePath("outside"_zc);
    zc::Vector<DetachedModuleDependencySite> outsideSites;
    auto outsideSite = DetachedModuleDependencySite::from(
        DetachedModuleDependencySiteKind::Import, cloneModulePath(outsidePath.asPtr()), 900);
    outsideSites.add(zc::mv(ZC_REQUIRE_NONNULL(outsideSite)));
    auto outsideSet = DetachedModuleDependencySiteSet::from(
        earlier.clone(), earlierSource.value().clone(), earlierSnapshot.value().contentDigest(),
        zc::mv(outsideSites));
    ZC_REQUIRE(transaction.set<ModuleDependencySiteInput>(earlier, ZC_REQUIRE_NONNULL(outsideSet))
                   .isApplied());

    auto missingPath = modulePath("missing"_zc);
    zc::Vector<DetachedModuleDependencySite> missingSites;
    auto missingSite = DetachedModuleDependencySite::from(
        DetachedModuleDependencySiteKind::Import, cloneModulePath(missingPath.asPtr()), 901);
    missingSites.add(zc::mv(ZC_REQUIRE_NONNULL(missingSite)));
    auto missingSet = DetachedModuleDependencySiteSet::from(
        later.clone(), laterSource.value().clone(), laterSnapshot.value().contentDigest(),
        zc::mv(missingSites));
    ZC_REQUIRE(transaction.set<ModuleDependencySiteInput>(later, ZC_REQUIRE_NONNULL(missingSet))
                   .isApplied());

    for (const auto name : {"outside"_zc, "missing"_zc}) {
      auto alias = identity::DependencyAlias::fromCanonical(name);
      auto key = incremental_module_resolution_query::DependencyAliasRootQueryKey::from(
          core.clone(), zc::mv(ZC_REQUIRE_NONNULL(alias)));
      ZC_REQUIRE(transaction
                     .set<incremental_module_resolution_query::DependencyAliasRootInput>(
                         ZC_REQUIRE_NONNULL(key),
                         incremental_module_resolution_query::ExplicitModuleTarget::absent())
                     .isApplied());
    }

    auto inactive = module(core, "outside"_zc);
    stageBucket(transaction, core, modulePath("core"_zc, "outside"_zc));
    zc::Maybe<identity::ModuleKey> inactiveTarget(inactive.clone());
    stageBucket(transaction, core, modulePath("outside"_zc), zc::mv(inactiveTarget));
    stageBucket(transaction, core, modulePath("core"_zc, "marker"_zc, "missing"_zc));
    stageBucket(transaction, core, modulePath("core"_zc, "missing"_zc));
    stageBucket(transaction, core, modulePath("missing"_zc));
    ZC_REQUIRE(transaction.commit().isCommitted());
  }

  auto roots = incremental_binding_query::CompilationRootSetQueryKey::singletonToolchainCore(core);
  ZC_REQUIRE(roots != zc::none);
  auto graph = queries.snapshot().get<ModuleGraphQuery>(ZC_REQUIRE_NONNULL(roots));
  ZC_REQUIRE(!graph.isRuntimeFailure());
  ZC_REQUIRE(graph.kind() == query::QueryValueKind::SemanticFailure);
  auto failure = ModuleDependencyFailureRecord::decodeCanonical(graph.semanticFailureBytes());
  ZC_REQUIRE(failure != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(failure).kind() == ModuleDependencyFailureKind::Missing);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(failure).request().requester().encode().asPtr() ==
            later.encode().asPtr());
}

ZC_TEST("Tarjan provider and Kosaraju verifier agree after a tracked cycle mutation") {
  basic::ThreadPool scheduler(2);
  auto queries = database(scheduler);
  auto empty = VerifiedModuleGraphInputLedger::empty();
  auto graphInputs = preparedTransaction(empty, true);
  ZC_REQUIRE(graphInputs != zc::none);
  ZC_REQUIRE(ZC_REQUIRE_NONNULL(graphInputs).commit(queries).isCommitted());
  auto sourceInputs = queries.beginInputTransaction(queries.snapshot().revision());
  ZC_REQUIRE(sourceInputs.isOpened());
  auto sourceTransaction = zc::mv(sourceInputs).takeTransaction();
  stageCoreDistribution(sourceTransaction);
  ZC_REQUIRE(sourceTransaction.commit().isCommitted());

  auto core = coreCrate();
  auto root = module(core, "core"_zc);
  auto marker = module(core, "core"_zc, "marker"_zc);
  auto snapshot = queries.snapshot();
  auto rootSource = snapshot.get<SelectedModuleSourceQuery>(root);
  auto markerSource = snapshot.get<SelectedModuleSourceQuery>(marker);
  ZC_REQUIRE(rootSource.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(markerSource.kind() == query::QueryValueKind::Value);
  auto rootStable = identity::source_query::StableSourceQueryKey::fromVerified(rootSource.value());
  auto markerStable =
      identity::source_query::StableSourceQueryKey::fromVerified(markerSource.value());
  ZC_REQUIRE(rootStable != zc::none);
  ZC_REQUIRE(markerStable != zc::none);
  auto rootSnapshot =
      snapshot.get<identity::source_query::SourceSnapshotInput>(ZC_REQUIRE_NONNULL(rootStable));
  auto markerSnapshot =
      snapshot.get<identity::source_query::SourceSnapshotInput>(ZC_REQUIRE_NONNULL(markerStable));
  ZC_REQUIRE(rootSnapshot.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(markerSnapshot.kind() == query::QueryValueKind::Value);

  auto mutation = queries.beginInputTransaction(queries.snapshot().revision());
  ZC_REQUIRE(mutation.isOpened());
  {
    auto transaction = zc::mv(mutation).takeTransaction();
    zc::Vector<DetachedModuleDependencySite> rootSites;
    auto rootSite = DetachedModuleDependencySite::from(DetachedModuleDependencySiteKind::Import,
                                                       modulePath("marker"_zc), 910);
    rootSites.add(zc::mv(ZC_REQUIRE_NONNULL(rootSite)));
    auto rootSet = DetachedModuleDependencySiteSet::from(root.clone(), rootSource.value().clone(),
                                                         rootSnapshot.value().contentDigest(),
                                                         zc::mv(rootSites));
    ZC_REQUIRE(
        transaction.set<ModuleDependencySiteInput>(root, ZC_REQUIRE_NONNULL(rootSet)).isApplied());

    zc::Vector<DetachedModuleDependencySite> markerSites;
    auto markerSite = DetachedModuleDependencySite::from(DetachedModuleDependencySiteKind::Import,
                                                         modulePath("core"_zc), 911);
    markerSites.add(zc::mv(ZC_REQUIRE_NONNULL(markerSite)));
    auto markerSet = DetachedModuleDependencySiteSet::from(
        marker.clone(), markerSource.value().clone(), markerSnapshot.value().contentDigest(),
        zc::mv(markerSites));
    ZC_REQUIRE(transaction.set<ModuleDependencySiteInput>(marker, ZC_REQUIRE_NONNULL(markerSet))
                   .isApplied());

    for (const auto name : {"marker"_zc, "core"_zc}) {
      auto alias = identity::DependencyAlias::fromCanonical(name);
      auto key = incremental_module_resolution_query::DependencyAliasRootQueryKey::from(
          core.clone(), zc::mv(ZC_REQUIRE_NONNULL(alias)));
      ZC_REQUIRE(transaction
                     .set<incremental_module_resolution_query::DependencyAliasRootInput>(
                         ZC_REQUIRE_NONNULL(key),
                         incremental_module_resolution_query::ExplicitModuleTarget::absent())
                     .isApplied());
    }
    zc::Maybe<identity::ModuleKey> markerTarget(marker.clone());
    stageBucket(transaction, core, modulePath("core"_zc, "marker"_zc), zc::mv(markerTarget));
    stageBucket(transaction, core, modulePath("marker"_zc));
    stageBucket(transaction, core, modulePath("core"_zc, "marker"_zc, "core"_zc));
    stageBucket(transaction, core, modulePath("core"_zc, "core"_zc));
    zc::Maybe<identity::ModuleKey> rootTarget(root.clone());
    stageBucket(transaction, core, modulePath("core"_zc), zc::mv(rootTarget));
    ZC_REQUIRE(transaction.commit().isCommitted());
  }

  auto roots = incremental_binding_query::CompilationRootSetQueryKey::singletonToolchainCore(core);
  ZC_REQUIRE(roots != zc::none);
  auto finalSnapshot = queries.snapshot();
  auto graph = finalSnapshot.get<ModuleGraphQuery>(ZC_REQUIRE_NONNULL(roots));
  auto scc = finalSnapshot.get<ModuleGraphSccQuery>(ZC_REQUIRE_NONNULL(roots));
  ZC_REQUIRE(graph.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(scc.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(graph.value().edges().size() == 3);
  ZC_EXPECT(scc.value().components().size() == 2);
  ZC_EXPECT(scc.value().hasCycle(graph.value()));
}

ZC_TEST("Independent transaction verifier rejects sites not backed by parsed syntax") {
  auto empty = VerifiedModuleGraphInputLedger::empty();
  auto graphInputs = preparedTransaction(empty, true, true);
  ZC_EXPECT(graphInputs == zc::none);
}

ZC_TEST("Complete context authority rejects malformed and unequal canonical inputs") {
  auto registry = targetRegistry();
  auto request = packageRequest(registry);
  auto rootSet = incremental_binding_query::PackageRootSetKey::fromVerified(request);
  ZC_REQUIRE(rootSet != zc::none);

  auto user = crate();
  auto dependencyPackage = packageNamed("dependency"_zc);
  auto dependency = crateForPackage(user, dependencyPackage.clone(), "dependency"_zc);
  auto core = coreCrate();
  auto resolvedEdge = packageEdge(package(), dependencyPackage);
  auto expandedEdge = crateEdge(resolvedEdge, user, dependency);
  zc::Vector<zc::Array<uint8_t>> graphPackages;
  graphPackages.add(package().encode());
  graphPackages.add(dependencyPackage.encode());
  zc::Vector<zc::Array<uint8_t>> graphResolvedEdges;
  graphResolvedEdges.add(resolvedEdge.encode());
  zc::Vector<zc::Array<uint8_t>> graphSelectedEdges;
  graphSelectedEdges.add(resolvedEdge.encode());
  zc::Vector<zc::Array<uint8_t>> graphCrates;
  graphCrates.add(user.encode());
  graphCrates.add(dependency.encode());
  zc::Vector<zc::Array<uint8_t>> graphCrateEdges;
  graphCrateEdges.add(expandedEdge.encode());
  auto graphBytes =
      packageGraphBytes(zc::mv(graphPackages), zc::mv(graphResolvedEdges),
                        zc::mv(graphSelectedEdges), zc::mv(graphCrates), zc::mv(graphCrateEdges));
  auto graph = incremental_binding_query::PackageGraphInput::decodeValue(graphBytes.asPtr());
  ZC_REQUIRE(graph != zc::none);

  auto distribution = core_library_test::admittedCoreDistribution();
  auto canonicalOptions =
      identity::source_query::CanonicalCompilationOptions::fromVerified(request);
  ZC_REQUIRE(canonicalOptions != zc::none);
  auto options = zc::mv(ZC_REQUIRE_NONNULL(canonicalOptions));
  zc::Vector<identity::CrateKey> consumers;
  consumers.add(user.clone());
  consumers.add(dependency.clone());

  zc::Vector<identity::CrateKey> userRoots;
  userRoots.add(user.clone());
  zc::Vector<identity::CrateKey> projectedCore;
  projectedCore.add(core.clone());
  zc::Vector<CompilationOptionsEntry> optionEntries;
  optionEntries.add(CompilationOptionsEntry::from(user.clone(), options.clone()));
  optionEntries.add(CompilationOptionsEntry::from(dependency.clone(), options.clone()));
  optionEntries.add(CompilationOptionsEntry::from(core.clone(), options.clone()));

  zc::Vector<binder::ModuleSearchRoot> workspaceRoots;
  zc::Vector<identity::CanonicalPathSegment> workspacePath;
  workspaceRoots.add(binder::ModuleSearchRoot::workspace(
      user.clone(), identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(workspacePath))));
  auto userSearchRoots =
      incremental_module_resolution_query::CanonicalModuleSearchRoots::fromVerified(
          user, workspaceRoots.asPtr());
  ZC_REQUIRE(userSearchRoots != zc::none);
  zc::Vector<ModuleSearchRootsEntry> searchEntries;
  searchEntries.add(
      ModuleSearchRootsEntry::from(user.clone(), zc::mv(ZC_REQUIRE_NONNULL(userSearchRoots))));
  zc::Vector<binder::ModuleSearchRoot> dependencyWorkspaceRoots;
  zc::Vector<identity::CanonicalPathSegment> dependencyWorkspacePath;
  dependencyWorkspacePath.add(scalar<identity::CanonicalPathSegment>("dependency"_zc));
  dependencyWorkspaceRoots.add(binder::ModuleSearchRoot::workspace(
      dependency.clone(),
      identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(dependencyWorkspacePath))));
  auto dependencySearchRoots =
      incremental_module_resolution_query::CanonicalModuleSearchRoots::fromVerified(
          dependency, dependencyWorkspaceRoots.asPtr());
  ZC_REQUIRE(dependencySearchRoots != zc::none);
  searchEntries.add(ModuleSearchRootsEntry::from(
      dependency.clone(), zc::mv(ZC_REQUIRE_NONNULL(dependencySearchRoots))));
  auto acceptedDistribution = source::core::initialCoreDistributionInput();
  ZC_REQUIRE(acceptedDistribution != zc::none);
  auto coreRoot = binder::ModuleSearchRoot::toolchainCore(
      core.clone(), ZC_REQUIRE_NONNULL(acceptedDistribution).digest());
  ZC_REQUIRE(coreRoot != zc::none);
  zc::Vector<binder::ModuleSearchRoot> coreEnvironment;
  coreEnvironment.add(zc::mv(ZC_REQUIRE_NONNULL(coreRoot)));
  auto coreSearchRoots =
      incremental_module_resolution_query::CanonicalModuleSearchRoots::fromVerified(
          core, coreEnvironment.asPtr());
  ZC_REQUIRE(coreSearchRoots != zc::none);
  searchEntries.add(
      ModuleSearchRootsEntry::from(core.clone(), zc::mv(ZC_REQUIRE_NONNULL(coreSearchRoots))));

  const CompleteCompilationContextSources sources{
      request,
      ZC_REQUIRE_NONNULL(rootSet),
      ZC_REQUIRE_NONNULL(graph),
      userRoots.asPtr(),
      projectedCore.asPtr(),
      optionEntries.asPtr(),
      searchEntries.asPtr(),
      ZC_REQUIRE_NONNULL(acceptedDistribution),
  };
  auto authority = CompleteCompilationContextAuthority::fromVerified(sources);
  ZC_REQUIRE(authority != zc::none);
  auto preparedCore = core_library_query::VerifiedCoreDistributionInputTransaction::prepare(
      query::DatabaseRevision(), distribution, request, ZC_REQUIRE_NONNULL(authority).clone(),
      options, consumers.asPtr());
  ZC_REQUIRE(preparedCore != zc::none);
  auto coreInputs = zc::mv(ZC_REQUIRE_NONNULL(preparedCore));
  ZC_EXPECT(CompleteCompilationContextAuthorityInputVerifier::verify(ZC_REQUIRE_NONNULL(authority),
                                                                     sources));

  auto alternateResolvedEdge = packageEdge(package(), dependencyPackage, "alternate"_zc);
  auto alternateExpandedEdge = crateEdge(alternateResolvedEdge, user, dependency);
  zc::Vector<zc::Array<uint8_t>> alternatePackages;
  alternatePackages.add(package().encode());
  alternatePackages.add(dependencyPackage.encode());
  zc::Vector<zc::Array<uint8_t>> alternateResolvedEdges;
  alternateResolvedEdges.add(alternateResolvedEdge.encode());
  zc::Vector<zc::Array<uint8_t>> alternateSelectedEdges;
  alternateSelectedEdges.add(alternateResolvedEdge.encode());
  zc::Vector<zc::Array<uint8_t>> alternateCrates;
  alternateCrates.add(user.encode());
  alternateCrates.add(dependency.encode());
  zc::Vector<zc::Array<uint8_t>> alternateCrateEdges;
  alternateCrateEdges.add(alternateExpandedEdge.encode());
  auto alternateGraphBytes = packageGraphBytes(
      zc::mv(alternatePackages), zc::mv(alternateResolvedEdges), zc::mv(alternateSelectedEdges),
      zc::mv(alternateCrates), zc::mv(alternateCrateEdges));
  auto alternateGraph =
      incremental_binding_query::PackageGraphInput::decodeValue(alternateGraphBytes.asPtr());
  ZC_REQUIRE(alternateGraph != zc::none);
  const CompleteCompilationContextSources alternateGraphSources{
      request,
      ZC_REQUIRE_NONNULL(rootSet),
      ZC_REQUIRE_NONNULL(alternateGraph),
      userRoots.asPtr(),
      projectedCore.asPtr(),
      optionEntries.asPtr(),
      searchEntries.asPtr(),
      coreInputs.distribution(),
  };
  auto alternateAuthority =
      CompleteCompilationContextAuthority::fromVerified(alternateGraphSources);
  ZC_REQUIRE(alternateAuthority != zc::none);
  ZC_EXPECT(CompleteCompilationContextAuthorityInputVerifier::verify(
      ZC_REQUIRE_NONNULL(alternateAuthority), alternateGraphSources));
  ZC_EXPECT(!CompleteCompilationContextAuthorityInputVerifier::verify(ZC_REQUIRE_NONNULL(authority),
                                                                      alternateGraphSources));

  zc::Vector<zc::Array<uint8_t>> mismatchedOriginPackages;
  mismatchedOriginPackages.add(package().encode());
  mismatchedOriginPackages.add(dependencyPackage.encode());
  zc::Vector<zc::Array<uint8_t>> mismatchedOriginResolvedEdges;
  mismatchedOriginResolvedEdges.add(resolvedEdge.encode());
  mismatchedOriginResolvedEdges.add(alternateResolvedEdge.encode());
  zc::Vector<zc::Array<uint8_t>> mismatchedOriginSelectedEdges;
  mismatchedOriginSelectedEdges.add(resolvedEdge.encode());
  zc::Vector<zc::Array<uint8_t>> mismatchedOriginCrates;
  mismatchedOriginCrates.add(user.encode());
  mismatchedOriginCrates.add(dependency.encode());
  zc::Vector<zc::Array<uint8_t>> mismatchedOriginCrateEdges;
  mismatchedOriginCrateEdges.add(alternateExpandedEdge.encode());
  auto mismatchedOriginBytes =
      packageGraphBytes(zc::mv(mismatchedOriginPackages), zc::mv(mismatchedOriginResolvedEdges),
                        zc::mv(mismatchedOriginSelectedEdges), zc::mv(mismatchedOriginCrates),
                        zc::mv(mismatchedOriginCrateEdges));
  ZC_EXPECT(incremental_binding_query::PackageGraphInput::decodeValue(
                mismatchedOriginBytes.asPtr()) == zc::none);

  basic::ThreadPool scheduler(2);
  auto queries = database(scheduler);
  auto opened = queries.beginInputTransaction(queries.snapshot().revision());
  ZC_REQUIRE(opened.isOpened());
  auto transaction = zc::mv(opened).takeTransaction();
  ZC_REQUIRE(transaction
                 .set<CompleteCompilationContextAuthorityInput>(
                     ZC_REQUIRE_NONNULL(authority).contextRoots(), ZC_REQUIRE_NONNULL(authority))
                 .isApplied());
  ZC_REQUIRE(transaction.commit().isCommitted());
  auto witness = computeCompleteCompilationContextWitness(ZC_REQUIRE_NONNULL(authority));
  ZC_REQUIRE(witness != zc::none);
  auto snapshot = queries.snapshot();
  ZC_EXPECT(CompleteCompilationContextAuthorityInput::verifyFinalAuthority(
                snapshot, ZC_REQUIRE_NONNULL(authority).contextRoots(),
                ZC_REQUIRE_NONNULL(authority),
                ZC_REQUIRE_NONNULL(witness)) == query::FinalAuthorityCheck::Rejected);
  ZC_EXPECT(CompleteCompilationContextAuthorityInput::verifyFinalAuthority(
                snapshot, ZC_REQUIRE_NONNULL(authority).contextRoots(),
                ZC_REQUIRE_NONNULL(authority),
                digest(0x93)) == query::FinalAuthorityCheck::Rejected);

  auto encoded = ZC_REQUIRE_NONNULL(authority).encodeCanonical();
  auto decoded = CompleteCompilationContextAuthority::decodeCanonical(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded) == ZC_REQUIRE_NONNULL(authority));

  auto fieldRanges = completeContextFieldRanges(encoded.asPtr());
  ZC_REQUIRE(fieldRanges.size() == 12);
  for (size_t fieldIndex = 0; fieldIndex < fieldRanges.size(); ++fieldIndex) {
    const auto range = fieldRanges[fieldIndex];
    auto fieldMutation = zc::heapArray<uint8_t>(encoded.asPtr());
    ZC_REQUIRE(range.end > range.begin);
    fieldMutation[range.end - 1] ^= 0x01;
    auto fieldCandidate =
        CompleteCompilationContextAuthority::decodeCanonical(fieldMutation.asPtr());
    ZC_IF_SOME(value, fieldCandidate) {
      ZC_EXPECT(!CompleteCompilationContextAuthorityInputVerifier::verify(value, sources),
                fieldIndex);
    }
  }

  auto domainMutation = zc::heapArray<uint8_t>(encoded.asPtr());
  domainMutation[0] ^= 0x01;
  ZC_EXPECT(CompleteCompilationContextAuthority::decodeCanonical(domainMutation.asPtr()) ==
            zc::none);
  ZC_EXPECT(CompleteCompilationContextAuthority::decodeCanonical(
                encoded.asPtr().slice(0, encoded.size() - 1)) == zc::none);

  zc::Vector<uint8_t> trailing(encoded.size() + 1);
  trailing.addAll(encoded.asPtr());
  trailing.add(0);
  ZC_EXPECT(CompleteCompilationContextAuthority::decodeCanonical(
                trailing.releaseAsArray().asPtr()) == zc::none);

  auto reordered = swapFirstTwoWireSequenceElements(encoded.asPtr(), fieldRanges[8]);
  ZC_EXPECT(CompleteCompilationContextAuthority::decodeCanonical(reordered.asPtr()) == zc::none);

  auto excessiveCount = zc::heapArray<uint8_t>(encoded.asPtr());
  writeWireUint64(excessiveCount.asPtr(), fieldRanges[4].begin,
                  static_cast<uint64_t>(UINT32_MAX) + 1);
  ZC_EXPECT(CompleteCompilationContextAuthority::decodeCanonical(excessiveCount.asPtr()) ==
            zc::none);

  const auto expectHostileCountRejected = [&](size_t fieldIndex) {
    auto hostileCount = zc::heapArray<uint8_t>(encoded.asPtr());
    writeWireUint64(hostileCount.asPtr(), fieldRanges[fieldIndex].begin, UINT32_MAX);
    ZC_EXPECT(CompleteCompilationContextAuthority::decodeCanonical(hostileCount.asPtr().slice(
                  0, fieldRanges[fieldIndex].begin + sizeof(uint64_t))) == zc::none,
              fieldIndex);
  };
  expectHostileCountRejected(4);
  expectHostileCountRejected(8);
  expectHostileCountRejected(9);

  auto excessiveBytes = zc::heapArray<uint8_t>(encoded.asPtr());
  writeWireUint64(excessiveBytes.asPtr(), fieldRanges[0].begin, UINT64_MAX);
  ZC_EXPECT(CompleteCompilationContextAuthority::decodeCanonical(excessiveBytes.asPtr()) ==
            zc::none);

  zc::Vector<CompilationOptionsEntry> duplicateOptions;
  for (const auto& entry : optionEntries) { duplicateOptions.add(entry.clone()); }
  duplicateOptions.add(optionEntries.front().clone());
  const CompleteCompilationContextSources duplicateSources{
      request,
      ZC_REQUIRE_NONNULL(rootSet),
      ZC_REQUIRE_NONNULL(graph),
      userRoots.asPtr(),
      projectedCore.asPtr(),
      duplicateOptions.asPtr(),
      searchEntries.asPtr(),
      coreInputs.distribution(),
  };
  ZC_EXPECT(CompleteCompilationContextAuthority::fromVerified(duplicateSources) == zc::none);
  ZC_EXPECT(!CompleteCompilationContextAuthorityInputVerifier::verify(ZC_REQUIRE_NONNULL(authority),
                                                                      duplicateSources));

  zc::Vector<identity::CrateKey> missingUserRoots;
  const CompleteCompilationContextSources missingRootSources{
      request,
      ZC_REQUIRE_NONNULL(rootSet),
      ZC_REQUIRE_NONNULL(graph),
      missingUserRoots.asPtr(),
      projectedCore.asPtr(),
      optionEntries.asPtr(),
      searchEntries.asPtr(),
      coreInputs.distribution(),
  };
  ZC_EXPECT(CompleteCompilationContextAuthority::fromVerified(missingRootSources) == zc::none);
  ZC_EXPECT(!CompleteCompilationContextAuthorityInputVerifier::verify(ZC_REQUIRE_NONNULL(authority),
                                                                      missingRootSources));

  zc::Vector<identity::CrateKey> additionalUserRoots;
  additionalUserRoots.add(user.clone());
  additionalUserRoots.add(core.clone());
  const CompleteCompilationContextSources additionalRootSources{
      request,
      ZC_REQUIRE_NONNULL(rootSet),
      ZC_REQUIRE_NONNULL(graph),
      additionalUserRoots.asPtr(),
      projectedCore.asPtr(),
      optionEntries.asPtr(),
      searchEntries.asPtr(),
      coreInputs.distribution(),
  };
  ZC_EXPECT(CompleteCompilationContextAuthority::fromVerified(additionalRootSources) == zc::none);
  ZC_EXPECT(!CompleteCompilationContextAuthorityInputVerifier::verify(ZC_REQUIRE_NONNULL(authority),
                                                                      additionalRootSources));

  auto unequalOptions = compilationOptions();
  zc::Vector<CompilationOptionsEntry> unequalOptionEntries;
  unequalOptionEntries.add(CompilationOptionsEntry::from(user.clone(), unequalOptions.clone()));
  unequalOptionEntries.add(
      CompilationOptionsEntry::from(dependency.clone(), unequalOptions.clone()));
  unequalOptionEntries.add(CompilationOptionsEntry::from(core.clone(), unequalOptions.clone()));
  const CompleteCompilationContextSources unequalOptionSources{
      request,
      ZC_REQUIRE_NONNULL(rootSet),
      ZC_REQUIRE_NONNULL(graph),
      userRoots.asPtr(),
      projectedCore.asPtr(),
      unequalOptionEntries.asPtr(),
      searchEntries.asPtr(),
      coreInputs.distribution(),
  };
  ZC_EXPECT(CompleteCompilationContextAuthority::fromVerified(unequalOptionSources) == zc::none);
  ZC_EXPECT(!CompleteCompilationContextAuthorityInputVerifier::verify(ZC_REQUIRE_NONNULL(authority),
                                                                      unequalOptionSources));

  zc::Vector<ModuleSearchRootsEntry> incompleteSearchEntries;
  incompleteSearchEntries.add(searchEntries.front().clone());
  const CompleteCompilationContextSources incompleteSearchSources{
      request,
      ZC_REQUIRE_NONNULL(rootSet),
      ZC_REQUIRE_NONNULL(graph),
      userRoots.asPtr(),
      projectedCore.asPtr(),
      optionEntries.asPtr(),
      incompleteSearchEntries.asPtr(),
      coreInputs.distribution(),
  };
  ZC_EXPECT(CompleteCompilationContextAuthority::fromVerified(incompleteSearchSources) == zc::none);
  ZC_EXPECT(!CompleteCompilationContextAuthorityInputVerifier::verify(ZC_REQUIRE_NONNULL(authority),
                                                                      incompleteSearchSources));

  auto unequalTargetName = identity::TargetName::fromCanonical("other"_zc);
  ZC_REQUIRE(unequalTargetName != zc::none);
  auto unequalCrate = identity::CrateKey::from(
      user.unit().clone(), identity::CrateTargetKind::Binary,
      zc::mv(ZC_REQUIRE_NONNULL(unequalTargetName)), user.compilation().clone());
  ZC_REQUIRE(unequalCrate != zc::none);
  identity::CanonicalEncoder unequalGraphEncoder;
  unequalGraphEncoder.encodeSequenceSize(1);
  unequalGraphEncoder.encodeByteString(package().encode().asPtr());
  unequalGraphEncoder.encodeSequenceSize(0);
  unequalGraphEncoder.encodeSequenceSize(0);
  unequalGraphEncoder.encodeSequenceSize(1);
  unequalGraphEncoder.encodeByteString(ZC_REQUIRE_NONNULL(unequalCrate).encode().asPtr());
  unequalGraphEncoder.encodeSequenceSize(0);
  auto unequalGraph = incremental_binding_query::PackageGraphInput::decodeValue(
      unequalGraphEncoder.finish().asPtr());
  ZC_REQUIRE(unequalGraph != zc::none);
  const CompleteCompilationContextSources unequalGraphSources{
      request,
      ZC_REQUIRE_NONNULL(rootSet),
      ZC_REQUIRE_NONNULL(unequalGraph),
      userRoots.asPtr(),
      projectedCore.asPtr(),
      optionEntries.asPtr(),
      searchEntries.asPtr(),
      coreInputs.distribution(),
  };
  ZC_EXPECT(CompleteCompilationContextAuthority::fromVerified(unequalGraphSources) == zc::none);
  ZC_EXPECT(!CompleteCompilationContextAuthorityInputVerifier::verify(ZC_REQUIRE_NONNULL(authority),
                                                                      unequalGraphSources));
}

}  // namespace zomlang::compiler::driver::module_graph_query
