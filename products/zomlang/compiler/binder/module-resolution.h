// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/node-id.h"
#include "zomlang/compiler/identity/module-resolution-key.h"
#include "zomlang/compiler/identity/semantic-identity-registry-set.h"

namespace zomlang::compiler::binder {

struct WorkspaceModuleSearchRoot final {
  identity::CrateKey crate;
  identity::CanonicalWorkspaceRelativePath root;
};

struct PackageModuleSearchRoot final {
  identity::CrateKey crate;
  identity::PackageKey package;
  identity::CanonicalRelativePath root;
};

struct GeneratedModuleSearchRoot final {
  identity::CrateKey crate;
  identity::BuildScriptProducerKey producer;
  identity::CanonicalRelativePath root;
};

enum class ModuleSearchRootKind : uint8_t { Workspace = 0x01, Package = 0x02, Generated = 0x03 };

/// \brief Ordered, closed search-root variant used by structural module resolution.
class ModuleSearchRoot final {
public:
  ModuleSearchRoot(ModuleSearchRoot&&) noexcept = default;
  ModuleSearchRoot& operator=(ModuleSearchRoot&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleSearchRoot);

  ZC_NODISCARD static ModuleSearchRoot workspace(identity::CrateKey&& crate,
                                                 identity::CanonicalWorkspaceRelativePath&& root);
  ZC_NODISCARD static ModuleSearchRoot package(identity::CrateKey&& crate,
                                               identity::PackageKey&& package,
                                               identity::CanonicalRelativePath&& root);
  ZC_NODISCARD static ModuleSearchRoot generated(identity::CrateKey&& crate,
                                                 identity::BuildScriptProducerKey producer,
                                                 identity::CanonicalRelativePath&& root);
  ZC_NODISCARD ModuleSearchRoot clone() const;
  ZC_NODISCARD static zc::Maybe<ModuleSearchRoot> decodeCanonical(
      identity::CanonicalDecoder& decoder);
  ZC_NODISCARD ModuleSearchRootKind kind() const noexcept;
  ZC_NODISCARD const identity::CrateKey& crate() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  explicit ModuleSearchRoot(WorkspaceModuleSearchRoot&& root) noexcept;
  explicit ModuleSearchRoot(PackageModuleSearchRoot&& root) noexcept;
  explicit ModuleSearchRoot(GeneratedModuleSearchRoot&& root) noexcept;

  zc::OneOf<WorkspaceModuleSearchRoot, PackageModuleSearchRoot, GeneratedModuleSearchRoot> value;

  friend class StructuralModuleResolver;
};

struct ModuleSourceSnapshotRevision final {
  ModuleSourceSnapshotRevision(identity::SourceFileKey&& source,
                               const identity::Sha256Digest& contentDigest) noexcept;
  ModuleSourceSnapshotRevision(ModuleSourceSnapshotRevision&&) noexcept = default;
  ModuleSourceSnapshotRevision& operator=(ModuleSourceSnapshotRevision&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleSourceSnapshotRevision);

  identity::SourceFileKey source;
  identity::Sha256Digest contentDigest;
};

struct GeneratedModuleSourceRevision final {
  GeneratedModuleSourceRevision(identity::BuildScriptProducerKey producer,
                                const identity::Sha256Digest& revision) noexcept;

  identity::BuildScriptProducerKey producer;
  identity::Sha256Digest revision;
};

struct ModuleDependencyAliasRoot final {
  ModuleDependencyAliasRoot(identity::CrateKey&& requester, identity::DependencyAlias&& alias,
                            identity::ModuleKey&& target) noexcept;
  ModuleDependencyAliasRoot(ModuleDependencyAliasRoot&&) noexcept = default;
  ModuleDependencyAliasRoot& operator=(ModuleDependencyAliasRoot&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleDependencyAliasRoot);

  identity::CrateKey requester;
  identity::DependencyAlias alias;
  identity::ModuleKey target;
};

struct RequesterModuleAncestryCandidate final {
  RequesterModuleAncestryCandidate(identity::ModuleKey&& requester,
                                   zc::Vector<identity::ModuleKey>&& ancestry) noexcept;
  RequesterModuleAncestryCandidate(RequesterModuleAncestryCandidate&&) noexcept = default;
  RequesterModuleAncestryCandidate& operator=(RequesterModuleAncestryCandidate&&) noexcept =
      default;
  ZC_DISALLOW_COPY(RequesterModuleAncestryCandidate);

