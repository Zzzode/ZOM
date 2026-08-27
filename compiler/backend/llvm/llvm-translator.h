// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include "zc/core/string.h"
#include "compiler/lir/lir-module.h"

// This header is the RFC 0021 LLVM isolation wall. It exposes a ZOM-native
// interface only: no `llvm/...` header is included here or reachable from any
// frontend header. Every LLVM type lives behind the Pimpl in the matching .cc,
// which is compiled and linked ONLY when ZOM_ENABLE_LLVM_BACKEND is ON.

namespace zomlang::compiler::backend::llvm {

/// \brief Outcome of translating one LIR module to a verified LLVM module.
///
/// A translation succeeds only when `llvm::verifyModule` reports no broken
/// module. On success the textual IR is retained for inspection and testing; on
/// failure it carries the diagnostic text produced by the translator or the
/// verifier. No LLVM module is published on failure.
class LlvmTranslationResult final {
public:
  LlvmTranslationResult(LlvmTranslationResult&&) noexcept = default;
  LlvmTranslationResult& operator=(LlvmTranslationResult&&) noexcept = default;
  ZC_DISALLOW_COPY(LlvmTranslationResult);

  /// \brief Builds a success result carrying the verified textual IR.
  ZC_NODISCARD static LlvmTranslationResult success(zc::String&& textualIr) noexcept {
    return LlvmTranslationResult(true, zc::mv(textualIr), zc::heapString(""));
  }
  /// \brief Builds a failure result carrying the diagnostic text.
  ZC_NODISCARD static LlvmTranslationResult failure(zc::String&& diagnostic) noexcept {
    return LlvmTranslationResult(false, zc::heapString(""), zc::mv(diagnostic));
  }

  ZC_NODISCARD bool verified() const noexcept { return verifiedValue; }
  ZC_NODISCARD zc::StringPtr textualIr() const noexcept { return textualIrValue; }
  ZC_NODISCARD zc::StringPtr diagnostic() const noexcept { return diagnosticValue; }

private:
  LlvmTranslationResult(bool verified, zc::String&& textualIr, zc::String&& diagnostic) noexcept
      : verifiedValue(verified),
        textualIrValue(zc::mv(textualIr)),
        diagnosticValue(zc::mv(diagnostic)) {}

  bool verifiedValue = false;
  zc::String textualIrValue;
  zc::String diagnosticValue;
};

/// \brief Deterministic RFC 0021 LIR -> LLVM translator behind the isolation wall.
class LlvmTranslator final {
public:
  LlvmTranslator();
  ~LlvmTranslator() noexcept;
  LlvmTranslator(LlvmTranslator&&) noexcept;
  LlvmTranslator& operator=(LlvmTranslator&&) noexcept;
  ZC_DISALLOW_COPY(LlvmTranslator);

  /// \brief Translates a LIR module to a verified LLVM module and returns its IR.
  ///
  /// Follows the RFC 0021 deterministic order: create one context and module,
  /// set the host triple and data layout, create value types, declare the
  /// function, create its entry block, translate the terminator, then run
  /// `llvm::verifyModule`. Opaque pointers are used; no undef/poison values and
  /// no unproved attributes are emitted. The module is materialized to text and
  /// returned only when verification reports no error.
  ///
  /// \param module Minimal LIR module produced by MIR -> LIR lowering.
  /// \return A success result with the textual IR, or a failure result.
  ZC_NODISCARD LlvmTranslationResult translate(const lir::LirModule& module);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::backend::llvm
