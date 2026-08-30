// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/cst/recovery-codec.h"
#include "compiler/source/location.h"
#include "zc/core/common.h"

namespace zomlang::compiler {
namespace source {
class BufferId;
class SourceManager;
}  // namespace source

namespace cst {

/// \brief Converts one source location to its buffer-relative byte offset.
ZC_NODISCARD zc::Maybe<uint64_t> recoveryByteOffset(const source::SourceManager& sources,
                                                    const source::BufferId& buffer,
                                                    source::SourceLoc location);

/// \brief Maps a parser-consumed source range to the exact contiguous lexeme run
/// retained by a verified stream.
ZC_NODISCARD zc::Maybe<RecoveryElement> skippedRecoveryForSourceRange(
    const VerifiedLexemeStream& stream, const source::SourceManager& sources,
    const source::BufferId& buffer, source::SourceRange range);

}  // namespace cst
}  // namespace zomlang::compiler
