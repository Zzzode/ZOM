lexer grammar ZomLexer;

// ================================================================================ Unicode Format-Control Characters
ZWNJ: '\u200C'; // ZERO WIDTH NON-JOINER
ZWJ: '\u200D'; // ZERO WIDTH JOINER
ZWNBSP: '\uFEFF'; // ZERO WIDTH NO-BREAK SPACE

// ================================================================================ Whitespace
TAB: '\u0009'; // CHARACTER TABULATION
VT: '\u000B'; // LINE TABULATION
FF: '\u000C'; // FORM FEED

// ================================================================================ Line Terminators
LF: '\u000A'; // LINE FEED
CR: '\u000D'; // CARRIAGE RETURN
LS: '\u2028'; // LINE SEPARATOR
PS: '\u2029'; // PARAGRAPH SEPARATOR

// ================================================================================ Comments
MULTI_LINE_COMMENT: '/*' .*? '*/' -> channel(HIDDEN);
SINGLE_LINE_COMMENT: '//' ~[\r\n]* -> channel(HIDDEN);

// ================================================================================ Tokens

// ================================================================================ Identifiers
DOLLAR: '$';
UNDERSCORE: '_';
BACKSLASH: '\\';
ANSI_LETTER: [a-zA-Z];
UNICODE_ID_START: [\p{ID_Start}];
UNICODE_ID_CONTINUE: [\p{ID_Continue}];
SINGLE_ESCAPE_CHAR_FRAG: ['"\\bfnrtv];
HEX_4_DIGITS_FRAG: HEX_DIGIT HEX_DIGIT HEX_DIGIT HEX_DIGIT;
HEX_ESCAPE_SEQUENCE: 'x' HEX_DIGIT HEX_DIGIT;
UNICODE_ESCAPE_SEQUENCE:
	'u' HEX_4_DIGITS_FRAG
	| 'u{' HEX_DIGIT+ '}' {
		String text = getText();
		String hexValue = text.substring(2, text.length() - 1);
		try {
			int codePoint = Integer.parseInt(hexValue, 16);
			if (codePoint > 0x10FFFF) {
				throw new LexerNoViableAltException(this);
			}
		} catch (NumberFormatException e) {
			throw new LexerNoViableAltException(this);
		}
	};

// ================================================================================ Keywords
ABSTRACT: 'abstract';
ACCESSOR: 'accessor';
ANY: 'any';
ASSERTS: 'asserts';
ASSERT: 'assert';
ASYNC: 'async';
AWAIT: 'await';
BIGINT: 'bigint';
BOOLEAN: 'boolean';
BREAK: 'break';
CASE: 'case';
CATCH: 'catch';
CLASS: 'class';
CONTINUE: 'continue';
CONSTRUCTOR: 'constructor';
DEBUGGER: 'debugger';
DECLARE: 'declare';
DEFAULT: 'default';
DELETE: 'delete';
DO: 'do';
EXTENDS: 'extends';
FINALLY: 'finally';
FROM: 'from';
FUN: 'fun';
GET: 'get';
GLOBAL: 'global';
IMMEDIATE: 'immediate';
IMPLEMENTS: 'implements';
IN: 'in';
INFER: 'infer';
INSTANCEOF: 'instanceof';
INTERFACE: 'interface';
INTRINSIC: 'intrinsic';
IS: 'is';
KEYOF: 'keyof';
MATCH: 'match';
MODULE: 'module';
MUTABLE: 'mutable';
NAMESPACE: 'namespace';
NEVER: 'never';
NEW: 'new';
NUMBER: 'number';
OBJECT: 'object';
OF: 'of';
OPTIONAL: 'optional';
OUT: 'out';
OVERRIDE: 'override';
PACKAGE: 'package';
PRIVATE: 'private';
PROTECTED: 'protected';
PUBLIC: 'public';
READONLY: 'readonly';
REQUIRE: 'require';
SATISFIES: 'satisfies';
SET: 'set';
STATIC: 'static';
SUPER: 'super';
SWITCH: 'switch';
SYMBOL: 'symbol';
THIS: 'this';
THROW: 'throw';
TRY: 'try';
TYPEOF: 'typeof';
UNDEFINED: 'undefined';
UNIQUE: 'unique';
USING: 'using';
VAR: 'var';
VOID: 'void';
WITH: 'with';
YIELD: 'yield';

// ================================================================================ Punctuators
OPTIONAL_CHAINING: '?.';
LPAREN: '(';
RPAREN: ')';
LBRACK: '[';
RBRACK: ']';
LBRACE: '{';
RBRACE: '}';
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

// ================================================================================ LITERALS
NULL: 'null';
TRUE: 'true';
FALSE: 'false';
DECIMAL_DIGIT: [0-9];
NON_ZERO_DIGIT: [1-9];
EXPONENT_INDICATOR: [eE];
BINARY_DIGIT: [01];
HEX_DIGIT: [0-9a-fA-F];
OCTAL_DIGIT: [0-7];
