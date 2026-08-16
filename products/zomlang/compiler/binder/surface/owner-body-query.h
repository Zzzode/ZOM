// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zomlang/compiler/ast/kinds.h"
#include "zomlang/compiler/binder/metadata/binding-metadata.h"
#include "zomlang/compiler/binder/identity/local-identity.h"
#include "zomlang/compiler/binder/surface/module-body-syntax.h"
#include "zomlang/compiler/binder/stable/stable-binding-facts.h"

namespace zomlang::compiler::binder {

namespace owner_body_query_detail {
struct OwnerBodySyntaxTraversalData;
struct OwnerBodyScopeProjectionData;
struct OwnerBodyBindingProjectionData;
struct OwnerBodyShadowProjectionData;
struct OwnerBodyLookupProjectionData;
struct OwnerBodySelfTypeProjectionData;
struct OwnerBodyReceiverProjectionData;
struct OwnerBodyDeferredMemberProjectionData;
struct OwnerBodyClosureProjectionData;
struct OwnerBodyFreeVariableProjectionData;
struct OwnerBodyExplicitCaptureProjectionData;
struct OwnerBodyLabelProjectionData;
struct OwnerBodyControlProjectionData;
}  // namespace owner_body_query_detail

/// \brief One detached body node with its stable structural location.
struct OwnerBodySyntaxPathEntry final {
  LocalSyntaxPath path;
  uint32_t nodeIndex;
  uint32_t parentIndex;
  uint32_t rootIndex;
  uint32_t childCount;
  DetachedModuleBodyNodeKind kind;
  ast::SyntaxKind syntaxKind;
  zc::Maybe<ScopeKind> scopeKind;
};

/// \brief Rebuilds stable local paths and structural ancestry from detached body syntax.
class OwnerBodySyntaxTraversal final {
public:
  ~OwnerBodySyntaxTraversal() noexcept(false);
  OwnerBodySyntaxTraversal(OwnerBodySyntaxTraversal&&) noexcept;
  OwnerBodySyntaxTraversal& operator=(OwnerBodySyntaxTraversal&&) noexcept;
  ZC_DISALLOW_COPY(OwnerBodySyntaxTraversal);

  /// \brief Reconstructs the canonical structural path for every detached node.
  ZC_NODISCARD static zc::Maybe<OwnerBodySyntaxTraversal> from(const ModuleBodySyntax& syntax);
  /// \brief Returns nodes in detached syntax preorder.
  ZC_NODISCARD zc::ArrayPtr<const OwnerBodySyntaxPathEntry> entries() const noexcept;

private:
  explicit OwnerBodySyntaxTraversal(
      zc::Own<owner_body_query_detail::OwnerBodySyntaxTraversalData>&& impl) noexcept;

  zc::Own<owner_body_query_detail::OwnerBodySyntaxTraversalData> impl;
};

/// \brief Independent scope and node-scope facts for one detached owner body.
class OwnerBodyScopeProjection final {
public:
  ~OwnerBodyScopeProjection() noexcept(false);
  OwnerBodyScopeProjection(OwnerBodyScopeProjection&&) noexcept;
  OwnerBodyScopeProjection& operator=(OwnerBodyScopeProjection&&) noexcept;
  ZC_DISALLOW_COPY(OwnerBodyScopeProjection);

  /// \brief Rebuilds body-local scopes from detached syntax and an owning skeleton scope.
  ZC_NODISCARD static zc::Maybe<OwnerBodyScopeProjection> from(
      const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
      const StableScopeOwnerKey& rootScope);
  /// \brief Rebuilds body-local scopes from the exact owning module skeleton.
  ZC_NODISCARD static zc::Maybe<OwnerBodyScopeProjection> fromSkeleton(
      const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
      const BoundModuleSkeleton& skeleton);
  /// \brief Independently reconstructs and checks canonical scope projections.
  ZC_NODISCARD static bool verify(const StableOwnerBodyQueryKey& owner,
                                  const ModuleBodySyntax& syntax,
                                  const StableScopeOwnerKey& rootScope,
                                  const CanonicalSequence<StableBodyScopeFact>& scopes,
                                  const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes);
  /// \brief Independently selects the skeleton root and checks canonical scope projections.
  ZC_NODISCARD static bool verifyFromSkeleton(
      const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
      const BoundModuleSkeleton& skeleton, const CanonicalSequence<StableBodyScopeFact>& scopes,
      const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes);
  ZC_NODISCARD const CanonicalSequence<StableBodyScopeFact>& scopes() const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes() const noexcept;

private:
  explicit OwnerBodyScopeProjection(
      zc::Own<owner_body_query_detail::OwnerBodyScopeProjectionData>&& impl) noexcept;

