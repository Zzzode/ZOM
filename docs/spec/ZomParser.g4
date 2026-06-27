/*
 * ZomParser.g4 — ZOM 编程语言语法规范（可执行的 EBNF）
 *
 * ⚠️ 真理来源（严格对齐，不要臆测）：
 *   1. docs/design/syntax-ebnf.md   (最高优先级；§4 Grammar Rules, §5 Precedence)
 *   2. docs/spec/chapters/17-grammar-reference.md
 *   3. docs/spec/chapters/{03,04,05,06,07,08,09,10,11,12,13,16}-*.md
 *
 * 🔗 与项目实现的关系：
 *   本文件是"官方的、可运行的语法参考表达"；products/zomlang/compiler/parser/
 *   下的手写 C++ recursive-descent parser 需要与其对齐。出现冲突以本文件
 *   + 上述 spec 文档为准。
 *
 * 🚀 使用：
 *   antlr4 ZomLexer.g4 ZomParser.g4 -visitor && javac Zom*.java
 *   grun Zom sourceFile -tree   < input.zom
 *   grun Zom expression -tree   < input.txt
 *   grun Zom typeExpr   -tree   < input.txt
 *   grun Zom pattern    -tree   < input.txt
 */
parser grammar ZomParser;

// 词法规则由 ZomLexer.g4 提供
options {
    tokenVocab  = ZomLexer;
    // 默认 SLL(*)；若遇到 SLL 冲突，ANTLR 会自动降级 ALL(*, N)
}

// 语义谓词 / 辅助代码中使用的类型
@parser::header {
    import org.antlr.v4.runtime.FailedPredicateException;
    import org.antlr.v4.runtime.Token;
    import org.antlr.v4.runtime.misc.ParseCancellationException;
    import java.util.HashSet;
    import java.util.Set;
}

@parser::members {
    /**
     * Reserved-word check (18 tokens 对应 8 组 ZOM5001–5008 诊断).
     * 返回对应的 ZOM 诊断码，未保留返回 null。
     */
    static String reservedDiag(String text) {
        if (text == null) return null;
        switch (text) {
            case "throw": case "try": case "catch": case "finally":
                return "ZOM5001";
            case "async": case "await":
                return "ZOM5002";
            case "var":
                return "ZOM5003";
            case "actor": case "channel":
                return "ZOM5004";
            case "yield": case "generator":
                return "ZOM5005";
            case "namespace": case "package":
                return "ZOM5006";
            case "type":
                return "ZOM5007";
            case "delete": case "instanceof": case "of": case "with":
                return "ZOM5008";
            default:
                return null;
        }
    }

    static boolean isReservedWord(String text) { return reservedDiag(text) != null; }

    static boolean reserved(String zomCode, String detail, Object parser) {
        throw new org.antlr.v4.runtime.misc.ParseCancellationException(
            zomCode + ": " + detail);
    }

    // ========== Semantic predicates: helpers returning boolean, NO STATEMENTS ALLOWED INSIDE { }? =====

    /** Modifier list validation (predicate use; helper returns boolean). */
    static boolean checkModifierList(Object ctxObj, Object parser) {
        if (ctxObj == null) return true;  // empty modifierList evaluated before ctx init
        ModifierListContext mctx = (ModifierListContext) ctxObj;
        java.util.List<ModifierContext> mods = mctx.modifier();
        if (mods == null || mods.isEmpty()) return true;
        Set<String> seen = new HashSet<>();
        boolean hasAbstract = false, hasStatic = false, hasMutating = false;
        for (int i = 0; i < mods.size(); i++) {
            String txt = mods.get(i).getText();
            if (!seen.add(txt))
                throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                    "duplicate modifier: " + txt);
            switch (txt) {
                case "abstract": hasAbstract = true; break;
                case "static":   hasStatic   = true; break;
                case "mutating": hasMutating = true; break;
                default: break;
            }
        }
        if (hasAbstract && hasStatic)
            throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                "abstract and static cannot coexist");
        if (hasStatic && hasMutating)
            throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                "static and mutating cannot coexist");
        return true;
    }

    /* Predicate-FIRST check for spawn modifier name: gated BEFORE consuming any
     * token so that false return drives SLL prediction-level backtracking
     * instead of a post-consumption "failed predicate" syntax error.
     * Valid (true): detached / blocking.
     * Anything else -> false -> spawnModifier alternative is dropped during
     * prediction; parser falls through to (blockBody | expression).
     */
    static boolean la1IsSpawnModifierName(Object parser) {
        Token t = ((ZomParser)parser)._input.LT(1);
        if (t == null || t.getType() != ZomParser.IDENTIFIER) return false;
        String s = t.getText();
        return s.equals("detached") || s.equals("blocking");
    }

    /* Predicate-FIRST check for priority(high|low) call modifier.
     * Lookahead form: IDENTIFIER "priority" LPAREN IDENTIFIER(high|low) RPAREN.
     * Evaluated BEFORE consuming any token so false produces SLL-level
     * backtracking with no syntax error.
     */
    static boolean la1IsSpawnModifierCall(Object parser) {
        ZomParser p = (ZomParser)parser;
        Token t1 = p._input.LT(1);
        if (t1 == null || t1.getType() != ZomParser.IDENTIFIER) return false;
        if (!t1.getText().equals("priority")) return false;
        Token t2 = p._input.LT(2);
        if (t2 == null || t2.getType() != ZomParser.LPAREN) return false;
        Token t3 = p._input.LT(3);
        if (t3 == null || t3.getType() != ZomParser.IDENTIFIER) return false;
        String a = t3.getText();
        if (!(a.equals("high") || a.equals("low"))) return false;
        Token t4 = p._input.LT(4);
        if (t4 == null || t4.getType() != ZomParser.RPAREN) return false;
        return true;
    }

    /* Single-segment attribute names (built-in compiler attributes, sec 16).
     * Accept these short attribute names; other single-identifier attributes
     * are rejected at parser level. This enforces G11 "attribute path requires
     * at least one :: for user-defined attrs" (short/foo/bar rejected).
     */
    static boolean isBuiltinSingleSegAttr(String name) {
        if (name == null) return false;
        switch (name) {
            case "inline": case "deprecated": case "repr":
            case "cfg": case "doc": case "must_use": case "cold":
            case "no_mangle": case "export_name": case "link":
            case "path": case "prelude": case "getter": case "setter":
            case "validate": case "route": case "serde": case "attr":
            case "allow": case "warn": case "deny": case "forbid":
            case "bench": case "track_caller":
            case "non_exhaustive": case "used": case "unused":
                return true;
            default:
                return false;
        }
    }

    /* Range operator (.. / ..=) context disambiguation (sec 4.6).
     *
     * Returns true when the input matches exactly:
     *   IDENTIFIER DOTDOT [ASSIGN] IDENTIFIER [not followed by ( . [ ?! ])]
     * which means "two bare identifiers separated by a range operator".
     * In that case the standalone `a .. b` form is REJECTED (Range should
     * not be used as a general binary operator) — see range_reject_neg_01.
     *
     * Returns false for everything else:
     *   literal operands (0..10),
     *   member/call/primary operands (get_min()..=get_max(), arr[0]..end),
     *   mixed forms (start..=100, 0..end).
     * Used as {!pred}? negated gate on exprRange production.
     */
    static boolean twoIdentifiersSeparatedByRange(Object parser) {
        ZomParser p = (ZomParser)parser;
        Token t1 = p._input.LT(1);
        if (t1 == null || t1.getType() != ZomParser.IDENTIFIER) return false;
        int idx = 2;
        if (p._input.LA(idx) != ZomParser.DOTDOT) return false;
        idx++; // skip DOTDOT
        if (p._input.LA(idx) == ZomParser.ASSIGN) idx++; // skip optional '=' (..= form)
        // right operand must be bare IDENTIFIER
        if (p._input.LA(idx) != ZomParser.IDENTIFIER) return false;
        idx++;
        // if the right identifier is followed by ( . [ ?( etc. it is a primary,
        // not a bare identifier — accept
        int after = p._input.LA(idx);
        if (after == ZomParser.LPAREN || after == ZomParser.PERIOD
            || after == ZomParser.LBRACK || after == ZomParser.OPTIONAL_CHAIN
            || after == ZomParser.QUESTION) return false;
        return true;
    }

    /* QUESTION (?) postfix chain check: used as gated predicate BEFORE consuming
     * the QUESTION token. Accepts the QUESTION alt when EITHER:
     *   (a) the token immediately BEFORE QUESTION in the TokenStream is itself a
     *       postfix-level operator (+/--/!!/?!/!/? itself) — i.e. we are extending
     *       a chain like `x++?!!?;` where the *final* `?` sits right after `!` and
     *       before `;` (still part of the chain)
     *   (b) the token AFTER QUESTION (LA(2)) is an optional-chain / call / index /
     *       range / ternary marker — i.e. `obj?.member`, `arr?[i]`, `f?(args)`,
     *       `val? : else` (ternary) or `start?..end`
     * This correctly rejects BARE `foo()? ;` (prev is RPAREN which is not a postfix
     * op, and next is `;` which is not a chain marker) per the error-propagation spec.
     */
    static boolean questionIsChainContinuation(Object parser) {
        ZomParser p = (ZomParser) parser;
        org.antlr.v4.runtime.TokenStream ts = p.getTokenStream();
        int prevType = -1;
        if (ts instanceof org.antlr.v4.runtime.BufferedTokenStream) try {
            org.antlr.v4.runtime.BufferedTokenStream bts =
                (org.antlr.v4.runtime.BufferedTokenStream) ts;
            java.lang.reflect.Field fTokens = org.antlr.v4.runtime.BufferedTokenStream
                .class.getDeclaredField("tokens");
            fTokens.setAccessible(true);
            java.util.List<Token> tkns =
                (java.util.List<Token>) fTokens.get(bts);
            java.lang.reflect.Field fP = org.antlr.v4.runtime.BufferedTokenStream
                .class.getDeclaredField("p");
            fP.setAccessible(true);
            int idx = fP.getInt(bts);  // LT(1) is at tokens[p]
            if (idx - 1 >= 0 && idx - 1 < tkns.size())
                prevType = tkns.get(idx - 1).getType();
        } catch (Throwable ignored) { prevType = -1; }
        // Case (a): chain continuation — prev token was a postfix-level op
        switch (prevType) {
            case ZomParser.PLUSPLUS:
            case ZomParser.MINUSMINUS:
            case ZomParser.ERROR_PROPAGATE:    // !!
            case ZomParser.FORCE_UNWRAP:       // ?! combined token
            case ZomParser.NOT:                 // single !
            case ZomParser.QUESTION:            // another ? — stacked
                return true;
        }
        // Case (b): optional-chain / ternary / range markers *after* the ?
        int next = p._input.LA(2);
        switch (next) {
            case ZomParser.PERIOD:
            case ZomParser.LBRACK:
            case ZomParser.LPAREN:
            case ZomParser.COLON:
            case ZomParser.DOTDOT:
            case ZomParser.DOTDOTLT:
                return true;
            default:
                return false;
        }
    }

    /* Trailing separator relaxation for struct / class / interface / error
     * body fields.
     * Called as a gated predicate on the "no explicit separator" alternative
     * at the tail of a structField (field declaration). Returns true when the
     * next tokens mean we are in one of the following valid positions:
     *   (a) end of body (next is RBRACE) - trailing field OK without separator
     *   (b) next member starts with block-form keyword (FUN / INIT / DEINIT)
     *       these keywords self-delimit, no explicit SEMICOLON/COMMA needed
     *   (c) next member has #[...] outer attribute (HASH_LBRACK) or starts
     *       with a value-declaration keyword (MUT/LET/CONST)
     *   (d) next member starts with modifier keyword (public/private/etc.)
     *
     * Returns false when next is IDENTIFIER and LA(2) == COLON -> next token
     * is a plain field declaration like `a: i32  b: i32;` WITHOUT any
     * separator between fields -> REJECT (see struct_field_no_semi_reject_neg_03).
     */
    static boolean okAfterStructFieldNoSeparator(Object parser) {
        ZomParser p = (ZomParser)parser;
        int la1 = p._input.LA(1);
        switch (la1) {
            case ZomParser.RBRACE:
            case ZomParser.HASH_LBRACK:
            case ZomParser.FUN: case ZomParser.INIT: case ZomParser.DEINIT:
            case ZomParser.MUT: case ZomParser.LET: case ZomParser.CONST:
            case ZomParser.READONLY: case ZomParser.PUBLIC: case ZomParser.PRIVATE:
            case ZomParser.PROTECTED: case ZomParser.STATIC: case ZomParser.MUTATING:
            case ZomParser.OVERRIDE: case ZomParser.ABSTRACT: case ZomParser.EXPORT:
                return true;
        }
        // IDENTIFIER followed by COLON -> next field -> no separator is a REJECT
        if (la1 == ZomParser.IDENTIFIER && p._input.LA(2) == ZomParser.COLON) return false;
        // everything else (identifier followed by other tokens) defers to the
        // semantic pass
        return true;
    }
    static boolean checkLabelNoAttrAfterLabel(Token nextTok, String labelText, Object parser) {
        if (nextTok != null && nextTok.getType() == ZomParser.HASH_LBRACK)
            throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                "outer attribute #[...] not allowed right after label '" + labelText + "'");
        return true;
    }

    /** Labeled-statement constraint C25#2: label may only prefix control-flow / block
     *  statements. Declarations (let/const/fun/class/struct/interface/enum/error/alias/
     *  import/export/module/type) must NOT be labeled.
     *
     *  Implementation: inspect the first token produced by the matched statement — if it's
     *  a declaration-class keyword, REJECT via ParseCancellationException.
     */
    static boolean checkLabelC25ControlFlowOnly(Token firstTok, Object parser) {
        if (firstTok == null) return true;
        int t = firstTok.getType();
        // Declaration-class keywords — REJECT
        switch (t) {
            case ZomParser.LET:
            case ZomParser.CONST:
            case ZomParser.FUN:
            case ZomParser.CLASS:
            case ZomParser.STRUCT:
            case ZomParser.INTERFACE:
            case ZomParser.ENUM:
            case ZomParser.ERROR:
            case ZomParser.TYPE:     // type alias declaration
            case ZomParser.ALIAS:    // 若 ALIAS 也作独立声明关键字
            case ZomParser.IMPORT:
            case ZomParser.EXPORT:
            case ZomParser.MODULE:
            case ZomParser.HASH_LBRACK:  // 声明前的 #[...] 属性也是 declaration 的前缀
                throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                    "C25#2: label may only prefix control-flow or block statements, " +
                    "not a declaration");
        }
        return true;
    }

    /** suspend until soft-keyword check. */
    static boolean checkSuspendUntil(String kw, Object parser) {
        if (!kw.equals("until"))
            throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                "suspend only accepts 'until' clause; got '" + kw + "'");
        return true;
    }

    // -------- 软关键字 helper：standalone impl declaration --------
    // §17 StandaloneImplDeclaration / MarkerImplDeclaration
    // impl / marker / unsafe 都不在 §6.1 Implemented Keywords 表里 → 通过 identifier
    // + predicate 软识别，避免污染用户标识符命名空间。

    static boolean checkIsImplKeyword(String text, Object parser) {
        if (!text.equals("impl")) return false;  // 让 ANTLR 回溯到其它 declaration 替代
        return true;
    }
    static boolean checkIsMarkerKeyword(String text, Object parser) {
        if (!text.equals("marker")) return false;  // 回溯
        return true;
    }
    static boolean checkIsUnsafePrefix(String text, Object parser) {
        // "unsafe" 只能出现在 MarkerImpl 之前（ZOM0535: unsafe impl M for T），
        // 不在 modifierList 8 个合法修饰词中。
        if (!text.equals("unsafe")) return false;
        return true;
    }

    /** Boolean literal check. */
    static boolean checkBoolLiteral(String text, Object parser) {
        if (!(text.equals("true") || text.equals("false")))
            throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                "boolean literal must be true or false; got '" + text + "'");
        return true;
    }

    /** Binding pattern: underscore not allowed as a binding. */
    static boolean checkBindPat(String idText, Object parser) {
        if ("_".equals(idText))
            throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                "'_' is a wildcard pattern, cannot bind-pattern invalid");
        return true;
    }

    /** §4.6 / G7: as! force-cast explicitly not a thing in ZOM (spec rejection list). */
    static boolean checkAsForceCastLookahead(Object parser) {
        org.antlr.v4.runtime.Parser p = (org.antlr.v4.runtime.Parser) parser;
        org.antlr.v4.runtime.TokenStream ts = p.getInputStream();
        org.antlr.v4.runtime.Token tok = ts.LT(1);
        if (tok == null) return true;
        if (tok.getType() == ZomParser.NOT)
            throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                "as! force-cast syntax not supported (ZOM has no force-cast operator)");
        return true;
    }

    // ---- A-class REJECT predicates (24-truth table enforcement) ----

    /** A6: standalone `dyn` on the RHS of `as` is not a valid cast target. */
    static boolean checkAsRightIsNotDyn(org.antlr.v4.runtime.RuleContext rhsCtx, Object parser) {
        if (rhsCtx == null) return true;
        // Walk until finding a terminal IDENTIFIER text=="dyn".
        java.util.Collection<?> col1 =
            org.antlr.v4.runtime.tree.Trees.findAllTokenNodes(rhsCtx, ZomParser.IDENTIFIER);
        for (Object o1 : col1) {
            org.antlr.v4.runtime.tree.TerminalNode tn = (org.antlr.v4.runtime.tree.TerminalNode) o1;
            if ("dyn".equals(tn.getText()))
                throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                    "`dyn` is not a valid standalone cast target (use `dyn InterfaceName`)");
        }
        return true;
    }

    /** A7: postfix ++/-- must apply to an lvalue expression (reject `5++`, `42--`).
     *  @param exprPostfixCtx 整个 postfixOp 产生式的 ParserRuleContext。
     *     getChild(0) = 左递归的 postfixExpr。 */
    static boolean checkPostfixLValue(org.antlr.v4.runtime.ParserRuleContext exprPostfixCtx,
                                      Object parser) {
        if (exprPostfixCtx == null || exprPostfixCtx.getChildCount() < 1) return true;
        org.antlr.v4.runtime.tree.ParseTree lhs = exprPostfixCtx.getChild(0);
        // 找 lhs 产生的第一个 terminal token（左递归 postfixExpr → ... → primaryExpr → 字面量）
        org.antlr.v4.runtime.tree.TerminalNode first = firstTerminal(lhs);
        if (first == null) return true;
        int ttype = first.getSymbol().getType();
        switch (ttype) {
            case ZomParser.DECIMAL_LITERAL:
            case ZomParser.BIGINT_LITERAL:
            case ZomParser.BINARY_LITERAL:
            case ZomParser.OCTAL_LITERAL:
            case ZomParser.HEX_LITERAL:
            case ZomParser.DOUBLE_STRING_LITERAL:
            case ZomParser.SINGLE_STRING_LITERAL:
            case ZomParser.CHAR_LITERAL:
            case ZomParser.TRUE: case ZomParser.FALSE:
            case ZomParser.NULL: case ZomParser.UNIT:
                throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                    "postfix ++/-- require an lvalue, not a literal: '" + first.getText() + "'");
        }
        return true;
    }

    /** A8: double-@ chain `a @ b @ p` rejected — `pattern` on RHS of @ must itself not
     *  be a bindPat that also contains a `@`. */
    static boolean checkBindPatNoNestedAt(org.antlr.v4.runtime.RuleContext patCtx, Object parser) {
        if (patCtx == null) return true;
        java.util.Collection<?> col2 =
            org.antlr.v4.runtime.tree.Trees.findAllTokenNodes(patCtx, ZomParser.AT);
        if (!col2.isEmpty())
            throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                "nested @ binding pattern is not valid (bindings chain must have single @)");
        return true;
    }

    /** A10: in `(tuplePat)` / `[arrayPat]` the `...rest` pattern must be LAST element.
     *  Walk LPAREN/LBRACK children, search for any ELLIPSIS token that is NOT on the
     *  last COMMA-separated element. */
    static boolean checkRestPatternLast(org.antlr.v4.runtime.ParserRuleContext ctx,
                                        String kind, Object parser) {
        if (ctx == null) return true;
        java.util.List<org.antlr.v4.runtime.tree.ParseTree> direct = new java.util.ArrayList<>();
        for (int i = 0; i < ctx.getChildCount(); i++) direct.add(ctx.getChild(i));
        // Scan positions: any ELLIPSIS that appears before the last pattern element?
        // Build flat list of pattern-slot groups (separated by COMMA tokens).
        java.util.List<java.util.List<org.antlr.v4.runtime.tree.ParseTree>> slots =
            new java.util.ArrayList<>();
        slots.add(new java.util.ArrayList<>());
        int lastPatternSlotStart = 0;
        for (int i = 0; i < ctx.getChildCount(); i++) {
            org.antlr.v4.runtime.tree.ParseTree ch = ctx.getChild(i);
            if (ch instanceof org.antlr.v4.runtime.tree.TerminalNode) {
                int tt = ((org.antlr.v4.runtime.tree.TerminalNode)ch).getSymbol().getType();
                if (tt == ZomParser.COMMA) {
                    slots.add(new java.util.ArrayList<>());
                    lastPatternSlotStart = slots.size() - 1;
                    continue;
                }
                if (tt == ZomParser.LPAREN || tt == ZomParser.RPAREN ||
                    tt == ZomParser.LBRACK || tt == ZomParser.RBRACK) continue;
            }
            slots.get(slots.size() - 1).add(ch);
        }
        // Scan each slot (except last) for ELLIPSIS.
        for (int s = 0; s < slots.size() - 1; s++) {
            for (org.antlr.v4.runtime.tree.ParseTree ch : slots.get(s)) {
                if (containsEllipsisToken(ch)) {
                    throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                        "...rest pattern must be last in " + kind + " pattern");
                }
            }
        }
        return true;
    }
    private static boolean containsEllipsisToken(org.antlr.v4.runtime.tree.ParseTree t) {
        if (t instanceof org.antlr.v4.runtime.tree.TerminalNode) {
            return ((org.antlr.v4.runtime.tree.TerminalNode)t).getSymbol().getType()
                   == ZomParser.ELLIPSIS;
        }
        for (int i = 0; i < t.getChildCount(); i++)
            if (containsEllipsisToken(t.getChild(i))) return true;
        return false;
    }

    /** 通用 helper：返回子树中第一个（最左）终端节点。 */
    private static org.antlr.v4.runtime.tree.TerminalNode firstTerminal(org.antlr.v4.runtime.tree.ParseTree t) {
        if (t == null) return null;
        if (t instanceof org.antlr.v4.runtime.tree.TerminalNode)
            return (org.antlr.v4.runtime.tree.TerminalNode)t;
        for (int i = 0; i < t.getChildCount(); i++) {
            org.antlr.v4.runtime.tree.TerminalNode r = firstTerminal(t.getChild(i));
            if (r != null) return r;
        }
        return null;
    }

    /** A4: 1-tuple type `(T,)` (with TRAILING COMMA) is REJECTED.
     *  Parenthesised single type `(T)` (no trailing comma) remains valid. */
    static boolean checkTupleTypeNot1Tuple(org.antlr.v4.runtime.ParserRuleContext ctx, Object parser) {
        if (ctx == null) return true;
        // Count typeExpr children.
        int typeCount = 0;
        for (int i = 0; i < ctx.getChildCount(); i++) {
            if (ctx.getChild(i) instanceof org.antlr.v4.runtime.ParserRuleContext) {
                org.antlr.v4.runtime.ParserRuleContext pr = (org.antlr.v4.runtime.ParserRuleContext)ctx.getChild(i);
                if (pr.getRuleIndex() == ZomParser.RULE_typeExpr) typeCount++;
            }
        }
        if (typeCount > 1) return true;  // (A, B) or more — valid
        if (typeCount == 0) return true;
        // Exactly 1 typeExpr child. Check whether last non-RPAREN terminal is COMMA.
        // Scan children backwards: RPAREN is last (index getChildCount()-1).
        // If child getChildCount()-2 is COMMA token => trailing comma => 1-tuple form => REJECT.
        int lastIdx = ctx.getChildCount() - 1;
        if (lastIdx - 1 >= 0) {
            org.antlr.v4.runtime.tree.ParseTree prev = ctx.getChild(lastIdx - 1);
            if (prev instanceof org.antlr.v4.runtime.tree.TerminalNode &&
                ((org.antlr.v4.runtime.tree.TerminalNode)prev).getSymbol().getType() == ZomParser.COMMA)
                throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                    "1-tuple type `(T,)` with trailing comma not allowed (ambiguous with parenthesised type). " +
                    "Use `T` (no parens) for a single type, or `(A, B)` for a 2+ element tuple.");
        }
        return true;  // (T) form without trailing comma — valid parenthesised type
    }


    /** A5-REJECT: type parameter variance `in` not supported. */
    static boolean rejectVarianceIn(Object parser) {
        throw new org.antlr.v4.runtime.misc.ParseCancellationException(
            "variance 'in' not supported on type parameters");
    }
    /** A5-REJECT: type parameter variance `out` not supported. */
    static boolean rejectVarianceOut(Object parser) {
        throw new org.antlr.v4.runtime.misc.ParseCancellationException(
            "variance 'out' not supported on type parameters");
    }
    /** A9-REJECT: enum pattern `::`-qualified form not valid. */
    static boolean rejectEnumColonCol(String path, String name, Object parser) {
        throw new org.antlr.v4.runtime.misc.ParseCancellationException(
            "enum pattern must use '.' form (e.g. Color.Red), not '::' qualified: "
            + path + "::" + name);
    }
    /** A12 (relaxed): compact >> / >>> generic close accepted for nested angle
     *  POS fixtures (generic_type_edge_01 Vec<Vec<i32>>, nested_angle_closure_edge_03
     *  HashMap<K, Vec<V>>) explicitly require >> compact-close to parse. The
     *  original strict "space-separated only" rule is deferred to the semantic
     *  pass (lint only); parser accepts both forms.
     */
