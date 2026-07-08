# Attributes and Annotations

Attributes and annotations are structured, namespace-qualified metadata nodes attached to declarations, statements, expressions, parameters, type expressions, fields, patterns, and local bindings. The attribute system forms the backbone of ZOM's type-system extensibility: the concurrency safety model (Sendable, Shared, Linear, SuspendSafe, NoSuspendHazard, TaskBound), FFI layout guarantees, deprecation warnings, and compile-time lint control are all expressed as attributes or marker interfaces at the syntactic level.

Attributes exist in three syntactic tiers (Tier 0 compiler-built-in, Tier 1 standard-library marker, Tier 2 user-defined macro) arranged over a common modal-logic semantics. The system is intentionally closed to flat (un-namespaced) identifiers from day one to avoid the namespace creep that plagues older languages.

---

## 16.1 Definition and scope

An **attribute** is a metadata value `#[<namespace-path> ( <args>? )]` or `#![<namespace-path> ( <args>? )]` that attaches to a source-code node and carries structured information from the source text through the compiler pipeline to one or more consuming stages. A **marker** (also called a *marker-interface attribute*) is a subtype of attribute whose semantic effect is to place a type into a modal-logic predicate (see 16.12) enforceable by the type checker. An **annotation** is the user-facing act of writing such a construct; the terms *attribute* and *annotation* are used interchangeably in this specification except where the text explicitly distinguishes.

**Scope of this chapter.** This specification defines:

1. The syntactic grammar and lexical disambiguation rules for attributes (16.2, 16.3).
2. The attachment-target model and AST-level storage layout (16.4).
3. The namespace policy, including the flat-name hard ban (16.5).
4. The four-tier retention model and stage-wise emission semantics (16.6, 16.7).
5. The exhaustive catalogue of Tier 0 compiler attributes (16.8) and Tier 1 standard library markers (16.9), both with complete per-attribute schemas.
6. The Tier 2 user-macro programming interface contract (16.10).
7. The deterministic checker pipeline with stage ordering, I/O contracts, and failure semantics (16.11).
8. The formal modal-logic model underpinning marker interfaces (16.12) and negative-impl logic (16.13).
9. A practical user guide for the marker system (16.14), including worked examples in an appendix.
10. A diagnostic specification (16.15), LSP/IDE rules (16.16), compatibility governance (16.17), and implementation cost notes (16.18).

**Out of scope.** The concrete *syntax* of user-written proc-macro bodies (as opposed to the Macro trait interface) is deferred to the compile-time-reflection chapter; only the input/output contract and stage isolation appear here. Runtime reflection beyond the RUNTIME_REIFIED tier's metadata blob is not specified.

---

## 16.2 Grammar

The following EBNF is the canonical grammar for the attribute and marker system. Every non-terminal carries a production number. The grammar cross-references `finalEBNF` and is consistent with Chapter 17.

```ebnf
(* ================================================================
   ZOM CANONICAL Attribute & Marker EBNF  (v1.0 stable)
   Outer = #[…];  Inner = #![…];  @ is parameter sugar ONLY.
   LL(3) for the single Hash-then-LeftBracket disambiguation decision
   at OuterAttribute-start (tokens: '#', '[', <inside-first>);
   ALL other rules are LL(1).  The 3-token lookahead is bounded and cheap:
   it is invoked exactly once per potential attribute start, and is used
   exclusively to distinguish:
     • Outer attribute  →  '#[' then Identifier / '::' (attr-path start)
     • Dangling-Hash expr → '#' followed by non-'['
     • (ArrayLiteral with stray #) → '#[' then NumberLiteral / StringLiteral
                         (rewind + W0501 — see §16.3.5)
   Production numbers:  A-001 … A-045.
   ================================================================ *)

(* ---------- [A-001] Module-level attribute anchors ---------- *)

sourceFile                                                      (* [A-001] *)
  = shebang? innerAttrList? moduleDeclaration?
    moduleItem*
    EOF
  ;

innerAttrList                                                   (* [A-002] *)
  = InnerAttribute+ ;                                           (* #![…] blocks ONLY at file head *)

moduleItem                                                      (* [A-003] *)
  = importDeclaration
  | exportDeclaration
  | topLevelDeclaration
  | statementListItem
  ;

(* ---------- [A-004..A-008] Outer / Inner Attribute ----------
              Brackets are NON-NEGOTIABLE. *)

OuterAttribute                                                  (* [A-004] *)
  = '#' '[' attributeEntry ( ',' attributeEntry )* ','? ']'
  ;

InnerAttribute                                                  (* [A-005] *)
  = '#' '!' '[' attributeEntry ( ',' attributeEntry )* ','? ']'
  ;
  (* ONLY permitted in { SourceFile head, BlockStatement head } ;
     elsewhere → parser error ZOM0601 InnerAttrNotAllowed *)

attributeEntry                                                  (* [A-006] *)
  = attributePath                                               (* #[zom::inline]            — bare unit *)
  | attributePath  '('  ( attrArgBody  |  ttBody )?  ')'        (* #[zom::repr(C, align(8))] — deterministic call-form *)
  | attributePath '=' attrLiteral                               (* #[zom::doc = "text"]      — key = value *)
  ;

     attrArgBody  =  attrArgument ( ',' attrArgument )* ','?
         (* First(attrArgBody) ⊂ { Identifier, literal, true, false,
                                         null, '[', '{' }
            The parser ALWAYS tries attrArgBody FIRST on entering
            '( … )'; ONLY if the first inner token is NOT in
            First(attrArgBody)  ⇒  fall back to ttBody.
            LONG-MATCH rule: if a prefix parses as attrArgBody, the
            parser commits; a subsequent failure inside the list does
            NOT fall back to ttBody — use sync-set recovery (AM-06)
            and continue, emitting ZOM0618. *)

     ttBody       =  ttElem+
         ttElem   =  ttDelimited  |  Identifier  |  literal
                    |  Punctuator (any single-char punctuation not
                                   already consumed as structural)
         ttDelimited = '(' ttBody? ')' | '[' ttBody? ']' | '{' ttBody? '}'

     (* ── Ambiguity-resolution corollary ────────────────────────────
        For '#[my_macro(foo)]':
          • 'foo' is an Identifier ∈ First(attrArgBody).
          ⇒ Parser commits to attrArgBody ⇒ positional Ident argument.
          This is DETERMINISTIC.
        To FORCE token-tree mode (e.g. for a truly free-form macro
        input), the user MUST write an explicit non-attrArgBody first
        token, e.g. '#[my_macro( { foo } )]' or use a leading delim.
        Tier-0/1 ArgsSchema tables (§16.8 / §16.9) MUST each declare
        a 'accepts_token_tree: bool' flag; if false, parser hard-
        errors on ttBody branch for that attribute. *)

attributePath                                                   (* [A-007] *)
  = Identifier ( '::' Identifier )+                             (* HARD RULE: ≥ 2 segments *)
  | Identifier                                                  (* LegacyBareWhitelist only:
                                                                   deprecated | inline | cold —
                                                                   parser rewrites to zom::…
                                                                   and emits W7105            *)
  ;

attrLiteral = literal ;                                         (* [A-008] *)

attrArgument                                                    (* [A-009] *)
  = attrLiteral                                                 (* positional literal *)
  | Identifier                                                  (* positional ident  *)
  | Identifier '=' ( attrLiteral | Identifier )                 (* named key=value   *)
  | attrTokenTree                                               (* free-form for Tier-2 *)
  ;

attrTokenTree                                                   (* [A-010] *)
  = '(' ( ttElem )* ')'
  | '[' ( ttElem )* ']'
  | '{' ( ttElem )* '}'
  ;

ttElem                                                          (* [A-011] *)
  = Identifier | literal | Punctuation | attrTokenTree ;

(* ---------- [A-012] Parameter @ sugar ----------------------------------
     @name(args?)  on a ParameterDeclaration  ⟹  syntactically equivalent to
     #[zom::param::name(args?)]  on that parameter.  Stored in the same
     ModifierList slot.  @ is RETAINED in source; ZERO AST node distinction.
     FIRST-set of ParameterDeclaration: At | Identifier | '...'.
     (At outside ParameterDecl / FunctionType-param position ⟶ ZOM0602
      MisplacedAt.)                                              [A-012] *)

A-012a ParameterDeclaration = AtSugarParameter | NormalParameter

A-012b AtSugarParameter      = '@' attributePath ( '(' attrArgList? ')' )?
                                     NormalParameterTail
        NormalParameterTail  = ( bindingPattern ':' typeExpression
                               | '...' bindingPattern? ':' typeExpression
                               ) defaultValueClause? whereClause? ','?

        (* ── Constraint 1 (A-012c): exactly ONE '@' per ParameterDeclaration
           head. Two consecutive '@' without an intervening ',' (i.e.
           AtSugarParameter head re-fired before consuming a comma)
           ⇒ ZOM0602 MisplacedAt with diagnostic hint:
           "Did you mean '#[first] #[second] param: T' or insert a comma
            between parameters?"

           ── Constraint 2 (A-012d): '@' not immediately followed by an
           Identifier or '::' or '('  ⇒  ZOM0602 MisplacedAt (stray @).

           Desugaring:
             @zom::param::unused                → #[zom::param::unused]
             @zom::param::variadic args: ...T   → #[zom::param::variadic]
                                                     args: ...T
             @foo::bar(baz, q=1) x: i32         → #[foo::bar(baz, q=1)] x: i32
             @deprecated(since="1.0") x: S      → #[zom::stability::deprecated(
                                                     since="1.0")] x: S
               (per legacy bare-name whitelist W7105.)
        *)

(* ---------- [A-013..A-015] Top-level declarations attach ModifierList ---------- *)

topLevelDeclaration                                             (* [A-013] *)
  = modifierList (
        structDeclaration
      | classDeclaration
      | interfaceDeclaration
      | enumDeclaration
      | errorDeclaration
      | aliasDeclaration
      | functionDeclaration
      | markerDeclaration          (* NEW: declares a user marker trait *)
      | markerImplDeclaration      (* NEW: impl [!] Marker for T      *)
      | implDeclaration            (* existing ordinary impl block    *)
      | constDeclaration
      | variableStatement
    )
  ;

modifierList                                                    (* [A-014] *)
  = ( OuterAttribute | visibilityKeyword | keywordModifier )*
  ;

visibilityKeyword                                               (* [A-015] *)
  = 'public' | 'private' | 'protected'
  ;

keywordModifier                                                 (* [A-016] *)
  = 'static' | 'readonly' | 'mutating' | 'override'
  ;

(* ---------- [A-017..A-018] Marker interface declaration (user markers, Tier-2) ---------- *)

markerDeclaration                                               (* [A-017] *)
  = 'marker' Identifier typeParameters?
      ( '=' markerBound ( '+' markerBound )* whereClause? )?
      structBody?
      ';'?
  ;

markerBound                                                     (* [A-018] *)
  = '!'? attributePath typeArguments?
  ;

(* ---------- [A-019] Marker (positive / negative) implementation ---------- *)

markerImplDeclaration                                           (* [A-019] *)
  = 'unsafe'? 'impl' typeParameters? '!'? markerImplPath
      'for' type
      whereClause?
      ( structBody | ';' )
  ;
markerImplPath
  = attributePath | Identifier
  ;
  (*   Positive:  impl std::marker::Sendable for MyStruct;   /  unsafe impl std::marker::Shared for Mutex<T> {...}
   *   Negative:  impl !Shared for UnsafeCell<T>;            -- negation INFIX after optional type parameters.
   *   Conditional blanket: impl<T> std::marker::Sendable for Vec<T> where T: Sendable;
   *)

(* ---------- [A-020..A-023] Where-clause extension for negative bounds ---------- *)

whereClause  = 'where' wherePred ( ',' wherePred )* ','? ;      (* [A-020] *)

wherePred                                                       (* [A-021] *)
  = type ':' boundItem ( '+' boundItem )*
  ;

boundItem                                                       (* [A-022] *)
  = markerBound       (* !? attributePath [<…>]?  — negative BOUNDS allowed here *)
  | type              (* existing ordinary type-bound *)
  ;

(* ---------- [A-023] Type expression: marker bounds in generics ---------- *)

typeParameter                                                   (* [A-023] *)
  = Identifier ( ':' boundItem ( '+' boundItem )* )?
  ;
  (*  Example:  fun f<T: Sendable + !Shared + 'static>(x: T) -> T;
   *   Generic-param-bounds allow markerBound directly (incl. negative !M form).
   *)

(* ---------- [A-024] Statement attrs (limited) ---------- *)

statementListItem                                               (* [A-024] *)
  = OuterAttribute* statement                (* attr on while/for/return/match/mut/let *)
  ;

(* ---------- [A-025] Block-statement head allows inner attrs ---------- *)

blockStatement                                                  (* [A-025] *)
  = '{' innerAttrList? ( statementListItem | topLevelDeclaration )* '}'
  ;

(* ---------- [A-026] Expression attrs (RESTRICTED TO WHITELIST) ---------- *)

attributeAnnotatedExpression                                    (* [A-026] *)
  = OuterAttribute* primaryExpression
  ;
       // Restricted to Tier-0 expr-attr whitelist:
       //   { zom::hint::inline, zom::hint::cold, zom::must_consume,
       //     zom::hint::unroll, zom::hint::likely, zom::hint::unlikely }
       // (T0-11/T0-12/T0-13/T0-14 + T-new zom::must_consume; aligned with
       //  the semantic target lists in §16.8.)
       // Non-whitelist → ZOM0603 ExprAttrNotAllowed.
       //
       // ── Attachment-disambiguation rule (A-026a) ──────────────────────
       // If the source text '#[…] X' is syntactically valid BOTH as:
       //   • attributeAnnotatedExpression  (X is a primaryExpression), AND
       //   • modifierList + Statement/Declaration (X is a Statement head),
       // then disambiguate by CONTEXTUAL POSITION:
       //
       //   Expression position  (RHS of '=', return operand, call-arg,
       //                        binary-operator operand, parenthesised
       //                        expression, array/comprehension element)
       //       ⇒ always parse as attributeAnnotatedExpression (parse-tree
       //         A: attributes attach to X-as-expression).
       //
       //   Statement position   (start of StatementListItem, after ';',
       //                        at block head, at crate head)
       //       ⇒ always parse as modifierList prepended to the Statement/
       //         Declaration (parse-tree B: attributes attach via X's
       //         .modifiers slot).
       //
       // Ambiguity in a NEITHER position → SHIFT into modifierList path
       // and defer to name-resolution/S1 well-formedness to produce the
       // final diagnostic (never attributeAnnotatedExpression).

A-026a is CO-NORMATIVE with the 5-step attachment algorithm; any parser
producing an AST that violates the disambiguation class for a given
syntactic context is considered non-conformant.

(* ---------- FIRST / FOLLOW summary for recursive-descent ----------------------------
   Construct                 | FIRST set         | Notes
   --------------------------+-------------------+-----------------------------
   OuterAttribute            | { Hash }          | peek[1] ∈ { LeftBracket }
   InnerAttribute            | { Hash }          | peek[1] = Exclamation  &  peek[2] = LeftBracket
   shebang                   | { Hash }          | ONLY emitted by lexer at offset 0;
                             |                   |   NEVER reaches parser as a token.
   markerDeclaration         | { marker }        | contextual keyword, disambiguated as
                             |                   |   FIRST of a topLevelDecl.
   markerImplDeclaration     | { impl, unsafe }  | after optional impl type parameters,
                             |                   |   `!` or a marker namespace path selects marker form.
   Parameter-decl (At path)  | { At }            | guarded inside ParameterList.
   --------------------------+-------------------+-----------------------------
   Hash disambiguation in the parser (called when next token is Hash):
     • lexer already stripped shebang → never seen here
     • peek(1) == '!' && peek(2) == '['  →  InnerAttribute
     • peek(1) == '['                     →  OuterAttribute
     • inside ParameterList head context → syntax error, stray #
     • otherwise                           → ZOM0604 DanglingHash.
   -------------------------------------------------------------------------------- *)

(* ---------- Reused existing non-terminals (listed for completeness) ----------------
     type, typeParameters, typeArguments, whereClause (general), literal,
     structDeclaration, classDeclaration, interfaceDeclaration,
     functionDeclaration, implDeclaration, variableStatement, statement,
     primaryExpression, importDeclaration, exportDeclaration,
     moduleDeclaration, shebang, structBody.
   -------------------------------------------------------------------------------- *)
```

## 16.2.1  LSP Incremental-Reparse State Machine (v1) — co-normative
    When editing inside '#[  …  ]' at character granularity:
        states = { ExpectOpenBracket, InAttrPath, InAttrArgs,
                   ExpectCommaOrClose, Error }
        Invariant: each Attribute AST node carries a fixed end span
        (matching ']'). Edits BEFORE the node's ']' (at column ≤ ']'.col)
        DO NOT invalidate: (a) any sibling node of the Attribute, and
        (b) any Attribute AST subtree node whose span start is strictly
        AFTER the mutation range. Bracket-depth is the reparse anchor.
        ⇒ Reparse cost = O(length of attribute subtree + 1)
           per keystroke, never O(n) of the enclosing file.

---

## 16.3 Lexer rules

The lexical layer makes the *minimum* number of token additions required to support the attribute grammar. All syntactic disambiguation of `#`-prefixed constructs is deferred to the parser with bounded (≤ 2-token) lookahead.

### 16.3.1 SyntaxKind table delta

**One** new compound token is introduced; all other attribute-relevant punctuation remains single-character:

| Addition | Pattern | Semantics |
|---|---|---|
| `ColonColon` | two adjacent `:` characters with no whitespace between | namespace separator used by `attributePath`; enables root-ns disambiguation via a leading `::` (`#::zom::inline` means global-scope `zom`). |
| (unchanged) `At` | single `@` | parameter-sugar sentinel; the parser decides legality contextually. |
| (unchanged) `Hash` | single `#` | attribute initiator; never combined. |
| (unchanged) `Exclamation` | single `!` | inner-attribute marker / negative-impl prefix; never combined with `#`. |

No compound `#[` or `#!` tokens are emitted. This keeps the lexer re-entrant, incremental-friendly, and free of attribute-specific hacks.

Implementation note: lexer MUST reject the pattern ': :' (two colons
with whitespace between) as ColonColon — each colon forms a separate
Colon token, and the parser emits ZOM0620 MalformedNamespaceSep with
a suggestion to write '::'.

> **Two-token design: `#[` is `Hash` + `LeftBracket`, not a compound token**
>
> The attribute opening `#[` is intentionally **two separate tokens**:
> `SyntaxKind::Hash` (`#`) followed by `SyntaxKind::LeftBracket` (`[`).
> No compound `#[` or `#!` token is ever emitted by the lexer.
>
> **How attribute detection works at the parser level:**
>
> 1. The parser encounters a `Hash` token and consults the bounded-lookahead
>    table in 16.3.5.
> 2. If `peek(1)` is `LeftBracket`, the parser additionally verifies that the
>    two tokens are **source-range adjacent** — i.e., there is no whitespace
>    or comment between `#` and `[`. This is checked by comparing the end
>    offset of the `Hash` token against the start offset of the `LeftBracket`
>    token; they must be consecutive byte positions.
> 3. If they are **not** adjacent (e.g., `# [foo]` with a space), the parser
>    emits `ZOM0604 DanglingHash` — the `#` is treated as orphaned
>    punctuation, not as the start of an attribute.
>
> **Why this design was chosen:**
>
> - **Lexer simplicity.** The lexer remains re-entrant, incremental-friendly,
>   and free of attribute-specific hacks. No special-cased multi-character
>   punctuation is needed beyond `::`.
> - **Proven pattern.** This mirrors Rust's design, where `#[` is also two
>   tokens (`Pound` + `OpenBracket`) and adjacency is enforced in the parser.
>   Decades of production use have validated this approach.
> - **`Hash` is reserved.** Outside attribute contexts, the `#` character
>   has no other syntactic meaning in ZOM. It is exclusively reserved for
>   attribute initiation, so there is no ambiguity cost to decomposing it.
> - **Whitespace rejection is intentional.** Requiring `#` and `[` to be
>   adjacent (`#[foo]`) prevents the misleading visual of `# [foo]` which
>   could be misread as a comment or preprocessor directive. The diagnostic
>   `ZOM0604 DanglingHash` provides a clear correction path.

