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

#include <cstdint>

#include "compiler/ir/ir-identity.h"
#include "compiler/lir/lir-store.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::lir {

// Minimal RFC 0021 in-memory LIR function model for the first end-to-end
// MIR -> LIR -> LLVM vertical slice. It carries exactly the shape one scalar
// module initializer needs: a named function with an integer return carrier and
// a single entry block whose terminator returns an integer constant. Richer
// block parameters, instructions, PHI nodes, calls, and the interned carrier
// store are the next RFC 0021 steps and are deliberately absent here.
//
// `LirBlockId` is the branded block identity already defined in ir-identity.h.
// The value-type record (`LirValueType`) is reused directly from the landed LIR
// foundation. Carrier interning through `LirValueTypeStore` is not performed for
// a single function; embedding the carrier is the minimal faithful choice and is
// the documented boundary of this slice.

/// \brief One immutable integer constant carried by a LIR integer type.
///
/// The bit pattern is the zero-extended unsigned value of the constant in its
/// carrier width. Signedness is not part of the LIR integer carrier (RFC 0021
/// integers are unsigned-agnostic), so the pattern alone is the value.
class LirIntegerConstant final {
public:
  /// \brief Builds an integer constant for an integer carrier.
  /// \param carrier Integer SSA carrier occupying the value.
  /// \param bits Zero-extended value in the carrier width.
  /// \return The constant, or none when the carrier is not an integer.
  ZC_NODISCARD static zc::Maybe<LirIntegerConstant> from(LirValueType carrier,
                                                         uint64_t bits) noexcept;

  ZC_NODISCARD const LirValueType& carrier() const noexcept { return carrierValue; }
  ZC_NODISCARD uint64_t bits() const noexcept { return bitsValue; }

private:
  LirIntegerConstant(LirValueType carrier, uint64_t bits) noexcept
      : carrierValue(carrier), bitsValue(bits) {}

  LirValueType carrierValue;
  uint64_t bitsValue = 0;
};

/// \brief Closed terminator kind for the currently supported LIR subset.
enum class LirTerminatorKind : uint8_t {
  ReturnInteger = 0x01,
  Goto = 0x02,
  CondBranch = 0x03,
  ReturnLocal = 0x04,
  Call = 0x05,
};

/// \brief Closed relational comparison operator for a LIR compare statement.
///
/// Mirrors the six MIR relational operators. The result is a one-bit integer.
enum class LirComparisonOp : uint8_t {
  Eq = 0x01,
  Ne = 0x02,
  Lt = 0x03,
  Le = 0x04,
  Gt = 0x05,
  Ge = 0x06,
};

/// \brief A LIR operand: an integer constant or a use of a local slot.
///
/// A `localUse` names a one-based local ordinal (a parameter or body local); the
/// renderer loads that local's storage slot. This is the minimal operand model
/// the conditional and comparison shapes need.
class LirOperand final {
public:
  /// \brief An integer-constant operand.
  ZC_NODISCARD static LirOperand constant(LirIntegerConstant value) noexcept;
  /// \brief A use of the local slot with the given one-based ordinal.
  ZC_NODISCARD static LirOperand localUse(uint32_t localOrdinal) noexcept;

  ZC_NODISCARD bool isConstant() const noexcept { return isConstantValue; }
  ZC_NODISCARD const LirIntegerConstant& constantValue() const noexcept { return constantSlot; }
  ZC_NODISCARD uint32_t localOrdinal() const noexcept { return localSlot; }

private:
  explicit LirOperand(LirIntegerConstant value) noexcept
      : isConstantValue(true), constantSlot(value) {}
  explicit LirOperand(uint32_t localOrdinal) noexcept
      : isConstantValue(false), constantSlot(fallbackConstant()), localSlot(localOrdinal) {}

  ZC_NODISCARD static LirIntegerConstant fallbackConstant() noexcept;

  bool isConstantValue;
  LirIntegerConstant constantSlot;
  uint32_t localSlot = 0;
};

/// \brief Closed LIR statement kind.
enum class LirStatementKind : uint8_t {
  Assign = 0x01,
  Compare = 0x02,
};

/// \brief One LIR statement: store an operand or a comparison into a local slot.
///
/// `Assign` stores `value` into `destinationOrdinal`. `Compare` stores the
/// one-bit result of `op` applied to `left` and `right` into the destination.
/// Locals are addressed by one-based ordinal.
class LirStatement final {
public:
  ZC_NODISCARD static LirStatement assign(uint32_t destinationOrdinal, LirOperand value) noexcept;
  ZC_NODISCARD static LirStatement compare(uint32_t destinationOrdinal, LirComparisonOp op,
                                           LirOperand left, LirOperand right) noexcept;

