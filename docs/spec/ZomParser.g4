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

identifierPart:
	identifierPartChar
	| BACKSLASH UNICODE_ESCAPE_SEQUENCE;

identifierStartChar: UNICODE_ID_START | DOLLAR | UNDERSCORE;
identifierPartChar: UNICODE_ID_CONTINUE | DOLLAR | ZWNJ | ZWJ;

bindingIdentifier: identifier;
identifier:
	identifierName {
    String text = _input.getText(ctx.identifierName());
    if(isReservedWord(text)) {
      throw new ParseCancellationException("Identifier cannot be a reserved word");
    }
};

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
literal:
	nullLiteral
	| booleanLiteral
	| numericLiteral
	| stringLiteral;

//// =============================================================================== NULL LITERALS
nullLiteral: NULL;

//// =============================================================================== BOOLEAN LITERALS
booleanLiteral: TRUE | FALSE;

//// =============================================================================== NUMERIC LITERALS
numericLiteralSeparator: UNDERSCORE;

numericLiteral:
	decimalLiteral
	| binaryIntegerLiteral
	| octalIntegerLiteral
	| hexIntegerLiteral;

decimalLiteral:
	decimalIntegerLiteral PERIOD decimalDigits? exponentPart?
	| PERIOD decimalDigits exponentPart? // e.g., .123
	| decimalIntegerLiteral exponentPart?; // e.g., 123e4 or 123

decimalIntegerLiteral:
	ZERO
	| NON_ZERO_DIGIT (numericLiteralSeparator? decimalDigits)?; // Assumes lexer token for '1'..'9'

decimalDigits:
	DECIMAL_DIGIT (numericLiteralSeparator? DECIMAL_DIGIT)*; // Assumes lexer token for '0'..'9'

exponentPart:
	EXPONENT_INDICATOR signedInteger; // Assumes lexer token for 'e'|'E'

signedInteger: (PLUS | MINUS)? decimalDigits;

binaryIntegerLiteral: BINARY_PREFIX binaryDigits;
binaryDigits:
	BINARY_DIGIT (numericLiteralSeparator? BINARY_DIGIT)*;

octalIntegerLiteral: OCTAL_PREFIX octalDigits;
octalDigits:
	OCTAL_DIGIT (numericLiteralSeparator? OCTAL_DIGIT)*;

hexIntegerLiteral: HEX_PREFIX hexDigits;
hexDigits: HEX_DIGIT (numericLiteralSeparator? HEX_DIGIT)*;

//// =============================================================================== STRING LITERALS
stringLiteral:
	DQUOTE doubleStringCharacters? DQUOTE
	| SQUOTE singleStringCharacters? SQUOTE;

doubleStringCharacters:
	doubleStringCharacter doubleStringCharacters?;

singleStringCharacters:
	singleStringCharacter singleStringCharacters?;

doubleStringCharacter:
	DOUBLE_STRING_ALLOWED_CHAR // SourceCharacter but not one of " or \ or LineTerminator
	| LS
	| PS
	| BACKSLASH escapeSequence // \ EscapeSequence
	| lineContinuation;

singleStringCharacter:
	SINGLE_STRING_ALLOWED_CHAR // SourceCharacter but not one of ' or \ or LineTerminator
	| LS
	| PS
	| BACKSLASH escapeSequence // \ EscapeSequence
	| lineContinuation;

// Define escapeSequence and lineContinuation if they are not already defined For example:
escapeSequence:
	characterEscapeSequence
	| ZERO {getCharPositionInLine() == 0}? // Assuming 0 is not part of a decimal literal
	| hexEscapeSequence
	| unicodeEscapeSequence;

characterEscapeSequence:
	singleEscapeCharacter
	| nonEscapeCharacter;

singleEscapeCharacter: SINGLE_ESCAPE_CHARACTER;

nonEscapeCharacter:
	NON_ESCAPE_CHARACTER; // Any source character not part of an escape sequence

hexEscapeSequence: 'x' HEX_DIGIT HEX_DIGIT;

unicodeEscapeSequence:
	'u' (hex4Digits | LBRACE codePoint RBRACE);

hex4Digits: HEX_DIGIT HEX_DIGIT HEX_DIGIT HEX_DIGIT;

