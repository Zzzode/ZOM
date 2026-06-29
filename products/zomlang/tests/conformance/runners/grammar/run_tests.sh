#!/usr/bin/env bash
# run_tests.sh - ZOM grammar conformance test driver
#
# Location: products/zomlang/tests/conformance/runners/grammar/run_tests.sh
#
# Responsibilities:
#   - Incrementally build ANTLR grammars from docs/spec/.
#   - Generate the Java lexer/parser and compile the local ParseTool helper.
#   - Traverse every .zom fixture under conformance/corpus.
#   - Read the matching grammar expectation, run ParseTool, and compare
#     ACCEPT/REJECT verdicts.
#
# Why not grun/TestRig?
#   ANTLR 4.13 TestRig can throw ClassCastException for split lexer/parser
#   grammars. ParseTool.java directly invokes the requested parser rule.
#
# Usage:
#   bash products/zomlang/tests/conformance/runners/grammar/run_tests.sh
#   bash run_tests.sh 04-expressions
#   bash run_tests.sh _neg_
#   bash run_tests.sh reservedSyntax

set -u
set -o pipefail

if [ "${BASH_VERSINFO[0]}" -lt 4 ]; then
  echo "[FATAL] grammar conformance requires bash 4 or newer." >&2
  echo "        Current shell: bash ${BASH_VERSION}" >&2
  exit 1
fi

# ------------------------------------------------------------------
# Path discovery: script directory plus repository root.
# ------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
[ -z "$SCRIPT_DIR" ] && SCRIPT_DIR=.

# Find the repository root by walking up until docs/spec exists.
_repo_root="$SCRIPT_DIR"
while [ "$_repo_root" != "/" ] && [ ! -d "$_repo_root/docs/spec" ]; do
  _repo_root="$(dirname "$_repo_root")"
done
if [ ! -d "$_repo_root/docs/spec" ]; then
  echo "[FATAL] cannot locate docs/spec from $SCRIPT_DIR" >&2
  exit 1
fi

REPO_ROOT="$_repo_root"
SPEC_DIR="$REPO_ROOT/docs/spec"
CONFORMANCE_ROOT="$REPO_ROOT/products/zomlang/tests/conformance"
CORPUS_ROOT="$CONFORMANCE_ROOT/corpus"
EXPECTATION_ROOT="$CONFORMANCE_ROOT/expectations/grammar"
# Build directory: CTest passes a build-tree path; direct shell use defaults to
# the repository build area instead of writing generated files into the corpus.
BUILD_DIR="${ZOM_CONFORMANCE_BUILD_DIR:-$REPO_ROOT/build/conformance-grammar}"

if [ -z "${ANTLRJAR:-}" ]; then
  for candidate in \
    /opt/homebrew/opt/antlr/antlr-*-complete.jar \
    /usr/local/opt/antlr/antlr-*-complete.jar \
    /opt/homebrew/Cellar/antlr/*/antlr-*-complete.jar \
    /usr/local/Cellar/antlr/*/antlr-*-complete.jar; do
    if [ -f "$candidate" ]; then
      ANTLRJAR="$candidate"
      break
    fi
  done
fi
ANTLRJAR="${ANTLRJAR:-}"

if [ -z "${JAVA_BIN:-}" ]; then
  for candidate in \
    /usr/local/opt/openjdk/bin/java \
    /opt/homebrew/opt/openjdk/bin/java \
    /usr/local/opt/java/bin/java \
    /opt/homebrew/opt/java/bin/java \
    "$(command -v java 2>/dev/null || true)" \
    /usr/bin/java; do
    if [ -n "$candidate" ] && [ -x "$candidate" ]; then
      JAVA_BIN="$candidate"
      break
    fi
  done
fi
JAVA_BIN="${JAVA_BIN:-java}"

