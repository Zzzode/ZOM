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
// The value-type record (`ValueType`) is reused directly from the landed LIR
// foundation. Carrier interning through `ValueTypeStore` is not performed for
// a single function; embedding the carrier is the minimal faithful choice and is
// the documented boundary of this slice.

/// \brief One immutable integer constant carried by a LIR integer type.
///
/// The bit pattern is the zero-extended unsigned value of the constant in its
/// carrier width. Signedness is not part of the LIR integer carrier (RFC 0021
/// integers are unsigned-agnostic), so the pattern alone is the value.
class IntegerConstant final {
public:
  /// \brief Builds an integer constant for an integer carrier.
  /// \param carrier Integer SSA carrier occupying the value.
  /// \param bits Zero-extended value in the carrier width.
  /// \return The constant, or none when the carrier is not an integer.
  ZC_NODISCARD static zc::Maybe<IntegerConstant> from(ValueType carrier, uint64_t bits) noexcept;

  ZC_NODISCARD const ValueType& carrier() const noexcept { return carrierValue; }
  ZC_NODISCARD uint64_t bits() const noexcept { return bitsValue; }

private:
  IntegerConstant(ValueType carrier, uint64_t bits) noexcept
      : carrierValue(carrier), bitsValue(bits) {}

  ValueType carrierValue;
  uint64_t bitsValue = 0;
};

/// \brief Closed terminator kind for the currently supported LIR subset.
enum class TerminatorKind : uint8_t {
  ReturnInteger = 0x01,
  Goto = 0x02,
  CondBranch = 0x03,
  ReturnLocal = 0x04,
  Call = 0x05,
  ReturnAggregate = 0x06,
};

/// \brief Upper bound on the slot count of a multi-slot aggregate return.
///
/// RFC 0021 packs a direct return of more than one value into an ordered carrier
/// bundle rendered as a literal struct. This first slice caps the bundle so the
/// terminator cannot describe an unbounded struct; a wider bound is a later step.
inline constexpr uint32_t kMaxAggregateReturnSlots = 64;

/// \brief Upper bound on the argument vector a multi-argument call terminator may
/// carry. This first multi-argument slice caps the vector at two so the terminator
/// cannot describe an unbounded argument list; a wider bound is a later step.
inline constexpr uint32_t kMaxCallArguments = 2;

/// \brief Closed relational comparison operator for a LIR compare statement.
///
/// Mirrors the six MIR relational operators. The result is a one-bit integer.
enum class ComparisonOp : uint8_t {
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
class Operand final {
public:
  /// \brief An integer-constant operand.
  ZC_NODISCARD static Operand constant(IntegerConstant value) noexcept;
  /// \brief A use of the local slot with the given one-based ordinal.
  ZC_NODISCARD static Operand localUse(uint32_t localOrdinal) noexcept;

  ZC_NODISCARD bool isConstant() const noexcept { return isConstantValue; }
  ZC_NODISCARD const IntegerConstant& constantValue() const noexcept { return constantSlot; }
  ZC_NODISCARD uint32_t localOrdinal() const noexcept { return localSlot; }

private:
  explicit Operand(IntegerConstant value) noexcept : isConstantValue(true), constantSlot(value) {}
  explicit Operand(uint32_t localOrdinal) noexcept
      : isConstantValue(false), constantSlot(fallbackConstant()), localSlot(localOrdinal) {}

  ZC_NODISCARD static IntegerConstant fallbackConstant() noexcept;

  bool isConstantValue;
  IntegerConstant constantSlot;
  uint32_t localSlot = 0;
};

/// \brief Closed LIR statement kind.
enum class StatementKind : uint8_t {
  Assign = 0x01,
  Compare = 0x02,
};

/// \brief One LIR statement: store an operand or a comparison into a local slot.
///
/// `Assign` stores `value` into `destinationOrdinal`. `Compare` stores the
/// one-bit result of `op` applied to `left` and `right` into the destination.
/// Locals are addressed by one-based ordinal.
class Statement final {
public:
  ZC_NODISCARD static Statement assign(uint32_t destinationOrdinal, Operand value) noexcept;
  ZC_NODISCARD static Statement compare(uint32_t destinationOrdinal, ComparisonOp op, Operand left,
                                        Operand right) noexcept;

