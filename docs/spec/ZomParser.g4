/*
 // * ZomParser.g4 - executable parser oracle for ZOM.
 // *
 // * Source of truth:
 // *   1. docs/spec/chapters/17-grammar-reference.md
 // *   2. docs/spec/chapters/02-lexical-structure.md for terminal spelling
 // *   3. RFC 0002 and RFC 0003 while parser and lexer architecture are under review
 // *
 // * This grammar is derived from the grammar reference and the compiler token
 // * inventory. The hand-written C++ parser is the compiler parser; this file is
 // * an oracle for conformance checks and must not define an independent language.
 // *
 // * Usage:
 // *   antlr4 ZomLexer.g4 ZomParser.g4 -visitor && javac Zom*.java
 // *   grun Zom sourceFile -tree   < input.zom
 // *   grun Zom expression -tree   < input.txt
 // *   grun Zom typeExpr   -tree   < input.txt
 // *   grun Zom pattern    -tree   < input.txt
 // */
parser grammar ZomParser;

// Terminal tokens are provided by ZomLexer.g4.
options {
    tokenVocab  = ZomLexer;
    // Keep ANTLR's default prediction behavior.
}

// Types used by semantic predicates and helpers.
@parser::header {
    import org.antlr.v4.runtime.FailedPredicateException;
    import org.antlr.v4.runtime.Token;
    import org.antlr.v4.runtime.misc.ParseCancellationException;
    import java.util.HashSet;
    import java.util.Set;
}

@parser::members {
    static boolean reserved(String detail, Object parser) {
        throw new org.antlr.v4.runtime.misc.ParseCancellationException(detail);
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
     // * token so that false return drives SLL prediction-level backtracking
     // * instead of a post-consumption "failed predicate" syntax error.
     // * Valid (true): detached / blocking.
     // * Anything else -> false -> spawnModifier alternative is dropped during
     // * prediction; parser falls through to (spawnBlockBody | expression).
     // */
    static boolean la1IsSpawnModifierName(Object parser) {
        Token t = ((ZomParser)parser)._input.LT(1);
        if (t == null || t.getType() != ZomParser.IDENTIFIER) return false;
        String s = t.getText();
        return s.equals("detached") || s.equals("blocking");
    }

    /* Predicate-FIRST check for priority(high|low) call modifier.
     // * Lookahead form: IDENTIFIER "priority" LPAREN IDENTIFIER(high|low) RPAREN.
     // * Evaluated BEFORE consuming any token so false produces SLL-level
     // * backtracking with no syntax error.
     // */
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

    /** Function-expression capture clause soft-keyword check. */
    static boolean la1IsCaptureClause(Object parser) {
        Token t = ((ZomParser)parser)._input.LT(1);
        return t != null && t.getType() == ZomParser.IDENTIFIER && t.getText().equals("use");
    }

    /* Single-segment attribute names (built-in compiler attributes, sec 16).
     // * Accept these short attribute names; other single-identifier attributes
     // * are rejected at parser level. This enforces G11 "attribute path requires
     // * at least one :: for user-defined attrs" (short/foo/bar rejected).
     // */
    static boolean isBuiltinSingleSegAttr(String name) {
        if (name == null) return false;
        switch (name) {
            case "inline": case "deprecated": case "cold": case "repr":
                return true;
            default:
                return false;
        }
    }

    static void rejectUnavailableConditionalAttribute(String first, String rest) {
        if ("zom".equals(first) && "cfg".equals(rest)) {
            throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                "ConditionalCompilationUnavailable: remove `zom::cfg`");
        }
    }

    /* Trailing separator relaxation for struct / class / interface / error
     // * body fields.
     // * Called as a gated predicate on the "no explicit separator" alternative
     // * at the tail of a structField (field declaration). Returns true when the
     // * next tokens mean we are in one of the following valid positions:
     // *   (a) end of body (next is RBRACE) - trailing field OK without separator
     // *   (b) next member starts with block-form keyword (FUN / INIT / DEINIT)
     // *       these keywords self-delimit, no explicit SEMICOLON/COMMA needed
     // *   (c) next member has #[...] outer attribute (HASH) or starts
     // *       with a value-declaration keyword (MUT/LET/CONST)
     // *   (d) next member starts with modifier keyword (public/private/etc.)
     // *
     // * Returns false when next is IDENTIFIER and LA(2) == COLON -> next token
     // * is a plain field declaration like `a: i32  b: i32;` WITHOUT any
     // * separator between fields -> REJECT (see struct_field_no_semi_reject_neg_03).
     // */
    static boolean okAfterStructFieldNoSeparator(Object parser) {
        ZomParser p = (ZomParser)parser;
        int la1 = p._input.LA(1);
        switch (la1) {
            case ZomParser.RBRACE:
            case ZomParser.HASH:
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
    /** suspend until soft-keyword check. */
    static boolean checkSuspendUntil(String kw, Object parser) {
        if (!kw.equals("until"))
            throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                "suspend only accepts 'until' clause; got '" + kw + "'");
        return true;
    }






    static boolean checkIsImplKeyword(String text, Object parser) {
        if (!text.equals("impl")) return false;
        return true;
    }
    static boolean checkIsWhereKeyword(String text, Object parser) {
        if (!text.equals("where")) return false;
        return true;
    }
    static boolean checkIsDynKeyword(String text, Object parser) {
        if (!text.equals("dyn")) return false;
        return true;
    }
    static boolean checkIsUnsafePrefix(String text, Object parser) {


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

    // ---- A-class REJECT predicates (24-truth table enforcement) ----

    /** A6: standalone `dyn` on the RHS of `as` is not a valid cast target. */
    static boolean checkAsRightIsNotDyn(org.antlr.v4.runtime.RuleContext rhsCtx, Object parser) {
        if (rhsCtx == null) return true;
        if ("dyn".equals(rhsCtx.getText()))
            throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                "`dyn` is not a valid standalone cast target (use `dyn InterfaceName`)");
        return true;
    }

    /** A7: postfix ++/-- must apply to an lvalue expression (reject `5++`, `42--`). */
    static boolean checkPostfixLValue(org.antlr.v4.runtime.ParserRuleContext exprPostfixCtx,
                                      Object parser) {
        if (exprPostfixCtx == null || exprPostfixCtx.getChildCount() < 1) return true;
        org.antlr.v4.runtime.tree.ParseTree lhs = exprPostfixCtx.getChild(0);

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
            case ZomParser.CHAR_LITERAL:
            case ZomParser.TRUE: case ZomParser.FALSE:
            case ZomParser.NULL: case ZomParser.UNIT:
                throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                    "postfix ++/-- require an lvalue, not a literal: '" + first.getText() + "'");
        }
        return true;
    }

    /** A8: double-@ chain `a @ b @ p` rejected — `pattern` on RHS of @ must itself not
     // *  be a bindPat that also contains a `@`. */
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
     // *  Walk LPAREN/LBRACK children, search for any ELLIPSIS token that is NOT on the
     // *  last COMMA-separated element. */
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

    private static void rejectTopLevelVisibility(
            org.antlr.v4.runtime.ParserRuleContext declaration) {
        org.antlr.v4.runtime.tree.TerminalNode first = firstTerminal(declaration);
        if (first == null) return;
        int type = first.getSymbol().getType();
        if (type == PUBLIC || type == PRIVATE || type == PROTECTED) {
            throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                "module-level declarations use export instead of member visibility");
        }
    }

    /** A4: 1-tuple type `(T,)` (with TRAILING COMMA) is REJECTED.
     // *  Parenthesised single type `(T)` (no trailing comma) remains valid. */
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
     // *  POS fixtures (generic_type_edge_01 Vec<Vec<i32>>, nested_angle_closure_edge_03
     // *  HashMap<K, Vec<V>>) explicitly require >> compact-close to parse. The
     // *  original strict "space-separated only" rule is deferred to the semantic
     // *  pass (lint only); parser accepts both forms.
     // */