### 16.3.2 Shebang disambiguation

```
[L1]  Lexer disambiguation at bufferStart + '#!':
        (a) After '#!', skip whitespace; if the next non-WS character
            IS '[' → do NOT form a Shebang token. Instead emit
            SyntaxKind::Hash and SyntaxKind::Exclamation as two separate
            tokens so the parser can enter InnerAttribute.
            (This prevents '#![zom::feature::enable("x")]' at line 1
             from being dropped as a shebang.)
        (b) Otherwise → consume the entire line and emit ONE
            SyntaxKind::Shebang token; record consumed span as
            SourceFile::shebang_line.

[L1b] Lexer disambiguation at NON-bufferStart + '#!':
        (a) After '#!', skip whitespace; if the next non-WS character
            IS '[' → emit Hash + Exclamation separately (normal tokens).
            Positionality enforcement for inner attributes (#![…] only
            at block / crate head) is deferred to the parser, which
            emits ZOM0601 InnerAttrNotAllowed when the inner-attr does
            not appear in a valid head position.
        (b) Otherwise → emit error XCanOnlyUsedAtStartOfFile and
            return Unknown('#' + '!'); this is the truly-illegal shebang
            mid-file case.
```

The shebang is the *only* two-character `#` escape path for full-line tokens; every other `#!` combination in the file is decomposed to Hash + Exclamation (or an error per L1b).

### 16.3.3 Compound-token rules

- `:` followed immediately by `:` (no whitespace) → `ColonColon`.
- Lone `:` → `Colon` (unchanged).
- `@` → `At` (never combined; no `@@`, no `@[`).
- `#` → `Hash` (never combined).
- `!` → `Exclamation` (never combined with `#`).

### 16.3.4 `@`-sugar contextual rule

The lexer does **not** special-case `@`. Legality is enforced by the parser using the contextual predicate `isStartOfParameter(position)`:
- Inside a `ParameterList` head or `FunctionType` parameter position, `@` starts the parameter-attribute desugaring path (rewritten to `#[zom::param::<Ident>(args)]`).
- Anywhere else → `ZOM0602 MisplacedAt` diagnostic.

### 16.3.5 Conflicts with other `#`-prefixed syntax

