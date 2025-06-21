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
	| ROCKET
	| DIV
	| DIV_ASSIGN;
divPunctuator: DIV | DIV_ASSIGN;
rightBracePunctuator: RBRACE;

// ================================================================================ LITERALS
literal:
	nilLiteral
	| booleanLiteral
	| numericLiteral
	| stringLiteral;

//// =============================================================================== NIL LITERALS
nilLiteral: NIL;

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
	| HEX_ESCAPE_SEQUENCE
	| UNICODE_ESCAPE_SEQUENCE;

characterEscapeSequence:
	singleEscapeCharacter
	| nonEscapeCharacter;

singleEscapeCharacter: SINGLE_ESCAPE_CHARACTER;

nonEscapeCharacter:
	NON_ESCAPE_CHARACTER; // Any source character not part of an escape sequence

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

assignmentExpression:
	conditionalExpression
	| leftHandSideExpression ASSIGN assignmentExpression
	| leftHandSideExpression assignmentOperator assignmentExpression
	| leftHandSideExpression AND_ASSIGN assignmentExpression
	| leftHandSideExpression OR_ASSIGN assignmentExpression
	| leftHandSideExpression NULL_COALESCE_ASSIGN assignmentExpression;

// Primary Expression
primaryExpression:
	THIS
	| identifier
	| literal
	| arrayLiteral
	| objectLiteral
	| coverParenthesizedExpressionAndArrowParameterList;

// Cover Parenthesized Expression and Arrow Parameter List
coverParenthesizedExpressionAndArrowParameterList:
	LPAREN expression RPAREN
	| LPAREN expression COMMA RPAREN
	| LPAREN RPAREN
	| LPAREN ELLIPSIS bindingIdentifier RPAREN
	| LPAREN ELLIPSIS bindingPattern RPAREN
	| LPAREN expression COMMA ELLIPSIS bindingIdentifier RPAREN
	| LPAREN expression COMMA ELLIPSIS bindingPattern RPAREN;

// Object Literal
objectLiteral:
	LBRACE RBRACE
	| LBRACE propertyDefinitionList RBRACE
	| LBRACE propertyDefinitionList COMMA RBRACE;

propertyDefinitionList:
	propertyDefinition
	| propertyDefinitionList COMMA propertyDefinition;

propertyDefinition:
	identifier
	| coverInitializedName
	| propertyName COLON assignmentExpression
	| ELLIPSIS assignmentExpression;

coverInitializedName: identifier initializer;

// Array Literal
arrayLiteral: LBRACK RBRACK | LBRACK elementList RBRACK;

elementList:
	assignmentExpression
	| spreadElement
	| elementList COMMA assignmentExpression
	| elementList COMMA spreadElement;

spreadElement: ELLIPSIS assignmentExpression;

// Left Hand Side Expression
leftHandSideExpression:
	newExpression
	| callExpression
	| optionalExpression;

// Member Expression
memberExpression:
	primaryExpression
	| memberExpression LBRACK expression RBRACK
	| memberExpression PERIOD identifier
	| superProperty
	| NEW memberExpression arguments;

// New Expression
newExpression: memberExpression | NEW newExpression;

// Super Property
superProperty: SUPER PERIOD identifier;

// Super Call
superCall: SUPER arguments;

// Arguments
arguments:
	typeArguments? LPAREN RPAREN
	| typeArguments? LPAREN argumentList RPAREN
	| typeArguments? LPAREN argumentList COMMA RPAREN;

argumentList:
	assignmentExpression
	| ELLIPSIS assignmentExpression
	| argumentList COMMA assignmentExpression
	| argumentList COMMA ELLIPSIS assignmentExpression;

// Call Expression
callExpression:
	memberExpression arguments
	| superCall
	| callExpression arguments
	| callExpression LBRACK expression RBRACK
	| callExpression PERIOD identifier;

// Optional Expression
optionalExpression:
	memberExpression optionalChain
	| callExpression optionalChain
	| optionalExpression optionalChain;

optionalChain:
	OPTIONAL_CHAINING identifier
	| optionalChain arguments
	| optionalChain LBRACK expression RBRACK
	| optionalChain PERIOD identifier;

