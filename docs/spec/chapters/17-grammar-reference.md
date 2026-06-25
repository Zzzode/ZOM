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
Punctuator ::= '::'                    (namespace separator, new SyntaxKind
                                          ColonColon — Ch.16 §16.3.1; NO
                                          whitespace between the two colons;
                                          ': :' with space → ZOM0620)
            | '{' | '}' | '(' | ')' | '[' | ']' | '.' | '...' | ';' | ',' | ':' | '?'
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
SourceFile ::= Shebang? InnerAttributeList? ModuleDeclaration?
               ModuleItem* EOF

InnerAttributeList ::= InnerAttribute*
InnerAttribute     ::= '#' '!' '[' AttrEntry ']'      (see Ch.16 §16.2 A-005)
OuterAttribute     ::= '#' '[' AttrEntry ']'          (see Ch.16 §16.2 A-004)
ModifierList       ::= ( OuterAttribute | ModifierKeyword )*

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
Declaration ::= ModifierList (
                   LetDeclaration
                 | ConstDeclaration
                 | FunDeclaration
                 | ClassDeclaration
                 | StructDeclaration
                 | InterfaceDeclaration
                 | EnumDeclaration
                 | ErrorDeclaration
                 | AliasDeclaration
                 | ModuleDeclaration
                 | ExportDeclaration
                 | ImportDeclaration
                 | MarkerDeclaration
                 | MarkerImplDeclaration
               )

LetDeclaration   ::= 'let' VariableDeclarationList ';'
ConstDeclaration ::= 'const' VariableDeclarationList ';'
FunDeclaration   ::= 'fun' BindingIdentifier TypeParameters? ParameterClause
                     ReturnType? BlockStatement
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
HeritageClause ::= 'extends' ExpressionWithTypeArguments
                  (',' ExpressionWithTypeArguments)*

InterfaceDeclaration ::= 'interface' BindingIdentifier TypeParameters? InterfaceHeritage? '{' InterfaceBody '}'
InterfaceHeritage ::= 'extends' InterfaceTypeList
InterfaceBody ::= InterfaceElement*
InterfaceElement ::= ';'
                   | Modifier* LetOrConst PropertySignature Initializer? ';'?
                   | Modifier* 'fun' MethodSignature ( BlockStatement | ';' )?
                      (* BlockStatement = default method body — Ch.09 §6 *)
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
ModifierKeyword ::= 'public' | 'private' | 'protected'
                  | 'static' | 'readonly' | 'mutating' | 'override'
                  | 'export'
                  | 'unsafe'              (marker impl prefix, Ch.16 A-019)
                  | 'marker'              (contextual, Ch.16 A-017)

ErrorDeclaration ::= 'error' BindingIdentifier '{' StatementList? '}'

EnumDeclaration ::= 'enum' BindingIdentifier '{' EnumBody? '}'
EnumBody ::= EnumMember (',' EnumMember)*
EnumMember ::= PropertyName (('=' Expression) | TupleType)?

AliasDeclaration ::= 'alias' BindingIdentifier TypeParameters? '=' TypeExpression ';'

(* Marker Declarations — Ch.16 §16.14.2 (four styles) *)
MarkerDeclaration
    ::= ( 'auto' | 'unsafe' )? 'marker' Identifier TypeParameters?
        ( '=' MarkerConjunction )? ';'

MarkerConjunction ::= MarkerPath ( '+' MarkerPath )*
MarkerPath        ::= '!'? ( Identifier | QualifiedMarkerPath )
QualifiedMarkerPath ::= Identifier ( '::' Identifier )+

MarkerImplDeclaration
    ::= 'unsafe'? 'impl' '!'? attributePath typeArguments?
        'for' TypeExpression whereClause? ( structBody | ';' )

(* ── impl-head disambiguation — MarkerImpl vs ordinary TraitImpl ─────────
   After the keyword 'impl', parser attempts MarkerImplDeclaration FIRST,
   using the following committed prefix:
     (a) '!' present                 → definitely marker impl.
     (b) 'unsafe' '!'                → definitely marker impl.
     (c) otherwise, try to parse an attributePath enforcing ≥2 segments
         (Ch.16 §16.3.7 hard rule — ≥2 segments for all namespaced attrs).
         If ≥2-segment path is present AND keyword 'for' is found after
         the path's optional typeArguments → marker impl.
     (d) If (c) fails (path is 1-segment, or no 'for' follows, or path is
         not a marker in the parser-visible prelude bitmap) → fall back
         to ordinary TraitImplDeclaration parsing.
   Fallback records the two alternatives in a disambiguation side-channel
   for S1; name resolution picks the valid one; if BOTH are valid after
   resolution → ZOM0799 AmbiguousMarkerOrTraitImpl with a note: "use
   'marker impl' prefix or qualify the marker to ≥2 segments to disambiguate."
   ────────────────────────────────────────────────────────────────────── *)

(* Standalone Interface Impl — Ch.09 §7 *)
StandaloneImplDeclaration
    ::= 'impl' TypeArguments? InterfaceType ( '+' MarkerPath )* 'for' TypeExpression
        whereClause? '{' ( MethodDeclaration | AssociatedTypeAssignment )* '}'