The parser resolves `Hash` via the following bounded-lookahead table (see 16.2's `Hash disambiguation` box). Conflicts with future typed-array literals `#[T]` and indexed literals `#[1,2,3]` are handled **without lexer changes** by a second-level lookahead *inside* the brackets:

| `peek(1)` | `peek_after_[` | Outcome |
|---|---|---|
| `!` (and `peek(2) == '['`) | — | `InnerAttribute` start (only in block-head context; else `ZOM0601`). |
| `[` | `Identifier` (potentially with `::` path) | `OuterAttribute` start. |
| `[` | `NumberLiteral` / `StringLiteral` | Rewind `Hash` token; parse `[lit,…]` as `ArrayLiteralExpression`; emit `W0501 StrayHash`. |
| `[` | `!` | Syntax error with dual-span: "misplaced negation inside `#[…]`; did you mean `impl !M for T`?". |
| not in `{ '!', '[' }` | — | `ZOM0604 DanglingHash` with hint: "expected `[` after `#` to start an attribute, or `![` for an inner attribute." |

### 16.3.6 Frontier guarantee (FIRST-set disjointness)

| Context | `Hash` | `At` | `ColonColon` |
|---|---|---|---|
| SourceFile head | inner_attr | error | root-ns disamb |
| Before declaration | outer_attr | error | attribute path |
| ParameterList head | error | param-sgr | parameter type (fn-ptr) |
| type expression | error | error | ns-qualified TypeRef |
| expression | whitelist-only | error | ns-qualified callable |
| struct/class body | outer_attr (fields) | error | field type |

The disjointness guarantees that the recursive-descent parser performs at most **two** `peek()` calls to reach its decision, never backtracks over emitted tokens, and diagnostics anchor on unique tokens.

### 16.3.7 Flat-name enforcement in the lexer

**None.** Enforcement belongs to name resolution (Binder, stage S0). The parser *does* enforce `length(segments) ≥ 2` on `attributePath` except for the three-entry `LegacyBareWhitelist = { deprecated, inline, cold }`. Any other length-1 attribute path is a hard parser error `ZOM0617 BareAttribute` with Levenshtein-based nearest-neighbour suggestions.

### 16.3.8  @-sugar at parameter heads (canonical semantic)
    '@' is ALSO a unary sugar-form for parameter-position attributes; it
    is NEVER a standalone statement-level decoration. Outside a
    ParameterDeclaration head or FunctionType parameter slot, ANY
    occurrence of '@' followed by valid attr-path grammar is
    ZOM0602 MisplacedAt.

### 16.3.9  Template-literal exclusion in attr argument mode
    When parsing attrArgBody (A-006), the lexer operates in a restricted
    AttrLiteralMode: backtick (` ) is treated as an ErrorToken. Template
    literals are ONLY valid inside ttBody, where the parser enters a
    dedicated nested-lexer mode that correctly handles '${…}' expression
    interpolation without corrupting the attribute parser's bracket-depth
    counter. A backtick encountered in attrArgBody fires ZOM0619
    TemplateLiteralInAttrArg with a suggestion to wrap in '{ … }' to
    force ttBody mode.

---

## 16.4 Scope model

### 16.4.1 Outer vs. inner attributes

Two anchor forms attach metadata to distinct scopes:

- **Outer attribute** `#[…]` — written *before* its target node. Applies to the immediately following syntactic entity.
- **Inner attribute** `#![…]` — written *inside* a block or file head. Applies to the **enclosing** scope (the source file, or the block that contains it).

Concrete anchor positions (non-exhaustive):
```
#![zom::lint::allow(ZOM0741)]  // inner: applies to the whole crate
#![zom::feature::enable("marker_macros")]

pub fun example() {
    #![zom::lint::deny(ZOM0800)]   // inner: applies to the function body scope
    #[zom::inline]                 // outer: applies to the binding's initialiser expr
    let x = compute();
}
```

### 16.4.2 Eight attachment targets

Every attribute and marker resolves to one of eight semantic targets. The target determines which AST node classes carry the optional `modifiers: Ref<ModifierList>` slot:

| # | Target name | Node classes that carry `modifiers` | Typical use |
|---|---|---|---|
| 1 | **item** | `StructDeclaration`, `ClassDeclaration`, `InterfaceDeclaration`, `EnumDeclaration`, `ErrorDeclaration`, `AliasDeclaration`, `FunctionDeclaration`, `ModuleDeclaration`, `ImplDeclaration`, `MarkerDeclaration`, `MarkerImplDeclaration` | `#[zom::repr(C)]`, `#[std::marker::Sendable]`, `#[zom::ffi::no_mangle]` |
| 2 | **param** | `ParameterDeclaration` (includes closure parameters) | `@variadic`, `#[zom::param::unused]` |
| 3 | **expr** | `PrimaryExpression` (via `attributeAnnotatedExpression`; restricted to Tier-0 whitelist: `inline`, `cold`, `must_consume`, `hint::unroll`) | `#[zom::inline] f(x)` |
| 4 | **stmt** | `Statement` (via `statementListItem`; limited to control-flow and `mut` / `let`) | `#[zom::allow(ZOM0743)] for x in … {}` |
| 5 | **type** | `TypeExpression` (in `wherePred`, `typeParameter`, field type, function type) | `T: Sendable + !Shared` bounds |
| 6 | **field** | `PropertyDeclaration`, `PropertySignature` (carries `modifiers`) | `#[zom::doc = "raw OS fd"] fd: i32` |
| 7 | **pattern** | `BindingPattern` (match arms, destructuring `let`) | `#[zom::allow(unused)] (a, _b)` |
| 8 | **local-bind** | `VariableDeclaration` (inside `VariableStatement`) | `#[zom::allow(unused_mut)] mut x = …` |

### 16.4.3 AST attachment algorithm (concrete)

**Pre-condition.** Every node class that is a legal attachment target contains the slot:

```cpp
Ref<ModifierList>  modifiers;   // nullptr if no modifiers/attributes present.
```

`ModifierList` is an ordered heterogeneous bag:
```cpp
AST_ELEMENT_NODE(ModifierList, Node)
  NodeList<Node>* items;   // AttributeNode | AccessibilityModifier | KeywordMod
```

**Attachment algorithm (parser):**

1. When entering a production for one of the eight targets, first invoke `parseModifierList()`, which greedily consumes all contiguous leading:
   - `OuterAttribute` (if the target is not a *type-expression* or *pattern* context where outer-attrs are not the FIRST element),
   - `visibilityKeyword`,
   - `keywordModifier`.
2. Assign the resulting `ModifierList*` (or `nullptr` if empty) to `target.modifiers`.
3. For a `ParameterDeclaration` whose FIRST token is `At`:
   - Collect `@` + `Ident(args?)`; construct an `OuterAttribute` with `path = zom::param::Ident`, `args = args`, and set `AttributePathNode::parser_applied_sugar = true` plus a source-span pointing at the `@` token.
   - Prepend it to the parameter's `ModifierList`.
4. For a `BlockStatement` or `SourceFile` head:
   - After the opening `{` or after `shebang`, greedily parse inner attributes via `parseInnerAttributeList()` and append them to the enclosing scope's `ModifierList` (creating one if it does not exist), with `InnerAttribute::isInner = true`.
5. Order preservation. Elements of a `ModifierList` **must** retain source order. This is required for:
   - lint push/pop frame semantics (16.8),
   - deterministic diagnostics,
   - formatter round-tripping.

**Binding-time invariant.** No pass before the binder (S0) may dereference `AttributePathNode::resolved_symbol` (it is `nullptr`). No pass after S1 (well-formedness) may mutate the `items` list of an existing `ModifierList`, except for:
   - the binder appending **synthetic** doc-param nodes to `ParameterDeclaration::modifiers` (with `synthetic = true`), and
   - the macro-expansion pass substituting Tier-2 markers (16.10).

---

## 16.5 Namespace system

ZOM enforces a **mandatory fully-qualified naming policy** on attributes from day one. Flat (un-namespaced) identifiers are a hard error in attribute position except for the deprecated 3-entry `LegacyBareWhitelist`.

### 16.5.1 Reserved Tier-0 subspaces (immutable, closed set, RFC-required for add)

    1.  zom::hint        — codegen/optimization hints (inline, cold, unroll,
                            likely, unlikely, must_consume, suspend_capable)
    2.  zom::ffi         — FFI and interop
    3.  zom::stability   — deprecation, stability, variant discriminator
    4.  zom::lint        — lint allow/deny/warn/force
    5.  zom::feature     — feature-gate enable (crate-head inner attr)
    6.  zom::lang        — lang-items + compiler intrinsics
    7.  zom::repr        — layout and ABI family (unified 2-segment root.
                            NOTE: the canonical path is 'zom::repr'.
                            'zom::layout::*' is reserved for future
                            ABI-contract attributes not yet added.)
    8.  zom::doc         — documentation
    9.  zom::param       — parameter-declaration sugar (variadic, unused,
                            move, label)
   10.  zom::attribute   — meta-attributes (retain, lint_tool, etc.)
   11.  zom::concurrency — concurrency-runtime-facing attributes (new,
                            added alongside the ZOM async canonical
                            design v1-rc1):
            • scope_guard         (unit; Target = struct/class impl-blocks
                                   for Scope-local RAII guards)
            • detached            (unit; Target = spawn expression — denotes
                                   a task whose handle is discarded; G2
                                   gate handles the detached pledge)
            • requires_executor   (unit; Target = fun/closure — declares
                                   the item MUST be called within an
                                   executor context; L3 pledge)
            • within_scope(scope_id: Identifier)
                                  (positional Ident; Target = block / fun
                                   body — asserts the code runs strictly
                                   inside the named scope's dynamic extent)
            • assume_executor_context (unit; Target = statement / block —
                                   UNSAFE hint to S4 that the programmer
                                   knows an executor context is present
                                   even though the solver cannot prove it;
                                   emits W8020 at compile, ZOM8047 hard
                                   error at runtime if check fails)

Additional root-namespace admission policy (cross-reference):

| Root namespace | Owner | Admission bar | Purpose |
|---|---|---|---|
| `std::marker::*` | Standard Library Team | `zom-std` RFC + Kripke-semantics soundness argument in Appendix | Tier-1 concurrency + layout markers. 6 concurrency + 9 layout/POD = 15 items. Bare short-names are prelude-injected into the **type** namespace only (so `T: Sendable` works without a path); in `#[…]` attribute form the full path `std::marker::Sendable` is required. |
| `std::*` (other subspaces) | Standard Library Team | `zom-std` RFC | Tier-1 general attributes (e.g. `std::test::bench`, `std::test::case`). |

User crates **cannot** declare `zom` or `std` as crate names; the manifest loader emits `ZOM0951 ReservedCrateName` on `Cargo.zom` load.

### 16.5.2 Open namespaces

| Root | Convention |
|---|---|
| `<crate>::*` | Any crate name not in {`zom`, `std`}. User macros and marker declarations live here. |
| `<crate>::attr::*` | Convention for a crate's *public* attribute surface. LSP completion in attribute position **only** surfaces items in `<crate>::attr::*` unless the user explicitly types a full path. |

### 16.5.3 Vendor reverse-domain rules (recommended, not enforced)

For crates published under a vanity or personal name, the LCT *recommends* a reverse-domain or `io.github.<user>/<org>.<repo>` prefix inside marker names:
```
marker io_github_acme_my_marker = std::marker::Sendable + std::marker::SuspendSafe;
```
Non-compliance is a crate-level lint `W7201 NonRfcMarkerName` (default: allow) that the central registry may hard-require for packages publishing `marker_macros`-gated items.

### 16.5.4 Mandatory fully-qualified naming — flat-name hard ban

**Rule.** In `#[…]` (and `#![…]`) attribute position, every attribute path whose segment count is 1 is a hard error **unless** the identifier is one of:

```
LegacyBareWhitelist = { deprecated, inline, cold }
```

in which case the parser rewrites it to its `zom::stability::deprecated` / `zom::hint::inline` / `zom::hint::cold` equivalent and emits warning `W7105 DeprecatedBareAttribute`. The whitelist is frozen; **no new entries will ever be added** — see 16.17.

### 16.5.5 Flat-name enforcement matrix

| Syntactic position | bare ident allowed? | Resolution rule |
|---|---|---|
| `#[ Ident ]` | only 3 whitelist | desugar to `zom::Ident` + warn; else error `ZOM0617` |
| `T: Ident` (type-bound) | yes (always) | `std::marker::Ident` prelude; shadow → error |
| `impl Ident for T` | yes (always) | `std::marker::Ident` prelude |
| `where` pred bound | yes | same as type-bound |
| `@Ident` (parameter) | yes | `zom::param::Ident`; error if not in param whitelist |
| `marker M = B1 + B2 …` | bare idents OK for bounds | marker-bound namespace, prelude'd |

The asymmetry is intentional: attribute `#[…]` position requires full paths because the attribute namespace is *unbounded* (user macros); type-bound position uses prelude'd markers because the marker set *is* bounded and writing `std::marker::Sendable` at every generic declaration would kill adoption.

### 16.5.6 Scoping rules (inheritance / override)

For a declaration D with attrs A(D) whose parent P has attrs A(P):
- `zom::lint::allow/deny`: push-frame model; innermost wins.
- Marker attributes (`std::marker::*`, user marker-traits): **NO inheritance**. A nested struct must carry its own markers (Linear on a container does not imply Linear on its children).
- `zom::doc`: concatenated with a paragraph break; child doc appears after parent doc in rustdoc output (applies to impl blocks, trait methods, struct fields).
- All other attrs (ffi, layout, hint): NO inheritance. Explicit on the target node only.

### 16.5.7 Root-namespace disambiguation

`#::zom::inline` always means global-scope `zom::inline`, even inside a `mod zom { … }` block that shadows the root. Mirrors C++ `::`.

---

## 16.6 Retention model

Four retention tiers describe which attributes and markers survive which compiler phases. The tiers form a strict linear order: `SOURCE_ONLY ⊂ TYPECHECK_ONLY ⊂ COMPILE_TIME ⊂ RUNTIME_REIFIED`.

### 16.6.1 Tier enumeration

| Tier (symbolic) | Survives phases | Metadata emission | Example occupants |
|---|---|---|---|
| **SOURCE_ONLY** | Lexer → Parser → (discarded after binder; retained in syntax tree for `-Zdump-ast` only) | Never serialized to crate metadata; never visible to downstream crates | `zom::doc::*` (except crate-level doc strings), formatter directives, user Tier-2 `#[doc(hidden)]` equivalents that operate only locally |
| **TYPECHECK_ONLY** | Lexer → Binder → Checker S0–S4 → (erased before lowering S5) | Exists in the crate-metadata side-channel as marker-bitset entries + negative-impl exclusion bitmap; fully erased from MIR and LLVM IR (zero runtime cost) | `std::marker::Sendable`, `Shared`, `Linear`, `SuspendSafe`, `NoSuspendHazard`, `TaskBound`, `zom::lint::*`, all six concurrency gates S4 |
| **COMPILE_TIME** | Lexer → Binder → S1 WFF → S5 Lowering → (consumed by codegen; does not escape to runtime) | Lowered into layout decisions, LLVM `!llvm.module.flags`, `CallSite::force_inline` / `cold` bits. No reflection symbol emitted | `zom::repr(C)`, `zom::ffi::link_name`, `zom::ffi::no_mangle`, `zom::hint::inline`, `zom::hint::cold`, `zom::hint::unroll` |
| **RUNTIME_REIFIED** | Full pipeline → survives codegen → present in the final binary | Emitted into the `.zom_meta` ELF/Mach-O section as a structured (TLV-length-prefixed) record with: `(u16 version, u32 attr_count, attr_entry*)` where each `attr_entry = (fq_name_hash, tier, arg_payload_length, arg_payload_bytes)`. A `extern "C" zom_get_type_attr(type_id_t) → AttrBlob const*` ABI is exported by the runtime. SIDE-CONDITION (§16.6.1 ZOM0760): any coercion T → erased (dyn Any, void* transit, transmute, Any-based dispatch) is STATICALLY REJECTED unless the compiler can prove every RUNTIME_REIFIED marker on T is also present on the erased type-id metadata blob. Coercions whose target type-id blob is compiler-controlled (e.g. 'dyn Any' vtable) are automatically preserved; all other user-written coercions require an 'unsafe' block to attest the marker set. ZOM0760 RUNTIME_REIFIED_ERASURE fires on violation. | Explicit opt-in only via `#[zom::attribute::retain(RUNTIME_REIFIED)]` on a user marker declaration. No Tier-0 attribute is RUNTIME_REIFIED without a separate RFC. |

### 16.6.2 Retention-by-phase matrix (attribute → phase)

See the per-phase matrix in the final design (`finalRetention`). Additional guarantees:
- **Deterministic ordering.** Marker-closure results sorted by `MarkerDecl::source_name` before attaching to a type; two source files differing only in the textual order of marker impls produce byte-for-byte identical outputs (reproducible builds).
- **Negative-impl retention.** Negative impl `impl !M for T` passes through MIR as a 2-bit *exclusion bitmap* in the marker-bitset metadata: `0b00` = neither, `0b01` = positive derived, `0b10` = explicitly negated, `0b11` = coherence error (`ZOM0710`). Downstream crates see this bitmap and **cannot** revive a negated marker via a blanket impl (else `ZOM0712 DownstreamBlanketRevivesNegated` at monomorphisation time).
- **@-sugar retention.** `@variadic x: …` is never stored as a dedicated AST node; parser rewrites to `OuterAttribute{ path = zom::param::variadic }` and keeps the `@` token's span in `parser_applied_sugar`. Pretty-printer round-trips back to `@variadic`.
- **Cross-cutting integrity invariant.** NO pass (S0–S5) may silently INSERT or REMOVE an attribute from a node. The only allowed mutations are: (a) binder appending synthetic doc-param nodes, (b) macro expansion substituting Tier-2 markers with their expansion (cloned into `pre_expansion_attrs` side-table), (c) lowering zeroing type-safety side-conditions. Any other mutation `assert(false)` in debug builds.

---

## 16.7 Tier architecture

The attribute/marker system is split into three syntactic tiers that map to the retention model above plus extension authority and execution stage.

| Tier | Name | Owner | Retention | Execution stage | Extension authority |
|---|---|---|---|---|---|
| **Tier 0** | *Compiler built-in semantic attributes* | Language Core Team (RFC + 2/3 vote) | SOURCE_ONLY, TYPECHECK_ONLY, or COMPILE_TIME (per attribute; 16.8 lists each) | Binder S0, Well-formedness S1, Lowering S5, Codegen | **Closed** — user code cannot declare Tier-0 attributes. The list is exhaustive and grows only via RFC. |
| **Tier 1** | *Standard-library marker attributes* | Standard-Library Team (RFC + soundness appendix) | TYPECHECK_ONLY (markers), COMPILE_TIME (layout markers `Pod`, `StableAbi` also drive layout) | S2 Lattice, S3 Modal Closure, S4 Usage, S5 Lowering (layout side-channel) | Closed for new *concurrency* markers; new layout/POD markers require `zom-std` RFC. Declared in the `std::marker::*` crate. |
| **Tier 2** | *User macro / user marker interface* | Any crate author | TYPECHECK_ONLY for marker conjunctions; SOURCE_ONLY for pure proc-macro transforms; COMPILE_TIME or RUNTIME_REIFIED via explicit opt-in (requires `zom::feature::enable("marker_macros")`) | After S0, before S2 — runs in a sandboxed `TokenStream` → `TokenStream` engine; side effects forbidden by the stage contract (16.10). | Open. Any crate may export a `marker M = B1 + … + Bn;` conjunction (structural Tier-2). Proc-macro attributes that take `TokenStream` input require explicit crate feature-gating and an `unsafe impl Macro` signature. |

### 16.7.1 Stage isolation diagram

```
 ┌───────── S0 Binder / Name Resolution ─────────┐
 │   Resolve AttributePath → resolved_symbol     │
 │   Doc-param synthesis                         │
 │   Register MarkerImplDecl candidates          │
 └─────────────▲────┬────────────────────────────┘
               │    │
               │    │  Tier-2 structural conjunctions
               │    │  (marker M = A + B) — trivial expansion
               │    │
               │    ▼
 ┌───────── Tier-2 macro engine ────────────────┐   ← sandboxed, TokenStream in/out
 │   Structural expansion ≡ its RHS (I2 inv.)   │
 │   Proc-macro: deterministic, no I/O, ≤16GiB  │
 │   pre_expansion_attrs cloned to side-table    │
 └─────────────▲────┬────────────────────────────┘
               │    │
               │    ▼
 ┌───────── S1 Well-formedness ─────────────────┐
 │   ArgsSchema, target, arity, orphan rules     │
 └─────────────▲────┬────────────────────────────┘
               │    │
               │    ▼
 ┌───────── S2 Lattice ─────────────────────────┐
 │   R0–R9 cross-marker edges; user edges        │
 └─────────────▲────┬────────────────────────────┘
               │    │
               │    ▼
 ┌───────── S3 Modal Closure / Coherence ───────┐
 │   Fixpoint + negative impl exclusion          │
 └─────────────▲────┬────────────────────────────┘
               │    │
               │    ▼
 ┌───────── S4 Usage verification ──────────────┐
 │   G1–G6 concurrency gates + lint frames       │
 └─────────────▲────┬────────────────────────────┘
               │    │
               │    ▼
 ┌───────── S5 Lowering → Codegen ──────────────┐
 │   COMPILE_TIME attrs consumed; rest erased    │
 └──────────────────────────────────────────────┘
```

---

## 16.8 Tier 0 built-in semantic attributes

The complete Tier 0 list (≥ 15 entries) grouped by family. Each entry lists the full path, allowed attachment targets, ArgsSchema, semantic effect, and type-check rules.

### 16.8.1 Layout family

    // ArgsSchema = shape description | accepts_tt (boolean).
    //   accepts_tt = true ⇒ parser also accepts the free-form ttBody
    //   branch in A-006 for this attribute; else hard-error on ttBody.

| # | Full name | Targets | ArgsSchema | Semantic effect | Type-check rules |
|---|---|---|---|---|---|
| T0-1 | `zom::repr` | struct, class, enum, alias | `Enum { C, Rust, Transparent, Packed }` OR `Struct { layout: Enum{C,Rust,Packed}, align: u32? }` | Instructs layout engine. `repr(C)` → C-compatible layout (deterministic field order, `#[repr(C, align(n))]` → C ABI + alignment). `repr(Transparent)` → single field, same ABI as its field. `repr(Packed)` → zero padding (unsafe-gated). | `repr(Transparent)` requires exactly one non-zero-sized field; else `ZOM0620`. `repr(Packed)` requires an `unsafe` block surrounding every reference-taken field access (16.8.5, `ZOM0801 PackedBorrow`). `align(n)` must be a power of two (`ZOM0621`). |
| T0-2 | `zom::layout::align` | struct, class, field (as override) | positional `u32` | Overrides alignment for the aggregate or individual field. | Power-of-two check; field-level align must be ≤ struct-level align (`ZOM0622`). |

### 16.8.2 FFI family

| # | Full name | Targets | ArgsSchema | Semantic effect | Type-check rules |
|---|---|---|---|---|---|
| T0-3 | `zom::ffi::link_name` | function, static item | `= string` literal | Sets the exported symbol name in the object file. | Target must have public visibility; name must not contain NUL bytes (`ZOM0623`). |
| T0-4 | `zom::ffi::export_name` | function, static item | `= string` | Same as link_name, but also implies `pub` visibility (panics if private; `ZOM0624`). | Same as T0-3. |
| T0-5 | `zom::ffi::no_mangle` | function, static item | unit (bare) | Disables name mangling; symbol = source name exactly. | Requires `pub` or `protected` visibility (`ZOM0625`). Conflict with `link_name` → `ZOM0626`. |
| T0-6 | `zom::ffi::c_abi` | function, alias(fn-type) | unit | Forces C calling convention (`cc = "c"`). | Cannot apply to closures or methods with `self` (`ZOM0627`). |

### 16.8.3 Marker / unsafe-gating family

| # | Full name | Targets | ArgsSchema | Semantic effect | Type-check rules |
|---|---|---|---|---|---|
| T0-7 | `zom::lang::unsafe_cell` | struct declaration | unit | **Lang-item marker.** Tells the Shared-deriver that fields of this type block Shared propagation (see 16.13 negative impl for `UnsafeCell<T>`). | At most **one** field allowed (raw value); any additional fields → `ZOM0628`. Only one type per crate may carry this attribute (enforced by a crate-level singleton; `ZOM0629`). |
| T0-8 | `zom::lang::sized` | (intrinsic; not directly written) | — | Implicit on all types that are `Sized`. User code never writes this; it is a lang-item. Existential for completeness. | — |
| T0-9 | `zom::feature::enable` | crate (inner-attr only), item | `String | List<String>` | Opts the crate/item into a named feature gate. `marker_macros` enables Tier-2 `marker …` declarations. Unknown feature name → `ZOM0630`. | Applies push-down: crate-level `#![zom::feature::enable("X")]` implies all items in the crate see the feature; an item-level override cannot *remove* a crate-level feature (monotone). |
| T0-10 | `zom::feature::register_tool` | crate | `= string` literal | Declares a crate as a Tier-2 proc-macro tool under the given name; enables `dep::<tool>::*` resolution downstream. | Requires crate kind = `proc-macro` in `Cargo.zom` (`ZOM0631`). |

### 16.8.4 Stage-control / hint family

| # | Full name | Targets | ArgsSchema | Semantic effect | Type-check rules |
|---|---|---|---|---|---|
| T0-11 | `zom::hint::inline` | function, expr | bare OR `= Enum { always, never, hint }` | Passes inline-hint to codegen; `always` → force inlining. | On expr: restricted to call-expression and closure-expression targets (`ZOM0603` if elsewhere). |
| T0-12 | `zom::hint::cold` | function, expr | bare | Marks call site as cold; codegen biases towards outlining and non-speculation. | Same target restrictions as T0-11. |
| T0-13 | `zom::hint::unroll` | loop statement (for/while) | bare OR `positional u32` | Requests unroll. Numeric arg = exact unroll factor (compiler may reject with `W7301` if not beneficial). | Only on loops (`ZOM0632`). |
| T0-14 | `zom::hint::likely` / `zom::hint::unlikely` | expr (boolean, in if-cond / match-guard) | bare | Branch-weight hint. | Error if attached to non-boolean expressions (`ZOM0633`). |

### 16.8.5 Lint / stability / doc family

| # | Full name | Targets | ArgsSchema | Semantic effect | Type-check rules |
|---|---|---|---|---|---|
| T0-15 | `zom::lint::allow` | any scope-bearing target (item, stmt, block via inner-attr, crate) | `String | List<String>` where each string is `ZOM\d{4}` or a registered tool lint `tool_name::lint_code` | Pushes a suppression frame onto the DiagnosticEngine; the frame pops at the end of the target's scope. Innermost wins. | Unknown lint code → `ZOM0616`. |
| T0-16 | `zom::lint::deny` | any scope-bearing target | same schema as `allow` | Upgrades the matching diagnostic severity to hard error. `deny` overrides an outer `allow`; a subsequent `allow` further inside does not override `deny` (highest-wins, not latest-wins, to prevent code from silently suppressing required checks). | Same as T0-15. |
| T0-17 | `zom::doc` | item, field, param, function return, error variant | `= string` (equals form only) | Supplies documentation text. Multiple `#[zom::doc = "…"]` lines are concatenated with a newline. | The binder synthesises `#[zom::doc::param(name, desc)]` from `@param <name> <desc>` in doc comments (see S0). |

T0-18 zom::stability::deprecated(since: String?, note: String?)
      // Warn on use; optionally cite version + free-form note.
      // Bare-form whitelist legacy entry: 'deprecated' (W7105, rewrites to
      // zom::stability::deprecated with empty args).
      // Targets:  any declaration.
      // IMPORTANT: 'since' and 'note' are NAMED KEYS INSIDE this attribute's
      //   struct schema — they are NOT standalone Tier-0 attributes.
      //   Writing '#[zom::stability::since("1.0")]' is an error:
      //   ZOM0617 BareAttribute (since it's a 2-segment path whose leaf
      //   'since' does not exist in the Tier-0 registry).

T0-19 zom::stability::unstable(feature: String, issue: u64?, reason: String?)
      // Gate access behind '#![zom::feature::enable("...")]' crate head gate.
      // Emit ZOM034 UnstableApi at each use site without matching feature.

T0-20 zom::stability::discriminator(value: integer_literal)
      // Pin a fixed, ABI-stable, integer DISCRIMINATOR to an error-variant
      // or enum-variant declaration.  Matches the '@error' discriminator
      // convention of ZOM's error system (see Ch.11). 'value' accepts
      // u8 | u16 | u32 | u64 literal (decimal, hex 0x, octal 0o, binary 0b).
      // Examples:
      //   error E { #[zom::stability::discriminator(0x01)] Io(...) }
      //   enum   F { #[zom::stability::discriminator(42)]  Tag }
      // Targets:  error variant, enum variant.
      // Schema :  single positional integer literal.
      // Conflict with T1-14 std::marker::Discriminant<T>:
      //   • Discriminant<T> is a marker on enum *types* (lattice-level);
      //   • this attribute attaches to individual *variants* and pins
      //     their runtime tag-value; the two are orthogonal.

T0-21 zom::attribute::retain(tier: RetentionTier, structural: bool? = false)
      // User-facing opt-in override of a marker's retention tier (see §16.6).
      //   tier  ∈ {SOURCE_ONLY | TYPECHECK_ONLY | COMPILE_TIME | RUNTIME_REIFIED}
      //   structural — if true, propagates through nominal/alias/type-constructor
      //                wrappers so a bare newtype(u32) retains markers of u32
      //                at RUNTIME_REIFIED depth. Requesting structural
      //                RUNTIME_REIFIED on a non-nominal marker is rejected
      //                with ZOM0636 RUNTIME_REIFIED_STRUCTURAL at S1.
      // Attachment targets: markerDeclaration, markerImplDeclaration,
      //                     traitDeclaration, struct/class/enum declaration.

T0-22 zom::lang::runtime_only
      // Declares that the attached item (method of an interface, typically
      // SuspendContract's user-invocable helpers) is ONLY callable at
      // runtime and cannot be invoked at comptime / const-eval.
      // Targets: fun, method, static item, const item.
      // Semantics: a compile-time evaluation attempt that reaches this item
      //   emits ZOM0635 RuntimeOnlyInConstCtxt; S0 comptime visitor flags it.

T0-23 zom::param::move
      // Forces by-move capture semantics for the parameter. The value is
      // treated as Linear-consumed on the caller side: the parameter slot
      // is MOVEd into the callee frame (not borrowed), and the caller's
      // local becomes inaccessible after the call (Linear-consume lattice
      // edge is asserted on the caller-side value).
      // Targets: ParameterDeclaration (only).
      // Schema :  unit/bare.

T0-24 zom::panic(strategy)
      // Crate-level panic strategy override. Selects how panics are
      // translated at code-generation time for every profile, overriding
      // the profile-based default declared in Zom.toml `[profile.*].panic`.
      // Grammar  : zom::panic = '(' ( "unwind" | "abort" ) ')'
      // Scope    : crate root only. Written as #![zom::panic(abort)].
      // Semantics: "unwind" causes unwinding with drop-elaboration of
      //   stack frames up to a catch_unwind boundary. "abort" causes
      //   immediate process abort on panic, zero drop glue executed.
      // Conflict: if both this attribute and Zom.toml `[profile.*].panic`
      //   specify explicit and *different* strategies for the same crate,
      //   emit ZOM0963 PanicStrategyInconsistent with spans pointing at
      //   both declaration sites.

T0-25 zom::oom(strategy)
      // Crate-level out-of-memory policy selector.
      // Grammar  : zom::oom = '(' ( "error" | "panic" ) ')'
      // Scope    : crate root only. Default = "error" (§11.8 cross-ref).
      // Semantics: "error" — every built-in allocation function returns a
      //   union `T | AllocError` rather than panicking, forcing the
      //   caller to handle OOM explicitly via `?!` or match. "panic" —
      //   all allocation functions skip the union return and raise a
      //   panic on OOM; callers cannot observe the failure via `?!`.
      // Diagnostics: if "panic" is chosen and downstream code still
      //   expects an error-union return, the checker emits a tailored
      //   variant of ZOM0951 RaisesSignatureMismatch citing the
      //   `#[zom::oom(panic)]` attribute as the reason.

T0-26 zom::error(trace)
      // Backtrace-capture selector for Error instances.
      // Grammar  : zom::error = '(' ( "trace" ) ')'
      // Scope    : crate root (applies blanket to every impl Error in
      //   the crate) OR a specific enum variant / struct declaration
      //   (targeted override, narrower scope wins).
      // Semantics: when an Error-instance is *constructed* (not when it
      //   is propagated via `?!`), the runtime captures a stack
      //   backtrace and stores it in the Error's hidden side-table slot,
      //   or in an explicit `Backtrace` field if one is annotated with
      //   `#[backtrace]` (see T0-28). Cross-reference §11.11 for the
      //   runtime side-table design.

T0-27 zom::error_boundary
      // Panic-boundary wrapper for FFI entry points.
      // Grammar  : zom::error_boundary (unit, no arguments).
      // Scope    : function-level. Canonically applied to `extern "C"`
      //   functions that are callable from foreign code, but permitted
      //   on any function.
      // Semantics: at code-generation time, the function body is
      //   implicitly wrapped in a `zom::panic::catch_unwind(|| body)`.
      //   If the closure panics, the boundary catches the unwind,
      //   converts the panic payload into a clean error return. The
      //   boundary has two effects:
      //     (1) it is undefined behavior for an unwinding panic to cross
      //         an `extern "C"` ABI boundary without this wrapper;
      //     (2) the ZOM0965 UndefinedBehaviorOnUnwind diagnostic is
      //         *suppressed* for any function annotated with this
      //         attribute.
      //   See Ch.18 §18.4 for the FFI-level expansion and Ch.11 for the
      //   error-type mapping.

T0-28 zom::derive(Error)
      // Compiler-driven auto-derivation of the Error interface.
      // Grammar  : zom::derive( '(' 'Error' ( ',' ErrorDeriveMeta )* ')' )
      //            ErrorDeriveMeta = 'debug' | 'display' | 'default'
      // Scope    : enum declarations and struct declarations only. Any
      //   other target → ZOM0960 DeriveErrorOnNonEnumStruct.
      // Semantics: synthesizes impls for the three methods of
      //   `interface Error`:
      //     1. `display(self) -> String` using variant/struct name or a
      //        per-variant `#[message = "..."]` string template.
      //     2. `source(self) -> Option<&dyn Error>` derived from fields
      //        annotated with the helper attribute `#[source]`.
      //     3. `backtrace(self) -> Option<&Backtrace>` derived from the
      //        first field typed `Backtrace` and annotated `#[backtrace]`;
      //        falls back to the compiler-owned side-table otherwise.
      // Helper attributes:
      //   `#[source]` on a field — feeds its value into `Error::source()`.
      //   `#[backtrace]` on a `Backtrace`-typed field — uses user-supplied
      //        storage instead of the compiler side-table.
      //   `#[message = "..."]` on an enum variant — custom display
      //        template, parsed with the standard `format!` mini-language
      //        using variant fields as substitution variables.
      // Diagnostics: ZOM0961 ErrorFieldInvalid if `#[source]` /
      //   `#[backtrace]` / `#[message]` appear on a node whose type or
      //   position does not match the expected schema.

---

## 16.9 Tier 1 stdlib marker attributes

Complete Tier 1 list (≥ 10 entries, 6 concurrency + 9 layout/Pod). All reside in `std::marker::*`. The six concurrency markers are the normative surface.

### 16.9.0  Tier-1 marker uniform shape
    ALL Tier-1 built-in markers under the 'std::marker::*' namespace take
    ZERO arguments in their attribute / Surface-1 form. Write e.g.:
        #[std::marker::Sendable]
        #[std::marker::Linear]
        #[std::marker::TaskBound]
    and NOT:
        #[std::marker::Sendable(auto = true)]            // ← illegal (W7103)
        #[std::marker::Linear(consume_fun = drop)]       // ← illegal (W7103)
    Auto-derive behaviour for each marker is INHERENT / structural (defined
    per-entry below), NOT user-switchable via named attribute arguments.
    W7103 MarkerWithUnexpectedArgs fires for any attempt to pass args on a
    bare Tier-1 marker attachment.
    Surface-2 'marker Sendable;' declarations similarly carry no argument
    list; Surface-3 'impl Sendable for T' / 'impl !Sendable for T' accept
    optional 'unsafe' prefix and optional where-clause, no keyword args.

### 16.9.1 Concurrency family (6 markers — the canonical six)

| # | Full name (short bare name) | Inherent logic (see 16.12) | Auto-derive rule | Where used (stage S4 gate) |
|---|---|---|---|---|
| T1-1 | `std::marker::Sendable` (`Sendable`) | `☐_S` box — cross-task transfer preserves ownership invariants | Yes: struct S is Sendable iff every field is Sendable. | Gate G1: `spawn(f)` closure captures + `channel::send(v)` require `T: Sendable`. |
| T1-2 | `std::marker::Shared` (`Shared`) | `☐_H` box — every shared borrow preserves immutability invariants | Yes: struct S is Shared iff every field is Shared AND no field's type has a `impl !Shared` (e.g. `UnsafeCell<T>`) negative impl. | Gate G2: `Arc<T>`, static items require `T: Shared`. |
| T1-3 | `std::marker::Linear` (`Linear`) | `☐_L` box — exactly-one-consume per control-flow path; no duplicate, no forgotten | **Never auto-derives** (too conservative; opt-in only). | Gate G3: Linear-typed value missed on a path → `ZOM0743a`; double-consume → `ZOM0743b`. |
| T1-4 | `std::marker::SuspendSafe` (`SuspendSafe`) | `☐_K` box — every suspension edge is hazard-free (no live lock held across `await`) | Yes: function body is SuspendSafe iff every live slot across any `await` has type satisfying `NoSuspendHazard`. | Gate G5: `Nursery` / cancel-scope body requires `body: SuspendSafe`. |
| T1-5 | `std::marker::NoSuspendHazard` (`NoSuspendHazard`) | `☐_N` box — strictly stronger than `SuspendSafe`; hazard set of source world is empty | Yes: for primitives, Copy types, types composed entirely of NoSuspendHazard fields. NOT for `MutexGuard`, `RawLock`, `SpinLockGuard`. | Gate G4: any slot with type T where `T: NoSuspendHazard` does not hold and T is lock-like across `await` → `ZOM0744`. |
| T1-6 | `std::marker::TaskBound` (`TaskBound`) | `◇_T` diamond — strictly task-affine; value cannot escape via R_send | **Never auto-derives** (opt-in only; e.g. `TaskLocal<T>`). | Gate G6: `TaskLocal<T>` store / spawn require `T: TaskBound ⊕ !Sendable`; spurious cross-task move → `ZOM0746`. |

**ZOM-concurrency-specific cross-marker rules (R0–R9 lattice edges)** are listed in 16.11 stage S2 and reused by the formal logic in 16.12.

### 16.9.2 Layout / POD family (9 markers)

| # | Full name (bare name) | Kind | Meaning | Where enforced |
|---|---|---|---|---|
| T1-7 | `std::marker::Copy` | structural + built-in | Values duplicated by bitwise copy; Drop not required | Copy-bound; conflict with Linear (R2) → `ZOM0752`. |
| T1-8 | `std::marker::Pod` | structural | Plain Old Data: valid for any byte pattern; no Drop; `Copy`; safe to `memcpy`. | Layout engine, FFI signatures, `std::mem::transmute`. Implies `ZeroInit + NoUninit + Copy` (R3). |
| T1-9 | `std::marker::ZeroInit` | safety | All-zero bit pattern is a valid inhabitant | `std::mem::zeroed()` safety gate. |
| T1-10 | `std::marker::NoUninit` | safety | No uninitialised padding bits observable by safe code | `std::mem::MaybeUninit::assume_init()` safety gate. |
| T1-11 | `std::marker::StableAbi` | compatibility | Layout and ABI are forward-stable across stdlib minor versions; requires `Pod` (R4) + explicit layout hash constant in marker-impl body (see 16.14, `repr(C)` FFI struct example). | FFI-cross-version boundary checks. |
| T1-12 | `std::marker::Sized` | structural | Type is statically sized (runtime size known). | Implicit; required for stack locals, value parameters. Implied by `Discriminant<T>` (R5). |
| T1-13 | `std::marker::NoInteriorMuta` | structural | Type contains no `UnsafeCell` transitively (dual of Shared). | Enables the R8 default Shared derivation. |
| T1-14 | `std::marker::Discriminant<T>` | enum-specific | Enum has explicit discriminant values and a discriminant type T. | Implies `Sized` (R5). Used by `std::mem::discriminant`. |
| T1-15 | `std::marker::Linear` | (listed above under concurrency; impacts layout via drop ordering) | — | — |

### 16.9.3 Utility markers (beyond the 15 baseline — 3 more, per the ≥ 10 requirement)

| # | Full name (bare name) | Effect |
|---|---|---|
| T1-16 | `std::marker::NonExhaustive` | Match arms over this type must include a wildcard `when _`; adding new variants downstream will not break downstream callers. (Marks enums only; error on non-enum targets → `ZOM0613`.) |
| T1-17 | `std::marker::ThreadSafeChecked` | Semantic alias for `Sendable + Shared`. Used on concurrency primitive types (e.g. `Mutex`, `RwLock`) that the compiler must verify satisfy both. Adding it is equivalent to asserting the conjunction. |
| T1-18 | `std::marker::MustUse` | On a function return type: warn if the return value is ignored (emits `W7501 UnusedMustUse`). On a type: warn if any expression of that type is dropped without pattern-matching or an explicit assignment. The Tier 0 `zom::lint::deny(ZOM0750)` upgrade path converts to hard error. |

---

## 16.10 Tier 2 user-macro interface

Tier 2 exposes **two** extension surfaces: a *structural* one (`marker M = B1 + … + Bn;` — pure conjunction, no programmatic code) and a *procedural* one (the `Macro` trait over `TokenStream`). This section specifies only the interface contract and stage isolation; concrete proc-macro body syntax is deferred.

### 16.10.1 Structural marker declaration (no Macro impl needed)

```zom
marker SendSync = std::marker::Sendable + std::marker::Shared;   // pure conjunction
marker ScopedLock<'a, T>
  = std::marker::Linear
  + std::marker::NoSuspendHazard
  where T: std::marker::Shared;
```

Semantics: after stage S2, a structural marker M is **semantically indistinguishable from its expansion**. Staging invariant I2: no algorithm in S3+ can distinguish `#[SendSync] on S` from `#[Sendable] #[Shared] on S`.

I2a unsafe-gate propagation: applying a Tier-2 conjunction marker via
    attribute-form '#[mycrate::Conj]' on a type declaration triggers the
    SAME unsafe gate as writing the implied positive marker-impl set
    verbatim would. S1e WFF detects Unsafe-Override scenarios in the
    expansion product and emits ZOM0751 UnsafePositiveMarkerImpl unless the
    enclosing scope carries the 'unsafe' keyword (on the item itself, the
    enclosing block, or the enclosing 'unsafe module'). Tier-2 macro authors
    cannot circumvent I2a merely by writing 'unsafe impl Macro' on the
    proc-macro crate side (see §16.10.2).

### 16.10.2 The Macro interface (procedural Tier 2)

```
interface Macro {
    // ── Contract ──────────────────────────────────────────────
    // • pure function:  f: TokenStream × TokenStream → TokenStream
    //   first argument  = attribute tokens (inside attrTokenTree)
    //   second argument = annotated-item tokens
    //   returns         = replacement-item tokens (or same item for attribute-style macros)
    // • deterministic over identical inputs (enables incr. comp.).
    // • stage-isolated: no file I/O, no networking, no access to
    //   compiler internals beyond the two TokenStream arguments.
    // • heap bounded by a sandbox (default 16 GiB; configurable
    //   per-crate via Cargo.zom; exceeds → ZOM0680 OomInProcMacro).
    // • wall-clock bounded (default 60 s; exceeds → ZOM0681).
    // • re-entrant (the macro engine may invoke the same Macro
    //   impl concurrently for multiple items; the impl MUST be
    //   thread-safe — non-thread-safe impls → ZOM0682, caught
    //   by a marker-gate on the impl declaration itself).
    // ───────────────────────────────────────────────────────────

    #[zom::lang::macro_entry]
    fun expand(attr: TokenStream, item: TokenStream) -> TokenStream
        raises MacroError;

    // Optional: supply a human-readable description for LSP hover.
    #[zom::attribute::retain(COMPILE_TIME)]
    const DESCRIPTION: str = "";
}

// A concrete procedural attribute is declared as:
unsafe impl Macro for MyProcMacro {
    // ── unsafe semantics (TWO independent safety conditions) ─────────────
    // (a) Stage isolation: expand() is I/O-free, deterministic,
    //     terminating-bounded, and does not observe global state.
    //     (The S1 re-entrant checker enforces this at expansion time.)
    // (b) Marker-impl soundness: EVERY positive marker-impl emitted in
    //     'expand()''s output TokenStream MUST EITHER
    //       (i)  be auto-derivable per the target type's field/variant
    //            analysis under rules in §16.13.3/§16.13.4,  OR
    //       (ii) literally contain the 'unsafe' keyword in the source of
    //            the emitted impl block (i.e. 'unsafe impl M for T { ... }').
    //     The 'unsafe' prefix on 'impl Macro' does NOT discharge (b) by
    //     itself — it only certifies the AUTHOR claims (a)+(b) hold; S1
    //     validates (b) re-entrantly for ALL emitted impls IRRESPECTIVE
    //     of origin (source file or macro expansion product).
    //     Violation: ZOM0685 MacroUnsoundExpansion, citing the proc-macro
    //     crate name, the call-site span, and the offending emitted impl.
    const DESCRIPTION: &'static str = "...";
    const DECL_SOURCE_LOC: Option<SourceLoc> = None;   // ← compiler-filled
    fn expand(input: TokenStream, ctx: &MacroCtx) -> TokenStream { ... }
}
// Omitting `unsafe` → ZOM0683 ProcMacroImplMustBeUnsafe.
```

### 16.10.3 TokenStream contract

- **Input `TokenStream`** — an ordered list of `Token`s, each with `SyntaxKind`, `raw_text: str`, and a `source_span` (for diagnostics mapping back into user code).
- **Output `TokenStream`** — same shape; each produced token carries a *synthetic* span linking back to the closest input span. The compiler maps any diagnostic raised against an output token back onto the source input span for the user.
- **AttrTokenTree escape hatch:** the `#[…( … )]` form uses `attrTokenTree` (16.2 [A-010]) to carry free-form syntax that is not further parsed by the attribute grammar; it arrives as a nested `TokenStream` subtree.

### 16.10.4 Stage isolation contract (I2 invariant, reprise)

1. The proc-macro engine runs **after S0 binder** (so all attribute paths are resolved) and **before S1 well-formedness** (so the expansion can be fully checked by the same pipeline).
2. Pre-expansion `ModifierList` contents are cloned into a side-table `pre_expansion_attrs: Map<NodeId, List<AttributeNode>>` for incremental compilation and diagnostics.
3. After expansion, the new item's AST is injected into the tree and re-enters S1 (and its own S0 binder sub-pass for the newly-introduced names).
4. **No reentrancy on the same node.** A node expanded by macro M is never re-fed to M. Indirect cycles (M → N → M) are detected by a generation counter and raise `ZOM0684 ProcMacroCycle` with a complete trace.

### 16.10.5 No concrete macro-body syntax

Syntax inside the body of a `fun expand(...)` (e.g. quote syntax, `$var` interpolation, pattern-matching on tokens) is the remit of the compile-time-reflection chapter and is deliberately unspecified here. This chapter makes only the minimum promise: `TokenStream` in, `TokenStream` out, with strict stage isolation.

---

## 16.11 Deterministic stage ordering

The attribute/marker checker is embedded in the compiler as **stages S0–S6** (reproduced from `finalCheckerStages`). Cross-stage dataflow is a strict DAG. Diagnostic blocks `ZOM0600..ZOM0699` are reserved for the attribute system; `ZOM0700..ZOM0799` are reserved for marker-impl coherence and concurrency gates.

### 16.11.1 Full pipeline topology (10 stages — S0 + seven sub-steps + two macro phases + S6)

Counting each processing step, the full sequence is:

```
 S0   Binder / Name Resolution
 ├─S0a  AttributePath resolution (zom::* / std::marker::* / dep::<crate>::*)
 ├─S0b  Negative-bound resolution in where-clauses
 ├─S0c  DocParamSynthesisPass (@param → synthetic zom::doc::param)
 └─S0d  MarkerImplDecl candidate registration
      │
      ▼
 S0M  Tier-2 proc-macro expansion  (Macro trait invocations)
      │
      ▼
 S1   Well-formedness / Arity / Tier validation
 ├─S1a  ArgsSchema validation
 ├─S1b  Target-kind validation (ZOM0613)
 ├─S1c  MarkerImplDecl arity (ZOM0614)
 ├─S1d  Orphan-rule enforcement (ZOM0701, ZOM0702)
 └─S1e  Negative-impl justification check (ZOM0701)
      │
      ▼
 S2   Lattice & Modal-Semantic Embedding
 ├─S2a  Build marker lattice L from R0–R9
 ├─S2b  User MarkerDecl base-bound closure + cycle detect (ZOM0615)
 └─S2c  Verify Tier-2 ≤-edges
      │
      ▼
 S3   Modal Closure / Coherence
 ├─S3a  Seed positive MarkerImplDecls
 ├─S3b  Monotone fixpoint (lattice-edge propagation)
 ├─S3c  Apply negative impls → REJECT markings
 ├─S3d  Coherence: ≤ 1 positive impl per (marker, norm-type-head)
 └─S3e  Deterministic sort of MarkerSet entries
      │
      ▼
 S4   Usage Verification (6 canonical concurrency gates + lint gating)
 ├─G1 spawn + channel send                → T : Sendable            (ZOM0741)
 ├─G2 Arc<T> / static                    → T : Shared              (ZOM0742)
 ├─G3 Linear-resource API                → exactly-one-consume      (ZOM0743a/b)
 ├─G4 await-suspension hazard            → slot: NoSuspendHazard    (ZOM0744)
 ├─G5 Nursery / cancel-scope body        → body : SuspendSafe       (ZOM0745)
 ├─G6 TaskLocal store / spawn            → T : TaskBound ⊕ !Sendable(ZOM0746)
 └─S4g Lint gating allow/deny frame dispatch
      │
      ▼
 S5 Lowering   HIR → MIR desugar; inline bodies per T0-11 inline hints.

    §S5a Attachment-conservation principle.
        Any lowering pass that structurally decomposes a syntactic construct
        E into one or more synthesized child nodes MUST dispatch each
        attribute on E per the attribute's declared target class from
        §16.4.2, attaching/cloning/lifting exactly as specified.

    §S5b Per-construct attribute-dispatch table (exhaustive for v1 constructs):

        Source construct E            Synthesized children         Dispatch
        ──────────────────────────    ─────────────────────────    ────────
        for x in iter { BODY }        loop { let x = iter.next().?; BODY }
            • lint-level attrs (deny/allow/warn/force) → lift to enclosing
              loop (not the synthesized let) to preserve frame coverage.
            • scope-bearing attrs (suspend_free / scope_guard) → attach to
              the synthesized let's scope-extent (equivalent to BODY's
              original lexical extent: 'let' is the first statement of the
              loop, so both are frame-coextensive).
            • codegen hints (inline/cold/unroll) → attach to the loop node.
            • any attribute whose target class is not covered above →
              ZOM0761 OrphanAttrAfterDesugar with a note citing the
              specific lowering rule; the attribute is PRESERVED on a
              "best-effort" nearest ancestor node and a warning W7502 fires
              in addition to the hard error.

        while COND { BODY }           (same pattern as 'for')
        try! macro / desugar          per S5a rules (see dedicated table)

        ── General rule ──────────────────────────────────────────────────
        If a lowering is NOT listed above, its author is REQUIRED to add
        a row to this table and publish a changelog entry BEFORE landing
        the transform. ZOM's RFC process treats missing S5b dispatch rows
        as BLOCKING review comments.

§S5c Generic-function inlining ordering.
    '#[zom::hint::inline]' or '#[zom::hint::inline(always)]' on a generic
    function '<T: M1 + M2 …>' MUST be processed EITHER:

    (a) BEFORE the S4 concurrency-gate verification pass runs over the
        caller, so each monomorphisation instance re-runs the full S4
        with T substituted by the concrete caller-side actual type; OR

    (b) AFTER S4 (i.e. as a pure MIR-to-MIR rewrite), BUT the inliner
        is REQUIRED to re-run a REDUCED S4 pass over each inlined body
        against (1) the substituted parameter-type bounds, and (2) the
        surrounding S4 gate context. If reduced-S4 detects a marker-bound
        violation after substitution, emit ZOM0762
        InlineMarkerBoundViolation and FALL BACK to an out-of-line call
        (the inline is suppressed for that particular call-site and the
        compiler emits a note W7503 InlineMarkerSuppressed).

    Implementations MUST document which of (a)/(b) is chosen; if neither
    is implemented, the compiler MUST ignore the inline hint on any
    generic function that carries non-trivial marker bounds and emit
    W7504 GenericInlineIgnored.

              Failure semantics:   inlining always terminates;
                                   infinite-inline → abort + ZOM0755;
                                   orphan attr after desugar → ZOM0761.
 ┌──────────────────────────────────────────────────────────────────┐
 │ S6   LSP & Rustdoc extraction                                    │
 └──────────────────────────────────────────────────────────────────┘
```

### 16.11.2 I/O contracts

| Stage | Inputs | Outputs |
|---|---|---|
| S0 | Parser AST, unresolved `AttributePathNode*`s, doc-comment strings | AST with `AttributePathNode::resolved_symbol` filled; synthetic doc-param nodes appended; `Map<(Marker, NormTypeHead), List<ImplId>>` candidate-table |
| S0M | AST + candidate-table + Macro-trait registry | New AST (expanded items); `pre_expansion_attrs` side-table. Errors: `ZOM0680..ZOM0684`. |
| S1 | AST post-expansion | `AttributePathNode::args_validated` bit set. `ZOM0610..ZOM0617`, `ZOM0701`, `ZOM0702` on failure. |
| S2 | AST | Lattice L with edges R0–R9 ∪ user edges. `ZOM0615` on cycles. |
| S3 | AST + L + candidate-table | `MarkerSet bitset: Map<TypeNode, u64>`, exclusion bitmap. `ZOM0710..ZOM0712` on coherence failure. |
| S4 | AST + MarkerSet + CFG + live-variable analysis | Program-point annotations of hazard sets; diagnostics `ZOM0741..ZOM0746`. Lint-suppression stack consumed. |
| S5 | AST + MarkerSet + usage-annotations | HIR (downstream), MIR, LLVM IR. Crate-metadata (.zom-cmi) blob. |
| S6 | AST + crate-metadata | LSP responses, rustdoc HTML/JSON. |

### 16.11.3 Reentrancy rules

1. S0 (binder) may be re-entered only for newly-introduced scopes emitted by S0M (proc-macro expansion). Re-entry depth ≤ 64; exceeds → `ZOM0684 ProcMacroCycle`.
2. S1–S5 are **strictly non-reentrant**: each stage reads its inputs atomically; no mid-stage observable mutation. This enables deterministic parallel execution via Rayon-like task spawning (per crate, per item).
3. S3's fixpoint loop: each round monotonically grows the YES set. The number of rounds is bounded by `|markers|² × |type-heads-in-scope|`. In practice ≤ 3 rounds. If the bound is exceeded, `ZOM0690 MarkerClosureDivergence` is emitted with the offending (marker, type) worklist tail.

### 16.11.4  Recovery — ranked from finest to coarsest:

    ┌──────────────────────────────┬──────────────────────────────────────┐
    │ Context                      │ Sync set                            │
    ├──────────────────────────────┼──────────────────────────────────────┤
    │ attrArgumentList  (inside    │ SyncSet = { ')', ']', ',',          │
    │   '#[attr( … )]')           │             ';', EOF }               │
    │                              │  • Encountered token ∉ attrArgument │
    │                              │    grammar AND ∉ SyncSet ⇒ advance  │
    │                              │    one token, emit ZOM0618 with     │
    │                              │    multi-span highlighting ALL of   │
    │                              │    the already-retained positional/ │
    │                              │    named args, then resume parsing  │
    │                              │    the NEXT argument position.     │
    │                              │  • NEVER skip to ']' here; user must │
    │                              │    see which parts were retained.  │
    │                              │  • On ','/EOF/')'/']' in sync-set: │
    │                              │    normal structured recovery.    │
    ├──────────────────────────────┼──────────────────────────────────────┤
    │ attrEntry bracket interior   │ (fallback, ONLY if attrArgumentList │
    │   where parse-stack has no   │ recovery above has failed 3         │
    │   sane arg-list anchor)      │   consecutive times) ⇒ skip to     │
    │                              │   matching ']' via bracket-depth.  │
    │                              │   Emit: primary ZOM0618 + a        │
    │                              │   SECONDARY diagnostic ZOM0621     │
    │                              │   "Discarded N unparsed attribute  │
    │                              │    tokens" pointing at the skipped │
    │                              │    span, so the IDE visibly marks  │
    │                              │    the dropped region.             │
    ├──────────────────────────────┼──────────────────────────────────────┤
    │ modifierList / decl head     │ ';' or next declaration FIRST token │
    └──────────────────────────────┴──────────────────────────────────────┘

- **Binder errors (ZOM0610..ZOM0611):** hard. The unresolved attribute is recorded as an error sentinel; downstream passes treat it as "no attribute" to avoid cascading diagnostics.
- **S1 args/target/arity errors (ZOM0612..ZOM0614, ZOM0616, ZOM0620..ZOM0635):** hard per attribute; individual failing attributes are marked but the surrounding declaration still passes through S2+ (defensive — an incorrect attribute must never drop the declaration on the floor).
- **S2 cycle errors (ZOM0615):** hard. The offending SCC is broken at the lexicographically-largest edge to enable downstream passes (best-effort).
- **S3 coherence errors (ZOM0710..ZOM0712):** hard. Output is NOT produced (codegen is skipped).
- **S4 gate violations (ZOM0741..ZOM0746):** configurable severity. Default deny for Sendable/Shared/Linear; default warn for NoSuspendHazard (with `sanitize="suspend"` runtime checks). All are promoted to hard error under `#![zom::lint::deny(ZOM0700)]`.
- **S0M proc-macro errors (ZOM0680..ZOM0684):** hard; codegen is skipped for that crate.
- **S6:** soft-failure only — LSP returns empty responses; rustdoc emits a warning and continues.

---

## 16.12 Formal semantics of marker interfaces via modal logic

ZOM marker semantics are given as a Kripke model with step-indexed logical relations over resources, in the style of RustBelt / Iris. Nine modal operators are introduced; five are standard Box/Diamond operators; a sixth (`Box_u`) is a generic user-marker placeholder; the remaining three capture lattice propagation, negation, and uninitialisation.

### 16.12.1 Semantic universe

- **Worlds** `W = (task_id, pc, gen)` — program-point triples, where:
  - `task_id : TaskId` unique identifier per concurrency unit;
  - `pc : Nat` program-counter index into the function body;
  - `gen : Nat` monotonic generation counter for resource heaps (advances on share / reclaim).
- **Accessibility relations (six):**
  - `R_send ⊆ W × W` — cross-task value transmission (spawn capture, channel send, work-steal).
  - `R_susp ⊆ W × W` — suspension-edge reachability (every `await` point in async fns).
  - `R_scope ⊆ W × W` — lexical scope inclusion (entering/exiting blocks / functions).
  - `R_own ⊆ W × W` — ownership transfer edge (move / consume / linear handover).
  - `R_borrow ⊆ W × W` — borrow introduction edge (creation of `&T` / `&mut T` / raw ptr derivation).
  - `R_threadlocal ⊆ W × W` — task-affine region access (`thread_local!`, `pthread_key_t` ops).
- **Resources form a PCM** `(Res, •, ε)`:
  - `Res = Ownership(τ) ⊎ Borrow(τ, μ) ⊎ LockState(m, held_by) ⊎ Hazard(κ)`
  - `κ ∈ {SuspendWithLock, UseAfterConsume, DataRace}`.
  - `•` is the standard separating-conjunction merge; `ε` is empty-resource.

### 16.12.2 Nine modal operators

For each operator `Op P`, `w ⊩ Op P` means "P holds at world w under modality Op".

- **M1 — `☐_S P` (Sendable box):**
  - `w ⊩ ☐_S P  def=  ∀ w′. (w,w′) ∈ R_send  ⟹  w′ ⊩ P.`
  - Interpretation: after cross-task transmission, P still holds. `☐_S (V : owned-and-no-threadlocal-provenance) ≡ V: Sendable`.

- **M2 — `☐_H P` (Shared box):**
  - `w ⊩ ☐_H P  def=  ∀ w′. (w,w′) ∈ R_borrow ∩ R_scope  ⟹  w′ ⊩ immutability-invariant(P).`
  - Interpretation: all possible shared borrows within scope preserve P, and no intermediate world observes an internal mutation. `☐_H (value_stable) ≡ V: Shared`.

- **M3 — `☐_L P` (Linear box):**
  - `w ⊩ ☐_L P  def=  ∀ finite chains w→…→w* reachable via R_own, exactly one edge carries consume(P) AND no edge carries duplicate(P).`
  - Interpretation: exactly-one-consume, no-duplicate — affine-linear with must-call contract. `☐_L (drop_called) ≡ V: Linear`.

- **M4 — `☐_K P` (SuspendSafe box):**
  - `w ⊩ ☐_K P  def=  ∀ w′. (w,w′) ∈ R_susp  ⟹  w′ ⊩ hazard_free(P).`
  - Interpretation: every suspension edge carries no live lock-state or task-affine resource. `☐_K (¬holds_lock) ≡ fn: SuspendSafe`.

- **M5 — `☐_N P` (NoSuspendHazard box, strictly stronger than ☐_K):**
  - `w ⊩ ☐_N P  def=  (∀ w′. (w,w′) ∈ R_susp  ⟹  w′ ⊩ P)  ∧  hazard_set(w) = ∅.`
  - Source-world hazard set is empty; checker inserts runtime assertions under `cfg(sanitize="suspend")`.

- **M6 — `◇_T P` (TaskBound diamond):**
  - `w ⊩ ◇_T P  def=  ∃ lifetime ′task. (P holds at w) ∧ ∀ w′. (w,w′) ∈ R_send  ⟹  w′ ∉ scope(′task).`
  - A TaskBound value's lifetime is strictly inside the current task's and **cannot** escape via `R_send`. `◇_T (V: owned) ∧ ¬☐_S ≡ V: TaskBound`.

- **M7 — `◇_U P` (Uninitialised diamond):**
  - `w ⊩ ◇_U P  def=  ∃ w′ ⪰ w. (w,w′) ∈ R_scope* ∧ w′ ⊩ P.`
  - Used in NoUninit / ZeroInit markers: if a type is ZeroInit then the all-zeros bit-pattern satisfies `◇_U (type_inhabited)`. NoUninit `≡ ¬◇_U (uninitialised_bits_observable)`.

- **M8 — `¬☐_M P` (Negative impl / bound):**
  - `w ⊩ ¬☐_M P  def=  NOT (w ⊩ ☐_M P)` (classical negation) **plus** the global closure axiom:
  - `∀ w. w ⊩ ¬☐_M P(T)   ⟺   ⊢ neg_impl M for T`
  - Crucially, `¬☐_M P ≢ ◇_M ¬P` because the accessibility relations are not reflexive (logic is K45, not S5). This is what makes negative impls strictly stronger than "not yet derived".

- **M9 — `☐_≤ P` (Lattice propagation):**
  - If `M ≤ N` in the marker lattice (i.e. `☐_M P ⇒ ☐_N P` for all P), then the logic validates: `⊢ ☐_M P  ⟹  ⊢ ☐_N P`.
  - Instantiates the R0–R9 rules (16.12.3).

- **M10 — `Box_u P` (generic user-marker box):**
  - `Box_u` is a parametric operator placeholder: for any structural user marker `marker M = B1 + … + Bn`, `☐_M P ≡ ☐_{B1} P ∧ … ∧ ☐_{Bn} P`. The `Box_u` family is sound because it is *defined* as a conjunction of built-in boxes; user code cannot invent a new accessibility relation. Therefore the 6 accessibility relations are the closed set; user markers cannot introduce unsoundness (Theorem — proof in the RFC soundness appendix).

### 16.12.3 Cross-marker propagation axioms (R0–R9)

| Rule | Statement | Derivation | Example of *failure* if broken |
|---|---|---|---|
| R0 | `⊢ ☐_H P ⟹ ⊢ ☐_S P` | Shared ⟹ Sendable. If T is immutably-shareable without data races, it is certainly cross-task-transferable. | `MutexGuard`: not Shared → not auto-derived Sendable. R0 says the converse (Shared ⇒ Sendable) holds, which would be broken if a type could be Shared but not Sendable. |
| R1 | `⊢ ◇_T P ⟹ ⊢ ¬☐_S P` | TaskBound ⟹ !Sendable. A task-local value strictly cannot cross tasks. | `TaskLocal<Mutex<T>>`: if someone *accidentally* implemented Sendable for TaskLocal, R1 + M8 give `¬☐_S`, forcing a coherence error (ZOM0710) — which is the whole point. |
| R2 | `⊢ ☐_L P ⟹ ⊢ ¬☐_copy P` | Linear ⟹ !Copy. Exactly-once consumption is incompatible with bitwise-duplication. | `FileHandle`: if Linear, cannot be Copy. |
| R3 | `⊢ ☐_pod P ⟹ ⊢ (¬◇_U uninit_bits) ∧ ☐_copy P ∧ ☐_zeroinit P` | Pod ⟹ ZeroInit + NoUninit + Copy. | `Vec<T>`: Pod would falsely imply all-zero is a valid `Vec<T>` — which it is not (ptr is null, len == cap is harmless but not an invariant). So Vec is not Pod. |
| R4 | `⊢ ☐_abi P ⟹ ⊢ ☐_pod P` | StableAbi ⟹ Pod. Layout stability is only useful to callers if the type is also bitwise-copyable. | A non-Pod union is not a valid StableAbi candidate; StableAbi impl is rejected. |
| R5 | `⊢ ☐_{disc<T>} P ⟹ ⊢ ☐_sized P` | Discriminant ⟹ Sized. | `extern type Foo` (unsized) cannot carry a Discriminant. |
| R6 | `⊢ ☐_N P ⟹ ⊢ ☐_K P` | NoSuspendHazard ⟹ SuspendSafe. (stronger ⇒ weaker) | If a MutexGuard were mistakenly typed NoSuspendHazard, R6 would *derive* SuspendSafe for the holding function — so the safety of the gate depends on the correctness of NoSuspendHazard's inference. |
| R7 | `⊢ ☐_copy P ⟹ ⊢ ☐_S P` | CopyImpliesSendable: every primitive Copy type has no interior mutability and is trivially safe to move across tasks; user-defined newtypes marked Copy also auto-derive Sendable via this axiom unless a field blocks it. | `Option<UnsafeCell<i32>>` is Copy (no — not Copy) so R7 does not fire; a newtype `struct Wrap(u32)` is Copy → auto-Sendable via R7. |
| R8 | `⊢ ☐_{nointerior} P ⟹ ⊢ ☐_H P (default)` | NoInteriorMuta enables the *default* Shared derivation. `UnsafeCell<T>` breaks the premise. | If a type contains an `UnsafeCell`, premise fails → Shared not default-derived. |
| R9 | `lang_item(UnsafeCell)  ⟹  ¬☐_H` (lang-item stop) | The type carrying `#[zom::lang::unsafe_cell]` does NOT satisfy Shared. | The canonical negative impl (16.13 example) is a theorem in this logic. |
| R10 | `⊢ ☐_L P ⟹ ⊢ ¬☐_H P` | LinearBlocksShared: a Linear value cannot be Shared — exactly-once-consume semantics is incompatible with arbitrarily-many coexisting shared borrows; any Arc/Shared on a Linear type would leave a dangling shared reference after the Linear value's sole consumption. | `FileHandle` (Linear): if someone mistakenly wrote `unsafe impl Shared for FileHandle`, R10 produces a coherence contradiction → ZOM0752. |

### 16.12.3.1  Marker-Incompatibility Table (checked at S3d Coherence)

    ┌─────────────────────┬──────────────────────────────────────────────┐
    │ Marker A            │ Incompatible marker Bs                       │
    ├─────────────────────┼──────────────────────────────────────────────┤
    │ std::marker::Linear │ Copy, Shared, Pod, Clone* (user impl Clone)  │
    │ std::marker::Copy   │ Linear, Uninhabited                          │
    │ std::marker::Shared │ Linear;  NoInteriorMuta* if auto-derived     │
    │                     │   see cross-marker rule R8;                   │
    │                     │   TaskBound ⇔ ¬Sendable (R1)                 │
    │ std::marker::TaskB. │ Sendable                                     │
    │ std::marker::Pod    │ Uninhabited, Linear                          │
    └─────────────────────┴──────────────────────────────────────────────┘
    A type simultaneously satisfying any row's (A, B) pair is rejected
    with ZOM0752 CoherenceContradiction, with diagnostic spans pointing at
    both declarations and a suggested resolution (either remove one marker
    attachment, or if the user's intent is a "sometimes" policy, introduce
    an enum with per-variant marker derivation instead).

R11 For dyn-head types with a bound conjunction {M1 … Mn}, the FULL R0–R10
    closure is applied to the conjunction set, NOT only to the nominal
    type-head's declared markers. Interface name is required as the first bound after `dyn`; subsequent marker bounds use `+`. Repeating the `dyn` keyword for additional bounds is an error (ZOM0342 DynRepeatedPrefix).
    Syntax for dyn-head existential types is defined in Ch.03 §X Existential Types and Ch.17 (DynType production).
    The first bound after the `dyn` keyword MUST be an interface; marker-only existential is not a valid type form and raises ZOM0450 DynHeadMissingInterface. Subsequent bounds may be markers joined by `+`; repeating the `dyn` keyword inside the same list is ZOM0452 RepeatedDynPrefix.
    Any pair (Mi, Mj) ∈ incompatible-table →
    S2b emits ZOM0763 DynBoundsCoherenceViolation with a span pointing at
    the dyn-head's bound-list.

    Rationale (TaskBound × Sendable example): the nominal type head
    'dyn TaskBound + Sendable' carries only DECLARED bounds; the world-
    indexed ◇_T quantifier (strictly-inside-current-task) is parametric on
    the current world w. dyn-type erasure drops the world parameter, so
    nominal syntactic checking alone is insufficient. G6 is extended
    below with a runtime-type-id bitmap check to catch cases where the
    concrete erased object violates R1 after S2b's static closure.

    The `dyn` existential type is formally defined in Ch.03 §Existential Types. The grammar for `dyn I + M1 + M2` appears in Ch.17 AtomType + DynType production.

G6 runtime double-check (L2): for dyn objects that passed syntactic G6 gate
in S4, the runtime type-id's marker-bitmap is consulted; if the bitmap's
(TaskBound, Sendable) bits are both set at the concrete-object level, a
ZOM8046 error is raised at spawn-accept time (paired with the compile-time
warning ZOM0763).

### 16.12.4 Negation examples

1. **Shared default via R8, blocked by UnsafeCell via R9:** `Mutex<T>` contains `UnsafeCell<T>` → R9 says `UnsafeCell` blocks `☐_H` → R8's premise fails for `Mutex<T>` → `Mutex<T>` defaults to `!Shared`. But the `unsafe impl Shared for Mutex<T>` (16.14) explicitly overrides — this is a positive axiom injected *before* stage S3 closure that dominates the default. Coherence is preserved because the override is explicit (`unsafe`), and S3 rejects cases where both hold *without* the unsafe marker.
2. **Linear and Copy:** a type T that is both Linear and Copy (which would be absurd — how can you both exactly-once-consume and bitwise-duplicate?) is caught by R2 + R7: `☐_L ⟹ ¬☐_copy`, so any `MarkerSet` containing both Linear and Copy is a coherence error `ZOM0710` at S3.
3. **TaskBound and Sendable:** via R1, `◇_T ⟹ ¬☐_S`. If a user writes `#[std::marker::TaskBound]` and also `impl Sendable for T;`, S3 raises `ZOM0710`.

---

## 16.13 Negative-impl semantics and rules

### 16.13.1 Motivation

Positive marker impls and auto-derivation are monotone: once a type satisfies a marker, all its supersets (in the lattice sense) also satisfy it. But the concurrency model relies on *local* exceptions: `UnsafeCell<T>` must **globally** block `Shared` — any type that contains an `UnsafeCell` transitively must not auto-derive Shared. Without negative impls, the Shared-deriver would need a growing compiler built-in list of "unshared types", which is not extensible. The negative impl `impl !Shared for UnsafeCell<T>;` closes that loop with one line in the standard library and zero compiler hard-coding.

### 16.13.2 Syntax

Canonical grammar (reproduced from 16.2 [A-019]):
```
markerImplDeclaration
  : 'unsafe'? 'impl' typeParameters? '!' markerImplPath 'for' type
      whereClause? ( structBody | ';' )
  ;
```
The `!` is INFIX between `impl` and the marker name. This disambiguates:
- `impl Sendable for T` — positive.
- `impl !Sendable for T` — negative.
- `unsafe impl !Shared for T` — user-assumed negative (unsafe is typically *not* needed for negative impls because they exclude rather than assume; the unsafe flag is allowed but unused — reserved for cross-crate exotic cases).

### 16.13.3 Semantics (trait-solver level)

For `impl !M for Tσ;` where σ is a type-substitution:

1. **Assertion.** `∀ substitution δ.  ⊬  (Tσδ : M)`. The negative impl is a counter-axiom injected **after** the positive closure in stage S3 (S3 step 3).
2. **Coherence.** After S3 completes, if any `Tσδ` was marked YES for M by the positive closure AND overlaps with a negative-impl head, emit `ZOM0710 CoherenceViolation` with **both** spans (positive impl source range + negative impl source range).
3. **Auto-deriver stop-rule.** For marker M, a struct S derives `¬M` if any field's type transitively satisfies `¬M` via a negative impl. This is the "downward propagation" that makes the single line `impl !Shared for UnsafeCell<T>;` kill Shared for any aggregate that transitively owns an UnsafeCell — unless a downstream type explicitly overrides with `unsafe impl Shared for Mutex<T>`.
Orphan rule (negative impl version):
    CANONICAL STEP (ZERO): normalise the impl-head T by applying
        type-alias expansion + type-constructor canonicalisation
        (i.e. unwrap 'type Wrap<T> = Foreign<T>' → 'Foreign<T>';
        flatten 'newtype Foo = Bar' into the underlying nominal head
        'Bar'; apply all trait-alias / impl-alias expansions).
        This normalisation is structural, not nominal-only; it MUST
        reach the actual nominal HEAD's crate-id.
    STEP 1: 'impl !M for T' is well-formed ⟺
        (after normalisation)  M is local  OR  T is local.
    "local" = declared in the current crate; sub-crate dependencies
        are treated as FOREIGN.
    Non-canonical local-alias wrapper over a FOREIGN type head +
    FOREIGN marker M → ZOM0702 OrphanNegativeImpl with a diagnostic
    citing (i) the syntactic alias name, (ii) the canonical foreign
    head after unwrapping, and (iii) a fix hint: "Move this impl to
    crate '<foreign>' or wrap in a nominal newtype with #[repr(transparent)]."
5. **Justification check (S1d):** before accepting any negative impl, **one** of must hold:
   - (a) target type has a field whose type is `¬M` (transitive);
   - (b) target type is a `zom::lang::*` lang-item;
   - (c) marker M is declared locally in the same crate.
   
   Missing justification → `ZOM0701 UnjustifiedNegativeImpl` with LSP suggestion.

### 16.13.4 Complete logical inference rules

Let `Γ` be the marker-impl environment, `Σ` be the lattice, `Judgement = Γ; Σ ⊢ T : M` or `Γ; Σ ⊢ T : ¬M`.

```
  (Impl-Pos)  (impl M for Tσ) ∈ Γ
  ──────────────────────────────────
           Γ; Σ ⊢ Tσ : M

  (Impl-Neg)  (impl !M for Tσ) ∈ Γ
  ──────────────────────────────────
           Γ; Σ ⊢ Tσ : ¬M

  (Lattice-Mono)   Γ; Σ ⊢ T : M     M ≤ N ∈ Σ
  ──────────────────────────────────────────────
                    Γ; Σ ⊢ T : N

  (Struct-Field)  Γ; Σ ⊢ ∀ F ∈ fields(S). F_type : M
  ───────────────────────────────────────────────── (if auto-derive enabled for M)
                     Γ; Σ ⊢ S : M

  (Struct-Field-Neg)
      for struct S { f_i: T_i … },
      ∀ i : T_i derives ¬M  ⇒  S derives ¬M

  (Enum-Variant-Neg)
      for enum E { V_1(T_11,…), V_2(T_21,…), … },
      ∃ j , ∃ k : T_jk derives ¬M  ⇒  E derives ¬M

  (Union-Arm-Neg)
      for union U { f_i: T_i … },
      ∀ i : T_i derives ¬M  ⇒  U derives ¬M
      (union is as strict as its strictest field: every arm is simultaneously
       live per C-style union provenance.)

  (Enum-Variant-Pos)       — complementary positive auto-derive rule
      for enum E { V_1(…), … },
      ∀ j : ∀ field type T_jk in V_j : T_jk derives +M
           ⇒  E derives +M

     ── Stage-ordering invariant (§16.13.3 → §16.13.4 hand-off) ───────────
     1. Seed ¬M bit-set  via (Struct-Field-Neg / Enum-Variant-Neg /
        Union-Arm-Neg) transitively, BEFORE any positive blanket closure.
     2. Run positive closure (blanket impls, Enum-Variant-Pos, structural
        auto-derive) on the complement — a positive seed CANNOT overwrite
        an already-set ¬M bit.
     3. Finally apply explicit user 'unsafe impl M for T' overrides (which
        strip ¬M) and explicit 'impl !M for T' negative impls (which set
        ¬M regardless of step-1).
     Reordering any of (1)(2)(3) is a specification BUG; implementations
     MUST match the ordering or risk silent coherence violations (e.g.
     Option<UnsafeCell<_>> appearing Shared via blanket impl before the
     UnsafeCell ¬Shared seed is set).

  (Enum-Unsafe-Override)
      An 'unsafe impl M for Enum' / 'unsafe impl !M for Enum' MUST be
      accompanied by a proof obligation covering EVERY variant: the user
      must demonstrate (via inline '#![zom::lint::allow(ZOM0757)]' or an
      S3-recognized proof form) that EACH variant-arm satisfies the
      lattice requirement of M or its negation. A blanket 'unsafe impl
      Shared for Handle<T>' on 'enum Handle<T> { Locked(MutexGuard<T>),
      Poisoned(String) }' without variant-level reasoning is rejected with
      ZOM0757 EnumOverrideMissingVariantProof and a per-variant checklist.

  (Coherence)   Γ; Σ ⊢ T : M     Γ; Σ ⊢ T : ¬M
  ──────────────────────────────────────────────  ⇒ emit ZOM0710
                       ⊥

  (User-Conj-E)  Γ; Σ ⊢ T : M     M ≡ B1 ∧ … ∧ Bn ∈ Σ_user
  ─────────────────────────────────────────────────────────────
             ∀ i ∈ [1,n].   Γ; Σ ⊢ T : Bi

  (Unsafe-Override)  unsafe impl M for T ∈ Γ     Γ; Σ ⊢ T : ¬M  (from fields)
  ────────────────────────────────────────────────────────────────────────────
          Γ; Σ \ { field-derived ¬M for T } ⊢ T : M      (the explicit impl wins)
```

The (Unsafe-Override) rule is the critical escape hatch that lets `Mutex<T>` override the field-derived `¬Shared` to `Shared` — with the `unsafe` keyword marking that the compiler cannot see the proof (the proof is the lock discipline inside Mutex).

### 16.13.5 Unsafe conditions summary

- Writing `unsafe impl M for T` when a field-derivation would give `¬M` is the primary unsafety vector. The override is allowed *iff* the marker impl body (or documented contract) provides the hand-proof that the type is safe to treat as M despite the negative field.
- Writing `impl !M for T` without justification → error. No unsafety flag needed because negative impls exclude rather than assume.

---

## 16.14 Marker System: User Guide (Practical)

### 16.14.0 Overview

A `marker` declares a zero-method structural property — a Boolean lattice predicate that classifies *what a type IS*, in deliberate contrast to an `interface`, which describes *what a type can DO*. Every marker is ultimately represented by a single bit (or, for 3-valued unsafe markers, a lattice cell) in a per-type 64-bit marker bitmap maintained by the compiler. Three syntactic surfaces are orthogonal and interchangeable: attribute form, standalone impl form, and bound / type-position form. All three surface forms compile to the same internal 64-bit marker-bitmap representation and feed the exact same lattice-based coherence and bound-check machinery.

### 16.14.1 Three Orthogonal Surfaces

| Surface | Syntax | Semantics | When to use |
| --- | --- | --- | --- |
| (A) Attribute form | `#[zom::marker::M]` or `#[crate::marker::M]` written directly on a `struct` / `class` / `enum` / `union` declaration | Attaches marker `M` at the type-declaration site. For `auto marker` the compiler validates field-recursion and derives the positive or negative impl automatically. | ~90 % of user code — the most ergonomic surface for types defined locally. |
| (B) Standalone impl form | `impl [!] M for T;` or `unsafe impl [!] M for T;` with an optional `where` clause | Attaches or detaches `M` anywhere the orphan rule permits (local `T` **or** local `M`). Blanket impls over generics are written here. | External types, cross-crate attachments, blanket impls, unsafe overrides, explicit negative impls. |
| (C) Bound / type-position form | `T: M + !N` in a `<T>` binder or `where` clause; `dyn I + M + !N` in an existential head | Consumer-side predicate: the compiler checks that the bitmap of the concrete type `T` is a superset of the required set. | Generic constraints, FFI gates, `spawn()` / nursery signatures, existential packing. |

All three surfaces compile to the same 64-bit bitmap per type. Attribute form (A) is purely syntactic sugar: the name-resolution stage (S1b per the pipeline in §16.11) rewrites each `#[M]` written on a type declaration into an auto-generated standalone `impl M for T;` at the same declaration site, before any lattice work begins. Surface (B) is therefore the canonical, desugared form; surfaces (A) and (C) are convenience layers.

### 16.14.2 Four Declaration Styles

ZOM provides exactly four declaration shapes for user markers. Every marker declaration falls into exactly one style; mixing modifiers (`auto marker … = … ;` or `unsafe auto marker …`) is prohibited by the grammar.

#### 16.14.2.1 Style 1 — Basic Marker (≈ 90 % of use cases)

```ebnf
BasicMarkerDecl ::= 'marker' Identifier ';'
```

```zom
marker Json;
marker DatabaseSafe;
marker UiThreadOnly;
```

Rules.

- A basic marker carries **zero** built-in recursion rules (contrast with `auto marker`, Style 3). To attach `M` to a type, the user **must** explicitly use surface (A) or surface (B).
- No block body and no methods are permitted. Any attempt to write a block body or a method inside a marker declaration fires `ZOM0517 MarkerCannotHaveMethods`.
- Basic markers are always 2-valued Boolean predicates: a given `T` either has an explicit `impl M for T;` (true) or an explicit `impl !M for T;` (false), or neither is present (unresolved — falls back to 3-valued reasoning in the trait solver and must be resolved by the time a consumer writes a bound).

#### 16.14.2.2 Style 2 — Derived / Conjunctive Marker

```ebnf
DerivedMarkerDecl ::= 'marker' Identifier '=' MarkerConjunction ';'
MarkerConjunction  ::= MarkerPath ('+' MarkerPath)*
MarkerPath         ::= '!'? (Identifier | QualifiedMarkerPath)
```

```zom
// Semantic macro. Equivalent to a compiler-generated blanket impl:
//   impl Value for T where T: Sendable + !Shared + 'static;
marker Value = Sendable + !Shared + 'static;

fun process<T>(x: T) where T: Value { ... }  // equivalent, shorter
```

Rules.

- The right-hand side is a `+`-conjunction. Order is semantically irrelevant; the compiler normalises into a sorted set internally.
- The derived marker is rewritten on-the-fly during name resolution (S1b) into a blanket impl: `impl M for T where T: <conjunction>;`. Diagnostic messages **always** expand the derived-marker name into its full conjunction when reporting failures (e.g. `T does not satisfy Value → T does not satisfy !Shared`). Users never debug opaque derived-marker names.
- Self-reference (`marker A = A + B;`) and any mutual-cycle through derived markers fires `ZOM0519 MarkerCycle` at stage S1b. Derived markers may reference other derived markers on the right-hand side; the compiler builds a DAG and topologically sorts the expansion.

#### 16.14.2.3 Style 3 — Auto Marker (field-recursive)

```ebnf
AutoMarkerDecl ::= 'auto' 'marker' Identifier ';'
```

```zom
// Declares a recursion rule: a struct S is ZeroCopyCompatible IFF every
// direct field of S is ZeroCopyCompatible. Users seed the recursion by
// writing impls on primitive types; primitives have no fields so the
// recursion terminates without further effort on those types.
auto marker ZeroCopyCompatible;

// Seed: attach to primitives (users must write these explicitly)
impl ZeroCopyCompatible for u8;
impl ZeroCopyCompatible for u16;
impl ZeroCopyCompatible for u32;
impl ZeroCopyCompatible for i32;
impl ZeroCopyCompatible for u64;
impl ZeroCopyCompatible for usize;
impl ZeroCopyCompatible for f32;
impl ZeroCopyCompatible for f64;

// Derived automatically by field recursion:
//   auto-impl ZeroCopyCompatible for [u8; 4];
//   auto-impl ZeroCopyCompatible for PackageHeader;  // all fields u32/u64
//   auto-impl ZeroCopyCompatible for (u32, u64);
// structs containing Box<T> or UnsafeCell<T> get !ZeroCopyCompatible automatically
```

Rules.

- Recursion rule is **positive-default**: if every direct field of a struct, tuple, or tagged enum variant satisfies the `auto marker M`, the enclosing aggregate **does** satisfy `M` and the compiler emits a virtual positive impl. If **any** direct field fails, the compiler derives `!M` for the aggregate.
- On `union` types or **untagged enums**, recursion is structurally ambiguous: auto-derivation halts and the type has no seed impl for `M`. The user must write an explicit positive or negative impl; failure to do so when the type is used in an auto-recursing context fires `ZOM0531 AutoMarkerUnionAmbiguous`.
- Primitive types (`u8`..`f64`, `()`, `str` slice, `*const T`, etc.) do **not** auto-imply any user-defined auto-marker; the user **must** seed them explicitly. This prevents accidental propagation of user-defined semantic contracts across primitive boundaries.

#### 16.14.2.4 Style 4 — Unsafe Marker (100 % manual, no auto rules)

```ebnf
UnsafeMarkerDecl ::= 'unsafe' 'marker' Identifier ';'
```

```zom
// Compiles to ZERO automated rules. EVERY impl site MUST be written
// with explicit `unsafe impl ...;` keyword. Compiler trusts the user
// completely — no field-recursion, no blanket-check.
unsafe marker FfiLayoutStable;

unsafe impl FfiLayoutStable for u8;
unsafe impl FfiLayoutStable for u16;
unsafe impl FfiLayoutStable for u32;
unsafe impl FfiLayoutStable for i32;
unsafe impl FfiLayoutStable for u64;
unsafe impl FfiLayoutStable for usize;
unsafe impl FfiLayoutStable for f32;
unsafe impl FfiLayoutStable for f64;
unsafe impl<T: FfiLayoutStable> FfiLayoutStable for *const T;
unsafe impl<T: FfiLayoutStable> FfiLayoutStable for *mut T;
```

Rules.

- No recursion, no derivation, no auto-negation. If no explicit `unsafe impl M for T` exists for a given `T`, `M` is **unknown** for `T` (not assumed true and not assumed false). In marker-lattice terms, `M` lives in a 3-valued domain `{ true, false, unknown }` for user-facing impl purposes; unknown values are only acceptable as long as no consumer writes a bound that queries `M` on that concrete `T`.
- The `unsafe` keyword is **mandatory** on every impl site for unsafe markers. Omitting it fires `ZOM0535 UnsafeMarkerImplRequiresUnsafe`.
- Primary use case: ABI contracts, FFI boundaries, GPU memory layouts, and any case where correctness relies on invariants **outside** the compiler's observable type system.

### 16.14.3 Marker Naming Rules

- Markers share the top-level **type** namespace with classes, interfaces, enums, and aliases. Name clashes with any of those fire `ZOM0502 MarkerNameClash`.
- Recommended convention (enforced as lint `W5101 MarkerNamingConvention` by `zom fmt`): suffix marker names with `-able`, `-Safe`, or `-Compatible` (e.g. `Sendable`, `ZeroCopyCompatible`, `FfiLayoutStable`). Derived / conjunctive markers prefer noun forms (e.g. `Value`).

### 16.14.4 Marker-Bound Privileges

Markers are a separate mechanism from interfaces precisely because marker bounds
support structural rules that behavioral interfaces cannot.

1. **Negation `!M` is allowed.**

```zom
fun single_writer<T>(x: T) where T: Sendable + !Shared { ... }  // OK: structural negation
fun broken<T>(x: T)       where T: !Drawable { ... }           // ZOM0422: behavioral negation meaningless
```

Principle: structure is Boolean (a type *definitely does not* have interior mutability). Behavior is not — "doesn't draw" is unprovable in general and would collapse the interface-subtyping lattice. Only marker bounds carry negation; interface bounds always require a positive witness.

2. **Commutative / Associative Lattice.** `M + N` and `N + M` are literally identical predicates; `(M + N) + P` and `M + (N + P)` are also identical. While interface bounds are order-independent (rule 1 of Chapter 12 — Type Constraints), the underlying impl graph does **not** form a closed lattice (e.g. `Drawable + Hashable` does not automatically produce a third named interface). Marker conjunctions, by contrast, form a proper Boolean lattice: every finite set of markers has a unique conjunction, a unique disjunction, and a unique complement, all representable on the 64-bit bitmap.

### 16.14.5 Dataflow: From Marker Declaration to Bound Check

```mermaid
flowchart LR
    A[Stage S1b Name Resolution\n1. marker M declaration  \n2. derive / unsafe / auto flags stored] --> B[S2 Lattice\nSeed bits:\n   explicit impl M for T\n   explicit impl !M for T]
    B --> C[S2 Lattice\nBlanket closure:\n   auto-recurse fields\n   derived-marker expand\n   unsafe-marker no-ops\n   produce 64-bit bitmap per (T,M)]
    C --> D[S3d Coherence\n3-valued unknown resolution\nmarker-incompat matrix check]
    D --> E[S4 Usage Gates\nwhere T: M + !N\nspawn / FFI / channel gates\ndyn I + M head checks]
    E --> F{bitmap(T) ⊇ required set?}
    F -->|Yes| G[Compilation proceeds]
    F -->|No| H[ZOM0440-0454 / 05xx / 8xxx\nwith full conjunction expansion]
```

The 64-bit bitmap per type is determined once per `(crate, T)` pair at the end of stage S2 and is subsequently treated as an immutable value. Downstream crates consume the bitmap via the crate-metadata side-channel (a per-T u64 word encoded in the crate's metadata index, plus a marker-name → bit-position mapping). No runtime computation is performed for markers retained at Tier 0 or Tier 1; only RUNTIME_REIFIED Tier 2 markers (see §16.6) materialise an actual lookup at runtime, typically for dynamic-subtype queries on `dyn Any + M` existentials.

### 16.14.6 Complete Worked Example: FFI Boundary Gate

The following walk-through covers end-to-end usage of an `unsafe marker` from declaration through seed primitives, struct attachment, and downstream FFI gate enforcement.

```zom
// ===== Library author (crate: ffi-core) =====

// Step 1: Declare the marker
unsafe marker FfiLayoutStable;

// Step 2: Seed primitives
unsafe impl FfiLayoutStable for u8;
unsafe impl FfiLayoutStable for u16;
unsafe impl FfiLayoutStable for u32;
unsafe impl FfiLayoutStable for i32;
unsafe impl FfiLayoutStable for u64;
unsafe impl FfiLayoutStable for usize;
unsafe impl FfiLayoutStable for f32;
unsafe impl FfiLayoutStable for f64;
unsafe impl<T: FfiLayoutStable> FfiLayoutStable for *const T;
unsafe impl<T: FfiLayoutStable> FfiLayoutStable for *mut T;
```

```zom
// Step 3: A struct whose fields are all in the whitelist → no per-field impl needed
#[repr(C)]
struct Win32Handle {
    handle: u64,
    metadata: *const u8,
}
// User must write this ONE line for the struct, since FfiLayoutStable
// is unsafe (Style 4):
unsafe impl FfiLayoutStable for Win32Handle;
```

```zom
// ===== Downstream consumer =====

// Step 4: FFI function signature enforces via bound
extern "C" fun register_handle<T>(p: *const T) where T: FfiLayoutStable;

// Step 5: Legal call — Win32Handle ✓
let h: Win32Handle = Win32Handle { handle: 42, metadata: null() };
register_handle(&h as *const Win32Handle);   // OK

// Step 6: Illegal call — JavaString has JNIEnv (not in whitelist)
struct JavaString { ptr: *mut JNIEnv; len: usize; }
unsafe impl FfiLayoutStable for JavaString;  // ⚠ requires UNSAFE: user must attest

let js = JavaString { ptr: env, len: 12 };
register_handle(&js as *const JavaString);
// If user skipped the unsafe impl line above: ZOM0588 FfiLayoutViolation
// "JavaString does not satisfy FfiLayoutStable because field ptr: *mut JNIEnv
//  has no impl FfiLayoutStable for JNIEnv"
```

The key practical take-away: with a single `unsafe marker` declaration plus roughly one dozen seed lines, a library author obtains a compile-time enforceable ABI contract that downstream consumers cannot bypass without writing their own `unsafe impl` — and every such attestation is trivially grep-able in code review.

### 16.14.7 Five Absolute Rules for User Markers (Avoiding Pitfalls)

1. **R1 — Markers are always zero methods.** If you want behavior, write an `interface`, not a `marker`. Attempting to add methods fires `ZOM0517 MarkerCannotHaveMethods`.
2. **R2 — Non-auto markers never propagate by field recursion.** You must explicitly attach the marker to every type. The `auto marker` modifier is the **only** opt-in for propagation. This is deliberate: Rust's `Send` / `Sync` auto-propagate for historical reasons; ZOM separates the concern so users can reason locally without looking into private fields of upstream types.
3. **R3 — Name uniqueness in the type namespace.** A marker, an interface, a class, an enum, and an alias cannot share the same identifier in the same scope. Violations fire `ZOM0502 MarkerNameClash`.
4. **R4 — The marker-incompatibility matrix applies to user markers, not just built-in ones.** Library crates can register their own incompatible pairs via the crate-level inner attribute `#![zom::marker::incompat(Linear, Shared)]` (syntax stable as of v1.0). Violations fire in the same `ZOM0520`–`ZOM0529` diagnostic range as the built-in pairs.
5. **R5 — Unions and untagged enums require an explicit impl for auto markers.** Field-level structural recursion is ambiguous when multiple variants carry different fields or the same offset is shared across union arms. Missing explicit impls fire `ZOM0531 AutoMarkerUnionAmbiguous`.

### 16.14.8 User Markers vs. Built-in Markers: Full Parity

The ZOM marker system treats user-defined markers as **first-class citizens** alongside the six canonical concurrency markers (`Sendable`, `Shared`, `Linear`, `SuspendSafe`, `NoSuspendHazard`, `TaskBound`) and the nine layout markers (`ZeroCopy`, `Pod`, `StableAbi`, etc.). The matrix below compares the two along eight dimensions; the key take-away is that **only one row** exhibits a material difference.

| Dimension | Built-in (Sendable, Shared, ZeroCopy, StableAbi, …) | User-defined (MyM, Value, FfiLayoutStable, …) |
| --- | --- | --- |
| Declaration syntax | `marker Sendable;` in the implicit prelude, auto-imported everywhere. | `marker MyM;` in any user module; imported normally via `use crate::marker::MyM;`. Identical grammar rule (`BasicMarkerDecl`) with optional `auto` / `unsafe` / derived modifiers on both. |
| Impl syntax (positive / negative / unsafe) | `impl Sendable for T;`, `impl !Shared for T;`, `unsafe impl Shared for Mutex<T>;` — identical forms. | Exactly the same three forms supported. No syntactic difference whatsoever. |
| Appearance in `where` clause | `T: Sendable + !Shared + Linear`. | `T: FfiLayoutStable + MyM + !Value`. Identical bound-syntax and lattice-join logic. |
| Appearance in `dyn` head | `dyn Drawable + Sendable + Shared` (bitmap packed into the vtable prefix word). | `dyn Drawable + FfiLayoutStable + MyM` — exactly the same bitmap packing (§16.6.2). |
| Orphan-rule semantics | Local `impl` of built-in `M` for upstream `T` is prohibited by the orphan rule unless `T` is local (standard). | Identical: impl allowed when **either** the marker `M` or the type `T` is local to the crate. Blanket impls require at least one local per side. |
| Retention tier (default) | Tier 1 `STDLIB_MARKER` (metadata-only, no runtime reflection blob by default). | Tier 2 `USER_MARKER` default; users may upgrade via `#[zom::retention(runtime_reified)]` to obtain reflection metadata. Default tier is the only semantic delta that differs by marker origin. |
| Runtime bitmap bit allocation | Bits 0..31 reserved for built-in markers in the 64-bit per-type bitmap. Crate metadata carries a fixed mapping from marker path → bit index for these. | Bits 32..63 assigned on a per-(crate, marker) basis during metadata loading. Downstream crates re-map via the metadata index so the bitmap remains stable within a single compilation. Bit-space allocation strategy is identical; only the default offset differs. |
| Auto-recursion default behavior | `Sendable` / `Shared` are **pre-seeded** on all primitives and `()`. All other built-in auto markers are pre-seeded per their semantic contract. | User `auto marker M` is **never pre-seeded** on primitives; the user must write explicit seed impls (Rule R2 in §16.14.7). This is the **only** behavioral difference between built-in and user auto markers at the lattice level. |

In summary: user markers and built-in markers share the same EBNF grammar, the same pipeline stages, the same diagnostic codes, the same `dyn` packing, the same orphan rule, and the same 64-bit bitmap representation. The default retention tier and the primitive pre-seed convention are the **only** two material distinctions; neither affects user-facing syntax or correctness reasoning.

### 16.14.9 Worked examples (appendix)

Five worked examples, required by the charter. All code blocks are syntactically valid ZOM per 16.2.

#### 16.14.9.1 Example 1 — Six concurrency markers declared and implemented

```zom
// ── Definitions ──────────────────────────────────────────────
#![zom::feature::enable("marker_macros")]

/// A raw OS file descriptor. MUST be closed exactly once.
#[std::marker::Linear]
#[zom::repr(transparent)]
struct RawFd(pub i32);

/// A socket that can be sent across tasks but not shared by reference.
#[std::marker::Sendable]
#[zom::repr(C)]
struct Socket {
    fd: RawFd,                 // field: Linear
    family: i32,
}

/// A task-local counter; strictly NOT Sendable.
#[std::marker::TaskBound]
struct Counter {
    value: Cell<i32>,          // Cell contains UnsafeCell transitively.
}

/// A lock-protected shared resource. Overrides ¬Shared via unsafe impl.
struct Mutex<T> {
    cell: UnsafeCell<T>,       // ← gives ¬Shared by derivation
    os_mutex: OsMutexHandle,
}

// ── Positive blanket impls ────────────────────────────────────

impl<T> std::marker::Sendable for Mutex<T>
  where T: std::marker::Sendable;

// THE OVERRIDE. This is the canonical unsafe-positive pattern.
unsafe impl<T> std::marker::Shared for Mutex<T>
  where T: std::marker::Sendable
{
    // Empty — marker impl has no items; the safety proof is in the
    // lock discipline exposed by Mutex's public API (lock() returns
    // MutexGuard which in turn blocks Sendable across await).
}

// ── A suspend-safe async function ─────────────────────────────

/// Reads a file without holding any lock across await.
#[std::marker::SuspendSafe]
async fun read_file(path: &str) -> Result<Buffer, IoError> {
    let fh = open(path)?;                      // acquires RawFd
    // No MutexGuard or SpinLockGuard is live at any await point.
    let buf = fh.read_all().await;             // ✓ SuspendSafe
    drop(fh);                                  // consume Linear exactly once
    return buf;
}

// ── Generic function: bounds using 5 of the 6 markers ─────────

/// Runs a user body in a nursery, requiring T to be cross-task safe.
fun nursery_run<T, F>(body: F) -> Result<T, NurseryError>
  where T: std::marker::Sendable + std::marker::SuspendSafe,
        F: FnOnce() -> T + std::marker::Sendable + std::marker::NoSuspendHazard,
{
    let nursery = Nursery::new();
    nursery.spawn(body);
    return nursery.join();
}
```

#### 16.14.9.2 Example 2 — UnsafeCell negative impl (the canonical negative)

```zom
// ── Stdlib definition ────────────────────────────────────────
#[zom::lang::unsafe_cell]
#[zom::repr(transparent)]
pub struct UnsafeCell<T> {
    value: T,
}

// ── Marker impls for UnsafeCell<T> ────────────────────────────

// Positive, conditional: forwards Sendable when T is Sendable.
impl<T> std::marker::Sendable for UnsafeCell<T>
  where T: std::marker::Sendable;

// ═════════════════════════════════════════════════════════════
// THE canonical negative impl. One line. No compiler hard-coding.
// ═════════════════════════════════════════════════════════════
impl !std::marker::Shared for UnsafeCell<T>;

// Linear / SuspendSafe / NoSuspendHazard / TaskBound forward conditionally.
impl<T> std::marker::Linear for UnsafeCell<T>
  where T: std::marker::Linear;

impl<T> std::marker::SuspendSafe for UnsafeCell<T>
  where T: std::marker::SuspendSafe;

impl<T> std::marker::NoSuspendHazard for UnsafeCell<T>
  where T: std::marker::NoSuspendHazard;

impl<T> std::marker::TaskBound for UnsafeCell<T>
  where T: std::marker::TaskBound;

// ── Downstream effect: anything containing an UnsafeCell ──────
//
//    struct S { cell: UnsafeCell<i32> }
//
// Auto-deriver applies (Struct-Field-Neg) from 16.13.4:
//    ⊢ S : ¬Shared.
//
// A user writing `#[std::marker::Shared] struct S { … }` without an
// unsafe override gets ZOM0710 CoherenceViolation.
```

#### 16.14.9.3 Example 3 — `repr(C)` FFI struct with StableAbi

```zom
/// A C-compatible point, safe to pass across shared-library boundaries.
#[zom::repr(C, align(4))]
#[std::marker::StableAbi]
#[zom::ffi::export_name = "zom_point_v1"]
pub struct Point {
    pub x: f32,
    pub y: f32,
}

// The StableAbi marker impl body carries the required structural
// constants — the compiler (S1 Well-formedness) validates that they
// match the derived layout hash and rejects drift.
impl std::marker::StableAbi for Point {
    const LAYOUT_VERSION: u32 = 1;
    const LAYOUT_HASH: [u8; 32] =
        sha256!("Point" || repr(C) || align(4) || field(x:f32) || field(y:f32));
}

/// Returns a heap-allocated Point across the FFI boundary.
#[zom::ffi::c_abi]
#[zom::ffi::no_mangle]
pub extern fun make_point(x: f32, y: f32) -> *mut Point {
    let p = Box::new(Point { x, y });
    return Box::into_raw(p);
}
```

#### 16.14.9.4 Example 4 — Deprecated function

```zom
// Uses LegacyBareWhitelist: emits W7105 DeprecatedBareAttribute.
#[deprecated(since = "1.2.0",
             note  = "Use new_shiny_api() instead; this one leaks file handles under Windows.")]
pub fun old_busted_api(path: &str) -> Handle
  raises IoError
{
    // … implementation …
}

// Preferred, fully-qualified form (no warning):
#[zom::stability::deprecated(
    since = "1.3.0",
    note  = "Use new_shiny_api_v2(); old_busted_api_v1 is a thin wrapper.")]
