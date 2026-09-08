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

#include "compiler/ast/generated/node-payload.h"
#include "compiler/type/semantic-type-data.h"
#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/one-of.h"
#include "zc/core/string.h"

namespace zomlang::compiler::checker {

enum class PrimitiveOperation : uint8_t {
  UnaryPlus = 0x01,
  Neg = 0x02,
  LogicalNot = 0x03,
  BitNot = 0x04,
  Dereference = 0x05,
  BorrowShared = 0x06,
  BorrowMutable = 0x07,
  PreIncrement = 0x08,
  PreDecrement = 0x09,
  PostIncrement = 0x0a,
  PostDecrement = 0x0b,
  Add = 0x0c,
  Sub = 0x0d,
  Mul = 0x0e,
  Div = 0x0f,
  Rem = 0x10,
  Pow = 0x11,
  Shl = 0x12,
  Shr = 0x13,
  UShr = 0x14,
  BitAnd = 0x15,
  BitOr = 0x16,
  BitXor = 0x17,
  LogicalAnd = 0x18,
  LogicalOr = 0x19,
  Eq = 0x1a,
  Ne = 0x1b,
  StrictEq = 0x1c,
  StrictNe = 0x1d,
  Lt = 0x1e,
  Le = 0x1f,
  Gt = 0x20,
  Ge = 0x21,
  Index = 0x22,
  IndexMut = 0x23,
  Contains = 0x24,
  NullCoalesce = 0x25
};

/// \brief Whether a primitive binary operator is defined for operands of the
/// given primitive type.
///
/// This is the type rule for same-typed primitive operands (ZOM has no numeric
/// widening, so operands of different types are rejected before this is asked).
/// It mirrors Rust's operator traits:
/// - Equality (`==`, `!=`) is defined for every primitive scalar, including
///   `bool`.
/// - Ordering comparisons (`<`, `<=`, `>`, `>=`) are defined for numeric types
///   and `char`, never for `bool`.
/// - Arithmetic operators (`+` `-` `*` `/` `%` `**`) are defined for numeric
///   types only; `bool` combines with the logical operators instead.
/// - Bitwise operators (`&` `|` `^`) are defined for integers and `bool`.
/// - Shifts (`<<` `>>` `>>>`) are defined for integers only.
///
/// Returns false for any unrelated operation, so callers can use it as the
/// single "is this a type error, not an unimplemented form" test.
ZC_NODISCARD bool primitiveBinaryOperationAdmits(PrimitiveOperation operation,
                                                 type::semantic::PrimitiveKind kind) noexcept;

enum class CompoundAssignmentOperation : uint8_t {
  AddAssign = 0x01,
  SubAssign = 0x02,
  MulAssign = 0x03,
  DivAssign = 0x04,
  RemAssign = 0x05,
  PowAssign = 0x06,
  ShlAssign = 0x07,
  ShrAssign = 0x08,
  UShrAssign = 0x09,
  BitAndAssign = 0x0a,
  BitOrAssign = 0x0b,
  BitXorAssign = 0x0c,
  LogicalAndAssign = 0x0d,
  LogicalOrAssign = 0x0e,
  NullCoalesceAssign = 0x0f
};

enum class ErrorOperatorKind : uint8_t { Propagate = 0x01, ForcedUnwrap = 0x02 };

struct AssignmentOperator final {};

/// \brief Diagnostics whose display contract contains one semantic operator.
enum class OperatorDiagnostic : uint16_t {
  UnsupportedInterfaceOperation = 4019,
  InvalidBinaryOperands = 4028,
  InvalidComparisonOperands = 4029,
  ConstantArithmeticFailure = 4081
};

/// \brief Closed semantic operator identity used only by diagnostics and canonical facts.
class OperatorKind final {
public:
  explicit OperatorKind(PrimitiveOperation value) noexcept : value(value) {}
  explicit OperatorKind(CompoundAssignmentOperation value) noexcept : value(value) {}
  explicit OperatorKind(AssignmentOperator value) noexcept : value(value) {}
  explicit OperatorKind(ErrorOperatorKind value) noexcept : value(value) {}
  OperatorKind(OperatorKind&&) noexcept = default;
  OperatorKind& operator=(OperatorKind&&) noexcept = default;
  ZC_DISALLOW_COPY(OperatorKind);

  ZC_NODISCARD const auto& variant() const noexcept { return value; }

  /// \brief Maps one syntax enum symbolically without relying on enum ordinals.
  ZC_NODISCARD static zc::Maybe<OperatorKind> fromUnary(ast::UnaryOperatorKind syntax);
  ZC_NODISCARD static zc::Maybe<OperatorKind> fromPostfix(ast::PostfixOperatorKind syntax);
  ZC_NODISCARD static zc::Maybe<OperatorKind> fromBinary(ast::BinaryOperatorKind syntax);
  ZC_NODISCARD static zc::Maybe<OperatorKind> fromAssignment(ast::AssignmentOperatorKind syntax);

private:
  zc::OneOf<PrimitiveOperation, CompoundAssignmentOperation, AssignmentOperator, ErrorOperatorKind>
      value;
};

/// \brief Encodes the exact unframed RFC 0015 operator record.
ZC_NODISCARD zc::Maybe<zc::Array<uint8_t>> encodeOperatorKind(const OperatorKind& value);

/// \brief Encodes the independent `zom.checker-operator-kind` oracle envelope.
ZC_NODISCARD zc::Maybe<zc::Array<uint8_t>> encodeOperatorKindOracle(const OperatorKind& value);

/// \brief Returns the deterministic source spelling for a validated operator.
ZC_NODISCARD zc::Maybe<zc::StringPtr> renderOperatorKind(const OperatorKind& value);

/// \brief Validates a reconstructed operator against one diagnostic's closed subset.
ZC_NODISCARD bool validateDiagnosticOperator(OperatorDiagnostic diagnostic,
                                             const OperatorKind& value,
                                             const OperatorKind& reconstructed);

}  // namespace zomlang::compiler::checker