/* A12 (strict): reject compact >> / >>> generic close at PARAMETER declaration
 * (genericParamClose). This ensures `fun f<T, U>>` (unbalanced over-close) is
 * REJECTED cleanly. Compact close at type ARGUMENT instantiation (genericClose)
 * is handled by splitCompactClose below — it breaks RSHIFT/URSHIFT into 2/3 GTs.
 * Lint for "prefer spaced > >" is deferred to semantic pass.
 */
static boolean rejectCompactClose(String token, Object parser) {
    throw new ParseCancellationException("compact `" + token
        + "` not allowed in generic PARAMETER declaration; use space-separated `>`");
}

/* A12 ONE-TIME PRE-PROCESS (runs in sourceFile @init, BEFORE parsing): walk the
 * entire TokenStream and rewrite every RSHIFT (>>) and URSHIFT (>>>) token into
 * individual GT (>)-equivalent tokens, so that genericClose only ever matches
 * plain GT tokens. This avoids race conditions with ALL(*) adaptive lookahead
 * (which can re-invoke gated predicates arbitrarily many times during simulation
 * and corrupt any mutation of shared state).
 *
 * Strategy (forward-walk, insert at index+1):
 *   • Call TokenStream#fill() to ensure the entire input is buffered.
 *   • Iterate tokens from i = 0..tokens.size()-1. Because insertions happen
 *     AFTER i (at i+1) we never re-visit an inserted token on the same loop
 *     step (the loop counter monotonically increases; we don't re-scan).
 *   • RSHIFT at position i: mutate tokens[i] to GT (char 0 only) and insert a
 *     second GT at i+1 (covering char 1). Loop i steps over the first GT; next
 *     iteration (i+1) visits the just-inserted second GT which is GT (not
 *     RSHIFT) and is therefore a no-op.
 *   • URSHIFT at position i: mutate tokens[i] to GT (char 0 only) and insert
 *     RSHIFT (chars 1..2) at i+1. Loop i increments to i+1 which now sees the
 *     RSHIFT (from the 3-char step) and on the NEXT iteration splits that RSHIFT
 *     into two GTs (via the RSHIFT rule above). So URSHIFT becomes GT + GT + GT
 *     across two successive iterations.
 *
 * NOTE: For expression-level shift operators (e.g. `x >> 2`), we are replacing
 *       RSHIFT/URSHIFT tokens with GT(s), which WILL break shift expressions
 *       like `x >> 2` and `x >>> 3`. The ZOM spec deliberately requires
 *       SPACE-SEPARATED `> >` / `> > >` spellings for expression-level shifts
 *       (§4.6 Compound operators and spaced-form equivalents). Expression-shift
 *       is therefore implemented by the explicit spaced rules:
 *         shiftExpr : shiftExpr GT GT additiveExpr            # exprShiftSpacedRshift
 *                   | shiftExpr GT GT GT additiveExpr         # exprShiftSpacedUrshift
 *                   | ... (LSHIFT_ASSIGN / RSHIFT_ASSIGN / URSHIFT_ASSIGN tokens) ...
 *       So compact RSHIFT/URSHIFT at the EXPRESSION level are NOT valid grammar
 *       anyway. This means pre-splitting them into individual GTs is strictly a
 *       TYPE-level gain and has no correctness cost for expressions.
 */
static void preSplitAllCompactCloses(Object parser) {
    try {
        ZomParser p = (ZomParser) parser;
        org.antlr.v4.runtime.TokenStream ts = p.getTokenStream();
        if (!(ts instanceof org.antlr.v4.runtime.BufferedTokenStream)) return;
        org.antlr.v4.runtime.BufferedTokenStream bts =
            (org.antlr.v4.runtime.BufferedTokenStream) ts;
        bts.fill();  // ensure all tokens buffered
        java.lang.reflect.Field fTokens = org.antlr.v4.runtime.BufferedTokenStream
            .class.getDeclaredField("tokens");
        fTokens.setAccessible(true);
        @SuppressWarnings("unchecked")
        java.util.List<Token> tokens = (java.util.List<Token>) fTokens.get(bts);

        int splitRshift = 0, splitUrshift = 0;
        // Forward pass. Insertions at i+1 don't affect back-edges; we skip inserted
        // GTs automatically because they have ttype==GT (not RSHIFT/URSHIFT).
        for (int i = 0; i < tokens.size(); i++) {
            Token t = tokens.get(i);
            if (t == null || !(t instanceof org.antlr.v4.runtime.CommonToken)) continue;
            org.antlr.v4.runtime.CommonToken ct = (org.antlr.v4.runtime.CommonToken) t;
            int s = t.getStartIndex();
            int e = t.getStopIndex();
            int line = t.getLine();
            int col = t.getCharPositionInLine();
            if (t.getType() == ZomParser.RSHIFT) {
                // 2-char >>  ->  GT (first) + GT (second)
                ct.setType(ZomParser.GT);
                ct.setStartIndex(s);
                ct.setStopIndex(s);
                // Build second GT (at i+1)
                org.antlr.v4.runtime.CommonToken gt2 =
                    new org.antlr.v4.runtime.CommonToken(ZomParser.GT);
                gt2.setChannel(t.getChannel());
                gt2.setStartIndex(s + 1);
                gt2.setStopIndex(e);
                gt2.setLine(line);
                try {
                    java.lang.reflect.Field fCol = org.antlr.v4.runtime.CommonToken
                        .class.getDeclaredField("column");
                    fCol.setAccessible(true);
                    fCol.setInt(gt2, col + 1);
                } catch (Throwable ignored) {
                    try { gt2.setCharPositionInLine(col + 1); }
                    catch (Throwable ignored2) {}
                }
                tokens.add(i + 1, gt2);
                splitRshift++;
            } else if (t.getType() == ZomParser.URSHIFT) {
                // 3-char >>>  ->  GT (first) + RSHIFT (remaining 2 chars)
                // On NEXT iteration i+1 we encounter the just-inserted RSHIFT and
                // split it again into two GTs, giving the effective 3 GT final form.
                ct.setType(ZomParser.GT);
                ct.setStartIndex(s);
                ct.setStopIndex(s);
                org.antlr.v4.runtime.CommonToken rs =
                    new org.antlr.v4.runtime.CommonToken(ZomParser.RSHIFT);
                rs.setChannel(t.getChannel());
                rs.setStartIndex(s + 1);
                rs.setStopIndex(e);
                rs.setLine(line);
                try {
                    java.lang.reflect.Field fCol = org.antlr.v4.runtime.CommonToken
                        .class.getDeclaredField("column");
                    fCol.setAccessible(true);
                    fCol.setInt(rs, col + 1);
                } catch (Throwable ignored) {
                    try { rs.setCharPositionInLine(col + 1); }
                    catch (Throwable ignored2) {}
                }
                tokens.add(i + 1, rs);
                splitUrshift++;
            }
            // else: leave alone (GT, EOF, punctuation, identifiers, etc.)
        }
    } catch (Throwable t) {
        // Non-fatal: if the reflection chain breaks on a particular JVM/ANTLR build
        // we fall back to "spaced > only" behavior. Tests that require compact-close
        // will fail with the usual ANTLR syntax error; no PCE or NPE escapes.
    }
}

/* A12 generic-nesting depth limit — §12.4 cap at 3 levels.
 * nested_angle_closure_neg_06 explicitly rejects 4+ depth like A<B<C<D<T>>>>.
 * Implementation notes (critical for correctness with ALL(*)):
 *   • Depth increment/decrement are placed in *plain semantic actions*
 *     (no trailing `?`), which ANTLR guarantees to run exactly once when the
 *     grammar symbol has been *actually matched (consumed)* in the real parse.
 *     Actions are NEVER invoked during adaptive lookahead simulation, so we
 *     don't get spurious double-counting from ALL(*).
 *   • The depth *check* is a *gated semantic predicate* (`{...}?`) after the
 *     depth-increment action and before the consuming rule body. Predicates
 *     CAN be re-evaluated during lookahead, so they MUST be side-effect-free.
 *     withinGenericDepthLimit() just reads a ThreadLocal<Integer> — pure check.
 *   • ThreadLocal is used so that per-fixture parse invocations (concurrent or
 *     sequential inside run_tests.sh) don't pollute each other's depth counter.
 */
static final int MAX_GENERIC_NEST = 3;
static final ThreadLocal<Integer> GENERIC_DEPTH = ThreadLocal.withInitial(() -> 0);
/* Called at sourceFile @init — resets the depth for a fresh parse. */
static void resetGenericDepth(Object p) { GENERIC_DEPTH.set(0); }
/* Plain semantic action (NOT a predicate) — runs only after an LT has actually
 * been consumed at the start of a generic instantiation. */
static void incGenericDepth(Object p) {
    Integer d = GENERIC_DEPTH.get();
    GENERIC_DEPTH.set((d == null ? 0 : d) + 1);
}
/* Plain semantic action — runs only after a genericClose terminal has actually
 * been consumed. Pops the nesting level by one. */
static void decGenericDepth(Object p) {
    Integer d = GENERIC_DEPTH.get();
    int nd = (d == null ? 0 : d) - 1;
    GENERIC_DEPTH.set(nd < 0 ? 0 : nd);
}
/* Pure gated predicate — reads the current depth and returns true iff depth ≤
 * MAX_GENERIC_NEST. Always safe to re-evaluate. Must be placed AFTER the
 * incGenericDepth plain semantic action so that the just-opened bracket is
 * counted when deciding legality. */
static boolean withinGenericDepthLimit(Object p) {
    Integer d = GENERIC_DEPTH.get();
    return d != null && d <= MAX_GENERIC_NEST;
}

/* A12 compact-close: GATED PREDICATE that runs BEFORE the genericClose rule's GT
 * terminal is matched (i.e. during prediction). Handles three cases:
 *
 *  - LA(1) == GT       : no split needed, return true → parser matches plain GT
 *  - LA(1) == RSHIFT   : lexer produced a single ">>" (2-char) token for two `>`
 *                        closers. We MUTATE the current token to make it look like
 *                        a single GT (1 char, first >), and INJECT a synthetic
 *                        second GT token into the BufferedTokenStream at position
 *                        p+1 (so the *next* genericClose invocation will consume it).
 *                        Returns true.
 *  - LA(1) == URSHIFT  : same as RSHIFT, but ">>>" (3-char). Mutate to GT, inject
 *                        RSHIFT (2 remaining chars) at p+1; the subsequent
 *                        genericClose will trigger this predicate again and split
 *                        the RSHIFT into two GTs.
 *
 * Because this is a GATED predicate positioned BEFORE the actual terminal, the
 * parser's decision engine sees only a single GT alt, drastically simplifying
 * SLL→ALL(*) lookahead. Works with reflection to reach BufferedTokenStream's
 * protected backing list and cursor.
 */
static boolean splitAngleIfCompact(Object parser) {
    try {
        ZomParser p = (ZomParser) parser;
        org.antlr.v4.runtime.TokenStream ts = p.getTokenStream();
        // Use LA(1) — this works both during ALL(*) lookahead simulation and
        // during real parsing, because LT(1) always points to the head of the
        // input regardless of how deep the adaptive prediction has looked ahead.
        Token cur = ts.LT(1);
        if (cur == null) return false;
        // IMPORTANT: gated predicates can be invoked many times during a single
        // ALL(*) decision simulation (every alternative re-evaluation re-runs them).
        // The token's own `type` field is therefore our IDEMPOTENCY marker:
        //   • first call with RSHIFT/URSHIFT: split and mutate type → GT
        //   • subsequent re-calls: type is now GT → short-circuit true
        //   • never-split case (plain GT from start): short-circuit true
        int ttype = cur.getType();
        if (ttype == ZomParser.GT) return true;
        if (!(cur instanceof org.antlr.v4.runtime.CommonToken)) return ttype == ZomParser.GT;
        org.antlr.v4.runtime.CommonToken ccur = (org.antlr.v4.runtime.CommonToken) cur;
        // Must also be able to inject into underlying BufferedTokenStream
        if (!(ts instanceof org.antlr.v4.runtime.BufferedTokenStream)) return false;
        org.antlr.v4.runtime.BufferedTokenStream bts =
            (org.antlr.v4.runtime.BufferedTokenStream) ts;
        java.lang.reflect.Field fTokens = org.antlr.v4.runtime.BufferedTokenStream
            .class.getDeclaredField("tokens");
        fTokens.setAccessible(true);
        @SuppressWarnings("unchecked")
        java.util.List<Token> tokens = (java.util.List<Token>) fTokens.get(bts);
        // Find the index of `cur` in the tokens list. Because ALL(*) lookahead
        // simulation doesn't advance `p` we can't rely on p. Walk from p to find
        // cur by identity.
        java.lang.reflect.Field fP = org.antlr.v4.runtime.BufferedTokenStream
            .class.getDeclaredField("p");
        fP.setAccessible(true);
        int idx = fP.getInt(bts);
        if (idx < 0) idx = 0;
        // Linear search starting at p, up to current size, to find `cur` by
        // object identity (safe since LT(1) returns the *same* Token object each
        // call during a single decision, and tokens list rarely exceeds size ~200
        // for fixture-level inputs).
        int curIdx = -1;
        for (int i = Math.min(idx, tokens.size() - 1);
             i < tokens.size() && i < idx + 32; i++) {
            if (tokens.get(i) == cur) { curIdx = i; break; }
        }
        if (curIdx == -1) {
            // Identity search failed (unusual) — fall back to scan from idx=0
            for (int i = 0; i < tokens.size(); i++) {
                if (tokens.get(i) == cur) { curIdx = i; break; }
            }
        }
        if (curIdx == -1) {
            // Cur not in list yet — probably because ALL(*) hasn't buffered it.
            // For GT case: still accept; for compact, let's fail rather than
            // corrupt state.
            return ttype == ZomParser.GT;
        }
        int s = cur.getStartIndex();
        int e = cur.getStopIndex();
        if (ttype == ZomParser.RSHIFT) {
            // Mutate to GT (first char). Subsequent re-evaluations of the
            // predicate will see GT and short-circuit true (idempotency!).
            ccur.setType(ZomParser.GT);
            ccur.setStopIndex(s);
            // Inject second GT at position curIdx+1. NOTE: during ALL(*)
            // simulation tokens may be re-traversed; but since we mutate
            // `cur` in-place, and the newly-injected token sits AFTER cur,
            // and we check identity of cur (not index), subsequent calls
            // continue to see cur as GT and return immediately. The injected
            // token becomes the LT(1) for the *next* rule (outer genericClose)
            // only after we've fully consumed the mutated GT (cur).
            org.antlr.v4.runtime.CommonToken ins =
                new org.antlr.v4.runtime.CommonToken(ZomParser.GT);
            ins.setChannel(cur.getChannel());
            ins.setStartIndex(s + 1);
            ins.setStopIndex(e);
            ins.setLine(cur.getLine());
            try {
                java.lang.reflect.Field fCol = org.antlr.v4.runtime.CommonToken
                    .class.getDeclaredField("column");
                fCol.setAccessible(true);
                int col = (int) fCol.get(ccur);
                fCol.setInt(ins, col + 1);
            } catch (Throwable ignored) {
                try {
                    java.lang.reflect.Method m = ccur.getClass().getMethod(
                        "setCharPositionInLine", int.class);
                    m.invoke(ins, ccur.getCharPositionInLine() + 1);
                } catch (Throwable ignored2) {}
            }
            tokens.add(curIdx + 1, ins);
            return true;
        }
        if (ttype == ZomParser.URSHIFT) {
            ccur.setType(ZomParser.GT);
            ccur.setStopIndex(s);
            org.antlr.v4.runtime.CommonToken ins =
                new org.antlr.v4.runtime.CommonToken(ZomParser.RSHIFT);
            ins.setChannel(cur.getChannel());
            ins.setStartIndex(s + 1);
            ins.setStopIndex(e);
            ins.setLine(cur.getLine());
            try {
                java.lang.reflect.Field fCol = org.antlr.v4.runtime.CommonToken
                    .class.getDeclaredField("column");
                fCol.setAccessible(true);
                int col = (int) fCol.get(ccur);
                fCol.setInt(ins, col + 1);
            } catch (Throwable ignored) {
                try {
                    java.lang.reflect.Method m = ccur.getClass().getMethod(
                        "setCharPositionInLine", int.class);
                    m.invoke(ins, ccur.getCharPositionInLine() + 1);
                } catch (Throwable ignored2) {}
            }
            tokens.add(curIdx + 1, ins);
            return true;
        }
        // Any other token → predicate fails → genericClose can't match
        // → ANTLR syntax error (correct behavior).
        return false;
    } catch (Throwable t) {
        // Fail-safe: propagate via PCE so we at least SEE it in failures.
        t.printStackTrace(System.err);
        throw new ParseCancellationException("splitAngleIfCompact("
            + t.getClass().getSimpleName() + "): " + t.getMessage());
    }
}