// Update Expression
updateExpression:
	leftHandSideExpression
	| leftHandSideExpression INC
	| leftHandSideExpression DEC
	| INC unaryExpression
	| DEC unaryExpression;

// Unary Expression
unaryExpression:
	updateExpression
	| VOID unaryExpression
	| TYPEOF unaryExpression
	| PLUS unaryExpression
	| MINUS unaryExpression
	| BIT_NOT unaryExpression
	| NOT unaryExpression
	| awaitExpression
	| LT type GT unaryExpression;

awaitExpression: AWAIT unaryExpression;

// Cast Expression
castExpression:
	unaryExpression
	| castExpression AS type
	| castExpression AS QUESTION type
	| castExpression AS NOT type;

// Exponentiation Expression
exponentiationExpression:
	castExpression
	| updateExpression POW exponentiationExpression;

// Multiplicative Expression
multiplicativeExpression:
	exponentiationExpression
	| multiplicativeExpression multiplicativeOperator exponentiationExpression;

multiplicativeOperator: MUL | DIV | MOD;

// Additive Expression
additiveExpression:
	multiplicativeExpression
	| additiveExpression PLUS multiplicativeExpression
	| additiveExpression MINUS multiplicativeExpression;

// Shift Expression
shiftExpression:
	additiveExpression
	| shiftExpression LSHIFT additiveExpression
	| shiftExpression RSHIFT additiveExpression
	| shiftExpression URSHIFT additiveExpression;

// Relational Expression
relationalExpression:
	shiftExpression
	| relationalExpression LT shiftExpression
	| relationalExpression GT shiftExpression
	| relationalExpression LTE shiftExpression
	| relationalExpression GTE shiftExpression
	| relationalExpression IS shiftExpression
	| relationalExpression IN shiftExpression;

// Equality Expression
equalityExpression:
	relationalExpression
	| equalityExpression EQ relationalExpression
	| equalityExpression NEQ relationalExpression
	| equalityExpression STRICT_EQ relationalExpression
	| equalityExpression STRICT_NEQ relationalExpression;

// Bitwise AND Expression
bitwiseANDExpression:
	equalityExpression
	| bitwiseANDExpression BIT_AND equalityExpression;

// Bitwise XOR Expression
bitwiseXORExpression:
	bitwiseANDExpression
	| bitwiseXORExpression BIT_XOR bitwiseANDExpression;

// Bitwise OR Expression
bitwiseORExpression:
	bitwiseXORExpression
	| bitwiseORExpression BIT_OR bitwiseXORExpression;

// Logical AND Expression
logicalANDExpression:
	bitwiseORExpression
	| logicalANDExpression AND bitwiseORExpression;

// Logical OR Expression
logicalORExpression:
	logicalANDExpression
	| logicalORExpression OR logicalANDExpression;

// Coalesce Expression
coalesceExpression:
	bitwiseORExpression
	| coalesceExpression NULL_COALESCE bitwiseORExpression;

// Short Circuit Expression
shortCircuitExpression:
	logicalORExpression
	| coalesceExpression;

// Conditional Expression
conditionalExpression:
	shortCircuitExpression
	| shortCircuitExpression QUESTION assignmentExpression COLON assignmentExpression;

// Binding Pattern
bindingPattern: arrayBindingPattern | objectBindingPattern;

objectBindingPattern: LBRACE bindingPropertyList? RBRACE;

bindingPropertyList:
	bindingProperty
	| bindingPropertyList COMMA bindingProperty;

bindingProperty:
	propertyName COLON bindingElement
	| bindingIdentifier initializer?;

// Initializer
initializer: ASSIGN assignmentExpression;

assignmentOperator:
	MUL_ASSIGN
	| DIV_ASSIGN
	| MOD_ASSIGN
	| PLUS_ASSIGN
	| MINUS_ASSIGN
	| LSHIFT_ASSIGN
	| RSHIFT_ASSIGN
	| URSHIFT_ASSIGN
	| BIT_AND_ASSIGN
	| BIT_XOR_ASSIGN
	| BIT_OR_ASSIGN
	| POW_ASSIGN;

