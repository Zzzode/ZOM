// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/tree.h"

namespace zomlang::compiler {
namespace source {
class BufferId;
class SourceManager;
}  // namespace source

namespace ast {

/// \brief Encodes one schema-verified tree with source-relative byte ranges.
ZC_NODISCARD zc::Maybe<zc::Array<uint8_t>> encodeCanonicalTree(
    const Tree& tree, const source::SourceManager& sources, const source::BufferId& buffer);

/// \brief Decodes one bounded canonical tree into the supplied source coordinate space.
ZC_NODISCARD zc::Maybe<Tree> decodeCanonicalTree(zc::ArrayPtr<const uint8_t> encoded,
                                                 source::SourceManager& sources,
                                                 const source::BufferId& buffer,
                                                 uint64_t sourceByteLength);

/// \brief Returns the logical source name retained by a valid source-file root.
ZC_NODISCARD zc::Maybe<zc::StringPtr> canonicalSourceFileName(const Tree& tree);

}  // namespace ast
}  // namespace zomlang::compiler
