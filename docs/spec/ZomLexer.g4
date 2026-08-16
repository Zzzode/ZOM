/*
 * ZomLexer.g4 - executable lexer oracle for ZOM.
 *
 * Source of truth:
 *   1. docs/spec/chapters/02-lexical-structure.md
 *   2. docs/spec/chapters/17-grammar-reference.md
 *   3. RFC 0003 while the lexer architecture is under review
 *
 * This grammar is derived from the lexical chapter and the compiler token
 * inventory. The hand-written C++ lexer is the compiler lexer; this file is an
 * oracle for conformance checks and must not define unsupported tokens.
 *
 * Usage:
 *   antlr4 ZomLexer.g4 ZomParser.g4 -visitor
 *   javac -cp $(antlr4 -cp) Zom*.java
 *   echo 'fun id() -> i32 { return 42; }' | grun Zom tokens
 *
 * ANTLR 4.13+ is required for Unicode property escapes.
 */
lexer grammar ZomLexer;

// Longest-match rules come before shorter rules. Keywords precede IDENTIFIER.
channels {
    WHITESPACE,
    COMMENTS
}

options {
    // Keep the default lexer prediction behavior.
}

// Java helpers for literal validation and template substitution state.
@lexer::members {
    /** Brace depth for each active template substitution, including nested templates. */
    private final java.util.ArrayDeque<Integer> templateBraceDepths =
        new java.util.ArrayDeque<Integer>();

    private boolean atTemplateBoundary() {
        return !templateBraceDepths.isEmpty() && templateBraceDepths.peek() == 0;
    }

    private boolean nextInputIsNotDecimalDigit() {
        int next = _input.LA(1);
        return next < '0' || next > '9';
    }

    private void beginTemplateSubstitution() {
        templateBraceDepths.push(0);
    }

    private void endTemplateSubstitution() {
        if (templateBraceDepths.isEmpty()) {
            throw new IllegalStateException("template tail without active substitution");
        }
        templateBraceDepths.pop();
    }

    private void enterTemplateBrace() {
        if (templateBraceDepths.isEmpty()) return;
        templateBraceDepths.push(templateBraceDepths.pop() + 1);
    }

    private void leaveTemplateBrace() {
        if (templateBraceDepths.isEmpty() || templateBraceDepths.peek() == 0) return;
        templateBraceDepths.push(templateBraceDepths.pop() - 1);
    }

    @Override
    public org.antlr.v4.runtime.Token emit() {
        if (_type == LBRACE) enterTemplateBrace();
        if (_type == RBRACE) leaveTemplateBrace();
        return super.emit();
    }

    /**
     * Count Unicode scalar values in a string. Escapes like '\n' count as 1;
     * unescaped multi-byte UTF-16 surrogate pairs count as 1.
     * For CHAR_LITERAL check we only care about the logical count:
     *   - each backslash-X escape counts as 1
     *   - backslash-x HH counts as 1
     *   - backslash-u HHHH / backslash-u{H+} counts as 1
     *   - a raw (not-escaped) pair of surrogate chars counts as 1 (or 2 if unpaired)
     * Fast path: iterate char-by-char, group escapes into single scalars.
     */
    static int countUnicodeScalars(String s) {
        int n = 0;
        for (int i = 0; i < s.length();) {
            char c = s.charAt(i);
            if (c == '\\') {
                i++;
                if (i >= s.length()) break;
                char e = s.charAt(i++);
                if (e == '\r') {
                    if (i < s.length() && s.charAt(i) == '\n') i++;
                    continue;
                }
                if (e == '\n' || e == '\u2028' || e == '\u2029') continue;
                n++;
                if (e == 'x') { i += 2; }
                else if (e == 'u') {
                    if (i < s.length() && s.charAt(i) == '{') {
                        while (i < s.length() && s.charAt(i) != '}') i++;
                        i++; // skip '}'
                    } else {
                        i += 4;
                    }
                } // else single-char escape
            } else if (Character.isHighSurrogate(c) && i + 1 < s.length()
                       && Character.isLowSurrogate(s.charAt(i+1))) {
                n++;
                i += 2;
            } else {
                n++;
                i++;
            }
        }
        return n;
    }

    /** Reject raw line terminators and keep ANTLR source positions exact for every sequence. */
    private void validateAndAccountForLiteralLines(String text, String literalKind) {
        int extraLines = 0;
        int lastTerminatorEnd = -1;
        boolean lastTerminatorNeedsColumnReset = false;

        for (int i = 1; i + 1 < text.length();) {
            char c = text.charAt(i);
            if (c != '\\') {
                if (c == '\r' || c == '\n' || c == '\u2028' || c == '\u2029') {
                    throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                        literalKind + " must not contain an unescaped line terminator");
                }
                i++;
                continue;
            }

            i++;
            if (i + 1 < text.length() && text.charAt(i) == '\r'
                && text.charAt(i + 1) == '\n') {
                i += 2;
                lastTerminatorEnd = i;
                lastTerminatorNeedsColumnReset = false;
                continue;
            }
            if (i < text.length() && text.charAt(i) == '\n') {
                i++;
                lastTerminatorEnd = i;
                lastTerminatorNeedsColumnReset = false;
                continue;
            }
            if (i < text.length()
                && (text.charAt(i) == '\r' || text.charAt(i) == '\u2028'
                    || text.charAt(i) == '\u2029')) {
                i++;
                extraLines++;
                lastTerminatorEnd = i;
                lastTerminatorNeedsColumnReset = true;
                continue;
            }
            i++;
        }

        if (extraLines != 0) setLine(getLine() + extraLines);
        if (lastTerminatorNeedsColumnReset) {
            setCharPositionInLine(text.length() - lastTerminatorEnd);
        }
    }

    /** Validate all backslash-u{...} escape sub-sequences in the last-matched token text. */
    static void validateUnicodeEscapes(String text) {
        int i = 0;
        while ((i = text.indexOf("\\u{", i)) != -1) {
            int end = text.indexOf('}', i);
            if (end == -1) return;
            String hex = text.substring(i + 3, end);
            try {
                int cp = Integer.parseInt(hex, 16);
                if (cp > 0x10FFFF) {
                    throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                        "unicode escape exceeds U+10FFFF: \\" + text.substring(i, end + 1));
                }
            } catch (NumberFormatException e) {
                throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                    "invalid unicode escape: \\" + text.substring(i, end + 1));
            }
            i = end + 1;
        }
    }
}