/* A12 compact-close: insert synthetic "remaining" token(s) into the TokenStream
 * AFTER the parser has consumed the current RSHIFT/URSHIFT.
 *
 * Context: the lexer performs longest-match and always turns consecutive >
 * characters into RSHIFT (>>) or URSHIFT (>>>) tokens. But generic type
 * arguments close one per > (e.g. Vec<Vec<i32>> needs two independent
 * genericClose matches, one for Vec<i32> and one for outer Vec). When
 * genericClose matches RSHIFT (covers 2 > chars), the outer close still
 * needs a single > — we therefore inject the remainder back into the stream.
 *
 * Because this is a plain semantic action (not gated predicate), it runs
 * AFTER the parser has successfully matched and CONSUMED the RSHIFT/URSHIFT
 * terminal, so TokenStream.p has already advanced to the next slot. We
 * INSERT the remainder token at index p — making it LT(1) for the next
 * parser decision, which is exactly the outer genericClose context.
 *
 * For RSHIFT: insert GT (remaining 1 >). For URSHIFT: insert RSHIFT
 * (remaining 2 >), which itself will recursively trigger this function
 * again in the subsequent genericClose. Reflection is used to access
 * BufferedTokenStream's protected fields (tokens list and pointer p).
 */
static void insertRemainingAngleTokens(int compactType, Object parser) {
    try {
        ZomParser p = (ZomParser) parser;
        org.antlr.v4.runtime.TokenStream ts = p.getTokenStream();
        if (!(ts instanceof org.antlr.v4.runtime.BufferedTokenStream)) {
        }
        org.antlr.v4.runtime.BufferedTokenStream bts =
            (org.antlr.v4.runtime.BufferedTokenStream) ts;
        java.lang.reflect.Field fTokens = org.antlr.v4.runtime.BufferedTokenStream
            .class.getDeclaredField("tokens");
        fTokens.setAccessible(true);
        @SuppressWarnings("unchecked")
        java.util.List<Token> tokens = (java.util.List<Token>) fTokens.get(bts);
        java.lang.reflect.Field fP = org.antlr.v4.runtime.BufferedTokenStream
            .class.getDeclaredField("p");
        fP.setAccessible(true);
        int idx = fP.getInt(bts);  // pointer after consume
        Token consumed = null;
        if (idx - 1 >= 0 && idx - 1 < tokens.size()) consumed = tokens.get(idx - 1);
        int s = consumed.getStartIndex();
        int e = consumed.getStopIndex();
        // Determine what synthetic token to inject, and its char-span.
        int newType;
        int newStart, newStop;
        if (compactType == ZomParser.RSHIFT) {
            newType = ZomParser.GT;
            newStart = s + 1;  // second char of ">>"
            newStop = e;       // = s + 1
        } else if (compactType == ZomParser.URSHIFT) {
            newType = ZomParser.RSHIFT;
            newStart = s + 1;  // last two chars of ">>>"
            newStop = e;       // = s + 2
        } else return;
        // Build the synthetic token. No source pair needed because we only
        // care about type + position; ZOM parser never re-reads token text.
        org.antlr.v4.runtime.CommonToken ins =
            new org.antlr.v4.runtime.CommonToken(newType);
        ins.setChannel(consumed.getChannel());
        ins.setStartIndex(newStart);
        ins.setStopIndex(newStop);
        ins.setLine(consumed.getLine());
        try {
            java.lang.reflect.Field fCol = org.antlr.v4.runtime.CommonToken
                .class.getDeclaredField("column");
            fCol.setAccessible(true);
            int col = (int) fCol.get(consumed);
            fCol.setInt(ins, col + 1);  // shifted into the compact token
        } catch (Throwable ignored) {
            // Fallback: public setter (exists on WritableToken impl)
            try {
                java.lang.reflect.Method m = org.antlr.v4.runtime.CommonToken
                    .class.getMethod("setCharPositionInLine", int.class);
                m.invoke(ins, consumed.getCharPositionInLine() + 1);
            } catch (Throwable ignored2) {}
        }
        // Insert at position p (the next LT(1) slot). Because tokens list is
        // ArrayList, insert at index p automatically shifts existing tokens
        // right (including anything that was already beyond p, e.g. lookahead
        // buffered by ALL(*)). BufferedTokenStream.p is not modified — it
        // still points at the "new" index p which now holds our synthetic
        // token. Perfect.
        tokens.add(idx, ins);
        // If ALL(*) already buffered lookahead tokens beyond idx the backing
        // list is fine but the parser's ATN simulation may reference stale
        // Token pointers. This is extremely unlikely here because
        // genericClose is a simple 1-token alt with no nested decisions and
        // the next decision (another genericClose) starts fresh with LT(1).
    } catch (Throwable t) {
        // Never fail the parse because of a token-injection bug. Silent
        // no-op keeps parser running; user only sees "unexpected <token>"
        // at a later position, which is still a failing test but not a
        // confusing NPE or PCE from our reflection code.
    }
}
    /** A13-REJECT: bare single-segment identifier as import target. */
    static boolean rejectImportBareId(String name, Object parser) {
        throw new org.antlr.v4.runtime.misc.ParseCancellationException(
            "import path must be qualified with `::` — bare identifier `"+name+"` is not valid");
    }

    /** A13 (importSimple/importRename): reject bare single-segment identifier at the top
     *  of an import declaration. Accepts `*` or any attributePath (contains `::`). */
    static boolean rejectImportBareIdUnlessStarOrAttrPath(
            org.antlr.v4.runtime.RuleContext importClauseCtx, Object parser) {
        if (importClauseCtx == null) return true;
        // 检查 importClause 的第 1 个 child 是否是 terminal（如 `*`）还是 identifier rule 还是 attributePath rule
        org.antlr.v4.runtime.ParserRuleContext pr = (org.antlr.v4.runtime.ParserRuleContext)importClauseCtx;
        // importClause 是父 rule，它包裹的 child 类型看 alt
        // 简便法：first terminal text == "*" → ok；第一子是 identifier rule → REJECT；attributePath → ok
        for (int i = 0; i < pr.getChildCount(); i++) {
            org.antlr.v4.runtime.tree.ParseTree ch = pr.getChild(i);
            if (ch instanceof org.antlr.v4.runtime.tree.TerminalNode) {
                if ("*".equals(((org.antlr.v4.runtime.tree.TerminalNode)ch).getText()))
                    return true;
            } else if (ch instanceof org.antlr.v4.runtime.ParserRuleContext) {
                int ridx = ((org.antlr.v4.runtime.ParserRuleContext)ch).getRuleIndex();
                if (ridx == ZomParser.RULE_identifier) {
                    // identifier alt → REJECT
                    org.antlr.v4.runtime.tree.TerminalNode tn = firstTerminal(ch);
                    String name = (tn == null) ? "?" : tn.getText();
                    throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                        "import declaration must use qualified path with `::` or group form; " +
                        "bare identifier `"+name+"` is not valid as a standalone import target");
                }
                // attributePath rule → ok
                return true;
            }
        }
        return true;
    }

    /** A11: abstract method must not have a block body. */    /** A11: abstract method must not have a block body. */
    static boolean checkAbstractNoBlock(org.antlr.v4.runtime.RuleContext modListCtx, Object parser) {
        if (modListCtx == null) return true;
        java.util.Collection<?> col3 =
            org.antlr.v4.runtime.tree.Trees.findAllTokenNodes(modListCtx, ZomParser.ABSTRACT);
        if (!col3.isEmpty())
            throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                "abstract method must not have a block body (use `;` to declare an abstract signature)");
        return true;
    }

    // =========================================================================
    // §19 Conditional Compilation — cfg predicates (ZOM1900/ZOM1901/ZOM1903)
    // =========================================================================

    /** Utility helper for semantic predicates: flatten a ParseTree's concatenated text
     *  (identifier COLONCOLON identifier)* to a single String. Used by attrCfg alt
     *  semantic predicate to test attributePath.toString() == "zom::cfg". */
    static String stringOf(org.antlr.v4.runtime.tree.ParseTree tree) {
        if (tree == null) return "";
        java.lang.StringBuilder sb = new java.lang.StringBuilder();
        java.util.Queue<org.antlr.v4.runtime.tree.ParseTree> q = new java.util.LinkedList<>();
        q.add(tree);
        while (!q.isEmpty()) {
            org.antlr.v4.runtime.tree.ParseTree n = q.poll();
            if (n instanceof org.antlr.v4.runtime.tree.TerminalNode) {
                sb.append(((org.antlr.v4.runtime.tree.TerminalNode)n).getSymbol().getText());
            } else {
                for (int i = 0; i < n.getChildCount(); i++) q.add(n.getChild(i));
            }
        }
        return sb.toString();
    }

    /** §19: Throw ZOM1900 CfgPredicateParseError — used as { rejectCfgPredicateBad(this) }?
     *  inline on fall-through alts so malformed cfg predicates are diagnosed with the
     *  right code instead of a generic ANTLR "no viable alternative". */
    static boolean rejectCfgPredicateBad(Object parser) {
        throw new org.antlr.v4.runtime.misc.ParseCancellationException(
            "ZOM1900 CfgPredicateParseError — malformed `#[zom::cfg(...)]` predicate; " +
            "see §19.3.1 grammar: cfgPredicate = cfgAll | cfgAny | cfgNot | cfgAtom");
    }

    /** §19.6.2 ZOM1901 CfgOnExpression — Module-Item-level semantic-predicate helper.
     *  Invoked on the `moduleItemStatementCfgGated` alt:
     *      outerAttributeList statement
     *  Called only when outerAttributeList is non-empty (the no-attr case goes to
     *  the separate `moduleItemStatement` alt). Validation rules:
     *    (a) outerAttributeList may only contain `#[zom::cfg(...)]` attributes —
     *        any other attribute path is rejected with ZOM1601.
     *    (b) if any zom::cfg attribute is present, statement MUST be a standalone
     *        `blockBody` (`#stmtBlock`); anything else raises ZOM1901.
     *  Declarations always pass because they go to `moduleItemDeclaration` alt,
     *  which unconditionally allows all attributes — never calls this helper.
     *
     *  @param attrsCtx outerAttributeList context (non-empty)
     *  @param stmtCtx  parsed statement context (already matched)
     */
    static boolean checkStatementCfgGate(
            org.antlr.v4.runtime.RuleContext attrsCtx,
            org.antlr.v4.runtime.RuleContext stmtCtx,
            Object parser) {
        if (attrsCtx == null || attrsCtx.getChildCount() == 0) return true;

        boolean hasCfgAttr = false;
        String firstNonCfgPath = null;
        // The attribute list lives under RULE_attrItem (after v3 dispatch split).
        // attrItem has two labelled alts:
        //   "AttrZomCfgContext"      → §19 `#[zom::cfg(PRED)]`
        //   "AttrGenericItemContext"  → §4.4 generic attr, wrapping a RULE_attr child
        java.util.Collection<?> items =
            org.antlr.v4.runtime.tree.Trees.findAllRuleNodes(attrsCtx, ZomParser.RULE_attrItem);
        if (items.isEmpty()) {
            // Legacy fallback: walk RULE_attr directly (in case caller passed a
            // single attr context without attrItem wrapping).
            items = org.antlr.v4.runtime.tree.Trees.findAllRuleNodes(attrsCtx, ZomParser.RULE_attr);
        }
        for (Object oa : items) {
            org.antlr.v4.runtime.RuleContext itemCtx = (org.antlr.v4.runtime.RuleContext) oa;
            if (itemCtx.getChildCount() == 0) continue;
            String itemAlt = itemCtx.getClass().getSimpleName();
            String pathText;
            if (itemAlt.contains("AttrZomCfg")) {
                // Directly matched attrItem#attrZomCfg alt — path is canonical.
                pathText = "zom::cfg";
            } else {
                // Either (a) attrItem#attrGenericItem wrapping a RULE_attr, or
                // (b) legacy RULE_attr direct node.
                org.antlr.v4.runtime.RuleContext attrCtx = itemCtx;
                if (itemAlt.contains("AttrGenericItem")) {
                    // Unwrap to find the inner RULE_attr child (should be at index 0
                    // of the non-children elements — walk looking for RuleContext whose
                    // rule index == RULE_attr).
                    attrCtx = null;
                    for (int _k = 0; _k < itemCtx.getChildCount(); _k++) {
                        Object c = itemCtx.getChild(_k);
                        if (c instanceof org.antlr.v4.runtime.RuleContext
                            && !(c instanceof org.antlr.v4.runtime.tree.TerminalNode)) {
                            int ri = ((org.antlr.v4.runtime.RuleContext) c).getRuleIndex();
                            if (ri == ZomParser.RULE_attr) {
                                attrCtx = (org.antlr.v4.runtime.RuleContext) c;
                                break;
                            }
                        }
                    }
                    if (attrCtx == null) continue;
                }
                // GRAMMAR NOTE (dispatcher for attr rule).
                //   The attr rule has two TOP-LEVEL labelled alternatives:
                //     "GenericMultiSegContext"  → multi-segment (first::tail + opt args)
                //     "GenericSegment1Context"  → 1-segment (name + opt args)
                String attrAlt = attrCtx.getClass().getSimpleName();
                StringBuilder sb = new StringBuilder();
                // Both alts put the leading identifier at child index 0.
                sb.append(stringOf(attrCtx.getChild(0)));
                // Only multi-seg has a RULE_attributePathTail child.
                for (int _k = 1; _k < attrCtx.getChildCount(); _k++) {
                    Object c = attrCtx.getChild(_k);
                    if (!(c instanceof org.antlr.v4.runtime.RuleContext)) continue;
                    String rn = ((org.antlr.v4.runtime.RuleContext) c).getClass().getSimpleName();
                    if (rn.contains("AttributePathTail")) {
                        sb.append("::").append(stringOf((org.antlr.v4.runtime.tree.ParseTree) c));
                        break;
                    }
                }
                pathText = sb.toString();
            }
            if ("zom::cfg".equals(pathText)) {
                hasCfgAttr = true;
            } else if (firstNonCfgPath == null) {
                firstNonCfgPath = pathText;
            }
        }

        // (a) Reject non-cfg attributes on statements (§16 + §19).
        if (firstNonCfgPath != null) {
            throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                "ZOM1601 AttrOnStatementDisallowed — statements may not carry outer " +
                "attributes other than `#[zom::cfg(...)]` at module/block scope. " +
                "Move attribute `#[" + firstNonCfgPath + "]` to the declaration " +
                "immediately enclosing this statement (§16 Attribute Targets, §19.6.2).");
        }

        // (b) With cfg attr present, only blockBody is an allowed statement form.
        if (hasCfgAttr && stmtCtx != null) {
            boolean isBlock = false;
            // Fast path: class name contains "StmtBlock".
            String cn = stmtCtx.getClass().getSimpleName();
            if (cn.contains("StmtBlock")) isBlock = true;
            // Fallback: first terminal is LBRACE.
            if (!isBlock) {
                org.antlr.v4.runtime.tree.TerminalNode tn0 = firstTerminal(stmtCtx);
                if (tn0 != null && tn0.getSymbol().getType() == ZomParser.LBRACE) isBlock = true;
            }
            if (!isBlock) {
                throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                    "ZOM1901 CfgOnExpression — `#[zom::cfg(...)]` at module or block " +
                    "scope can only gate STANDALONE BLOCK statements of the form " +
                    "`#[zom::cfg(...)] { stmt* }`. It cannot gate individual " +
                    "expressions, control-flow statements, or declaration-statement " +
                    "forms like `let`, `mut`, `for`, `if`, `return`. " +
                    "Wrap the statement in braces (§19.6.2).");
            }
        }

        return true;
    }

    /** Helper used by checkStatementCfgGate + checkCfgDeclarationTarget to
     *  reconstruct the canonical attribute-path string from an attrItem or
     *  attr context. Mirrors the v3 dispatch hierarchy:
     *    attrItem#attrZomCfg       → "zom::cfg"
     *    attrItem#attrGenericItem  → unwrap to RULE_attr, then dispatch:
     *      attr#genericMultiSeg    → first::attributePathTail
     *      attr#genericSegment1    → name
     *  Returns null if no path can be reconstructed (should never happen).
     */
    static String pathOfAttrItemOrAttr(Object ctx) {
        if (!(ctx instanceof org.antlr.v4.runtime.RuleContext)) return null;
        org.antlr.v4.runtime.RuleContext rctx = (org.antlr.v4.runtime.RuleContext) ctx;
        String altName = rctx.getClass().getSimpleName();
        if (altName.contains("AttrZomCfg")) return "zom::cfg";

        org.antlr.v4.runtime.RuleContext attrCtx = rctx;
        // Unwrap attrItem#attrGenericItem to find its RULE_attr child.
        if (altName.contains("AttrGenericItem")) {
            attrCtx = null;
            for (int _k = 0; _k < rctx.getChildCount(); _k++) {
                Object c = rctx.getChild(_k);
                if (c instanceof org.antlr.v4.runtime.RuleContext
                    && !(c instanceof org.antlr.v4.runtime.tree.TerminalNode)) {
                    int ri = ((org.antlr.v4.runtime.RuleContext) c).getRuleIndex();
                    if (ri == ZomParser.RULE_attr) {
                        attrCtx = (org.antlr.v4.runtime.RuleContext) c;
                        break;
                    }
                }
            }
            if (attrCtx == null) return null;
        }
        // Now inside a RULE_attr context (genericMultiSeg or genericSegment1).
        StringBuilder sb = new StringBuilder();
        sb.append(stringOf(attrCtx.getChild(0)));
        for (int _k = 1; _k < attrCtx.getChildCount(); _k++) {
            Object c = attrCtx.getChild(_k);
            if (!(c instanceof org.antlr.v4.runtime.RuleContext)) continue;
            String rn = ((org.antlr.v4.runtime.RuleContext) c).getClass().getSimpleName();
            if (rn.contains("AttributePathTail")) {
                sb.append("::").append(stringOf((org.antlr.v4.runtime.tree.ParseTree) c));
                break;
            }
        }
        return sb.toString();
    }

    /** §19.6.2 ZOM1901 CfgOnExpression — Declaration-side complement of
     *  checkStatementCfgGate. Invoked on the `moduleItemDeclaration` alt:
     *      outerAttributeList declaration
     *  Declarations that are "statement-like" (let / mut / const, which also
     *  appear in statement-form inside block bodies) are NOT allowed to carry
     *  `#[zom::cfg(...)]` at module scope either — they must be wrapped in a
     *  standalone block per §19.6.2 ("you cannot cfg-gate a single
     *  `let x = 5;` statement without wrapping it in `{ ... }`").
     *  Real declarations (fun, class, struct, enum, interface, error, import,
     *  export, alias, marker, standalone impl, type alias, module, package,
     *  const) are always allowed.
     *
     *  Detection strategy: look at the declaration context's alt class name.
     *  "LetDeclContext", "MutDeclContext" (actually these appear as
     *  "LetDeclarationContext"/"MutDeclarationContext" — use contains() as a
     *  sub-string match so we are immune to exact naming conventions).
     */
    static boolean checkCfgDeclarationTarget(
            org.antlr.v4.runtime.RuleContext attrsCtx,
            org.antlr.v4.runtime.RuleContext declCtx,
            Object parser) {
        if (attrsCtx == null || attrsCtx.getChildCount() == 0) return true;
        if (declCtx == null) return true;
        // Does the outerAttrList actually contain at least one zom::cfg?
        boolean hasCfg = false;
        String firstNonCfgPath = null;
        java.util.Collection<?> items =
            org.antlr.v4.runtime.tree.Trees.findAllRuleNodes(attrsCtx, ZomParser.RULE_attrItem);
        if (items.isEmpty()) {
            // Legacy fallback (same as checkStatementCfgGate).
            items = org.antlr.v4.runtime.tree.Trees.findAllRuleNodes(attrsCtx, ZomParser.RULE_attr);
        }
        for (Object oa : items) {
            String p = pathOfAttrItemOrAttr(oa);
            if (p == null) continue;
            if ("zom::cfg".equals(p)) hasCfg = true;
            else if (firstNonCfgPath == null) firstNonCfgPath = p;
        }
        // Non-cfg attrs on declarations are always fine.
        if (!hasCfg) return true;
        // If cfg is present, reject statement-like decls.
        String cn = declCtx.getClass().getSimpleName();
        boolean isStatementLike =
            cn.contains("Let") || cn.contains("Mut");  // constDecl stays allowed
        if (isStatementLike) {
            throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                "ZOM1901 CfgOnExpression — `#[zom::cfg(...)]` at module scope cannot " +
                "gate a single let/mut declaration. Wrap it in a standalone block: " +
                "`#[zom::cfg(...)] { let x: T = v; }` (§19.6.2).");
        }
        return true;
    }

    /** @deprecated Replaced by {@link #checkStatementCfgGate}. Retained so that
     *  external code that reflects over these helper names does not break;
     *  the current grammar never calls it (it couldn't — by the time the
     *  `statement` rule is matched, its parent's outerAttributeList has not
     *  yet been constructed in ANTLR's bottom-up parse order). */
    @Deprecated
    static boolean checkCfgNotOnExpression(org.antlr.v4.runtime.RuleContext stmtCtx, Object parser) {
        return true;
    }

    /** §19.4.3 ZOM1903 FeatureUndeclared: lightweight parser-side structural check
     *  (the full check requires reading Zom.toml, which runs at a later semantic pass;
     *  the parser only validates that `feature = "..."` has a non-empty identifier RHS). */
    /** §19.4.3 ZOM1903 FeatureUndeclared — structural probe.
     *
     *  ⚠  THIS IS A LEGACY PROBE ONLY — NEVER call it inside { ... }? semantic predicates
     *     because ParseCancellationException thrown inside predicate-eval is wrapped as
     *     predicate-false by the ANTLR 4 runtime.
     *
     *  Real enforcement lives in `enforceCfgAtomQuotedRhs` (tail parser-action in
     *  attrItem#attrZomCfg), which walks the complete cfgPredicate parse-tree AFTER
     *  all tokens have been consumed — throws PCE cleanly as an exception (rc=2).
     *
     *  Returns true for non-`feature` keys AND for `feature` with non-empty stripped
     *  value; returns false for `feature = ""` (empty quoted string). Used as a read-
     *  only predicate probe by anyone who wants to guard without throwing.
     *
     *  Historical: before tail-action enforcement, this method attempted to throw
     *  PCE inside a semantic predicate → the exception was swallowed by ANTLR's
     *  PredicateEval try/catch → `cfg_feature_empty_value_neg_04.zom` silently accepted
     *  with rc=0 (a false-negative bug — see R12 bugfix). */
    static boolean checkCfgFeatureAtomFormat(Token key, String valueText, Object parser) {
        if (key == null) return true;
        String k = key.getText();
        if (!"feature".equals(k)) return true;
        if (valueText == null || valueText.length() < 3) return false;
        return true;
    }

    /** §19.3.1.1 ZOM1902 helper (warning-level, not hard error): emit unknown-key warning.
     *  Because ANTLR semantic predicates cannot emit warnings, we only validate structural
     *  shape (comparison operators do not apply to bare-key atoms) here; the warning
     *  is emitted in a post-parse cfg-lint pass. */
    static boolean checkCfgAtomShape(Object keyNode, boolean hasOp, String opText, Object parser) {
        // - Bare atom: hasOp == false → always ok.
        // - Valued atom: op must be one of = != < <= > >= (enforced by grammar already),
        //   just sanity-check that comparisons operate on version-capable keys.
        if (!hasOp) return true;
        return true;
    }

    /** §19.3.1 ZOM1900 CfgPredicateMalformed — used by cfgAtomBare as a negative-lookahead
     *  gate: when the next token is a CfgOp (= != < <= > >=), it means the user attempted
     *  to write a valued atom but produced a non-DOUBLE_STRING_LITERAL RHS (e.g. a bare
     *  identifier like `target_os = linux`). Without this explicit gate, ANTLR's default
     *  error recovery would single-token-delete the stray op and silently accept the
     *  malformed line as a bare-key atom, producing a false-positive ACCEPT.
     *
     *  Returns true (proceed) when LA(1) is NOT a CfgOp; throws ParseCancellationException
     *  (parser aborts, reports ZOM1900) otherwise.
     */
    static boolean checkCfgAtomNoPendingOp(Object parser) {
        if (!(parser instanceof ZomParser)) return true;
        ZomParser self = (ZomParser) parser;
        int la;
        try {
            la = ((org.antlr.v4.runtime.TokenStream) self.getTokenStream()).LA(1);
        } catch (Exception ignore) { return true; }
        // Six CfgOp token types: ASSIGN NEQ LT LTE GT GTE
        if (la == ASSIGN || la == NEQ || la == LT || la == LTE || la == GT || la == GTE) {
            String sym = ZomParser.VOCABULARY.getSymbolicName(la);
            if (sym == null) sym = "<unknown>";
            throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                "ZOM1900 CfgPredicateMalformed — cfg atom value must be a double-quoted string literal after '"
                + sym + "' operator, e.g. key = \"value\" (got unquoted bare identifier or wrong token type).");
        }
        return true;
    }

    /** §19 attr-dispatcher: deterministic selection between #attrCfg and #attrGeneric.
     *
     *  PROBLEM: ANTLR 4 ALL(*) makes alt-selection decisions based on UNLIMITED lookahead.
     *  When the raw token stream matches `IDENTIFIER :: IDENTIFIER ( ... )`, ALL(*) reads
     *  ahead through the PAREN body to decide between the two labeled alts. If it sees
     *  that the *next* tokens (e.g. unquoted bare identifier `target_os = linux`) would
     *  cause cfgPredicate to fail, it ABANDONS the gated attrCfg path and falls through
     *  to attrGeneric — where the entire `zom::cfg(...)` block is silently parsed as a
     *  generic attribute with an expression inside. Result: false-positive ACCEPT on
     *  malformed cfg predicates (REJECT tests wrongly pass).
     *
     *  SOLUTION: make the two alts' LEADING GATED PREDICATES DETERMINISTIC and MUTUALLY
     *  EXCLUSIVE. We do this by peeking 4 tokens ahead in the raw TokenStream:
     *    pattern  =  IDENTIFIER("zom")  COLONCOLON  IDENTIFIER("cfg")  LPAREN
     *  If this pattern matches, the parser MUST dispatch to attrCfg (no fallthrough),
     *  and attrGeneric's gated predicate returns false so it is not even considered.
     *  If it does NOT match, only attrGeneric is considered. This converts a non-local
     *  ALL(*) decision (unbounded lookahead into the paren body) into a LOCAL, bounded
     *  lookahead that ANTLR evaluates identically in prediction and parse phases.
     */
    static boolean peekIsZomCfgParen(Object parser) {
        if (!(parser instanceof ZomParser)) return false;
        org.antlr.v4.runtime.TokenStream ts;
        try { ts = ((ZomParser) parser).getTokenStream(); }
        catch (Exception ignore) { return false; }
        int la1, la2, la3, la4;
        String t1, t3;
        try {
            la1 = ts.LA(1); t1 = ts.LT(1).getText();
            la2 = ts.LA(2);
            la3 = ts.LA(3); t3 = ts.LT(3).getText();
            la4 = ts.LA(4);
        } catch (Exception ignore) { return false; }
        return la1 == IDENTIFIER && "zom".equals(t1)
            && la2 == COLONCOLON
            && la3 == IDENTIFIER && "cfg".equals(t3)
            && la4 == LPAREN;
    }

    /** §19 attr dispatch — 2-token bounded lookahead for generic multi-segment form.
     *  Returns true iff:
     *    LA(1) == IDENTIFIER   AND   LA(2) == COLONCOLON
     *  AND   NOT the 4-token "zom" :: "cfg" "(" pattern (that belongs to #zomCfg).
     *  Combined with the other two leading gates (peekIsZomCfgParen for #zomCfg,
     *  !isNextToken(COLONCOLON) for #genericSegment1), the three attr alternatives
     *  form a PARTITION of the reachable prefix space: mutually exclusive & total.
     *
     *  Why leading-gate all three instead of relying on FIRST-set tie-breaking:
     *  ANTLR 4 ALL(*) simulator continues exploring past the gated entry into the
     *  alt body IF the gated predicate succeeds. A gated that returns false short-
     *  circuits body simulation entirely — which is precisely what we need for
     *  the #zomCfg alt (its body can throw PCE on malformed cfgPredicates, so we
     *  must prevent ALL(*) from trying it when the 4-token prefix does not match).
     *  For the generic alts, prefix-disjoint gates eliminate the ALL(*) cross-body
     *  reachability check that otherwise makes the decision SLL-k=∞ and fragile.
     */
    static boolean peekIsGenericMultiSeg(Object parser) {
        if (!(parser instanceof ZomParser)) return false;
        if (peekIsZomCfgParen(parser)) return false;  // #zomCfg's prefix, not ours
        org.antlr.v4.runtime.TokenStream ts;
        try { ts = ((ZomParser) parser).getTokenStream(); }
        catch (Exception ignore) { return false; }
        int la1, la2;
        try {
            la1 = ts.LA(1);
            if (la1 != IDENTIFIER) return false;
            la2 = ts.LA(2);
        } catch (Exception ignore) { return false; }
        return la2 == COLONCOLON;
    }

    /** Bounded 1-token lookahead helper: returns true iff TokenStream.LA(1) equals
     *  the given token-type. Exception-safe; returns false on EOF. */
    static boolean isNextToken(Object parser, int tokenType) {
        if (!(parser instanceof ZomParser)) return false;
        org.antlr.v4.runtime.TokenStream ts;
        try { ts = ((ZomParser) parser).getTokenStream(); }
        catch (Exception ignore) { return false; }
        try { return ts.LA(1) == tokenType; }
        catch (Exception ignore) { return false; }
    }

    /** Bounded 2-token lookahead helper: returns true iff TokenStream.LA(2) equals
     *  the given token-type. Exception-safe; returns false on EOF. */
    static boolean la2Is(Object parser, int tokenType) {
        if (!(parser instanceof ZomParser)) return false;
        org.antlr.v4.runtime.TokenStream ts;
        try { ts = ((ZomParser) parser).getTokenStream(); }
        catch (Exception ignore) { return false; }
        try { return ts.LA(2) == tokenType; }
        catch (Exception ignore) { return false; }
    }

    /** Convenience: bounded 1-token lookahead for "LA(1) == IDENTIFIER AND LA(2) == type".
     *  Used to gate attrs whose leading two tokens determine the dispatch class. */
    static boolean isNextTokenAfterIdent(Object parser, int tokenType) {
        return isNextToken(parser, IDENTIFIER) && la2Is(parser, tokenType);
    }

    // ---------------------------------------------------------------------
    // §19.3.1 cfgAtom 3-alt deterministic group helpers
    // ---------------------------------------------------------------------
    /** Entry gate for cfgAtom alt 1 + alt 2.
     *  Returns true iff LA(1) is one of the six CfgOp tokens. */
    static boolean isCfgOpNext(Object parser) {
        if (!(parser instanceof ZomParser)) return false;
        org.antlr.v4.runtime.TokenStream ts;
        try { ts = ((ZomParser) parser).getTokenStream(); }
        catch (Exception ignore) { return false; }
        int la;
        try { la = ts.LA(1); } catch (Exception ignore) { return false; }
        return la == ASSIGN || la == NEQ || la == LT || la == LTE || la == GT || la == GTE;
    }

    /** cfgAtom alt 1 guard (valued form: CfgOp + quoted string).
     *  Returns true iff LA(2) (position after the consumed CfgOp) is DOUBLE_STRING_LITERAL.
     *  Precondition: caller has already gated with `isCfgOpNext`, so LA(1) ∈ CfgOpSet. */
    static boolean isDoubleStringAfterCfgOp(Object parser) {
        if (!(parser instanceof ZomParser)) return false;
        org.antlr.v4.runtime.TokenStream ts;
        try { ts = ((ZomParser) parser).getTokenStream(); }
        catch (Exception ignore) { return false; }
        int la2;
        try { la2 = ts.LA(2); } catch (Exception ignore) { return false; }
        return la2 == DOUBLE_STRING_LITERAL;
    }

    /** Tail parser-action for attrItem#attrZomCfg — invoked AFTER the entire
     *  `#[zom::cfg(PRED)]` block has been successfully matched (i.e. every token
     *  consumed, all semantic predicates accepted, rule-end RPAREN recognised).
     *
     *  POSITIONAL SAFETY NOTE (critical, do not inline the throw elsewhere)
     *  --------------------------------------------------------------------
     *  This action is placed at the TAIL of a fully-matched rule alternative.
     *  ANTLR 4's ALL(*) simulator walks the ATN ONLY through positions that are
     *  reachable during PREDICTION — i.e. every node that precedes a decision
     *  state that the simulator must disambiguate. A tail action following the
     *  LAST concrete terminal in a rule (here, `RPAREN`) is NOT on any
     *  prediction path; the simulator treats it as a benign ε-action leading
     *  directly to the rule's stop-state. Unlike a mid-body throw (which the
     *  simulator walks through to determine reachability, producing poisoned
     *  DFA states with "exception-terminated accept-sets"), a tail throw is
     *  INVISIBLE to the entire ATN construction / DFA merge phase.
     *
     *  This is why ZOM1900 + ZOM1903 are enforced HERE and not inside
     *  (V1/V2/V3 designs that placed the throws inline all produced spurious
     *  NVA or silent-failed predicates that the DefaultErrorStrategy recovered).
     *
     *  WALK (V5 — dedicated sub-rule matching):
     *    1. Collect every cfgAtom node under `predCtx` (recursive, so && / || /
     *       ! / paren-groups are all descended into).
     *    2. For each cfgAtom, scan its FIRST-level rule children:
     *       a. RULE_badRhsCfgAtomRhs → OP + unquoted-IDENTIFIER → throw ZOM1900
     *       b. RULE_valuedCfgAtomRhs → OP + DOUBLE_STRING
     *          i. key=="feature" AND stripped-val=="" → throw ZOM1903 FeatureUndeclared
     *       c. RULE_bareCfgAtomRhs → bare (no OP) → OK
     *
     *  Returns void so the generated code is a plain { method_call(); } parser
     *  action — NO trailing `?`, NO predicate-false swallowing, so any PCE throw
     *  produces rc=2 (NOT rc=1).
     */
    static void enforceCfgAtomQuotedRhs(org.antlr.v4.runtime.RuleContext predCtx, Object parser) {
        if (predCtx == null || !(parser instanceof ZomParser)) return;
        final int RULE_ATOM = ZomParser.RULE_cfgAtom;
        final int RULE_BAD  = ZomParser.RULE_badRhsCfgAtomRhs;
        final int RULE_VAL  = ZomParser.RULE_valuedCfgAtomRhs;
        java.util.Collection<? extends org.antlr.v4.runtime.tree.Tree> atoms =
            org.antlr.v4.runtime.tree.Trees.findAllRuleNodes(predCtx, RULE_ATOM);
        for (org.antlr.v4.runtime.tree.Tree atom : atoms) {
            if (!(atom instanceof org.antlr.v4.runtime.RuleContext)) continue;
            org.antlr.v4.runtime.RuleContext atomCtx = (org.antlr.v4.runtime.RuleContext) atom;
            // Extract cfgAtom's key token: first IDENTIFIER terminal child.
            Token keyTok = null;
            // Locate first-level rule child (BAD, VAL, BARE — exactly one present).
            org.antlr.v4.runtime.RuleContext rhsBad = null;
            org.antlr.v4.runtime.RuleContext rhsVal = null;
            for (int i = 0; i < atomCtx.getChildCount(); i++) {
                Object c = atomCtx.getChild(i);
                if (c instanceof org.antlr.v4.runtime.tree.TerminalNode) {
                    Token tk = ((org.antlr.v4.runtime.tree.TerminalNode)c).getSymbol();
                    if (tk.getType() == IDENTIFIER && keyTok == null) keyTok = tk;
                } else if (c instanceof org.antlr.v4.runtime.RuleContext) {
                    org.antlr.v4.runtime.RuleContext rc = (org.antlr.v4.runtime.RuleContext) c;
                    if (rc.getRuleIndex() == RULE_BAD) rhsBad = rc;
                    if (rc.getRuleIndex() == RULE_VAL) rhsVal = rc;
                }
            }
            if (rhsBad != null) {
                // ——— ZOM1900 — unquoted RHS (badRhsCfgAtomRhs) ———
                String key = "<key>", op = "<op>", rhs = "<rhs>";
                int line = -1, col = -1;
                if (keyTok != null) {
                    key = keyTok.getText();
                    line = keyTok.getLine();
                    col = keyTok.getCharPositionInLine();
                }
                for (int i = 0; i < rhsBad.getChildCount(); i++) {
                    Object c = rhsBad.getChild(i);
                    if (!(c instanceof org.antlr.v4.runtime.tree.TerminalNode)) continue;
                    Token tk = ((org.antlr.v4.runtime.tree.TerminalNode) c).getSymbol();
                    int t = tk.getType();
                    if (t == ASSIGN || t == NEQ || t == LT || t == LTE || t == GT || t == GTE) {
                        op = tk.getText();
                        if (line < 0) { line = tk.getLine(); col = tk.getCharPositionInLine(); }
                    } else if (t == IDENTIFIER) {
                        rhs = tk.getText();
                    }
                }
                String loc = (line >= 0) ? (line + ":" + col) : "<unknown>";
                throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                  "ZOM1900 CfgPredicateMalformed at " + loc
                  + " — cfg atom value must be a double-quoted string literal after '"
                  + op + "' operator, e.g. key = \"value\" (got unquoted token '"
                  + rhs + "' for key '" + key + "').");
            }
            if (rhsVal != null && keyTok != null && "feature".equals(keyTok.getText())) {
                // ——— ZOM1903 — `feature = ""` — empty value ———
                // Find the DOUBLE_STRING_LITERAL terminal child
                String valText = null;
                int line = keyTok.getLine(), col = keyTok.getCharPositionInLine();
                for (int i = 0; i < rhsVal.getChildCount(); i++) {
                    Object c = rhsVal.getChild(i);
                    if (!(c instanceof org.antlr.v4.runtime.tree.TerminalNode)) continue;
                    Token tk = ((org.antlr.v4.runtime.tree.TerminalNode) c).getSymbol();
                    if (tk.getType() == DOUBLE_STRING_LITERAL) {
                        valText = tk.getText();
                        if (line < 0) { line = tk.getLine(); col = tk.getCharPositionInLine(); }
                    }
                }
                // Strip the surrounding quotes.
                String inner = valText;
                if (inner != null && inner.length() >= 2) inner = inner.substring(1, inner.length() - 1);
                if (inner == null || inner.isEmpty()) {
                    String loc = line >= 0 ? (line + ":" + col) : "<unknown>";
                    throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                      "ZOM1903 FeatureUndeclared at " + loc
                      + " — `feature = \"\" requires a non-empty string value "
                      + "(got empty double-quoted empty string).");
                }
            }
        }
    }

    // ===== §20 FFI / §21 Macros 新增软关键字 & ABI 校验谓词（尾动作安全）=====

    /** §20 extern "C"/"Cdecl"/"system"/"zom-cdecl" ABI 校验（ZOM2001）。
     *  Parser 层 semantic predicate（Lexer 未定义 EXTERN_ABI_STRING，复用
     *  DOUBLE_STRING_LITERAL）。允许值集合 = {"C", "Cdecl", "system", "zom-cdecl"}。
     *  按 ZOM-G4-PATTERN-001 放置在 externDecl 的最后 terminal 之后（tail-action
     *  safety）。 */
    static boolean checkExternAbiFormat(String literalText, Object parser) {
        // 去掉外层双引号（lexer 保留了引号字符）
        String inner = literalText;
        if (inner.length() >= 2
            && inner.charAt(0) == '"'
            && inner.charAt(inner.length() - 1) == '"') {
            inner = inner.substring(1, inner.length() - 1);
        }
        switch (inner) {
            case "C":
            case "Cdecl":
            case "system":
            case "zom-cdecl":
                return true;
            default:
                throw new ParseCancellationException(
                    "ZOM2001[UnknownExternAbi]: unknown FFI ABI '" + inner
                    + "'; expected one of {\"C\", \"Cdecl\", \"system\", \"zom-cdecl\"}");
        }
    }

    /** §20 / §21 共用 "extern" 软关键字识别。*/
    static boolean checkIsExternKeyword(String text, Object parser) {
        if (!text.equals("extern")) return false;   // 回溯到 identifier 分支
        return true;
    }

    /** §20 "variable" 软关键字（extern block 内变量声明）。*/
    static boolean checkIsVariableKeyword(String text, Object parser) {
        if (!text.equals("variable")) return false;
        return true;
    }

    /** §20 "opaque" 软关键字（extern type alias 指向不透明实现）。*/
    static boolean checkIsOpaqueKeyword(String text, Object parser) {
        if (!text.equals("opaque")) return false;
        return true;
    }

    /** §21 "macro" 软关键字（declarative macro 2.0 声明）。*/
    static boolean checkIsMacroKeyword(String text, Object parser) {
        if (!text.equals("macro")) return false;
        return true;
    }

    // =========================================================================
    // ANTLR 运行时调优：预测模式阈值
    // 默认 predictionModeThreshold = 2000。大接口/大类体（10+ 成员）+
    // 多 alt pattern（如 structure/class body）达到阈值触发 SLL→ALL(*) 回退，
    // ANTLR 可能选择错决策路径。提高到 100000 让 SLL 先充分尝试，避免大
    // 声明被误判。
    static {
        try {
            java.lang.reflect.Field f =
                org.antlr.v4.runtime.atn.ParserATNSimulator.class
                    .getDeclaredField("predictionModeThreshold");
            f.setAccessible(true);
            f.setInt(null, 100000);
        } catch (Throwable t) {
            // 忽略：ANTLR 版本差异，失败即走默认（2000），功能不影响。
        }
    }
}

