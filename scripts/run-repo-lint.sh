#!/usr/bin/env bash
# Repository lint entry point.
#
# These checks are build-free source/artifact policies and cross-artifact
# inventories: naming and stay-dead rules, spec <-> grammar <-> implementation
# set equality, generated-file freshness, and self-tests of the check scripts
# themselves. They are NOT compiler regression tests and therefore do not run
# under ctest; behavioral coverage lives in ztest, lit, negative-compile, and
# the real-command ctest drivers.
#
# Runs entirely from a checkout (no configure/build). Fails fast on the first
# failing check. CI invokes this from the repo-lint job.
set -u
set -o pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PY="${PYTHON:-python3}"
failures=0

run() {
  local description="$1"
  shift
  if "$@"; then
    echo "lint ok: $description"
  else
    echo "lint FAIL: $description" >&2
    failures=$((failures + 1))
  fi
}

# Architecture / drift inventories and their mutation self-tests.
run "binder architecture"          $PY scripts/check-binder-architecture.py --check
run "binder architecture self-test" $PY scripts/check-binder-architecture.py --self-test
run "stable binding schema"        $PY scripts/check-stable-binding-schema.py --check
run "stable binding schema self-test" $PY scripts/check-stable-binding-schema.py --self-test
run "stable landing scope self-test" $PY scripts/check-landing-scope.py --self-test
run "lexer architecture"           $PY scripts/check-lexer-architecture.py
run "spec alignment"               $PY scripts/check-spec-alignment.py --check
run "spec alignment self-test"     $PY scripts/check-spec-alignment.py --self-test
run "parser coverage"              $PY scripts/check-parser-coverage.py
run "compiler session architecture" $PY scripts/check-compiler-session-architecture.py --check
run "compiler session architecture self-test" $PY scripts/check-compiler-session-architecture.py --self-test
run "checker architecture"         $PY scripts/check-checker-architecture.py --check
run "checker architecture self-test" $PY scripts/check-checker-architecture.py --self-test
run "impl source architecture"     $PY scripts/check-impl-source-architecture.py --check
run "impl source architecture self-test" $PY scripts/check-impl-source-architecture.py --self-test
run "identity architecture"        $PY scripts/check-identity-architecture.py --check
run "identity architecture self-test" $PY scripts/check-identity-architecture.py --self-test
run "ownership architecture"       $PY scripts/check-ownership-architecture.py --check
run "ownership architecture self-test" $PY scripts/check-ownership-architecture.py --self-test
run "ownership coverage self-test" $PY scripts/check-ownership-coverage.py --self-test
run "incremental query architecture" $PY scripts/check-incremental-query-architecture.py --check
run "incremental query architecture self-test" $PY scripts/check-incremental-query-architecture.py --self-test
run "query descriptor architecture" $PY scripts/check-query-descriptor-architecture.py --check
run "query descriptor architecture self-test" $PY scripts/check-query-descriptor-architecture.py --self-test
run "package architecture"         $PY scripts/check-package-architecture.py --check
run "package architecture self-test" $PY scripts/check-package-architecture.py --self-test
run "ir architecture"              $PY scripts/check-ir-architecture.py --check
run "ir architecture self-test"    $PY scripts/check-ir-architecture.py --self-test
run "lir verification gate"        $PY scripts/check-lir-verification-gate.py --check
run "lir verification gate self-test" $PY scripts/check-lir-verification-gate.py --self-test
run "diagnostic coverage"          $PY scripts/check-diagnostic-coverage.py --check
run "diagnostic coverage self-test" $PY scripts/check-diagnostic-coverage.py --self-test
run "lit exec root"                $PY scripts/check-lit-exec-root.py --check
run "lit exec root self-test"      $PY scripts/check-lit-exec-root.py --self-test
run "incremental query benchmark runner self-test" \
                                   $PY scripts/run-incremental-query-benchmarks.py --self-test

# Generated-file freshness.
run "ast generated schema"         $PY scripts/codegen/gen_ast.py --check
run "canonical header schema"      $PY scripts/generate-canonical-header-syntax-schema.py --check
run "canonical header schema self-test" $PY scripts/generate-canonical-header-syntax-schema.py --self-test
run "package generated oracles"    $PY scripts/codegen/gen_package_oracles.py --check
run "package generated oracles self-test" $PY scripts/codegen/gen_package_oracles.py --self-test
run "vendored dependencies"        $PY tests/tools/check-vendored-dependencies.py --check
run "vendored dependencies self-test" $PY tests/tools/check-vendored-dependencies.py --self-test

# Commit policies with dedicated behavior already covered by CI jobs
# (check-english-only, check-diff-hygiene, check-internal-versioning,
# check-rfc, check-format) are intentionally not duplicated here.

if [ "$failures" -ne 0 ]; then
  echo "$failures repo lint check(s) failed" >&2
  exit 1
fi
echo "repo lint: all checks passed"
