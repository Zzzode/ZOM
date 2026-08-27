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

#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "compiler/ir/ir-identity.h"
#include "compiler/lir/lir-store.h"

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
};

/// \brief Closed terminator algebra for the minimal LIR slice.
///
/// Only an integer-constant return is representable today; block-to-block
/// control flow, calls, and switches are later RFC 0021 steps.
class LirTerminator final {
public:
  /// \brief Builds a terminator that returns an integer constant.
  ZC_NODISCARD static LirTerminator returnInteger(LirIntegerConstant value) noexcept;

  ZC_NODISCARD LirTerminatorKind kind() const noexcept { return kindValue; }
  ZC_NODISCARD const LirIntegerConstant& returnIntegerValue() const noexcept {
    return integerValue;
  }

private:
  LirTerminator(LirTerminatorKind kind, LirIntegerConstant value) noexcept
      : kindValue(kind), integerValue(value) {}

  LirTerminatorKind kindValue;
  LirIntegerConstant integerValue;
};

/// \brief One immutable LIR basic block: an identity and its terminator.
class LirBasicBlock final {
public:
  LirBasicBlock(LirBlockId id, LirTerminator terminator) noexcept
      : idValue(id), terminatorValue(terminator) {}

  ZC_NODISCARD LirBlockId id() const noexcept { return idValue; }
  ZC_NODISCARD const LirTerminator& terminator() const noexcept { return terminatorValue; }

private:
  LirBlockId idValue;
  LirTerminator terminatorValue;
};

/// \brief One immutable LIR function: an ASCII symbol, a return carrier, blocks.
class LirFunction final {
public:
  LirFunction(zc::String&& symbolName, LirValueType returnCarrier,
              zc::Vector<LirBasicBlock>&& blocks) noexcept
      : symbolNameValue(zc::mv(symbolName)),
        returnCarrierValue(returnCarrier),
        blocksValue(zc::mv(blocks)) {}
  ZC_DISALLOW_COPY(LirFunction);
  LirFunction(LirFunction&&) = default;
  LirFunction& operator=(LirFunction&&) = default;

  ZC_NODISCARD zc::StringPtr symbolName() const noexcept { return symbolNameValue; }
  ZC_NODISCARD const LirValueType& returnCarrier() const noexcept { return returnCarrierValue; }
  ZC_NODISCARD zc::ArrayPtr<const LirBasicBlock> blocks() const noexcept {
    return blocksValue.asPtr();
  }

private:
  zc::String symbolNameValue;
  LirValueType returnCarrierValue;
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