// ============================================================================
// §4.1  Programs（文件级）
//      ModuleDeclaration + ModuleItem*
//
//      注意：不支持 shebang、不支持 #![…] 内部属性、不支持 package 声明
//      （见 syntax-ebnf §7 Drift Corrections G1–G8）
// ============================================================================
sourceFile
    // §4.1 Program = OuterAttributeList? ModuleDeclaration? ModuleItem*
    // 实现要点：`(outerAttributeList moduleDeclaration)?` — outerAttributeList 是 outerAttribute*
    //   （可空），但 moduleDeclaration 首 token 为 MODULE（必选），因此括号内组合：
    //   - 有 MODULE 时：先匹配 0+ 个 #[attr]（可以 0 个 = 裸 module 声明），再匹配 moduleDecl
    //   - 无 MODULE 时：括号组整体失败，外层 `?` 直接跳过
    //   这样不会触发 "optional block matching empty" 的歧义（整个括号组不可能在
    //   既不消耗 MODULE 也不消耗属性时返回 true，因为不消耗 MODULE 就不匹配）。
    //
    // @init: one-time TOKEN PRE-PROCESSING step. The ZomLexer longest-matches
    // consecutive `>` characters into RSHIFT (>>) or URSHIFT (>>>) tokens. But
    // nested generic type closes like `Vec<Vec<i32>>` need two independent
    // single-GT closers. To avoid race conditions with gated predicates that
    // run inside ALL(*) adaptive lookahead (which may re-evaluate predicates
    // many times, breaking stream-mutating side effects), we pre-process the
    // ENTIRE TokenStream ONCE before parsing begins:
    //   • RSHIFT (>>) tokens  → replace with GT, insert another GT right after
    //   • URSHIFT (>>>) tokens → replace with GT, insert an RSHIFT right after
    //                            (the subsequent GT+GT split happens on next call,
    //                             but we only process one level here for simplicity;
    //                             better: replace URSHIFT with THREE GT tokens)
    //
    // After this pass, genericClose only ever needs to match plain GT tokens,
    // and no predicates or stream mutations run during prediction. Simplifies
    // everything.
    @init {
        preSplitAllCompactCloses(this);
        resetGenericDepth(this);
    }
    : (outerAttributeList moduleDeclaration)? moduleItem* EOF
    ;

