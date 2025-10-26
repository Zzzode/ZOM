// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#pragma once

namespace zomlang {
namespace compiler {
namespace ast {

/// \brief Unified syntax element kind enumeration for both tokens and AST nodes
/// This replaces the separate SyntaxKind and SyntaxKind enums to eliminate duplication
/// and provide a consistent representation across lexer and parser phases.
enum class SyntaxKind {
  Unknown,

  // ================================================================================
  // LEXICAL TOKENS
  // ================================================================================

  // Literals
  CharacterLiteral,

  // ================================================================================
  // KEYWORDS
  // ================================================================================

  // Control flow keywords
  AbstractKeyword,     // abstract
  AccessorKeyword,     // accessor
  AnyKeyword,          // any
  AsKeyword,           // as
  AssertsKeyword,      // asserts
  AssertKeyword,       // assert
  AsyncKeyword,        // async
  AwaitKeyword,        // await
  BigIntKeyword,       // bigint
  BreakKeyword,        // break
  CaseKeyword,         // case
  CatchKeyword,        // catch
  ClassKeyword,        // class
  ContinueKeyword,     // continue
  ConstKeyword,        // const
  ConstructorKeyword,  // constructor
  DebuggerKeyword,     // debugger
  DeclareKeyword,      // declare
  DefaultKeyword,      // default
  DeleteKeyword,       // delete
  DoKeyword,           // do
  ElseKeyword,         // else
  ExtendsKeyword,      // extends
  ExportKeyword,       // export
  FinallyKeyword,      // finally
  ForKeyword,          // for
  FromKeyword,         // from
  FunKeyword,          // fun
  GetKeyword,          // get
  GlobalKeyword,       // global
  IfKeyword,           // if
  ImmediateKeyword,    // immediate
  ImplementsKeyword,   // implements
  ImportKeyword,       // import
  InKeyword,           // in
  InferKeyword,        // infer
  InstanceOfKeyword,   // instanceof
  InterfaceKeyword,    // interface
  IntrinsicKeyword,    // intrinsic
  IsKeyword,           // is
  KeyOfKeyword,        // keyof
  LetKeyword,          // let
  MatchKeyword,        // match
  ModuleKeyword,       // module
  MutableKeyword,      // mutable
  NamespaceKeyword,    // namespace
  NeverKeyword,        // never
  NewKeyword,          // new
  ObjectKeyword,       // object
  OfKeyword,           // of
  OptionalKeyword,     // optional
  OutKeyword,          // out
  OverrideKeyword,     // override
  PackageKeyword,      // package
  PrivateKeyword,      // private
  ProtectedKeyword,    // protected
  PublicKeyword,       // public
  ReadonlyKeyword,     // readonly
  RequireKeyword,      // require
  ReturnKeyword,       // return
  SatisfiesKeyword,    // satisfies
  SetKeyword,          // set
  StaticKeyword,       // static
  SuperKeyword,        // super
  SwitchKeyword,       // switch
  SymbolKeyword,       // symbol
  ThisKeyword,         // this
  ThrowKeyword,        // throw
  TryKeyword,          // try
  TypeOfKeyword,       // typeof
  UndefinedKeyword,    // undefined
  UniqueKeyword,       // unique
  UsingKeyword,        // using
  VarKeyword,          // var
  VoidKeyword,         // void
  WhenKeyword,         // when
  WhileKeyword,        // while
  WithKeyword,         // with
  YieldKeyword,        // yield

  // Type keywords
  BoolKeyword,    // bool
  I8Keyword,      // i8
  I16Keyword,     // i16
  I32Keyword,     // i32
  I64Keyword,     // i64
  U8Keyword,      // u8
  U16Keyword,     // u16
  U32Keyword,     // u32
  U64Keyword,     // u64
  F32Keyword,     // f32
  F64Keyword,     // f64
  StrKeyword,     // str
  UnitKeyword,    // unit
  StructKeyword,  // struct
  EnumKeyword,    // enum
  ErrorKeyword,   // error
  AliasKeyword,   // alias
  InitKeyword,    // init
  DeinitKeyword,  // deinit
  RaisesKeyword,  // raises
  TypeKeyword,    // type

  // Boolean and null literals
  TrueKeyword,   // true
  FalseKeyword,  // false
  NullKeyword,   // null

  // ================================================================================
  // OPERATORS
  // ================================================================================

  // Basic operators
  Arrow,      // ->
  Colon,      // :
  Period,     // .
  DotDotDot,  // ...

  // Comparison operators
  LessThan,                 // <
  GreaterThan,              // >
  LessThanEquals,           // <=
  GreaterThanEquals,        // >=
  EqualsEquals,             // ==
  ExclamationEquals,        // !=
  EqualsEqualsEquals,       // ===
  ExclamationEqualsEquals,  // !==
  EqualsGreaterThan,        // =>

  // Arithmetic operators
  Plus,              // +
  Minus,             // -
  AsteriskAsterisk,  // **
  Asterisk,          // *
  Slash,             // /
  Percent,           // %
  PlusPlus,          // ++
  MinusMinus,        // --

  // Shift operators
  LessThanLessThan,                   // <<
  LessThanSlash,                      // </
  GreaterThanGreaterThan,             // >>
  GreaterThanGreaterThanGreaterThan,  // >>>