// ============================================================================
// §3.2 Format-control characters.
// ZWNJ and ZWJ are allowed only inside identifiers. ZWNBSP is whitespace except
// at byte offset zero, where the compiler lexer treats it as a BOM.
// ============================================================================
// These tokens exist so IdentifierPart can refer to them. Outside identifier
// position, the parser rejects them as syntax errors.
ZWNJ   : '\u200C';
ZWJ    : '\u200D';

// ============================================================================
// §3.3  Whitespace + Line Terminators
//
//   Whitespace: U+0009 | U+000B | U+000C | U+0020 | U+00A0 | U+1680
//             | U+2000..U+200A | U+202F | U+205F | U+3000 | ZWNBSP
//   LineTerminator: LF | CR | LS | PS
//   LineTerminatorSeq = LF | CRLF | CR | LS | PS
//
//   Whitespace is emitted on the WHITESPACE channel.
// ============================================================================
WS
    : (
          [\u0009\u000B\u000C\u0020\u00A0\u1680\u202F\u205F\u3000\uFEFF]
        // Unicode general category Zs, U+2000 through U+200A.
        | ' '..' '
        // LineTerminator
        | [\r\n\u2028\u2029]
      )+
      -> channel(WHITESPACE)
    ;

// ============================================================================
// §3.4  Comments
//
//   SingleLineComment  ::= '//' (~ LineTerminator)*
//   MultiLineComment   ::= '/*' (MultiLineCommentChar | MultiLineComment)* '*/'
//
//   MultiLineCommentChar = ~( '*' | '/' ) | '*' ~'/' | '/' ~'*'
//
//   Multi-line comments are not nestable.
//   Non-recursive implementation: non-greedy '/*' .*? '*/'.
// ============================================================================
SINGLE_LINE_COMMENT
    : '//' ~[\r\n\u2028\u2029]*
      -> channel(COMMENTS)
    ;