if [ -z "${JAVAC_BIN:-}" ]; then
  for candidate in \
    /usr/local/opt/openjdk/bin/javac \
    /opt/homebrew/opt/openjdk/bin/javac \
    /usr/local/opt/java/bin/javac \
    /opt/homebrew/opt/java/bin/javac \
    "$(command -v javac 2>/dev/null || true)" \
    /usr/bin/javac; do
    if [ -n "$candidate" ] && [ -x "$candidate" ]; then
      JAVAC_BIN="$candidate"
      break
    fi
  done
fi
JAVAC_BIN="${JAVAC_BIN:-javac}"

PATTERN="${1:-}"

if [ -z "$ANTLRJAR" ] || [ ! -f "$ANTLRJAR" ]; then
  echo "[FATAL] ANTLR jar not found." >&2
  echo "        Set ANTLRJAR to an antlr-*-complete.jar path." >&2
  exit 1
fi

if ! "$JAVA_BIN" -version >/tmp/zom_java_err 2>&1; then
  echo "[FATAL] Java runtime is not available: $JAVA_BIN" >&2
  cat /tmp/zom_java_err >&2
  exit 1
fi

if ! "$JAVAC_BIN" -version >/tmp/zom_javac_err 2>&1; then
  echo "[FATAL] Java compiler is not available: $JAVAC_BIN" >&2
  cat /tmp/zom_javac_err >&2
  exit 1
fi

# ==================================================================
# Incremental ANTLR generation and javac compilation.
# ==================================================================
need_rebuild=0
if [ ! -d "$BUILD_DIR" ] || [ ! -f "$BUILD_DIR/ZomParser.class" ]; then
  need_rebuild=1
else
  for f in "$SPEC_DIR/ZomLexer.g4" "$SPEC_DIR/ZomParser.g4"; do
    if [ "$f" -nt "$BUILD_DIR/ZomParser.java" ]; then
      need_rebuild=1; break
    fi
  done
