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
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "compiler/ast/tree.h"
#include "compiler/driver/package/source-tree.h"
#include "compiler/identity/canonical/canonical-scalar.h"
#include "compiler/identity/key/package-key.h"
#include "compiler/source/core-source-catalog.h"

namespace zomlang::compiler::driver {

/// \brief Closed invariant branch for an empty structural module path.
class InvalidModuleSourceRequest final {
public:
  InvalidModuleSourceRequest() = default;
  InvalidModuleSourceRequest(InvalidModuleSourceRequest&&) noexcept = default;
  InvalidModuleSourceRequest& operator=(InvalidModuleSourceRequest&&) noexcept = default;
  ZC_DISALLOW_COPY(InvalidModuleSourceRequest);
};

/// \brief Closed result branch for a source tree without either module candidate.
class MissingModuleSource final {
public:
  MissingModuleSource() = default;
  MissingModuleSource(MissingModuleSource&&) noexcept = default;
  MissingModuleSource& operator=(MissingModuleSource&&) noexcept = default;
  ZC_DISALLOW_COPY(MissingModuleSource);
};

/// \brief Closed result branch containing the sole source-tree module candidate.
class ResolvedModuleSource final {
public:
  /// \brief Creates a resolved source result from its canonical package-relative path.
  /// \param path Selected path present in the frozen source-tree record.
  /// \return A move-only resolved source result.
  ZC_NODISCARD static ResolvedModuleSource from(identity::CanonicalRelativePath&& path);

  ResolvedModuleSource(ResolvedModuleSource&&) noexcept = default;
  ResolvedModuleSource& operator=(ResolvedModuleSource&&) noexcept = default;
  ZC_DISALLOW_COPY(ResolvedModuleSource);

  /// \brief Returns the selected canonical package-relative path.
  ZC_NODISCARD const identity::CanonicalRelativePath& path() const noexcept;

private:
  explicit ResolvedModuleSource(identity::CanonicalRelativePath&& path) noexcept;

  identity::CanonicalRelativePath pathValue;
};

/// \brief Closed result branch containing both canonically sorted module candidates.
class AmbiguousModuleSource final {
public:
  /// \brief Creates an ambiguous source result and sorts both canonical paths.
  /// \param first One path present in the frozen source-tree record.
  /// \param second The other path present in the frozen source-tree record.
  /// \return A move-only ambiguous source result with exactly two sorted paths.
  ZC_NODISCARD static AmbiguousModuleSource from(identity::CanonicalRelativePath&& first,
                                                 identity::CanonicalRelativePath&& second);

  AmbiguousModuleSource(AmbiguousModuleSource&&) noexcept = default;
  AmbiguousModuleSource& operator=(AmbiguousModuleSource&&) noexcept = default;
  ZC_DISALLOW_COPY(AmbiguousModuleSource);

  /// \brief Returns exactly two canonical paths sorted by canonical encoding.
  ZC_NODISCARD zc::ArrayPtr<const identity::CanonicalRelativePath> paths() const noexcept;

private:
  explicit AmbiguousModuleSource(zc::Vector<identity::CanonicalRelativePath>&& paths) noexcept;

  zc::Vector<identity::CanonicalRelativePath> pathValues;
};

using ModuleSourceDiscoveryResult = zc::OneOf<InvalidModuleSourceRequest, MissingModuleSource,
                                              ResolvedModuleSource, AmbiguousModuleSource>;

/// \brief Sole admitted source identity for one logical toolchain-core module.
class ResolvedCoreModuleSource final {
public:
  ZC_NODISCARD static ResolvedCoreModuleSource from(identity::SourceFileKey&& source,
                                                    const identity::Sha256Digest& contentDigest);

  ResolvedCoreModuleSource(ResolvedCoreModuleSource&&) noexcept = default;
  ResolvedCoreModuleSource& operator=(ResolvedCoreModuleSource&&) noexcept = default;
  ZC_DISALLOW_COPY(ResolvedCoreModuleSource);

  ZC_NODISCARD const identity::SourceFileKey& source() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& contentDigest() const noexcept;

private:
  ResolvedCoreModuleSource(identity::SourceFileKey&& source,
                           const identity::Sha256Digest& contentDigest) noexcept;

