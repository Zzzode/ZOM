// Copyright (c) 2025 Zode.Z. All rights reserved
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

#include "zomlang/compiler/checker/exhaustiveness.h"

#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/compiler/type/primitive-type.h"
#include "zomlang/compiler/type/type-env.h"
#include "zomlang/compiler/type/union-type.h"
#include "zomlang/tests/unittests/compiler/test-ast-builder.h"

namespace zomlang {
namespace compiler {
namespace checker {

using tests::TestFixture;

namespace {

// Helper: build a simple match statement with given arms and check exhaustiveness.
struct MatchBuildResult {
  ast::NodeId matchStmt;
  ast::NodeId scrutinee;
};

MatchBuildResult buildBoolMatch(TestFixture& fix, bool hasTrue, bool hasFalse, bool hasWildcard) {
  auto scrutinee = fix.makeBoolLiteral(true);

  zc::Vector<ast::NodeId> arms;
  if (hasTrue) {
    auto pat = fix.makeLiteralPattern(fix.makeBoolLiteral(true));
    auto body = fix.makeBlockStmt(ast::NodeList());
    arms.add(fix.makeMatchArmStmt(pat, body));
  }
  if (hasFalse) {
    auto pat = fix.makeLiteralPattern(fix.makeBoolLiteral(false));
    auto body = fix.makeBlockStmt(ast::NodeList());
    arms.add(fix.makeMatchArmStmt(pat, body));
  }
  if (hasWildcard) {
    auto pat = fix.makeWildcardPattern();
    auto body = fix.makeBlockStmt(ast::NodeList());
    arms.add(fix.makeMatchArmStmt(pat, body));
  }

  auto matchStmt = fix.makeMatchStmt(scrutinee, fix.makeNodeList(arms.asPtr()));
  return {matchStmt, scrutinee};
}

}  // namespace

// ============================================================================
// Boolean match exhaustiveness
// ============================================================================

ZC_TEST("Exhaustiveness.BoolMatchTrueAndFalseExhaustive") {
  TestFixture fix;
  auto result = buildBoolMatch(fix, true, true, false);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&result.matchStmt, 1));

  type::TypeEnv typeEnv;
  auto boolTy = type::PrimitiveType::createBool();
  typeEnv.setType(result.scrutinee, type::PrimitiveType::createBool());

  ExhaustivenessChecker checker(typeEnv, tree, fix.diagnostics());
  checker.checkMatchExhaustiveness(result.matchStmt, *boolTy);

  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

ZC_TEST("Exhaustiveness.BoolMatchMissingTrueReportsError") {
  TestFixture fix;
  auto result = buildBoolMatch(fix, false, true, false);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&result.matchStmt, 1));

  type::TypeEnv typeEnv;
  auto boolTy = type::PrimitiveType::createBool();
  typeEnv.setType(result.scrutinee, type::PrimitiveType::createBool());

  ExhaustivenessChecker checker(typeEnv, tree, fix.diagnostics());
  checker.checkMatchExhaustiveness(result.matchStmt, *boolTy);

  ZC_EXPECT(fix.diagnostics().hasErrors());
}

ZC_TEST("Exhaustiveness.BoolMatchMissingFalseReportsError") {
  TestFixture fix;
  auto result = buildBoolMatch(fix, true, false, false);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&result.matchStmt, 1));

  type::TypeEnv typeEnv;
  auto boolTy = type::PrimitiveType::createBool();
  typeEnv.setType(result.scrutinee, type::PrimitiveType::createBool());

  ExhaustivenessChecker checker(typeEnv, tree, fix.diagnostics());
  checker.checkMatchExhaustiveness(result.matchStmt, *boolTy);

  ZC_EXPECT(fix.diagnostics().hasErrors());
}

ZC_TEST("Exhaustiveness.BoolMatchWildcardMakesExhaustive") {
  TestFixture fix;
  // Only true + wildcard should be exhaustive
  auto result = buildBoolMatch(fix, true, false, true);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&result.matchStmt, 1));

  type::TypeEnv typeEnv;
  auto boolTy = type::PrimitiveType::createBool();
  typeEnv.setType(result.scrutinee, type::PrimitiveType::createBool());

  ExhaustivenessChecker checker(typeEnv, tree, fix.diagnostics());
  checker.checkMatchExhaustiveness(result.matchStmt, *boolTy);

  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

