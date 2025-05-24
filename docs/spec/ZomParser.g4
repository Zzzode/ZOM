parser grammar ZomParser;

options {
	tokenVocab = ZomLexer;
}

// ================================================================================ White Space
whitespace: TAB | VT | FF | ZWNBSP;

// ================================================================================ Line Terminator
lineTerminator: LF | CR | LS | PS;
lineTerminatorSequence:
	LF
	| CR {_input.LA(1) != LF}?
	| LS
	| PS
	| CR LF;

// ================================================================================ Comments

// ================================================================================ Tokens

// ================================================================================ Identifiers
identifierName: identifierStart identifierPart*;

identifierStart:
	identifierStartChar
	| BACKSLASH UNICODE_ESCAPE_SEQUENCE;
// Assuming unicodeEscapeSequence is a lexer rule or fragment

identifierPart:
	identifierPartChar
	| BACKSLASH UNICODE_ESCAPE_SEQUENCE;
// Assuming unicodeEscapeSequence is a lexer rule or fragment

identifierStartChar: UNICODE_ID_START | DOLLAR | UNDERSCORE;

identifierPartChar: UNICODE_ID_CONTINUE | DOLLAR | ZWNJ | ZWJ;

// ================================================================================ Keywords

// ================================================================================ Punctuators
punctuator: optionalChainingPunctuator | otherPunctuator;
optionalChainingPunctuator:
	OPTIONAL_CHAINING { _input.LT(1).getType() != ZomLexer.DECIMAL_DIGIT }?;
otherPunctuator:
	LBRACE
	| LPAREN
	| RPAREN
	| LBRACK
	| RBRACK
	| PERIOD
	| ELLIPSIS
	| SEMICOLON
	| COMMA
	| LT
	| GT
	| LTE
	| GTE
	| EQ
	| NEQ
	| STRICT_EQ
	| STRICT_NEQ
	| PLUS
	| MINUS
	| MUL
	| MOD
	| POW
	| INC
	| DEC
	| LSHIFT
	| RSHIFT
	| URSHIFT
	| BIT_AND
	| BIT_OR
	| BIT_XOR
	| NOT
	| BIT_NOT
	| AND
	| OR
	| NULL_COALESCE
	| QUESTION
	| COLON
	| ASSIGN
	| PLUS_ASSIGN
	| MINUS_ASSIGN
	| MUL_ASSIGN
	| MOD_ASSIGN
	| POW_ASSIGN
	| LSHIFT_ASSIGN
	| RSHIFT_ASSIGN
	| URSHIFT_ASSIGN
	| BIT_AND_ASSIGN
	| BIT_OR_ASSIGN
	| BIT_XOR_ASSIGN
	| AND_ASSIGN
	| OR_ASSIGN
	| NULL_COALESCE_ASSIGN
	| ARROW
	| DIV
	| DIV_ASSIGN;
divPunctuator: DIV | DIV_ASSIGN;
rightBracePunctuator: RBRACE;

// ================================================================================ LITERALS