  identity::SourceFileKey sourceValue;
  identity::Sha256Digest contentDigestValue;
};

using CoreModuleSourceDiscoveryResult =
    zc::OneOf<InvalidModuleSourceRequest, MissingModuleSource, ResolvedCoreModuleSource>;

/// \brief Closed structural dependency forms admitted by the module graph.
enum class StructuralModuleDependencyKind : uint8_t {
  Import,
  ForeignReexport,
  ModuleAlias,
};

/// \brief One normalized module dependency extracted from an immutable syntax tree.
class StructuralModuleDependencyRequest final {
public:
  /// \brief Creates an immutable structural dependency request.
  /// \param kind Closed syntax form that introduced the dependency.
  /// \param path Non-empty normalized module path.
  /// \param syntaxNode Unique syntax node that introduced the dependency.
  /// \param syntaxRange Parser-owned source range for the dependency declaration.
  /// \param schemaPreorderOrdinal Stable ordinal in generated-schema preorder.
  /// \return A move-only structural dependency request.
  ZC_NODISCARD static zc::Maybe<StructuralModuleDependencyRequest> from(
      StructuralModuleDependencyKind kind, zc::Vector<identity::ModulePathSegment>&& path,
      ast::NodeId syntaxNode, source::SourceRange syntaxRange,
      uint32_t schemaPreorderOrdinal) noexcept;

  StructuralModuleDependencyRequest(StructuralModuleDependencyRequest&&) noexcept = default;
  StructuralModuleDependencyRequest& operator=(StructuralModuleDependencyRequest&&) noexcept =
      default;
  ZC_DISALLOW_COPY(StructuralModuleDependencyRequest);

  /// \brief Returns the closed dependency form.
  ZC_NODISCARD StructuralModuleDependencyKind kind() const noexcept;

  /// \brief Returns the non-empty normalized module path.
  ZC_NODISCARD zc::ArrayPtr<const identity::ModulePathSegment> normalizedPath() const noexcept;

  /// \brief Returns the unique syntax node that introduced this dependency.
  ZC_NODISCARD ast::NodeId syntaxNode() const noexcept;

  /// \brief Returns the parser-owned source range for this declaration.
  ZC_NODISCARD source::SourceRange syntaxRange() const noexcept;

  /// \brief Returns the stable generated-schema preorder ordinal.
  ZC_NODISCARD uint32_t schemaPreorderOrdinal() const noexcept;

private:
  StructuralModuleDependencyRequest(StructuralModuleDependencyKind kind,
                                    zc::Vector<identity::ModulePathSegment>&& path,
                                    ast::NodeId syntaxNode, source::SourceRange syntaxRange,
                                    uint32_t schemaPreorderOrdinal) noexcept;

  StructuralModuleDependencyKind kindValue;
  zc::Vector<identity::ModulePathSegment> pathValue;
  ast::NodeId syntaxNodeValue;
  source::SourceRange syntaxRangeValue;
  uint32_t schemaPreorderOrdinalValue;
};

/// \brief Closed failure branch for an inexact tree, path, or dependency site.
class InvalidStructuralModuleDependencyRequests final {
public:
  InvalidStructuralModuleDependencyRequests() = default;
  InvalidStructuralModuleDependencyRequests(InvalidStructuralModuleDependencyRequests&&) noexcept =
      default;
  InvalidStructuralModuleDependencyRequests& operator=(
      InvalidStructuralModuleDependencyRequests&&) noexcept = default;
  ZC_DISALLOW_COPY(InvalidStructuralModuleDependencyRequests);
};

using StructuralModuleDependencyRequestResult =
    zc::OneOf<zc::Vector<StructuralModuleDependencyRequest>,
              InvalidStructuralModuleDependencyRequests>;

/// \brief Discovers one module source within one frozen package source tree and search root.
/// \param sourceTree Frozen source inventory used without filesystem access.
/// \param searchRoot Canonical package-relative root prepended to both candidates.
/// \param modulePath Non-empty normalized module path used to form the two candidates.
/// \return Invalid for an empty path, Missing, the sole path, or both sorted ambiguous paths.
ZC_NODISCARD ModuleSourceDiscoveryResult discoverModuleSource(
    const package::SourceTreeRecord& sourceTree, const identity::CanonicalRelativePath& searchRoot,
    zc::ArrayPtr<const identity::ModulePathSegment> modulePath);

/// \brief Selects one core module only from the independently verified structural catalog.
/// \param catalog Handle-free logical catalog bound to one projected toolchain-core crate.
/// \param modulePath Complete non-empty canonical core module path.
/// \return Invalid for an empty path, Missing for no admitted entry, or the exact source identity.
ZC_NODISCARD CoreModuleSourceDiscoveryResult
discoverCoreModuleSource(const source::core::AdmittedCoreSourceCatalog& catalog,
                         zc::ArrayPtr<const identity::ModulePathSegment> modulePath);

/// \brief Purely extracts module dependencies from one immutable schema-valid syntax tree.
/// \param tree Syntax tree whose root-reachable nodes are traversed without filesystem access.
/// \return Canonically sorted requests, or Invalid for a malformed root, path, or repeated site.
ZC_NODISCARD StructuralModuleDependencyRequestResult
extractStructuralModuleDependencyRequests(const ast::Tree& tree);

}  // namespace zomlang::compiler::driver