  zc::Own<owner_body_query_detail::OwnerBodyScopeProjectionData> impl;
};

/// \brief Projects local declaration facts from detached owner-body syntax.
class OwnerBodyBindingProjection final {
public:
  ~OwnerBodyBindingProjection() noexcept(false);
  OwnerBodyBindingProjection(OwnerBodyBindingProjection&&) noexcept;
  OwnerBodyBindingProjection& operator=(OwnerBodyBindingProjection&&) noexcept;
  ZC_DISALLOW_COPY(OwnerBodyBindingProjection);

  /// \brief Rebuilds local declaration facts from canonical detached syntax.
  ZC_NODISCARD static zc::Maybe<OwnerBodyBindingProjection> from(
      const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
      const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes);
  /// \brief Independently validates canonical local declaration facts.
  ZC_NODISCARD static bool verify(const StableOwnerBodyQueryKey& owner,
                                  const ModuleBodySyntax& syntax,
                                  const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes,
                                  const CanonicalSequence<StableOwnerLocalBindingFact>& bindings);
  ZC_NODISCARD const CanonicalSequence<StableOwnerLocalBindingFact>& bindings() const noexcept;

private:
  explicit OwnerBodyBindingProjection(
      zc::Own<owner_body_query_detail::OwnerBodyBindingProjectionData>&& impl) noexcept;

  zc::Own<owner_body_query_detail::OwnerBodyBindingProjectionData> impl;
};

/// \brief Projects lexical shadow relations between body-local bindings.
class OwnerBodyShadowProjection final {
public:
  ~OwnerBodyShadowProjection() noexcept(false);
  OwnerBodyShadowProjection(OwnerBodyShadowProjection&&) noexcept;
  OwnerBodyShadowProjection& operator=(OwnerBodyShadowProjection&&) noexcept;
  ZC_DISALLOW_COPY(OwnerBodyShadowProjection);

  /// \brief Rebuilds canonical shadow facts from body scope ancestry and declaration order.
  ZC_NODISCARD static zc::Maybe<OwnerBodyShadowProjection> from(
      const StableOwnerBodyQueryKey& owner, const CanonicalSequence<StableBodyScopeFact>& scopes,
      const CanonicalSequence<StableOwnerLocalBindingFact>& bindings);
  /// \brief Independently validates canonical shadow facts.
  ZC_NODISCARD static bool verify(const StableOwnerBodyQueryKey& owner,
                                  const CanonicalSequence<StableBodyScopeFact>& scopes,
                                  const CanonicalSequence<StableOwnerLocalBindingFact>& bindings,
                                  const CanonicalSequence<StableShadowTargetFact>& shadows);
  ZC_NODISCARD const CanonicalSequence<StableShadowTargetFact>& shadows() const noexcept;

private:
  explicit OwnerBodyShadowProjection(
      zc::Own<owner_body_query_detail::OwnerBodyShadowProjectionData>&& impl) noexcept;

  zc::Own<owner_body_query_detail::OwnerBodyShadowProjectionData> impl;
};

/// \brief Projects direct lexical identifier lookup results from one detached owner body.
class OwnerBodyLookupProjection final {
public:
  ~OwnerBodyLookupProjection() noexcept(false);
  OwnerBodyLookupProjection(OwnerBodyLookupProjection&&) noexcept;
  OwnerBodyLookupProjection& operator=(OwnerBodyLookupProjection&&) noexcept;
  ZC_DISALLOW_COPY(OwnerBodyLookupProjection);