  ZC_NODISCARD StatementKind kind() const noexcept { return kindValue; }
  ZC_NODISCARD uint32_t destinationOrdinal() const noexcept { return destinationValue; }
  ZC_NODISCARD const Operand& value() const noexcept { return leftValue; }
  ZC_NODISCARD ComparisonOp comparisonOp() const noexcept { return opValue; }
  ZC_NODISCARD const Operand& left() const noexcept { return leftValue; }
  ZC_NODISCARD const Operand& right() const noexcept { return rightValue; }

private:
  Statement(StatementKind kind, uint32_t destinationOrdinal, ComparisonOp op, Operand left,
            Operand right) noexcept
      : kindValue(kind),
        destinationValue(destinationOrdinal),
        opValue(op),
        leftValue(left),
        rightValue(right) {}

  StatementKind kindValue;
  uint32_t destinationValue;
  ComparisonOp opValue;
  Operand leftValue;
  Operand rightValue;
};

/// \brief Closed terminator algebra for the supported LIR subset.
///
/// `ReturnInteger` returns an integer constant directly (the scalar-initializer
/// slice). `Goto` transfers to one block; `CondBranch` selects a block by a
/// boolean local; `ReturnLocal` returns the value held by a local slot. Together
/// these lower the four-block diamond (a `SwitchInt` on a boolean parameter with
/// two `Goto` arms into a `Return` of the result local). Calls and integer
/// switches over wider carriers are later RFC 0021 steps.
class Terminator final {
public:
  /// \brief Builds a terminator that returns an integer constant.
  ZC_NODISCARD static Terminator returnInteger(IntegerConstant value) noexcept;
  /// \brief Builds an unconditional branch to `target`.
  ZC_NODISCARD static Terminator gotoBlock(LirBlockId target) noexcept;
  /// \brief Builds a conditional branch selecting a block by a boolean local.
  ZC_NODISCARD static Terminator condBranch(uint32_t conditionOrdinal, LirBlockId trueTarget,
                                            LirBlockId falseTarget) noexcept;
  /// \brief Builds a terminator that returns the value held by a local slot.
  ZC_NODISCARD static Terminator returnLocal(uint32_t localOrdinal) noexcept;
  /// \brief Builds a call terminator: call a module-local function (by zero-based
  /// index), store its integer result into `destinationOrdinal`, then branch to
  /// `normalTarget`. This form passes no arguments.
  ZC_NODISCARD static Terminator callFunction(uint32_t calleeIndex, uint32_t destinationOrdinal,
                                              LirBlockId normalTarget) noexcept;
  /// \brief Builds a call terminator that passes one integer-constant argument.
  /// Otherwise identical to `callFunction`. This is the first argument-carrying
  /// call shape (RFC 0021 KR5.2); wider argument vectors are later steps.
  ZC_NODISCARD static Terminator callFunctionWithArgument(uint32_t calleeIndex,
                                                          uint32_t destinationOrdinal,
                                                          IntegerConstant argument,
                                                          LirBlockId normalTarget) noexcept;
  /// \brief Builds a call terminator that passes an ordered vector of
  /// integer-constant arguments. This is the first multi-argument call shape
  /// (RFC 0021 KR5.2 / RFC 0009 widening); the vector must be non-empty and within
  /// `kMaxCallArguments`, so the factory itself fails closed on an empty or
  /// over-cap vector rather than trusting the caller. A single-argument vector is
  /// representable here but the existing `callFunctionWithArgument` remains the
  /// canonical one-argument form.
  /// \return The terminator, or none when the vector is empty or over the cap.
  ZC_NODISCARD static zc::Maybe<Terminator> callFunctionWithArguments(
      uint32_t calleeIndex, uint32_t destinationOrdinal, zc::Vector<IntegerConstant>&& arguments,
      LirBlockId normalTarget) noexcept;
  /// \brief Builds a terminator that returns an ordered bundle of integer
  /// constants as a multi-slot direct return (RFC 0021 carrier bundle rendered as
  /// a literal struct). The slots are returned in the given order.
  /// \param slots The ordered slot constants; must be non-empty and within
  ///        `kMaxAggregateReturnSlots`.
  /// \return The terminator, or none when the bundle is empty or over the cap.
  ZC_NODISCARD static zc::Maybe<Terminator> returnAggregate(
      zc::Vector<IntegerConstant>&& slots) noexcept;

