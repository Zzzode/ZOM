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

#include "zomlang/compiler/checker/operator-kind.h"

#include "zc/core/vector.h"

namespace zomlang::compiler::checker {
namespace {

bool known(PrimitiveOperation value) {
  switch (value) {
    case PrimitiveOperation::UnaryPlus:
    case PrimitiveOperation::Neg:
    case PrimitiveOperation::LogicalNot:
    case PrimitiveOperation::BitNot:
    case PrimitiveOperation::Dereference:
    case PrimitiveOperation::BorrowShared:
    case PrimitiveOperation::BorrowMutable:
    case PrimitiveOperation::PreIncrement:
    case PrimitiveOperation::PreDecrement:
    case PrimitiveOperation::PostIncrement:
    case PrimitiveOperation::PostDecrement:
    case PrimitiveOperation::Add:
    case PrimitiveOperation::Sub:
    case PrimitiveOperation::Mul:
    case PrimitiveOperation::Div:
    case PrimitiveOperation::Rem:
    case PrimitiveOperation::Pow:
    case PrimitiveOperation::Shl:
    case PrimitiveOperation::Shr:
    case PrimitiveOperation::UShr:
    case PrimitiveOperation::BitAnd:
    case PrimitiveOperation::BitOr:
    case PrimitiveOperation::BitXor:
    case PrimitiveOperation::LogicalAnd:
    case PrimitiveOperation::LogicalOr:
    case PrimitiveOperation::Eq:
    case PrimitiveOperation::Ne:
    case PrimitiveOperation::StrictEq:
    case PrimitiveOperation::StrictNe:
    case PrimitiveOperation::Lt:
    case PrimitiveOperation::Le:
    case PrimitiveOperation::Gt:
    case PrimitiveOperation::Ge:
    case PrimitiveOperation::Index:
    case PrimitiveOperation::IndexMut:
    case PrimitiveOperation::Contains:
    case PrimitiveOperation::NullCoalesce:
      return true;
  }
  return false;
}

bool known(CompoundAssignmentOperation value) {
  switch (value) {
    case CompoundAssignmentOperation::AddAssign:
    case CompoundAssignmentOperation::SubAssign:
    case CompoundAssignmentOperation::MulAssign:
    case CompoundAssignmentOperation::DivAssign:
    case CompoundAssignmentOperation::RemAssign:
    case CompoundAssignmentOperation::PowAssign:
    case CompoundAssignmentOperation::ShlAssign:
    case CompoundAssignmentOperation::ShrAssign:
    case CompoundAssignmentOperation::UShrAssign:
    case CompoundAssignmentOperation::BitAndAssign:
    case CompoundAssignmentOperation::BitOrAssign:
    case CompoundAssignmentOperation::BitXorAssign:
    case CompoundAssignmentOperation::LogicalAndAssign:
    case CompoundAssignmentOperation::LogicalOrAssign:
    case CompoundAssignmentOperation::NullCoalesceAssign:
      return true;
  }
  return false;
}

bool known(ErrorOperatorKind value) {
  switch (value) {
    case ErrorOperatorKind::Propagate:
    case ErrorOperatorKind::ForcedUnwrap:
      return true;
  }
  return false;
}

bool sameOperatorKind(const OperatorKind& left, const OperatorKind& right) {
  const auto& leftValue = left.variant();
  const auto& rightValue = right.variant();
  if (leftValue.is<PrimitiveOperation>()) {
    return rightValue.is<PrimitiveOperation>() &&
           leftValue.get<PrimitiveOperation>() == rightValue.get<PrimitiveOperation>();
  }
  if (leftValue.is<CompoundAssignmentOperation>()) {
    return rightValue.is<CompoundAssignmentOperation>() &&
           leftValue.get<CompoundAssignmentOperation>() ==
               rightValue.get<CompoundAssignmentOperation>();
  }
  if (leftValue.is<AssignmentOperator>()) { return rightValue.is<AssignmentOperator>(); }
  return rightValue.is<ErrorOperatorKind>() &&
         leftValue.get<ErrorOperatorKind>() == rightValue.get<ErrorOperatorKind>();
}

bool allowedInterfaceOperation(PrimitiveOperation operation) {
  switch (operation) {
    case PrimitiveOperation::Neg:
    case PrimitiveOperation::LogicalNot:
    case PrimitiveOperation::Add:
    case PrimitiveOperation::Sub:
    case PrimitiveOperation::Mul:
    case PrimitiveOperation::Div:
    case PrimitiveOperation::Rem:
    case PrimitiveOperation::Pow:
    case PrimitiveOperation::Eq:
    case PrimitiveOperation::Ne:
    case PrimitiveOperation::Lt:
    case PrimitiveOperation::Le:
    case PrimitiveOperation::Gt:
    case PrimitiveOperation::Ge:
    case PrimitiveOperation::Index:
    case PrimitiveOperation::IndexMut:
    case PrimitiveOperation::Contains:
      return true;
    default:
      return false;
  }
}

bool allowedBinaryOperation(PrimitiveOperation operation) {
  switch (operation) {
    case PrimitiveOperation::Add:
    case PrimitiveOperation::Sub:
    case PrimitiveOperation::Mul:
    case PrimitiveOperation::Div:
    case PrimitiveOperation::Rem:
    case PrimitiveOperation::Pow:
    case PrimitiveOperation::Shl:
    case PrimitiveOperation::Shr:
    case PrimitiveOperation::UShr:
    case PrimitiveOperation::BitAnd:
    case PrimitiveOperation::BitOr:
    case PrimitiveOperation::BitXor:
    case PrimitiveOperation::LogicalAnd:
    case PrimitiveOperation::LogicalOr:
    case PrimitiveOperation::Contains:
    case PrimitiveOperation::NullCoalesce:
      return true;
    default:
      return false;
  }
}

bool allowedComparisonOperation(PrimitiveOperation operation) {
  switch (operation) {
    case PrimitiveOperation::Eq:
    case PrimitiveOperation::Ne:
    case PrimitiveOperation::StrictEq:
    case PrimitiveOperation::StrictNe:
    case PrimitiveOperation::Lt:
    case PrimitiveOperation::Le:
    case PrimitiveOperation::Gt:
    case PrimitiveOperation::Ge:
      return true;
    default:
      return false;
  }
}

}  // namespace

zc::Maybe<OperatorKind> OperatorKind::fromUnary(ast::UnaryOperatorKind syntax) {
  switch (syntax) {
    case ast::UnaryOperatorKind::Plus:
      return OperatorKind(PrimitiveOperation::UnaryPlus);
    case ast::UnaryOperatorKind::Minus:
      return OperatorKind(PrimitiveOperation::Neg);
    case ast::UnaryOperatorKind::LogicalNot:
      return OperatorKind(PrimitiveOperation::LogicalNot);
    case ast::UnaryOperatorKind::BitNot:
      return OperatorKind(PrimitiveOperation::BitNot);
    case ast::UnaryOperatorKind::Deref:
      return OperatorKind(PrimitiveOperation::Dereference);
    case ast::UnaryOperatorKind::Ref:
      return OperatorKind(PrimitiveOperation::BorrowShared);
    case ast::UnaryOperatorKind::RefMut:
      return OperatorKind(PrimitiveOperation::BorrowMutable);
    case ast::UnaryOperatorKind::PreIncrement:
      return OperatorKind(PrimitiveOperation::PreIncrement);
    case ast::UnaryOperatorKind::PreDecrement:
      return OperatorKind(PrimitiveOperation::PreDecrement);
  }
  return zc::none;
}

zc::Maybe<OperatorKind> OperatorKind::fromPostfix(ast::PostfixOperatorKind syntax) {
  switch (syntax) {
    case ast::PostfixOperatorKind::Increment:
      return OperatorKind(PrimitiveOperation::PostIncrement);
    case ast::PostfixOperatorKind::Decrement:
      return OperatorKind(PrimitiveOperation::PostDecrement);
    case ast::PostfixOperatorKind::ErrorPropagate:
      return OperatorKind(ErrorOperatorKind::Propagate);
    case ast::PostfixOperatorKind::ErrorUnwrap:
      return OperatorKind(ErrorOperatorKind::ForcedUnwrap);
  }
  return zc::none;
}

zc::Maybe<OperatorKind> OperatorKind::fromBinary(ast::BinaryOperatorKind syntax) {
  switch (syntax) {
    case ast::BinaryOperatorKind::Add:
      return OperatorKind(PrimitiveOperation::Add);
    case ast::BinaryOperatorKind::Sub:
      return OperatorKind(PrimitiveOperation::Sub);
    case ast::BinaryOperatorKind::Mul:
      return OperatorKind(PrimitiveOperation::Mul);
    case ast::BinaryOperatorKind::Div:
      return OperatorKind(PrimitiveOperation::Div);
    case ast::BinaryOperatorKind::Mod:
      return OperatorKind(PrimitiveOperation::Rem);
    case ast::BinaryOperatorKind::Pow:
      return OperatorKind(PrimitiveOperation::Pow);
    case ast::BinaryOperatorKind::Shl:
      return OperatorKind(PrimitiveOperation::Shl);
    case ast::BinaryOperatorKind::Shr:
      return OperatorKind(PrimitiveOperation::Shr);
    case ast::BinaryOperatorKind::UShr:
      return OperatorKind(PrimitiveOperation::UShr);
    case ast::BinaryOperatorKind::BitAnd:
      return OperatorKind(PrimitiveOperation::BitAnd);
    case ast::BinaryOperatorKind::BitOr:
      return OperatorKind(PrimitiveOperation::BitOr);
    case ast::BinaryOperatorKind::BitXor:
      return OperatorKind(PrimitiveOperation::BitXor);
    case ast::BinaryOperatorKind::LogAnd:
      return OperatorKind(PrimitiveOperation::LogicalAnd);
    case ast::BinaryOperatorKind::LogOr:
      return OperatorKind(PrimitiveOperation::LogicalOr);
    case ast::BinaryOperatorKind::Eq:
      return OperatorKind(PrimitiveOperation::Eq);
    case ast::BinaryOperatorKind::Ne:
      return OperatorKind(PrimitiveOperation::Ne);
    case ast::BinaryOperatorKind::StrictEq:
      return OperatorKind(PrimitiveOperation::StrictEq);
    case ast::BinaryOperatorKind::StrictNe:
      return OperatorKind(PrimitiveOperation::StrictNe);
    case ast::BinaryOperatorKind::Lt:
      return OperatorKind(PrimitiveOperation::Lt);
    case ast::BinaryOperatorKind::Le:
      return OperatorKind(PrimitiveOperation::Le);
    case ast::BinaryOperatorKind::Gt:
      return OperatorKind(PrimitiveOperation::Gt);
    case ast::BinaryOperatorKind::Ge:
      return OperatorKind(PrimitiveOperation::Ge);
  }
  return zc::none;
}

zc::Maybe<OperatorKind> OperatorKind::fromAssignment(ast::AssignmentOperatorKind syntax) {
  switch (syntax) {
    case ast::AssignmentOperatorKind::Assign:
      return OperatorKind(AssignmentOperator{});
    case ast::AssignmentOperatorKind::AddAssign:
      return OperatorKind(CompoundAssignmentOperation::AddAssign);
    case ast::AssignmentOperatorKind::SubAssign:
      return OperatorKind(CompoundAssignmentOperation::SubAssign);
    case ast::AssignmentOperatorKind::MulAssign:
      return OperatorKind(CompoundAssignmentOperation::MulAssign);
    case ast::AssignmentOperatorKind::DivAssign:
      return OperatorKind(CompoundAssignmentOperation::DivAssign);
    case ast::AssignmentOperatorKind::ModAssign:
      return OperatorKind(CompoundAssignmentOperation::RemAssign);
    case ast::AssignmentOperatorKind::PowAssign:
      return OperatorKind(CompoundAssignmentOperation::PowAssign);
    case ast::AssignmentOperatorKind::ShlAssign:
      return OperatorKind(CompoundAssignmentOperation::ShlAssign);
    case ast::AssignmentOperatorKind::ShrAssign:
      return OperatorKind(CompoundAssignmentOperation::ShrAssign);
    case ast::AssignmentOperatorKind::UShrAssign:
      return OperatorKind(CompoundAssignmentOperation::UShrAssign);
    case ast::AssignmentOperatorKind::BitAndAssign:
      return OperatorKind(CompoundAssignmentOperation::BitAndAssign);
    case ast::AssignmentOperatorKind::BitOrAssign:
      return OperatorKind(CompoundAssignmentOperation::BitOrAssign);
    case ast::AssignmentOperatorKind::BitXorAssign:
      return OperatorKind(CompoundAssignmentOperation::BitXorAssign);
    case ast::AssignmentOperatorKind::LogicalAndAssign:
      return OperatorKind(CompoundAssignmentOperation::LogicalAndAssign);
    case ast::AssignmentOperatorKind::LogicalOrAssign:
      return OperatorKind(CompoundAssignmentOperation::LogicalOrAssign);
    case ast::AssignmentOperatorKind::NullCoalesceAssign:
      return OperatorKind(CompoundAssignmentOperation::NullCoalesceAssign);
  }
  return zc::none;
}

zc::Maybe<zc::Array<uint8_t>> encodeOperatorKind(const OperatorKind& value) {
  zc::Vector<uint8_t> result;
  const auto& variant = value.variant();
  if (variant.is<PrimitiveOperation>()) {
    const auto operation = variant.get<PrimitiveOperation>();
    if (!known(operation)) return zc::none;
    result.add(0x01);
    result.add(static_cast<uint8_t>(operation));
  } else if (variant.is<CompoundAssignmentOperation>()) {
    const auto operation = variant.get<CompoundAssignmentOperation>();
    if (!known(operation)) return zc::none;
    result.add(0x02);
    result.add(static_cast<uint8_t>(operation));
  } else if (variant.is<AssignmentOperator>()) {
    result.add(0x03);
  } else {
    const auto operation = variant.get<ErrorOperatorKind>();
    if (!known(operation)) return zc::none;
    result.add(0x04);
    result.add(static_cast<uint8_t>(operation));
  }
  return result.releaseAsArray();
}

zc::Maybe<zc::Array<uint8_t>> encodeOperatorKindOracle(const OperatorKind& value) {
  auto encoded = encodeOperatorKind(value);
  if (encoded == zc::none) return zc::none;
  zc::Vector<uint8_t> result;
  result.addAll(zc::StringPtr("zom.checker-operator-kind.v0").asBytes());
  result.add(0x00);
  ZC_IF_SOME(bytes, encoded) { result.addAll(bytes.asPtr()); }
  return result.releaseAsArray();
}

zc::Maybe<zc::StringPtr> renderOperatorKind(const OperatorKind& value) {
  const auto& variant = value.variant();
  if (variant.is<PrimitiveOperation>()) {
    switch (variant.get<PrimitiveOperation>()) {
      case PrimitiveOperation::UnaryPlus:
        return zc::StringPtr("+");
      case PrimitiveOperation::Neg:
        return zc::StringPtr("-");
      case PrimitiveOperation::LogicalNot:
        return zc::StringPtr("!");
      case PrimitiveOperation::BitNot:
        return zc::StringPtr("~");
      case PrimitiveOperation::Dereference:
        return zc::StringPtr("*");
      case PrimitiveOperation::BorrowShared:
        return zc::StringPtr("&");
      case PrimitiveOperation::BorrowMutable:
        return zc::StringPtr("&mut");
      case PrimitiveOperation::PreIncrement:
        return zc::StringPtr("++");
      case PrimitiveOperation::PreDecrement:
        return zc::StringPtr("--");
      case PrimitiveOperation::PostIncrement:
        return zc::StringPtr("++");
      case PrimitiveOperation::PostDecrement:
        return zc::StringPtr("--");
      case PrimitiveOperation::Add:
        return zc::StringPtr("+");
      case PrimitiveOperation::Sub:
        return zc::StringPtr("-");
      case PrimitiveOperation::Mul:
        return zc::StringPtr("*");
      case PrimitiveOperation::Div:
        return zc::StringPtr("/");
      case PrimitiveOperation::Rem:
        return zc::StringPtr("%");
      case PrimitiveOperation::Pow:
        return zc::StringPtr("**");
      case PrimitiveOperation::Shl:
        return zc::StringPtr("<<");
      case PrimitiveOperation::Shr:
        return zc::StringPtr(">>");
      case PrimitiveOperation::UShr:
        return zc::StringPtr(">>>");
      case PrimitiveOperation::BitAnd:
        return zc::StringPtr("&");
      case PrimitiveOperation::BitOr:
        return zc::StringPtr("|");
      case PrimitiveOperation::BitXor:
        return zc::StringPtr("^");
      case PrimitiveOperation::LogicalAnd:
        return zc::StringPtr("&&");
      case PrimitiveOperation::LogicalOr:
        return zc::StringPtr("||");
      case PrimitiveOperation::Eq:
        return zc::StringPtr("==");
      case PrimitiveOperation::Ne:
        return zc::StringPtr("!=");
      case PrimitiveOperation::StrictEq:
        return zc::StringPtr("===");
      case PrimitiveOperation::StrictNe:
        return zc::StringPtr("!==");
      case PrimitiveOperation::Lt:
        return zc::StringPtr("<");
      case PrimitiveOperation::Le:
        return zc::StringPtr("<=");
      case PrimitiveOperation::Gt:
        return zc::StringPtr(">");
      case PrimitiveOperation::Ge:
        return zc::StringPtr(">=");
      case PrimitiveOperation::Index:
        return zc::StringPtr("[]");
      case PrimitiveOperation::IndexMut:
        return zc::StringPtr("[]");
      case PrimitiveOperation::Contains:
        return zc::StringPtr("in");
      case PrimitiveOperation::NullCoalesce:
        return zc::StringPtr("??");
    }
    return zc::none;
  }
  if (variant.is<CompoundAssignmentOperation>()) {
    switch (variant.get<CompoundAssignmentOperation>()) {
      case CompoundAssignmentOperation::AddAssign:
        return zc::StringPtr("+=");
      case CompoundAssignmentOperation::SubAssign:
        return zc::StringPtr("-=");
      case CompoundAssignmentOperation::MulAssign:
        return zc::StringPtr("*=");
      case CompoundAssignmentOperation::DivAssign:
        return zc::StringPtr("/=");
      case CompoundAssignmentOperation::RemAssign:
        return zc::StringPtr("%=");
      case CompoundAssignmentOperation::PowAssign:
        return zc::StringPtr("**=");
      case CompoundAssignmentOperation::ShlAssign:
        return zc::StringPtr("<<=");
      case CompoundAssignmentOperation::ShrAssign:
        return zc::StringPtr(">>=");
      case CompoundAssignmentOperation::UShrAssign:
        return zc::StringPtr(">>>=");
      case CompoundAssignmentOperation::BitAndAssign:
        return zc::StringPtr("&=");
      case CompoundAssignmentOperation::BitOrAssign:
        return zc::StringPtr("|=");
      case CompoundAssignmentOperation::BitXorAssign:
        return zc::StringPtr("^=");
      case CompoundAssignmentOperation::LogicalAndAssign:
        return zc::StringPtr("&&=");
      case CompoundAssignmentOperation::LogicalOrAssign:
        return zc::StringPtr("||=");
      case CompoundAssignmentOperation::NullCoalesceAssign:
        return zc::StringPtr(
            "?"
            "?=");
    }
    return zc::none;
  }
  if (variant.is<AssignmentOperator>()) return zc::StringPtr("=");
  switch (variant.get<ErrorOperatorKind>()) {
    case ErrorOperatorKind::Propagate:
      return zc::StringPtr("?!");
    case ErrorOperatorKind::ForcedUnwrap:
      return zc::StringPtr("!!");
  }
  return zc::none;
}

bool validateDiagnosticOperator(OperatorDiagnostic diagnostic, const OperatorKind& value,
                                const OperatorKind& reconstructed) {
  if (!sameOperatorKind(value, reconstructed)) { return false; }
  const auto& variant = value.variant();
  if (!variant.is<PrimitiveOperation>()) { return false; }
  const auto operation = variant.get<PrimitiveOperation>();
  if (!known(operation)) { return false; }
  switch (diagnostic) {
    case OperatorDiagnostic::UnsupportedInterfaceOperation:
      return allowedInterfaceOperation(operation);
    case OperatorDiagnostic::InvalidBinaryOperands:
      return allowedBinaryOperation(operation);
    case OperatorDiagnostic::InvalidComparisonOperands:
      return allowedComparisonOperation(operation);
    case OperatorDiagnostic::ConstantArithmeticFailure:
      return true;
  }
  return false;
}

}  // namespace zomlang::compiler::checker
