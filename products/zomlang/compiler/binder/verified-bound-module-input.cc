// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/verified-bound-module-input.h"

#include "zomlang/compiler/binder/binding-input.h"
#include "zomlang/compiler/binder/binding-run.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::binder {
namespace {

bool sameBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  return left == right;
}

bool sameSpan(const identity::SourceSpan& left, const identity::SourceSpan& right) {
  identity::CanonicalEncoder leftEncoder;
  identity::CanonicalEncoder rightEncoder;
  left.encode(leftEncoder);
  right.encode(rightEncoder);
  return sameBytes(leftEncoder.finish().asPtr(), rightEncoder.finish().asPtr());
}

bool sameDefinitionSite(const DefinitionSite& left, const DefinitionSite& right) {
  const auto& leftValue = left.value();
  const auto& rightValue = right.value();
  if (leftValue.is<DeclarationDefinitionSite>()) {
    return rightValue.is<DeclarationDefinitionSite>() &&
           leftValue.get<DeclarationDefinitionSite>().node ==
               rightValue.get<DeclarationDefinitionSite>().node;
  }
  if (!rightValue.is<PatternBindingSite>()) return false;
  const auto& leftPattern = leftValue.get<PatternBindingSite>();
  const auto& rightPattern = rightValue.get<PatternBindingSite>();
  return leftPattern.introducer == rightPattern.introducer &&
         leftPattern.patternPath.asPtr() == rightPattern.patternPath.asPtr();
}

bool exactDefinitionInventory(const VerifiedBindingInput& input,
                              const VerifiedBindingOutput& output) {
  const auto frozen = input.definitions().definitions();
  const auto bound = output.metadata.definitions();
  if (frozen.size() != bound.size()) return false;
  for (const auto& expected : frozen) {
    size_t occurrences = 0;
    for (const auto& actual : bound) {
      if (actual.identity != expected.definition) continue;
      ++occurrences;
      if (actual.kind != expected.record.kind() || !sameSpan(actual.source, expected.source) ||
          !sameDefinitionSite(actual.site, expected.site)) {
        return false;
      }
    }
    if (occurrences != 1) return false;
  }
  const auto frozenImpls = input.definitions().impls();
  const auto boundImpls = output.metadata.impls();
  if (frozenImpls.size() != boundImpls.size()) return false;
  for (const auto& expected : frozenImpls) {
    size_t occurrences = 0;
    for (const auto& actual : boundImpls) {
      if (actual.occurrence != expected.occurrence) continue;
      ++occurrences;
      if (actual.authority != expected.authority || actual.node != expected.node ||
          !sameSpan(actual.source, expected.source)) {
        return false;
      }
    }
    if (occurrences != 1) return false;
  }
  const auto frozenGenerics = input.definitions().genericParameters();
  const auto boundGenerics = output.metadata.genericParameters();
  if (frozenGenerics.size() != boundGenerics.size()) return false;
  for (const auto& expected : frozenGenerics) {
    size_t occurrences = 0;
    for (const auto& actual : boundGenerics) {
      if (actual.identity != expected.parameter) continue;
      ++occurrences;
      if (!sameSpan(actual.source, expected.source) ||
          !sameDefinitionSite(actual.site, expected.site)) {
        return false;
      }
    }
    if (occurrences != 1) return false;
  }
  const auto frozenCallables = input.definitions().callableParameters();
  const auto boundCallables = output.metadata.callableParameters();
  if (frozenCallables.size() != boundCallables.size()) return false;
  for (const auto& expected : frozenCallables) {
    size_t occurrences = 0;
    for (const auto& actual : boundCallables) {
      if (actual.identity != expected.parameter) continue;
      ++occurrences;
      if (!sameSpan(actual.source, expected.source) ||
          !sameDefinitionSite(actual.site, expected.site)) {
        return false;
      }
    }
    if (occurrences != 1) return false;
  }
  const auto frozenLocals = input.definitions().ownerLocalBindings();
  const auto boundLocals = output.metadata.ownerLocalBindings();
  if (frozenLocals.size() != boundLocals.size()) return false;
  for (const auto& expected : frozenLocals) {
    size_t occurrences = 0;
    for (const auto& actual : boundLocals) {
      if (actual.identity != expected.binding) continue;
      ++occurrences;
      if (!sameSpan(actual.source, expected.source) ||
          !sameDefinitionSite(actual.site, expected.site)) {
        return false;
      }
    }
    if (occurrences != 1) return false;
  }
  return true;
}

}  // namespace