// ================================================================================ STATEMENTS
blockStatement: block;

block: LBRACE statementList? RBRACE;

statementList: statementListItem+;

statementListItem: statement | declaration;

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

emptyStatement: SEMICOLON;

expressionStatement:
	{!(_input.LT(1).getType() == LBRACE || _input.LT(1).getText().equals("function") || _input.LT(1).getText().equals("class") || _input.LT(1).getText().equals("let"))
		}? expression SEMICOLON;

ifStatement:
	IF LPAREN expression RPAREN statement ELSE statement
	| IF LPAREN expression RPAREN statement {_input.LT(1).getType() != ZomLexer.ELSE}?;

breakableStatement: iterativeStatement;

iterativeStatement: whileStatement | forStatement;

whileStatement: WHILE LPAREN expression RPAREN statement;

forStatement:
	FOR LPAREN forInit? SEMICOLON expression? SEMICOLON forUpdate? RPAREN statement;

forInit: expression | lexicalDeclaration;

forUpdate: expression;

continueStatement: CONTINUE identifier? SEMICOLON;

breakStatement: BREAK identifier? SEMICOLON;

returnStatement: RETURN expression? SEMICOLON;

debuggerStatement: DEBUGGER SEMICOLON;

matchStatement: MATCH LPAREN expression RPAREN matchBlock;
matchBlock:
	LBRACE matchClauses RBRACE
	| LBRACE matchClauses defaultClause RBRACE
	| LBRACE defaultClause RBRACE;

matchClauses: matchClause*;
matchClause:
	WHEN pattern guardClause? ROCKET expression
	| block;

guardClause: IF expression;
defaultClause: DEFAULT ROCKET statementList;

pattern: primaryPattern (AS type)?;

primaryPattern:
	wildcardPattern
	| identifierPattern
	| tuplePattern
	| structurePattern
	| arrayPattern
	| isPattern
	| expressionPattern
	| enumPattern;

wildcardPattern: UNDERSCORE typeAnnotation?;
identifierPattern: bindingIdentifier typeAnnotation?;
tuplePattern: LPAREN tupleElements? RPAREN;
tupleElements: tupleElement (COMMA tupleElement)*;
tupleElement: bindingIdentifier typeAnnotation?;
structurePattern: objectType;
arrayPattern: arrayBindingPattern;
isPattern: IS type;
expressionPattern: expression;
enumPattern: typeReference? PERIOD propertyName tuplePattern;

// ================================================================================ DECLARATIONS
declaration:
	functionDeclaration
	| classDeclaration
	| interfaceDeclaration
	| typeAliasDeclaration
	| structDeclaration
	| errorDeclaration
	| enumDeclaration
	| lexicalDeclaration;

//// ================================================================================ TYPES

// Type
type: OPTIONAL? unionType;

// ArrayType
arrayType: typeAtom (LBRACK RBRACK)*;

// PrimaryType
primaryType: typeAtom | arrayType;
typeAtom:
	parenthesizedType
	| predefinedType
	| typeReference
	| objectType
	| tupleType
	| typeQuery;

// ParenthesizedType
parenthesizedType: LPAREN type RPAREN;

// UnionType
unionType: intersectionType (BIT_OR intersectionType)*;
intersectionType: primaryType (BIT_AND primaryType)*;

// PredefinedType
predefinedType:
	I8
	| I32
	| I64
	| U8
	| U16
	| U32
	| U64
	| F32
	| F64
	| STR
	| BOOL
	| NIL
	| UNIT;

// TypeReference
typeReference: typeName typeArguments?;

// TypeName
typeName: identifier;

// ObjectType
objectType: LBRACE typeBody? RBRACE;

// TypeBody
typeBody: typeMemberList SEMICOLON? | typeMemberList COMMA?;

// TypeMemberList
typeMemberList:
	typeMember
	| typeMemberList SEMICOLON typeMember
	| typeMemberList COMMA typeMember;

// TypeMember
typeMember: propertySignature;

// TupleType
tupleType: LPAREN tupleElementTypes? RPAREN;

// TupleElementTypes
tupleElementTypes:
	tupleElementType
	| tupleElementTypes COMMA tupleElementType;

