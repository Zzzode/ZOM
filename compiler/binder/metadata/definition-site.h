// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/common.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "compiler/ast/node-id.h"

namespace zomlang::compiler::binder {

struct DeclarationDefinitionSite final {
  ast::NodeId node;
};

struct PatternBindingSite final {
  ast::NodeId introducer;
  zc::Vector<uint32_t> patternPath;
};

using DefinitionSiteValue = zc::OneOf<DeclarationDefinitionSite, PatternBindingSite>;

/// \brief Exact declaration or pattern-leaf provenance for one definition identity.
class DefinitionSite final {
public:
  DefinitionSite(DefinitionSite&&) noexcept = default;
  DefinitionSite& operator=(DefinitionSite&&) noexcept = default;
  ZC_DISALLOW_COPY(DefinitionSite);

  ZC_NODISCARD static DefinitionSite declaration(ast::NodeId node);
  ZC_NODISCARD static DefinitionSite pattern(ast::NodeId introducer,
                                             zc::Vector<uint32_t>&& patternPath);
  ZC_NODISCARD DefinitionSite clone() const;
  ZC_NODISCARD const DefinitionSiteValue& value() const noexcept;

private:
  explicit DefinitionSite(DefinitionSiteValue&& value) noexcept;
  DefinitionSiteValue valueValue;
};

}  // namespace zomlang::compiler::binder