  // Bitwise operators
  Ampersand,    // &
  Bar,          // |
  Caret,        // ^
  Exclamation,  // !
  Tilde,        // ~

  // Logical operators
  AmpersandAmpersand,  // &&
  BarBar,              // ||

  // Conditional operators
  Question,          // ?
  QuestionQuestion,  // ??
  QuestionDot,       // ?.

  // Assignment operators
  Equals,                                   // =
  PlusEquals,                               // +=
  MinusEquals,                              // -=
  AsteriskEquals,                           // *=
  AsteriskAsteriskEquals,                   // **=
  SlashEquals,                              // /=
  PercentEquals,                            // %=
  LessThanLessThanEquals,                   // <<=
  GreaterThanGreaterThanEquals,             // >>=
  GreaterThanGreaterThanGreaterThanEquals,  // >>>=
  AmpersandEquals,                          // &=
  BarEquals,                                // |=
  CaretEquals,                              // ^=
  BarBarEquals,                             // ||=
  AmpersandAmpersandEquals,                 // &&=
  QuestionQuestionEquals,                   // ??=

  // Error handling operators
  ErrorPropagate,  // ?!
  ErrorUnwrap,     // !!
  ErrorDefault,    // ?:

  // Special operators
  At,          // @
  Hash,        // #
  Backtick,    // `
  Underscore,  // _

  // ================================================================================
  // PUNCTUATION
  // ================================================================================

  LeftParen,     // (
  RightParen,    // )
  LeftBrace,     // {
  RightBrace,    // }
  Semicolon,     // ;
  Comma,         // ,
  LeftBracket,   // [
  RightBracket,  // ]

  // ================================================================================
  // SPECIAL TOKENS
  // ================================================================================

  Comment,
  EndOfFile,

// ================================================================================
// AST NODES (Generated from ast-nodes.def)
// ================================================================================

// Generate SyntaxKind enum values for element nodes only
#define AST_ELEMENT_NODE(Class) Class,
#define AST_INTERFACE_NODE(Class)  // Skip interface nodes
#include "zomlang/compiler/ast/ast-nodes.def"
#undef AST_ELEMENT_NODE
#undef AST_INTERFACE_NODE

  // Additional nodes not covered by ast-nodes.def
  UpdateExpression,
  CastExpression,
  ExponentiationExpression,
  MultiplicativeExpression,
  AdditiveExpression,
  ShiftExpression,
  RelationalExpression,
  EqualityExpression,
  BitwiseAndExpression,
  BitwiseXorExpression,
  BitwiseOrExpression,
  LogicalAndExpression,
  LogicalOrExpression,
  CoalesceExpression,
  ShortCircuitExpression,

  PostfixType,
  TypeAnnotation,

  GuardClause,
  PropertyDefinition,
  SpreadElement,
  ElementList,
  ArgumentList,
  BindingPattern,
  Initializer,
  TypeArguments,
  TypeArgumentList,
  CallSignature,
  ParameterList,
  FunctionBody,
  ClassElement,
  InterfaceElement,
  StructMember,
  ErrorMember,
  EnumMember,
  AccessibilityModifier,
  InitDeclaration,
  DeinitDeclaration,
  GetAccessor,
  SetAccessor,
  MemberVariableDeclaration,
  MemberFunctionDeclaration,
  MemberAccessorDeclaration,
  PropertyMemberDeclaration,
  ClassHeritage,
  InterfaceHeritage,
  ErrorReturnClause,
  RaisesClause,
  ErrorTypeList,

  Count,

  // ================================================================================
  // RANGE DEFINITIONS
  // ================================================================================

  // Token ranges
  FirstKeyword = AbstractKeyword,
  LastKeyword = NullKeyword,
  FirstReservedWord = AbstractKeyword,
  LastReservedWord = NullKeyword,
  FirstBinaryOperator = LessThan,
  LastBinaryOperator = ErrorDefault,
  FirstPunctuation = LeftParen,
  LastPunctuation = RightBracket,

  // AST node ranges
  FirstStatement = BlockStatement,
  LastStatement = DebuggerStatement,
};

// ================================================================================
// UTILITY FUNCTIONS
// ================================================================================

/// \brief Check if a syntax element kind represents a keyword
inline bool isKeyword(SyntaxKind kind) {
  return kind >= SyntaxKind::FirstKeyword && kind <= SyntaxKind::LastKeyword;
}

/// \brief Check if a syntax element kind represents a reserved keyword
inline bool isReservedKeyword(SyntaxKind kind) {
  return kind >= SyntaxKind::FirstReservedWord && kind <= SyntaxKind::LastReservedWord;
}

/// \brief Check if a syntax element kind represents an identifier or keyword
inline bool isIdentifierOrKeyword(SyntaxKind kind) {
  return kind == SyntaxKind::Identifier || isKeyword(kind);
}

/// \brief Check if a syntax element kind represents punctuation
inline bool isPunctuation(SyntaxKind kind) {
  return kind >= SyntaxKind::FirstPunctuation && kind <= SyntaxKind::LastPunctuation;
}

/// \brief Check if a syntax element kind represents a statement
inline bool isStatement(SyntaxKind kind) {
  return kind >= SyntaxKind::FirstStatement && kind <= SyntaxKind::LastStatement;
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