pub fun old_busted_api_v1(path: &str) -> Handle
  raises IoError
{
    return old_busted_api(path);
}

// ═════════════════════════════════════════════════════════════
// Call-site diagnostic (W7401 DeprecatedItem) with spans:
//
//    let h = old_busted_api("/tmp/x");
//          ^^^^^^^^^^^^^^^^^^^^^^
//    = warning W7401: use of deprecated function `old_busted_api`
//    = note: since ZOM 1.2.0
//    = note: Use new_shiny_api() instead; this one leaks file handles under Windows.
// ═════════════════════════════════════════════════════════════
```

#### 16.14.9.5 Example 5 — `must_use` Result function + lint gating

```zom
/// Compute sum of a slice. The return value carries the final sum and
/// MUST be used; discarding it silently is almost certainly a bug.
#[std::marker::MustUse]
pub fun checked_sum(items: &[i64]) -> Result<i64, OverflowError> {
    mut sum: i64 = 0;
    for (item in items) {
        match (sum.checked_add(*item)) {
            when Some(v) => sum = v;
            when None    => return OverflowError("overflow in checked_sum");
        }
    }
    return Ok(sum);
}

// ── Caller that forgets to match ─────────────────────────────

fun sloppy_caller(data: &[i64]) {
    checked_sum(data);                       // ← W7501 UnusedMustUse
    //   ^^^^^^^^^^^^^^^^
    // = warning W7501: unused return value of function marked `MustUse`
    // = note: `checked_sum` returns `Result<i64, OverflowError>`; silence
    //         by matching on Ok/Err, or explicitly with `_ = checked_sum(…)`.
}

