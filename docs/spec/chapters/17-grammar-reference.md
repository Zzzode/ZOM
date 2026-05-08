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
NumericLiteral ::= DecimalLiteral | BinaryLiteral | OctalLiteral | HexLiteral | BigIntLiteral

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
BigIntLiteral ::= DecimalDigits 'n'

NumericLiteralSeparator ::= '_'

(* String Literals *)
StringLiteral ::= '"' DoubleStringCharacter* '"' | "'" SingleStringCharacter* "'"
DoubleStringCharacter ::= ~["\\\r\n\u2028\u2029] | LineTerminator | '\\' EscapeSequence | LineContinuation
SingleStringCharacter ::= ~['\\\r\n\u2028\u2029] | LineTerminator | '\\' EscapeSequence | LineContinuation

TemplateLiteral ::= NoSubstitutionTemplateLiteral | TemplateHead TemplateSpan+
TemplateSpan ::= Expression (TemplateMiddle | TemplateTail)

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
            | '&&' | '||' | '??' | '?!' | '!!'
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
ModuleDeclaration ::= 'module' ModuleName ';'
ModuleItem ::= ImportDeclaration | ExportDeclaration | StatementListItem
ModuleName ::= Identifier ('.' Identifier)*

ImportDeclaration ::= 'import' ImportClause ';'
ImportClause ::= NamedImportClause | ModuleImportClause
ModuleImportClause ::= ModuleName ('as' Identifier)?
NamedImportClause ::= ModuleName '.' '{' ImportSpecifierList? '}'
ImportSpecifierList ::= ImportSpecifier (',' ImportSpecifier)* ','?
ImportSpecifier ::= Identifier ('as' Identifier)?

ExportDeclaration ::= 'export' Declaration
                    | 'export' ExportClause ';'
ExportClause ::= LocalExportClause | ReexportClause
LocalExportClause ::= '{' ExportSpecifierList? '}'
ReexportClause ::= ModuleName '.' '{' ExportSpecifierList? '}'
ExportSpecifierList ::= ExportSpecifier (',' ExportSpecifier)* ','?
ExportSpecifier ::= Identifier ('as' Identifier)?

(* Declarations *)
Declaration ::= FunctionDeclaration
             | ClassDeclaration
             | InterfaceDeclaration
             | AliasDeclaration
             | StructDeclaration
             | ErrorDeclaration
             | EnumDeclaration
             | VariableStatement

VariableStatement ::= LetOrConst VariableDeclarationList ';'
LetOrConst ::= 'let' | 'const'
VariableDeclarationList ::= VariableDeclaration (',' VariableDeclaration)*
VariableDeclaration ::= (BindingIdentifier | BindingPattern) TypeAnnotation? Initializer?
Initializer ::= '=' AssignmentExpression

FunctionDeclaration ::= 'fun' BindingIdentifier TypeParameters? ParameterClause
                       ReturnType? BlockStatement
ReturnType ::= '->' TypeExpression RaisesClause?

ClassDeclaration ::= 'class' BindingIdentifier TypeParameters? HeritageClauses?
                    '{' ClassElement* '}'
StructDeclaration ::= 'struct' BindingIdentifier TypeParameters? HeritageClauses?
                     '{' ClassElement* '}'
HeritageClauses ::= HeritageClause+
HeritageClause ::= ('extends' | 'implements') ExpressionWithTypeArguments
                  (',' ExpressionWithTypeArguments)*

InterfaceDeclaration ::= 'interface' BindingIdentifier TypeParameters? InterfaceHeritage? '{' InterfaceBody '}'
InterfaceHeritage ::= 'extends' InterfaceTypeList
InterfaceBody ::= InterfaceElement*
InterfaceElement ::= ';'
                   | Modifier* LetOrConst PropertySignature Initializer? ';'?
                   | Modifier* 'fun' MethodSignature ';'?
PropertySignature ::= PropertyName '?'? TypeAnnotation
MethodSignature ::= PropertyName '?'? TypeParameters? ParameterClause ReturnType?

ClassElement ::= ';'
               | Modifier* InitDeclaration
               | Modifier* DeinitDeclaration
               | Modifier* AccessorDeclaration
               | Modifier* LetOrConst PropertyDeclaration
               | Modifier* 'fun' MethodDeclaration
PropertyDeclaration ::= PropertyName '?'? TypeAnnotation? Initializer? ';'
MethodDeclaration ::= PropertyName '?'? TypeParameters? ParameterClause ReturnType? (BlockStatement | ';')
InitDeclaration ::= 'init' TypeParameters? ParameterClause ReturnType? (BlockStatement | ';')
DeinitDeclaration ::= 'deinit' (BlockStatement | ';')
AccessorDeclaration ::= ('get' | 'set') PropertyName TypeParameters? ParameterClause ReturnType?
                        (BlockStatement | ';')
Modifier ::= 'public' | 'private' | 'protected' | 'static' | 'readonly' | 'mutating' | 'override'

ErrorDeclaration ::= 'error' BindingIdentifier '{' StatementList? '}'

EnumDeclaration ::= 'enum' BindingIdentifier '{' EnumBody? '}'
EnumBody ::= EnumMember (',' EnumMember)*
EnumMember ::= PropertyName (('=' Expression) | TupleType)?

AliasDeclaration ::= 'alias' BindingIdentifier TypeParameters? '=' TypeExpression ';'

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
PredefinedType ::= 'i8' | 'i16' | 'i32' | 'i64' | 'u8' | 'u16' | 'u32' | 'u64'
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

FunctionType ::= TypeParameters? ParameterClause '->' TypeExpression RaisesClause?
ParameterClause ::= '(' ParameterList? ')'
RaisesClause ::= 'raises' TypeList

ObjectType ::= '{' TypeBody? '}'
TypeBody ::= TypeMemberList (';' | ',')?
TypeMemberList ::= TypeMember (';' TypeMember | ',' TypeMember)*
TypeMember ::= PropertySignature | MethodSignature

TypeParameters ::= '<' TypeParameterList '>'
TypeParameterList ::= TypeParameter (',' TypeParameter)*
TypeParameter ::= Identifier Constraint?
Constraint ::= 'extends' TypeExpression

TypeArguments ::= '<' TypeArgumentList '>'
TypeArgumentList ::= TypeExpression (',' TypeExpression)*

TypeAnnotation ::= ':' TypeExpression
CallSignature ::= TypeParameters? ParameterClause ReturnType?
InterfaceTypeList ::= TypeReference (',' TypeReference)*
TypeList ::= TypeExpression (',' TypeExpression)*
BindingIdentifier ::= Identifier
BindingPattern ::= ArrayBindingPattern | ObjectBindingPattern
ArrayBindingPattern ::= '[' BindingElementList? ']'
ObjectBindingPattern ::= '{' BindingPropertyList? '}'
BindingElementList ::= BindingElement (',' BindingElement)* ','?
BindingPropertyList ::= BindingProperty (',' BindingProperty)* ','?
BindingElement ::= '...'? (BindingIdentifier | BindingPattern) Initializer?
BindingProperty ::= '...'? (BindingIdentifier | PropertyName ':' BindingElement) Initializer?
ExpressionWithTypeArguments ::= LeftHandSideExpression TypeArguments?

(* Statements *)
Statement ::= BlockStatement
           | EmptyStatement
           | VariableStatement
           | ExpressionStatement
           | IfStatement
           | MatchStatement
           | WhileStatement
           | DoWhileStatement
           | ForStatement
           | ForInStatement
           | ContinueStatement
           | BreakStatement
           | ReturnStatement
           | DebuggerStatement
           | LabeledStatement

BlockStatement ::= '{' StatementList? '}'
StatementList ::= StatementListItem+
StatementListItem ::= Statement | Declaration

EmptyStatement ::= ';'

ExpressionStatement ::= Expression ';'

IfStatement ::= 'if' '(' Expression ')' Statement ('else' Statement)?

MatchStatement ::= 'match' '(' Expression ')' MatchBlock
MatchBlock ::= '{' MatchClause* DefaultClause? '}'
MatchClause ::= 'when' Pattern GuardClause? '=>' Statement
DefaultClause ::= 'default' '=>' StatementList
GuardClause ::= 'if' Expression

WhileStatement ::= 'while' '(' Expression ')' Statement
DoWhileStatement ::= 'do' Statement 'while' '(' Expression ')' ';'?

ForStatement ::= 'for' '(' ForInit? ';' Expression? ';' ForUpdate? ')' Statement
ForInStatement ::= 'for' '(' (ForDeclaration | LeftHandSideExpression) 'in' Expression ')' Statement
ForDeclaration ::= ('let' | 'const') ForBinding
ForBinding ::= BindingIdentifier | BindingPattern
ForInit ::= LetOrConst VariableDeclarationList | Expression
ForUpdate ::= Expression

ContinueStatement ::= 'continue' Identifier? ';'
BreakStatement ::= 'break' Identifier? ';'
ReturnStatement ::= 'return' Expression? ';'

DebuggerStatement ::= 'debugger' ';'
LabeledStatement ::= Identifier ':' Statement

(* Expressions *)
Expression ::= AssignmentExpression (',' AssignmentExpression)*
AssignmentExpression ::= ConditionalExpression
                      | FunctionExpression
                      | LeftHandSideExpression AssignmentOperator AssignmentExpression
AssignmentOperator ::= '=' | '+=' | '-=' | '*=' | '/=' | '%=' | '**='
                     | '<<=' | '>>=' | '>>>=' | '&=' | '|=' | '^='
                     | '&&=' | '||=' | '??='

ConditionalExpression ::= ErrorDefaultExpression ('?' AssignmentExpression ':' AssignmentExpression)?

ErrorDefaultExpression ::= CoalesceExpression (ErrorDefaultOperator CoalesceExpression)*
ErrorDefaultOperator ::= '?:'
                       (* parsed as adjacent '?' ':' tokens with no whitespace between them *)
CoalesceExpression ::= LogicalORExpression ('??' LogicalORExpression)*
LogicalORExpression ::= LogicalANDExpression ('||' LogicalANDExpression)*
LogicalANDExpression ::= BitwiseORExpression ('&&' BitwiseORExpression)*
BitwiseORExpression ::= BitwiseXORExpression ('|' BitwiseXORExpression)*
BitwiseXORExpression ::= BitwiseANDExpression ('^' BitwiseANDExpression)*
BitwiseANDExpression ::= EqualityExpression ('&' EqualityExpression)*
EqualityExpression ::= RelationalExpression (('==' | '!=' | '===' | '!==') RelationalExpression)*
RelationalExpression ::= ShiftExpression ((('<' | '>' | '<=' | '>=') ShiftExpression)
                       | ('as' ('?' | '!')? TypeExpression))*
ShiftExpression ::= AdditiveExpression (('<<' | '>>' | '>>>') AdditiveExpression)*
AdditiveExpression ::= MultiplicativeExpression (('+' | '-') MultiplicativeExpression)*
MultiplicativeExpression ::= ExponentiationExpression (('*' | '/' | '%') ExponentiationExpression)*
ExponentiationExpression ::= UnaryExpression ('**' ExponentiationExpression)?

UnaryExpression ::= PostfixExpression
                 | UpdateExpression
                 | ('+' | '-' | '!' | '~' | 'typeof') UnaryExpression

PostfixExpression ::= LeftHandSideExpression PostfixSuffix*
PostfixSuffix ::= '?!' | '!!' | '++' | '--'
UpdateExpression ::= ('++' | '--') LeftHandSideExpression
                 | LeftHandSideExpression ('++' | '--')

LeftHandSideExpression ::= NewExpression
                        | CallExpression
                        | MemberExpression
                        | OptionalExpression

NewExpression ::= MemberExpression
                | 'new' NewExpression

MemberExpression ::= PrimaryExpression
                  | SuperProperty
                  | 'new' MemberExpression Arguments
                  | MemberExpression '[' Expression ']'
                  | MemberExpression '.' Identifier

SuperProperty ::= 'super' '.' Identifier
SuperCall ::= 'super' Arguments
ImportCall ::= 'import' Arguments

CallExpression ::= MemberExpression Arguments
                | SuperCall
                | ImportCall
                | CallExpression Arguments
                | CallExpression '[' Expression ']'
                | CallExpression '.' Identifier

OptionalExpression ::= (MemberExpression | CallExpression) OptionalChain+
OptionalChain ::= '?.' (Identifier | '[' Expression ']' | Arguments)
                  (Arguments | '[' Expression ']' | '.' Identifier)*

Arguments ::= '(' ArgumentList? ')'
ArgumentList ::= (AssignmentExpression | '...' AssignmentExpression)
                 (',' (AssignmentExpression | '...' AssignmentExpression))* ','?

PrimaryExpression ::= 'this'
                   | Identifier
                   | Literal
                   | ArrayLiteral
                   | ObjectLiteral
                   | FunctionExpression
                   | '(' Expression ')'

ArrayLiteral ::= '[' (ElementList)? ']'
ElementList ::= (AssignmentExpression | '...' AssignmentExpression)
              (',' (AssignmentExpression | '...' AssignmentExpression))* ','?

ObjectLiteral ::= '{' (PropertyDefinitionList)? '}'
PropertyDefinitionList ::= PropertyDefinition (',' PropertyDefinition)* ','?
PropertyDefinition ::= Identifier
                    | Identifier Initializer
                    | PropertyName ':' Expression
                    | '...' Expression
PropertyName ::= Identifier

FunctionExpression ::= 'fun' TypeParameters? ParameterClause CaptureClause? ReturnType? BlockStatement
CaptureClause ::= 'use' '[' CaptureList? ']'
CaptureList ::= CaptureElement (',' CaptureElement)*
CaptureElement ::= '&'? Identifier | 'this'

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

WildcardPattern ::= '_' TypeAnnotation?
IdentifierPattern ::= Identifier TypeAnnotation?
TuplePattern ::= '(' PatternList? ')'
PatternList ::= Pattern (',' Pattern)* ','?
StructurePattern ::= '{' PatternPropertyList? '}'
PatternPropertyList ::= PatternProperty (',' PatternProperty)* ','?
PatternProperty ::= PropertyName (':' TypeExpression)?
ArrayPattern ::= '[' PatternList? ']'
IsPattern ::= 'is' TypeExpression
ExpressionPattern ::= Expression
EnumPattern ::= PropertyName TuplePattern
              | TypeReference '.' PropertyName TuplePattern?
```

This completes the implementation-aligned Zom grammar reference for lexical structure, types, expressions, statements, declarations, patterns, classes, interfaces, enumerations, error handling, generics, modules, and the complete formal grammar.

The specification provides detailed explanations, extensive examples, and serves as both a reference for language implementers and a guide for developers learning Zom. It follows the style and depth of modern language specifications like those for Swift and Kotlin, ensuring that all language features are thoroughly documented with clear semantics and usage patterns.
