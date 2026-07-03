// Copyright (c) 2024-2025 Zode.Z. All rights reserved
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
// See the License for the specific language governing permissions and
// limitations under the License.

// Recovery contract tests per RFC 0002.
//
// These tests verify the parser's error-recovery guarantees:
//   1. Bounded diagnostics (errorBudget = 100)
//   2. EOF termination (parser always terminates at EOF)
//   3. Progress invariants (recovery always advances the token cursor)
//   4. Fail closed on error (parse() returns zc::none)

#include <cstddef>
#include <cstring>

#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/parser/parser.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {
namespace parser {

namespace {

constexpr size_t kErrorBudget = 100;

zc::String repeatSource(zc::StringPtr source, size_t count) {
  zc::String result = zc::heapString(source.size() * count);
  char* cursor = result.begin();
  for (size_t i = 0; i < count; ++i) {
    memcpy(cursor, source.begin(), source.size());
    cursor += source.size();
  }
  return result;
}

/// Helper: create a parser for the given source code and run parse().
/// Returns the parse result and sets hadErrors / errorCount from the engine.
struct ParseOutcome {
  zc::Maybe<ast::Tree> result;
  bool hadErrors = false;
  size_t errorCount = 0;
};

ParseOutcome parseSource(zc::StringPtr source) {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(source.asBytes(), "recovery-test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  ParseOutcome outcome;
  outcome.result = parser.parse();
  outcome.hadErrors = diagnosticEngine->hasErrors();
  outcome.errorCount = diagnosticEngine->errorCount();
  return outcome;
}

}  // namespace

// ============================================================================
// 1. Bounded diagnostics -- errorBudget = 100
// ============================================================================

ZC_TEST("RecoveryTest.BoundedDiagnostics") {
  // Generate enough distinct errors to exceed the error budget.
  // Each "let = ;" on its own line produces one error at a unique location,
  // so deduplication does not apply.  With 200 lines we expect the count
  // to be capped at the budget (100).
  zc::String source = repeatSource("let = ;\n"_zc, 200);
  auto outcome = parseSource(source.asPtr());

  ZC_EXPECT(outcome.hadErrors, "Invalid source should produce errors");
  ZC_EXPECT(outcome.result == zc::none, "Parser must fail closed on invalid input");
  ZC_EXPECT(outcome.errorCount <= kErrorBudget,
            "Diagnostic count must be capped by errorBudget (100)");
  ZC_EXPECT(outcome.errorCount == kErrorBudget,
            "With 200 distinct errors, count should reach the budget cap");
}

ZC_TEST("RecoveryTest.BoundedDiagnosticsManyInvalidStatements") {
  // Alternative path: many malformed expression-statement-like tokens,
  // each on its own line, ensuring different source locations so
  // EmittedDiagnosticKey deduplication does not collapse them.
  zc::String source = repeatSource("} } } } } } } } } }\n"_zc, 25);
  auto outcome = parseSource(source.asPtr());

  ZC_EXPECT(outcome.hadErrors, "Unbalanced braces should produce errors");
  ZC_EXPECT(outcome.errorCount <= kErrorBudget, "Diagnostic count must never exceed errorBudget");
}

// ============================================================================
// 2. EOF termination -- parser always terminates cleanly at EOF
// ============================================================================

ZC_TEST("RecoveryTest.EofTerminationMidLet") {
  // Source ends mid-let-statement without a value or semicolon.
  // The parser must reach EOF and return rather than looping.
  auto outcome = parseSource("let x = ");

  ZC_EXPECT(outcome.hadErrors, "Incomplete let statement should produce errors");
  ZC_EXPECT(outcome.result == zc::none, "Parser must fail closed");
  // If we reach here, the parser terminated -- no infinite loop.
}

ZC_TEST("RecoveryTest.EofTerminationMidFunction") {
  // Source ends mid-function-body without a closing brace.
  auto outcome = parseSource("fun foo() { let x = 1;");

  ZC_EXPECT(outcome.hadErrors, "Unclosed function body should produce errors");
  ZC_EXPECT(outcome.result == zc::none, "Parser must fail closed");
}

ZC_TEST("RecoveryTest.EofTerminationMidExpression") {
  // Source ends mid-binary-expression.
  auto outcome = parseSource("let x = 1 +");

  ZC_EXPECT(outcome.hadErrors, "Incomplete expression should produce errors");
  ZC_EXPECT(outcome.result == zc::none, "Parser must fail closed");
}

ZC_TEST("RecoveryTest.EofTerminationMidString") {
  // Source ends with an unterminated string literal.
  auto outcome = parseSource("let x = \"hello");

  ZC_EXPECT(outcome.hadErrors, "Unterminated string should produce errors");
  ZC_EXPECT(outcome.result == zc::none, "Parser must fail closed");
}

ZC_TEST("RecoveryTest.EofTerminationEmptySource") {
  // Empty source should parse successfully and terminate.
  auto outcome = parseSource("");

  ZC_EXPECT(!outcome.hadErrors, "Empty source should not produce errors");
  ZC_EXPECT(outcome.result != zc::none, "Empty source should parse successfully");
}

// ============================================================================
// 3. Progress invariant -- recovery always advances the token cursor
// ============================================================================

ZC_TEST("RecoveryTest.ProgressInvariantManyInvalidTokens") {
  // Feed a large number of consecutive invalid tokens.  If the recovery
  // system ever failed to advance the cursor, this would hang forever.
  // The fact that parse() returns proves the progress invariant holds.
  zc::String source = repeatSource("} "_zc, 1000);
  auto outcome = parseSource(source.asPtr());

  // Parser returned -- progress invariant verified.
  ZC_EXPECT(outcome.hadErrors, "Many invalid tokens should produce errors");
  ZC_EXPECT(outcome.result == zc::none, "Parser must fail closed");
}

ZC_TEST("RecoveryTest.ProgressInvariantRepeatedMalformedLets") {
  // Each "let = ;" triggers a recovery.  Verify that the parser
  // processes all of them and returns (no infinite loop).
  zc::String source = repeatSource("let = ;\n"_zc, 500);
  auto outcome = parseSource(source.asPtr());

  // Parser returned -- each recovery step advanced the cursor.
  ZC_EXPECT(outcome.hadErrors, "Malformed let statements should produce errors");
  ZC_EXPECT(outcome.errorCount <= kErrorBudget,
            "Even with 500 malformed statements, error count stays within budget");
}

ZC_TEST("RecoveryTest.ProgressInvariantMixedGarbage") {
  // Mix of valid-but-out-of-context tokens to stress recovery paths.
  // Uses statement-level malformed constructs that recovery can handle.
  zc::String source = repeatSource("let = ;\nfun ( { }\nlet x = ;\n"_zc, 50);
  auto outcome = parseSource(source.asPtr());

  // Parser returned -- progress holds across mixed garbage.
  ZC_EXPECT(outcome.hadErrors, "Mixed garbage should produce errors");
  ZC_EXPECT(outcome.result == zc::none, "Parser must fail closed");
  ZC_EXPECT(outcome.errorCount <= kErrorBudget,
            "Error count stays within budget even with mixed garbage");
}

// ============================================================================
// 4. Fail closed on error -- parse() returns zc::none after any error
// ============================================================================

ZC_TEST("RecoveryTest.FailClosedMissingInitializer") {
  auto outcome = parseSource("let x = ;");

  ZC_EXPECT(outcome.hadErrors, "Missing initializer should produce errors");
  ZC_EXPECT(outcome.result == zc::none,
            "Parser must fail closed (return zc::none) for missing initializer");
}

ZC_TEST("RecoveryTest.FailClosedMissingSemicolon") {
  auto outcome = parseSource("let x = 1 let y = 2;");

  ZC_EXPECT(outcome.hadErrors, "Missing semicolon should produce errors");
  ZC_EXPECT(outcome.result == zc::none, "Parser must fail closed for missing semicolons");
}

ZC_TEST("RecoveryTest.FailClosedMissingClosingBrace") {
  auto outcome = parseSource("fun foo() { let x = 1;");

  ZC_EXPECT(outcome.hadErrors, "Missing closing brace should produce errors");
  ZC_EXPECT(outcome.result == zc::none, "Parser must fail closed for unclosed function body");
}

ZC_TEST("RecoveryTest.FailClosedUnterminatedString") {
  auto outcome = parseSource("let x = \"hello");

  ZC_EXPECT(outcome.hadErrors, "Unterminated string should produce errors");
  ZC_EXPECT(outcome.result == zc::none, "Parser must fail closed for unterminated string literal");
}

ZC_TEST("RecoveryTest.FailClosedInvalidTokenSequence") {
  auto outcome = parseSource("let = ;");

  ZC_EXPECT(outcome.hadErrors, "Invalid token sequence should produce errors");
  ZC_EXPECT(outcome.result == zc::none, "Parser must fail closed for invalid token sequences");
}

ZC_TEST("RecoveryTest.FailClosedExtraTokens") {
  auto outcome = parseSource("let x = 1; } extra");

  ZC_EXPECT(outcome.hadErrors, "Extra tokens after statement should produce errors");
  ZC_EXPECT(outcome.result == zc::none, "Parser must fail closed for extra tokens");
}

ZC_TEST("RecoveryTest.FailClosedMisplacedModule") {
  auto outcome = parseSource("import math::geometry;\nmodule graphics;\nlet x = 1;\n");

  ZC_EXPECT(outcome.hadErrors, "Late module declaration should produce errors");
  ZC_EXPECT(outcome.result == zc::none,
            "Parser must fail closed for misplaced module declarations");
}

ZC_TEST("RecoveryTest.FailClosedValidSourceReturnsTree") {
  // Sanity check: valid source returns a tree (not zc::none).
  auto outcome = parseSource("let x: i32 = 42;");

  ZC_EXPECT(!outcome.hadErrors, "Valid source should not produce errors");
  ZC_EXPECT(outcome.result != zc::none, "Valid source should return a tree (not zc::none)");
  ZC_EXPECT(outcome.errorCount == 0, "Valid source should have zero errors");
}

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
