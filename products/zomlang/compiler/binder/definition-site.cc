// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/definition-site.h"

namespace zomlang::compiler::binder {

DefinitionSite::DefinitionSite(DefinitionSiteValue&& value) noexcept : valueValue(zc::mv(value)) {}

DefinitionSite DefinitionSite::declaration(ast::NodeId node) {
  return DefinitionSite(DefinitionSiteValue(DeclarationDefinitionSite{node}));
}

DefinitionSite DefinitionSite::pattern(ast::NodeId introducer, zc::Vector<uint32_t>&& patternPath) {
  return DefinitionSite(DefinitionSiteValue(PatternBindingSite{introducer, zc::mv(patternPath)}));
}

DefinitionSite DefinitionSite::clone() const {
  if (valueValue.is<DeclarationDefinitionSite>()) {
    return declaration(valueValue.get<DeclarationDefinitionSite>().node);
  }
  const auto& patternValue = valueValue.get<PatternBindingSite>();
  zc::Vector<uint32_t> path(patternValue.patternPath.size());
  path.addAll(patternValue.patternPath);
  return pattern(patternValue.introducer, zc::mv(path));
}

const DefinitionSiteValue& DefinitionSite::value() const noexcept { return valueValue; }

}  // namespace zomlang::compiler::binder
