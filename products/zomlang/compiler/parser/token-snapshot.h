// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/kinds.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang::compiler::binder {
class ParsedModuleVerifier;
}

namespace zomlang::compiler::parser {

/// \brief Immutable parser-owned token boundary retained after successful parsing.
struct ParsedTokenRange final {
  ParsedTokenRange(ast::SyntaxKind kind, source::SourceRange source,
                   zc::String&& canonicalText) noexcept
      : kind(kind), source(source), canonicalText(zc::mv(canonicalText)) {}
  ParsedTokenRange(ParsedTokenRange&&) noexcept = default;
  ParsedTokenRange& operator=(ParsedTokenRange&&) noexcept = default;
  ZC_DISALLOW_COPY(ParsedTokenRange);
  ast::SyntaxKind kind;
  source::SourceRange source;
  zc::String canonicalText;
};

/// \brief Complete token provenance constructible only after a successful parse.
class ParsedTokenSnapshot final {
public:
  ParsedTokenSnapshot(ParsedTokenSnapshot&&) noexcept = default;
  ParsedTokenSnapshot& operator=(ParsedTokenSnapshot&&) noexcept = default;
  ZC_DISALLOW_COPY(ParsedTokenSnapshot);

private:
  ParsedTokenSnapshot(const source::SourceManager& sources, const source::BufferId& buffer,
                      zc::Array<ParsedTokenRange>&& tokens) noexcept;
  const source::SourceManager* sourceManager;
  source::BufferId buffer;
  zc::Array<ParsedTokenRange> tokenValues;
  friend class Parser;
  friend class binder::ParsedModuleVerifier;
};

}  // namespace zomlang::compiler::parser