struct VerifiedBoundModuleInput::Impl final {
  Impl(const VerifiedBindingInput& input, const VerifiedBindingOutput& output) noexcept
      : input(input), output(output) {}

  const VerifiedBindingInput& input;
  const VerifiedBindingOutput& output;
};

VerifiedBoundModuleInput::VerifiedBoundModuleInput(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
VerifiedBoundModuleInput::~VerifiedBoundModuleInput() noexcept(false) = default;
VerifiedBoundModuleInput::VerifiedBoundModuleInput(VerifiedBoundModuleInput&&) noexcept = default;
VerifiedBoundModuleInput& VerifiedBoundModuleInput::operator=(VerifiedBoundModuleInput&&) noexcept =
    default;

zc::Maybe<VerifiedBoundModuleInput> VerifiedBoundModuleInput::from(
    const VerifiedBindingInput& input, const VerifiedBindingOutput& output) {
  if (!input.semanticContext().isValid() ||
      input.semanticContext() != output.metadata.semanticContext() ||
      input.module() != output.metadata.module() ||
      input.module() != output.surface.sourceModule() ||
      input.package() != output.surface.sourcePackage() ||
      !exactDefinitionInventory(input, output)) {
    return zc::none;
  }
  return VerifiedBoundModuleInput(zc::heap<Impl>(input, output));
}

identity::SemanticContextBrand VerifiedBoundModuleInput::semanticContext() const noexcept {
  return impl->input.semanticContext();
}
identity::PackageId VerifiedBoundModuleInput::package() const noexcept {
  return impl->input.package();
}
identity::CrateId VerifiedBoundModuleInput::crate() const noexcept { return impl->input.crate(); }
identity::ModuleId VerifiedBoundModuleInput::module() const noexcept {
  return impl->input.module();
}
const identity::SemanticContextFingerprint& VerifiedBoundModuleInput::semanticFingerprint()
    const noexcept {
  return impl->input.semanticFingerprint();
}
const ast::Tree& VerifiedBoundModuleInput::tree() const noexcept { return impl->input.tree(); }
const VerifiedParsedModule& VerifiedBoundModuleInput::parsedModule() const noexcept {
  return impl->input.parsedModule();
}
const FrozenDefinitionInventoryView& VerifiedBoundModuleInput::definitions() const noexcept {
  return impl->input.definitions();
}
zc::ArrayPtr<const VerifiedExportSurfaceView> VerifiedBoundModuleInput::dependencySurfaces()
    const noexcept {
  return impl->input.dependencySurfaces();
}
zc::Maybe<const VerifiedExportSurfaceView&> VerifiedBoundModuleInput::preludeSurface()
    const noexcept {
  return impl->input.preludeSurface();
}
zc::ArrayPtr<const ResolvedImportEdge> VerifiedBoundModuleInput::resolvedImports() const noexcept {
  return impl->input.resolvedImports();
}
zc::ArrayPtr<const ResolvedModuleAlias> VerifiedBoundModuleInput::resolvedModuleAliases()
    const noexcept {
  return impl->input.resolvedModuleAliases();
}
const VerifiedBindingMetadata& VerifiedBoundModuleInput::bindings() const noexcept {
  return impl->output.metadata;
}
const VerifiedExportSurface& VerifiedBoundModuleInput::bindingSurface() const noexcept {
  return impl->output.surface;
}

}  // namespace zomlang::compiler::binder