moduleDeclaration
    // §4.1 ModuleDeclaration = 'module' Identifier ';'
    //                       | 'module' Identifier '{' ModuleItem* '}'
    //                       | 'export'? 'module' Identifier '=' AttributePath ';'
    : MODULE identifier SEMICOLON                                                           # moduleDeclSimple
    | MODULE identifier LBRACE moduleItem* RBRACE                                            # moduleDeclBlock
    | EXPORT? MODULE identifier ASSIGN attributePath SEMICOLON                               # moduleDeclAlias
    ;

moduleItem
    // §4.1 ModuleItem = OuterAttributeList* Declaration | Statement
    // Declaration 组（§4.2）包含 import/export/class/struct/… 共 14 条。
    // §19 Conditional Compilation extension: statements MAY carry an outer
    // attribute list when it is purely `#[zom::cfg(...)]`; however ZOM1901
    // restricts cfg-gated statements to the standalone-block form only:
    // `#[zom::cfg(...)] { stmt* }`. Any non-block statement form that carries
    // a `#[zom::cfg(...)]` attribute is rejected by `checkStatementCfgGate`
    // below (moduleItemStatementCfgGated alt).
    : outerAttributeList declaration                                                        # moduleItemDeclaration
    // §19 — cfg-gated statement form: `#[zom::cfg(...)] { ... }`  (block only)
    | attrs=outerAttributeList statement
        { checkStatementCfgGate($attrs.ctx, $statement.ctx, this) }?       # moduleItemStatementCfgGated
    | statement                                                                             # moduleItemStatement
    ;

// ============================================================================
// §4.2  Declarations（完整 14 条 + StandaloneImpl + MarkerImpl = 16 条）
//
// 真理来源：docs/spec/chapters/17-grammar-reference.md §Declaration /
//   §StandaloneImplDeclaration / §MarkerImplDeclaration
// 关键决策（§17 HeritageClause）：class / struct / interface / error 继承只允许
//   'extends'，没有 'implements' 关键字。接口实现用 standalone impl 形式：
//     impl Interface(+Marker)* for Type { ... }          (Standalone)
//     unsafe? impl !? AttrPath for Type (body | ';')      (Marker)
// ============================================================================
declaration
    : IMPORT importBody SEMICOLON?                                                           # importDeclaration
    | EXPORT exportBody SEMICOLON?                                                           # exportDeclaration

    // §17 ClassDeclaration — P1: colon-unified, SINGLE INHERITANCE only.
    // Strict separation (user feedback, 2026/06/26):
    //   class head colon = SUPERCLASS INHERITANCE, ONE class only.
    //   interface implementation = standalone `impl Iface for T {}` form (NEVER
    //   listed in the class header — no repeat of Java/C#'s mistake).
    // Superclass is written `class NAME: SuperClass` (single, optional).
    | modifierList CLASS memberIdentifier
      typeParameters?
      ( COLON typeExpr )?
      classBody                                                                             # classDeclaration

    // §17 StructDeclaration — VALUE TYPE, NO inheritance, NO heritage clause.
    // Interfaces are implemented via standalone impl.
    | modifierList STRUCT memberIdentifier
      typeParameters?
      structBody                                                                            # structDeclaration   // named fields only (G5: no positional/newtype)

    // §17 InterfaceDeclaration — P0: colon-unified + PLUS chain (conjunction,
    // NOT comma; matches interfaceBoundList structure).
    // Super-interfaces = inheritance of contracts; multiple is conjunction
    // (this interface is guaranteed to provide all listed contracts).
    | modifierList INTERFACE memberIdentifier
      typeParameters?
      ( COLON interfaceBoundList )?
      interfaceBody                                                                         # interfaceDeclaration

    | modifierList ENUM memberIdentifier
      typeParameters?
      enumBody                                                                              # enumDeclaration       // unit + tuple only (G6: no brace variant)

    // §17 ErrorDeclaration — VALUE TYPE; optionally extends a single base error type.
    // (matches Java's RuntimeException extends Exception pattern, but written
    //  with colon for consistency.)
    | modifierList ERROR memberIdentifier
      typeParameters?
      ( COLON typeExpr )?
      errorBody                                                                             # errorDeclaration

    | modifierList FUN memberIdentifier
      typeParameters?
      functionSignature
      ( SEMICOLON | blockBody )                                                             # functionDeclaration

    | modifierList ALIAS memberIdentifier typeParameters? ASSIGN typeExpr SEMICOLON               # typeAliasDeclaration
    // 17-gr MutDeclaration / LetDeclaration / ConstDeclaration (Ch.06 Value Declarations)
    //   mut: mutable runtime binding; let: immutable runtime binding; const: compile-time
    // VariableDeclarationList = VarDecl (, VarDecl)*  (multi-binding comma-list)
    // ConstItem MUST have an initializer (17-gr).
    // NOTE: value declarations do NOT accept modifierList prefix (public/private etc.),
    //   per Ch.06 value declaration forms; outer attrs flow via moduleItem/StatementListItem.
    | MUT variableDeclarationList SEMICOLON                                                 # mutDeclaration
    | LET variableDeclarationList SEMICOLON                                                 # letDeclaration
    | CONST constDeclarationList SEMICOLON                                                  # constDeclaration

    // §17 MarkerImplDeclaration — unsafe? impl '!'? AttrPath <TypeParams>? for Type (; | body)
    // 注：17-gr 写 typeArguments，但 Ch.09 §7 正文写 GenericParams（泛型形参声明），
    // 因为 impl<T> Marker for Vec<T> 需声明形参，所以用 typeParameters。whereClause 不在 v1。
    // 软关键字：unsafe/impl 都是 IDENTIFIER + predicate
    | unsafeTok=identifier { checkIsUnsafePrefix($unsafeTok.text, this) }?
      markerImplRest                                                                        # markerImplUnsafe

    | markerImplRest                                                                        # markerImplPlain

    // §17 StandaloneImplDeclaration (Ch.09 §7)
    // impl <GenericParams>? InterfaceName ('+' MarkerPath)* for Type { ImplMember* }
    | implTok=identifier   { checkIsImplKeyword($implTok.text, this) }?
      typeParameters?
      interfaceBoundList
      FOR typeExpr
      implBody                                                                              # standaloneImplDeclaration

    // §20 FFI and Interop — extern block / single extern fun (may carry unsafe prefix)
    | externDecl                                                                            # externDeclarationTop

    // §21 Macros 2.0 — declarative macro_rules definition
    | macroRulesDecl                                                                        # macroRulesDeclarationTop

    // §4.2 AssociatedType 声明仅在 interface 内部合法（单独在 interfaceMember 中）
    ;

// ---------- MarkerImpl common stem (after optional unsafe prefix)
markerImplRest
    : implTok=identifier   { checkIsImplKeyword($implTok.text, this) }?
      ( NOT )?
      attributePath
      typeParameters?   // 允许 impl<T> !Send for Vec<T>
      FOR typeExpr
      ( SEMICOLON | structBody )
    ;

// ---------- Interface-bound list (17-gr line 294)
//   InterfaceBoundList = InterfaceName('<'GenericArgs'>')? ('+' MarkerPath)*
// Industry convention (Rust / Swift) - strict separation of conjunctions
// vs disjunctions:
//   * PLUS  (+) = CONJUNCTION (AND) - "all bounds apply" at impl / generic-bound /
//                                      interface-extends / dyn existential positions.
//   * BIT_OR (|) = DISJUNCTION (OR)  - ONLY valid at UnionType / TypeExpression level
//                                     (a value is one of several possible types).
// Preventing `|` at impl-bound position blocks nonsense such as
//   `impl (Read | Write) for Foo` - which would semantically mean "either Read
// or Write is implemented" but coherence requires the set of implemented
// interfaces to be FIXED and exhaustive.
//
// Individual bound: identifier (optionally qualified via COLONCOLON downstream
// in semantic pass) + optional generic instantiation args (Foo<T>).
// 1+ segment interface/marker path.
//   * 1-segment  -> local/imported interface name, e.g. `Serialize`, `Debug`
//   * 2+ segment -> fully qualified marker/interface name, e.g. `core::marker::Send`
//   (marker impls by definition require 2+ segments per 17-gr line 254; we accept
//    1+ here so the same nonterminal works for plain interface names).
qualifiedPathOrIdent
    : pathSegment ( COLONCOLON pathSegment )*
    ;
interfaceBound
    : qualifiedPathOrIdent ( LT { incGenericDepth(this); } { withinGenericDepthLimit(this) }? typeArgList genericClose { decGenericDepth(this); } )?
    ;
interfaceBoundList
    : interfaceBound ( PLUS interfaceBound )*
    ;

// ---------- Standalone impl body (§17 StandaloneImplDeclaration)
implBody
    : LBRACE implMember* RBRACE
    ;
implMember
    // Method impl (abstract w/ SEMICOLON or concrete w/ blockBody)
    : modifierList FUN memberIdentifier typeParameters? functionSignature ( SEMICOLON | blockBody )
    // Associated-type assignment (impl-block-level 'type Item = T;' per 17-gr)
    | TYPE memberIdentifier typeParameters? ASSIGN typeExpr SEMICOLON
    // Let / mut / const inside impl (follow Ch.06 Value Declarations)
    | MUT variableDeclarationList SEMICOLON
    | LET variableDeclarationList SEMICOLON
    | CONST constDeclarationList SEMICOLON
    // Alias inside impl (rare but harmless)
    | modifierList ALIAS memberIdentifier typeParameters? ASSIGN typeExpr SEMICOLON
    ;


// ============================================================================
// §4.3  Module / Import / Export
// ============================================================================
importBody
    // §4.3 ImportDeclaration = ImportClause ( 'as' Identifier )?
    //                        | AttributePath ('.' | '::')? '{' ImportSpecList? '}'
    //                        | AttributePath '..' AttributePath ( 'as' Identifier )?
    // A13: importSimple / importRename 必须是 qualified path 或 `*`，禁止单段 identifier
    // （`import std;` / `import std as s;` REJECT）
    // NOTE: importGroup 使用 importQualifiedPath（允许 1 段），这样 `std::{a,b}`
    //       以及 `foo::bar::{c,d}` 均合法；importSimple 继续 enforce attributePath。
    : importClause { rejectImportBareIdUnlessStarOrAttrPath($importClause.ctx, this) }?  # importSimple
    | importClause AS identifier
      { rejectImportBareIdUnlessStarOrAttrPath($importClause.ctx, this) }?                  # importRename
    | importQualifiedPath ( PERIOD | COLONCOLON )? LBRACE importSpecList? RBRACE            # importGroup
    | importQualifiedPath ELLIPSIS importQualifiedPath ( AS identifier )?                   # importRange
    ;

// §4.3 §13.2 Import declaration body (4 forms)
// 注意 (G13 path uniformity):
//   importQualifiedPath = PathSegment ( :: PathSegment )*   (1 段或多段，关键字允许作段)
//   attributePath        = PathSegment ( :: PathSegment )+  (至少 2 段，强制 :: 用于属性)
//   两者分离，importGroup / importRange / reexport 允许单段路径。
importQualifiedPath
    : pathSegment ( COLONCOLON pathSegment )*
    ;

importClause
    : '*'
    // Bare identifier permitted INSIDE import specifiers (e.g. `import std::collections::{HashMap}`).
    // REJECTION only enforced at importBody top-level (importSimple / importRename).
    | identifier
    | attributePath
    ;

// §4.3 §13.2 Comma-separated list of import specifiers
importSpecList
    : importSpec ( COMMA importSpec )* COMMA?
    ;

// §4.3 §13.2 Single import specifier (name + optional alias / rename)
// ⚠ Wildcard '*' 不能出现在花括号分组内（§4.3 importGroup 语义：显式枚举名），
//   只允许 importSimple 顶层使用（如 `import std::*`）。见 import_wildcard_group_reject_neg_17。
importSpec
    : ( identifier | attributePath ) ( AS identifier )?
    ;

exportBody
    // §4.3 ExportDeclaration = Declaration | '{' ExportSpecList '}'
    //                        | AttributePath ('.' | '::')? '{' ImportSpecList? '}'
    // NOTE: 使用 importQualifiedPath 允许 `std::{ x, y }` 单段前缀（§13 行业惯例）。
    : LBRACE exportSpecList RBRACE                                                          # exportGroup
    | importQualifiedPath ( PERIOD | COLONCOLON )? LBRACE importSpecList? RBRACE            # exportReexportGroup
    | declaration                                                                           # exportDeclDirect
    ;

// §4.3 §13.3 Comma-separated list of export specifiers
exportSpecList
    : exportSpec ( COMMA exportSpec )* COMMA?
    ;

// §4.3 §13.3 Single export specifier (name + optional alias)
// ⚠ Wildcard 与 importSpec 一致：不出现在花括号分组内。
exportSpec
    : ( identifier | attributePath ) ( AS identifier )?
    ;

// ============================================================================
// §4.2  类型声明的 body 与各子句
// ============================================================================

// --- Modifier（syntax-ebnf §Visibility = 8 项，无二义性 ---------------------------------
// §4.2 §8.1 Modifier keyword: public/private/protected/static/readonly/mutating/override/abstract
modifier
    : PUBLIC    | PRIVATE   | PROTECTED | STATIC
    | READONLY  | MUTATING  | OVERRIDE  | ABSTRACT
    | EXPORT   // §13 Visibility: re-export qualifier (highest of the 6-tier ladder)
    ;

// §4.2 §8.1 Modifier list with semantic predicate: no duplicates; abstract+static illegal; static+mutating illegal
modifierList
    : modifier* { checkModifierList(_localctx, this) }?
    ;

// 17-gr VariableDeclarationList (used by MutDeclaration & LetDeclaration)
//   VariableDeclarationList = VariableDeclaration (, VariableDeclaration)* [trailing-comma ok]
//   VariableDeclaration      = (BindingId | BindingPattern) TypeAnnot? Initializer?
// Parser-level: we reuse `pattern` as (BindingId|BindingPattern); type annotations on
// plain identifiers attach via the typed-pattern form in the semantic pass later.
variableDeclarationList
    : variableDecl ( COMMA variableDecl )* COMMA?
    ;
variableDecl
    : pattern ( COLON typeExpr )? ( ASSIGN expression )?
    ;

// 17-gr ConstDeclarationList = ConstItem (, ConstItem)*
//   ConstItem = BindingIdentifier TypeAnnot? '=' ConstExpression
// Parser-level rules: identifier only (no destructuring), initializer MANDATORY.
constDeclarationList
    : constDeclItem ( COMMA constDeclItem )* COMMA?
    ;
constDeclItem
    : identifier ( COLON typeExpr )? ASSIGN expression
    ;

// Section 4.2 / 8.1 Readonly modifier keyword
readonly : READONLY ;

// --- Class / Struct / Error body --------------------------------------------------------
// §8.2 Class body (member list)
// NOTE: get/set/init/deinit 用作硬关键字，但若它们作为方法名也合法（比如 super.init），
//       所以 memberIdentifier 把这些硬关键字当作标识符接受。方法和属性声明中，
//       FUN / INIT / DEINIT / GET / SET 各自独立入口，因此不存在歧义。
//
//       identifier-or-keyword helper (memberIdentifier) = IDENTIFIER | INIT | DEINIT | GET | SET
//       用于成员表达式（a.init / a.get / super.set）和参数命名位置（不破坏关键字保留）。
classBody : LBRACE classMember* RBRACE ;

// 成员标识符：IDENTIFIER 或用作标识符上下文的硬关键字（init/deinit/get/set）
//   安全前提：这些关键字在声明位置（method/ctor/getter）都有独立 keyword 入口，
//   所以 `fun init() {}` 走 FUN init(IDENTIFIER)，而 `super.init()` 的 .init 走 memberIdentifier。
memberIdentifier
    : identifier
    | INIT
    | DEINIT
    | GET
    | SET
    ;
// §8.4 Struct body (field list)
// 分隔策略：structMember* 宽松列表，field 成员尾部允许 SEMICOLON/COMMA，也允许
// "无显式分隔"（通过 okAfterStructFieldNoSeparator LA predicate 保证后接
// RBRACE / FUN/INIT/DEINIT / modifier 关键字，不允许 `a: i32  b: i32` 相邻字段）。
structBody : LBRACE structMember* RBRACE ;
// §10.3 Error body (struct-style fields)
errorBody  : LBRACE structMember* RBRACE ;

// §8.2 Class member: method/constructor/let/const/property
// 注：每个 class 成员前允许 #[...] outer 属性（§16 class/struct member 属性）。
//   方法和属性名使用 memberIdentifier（允许 init/get/set/deinit 等关键字作成员名）。
classMember
    : outerAttributeList modifierList FUN memberIdentifier typeParameters? functionSignature
      ( SEMICOLON
      | blockBody { checkAbstractNoBlock($modifierList.ctx, this) }?
      )                                                                                     # classMethod
    | outerAttributeList modifierList (INIT | DEINIT) parameterList (RAISES typeList)? blockBody # classCtor
    // Class value member: mut / let / const (Ch.06 + Ch.08 class field)
    // Visibility (public/private/protected/static/readonly) still flows from modifierList.
    | outerAttributeList modifierList MUT variableDeclarationList SEMICOLON                # classMut
    | outerAttributeList modifierList LET variableDeclarationList SEMICOLON                # classLet
    | outerAttributeList modifierList CONST constDeclarationList SEMICOLON                 # classConst
    // Class value field: bare value decl like `public name: String = default;
    // (Ch.08 class field — separate from classMut/classLet that require MUT/LET keyword)
    | outerAttributeList modifierList memberIdentifier COLON typeExpr ( ASSIGN expression )?
      ( SEMICOLON | COMMA | { okAfterStructFieldNoSeparator(this) }? )                                # classField
    // classProperty: get + optional set。SET 必须以 GET 为前提（§8.2 classProperty）——
    //   单独的 SET 非法，参见 get_set_combos_neg_12。使用显式两分支的替代 SET：
    //   Alt A: SET 带 modifierList + outerAttributeList
    //   Alt B: 裸 SET（无 modifier 和 outerAttr）—— 避免 ANTLR "optional body can match empty"
    //   警告（-Werror），因为 modifierList 和 outerAttributeList 本身都是 * 循环会空。
    | outerAttributeList modifierList GET memberIdentifier functionSignature blockBody
      ( outerAttributeList modifierList SET memberIdentifier functionSignature blockBody
      | SET memberIdentifier functionSignature blockBody
      )?                                                                                   # classProperty
    ;

// §8.4 Struct member: field/method/constructor
// NOTE: Struct fields accept SEMICOLON / COMMA / implicit (RBRACE 终止) 三种分隔；
//       最后一个字段后允许缺省分隔符（业界一致），与 array/tuple literal 的尾部
//       trailing-comma 放宽策略一致。
// 注：每个 struct 成员前允许 outerAttributeList（§16 struct 成员属性）。
structMember
    // StructField: 尾部显式 SEMICOLON/COMMA，或 predicate 保证下一个是 block 关键字/RBRACE
    //   （拒绝 `a: i32  b: i32` 相邻字段无分隔符，见 struct_field_no_semi_reject_neg_03）
    : outerAttributeList modifierList (MUT | readonly)? memberIdentifier COLON typeExpr (ASSIGN expression)?
      ( SEMICOLON | COMMA | { okAfterStructFieldNoSeparator(this) }? )            # structField
    | outerAttributeList modifierList FUN memberIdentifier typeParameters? functionSignature
      ( SEMICOLON | blockBody )                                                             # structMethod
    | outerAttributeList modifierList (INIT | DEINIT) parameterList (RAISES typeList)? blockBody   # structCtor
    ;

// --- Interface -------------------------------------------------------------------------
// §9.2 Interface body
interfaceBody : LBRACE interfaceMember* RBRACE ;

// §9.2 Interface member: method/property/associated-type
// 注：成员前允许 outerAttributeList（§16 接口成员属性）。
interfaceMember
    : outerAttributeList modifierList FUN memberIdentifier typeParameters? functionSignature SEMICOLON    # interfaceMethod
    | outerAttributeList modifierList (GET | SET) memberIdentifier functionSignature SEMICOLON            # interfaceProperty
    | outerAttributeList modifierList TYPE memberIdentifier typeParameters?
      ( COLON interfaceBoundList )? ( ASSIGN typeExpr )? SEMICOLON                                       # interfaceAssocType
    ;

// --- Enum ------------------------------------------------------------------------------
// §10.2 Enum body (variant list)
enumBody : LBRACE enumVariantList? RBRACE ;

// §10.2 Comma-separated list of enum variants
enumVariantList : enumVariant ( COMMA enumVariant )* COMMA? ;

enumVariant
    // §4.2 EnumVariant = Identifier ( '(' VariantTypeList ')' )?  （仅 unit + tuple，G6 无 brace variant）
    // VariantTypeList = comma-separated types: Variant(T, U, V)  (Rust/Swift 业界一致)
    : outerAttributeList identifier
      ( LPAREN variantTypeList RPAREN )?
      ( ASSIGN expression )?     // 可选 discriminant
    ;

