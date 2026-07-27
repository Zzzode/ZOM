// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zc/core/debug.h"
#include "zc/core/map.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/binder/binding-diagnostic-adapter.h"
#include "zomlang/compiler/binder/import-binding.h"
#include "zomlang/compiler/binder/internal/binding-verifier.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::binder {
namespace {

BindingTarget cloneTarget(const BindingTarget& target) { return target.clone(); }

VisibilityEnvelope cloneVisibility(const VisibilityEnvelope& visibility) {
  const auto& value = visibility.value();
  if (value.is<ModuleVisibility>()) {
    return VisibilityEnvelope::module(value.get<ModuleVisibility>().module);
  }
  return VisibilityEnvelope::external();
}

ExportSurfaceEntry cloneEntry(const ExportSurfaceEntry& entry) {
  zc::Maybe<identity::SourceSpan> aliasSpan;
  ZC_IF_SOME(value, entry.aliasSpan) { aliasSpan = value.clone(); }
  zc::Maybe<identity::SourceSpan> exportSpan;
  ZC_IF_SOME(value, entry.exportSpan) { exportSpan = value.clone(); }
  zc::Vector<ReexportProvenanceStep> chain;
  for (const auto& step : entry.reexportChain) {
    chain.add(ReexportProvenanceStep{step.module, cloneTarget(step.bindingIdentity),
                                     cloneTarget(step.canonicalTarget), step.exportSpan.clone()});
  }
  return ExportSurfaceEntry(
      entry.name.clone(), cloneTarget(entry.bindingIdentity), cloneTarget(entry.canonicalTarget),
      cloneVisibility(entry.visibility), entry.exported, entry.bindingSpan.clone(),
      entry.canonicalDeclarationSpan.clone(), zc::mv(aliasSpan), zc::mv(exportSpan), zc::mv(chain));
}

}  // namespace

ExportSurfaceCandidate::ExportSurfaceCandidate(identity::ModuleId sourceModule,
                                               identity::CompilationUnitId sourceCompilationUnit,
                                               ExportSurfaceRevision revision,
                                               zc::Vector<ExportSurfaceEntry>&& visibleEntries,
                                               zc::Vector<ExportSurfaceEntry>&& exports) noexcept
    : sourceModule(sourceModule),
      sourceCompilationUnit(sourceCompilationUnit),
      revision(revision),
      visibleEntries(zc::mv(visibleEntries)),
      exports(zc::mv(exports)) {}

ExportSurfaceCandidate ExportSurfaceCandidate::clone() const {
  zc::Vector<ExportSurfaceEntry> visible;
  for (const auto& entry : visibleEntries) { visible.add(cloneEntry(entry)); }
  zc::Vector<ExportSurfaceEntry> external;
  for (const auto& entry : exports) { external.add(cloneEntry(entry)); }
  return ExportSurfaceCandidate(sourceModule, sourceCompilationUnit, revision, zc::mv(visible),
                                zc::mv(external));
}

BindingMetadataCandidate::BindingMetadataCandidate(identity::SemanticContextBrand semanticContext,
                                                   identity::ModuleId module,
                                                   zc::Vector<NodeScopeFact>&& nodeScopes,
                                                   zc::Vector<DefinitionFact>&& definitions,
                                                   zc::Vector<ImplBindingFact>&& impls,
                                                   zc::Vector<ScopeRecord>&& scopes,
                                                   ExportSurfaceCandidate&& currentSurface) noexcept
    : semanticContext(semanticContext),
      module(module),
      nodeScopes(zc::mv(nodeScopes)),
      definitions(zc::mv(definitions)),
      impls(zc::mv(impls)),
      scopes(zc::mv(scopes)),
      currentSurface(zc::mv(currentSurface)) {}

VerifiedBindingOutput::VerifiedBindingOutput(VerifiedBindingMetadata&& metadata,
                                             VerifiedExportSurface&& surface) noexcept
    : metadata(zc::mv(metadata)), surface(zc::mv(surface)) {}

SourceRejected::SourceRejected(zc::Vector<BindingFailureRef>&& failures) noexcept
    : failureValues(zc::mv(failures)) {}
zc::ArrayPtr<const BindingFailureRef> SourceRejected::failures() const noexcept {
  return failureValues.asPtr();
}