  Terminator(Terminator&&) = default;
  Terminator& operator=(Terminator&&) = default;
  ZC_DISALLOW_COPY(Terminator);
  ~Terminator() noexcept = default;

  ZC_NODISCARD TerminatorKind kind() const noexcept { return kindValue; }
  ZC_NODISCARD const IntegerConstant& returnIntegerValue() const noexcept { return integerValue; }
  ZC_NODISCARD LirBlockId gotoTarget() const noexcept { return trueTargetValue; }
  ZC_NODISCARD uint32_t conditionOrdinal() const noexcept { return localOrdinalValue; }
  ZC_NODISCARD LirBlockId condTrueTarget() const noexcept { return trueTargetValue; }
  ZC_NODISCARD LirBlockId condFalseTarget() const noexcept { return falseTargetValue; }
  ZC_NODISCARD uint32_t returnLocalOrdinal() const noexcept { return localOrdinalValue; }
  ZC_NODISCARD uint32_t calleeIndex() const noexcept { return calleeIndexValue; }
  ZC_NODISCARD uint32_t callDestinationOrdinal() const noexcept { return localOrdinalValue; }
  ZC_NODISCARD LirBlockId callNormalTarget() const noexcept { return trueTargetValue; }
  /// \brief True when a Call terminator passes one integer-constant argument.
  ZC_NODISCARD bool callHasArgument() const noexcept { return callHasArgumentValue; }
  /// \brief The single integer-constant call argument; valid when callHasArgument().
  ZC_NODISCARD const IntegerConstant& callArgument() const noexcept { return integerValue; }
  /// \brief The ordered multi-argument call vector; empty for the zero-argument and
  /// canonical one-argument (`callHasArgument()`) call forms.
  ZC_NODISCARD zc::ArrayPtr<const IntegerConstant> callArguments() const noexcept {
    return callArgumentsValue.asPtr();
  }
  /// \brief The ordered return slots; valid only for a ReturnAggregate terminator.
  ZC_NODISCARD zc::ArrayPtr<const IntegerConstant> returnAggregateSlots() const noexcept {
    return aggregateSlotsValue.asPtr();
  }

private:
  explicit Terminator(IntegerConstant value) noexcept
      : kindValue(TerminatorKind::ReturnInteger), integerValue(value) {}
  Terminator(TerminatorKind kind, uint32_t localOrdinal, LirBlockId trueTarget,
             LirBlockId falseTarget) noexcept
      : kindValue(kind),
        integerValue(fallbackConstant()),
        localOrdinalValue(localOrdinal),
        trueTargetValue(trueTarget),
        falseTargetValue(falseTarget) {}
  Terminator(uint32_t calleeIndex, uint32_t destinationOrdinal, LirBlockId normalTarget) noexcept
      : kindValue(TerminatorKind::Call),
        integerValue(fallbackConstant()),
        localOrdinalValue(destinationOrdinal),
        trueTargetValue(normalTarget),
        calleeIndexValue(calleeIndex) {}
  Terminator(uint32_t calleeIndex, uint32_t destinationOrdinal, IntegerConstant argument,
             LirBlockId normalTarget) noexcept
      : kindValue(TerminatorKind::Call),
        integerValue(argument),
        localOrdinalValue(destinationOrdinal),
        trueTargetValue(normalTarget),
        calleeIndexValue(calleeIndex),
        callHasArgumentValue(true) {}
  Terminator(uint32_t calleeIndex, uint32_t destinationOrdinal,
             zc::Vector<IntegerConstant>&& arguments, LirBlockId normalTarget) noexcept
      : kindValue(TerminatorKind::Call),
        integerValue(fallbackConstant()),
        localOrdinalValue(destinationOrdinal),
        trueTargetValue(normalTarget),
        calleeIndexValue(calleeIndex),
        callArgumentsValue(zc::mv(arguments)) {}
  explicit Terminator(zc::Vector<IntegerConstant>&& slots) noexcept
      : kindValue(TerminatorKind::ReturnAggregate),
        integerValue(fallbackConstant()),
        aggregateSlotsValue(zc::mv(slots)) {}

  ZC_NODISCARD static IntegerConstant fallbackConstant() noexcept;