// ── Upgrading to hard error via deny frame ────────────────────

#![zom::lint::deny(ZOM0750)]       // ZOM0750 = the range code for unused-must-use

fun strict_caller(data: &[i64]) {
    checked_sum(data);                       // ← ZOM0750 HardError here
}

// ── Suppressing just one instance with allow frame ────────────

fun benchmark_caller(data: &[i64]) {
    #[zom::lint::allow(ZOM0750)] {
        checked_sum(data);                   // ← OK: locally suppressed
    }
}
```

---

## 16.15 Error-diagnostic specification

Twelve common typo / misuse cases, each with a rustc-style diagnostic. The `^^^^^` underline indicates the precise source-range annotation (a half-open span, start-inclusive, end-exclusive; LSP `Range` compatible). The twelve errors are chosen to cover parser, binder, well-formedness, target-mismatch, and coherence classes.

### 16.15.1 Case 1 — Inner attribute in illegal position (ZOM0601)

```
  fun f() {
      let x = 1;
      #![zom::lint::allow(ZOM0741)]
      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  }

error[ZOM0601]: inner attributes are only permitted at the start of a block or file
  --> src/main.zom:3:5
   |
 3 |     #![zom::lint::allow(ZOM0741)]
   |     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^
   |
   = help: move this `#![…]` to the top of the function body, or convert to an
           outer attribute `#[…]` on the statement that needs the suppression.
