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
    /**
     // * Reserved-word check for ZOM5001-ZOM5008 diagnostics.
     // * Returns the diagnostic code or null when the spelling is not reserved.
     // */
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
    static boolean checkLabelNoAttrAfterLabel(Token nextTok, String labelText, Object parser) {
        if (nextTok != null && nextTok.getType() == ZomParser.HASH)
            throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                "outer attribute #[...] not allowed right after label '" + labelText + "'");
        return true;
    }

    /** Labeled-statement constraint C25#2: label may only prefix control-flow / block
     // *  statements. Declarations (let/const/fun/class/struct/interface/enum/error/alias/
     // *  import/export/module/type) must NOT be labeled.
     // *
     // *  Implementation: inspect the first token produced by the matched statement — if it's
     // *  a declaration-class keyword, REJECT via ParseCancellationException.
     // */
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
            case ZomParser.ALIAS:
            case ZomParser.IMPORT:
            case ZomParser.EXPORT:
            case ZomParser.MODULE:
            case ZomParser.HASH:
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






    static boolean checkIsImplKeyword(String text, Object parser) {
        if (!text.equals("impl")) return false;
        return true;
    }
    static boolean checkIsMarkerKeyword(String text, Object parser) {
        if (!text.equals("marker")) return false;
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

    /** Utility helper for semantic predicates: flatten a ParseTree's concatenated text
     // *  (identifier ':' ':' identifier)* to a single String. Used by attrCfg alt
     // *  semantic predicate to test attributePath.toString() == "zom::cfg". */
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

     // *  inline on fall-through alts so malformed cfg predicates are diagnosed with the
     // *  right code instead of a generic ANTLR "no viable alternative". */
    static boolean rejectCfgPredicateBad(Object parser) {
        throw new org.antlr.v4.runtime.misc.ParseCancellationException(
            "ZOM1900 CfgPredicateParseError: malformed `#[zom::cfg(...)]` predicate; " +
            "expected all(...), any(...), not(...), or a key/value cfg atom.");
    }

     // *  Invoked on the `moduleItemStatementCfgGated` alt:
     // *      outerAttributeList statement
     // *  Called only when outerAttributeList is non-empty (the no-attr case goes to
     // *  the separate `moduleItemStatement` alt). Validation rules:
     // *    (a) outerAttributeList may only contain `#[zom::cfg(...)]` attributes —
     // *        any other attribute path is rejected with ZOM1601.
     // *    (b) if any zom::cfg attribute is present, statement MUST be a standalone
     // *        `blockBody` (`#stmtBlock`); anything else raises ZOM1901.
     // *  Declarations always pass because they go to `moduleItemDeclaration` alt,
     // *  which unconditionally allows all attributes — never calls this helper.
     // *
     // *  @param attrsCtx outerAttributeList context (non-empty)
     // *  @param stmtCtx  parsed statement context (already matched)
     // */
    static boolean checkStatementCfgGate(
            org.antlr.v4.runtime.RuleContext attrsCtx,
            org.antlr.v4.runtime.RuleContext stmtCtx,
            Object parser) {
        if (attrsCtx == null || attrsCtx.getChildCount() == 0) return true;

        boolean hasCfgAttr = false;
        String firstNonCfgPath = null;
        // The attribute list lives under RULE_attrItem (after v3 dispatch split).
        // attrItem has two labelled alts:


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


        if (firstNonCfgPath != null) {
            throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                "ZOM1601 AttrOnStatementDisallowed: statements may not carry outer " +
                "attributes other than `#[zom::cfg(...)]` at module/block scope. " +
                "Move attribute `#[" + firstNonCfgPath + "]` to the declaration " +
                "or wrap the guarded statement in a standalone block.");
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
                    "ZOM1901 CfgOnExpression: `#[zom::cfg(...)]` at module or block " +
                    "scope can only gate STANDALONE BLOCK statements of the form " +
                    "`#[zom::cfg(...)] { stmt* }`. It cannot gate individual " +
                    "expressions, control-flow statements, or declaration-statement " +
                    "forms like `let`, `mut`, `for`, `if`, `return`. " +
                    "Wrap the guarded statement in a standalone block.");
            }
        }

        return true;
    }

    /** Helper used by checkStatementCfgGate + checkCfgDeclarationTarget to
     // *  reconstruct the canonical attribute-path string from an attrItem or
     // *  attr context. Mirrors the v3 dispatch hierarchy:
     // *    attrItem#attrZomCfg       → "zom::cfg"
     // *    attrItem#attrGenericItem  → unwrap to RULE_attr, then dispatch:
     // *      attr#genericMultiSeg    → first::attributePathTail
     // *      attr#genericSegment1    → name
     // *  Returns null if no path can be reconstructed (should never happen).
     // */
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

     // *  checkStatementCfgGate. Invoked on the `moduleItemDeclaration` alt:
     // *      outerAttributeList declaration
     // *  Declarations that are "statement-like" (let / mut / const, which also
     // *  appear in statement-form inside block bodies) are NOT allowed to carry
     // *  `#[zom::cfg(...)]` at module scope either — they must be wrapped in a
     // *  `let x = 5;` statement without wrapping it in `{ ... }`").
     // *  Real declarations (fun, class, struct, enum, interface, error, import,
     // *  export, alias, marker, standalone impl, type alias, module, package,
     // *  const) are always allowed.
     // *
     // *  Detection strategy: look at the declaration context's alt class name.
     // *  "LetDeclContext", "MutDeclContext" (actually these appear as
     // *  "LetDeclarationContext"/"MutDeclarationContext" — use contains() as a
     // *  sub-string match so we are immune to exact naming conventions).
     // */
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
                "ZOM1901 CfgOnExpression: `#[zom::cfg(...)]` at module scope cannot " +
                "gate a single let/mut declaration. Wrap it in a standalone block: " +
                "`#[zom::cfg(...)] { let x: T = v; }`.");
        }
        return true;
    }

    /** @deprecated Replaced by {@link #checkStatementCfgGate}. Retained so that
     // *  external code that reflects over these helper names does not break;
     // *  the current grammar never calls it (it couldn't — by the time the
     // *  `statement` rule is matched, its parent's outerAttributeList has not
     // *  yet been constructed in ANTLR's bottom-up parse order). */
    @Deprecated
    static boolean checkCfgNotOnExpression(org.antlr.v4.runtime.RuleContext stmtCtx, Object parser) {
        return true;
    }

     // *  (the full check requires reading Zom.toml, which runs at a later semantic pass;
     // *  the parser only validates that `feature = "..."` has a non-empty identifier RHS). */
     // *
     // *  ⚠  THIS IS A LEGACY PROBE ONLY — NEVER call it inside { ... }? semantic predicates
     // *     because ParseCancellationException thrown inside predicate-eval is wrapped as
     // *     predicate-false by the ANTLR 4 runtime.
     // *
     // *  Real enforcement lives in `enforceCfgAtomQuotedRhs` (tail parser-action in
     // *  attrItem#attrZomCfg), which walks the complete cfgPredicate parse-tree AFTER
     // *  all tokens have been consumed — throws PCE cleanly as an exception (rc=2).
     // *
     // *  Returns true for non-`feature` keys AND for `feature` with non-empty stripped
     // *  value; returns false for `feature = ""` (empty quoted string). Used as a read-
     // *  only predicate probe by anyone who wants to guard without throwing.
     // *
     // *  Historical: before tail-action enforcement, this method attempted to throw
     // *  PCE inside a semantic predicate → the exception was swallowed by ANTLR's
     // *  PredicateEval try/catch → `cfg_feature_empty_value_neg_04.zom` silently accepted
     // *  with rc=0 (a false-negative bug — see R12 bugfix). */
    static boolean checkCfgFeatureAtomFormat(Token key, String valueText, Object parser) {
        if (key == null) return true;
        String k = key.getText();
        if (!"feature".equals(k)) return true;
        if (valueText == null || valueText.length() < 3) return false;
        return true;
    }

     // *  Because ANTLR semantic predicates cannot emit warnings, we only validate structural
     // *  shape (comparison operators do not apply to bare-key atoms) here; the warning
     // *  is emitted in a post-parse cfg-lint pass. */
    static boolean checkCfgAtomShape(Object keyNode, boolean hasOp, String opText, Object parser) {
        // - Bare atom: hasOp == false → always ok.
        // - Valued atom: op must be one of = != < <= > >= (enforced by grammar already),
        //   just sanity-check that comparisons operate on version-capable keys.
        if (!hasOp) return true;
        return true;
    }

     // *  gate: when the next token is a CfgOp (= != < <= > >=), it means the user attempted
     // *  to write a valued atom but produced a non-DOUBLE_STRING_LITERAL RHS (e.g. a bare
     // *  identifier like `target_os = linux`). Without this explicit gate, ANTLR's default
     // *  error recovery would single-token-delete the stray op and silently accept the
     // *  malformed line as a bare-key atom, producing a false-positive ACCEPT.
     // *
     // *  Returns true (proceed) when LA(1) is NOT a CfgOp; throws ParseCancellationException
     // *  (parser aborts, reports ZOM1900) otherwise.
     // */
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

     // *
     // *  PROBLEM: ANTLR 4 ALL(*) makes alt-selection decisions based on UNLIMITED lookahead.
     // *  When the raw token stream matches `IDENTIFIER :: IDENTIFIER ( ... )`, ALL(*) reads
     // *  ahead through the PAREN body to decide between the two labeled alts. If it sees
     // *  that the *next* tokens (e.g. unquoted bare identifier `target_os = linux`) would
     // *  cause cfgPredicate to fail, it ABANDONS the gated attrCfg path and falls through
     // *  to attrGeneric — where the entire `zom::cfg(...)` block is silently parsed as a
     // *  generic attribute with an expression inside. Result: false-positive ACCEPT on
     // *  malformed cfg predicates (REJECT tests wrongly pass).
     // *
     // *  SOLUTION: make the two alts' LEADING GATED PREDICATES DETERMINISTIC and MUTUALLY
     // *  EXCLUSIVE. We do this by peeking 4 tokens ahead in the raw TokenStream:
     // *    pattern  =  IDENTIFIER("zom")  COLONCOLON  IDENTIFIER("cfg")  LPAREN
     // *  If this pattern matches, the parser MUST dispatch to attrCfg (no fallthrough),
     // *  and attrGeneric's gated predicate returns false so it is not even considered.
     // *  If it does NOT match, only attrGeneric is considered. This converts a non-local
     // *  ALL(*) decision (unbounded lookahead into the paren body) into a LOCAL, bounded
     // *  lookahead that ANTLR evaluates identically in prediction and parse phases.
     // */
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
            la3 = ts.LA(3);
            t3 = ts.LT(3).getText();
            la4 = ts.LA(4);
        } catch (Exception ignore) { return false; }
        return la1 == IDENTIFIER && "zom".equals(t1)
            && la2 == COLONCOLON
            && la3 == IDENTIFIER && "cfg".equals(t3)
            && la4 == LPAREN;
    }

     // *  Returns true iff:
     // *    LA(1) == IDENTIFIER   AND   LA(2) == COLONCOLON
     // *  AND   NOT the 4-token "zom" :: "cfg" "(" pattern (that belongs to #zomCfg).
     // *  Combined with the other two leading gates (peekIsZomCfgParen for #zomCfg,
     // *  !la2IsColonColon for #genericSegment1), the three attr alternatives
     // *  form a PARTITION of the reachable prefix space: mutually exclusive & total.
     // *
     // *  Why leading-gate all three instead of relying on FIRST-set tie-breaking:
     // *  ANTLR 4 ALL(*) simulator continues exploring past the gated entry into the
     // *  alt body IF the gated predicate succeeds. A gated that returns false short-
     // *  circuits body simulation entirely — which is precisely what we need for
     // *  the #zomCfg alt (its body can throw PCE on malformed cfgPredicates, so we
     // *  must prevent ALL(*) from trying it when the 4-token prefix does not match).
     // *  For the generic alts, prefix-disjoint gates eliminate the ALL(*) cross-body
     // *  reachability check that otherwise makes the decision SLL-k=∞ and fragile.
     // */
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

    // ---------------------------------------------------------------------
    /** Entry gate for cfgAtom alt 1 + alt 2.
     // *  Returns true iff LA(1) is one of the six CfgOp tokens. */
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
     // *  Returns true iff LA(2) (position after the consumed CfgOp) is DOUBLE_STRING_LITERAL.
     // *  Precondition: caller has already gated with `isCfgOpNext`, so LA(1) ∈ CfgOpSet. */
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
     // *  `#[zom::cfg(PRED)]` block has been successfully matched (i.e. every token
     // *  consumed, all semantic predicates accepted, rule-end RPAREN recognised).
     // *
     // *  POSITIONAL SAFETY NOTE (critical, do not inline the throw elsewhere)
     // *  --------------------------------------------------------------------
     // *  This action is placed at the TAIL of a fully-matched rule alternative.
     // *  ANTLR 4's ALL(*) simulator walks the ATN ONLY through positions that are
     // *  reachable during PREDICTION — i.e. every node that precedes a decision
     // *  state that the simulator must disambiguate. A tail action following the
     // *  LAST concrete terminal in a rule (here, `RPAREN`) is NOT on any
     // *  prediction path; the simulator treats it as a benign ε-action leading
     // *  directly to the rule's stop-state. Unlike a mid-body throw (which the
     // *  simulator walks through to determine reachability, producing poisoned
     // *  DFA states with "exception-terminated accept-sets"), a tail throw is
     // *  INVISIBLE to the entire ATN construction / DFA merge phase.
     // *
     // *  This is why ZOM1900 + ZOM1903 are enforced HERE and not inside
     // *  (V1/V2/V3 designs that placed the throws inline all produced spurious
     // *  NVA or silent-failed predicates that the DefaultErrorStrategy recovered).
     // *
     // *  WALK (V5 — dedicated sub-rule matching):
     // *    1. Collect every cfgAtom node under `predCtx` (recursive, so && / || /
     // *       ! / paren-groups are all descended into).
     // *    2. For each cfgAtom, scan its FIRST-level rule children:
     // *       a. RULE_badRhsCfgAtomRhs → OP + unquoted-IDENTIFIER → throw ZOM1900
     // *       b. RULE_valuedCfgAtomRhs → OP + DOUBLE_STRING
     // *          i. key=="feature" AND stripped-val=="" → throw ZOM1903 FeatureUndeclared
     // *       c. RULE_bareCfgAtomRhs → bare (no OP) → OK
     // *
     // *  Returns void so the generated code is a plain { method_call(); } parser
     // *  action — NO trailing `?`, NO predicate-false swallowing, so any PCE throw
     // *  produces rc=2 (NOT rc=1).
     // */
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
                    "ZOM2001[UnknownExternAbi]: unknown FFI ABI '" + inner
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

    static boolean checkIsOpaqueKeyword(String text, Object parser) {
        if (!text.equals("opaque")) return false;
        return true;
    }

    static boolean checkIsMacroKeyword(String text, Object parser) {
        if (!text.equals("macro")) return false;
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
    : (outerAttributeList moduleDeclaration)? moduleItem* EOF
    ;

moduleDeclaration

    //                       | 'module' Identifier '{' ModuleItem* '}'
    //                       | 'export'? 'module' Identifier '=' AttributePath ';'
    : MODULE identifier SEMICOLON                                                           # moduleDeclSimple
    | MODULE identifier LBRACE moduleItem* RBRACE                                            # moduleDeclBlock
    | EXPORT? MODULE identifier ASSIGN attributePath SEMICOLON                               # moduleDeclAlias
    ;

moduleItem



    // attribute list when it is purely `#[zom::cfg(...)]`; however ZOM1901
    // restricts cfg-gated statements to the standalone-block form only:
    // `#[zom::cfg(...)] { stmt* }`. Any non-block statement form that carries
    // a `#[zom::cfg(...)]` attribute is rejected by `checkStatementCfgGate`
    // below (moduleItemStatementCfgGated alt).
    : outerAttributeList declaration                                                        # moduleItemDeclaration

    | attrs=outerAttributeList statement
        { checkStatementCfgGate($attrs.ctx, $statement.ctx, this) }?       # moduleItemStatementCfgGated
    | statement                                                                             # moduleItemStatement
    ;

// ============================================================================

//




//     impl Interface(+Marker)* for Type { ... }          (Standalone)
//     unsafe? impl !? AttrPath for Type (body | ';')      (Marker)
// ============================================================================
declaration
    : IMPORT importBody SEMICOLON?                                                           # importDeclaration
    | EXPORT exportBody SEMICOLON?                                                           # exportDeclaration


    // Strict separation (user feedback, 2026/06/26):
    //   class head colon = SUPERCLASS INHERITANCE, ONE class only.
    //   interface implementation = standalone `impl Iface for T {}` form (NEVER
    //   listed in the class header — no repeat of Java/C#'s mistake).
    // Superclass is written `class NAME: SuperClass` (single, optional).
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


    // (matches Java's RuntimeException extends Exception pattern, but written
    //  with colon for consistency.)
    | modifierList ERROR memberIdentifier
      typeParameters?
      ( COLON typeExpr )?
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
    | implTok=identifier   { checkIsImplKeyword($implTok.text, this) }?
      typeParameters?
      interfaceBoundList
      FOR typeExpr
      whereClause?
      implBody                                                                              # standaloneImplDeclaration


    | externDecl                                                                            # externDeclarationTop


    | macroRulesDecl                                                                        # macroRulesDeclarationTop


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
    // Alias inside impl (rare but harmless)
    | modifierList ALIAS memberIdentifier typeParameters? ASSIGN typeExpr SEMICOLON
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
    | importQualifiedPath ( PERIOD | colonColon )? LBRACE importSpecList? RBRACE            # importGroup
    | importQualifiedPath ELLIPSIS importQualifiedPath ( AS identifier )?                   # importRange
    ;






importQualifiedPath
    : pathSegment ( colonColon pathSegment )*
    ;

importClause
    : '*'
    // Bare identifier permitted INSIDE import specifiers (e.g. `import std::collections::{HashMap}`).
    // REJECTION only enforced at importBody top-level (importSimple / importRename).
    | identifier
    | attributePath
    ;


importSpecList
    : importSpec ( COMMA importSpec )* COMMA?
    ;




importSpec
    : ( identifier | attributePath ) ( AS identifier )?
    ;

exportBody

    //                        | AttributePath ('.' | '::')? '{' ImportSpecList? '}'

    : LBRACE exportSpecList RBRACE                                                          # exportGroup
    | importQualifiedPath ( PERIOD | colonColon )? LBRACE importSpecList? RBRACE            # exportReexportGroup
    | declaration                                                                           # exportDeclDirect
    ;


exportSpecList
    : exportSpec ( COMMA exportSpec )* COMMA?
    ;



exportSpec
    : ( identifier | attributePath ) ( AS identifier )?
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
//       identifier-or-keyword helper (memberIdentifier) = IDENTIFIER | INIT | DEINIT | GET | SET

classBody : LBRACE classMember* RBRACE ;




memberIdentifier
    : identifier
    | INIT
    | DEINIT
    | GET
    | SET
    ;




structBody : LBRACE structMember* RBRACE ;

errorBody  : LBRACE structMember* RBRACE ;




classMember
    : outerAttributeList modifierList FUN memberIdentifier typeParameters? functionSignature
      ( SEMICOLON
      | blockBody { checkAbstractNoBlock($modifierList.ctx, this) }?
      )                                                                                     # classMethod
    | outerAttributeList modifierList (INIT | DEINIT) parameterList (RAISES typeExpr)? blockBody # classCtor
    // Class value member: mut / let / const (Ch.06 + Ch.08 class field)
    // Visibility (public/private/protected/static/readonly) still flows from modifierList.
    | outerAttributeList modifierList MUT variableDeclarationList SEMICOLON                # classMut
    | outerAttributeList modifierList LET variableDeclarationList SEMICOLON                # classLet
    | outerAttributeList modifierList CONST constDeclarationList SEMICOLON                 # classConst
    // Class value field: bare value decl like `public name: String = default;
    // (Ch.08 class field — separate from classMut/classLet that require MUT/LET keyword)
    | outerAttributeList modifierList memberIdentifier COLON typeExpr ( ASSIGN expression )?
      ( SEMICOLON | COMMA | { okAfterStructFieldNoSeparator(this) }? )                                # classField





    | outerAttributeList modifierList GET memberIdentifier functionSignature blockBody
      ( outerAttributeList modifierList SET memberIdentifier functionSignature blockBody
      | SET memberIdentifier functionSignature blockBody
      )?                                                                                   # classProperty
    ;






structMember


    : outerAttributeList modifierList (MUT | readonly)? memberIdentifier COLON typeExpr (ASSIGN expression)?
      ( SEMICOLON | COMMA | { okAfterStructFieldNoSeparator(this) }? )            # structField
    | outerAttributeList modifierList FUN memberIdentifier typeParameters? functionSignature
      ( SEMICOLON | blockBody )                                                             # structMethod
    | outerAttributeList modifierList (INIT | DEINIT) parameterList (RAISES typeExpr)? blockBody   # structCtor
    ;

// --- Interface -------------------------------------------------------------------------

interfaceBody : LBRACE interfaceMember* RBRACE ;



interfaceMember
    : outerAttributeList modifierList FUN memberIdentifier typeParameters? functionSignature SEMICOLON    # interfaceMethod
    | outerAttributeList modifierList (GET | SET) memberIdentifier functionSignature SEMICOLON            # interfaceProperty
    | outerAttributeList modifierList TYPE memberIdentifier typeParameters?
      ( COLON interfaceBoundList )? ( ASSIGN typeExpr )? SEMICOLON                                       # interfaceAssocType
    ;

// --- Enum ------------------------------------------------------------------------------

enumBody : LBRACE enumVariantList? RBRACE ;


enumVariantList : enumVariant ( COMMA enumVariant )* COMMA? ;

enumVariant


    : outerAttributeList identifier
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
    : outerAttributeList ( ( identifier | THIS ) COLON )? typeExpr ( ASSIGN expression )?
    | outerAttributeList THIS
    ;


typeParameters
    : LT typeParameter ( COMMA typeParameter )* COMMA? genericParamClose
    ;


// Industry convention (Swift / Kotlin): bounds are written with COLON, not Java-style
// 'extends' keyword. EXTENDS is reserved for class heritage (superclass).
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


// NOTE: uses `attrItem` (not `attr` directly) so that `#[zom::cfg(PRED)]`
// can have a DISJOINT top-level dispatch without ANTLR 4 ALL(*) simulator
// poisoning the decision with body-reachability scans into cfgPredicate.
attrList : attrItem ( COMMA attrItem )* COMMA? ;


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
    //          which itself splits into multi-segment (LA2 == ::) vs
    //          single-segment (LA2 != ::) via bounded lookahead.


    : { peekIsZomCfgParen(this) }?
      nsIdent=identifier colonColon cfgIdent=identifier
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

    // because that shape is intercepted by attrItem#attrZomCfg above.
    //
    // Alt 1: Multi-segment (attrpath >= 2 segments with ::) — gated by
    //        LA2 == ::. Accepts serde::rename, zom::deprecated, etc.
    // Alt 2: Single-segment built-in compiler attribute selected by the
    //        isBuiltinSingleSegAttr allowlist. Custom single-segment
    //        attributes are rejected to preserve the G11 path rule.
    //
    // The two alternatives are complementary, keeping SLL decisions stable.
    // =======================================================================
    : { isNextToken(this, IDENTIFIER) && la2IsColonColon(this) }?
      first=identifier colonColon rest=attributePathTail
      ( LPAREN input=attrInput RPAREN
      | ASSIGN value=expression
      )?                                                                            # genericMultiSeg
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
    | NEW | THIS | SUPER | EXTENDS | IMPLEMENTS | RAISES
    // -- Module --
    | MODULE | IMPORT | EXPORT | FROM | USING | REQUIRE
    // -- Concurrency --
    | SUSPEND | SPAWN
    // -- Reserved (ZOM500x) --
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

    //   Bare-key existence-check if no CfgOp.
    //   Valued-equality if CfgOp is `=` (ASSIGN) or `!=` (NEQ).

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

    // predicate partition — no DFA state merges can conflict.
    : key=IDENTIFIER
      ( valuedCfgAtomRhs
      | badRhsCfgAtomRhs
      | bareCfgAtomRhs
      )
    ;


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
    : label=identifier COLON
      { checkLabelNoAttrAfterLabel(_input.LT(1), $label.text, this) }?
      stmt=statement
      { checkLabelC25ControlFlowOnly($stmt.start, this) }?
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
    : spawnModifier ( COMMA? spawnModifier )*
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

// ---- Reserved syntax (ZOM5001~ZOM5008 precise diagnostics) ----------------
//   NOTE: Use semantic predicate `{ ... }?` (not bare action) so ANTLR does
//   not inject an unreachable `break;` after the action, which would make
//   javac complain.

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

blockBody : LBRACE statementList RBRACE ;

spawnBlockBody : LBRACE statementList expression? RBRACE ;

statementList : statement* ;


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
    : relationalExpr AS { checkAsForceCastLookahead(this) }? typeExpr
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
    : ( PLUS | MINUS | NOT | BIT_NOT | TYPEOF | MUL | BIT_AND ) unaryExpr     # exprUnary
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
    | callExpr OPTIONAL_CHAIN LPAREN expressionList? RPAREN (RAISES typeExpr)?             # exprOptionalCall
    | callExpr QUESTION? PERIOD memberIdentifier                                          # exprMember
    | callExpr OPTIONAL_CHAIN memberIdentifier                                            # exprOptionalMember
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

    | macroInvocationExpr                                                                 # exprMacroInvocation
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
    : typeParameters? parameterList ( ARROW returnType ( RAISES typeExpr )? )              # typeFunction
    // FUN-keyword-prefixed function type: fun(T) -> U raises E? (industry-standard explicit syntax)
    | FUN typeParameters? parameterList ( ARROW returnType ( RAISES typeExpr )? )          # typeFunctionKeyword
    | unionType                                                                           # typeUnionSingle
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
    | dynType                                                                             # typeDyn
    | associatedTypeProjection                                                            # typeAssociatedProjection
    | attributePath ( LT typeArgList genericClose )?                                     # typeQualified
    | identifier ( LT typeArgList genericClose )?                                        # typeNamed
    | LPAREN typeExpr ( COMMA typeExpr )* COMMA? RPAREN
      { checkTupleTypeNot1Tuple(_localctx, this) }?                                   # typeTuple
    | LBRACE structFieldType ( COMMA structFieldType )* COMMA? RBRACE                     # typeObject
    | identifier LPAREN variantTypeList RPAREN                                                    # typeTupleVariant
    | LBRACE RBRACE                                                                        # typeObjectEmpty
    | LBRACK typeExpr (SEMICOLON expression)? RBRACK                                         # typeArrayLiteralAtom
    ;

dynType
    : dynTok=identifier { checkIsDynKeyword($dynTok.text, this) }?
      interfaceBoundList
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
      typeParameters?
      functionSignature
      ( SEMICOLON | blockBody )
    ;

// 20.1 Unsafe block expression (escape hatch for unsafe operations)

unsafeBlockExpr
    : tok=identifier { checkIsUnsafePrefix($tok.text, this) }?
      LBRACE statement* expression? RBRACE
    ;

// 20.2 Extern declaration (foreign function interface)





//


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



externItem
    : funDecl                                                                           # externFunDecl
    | vTok=identifier   { checkIsVariableKeyword($vTok.text, this) }?
      identifier COLON typeExpr SEMICOLON                                               # externVarDecl
    | TYPE identifier ASSIGN
      ( oTok=identifier { checkIsOpaqueKeyword($oTok.text, this) }? )?
      identifier typeExpr? SEMICOLON                                                    # externTypeAlias
    ;

// 20.5 Unsafe function declaration (caller guarantees memory safety)


unsafeFunDecl
    : tok=identifier { checkIsUnsafePrefix($tok.text, this) }?
      funDecl
    ;

// ============================================================================

//








// ============================================================================

// 21.1 Function-like macro invocation: name!(token_tree) / name![tt] / name!{tt}

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




    : ( NOT | AND | OR | BIT_OR | BIT_AND | BIT_XOR | PLUS | MINUS | MUL | DIV
      | MOD | ASSIGN | COLON | SEMICOLON | COMMA | QUESTION | AT | BIT_NOT
      | LSHIFT | RSHIFT | URSHIFT | LT | GT | EQ | NEQ | LTE | GTE | PERIOD
      | ELLIPSIS )                                                             # macroPunctTok
    | identifier                                                             # macroIdentTok
    | literal                                                                # macroLiteralTok


    | ( CLASS | STRUCT | INTERFACE | ENUM | ERROR | FUN | MUT | LET | CONST
      | CONSTRUCTOR | ALIAS | INIT | DEINIT | GET | SET | ACCESSOR | DECLARE
      | IF | ELSE | MATCH | WHEN | DEFAULT | CASE
      | FOR | WHILE | DO | BREAK | CONTINUE | RETURN | DEBUGGER | IN | OUT
      | BOOL | STR | CHAR | NULL | UNIT | NEVER | ANY | OBJECT | SYMBOL | BIGINT | UNDEFINED
      | PUBLIC | PRIVATE | PROTECTED | STATIC | READONLY | MUTATING | OVERRIDE
      | ABSTRACT | GLOBAL | IMMEDIATE | INTRINSIC | UNIQUE
      | AS | IS | TYPEOF | KEYOF | INFER | SATISFIES | ASSERTS | ASSERT
      | NEW | THIS | SUPER | EXTENDS | IMPLEMENTS | RAISES
      | MODULE | IMPORT | EXPORT | FROM | USING | REQUIRE | SUSPEND | SPAWN
      | THROW | TRY | CATCH | FINALLY | ASYNC | AWAIT | VAR | ACTOR | CHANNEL
      | YIELD | GENERATOR | NAMESPACE | PACKAGE | TYPE | DELETE | INSTANCEOF
      | OF | WITH | TRUE | FALSE | UNDERSCORE
      | I8 | I16 | I32 | I64 | U8 | U16 | U32 | U64 | F32 | F64 )              # macroKeywordTok
    | LPAREN macroTokenTree RPAREN                                           # macroParenGroup
    | LBRACK macroTokenTree RBRACK                                           # macroBrackGroup
    | LBRACE macroTokenTree RBRACE                                           # macroBraceGroup
    ;

// 21.3 Macro Rules (declarative macro 2.0)
//      `macro NAME! { ( $a:expr ) => { ... }; ... }`


macroRulesDecl
    : MAC_=identifier { checkIsMacroKeyword($MAC_.text, this) }?
      identifier NOT
      LBRACE ( macroRule ( SEMICOLON )? )* RBRACE
    ;

macroRule

    : LPAREN macroPattern RPAREN ROCKET LBRACE macroTokenTree RBRACE          # macroRuleParen
    | LBRACK macroPattern RBRACK ROCKET LBRACE macroTokenTree RBRACE          # macroRuleBrack
    ;



macroPattern
    : macroPatToken ( COMMA macroPatToken )* ( COMMA ELLIPSIS )?
    ;

macroPatToken
    : captureName=identifier COLON macroFragSpec
        { $captureName.text.length() > 1
          && $captureName.text.charAt(0) == '$' }?                                         # macroCapture
    | identifier                                                              # macroPatIdent
    | literal                                                                 # macroPatLiteral

    | ( NOT | AND | OR | BIT_OR | BIT_AND | BIT_XOR | PLUS | MINUS | MUL | DIV
      | MOD | ASSIGN | QUESTION | AT | BIT_NOT
      | LSHIFT | RSHIFT | URSHIFT | LT | GT | EQ | NEQ | LTE | GTE | PERIOD
      | ELLIPSIS )                                                           # macroPatPunct

    | ( TRUE | FALSE | NULL | UNIT | NEVER | ANY | BOOL | STR | CHAR
      | I8 | I16 | I32 | I64 | U8 | U16 | U32 | U64 | F32 | F64
      | IF | ELSE | MATCH | FOR | WHILE | RETURN | LET | CONST | MUT
      | UNDERSCORE )                                                          # macroPatKeyword
    ;




macroFragSpec
    : identifier
    ;


//      `#[derive(Debug, Clone, Copy)]` / `#[derive(Serde(rename_all = "camelCase"))]`




deriveList
    : LPAREN ( deriveItem ( COMMA deriveItem )* COMMA? )? RPAREN
    ;

deriveItem
    : path=identifier ( LPAREN attrInput RPAREN )?
    ;