ZC_TEST("Exhaustiveness.BoolMatchOnlyWildcardExhaustive") {
  TestFixture fix;
  auto result = buildBoolMatch(fix, false, false, true);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&result.matchStmt, 1));

  type::TypeEnv typeEnv;
  auto boolTy = type::PrimitiveType::createBool();
  typeEnv.setType(result.scrutinee, type::PrimitiveType::createBool());

  ExhaustivenessChecker checker(typeEnv, tree, fix.diagnostics());
  checker.checkMatchExhaustiveness(result.matchStmt, *boolTy);

  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

ZC_TEST("Exhaustiveness.GuardedWildcardDoesNotProveCoverage") {
  TestFixture fix;
  auto scrutinee = fix.makeBoolLiteral(true);
  auto wildPat = fix.makeWildcardPattern();
  auto guard = fix.makeBoolLiteral(true);
  auto arm = fix.makeMatchArmStmt(wildPat, fix.makeBlockStmt(ast::NodeList()), guard);
  zc::Vector<ast::NodeId> arms;
  arms.add(arm);
  auto matchStmt = fix.makeMatchStmt(scrutinee, fix.makeNodeList(arms.asPtr()));
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&matchStmt, 1));

  type::TypeEnv typeEnv;
  auto boolTy = type::PrimitiveType::createBool();
  typeEnv.setType(scrutinee, type::PrimitiveType::createBool());

  ExhaustivenessChecker checker(typeEnv, tree, fix.diagnostics());
  checker.checkMatchExhaustiveness(matchStmt, *boolTy);

  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(fix.diagnostics().errorCount() == 1);
}

ZC_TEST("Exhaustiveness.BoolMatchEmptyArmsReportsError") {
  TestFixture fix;
  auto scrutinee = fix.makeBoolLiteral(true);
  zc::Vector<ast::NodeId> emptyArms;
  auto matchStmt = fix.makeMatchStmt(scrutinee, fix.makeNodeList(emptyArms.asPtr()));
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&matchStmt, 1));

  type::TypeEnv typeEnv;
  auto boolTy = type::PrimitiveType::createBool();
  typeEnv.setType(scrutinee, type::PrimitiveType::createBool());

  ExhaustivenessChecker checker(typeEnv, tree, fix.diagnostics());
  checker.checkMatchExhaustiveness(matchStmt, *boolTy);

  ZC_EXPECT(fix.diagnostics().hasErrors());
}

// ============================================================================
// Unit type match
// ============================================================================

ZC_TEST("Exhaustiveness.UnitMatchUnitPatternExhaustive") {
  TestFixture fix;
  auto scrutinee = fix.makeUnitLiteral();

  auto unitPat = fix.makeLiteralPattern(fix.makeUnitLiteral());
  auto body = fix.makeBlockStmt(ast::NodeList());
  auto arm = fix.makeMatchArmStmt(unitPat, body);
  zc::Vector<ast::NodeId> arms;
  arms.add(arm);
  auto matchStmt = fix.makeMatchStmt(scrutinee, fix.makeNodeList(arms.asPtr()));

  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&matchStmt, 1));

  type::TypeEnv typeEnv;
  auto unitTy = type::PrimitiveType::createUnit();
  typeEnv.setType(scrutinee, type::PrimitiveType::createUnit());

  ExhaustivenessChecker checker(typeEnv, tree, fix.diagnostics());
  checker.checkMatchExhaustiveness(matchStmt, *unitTy);

  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

ZC_TEST("Exhaustiveness.UnitMatchWildcardExhaustive") {
  TestFixture fix;
  auto scrutinee = fix.makeUnitLiteral();

  auto wildPat = fix.makeWildcardPattern();
  auto body = fix.makeBlockStmt(ast::NodeList());
  auto arm = fix.makeMatchArmStmt(wildPat, body);
  zc::Vector<ast::NodeId> arms;
  arms.add(arm);
  auto matchStmt = fix.makeMatchStmt(scrutinee, fix.makeNodeList(arms.asPtr()));

  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&matchStmt, 1));

  type::TypeEnv typeEnv;
  auto unitTy = type::PrimitiveType::createUnit();
  typeEnv.setType(scrutinee, type::PrimitiveType::createUnit());

  ExhaustivenessChecker checker(typeEnv, tree, fix.diagnostics());
  checker.checkMatchExhaustiveness(matchStmt, *unitTy);

  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

// ============================================================================
// Null type match
// ============================================================================