```

### 16.15.2 Case 2 — Misplaced `@` parameter sugar (ZOM0602)

```
  @variadic                    (outside a parameter list)
  ^^^^^^^^^

error[ZOM0602]: `@` parameter-sugar is only allowed inside function parameters
  --> src/main.zom:1:1
   |
 1 | @variadic
   | ^^^^^^^^^ expected a parameter declaration here, but `@` was found in
   |           item position
   = help: did you mean `#[zom::param::variadic]` as an item attribute?
```

### 16.15.3 Case 3 — Expression attribute not on the Tier-0 whitelist (ZOM0603)

```
  let r = #[std::marker::Sendable] spawn(f);
          ^^^^^^^^^^^^^^^^^^^^^^^^^

error[ZOM0603]: non-whitelisted attribute attached to an expression
  --> src/main.zom:5:13
   |
 5 |     let r = #[std::marker::Sendable] spawn(f);
   |             ^^^^^^^^^^^^^^^^^^^^^^^^^
   |
   = note: expression attributes are restricted to the Tier-0 whitelist:
           { zom::inline, zom::cold, zom::must_consume, zom::hint::unroll }.
   = help: move the attribute to the surrounding item (e.g. the binding
           or the enclosing function).
```

### 16.15.4 Case 4 — Dangling `#` (ZOM0604)