/* A12 (strict): reject compact >> / >>> generic close at PARAMETER declaration
 // * (genericParamClose). This ensures `fun f<T, U>>` (unbalanced over-close) is
 // * REJECTED cleanly. Compact close at type ARGUMENT instantiation (genericClose)
 // * is handled by splitCompactClose below — it breaks RSHIFT/URSHIFT into 2/3 GTs.
 // * Lint for "prefer spaced > >" is deferred to semantic pass.
 // */
static boolean rejectCompactClose(String token, Object parser) {
    throw new ParseCancellationException("compact `" + token
        + "` not allowed in generic PARAMETER declaration; use space-separated `>`");
}

/* A12 ONE-TIME PRE-PROCESS (runs in sourceFile @init, BEFORE parsing): walk the
 // * entire TokenStream and rewrite every RSHIFT (>>) and URSHIFT (>>>) token into
 // * individual GT (>)-equivalent tokens, so that genericClose only ever matches
 // * plain GT tokens. This avoids race conditions with ALL(*) adaptive lookahead
 // * (which can re-invoke gated predicates arbitrarily many times during simulation
 // * and corrupt any mutation of shared state).
 // *
 // * Strategy (forward-walk, insert at index+1):
 // *   • Call TokenStream#fill() to ensure the entire input is buffered.
 // *   • Iterate tokens from i = 0..tokens.size()-1. Because insertions happen
 // *     AFTER i (at i+1) we never re-visit an inserted token on the same loop
 // *     step (the loop counter monotonically increases; we don't re-scan).
 // *   • RSHIFT at position i: mutate tokens[i] to GT (char 0 only) and insert a
 // *     second GT at i+1 (covering char 1). Loop i steps over the first GT; next
 // *     iteration (i+1) visits the just-inserted second GT which is GT (not
 // *     RSHIFT) and is therefore a no-op.
 // *   • URSHIFT at position i: mutate tokens[i] to GT (char 0 only) and insert
 // *     RSHIFT (chars 1..2) at i+1. Loop i increments to i+1 which now sees the
 // *     RSHIFT (from the 3-char step) and on the NEXT iteration splits that RSHIFT
 // *     into two GTs (via the RSHIFT rule above). So URSHIFT becomes GT + GT + GT
 // *     across two successive iterations.
 // *
 // * NOTE: For expression-level shift operators (e.g. `x >> 2`), we are replacing
 // *       RSHIFT/URSHIFT tokens with GT(s), which WILL break shift expressions
 // *       like `x >> 2` and `x >>> 3`. The ZOM spec deliberately requires
 // *       SPACE-SEPARATED `> >` / `> > >` spellings for expression-level shifts
 // *       is therefore implemented by the explicit spaced rules:
 // *         shiftExpr : shiftExpr GT GT additiveExpr            # exprShiftSpacedRshift
 // *                   | shiftExpr GT GT GT additiveExpr         # exprShiftSpacedUrshift
 // *                   | ... (LSHIFT_ASSIGN / RSHIFT_ASSIGN / URSHIFT_ASSIGN tokens) ...
 // *       So compact RSHIFT/URSHIFT at the EXPRESSION level are NOT valid grammar
 // *       anyway. This means pre-splitting them into individual GTs is strictly a
 // *       TYPE-level gain and has no correctness cost for expressions.
 // */
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