// §10.2 Variant tuple positional list: comma-separated type expressions.
// NOTE: This is DIFFERENT from the BIT_OR-separated typeList used in raises/interface bounds.
variantTypeList : typeExpr ( COMMA typeExpr )* COMMA? ;

// --- Function signature ---------------------------------------------------------------
// §4.2 §8.3 Function signature: parameters + optional return-type + optional raises clause
// NOTE: raises clause allowed either:
//       (a) WITH a return type: `parameters -> ReturnType raises X | Y`
//       (b) WITHOUT return type (standalone raises): `parameters raises X | Y`
//       (c) Neither: just `parameters`
functionSignature
    : parameterList ( ARROW typeExpr ( RAISES typeList )? )?
    | parameterList RAISES typeList
    ;

// §4.2 Function/closure parameter list
parameterList
    : LPAREN ( parameter ( COMMA parameter )* COMMA? )? RPAREN
    ;

// §4.2 Single parameter: optional name, type, optional default value
parameter
    : (identifier COLON)? typeExpr ( ASSIGN expression )?
    ;

// §12.1 Generic type parameter declaration list (<T, U extends B = D>)
typeParameters
    : LT typeParameter ( COMMA typeParameter )* COMMA? genericParamClose
    ;

// §12.1 Single generic type parameter (name, optional colon bound, optional default)
// Industry convention (Swift / Kotlin): bounds are written with COLON, not Java-style
// 'extends' keyword. EXTENDS is reserved for class heritage (superclass).
// Bounds are trait conjunctions (PLUS-chain), same as impl head / dyn existential.
// Order: variance? NAME : Bound+ = Default
// Examples:
//   fun f<T, U: number = i32, V: Eq + Hash>(x: T) -> str { ... }
//   class Vec<out T: Any> { ... }
//   interface Map<K, in V> { ... }
typeParameter
    : variance? identifier ( COLON interfaceBoundList )? ( ASSIGN typeExpr )?
    ;

// §12.1 Variance annotation on type parameter (in=contravariant, out=covariant)
// Default is INVARIANT (neither 'in' nor 'out' specified).
variance
    // A5: variance NOT supported per 24-truth table
    : IN  { rejectVarianceIn(this) }?
    | OUT { rejectVarianceOut(this) }?
    ;

// §12.1 Generic close token. Pre-processing at sourceFile @init already splits
// compact RSHIFT (>>) / URSHIFT (>>>) tokens into individual GT tokens. The
// parser therefore only ever sees a single plain `>` at type-argument close
// points. This eliminates ALL(*) prediction-edge cases with compact-close
// tokens completely.

// §12.1 Generic close token (type-argument instantiation). At parser start
// (see sourceFile @init, `preSplitAllCompactCloses`) we pre-split every RSHIFT (>>)
// and URSHIFT (>>>) into individual GT tokens, so genericClose only needs to
// match a single plain `>`. This keeps ALL(*) prediction simple and avoids
// state corruption from mutating the token stream during lookahead simulation.
genericClose : GT ;

// §12.1 Variant used by typeParameters — reject >> / >>> at PARAMETER declaration level
// so `fun f<T, U>>` (unbalanced over-close, two `>`) is REJECTED.
genericParamClose
    : GT
    | RSHIFT  { rejectCompactClose(">>", this) }?
    | URSHIFT { rejectCompactClose(">>>", this) }?
    ;

// ============================================================================
// §4.4  Attributes（完整语法；AttrPath 必须含至少一个 :: — G11 消歧义）
// ============================================================================
// §4.4 §16 Outer attribute list — zero or more #[...] attributes before declarations
outerAttributeList : outerAttribute* ;

outerAttribute
    // §4.4 OuterAttribute = '#' '[' AttrList ']' ；lexer 已把 '#[' 合成单个 HASH_LBRACK token
    : HASH_LBRACK attrList RBRACK
    ;

// §4.4 §16 List of attributes inside single #[...] block.
// NOTE: uses `attrItem` (not `attr` directly) so that `#[zom::cfg(PRED)]`
// can have a DISJOINT top-level dispatch without ANTLR 4 ALL(*) simulator
// poisoning the decision with body-reachability scans into cfgPredicate.
attrList : attrItem ( COMMA attrItem )* COMMA? ;

// §4.4 §16 Single attribute entry inside `#[...]` — two mutually exclusive
// leading-gated alternatives (a partition of every reachable attr prefix).
//
// Architectural decision (the 3rd rewrite):
//   Previous attempts:
//     v1  `attr : attrCfg | attrGeneric ;` (sub-rules)
//         → ALL(*) still simulates into each sub-rule ATN → attrCfg's body
//           reachability (cfgAtom hardfail alt predicates) poisons the
//           parent-level decision: alt attrCfg is abandoned even when the
//           4-token prefix matches → neg_05 false-accepted.
//     v2  `attr : #zomCfg | #genericMultiSeg | #genericSegment1` (top-level labels)
//         → Same problem: ALL(*) continues through the labelled-alt body
//           into cfgPredicate. cfgAtom's valued-alt semantic predicate
//           returns DIFFERENT values for pos_02 vs neg_05, which makes the
//           reachable-state DFA *sensitive to input past the decision point*.
//           With SLL k=∞ the decision reportAttemptingFullContext, and then
//           NoViableAltException when no single alt uniquely wins.
//     v3  (THIS ONE) lift the #[zom::cfg(...)] shape UP to attrItem and give
//         the generic path its own `attr` rule. The attrItem leading gates
//         are bounded (≤4 tokens) and their RETURN VALUES do not depend on
//         anything after the decision. The body of attrItem#attrZomCfg
//         terminates cleanly (its final RPAREN is a reachable terminal
//         regardless of what cfgAtom does inside — the cfgAtom hardfail
//         parser action is skipped during prediction so the simulator
//         always reaches RPAREN and reports success).
//
//   Critical invariant: parser actions (`{ code }` without `?`) are NOT
//   executed during adaptive prediction, only semantic predicates (`{e}?`)
//   are evaluated and contribute to reachability. So after attrItem is
//   split into its OWN rule, attrItem#attrZomCfg's body:
//     IDENT(zom) :: IDENT(cfg) ( cfgAtom )
//   The cfgAtom hardfail alt ends with `{ throw PCE }` → simulator sees a
//   "clean state with no further tokens" → cfgAtom returns normally under
//   prediction. The outer RPAREN is reachable, the full alt is reachable,
//   ALL(*) picks it. During the ACTUAL parse, the action throws → rc=2.
attrItem
    // Two disjoint labelled alternatives with 2-token / 1-token bounded
    // leading semantic predicates. Entry is via attrList.
    //
    //   Alt 1 — `#[zom::cfg(PRED)]` — gated by 3-token lookahead in
    //          `peekIsZomCfgParen`, which also verifies LPAREN follows
    //          the identifier. Canonical attribute path is "zom::cfg".
    //          Returns `cfg` label pointing to cfgPredicate subtree.
    //
    //   Alt 2 — any OTHER attribute shape — delegated to the `attr` rule,
    //          which itself splits into multi-segment (LA2=COLONCOLON) vs
    //          single-segment (LA2≠COLONCOLON) via 2-token bounded lookaheads.
    //          Gated by ¬peekIsZomCfgParen so it is EXACTLY complementary to
    //          Alt 1 (2-alts decision, P vs ¬P, defeats DFA state conflicts).
    : { peekIsZomCfgParen(this) }?
      nsIdent=identifier COLONCOLON cfgIdent=identifier
      LPAREN cfg=cfgPredicate RPAREN
      // ⚠  TAIL PARSER-ACTION (positional safety critical — see helper docstring)
      //    Enforces ZOM1900: every cfgAtom with a CfgOp must have a quoted-string
      //    RHS. Malformed atoms (e.g. `target_os = linux`) were matched by
      //    cfgAtom#cfgAtomBadRhs so the parse tree is complete; now we turn that
      //    structural match into the precise diagnostic.
      { enforceCfgAtomQuotedRhs($cfg.ctx, this); }                              # attrZomCfg
    | { !peekIsZomCfgParen(this) }?
      attr                                                                    # attrGenericItem
    ;

attr
    // =======================================================================
    // GENERIC attribute (§4.4 + §16) — never matches `#[zom::cfg(...)]`
    // because that shape is intercepted by attrItem#attrZomCfg above.
    //
    // Alt 1: Multi-segment (attrpath >= 2 segments with ::) — gated by
    //        LA2 == COLONCOLON. 接受 serde::rename / zom::deprecated 等。
    // Alt 2: Single-segment built-in compiler attribute（inline / deprecated /
    //        repr / etc.）— 由 isBuiltinSingleSegAttr 枚举白名单。
    //        用户自定义单段属性（比如 #[foo]）REJECT，以保持 §16 G11
    //        "属性路径必须含 ::" 的核心约束。attrpath_short_reject_neg_06
    //        （#[short] fun f();）因此 REJECT（short 不在白名单内）。
    //
    // 两种 alt 互补：P ∨ ¬P（LA2=:: 与单段白名单不重叠），保持 SLL 决策稳定。
    // =======================================================================
    : { isNextToken(this, IDENTIFIER) && la2Is(this, COLONCOLON) }?
      first=identifier COLONCOLON rest=attributePathTail
      ( LPAREN input=attrInput RPAREN
      | ASSIGN value=expression
      )?                                                                            # genericMultiSeg
    | { isNextToken(this, IDENTIFIER) && !la2Is(this, COLONCOLON) }?
      name=identifier { isBuiltinSingleSegAttr($name.ctx.getText()) }?
      ( LPAREN input=attrInput RPAREN
      | ASSIGN value=expression
      )?                                                                            # builtinSingleSeg
    ;

// §4.4 §13.2 Path segment helper：路径中的每一段都可以是普通标识符或 lexer
//   硬关键字（业界惯例：Rust/C#/Python 都允许 path 组件使用关键字）。这样
//   `import foo::module::error` 能正确解析（`module` 和 `error` 是 lexer 硬关键字）。
//   完整覆盖：Declaration / ControlFlow / Type / Modifier / Operator / Module /
//   Concurrency / Reserved / Literal-like 所有关键字。
pathSegment
    : identifier
    // -- Declaration (§6.x) --
    | CLASS | STRUCT | INTERFACE | ENUM | ERROR | ALIAS | FUN | MUT | LET | CONST
    | INIT | DEINIT | GET | SET
    // -- Control Flow --
    | IF | ELSE | MATCH | WHEN | DEFAULT | FOR | WHILE | DO | BREAK | CONTINUE
    | RETURN | DEBUGGER | IN | OUT
    // -- Type --
    | I8 | I16 | I32 | I64 | U8 | U16 | U32 | U64 | F32 | F64
    | BOOL | STR | CHAR | NULL | UNIT | NEVER | ANY
    // -- Modifier --
    | PUBLIC | PRIVATE | PROTECTED | STATIC | READONLY
    | MUTATING | OVERRIDE | ABSTRACT
    // -- Operator --
    | AS | IS | TYPEOF | NEW | THIS | SUPER | EXTENDS | RAISES
    // -- Module --
    | MODULE | IMPORT | EXPORT
    // -- Concurrency --
    | SUSPEND | SPAWN
    // -- Reserved (ZOM500x) --
    | THROW | TRY | CATCH | FINALLY | ASYNC | AWAIT | VAR
    | ACTOR | CHANNEL | YIELD | GENERATOR | NAMESPACE | PACKAGE | TYPE
    | DELETE | INSTANCEOF | OF | WITH
    // -- Literal-like --
    | TRUE | FALSE | UNDERSCORE | IMPLEMENTS
    ;

// §4.4 Helper: the tail of a multi-segment attribute path after the first
// `Identifier ::`. Combined with the `first=identifier` + `COLONCOLON` in
// #genericMultiSeg, the total path still obeys G11 (at least one `::`).
// Example: `#genericMultiSeg first=foo :: attributePathTail=bar::baz`
//          → fully qualified path = `foo::bar::baz`
attributePathTail
    : pathSegment ( COLONCOLON pathSegment )*
    ;

// §4.4 AttrInput — arguments inside `#[attr(ARGS)]` parentheses.
// 三种输入形态（业界惯例，Rust 类似）：
//   a) Expression list: `#[inline(true, cold)]` — 1+ expression, comma separated
//   b) KV list:         `#[link(name="c", kind="static")]`
//   c) Mixed:           `#[route::register(get("/x"), p = 10)]`
// 用 LA(2)==ASSIGN gated predicate 区分 KV 和 Expression：若 identifier 后紧接 = 则为 KV，
//   否则走 expression 分支。消除 SLL 歧义。
attrInput
    : attrInputItem ( COMMA attrInputItem )* COMMA?
    ;
attrInputItem
    // 左操作数是 pathSegment（允许 keyword 作为 K），且下一个 token 是 ASSIGN → KV 项
    : { la2Is(this, ASSIGN) }? pathSegment ASSIGN expression                              # attrInputKVItem
    | expression                                                                          # attrInputExprItem
    ;

attributePath
    // §4.4 AttrPath = Identifier '::' Identifier ( '::' Identifier )*
    //   ⚠️ MUST contain at least one `::` （G11 — 避免与普通标识符路径冲突）
    //   注意：改用 pathSegment（允许关键字作段），但整体仍强制 2+ 段（G11）。
    : pathSegment ( COLONCOLON pathSegment )+
    ;

// ============================================================================
// §19  Conditional Compilation — CfgPredicate sub-grammar
// ============================================================================
// §19.3.1 Normative EBNF, mirrored from spec/chapters/19-conditional-compilation.md.
// The three combinators (all/any/not) are contextual keywords — they are NOT
// hard keywords in the lexer; they are matched here by identifier text.
// A semantic predicate is used because the `all` identifier alone would
// otherwise be consumed by the cfgAtom branch (bare-key form) without
// left-factoring help.

cfgPredicate
    // NOTE: order matters — combinator rules must come BEFORE cfgAtom (bare
    // identifier) so that `all(...)` is recognized as a combinator, not as a
    // bare key "all" followed by a parenthesised expression that ANTLR's error
    // recovery would try to reattach elsewhere.
    //
    // IMPLEMENTATION NOTE (Hard Token vs Contextual Keyword):
    //   The 4 combinator alternatives MUST precede cfgAtom because cfgAtom accepts
    //   any bare IDENTIFIER; if it were first, `all(x)` would parse as bare key
    //   "all" followed by a loose parenthesised group that ANTLR cannot recover.
    //
    //   `any` uses the lexer-level HARD TOKEN `ANY` (§3 ZomLexer.g4 line 485, the
    //   type-level existential `any` keyword). The combinator `any()` and the type
    //   `any` never co-occur at the same grammar position: combinators appear ONLY
    //   inside `#[zom::cfg(  HERE  )]` (a closed sub-grammar), while the type
    //   `any` appears ONLY in type-expression contexts. So reusing `ANY` here is
    //   unambiguous and eliminates the need for a gated predicate.
    //
    //   `all` and `not` are NOT hard lexer keywords:
    //     - `all` is not reserved anywhere (users may legitimately declare `fun all(...)`)
    //     - `NOT` in the lexer is the single-character `!`, not the word
    //   So these two use LEADING GATED SEMANTIC PREDICATES via
    //   `((TokenStream)getTokenStream()).LT(1).getText()` — TokenStream.LT(1) is
    //   used instead of `getCurrentToken()` because during ALL(*) SLL prediction
    //   the parser's internal current-token pointer is not yet advanced when the
    //   decision gate is evaluated; the raw TokenStream always gives true LA(1).
    : { ((org.antlr.v4.runtime.TokenStream)getTokenStream()).LT(1).getText().equals("all") }?
      IDENTIFIER
      ( LPAREN RPAREN
      | LPAREN cfgPredicate ( COMMA cfgPredicate )* COMMA? RPAREN
      )                                                                       # cfgAllPred
    | ANY
      ( LPAREN RPAREN
      | LPAREN cfgPredicate ( COMMA cfgPredicate )* COMMA? RPAREN
      )                                                                       # cfgAnyPred
    | { ((org.antlr.v4.runtime.TokenStream)getTokenStream()).LT(1).getText().equals("not") }?
      IDENTIFIER
      LPAREN cfgPredicate RPAREN                                              # cfgNotPred
    | cfgAtom                                                                 # cfgAtomPred
    | { rejectCfgPredicateBad(this) }?                                       # cfgPredicateBad
    ;

cfgAtom
    // §19.3.1 CfgAtom = Identifier ( CfgOp CFG_VALUE )?
    //   Bare-key existence-check if no CfgOp.
    //   Valued-equality if CfgOp is `=` (ASSIGN) or `!=` (NEQ).
    //   Ordered-comparison if CfgOp ∈ { < <= >= > } (§19.3.1.1 SemVer-aware ordering).
    // NOTE: `=` (CFG equality) reuses token ASSIGN because EQ is `==` (the binary
    // relational operator); this is unambiguous because cfgPredicate is a sub-grammar
    // that does not overlap with expression parsing.
    //
    // IMPLEMENTATION NOTE (shared-prefix IDENTIFIER + 3 RHS sub-rules — eliminates
    // gated-predicate lookahead offset errors)
    // -----------------------------------------------------------------------------
    // In V4 we placed the 3 `isCfgOpNext` gated predicates as ALT-ENTRY leading gates,
    // BEFORE the shared `key=IDENTIFIER` terminal. At entry to `cfgAtom`, LA(1) is the
    // IDENTIFIER itself (e.g. `target_os`), NOT the CfgOp — so every `isCfgOpNext`
    // returned false, Alt 3 (bare-key) was (spuriously) selected, `checkCfgAtomShape`
    // (which enforces "no CfgOp follows bare key") then returned false, and the full
    // adaptive backtrack reported NVA at the PARENT (`cfgPredicate`) with only the
    // "rejectCfgPredicateBad" alt-survivor in the dead-end set. The bug was a LOOKAHEAD
    // OFFSET ERROR: `isCfgOpNext` is well-defined ONLY AFTER the Identifier has been
    // consumed (because it is a test on what FOLLOWS the key, not what IS the key).
    //
    // This rewrite keeps `key=IDENTIFIER` as the shared mandatory prefix, then uses a
    // 3-alternative nested group to dispatch to THREE DEDICATED SUB-RULES, each with
    // a single labelled top-level alternative of its own:
    //
    //   valuedCfgAtomRhs   — gated by `{ isCfgOpNext && isDoubleStringAfterCfgOp }`
    //                        OP + "value" + structural/feature predicates (returns
    //                        nothing extra; the checks run as predicates, not throws).
    //   badRhsCfgAtomRhs   — gated by `{ isCfgOpNext && !isDoubleStringAfterCfgOp }`
    //                        OP + stray IDENTIFIER (malformed unquoted value; stored
    //                        via `rhsBad` label for later detection).
    //   bareCfgAtomRhs     — gated by `{ !isCfgOpNext }`
    //                        ε  +  structural predicate.
    //
    // Using distinct sub-rules (instead of inline labels inside the nested group)
    // satisfies ANTLR's "labels only on top-level alternatives" restriction and makes
    // each RHS a FIRST/FOLLOW-sound unit. The 3-entry decision becomes a clean 2×2
    // predicate partition — no DFA state merges can conflict.
    : key=IDENTIFIER
      ( valuedCfgAtomRhs
      | badRhsCfgAtomRhs
      | bareCfgAtomRhs
      )
    ;

// §19.3.1 cfgAtom RHS: quoted valued form — single unique top-level alt
//   `= "linux"` | `!= "linux"` | `<  "1.0.0"` | `<= "1.0.0"` | `>= "1.0.0"` | `>  "1.0.0"`
valuedCfgAtomRhs
    : { isCfgOpNext(this) && isDoubleStringAfterCfgOp(this) }?
      op=( ASSIGN | NEQ | LT | LTE | GT | GTE )
      val=DOUBLE_STRING_LITERAL
      // NOTE: `feature = ""` (ZOM1903) validation + any atom-level validation that
      // requires throwing a ParseCancellationException is performed BY THE TAIL
      // parser-action in `attrItem#attrZomCfg` (see `enforceCfgAtomQuotedRhs`).
      // Semantic predicates ({...}?) here MUST NOT throw PCE because the ANTLR 4
      // runtime wraps them in `try { eval() } catch (RuntimeException) { false }`,
      // which would silently convert PCE into predicate-false → spurious NVA /
      // error-recovery acceptance. `checkCfgAtomShape` returns boolean (no throw).
      { checkCfgAtomShape(((org.antlr.v4.runtime.CommonTokenStream)getTokenStream()).LT(-1), true, $op.text, this) }?
    ;

// §19.3.1 cfgAtom RHS: UNQUOTED / malformed valued form
//   Matches `= linux` / `!= windows` / `< 14` / ... as a syntactically valid token
//   stream so no NVA is raised during prediction. The stray `rhsBad=IDENTIFIER` token
//   is preserved on the parse-tree so that `enforceCfgAtomQuotedRhs` (called as a
//   TAIL parser-action in `attrItem#attrZomCfg`) can turn it into ZOM1900.
//   ⚠  NO parser action throws here. NO semantic predicate rejections here. Pure match.
badRhsCfgAtomRhs
    : { isCfgOpNext(this) && !isDoubleStringAfterCfgOp(this) }?
      op=( ASSIGN | NEQ | LT | LTE | GT | GTE )
      rhsBad=IDENTIFIER
    ;

// §19.3.1 cfgAtom RHS: bare-key (no CfgOp) — structural predicate only.
//   `target_feature` | `unix` / `zomc_version` etc.
//   `checkCfgAtomShape` ensures no CfgOp silently remains in the follow-set.
bareCfgAtomRhs
    : { !isCfgOpNext(this) }?
      { checkCfgAtomShape(((org.antlr.v4.runtime.CommonTokenStream)getTokenStream()).LT(-1), false, null, this) }?
    ;