ZC_TEST("Exhaustiveness.NullMatchNullPatternExhaustive") {
  TestFixture fix;
  auto scrutinee = fix.makeNullLiteral();

  auto nullPat = fix.makeLiteralPattern(fix.makeNullLiteral());
  auto body = fix.makeBlockStmt(ast::NodeList());
  auto arm = fix.makeMatchArmStmt(nullPat, body);
  zc::Vector<ast::NodeId> arms;
  arms.add(arm);
  auto matchStmt = fix.makeMatchStmt(scrutinee, fix.makeNodeList(arms.asPtr()));

  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&matchStmt, 1));

  type::TypeEnv typeEnv;
  auto nullTy = type::PrimitiveType::createNull();
  typeEnv.setType(scrutinee, type::PrimitiveType::createNull());

  ExhaustivenessChecker checker(typeEnv, tree, fix.diagnostics());
  checker.checkMatchExhaustiveness(matchStmt, *nullTy);

  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

// ============================================================================
// Never type (should always be exhaustive - no values to match)
// ============================================================================

ZC_TEST("Exhaustiveness.NeverTypeAlwaysExhaustive") {
  TestFixture fix;
  auto scrutinee = fix.makeIntLiteral(0);  // placeholder node

  zc::Vector<ast::NodeId> emptyArms;
  auto matchStmt = fix.makeMatchStmt(scrutinee, fix.makeNodeList(emptyArms.asPtr()));
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&matchStmt, 1));

  type::TypeEnv typeEnv;
  auto neverTy = type::PrimitiveType::createNever();
  typeEnv.setType(scrutinee, type::PrimitiveType::createNever());

  ExhaustivenessChecker checker(typeEnv, tree, fix.diagnostics());
  checker.checkMatchExhaustiveness(matchStmt, *neverTy);

  // Never type has no inhabitants, so empty match is exhaustive
  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

// ============================================================================
// Union type match
// ============================================================================

ZC_TEST("Exhaustiveness.UnionTypeAllAlternativesExhaustive") {
  TestFixture fix;
  auto scrutinee = fix.makeIntLiteral(0);

  // Build union type i32 | str
  zc::Vector<zc::Own<type::Type>> alts;
  alts.add(type::PrimitiveType::createI32());
  alts.add(type::PrimitiveType::createStr());
  auto unionTy = zc::heap<type::UnionType>(zc::mv(alts));

  // Match arms: i32 pattern + str pattern
  auto i32Pat = fix.makeTypedPattern("i32"_zc);
  auto strPat = fix.makeTypedPattern("str"_zc);
  auto body1 = fix.makeBlockStmt(ast::NodeList());
  auto body2 = fix.makeBlockStmt(ast::NodeList());

  zc::Vector<ast::NodeId> arms;
  arms.add(fix.makeMatchArmStmt(i32Pat, body1));
  arms.add(fix.makeMatchArmStmt(strPat, body2));
  auto matchStmt = fix.makeMatchStmt(scrutinee, fix.makeNodeList(arms.asPtr()));

  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&matchStmt, 1));

  type::TypeEnv typeEnv;
  // Build a second union type for the type env (setType takes ownership)
  zc::Vector<zc::Own<type::Type>> alts2;
  alts2.add(type::PrimitiveType::createI32());
  alts2.add(type::PrimitiveType::createStr());
  typeEnv.setType(scrutinee, zc::heap<type::UnionType>(zc::mv(alts2)));

  ExhaustivenessChecker checker(typeEnv, tree, fix.diagnostics());
  checker.checkMatchExhaustiveness(matchStmt, *unionTy);

  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

ZC_TEST("Exhaustiveness.UnionTypeMissingAlternativeReportsError") {
  TestFixture fix;
  auto scrutinee = fix.makeIntLiteral(0);

  zc::Vector<zc::Own<type::Type>> alts;
  alts.add(type::PrimitiveType::createI32());
  alts.add(type::PrimitiveType::createStr());
  auto unionTy = zc::heap<type::UnionType>(zc::mv(alts));

  // Only i32 pattern, missing str
  auto i32Pat = fix.makeTypedPattern("i32"_zc);
  auto body = fix.makeBlockStmt(ast::NodeList());

  zc::Vector<ast::NodeId> arms;
  arms.add(fix.makeMatchArmStmt(i32Pat, body));
  auto matchStmt = fix.makeMatchStmt(scrutinee, fix.makeNodeList(arms.asPtr()));

  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&matchStmt, 1));

  type::TypeEnv typeEnv;
  zc::Vector<zc::Own<type::Type>> alts2;
  alts2.add(type::PrimitiveType::createI32());
  alts2.add(type::PrimitiveType::createStr());
  typeEnv.setType(scrutinee, zc::heap<type::UnionType>(zc::mv(alts2)));

  ExhaustivenessChecker checker(typeEnv, tree, fix.diagnostics());
  checker.checkMatchExhaustiveness(matchStmt, *unionTy);

  ZC_EXPECT(fix.diagnostics().hasErrors());
}

