// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/cst/recovery-stream-builder.h"

#include "compiler/source/manager.h"
#include "zc/core/debug.h"

namespace zomlang::compiler::cst {

zc::Maybe<uint64_t> recoveryByteOffset(const source::SourceManager& sources,
                                       const source::BufferId& buffer, source::SourceLoc location) {
  if (location.isInvalid()) return zc::none;
  const auto admitted = sources.getRangeForBuffer(buffer);
  if (location < admitted.getStart() || location > admitted.getEnd()) return zc::none;
  return static_cast<uint64_t>(sources.getLocOffsetInBuffer(location, buffer));
}

zc::Maybe<RecoveryElement> skippedRecoveryForSourceRange(const VerifiedLexemeStream& stream,
                                                         const source::SourceManager& sources,
                                                         const source::BufferId& buffer,
                                                         source::SourceRange range) {
  auto start = recoveryByteOffset(sources, buffer, range.getStart());
  auto end = recoveryByteOffset(sources, buffer, range.getEnd());
  if (start == zc::none || end == zc::none || ZC_ASSERT_NONNULL(start) >= ZC_ASSERT_NONNULL(end)) {
    return zc::none;
  }

  const auto lexemes = stream.lexemes();
  size_t first = lexemes.size();
  size_t last = lexemes.size();
  for (size_t index = 0; index < lexemes.size(); ++index) {
    const ByteRange lexemeRange = lexemes[index].range();
    if (lexemeRange.end <= ZC_ASSERT_NONNULL(start)) continue;
    if (lexemeRange.start >= ZC_ASSERT_NONNULL(end)) break;
    if (first == lexemes.size()) first = index;
    last = index;
  }
  if (first == lexemes.size() || last == lexemes.size() ||
      last - first + 1 > static_cast<size_t>(UINT32_MAX) || first > UINT32_MAX) {
    return zc::none;
  }
  return RecoveryElement::skippedTokens(
      static_cast<uint32_t>(first), static_cast<uint32_t>(last - first + 1),
      ByteRange{lexemes[first].range().start, lexemes[last].range().end});
}

}  // namespace zomlang::compiler::cst