// TupleElementType
tupleElementType: namedTupleElement | type;

// NamedTupleElement
namedTupleElement: elementName COLON type;

// ElementName
elementName: identifier;

// FunctionType
functionType: typeParameters? parameterClause ARROW type;

// ParameterClause
parameterClause: LPAREN parameterList? RPAREN;

// TypeQuery
typeQuery: TYPEOF typeQueryExpression;

// TypeQueryExpression
typeQueryExpression:
	identifier
	| typeQueryExpression PERIOD identifier;

// OptionalType
optionalType: OPTIONAL primaryType;

// Supporting rules
typeArguments: LT typeArgumentList GT;
typeArgumentList: type (COMMA type)*;
propertySignature: propertyName QUESTION? typeAnnotation;
propertyName: identifier | stringLiteral | numericLiteral;
typeParameters: LT typeParameterList GT;
typeParameterList: typeParameter (COMMA typeParameter)*;
typeParameter: identifier constraint?;
constraint: EXTENDS type;
interfaceType: typeReference;
callSignature:
	typeParameters? LPAREN parameterList? RPAREN (
		(ARROW | ERROR_RETURN) type
	)?;
parameterList: parameter (COMMA parameter)*;
parameter: bindingIdentifier typeAnnotation? initilizer?;
initilizer: ASSIGN expression;
functionBody: statementList?;

arrayBindingPattern: LBRACK bindingElementList? RBRACK;
bindingElementList: bindingElement (COMMA bindingElement)*;
bindingElement: bindingIdentifier typeAnnotation?;
bindingList: bindingElement (COMMA bindingElement)*;
methodSignature: propertyName QUESTION? callSignature;

// ================================================================================ INTERFACE DECLARATIONS
interfaceDeclaration:
	INTERFACE bindingIdentifier typeParameters? interfaceHeritage? LBRACE interfaceBody RBRACE;
interfaceHeritage: EXTENDS interfaceTypeList;
interfaceBody: interfaceElement*;
interfaceElement: propertySignature | methodSignature;

// ================================================================================ TYPE ALIAS DECLARATIONS
typeAliasDeclaration:
	ALIAS bindingIdentifier typeParameters? ASSIGN type;

// ================================================================================ STRUCT DECLARATIONS
structDeclaration:
	STRUCT bindingIdentifier typeParameters? LBRACE structBody? RBRACE;

structBody: structMember (COMMA structMember)*;
structMember: propertyName COLON type;

// ================================================================================ ERROR DECLARATIONS
errorDeclaration:
	ERROR bindingIdentifier typeParameters? LBRACE errorBody? RBRACE;

errorBody: errorMember (COMMA errorMember)*;
errorMember: propertyName COLON type;

// ================================================================================ ENUM DECLARATIONS
enumDeclaration: ENUM bindingIdentifier LBRACE enumBody? RBRACE;
enumBody: enumMember (COMMA enumMember)*;
enumMember: propertyName (ASSIGN expression)?;

// ================================================================================ CLASS MEMBER ACCESSORS
accessibityModifier: PUBLIC | PRIVATE | PROTECTED;
initDeclaration:
	accessibityModifier? INIT callSignature LBRACE functionBody RBRACE;
deinitDeclaration: DEINIT LBRACE functionBody RBRACE;
getAccessor:
	GET propertyName LPAREN RPAREN typeAnnotation? LBRACE functionBody RBRACE;
setAccessor:
	SET propertyName LPAREN parameter RPAREN LBRACE functionBody RBRACE;

//// ============================================================================== LET AND CONST DECLARATIONS
lexicalDeclaration: LET_OR_CONST bindingList;

//// ============================================================================== TYPES
typeAnnotation: COLON type;

// ================================================================================ FUNCTIONS
functionDeclaration:
	FUN bindingIdentifier callSignature errorReturnClause? raisesClause? LBRACE functionBody RBRACE;

// Error handling clauses
errorReturnClause: ERROR_RETURN type;
raisesClause: RAISES errorTypeList;
errorTypeList: type (BIT_OR type)*;

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
	initDeclaration
	| deinitDeclaration
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