  ZC_NODISCARD LirStatementKind kind() const noexcept { return kindValue; }
  ZC_NODISCARD uint32_t destinationOrdinal() const noexcept { return destinationValue; }
  ZC_NODISCARD const LirOperand& value() const noexcept { return leftValue; }
  ZC_NODISCARD LirComparisonOp comparisonOp() const noexcept { return opValue; }
  ZC_NODISCARD const LirOperand& left() const noexcept { return leftValue; }
  ZC_NODISCARD const LirOperand& right() const noexcept { return rightValue; }

private:
  LirStatement(LirStatementKind kind, uint32_t destinationOrdinal, LirComparisonOp op,
               LirOperand left, LirOperand right) noexcept
      : kindValue(kind),
        destinationValue(destinationOrdinal),
        opValue(op),
        leftValue(left),
        rightValue(right) {}

  LirStatementKind kindValue;
  uint32_t destinationValue;
  LirComparisonOp opValue;
  LirOperand leftValue;
  LirOperand rightValue;
};

/// \brief Closed terminator algebra for the supported LIR subset.
///
/// `ReturnInteger` returns an integer constant directly (the scalar-initializer
/// slice). `Goto` transfers to one block; `CondBranch` selects a block by a
/// boolean local; `ReturnLocal` returns the value held by a local slot. Together
/// these lower the four-block diamond (a `SwitchInt` on a boolean parameter with
/// two `Goto` arms into a `Return` of the result local). Calls and integer
/// switches over wider carriers are later RFC 0021 steps.
class LirTerminator final {
public:
  /// \brief Builds a terminator that returns an integer constant.
  ZC_NODISCARD static LirTerminator returnInteger(LirIntegerConstant value) noexcept;
  /// \brief Builds an unconditional branch to `target`.
  ZC_NODISCARD static LirTerminator gotoBlock(LirBlockId target) noexcept;
  /// \brief Builds a conditional branch selecting a block by a boolean local.
  ZC_NODISCARD static LirTerminator condBranch(uint32_t conditionOrdinal, LirBlockId trueTarget,
                                               LirBlockId falseTarget) noexcept;
  /// \brief Builds a terminator that returns the value held by a local slot.
  ZC_NODISCARD static LirTerminator returnLocal(uint32_t localOrdinal) noexcept;
  /// \brief Builds a call terminator: call a module-local function (by zero-based
  /// index), store its integer result into `destinationOrdinal`, then branch to
  /// `normalTarget`. This slice supports only a zero-argument call.
  ZC_NODISCARD static LirTerminator callFunction(uint32_t calleeIndex, uint32_t destinationOrdinal,
                                                 LirBlockId normalTarget) noexcept;

  ZC_NODISCARD LirTerminatorKind kind() const noexcept { return kindValue; }
  ZC_NODISCARD const LirIntegerConstant& returnIntegerValue() const noexcept {
    return integerValue;
  }
  ZC_NODISCARD LirBlockId gotoTarget() const noexcept { return trueTargetValue; }
  ZC_NODISCARD uint32_t conditionOrdinal() const noexcept { return localOrdinalValue; }
  ZC_NODISCARD LirBlockId condTrueTarget() const noexcept { return trueTargetValue; }
  ZC_NODISCARD LirBlockId condFalseTarget() const noexcept { return falseTargetValue; }
  ZC_NODISCARD uint32_t returnLocalOrdinal() const noexcept { return localOrdinalValue; }
  ZC_NODISCARD uint32_t calleeIndex() const noexcept { return calleeIndexValue; }
  ZC_NODISCARD uint32_t callDestinationOrdinal() const noexcept { return localOrdinalValue; }
  ZC_NODISCARD LirBlockId callNormalTarget() const noexcept { return trueTargetValue; }

private:
  explicit LirTerminator(LirIntegerConstant value) noexcept
      : kindValue(LirTerminatorKind::ReturnInteger), integerValue(value) {}
  LirTerminator(LirTerminatorKind kind, uint32_t localOrdinal, LirBlockId trueTarget,
                LirBlockId falseTarget) noexcept
      : kindValue(kind),
        integerValue(fallbackConstant()),
        localOrdinalValue(localOrdinal),
        trueTargetValue(trueTarget),
        falseTargetValue(falseTarget) {}
  LirTerminator(uint32_t calleeIndex, uint32_t destinationOrdinal, LirBlockId normalTarget) noexcept
      : kindValue(LirTerminatorKind::Call),
        integerValue(fallbackConstant()),
        localOrdinalValue(destinationOrdinal),
        trueTargetValue(normalTarget),
        calleeIndexValue(calleeIndex) {}

  ZC_NODISCARD static LirIntegerConstant fallbackConstant() noexcept;