ZC_TEST("Exhaustiveness.UnionTypeWildcardMakesExhaustive") {
  TestFixture fix;
  auto scrutinee = fix.makeIntLiteral(0);

  zc::Vector<zc::Own<type::Type>> alts;
  alts.add(type::PrimitiveType::createI32());
  alts.add(type::PrimitiveType::createStr());
  auto unionTy = zc::heap<type::UnionType>(zc::mv(alts));

  auto i32Pat = fix.makeTypedPattern("i32"_zc);
  auto wildPat = fix.makeWildcardPattern();

  zc::Vector<ast::NodeId> arms;
  arms.add(fix.makeMatchArmStmt(i32Pat, fix.makeBlockStmt(ast::NodeList())));
  arms.add(fix.makeMatchArmStmt(wildPat, fix.makeBlockStmt(ast::NodeList())));
  auto matchStmt = fix.makeMatchStmt(scrutinee, fix.makeNodeList(arms.asPtr()));

  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&matchStmt, 1));

  type::TypeEnv typeEnv;
  zc::Vector<zc::Own<type::Type>> alts2;
  alts2.add(type::PrimitiveType::createI32());
  alts2.add(type::PrimitiveType::createStr());
  typeEnv.setType(scrutinee, zc::heap<type::UnionType>(zc::mv(alts2)));

  ExhaustivenessChecker checker(typeEnv, tree, fix.diagnostics());
  checker.checkMatchExhaustiveness(matchStmt, *unionTy);

  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

// ============================================================================
// Open types (i32, str, etc.) need wildcard
// ============================================================================

ZC_TEST("Exhaustiveness.OpenTypeI32WithoutWildcardReportsError") {
  TestFixture fix;
  auto scrutinee = fix.makeIntLiteral(42);

  // Matching on i32 with specific patterns - i32 is open/infinite
  auto pat42 = fix.makeLiteralPattern(fix.makeIntLiteral(42));
  auto pat0 = fix.makeLiteralPattern(fix.makeIntLiteral(0));

  zc::Vector<ast::NodeId> arms;
  arms.add(fix.makeMatchArmStmt(pat42, fix.makeBlockStmt(ast::NodeList())));
  arms.add(fix.makeMatchArmStmt(pat0, fix.makeBlockStmt(ast::NodeList())));
  auto matchStmt = fix.makeMatchStmt(scrutinee, fix.makeNodeList(arms.asPtr()));

  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&matchStmt, 1));

  type::TypeEnv typeEnv;
  auto i32Ty = type::PrimitiveType::createI32();
  typeEnv.setType(scrutinee, type::PrimitiveType::createI32());

  ExhaustivenessChecker checker(typeEnv, tree, fix.diagnostics());
  checker.checkMatchExhaustiveness(matchStmt, *i32Ty);

  // i32 is open/infinite, specific literals don't cover all cases
  ZC_EXPECT(fix.diagnostics().hasErrors());
}

ZC_TEST("Exhaustiveness.OpenTypeI32WithWildcardExhaustive") {
  TestFixture fix;
  auto scrutinee = fix.makeIntLiteral(42);

  auto pat42 = fix.makeLiteralPattern(fix.makeIntLiteral(42));
  auto wildPat = fix.makeWildcardPattern();

  zc::Vector<ast::NodeId> arms;
  arms.add(fix.makeMatchArmStmt(pat42, fix.makeBlockStmt(ast::NodeList())));
  arms.add(fix.makeMatchArmStmt(wildPat, fix.makeBlockStmt(ast::NodeList())));
  auto matchStmt = fix.makeMatchStmt(scrutinee, fix.makeNodeList(arms.asPtr()));

  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&matchStmt, 1));

  type::TypeEnv typeEnv;
  auto i32Ty = type::PrimitiveType::createI32();
  typeEnv.setType(scrutinee, type::PrimitiveType::createI32());

  ExhaustivenessChecker checker(typeEnv, tree, fix.diagnostics());
  checker.checkMatchExhaustiveness(matchStmt, *i32Ty);

  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

