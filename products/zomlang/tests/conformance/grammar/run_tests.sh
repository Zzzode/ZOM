#!/usr/bin/env bash
# run_tests.sh — ZOM Grammar Conformance 测试套件驱动
#
# 位置: products/zomlang/tests/conformance/grammar/run_tests.sh
#
# 做什么:
#   - 增量构建 ANTLR .g4 (位于 docs/spec/)
#   - 增量生成 Java parser + 自写 ParseTool
#   - 遍历 13 子目录 * 511 个 .zom fixture
#   - 对每个 fixture: 解析 4 行 header → 调 ParseTool → 判定 ACCEPT/REJECT → 汇总
#
# 为什么不用 grun/TestRig?
#   ANTLR 4.13 分离式 lexer+parser grammar 的 TestRig 有 ClassCastException
#   (Parser.asSubclass) 问题，用 ParseTool.java + 反射直调 parser。
#
# 用法:
#   bash products/zomlang/tests/conformance/grammar/run_tests.sh             # 全量
#   bash run_tests.sh 04-expressions                                         # 路径过滤
#   bash run_tests.sh _neg_                                                  # 仅负例
#   bash run_tests.sh reservedSyntax                                         # 规则名过滤

set -u
set -o pipefail

# ------------------------------------------------------------------
# 路径定位（脚本所在目录 + repo root）
# ------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
[ -z "$SCRIPT_DIR" ] && SCRIPT_DIR=.

# 找 repo root（向上爬直到看到 docs/spec 目录）
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
# 测试根目录：脚本所在目录（13 子目录 *.zom）
TEST_ROOT="$SCRIPT_DIR"
# Build 目录：放在 SCRIPT_DIR 下
BUILD_DIR="$SCRIPT_DIR/.antlr_build"

ANTLRJAR="${ANTLRJAR:-/opt/homebrew/Cellar/antlr/4.13.2/antlr-4.13.2-complete.jar}"
JAVA_BIN="${JAVA_BIN:-/usr/bin/java}"
JAVAC_BIN="${JAVAC_BIN:-/usr/bin/javac}"

PATTERN="${1:-}"

# ==================================================================
# 增量 ANTLR 生成 + javac
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
    echo "[FATAL] antlr4 生成失败 (exit $rc)" >&2
    cat /tmp/zom_antlr_err >&2
    exit 1
  fi
  "$JAVAC_BIN" -cp "$ANTLRJAR" -d "$BUILD_DIR" "$BUILD_DIR"/*.java \
    >/dev/null 2>/tmp/zom_javac_err
  rc=$?
  if [ $rc != 0 ]; then
    echo "[FATAL] javac 编译失败 (exit $rc)" >&2
    cat /tmp/zom_javac_err >&2
    exit 1
  fi
  nclass=$(/bin/ls -1 "$BUILD_DIR"/*.class 2>/dev/null | wc -l | tr -d ' ')
  echo "(ANTLR) OK: $nclass classes"
fi

# ==================================================================
# ParseTool 辅助类（内置源码，首次自动编译）
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
    echo "[FATAL] ParseTool.java 编译失败 (exit $rc):" >&2
    cat /tmp/zom_parsetool_javac >&2
    exit 1
  fi
  echo "(ParseTool) built."
fi
CP="$ANTLRJAR:$BUILD_DIR"

# ==================================================================
# 遍历测试文件（用 process substitution 避免 while 子 shell 丢失关联数组）
# ==================================================================
total=0 pass=0 fail=0
declare -A dir_pass dir_fail dir_total
fail_files=()

while IFS= read -r -d '' f; do
  # 过滤器
  if [ -n "$PATTERN" ]; then
    header_rule=""
    if printf '%s' "$f" | grep -qF "$PATTERN"; then
      :
    else
      header_rule=$(sed -n '2p' "$f" | awk -F': ' '{print $2}' | tr -d '\r')
      if [ -z "$header_rule" ] || ! printf '%s' "$header_rule" | grep -qF "$PATTERN"; then
        continue
      fi
    fi
  fi
  total=$((total+1))
  rel="${f#$TEST_ROOT/}"
  rule=$(sed -n '2p'    "$f" | awk -F': ' '{print $2}' | tr -d '\r')
  expect=$(sed -n '3p'  "$f" | awk '{print $NF}' | tr -d '\r')
  diag=$(sed -n '4p'    "$f" | sed -E 's|^// ExpectedDiagnostic:||;s|^ *||' | tr -d '\r')
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
      status="FAIL [bad Expected header (${expect:-empty})]"
      fail=$((fail+1))
      fail_files+=("$rel  [bad header]")
      ;;
  esac

  printf '%-60s  %s\n' "$status" "[$rel  rule=${rule}]"
  rm -f /tmp/zom_$$_out /tmp/zom_$$_err
done < <(find "$TEST_ROOT" -type f -name '*.zom' -print0 | sort -z)

# ==================================================================
# 汇总（子目录 pass/fail 现在可见，因为 while 在主 shell 执行）
# ==================================================================
echo
echo "=============================================================="
echo "  Subtotals per directory"
echo "=============================================================="
all_dirs=()
while IFS= read -r d; do
  [ -z "$d" ] && continue
  all_dirs+=("$d")
done < <(cd "$TEST_ROOT" && /bin/ls -d */ 2>/dev/null | sed 's|/$||' | sort)

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
covered=$(grep -rh "^// Covers rule:" "$TEST_ROOT" 2>/dev/null \
  | awk -F': ' '{print $2}' | sort -u | wc -l | tr -d ' ')

total_rules=$(awk '
  # 跳过注释行
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
  grep -rh "^// Covers rule:" "$TEST_ROOT" 2>/dev/null | awk -F': ' '{print $2}' | sort -u > /tmp/zom_cov.txt
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
# 失败列表 & exit
# ==================================================================
if [ $fail -gt 0 ]; then
  echo
  echo "=============================================================="
  echo "  FAILURES ($fail):"
  echo "=============================================================="
  for ff in "${fail_files[@]}"; do echo "    ✗ $ff"; done
  exit 1
fi
echo "ALL PASSED."
exit 0