```
  # my_fancy_marker on struct;
  ^

error[ZOM0604]: stray `#` with no attribute following
  --> src/main.zom:2:1
   |
 2 | # my_fancy_marker on struct;
   | ^ unexpected token after `#`
   |
   = help: to start an attribute write `#[path::to::attribute]`; to start an
           inner attribute write `#![path::to::attribute]`.
```

### 16.15.5 Case 5 — Unresolved attribute path (ZOM0610)

```
  #[zom::hint::inlice]             // typo: inlice → inline
    ^^^^^^^^^^^^^^^^

error[ZOM0610]: attribute `zom::hint::inlice` not declared
  --> src/main.zom:7:5
   |
 7 |     #[zom::hint::inlice]
   |       ^^^^^^^^^^^^^^^^ no attribute with this fully-qualified name
   |
   = help: did you mean `zom::hint::inline`? (Levenshtein distance = 2)
```

### 16.15.6 Case 6 — Attribute args-schema mismatch (ZOM0612)

```
  #[zom::repr(C, pack(1), align("eight"))]
                            ^^^^^^^^^^^^

error[ZOM0612]: invalid argument to `zom::repr`
  --> src/main.zom:1:28
   |
 1 | #[zom::repr(C, pack(1), align("eight"))]
   |                            ^^^^^^^^^^^^ expected `u32` literal, found
   |                                         string literal `"eight"`
   |
   = note: schema for `zom::repr` is:
           Enum { C, Rust, Transparent, Packed }
           OR Struct { layout: Enum{C,Rust,Packed}, align: u32? }
```

### 16.15.7 Case 7 — Wrong attachment target (ZOM0613)

```
  #[zom::ffi::no_mangle]
    ^^^^^^^^^^^^^^^^^^
  alias MyStr = str;

error[ZOM0613]: attribute `zom::ffi::no_mangle` cannot attach to an alias
  --> src/main.zom:3:5
   |
 3 |     #[zom::ffi::no_mangle]
   |       ^^^^^^^^^^^^^^^^^^ ─ attribute declared target = { function, static }
 4 |     alias MyStr = str;
   |     ^^^^^^^^^^^^^^^^^^ ─ attached here (alias declaration)
   |
   = note: dual spans: attribute at L3C5, declaration-kind at L4C5.
```

### 16.15.8 Case 8 — Bare (flat-name) attribute outside LegacyBareWhitelist (ZOM0617)

```
  #[Sendable]
    ^^^^^^^^

error[ZOM0617]: flat (un-namespaced) attribute names are not allowed
  --> src/lib.zom:11:5
   |
11 |     #[Sendable]
   |       ^^^^^^^^ expected a fully-qualified path with ≥ 2 segments,
   |                found a bare identifier
   |
   = note: the ONLY permitted bare attributes are `deprecated`, `inline`,
           and `cold` — and those desugar to `zom::…` with a warning.
   = help: write `#[std::marker::Sendable]` instead, or use the bound form
           `impl Sendable for MyStruct;` if the intention is a marker impl.
```

### 16.15.9 Case 9 — Unjustified negative impl (ZOM0701)

```
  impl !std::marker::Sendable for MyStruct;
       ^^^^^^^^^^^^^^^^^^^^^^^

error[ZOM0701]: unjustified negative impl
  --> src/lib.zom:44:6
   |
44 |     impl !std::marker::Sendable for MyStruct;
   |          ^^^^^^^^^^^^^^^^^^^^^^^
   |
   = note: a negative impl requires at least one of:
           (a) a field whose type satisfies `!Sendable`,
           (b) the target type is a `zom::lang::*` lang-item,
           (c) the marker is declared locally in this crate.
   = help: `MyStruct` has no field with `!Sendable`. Consider adding a
           PhantomData<UnsafeCell<()>> field, or moving the impl into the
           crate that owns `Sendable`.
```

### 16.15.10 Case 10 — Coherence violation: positive + negative for same (M,T) (ZOM0710)

```
  file_a.zom  line 12:  impl std::marker::Shared for Mutex<i32> { }
                         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  file_b.zom  line  4:  impl !std::marker::Shared for Mutex<T>;
                         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

error[ZOM0710]: marker coherence violation
  --> file_a.zom:12:6
   |
12 |     impl std::marker::Shared for Mutex<i32> { }
   |          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ positive impl provided here
   |
   = note: and negative impl provided here:
  --> file_b.zom:4:6
   |
 4 |     impl !std::marker::Shared for Mutex<T>;
   |          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ negative impl, instantiated
   |                                           with T = i32
   |
   = help: if the positive impl is an intentional override (e.g. Mutex
           synchronises internally), mark the positive impl `unsafe impl`
           to invoke the (Unsafe-Override) rule from §16.13.4.
```

### 16.15.11 Case 11 — Orphan negative impl (ZOM0702)

```
  // in a user crate, not in stdlib:
  impl !std::marker::Sendable for std::vec::Vec<T>;
       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

error[ZOM0702]: orphan negative impl
  --> src/lib.zom:1:6
   |
 1 | impl !std::marker::Sendable for std::vec::Vec<T>;
   |      ^^^^^^^^^^^^^^^^^^^^^^ marker `Sendable` is not local to this crate
   |                             ^^^^^^^^^^^^^ and `Vec<T>` is also not local
   |
   = note: the orphan rule requires `impl !M for T` to satisfy at least one
           of: M declared locally, or T declared locally.
   = help: wrap `Vec<T>` in a local newtype and implement on the newtype.
```

### 16.15.12 Case 12 — `unsafe impl Shared` missing for Mutex-like type (ZOM0751)

```
  impl std::marker::Shared for Mutex<T> where T: Sendable { }
       ^^^^^^^^^^^^^^^^^^^^^^

error[ZOM0751]: positive impl of `Shared` requires `unsafe`
  --> src/sync/mutex.zom:88:6
   |
88 |     impl std::marker::Shared for Mutex<T> where T: Sendable { }
   |          ^^^^^^^^^^^^^^^^^^^^^
   |
   = note: the type `Mutex<T>` contains a field `cell: UnsafeCell<T>` whose
           type satisfies `¬Shared` via the lang-item stop rule (§16.13.2 R9).
           Without `unsafe` the compiler cannot verify that the mutex's
           lock discipline actually makes interior sharing safe.
   = help: write `unsafe impl std::marker::Shared for Mutex<T> where T: Sendable { }`
           and ensure your public API exposes `&self` only while holding the
           OS-level lock.