fi
if [ "$need_rebuild" = "1" ]; then
  echo "(ANTLR) Rebuilding parser/lexer..."
  rm -rf "$BUILD_DIR" && mkdir -p "$BUILD_DIR"
  "$JAVA_BIN" -jar "$ANTLRJAR" -Werror -o "$BUILD_DIR" \
    "$SPEC_DIR/ZomLexer.g4" "$SPEC_DIR/ZomParser.g4" \
    >/dev/null 2>/tmp/zom_antlr_err
  rc=$?
  if [ $rc != 0 ] || [ ! -f "$BUILD_DIR/ZomParser.java" ]; then
    echo "[FATAL] antlr4 generation failed (exit $rc)" >&2
    cat /tmp/zom_antlr_err >&2
    exit 1
  fi
  "$JAVAC_BIN" -cp "$ANTLRJAR" -d "$BUILD_DIR" "$BUILD_DIR"/*.java \
    >/dev/null 2>/tmp/zom_javac_err
  rc=$?
  if [ $rc != 0 ]; then
    echo "[FATAL] javac compilation failed (exit $rc)" >&2
    cat /tmp/zom_javac_err >&2
    exit 1
  fi
  nclass=$(/bin/ls -1 "$BUILD_DIR"/*.class 2>/dev/null | wc -l | tr -d ' ')
  echo "(ANTLR) OK: $nclass classes"
fi

# ==================================================================
# ParseTool helper source. The helper is generated and compiled on demand.
# ==================================================================
if [ ! -f "$BUILD_DIR/ParseTool.class" ] || [ /tmp/ParseTool.java -nt "$BUILD_DIR/ParseTool.class" ]; then
  if [ -f /tmp/ParseTool.java ]; then
    cp /tmp/ParseTool.java "$BUILD_DIR/ParseTool.java"
  else
    cat > "$BUILD_DIR/ParseTool.java" <<'JEOF'
import org.antlr.v4.runtime.*;
import org.antlr.v4.runtime.misc.ParseCancellationException;
import org.antlr.v4.runtime.tree.ParseTree;
import java.lang.reflect.Method;
public class ParseTool {
  public static void main(String[] args) throws Exception {
    String startRule = args.length > 0 ? args[0] : "sourceFile";
    StringBuilder sb = new StringBuilder();
    byte[] buf = new byte[16384]; int n;
    while ((n = System.in.read(buf)) > 0) sb.append(new String(buf, 0, n));
    CharStream in = CharStreams.fromString(sb.toString());
    ZomLexer lex = new ZomLexer(in);
    CommonTokenStream ts = new CommonTokenStream(lex);
    ZomParser p = new ZomParser(ts);
    p.removeErrorListeners();
    lex.removeErrorListeners();
    final StringBuilder errBuf = new StringBuilder();
    final int[] errs = {0};
    ANTLRErrorListener lst = new BaseErrorListener() {
      public void syntaxError(Recognizer<?,?> r, Object sym, int l, int c, String msg, RecognitionException e) {
        errs[0]++;
        errBuf.append("(syntax) line ").append(l).append(':').append(c).append(' ').append(msg).append('\n');
      }
    };
    p.addErrorListener(lst);
    lex.addErrorListener(lst);
    try {
      Method m = ZomParser.class.getMethod(startRule);
      Object tree = m.invoke(p);
      boolean hadErrors = (p.getNumberOfSyntaxErrors() > 0 || errs[0] > 0);
      if (hadErrors) {
        System.err.print(errBuf);
        System.exit(1);
      }
      if (args.length > 1 && "tree".equals(args[1]))
        System.out.println(((ParseTree)tree).toStringTree(p));
      System.exit(0);
    } catch (ReflectiveOperationException roe) {
      Throwable cause = roe.getCause() != null ? roe.getCause() : roe;
      String msg = cause.getClass().getSimpleName() + ": " + cause.getMessage();
      if (errs[0] > 0) System.err.print(errBuf);
      System.err.println(msg);
      // exit 2 = semantic predicate fail (PCE); exit 1 = any other uncaught
      if (cause instanceof ParseCancellationException) System.exit(2);
      System.exit(1);
    }
  }
}
JEOF
  fi
  "$JAVAC_BIN" -cp "$ANTLRJAR:$BUILD_DIR" -d "$BUILD_DIR" "$BUILD_DIR/ParseTool.java" \
    >/dev/null 2>/tmp/zom_parsetool_javac
  rc=$?
  if [ $rc != 0 ]; then
    echo "[FATAL] ParseTool.java compilation failed (exit $rc):" >&2
    cat /tmp/zom_parsetool_javac >&2
    exit 1
  fi
  echo "(ParseTool) built."
fi
CP="$ANTLRJAR:$BUILD_DIR"

yaml_value() {
  local key="$1"
  local file="$2"
  sed -nE "s|^${key}:[[:space:]]*\"(.*)\"[[:space:]]*$|\\1|p" "$file" \
    | head -n 1 \
    | tr -d '\r'
}

expected_verdict() {
  local value="$1"
  case "$value" in
    ACCEPT*) echo "ACCEPT" ;;
    REJECT*) echo "REJECT" ;;
    *) echo "$value" ;;
  esac
}

# ==================================================================
# Traverse fixtures. Process substitution keeps associative arrays in this shell.
# ==================================================================
total=0 pass=0 fail=0
declare -A dir_pass dir_fail dir_total
fail_files=()

while IFS= read -r -d '' expectation; do
  rel="${expectation#$EXPECTATION_ROOT/}"
  rel="${rel%.yml}.zom"
  f="$CORPUS_ROOT/$rel"
  if [ ! -f "$f" ]; then
    echo "[FATAL] missing corpus source for grammar expectation $rel" >&2
    exit 2
  fi

  # Optional path or rule-name filter.
  if [ -n "$PATTERN" ]; then
    expected_rule=""
    if printf '%s' "$f" | grep -qF "$PATTERN"; then
      :
    else
      expected_rule=$(yaml_value "covers_rule" "$expectation")
      if [ -z "$expected_rule" ] || ! printf '%s' "$expected_rule" | grep -qF "$PATTERN"; then
        continue
      fi
    fi
  fi
  total=$((total+1))
  rule=$(yaml_value "covers_rule" "$expectation")
  expect=$(expected_verdict "$(yaml_value "expected" "$expectation")")
  diag=$(yaml_value "expected_diagnostic" "$expectation")
  case "$diag" in
    none|None|NONE) diag="" ;;
  esac
  [ -z "$rule" ] && rule="(unspecified)"
  subdir="${rel%%/*}"
  dir_total[$subdir]=$(( ${dir_total[$subdir]:-0} + 1 ))

  "$JAVA_BIN" -Djava.awt.headless=true -cp "$CP" ParseTool sourceFile < "$f" \
    >/tmp/zom_$$_out 2>/tmp/zom_$$_err
  rc=$?
  stdout=$(cat /tmp/zom_$$_out 2>/dev/null)
  stderr=$(cat /tmp/zom_$$_err 2>/dev/null)
  combined="$stdout
$stderr"

  status=""
  case "$expect" in
    ACCEPT)
      if [ $rc = 0 ] && ! printf '%s' "$stderr" | grep -qE "error|Error|Exception|line [0-9]"; then
        status="PASS"
        pass=$((pass+1)); dir_pass[$subdir]=$(( ${dir_pass[$subdir]:-0} + 1 ))
      else
        status="FAIL (rc=$rc)"
        fail=$((fail+1)); dir_fail[$subdir]=$(( ${dir_fail[$subdir]:-0} + 1 ))
        fail_files+=("$rel  [rc=$rc, stderr: ${stderr//$'\n'/' | '}]")
      fi
      ;;
    REJECT)
      diag_hit=1
      if [ -n "$diag" ]; then
        diag_hit=0
        printf '%s' "$combined" | grep -qF "$diag" && diag_hit=1
      fi
      rejected=0
      if [ $rc != 0 ]; then rejected=1; fi
      if printf '%s' "$stderr" | grep -qE "error|Error|Exception|syntax error|line [0-9]"; then rejected=1; fi
      if [ $rejected = 1 ]; then
        if [ -z "$diag" ] || [ $diag_hit = 1 ]; then
          status="PASS (rejected, diag='${diag:-n/a}')"
          pass=$((pass+1)); dir_pass[$subdir]=$(( ${dir_pass[$subdir]:-0} + 1 ))
        else
          status="FAIL [diagnostic '${diag}' not matched]"
          fail=$((fail+1)); dir_fail[$subdir]=$(( ${dir_fail[$subdir]:-0} + 1 ))
          fail_files+=("$rel  [diagnostic '${diag}' missing; stderr='${stderr//$'\n'/' | '}]")
        fi
      else
        status="FAIL [parser ACCEPTED this REJECT test]"
        fail=$((fail+1)); dir_fail[$subdir]=$(( ${dir_fail[$subdir]:-0} + 1 ))
        fail_files+=("$rel  [parser wrongly accepted]")
      fi
      ;;
    *)
      status="FAIL [bad expected verdict (${expect:-empty})]"
      fail=$((fail+1))
      fail_files+=("$rel  [bad expectation metadata]")
      ;;
  esac

  printf '%-60s  %s\n' "$status" "[$rel  rule=${rule}]"
  rm -f /tmp/zom_$$_out /tmp/zom_$$_err
done < <(find "$EXPECTATION_ROOT" -type f -name '*.yml' -print0 | sort -z)

# ==================================================================
# Summary. Directory pass/fail counters are visible because the loop ran in
# this shell.
# ==================================================================
echo
echo "=============================================================="
echo "  Subtotals per directory"
echo "=============================================================="
all_dirs=()
while IFS= read -r d; do
  [ -z "$d" ] && continue
  all_dirs+=("$d")
done < <(cd "$EXPECTATION_ROOT" && /bin/ls -d */ 2>/dev/null | sed 's|/$||' | sort)