  /// \brief Resolves direct identifier expressions against body and skeleton lexical scopes.
  ZC_NODISCARD static zc::Maybe<OwnerBodyLookupProjection> from(
      const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
      const BoundModuleSkeleton& skeleton, const CanonicalSequence<StableBodyScopeFact>& scopes,
      const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes,
      const CanonicalSequence<StableOwnerLocalBindingFact>& bindings);
  /// \brief Independently validates direct identifier lookup and failure projections.
  ZC_NODISCARD static bool verify(const StableOwnerBodyQueryKey& owner,
                                  const ModuleBodySyntax& syntax,
                                  const BoundModuleSkeleton& skeleton,
                                  const CanonicalSequence<StableBodyScopeFact>& scopes,
                                  const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes,
                                  const CanonicalSequence<StableOwnerLocalBindingFact>& bindings,
                                  const CanonicalSequence<StableResolutionFact>& resolutions,
                                  const CanonicalSequence<StableFailedLookupFact>& failedLookups);
  ZC_NODISCARD const CanonicalSequence<StableResolutionFact>& resolutions() const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableFailedLookupFact>& failedLookups() const noexcept;

private:
  explicit OwnerBodyLookupProjection(
      zc::Own<owner_body_query_detail::OwnerBodyLookupProjectionData>&& impl) noexcept;

  zc::Own<owner_body_query_detail::OwnerBodyLookupProjectionData> impl;
};

/// \brief Projects contextual Self type uses from the owning definition chain.
class OwnerBodySelfTypeProjection final {
public:
  ~OwnerBodySelfTypeProjection() noexcept(false);
  OwnerBodySelfTypeProjection(OwnerBodySelfTypeProjection&&) noexcept;
  OwnerBodySelfTypeProjection& operator=(OwnerBodySelfTypeProjection&&) noexcept;
  ZC_DISALLOW_COPY(OwnerBodySelfTypeProjection);

  /// \brief Rebuilds Self type facts from detached syntax and stable declaration owners.
  ZC_NODISCARD static zc::Maybe<OwnerBodySelfTypeProjection> from(
      const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
      const BoundModuleSkeleton& skeleton);
  /// \brief Independently validates contextual Self facts.
  ZC_NODISCARD static bool verify(const StableOwnerBodyQueryKey& owner,
                                  const ModuleBodySyntax& syntax,
                                  const BoundModuleSkeleton& skeleton,
                                  const CanonicalSequence<StableSelfTypeFact>& selfTypes);
  ZC_NODISCARD const CanonicalSequence<StableSelfTypeFact>& selfTypes() const noexcept;

private:
  explicit OwnerBodySelfTypeProjection(
      zc::Own<owner_body_query_detail::OwnerBodySelfTypeProjectionData>&& impl) noexcept;

  zc::Own<owner_body_query_detail::OwnerBodySelfTypeProjectionData> impl;
};

/// \brief Projects receiver-bound this expressions from one detached owner body.
class OwnerBodyReceiverProjection final {
public:
  ~OwnerBodyReceiverProjection() noexcept(false);
  OwnerBodyReceiverProjection(OwnerBodyReceiverProjection&&) noexcept;
  OwnerBodyReceiverProjection& operator=(OwnerBodyReceiverProjection&&) noexcept;
  ZC_DISALLOW_COPY(OwnerBodyReceiverProjection);

  /// \brief Selects the exact receiver parameter for every this expression.
  ZC_NODISCARD static zc::Maybe<OwnerBodyReceiverProjection> from(
      const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
      const BoundModuleSkeleton& skeleton);
  /// \brief Independently selects and validates every receiver-bound this expression.
  ZC_NODISCARD static bool verify(const StableOwnerBodyQueryKey& owner,
                                  const ModuleBodySyntax& syntax,
                                  const BoundModuleSkeleton& skeleton,
                                  const CanonicalSequence<StableThisBindingFact>& bindings);
  ZC_NODISCARD const CanonicalSequence<StableThisBindingFact>& bindings() const noexcept;

private:
  explicit OwnerBodyReceiverProjection(
      zc::Own<owner_body_query_detail::OwnerBodyReceiverProjectionData>&& impl) noexcept;

  zc::Own<owner_body_query_detail::OwnerBodyReceiverProjectionData> impl;
};

/// \brief Projects deferred member lookups from detached owner-body syntax.
class OwnerBodyDeferredMemberProjection final {
public:
  ~OwnerBodyDeferredMemberProjection() noexcept(false);
  OwnerBodyDeferredMemberProjection(OwnerBodyDeferredMemberProjection&&) noexcept;
  OwnerBodyDeferredMemberProjection& operator=(OwnerBodyDeferredMemberProjection&&) noexcept;
  ZC_DISALLOW_COPY(OwnerBodyDeferredMemberProjection);