  TerminatorKind kindValue;
  IntegerConstant integerValue;
  uint32_t localOrdinalValue = 0;
  LirBlockId trueTargetValue;
  LirBlockId falseTargetValue;
  uint32_t calleeIndexValue = 0;
  bool callHasArgumentValue = false;
  zc::Vector<IntegerConstant> aggregateSlotsValue;
  zc::Vector<IntegerConstant> callArgumentsValue;
};

/// \brief One immutable LIR basic block: an identity, statements, a terminator.
class BasicBlock final {
public:
  BasicBlock(LirBlockId id, Terminator&& terminator) noexcept
      : idValue(id), terminatorValue(zc::mv(terminator)) {}
  BasicBlock(LirBlockId id, zc::Vector<Statement>&& statements, Terminator&& terminator) noexcept
      : idValue(id), statementsValue(zc::mv(statements)), terminatorValue(zc::mv(terminator)) {}
  ZC_DISALLOW_COPY(BasicBlock);
  BasicBlock(BasicBlock&&) = default;
  BasicBlock& operator=(BasicBlock&&) = default;

  ZC_NODISCARD LirBlockId id() const noexcept { return idValue; }
  ZC_NODISCARD zc::ArrayPtr<const Statement> statements() const noexcept {
    return statementsValue.asPtr();
  }
  ZC_NODISCARD const Terminator& terminator() const noexcept { return terminatorValue; }

private:
  LirBlockId idValue;
  zc::Vector<Statement> statementsValue;
  Terminator terminatorValue;
};

/// \brief One immutable LIR local slot: a one-based ordinal and its carrier.
class Local final {
public:
  Local(uint32_t ordinal, ValueType carrier) noexcept
      : ordinalValue(ordinal), carrierValue(carrier) {}

  ZC_NODISCARD uint32_t ordinal() const noexcept { return ordinalValue; }
  ZC_NODISCARD const ValueType& carrier() const noexcept { return carrierValue; }

private:
  uint32_t ordinalValue;
  ValueType carrierValue;
};

/// \brief One immutable LIR function: an ASCII symbol, a return carrier, blocks.
///
/// The function optionally declares parameter locals (each a one-based ordinal
/// and carrier) and body locals; the diamond needs a boolean parameter and a
/// result local. A parameterless single-block function omits both lists.
class Function final {
public:
  Function(zc::String&& symbolName, ValueType returnCarrier,
           zc::Vector<BasicBlock>&& blocks) noexcept
      : symbolNameValue(zc::mv(symbolName)),
        returnCarrierValue(returnCarrier),
        blocksValue(zc::mv(blocks)) {}
  Function(zc::String&& symbolName, ValueType returnCarrier, zc::Vector<Local>&& parameters,
           zc::Vector<Local>&& locals, zc::Vector<BasicBlock>&& blocks) noexcept
      : symbolNameValue(zc::mv(symbolName)),
        returnCarrierValue(returnCarrier),
        parametersValue(zc::mv(parameters)),
        localsValue(zc::mv(locals)),
        blocksValue(zc::mv(blocks)) {}
  ZC_DISALLOW_COPY(Function);
  Function(Function&&) = default;
  Function& operator=(Function&&) = default;

  ZC_NODISCARD zc::StringPtr symbolName() const noexcept { return symbolNameValue; }
  ZC_NODISCARD const ValueType& returnCarrier() const noexcept { return returnCarrierValue; }
  ZC_NODISCARD zc::ArrayPtr<const Local> parameters() const noexcept {
    return parametersValue.asPtr();
  }
  ZC_NODISCARD zc::ArrayPtr<const Local> locals() const noexcept { return localsValue.asPtr(); }
  ZC_NODISCARD zc::ArrayPtr<const BasicBlock> blocks() const noexcept {
    return blocksValue.asPtr();
  }

private:
  zc::String symbolNameValue;
  ValueType returnCarrierValue;
  zc::Vector<Local> parametersValue;
  zc::Vector<Local> localsValue;
  zc::Vector<BasicBlock> blocksValue;
};

/// \brief One immutable LIR module: an ordered sequence of LIR functions.
class Module final {
public:
  explicit Module(zc::Vector<Function>&& functions) noexcept : functionsValue(zc::mv(functions)) {}
  ZC_DISALLOW_COPY(Module);
  Module(Module&&) = default;
  Module& operator=(Module&&) = default;

  ZC_NODISCARD zc::ArrayPtr<const Function> functions() const noexcept {
    return functionsValue.asPtr();
  }

private:
  zc::Vector<Function> functionsValue;
};

}  // namespace zomlang::compiler::lir
