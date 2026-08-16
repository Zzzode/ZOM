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

#include "zc/core/common.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/binder/binding-metadata.h"

namespace zomlang::compiler::binder {

/// \brief Canonical binding name projected without privileged BindingNameKey construction.
class ImportBindingNameProjection final {
public:
  ImportBindingNameProjection(Namespace nameSpace,
                              identity::DeclaredDefinitionName&& name) noexcept;
  ImportBindingNameProjection(ImportBindingNameProjection&&) noexcept = default;
  ImportBindingNameProjection& operator=(ImportBindingNameProjection&&) noexcept = default;
  ZC_DISALLOW_COPY(ImportBindingNameProjection);

  ZC_NODISCARD ImportBindingNameProjection clone() const;
  ZC_NODISCARD Namespace nameSpace() const noexcept;
  ZC_NODISCARD const identity::DeclaredDefinitionName& name() const noexcept;

private:
  Namespace namespaceValue;
  identity::DeclaredDefinitionName nameValue;
};

/// \brief Exact module-scope binding projected before privileged scope mutation.
struct ModuleScopeBindingProjection final {
  ModuleScopeBindingProjection(ast::NodeId node, ImportBindingNameProjection&& name,
                               BindingTarget&& bindingIdentity, BindingTarget&& canonicalTarget,
                               BindingOrigin origin, identity::SourceSpan&& declarationSpan,
                               zc::Maybe<identity::SourceSpan>&& aliasSpan,
                               identity::SourceSpan&& canonicalDeclarationSpan,
                               zc::Vector<ReexportProvenanceStep>&& reexportChain) noexcept;
  ModuleScopeBindingProjection(ModuleScopeBindingProjection&&) noexcept = default;
  ModuleScopeBindingProjection& operator=(ModuleScopeBindingProjection&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleScopeBindingProjection);

  ZC_NODISCARD ModuleScopeBindingProjection clone() const;

  ast::NodeId node;
  ImportBindingNameProjection name;
  NameBinding binding;
  identity::SourceSpan canonicalDeclarationSpan;
  zc::Vector<ReexportProvenanceStep> reexportChain;
};

/// \brief One current-module surface entry projected without privileged surface publication.
struct ImportSurfaceSeed final {
  ImportSurfaceSeed(ImportBindingNameProjection&& name, BindingTarget&& bindingIdentity,
                    BindingTarget&& canonicalTarget, VisibilityEnvelope&& visibility, bool exported,
                    identity::SourceSpan&& bindingSpan,
                    identity::SourceSpan&& canonicalDeclarationSpan,
                    zc::Maybe<identity::SourceSpan>&& aliasSpan,
                    zc::Maybe<identity::SourceSpan>&& exportSpan,
                    zc::Vector<ReexportProvenanceStep>&& reexportChain) noexcept;
  ImportSurfaceSeed(ImportSurfaceSeed&&) noexcept = default;
  ImportSurfaceSeed& operator=(ImportSurfaceSeed&&) noexcept = default;
  ZC_DISALLOW_COPY(ImportSurfaceSeed);

  ImportBindingNameProjection name;
  BindingTarget bindingIdentity;
  BindingTarget canonicalTarget;
  VisibilityEnvelope visibility;
  bool exported;
  identity::SourceSpan bindingSpan;
  identity::SourceSpan canonicalDeclarationSpan;
  zc::Maybe<identity::SourceSpan> aliasSpan;
  zc::Maybe<identity::SourceSpan> exportSpan;
  zc::Vector<ReexportProvenanceStep> reexportChain;
};

/// \brief Verified module-alias fields consumed by the pure import projection.
struct ResolvedModuleAliasProjection final {
  ResolvedModuleAliasProjection(ast::NodeId node, uint32_t schemaPreorderOrdinal,
                                identity::DefId alias, ImportBindingNameProjection&& localName,
                                identity::ModuleId target,
                                ModuleAliasExportNamesRevision targetExportNamesRevision,
                                identity::SourceSpan&& declarationSpan,
                                identity::SourceSpan&& targetSpan, bool exported) noexcept;
  ResolvedModuleAliasProjection(ResolvedModuleAliasProjection&&) noexcept = default;
  ResolvedModuleAliasProjection& operator=(ResolvedModuleAliasProjection&&) noexcept = default;
  ZC_DISALLOW_COPY(ResolvedModuleAliasProjection);