/* A12 compact-close: GATED PREDICATE that runs BEFORE the genericClose rule's GT
 // * terminal is matched (i.e. during prediction). Handles three cases:
 // *
 // *  - LA(1) == GT       : no split needed, return true → parser matches plain GT
 // *  - LA(1) == RSHIFT   : lexer produced a single ">>" (2-char) token for two `>`
 // *                        closers. We MUTATE the current token to make it look like
 // *                        a single GT (1 char, first >), and INJECT a synthetic
 // *                        second GT token into the BufferedTokenStream at position
 // *                        p+1 (so the *next* genericClose invocation will consume it).
 // *                        Returns true.
 // *  - LA(1) == URSHIFT  : same as RSHIFT, but ">>>" (3-char). Mutate to GT, inject
 // *                        RSHIFT (2 remaining chars) at p+1; the subsequent
 // *                        genericClose will trigger this predicate again and split
 // *                        the RSHIFT into two GTs.
 // *
 // * Because this is a GATED predicate positioned BEFORE the actual terminal, the
 // * parser's decision engine sees only a single GT alt, drastically simplifying
 // * SLL→ALL(*) lookahead. Works with reflection to reach BufferedTokenStream's
 // * protected backing list and cursor.
 // */
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
 // * AFTER the parser has consumed the current RSHIFT/URSHIFT.
 // *
 // * Context: the lexer performs longest-match and always turns consecutive >
 // * characters into RSHIFT (>>) or URSHIFT (>>>) tokens. But generic type
 // * arguments close one per > (e.g. Vec<Vec<i32>> needs two independent
 // * genericClose matches, one for Vec<i32> and one for outer Vec). When
 // * genericClose matches RSHIFT (covers 2 > chars), the outer close still
 // * needs a single > — we therefore inject the remainder back into the stream.
 // *
 // * Because this is a plain semantic action (not gated predicate), it runs
 // * AFTER the parser has successfully matched and CONSUMED the RSHIFT/URSHIFT
 // * terminal, so TokenStream.p has already advanced to the next slot. We
 // * INSERT the remainder token at index p — making it LT(1) for the next
 // * parser decision, which is exactly the outer genericClose context.
 // *
 // * For RSHIFT: insert GT (remaining 1 >). For URSHIFT: insert RSHIFT
 // * (remaining 2 >), which itself will recursively trigger this function
 // * again in the subsequent genericClose. Reflection is used to access
 // * BufferedTokenStream's protected fields (tokens list and pointer p).
 // */
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
     // *  of an import declaration. Accepts `*` or any attributePath (contains `::`). */
    static boolean rejectImportBareIdUnlessStarOrAttrPath(
            org.antlr.v4.runtime.RuleContext importClauseCtx, Object parser) {
        if (importClauseCtx == null) return true;

        org.antlr.v4.runtime.ParserRuleContext pr = (org.antlr.v4.runtime.ParserRuleContext)importClauseCtx;


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

    // =========================================================================

    static boolean checkAttributedStatementTarget(
            org.antlr.v4.runtime.RuleContext attrsCtx,
            org.antlr.v4.runtime.RuleContext stmtCtx,
            Object parser) {
        if (attrsCtx == null || attrsCtx.getChildCount() == 0) return true;

        if (stmtCtx != null && stmtCtx.getClass().getSimpleName().contains("StmtExpr")) {
            throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                "AttributeRequiresSupportedTarget: attributes cannot prefix an expression statement");
        }

        return true;
    }

    static boolean peekIsGenericMultiSeg(Object parser) {
        if (!(parser instanceof ZomParser)) return false;
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
     // *  the given token-type. Exception-safe; returns false on EOF. */
    static boolean isNextToken(Object parser, int tokenType) {
        if (!(parser instanceof ZomParser)) return false;
        org.antlr.v4.runtime.TokenStream ts;
        try { ts = ((ZomParser) parser).getTokenStream(); }
        catch (Exception ignore) { return false; }
        try { return ts.LA(1) == tokenType; }
        catch (Exception ignore) { return false; }
    }

    /** Bounded 2-token lookahead helper: returns true iff TokenStream.LA(2) equals
     // *  the given token-type. Exception-safe; returns false on EOF. */
    static boolean la2Is(Object parser, int tokenType) {
        if (!(parser instanceof ZomParser)) return false;
        org.antlr.v4.runtime.TokenStream ts;
        try { ts = ((ZomParser) parser).getTokenStream(); }
        catch (Exception ignore) { return false; }
        try { return ts.LA(2) == tokenType; }
        catch (Exception ignore) { return false; }
    }

    /** Bounded lookahead helper for the single-token :: spelling. */
    static boolean la2IsColonColon(Object parser) {
        if (!(parser instanceof ZomParser)) return false;
        org.antlr.v4.runtime.TokenStream ts;
        try { ts = ((ZomParser) parser).getTokenStream(); }
        catch (Exception ignore) { return false; }
        try { return ts.LA(2) == COLONCOLON; }
        catch (Exception ignore) { return false; }
    }

    static boolean peekIsMarkerImplAfterOptionalUnsafe(Object parser, boolean hasUnsafePrefix) {
        if (!(parser instanceof ZomParser)) return false;
        org.antlr.v4.runtime.TokenStream ts;
        try { ts = ((ZomParser) parser).getTokenStream(); }
        catch (Exception ignore) { return false; }
        int implOffset = hasUnsafePrefix ? 2 : 1;
        try {
            org.antlr.v4.runtime.Token implTok = ts.LT(implOffset);
            if (implTok == null || !"impl".equals(implTok.getText())) return false;
            return peekIsMarkerImplRestFromOffset(parser, implOffset + 1);
        } catch (Exception ignore) {
            return false;
        }
    }

    static boolean peekIsOrdinaryImplAfterOptionalUnsafe(Object parser, boolean hasUnsafePrefix) {
        if (!(parser instanceof ZomParser)) return false;
        org.antlr.v4.runtime.TokenStream ts;
        try { ts = ((ZomParser) parser).getTokenStream(); }
        catch (Exception ignore) { return false; }
        int implOffset = hasUnsafePrefix ? 2 : 1;
        try {
            org.antlr.v4.runtime.Token unsafeTok = hasUnsafePrefix ? ts.LT(1) : null;
            if (hasUnsafePrefix && (unsafeTok == null || !"unsafe".equals(unsafeTok.getText()))) {
                return false;
            }
            org.antlr.v4.runtime.Token implTok = ts.LT(implOffset);
            if (implTok == null || !"impl".equals(implTok.getText())) return false;
            return !peekIsMarkerImplRestFromOffset(parser, implOffset + 1);
        } catch (Exception ignore) {
            return false;
        }
    }

    static boolean peekIsMarkerImplRestFromOffset(Object parser, int offset) {
        if (!(parser instanceof ZomParser)) return false;
        org.antlr.v4.runtime.TokenStream ts;
        try { ts = ((ZomParser) parser).getTokenStream(); }
        catch (Exception ignore) { return false; }
        try {
            if (ts.LA(offset) == LT) {
                int depth = 0;
                while (true) {
                    int tokenType = ts.LA(offset);
                    if (tokenType == EOF) return false;
                    if (tokenType == LT) {
                        ++depth;
                    } else if (tokenType == GT) {
                        --depth;
                        if (depth == 0) {
                            ++offset;
                            break;
                        }
                    } else if (tokenType == RSHIFT) {
                        depth -= 2;
                        ++offset;
                        if (depth <= 0) break;
                        continue;
                    } else if (tokenType == URSHIFT) {
                        depth -= 3;
                        ++offset;
                        if (depth <= 0) break;
                        continue;
                    }
                    ++offset;
                }
            }

            if (ts.LA(offset) == NOT) return true;

            int cursor = offset;
            int segments = 0;
            boolean sawMarkerSegment = false;
            while (true) {
                org.antlr.v4.runtime.Token segment = ts.LT(cursor);
                if (segment == null) return false;
                int segmentType = segment.getType();
                if (segmentType == EOF || segmentType == FOR) return false;

                ++segments;
                if ("marker".equals(segment.getText())) { sawMarkerSegment = true; }

                if (ts.LA(cursor + 1) != COLONCOLON) { break; }
                cursor += 2;
            }

            return segments >= 2 && sawMarkerSegment && ts.LA(cursor + 1) == FOR;
        } catch (Exception ignore) {
            return false;
        }
    }

    /** Verify that a two-token compound spelling has no trivia between tokens. */
    static boolean checkAdjacent(Token left, Token right, String spelling) {
        if (left == null || right == null) return true;
        if (left.getStopIndex() + 1 == right.getStartIndex()) return true;
        throw new ParseCancellationException(
            "tokens for `" + spelling + "` must be adjacent with no whitespace or comments");
    }

    /** Convenience: bounded 1-token lookahead for "LA(1) == IDENTIFIER AND LA(2) == type".
     // *  Used to gate attrs whose leading two tokens determine the dispatch class. */
    static boolean isNextTokenAfterIdent(Object parser, int tokenType) {
        return isNextToken(parser, IDENTIFIER) && la2Is(parser, tokenType);
    }

    // ---------------------------------------------------------------------
    static boolean checkExternAbiFormat(String literalText, Object parser) {

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
                    "unknown FFI ABI '" + inner
                    + "'; expected one of {\"C\", \"Cdecl\", \"system\", \"zom-cdecl\"}");
        }
    }

    static boolean checkIsExternKeyword(String text, Object parser) {
        if (!text.equals("extern")) return false;
        return true;
    }

    static boolean checkIsVariableKeyword(String text, Object parser) {
        if (!text.equals("variable")) return false;
        return true;
    }

    // =========================================================================





    static {
        try {
            java.lang.reflect.Field f =
                org.antlr.v4.runtime.atn.ParserATNSimulator.class
                    .getDeclaredField("predictionModeThreshold");
            f.setAccessible(true);
            f.setInt(null, 100000);
        } catch (Throwable t) {

        }
    }
}

// ============================================================================

//      ModuleDeclaration + ModuleItem*
//


// ============================================================================
sourceFile







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
    }
    : moduleDeclaration? moduleItem* EOF
    ;

moduleDeclaration

    //                       | 'module' Identifier '{' ModuleItem* '}'
    //                       | 'export'? 'module' Identifier '=' AttributePath ';'
    : MODULE identifier SEMICOLON                                                           # moduleDeclSimple
    | MODULE identifier LBRACE moduleItem* RBRACE                                            # moduleDeclBlock
    | EXPORT? MODULE identifier ASSIGN moduleAliasPath SEMICOLON                             # moduleDeclAlias
    ;

moduleAliasPath
    : identifier COLONCOLON identifier (COLONCOLON identifier)*
    ;

moduleItem
    : outerAttributeList declaration
        { rejectTopLevelVisibility($declaration.ctx); }                                    # moduleItemDeclaration

    | attrs=outerAttributeList statement
        { checkAttributedStatementTarget($attrs.ctx, $statement.ctx, this) }? # moduleItemStatementAttributed
    | statement                                                                             # moduleItemStatement
    ;

// ============================================================================

//




