lexer grammar lexical;

// Unicode Format Control Characters: These are the characters that are used to represent Unicode
// format control characters.
ZWNJ: '\u200C'; // ZERO WIDTH NON-JOINER
ZWJ: '\u200D'; // ZERO WIDTH JOINER
ZWNBSP: '\uFEFF'; // ZERO WIDTH NO-BREAK SPACE

// Whitespace: These are the characters that are ignored by the parser. They are used to separate
// tokens in a program.
TAB: '\u0009'; // CHARACTER TABULATION
VT: '\u000B'; // LINE TABULATION
FF: '\u000C'; // FORM FEED

// Line Terminator: These are the characters that terminate a line of code. They are used to
// separate statements in a program.
LF: '\u000A'; // LINE FEED
CR: '\u000D'; // CARRIAGE RETURN
LS: '\u2028'; // LINE SEPARATOR
PS: '\u2029'; // PARAGRAPH SEPARATOR

DOLLAR: '$';
UNDERSCORE: '_';
BACKSLASH: '\\';
ANSI_LETTER: [a-zA-Z];

// Unicode Identifier Characters Corresponds to any Unicode code point with the Unicode property
// “ID_Start”
UNICODE_ID_START: [\p{L}\p{Nl}]; // L: Letter, Nl: Letter_Number

// Corresponds to any Unicode code point with the Unicode property “ID_Continue”
UNICODE_ID_CONTINUE: [\p{L}\p{Nl}\p{Mn}\p{Mc}\p{Nd}\p{Pc}];
// Mn: Nonspacing_Mark, Mc: Spacing_Mark, Nd: Decimal_Number, Pc: Connector_Punctuation

// Punctuators
OPTIONAL_CHAINING: '?.';
// Note: [lookahead ∉ DecimalDigit] needs semantic predicate in parser or combined lexer/parser rule.

LPAREN: '(';
RPAREN: ')';
LBRACK: '[';
RBRACK: ']';
PERIOD: '.';
ELLIPSIS: '...';
SEMICOLON: ';';
COMMA: ',';
LT: '<';
GT: '>';
LTE: '<=';
GTE: '>=';
EQ: '==';
NEQ: '!=';
STRICT_EQ: '===';
STRICT_NEQ: '!==';
PLUS: '+';
MINUS: '-';
MUL: '*';
MOD: '%';
POW: '**';
INC: '++';
DEC: '--';
LSHIFT: '<<';
RSHIFT: '>>';
URSHIFT: '>>>';
BIT_AND: '&';
BIT_OR: '|';
BIT_XOR: '^';
NOT: '!';
BIT_NOT: '~';
AND: '&&';
OR: '||';
NULL_COALESCE: '??';
QUESTION: '?';
COLON: ':';
ASSIGN: '=';
PLUS_ASSIGN: '+=';
MINUS_ASSIGN: '-=';
MUL_ASSIGN: '*=';
MOD_ASSIGN: '%=';
POW_ASSIGN: '**=';
LSHIFT_ASSIGN: '<<=';
RSHIFT_ASSIGN: '>>=';
URSHIFT_ASSIGN: '>>>=';
BIT_AND_ASSIGN: '&=';
BIT_OR_ASSIGN: '|=';
BIT_XOR_ASSIGN: '^=';
AND_ASSIGN: '&&=';
OR_ASSIGN: '||=';
NULL_COALESCE_ASSIGN: '??=';
ARROW: '=>';

DIV: '/';
DIV_ASSIGN: '/=';

RBRACE: '}';

// Escape Sequence Definitions from EBNF

// Helper Fragments
fragment HEX_DIGIT: [0-9a-fA-F];

// SingleEscapeCharacter :: one of ' " \ b f n r t v
fragment SINGLE_ESCAPE_CHAR_FRAG: ['"\\bfnrtv]; // Escaped for ANTLR string literals

// Hex4Digits :: HexDigit HexDigit HexDigit HexDigit
fragment HEX_4_DIGITS_FRAG: HEX_DIGIT HEX_DIGIT HEX_DIGIT HEX_DIGIT;

// Actual Escape Sequence Tokens
// HexEscapeSequence :: x HexDigit HexDigit
HEX_ESCAPE_SEQUENCE: 'x' HEX_DIGIT HEX_DIGIT;

// UnicodeEscapeSequence :: u Hex4Digits | u{ CodePoint }
// Note: CodePoint in u{CodePoint} can be 1 to 6 hex digits.
// For simplicity, using HEX_DIGIT+ here. A more precise rule might be needed for CodePoint.
UNICODE_ESCAPE_SEQUENCE: 'u' HEX_4_DIGITS_FRAG | 'u{' HEX_DIGIT+ '}';

// CharacterEscapeSequence :: SingleEscapeCharacter | NonEscapeCharacter
// For lexical purposes, SingleEscapeCharacter is represented by SINGLE_ESCAPE_CHAR_FRAG.
// NonEscapeCharacter ('SourceCharacter but not one of EscapeCharacter or LineTerminator')
// is context-dependent and not easily a standalone token. It's usually handled
// as "any other character" within a string literal rule, for example.

// The EBNF 'EscapeSequence' rule combines these elements:
// EscapeSequence ::
//   CharacterEscapeSequence
//   0 [lookahead ∉ DecimalDigit]
//   HexEscapeSequence
//   UnicodeEscapeSequence
// This overall structure is typically part of a larger token like a String Literal.
// For example, a StringLiteral rule would be like:
// STRING_LITERAL: '"' ( STRING_CHAR | EMBEDDED_ESCAPE )* '"';
// where EMBEDDED_ESCAPE would use HEX_ESCAPE, UNICODE_ESCAPE, SINGLE_ESCAPE_CHAR_FRAG, etc.
// The '0' [lookahead ∉ DecimalDigit] (interpreted as octal escape \0 not followed by digit)
// would also be part of such an EMBEDDED_ESCAPE rule, possibly with a semantic predicate.

// The EBNF 'EscapeCharacter' (SingleEscapeCharacter | DecimalDigit | x | u) is a helper
// concept for defining NonEscapeCharacter and isn't typically a standalone token itself.