  identity::ModuleKey requester;
  zc::Vector<identity::ModuleKey> ancestry;
};

/// \brief Immutable discovery inputs whose exact encoding determines resolution semantics.
struct ModuleResolutionEnvironmentRecord final {
  ModuleResolutionEnvironmentRecord(
      zc::Vector<ModuleSearchRoot>&& searchRoots,
      zc::Vector<ModuleSourceSnapshotRevision>&& sourceSnapshots,
      zc::Vector<GeneratedModuleSourceRevision>&& generatedSourceRevisions,
      zc::Vector<ModuleDependencyAliasRoot>&& dependencyAliasRoots,
      zc::Vector<RequesterModuleAncestryCandidate>&& requesterAncestry) noexcept;
  ModuleResolutionEnvironmentRecord(ModuleResolutionEnvironmentRecord&&) noexcept = default;
  ModuleResolutionEnvironmentRecord& operator=(ModuleResolutionEnvironmentRecord&&) noexcept =
      default;
  ZC_DISALLOW_COPY(ModuleResolutionEnvironmentRecord);

  zc::Vector<ModuleSearchRoot> searchRoots;
  zc::Vector<ModuleSourceSnapshotRevision> sourceSnapshots;
  zc::Vector<GeneratedModuleSourceRevision> generatedSourceRevisions;
  zc::Vector<ModuleDependencyAliasRoot> dependencyAliasRoots;
  zc::Vector<RequesterModuleAncestryCandidate> requesterAncestry;
};

struct StructuralModuleCatalogEntry final {
  StructuralModuleCatalogEntry(identity::ModuleKey&& key, identity::ModuleId module,
                               identity::SourceFileKey&& source) noexcept;
  StructuralModuleCatalogEntry(StructuralModuleCatalogEntry&&) noexcept = default;
  StructuralModuleCatalogEntry& operator=(StructuralModuleCatalogEntry&&) noexcept = default;
  ZC_DISALLOW_COPY(StructuralModuleCatalogEntry);

  identity::ModuleKey key;
  identity::ModuleId module;
  identity::SourceFileKey source;
};

enum class ModuleResolutionInvariantKind : uint8_t {
  InputMismatch,
  InvalidEnvironment,
  InvalidRequest,
  RevisionMismatch
};

struct ModuleResolutionInvariantFact final {
  ModuleResolutionInvariantKind kind;
  uint32_t occurrence;
};

struct ModuleSyntaxDependencySite final {
  ModuleSyntaxDependencySite(ast::NodeId node, identity::SourceSpan&& span,
                             uint32_t schemaPreorderOrdinal) noexcept;
  ModuleSyntaxDependencySite(ModuleSyntaxDependencySite&&) noexcept = default;
  ModuleSyntaxDependencySite& operator=(ModuleSyntaxDependencySite&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleSyntaxDependencySite);

  ast::NodeId node;
  identity::SourceSpan span;
  uint32_t schemaPreorderOrdinal;
};

struct ModulePreludeDependencySite final {
  ModulePreludeDependencySite(identity::ModuleKey&& selectedTarget,
                              const identity::Sha256Digest& configurationRevision) noexcept;
  ModulePreludeDependencySite(ModulePreludeDependencySite&&) noexcept = default;
  ModulePreludeDependencySite& operator=(ModulePreludeDependencySite&&) noexcept = default;
  ZC_DISALLOW_COPY(ModulePreludeDependencySite);

  identity::ModuleKey selectedTarget;
  identity::Sha256Digest configurationRevision;
};

/// \brief Canonical semantic dependency request derived from syntax or prelude configuration.
class ModuleDependencyRequest final {
public:
  ModuleDependencyRequest(ModuleDependencyRequest&&) noexcept = default;
  ModuleDependencyRequest& operator=(ModuleDependencyRequest&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleDependencyRequest);

  ZC_NODISCARD static zc::Maybe<ModuleDependencyRequest> source(
      identity::ModuleId requester, identity::ModuleResolutionKey&& key,
      const identity::Sha256Digest& environmentRevision,
      zc::Vector<ModuleSyntaxDependencySite>&& syntaxSites);
  ZC_NODISCARD static zc::Maybe<ModuleDependencyRequest> prelude(
      identity::ModuleId requester, identity::ModuleResolutionKey&& key,
      identity::ModuleKey&& selectedTarget, const identity::Sha256Digest& environmentRevision,
      const identity::Sha256Digest& configurationRevision);
  ZC_NODISCARD ModuleDependencyRequest clone() const;

