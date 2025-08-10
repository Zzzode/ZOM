# Grammar Reference

This section provides the complete formal grammar for the Zom language in EBNF notation.

### Lexical Grammar

```ebnf
(* Whitespace and Line Terminators *)
Whitespace ::= [ \t\v\f\u0020\u00A0\uFEFF\u1680\u2000-\u200A\u202F\u205F\u3000]
LineTerminator ::= [\n\r\u2028\u2029]

(* Comments *)
SingleLineComment ::= '//' [^\n\r\u2028\u2029]*
MultiLineComment ::= '/*' (!'*/' .)* '*/'

(* Identifiers *)
IdentifierName ::= IdentifierStart IdentifierPart*
IdentifierStart ::= UnicodeIDStart | '$' | '_' | '\\' UnicodeEscapeSequence
IdentifierPart ::= UnicodeIDContinue | '$' | '\u200C' | '\u200D' | '\\' UnicodeEscapeSequence

(* Literals *)
NullLiteral ::= 'null'
BooleanLiteral ::= 'true' | 'false'

(* Numeric Literals *)
NumericLiteral ::= DecimalLiteral | BinaryLiteral | OctalLiteral | HexLiteral

DecimalLiteral ::= DecimalIntegerLiteral ('.' DecimalDigits?)? ExponentPart?
                 | '.' DecimalDigits ExponentPart?
                 | DecimalIntegerLiteral ExponentPart?

DecimalIntegerLiteral ::= '0' | NON_ZERO_DIGIT (NumericLiteralSeparator? DECIMAL_DIGIT)*
DecimalDigits ::= DECIMAL_DIGIT (NumericLiteralSeparator? DECIMAL_DIGIT)*
ExponentPart ::= [eE] SignedInteger
SignedInteger ::= ('+' | '-')? DecimalDigits

BinaryLiteral ::= '0' [bB] BinaryDigits
BinaryDigits ::= BINARY_DIGIT (NumericLiteralSeparator? BINARY_DIGIT)*

OctalLiteral ::= '0' [oO] OctalDigits
OctalDigits ::= OCTAL_DIGIT (NumericLiteralSeparator? OCTAL_DIGIT)*

HexLiteral ::= '0' [xX] HexDigits
HexDigits ::= HEX_DIGIT (NumericLiteralSeparator? HEX_DIGIT)*

NumericLiteralSeparator ::= '_'

(* String Literals *)
StringLiteral ::= '"' DoubleStringCharacter* '"' | "'" SingleStringCharacter* "'"
DoubleStringCharacter ::= ~["\\\r\n\u2028\u2029] | LineTerminator | '\\' EscapeSequence | LineContinuation
SingleStringCharacter ::= ~['\\\r\n\u2028\u2029] | LineTerminator | '\\' EscapeSequence | LineContinuation

EscapeSequence ::= CharacterEscapeSequence | '0' | HexEscapeSequence | UnicodeEscapeSequence
CharacterEscapeSequence ::= '\\' ["\\bfnrtv]
HexEscapeSequence ::= 'x' HEX_DIGIT HEX_DIGIT
UnicodeEscapeSequence ::= 'u' HEX_DIGIT HEX_DIGIT HEX_DIGIT HEX_DIGIT | 'u{' HEX_DIGIT+ '}'
LineContinuation ::= '\\' LineTerminatorSequence

(* Character Literals *)
CharacterLiteral ::= "'" SingleStringCharacter "'"

(* Punctuators *)
Punctuator ::= '{' | '}' | '(' | ')' | '[' | ']' | '.' | '...' | ';' | ',' | ':' | '?'
            | '+' | '-' | '*' | '/' | '%' | '**'
            | '++' | '--'
            | '<<' | '>>' | '>>>'
            | '<' | '>' | '<=' | '>='
            | '==' | '!=' | '===' | '!=='
            | '&' | '|' | '^' | '!' | '~'
            | '&&' | '||' | '??' | '?!' | '!!' | '?:'
            | '=' | '+=' | '-=' | '*=' | '/=' | '%=' | '**='
            | '<<=' | '>>=' | '>>>=' | '&=' | '|=' | '^='
            | '&&=' | '||=' | '??='
            | '=>' | '->' | '?.'
```