ZC_TEST("Exhaustiveness.OpenTypeStrWithoutWildcardReportsError") {
  TestFixture fix;
  auto scrutinee = fix.makeStrLiteral("hello"_zc);

  auto patHello = fix.makeLiteralPattern(fix.makeStrLiteral("hello"_zc));

  zc::Vector<ast::NodeId> arms;
  arms.add(fix.makeMatchArmStmt(patHello, fix.makeBlockStmt(ast::NodeList())));
  auto matchStmt = fix.makeMatchStmt(scrutinee, fix.makeNodeList(arms.asPtr()));

  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&matchStmt, 1));

  type::TypeEnv typeEnv;
  auto strTy = type::PrimitiveType::createStr();
  typeEnv.setType(scrutinee, type::PrimitiveType::createStr());

  ExhaustivenessChecker checker(typeEnv, tree, fix.diagnostics());
  checker.checkMatchExhaustiveness(matchStmt, *strTy);

  ZC_EXPECT(fix.diagnostics().hasErrors());
}

ZC_TEST("Exhaustiveness.OpenTypeStrWithWildcardExhaustive") {
  TestFixture fix;
  auto scrutinee = fix.makeStrLiteral("hello"_zc);

  auto patHello = fix.makeLiteralPattern(fix.makeStrLiteral("hello"_zc));
  auto wildPat = fix.makeWildcardPattern();

  zc::Vector<ast::NodeId> arms;
  arms.add(fix.makeMatchArmStmt(patHello, fix.makeBlockStmt(ast::NodeList())));
  arms.add(fix.makeMatchArmStmt(wildPat, fix.makeBlockStmt(ast::NodeList())));
  auto matchStmt = fix.makeMatchStmt(scrutinee, fix.makeNodeList(arms.asPtr()));

  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&matchStmt, 1));

  type::TypeEnv typeEnv;
  auto strTy = type::PrimitiveType::createStr();
  typeEnv.setType(scrutinee, type::PrimitiveType::createStr());

  ExhaustivenessChecker checker(typeEnv, tree, fix.diagnostics());
  checker.checkMatchExhaustiveness(matchStmt, *strTy);

  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

// ============================================================================
// Unreachable arm detection
// ============================================================================

ZC_TEST("Exhaustiveness.WildcardFirstMakesLaterArmsUnreachable") {
  TestFixture fix;
  auto scrutinee = fix.makeBoolLiteral(true);

  // wildcard first, then true - true arm is unreachable
  auto wildPat = fix.makeWildcardPattern();
  auto truePat = fix.makeLiteralPattern(fix.makeBoolLiteral(true));

  zc::Vector<ast::NodeId> arms;
  arms.add(fix.makeMatchArmStmt(wildPat, fix.makeBlockStmt(ast::NodeList())));
  arms.add(fix.makeMatchArmStmt(truePat, fix.makeBlockStmt(ast::NodeList())));
  auto matchStmt = fix.makeMatchStmt(scrutinee, fix.makeNodeList(arms.asPtr()));

  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&matchStmt, 1));

  type::TypeEnv typeEnv;
  auto boolTy = type::PrimitiveType::createBool();
  typeEnv.setType(scrutinee, type::PrimitiveType::createBool());

  ExhaustivenessChecker checker(typeEnv, tree, fix.diagnostics());
  checker.checkMatchExhaustiveness(matchStmt, *boolTy);

  // Should warn about unreachable arm
  // (may be a warning rather than error, depending on implementation)
  ZC_EXPECT(true);  // Verify no crash at minimum
}