//     impl Interface(+Marker)* for Type { ... }          (Standalone)
//     unsafe? impl !? AttrPath for Type (body | ';')      (Marker)
// ============================================================================
declaration
    : IMPORT importBody SEMICOLON                                                            # importDeclaration
    | EXPORT exportBody                                                                      # exportDeclaration


    // A class header has at most one superclass. Interface implementations use
    // standalone `impl Interface for Type {}` declarations.
    | modifierList CLASS memberIdentifier
      typeParameters?
      ( COLON typeExpr )?
      whereClause?
      classBody                                                                             # classDeclaration


    // Interfaces are implemented via standalone impl.
    | modifierList STRUCT memberIdentifier
      typeParameters?
      whereClause?
      structBody                                                                            # structDeclaration   // named fields only (G5: no positional/newtype)


    // Multiple super-interfaces form a conjunction.
    | modifierList INTERFACE memberIdentifier
      typeParameters?
      ( COLON interfaceBoundList )?
      interfaceBody                                                                         # interfaceDeclaration

    | modifierList ENUM memberIdentifier
      typeParameters?
      enumBody                                                                              # enumDeclaration       // unit + tuple only (G6: no brace variant)


    | modifierList ERROR memberIdentifier
      errorBody                                                                             # errorDeclaration

    | modifierList FUN memberIdentifier
      typeParameters?
      functionSignature
      whereClause?
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





    | { peekIsMarkerImplAfterOptionalUnsafe(this, true) }?
      unsafeTok=identifier { checkIsUnsafePrefix($unsafeTok.text, this) }?
      markerImplRest                                                                        # markerImplUnsafe

    | { peekIsMarkerImplAfterOptionalUnsafe(this, false) }?
      markerImplRest                                                                        # markerImplPlain


    // impl <GenericParams>? InterfaceName ('+' MarkerPath)* for Type { ImplMember* }
    | { peekIsOrdinaryImplAfterOptionalUnsafe(this, true) }?
      unsafeTok=identifier { checkIsUnsafePrefix($unsafeTok.text, this) }?
      implTok=identifier   { checkIsImplKeyword($implTok.text, this) }?
      typeParameters?
      interfaceBoundList
      FOR typeExpr
      whereClause?
      implBody                                                                              # standaloneUnsafeImplDeclaration

    | { peekIsOrdinaryImplAfterOptionalUnsafe(this, false) }?
      implTok=identifier   { checkIsImplKeyword($implTok.text, this) }?
      typeParameters?
      interfaceBoundList
      FOR typeExpr
      whereClause?
      implBody                                                                              # standaloneImplDeclaration


    | externDecl                                                                            # externDeclarationTop


    ;

// ---------- MarkerImpl common stem (after optional unsafe prefix)
markerImplRest
    : implTok=identifier   { checkIsImplKeyword($implTok.text, this) }?
      typeParameters?
      ( NOT )?
      markerImplPath
      FOR typeExpr
      whereClause?
      ( SEMICOLON | structBody )
    ;

markerImplPath
    : pathSegment ( colonColon pathSegment )*
    ;

// ---------- Interface-bound list (17-gr line 294)
//   InterfaceBoundList = InterfaceName('<'GenericArgs'>')? ('+' MarkerPath)*
// Industry convention (Rust / Swift) - strict separation of conjunctions
// vs disjunctions:
//   * PLUS  (+) = CONJUNCTION (AND) - "all bounds apply" at impl / generic-bound /
//                                      interface-heritage / dyn existential positions.
//   * BIT_OR (|) = DISJUNCTION (OR)  - ONLY valid at UnionType / TypeExpression level
//                                     (a value is one of several possible types).
// Preventing `|` at impl-bound position blocks nonsense such as
//   `impl (Read | Write) for Foo` - which would semantically mean "either Read
// or Write is implemented" but coherence requires the set of implemented
// interfaces to be FIXED and exhaustive.
//
// Individual bound: identifier, optionally qualified downstream by :: in the
// semantic pass, plus optional generic instantiation args (Foo<T>).
// 1+ segment interface/marker path.
//   * 1-segment  -> local/imported interface name, e.g. `Serialize`, `Debug`
//   * 2+ segment -> fully qualified marker/interface name, e.g. `core::marker::Send`
//   Marker impls use `attributePath`; negative marker impls are syntactically
//   disambiguated by `!`, while positive short marker names are resolved by S1.
qualifiedPathOrIdent
    : pathSegment ( colonColon pathSegment )*
    ;
interfaceBound
    : qualifiedPathOrIdent ( LT typeArgList genericClose )?
    ;
interfaceBoundList
    : interfaceBound ( PLUS interfaceBound )*
    ;

colonColon
    : COLONCOLON
    ;


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
    ;


// ============================================================================

// ============================================================================
importBody

    //                        | AttributePath ('.' | '::')? '{' ImportSpecList? '}'
    //                        | AttributePath '..' AttributePath ( 'as' Identifier )?

    // Bare single-segment imports are rejected (`import std;`, `import std as s;`).


    : importClause { rejectImportBareIdUnlessStarOrAttrPath($importClause.ctx, this) }?  # importSimple
    | importClause AS identifier
      { rejectImportBareIdUnlessStarOrAttrPath($importClause.ctx, this) }?                  # importRename
    | importQualifiedPath colonColon LBRACE importSpecList? RBRACE                           # importGroup
    ;






importQualifiedPath
    : pathSegment ( colonColon pathSegment )*
    ;

importClause
    // Bare identifier is rejected at importBody top-level.
    : identifier
    | attributePath
    ;


importSpecList
    : importSpec ( COMMA importSpec )* COMMA?
    ;




importSpec
    : identifier ( AS identifier )?
    ;

exportBody

    //                        | AttributePath ('.' | '::')? '{' ImportSpecList? '}'

    : LBRACE exportSpecList RBRACE SEMICOLON                                                # exportGroup
    | importQualifiedPath colonColon LBRACE importSpecList? RBRACE SEMICOLON                 # exportReexportGroup
    | declaration                                                                           # exportDeclDirect
    ;


exportSpecList
    : exportSpec ( COMMA exportSpec )* COMMA?
    ;



exportSpec
    : identifier ( AS identifier )?
    ;

// ============================================================================

// ============================================================================



modifier
    : PUBLIC    | PRIVATE   | PROTECTED | STATIC
    | READONLY  | MUTATING  | OVERRIDE  | ABSTRACT
    | EXPORT
    ;


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




//
//       declaration-name helper (memberIdentifier) = IDENTIFIER | INIT | DEINIT | GET | SET

classBody : LBRACE classMember* RBRACE ;




memberIdentifier
    : identifier
    | INIT
    | DEINIT
    | GET
    | SET
    ;

declaredDefinitionName
    : identifier
    | INIT
    | DEINIT
    | GET
    | SET
    | THIS
    ;




structBody : LBRACE structMember* RBRACE ;

errorBody  : LBRACE structMember* RBRACE ;




classMember
    : modifierList FUN memberIdentifier typeParameters? functionSignature
      ( SEMICOLON
      | blockBody { checkAbstractNoBlock($modifierList.ctx, this) }?
      )                                                                                     # classMethod
    | modifierList (INIT | DEINIT) parameterList (RAISES typeExpr)? blockBody # classCtor
    // Class value member: mut / let / const (Ch.06 + Ch.08 class field)
    // Visibility (public/private/protected/static/readonly) still flows from modifierList.
    | modifierList MUT variableDeclarationList SEMICOLON                # classMut
    | modifierList LET variableDeclarationList SEMICOLON                # classLet
    | modifierList CONST constDeclarationList SEMICOLON                 # classConst
    // Class value field: bare value decl like `public name: String = default;
    // (Ch.08 class field — separate from classMut/classLet that require MUT/LET keyword)
    | modifierList memberIdentifier COLON typeExpr ( ASSIGN expression )?
      ( SEMICOLON | COMMA | { okAfterStructFieldNoSeparator(this) }? )                                # classField





    | modifierList GET memberIdentifier functionSignature blockBody
      ( modifierList SET memberIdentifier functionSignature blockBody
      | SET memberIdentifier functionSignature blockBody
      )?                                                                                   # classProperty
    ;






