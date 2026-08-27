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

#include "compiler/lir/lir-store.h"

namespace zomlang::compiler::lir {
namespace {

bool isAscii(zc::StringPtr value) {
  if (value.size() == 0) { return false; }
  for (const auto byte : value) {
    if (static_cast<uint8_t>(byte) >= 0x80U || byte == '\0') { return false; }
  }
  return true;
}

bool isPowerOfTwo(uint32_t value) noexcept { return value != 0 && (value & (value - 1)) == 0; }

bool isIntegerBitWidth(IntegerBitWidth width) noexcept {
  switch (width) {
    case IntegerBitWidth::Bit1:
    case IntegerBitWidth::Bit8:
    case IntegerBitWidth::Bit16:
    case IntegerBitWidth::Bit32:
    case IntegerBitWidth::Bit64:
      return true;
  }
  return false;
}

bool isFloatFormat(FloatFormat format) noexcept {
  return format == FloatFormat::Binary32 || format == FloatFormat::Binary64;
}

}  // namespace

zc::Maybe<LirValueType> LirValueType::integer(IntegerBitWidth width) noexcept {
  if (!isIntegerBitWidth(width)) { return zc::none; }
  LirValueType type;
  type.kindValue = LirValueTypeKind::Integer;
  type.integerValue = width;
  return type;
}

zc::Maybe<LirValueType> LirValueType::floating(FloatFormat format) noexcept {
  if (!isFloatFormat(format)) { return zc::none; }
  LirValueType type;
  type.kindValue = LirValueTypeKind::Float;
  type.floatValue = format;
  return type;
}

LirValueType LirValueType::pointer(uint32_t addressSpace) noexcept {
  LirValueType type;
  type.kindValue = LirValueTypeKind::Pointer;
  type.addressSpaceValue = addressSpace;
  return type;
}

bool LirValueType::operator==(const LirValueType& other) const noexcept {
  if (kindValue != other.kindValue) { return false; }
  switch (kindValue) {
    case LirValueTypeKind::Integer:
      return integerValue == other.integerValue;
    case LirValueTypeKind::Float:
      return floatValue == other.floatValue;
    case LirValueTypeKind::Pointer:
      return addressSpaceValue == other.addressSpaceValue;
  }
  return false;
}

zc::Maybe<StorageLayout> StorageLayout::scalar(uint64_t sizeBytes, uint32_t abiAlignment,
                                               LirValueTypeId carrier) noexcept {
  if (!isPowerOfTwo(abiAlignment) || !carrier.isValid()) { return zc::none; }
  return StorageLayout(sizeBytes, abiAlignment, carrier);
}

bool StorageLayout::operator==(const StorageLayout& other) const noexcept {
  return sizeValue == other.sizeValue && alignmentValue == other.alignmentValue &&
         carrierValue == other.carrierValue;
}

void FnAbi::addReturnCarrier(LirValueTypeId carrier) { returnValues.add(carrier); }
void FnAbi::addParameterCarrier(LirValueTypeId carrier) { parameterValues.add(carrier); }

FnAbi FnAbi::clone() const {
  FnAbi copy;
  for (const auto carrier : returnValues) { copy.returnValues.add(carrier); }
  for (const auto carrier : parameterValues) { copy.parameterValues.add(carrier); }
  return copy;
}

bool FnAbi::operator==(const FnAbi& other) const noexcept {
  if (returnValues.size() != other.returnValues.size() ||
      parameterValues.size() != other.parameterValues.size()) {
    return false;
  }
  for (size_t index = 0; index < returnValues.size(); ++index) {
    if (returnValues[index] != other.returnValues[index]) { return false; }
  }
  for (size_t index = 0; index < parameterValues.size(); ++index) {
    if (parameterValues[index] != other.parameterValues[index]) { return false; }
  }
  return true;
}

zc::Maybe<RuntimeSymbol> RuntimeSymbol::from(zc::StringPtr name, FnAbiId fnAbi) {
  if (!isAscii(name) || !fnAbi.isValid()) { return zc::none; }
  return RuntimeSymbol(zc::str(name), fnAbi);
}

RuntimeSymbol RuntimeSymbol::clone() const { return RuntimeSymbol(zc::str(nameValue), fnAbiValue); }

bool RuntimeSymbol::operator==(const RuntimeSymbol& other) const noexcept {
  return fnAbiValue == other.fnAbiValue && nameValue == other.nameValue;
}

zc::Maybe<LirSourceLocation> LirSourceLocation::from(uint64_t byteStart,
                                                     uint64_t byteEnd) noexcept {
  if (byteEnd < byteStart) { return zc::none; }
  return LirSourceLocation(byteStart, byteEnd);
}

bool LirSourceLocation::operator==(const LirSourceLocation& other) const noexcept {
  return startValue == other.startValue && endValue == other.endValue;
}

}  // namespace zomlang::compiler::lir