  ZC_NODISCARD const identity::ModuleResolutionKey& key() const noexcept;
  ZC_NODISCARD identity::ModuleId requester() const noexcept;
  ZC_NODISCARD identity::ModuleDependencyKind kind() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::ModulePathSegment> normalizedPath() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& environmentRevision() const noexcept;
  ZC_NODISCARD bool isPrelude() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ModuleSyntaxDependencySite> syntaxSites() const noexcept;
  /// \brief Returns the canonical first site for single-site materialization consumers.
  ZC_NODISCARD const ModuleSyntaxDependencySite& syntaxSite() const;
  ZC_NODISCARD const identity::ModuleKey& requestedTarget() const;
  ZC_NODISCARD const identity::Sha256Digest& configurationRevision() const;

private:
  ModuleDependencyRequest(identity::ModuleId requester, identity::ModuleResolutionKey&& key,
                          const identity::Sha256Digest& environmentRevision,
                          zc::Vector<ModuleSyntaxDependencySite>&& syntaxSites,
                          zc::Maybe<ModulePreludeDependencySite>&& preludeSite) noexcept;

  identity::ModuleId requesterValue;
  identity::ModuleResolutionKey keyValue;
  identity::Sha256Digest environmentRevisionValue;
  zc::Vector<ModuleSyntaxDependencySite> syntaxSiteValues;
  zc::Maybe<ModulePreludeDependencySite> preludeSiteValue;

  friend class StructuralModuleResolver;
  friend class ModuleGraphVerifier;
};

class StructuralModuleResolver;
class ModuleGraphVerifier;

/// \brief Private-constructor proof of the exact zero, one, or many structural candidates.
class VerifiedStructuralResolutionReceipt final {
public:
  VerifiedStructuralResolutionReceipt(VerifiedStructuralResolutionReceipt&&) noexcept = default;
  VerifiedStructuralResolutionReceipt& operator=(VerifiedStructuralResolutionReceipt&&) noexcept =
      default;
  ZC_DISALLOW_COPY(VerifiedStructuralResolutionReceipt);

  ZC_NODISCARD zc::ArrayPtr<const identity::ModuleKey> candidates() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& revision() const noexcept;

private:
  VerifiedStructuralResolutionReceipt(uint64_t issuer, zc::Array<uint8_t>&& requestKey,
                                      zc::Vector<identity::ModuleKey>&& candidates,
                                      const identity::Sha256Digest& revision) noexcept;

  uint64_t issuerValue;
  zc::Array<uint8_t> requestKeyValue;
  zc::Vector<identity::ModuleKey> candidateValues;
  identity::Sha256Digest revisionValue;

  friend class StructuralModuleResolver;
  friend class ModuleGraphVerifier;
};

struct ResolvedModulePath final {
  ResolvedModulePath(ModuleDependencyRequest&& request, identity::ModuleId target,
                     VerifiedStructuralResolutionReceipt&& receipt) noexcept;
  ResolvedModulePath(ResolvedModulePath&&) noexcept = default;
  ResolvedModulePath& operator=(ResolvedModulePath&&) noexcept = default;
  ZC_DISALLOW_COPY(ResolvedModulePath);

  ModuleDependencyRequest request;
  identity::ModuleId target;
  VerifiedStructuralResolutionReceipt receipt;
};

struct MissingModulePath final {
  MissingModulePath(ModuleDependencyRequest&& request,
                    VerifiedStructuralResolutionReceipt&& receipt) noexcept;
  MissingModulePath(MissingModulePath&&) noexcept = default;
  MissingModulePath& operator=(MissingModulePath&&) noexcept = default;
  ZC_DISALLOW_COPY(MissingModulePath);

  ModuleDependencyRequest request;
  VerifiedStructuralResolutionReceipt receipt;
};

struct AmbiguousModulePath final {
  AmbiguousModulePath(ModuleDependencyRequest&& request,
                      zc::Vector<identity::ModuleKey>&& candidates,
                      VerifiedStructuralResolutionReceipt&& receipt) noexcept;
  AmbiguousModulePath(AmbiguousModulePath&&) noexcept = default;
  AmbiguousModulePath& operator=(AmbiguousModulePath&&) noexcept = default;
  ZC_DISALLOW_COPY(AmbiguousModulePath);