  ZC_NODISCARD static zc::Maybe<OwnerBodyDeferredMemberProjection> from(
      const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax);
  ZC_NODISCARD static bool verify(
      const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
      const CanonicalSequence<StableDeferredMemberFact>& deferredMembers);
  ZC_NODISCARD const CanonicalSequence<StableDeferredMemberFact>& deferredMembers() const noexcept;

private:
  explicit OwnerBodyDeferredMemberProjection(
      zc::Own<owner_body_query_detail::OwnerBodyDeferredMemberProjectionData>&& impl) noexcept;

  zc::Own<owner_body_query_detail::OwnerBodyDeferredMemberProjectionData> impl;
};

/// \brief Projects closure declarations from detached owner-body syntax.
class OwnerBodyClosureProjection final {
public:
  ~OwnerBodyClosureProjection() noexcept(false);
  OwnerBodyClosureProjection(OwnerBodyClosureProjection&&) noexcept;
  OwnerBodyClosureProjection& operator=(OwnerBodyClosureProjection&&) noexcept;
  ZC_DISALLOW_COPY(OwnerBodyClosureProjection);

  /// \brief Rebuilds closure facts before lexical lookup projection.
  ZC_NODISCARD static zc::Maybe<OwnerBodyClosureProjection> from(
      const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
      const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes);
  /// \brief Independently validates canonical closure facts before lookup projection.
  ZC_NODISCARD static bool verify(const StableOwnerBodyQueryKey& owner,
                                  const ModuleBodySyntax& syntax,
                                  const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes,
                                  const CanonicalSequence<StableClosureFact>& closures);
  ZC_NODISCARD const CanonicalSequence<StableClosureFact>& closures() const noexcept;

private:
  explicit OwnerBodyClosureProjection(
      zc::Own<owner_body_query_detail::OwnerBodyClosureProjectionData>&& impl) noexcept;

  zc::Own<owner_body_query_detail::OwnerBodyClosureProjectionData> impl;
};

/// \brief Projects inferred closure free variables from verified lexical resolutions.
class OwnerBodyFreeVariableProjection final {
public:
  ~OwnerBodyFreeVariableProjection() noexcept(false);
  OwnerBodyFreeVariableProjection(OwnerBodyFreeVariableProjection&&) noexcept;
  OwnerBodyFreeVariableProjection& operator=(OwnerBodyFreeVariableProjection&&) noexcept;
  ZC_DISALLOW_COPY(OwnerBodyFreeVariableProjection);

  /// \brief Rebuilds free-variable facts for every closure crossed by a capturable resolution.
  ZC_NODISCARD static zc::Maybe<OwnerBodyFreeVariableProjection> from(
      const StableOwnerBodyQueryKey& owner, const BoundModuleSkeleton& skeleton,
      const CanonicalSequence<StableBodyScopeFact>& scopes,
      const CanonicalSequence<StableOwnerLocalBindingFact>& bindings,
      const CanonicalSequence<StableClosureFact>& closures,
      const CanonicalSequence<StableResolutionFact>& resolutions);
  /// \brief Independently validates inferred closure free variables.
  ZC_NODISCARD static bool verify(
      const StableOwnerBodyQueryKey& owner, const BoundModuleSkeleton& skeleton,
      const CanonicalSequence<StableBodyScopeFact>& scopes,
      const CanonicalSequence<StableOwnerLocalBindingFact>& bindings,
      const CanonicalSequence<StableClosureFact>& closures,
      const CanonicalSequence<StableResolutionFact>& resolutions,
      const CanonicalSequence<StableClosureFreeVariableFact>& freeVariables);
  ZC_NODISCARD const CanonicalSequence<StableClosureFreeVariableFact>& freeVariables()
      const noexcept;

private:
  explicit OwnerBodyFreeVariableProjection(
      zc::Own<owner_body_query_detail::OwnerBodyFreeVariableProjectionData>&& impl) noexcept;

  zc::Own<owner_body_query_detail::OwnerBodyFreeVariableProjectionData> impl;
};

/// \brief Projects explicit function-expression captures from lexical body evidence.
class OwnerBodyExplicitCaptureProjection final {
public:
  ~OwnerBodyExplicitCaptureProjection() noexcept(false);
  OwnerBodyExplicitCaptureProjection(OwnerBodyExplicitCaptureProjection&&) noexcept;
  OwnerBodyExplicitCaptureProjection& operator=(OwnerBodyExplicitCaptureProjection&&) noexcept;
  ZC_DISALLOW_COPY(OwnerBodyExplicitCaptureProjection);

