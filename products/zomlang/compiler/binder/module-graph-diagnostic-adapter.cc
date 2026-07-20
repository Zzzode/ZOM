// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/module-graph-diagnostic-adapter.h"

#include <climits>

#include "zc/core/string.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/source/location.h"

namespace zomlang::compiler::binder {
namespace {

struct VerifiedDiagnosticRange final {
  source::SourceLoc start;
  source::SourceLoc end;
};

zc::Maybe<VerifiedDiagnosticRange> resolveFailureRange(const VerifiedParsedModule& parsedModule,
                                                       const ModuleSyntaxDependencySite& site) {
  const auto& span = site.span;
  const auto parsedSource = parsedModule.rootSpan();
  if (!span.belongsTo(parsedSource.source()) || span.byteStart() > span.byteEnd() ||
      span.byteEnd() > parsedModule.byteLength() || span.byteEnd() > UINT_MAX) {
    return zc::none;
  }
  auto start = parsedModule.sourceLocFor(span);
  if (start == zc::none) { return zc::none; }
  ZC_IF_SOME(startValue, start) {
    const auto length = static_cast<unsigned>(span.byteEnd() - span.byteStart());
    return VerifiedDiagnosticRange{startValue, startValue.getAdvancedLoc(length)};
  }
  ZC_UNREACHABLE;
}

bool isImportDependency(identity::ModuleDependencyKind kind) {
  return kind == identity::ModuleDependencyKind::Import ||
         kind == identity::ModuleDependencyKind::ModuleAlias;
}

zc::Maybe<zc::String> renderModulePath(const ModuleDependencyRequest& request) {
  if (request.normalizedPath().size() == 0) { return zc::none; }
  zc::String path;
  bool first = true;
  for (const auto& segment : request.normalizedPath()) {
    if (!first) { path = zc::str(path, "::"_zc); }
    path = zc::str(path, segment.text());
    first = false;
  }
  return zc::mv(path);
}

}  // namespace

bool canEmitModuleGraphSourceFailure(const VerifiedParsedModule& parsedModule,
                                     const ModuleGraphSourceFailure& failure) {
  const auto& request = failure.request();
  if (request.isPrelude() || request.syntaxSites().size() == 0) { return false; }
  for (const auto& site : request.syntaxSites()) {
    if (resolveFailureRange(parsedModule, site) == zc::none) { return false; }
  }
  switch (failure.diagnostic()) {
    case ModuleGraphDiagnostic::CircularImport:
    case ModuleGraphDiagnostic::ImportModuleNotFound:
      return isImportDependency(request.kind()) && renderModulePath(request) != zc::none;
    case ModuleGraphDiagnostic::CircularReexport:
    case ModuleGraphDiagnostic::ReexportModuleNotFound:
      return request.kind() == identity::ModuleDependencyKind::ForeignReexport &&
             renderModulePath(request) != zc::none;
    case ModuleGraphDiagnostic::ImportModuleAmbiguous:
      return isImportDependency(request.kind());
    case ModuleGraphDiagnostic::ReexportModuleAmbiguous:
      return request.kind() == identity::ModuleDependencyKind::ForeignReexport;
  }
  return false;
}

bool emitModuleGraphSourceFailure(diagnostics::DiagnosticEngine& diagnostics,
                                  const VerifiedParsedModule& parsedModule,
                                  const ModuleGraphSourceFailure& failure) {
  if (!canEmitModuleGraphSourceFailure(parsedModule, failure)) { return false; }
  const auto& request = failure.request();
  const auto importDependency = isImportDependency(request.kind());
  const auto reexportDependency = request.kind() == identity::ModuleDependencyKind::ForeignReexport;
  for (const auto& site : request.syntaxSites()) {
    auto range = resolveFailureRange(parsedModule, site);
    ZC_IREQUIRE(range != zc::none, "prevalidated module diagnostic site must resolve");
    ZC_IF_SOME(rangeValue, range) {
      const auto sourceRange =
          source::CharSourceRange::getCharRange(rangeValue.start, rangeValue.end);
      using diagnostics::DiagID;
      switch (failure.diagnostic()) {
        case ModuleGraphDiagnostic::CircularImport:
        case ModuleGraphDiagnostic::ImportModuleNotFound: {
          if (!importDependency) { return false; }
          auto path = renderModulePath(request);
          if (path == zc::none) { return false; }
          ZC_IF_SOME(pathValue, path) {
            if (failure.diagnostic() == ModuleGraphDiagnostic::CircularImport) {
              auto diagnostic =
                  diagnostics.diagnose<DiagID::CircularImport>(rangeValue.start, zc::mv(pathValue));
              diagnostic.addRange(sourceRange);
              diagnostic.emit();
            } else {
              auto diagnostic = diagnostics.diagnose<DiagID::ImportModuleNotFound>(
                  rangeValue.start, zc::mv(pathValue));
              diagnostic.addRange(sourceRange);
              diagnostic.emit();
            }
            break;
          }
          ZC_UNREACHABLE;
        }
        case ModuleGraphDiagnostic::CircularReexport:
        case ModuleGraphDiagnostic::ReexportModuleNotFound: {
          if (!reexportDependency) { return false; }
          auto path = renderModulePath(request);
          if (path == zc::none) { return false; }
          ZC_IF_SOME(pathValue, path) {
            if (failure.diagnostic() == ModuleGraphDiagnostic::CircularReexport) {
              auto diagnostic = diagnostics.diagnose<DiagID::CircularReexport>(rangeValue.start,
                                                                               zc::mv(pathValue));
              diagnostic.addRange(sourceRange);
              diagnostic.emit();
            } else {
              auto diagnostic = diagnostics.diagnose<DiagID::ReexportModuleNotFound>(
                  rangeValue.start, zc::mv(pathValue));
              diagnostic.addRange(sourceRange);
              diagnostic.emit();
            }
            break;
          }
          ZC_UNREACHABLE;
        }
        case ModuleGraphDiagnostic::ImportModuleAmbiguous: {
          if (!importDependency) { return false; }
          auto diagnostic = diagnostics.diagnose<DiagID::ImportModuleAmbiguous>(rangeValue.start);
          diagnostic.addRange(sourceRange);
          diagnostic.emit();
          break;
        }
        case ModuleGraphDiagnostic::ReexportModuleAmbiguous: {
          if (!reexportDependency) { return false; }
          auto diagnostic = diagnostics.diagnose<DiagID::ReexportModuleAmbiguous>(rangeValue.start);
          diagnostic.addRange(sourceRange);
          diagnostic.emit();
          break;
        }
      }
    }
  }
  return true;
}

}  // namespace zomlang::compiler::binder