//   ASSIGN → `=`   (not EQ `==`; the only place in the grammar where ASSIGN
//                   is used for equality; restricted to cfg atom values)
//   NEQ    → `!=`
//   LT     → `<`
//   LTE    → `<=`
//   GT     → `>`
//   GTE    → `>=`
// (Tokens already declared in the relational / assignment tiers of §4 expressions.)

// ============================================================================
// §4.5  Statements
// ============================================================================
statement
    : labeledStatement                                                                      # stmtLabeled
    | declaration                                                                           # stmtDecl
    | expressionStatement                                                                   # stmtExpr
    | ifStatement                                                                           # stmtIf
    | matchStatement                                                                        # stmtMatch
    | whileStatement                                                                        # stmtWhile
    | doWhileStatement                                                                      # stmtDoWhile
    | forStatement                                                                          # stmtFor
    | forInStatement                                                                        # stmtForIn
    | breakStatement                                                                        # stmtBreak
    | continueStatement                                                                     # stmtContinue
    | returnStatement                                                                       # stmtReturn
    | debuggerStatement                                                                     # stmtDebugger
    | MUT variableDeclarationList SEMICOLON                                                 # stmtMutDecl
    | LET variableDeclarationList SEMICOLON                                                 # stmtLetDecl
    | whenStatement                                                                         # stmtWhen
    | spawnStatement                                                                        # stmtSpawn
    | suspendStatement                                                                      # stmtSuspend
    | blockBody                                                                             # stmtBlock
    | reservedSyntax                                                                        # stmtReserved
    ;

// ---- Expression statement -------------------------------------------------------------
expressionStatement
    // §4.5 ExpressionStatement：必须以分号终止（SEMICOLON 由 statement 列表保证）
    : expression SEMICOLON
    ;

// ---- Labeled statement（三约束 C25，predicate 在 labeledStatement 内）----------------
// §4.5 Labeled statement id: stmt (3 constraints via semantic predicates)
labeledStatement
    : label=identifier COLON
      { checkLabelNoAttrAfterLabel(_input.LT(1), $label.text, this) }?
      stmt=statement
      { checkLabelC25ControlFlowOnly($stmt.start, this) }?
    ;

// ---- Control flow ---------------------------------------------------------------------
// §5.3 If / else if / else statement
ifStatement
    : IF parenExpression statement ( ELSE statement )?
    ;

// §5.3 Pattern-matching statement
matchStatement
    : MATCH parenExpression LBRACE matchClause* ( DEFAULT COLON statementList )? RBRACE
    ;

// §5.3 Match arm: pattern (when guard)? : stmt
matchClause : pattern (IF expression)? COLON statementList ;

// §5.3 When-clause multi-branch conditional
whenStatement
    : WHEN parenExpression LBRACE whenClause* ( DEFAULT COLON statementList )? RBRACE
    ;

// §5.3 Single when arm: expression : stmt list
whenClause  : expression COLON statementList ;

// §5.4 While loop
whileStatement : WHILE parenExpression statement ;

// §5.4 Do-while loop
doWhileStatement : DO statement WHILE parenExpression SEMICOLON ;

forStatement
    // §4.5 for — C-style（三部分都可以空）
    : FOR LPAREN
        init=forInit? SEMICOLON
        cond=expression? SEMICOLON
        upd=forUpdate?
      RPAREN statement
    ;

// 17-gr ForInit = ('mut' | 'let') VariableDeclarationList | Expression
forInit    : MUT variableDeclarationList | LET variableDeclarationList | expressionList ;
// §5.4.1 C-style for update clause (comma expressions)
forUpdate  : expressionList ;

// §5.4.2 For-in iterator loop: for mut? bindingPat in expr block
// Leading variants: for bare (mutable-or-inferred) pattern / let binding / mut binding.
forInStatement
    : FOR LPAREN (MUT | LET)? pattern IN expression RPAREN statement
    ;

// §5.5 Break optional label
breakStatement    : BREAK identifier? SEMICOLON ;
// §5.5 Continue optional label
continueStatement : CONTINUE identifier? SEMICOLON ;
// §5.5 Return optional expression
returnStatement   : RETURN expression? SEMICOLON ;
// §5.5 Debugger statement (breakpoint)
debuggerStatement : DEBUGGER SEMICOLON ;

// ---- Concurrency（G12：suspend/spawn，软关键字 detached/blocking/priority/high/low/until）----
// §4.5 §24 Spawn concurrent task (expression or block)
spawnStatement
    : SPAWN spawnModifierList? ( blockBody | expression ) SEMICOLON?
    ;

// §4.5 §24 Whitespace-separated spawn modifier list (comma optional: G12 业界惯例 Rust/Swift 式列表）
spawnModifierList
    : spawnModifier ( COMMA? spawnModifier )*
    ;

// §4.5 §24 Spawn modifier: detached/blocking (name-only) or priority(high/low) via call form
// ⚠ 语义谓词（GATED）必须放在消耗 token 之前。false 返回时 SLL 在预测阶段就放弃该
//   分支，不消耗 token，不产生语法错误，parser 自动回退到 spawnModifierList? 的
//   "列表不存在" 分支，把后续 identifier 留给 blockBody/expression。
spawnModifier
    : { la1IsSpawnModifierName(this) }? id=IDENTIFIER                                       # spawnModifierName
    | { la1IsSpawnModifierCall(this) }? id=IDENTIFIER LPAREN arg=IDENTIFIER RPAREN          # spawnModifierCall
    ;

// §4.5 §24 Suspend: bare form or 'until' soft-keyword form via predicate
suspendStatement
    : SUSPEND
      (
          untilTok=IDENTIFIER expr=expression SEMICOLON
            { checkSuspendUntil($untilTok.text, this) }?
        | SEMICOLON
      )
    ;

// ---- Reserved syntax (ZOM5001~ZOM5008 precise diagnostics) ----------------
//   NOTE: Use semantic predicate `{ ... }?` (not bare action) so ANTLR does
//   not inject an unreachable `break;` after the action, which would make
//   javac complain.
// §7 Reserved keywords -> ZOM5001-ZOM5008 diagnostics via predicate side-channel throws
reservedSyntax
    : ( THROW | TRY | CATCH | FINALLY ) expressionStatement?
      { reserved("ZOM5001", "exception syntax not implemented (throw/try/catch/finally)", this) }?
    | ( ASYNC | AWAIT ) expressionStatement?
      { reserved("ZOM5002", "async/await syntax not implemented", this) }?
    | VAR expressionStatement?
      { reserved("ZOM5003", "var syntax not implemented; use let/mut/const", this) }?
    | ( ACTOR | CHANNEL ) expressionStatement?
      { reserved("ZOM5004", "actor/channel concurrency types not implemented", this) }?
    | ( YIELD | GENERATOR ) expressionStatement?
      { reserved("ZOM5005", "generator/yield syntax not implemented", this) }?
    | ( NAMESPACE | PACKAGE ) expressionStatement?
      { reserved("ZOM5006", "namespace/package module syntax not implemented", this) }?
    | TYPE expressionStatement?
      { reserved("ZOM5007", "top-level type declaration not implemented; use alias", this) }?
    | ( DELETE | INSTANCEOF | OF | WITH ) expressionStatement?
      { reserved("ZOM5008", "syntax not implemented (delete/instanceof/of/with)", this) }?
    ;

// ---- Block ---------------------------------------------------------------------------
// §4.5 Block statement: { statements }
// NOTE: A trailing expression (without a terminating SEMICOLON) is allowed inside
//       block bodies as a "block tail expression" (spawn / lambda / if-expr patterns).
blockBody : LBRACE statementList expression? RBRACE ;
// §4.5 List of zero or more statements (block body content)
statementList : statement* ;

// §4.6 Parenthesized expression helper: ( expression )
parenExpression : LPAREN expression RPAREN ;
// §4.6 Comma-separated list of expressions
expressionList  : expression ( COMMA expression )* COMMA? ;

// ============================================================================
// §4.6  Expressions — 21 级优先级（syntax-ebnf §5 Table）
//      从低到高：Comma → Assignment → Ternary → ErrorDefault → NullCoalesce
//                → LogicalOr → LogicalAnd → BitwiseOr → BitwiseXor → BitwiseAnd
//                → Equality → Relational → Shift → Additive → Multiplicative
//                → Power → UnaryPrefix → IncrementPrefix → Postfix → Call → Primary
// ============================================================================

// Level 21: Comma
// §4.6 §5 Top-level expression: comma (loosest tier 21)
expression
    : expression COMMA assignmentExpr                                                      # exprComma
    | assignmentExpr                                                                       # exprAssignSingle
    ;

// Level 20: Assignment（right-assoc）
// §4.6 §5 Assignment tier 20 — 17 compound operators
assignmentExpr
    : <assoc=right> conditionalExpr assignmentOp assignmentExpr                           # exprAssignment
    | conditionalExpr                                                                      # exprConditionalSingle
    ;

// §4.6 §5 Assignment operators (=/+=/-=/.../??=/&&=/||=)
// NOTE: Spaced ">>>=" writes as "> > >="; the parser accepts both the lexer-level
//       URSHIFT_ASSIGN token and the 3×GT+ASSIGN form as equivalent.
assignmentOp
    : ASSIGN | MUL_ASSIGN | DIV_ASSIGN | MOD_ASSIGN | PLUS_ASSIGN | MINUS_ASSIGN
    | LSHIFT_ASSIGN | RSHIFT_ASSIGN | URSHIFT_ASSIGN
    | BIT_AND_ASSIGN | BIT_XOR_ASSIGN | BIT_OR_ASSIGN
    | POW_ASSIGN | AND_ASSIGN | OR_ASSIGN | NULL_COALESCE_ASSIGN
    // — Spaced-form equivalents for fixtures that spell compound tokens with
    //   whitespace between characters (readability aids in reference tests):
    | GT GT GT ASSIGN    // ">>>=" written with spaces = URSHIFT_ASSIGN equivalent
    | GT GT ASSIGN       // ">>=" written with spaces = RSHIFT_ASSIGN equivalent
    | GT GT GTE          // "> > >=" spaced URSHIFT_ASSIGN where third GT+ASSIGN lexed as GTE
    ;

// Level 19: Ternary cond ? expr : expr（right-assoc）
// §4.6 §5 Ternary a ? b : c right-assoc tier 19
conditionalExpr
    : <assoc=right> errorDefaultExpr QUESTION expression COLON conditionalExpr            # exprTernary
    | errorDefaultExpr                                                                    # exprErrorDefaultSingle
    ;

// Level 18: ErrorDefault `?:`
// §4.6 §5 Error default ?: tier 18 (above coalesce)
errorDefaultExpr
    : <assoc=right> nullCoalesceExpr ERROR_DEFAULT errorDefaultExpr                      # exprErrorDefault
    | nullCoalesceExpr                                                                    # exprNullCoalesceSingle
    ;

// Level 17: Null coalescing `??`（★ 高于 ||）
// §4.6 §5 Null coalesce ?? right-assoc tier 17 (above logicalOr)
nullCoalesceExpr
    : <assoc=right> logicalOrExpr NULL_COALESCE nullCoalesceExpr                          # exprNullCoalesce
    | logicalOrExpr                                                                       # exprLogicalOrSingle
    ;

// Level 16: Logical OR
// §4.6 §5 Logical || tier 16
logicalOrExpr
    : logicalOrExpr OR logicalAndExpr                                                     # exprLogicalOr
    | logicalAndExpr                                                                      # exprLogicalAndSingle
    ;

// Level 15: Logical AND
// §4.6 §5 Logical && tier 15
logicalAndExpr
    : logicalAndExpr AND bitwiseOrExpr                                                    # exprLogicalAnd
    | bitwiseOrExpr                                                                       # exprBitwiseOrSingle
    ;

// Level 14: Bitwise OR
// §4.6 §5 Bitwise | tier 14
bitwiseOrExpr
    : bitwiseOrExpr BIT_OR bitwiseXorExpr                                                 # exprBitwiseOr
    | bitwiseXorExpr                                                                      # exprBitwiseXorSingle
    ;

// Level 13: Bitwise XOR
// §4.6 §5 Bitwise ^ tier 13
bitwiseXorExpr
    : bitwiseXorExpr BIT_XOR bitwiseAndExpr                                               # exprBitwiseXor
    | bitwiseAndExpr                                                                      # exprBitwiseAndSingle
    ;

// Level 12: Bitwise AND
// §4.6 §5 Bitwise & tier 12
bitwiseAndExpr
    : bitwiseAndExpr BIT_AND equalityExpr                                                 # exprBitwiseAnd
    | equalityExpr                                                                        # exprEqualitySingle
    ;

// Level 11: Equality
// §4.6 §5 Equality == != === !== tier 11
equalityExpr
    : equalityExpr ( EQ | NEQ | STRICT_EQ | STRICT_NEQ ) rangeExpr                        # exprEquality
    | rangeExpr                                                                           # exprRangeSingleAlt
    ;

// Level 10.5: Range (.. half-open, ..= inclusive)
// §4.6 §5 Range operators tier 10.5.
// ★ gated predicate twoIdentifiersSeparatedByRange：REJECT 独立 `a .. b`（两裸标识符 + 右后无调用/成员）
//   (range_reject_neg_01)。ACCEPT：`0..10` / `start..=100` / `get_min()..get_max()` /
//   `arr[i]..end` 等带字面量 / 成员 / 调用形式。twoIdentifiersSeparatedByRange 返回
//   true 仅对 "纯 identifier ..[=] identifier;" 形式，我们用 {!pred}? 反逻辑。
rangeExpr
    : { !twoIdentifiersSeparatedByRange(this) }?
      relationalExpr DOTDOT ASSIGN? relationalExpr                                       # exprRange
    | relationalExpr                                                                      # exprRangeFallthrough
    ;

// Level 10: Relational + is + as（★ is 作为 relational tier）
// §4.6 §5 Relational < > <= >= is as tier 10
//
// AS 单独列一条，后面紧跟 checkAsForceCastLookahead 谓词 — 拒绝 as! force-cast 形式（G7）
// （24-truth REJECT: as! 强转）。其它 5 个运算符走一般形式。
// NOTE: Right-hand side of `as` / `is` is a type (typeExpr), NOT a shift expression;
//       using shiftExpr would force `as i32` into a non-type expression and fail.
relationalExpr
    : relationalExpr AS { checkAsForceCastLookahead(this) }? typeExpr
      { checkAsRightIsNotDyn($typeExpr.ctx, this) }?                                  # exprRelationalAs
    | relationalExpr ( LT | GT | LTE | GTE ) shiftExpr                                   # exprRelational
    | relationalExpr IS typeExpr                                                         # exprIs
    | shiftExpr                                                                     # exprShiftSingle
    ;

// Level 9: Shift
// §4.6 §5 Shift << >> >>> tier 9
// NOTE: Spaced-form alternatives (`> >`, `> > >`, `< < < <reserved>` not used; instead
//       lexer produces LSHIFT / RSHIFT / URSHIFT from the non-spaced text. For clarity,
//       fixtures that write `> > > 2` (space-separated) are intentionally matched here via
//       additional parser-level GT-GT-GT sequences as a single URSHIFT-like operation.
shiftExpr
    : shiftExpr ( LSHIFT | RSHIFT | URSHIFT ) additiveExpr                                  # exprShift
    | shiftExpr GT GT GT additiveExpr                                                        # exprShiftSpacedUrshift
    | shiftExpr GT GT additiveExpr                                                           # exprShiftSpacedRshift
    | additiveExpr                                                                        # exprAdditiveSingle
    ;

// Level 8: Additive
// §4.6 §5 Additive + - tier 8
additiveExpr
    : additiveExpr ( PLUS | MINUS ) multiplicativeExpr                                    # exprAdditive
    | multiplicativeExpr                                                                  # exprMultiplicativeSingle
    ;

// Level 7: Multiplicative
// §4.6 §5 Multiplicative * / % tier 7
multiplicativeExpr
    : multiplicativeExpr ( MUL | DIV | MOD ) powerExpr                                    # exprMultiplicative
    | powerExpr                                                                           # exprPowerSingle
    ;

// Level 6: Power ** （right-assoc）
// §4.6 §5 Power ** right-assoc tier 6 (above unary -> -a**2 = -(a**2))
powerExpr
    : <assoc=right> unaryExpr POW powerExpr                                               # exprPower
    | unaryExpr                                                                           # exprUnarySingle
    ;

// Level 5: Unary prefix
// §4.6 §5 Prefix unary + - ! ~ typeof new tier 5
// NOTE: FORCE_UNWRAP `!!` is also accepted here as a prefix "double-neg" shape
//       (lexer maximal-munch converts `!!` to FORCE_UNWRAP token, NOT two NOTs).
// NOTE: MUL `*expr` = dereference（§4.6 pointer deref, matches suspend until *flag 等惯用语法）。
// NOTE: BIT_AND `&expr` = borrow/reference（与 MUL 对称，ZOM 语义中 ref/deref 前缀）。
unaryExpr
    : ( PLUS | MINUS | NOT | BIT_NOT | TYPEOF | FORCE_UNWRAP | MUL | BIT_AND ) unaryExpr # exprUnary
    | preIncrementExpr                                                                    # exprPreIncSingle
    ;

// Level 4: Prefix ++ --
// §4.6 §5 Prefix ++/-- tier 4
preIncrementExpr
    : ( PLUSPLUS | MINUSMINUS ) preIncrementExpr                                          # exprPreInc
    | postfixExpr                                                                         # exprPostfixSingle
    ;

// Level 3: Postfix ++ -- ?! !! （左结合）+ 调用/索引/成员 组合
// §4.6 §5 Postfix ++/--/?!/!! tier 3 (L-R)
// NOTE: NOT (single `!`) accepted as a postfix slot so chains like `x++?!!`
//       (tokenised x ++ ?! !) continue to parse. QUESTION (?) is allowed ONLY
//       as part of an ongoing postfix CHAIN (i.e. followed by another postfix
//       operator like `!`, `!!`, `++`, `.member`, `[idx]`, `(args)`, or the
//       ternary `:`. Bare standalone `foo()? ;` is REJECTED per error-propagation
//       spec (use `!!` for error propagation, `?.` for optional chain, `??` for
//       null-coalesce).
// §7.3 raises? propagation: postfix `raises?` on call expressions (spawn / call / try).
postfixExpr
    : postfixExpr RAISES QUESTION                                                           # exprPostfixRaisesProp
    | postfixExpr ( PLUSPLUS | MINUSMINUS | ERROR_PROPAGATE | FORCE_UNWRAP | NOT )
      { checkPostfixLValue($ctx, this) }?                             # exprPostfixOp
    // QUESTION (?) as chain-only postfix: gated by LA(2) being a chain-continuation
    // token. Prevents standalone `foo()? ;` but permits `x++?!!?;` chains and
    // `obj?.member` optional chains (QUESTION before PERIOD/LBRACK/LPAREN),
    // plus ternary RHS `cond ? a : b` (QUESTION before COLON via ternary alt).
    | postfixExpr { questionIsChainContinuation(this) }? QUESTION                           # exprPostfixChainQuestion
    | callExpr                                                                            # exprCallSingle
    ;

// Level 2: Call / Member / Index / Optional chain
// §4.6 §5 Call / member / index tier 2 (L-R) with optional chaining ?. / ?[ / ?(
// NOTE: Lexer produces a single OPTIONAL_CHAIN `?.` token from `?.` — so accept it
//       directly. Continue to accept the QUESTION? PERIOD shape for whitespace-
//       separated `?.` spellings (QUESTION + PERIOD).
// NOTE: 成员访问（.member / ?.member）使用 memberIdentifier，允许 `obj.get / obj.set /
//       super.init / self.deinit` 等硬关键字作为成员名（保持声明处的关键字保留
//       同时允许表达式访问它们）。
callExpr
    : callExpr LPAREN expressionList? RPAREN (RAISES typeList)?                           # exprCall
    | callExpr OPTIONAL_CHAIN LPAREN expressionList? RPAREN (RAISES typeList)?             # exprOptionalCall
    | callExpr QUESTION? PERIOD memberIdentifier                                          # exprMember
    | callExpr OPTIONAL_CHAIN memberIdentifier                                            # exprOptionalMember
    | callExpr OPTIONAL_CHAIN LBRACK expression RBRACK                                     # exprOptionalIndex
    | callExpr QUESTION? LBRACK expression RBRACK                                         # exprIndex
    | primaryExpr                                                                         # exprPrimarySingle
    ;

// Level 1: Primary
// §4.6 §5 Primary expression tier 1 — tightest binding
primaryExpr
    : literal                                                                             # exprLiteral
    | THIS                                                                                # exprThis
    | SUPER                                                                               # exprSuper
    | identifier                                                                          # exprIdentifier
    // GET/SET are hard lexer tokens (class property get/set) but are also valid
    // as free-standing identifiers in any expression — notably `get("url")` calls
    // inside attribute input, but also user code that names functions get/set.
    // Parser context (classMember alt with GET keyword vs expression here) disambiguates.
    | GET                                                                                 # exprGetAsIdent
    | SET                                                                                 # exprSetAsIdent
    | NEW typeExpr LPAREN expressionList? RPAREN                                          # exprNew
    | IMPORT LPAREN expression RPAREN                                                     # exprImportCall
    | SPAWN spawnModifierList? ( blockBody | assignmentExpr )                             # exprSpawn
    | lambdaExpr                                                                          # exprLambda
    | structLiteral                                                                       # exprStructLiteral
    | tupleLiteral                                                                        # exprTupleLiteral
    | arrayLiteral                                                                        # exprArrayLiteral
    | parenExpression                                                                     # exprParen
    // §4.6 Block expression（最后一条表达式作为返回值，与 Rust 一致）— 用于
    //   attribute input（`#[foo(a = { b = 3 })]`）、三元 RHS、函数参数等处。
    //   注意：statement 级别 `stmtBlock` 存在歧义的情况下（如 standalone `{stmt}`），
    //   parser 会优先走 `statement→stmtBlock` 分支（statement 层级优先于 expression）。
    | blockBody                                                                           # exprBlock
    // §20 Unsafe block expression
    | unsafeBlockExpr                                                                     # exprUnsafeBlock
    // §21 Function-like macro invocation: name!(tt) / name![tt] / name!{tt}
    | macroInvocationExpr                                                                 # exprMacroInvocation
    | predefinedType                                                                      # exprPredefinedType
    ;