  ModuleDependencyRequest request;
  zc::Vector<identity::ModuleKey> candidates;
  VerifiedStructuralResolutionReceipt receipt;
};

using ModulePathResolution = zc::OneOf<ResolvedModulePath, MissingModulePath, AmbiguousModulePath>;

/// \brief Verified immutable environment and sole issuer of structural receipts.
class StructuralModuleResolver final {
public:
  ~StructuralModuleResolver() noexcept(false);
  StructuralModuleResolver(StructuralModuleResolver&&) noexcept;
  StructuralModuleResolver& operator=(StructuralModuleResolver&&) noexcept;
  ZC_DISALLOW_COPY(StructuralModuleResolver);

  using FreezeResult = zc::OneOf<StructuralModuleResolver, ModuleResolutionInvariantFact>;
  ZC_NODISCARD static FreezeResult freeze(identity::SemanticContextBrand context,
                                          const identity::SemanticIdentityRegistrySet& registries,
                                          ModuleResolutionEnvironmentRecord&& environment,
                                          zc::Vector<StructuralModuleCatalogEntry>&& catalog);

  ZC_NODISCARD const identity::Sha256Digest& environmentRevision() const noexcept;
  ZC_NODISCARD const identity::ModuleResolutionPolicyKey& policy() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const StructuralModuleCatalogEntry> catalog() const noexcept;
  /// \brief Returns the independently admitted crate search-root query inputs.
  ZC_NODISCARD zc::ArrayPtr<const ModuleSearchRoot> searchRootInputs() const noexcept;
  /// \brief Returns the independently admitted dependency-alias query inputs.
  ZC_NODISCARD zc::ArrayPtr<const ModuleDependencyAliasRoot> dependencyAliasRootInputs()
      const noexcept;
  /// \brief Returns the independently admitted requester ancestry query inputs.
  ZC_NODISCARD zc::ArrayPtr<const identity::RequesterModuleAncestry> requesterAncestryInputs()
      const noexcept;
  /// \brief Returns the independently admitted present catalog-bucket query inputs.
  ZC_NODISCARD zc::ArrayPtr<const identity::ModuleCatalogPathBucket> catalogPathBucketInputs()
      const noexcept;
  ZC_NODISCARD zc::Maybe<identity::DependencyAlias> dependencyAlias(
      const identity::CrateKey& requester,
      const identity::ModulePathSegment& firstPathSegment) const;
  ZC_NODISCARD zc::Maybe<identity::ModuleResolutionKey> resolutionKey(
      identity::ModuleId requester, identity::ModuleDependencyKind kind,
      zc::Vector<identity::ModulePathSegment>&& normalizedPath) const;
  /// \brief Materializes a revision-local graph receipt from one verified query candidate set.
  ZC_NODISCARD zc::OneOf<ModulePathResolution, ModuleResolutionInvariantFact>
  materializeQueryResolution(ModuleDependencyRequest&& request,
                             const identity::ModuleResolutionCandidates& candidates) const;
  /// \brief Projects an exact explicit present-or-absent catalog input.
  ZC_NODISCARD zc::Maybe<identity::ModuleCatalogPathBucket> catalogPathBucketInput(
      const identity::CrateKey& crate, zc::ArrayPtr<const identity::ModulePathSegment> path) const;

private:
  struct Impl;
  explicit StructuralModuleResolver(zc::Own<Impl>&& impl) noexcept;
  ZC_NODISCARD bool verifiesReceipt(const ModuleDependencyRequest& request,
                                    const VerifiedStructuralResolutionReceipt& receipt) const;
  ZC_NODISCARD zc::Maybe<zc::Array<uint8_t>> requestKey(
      const ModuleDependencyRequest& request) const;
  ZC_NODISCARD zc::Maybe<identity::ModuleId> moduleForKey(const identity::ModuleKey& key) const;
  ZC_NODISCARD zc::Maybe<identity::ModuleCatalogPathBucket> readCatalogPathBucket(
      const identity::CrateKey& crate, zc::ArrayPtr<const identity::ModulePathSegment> path) const;
  zc::Own<Impl> impl;

  friend class ModuleGraphVerifier;
};

}  // namespace zomlang::compiler::binder
