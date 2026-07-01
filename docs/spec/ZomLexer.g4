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

// Java helper for CHAR_LITERAL semantic predicate.
@lexer::members {
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
                // Any escape: backslash-x HH, backslash-u HHHH, backslash-u{H+}, newline, tab, etc.
                n++;
                i++;
                if (i >= s.length()) break;
                char e = s.charAt(i++);
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
    : '//' ~['\r\n\u2028\u2029]*
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

// ----------------------------------------------------------------------------
// §3.6.2 Numeric literals. The lexer emits one complete token.
// ----------------------------------------------------------------------------
BIGINT_LITERAL
    : // §3.6.2 BigIntLiteral ::= DecimalDigits 'n'
      // Separators cannot be first or last.
      ( '0' | NON_ZERO_DECIMAL_DIGITS ) 'n'
    ;

DECIMAL_LITERAL
    : // §3.6.2 DecimalLiteral, with three mutually exclusive forms.
      // A: . DecimalDigits ExponentPart?
      // B: DecimalIntegerLiteral '.' DecimalDigits? ExponentPart?
      // C: DecimalIntegerLiteral ExponentPart?
      //
      // DecimalIntegerLiteral = '0' | NON_ZERO_DECIMAL_DIGITS
      // ANTLR longest-match behavior handles the mutually exclusive order.
      (   '0'
            ( '.' DECIMAL_DIGITS? (EXPONENT_INDICATOR SIGN? DECIMAL_DIGITS)? )?
            ( EXPONENT_INDICATOR SIGN? DECIMAL_DIGITS )?
        | NON_ZERO_DECIMAL_DIGITS
            ( '.' DECIMAL_DIGITS? (EXPONENT_INDICATOR SIGN? DECIMAL_DIGITS)? )?
            ( EXPONENT_INDICATOR SIGN? DECIMAL_DIGITS )?
        | '.' DECIMAL_DIGITS (EXPONENT_INDICATOR SIGN? DECIMAL_DIGITS)?
      )
      {
        // §3.6.2 NUM_SEP MUST NOT appear first or last
        // Structural guard plus explicit defensive check.
        String t = getText();
        if (t.startsWith("_")) {
            throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                "numeric literal must not start with separator: " + t);
        }
      }
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
    : '\\' (["\\bfnrtv0] | '\'')
    ;

fragment STRING_ESCAPE_SEQ
    : CHARACTER_ESCAPE
    | '\\' HEX_ESCAPE
    | '\\' UNICODE_ESCAPE
    | '\\' '\n'  | '\\' '\r'     // LineContinuation
    | '\\' '\u2028'  | '\\' '\u2029'
    ;

DOUBLE_STRING_LITERAL
    : '"'
      (
          ~["\\\r\n\u2028\u2029]
        | STRING_ESCAPE_SEQ
      )*
      '"'
      {
          validateUnicodeEscapes(getText());
          // A3-REJECT: string literals cannot contain unescaped line terminators.
          String t = getText();
          for (int i = 0; i < t.length(); i++) {
              char c = t.charAt(i);
              if (c == '\r' || c == '\n' || c == '\u2028' || c == '\u2029') {
                  throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                      "string literal must not contain unescaped line terminator");
              }
          }
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
        | STRING_ESCAPE_SEQ
      )*
      '\''
      {
          validateUnicodeEscapes(getText());
          String raw = getText();
          // A3-REJECT: line terminators must be escaped.
          for (int i = 0; i < raw.length(); i++) {
              char c = raw.charAt(i);
              if (c == '\r' || c == '\n' || c == '\u2028' || c == '\u2029') {
                  throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                      "single-quoted literal must not contain unescaped line terminator");
              }
          }
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
TEMPLATE_ESCAPE : '\\' . ;

NO_SUBSTITUTION_TEMPLATE_LITERAL
    : '`' ( ~[`\\$] | '\\' . | '$' ~[{] )* '`'
    ;

TEMPLATE_HEAD
    : '`' ( ~[`\\$] | '\\' . | '$' ~[{] )* '${'
    ;

TEMPLATE_MIDDLE
    : '}' ( ~[`\\$] | '\\' . | '$' ~[{] )* '${'
    ;

TEMPLATE_TAIL
    : '}' ( ~[`\\] | '\\' . )* '`'
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
ALIAS    : 'alias';
INIT     : 'init';
DEINIT   : 'deinit';
GET      : 'get';
SET      : 'set';

// -- Control Flow ------------------------------------------------------------
IF       : 'if';
ELSE     : 'else';
MATCH    : 'match';
WHEN     : 'when';
DEFAULT  : 'default';
FOR      : 'for';
WHILE    : 'while';
DO       : 'do';
BREAK    : 'break';
CONTINUE : 'continue';
RETURN   : 'return';
DEBUGGER : 'debugger';
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

// -- Modifier ----------------------------------------------------------------
PUBLIC    : 'public';
PRIVATE   : 'private';
PROTECTED : 'protected';
STATIC    : 'static';
READONLY  : 'readonly';
MUTATING  : 'mutating';
OVERRIDE  : 'override';
ABSTRACT  : 'abstract';

// -- Operator / operation ----------------------------------------------------
AS       : 'as';
IS       : 'is';
TYPEOF   : 'typeof';
NEW      : 'new';
THIS     : 'this';
SUPER    : 'super';
EXTENDS  : 'extends';
RAISES   : 'raises';

// -- Module ------------------------------------------------------------------
MODULE : 'module';
IMPORT : 'import';
EXPORT : 'export';
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
THROW      : 'throw';       // ZOM5001
TRY        : 'try';         // ZOM5001
CATCH      : 'catch';       // ZOM5001
FINALLY    : 'finally';     // ZOM5001
ASYNC      : 'async';       // ZOM5002
AWAIT      : 'await';       // ZOM5002
VAR        : 'var';         // ZOM5003
ACTOR      : 'actor';       // ZOM5004
CHANNEL    : 'channel';     // ZOM5004
YIELD      : 'yield';       // ZOM5005
GENERATOR  : 'generator';   // ZOM5005
NAMESPACE  : 'namespace';   // ZOM5006
PACKAGE    : 'package';     // ZOM5006
TYPE       : 'type';        // ZOM5007, except where object/interface syntax admits it.
DELETE     : 'delete';      // ZOM5008
INSTANCEOF : 'instanceof';  // ZOM5008
OF         : 'of';          // ZOM5008
WITH       : 'with';        // ZOM5008

// ============================================================================
// Literal-like hard keywords.
// TRUE and FALSE are boolean literals. UNDERSCORE is the wildcard token.
//   NOTE: 'implements' is NOT a keyword (per Ch.06 / 17-gr truth). Interface
//   implementations use standalone 'impl Interface for Type { }' form.
//   'implements' remains a plain IDENTIFIER so users get ordinary parser
//   'mismatched input' diagnostics instead of cryptic tokenizer errors.
//   MUT is declared in Declaration keywords section (Ch.06).
TRUE       : 'true';
FALSE      : 'false';
UNDERSCORE : '_';
IMPLEMENTS : 'implements';

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