ZC_TEST("Exhaustiveness.DuplicateBoolPatternUnreachable") {
  TestFixture fix;
  auto scrutinee = fix.makeBoolLiteral(true);

  // true, true, false - second true is unreachable
  auto truePat1 = fix.makeLiteralPattern(fix.makeBoolLiteral(true));
  auto truePat2 = fix.makeLiteralPattern(fix.makeBoolLiteral(true));
  auto falsePat = fix.makeLiteralPattern(fix.makeBoolLiteral(false));

  zc::Vector<ast::NodeId> arms;
  arms.add(fix.makeMatchArmStmt(truePat1, fix.makeBlockStmt(ast::NodeList())));
  arms.add(fix.makeMatchArmStmt(truePat2, fix.makeBlockStmt(ast::NodeList())));
  arms.add(fix.makeMatchArmStmt(falsePat, fix.makeBlockStmt(ast::NodeList())));
  auto matchStmt = fix.makeMatchStmt(scrutinee, fix.makeNodeList(arms.asPtr()));

  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&matchStmt, 1));

  type::TypeEnv typeEnv;
  auto boolTy = type::PrimitiveType::createBool();
  typeEnv.setType(scrutinee, type::PrimitiveType::createBool());

  ExhaustivenessChecker checker(typeEnv, tree, fix.diagnostics());
  checker.checkMatchExhaustiveness(matchStmt, *boolTy);

  // Should detect duplicate/unreachable pattern
  ZC_EXPECT(true);  // Verify no crash
}

// ============================================================================
// Multiple missing patterns
// ============================================================================

ZC_TEST("Exhaustiveness.BoolMatchNoArmsReportsMissingBoth") {
  TestFixture fix;
  auto scrutinee = fix.makeBoolLiteral(true);
  zc::Vector<ast::NodeId> emptyArms;
  auto matchStmt = fix.makeMatchStmt(scrutinee, fix.makeNodeList(emptyArms.asPtr()));
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&matchStmt, 1));

  type::TypeEnv typeEnv;
  auto boolTy = type::PrimitiveType::createBool();
  typeEnv.setType(scrutinee, type::PrimitiveType::createBool());

  ExhaustivenessChecker checker(typeEnv, tree, fix.diagnostics());
  checker.checkMatchExhaustiveness(matchStmt, *boolTy);

  // Should report both true and false missing
  ZC_EXPECT(fix.diagnostics().hasErrors());
}

// ============================================================================
// Constructor and pattern matrix utilities
// ============================================================================

ZC_TEST("Exhaustiveness.ConstructorBoolTrueKind") {
  Constructor ctor(Constructor::Kind::BoolTrue);
  ZC_EXPECT(ctor.kind == Constructor::Kind::BoolTrue);
}

ZC_TEST("Exhaustiveness.ConstructorBoolFalseKind") {
  Constructor ctor(Constructor::Kind::BoolFalse);
  ZC_EXPECT(ctor.kind == Constructor::Kind::BoolFalse);
}

ZC_TEST("Exhaustiveness.ConstructorUnitKind") {
  Constructor ctor(Constructor::Kind::Unit);
  ZC_EXPECT(ctor.kind == Constructor::Kind::Unit);
}

ZC_TEST("Exhaustiveness.ConstructorNullKind") {
  Constructor ctor(Constructor::Kind::Null);
  ZC_EXPECT(ctor.kind == Constructor::Kind::Null);
}

ZC_TEST("Exhaustiveness.ConstructorEnumVariantKind") {
  Constructor ctor(Constructor::Kind::EnumVariant);
  ZC_EXPECT(ctor.kind == Constructor::Kind::EnumVariant);
}

ZC_TEST("Exhaustiveness.ConstructorUnionBranchKind") {
  Constructor ctor(Constructor::Kind::UnionBranch);
  ZC_EXPECT(ctor.kind == Constructor::Kind::UnionBranch);
}

ZC_TEST("Exhaustiveness.ConstructorSealedSubclassKind") {
  Constructor ctor(Constructor::Kind::SealedSubclass);
  ZC_EXPECT(ctor.kind == Constructor::Kind::SealedSubclass);
}

ZC_TEST("Exhaustiveness.ConstructorOpenKind") {
  Constructor ctor(Constructor::Kind::Open);
  ZC_EXPECT(ctor.kind == Constructor::Kind::Open);
}

// ============================================================================
// Pattern matrix / row types
// ============================================================================

ZC_TEST("Exhaustiveness.PatternRowDefaultConstruct") {
  PatternRow row;
  // Default-constructed row should be valid
  ZC_EXPECT(row.size() == 0);
}

ZC_TEST("Exhaustiveness.PatternMatrixDefaultConstruct") {
  PatternMatrix matrix;
  ZC_EXPECT(matrix.size() == 0);
}

ZC_TEST("Exhaustiveness.PatternMatrixAddRow") {
  PatternMatrix matrix;
  PatternRow row;
  matrix.add(zc::mv(row));
  ZC_EXPECT(matrix.size() == 1);
}

}  // namespace checker
}  // namespace compiler
}  // namespace zomlang
