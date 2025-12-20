// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include <cstdint>

namespace zomlang {
namespace compiler {

namespace lexer {
enum class SyntaxKind;
}  // namespace lexer

namespace ast {

// Operator precedence levels
enum class OperatorPrecedence : uint8_t {
  kLowest = 0,
  kComma,           // ,
  kAssignment,      // =, +=, -=, etc.
  kConditional,     // ?:
  kLogicalOr,       // ||
  kLogicalAnd,      // &&
  kBitwiseOr,       // |
  kBitwiseXor,      // ^
  kBitwiseAnd,      // &
  kEquality,        // ==, !=
  kRelational,      // <, >, <=, >=
  kShift,           // <<, >>
  kAdditive,        // +, -
  kMultiplicative,  // *, /, %
  kExponentiation,  // **
  kUnary,           // +, -, !, ~, ++, --
  kPostfix,         // ++, --, [], (), .
  kLeftHandSide,    // left-hand side of an assignment
  kMemberAccess,    // . []
  kPrimary,         // literals, identifiers

  kInvalid = 255
};

// Operator associativity
enum class OperatorAssociativity { kLeft, kRight, kNone };

// Operator types
enum class OperatorType { kBinary, kUnary, kAssignment, kUpdate };

// Binary operators: +, -, *, /, %, ==, !=, <, >, <=, >=, &&, ||, &, |, ^, <<, >>
// Unary operators: +, -, !, ~, ++, --
// Assignment operators: =, +=, -=, *=, /=, %=, &=, |=, ^=, <<=, >>=

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