structMember


    : modifierList (MUT | readonly)? memberIdentifier COLON typeExpr (ASSIGN expression)?
      ( SEMICOLON | COMMA | { okAfterStructFieldNoSeparator(this) }? )            # structField
    | modifierList FUN memberIdentifier typeParameters? functionSignature
      ( SEMICOLON | blockBody )                                                             # structMethod
    | modifierList (INIT | DEINIT) parameterList (RAISES typeExpr)? blockBody   # structCtor
    ;

// --- Interface -------------------------------------------------------------------------

interfaceBody : LBRACE interfaceMember* RBRACE ;



interfaceMember
    : modifierList FUN memberIdentifier typeParameters? functionSignature SEMICOLON    # interfaceMethod
    | modifierList (GET | SET) memberIdentifier functionSignature SEMICOLON            # interfaceProperty
    | modifierList TYPE memberIdentifier typeParameters?
      ( COLON interfaceBoundList )? ( ASSIGN typeExpr )? SEMICOLON                                       # interfaceAssocType
    ;

// --- Enum ------------------------------------------------------------------------------

enumBody : LBRACE enumVariantList? RBRACE ;


enumVariantList : enumVariant ( COMMA enumVariant )* COMMA? ;

enumVariant


    : identifier
      ( LPAREN variantTypeList RPAREN )?
      ( ASSIGN expression )?
    ;


// NOTE: This is different from union types used by raises clauses and bounds.
variantTypeList : typeExpr ( COMMA typeExpr )* COMMA? ;

// --- Function signature ---------------------------------------------------------------

// NOTE: raises clause allowed either:
//       (a) WITH a return type: `parameters -> ReturnType raises X | Y`
//       (b) WITHOUT return type (standalone raises): `parameters raises X | Y`
//       (c) Neither: just `parameters`
functionSignature
    : parameterList ( ARROW typeExpr ( RAISES typeExpr )? )?
    | parameterList RAISES typeExpr
    ;


parameterList
    : LPAREN ( parameter ( COMMA parameter )* COMMA? )? RPAREN
    ;


parameter
    : outerAttributeList identifier COLON typeExpr ( ASSIGN expression )?
    | outerAttributeList THIS ( COLON typeExpr )?
    ;


typeParameters
    : LT typeParameter ( COMMA typeParameter )* COMMA? genericParamClose
    ;


// Bounds are trait conjunctions (PLUS-chain), same as impl head / dyn existential.
// Order: variance? NAME : Bound+ = Default
// Examples:
//   fun f<T, U: number = i32, V: Eq + Hash>(x: T) -> str { ... }
//   class Vec<out T: Any> { ... }
//   interface Map<K, in V> { ... }
typeParameter
    : variance? identifier ( COLON typeParameterBoundList )? ( ASSIGN typeExpr )?
    ;

typeParameterBoundList
    : typeExpr ( PLUS typeExpr )*
    ;

whereClause
    : whereTok=identifier { checkIsWhereKeyword($whereTok.text, this) }?
      wherePredicate ( COMMA wherePredicate )* COMMA?
    ;

wherePredicate
    : typeExpr COLON typeExpr
    ;


// Default is INVARIANT (neither 'in' nor 'out' specified).
variance
    // A5: variance NOT supported per 24-truth table
    : IN  { rejectVarianceIn(this) }?
    | OUT { rejectVarianceOut(this) }?
    ;


// compact RSHIFT (>>) / URSHIFT (>>>) tokens into individual GT tokens. The
// parser therefore only ever sees a single plain `>` at type-argument close
// points. This eliminates ALL(*) prediction-edge cases with compact-close
// tokens completely.


// (see sourceFile @init, `preSplitAllCompactCloses`) we pre-split every RSHIFT (>>)
// and URSHIFT (>>>) into individual GT tokens, so genericClose only needs to
// match a single plain `>`. This keeps ALL(*) prediction simple and avoids
// state corruption from mutating the token stream during lookahead simulation.
genericClose : GT ;


// so `fun f<T, U>>` (unbalanced over-close, two `>`) is REJECTED.
genericParamClose
    : GT
    | RSHIFT  { rejectCompactClose(">>", this) }?
    | URSHIFT { rejectCompactClose(">>>", this) }?
    ;

// ============================================================================

// ============================================================================

outerAttributeList : outerAttribute* ;

outerAttribute

    : hash=HASH lbrack=LBRACK attrList RBRACK
      { checkAdjacent($hash, $lbrack, "#["); }
    ;


attrList : attrItem ( COMMA attrItem )* COMMA? ;

attrItem
    : attr
    ;

attr
    : { isNextToken(this, IDENTIFIER) && la2IsColonColon(this) }?
      first=identifier colonColon rest=attributePathTail
      ( LPAREN input=attrInput RPAREN
      | ASSIGN value=expression
      )?
      { rejectUnavailableConditionalAttribute($first.text, $rest.text); }            # genericMultiSeg
    | { isNextToken(this, IDENTIFIER) && !la2IsColonColon(this) }?
      name=identifier { isBuiltinSingleSegAttr($name.ctx.getText()) }?
      ( LPAREN input=attrInput RPAREN
      | ASSIGN value=expression
      )?                                                                            # builtinSingleSeg
    ;






pathSegment
    : identifier

    | CLASS | STRUCT | INTERFACE | ENUM | ERROR | ALIAS | FUN | MUT | LET | CONST
    | CONSTRUCTOR | INIT | DEINIT | GET | SET | ACCESSOR | DECLARE
    // -- Control Flow --
    | IF | ELSE | MATCH | WHEN | DEFAULT | CASE | FOR | WHILE | DO | BREAK | CONTINUE
    | RETURN | DEBUGGER | IN | OUT
    // -- Type --
    | I8 | I16 | I32 | I64 | U8 | U16 | U32 | U64 | F32 | F64
    | BOOL | STR | CHAR | NULL | UNIT | NEVER | ANY | OBJECT | SYMBOL | BIGINT | UNDEFINED
    // -- Modifier --
    | PUBLIC | PRIVATE | PROTECTED | STATIC | READONLY
    | MUTATING | OVERRIDE | ABSTRACT | GLOBAL | IMMEDIATE | INTRINSIC | UNIQUE
    // -- Operator --
    | AS | IS | TYPEOF | KEYOF | INFER | SATISFIES | ASSERTS | ASSERT
    | NEW | THIS | SUPER | RAISES
    // -- Module --
    | MODULE | IMPORT | EXPORT | FROM | USING | REQUIRE
    // -- Concurrency --
    | SUSPEND | SPAWN
    // -- Reserved syntax --
    | THROW | TRY | CATCH | FINALLY | ASYNC | AWAIT | VAR
    | ACTOR | CHANNEL | YIELD | GENERATOR | NAMESPACE | PACKAGE | TYPE
    | DELETE | INSTANCEOF | OF | WITH
    // -- Literal-like --
    | TRUE | FALSE | UNDERSCORE
    ;


// `Identifier ::`. Combined with the `first=identifier` + `colonColon` in
// #genericMultiSeg, the total path still obeys G11 (at least one `::`).
// Example: `#genericMultiSeg first=foo :: attributePathTail=bar::baz`
//          → fully qualified path = `foo::bar::baz`
attributePathTail
    : pathSegment ( colonColon pathSegment )*
    ;



