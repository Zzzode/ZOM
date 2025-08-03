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
ALIAS: 'alias';
ANY: 'any';
AS: 'as';
ASSERTS: 'asserts';
ASSERT: 'assert';
ASYNC: 'async';
AWAIT: 'await';
BIGINT: 'bigint';
BOOLEAN: 'boolean';
BOOL: 'bool';
BREAK: 'break';
CASE: 'case';
CATCH: 'catch';
CLASS: 'class';
CONTINUE: 'continue';
CONSTRUCTOR: 'constructor';
DEBUGGER: 'debugger';
DECLARE: 'declare';
DEFAULT: 'default';
DEINIT: 'deinit';
DELETE: 'delete';
DO: 'do';
EXTENDS: 'extends';
ELSE: 'else';
ENUM: 'enum';
ERROR: 'error';
EXPORT: 'export';
FALSE: 'false';
FINALLY: 'finally';
FOR: 'for';
FROM: 'from';
F32: 'f32';
F64: 'f64';
FUN: 'fun';
GET: 'get';
GLOBAL: 'global';
I8: 'i8';
I32: 'i32';
I64: 'i64';
IF: 'if';
IMMEDIATE: 'immediate';
IMPLEMENTS: 'implements';
IMPORT: 'import';
IN: 'in';
INFER: 'infer';
INIT: 'init';
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
NIL: 'nil';
NUMBER: 'number';
NULL: 'null';
OBJECT: 'object';
OF: 'of';
OUT: 'out';
OVERRIDE: 'override';
PACKAGE: 'package';
PRIVATE: 'private';
PROTECTED: 'protected';
PUBLIC: 'public';
RAISES: 'raises';
READONLY: 'readonly';
REQUIRE: 'require';
RETURN: 'return';
SATISFIES: 'satisfies';
SET: 'set';
STATIC: 'static';
STR: 'str';
STRUCT: 'struct';
SUPER: 'super';
SWITCH: 'switch';
SYMBOL: 'symbol';
THIS: 'this';
THROW: 'throw';
TRUE: 'true';
TRY: 'try';
TYPE: 'type';
TYPEOF: 'typeof';
U8: 'u8';
U16: 'u16';
U32: 'u32';
U64: 'u64';
UNDEFINED: 'undefined';
UNIQUE: 'unique';
UNIT: 'unit';
USING: 'using';
VAR: 'var';
VOID: 'void';
WHEN: 'when';
WHILE: 'while';
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
ERROR_PROPAGATE: '?!';
FORCE_UNWRAP: '!!';
ERROR_DEFAULT: '?:';
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
ROCKET: '=>';
ARROW: '->';

DIV: '/';
DIV_ASSIGN: '/=';

// ================================================================================ LITERALS

DECIMAL_DIGIT: [0-9];
NON_ZERO_DIGIT: [1-9];
EXPONENT_INDICATOR: [eE];
BINARY_DIGIT: [01];
HEX_DIGIT: [0-9a-fA-F];
OCTAL_DIGIT: [0-7];
ZERO: '0';
BINARY_PREFIX: '0' [bB];
OCTAL_PREFIX: '0' [oO];
HEX_PREFIX: '0' [xX];

//// ================================================================================ STRING LITERALS
SQUOTE: '\'';
DQUOTE: '"';
DOUBLE_STRING_ALLOWED_CHAR:
	~["\\\r\n\u2028\u2029]; // Any char except ", \, or LineTerminator
SINGLE_STRING_ALLOWED_CHAR:
	~['\\\r\n\u2028\u2029]; // Any char except ', \, or LineTerminator
SINGLE_ESCAPE_CHARACTER:
	SQUOTE
	| DQUOTE
	| BACKSLASH
	| 'b'
	| 'f'
	| 'n'
	| 'r'
	| 't'
	| 'v';
NON_ESCAPE_CHARACTER:
	~['"\\bfnrtvxu0-9LF\u000A\u000D\u2028\u2029];
// Any source character not part of an escape sequence

//// ================================================================================ CHARACTER LITERALS
CHAR_LITERAL:
	SQUOTE (CHAR_CONTENT | CHAR_ESCAPE_SEQUENCE) SQUOTE;
CHAR_CONTENT:
	~['\\\r\n\u2028\u2029]; // Any char except ', \, or LineTerminator
CHAR_ESCAPE_SEQUENCE:
	BACKSLASH (
		SINGLE_ESCAPE_CHARACTER
		| HEX_ESCAPE_SEQUENCE
		| UNICODE_ESCAPE_SEQUENCE
	);

// ================================================================================== DECLARATIONS

LET_OR_CONST: 'let' | 'const';
