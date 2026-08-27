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
#include "compiler/lir/lir-identity.h"

namespace zomlang::compiler::lir {

// RFC 0021 "SSA Carrier Types" and "Module And Store Model". These are the
// closed, immutable pure-data records interned by the LIR value-type, layout,
// function-ABI, runtime-symbol, and source-location stores. Closed enum tags
// begin at 0x01 in declaration order, matching the canonical encoding rule.

/// \brief Closed SSA carrier kind (RFC 0021 `LirValueType`).
enum class LirValueTypeKind : uint8_t {
  Integer = 0x01,
  Float = 0x02,
  Pointer = 0x03,
};

/// \brief Closed integer carrier width in bits (RFC 0021 `IntegerBitWidth`).
enum class IntegerBitWidth : uint8_t {
  Bit1 = 1,
  Bit8 = 8,
  Bit16 = 16,
  Bit32 = 32,
  Bit64 = 64,
};

/// \brief Closed floating-point carrier format (RFC 0021 `FloatFormat`).
enum class FloatFormat : uint8_t {
  Binary32 = 0x01,
  Binary64 = 0x02,
};

/// \brief One immutable SSA carrier type record.
///
/// `Integer` has no signedness and `Pointer` is opaque, carrying only an
/// address space, exactly as RFC 0021 requires. Construction fails closed on an
/// out-of-domain width or format.
class LirValueType final {
public:
  /// \brief Builds an integer carrier of the given closed bit width.
  ZC_NODISCARD static zc::Maybe<LirValueType> integer(IntegerBitWidth width) noexcept;
  /// \brief Builds a floating-point carrier of the given closed format.
  ZC_NODISCARD static zc::Maybe<LirValueType> floating(FloatFormat format) noexcept;
  /// \brief Builds an opaque pointer carrier in the given address space.
  ZC_NODISCARD static LirValueType pointer(uint32_t addressSpace) noexcept;

  ZC_NODISCARD LirValueTypeKind kind() const noexcept { return kindValue; }
  ZC_NODISCARD IntegerBitWidth integerWidth() const noexcept { return integerValue; }
  ZC_NODISCARD FloatFormat floatFormat() const noexcept { return floatValue; }
  ZC_NODISCARD uint32_t pointerAddressSpace() const noexcept { return addressSpaceValue; }

  bool operator==(const LirValueType& other) const noexcept;
  bool operator!=(const LirValueType& other) const noexcept { return !(*this == other); }

private:
  LirValueTypeKind kindValue = LirValueTypeKind::Integer;
  IntegerBitWidth integerValue = IntegerBitWidth::Bit1;
  FloatFormat floatValue = FloatFormat::Binary32;
  uint32_t addressSpaceValue = 0;
};

/// \brief One immutable scalar storage-layout record (RFC 0021 layout model).
///
/// This foundation slice models a scalar storage layout: a size, a
/// power-of-two ABI alignment, and the carrier that occupies the slot. Richer
/// aggregate and variant layouts are the next RFC 0021 store step.
class StorageLayout final {
public:
  /// \brief Builds a scalar storage layout.
  /// \param sizeBytes Storage size in bytes.
  /// \param abiAlignment ABI alignment in bytes; must be a non-zero power of two.
  /// \param carrier Store-local carrier occupying the slot.
  /// \return The layout, or none for an invalid alignment or carrier.
  ZC_NODISCARD static zc::Maybe<StorageLayout> scalar(uint64_t sizeBytes, uint32_t abiAlignment,
                                                      LirValueTypeId carrier) noexcept;

  ZC_NODISCARD uint64_t sizeBytes() const noexcept { return sizeValue; }
  ZC_NODISCARD uint32_t abiAlignment() const noexcept { return alignmentValue; }
  ZC_NODISCARD LirValueTypeId carrier() const noexcept { return carrierValue; }