//   a) Expression list: `#[inline(true, cold)]` — 1+ expression, comma separated
//   b) KV list:         `#[link(name="c", kind="static")]`
//   c) Mixed:           `#[route::register(get("/x"), p = 10)]`


attrInput
    : attrInputItem ( COMMA attrInputItem )* COMMA?
    ;
attrInputItem

    : { la2Is(this, ASSIGN) }? pathSegment ASSIGN attrInputValue                          # attrInputKVItem
    | attrInputValue                                                                      # attrInputExprItem
    ;

attrInputValue
    : nestedAttrInputBlock
    | expression
    ;

nestedAttrInputBlock
    : LBRACE attrInput? RBRACE
    ;

attributePath



    : pathSegment ( colonColon pathSegment )+
    ;

// ============================================================================

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

    : expression SEMICOLON
    ;



labeledStatement
    : identifier COLON labelTarget
    ;

labelTarget
    : whileStatement
    | doWhileStatement
    | forStatement
    | forInStatement
    | blockBody
    | labeledStatement
    ;

// ---- Control flow ---------------------------------------------------------------------

ifStatement
    : IF parenExpression statement ( ELSE statement )?
    ;


matchStatement
    : MATCH parenExpression LBRACE matchClause* ( DEFAULT ROCKET statementList )? RBRACE
    ;


matchClause : WHEN pattern (IF expression)? ROCKET statement ;


whenStatement
    : WHEN parenExpression LBRACE whenClause* ( DEFAULT COLON statementList )? RBRACE
    ;


whenClause  : expression COLON statementList ;


whileStatement : WHILE parenExpression statement ;


doWhileStatement : DO statement WHILE parenExpression SEMICOLON ;

forStatement

    : FOR LPAREN
        init=forInit? SEMICOLON
        cond=expression? SEMICOLON
        upd=forUpdate?
      RPAREN statement
    ;

// 17-gr ForInit = ('mut' | 'let') VariableDeclarationList | Expression
forInit    : MUT variableDeclarationList | LET variableDeclarationList | expressionList ;

forUpdate  : expressionList ;


// Leading variants: for bare (mutable-or-inferred) pattern / let binding / mut binding.
forInStatement
    : FOR LPAREN (MUT | LET)? pattern IN expression RPAREN statement
    ;


breakStatement    : BREAK identifier? SEMICOLON ;

continueStatement : CONTINUE identifier? SEMICOLON ;

returnStatement   : RETURN expression? SEMICOLON ;

debuggerStatement : DEBUGGER SEMICOLON ;



spawnStatement
    : SPAWN spawnModifierList? ( spawnBlockBody | expression ) SEMICOLON?
    ;


spawnModifierList
    : spawnModifier+
    ;





spawnModifier
    : { la1IsSpawnModifierName(this) }? id=IDENTIFIER                                       # spawnModifierName
    | { la1IsSpawnModifierCall(this) }? id=IDENTIFIER LPAREN arg=IDENTIFIER RPAREN          # spawnModifierCall
    ;


suspendStatement
    : SUSPEND
      (
          untilTok=IDENTIFIER expr=expression SEMICOLON
            { checkSuspendUntil($untilTok.text, this) }?
        | SEMICOLON
      )
    ;

// ---- Reserved syntax -------------------------------------------------------
//   NOTE: Use semantic predicate `{ ... }?` (not bare action) so ANTLR does
//   not inject an unreachable `break;` after the action, which would make
//   javac complain.

reservedSyntax
    : ( THROW | TRY | CATCH | FINALLY ) expressionStatement?
      { reserved("exception syntax is not accepted (throw/try/catch/finally)", this) }?
    | ( ASYNC | AWAIT ) expressionStatement?
      { reserved("async/await syntax is not accepted", this) }?
    | VAR expressionStatement?
      { reserved("var syntax is not accepted; use let/mut/const", this) }?
    | ( ACTOR | CHANNEL ) expressionStatement?
      { reserved("actor/channel syntax is not accepted", this) }?
    | ( YIELD | GENERATOR ) expressionStatement?
      { reserved("generator/yield syntax is not accepted", this) }?
    | ( NAMESPACE | PACKAGE ) expressionStatement?
      { reserved("namespace/package syntax is not accepted", this) }?
    | TYPE expressionStatement?
      { reserved("top-level type declaration is not accepted; use alias", this) }?
    | ( DELETE | INSTANCEOF | OF | WITH ) expressionStatement?
      { reserved("delete/instanceof/of/with syntax is not accepted", this) }?
    ;

// ---- Block ---------------------------------------------------------------------------

blockBody : LBRACE statementList RBRACE ;

spawnBlockBody : LBRACE statementList expression? RBRACE ;

statementList : moduleItem* ;


parenExpression : LPAREN expression RPAREN ;

expressionList  : expression ( COMMA expression )* COMMA? ;

// ============================================================================


//                → LogicalOr → LogicalAnd → BitwiseOr → BitwiseXor → BitwiseAnd
//                → Equality → Relational → Shift → Additive → Multiplicative
//                → Power → UnaryPrefix → IncrementPrefix → Postfix → Call → Primary
// ============================================================================

// Level 21: Comma

expression
    : expression COMMA assignmentExpr                                                      # exprComma
    | assignmentExpr                                                                       # exprAssignSingle
    ;

// Level 20: Assignment (right-associative)

assignmentExpr
    : <assoc=right> conditionalExpr assignmentOp assignmentExpr                           # exprAssignment
    | conditionalExpr                                                                      # exprConditionalSingle
    ;


// NOTE: Spaced ">>>=" writes as "> > >="; the parser accepts both the lexer-level

assignmentOp
    : ASSIGN | MUL_ASSIGN | DIV_ASSIGN | MOD_ASSIGN | PLUS_ASSIGN | MINUS_ASSIGN
    | LSHIFT_ASSIGN | RSHIFT_ASSIGN | URSHIFT_ASSIGN
    | BIT_AND_ASSIGN | BIT_XOR_ASSIGN | BIT_OR_ASSIGN
    | POW_ASSIGN | AND_ASSIGN | OR_ASSIGN | NULL_COALESCE_ASSIGN
    // Spaced-form equivalents for fixtures that spell compound tokens with
    //   whitespace between characters (readability aids in reference tests):
    | GT GT GT ASSIGN    // ">>>=" written with spaces = URSHIFT_ASSIGN equivalent
    | GT GT ASSIGN       // ">>=" written with spaces = RSHIFT_ASSIGN equivalent
    | GT GT GTE          // "> > >=" spaced URSHIFT_ASSIGN where third GT+ASSIGN lexed as GTE
    ;

// Level 19: Ternary cond ? expr : expr (right-associative)

conditionalExpr
    : <assoc=right> errorDefaultExpr QUESTION expression COLON conditionalExpr            # exprTernary
    | errorDefaultExpr                                                                    # exprErrorDefaultSingle
    ;

// Level 18: ErrorDefault `?:`

errorDefaultExpr
    : <assoc=right> nullCoalesceExpr ERROR_DEFAULT errorDefaultExpr                      # exprErrorDefault
    | nullCoalesceExpr                                                                    # exprNullCoalesceSingle
    ;



nullCoalesceExpr
    : <assoc=right> logicalOrExpr NULL_COALESCE nullCoalesceExpr                          # exprNullCoalesce
    | logicalOrExpr                                                                       # exprLogicalOrSingle
    ;