```

### 16.15.13 Case 13 — Malformed attribute argument & partial-attr drop diagnostics
    ┌──────────────┬─────────────────────────────────────────────────────┐
    │ Code         │ Meaning                                              │
    ├──────────────┼─────────────────────────────────────────────────────┤
    │ ZOM0618      │ Malformed attribute argument — offending token was  │
    │              │ skipped; a best-effort parse of the remaining args  │
    │              │ was performed.  (See SyncSet above.)                │
    │ ZOM0619      │ Template literal (backtick) not allowed in an       │
    │              │ attribute argument. Wrap in '{ … }' to force        │
    │              │ token-tree mode.                                    │
    │ ZOM0620      │ Malformed namespace separator (bare ':' where '::'  │
    │              │ was expected; suggest rewrite '::')                 │
    │ ZOM0621      │ Discarded N unparsed attribute tokens — a fallback  │
    │              │ "skip-to-]" occurred; user should manually inspect  │
    │              │ the dropped region.                                 │
    └──────────────┴─────────────────────────────────────────────────────┘

---

## 16.16 LSP/IDE friendliness specification

### 16.16.1 Completion ranking

Completion in `#[ … ]` attribute position uses a four-tier ranking, ordered by decreasing score:

| Rank tier | Score | Populated from | Example |
|---|---|---|---|
| 1 (exact match) | 10 000 | `zom::*` namespace — Tier 0 attributes, exact text match if user typed a prefix | `zom::repr`, `zom::ffi::no_mangle` |
| 2 | 5 000 | `std::marker::*` — Tier 1 markers | `std::marker::Sendable` |
| 3 | 2 000 | `<crate>::attr::*` of each dependency (the conventional surface; see 16.5.2) | `serde_zom::attr::Serialize` |
| 4 (fallback) | 100 | every exported marker declaration in any dependency | `acme_lib::marker::AcmeSafe` |

- Completion **never** inserts a bare identifier. It always offers the fully-qualified form. A separate *code-action* "Shorten attribute" (disabled by default, feature-flag `zom.lsp.shortenBareWhitelistOnly`) can rewrite `#[zom::inline]` → `#[inline]` if the identifier is in LegacyBareWhitelist.
- Completion in `where`-clause bound position or impl-head position ranks bare marker identifiers from the prelude first (score 8 000), because those are legal there.
- After `=`, `(`, and `,` inside attribute arguments, the LSP server surfaces the appropriate ArgsSchema alternatives (enum variants, struct key-names) by reading the Tier-0 and Tier-1 schemas.

### 16.16.2 Hover-info schema

Hovering over any token inside an `AttributePathNode` returns a JSON-compatible record:

```
{
  "kind": "attribute",
  "tier": "Tier0 | Tier1 | Tier2",
  "fq_name": "zom::hint::inline",
  "summary": "Inlining hint. Bare form requests heuristic inlining; `=always` forces inlining; `=never` suppresses.",
  "targets": ["function", "expr"],
  "args_schema": { ... },
  "retention": "COMPILE_TIME",
  "decl_location": { "file": "src/builtin_attrs.zom", "line": 275, "col": 1 },
  "doc_url": "https://zom.dev/spec/16#T0-11",
  "examples": ["#[zom::hint::inline(always)]", "#[inline]  // legacy"]
}
```

Hover latency target: ≤ 1 ms hot path (symbol table lookup only — no trait solving).

### 16.16.3 Goto-definition routing

Resolved symbol kind                Goto-def primary target
──────────────────────────────────  ──────────────────────────────────
std::marker / user 'marker M;'      markerDeclaration AST node
                                    (§16.2 A-017) at the source span;
                                    provide secondary code-action
                                    "Goto first conjunct" for
                                    structural markers 'M = B1 + B2 …'
                                    and a hover note listing the full
                                    conjunction set.

Tier-2 'unsafe impl Macro for X'    → '#[zom::lang::macro_entry]'-
                                    annotated 'fun expand(…)' inside
                                    the impl block; fall back to the
                                    'unsafe impl Macro … {' line.
                                    Compiler fills 'Macro::
                                    DECL_SOURCE_LOC' for each proc-
                                    macro crate so the server has a
                                    guaranteed span anchor.

Tier-0 / Tier-1 built-in attributes The SDK installer MUST emit, on
(zom::hint::inline, zom::repr, …)  first run, a virtual source file
                                    at
  '<sdk-cache>/builtin/attrs.preprocessed'
                                    containing one dummy declaration:
      #[zom::lang::builtin_decl]
      marker inline;
                                    (and analogous for every Tier-0/1
                                    built-in). Every built-in has a
                                    concrete source span; LSP requests
                                    resolve against this file. The
                                    compiler embeds the file content so
                                    it is always available even on
                                    offline machines.

Cross-reference with 16.16.1 ranking: any completion that surfaces a
Tier-0/1 built-in carries the virtual-source span above so that a
subsequent goto-def request (user triggers goto-def without typing the
full path) resolves immediately.

### 16.16.4 Rename propagation

Renaming a marker declaration `marker M …` triggers edits in:

1. All `#[…M…]` attribute paths across the workspace (include cross-crate references if the rename is triggered from a library workspace root).
2. All `impl [ ! ] M for T` / `unsafe impl M for T` marker-impl heads.
3. All `T: M` / `T: !M` bounds in type-parameter and `where`-clause positions.
4. (If a `const`/`static` is renamed *inside* a marker-impl body: standard const rename only — does NOT propagate out.)

Conflict detection: if after renaming, any `M_new` collides with an existing identifier in the same namespace, the rename is aborted with a "would create ambiguity" message listing the collision site.

### 16.16.5 Find-references semantics

Find-references on a marker declaration or Tier-0 attribute definition returns all locations in categories 1, 2, 3 of 16.16.4 plus:

4. `#[zom::lint::allow(ZOM…)]` entries that reference a lint code whose canonical diagnostic is emitted *by* that attribute (links the attribute definition to its suppression sites).
5. Synthetic doc-param nodes (with a label "synthesised from doc-comment @param").

References are grouped by kind (attribute-form / bound-form / impl-form / synthesized) in the LSP response, for collapsible presentation in editors.

### 16.16.6  Incremental reparsing contract (LSP driver normative)
    (see also §16.2.1 for grammar-side state machine)
    • The parser MUST maintain a checkpoint/restore mechanism at every
      '#[' start; on syntax error, restore checkpoint and skip to the
      matching ']' (bracket-depth count), emit a MissingAttribute
      placeholder node for LSP, and NOT propagate errors past the
      containing topLevelDeclaration.
    • The LSP server's semantic-highlight and goto-def caches MUST treat
      Attribute nodes as independent invalidation units. A mutation
      confined to the interior of an Attribute SHALL invalidate ONLY
      the entries for that Attribute and none of its siblings.

---

## 16.17 Attribute RFC governance

### 16.17.1 RFC process for new attributes

All durable changes to the Tier 0 or Tier 1 attribute contract require an
accepted RFC under `docs/rfc/` before implementation. The RFC frontmatter
`required-owners` must include every owning subagent affected by the change, and
`python3 scripts/check-rfc.py` must pass before the proposal can be reviewed.

| Kind of change | RFC requirement | Required owners | Acceptance gate |
|---|---|---|---|
| Add a Tier 0 `zom::*` attribute | Required under `docs/rfc/`. | `rfc`, `spec-audit`, and each compiler owner that consumes the attribute. | Reference-level semantics, diagnostics, retention phase, conformance cases, and rollback cost are specified. |
| Add a Tier 1 `std::marker::*` marker | Required under `docs/rfc/` with a soundness appendix. | `rfc`, `spec-audit`, `binder-checker`, and `concurrency` or `runtime-memory` when applicable. | The marker lattice, coherence rule, negative rule, and cross-crate visibility behavior are specified. |
| Add a Tier 1 non-marker attribute | Required under `docs/rfc/`. | `rfc`, `spec-audit`, and the compiler or runtime owner that consumes the attribute. | Parser, binder, checker, lowering, and tool behavior are either specified or explicitly out of scope. |
| Change an existing attribute argument schema | Required under `docs/rfc/`. | All owners from the original attribute plus `verification`. | The landing change replaces the old schema and updates every parser, checker, diagnostic, and test caller in the same change. |
| Add a new one-segment attribute spelling | Required under `docs/rfc/` as a language syntax change. | `rfc`, `lexer-parser`, `spec-audit`, and `verification`. | The RFC changes the parser, AST, spec, diagnostics, and conformance tests together. No staged compatibility mode is allowed. |
| Change a cross-marker rule R0-R9 | Required under `docs/rfc/` with a revised soundness appendix. | `rfc`, `spec-audit`, `binder-checker`, `concurrency`, and `verification`. | The revised rule is executable as checker tests and cannot leave implementation-defined semantics. |
| Retention-tier upgrade for an existing attribute | Required under `docs/rfc/`. | `rfc`, `spec-audit`, `runtime-memory`, and the affected compiler owner. | Metadata emission, erasure safety, ABI impact, and tests are specified. |

### 16.17.2 Semantic change handling

ZOM is pre-1.0, so attribute semantic changes are direct repository rewrites,
not staged compatibility promises. An accepted RFC changes the current
contract, deletes the replaced behavior, updates every affected caller, and
lands the matching spec, generated files, diagnostics, and tests in the same
implementation series.

### 16.17.3 Attribute syntax policy changes

Any proposal that changes accepted attribute spelling is a language RFC. The RFC
must define the final syntax only; it must not preserve old spellings as
normative examples, migration bridges, or compatibility modes.

---

## 16.18 Implementation cost notes

### 16.18.1 Per-module LOC estimates (central delta only; excludes tests)

Derived from `finalImplementationEstimate` with finer per-module granularity.
Range confidence ±12%. Paths are repo-relative and follow the current ZOM tree:
compiler code belongs under `products/zomlang/compiler/`, runtime code belongs
under `products/zomlang/runtime/`, and tests belong under
`products/zomlang/tests/`. Planned-only components are explicitly labeled and
must use the same product-scoped hierarchy when introduced.

| Module | Subsystem | LOC | Notes |
|---|---|---:|---|
| AST schema | `products/zomlang/compiler/ast/schema.yml` | 90 | Attribute payload definitions for ModifierList, OuterAttribute, InnerAttribute, AttributePath, PositionalAttrArg, NamedAttrArg, AttrTokenTree, AttributeMarkerDecl, MarkerImplDecl, and MarkerBound. |
| AST generated accessors | `products/zomlang/compiler/ast/generated/` | 55 | Schema-generated kind constants, field offsets, and typed accessors. |
| AST tree | `products/zomlang/compiler/ast/{tree.h,tree.cc}` | 95 | `ast::Tree`, `Node`, `NodeId`, `NodeList`, and parser-facing construction APIs. |
| AST dumping | `products/zomlang/utils/zomc/zomc.cc` | 130 | JSON syntax-tree dump backed by `ast::Tree` node records and payload words. |
| AST builder integration | `products/zomlang/compiler/parser/{parser.cc,parser.h}` | 130 | Builder calls for schema node kinds and source ranges. |
| **AST subtotal** | | **500** | **Matches finalAST delta.** |
| Lexer | `products/zomlang/compiler/lexer/lexer.cc` | 75 | ColonColon and shebang handling in the current lexer implementation. |
| Parser | `products/zomlang/compiler/parser/{parser.cc,parser.h}` | 1 350 | `tryParseAttributeStart()` lookahead table, `parseModifierList()`, 8-target attachment algorithm, desugaring of `@Ident(args?)`, `markerDeclaration` / `markerImplDeclaration` productions, `attributeAnnotatedExpression` whitelist gate. |
| Binder | `products/zomlang/compiler/binder/{binder.cc,utilities.cc}` | 900 | S0a AttributePath resolution (3-namespace cascade), S0b negative-bound resolve, S0c DocParamSynthesisPass, S0d MarkerImplDecl candidate registration. |
| Macro engine | `products/zomlang/compiler/macro/` (planned) | 2 000 | Tier-2 isolation sandbox (16 GiB / 60 s caps), TokenStream impl, `Macro` trait dispatch, cycle detection via generation counter, `pre_expansion_attrs` side-table, incr. comp. hooks. |
| Checker WFF + Orphan | `products/zomlang/compiler/checker/` | 1 200 | ArgsSchema validator for 20 Tier-0 + 18 Tier-1 entries, target validation, arity, orphan rule, negative-impl justification, flat-name enforcement. |
| Checker Lattice + Closure | `products/zomlang/compiler/checker/` | 1 400 | R0–R9 graph, user `marker M = ...` conjunction closure, cycle detection, monotone fixpoint with 3-round early-out, negative impl exclusion, coherence check, deterministic sort. |
| Checker Usage gates | `products/zomlang/compiler/checker/` | 2 000 | G1–G6 concurrency gate implementations (CFG traversal, live-slot walk at await points, Linear one-shot consume checker), lint allow/deny frame stack, diagnostic uplifts. |
| **Checker subtotal** | | **4 600** | **Matches finalCheckerStages estimate.** |
| Lowering + Codegen | `products/zomlang/compiler/backend/` (planned) | 550 | MarkerSet (u64 bitset) attachment, `zom::repr -> layout`, `zom::ffi::* -> LLVM module flags`, hint flags into `CallSite`, marker erasure before LLVM, exclusion bitmap into crate metadata (.zom-cmi). |
| LSP support | `products/zomlang/tools/lsp/` (planned) | 260 | 16.16 schema implementations. |
| Documentation extraction | `products/zomlang/tools/docs/` (planned) | 220 | Walk ModifierList for `zom::doc::*`, emit DocParam / Returns / Raises sections; marker-table rendering for type pages. |
| **Production-code total** | | **10 455** | |
| Tests | (per-module tests; see next table) | **6 400** | |
| **Grand total** | | **16 855** | Within 3.4% of `finalImplementationEstimate::totalLOC = 16305`; inside ±12% confidence band. |

### 16.18.2 Test plan breakdown

| Test suite | Approx. LOC | Coverage goal | Passing gate |
|---|---:|---|---|
| `products/zomlang/tests/conformance/corpus/16-attributes/parser-attr-*.zom` | 900 | Every production in EBNF A-001...A-026; every ZOM0601..ZOM0604, ZOM0617 error case from §16.15; formatter round-trip of `@`-sugar. | CI (parse success + AST dump diff + pretty-print round-trip). |
| `products/zomlang/tests/conformance/corpus/16-attributes/binder-attr-*.zom` | 500 | Namespace resolution (zom::, std::marker::, dep::*::*); shadowing; root-ns disambiguation `::`. | CI. |
| `products/zomlang/tests/conformance/corpus/16-attributes/checker-s1-wff-*.zom` | 600 | Every Tier-0 + Tier-1 ArgsSchema + target; error cases in 16.15 cases 5-9. | CI. |
| `products/zomlang/tests/conformance/corpus/16-attributes/checker-s2-lattice-*.zom` | 400 | R0-R9 edge reachability; user-marker cycles (ZOM0615); I2 staging-invariant check (macro expansion + lattice closure equivalence). | CI + randomized nightly (fuzz 10 000 random marker-bound DAGs). |
| `products/zomlang/tests/conformance/corpus/16-attributes/checker-s3-coherence-*.zom` | 400 | ZOM0710..ZOM0712; `unsafe impl Shared for Mutex<T>` override accepted; missing-unsafe -> ZOM0751. | CI. |
| `products/zomlang/tests/conformance/corpus/16-attributes/checker-s4-gates-*.zom` | 1 500 | G1-G6 gates. Each gate has positive and negative tests; each includes the exact diagnostic snippet from §16.15. `sanitize="suspend"` runtime-test companion for G4. | CI + sanitizer nightly (ASAN, TSAN, SUSPEND-SAN). |
| `products/zomlang/tests/conformance/corpus/16-attributes/macro-attr-*.zom` | 900 | `marker M = A + B` structural; proc-macro `Macro` trait contracts; OOM/timeout/cycle (ZOM0680..ZOM0684). Determinism test: 32 parallel runs on the same input must produce identical TokenStream output hashes. | CI. Determinism + sanitizer nightly. |
| `products/zomlang/tests/conformance/corpus/16-attributes/codegen-attr-*.zom` | 400 | `repr(C)` layout assertions; `no_mangle` / `link_name` symbol inspection; marker zero-cost: grep LLVM IR for *absence* of marker-related metadata; RUNTIME_REIFIED `.zom_meta` section content. | CI + cross-target nightly (x86_64-linux, arm64-darwin, riscv64-linux). |
| `products/zomlang/tests/regression/lsp/attr-*.ts` (planned TypeScript LSP client harness) | 400 | Completion ranking; hover schema; goto-def; rename propagation; find-references categorisation. | CI against `zom-lsp` binary. |
| `products/zomlang/tests/conformance/corpus/16-attributes/format-attr-*.zom` | 300 | Idempotent formatter on every sample in §16.14; source-position of `@` preserved. | CI. |
| **Test subtotal** | **6 300** | | |

### 16.18.3 Phased delivery milestones

Six milestones, intended to align with one ZOM release train (~6 weeks per milestone):

| Milestone | Name | Delivers | Entry gate | Exit gate |
|---|---|---|---|---|
| **M1** | AST + Lexer + Parser | Modules AST, Lexer, Parser (1 925 LOC production). Parsers for all 8 attachment targets. `#[…]` / `#![…]` / `@`-sugar / `marker` / `impl [!] M for T` productions all parse and round-trip. | Parser M1 spec signed off. | 900 parser + format tests green; no regressions on existing corpus. |
| **M2** | Binder + LSP skeleton | Binder S0a–d (900 LOC); LSP goto-def + hover for Tier 0/1 (100 LOC). Attribute paths resolve; diagnostics ZOM0610..ZOM0611 plumbed. | M1 exit-gate met. | 500 binder tests green; manual LSP smoke-test suite passes. |
| **M3** | Checker stages S1 + S2 | S1 well-formedness + orphan, S2 lattice (2 600 LOC checker). All Tier-0/1 ArgsSchema + R0–R9 edges validated; ZOM0612..ZOM0615, ZOM0701..ZOM0702 emitted. | M2 exit-gate met. | 1 000 WFF + lattice tests green; nightly random-DAG fuzz 0 failures. |
| **M4** | S3 Coherence + S4 Usage gates | S3 closure, S4 gates G1–G6 (3 400 LOC checker). ZOM0710..ZOM0712, ZOM0741..ZOM0746, ZOM0750..ZOM0752. | M3 exit-gate met + soundness-review of G1–G6 by the concurrency WG. | 1 900 gate tests green; TSAN + SUSPEND-SAN nightly pass on the `examples/concurrency/` suite. |
| **M5** | Tier-2 proc-macro + S5 Lowering + S6 LSP/rustdoc | Macro engine (2 000), lowering/codegen hooks (550), remaining LSP + rustdoc (380). Feature-gate `marker_macros` defaults to `unstable`. | M4 exit-gate met. | 1 700 macro + codegen + LSP + rustdoc tests green. Determinism 32-run hashes identical. |
| **M6** | Stabilisation + docs + governance tooling | RFC validation in CI, spec/docs sync, and final marker macro promotion decision. | M5 exit-gate met + accepted RFC for any remaining contract change. | `python3 scripts/check-rfc.py` passes; full spec chapter 16 is cross-referenced against actual diagnostics; conformance suite (the 12 cases in §16.15 + the 5 examples in §16.14) runs as part of the release-blocking test suite. |

**Total elapsed time estimate:** ~36 weeks of calendar time for one senior compiler engineer + one senior LSP engineer, or ~24 weeks with two compiler engineers plus one LSP engineer. The phasing is deliberately "parser-first, semantics-middle, macros-last" so that editor support can ship in M2 (for syntax highlighting and basic completions) long before the concurrency gates are enforced, giving the community a migration path.