for d in "${all_dirs[@]}"; do
  t=${dir_total[$d]:-0}
  p=${dir_pass[$d]:-0}
  f=${dir_fail[$d]:-0}
  pct="0.0"
  if [ "$t" -gt 0 ]; then
    pct=$(awk -v a="$p" -v b="$t" 'BEGIN {printf "%.1f", a*100/b}')
  fi
  printf "  %-18s %3d/%3d passed   (fail %3d)   %5s%%\n" "$d" "$p" "$t" "$f" "$pct"
done

echo
echo "=============================================================="
echo "  TOTAL: $pass/$total passed, $fail failed"
echo "=============================================================="

# ==================================================================
# Rule coverage
# ==================================================================
covered=$(grep -rh "^covers_rule:" "$EXPECTATION_ROOT" 2>/dev/null \
  | sed -nE 's|^covers_rule:[[:space:]]*"(.*)"[[:space:]]*$|\1|p' \
  | sort -u | wc -l | tr -d ' ')

total_rules=$(awk '
  # Skip comment lines.
  /^[[:space:]]*\/\// { next }
  /^[a-zA-Z][a-zA-Z0-9_]*[[:space:]]*:/ {
    sub(":.*", "", $1)
    if ($1 ~ /^[a-z]/) print $1
    next
  }
  /^[a-zA-Z][a-zA-Z0-9_]*[[:space:]]*$/ {
    name=$1
    getline
    if ($0 ~ /^[[:space:]]*:/ && name ~ /^[a-z]/) print name
  }
' "$SPEC_DIR/ZomParser.g4" 2>/dev/null | grep -v "^$" | sort -u | wc -l | tr -d ' ')

pct="0.0"
if [ "$total_rules" -gt 0 ]; then
  pct=$(awk -v a="$covered" -v b="$total_rules" 'BEGIN {printf "%.1f", a*100/b}')
fi
echo
echo "Rule coverage: $covered/$total_rules parser rules have at least one test (${pct}%)"

if [ "$covered" -lt "$total_rules" ] 2>/dev/null; then
  grep -rh "^covers_rule:" "$EXPECTATION_ROOT" 2>/dev/null \
    | sed -nE 's|^covers_rule:[[:space:]]*"(.*)"[[:space:]]*$|\1|p' \
    | sort -u > /tmp/zom_cov.txt
  awk '
    /^[[:space:]]*\/\// { next }
    /^[a-zA-Z][a-zA-Z0-9_]*[[:space:]]*:/ {
      sub(":.*", "", $1)
      if ($1 ~ /^[a-z]/) print $1
      next
    }
    /^[a-zA-Z][a-zA-Z0-9_]*[[:space:]]*$/ {
      name=$1; getline
      if ($0 ~ /^[[:space:]]*:/ && name ~ /^[a-z]/) print name
    }
  ' "$SPEC_DIR/ZomParser.g4" 2>/dev/null | grep -v "^$" | sort -u > /tmp/zom_all.txt
  missing=$(/usr/bin/comm -23 /tmp/zom_all.txt /tmp/zom_cov.txt 2>/dev/null | wc -l | tr -d ' ')
  if [ "$missing" -gt 0 ] 2>/dev/null; then
    echo "--- Uncovered parser rules ($missing) ---"
    /usr/bin/comm -23 /tmp/zom_all.txt /tmp/zom_cov.txt 2>/dev/null | awk '{printf "  [ ] %s\n", $0}'
  fi
fi

# ==================================================================
# Failure list and exit status.
# ==================================================================
if [ $fail -gt 0 ]; then
  echo
  echo "=============================================================="
  echo "  FAILURES ($fail):"
  echo "=============================================================="
  for ff in "${fail_files[@]}"; do echo "    x $ff"; done
  exit 1
fi
echo "ALL PASSED."
exit 0