### Syntactic Grammar

```ebnf
(* Program Structure *)
Program ::= SourceFile
SourceFile ::= ModuleDeclaration? ModuleItem*
ModuleItem ::= StatementListItem | ExportDeclaration | ImportDeclaration

ImportDeclaration ::= 'import' ModulePath ('as' Identifier)? ';'
ExportDeclaration ::= 'export' (ExportModule | ExportRename) ';'
ModulePath ::= Identifier ('.' Identifier)*
ExportModule ::= Identifier
ExportRename ::= Identifier 'as' Identifier 'from' ModulePath

(* Declarations *)
Declaration ::= FunctionDeclaration
             | ClassDeclaration
             | InterfaceDeclaration
             | AliasDeclaration
             | StructDeclaration
             | ErrorDeclaration
             | EnumDeclaration
             | VariableDeclaration

VariableDeclaration ::= LetOrConst BindingList ';'
LetOrConst ::= 'let' | 'const'
BindingList ::= BindingElement (',' BindingElement)*
BindingElement ::= BindingIdentifier (':' TypeExpression)? ('=' Expression)?

FunctionDeclaration ::= Attribute* 'async'? 'fun' BindingIdentifier TypeParameters?
                       CallSignature (BlockStatement | ';')

ClassDeclaration ::= Attribute* 'abstract'? 'class' BindingIdentifier TypeParameters?
                    ('extends' TypeExpression)? ('implements' InterfaceTypeList)?
                    '{' ClassMember* '}'

InterfaceDeclaration ::= 'interface' BindingIdentifier TypeParameters? InterfaceHeritage? '{' InterfaceBody '}'
InterfaceHeritage ::= 'extends' InterfaceTypeList
InterfaceBody ::= InterfaceElement*
InterfaceElement ::= PropertySignature | MethodSignature
PropertySignature ::= PropertyName '?'? TypeAnnotation
MethodSignature ::= PropertyName '?'? CallSignature

StructDeclaration ::= 'struct' BindingIdentifier TypeParameters? '{' StructBody? '}'
StructBody ::= StructMember (',' StructMember)*
StructMember ::= PropertyName ':' TypeExpression

ErrorDeclaration ::= 'error' BindingIdentifier TypeParameters? '{' ErrorBody? '}'
ErrorBody ::= ErrorMember (',' ErrorMember)*
ErrorMember ::= PropertyName ':' TypeExpression

EnumDeclaration ::= 'enum' BindingIdentifier '{' EnumBody? '}'
EnumBody ::= EnumMember (',' EnumMember)*
EnumMember ::= PropertyName ('=' Expression)?

AliasDeclaration ::= 'alias' BindingIdentifier TypeParameters? '=' TypeExpression

(* Type Expressions *)
TypeExpression ::= UnionType
UnionType ::= IntersectionType ('|' IntersectionType)*
IntersectionType ::= PostfixType ('&' PostfixType)*
PostfixType ::= AtomType PostfixTypeSuffix*
PostfixTypeSuffix ::= '[' ']' | '?'

AtomType ::= ParenthesizedType
          | PredefinedType
          | TypeReference
          | ObjectType
          | TupleType
          | FunctionType
          | TypeQuery

ParenthesizedType ::= '(' TypeExpression ')'
PredefinedType ::= 'i8' | 'i32' | 'i64' | 'u8' | 'u16' | 'u32' | 'u64'
                | 'f32' | 'f64' | 'str' | 'bool' | 'null' | 'unit'
TypeReference ::= TypeName TypeArguments?
TypeName ::= Identifier
TypeQuery ::= 'typeof' TypeQueryExpression
TypeQueryExpression ::= Identifier ('.' Identifier)*

TupleType ::= '(' TupleElementTypes? ')'
TupleElementTypes ::= TupleElementType (',' TupleElementType)*
TupleElementType ::= NamedTupleElement | TypeExpression
NamedTupleElement ::= ElementName ':' TypeExpression
ElementName ::= Identifier

FunctionType ::= TypeParameters? ParameterClause ('->' TypeExpression RaisesClause?)?
ParameterClause ::= '(' ParameterList? ')'
RaisesClause ::= 'raises' TypeList

ObjectType ::= '{' TypeBody? '}'
TypeBody ::= TypeMemberList (';' | ',')?
TypeMemberList ::= TypeMember (';' TypeMember | ',' TypeMember)*
TypeMember ::= PropertySignature

TypeParameters ::= '<' TypeParameterList '>'
TypeParameterList ::= TypeParameter (',' TypeParameter)*
TypeParameter ::= Identifier Constraint?
Constraint ::= 'extends' TypeExpression

TypeArguments ::= '<' TypeArgumentList '>'
TypeArgumentList ::= TypeExpression (',' TypeExpression)*

TypeAnnotation ::= ':' TypeExpression
CallSignature ::= TypeParameters? ParameterClause ('->' TypeExpression RaisesClause?)?
InterfaceTypeList ::= TypeReference (',' TypeReference)*
TypeList ::= TypeExpression (',' TypeExpression)*
BindingIdentifier ::= Identifier

(* Statements *)
Statement ::= BlockStatement
           | VariableDeclaration
           | ExpressionStatement
           | IfStatement
           | MatchStatement
           | WhileStatement
           | ForStatement
           | ContinueStatement
           | BreakStatement
           | ReturnStatement
           | DebuggerStatement

BlockStatement ::= '{' StatementList? '}'
StatementList ::= StatementListItem+
StatementListItem ::= Statement | Declaration

EmptyStatement ::= ';'

ExpressionStatement ::= Expression ';'

IfStatement ::= 'if' '(' Expression ')' Statement ('else' Statement)?

MatchStatement ::= 'match' '(' Expression ')' '{' MatchArm* '}'
MatchArm ::= 'when' Pattern GuardClause? ('=>' Expression | BlockStatement)
          | 'default' ('=>' Expression | BlockStatement)
GuardClause ::= 'if' Expression

WhileStatement ::= 'while' '(' Expression ')' Statement

ForStatement ::= 'for' '(' ForInit? ';' Expression? ';' ForUpdate? ')' Statement
ForInit ::= VariableDeclaration | Expression
ForUpdate ::= Expression

ContinueStatement ::= 'continue' Identifier? ';'
BreakStatement ::= 'break' Identifier? ';'
ReturnStatement ::= 'return' Expression? ';'

DebuggerStatement ::= 'debugger' ';'

(* Expressions *)
Expression ::= AssignmentExpression
AssignmentExpression ::= ConditionalExpression
                      | LeftHandSideExpression AssignmentOperator AssignmentExpression

ConditionalExpression ::= LogicalORExpression ('?' AssignmentExpression ':' AssignmentExpression)?

LogicalORExpression ::= LogicalANDExpression ('||' LogicalANDExpression)*
LogicalANDExpression ::= BitwiseORExpression ('&&' BitwiseORExpression)*
BitwiseORExpression ::= BitwiseXORExpression ('|' BitwiseXORExpression)*
BitwiseXORExpression ::= BitwiseANDExpression ('^' BitwiseANDExpression)*
BitwiseANDExpression ::= EqualityExpression ('&' EqualityExpression)*
EqualityExpression ::= RelationalExpression (('==' | '!=' | '===' | '!==') RelationalExpression)*
RelationalExpression ::= ShiftExpression (('<' | '>' | '<=' | '>=' | 'is' | 'in') ShiftExpression)*
ShiftExpression ::= AdditiveExpression (('<<' | '>>' | '>>>') AdditiveExpression)*
AdditiveExpression ::= MultiplicativeExpression (('+' | '-') MultiplicativeExpression)*
MultiplicativeExpression ::= ExponentiationExpression (('*' | '/' | '%') ExponentiationExpression)*
ExponentiationExpression ::= UnaryExpression ('**' ExponentiationExpression)?

UnaryExpression ::= PostfixExpression
                 | UpdateExpression
                 | ('++' | '--' | '+' | '-' | '!' | '~' | 'typeof' | 'await') UnaryExpression
                 | CastExpression
                 | AwaitExpression

CastExpression ::= UnaryExpression ('as' | 'as?') TypeExpression
                 | '<' TypeExpression '>' UnaryExpression

AwaitExpression ::= 'await' UnaryExpression

UpdateExpression ::= ('++' | '--') UnaryExpression
                 | LeftHandSideExpression ('++' | '--')

PostfixExpression ::= LeftHandSideExpression PostfixSuffix*
LeftHandSideExpression ::= NewExpression
                        | CallExpression
                        | MemberExpression

NewExpression ::= MemberExpression
                | 'new' NewExpression

MemberExpression ::= PrimaryExpression
                  | MemberExpression '[' Expression ']'
                  | MemberExpression '.' Identifier
                  | MemberExpression '?.' Identifier

CallExpression ::= MemberExpression Arguments
                | CallExpression Arguments
                | CallExpression '[' Expression ']'
                | CallExpression '.' Identifier
                | CallExpression '?.' Identifier

Arguments ::= '(' ArgumentList? ')'
ArgumentList ::= Expression (',' Expression)*

PrimaryExpression ::= 'this'
                   | 'super'
                   | Identifier
                   | Literal
                   | ArrayLiteral
                   | ObjectLiteral
                   | FunctionExpression
                   | '(' Expression ')'

ArrayLiteral ::= '[' (ElementList)? ']'
ElementList ::= Expression (',' Expression)*

ObjectLiteral ::= '{' (PropertyDefinitionList)? '}'
PropertyDefinitionList ::= PropertyDefinition (',' PropertyDefinition)*
PropertyDefinition ::= Identifier
                    | PropertyName ':' Expression
                    | '...' Expression
PropertyName ::= Identifier | StringLiteral | NumericLiteral

FunctionExpression ::= 'fun' CallSignature BlockStatement

(* Patterns *)
Pattern ::= PrimaryPattern

PrimaryPattern ::= WildcardPattern
                | IdentifierPattern
                | TuplePattern
                | StructurePattern
                | ArrayPattern
                | IsPattern
                | ExpressionPattern
                | EnumPattern

WildcardPattern ::= '_'
IdentifierPattern ::= Identifier
TuplePattern ::= '(' (Pattern (',' Pattern)*)? ')'
StructurePattern ::= '{' (PropertyPattern (',' PropertyPattern)*)? '}'
ArrayPattern ::= '[' (Pattern (',' Pattern)*)? ']'
IsPattern ::= 'is' TypeExpression
ExpressionPattern ::= Expression
EnumPattern ::= TypeReference ('.' Identifier)?
PropertyPattern ::= PropertyName ':' Pattern
```

This completes the comprehensive Zom Language Specification. The document covers all major aspects of the language including lexical structure, types, expressions, statements, declarations, patterns, classes, interfaces, enumerations, error handling, generics, modules, memory management, concurrency, attributes, and the complete formal grammar.

The specification provides detailed explanations, extensive examples, and serves as both a reference for language implementers and a guide for developers learning Zom. It follows the style and depth of modern language specifications like those for Swift and Kotlin, ensuring that all language features are thoroughly documented with clear semantics and usage patterns.