BindingVerificationFailure::BindingVerificationFailure(
    BindingVerificationFailureValue&& value) noexcept
    : value(zc::mv(value)) {}

InvariantRejected::InvariantRejected(zc::Vector<BindingVerificationFailure>&& failures) noexcept
    : failureValues(zc::mv(failures)) {}
InvariantRejected InvariantRejected::single(BindingVerificationFailure&& failure) {
  zc::Vector<BindingVerificationFailure> failures;
  failures.add(zc::mv(failure));
  return InvariantRejected(zc::mv(failures));
}
zc::ArrayPtr<const BindingVerificationFailure> InvariantRejected::failures() const noexcept {
  return failureValues.asPtr();
}

struct VerifiedBindingMetadata::Impl final {
  explicit Impl(BindingMetadataCandidate&& candidate) : candidate(zc::mv(candidate)) {}
  BindingMetadataCandidate candidate;
};

VerifiedBindingMetadata::VerifiedBindingMetadata(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
VerifiedBindingMetadata::~VerifiedBindingMetadata() noexcept(false) = default;
VerifiedBindingMetadata::VerifiedBindingMetadata(VerifiedBindingMetadata&&) noexcept = default;
VerifiedBindingMetadata& VerifiedBindingMetadata::operator=(VerifiedBindingMetadata&&) noexcept =
    default;
identity::SemanticContextBrand VerifiedBindingMetadata::semanticContext() const noexcept {
  return impl->candidate.semanticContext;
}
identity::ModuleId VerifiedBindingMetadata::module() const noexcept {
  return impl->candidate.module;
}
#define ZOM_BINDING_ACCESSOR_Internal(type, member, accessor)
#define ZOM_BINDING_ACCESSOR_Published(type, member, accessor)         \
  zc::ArrayPtr<const type> VerifiedBindingMetadata::accessor() const { \
    return impl->candidate.member.asPtr();                             \
  }
#define ZOM_BINDING_FACT(id, type, member, accessor, publication, tag, domain, mutations, test) \
  ZOM_BINDING_ACCESSOR_##publication(type, member, accessor)
#include "zomlang/compiler/binder/binding-fact-schema.def"
#undef ZOM_BINDING_FACT
#undef ZOM_BINDING_ACCESSOR_Published
#undef ZOM_BINDING_ACCESSOR_Internal

struct VerifiedExportSurface::Impl final {
  explicit Impl(ExportSurfaceCandidate&& candidate) : candidate(zc::mv(candidate)) {}
  ExportSurfaceCandidate candidate;
};

VerifiedExportSurface::VerifiedExportSurface(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
VerifiedExportSurface::~VerifiedExportSurface() noexcept(false) = default;
VerifiedExportSurface::VerifiedExportSurface(VerifiedExportSurface&&) noexcept = default;
VerifiedExportSurface& VerifiedExportSurface::operator=(VerifiedExportSurface&&) noexcept = default;
VerifiedExportSurface VerifiedExportSurface::clone() const {
  return VerifiedExportSurface(zc::heap<Impl>(impl->candidate.clone()));
}
identity::ModuleId VerifiedExportSurface::sourceModule() const noexcept {
  return impl->candidate.sourceModule;
}
identity::CompilationUnitId VerifiedExportSurface::sourceCompilationUnit() const noexcept {
  return impl->candidate.sourceCompilationUnit;
}
const ExportSurfaceRevision& VerifiedExportSurface::revision() const noexcept {
  return impl->candidate.revision;
}
zc::ArrayPtr<const ExportSurfaceEntry> VerifiedExportSurface::visibleEntries() const {
  return impl->candidate.visibleEntries.asPtr();
}
zc::ArrayPtr<const ExportSurfaceEntry> VerifiedExportSurface::exports() const {
  return impl->candidate.exports.asPtr();
}

VerifiedBindingOutput BindingVerifier::publishCandidate(BindingMetadataCandidate&& candidate) {
  auto surface = candidate.currentSurface.clone();
  return VerifiedBindingOutput(
      VerifiedBindingMetadata(zc::heap<VerifiedBindingMetadata::Impl>(zc::mv(candidate))),
      VerifiedExportSurface(zc::heap<VerifiedExportSurface::Impl>(zc::mv(surface))));
}

}  // namespace zomlang::compiler::binder