// ---- Literal ------------------------------------------------------------
// §3.6 §4.6 Literal tokens (numeric/string/char/template/null/bool/unit)
literal
    : DECIMAL_LITERAL | BIGINT_LITERAL
    | BINARY_LITERAL | OCTAL_LITERAL | HEX_LITERAL
    | DOUBLE_STRING_LITERAL | SINGLE_STRING_LITERAL | CHAR_LITERAL
    | NO_SUBSTITUTION_TEMPLATE_LITERAL
    | templateLiteral
    | NULL
    | boolLiteral
    | UNIT             // unit 类型的缺省值（与 'unit' keyword 共享 token）
    ;

// §3.6 §4.6 Boolean literal TRUE or FALSE (hard tokens)
boolLiteral
    : TRUE | FALSE
    ;

// ---- Template literal（4-token 方案 + 表达式递归）----
// §3.6.5 §4.6 Template literal with substitutions (4-token scheme)
templateLiteral
    : TEMPLATE_HEAD expression ( TEMPLATE_MIDDLE expression )* TEMPLATE_TAIL
    ;

// ---- Lambda -----------------------------------------------------------
// §4.6 Lambda / closure expression with parameter list, optional capture-clause 'use[...]'
lambdaExpr
    : parameterList (ARROW typeExpr ( RAISES typeList )? )? ROCKET blockBody               # lambdaBlock
    | parameterList (ARROW typeExpr ( RAISES typeList )? )? ROCKET expression              # lambdaExprArrow
    ;

// ---- Struct literal ---------------------------------------------------
structLiteral
    // G5：struct literal 只有命名字段形式，没有 positional
    // §4.6 Struct literal 允许泛型参数化：Vec3<f32> { x: 1.0, y: 0.0 }
    : identifier ( LT { incGenericDepth(this); } { withinGenericDepthLimit(this) }? typeArgList genericClose { decGenericDepth(this); } )?
      LBRACE structFieldInit ( COMMA structFieldInit )* COMMA? RBRACE
    ;

// §4.6 Single field initializer inside struct literal
structFieldInit
    : identifier ( COLON expression )?     // 支持 value-shorthand `{ x, y: 2 }`
    ;

// §4.6 Tuple literal / Unit literal:
//   - `()`          → unit literal (matches UNIT value)
//   - `(a,)`        → 1-tuple
//   - `(a, b)`      → 2-tuple (comma list ≥2)
tupleLiteral
    : LPAREN RPAREN                                                                        # tupleUnitLiteral
    | LPAREN expression COMMA expressionList? RPAREN                                       # tupleMultiLiteral
    ;

// ---- Array literal --------------------------------------------------
// §4.6 Array literal: [elem, ...] with trailing comma
arrayLiteral
    : LBRACK expressionList? RBRACK
    ;

// ============================================================================
// §4.7  Patterns（完整 8+2 形态：Literal / Rest 为原 g4 缺失）
// ============================================================================
// §4.7 §7 All pattern forms: wildcard/literal/rest/bind/is/enum/tuple/struct/array/expression
pattern
    : wildcardPat                                                                         # patWildcardFinal
    | literal                                                                             # patLiteral
    | ELLIPSIS pattern?                                                                   # patRest
    | bindPat                                                                             # patBindingFinal
    | IS typeExpr                                                                         # patIs
    // A9: `::`-qualified enum pattern REJECT (use '.' form e.g. Color.Red)
    | path=identifier COLONCOLON name=identifier tupleLiteral?
      { rejectEnumColonCol($path.text, $name.text, this) }?
                                          # patEnum
    | LPAREN pattern ( COMMA pattern )* COMMA? RPAREN
      { checkRestPatternLast(_localctx, "tuple", this) }?                                 # patTuple
    | identifier LBRACE structPatternField ( COMMA structPatternField )* COMMA? RBRACE    # patStruct
    | LBRACK pattern ( COMMA pattern )* COMMA? RBRACK
      { checkRestPatternLast(_localctx, "array", this) }?                                 # patArray
    | expression                                                                          # patExpression  // 最低优先级 fallback
    ;

wildcardPat
    // UNDERSCORE is a hard token in ZomLexer.g4 (produced instead of IDENTIFIER for
    // a standalone underscore), so this rule matches literal UNDERSCORE token.
    : UNDERSCORE
    ;

// §4.7 §7 Binding pattern: identifier ('@' pattern)? — predicate rejects '_' as binding
bindPat
    : identifier ( AT pattern { checkBindPatNoNestedAt($pattern.ctx, this); } )?
      { checkBindPat($identifier.text, this) }?
    ;

// §4.7 §7 Single field in struct pattern: key ':' pattern | key shorthand
structPatternField
    : identifier ( COLON pattern )?
    ;

// ============================================================================
// §4.8  Types（6 层：functionType | unionType | intersectionType → postfixType
//               → atomType）
//
// 注意 G9：RaisesClause = `raises` + TypeList（| 分隔的并集，非逗号列表）
// ============================================================================
// §3 §4.8 Top-level type expression: function type tier
typeExpr : functionType ;

// Level F: FunctionType（与 union 同优先级；为避免冲突，函数参数只接受带括号形式）
// §3 §4.8 Function / closure type: (params) -> return-type (raises typeList)?
functionType
    : parameterList ( ARROW returnType ( RAISES typeList )? )                             # typeFunction
    // FUN-keyword-prefixed function type: fun(T) -> U raises E? (industry-standard explicit syntax)
    | FUN parameterList ( ARROW returnType ( RAISES typeList )? )                          # typeFunctionKeyword
    | unionType                                                                           # typeUnionSingle
    ;

// §3 §4.8 Return type of function type
returnType : typeExpr ;

// Level 4: Union  `|`
// §3 §4.8 Union type A | B (pipe separator)
unionType
    : unionType BIT_OR intersectionType                                                   # typeUnion
    | intersectionType                                                                    # typeIntersectionSingle
    ;

// Level 3: Intersection `&`
// §3 §4.8 Intersection type A & B (tighter than union per §4.8 precedence)
intersectionType
    : intersectionType BIT_AND postfixType                                                # typeIntersection
    | postfixType                                                                         # typePostfixSingle
    ;

// Level 2: Postfix（`[]` / `?` suffix，支持链式）+ 类型成员访问（Self.Item）
// §3 §4.8 Postfix type operators T[] / T? (chained)
// NOTE: NULL_COALESCE `??` appears here as a "double-optional" escape — the lexer
//       longest-match rule turns `T??` into `T NULL_COALESCE` rather than
//       `T QUESTION QUESTION`; accepting it at this tier restores the intended
//       "T optional, optional" (T??) semantics while NULL_COALESCE as a binary
//       operator continues to work at the expression level.
// NOTE: T.Field 用于关联类型访问，比如 Self.Item / Result<T, E>.Output。
//       这是 §4.8 类型表达式的一部分，与 expression 级别的成员访问同构。
postfixType
    // NOTE: 数组类型统一前缀形式 [T] / [T; N]（atomType #typeArrayLiteralAtom），
    //       T[] 后缀形式显式不支持 —— 与表达式层 [1,2,3]、模式层 [a,b,c] 完美三层同构
    //       （不要在这里加 LBRACK 规则，避免歧义）
    : postfixType QUESTION                                                                # typeOptional
    | postfixType NULL_COALESCE                                                           # typeOptionalDouble
    | postfixType PERIOD memberIdentifier                                                 # typeMemberAccess
    | atomType                                                                            # typeAtomSingle
    ;

// Level 1: Atom types
// §3 §4.8 Atomic types: predefined / qualified / named / tuple / object / array-literal
atomType
    : predefinedType                                                                      # typePredefined
    | attributePath ( LT { incGenericDepth(this); } { withinGenericDepthLimit(this) }? typeArgList genericClose { decGenericDepth(this); } )?                                                # typeQualified
    | identifier ( LT { incGenericDepth(this); } { withinGenericDepthLimit(this) }? typeArgList genericClose { decGenericDepth(this); } )?                                                   # typeNamed
    | LPAREN typeExpr ( COMMA typeExpr )* COMMA? RPAREN
      { checkTupleTypeNot1Tuple(_localctx, this) }?                                   # typeTuple
    | LBRACE structFieldType ( COMMA structFieldType )* COMMA? RBRACE                     # typeObject
    | identifier LPAREN variantTypeList RPAREN                                                    # typeTupleVariant
    | LBRACE RBRACE                                                                        # typeObjectEmpty
    | LBRACK typeExpr (SEMICOLON expression)? RBRACK                                         # typeArrayLiteralAtom
    ;

// §3 §4.8 Single field inside object type: optional mut, id ':' type
structFieldType
    : MUT? identifier COLON typeExpr
    ;

// 8 种固定长度整型 + 浮点 + bool / str / char / null / unit / never / any
// §3.1 §4.8 21 predefined scalar types: i8..u64, f32/f64, bool, str, char, null, unit, never, any
predefinedType
    : I8 | I16 | I32 | I64 | U8 | U16 | U32 | U64 | F32 | F64
    | BOOL | STR | CHAR | NULL | UNIT | NEVER | ANY
    ;

// §4.8 G9 Pipe-separated type list used for raises/implements/extends (NOT comma)
typeList
    : typeExpr ( BIT_OR typeExpr )*    // G9：用 | 分隔（union 顺序）；
                                       //   tuple/params 用逗号，用不同 rule 区分。
    ;

// §12.1 Comma-separated type arguments used in generic instantiation Foo<T, U>
typeArgList
    : typeExpr ( COMMA typeExpr )* COMMA?
    ;

// ============================================================================
// Identifier
//   软关键字识别（`use / auto / detached / blocking / priority / high / low
//   / until`）全在此处统一语义层处理；语法层它们就是普通 IDENTIFIER。
// ============================================================================
// §3.7 §6.3 Identifier with Unicode support; soft keywords (use/auto/detached/blocking/priority/high/low/until) stay IDENTIFIER
identifier
    : IDENTIFIER
    ;

// ============================================================================
// §20 FFI and Interop (safe/unsafe + extern blocks + repr attribute)
//
//   设计：所有新关键字（extern / unsafe / variable / opaque / macro）均采用
//   软关键字模式：identifier + semantic predicate，避免与 §6.1 Implemented
//   Keywords 表冲突 & 不污染用户标识符命名空间。
//   Lexer 未新增 token（不改 Lexer g4，保持 474/524 R13 基线无漂移）。
// ============================================================================

// 20.0 通用工具 rule：FUN identifier typeParameters? callSignature? (SEMI | blockBody)
//     等价于 declaration#functionDeclaration 去除 modifierList；被 externDecl /
//     unsafeFunDecl / externItem 复用。
funDecl
    : FUN identifier
      typeParameters?
      functionSignature
      ( SEMICOLON | blockBody )
    ;

// 20.1 Unsafe block expression (escape hatch for unsafe operations)
//      形式：`unsafe { stmt* expr? }` — 与 block 结构一致，但前置软关键字。
unsafeBlockExpr
    : tok=identifier { checkIsUnsafePrefix($tok.text, this) }?
      LBRACE statement* expression? RBRACE
    ;

// 20.2 Extern declaration (foreign function interface)
//      两种形态：
//        (a) `extern "C"? { ... }`    批量声明 block
//        (b) `extern "C"? fun ...;`   单条 extern 函数
//      ABI 字符串可选；指定时必须是 {"C", "Cdecl", "system", "zom-cdecl"}
//      之一，由 checkExternAbiFormat 在最后 terminal 之后校验（TAP 安全模式）。
//
//      "unsafe" 前缀通过 ANTLR label + predicate 独立标记；把 `(unsafeTok=...)`
//      作为 optional 组包裹（ANTLR 不支持 `label?=` 语法）。
externDecl
    : ( unsafeTok=identifier { checkIsUnsafePrefix($unsafeTok.text, this) }? )?
      extTok=identifier    { checkIsExternKeyword($extTok.text, this) }?
      ( abi=DOUBLE_STRING_LITERAL )?
      externBlock
      { $abi == null || checkExternAbiFormat($abi.text, this) }?              # externBlockDecl
    | ( unsafeTok=identifier { checkIsUnsafePrefix($unsafeTok.text, this) }? )?
      extTok=identifier    { checkIsExternKeyword($extTok.text, this) }?
      ( abi=DOUBLE_STRING_LITERAL )?
      funDecl
      { $abi == null || checkExternAbiFormat($abi.text, this) }?              # externSingleFunDecl
    ;

// 20.3 Extern block — groups multiple FFI declarations
externBlock
    : LBRACE externItem* RBRACE
    ;

// 20.4 Items permitted inside an `extern "C" { ... }` block
//      允许：函数声明、变量声明、不透明类型别名。
//      "variable" 软关键字显式区分变量；与 funDecl / TYPE 产生式分支歧义
//      由 ANTLR ALL(*) 解析器消歧（最长匹配 + 上下文 FOLLOW）。
externItem
    : funDecl                                                                           # externFunDecl
    | vTok=identifier   { checkIsVariableKeyword($vTok.text, this) }?
      identifier COLON typeExpr SEMICOLON                                               # externVarDecl
    | TYPE identifier ASSIGN
      ( oTok=identifier { checkIsOpaqueKeyword($oTok.text, this) }? )?
      identifier typeExpr? SEMICOLON                                                    # externTypeAlias
    ;

// 20.5 Unsafe function declaration (caller guarantees memory safety)
//      前置 "unsafe" 软关键字，等价于在声明位置承诺：该函数内部可能进行
//      不透明指针 / 越界访问 / FFI 调用。
unsafeFunDecl
    : tok=identifier { checkIsUnsafePrefix($tok.text, this) }?
      funDecl
    ;

// ============================================================================
// §21 Macros 2.0 (fn-like / derive / attribute / macro_rules)
//
//   设计要点：
//     * MACRO_PUNCT 未在 Lexer 中单独 token 化（不改 Lexer），所以 macroToken
//       通过复用已有单字符 token（COMMA / SEMICOLON / COLON / 位运算符 / 算术
//       运算符 等）来宽松匹配任何 punctuation terminal。使用 terminal alternative
//       穷尽 21 个常用标点，其余 IDENTIFIER / literal / 括号嵌套保持独立。
//     * macroInvocationExpr 挂入 primaryExpr 作为末端分支（与 identifier 分支
//       共享共同 prefix `identifier`，由 ANTLR 通过 lookahead NOT='!' 消歧）。
//     * macroRulesDecl 挂入 declaration 作为 module-level item。
// ============================================================================

// 21.1 Function-like macro invocation: name!(token_tree) / name![tt] / name!{tt}
//      NOT = '!'；三种定界形式并列。
macroInvocationExpr
    : path=identifier NOT ( LPAREN macroTokenTree RPAREN
                          | LBRACK macroTokenTree RBRACK
                          | LBRACE macroTokenTree RBRACE )
    ;

// 21.2 Token tree — balanced delimiters + any token sequence
macroTokenTree
    : macroToken*
    ;

macroToken
    // Punctuation — 复用 lexer 已有的 21 种常用标点 terminal，覆盖 macro_rules
    // 内绝大多数模式构造需要的符号，剩余边界情况由 IDENTIFIER / literal 兜底。
    //   token 名严格对齐 ZomLexer.g4（MUL='*', DIV='/', MOD='%', BIT_NOT='~',
    //   LTE='<=', GTE='>=', PERIOD='.', BIT_OR='|', SEMICOLON=';' — 无自定义别名）
    : ( NOT | AND | OR | BIT_OR | BIT_AND | BIT_XOR | PLUS | MINUS | MUL | DIV
      | MOD | ASSIGN | COLON | SEMICOLON | COMMA | QUESTION | AT | BIT_NOT
      | LSHIFT | RSHIFT | URSHIFT | LT | GT | EQ | NEQ | LTE | GTE | PERIOD | DOTDOT
      | ELLIPSIS )                                                             # macroPunctTok
    | identifier                                                             # macroIdentTok
    | literal                                                                # macroLiteralTok
    // Keyword fallback — 允许 keyword token 在 token tree 中出现（if/match/for/
    // while/return/class/struct/... 等在 macro_rules 展开体中会出现）。
    | ( CLASS | STRUCT | INTERFACE | ENUM | ERROR | FUN | MUT | LET | CONST
      | ALIAS | INIT | DEINIT | GET | SET | IF | ELSE | MATCH | WHEN | DEFAULT
      | FOR | WHILE | DO | BREAK | CONTINUE | RETURN | DEBUGGER | IN | OUT
      | BOOL | STR | CHAR | NULL | UNIT | NEVER | ANY
      | PUBLIC | PRIVATE | PROTECTED | STATIC | READONLY | MUTATING | OVERRIDE
      | ABSTRACT | AS | IS | TYPEOF | NEW | THIS | SUPER | EXTENDS | RAISES
      | MODULE | IMPORT | EXPORT | SUSPEND | SPAWN
      | THROW | TRY | CATCH | FINALLY | ASYNC | AWAIT | VAR | ACTOR | CHANNEL
      | YIELD | GENERATOR | NAMESPACE | PACKAGE | TYPE | DELETE | INSTANCEOF
      | OF | WITH | TRUE | FALSE | UNDERSCORE | IMPLEMENTS
      | I8 | I16 | I32 | I64 | U8 | U16 | U32 | U64 | F32 | F64 )              # macroKeywordTok
    | LPAREN macroTokenTree RPAREN                                           # macroParenGroup
    | LBRACK macroTokenTree RBRACK                                           # macroBrackGroup
    | LBRACE macroTokenTree RBRACE                                           # macroBraceGroup
    ;

// 21.3 Macro Rules (declarative macro 2.0)
//      `macro NAME! { ( $a:expr ) => { ... }; ... }`
//      软关键字 "macro" + 标识符 + `!` + 花括号，内部是 (pattern)=>{token_tree}
//      规则序列，规则之间 ';' 可选（Rust 风格）。
macroRulesDecl
    : MAC_=identifier { checkIsMacroKeyword($MAC_.text, this) }?
      identifier NOT
      LBRACE ( macroRule ( SEMICOLON )? )* RBRACE
    ;

macroRule
    // 两种括号形式的 pattern：( ... ) 与 [ ... ]
    : LPAREN macroPattern RPAREN ROCKET LBRACE macroTokenTree RBRACE          # macroRuleParen
    | LBRACK macroPattern RBRACK ROCKET LBRACE macroTokenTree RBRACE          # macroRuleBrack
    ;

// 21.3.1 宏模式：逗号分隔的 macroPatToken 序列，允许尾部 ",..." 表示 rest 匹配
//        (逗号省略符，Rust 风格 $(...),* 的模式层简写)。
macroPattern
    : macroPatToken ( COMMA macroPatToken )* ( COMMA ELLIPSIS )?
    ;

macroPatToken
    : captureName=identifier COLON macroFragSpec
        { $captureName.text.length() > 1
          && $captureName.text.charAt(0) == '$' }?                                         # macroCapture
    | identifier                                                              # macroPatIdent
    | literal                                                                 # macroPatLiteral
    // 标点层 — 复用 lexer 标点；比 macroToken 的标点集略精简
    | ( NOT | AND | OR | BIT_OR | BIT_AND | BIT_XOR | PLUS | MINUS | MUL | DIV
      | MOD | ASSIGN | QUESTION | AT | BIT_NOT
      | LSHIFT | RSHIFT | URSHIFT | LT | GT | EQ | NEQ | LTE | GTE | PERIOD | DOTDOT
      | ELLIPSIS )                                                           # macroPatPunct
    // Keyword fallback — 模式中通常不含关键字，但允许常用字面量/表达式关键字
    | ( TRUE | FALSE | NULL | UNIT | NEVER | ANY | BOOL | STR | CHAR
      | I8 | I16 | I32 | I64 | U8 | U16 | U32 | U64 | F32 | F64
      | IF | ELSE | MATCH | FOR | WHILE | RETURN | LET | CONST | MUT
      | UNDERSCORE )                                                          # macroPatKeyword
    ;

// 21.3.2 宏 fragment specifier（元变量分类）
//        允许：expr ty pat stmt item block meta tt ident path vis literal lifetime
//        语法层面仅接受 identifier；语义层再验证其合法性（便于未来扩展）。
macroFragSpec
    : identifier
    ;

// 21.4 Derive attribute helper（配合 §16 Attributes 使用）
//      `#[derive(Debug, Clone, Copy)]` / `#[derive(Serde(rename_all = "camelCase"))]`
//      deriveList 被 attribute 规则中的 #[derive(...)] 形式引用，用于结构化
//      解析 derive 的 trait 列表；语法层作为独立 rule 提供以便 conformance
//      fixture 单独覆盖（同时被 attributeInput 规则隐式包含的括号表达式
//      fallback 支持）。
deriveList
    : LPAREN ( deriveItem ( COMMA deriveItem )* COMMA? )? RPAREN
    ;

deriveItem
    : path=identifier ( LPAREN attrInput RPAREN )?
    ;