AssociatedTypeAssignment ::= 'type' Identifier TypeParameters? '=' TypeExpression ';'

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
          | DynType            (* existential type — Ch.03 §X *)
DynType ::= 'dyn' InterfaceType ( '+' MarkerPath ( '+' MarkerPath )* )?
InterfaceType ::= TypeName TypeArguments?

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
TypeList     ::= TypeExpression ( ',' TypeExpression )* ','?
    (* RaisesClause allows a comma-separated set of error types.
       Example: 'fun f() -> T raises IoError, ParseError, ZOM80xx'
       Sync requirement: parser's parseRaisesClause parses as a
       comma-list (min size = 1), not a single Type. *)

ObjectType ::= '{' TypeBody? '}'
TypeBody ::= TypeMemberList (';' | ',')?
TypeMemberList ::= TypeMember (';' TypeMember | ',' TypeMember)*
TypeMember ::= PropertySignature | MethodSignature

TypeParameters ::= '<' TypeParameterList '>'
TypeParameterList ::= TypeParameter (',' TypeParameter)*
TypeParameter  ::= Identifier ( ':' BoundItem ( '+' BoundItem )* )? ','?
BoundItem      ::= '!'? ( TypeExpression | MarkerPath )
    (* Example: <T: std::marker::Sendable + !std::marker::Shared,
                 U: 'static + Linear>
       Marker negation (!) is allowed — aligned with Ch.16 A-023.
       The 'extends' keyword is retained for backward compatibility
       (equivalent to ':'), but marker bindings MUST use ':' with
       '+' separators and '!' for negation. *)

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
StatementListItem ::= ModifierList ( Statement | Declaration )
    (* NOTE: Hash ∈ FIRST(StatementListItem) always denotes a ModifierList
       containing an OuterAttribute. Hash is NEVER the start of an
       attributeAnnotatedExpression at statement head. This is the
       canonical S/R resolution for the "2 consecutive items with
       leading attrs" scenario. *)

EmptyStatement ::= ';'

ExpressionStatement ::= !OuterAttrStart AssignmentExpression ';'
    (* OuterAttrStart = syntactic predicate: current token == Hash
                         AND peek(1) == LeftBracket
       (Negative lookahead.) Expression statements CANNOT begin with
       an attribute; such a form is routed to StatementListItem instead. *)

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
    with the following co-normative FIRST/FOLLOW constraints:

    (a) Contextual-priority: at the start of ANY StatementListItem, try
        markerDeclaration (look for 'marker' + Identifier | '!') FIRST.
        Only if that fails (recoverable) fall back to LabeledStatement.
        This prevents 'marker: loop { break marker; }' from being parsed
        as a marker declaration with a recovery error at ': loop'.

    (b) Label FOLLOW set restriction: The 'Statement' child of a
        LabeledStatement MUST have
        FIRST(Statement) ∈ { if, match, while, do, for, loop, '{', ';' }
        ∪ { Identifier ∈ {loop, while, for, match, if, block, switch} }.
        In particular, 'label: fun f(){}' or 'label: struct S{}' are
        SYNTAX ERRORS (declarations are not statements).

    (c) Outer-attr insertion forbidden between label ':' and Statement:
        After the ':' of a label, a Hash starting an OuterAttribute is
        ZOM0602 with diagnostic: "Attach attributes BEFORE the label
        (e.g. '#[attr] label: loop …') or INSIDE the controlled statement
        body."
        Rationale: Without (c),
            loop1: #[zom::hint::unroll]
            for x in xs { … }
        produces two structurally different ASTs that are semantically
        equivalent but differ on pretty-print / LSP incremental node
        identity.

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

(* ── Control-flow exclusions from PrimaryExpression ─────────────────────
   The following statement-level forms are NOT primary expressions and
   CANNOT appear in expression position:
       whileStatement, doWhileStatement, forStatement, forInStatement,
       forOfStatement, doStatement, returnStatement, breakStatement,
       continueStatement, debuggerStatement.
   Hence 'return #[zom::hint::unroll] while COND { BODY }' is syntactically
   impossible: the while is a Statement, not a primary expression, so the
   '#[zom::hint::unroll]' attaches to the while-Statement via its
   ModifierList slot (parse-tree B), NEVER as an attributeAnnotatedExpression.
   This matches Ch.16 A-026a (contextual attachment disambiguation).
   ────────────────────────────────────────────────────────────────────── *)

ArrayLiteral ::= '[' (ElementList)? ']'
ElementList ::= (AssignmentExpression | '...' AssignmentExpression)
              (',' (AssignmentExpression | '...' AssignmentExpression))* ','?

ObjectLiteral ::= '{' ObjectLiteralElement (',' ObjectLiteralElement)* ','? '}'
    (* Constraint: the Identifier of an ObjectLiteralElement in
       statement position may NOT be one of:
         struct | class | fun | enum | marker | alias | error | interface
       (i.e. any contextual declaration keyword.)
       This disambiguates struct-declaration-vs-object-literal when
       combined with the statement-head Hash rule above. *)
ObjectLiteralElement ::= PropertyDefinition
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