MULTI_LINE_COMMENT
    : '/*' .*? '*/'
      -> channel(COMMENTS)
    ;

// ============================================================================
// §3.6.2 Internal fragments for digits, hex digits, and escapes.
// ============================================================================
fragment NUM_SEP            : '_';
fragment DECIMAL_DIGIT      : [0-9];
fragment NON_ZERO_DIGIT     : [1-9];
fragment BINARY_DIGIT       : [01];
fragment OCTAL_DIGIT        : [0-7];
fragment HEX_DIGIT          : [0-9a-fA-F];
fragment EXPONENT_INDICATOR : [eE];
fragment SIGN               : '+' | '-';

fragment DECIMAL_DIGITS         : DECIMAL_DIGIT (NUM_SEP? DECIMAL_DIGIT)*;
fragment NON_ZERO_DECIMAL_DIGITS: NON_ZERO_DIGIT (NUM_SEP? DECIMAL_DIGIT)*;
fragment BINARY_DIGITS          : BINARY_DIGIT (NUM_SEP? BINARY_DIGIT)*;
fragment OCTAL_DIGITS           : OCTAL_DIGIT  (NUM_SEP? OCTAL_DIGIT)*;
fragment HEX_DIGITS             : HEX_DIGIT+;
fragment HEX_LITERAL_DIGITS     : HEX_DIGIT (NUM_SEP? HEX_DIGIT)*;

fragment HEX_4_DIGITS       : HEX_DIGIT HEX_DIGIT HEX_DIGIT HEX_DIGIT;
fragment HEX_ESCAPE         : 'x' HEX_DIGIT HEX_DIGIT;
fragment UNICODE_ESCAPE
    : 'u' HEX_4_DIGITS
    | 'u{' HEX_DIGITS '}'
    ;
fragment DECIMAL_INTEGER_LITERAL : '0' | NON_ZERO_DECIMAL_DIGITS;
fragment EXPONENT_PART           : EXPONENT_INDICATOR SIGN? DECIMAL_DIGITS;

// ----------------------------------------------------------------------------
// §3.6.2 Numeric literals. The lexer emits one complete token.
// ----------------------------------------------------------------------------
BIGINT_LITERAL
    : // §3.6.2 BigIntLiteral includes every integer radix.
      DECIMAL_INTEGER_LITERAL 'n'
    | '0' [bB] BINARY_DIGITS 'n'
    | '0' [oO] OCTAL_DIGITS 'n'
    | '0' [xX] HEX_LITERAL_DIGITS 'n'
    ;

DECIMAL_LITERAL
    : // Each alternative has exactly one exponent position.
      DECIMAL_INTEGER_LITERAL '.' DECIMAL_DIGITS? EXPONENT_PART?
    | DECIMAL_INTEGER_LITERAL EXPONENT_PART
    | DECIMAL_INTEGER_LITERAL
    | '.' DECIMAL_DIGITS EXPONENT_PART?
    ;

BINARY_LITERAL
    : '0' [bB] BINARY_DIGITS
    ;

OCTAL_LITERAL
    : '0' [oO] OCTAL_DIGITS
    ;

HEX_LITERAL
    : '0' [xX] HEX_LITERAL_DIGITS
    ;

// ----------------------------------------------------------------------------
// §3.6.3 / §3.6.4 String and character literals.
//
//   §3.6.3 StringLiteral: double-quoted string.
//   §3.6.4  CharacterLiteral   MUST contain exactly one Unicode scalar value
// ----------------------------------------------------------------------------
fragment CHARACTER_ESCAPE
    : '\\'
      (
          '0' { nextInputIsNotDecimalDigit() }?
        | '\''
        | '"'
        | '\\'
        | [bfnrtv]
      )
    ;

