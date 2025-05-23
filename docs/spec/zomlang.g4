parser grammar zomlang;

options {
	tokenVocab = lexical;
}

// ================================================================================
// White Space
whitespace: TAB | VT | FF | ZWNBSP;

// ================================================================================
// Line Terminator
lineTerminator: LF | CR | LS | PS;
lineTerminatorSequence:
	LF
	| CR {_input.LA(1) != LF}?
	| LS
	| PS
	| CR LF;

// ================================================================================
// Comments

// ================================================================================
// Tokens

// ================================================================================
// Identifiers
identifierName: identifierStart identifierPart*;

identifierStart:
	identifierStartChar
	| BACKSLASH UNICODE_ESCAPE_SEQUENCE;
	// Assuming unicodeEscapeSequence is a lexer rule or fragment

identifierPart:
	identifierPartChar
	| BACKSLASH UNICODE_ESCAPE_SEQUENCE;
	// Assuming unicodeEscapeSequence is a lexer rule or fragment

identifierStartChar:
	UNICODE_ID_START
	// Lexer rule, e.g., UNICODE_ID_START : [_a-zA-Z] | <Unicode classes for ID_Start> ;
	| DOLLAR
	| UNDERSCORE;

identifierPartChar:
	UNICODE_ID_CONTINUE
	// Lexer rule, e.g., UNICODE_ID_CONTINUE : [_a-zA-Z0-9] | <Unicode classes for ID_Continue> ;
	| '$'
	| ZWNJ // Lexer rule for Zero Width Non-Joiner, e.g., ZWNJ_CHAR : '\u200C' ;
	| ZWJ; // Lexer rule for Zero Width Joiner, e.g., ZWJ_CHAR : '\u200D' ;

// Keywords