// Level 16: Logical OR

logicalOrExpr
    : logicalOrExpr OR logicalAndExpr                                                     # exprLogicalOr
    | logicalAndExpr                                                                      # exprLogicalAndSingle
    ;

// Level 15: Logical AND

logicalAndExpr
    : logicalAndExpr AND bitwiseOrExpr                                                    # exprLogicalAnd
    | bitwiseOrExpr                                                                       # exprBitwiseOrSingle
    ;

// Level 14: Bitwise OR

bitwiseOrExpr
    : bitwiseOrExpr BIT_OR bitwiseXorExpr                                                 # exprBitwiseOr
    | bitwiseXorExpr                                                                      # exprBitwiseXorSingle
    ;

// Level 13: Bitwise XOR

bitwiseXorExpr
    : bitwiseXorExpr BIT_XOR bitwiseAndExpr                                               # exprBitwiseXor
    | bitwiseAndExpr                                                                      # exprBitwiseAndSingle
    ;

// Level 12: Bitwise AND

bitwiseAndExpr
    : bitwiseAndExpr BIT_AND equalityExpr                                                 # exprBitwiseAnd
    | equalityExpr                                                                        # exprEqualitySingle
    ;

// Level 11: Equality

equalityExpr
    : equalityExpr ( EQ | NEQ | STRICT_EQ | STRICT_NEQ ) relationalExpr                   # exprEquality
    | relationalExpr                                                                      # exprRelationalSingleAlt
    ;



//


// NOTE: Right-hand side of `as` / `is` is a type (typeExpr), NOT a shift expression;
//       using shiftExpr would force `as i32` into a non-type expression and fail.
relationalExpr
    : relationalExpr AS (QUESTION | NOT)? typeExpr
      { checkAsRightIsNotDyn($typeExpr.ctx, this) }?                                  # exprRelationalAs
    | relationalExpr ( LT | GT | LTE | GTE ) shiftExpr                                   # exprRelational
    | relationalExpr IS typeExpr                                                         # exprIs
    | shiftExpr                                                                     # exprShiftSingle
    ;

// Level 9: Shift

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

additiveExpr
    : additiveExpr ( PLUS | MINUS ) multiplicativeExpr                                    # exprAdditive
    | multiplicativeExpr                                                                  # exprMultiplicativeSingle
    ;

// Level 7: Multiplicative

multiplicativeExpr
    : multiplicativeExpr ( MUL | DIV | MOD ) powerExpr                                    # exprMultiplicative
    | powerExpr                                                                           # exprPowerSingle
    ;

// Level 6: Power ** (right-associative)

powerExpr
    : <assoc=right> unaryExpr POW powerExpr                                               # exprPower
    | unaryExpr                                                                           # exprUnarySingle
    ;

// Level 5: Unary prefix

unaryExpr
    : BIT_AND MUT? unaryExpr                                                  # exprReference
    | ( PLUS | MINUS | NOT | BIT_NOT | TYPEOF | MUL ) unaryExpr              # exprUnary
    | preIncrementExpr                                                                    # exprPreIncSingle
    ;

// Level 4: Prefix ++ --

preIncrementExpr
    : ( PLUSPLUS | MINUSMINUS ) preIncrementExpr                                          # exprPreInc
    | postfixExpr                                                                         # exprPostfixSingle
    ;



postfixExpr
    : postfixExpr RAISES QUESTION                                                           # exprPostfixRaisesProp
    | postfixExpr ( PLUSPLUS | MINUSMINUS | ERROR_PROPAGATE | FORCE_UNWRAP )
      { checkPostfixLValue($ctx, this) }?                             # exprPostfixOp
    | callExpr                                                                            # exprCallSingle
    ;

// Level 2: Call / Member / Index / Optional chain

// NOTE: Lexer produces a single OPTIONAL_CHAIN `?.` token from `?.` — so accept it
//       directly. Continue to accept the QUESTION? PERIOD shape for whitespace-
//       separated `?.` spellings (QUESTION + PERIOD).



callExpr
    : callExpr LPAREN expressionList? RPAREN (RAISES typeExpr)?                           # exprCall
    | callExpr LT typeArgList genericClose LPAREN expressionList? RPAREN
      (RAISES typeExpr)?                                                                  # exprGenericCall
    | callExpr OPTIONAL_CHAIN LPAREN expressionList? RPAREN (RAISES typeExpr)?             # exprOptionalCall
    | callExpr QUESTION? PERIOD declaredDefinitionName                                    # exprMember
    | callExpr colonColon declaredDefinitionName                                          # exprQualifiedMember
    | callExpr OPTIONAL_CHAIN declaredDefinitionName                                      # exprOptionalMember
    | callExpr OPTIONAL_CHAIN LBRACK expression RBRACK                                     # exprOptionalIndex
    | callExpr QUESTION? LBRACK expression RBRACK                                         # exprIndex
    | primaryExpr                                                                         # exprPrimarySingle
    ;

// Level 1: Primary

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
    | SPAWN spawnModifierList? ( spawnBlockBody | assignmentExpr )                        # exprSpawn
    | functionExpression                                                                  # exprFunction
    | lambdaExpr                                                                          # exprLambda
    | objectLiteral                                                                       # exprObjectLiteral
    | structLiteral                                                                       # exprStructLiteral
    | tupleLiteral                                                                        # exprTupleLiteral
    | arrayLiteral                                                                        # exprArrayLiteral
    | parenExpression                                                                     # exprParen




    | unsafeBlockExpr                                                                     # exprUnsafeBlock

    | predefinedType                                                                      # exprPredefinedType
    ;

// ---- Literal ------------------------------------------------------------

literal
    : DECIMAL_LITERAL | BIGINT_LITERAL
    | BINARY_LITERAL | OCTAL_LITERAL | HEX_LITERAL
    | DOUBLE_STRING_LITERAL | CHAR_LITERAL
    | NO_SUBSTITUTION_TEMPLATE_LITERAL
    | templateLiteral
    | NULL
    | boolLiteral
    | UNIT
    ;


boolLiteral
    : TRUE | FALSE
    ;



templateLiteral
    : TEMPLATE_HEAD expression ( TEMPLATE_MIDDLE expression )* TEMPLATE_TAIL
    ;

// ---- Lambda -----------------------------------------------------------

lambdaExpr
    : parameterList (ARROW typeExpr ( RAISES typeExpr )? )? ROCKET blockBody               # lambdaBlock
    | parameterList (ARROW typeExpr ( RAISES typeExpr )? )? ROCKET expression              # lambdaExprArrow
    ;

// ---- Struct literal ---------------------------------------------------
structLiteral


    : identifier ( LT typeArgList genericClose )?
      LBRACE structFieldInit ( COMMA structFieldInit )* COMMA? RBRACE
    ;


structFieldInit
    : propertyName ( COLON expression )?
    ;

objectLiteral
    : LBRACE objectProperty ( COMMA objectProperty )* COMMA? RBRACE
    ;

objectProperty
    : propertyName ( COLON expression )?
    ;

propertyName
    : identifier
    | IN
    ;


//   - `()`          → unit literal (matches UNIT value)
//   - `(a,)`        → 1-tuple
//   - `(a, b)`      → 2-tuple (comma list ≥2)
tupleLiteral
    : LPAREN RPAREN                                                                        # tupleUnitLiteral
    | LPAREN expression COMMA expressionList? RPAREN                                       # tupleMultiLiteral
    ;

// ---- Array literal --------------------------------------------------

arrayLiteral
    : LBRACK expressionList? RBRACK
    ;

// ============================================================================