  ast::NodeId node;
  uint32_t schemaPreorderOrdinal;
  identity::DefId alias;
  ImportBindingNameProjection localName;
  identity::ModuleId target;
  ModuleAliasExportNamesRevision targetExportNamesRevision;
  identity::SourceSpan declarationSpan;
  identity::SourceSpan targetSpan;
  bool exported;
};

/// \brief Verified selected import or foreign re-export fields consumed by projection.
struct ResolvedImportBindingProjection final {
  ResolvedImportBindingProjection(
      ast::NodeId node, uint32_t schemaPreorderOrdinal,
      identity::ImportBindingKey&& binding, ImportBindingNameProjection&& localName,
      BindingTarget&& canonicalTarget, identity::ModuleId sourceModule,
      ExportSurfaceRevision sourceRevision, ImportBindingKind kind,
      identity::SourceSpan&& declarationSpan, zc::Maybe<identity::SourceSpan>&& aliasSpan,
      identity::SourceSpan&& canonicalDeclarationSpan, zc::Maybe<identity::SourceSpan>&& exportSpan,
      zc::Vector<ReexportProvenanceStep>&& sourceReexportChain) noexcept;
  ResolvedImportBindingProjection(ResolvedImportBindingProjection&&) noexcept = default;
  ResolvedImportBindingProjection& operator=(ResolvedImportBindingProjection&&) noexcept = default;
  ZC_DISALLOW_COPY(ResolvedImportBindingProjection);

  ast::NodeId node;
  uint32_t schemaPreorderOrdinal;
  identity::ImportBindingKey binding;
  ImportBindingNameProjection localName;
  BindingTarget canonicalTarget;
  identity::ModuleId sourceModule;
  ExportSurfaceRevision sourceRevision;
  ImportBindingKind kind;
  identity::SourceSpan declarationSpan;
  zc::Maybe<identity::SourceSpan> aliasSpan;
  identity::SourceSpan canonicalDeclarationSpan;
  zc::Maybe<identity::SourceSpan> exportSpan;
  zc::Vector<ReexportProvenanceStep> sourceReexportChain;
};

/// \brief One local export specifier projected for current-module lookup.
struct LocalExportBindingProjection final {
  LocalExportBindingProjection(ast::NodeId node, uint32_t schemaPreorderOrdinal,
                               identity::SemanticIdentifier&& sourceName,
                               identity::SemanticIdentifier&& exportedName,
                               identity::SourceSpan&& sourceNameSpan,
                               identity::SourceSpan&& declarationSpan,
                               zc::Maybe<identity::SourceSpan>&& aliasSpan,
                               identity::SourceSpan&& exportSpan) noexcept;
  LocalExportBindingProjection(LocalExportBindingProjection&&) noexcept = default;
  LocalExportBindingProjection& operator=(LocalExportBindingProjection&&) noexcept = default;
  ZC_DISALLOW_COPY(LocalExportBindingProjection);

  ast::NodeId node;
  uint32_t schemaPreorderOrdinal;
  identity::SemanticIdentifier sourceName;
  identity::SemanticIdentifier exportedName;
  identity::SourceSpan sourceNameSpan;
  identity::SourceSpan declarationSpan;
  zc::Maybe<identity::SourceSpan> aliasSpan;
  identity::SourceSpan exportSpan;
};

/// \brief One binder-family failure plus its normalized diagnostic argument.
struct ImportBindingFailureProjection final {
  ImportBindingFailureProjection(ast::NodeId node, identity::DeclaredDefinitionName&& name,
                                 BindingFailureRef&& failure) noexcept;
  ImportBindingFailureProjection(ImportBindingFailureProjection&&) noexcept = default;
  ImportBindingFailureProjection& operator=(ImportBindingFailureProjection&&) noexcept = default;
  ZC_DISALLOW_COPY(ImportBindingFailureProjection);

  ast::NodeId node;
  identity::DeclaredDefinitionName name;
  BindingFailureRef failure;
};

/// \brief Complete verified fields required by the dependency-binding projection.
struct ImportBindingProjectionInput final {
  identity::ModuleId currentModule;
  zc::Vector<ModuleScopeBindingProjection> existingModuleBindings;
  zc::Vector<ResolvedModuleAliasProjection> moduleAliases;
  zc::Vector<ResolvedImportBindingProjection> imports;
  zc::Vector<LocalExportBindingProjection> localExports;
};

/// \brief Closed output of RFC 0004 import and re-export semantic projection.
struct ImportBindingProjectionCandidate final {
  zc::Vector<ModuleAliasBindingFact> moduleAliases;
  zc::Vector<ImportBindingFact> imports;
  zc::Vector<LocalExportFact> localExports;
  zc::Vector<ModuleScopeBindingProjection> moduleScopeBindings;
  zc::Vector<ImportSurfaceSeed> surfaceSeeds;
  zc::Vector<ImportBindingFailureProjection> sourceFailures;
};

using ImportBindingProjectionResult =
    zc::OneOf<ImportBindingProjectionCandidate, BinderInvariantFact>;

/// \brief Pure RFC 0004 producer between verified dependency handoff and body binding.
class ImportBindingProjector final {
public:
  /// \brief Projects aliases, imports, and local exports without filesystem or graph access.
  /// \param input Complete context-verified handoff fields and existing module bindings.
  /// \return Deterministic facts and seeds, or one closed ImportBinding invariant.
  ZC_NODISCARD static ImportBindingProjectionResult project(ImportBindingProjectionInput&& input);
};

}  // namespace zomlang::compiler::binder
