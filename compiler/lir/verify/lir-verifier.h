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

namespace zomlang::compiler::lir {

/// \brief Closed structural fault tags a LIR module can violate in isolation.
///
/// Every tag names a defect constructible through the public `lir::Module`
/// algebra; invariants the closed factories already make unrepresentable
/// (empty or over-cap call/aggregate vectors) intentionally have no tag.
enum class LirVerificationFaultKind : uint8_t {
  /// A function symbol is empty or two module functions share one symbol.
  EmptyOrDuplicateSymbol = 0x01,
  /// A function has no block or no block with ordinal one (its entry).
  MissingEntryBlock = 0x02,
  /// Two blocks of one function carry the same block ordinal.
  DuplicateBlockOrdinal = 0x03,
  /// Block ordinals start at one and are unique but skip an ordinal.
  NonDenseBlockOrdinals = 0x04,
  /// A block is not reachable from the entry block through terminator edges.
  UnreachableBlock = 0x05,
  /// Parameter or body-local ordinals are not the dense split ranges
  /// 1..P and P+1..P+L in declaration order, or the ranges overlap.
  NonDenseLocalSlots = 0x06,
  /// A statement or terminator references a slot ordinal that is neither a
  /// declared parameter nor a declared body local.
  UndeclaredLocalSlot = 0x07,
  /// A Goto, CondBranch, or Call terminator names a block the function does
  /// not declare.
  DanglingBlockTarget = 0x08,
  /// A Call passes a different argument count than the callee declares.
  TerminatorArity = 0x09,
  /// An Assign, Compare, Call, or aggregate return carries an operand,
  /// destination, argument, or result with an inconsistent carrier.
  CarrierMismatch = 0x0a,
  /// A CondBranch condition names a slot whose carrier is not one bit.
  ConditionNotBit1 = 0x0b,
  /// A Call terminator indexes a function outside the module range.
  CalleeIndexOutOfRange = 0x0c,
  /// A Return terminator carrier does not match the function return carrier.
  ReturnCarrierMismatch = 0x0d,
};

/// \brief One immutable structural verification finding.
///
/// Contexts locate the fault deterministically: `functionIndex` is the
/// zero-based module position, `blockOrdinal` the one-based LIR block ordinal
/// (zero for a function-level finding), and `statementIndex` the one-based
/// statement position within the block (zero for a terminator- or
/// function-level finding).
struct LirVerificationFinding final {
  LirVerificationFaultKind fault;
  uint32_t functionIndex = 0;
  uint32_t blockOrdinal = 0;
  uint32_t statementIndex = 0;
};

/// \brief Structural verifier for the closed LIR module algebra (RFC 0053).
///
/// Validates a LIR module independently of its producer: dense block and slot
/// ordinals, declared terminator targets, intra-function reachability, slot
/// carrier consistency, and module-level call arity and carrier integrity.
/// The verifier never infers validity from a whole-function shape; each check
/// is local to one construct and runs in a fixed deterministic order.
class LirStructuralVerifier final {
public:
  /// \brief Verifies one LIR module structurally.
  /// \param module The LIR module a lowering produced.
  /// \return None when the module is well-formed, else the first finding in
  /// deterministic visitation order.
  ZC_NODISCARD static zc::Maybe<LirVerificationFinding> verify(const Module& module) noexcept;
};

}  // namespace zomlang::compiler::lir