  ZC_NODISCARD static zc::Maybe<OwnerBodyExplicitCaptureProjection> from(
      const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
      const BoundModuleSkeleton& skeleton, const CanonicalSequence<StableBodyScopeFact>& scopes,
      const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes,
      const CanonicalSequence<StableOwnerLocalBindingFact>& bindings,
      const CanonicalSequence<StableClosureFact>& closures);
  ZC_NODISCARD static bool verify(
      const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
      const BoundModuleSkeleton& skeleton, const CanonicalSequence<StableBodyScopeFact>& scopes,
      const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes,
      const CanonicalSequence<StableOwnerLocalBindingFact>& bindings,
      const CanonicalSequence<StableClosureFact>& closures,
      const CanonicalSequence<StableExplicitClosureCaptureFact>& captures);
  ZC_NODISCARD const CanonicalSequence<StableExplicitClosureCaptureFact>& captures() const noexcept;

private:
  explicit OwnerBodyExplicitCaptureProjection(
      zc::Own<owner_body_query_detail::OwnerBodyExplicitCaptureProjectionData>&& impl) noexcept;

  zc::Own<owner_body_query_detail::OwnerBodyExplicitCaptureProjectionData> impl;
};

/// \brief Projects labeled block and loop targets from detached owner-body syntax.
class OwnerBodyLabelProjection final {
public:
  ~OwnerBodyLabelProjection() noexcept(false);
  OwnerBodyLabelProjection(OwnerBodyLabelProjection&&) noexcept;
  OwnerBodyLabelProjection& operator=(OwnerBodyLabelProjection&&) noexcept;
  ZC_DISALLOW_COPY(OwnerBodyLabelProjection);

  /// \brief Rebuilds canonical label facts from detached syntax and node scopes.
  ZC_NODISCARD static zc::Maybe<OwnerBodyLabelProjection> from(
      const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
      const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes);
  /// \brief Independently validates canonical label facts.
  ZC_NODISCARD static bool verify(const StableOwnerBodyQueryKey& owner,
                                  const ModuleBodySyntax& syntax,
                                  const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes,
                                  const CanonicalSequence<StableLabelFact>& labels);
  ZC_NODISCARD const CanonicalSequence<StableLabelFact>& labels() const noexcept;

private:
  explicit OwnerBodyLabelProjection(
      zc::Own<owner_body_query_detail::OwnerBodyLabelProjectionData>&& impl) noexcept;

  zc::Own<owner_body_query_detail::OwnerBodyLabelProjectionData> impl;
};

/// \brief Projects valid break and continue targets from detached owner-body syntax.
class OwnerBodyControlProjection final {
public:
  ~OwnerBodyControlProjection() noexcept(false);
  OwnerBodyControlProjection(OwnerBodyControlProjection&&) noexcept;
  OwnerBodyControlProjection& operator=(OwnerBodyControlProjection&&) noexcept;
  ZC_DISALLOW_COPY(OwnerBodyControlProjection);

  /// \brief Rebuilds canonical control-transfer facts from detached syntax.
  ZC_NODISCARD static zc::Maybe<OwnerBodyControlProjection> from(
      const StableOwnerBodyQueryKey& owner, const ModuleBodySyntax& syntax,
      const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes,
      const CanonicalSequence<StableLabelFact>& labels);
  /// \brief Independently validates canonical control-transfer facts.
  ZC_NODISCARD static bool verify(const StableOwnerBodyQueryKey& owner,
                                  const ModuleBodySyntax& syntax,
                                  const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes,
                                  const CanonicalSequence<StableLabelFact>& labels,
                                  const CanonicalSequence<StableControlTransferFact>& transfers);
  ZC_NODISCARD const CanonicalSequence<StableControlTransferFact>& transfers() const noexcept;

private:
  explicit OwnerBodyControlProjection(
      zc::Own<owner_body_query_detail::OwnerBodyControlProjectionData>&& impl) noexcept;

  zc::Own<owner_body_query_detail::OwnerBodyControlProjectionData> impl;
};

}  // namespace zomlang::compiler::binder
