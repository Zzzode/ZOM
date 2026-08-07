// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/binding-metadata.h"

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

zc::Vector<ExportSurfaceEntry> cloneEntries(zc::ArrayPtr<const ExportSurfaceEntry> entries) {
  zc::Vector<ExportSurfaceEntry> result(entries.size());
  for (const auto& entry : entries) { result.add(cloneEntry(entry)); }
  return result;
}

}  // namespace

struct VerifiedExportSurface::Impl final {
  Impl(identity::ModuleId sourceModule, identity::CompilationUnitId sourceCompilationUnit,
       ExportSurfaceRevision revision, zc::Vector<ExportSurfaceEntry>&& visibleEntries,
       zc::Vector<ExportSurfaceEntry>&& exports) noexcept
      : sourceModule(sourceModule),
        sourceCompilationUnit(sourceCompilationUnit),
        revision(revision),
        visibleEntries(zc::mv(visibleEntries)),
        exports(zc::mv(exports)) {}

  identity::ModuleId sourceModule;
  identity::CompilationUnitId sourceCompilationUnit;
  ExportSurfaceRevision revision;
  zc::Vector<ExportSurfaceEntry> visibleEntries;
  zc::Vector<ExportSurfaceEntry> exports;
};

VerifiedExportSurface::VerifiedExportSurface(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}

VerifiedExportSurface VerifiedExportSurface::fromVerified(
    identity::ModuleId sourceModule, identity::CompilationUnitId sourceCompilationUnit,
    ExportSurfaceRevision revision, zc::Vector<ExportSurfaceEntry>&& visibleEntries,
    zc::Vector<ExportSurfaceEntry>&& exports) noexcept {
  return VerifiedExportSurface(zc::heap<Impl>(sourceModule, sourceCompilationUnit, revision,
                                              zc::mv(visibleEntries), zc::mv(exports)));
}

VerifiedExportSurface::~VerifiedExportSurface() noexcept(false) = default;
VerifiedExportSurface::VerifiedExportSurface(VerifiedExportSurface&&) noexcept = default;
VerifiedExportSurface& VerifiedExportSurface::operator=(VerifiedExportSurface&&) noexcept = default;

VerifiedExportSurface VerifiedExportSurface::clone() const {
  return VerifiedExportSurface(zc::heap<Impl>(
      impl->sourceModule, impl->sourceCompilationUnit, impl->revision,
      cloneEntries(impl->visibleEntries.asPtr()), cloneEntries(impl->exports.asPtr())));
}

identity::ModuleId VerifiedExportSurface::sourceModule() const noexcept { return impl->sourceModule; }

identity::CompilationUnitId VerifiedExportSurface::sourceCompilationUnit() const noexcept {
  return impl->sourceCompilationUnit;
}

const ExportSurfaceRevision& VerifiedExportSurface::revision() const noexcept {
  return impl->revision;
}

zc::ArrayPtr<const ExportSurfaceEntry> VerifiedExportSurface::visibleEntries() const {
  return impl->visibleEntries.asPtr();
}

zc::ArrayPtr<const ExportSurfaceEntry> VerifiedExportSurface::exports() const {
  return impl->exports.asPtr();
}

}  // namespace zomlang::compiler::binder
