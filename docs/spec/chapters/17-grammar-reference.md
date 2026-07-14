# Grammar Reference

This chapter is the normative EBNF reference for the ZOM language. The lexical
rules in Chapter 02 and `docs/spec/ZomLexer.g4`, the parser grammar in
`docs/spec/ZomParser.g4`, the recursive parser, and the generated AST schema
must remain aligned with these productions.

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
StringLiteral ::= '"' DoubleStringCharacter* '"'
DoubleStringCharacter ::= ~["\\\r\n\u2028\u2029] | LineTerminator | '\\' EscapeSequence | LineContinuation

TemplateLiteral ::= NoSubstitutionTemplateLiteral | TemplateHead TemplateSpan+
TemplateSpan ::= Expression (TemplateMiddle | TemplateTail)

EscapeSequence ::= CharacterEscapeSequence | '0' | HexEscapeSequence | UnicodeEscapeSequence
CharacterEscapeSequence ::= '\\' ["\\bfnrtv]
HexEscapeSequence ::= 'x' HEX_DIGIT HEX_DIGIT
UnicodeEscapeSequence ::= 'u' HEX_DIGIT HEX_DIGIT HEX_DIGIT HEX_DIGIT | 'u{' HEX_DIGIT+ '}'
LineContinuation ::= '\\' LineTerminatorSequence

(* Character Literals *)
CharacterLiteral ::= "'" CharacterLiteralCharacter "'"
CharacterLiteralCharacter ::= ~['\\\r\n\u2028\u2029] | '\\' EscapeSequence | LineContinuation

(* Punctuators *)
Punctuator ::= '{' | '}' | '(' | ')' | '[' | ']' | '.' | '...' | '#' | '@'
            | ';' | ',' | ':' | '?'
            | '::'
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
SourceFile ::= Shebang? ModuleDeclaration?
               ModuleItem* EOF

OuterAttributeList ::= OuterAttribute*
OuterAttribute ::= '#' '[' AttributeEntry
                       (',' AttributeEntry)* ','? ']'
AttributeEntry ::= AttributePath AttributePayload?
AttributePayload ::= '(' AttributeInput? ')' | '=' Expression
AttributeInput ::= AttributeInputItem (',' AttributeInputItem)* ','?
AttributeInputItem ::= IdentifierName '=' AttributeInputValue
                     | AttributeInputValue
AttributeInputValue ::= Expression | '{' AttributeInput? '}'
AttributePath ::= IdentifierName ('::' IdentifierName)+
                | BuiltinSingleSegmentAttribute
BuiltinSingleSegmentAttribute ::= 'inline' | 'deprecated' | 'cold' | 'repr'
ModifierList ::= ModifierKeyword*
ModifierKeyword ::= 'public' | 'private' | 'protected' | 'static'
                  | 'readonly' | 'mutating' | 'override' | 'abstract'
                  | 'export'

ModuleDeclaration ::= 'module' Identifier ';'
                    | 'module' Identifier '{' ModuleItem* '}'
                    | 'export'? 'module' Identifier '=' ModuleAliasPath ';'
ModuleItem ::= StatementListItem
ModuleAliasPath ::= Identifier ('::' Identifier)+
QualifiedModulePath ::= Identifier ('::' Identifier)+
GroupBasePath ::= Identifier ('::' Identifier)*

ImportDeclaration ::= 'import' ImportClause ';'
ImportClause ::= NamedImportClause | ModuleImportClause
ModuleImportClause ::= QualifiedModulePath ('as' Identifier)?
NamedImportClause ::= GroupBasePath '::' '{' ImportSpecifierList? '}'
ImportSpecifierList ::= ImportSpecifier (',' ImportSpecifier)* ','?
ImportSpecifier ::= Identifier ('as' Identifier)?

ExportDeclaration ::= 'export' Declaration
                    | 'export' ExportClause ';'
ExportClause ::= LocalExportClause | ReexportClause
LocalExportClause ::= '{' ExportSpecifierList? '}'
ReexportClause ::= GroupBasePath '::' '{' ExportSpecifierList? '}'
ExportSpecifierList ::= ExportSpecifier (',' ExportSpecifier)* ','?
ExportSpecifier ::= Identifier ('as' Identifier)?

PathPrefix    ::= 'crate::' | 'self::' | 'super::' | '::'
QualifiedPath ::= PathPrefix? Identifier ( '::' Identifier )*

(* Declarations *)
Declaration ::= MutDeclaration
              | LetDeclaration
              | ConstDeclaration
              | FunctionDeclaration
              | ClassDeclaration
              | StructDeclaration
              | InterfaceDeclaration
              | EnumDeclaration
              | ErrorDeclaration
              | AliasDeclaration
              | ExportDeclaration
              | ImportDeclaration
              | StandaloneImplDeclaration
              | MarkerImplDeclaration
              | ExternBlockDeclaration

MutDeclaration   ::= 'mut' VariableDeclarationList ';'
LetDeclaration   ::= 'let' VariableDeclarationList ';'
ConstDeclaration ::= 'const' ConstDeclarationList ';'
VariableDeclarationList ::= VariableDeclaration (',' VariableDeclaration)*
VariableDeclaration ::= (BindingIdentifier | BindingPattern) TypeAnnotation? Initializer?
Initializer ::= '=' AssignmentExpression
ConstDeclarationList ::= ConstDeclarationItem (',' ConstDeclarationItem)*
ConstDeclarationItem ::= BindingIdentifier TypeAnnotation? '=' ConstExpression
ConstExpression ::= AssignmentExpression
    (* Semantically restricted to expressions accepted by the constant evaluator. *)

(* `mut` and `let` are runtime bindings. Only `mut` may be reassigned or used
   as a mutable place. `const` is a compile-time value and has no stable
   storage address. A `let` field may be definitely assigned by an `init`
   callable that explicitly declares `this`, before that receiver escapes. *)

FunctionDeclaration ::= ModifierList 'fun' BindingIdentifier TypeParameters?
                        FunctionSignature WhereClause? (BlockStatement | ';')
FunctionSignature ::= ParameterClause (ReturnType | RaisesClause)?
ReturnType ::= '->' TypeExpression RaisesClause?

ClassDeclaration ::= ModifierList 'class' BindingIdentifier
                     TypeParameters? (':' TypeExpression)? WhereClause?
                     '{' ClassElement* '}'
    (* Class headers admit at most one superclass after ':'.
       Interface implementations are written only as standalone impl blocks. *)
StructDeclaration ::= ModifierList 'struct' BindingIdentifier TypeParameters?
                      WhereClause? '{' StructElement* '}'
    (* Struct headers do not accept a base type or interface list. *)

InterfaceDeclaration ::= ModifierList 'interface' BindingIdentifier
                         TypeParameters? InterfaceHeritage? '{' InterfaceBody '}'
    (* Interface declarations do not accept WhereClause. Put interface type
       parameter constraints in TypeParameters.
       Canonical grammar: this chapter, InterfaceDeclaration *)
InterfaceHeritage ::= ':' InterfaceBoundList
    (* '+' = conjunction (AND); '|' is ONLY for UnionType.
       Canonical grammar: this chapter, InterfaceHeritage *)
InterfaceBoundList ::= InterfaceBound ( '+' InterfaceBound )*
InterfaceBound ::= QualifiedPathOrIdent ( '<' TypeArgumentList '>' )?
InterfaceBody ::= InterfaceElement*
InterfaceElement ::= ModifierList 'fun' MethodSignature ';'
                   | ModifierList ('get' | 'set') PropertySignature ';'
                   | ModifierList 'type' Identifier TypeParameters?
                     (':' InterfaceBoundList)? ('=' TypeExpression)? ';'
                      (* NOTE: Method bodies (BlockStatement) inside interface
                         declarations are not part of ZOM v1. The parser
                         rejects a block after an interface method signature.
                         See Ch.09 §9.3.1. *)
PropertySignature ::= PropertyName FunctionSignature
MethodSignature ::= PropertyName TypeParameters? FunctionSignature
PropertyStorage ::= 'mut' | 'let'
ConstantDeclaration ::= 'const' BindingIdentifier TypeAnnotation? '=' ConstExpression ';'

ClassElement ::= ModifierList 'fun' MethodDeclaration
               | ModifierList InitDeclaration
               | ModifierList DeinitDeclaration
               | ModifierList 'mut' VariableDeclarationList ';'
               | ModifierList 'let' VariableDeclarationList ';'
               | ModifierList 'const' ConstDeclarationList ';'
               | ModifierList PropertyName ':' TypeExpression Initializer? FieldTerminator
               | AccessorDeclaration
StructElement ::= ModifierList ('mut' | 'readonly')? PropertyName ':' TypeExpression
                  Initializer? FieldTerminator
                | ModifierList 'fun' MethodDeclaration
                | ModifierList InitDeclaration
                | ModifierList DeinitDeclaration
MethodDeclaration ::= PropertyName TypeParameters? FunctionSignature (BlockStatement | ';')
InitDeclaration ::= 'init' ParameterClause RaisesClause? BlockStatement
DeinitDeclaration ::= 'deinit' ParameterClause RaisesClause? BlockStatement
AccessorDeclaration ::= ModifierList 'get' PropertyName FunctionSignature BlockStatement
                        (ModifierList 'set' PropertyName FunctionSignature BlockStatement)?
FieldTerminator ::= ';' | ',' | /* empty */
    (* The empty form is accepted only before the next member or the closing brace. *)

ErrorDeclaration ::= ModifierList 'error' BindingIdentifier '{' StructElement* '}'

EnumDeclaration ::= ModifierList 'enum' BindingIdentifier TypeParameters?
                    '{' EnumBody? '}'
EnumBody ::= EnumMember (',' EnumMember)* ','?
EnumMember ::= Identifier ('(' TypeExpression (',' TypeExpression)* ','? ')')?
               ('=' ConstExpression)?

AliasDeclaration ::= ModifierList 'alias' BindingIdentifier TypeParameters?
                     '=' TypeExpression ';'

ExternBlockDeclaration ::= 'extern' AbiLiteral? '{' ExternItem* '}'
AbiLiteral ::= '"C"' | '"Cdecl"' | '"system"' | '"zom-cdecl"'
ExternItem ::= ExternFunctionDeclaration | ExternVariableDeclaration
ExternFunctionDeclaration ::= 'fun' Identifier FunctionSignature ';'
ExternVariableDeclaration ::= 'variable' Identifier ':' TypeExpression ';'

MarkerPath        ::= Identifier | QualifiedMarkerPath
QualifiedMarkerPath ::= Identifier ( '::' Identifier )+

MarkerImplDeclaration
    ::= 'unsafe'? 'impl' TypeParameters? '!'? MarkerImplPath
        'for' TypeExpression WhereClause? ( '{' StructElement* '}' | ';' )
MarkerImplPath ::= Identifier ('::' Identifier)*

(* ── impl-head disambiguation — MarkerImpl vs ordinary TraitImpl ─────────
   After the keyword 'impl', parser attempts MarkerImplDeclaration FIRST,
   using the following committed prefix:
     (a) after optional impl TypeParameters, '!' present -> definitely marker impl.
         The marker path may be a short prelude name or a qualified path.
     (b) 'unsafe' TypeParameters? '!' -> definitely marker impl.
     (c) otherwise, after optional impl TypeParameters, a path containing
         the `marker` namespace segment (for example `std::marker::Shared`)
         is parsed directly as a marker impl.
     (d) Other positive impl heads fall back to ordinary TraitImplDeclaration
         parsing and are resolved semantically by S1.
   If S1 later finds that a positive impl head is ambiguous between an
   interface and a marker, it emits ZOM0799 AmbiguousMarkerOrTraitImpl with a
   note: "write the marker through its qualified marker namespace path to
   disambiguate."
   ────────────────────────────────────────────────────────────────────── *)

(* Standalone Interface Impl — Ch.09 §7 *)
StandaloneImplDeclaration
    ::= 'unsafe'? 'impl' TypeParameters? InterfaceBoundList 'for' TypeExpression
        WhereClause? '{' ImplMember* '}'
ImplMember ::= ModifierList 'fun' MethodDeclaration
             | AssociatedTypeAssignment
             | 'mut' VariableDeclarationList ';'
             | 'let' VariableDeclarationList ';'
             | 'const' ConstDeclarationList ';'
AssociatedTypeAssignment ::= 'type' Identifier TypeParameters? '=' TypeExpression ';'

(* Type Expressions *)
TypeExpression ::= UnionType
UnionType ::= IntersectionType ('|' IntersectionType)*
IntersectionType ::= PostfixType ('&' PostfixType)*
PostfixType ::= AtomType PostfixTypeSuffix*
PostfixTypeSuffix ::= '[' ']' | '?' | '??'

AtomType ::= ParenthesizedType
          | PredefinedType
          | TypeReference
          | AssociatedTypeProjection
          | ObjectType
          | TupleType
          | FunctionType
          | TypeQuery
          | ReferenceType          (* &T / &mut T — Ch.03 §Reference Types *)
          | RawPointerType         (* *const T / *mut T — Ch.03 §Raw Pointer Types *)
          | DynType                (* existential type — Ch.03 §Existential Types *)
DynType ::= 'dyn' InterfaceType DynAssocBindingArgs? ( '+' MarkerPath )*
    (* Parser AST stores the interface head in DynTypeIfaceList, associated
       type bindings in DynTypeAssocBindingList, and marker suffixes in
       DynTypeMarkerList. *)
DynAssocBindingArgs ::= '<' DynAssocBinding ( ',' DynAssocBinding )* ','? '>'
DynAssocBinding ::= Identifier '=' TypeExpression

ReferenceType ::= '&' ('mut')? TypeExpression
    (* Immutable or mutable reference. Sized = ptr_size.
       &mut T coerces to &T. See Ch.03 §Reference Types. *)

RawPointerType ::= '*' ('const' | 'mut')? TypeExpression
    (* Raw pointer for FFI and unsafe code. Sized = ptr_size.
       Dereference requires unsafe { }. See Ch.03 §Raw Pointer Types. *)

InterfaceType ::= TypeName TypeArguments?
GenericArgs ::= TypeArgumentList

ParenthesizedType ::= '(' TypeExpression ')'
PredefinedType ::= 'i8' | 'i16' | 'i32' | 'i64' | 'u8' | 'u16' | 'u32' | 'u64'
                | 'f32' | 'f64' | 'str' | 'char' | 'bool' | 'never' | 'any'
                | 'null' | 'unit'
TypeReference ::= TypeName TypeArguments?
TypeName ::= QualifiedPath
AssociatedTypeProjection ::= '<' TypeExpression 'as' TypeExpression '>' '::' Identifier
    (* The unqualified spelling T::Item is parsed by TypeReference as a
       two-segment QualifiedPath. The checker resolves it as an associated
       type projection only when the first segment denotes a type or generic
       parameter; longer paths remain ordinary qualified type references. *)
TypeQuery ::= 'typeof' TypeQueryExpression
TypeQueryExpression ::= Identifier ('.' Identifier)*

TupleType ::= '(' TupleElementTypes? ')'
TupleElementTypes ::= TypeExpression (',' TypeExpression)*

FunctionType ::= TypeParameters? FunctionTypeParameterClause '->' TypeExpression RaisesClause?
               | 'fun' TypeParameters? FunctionTypeParameterClause '->' TypeExpression RaisesClause?
FunctionTypeParameterClause ::= '(' FunctionTypeParameterList? ')'
FunctionTypeParameterList ::= TypeExpression (',' TypeExpression)* ','?
ParameterClause ::= '(' ParameterList? ')'
ParameterList ::= Parameter (',' Parameter)* ','?
Parameter ::= OuterAttributeList? Identifier ':' TypeExpression Initializer?
            | OuterAttributeList? 'this' (':' TypeExpression)?
    (* The second form is the only receiver declaration. A containing type or
       impl declaration does not synthesize a receiver. *)
RaisesClause ::= 'raises' TypeExpression
    (* Multiple error types are written as a union type expression.
       Example: 'fun f() -> T raises IoError | ParseError | ZOM80xx'
       Sync requirement: parser's raises clause parses exactly one
       TypeExpression; there is no comma-list or bracket-list form. *)

ObjectType ::= '{' TypeBody? '}'
TypeBody ::= TypeMemberList (';' | ',')?
TypeMemberList ::= TypeMember (';' TypeMember | ',' TypeMember)*
TypeMember ::= ObjectPropertySignature | MethodSignature
ObjectPropertySignature ::= 'mut'? PropertyName '?'? TypeAnnotation

TypeParameters ::= '<' TypeParameterList '>'
TypeParameterList ::= TypeParameter (',' TypeParameter)* ','?
TypeParameter ::= Identifier (':' TypeParameterBoundList)? ('=' TypeExpression)?
TypeParameterBoundList ::= TypeExpression ('+' TypeExpression)*

TypeArguments ::= '<' TypeArgumentList '>'
TypeArgumentList ::= TypeExpression (',' TypeExpression)* ','?
WhereClause ::= 'where' WherePredicate (',' WherePredicate)* ','?
WherePredicate ::= TypeExpression ':' TypeExpression
    (* AST note: declarations that carry TypeParameters and WhereClause store
       both in a GenericParams wrapper. If a declaration has no explicit
       TypeParameters but has WhereClause, the parser still creates
       GenericParams with nparams=0 so the where-clause is preserved. *)

TypeAnnotation ::= ':' TypeExpression
CallSignature ::= TypeParameters? ParameterClause ReturnType?
InterfaceTypeList ::= TypeReference (',' TypeReference)*
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
           | SpawnStatement
           | SuspendStatement
           | ContinueStatement
           | BreakStatement
           | ReturnStatement
           | DebuggerStatement
           | LabeledStatement

VariableStatement ::= ('mut' | 'let') VariableDeclarationList ';'

BlockStatement ::= '{' StatementList? '}'
StatementList ::= StatementListItem+
StatementListItem ::= OuterAttributeList Declaration
                    | OuterAttributeList Statement
                    | Statement
    (* The parser predicate validates which statement forms may carry an
       outer attribute. Modifiers are consumed by their owning declaration. *)

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
DoWhileStatement ::= 'do' Statement 'while' '(' Expression ')' ';'

ForStatement ::= 'for' '(' ForInit? ';' Expression? ';' ForUpdate? ')' Statement
ForInit ::= ('mut' | 'let') VariableDeclarationList | ExpressionList
ForUpdate ::= ExpressionList
ExpressionList ::= Expression (',' Expression)* ','?
ForInStatement ::= 'for' '(' ('mut' | 'let')? Pattern 'in' Expression ')' Statement

SpawnStatement ::= 'spawn' SpawnModifierList? (SpawnBlockStatement | Expression) ';'?
SpawnExpression ::= 'spawn' SpawnModifierList? (SpawnBlockStatement | AssignmentExpression)
SpawnBlockStatement ::= '{' StatementList? Expression? '}'
SpawnModifierList ::= SpawnModifier+
SpawnModifier ::= 'detached'
                | 'blocking'
                | 'priority' '(' ('high' | 'low') ')'

SuspendStatement ::= 'suspend' ';'
                   | 'suspend' 'until' Expression ';'

ContinueStatement ::= 'continue' Identifier? ';'
BreakStatement ::= 'break' Identifier? ';'
ReturnStatement ::= 'return' Expression? ';'

DebuggerStatement ::= 'debugger' ';'
LabeledStatement ::= Identifier ':' LabelTarget
LabelTarget ::= BlockStatement
              | WhileStatement
              | DoWhileStatement
              | ForStatement
              | ForInStatement
              | LabeledStatement
    (* An outer attribute cannot occur between the label colon and its target. *)

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
                       | ('is' TypeExpression)
                       | ('as' ('?' | '!')? TypeExpression))*
ShiftExpression ::= AdditiveExpression (('<<' | '>>' | '>>>') AdditiveExpression)*
AdditiveExpression ::= MultiplicativeExpression (('+' | '-') MultiplicativeExpression)*
MultiplicativeExpression ::= ExponentiationExpression (('*' | '/' | '%') ExponentiationExpression)*
ExponentiationExpression ::= UnaryExpression ('**' ExponentiationExpression)?

UnaryExpression ::= PostfixExpression
                 | UpdateExpression
                 | ('+' | '-' | '!' | '~' | '*' | 'typeof') UnaryExpression
                 | '&' 'mut'? UnaryExpression

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
                  | MemberExpression '.' DeclaredDefinitionName
                  | MemberExpression '::' DeclaredDefinitionName

SuperProperty ::= 'super' '.' DeclaredDefinitionName
SuperCall ::= 'super' Arguments
ImportCall ::= 'import' Arguments

CallExpression ::= MemberExpression TypeArguments? Arguments
                | SuperCall
                | ImportCall
                | CallExpression Arguments
                | CallExpression '[' Expression ']'
                | CallExpression '.' DeclaredDefinitionName
                | CallExpression '::' DeclaredDefinitionName

OptionalExpression ::= (MemberExpression | CallExpression) OptionalChain+
OptionalChain ::= '?.' (DeclaredDefinitionName | '[' Expression ']' | Arguments)
                  (Arguments | '[' Expression ']' | '.' DeclaredDefinitionName)*

DeclaredDefinitionName ::= Identifier | 'init' | 'deinit' | 'get' | 'set' | 'this'

Arguments ::= '(' ArgumentList? ')'
ArgumentList ::= (AssignmentExpression | '...' AssignmentExpression)
                 (',' (AssignmentExpression | '...' AssignmentExpression))* ','?

PrimaryExpression ::= 'this'
                   | Identifier
                   | Literal
                   | ArrayLiteral
                   | ObjectLiteral
                   | StructLiteral
                   | FunctionExpression
                   | SpawnExpression
                   | UnsafeBlockExpression
                   | '(' Expression ')'

UnsafeBlockExpression ::= 'unsafe' BlockStatement
    (* Grants the capability required by raw-pointer operations.
       See Ch.03 §Unsafe Safety Model and Ch.05. *)
(* Statement forms are not alternatives of PrimaryExpression. *)

ArrayLiteral ::= '[' (ElementList)? ']'
ArrayLiteral ::= '[' (ElementList)? ']'
ElementList ::= (AssignmentExpression | '...' AssignmentExpression)
              (',' (AssignmentExpression | '...' AssignmentExpression))* ','?

ObjectLiteral ::= '{' ObjectLiteralElement (',' ObjectLiteralElement)* ','? '}'
ObjectLiteralElement ::= PropertyDefinition
PropertyDefinitionList ::= PropertyDefinition (',' PropertyDefinition)* ','?
PropertyDefinition ::= Identifier
                    | Identifier Initializer
                    | PropertyName ':' Expression
                    | '...' Expression
PropertyName ::= Identifier

StructLiteral ::= TypeReference '{' StructLiteralFields? '}'
StructLiteralFields ::= StructLiteralField (',' StructLiteralField)* ','?
StructLiteralField ::= Identifier ':' Expression
                    | Identifier

FunctionExpression ::= 'fun' TypeParameters? ParameterClause CaptureClause? ReturnType? BlockStatement
                     | LambdaExpression
LambdaExpression ::= ParameterClause ReturnType? '=>' (AssignmentExpression | BlockStatement)
CaptureClause ::= 'use' '[' CaptureList? ']'
CaptureList ::= CaptureElement (',' CaptureElement)* ','?
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

## Module System Grammar (Authoritative)

This section mirrors Chapter 13 and the recursive parser. `module` declarations
use one declared identifier. Imports and re-exports use `::` paths.

```ebnf
ModuleDeclaration ::=
    'module' Identifier ';'
  | 'module' Identifier '{' ModuleItem* '}'
  | 'export'? 'module' Identifier '=' ModuleAliasPath ';'

ModuleAliasPath ::= Identifier ('::' Identifier)+
QualifiedModulePath ::= Identifier ('::' Identifier)+
GroupBasePath ::= Identifier ('::' Identifier)*

ImportDeclaration ::= 'import' ImportClause ';'
ImportClause ::=
    QualifiedModulePath ('as' Identifier)?
  | GroupBasePath '::' '{' ImportSpecifierList? '}'

ImportSpecifierList ::= ImportSpecifier (',' ImportSpecifier)* ','?
ImportSpecifier ::= Identifier ('as' Identifier)?

ExportDeclaration ::=
    'export' Declaration
  | 'export' '{' ExportSpecifierList? '}' ';'
  | 'export' GroupBasePath '::' '{' ExportSpecifierList? '}' ';'

ExportSpecifierList ::= ExportSpecifier (',' ExportSpecifier)* ','?
ExportSpecifier ::= Identifier ('as' Identifier)?
```

| Production | FIRST set |
|---|---|
| `ModuleDeclaration` | `module`, or `export module` for the alias form |
| `ImportDeclaration` | `import` |
| `ExportDeclaration` | `export` |
| `MutDeclaration` | `mut` |
| `LetDeclaration` | `let` |
| `ConstDeclaration` | `const` |
| `ClassDeclaration` | `class` or a valid declaration modifier |
| `InterfaceDeclaration` | `interface` or a valid declaration modifier |

---

This completes the implementation-aligned Zom grammar reference for lexical
structure, types, expressions, statements, declarations, patterns, classes,
interfaces, enumerations, error handling, generics, modules, and the complete
formal grammar.
