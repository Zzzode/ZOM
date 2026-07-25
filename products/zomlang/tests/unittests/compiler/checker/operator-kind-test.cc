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

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/sha256.h"

namespace zomlang::compiler::checker {
namespace {

void expectOracle(OperatorKind&& value, zc::StringPtr expectedHex, zc::StringPtr expectedDigest) {
  auto encoded = encodeOperatorKindOracle(value);
  ZC_REQUIRE(encoded != zc::none);
  ZC_IF_SOME(bytes, encoded) {
    ZC_EXPECT(zc::encodeHex(bytes.asPtr()) == expectedHex);
    auto digest = identity::sha256(bytes.asPtr());
    ZC_REQUIRE(digest != zc::none);
    ZC_IF_SOME(actual, digest) { ZC_EXPECT(zc::encodeHex(actual.bytes()) == expectedDigest); }
  }
}

template <typename Syntax>
void expectRendering(zc::Maybe<OperatorKind> (*map)(Syntax), Syntax syntax,
                     zc::StringPtr expected) {
  auto semantic = map(syntax);
  ZC_REQUIRE(semantic != zc::none);
  ZC_IF_SOME(value, semantic) {
    auto rendered = renderOperatorKind(value);
    ZC_REQUIRE(rendered != zc::none);
    ZC_IF_SOME(text, rendered) { ZC_EXPECT(text == expected); }
  }
}

}  // namespace

ZC_TEST("OperatorKind.MatchesNormativeOracleVectors") {
  expectOracle(OperatorKind(PrimitiveOperation::StrictNe),
               zc::StringPtr("7a6f6d2e636865636b65722d6f70657261746f722d6b696e6400011d"),
               zc::StringPtr("d5f8705504de8ddc1fa9c162c0aa7ed45a51525c35d022665f51c7d59bfbe27e"));
  expectOracle(OperatorKind(CompoundAssignmentOperation::NullCoalesceAssign),
               zc::StringPtr("7a6f6d2e636865636b65722d6f70657261746f722d6b696e6400020f"),
               zc::StringPtr("56bbf860601f317e6df86b2215daaa0d57bd0493fa7aeea8c4447e6abd3201c6"));
  expectOracle(OperatorKind(AssignmentOperator{}),
               zc::StringPtr("7a6f6d2e636865636b65722d6f70657261746f722d6b696e640003"),
               zc::StringPtr("b64344ce42585099d4bdb2c8358d4a994689f5bb683a7c4be478d8ea802806e9"));
  expectOracle(OperatorKind(ErrorOperatorKind::Propagate),
               zc::StringPtr("7a6f6d2e636865636b65722d6f70657261746f722d6b696e64000401"),
               zc::StringPtr("bbfd75c73052181aa904dd07679afa60ada8491537ddc0d14ca64fc156915f72"));
}

ZC_TEST("OperatorKind.SymbolicallyMapsEveryAstOperatorFamily") {
  expectRendering(OperatorKind::fromUnary, ast::UnaryOperatorKind::RefMut, zc::StringPtr("&mut"));
  expectRendering(OperatorKind::fromPostfix, ast::PostfixOperatorKind::ErrorUnwrap,
                  zc::StringPtr("!!"));
  expectRendering(OperatorKind::fromBinary, ast::BinaryOperatorKind::Mod, zc::StringPtr("%"));
  expectRendering(OperatorKind::fromBinary, ast::BinaryOperatorKind::StrictEq,
                  zc::StringPtr("==="));
  expectRendering(OperatorKind::fromAssignment, ast::AssignmentOperatorKind::Assign,
                  zc::StringPtr("="));
  expectRendering(OperatorKind::fromAssignment, ast::AssignmentOperatorKind::NullCoalesceAssign,
                  zc::StringPtr("?"
                                "?="));
}

ZC_TEST("OperatorKind.RendersSharedSpellingsWithoutCollapsingSemanticIdentity") {
  auto prefix = encodeOperatorKind(OperatorKind(PrimitiveOperation::PreIncrement));
  auto postfix = encodeOperatorKind(OperatorKind(PrimitiveOperation::PostIncrement));
  ZC_REQUIRE(prefix != zc::none);
  ZC_REQUIRE(postfix != zc::none);
  ZC_IF_SOME(prefixBytes, prefix) {
    ZC_IF_SOME(postfixBytes, postfix) { ZC_EXPECT(prefixBytes.asPtr() != postfixBytes.asPtr()); }
  }
  auto read = encodeOperatorKind(OperatorKind(PrimitiveOperation::Index));
  auto write = encodeOperatorKind(OperatorKind(PrimitiveOperation::IndexMut));
  ZC_REQUIRE(read != zc::none);
  ZC_REQUIRE(write != zc::none);
  ZC_IF_SOME(readBytes, read) {
    ZC_IF_SOME(writeBytes, write) { ZC_EXPECT(readBytes.asPtr() != writeBytes.asPtr()); }
  }
}

ZC_TEST("OperatorKind.EnforcesDiagnosticSpecificSubsetsAndReconstruction") {
  ZC_EXPECT(validateDiagnosticOperator(OperatorDiagnostic::UnsupportedInterfaceOperation,
                                       OperatorKind(PrimitiveOperation::IndexMut),
                                       OperatorKind(PrimitiveOperation::IndexMut)));
  ZC_EXPECT(!validateDiagnosticOperator(OperatorDiagnostic::UnsupportedInterfaceOperation,
                                        OperatorKind(PrimitiveOperation::BitAnd),
                                        OperatorKind(PrimitiveOperation::BitAnd)));

  ZC_EXPECT(validateDiagnosticOperator(OperatorDiagnostic::InvalidBinaryOperands,
                                       OperatorKind(PrimitiveOperation::LogicalOr),
                                       OperatorKind(PrimitiveOperation::LogicalOr)));
  ZC_EXPECT(validateDiagnosticOperator(OperatorDiagnostic::InvalidBinaryOperands,
                                       OperatorKind(PrimitiveOperation::Contains),
                                       OperatorKind(PrimitiveOperation::Contains)));
  ZC_EXPECT(!validateDiagnosticOperator(OperatorDiagnostic::InvalidBinaryOperands,
                                        OperatorKind(PrimitiveOperation::Eq),
                                        OperatorKind(PrimitiveOperation::Eq)));

  ZC_EXPECT(validateDiagnosticOperator(OperatorDiagnostic::InvalidComparisonOperands,
                                       OperatorKind(PrimitiveOperation::StrictNe),
                                       OperatorKind(PrimitiveOperation::StrictNe)));
  ZC_EXPECT(!validateDiagnosticOperator(OperatorDiagnostic::InvalidComparisonOperands,
                                        OperatorKind(PrimitiveOperation::Add),
                                        OperatorKind(PrimitiveOperation::Add)));

  ZC_EXPECT(validateDiagnosticOperator(OperatorDiagnostic::ConstantArithmeticFailure,
                                       OperatorKind(PrimitiveOperation::Neg),
                                       OperatorKind(PrimitiveOperation::Neg)));
  ZC_EXPECT(!validateDiagnosticOperator(OperatorDiagnostic::ConstantArithmeticFailure,
                                        OperatorKind(PrimitiveOperation::Neg),
                                        OperatorKind(PrimitiveOperation::LogicalNot)));
  ZC_EXPECT(!validateDiagnosticOperator(OperatorDiagnostic::ConstantArithmeticFailure,
                                        OperatorKind(CompoundAssignmentOperation::AddAssign),
                                        OperatorKind(CompoundAssignmentOperation::AddAssign)));
}

}  // namespace zomlang::compiler::checker