  bool operator==(const StorageLayout& other) const noexcept;
  bool operator!=(const StorageLayout& other) const noexcept { return !(*this == other); }

private:
  StorageLayout(uint64_t sizeBytes, uint32_t abiAlignment, LirValueTypeId carrier) noexcept
      : sizeValue(sizeBytes), alignmentValue(abiAlignment), carrierValue(carrier) {}

  uint64_t sizeValue = 0;
  uint32_t alignmentValue = 0;
  LirValueTypeId carrierValue;
};

/// \brief One immutable function-ABI record (RFC 0021 `FnAbi`).
///
/// This foundation slice models the physical carrier decomposition: the
/// ordered return carriers and the ordered parameter carriers a call must
/// supply. Passing modes, hidden parameters, calling convention, and unwind
/// contract are the next RFC 0021 ABI-classifier step.
class FnAbi final {
public:
  FnAbi() = default;

  /// \brief Appends a return carrier in physical order.
  void addReturnCarrier(LirValueTypeId carrier);
  /// \brief Appends a parameter carrier in physical order.
  void addParameterCarrier(LirValueTypeId carrier);

  ZC_NODISCARD zc::ArrayPtr<const LirValueTypeId> returnCarriers() const noexcept {
    return returnValues.asPtr();
  }
  ZC_NODISCARD zc::ArrayPtr<const LirValueTypeId> parameterCarriers() const noexcept {
    return parameterValues.asPtr();
  }

  ZC_NODISCARD FnAbi clone() const;

  bool operator==(const FnAbi& other) const noexcept;
  bool operator!=(const FnAbi& other) const noexcept { return !(*this == other); }

private:
  zc::Vector<LirValueTypeId> returnValues;
  zc::Vector<LirValueTypeId> parameterValues;
};

/// \brief One immutable imported runtime-symbol record (RFC 0021 runtime ABI).
///
/// A runtime symbol is not a semantic definition; it is imported by
/// `RuntimeSymbolId` and named by an ASCII symbol string with its function ABI.
class RuntimeSymbol final {
public:
  /// \brief Builds a runtime symbol from an ASCII name and function ABI.
  /// \return The symbol, or none for an empty or non-ASCII name.
  ZC_NODISCARD static zc::Maybe<RuntimeSymbol> from(zc::StringPtr name, FnAbiId fnAbi);

  ZC_NODISCARD zc::StringPtr name() const noexcept { return nameValue; }
  ZC_NODISCARD FnAbiId fnAbi() const noexcept { return fnAbiValue; }

  ZC_NODISCARD RuntimeSymbol clone() const;

  bool operator==(const RuntimeSymbol& other) const noexcept;
  bool operator!=(const RuntimeSymbol& other) const noexcept { return !(*this == other); }

private:
  RuntimeSymbol(zc::String&& name, FnAbiId fnAbi) noexcept
      : nameValue(zc::mv(name)), fnAbiValue(fnAbi) {}

  zc::String nameValue;
  FnAbiId fnAbiValue;
};

/// \brief One immutable source-location record (RFC 0021 `LirSourceLocation`).
///
/// This foundation slice retains the byte span; the source file key and
/// inlining chain are the next RFC 0021 provenance step.
class LirSourceLocation final {
public:
  /// \brief Builds a source location from a half-open byte span.
  /// \return The location, or none when the end precedes the start.
  ZC_NODISCARD static zc::Maybe<LirSourceLocation> from(uint64_t byteStart,
                                                        uint64_t byteEnd) noexcept;

  ZC_NODISCARD uint64_t byteStart() const noexcept { return startValue; }
  ZC_NODISCARD uint64_t byteEnd() const noexcept { return endValue; }

  bool operator==(const LirSourceLocation& other) const noexcept;
  bool operator!=(const LirSourceLocation& other) const noexcept { return !(*this == other); }

private:
  LirSourceLocation(uint64_t byteStart, uint64_t byteEnd) noexcept
      : startValue(byteStart), endValue(byteEnd) {}

  uint64_t startValue = 0;
  uint64_t endValue = 0;
};

}  // namespace zomlang::compiler::lir