  LirTerminatorKind kindValue;
  LirIntegerConstant integerValue;
  uint32_t localOrdinalValue = 0;
  LirBlockId trueTargetValue;
  LirBlockId falseTargetValue;
  uint32_t calleeIndexValue = 0;
};

/// \brief One immutable LIR basic block: an identity, statements, a terminator.
class LirBasicBlock final {
public:
  LirBasicBlock(LirBlockId id, LirTerminator terminator) noexcept
      : idValue(id), terminatorValue(terminator) {}
  LirBasicBlock(LirBlockId id, zc::Vector<LirStatement>&& statements,
                LirTerminator terminator) noexcept
      : idValue(id), statementsValue(zc::mv(statements)), terminatorValue(terminator) {}
  ZC_DISALLOW_COPY(LirBasicBlock);
  LirBasicBlock(LirBasicBlock&&) = default;
  LirBasicBlock& operator=(LirBasicBlock&&) = default;

  ZC_NODISCARD LirBlockId id() const noexcept { return idValue; }
  ZC_NODISCARD zc::ArrayPtr<const LirStatement> statements() const noexcept {
    return statementsValue.asPtr();
  }
  ZC_NODISCARD const LirTerminator& terminator() const noexcept { return terminatorValue; }

private:
  LirBlockId idValue;
  zc::Vector<LirStatement> statementsValue;
  LirTerminator terminatorValue;
};

/// \brief One immutable LIR local slot: a one-based ordinal and its carrier.
class LirLocal final {
public:
  LirLocal(uint32_t ordinal, LirValueType carrier) noexcept
      : ordinalValue(ordinal), carrierValue(carrier) {}

  ZC_NODISCARD uint32_t ordinal() const noexcept { return ordinalValue; }
  ZC_NODISCARD const LirValueType& carrier() const noexcept { return carrierValue; }

private:
  uint32_t ordinalValue;
  LirValueType carrierValue;
};

/// \brief One immutable LIR function: an ASCII symbol, a return carrier, blocks.
///
/// The function optionally declares parameter locals (each a one-based ordinal
/// and carrier) and body locals; the diamond needs a boolean parameter and a
/// result local. A parameterless single-block function omits both lists.
class LirFunction final {
public:
  LirFunction(zc::String&& symbolName, LirValueType returnCarrier,
              zc::Vector<LirBasicBlock>&& blocks) noexcept
      : symbolNameValue(zc::mv(symbolName)),
        returnCarrierValue(returnCarrier),
        blocksValue(zc::mv(blocks)) {}
  LirFunction(zc::String&& symbolName, LirValueType returnCarrier,
              zc::Vector<LirLocal>&& parameters, zc::Vector<LirLocal>&& locals,
              zc::Vector<LirBasicBlock>&& blocks) noexcept
      : symbolNameValue(zc::mv(symbolName)),
        returnCarrierValue(returnCarrier),
        parametersValue(zc::mv(parameters)),
        localsValue(zc::mv(locals)),
        blocksValue(zc::mv(blocks)) {}
  ZC_DISALLOW_COPY(LirFunction);
  LirFunction(LirFunction&&) = default;
  LirFunction& operator=(LirFunction&&) = default;

  ZC_NODISCARD zc::StringPtr symbolName() const noexcept { return symbolNameValue; }
  ZC_NODISCARD const LirValueType& returnCarrier() const noexcept { return returnCarrierValue; }
  ZC_NODISCARD zc::ArrayPtr<const LirLocal> parameters() const noexcept {
    return parametersValue.asPtr();
  }
  ZC_NODISCARD zc::ArrayPtr<const LirLocal> locals() const noexcept { return localsValue.asPtr(); }
  ZC_NODISCARD zc::ArrayPtr<const LirBasicBlock> blocks() const noexcept {
    return blocksValue.asPtr();
  }

private:
  zc::String symbolNameValue;
  LirValueType returnCarrierValue;
  zc::Vector<LirLocal> parametersValue;
  zc::Vector<LirLocal> localsValue;
  zc::Vector<LirBasicBlock> blocksValue;
};

/// \brief One immutable LIR module: an ordered sequence of LIR functions.
class LirModule final {
public:
  explicit LirModule(zc::Vector<LirFunction>&& functions) noexcept
      : functionsValue(zc::mv(functions)) {}
  ZC_DISALLOW_COPY(LirModule);
  LirModule(LirModule&&) = default;
  LirModule& operator=(LirModule&&) = default;

  ZC_NODISCARD zc::ArrayPtr<const LirFunction> functions() const noexcept {
    return functionsValue.asPtr();
  }

private:
  zc::Vector<LirFunction> functionsValue;
};

}  // namespace zomlang::compiler::lir
