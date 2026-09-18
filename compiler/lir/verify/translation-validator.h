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
// See the License for the specific language governing permissions and limitations
// under the License.

#pragma once

#include <cstdint>

#include "compiler/lir/lir-module.h"
#include "zc/core/common.h"

namespace zomlang::compiler::mir {
struct MirFunction;
}  // namespace zomlang::compiler::mir

namespace zomlang::compiler::type {
class SemanticTypeStore;
}  // namespace zomlang::compiler::type

namespace zomlang::compiler::lir {

/// \brief Closed translation-validation fault tags (RFC 0053 slice 2).
///
/// Each tag names one way a structurally well-formed LIR module can fail to
/// preserve the semantics of the verified Built MIR functions it was lowered
/// from. Slice 2 covers single-function modules; multi-function call integrity
/// arrives in slice 3.
enum class TranslationFaultKind : uint8_t {
  /// The presented MIR function set and LIR function set differ in count.
  FunctionSetMismatch = 0x01,
  /// A matched function has a different MIR and LIR block count or order.
  BlockBijectionMismatch = 0x02,
  /// An expected MIR effect has no matching LIR effect, or an LIR instruction
  /// is unaccounted for.
  EffectMismatch = 0x03,
  /// A constant bit pattern or carrier differs across the hop.
  ConstantMismatch = 0x04,
  /// A place does not map to the slot the MIR local ordinal requires.
  PlaceMappingMismatch = 0x05,
  /// A comparison operator, or an rvalue operation kind, is not preserved.
  OperatorMismatch = 0x06,
  /// A terminator branch edge (including polarity) is not preserved under the
  /// block bijection.
  EdgeTargetMismatch = 0x07,
  /// The declared slot set or a slot carrier differs from the MIR locals.
  SlotSetMismatch = 0x08,
};

/// \brief One immutable translation-validation finding.
///
/// `mirBlockOrdinal` and `lirBlockOrdinal` locate the divergent block pair
/// (one-based; zero for a function-level finding). `statementIndex` is the
/// one-based LIR statement position in the block (zero for a terminator- or
/// function-level finding).
struct TranslationFinding final {
  TranslationFaultKind fault;
  uint32_t functionIndex = 0;
  uint32_t mirBlockOrdinal = 0;
  uint32_t lirBlockOrdinal = 0;
  uint32_t statementIndex = 0;
};

/// \brief Construct-level MIR-to-LIR translation validator (RFC 0053).
///
/// Proves that a LIR module preserves the semantics of the verified Built MIR
/// functions presented for lowering. The correspondence is derived by walking
/// MIR constructs, never by classifying whole-function shapes: every MIR
/// statement and terminator maps to zero or more LIR effects, block
/// terminators map under a dense block bijection, and the two sequences are
/// consumed in lockstep with no unaccounted LIR instruction.
///
/// The validator implements its own carrier derivation, integer bit
/// materialization, and place mapping; it shares no helper code with
/// `mir-to-lir.cc`. Slice 2 validates single-function modules in both lowering
/// modes the producer emits: the folded mode (a constant-only local whose only
/// use is the return is resolved to a direct `ReturnInteger` or
/// `ReturnAggregate` with no declared slot) and the materialized mode (MIR
/// locals map one-to-one to LIR parameter/local slots).
class TranslationValidator final {
public:
  /// \brief Validates one single-function lowering.
  /// \param mirFunction The verified Built MIR function handed to lowering.
  /// \param lirModule The LIR module the lowering produced.
  /// \param semanticTypes Session-owned store for the MIR semantic types.
  /// \return None when the LIR preserves the MIR, else the first finding.
  ZC_NODISCARD static zc::Maybe<TranslationFinding> validate(
      const mir::MirFunction& mirFunction, const Module& lirModule,
      const type::SemanticTypeStore& semanticTypes) noexcept;
};

}  // namespace zomlang::compiler::lir