fragment STRING_ESCAPE_SEQUENCE
    : CHARACTER_ESCAPE
    | '\\' HEX_ESCAPE
    | '\\' UNICODE_ESCAPE
    ;

fragment LINE_TERMINATOR_SEQUENCE
    : '\r\n'
    | '\n'
    | '\r'
    | '\u2028'
    | '\u2029'
    ;

fragment LINE_CONTINUATION
    : '\\' LINE_TERMINATOR_SEQUENCE
    ;

DOUBLE_STRING_LITERAL
    : '"'
      (
          ~["\\\r\n\u2028\u2029]
        | STRING_ESCAPE_SEQUENCE
        | LINE_CONTINUATION
      )*
      '"'
      {
          validateUnicodeEscapes(getText());
          validateAndAccountForLiteralLines(getText(), "string literal");
      }
    ;

// §3.6.4 Single-quoted character literals.
//
//   A valid single-quoted literal contains exactly one Unicode scalar after
//   escape processing. Empty and multi-scalar forms are lexer errors.
CHAR_LITERAL
    : '\''
      (
          ~['\\\r\n  ]
        | STRING_ESCAPE_SEQUENCE
        | LINE_CONTINUATION
      )*
      '\''
      {
          validateUnicodeEscapes(getText());
          String raw = getText();
          validateAndAccountForLiteralLines(raw, "single-quoted literal");
          String content = raw.substring(1, raw.length() - 1);
          int scalars = countUnicodeScalars(content);
          if (scalars != 1) {
              // A1-REJECT: exactly one Unicode scalar is required.
              throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                  "single-quoted literal must contain exactly one Unicode scalar value, got "
                  + scalars + ": " + raw);
          }
      }
    ;

// ============================================================================
// §3.6.5  Template Literals
//
//   NoSubTemplate  =  ` (TemplateChar | TemplateEscape | '$' ~'{')* `
//   TemplateHead    =  ` ...  ${
//   TemplateMiddle  =  } ...  ${
//   TemplateTail    =  } ...  `
//
//   TemplateChar    = ~ [`\$]
//   TemplateEscape  =  \ SourceCharacter
// ============================================================================
fragment TEMPLATE_ESCAPE
    : LINE_CONTINUATION
    | '\\' ~[\r\n\u2028\u2029]
    ;

fragment TEMPLATE_NON_INTERPOLATION_DOLLAR
    : '$' { _input.LA(1) != '{' }?
    ;

fragment TEMPLATE_ELEMENT
    : ~[`\\$]
    | TEMPLATE_ESCAPE
    | TEMPLATE_NON_INTERPOLATION_DOLLAR
    ;

NO_SUBSTITUTION_TEMPLATE_LITERAL
    : '`' TEMPLATE_ELEMENT* '`'
    ;

TEMPLATE_HEAD
    : '`' TEMPLATE_ELEMENT* '${' { beginTemplateSubstitution(); }
    ;

TEMPLATE_MIDDLE
    : { atTemplateBoundary() }? '}' TEMPLATE_ELEMENT* '${'
    ;

TEMPLATE_TAIL
    : { atTemplateBoundary() }? '}' TEMPLATE_ELEMENT* '`' { endTemplateSubstitution(); }
    ;

// ============================================================================
// §3.7 Operators and punctuators. Longest spellings precede shorter ones.
// ============================================================================
ELLIPSIS       : '...';

LSHIFT_ASSIGN  : '<<=';
RSHIFT_ASSIGN  : '>>=';
URSHIFT_ASSIGN : '>>>=';
AND_ASSIGN     : '&&=';
OR_ASSIGN      : '||=';
NULL_COALESCE_ASSIGN : '??=';
POW_ASSIGN     : '**=';
MUL_ASSIGN     : '*=';
DIV_ASSIGN     : '/=';
MOD_ASSIGN     : '%=';
PLUS_ASSIGN    : '+=';
MINUS_ASSIGN   : '-=';
BIT_AND_ASSIGN : '&=';
BIT_OR_ASSIGN  : '|=';
BIT_XOR_ASSIGN : '^=';

URSHIFT        : '>>>';
LSHIFT         : '<<';
RSHIFT         : '>>';
STRICT_EQ      : '===';
STRICT_NEQ     : '!==';
EQ             : '==';
NEQ            : '!=';
LTE            : '<=';
GTE            : '>=';
PLUSPLUS       : '++';
MINUSMINUS     : '--';
POW            : '**';
AND            : '&&';
OR             : '||';
NULL_COALESCE  : '??';
ERROR_DEFAULT  : '?:';
OPTIONAL_CHAIN : '?.';
ERROR_PROPAGATE: '?!';
FORCE_UNWRAP   : '!!';
BIT_AND        : '&';
BIT_OR         : '|';
BIT_XOR        : '^';
NOT            : '!';
BIT_NOT        : '~';
PLUS           : '+';
MINUS          : '-';
MUL            : '*';
DIV            : '/';
MOD            : '%';
LT             : '<';
GT             : '>';
ASSIGN         : '=';
QUESTION       : '?';
COLONCOLON     : '::';
COLON          : ':';
SEMICOLON      : ';';
COMMA          : ',';
PERIOD         : '.';
HASH           : '#';
LPAREN         : '(';
RPAREN         : ')';
LBRACK         : '[';
RBRACK         : ']';
LBRACE         : '{';
RBRACE         : '}';
ROCKET         : '=>';
ARROW          : '->';
AT             : '@';

// ============================================================================
// §6.1 Implemented keywords, grouped by language area.
//
// Keywords precede IDENTIFIER so they win over identifier matching.
// ============================================================================
// -- Declaration -------------------------------------------------------------
CLASS    : 'class';
STRUCT   : 'struct';
INTERFACE: 'interface';
ENUM     : 'enum';
ERROR    : 'error';
FUN      : 'fun';
MUT      : 'mut';
LET      : 'let';
CONST    : 'const';
CONSTRUCTOR : 'constructor';
ALIAS    : 'alias';
INIT     : 'init';
DEINIT   : 'deinit';
GET      : 'get';
SET      : 'set';
ACCESSOR : 'accessor';
DECLARE  : 'declare';

// -- Control Flow ------------------------------------------------------------
IF       : 'if';
ELSE     : 'else';
MATCH    : 'match';
WHEN     : 'when';
DEFAULT  : 'default';
CASE     : 'case';
FOR      : 'for';
WHILE    : 'while';
DO       : 'do';
BREAK    : 'break';
CONTINUE : 'continue';
RETURN   : 'return';
IN       : 'in';
OUT      : 'out';

// -- Type --------------------------------------------------------------------
I8   : 'i8';
I16  : 'i16';
I32  : 'i32';
I64  : 'i64';
U8   : 'u8';
U16  : 'u16';
U32  : 'u32';
U64  : 'u64';
F32  : 'f32';
F64  : 'f64';
BOOL : 'bool';
STR  : 'str';
CHAR : 'char';   // Host type for character literals.
NULL : 'null';
UNIT : 'unit';
NEVER: 'never';
ANY  : 'any';
OBJECT: 'object';
SYMBOL: 'symbol';
BIGINT: 'bigint';
UNDEFINED: 'undefined';

// -- Modifier ----------------------------------------------------------------
PUBLIC    : 'public';
PRIVATE   : 'private';
PROTECTED : 'protected';
STATIC    : 'static';
READONLY  : 'readonly';
MUTATING  : 'mutating';
OVERRIDE  : 'override';
ABSTRACT  : 'abstract';
GLOBAL    : 'global';
IMMEDIATE : 'immediate';
UNIQUE    : 'unique';

// -- Operator / operation ----------------------------------------------------
AS       : 'as';
IS       : 'is';
TYPEOF   : 'typeof';
KEYOF    : 'keyof';
INFER    : 'infer';
SATISFIES: 'satisfies';
ASSERTS  : 'asserts';
ASSERT   : 'assert';
NEW      : 'new';
THIS     : 'this';
SUPER    : 'super';
RAISES   : 'raises';

// -- Module ------------------------------------------------------------------
MODULE : 'module';
IMPORT : 'import';
EXPORT : 'export';
FROM   : 'from';
USING  : 'using';
REQUIRE: 'require';
// AS is declared in the operator keyword group.

// -- Concurrency -------------------------------------------------------------
SUSPEND : 'suspend';
SPAWN   : 'spawn';

// -- Marker Type Names --------------------------------------------------------
// Marker names remain ordinary identifiers. The parser and semantic phases
// recognize marker positions without reserving capitalized user identifiers.

// ============================================================================
// §6.2 Reserved keywords rejected by targeted diagnostics.
//
// Dedicated tokens let the parser report precise ZOM500x diagnostics.
// ============================================================================
THROW      : 'throw';
TRY        : 'try';
CATCH      : 'catch';
FINALLY    : 'finally';
ASYNC      : 'async';
AWAIT      : 'await';
VAR        : 'var';
ACTOR      : 'actor';
CHANNEL    : 'channel';
YIELD      : 'yield';
GENERATOR  : 'generator';
NAMESPACE  : 'namespace';
PACKAGE    : 'package';
TYPE       : 'type';
DELETE     : 'delete';
INSTANCEOF : 'instanceof';
OF         : 'of';
WITH       : 'with';

// ============================================================================
// Literal-like hard keywords.
// TRUE and FALSE are boolean literals. UNDERSCORE is the wildcard token.
TRUE       : 'true';
FALSE      : 'false';
UNDERSCORE : '_';

// ============================================================================
// §3.5 + §3.8 Identifier. This rule comes after keywords.
//
//   IdentifierName   ::= IdentifierStart IdentifierPart*
//   IdentifierStart  ::= \p{ID_Start} | '$' | '_' | '\' UnicodeEscapeSeq
//   IdentifierPart   ::= \p{ID_Continue} | '$' | ZWNJ | ZWJ | '\' UnicodeEscapeSeq
//   Identifier       ::= IdentifierName
//
//   Reserved-word rejection is handled by parser predicates where needed.
// ============================================================================
IDENTIFIER
    : (
          // IdentifierStart
          [\p{ID_Start}$_]
        | '\\' ( HEX_ESCAPE | UNICODE_ESCAPE )
      )
      (
          // IdentifierPart
          [\p{ID_Continue}$]
        | '\u200C' | '\u200D'     // ZWNJ / ZWJ
        | '\\' ( HEX_ESCAPE | UNICODE_ESCAPE )
      )*
      {
          validateUnicodeEscapes(getText());
          // A2-REJECT: reject identifiers that are numeric literals with a
          // leading separator, such as _123, _0, _9n, and _1_000. Pure
          // underscore identifiers and normal identifiers like _foo remain valid.
          String t = getText();
          if (t.length() >= 2 && t.charAt(0) == '_') {
              boolean seenDigit = false;
              boolean validNumeric = true;
              for (int i = 1; i < t.length(); i++) {
                  char c = t.charAt(i);
                  if (c >= '0' && c <= '9') {
                      seenDigit = true;
                  } else if (c == '_') {
                      // ok, numeric separator placeholder
                  } else if (c == 'n' && i == t.length() - 1) {
                      // ok, bigint suffix
                  } else {
                      validNumeric = false;
                      break;
                  }
              }
              if (seenDigit && validNumeric) {
                  throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                      "identifier must not start with '_' followed by digits (numeric literal with leading separator): '" + t + "'");
              }
          }
      }
    ;

// ============================================================================
// §6.3 Contextual / soft keywords. These remain IDENTIFIER tokens and are
// recognized by parser predicates in the relevant grammar positions.
//
//   use           CaptureClause    `use [...]`
//   detached      SpawnModifier
//   blocking      SpawnModifier
//   priority      SpawnModifier   `priority( high | low )`
//   high / low    SpawnModifier   priority arg
//   until         SuspendEventSelector  `suspend until <expr>`
//
// Keeping them as identifiers avoids reserving user namespaces unnecessarily.
// ============================================================================