// ============================================================================

pattern
    : wildcardPat                                                                         # patWildcardFinal
    | literal                                                                             # patLiteral
    | ELLIPSIS pattern?                                                                   # patRest
    | enumPatternPath LPAREN pattern ( COMMA pattern )* COMMA? RPAREN                     # patEnumCall
    | enumPatternPath                                                                     # patEnumBare
    | bindPat                                                                             # patBindingFinal
    | IS typeExpr                                                                         # patIs
    // A9: `::`-qualified enum pattern REJECT (use '.' form e.g. Color.Red)
    | path=identifier colonColon name=identifier tupleLiteral?
      { rejectEnumColonCol($path.text, $name.text, this) }?
                                          # patEnum
    | LPAREN pattern ( COMMA pattern )* COMMA? RPAREN
      { checkRestPatternLast(_localctx, "tuple", this) }?                                 # patTuple
    | identifier LBRACE structPatternField ( COMMA structPatternField )* COMMA? RBRACE    # patStruct
    | LBRACK pattern ( COMMA pattern )* COMMA? RBRACK
      { checkRestPatternLast(_localctx, "array", this) }?                                 # patArray
    | expression                                                                          # patExpression
    ;

wildcardPat
    // UNDERSCORE is a hard token in ZomLexer.g4 (produced instead of IDENTIFIER for
    // a standalone underscore), so this rule matches literal UNDERSCORE token.
    : UNDERSCORE
    ;


bindPat
    : identifier ( AT pattern { checkBindPatNoNestedAt($pattern.ctx, this); } )?
      { checkBindPat($identifier.text, this) }?
    ;


structPatternField
    : ELLIPSIS identifier?
    | propertyName ( COLON pattern )?
    ;

enumPatternPath
    : identifier PERIOD propertyName ( PERIOD propertyName )*
    ;

// ============================================================================

//               to atomType.
//

// ============================================================================

typeExpr : functionType ;



functionType
    : typeParameters? functionTypeParameterList ( ARROW returnType ( RAISES typeExpr )? )  # typeFunction
    // FUN-keyword-prefixed function type: fun(T) -> U raises E? (industry-standard explicit syntax)
    | FUN typeParameters? functionTypeParameterList ( ARROW returnType ( RAISES typeExpr )? ) # typeFunctionKeyword
    | unionType                                                                           # typeUnionSingle
    ;

functionTypeParameterList
    : LPAREN ( typeExpr ( COMMA typeExpr )* COMMA? )? RPAREN
    ;

functionExpression
    : FUN typeParameters? parameterList captureClause?
      ( ARROW typeExpr ( RAISES typeExpr )? )? blockBody
    ;

captureClause
    : { la1IsCaptureClause(this) }? IDENTIFIER LBRACK captureList? RBRACK
    ;

captureList
    : captureElement ( COMMA captureElement )* COMMA?
    ;

captureElement
    : BIT_AND identifier
    | identifier
    | THIS
    ;


returnType : typeExpr ;

// Level 4: Union  `|`

unionType
    : unionType BIT_OR intersectionType                                                   # typeUnion
    | intersectionType                                                                    # typeIntersectionSingle
    ;

// Level 3: Intersection `&`

intersectionType
    : intersectionType BIT_AND postfixType                                                # typeIntersection
    | postfixType                                                                         # typePostfixSingle
    ;



// NOTE: NULL_COALESCE `??` appears here as a "double-optional" escape — the lexer
//       longest-match rule turns `T??` into `T NULL_COALESCE` rather than
//       `T QUESTION QUESTION`; accepting it at this tier restores the intended
//       "T optional, optional" (T??) semantics while NULL_COALESCE as a binary
//       operator continues to work at the expression level.


postfixType



    : postfixType LBRACK RBRACK                                                           # typeArrayPostfix
    | postfixType QUESTION                                                                # typeOptional
    | postfixType NULL_COALESCE                                                           # typeOptionalDouble
    | postfixType PERIOD memberIdentifier                                                 # typeMemberAccess
    | atomType                                                                            # typeAtomSingle
    ;

// Level 1: Atom types

atomType
    : predefinedType                                                                      # typePredefined
    | BIT_AND MUT? typeExpr                                                               # typeReference
    | MUL ( CONST | MUT )? typeExpr                                                       # typeRawPointer
    | dynType                                                                             # typeDyn
    | associatedTypeProjection                                                            # typeAssociatedProjection
    | attributePath ( LT typeArgList genericClose )?                                     # typeQualified
    | identifier ( LT typeArgList genericClose )?                                        # typeNamed
    | LPAREN RPAREN                                                                       # typeTupleEmpty
    | LPAREN typeExpr ( COMMA typeExpr )* COMMA? RPAREN
      { checkTupleTypeNot1Tuple(_localctx, this) }?                                   # typeTuple
    | LBRACE structFieldType ( COMMA structFieldType )* COMMA? RBRACE                     # typeObject
    | identifier LPAREN variantTypeList RPAREN                                                    # typeTupleVariant
    | LBRACE RBRACE                                                                        # typeObjectEmpty
    | LBRACK typeExpr (SEMICOLON expression)? RBRACK                                         # typeArrayLiteralAtom
    ;

dynType
    : dynTok=identifier { checkIsDynKeyword($dynTok.text, this) }?
      qualifiedPathOrIdent ( LT typeArgList genericClose )?
      dynAssocBindingArgs? ( PLUS markerPath )*
    ;

dynAssocBindingArgs
    : LT dynAssocBinding ( COMMA dynAssocBinding )* COMMA? genericClose
    ;

dynAssocBinding
    : identifier ASSIGN typeExpr
    ;

markerPath
    : qualifiedPathOrIdent
    ;

associatedTypeProjection
    : LT typeExpr AS typeExpr genericClose colonColon memberIdentifier
    ;


structFieldType
    : MUT? identifier ( COLON | QUESTION COLON | ERROR_DEFAULT ) typeExpr
    ;

// Eight fixed-width integer types plus float, bool, str, char, null, unit, never, and any.

predefinedType
    : I8 | I16 | I32 | I64 | U8 | U16 | U32 | U64 | F32 | F64
    | BOOL | STR | CHAR | NULL | UNIT | NEVER | ANY
    ;


typeArgList
    : typeExpr ( COMMA typeExpr )* COMMA?
    ;

// ============================================================================
// Identifier


// ============================================================================

identifier
    : IDENTIFIER
    ;

// ============================================================================

//




// ============================================================================




funDecl
    : FUN identifier
      functionSignature
      SEMICOLON
    ;

// 20.1 Unsafe block expression (escape hatch for unsafe operations)

unsafeBlockExpr
    : tok=identifier { checkIsUnsafePrefix($tok.text, this) }?
      LBRACE statement* expression? RBRACE
    ;

// 20.2 Extern declaration (foreign function interface)





//


externDecl
    : extTok=identifier    { checkIsExternKeyword($extTok.text, this) }?
      ( abi=DOUBLE_STRING_LITERAL )?
      externBlock
      { $abi == null || checkExternAbiFormat($abi.text, this) }?              # externBlockDecl
    ;

// 20.3 Extern block — groups multiple FFI declarations
externBlock
    : LBRACE externItem* RBRACE
    ;

// 20.4 Items permitted inside an `extern "C" { ... }` block



externItem
    : funDecl                                                                           # externFunDecl
    | vTok=identifier   { checkIsVariableKeyword($vTok.text, this) }?
      identifier COLON typeExpr SEMICOLON                                               # externVarDecl
    ;

// ============================================================================

//








// ============================================================================