lineContinuation: BACKSLASH lineTerminatorSequence;

//// ============================================================================== TEMPLATE LITERALS

codePoint:
	hexDigits {
    String hexText = _input.getText(_localctx.hexDigits());
    long value = Long.parseLong(hexText, 16);
    if (value > 0x10FFFF) {
        throw new ParseCancellationException("Code point value cannot exceed 0x10FFFF");
    }
};

// ================================================================================ EXPRESSIONS
expression: assignmentExpression (COMMA assignmentExpression)*;

// ================================================================================ STATEMENTS
statement:
	blockStatement
	| emptyStatement
	| expressionStatement
	| ifStatement
	| matchStatement
	| breakableStatement
	| continueStatement
	| breakStatement
	| returnStatement
	| debuggerStatement;

statementListItem: statement | declaration;

matchStatement: MATCH LPAREN expression RPAREN matchBlock;
matchBlock:
	LBRACE matchClauses RBRACE
	| LBRACE matchClauses defaultClause RBRACE
	| LBRACE defaultClause RBRACE;

matchClauses: matchClause*;
matchClause: WHEN pattern guardClause? ARROW expression | block;

guardClause: IF expression;
defaultClause: DEFAULT ARROW statementList;

pattern:
	wildcardPattern
	| identifierPattern
	| tuplePattern
	| structurePattern
	| arrayPattern
	| typeCastPattern
	| expressionPattern
	| enumPattern;

wildcardPattern: UNDERSCORE typeAnnotation?;
identifierPattern: bindingIdentifier typeAnnotation?;
tuplePattern: LPAREN tupleElements? RPAREN;
tupleElements: tupleElement (COMMA tupleElement)*;
tupleElement: bindingIdentifier typeAnnotation?;
structurePattern: objectType;
arrayPattern: arrayBindingPattern;
typeCastPattern: isPattern | asPattern;
isPattern: IS type;
asPattern: pattern AS type;
expressionPattern: expression;
enumPattern: typeReference? PERIOD propertyName tuplePattern;

// ================================================================================ DECLARATIONS
declaration:
	functionDeclaration
	| classDeclaration
	| interfaceDeclaration
	| typeAliasDeclaration
	| enumDeclaration
	| lexicalDeclaration;

//// ============================================================================== LET AND CONST DECLARATIONS
lexicalDeclaration: LET_OR_CONST bindingList;

//// ============================================================================== TYPES
typeAnnotation: COLON type;

// ================================================================================ FUNCTIONS
functionDeclaration:
	FUN bindingIdentifier callSignature LBRACE functionBody RBRACE;

// ================================================================================ CLASSES
classDeclaration:
	CLASS bindingIdentifier typeParameters? classTail;

classTail: classHeritage? LBRACE classBody RBRACE;

classHeritage: EXTENDS classType | IMPLEMENTS interfaceTypeList;

classType: typeReference;

interfaceTypeList: interfaceType ( COMMA interfaceType)*;

classBody: classElementList?;

classElementList: classElement+;

classElement:
	constructorDeclaration
	| propertyMemberDeclaration;

propertyMemberDeclaration:
	memberVariableDeclaration
	| memberFunctionDeclaration
	| memberAccessorDeclaration;

memberVariableDeclaration:
	accessibityModifier? STATIC? propertyName typeAnnotation? initilizer?;

memberFunctionDeclaration:
	accessibityModifier? STATIC? propertyName callSignature? LBRACE functionBody RBRACE;

memberAccessorDeclaration:
	accessibityModifier? STATIC? (getAccessor | setAccessor);

// ================================================================================ INTERFACES

// ================================================================================ SCRIPTS AND MODULES
sourceFile: module;

module: moduleBody?;
moduleBody: moduleItemList;
moduleItemList: moduleItem+;
moduleItem:
	statementListItem
	| exportDeclaration
	| importDeclaration;

importDeclaration: IMPORT modulePath ( AS identifierName)?;

exportDeclaration: EXPORT (exportModule | exportRename);

modulePath: bindingIdentifier ( PERIOD bindingIdentifier)*;

exportModule: bindingIdentifier;

exportRename:
	bindingIdentifier AS bindingIdentifier FROM modulePath;
