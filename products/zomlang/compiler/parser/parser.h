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

#include "zc/core/common.h"
#include "zc/core/function.h"
#include "zc/core/memory.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/statement.h"
#include "zomlang/compiler/ast/type.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/lexer/lexer.h"
#include "zomlang/compiler/lexer/token.h"
#include "zomlang/compiler/source/location.h"

namespace zomlang {
namespace compiler {

namespace basic {
struct LangOptions;
}

namespace diagnostics {
class DiagnosticEngine;
}

namespace source {
class BufferId;
class SourceManager;
}  // namespace source

namespace lexer {
class Token;
}  // namespace lexer

// Forward declarations for AST nodes that will be used by the parser.
// These should be defined in ast.h
namespace ast {
class SourceFile;
class ImplementationModule;
class ImplementationModuleElement;
class ImportDeclaration;
class ExportDeclaration;
class ModulePath;
class ImplementationElement;

class TypeNode;
class TypeReferenceNode;
class ArrayTypeNode;
class UnionTypeNode;
class IntersectionTypeNode;
class ParenthesizedTypeNode;
class PredefinedTypeNode;
class ObjectTypeNode;
class TupleTypeNode;
class FunctionTypeNode;

// Expression forward declarations
class Expression;
class BinaryExpression;
class UnaryExpression;
class ConditionalExpression;
class CallExpression;
class MemberExpression;
class UpdateExpression;
class CastExpression;
class VoidExpression;
class TypeOfExpression;
class AwaitExpression;
class LeftHandSideExpression;
class NewExpression;
class PrimaryExpression;
class ParenthesizedExpression;
class LiteralExpression;
class ArrayLiteralExpression;
class ObjectLiteralExpression;
class Identifier;
class StringLiteral;
class NumericLiteral;
class BooleanLiteral;
class NullLiteral;

enum class OperatorPrecedence : uint8_t;
}  // namespace ast

namespace lexer {
struct LexerState;
}  // namespace lexer

namespace parser {

enum class ParsingContext : uint32_t {
  SourceElements = 0,     // Elements in source file
  BlockElements,          // Statements in block
  MatchClauses,           // Clauses in match statement
  MatchClauseStatements,  // Statements in match clause
  InterfaceMembers,       // Members in interface
  ClassMembers,           // Members in class declaration
  EnumMembers,            // Members in enum declaration
  HeritageClauseElement,  // Elements in a heritage clause
  VariableDeclarations,   // Variable declarations in variable statement
  ObjectBindingElements,  // Binding elements in object binding list
  ArrayBindingElements,   // Binding elements in array binding list
  ArgumentExpressions,    // Expressions in argument list
  ObjectLiteralMembers,   // Members in object literal
  ArrayLiteralMembers,    // Members in array literal
  Parameters,             // Parameters in parameter list
  RestProperties,         // Property names in a rest type list
  TypeParameters,         // Type parameters in type parameter list
  TypeArguments,          // Type arguments in type argument list
  TupleElementTypes,      // Element types in tuple element type list
  HeritageClauses,        // Heritage clauses for a class or interface declaration.
  Count,                  // Number of parsing contexts
};

using ParsingContexts = uint32_t;

inline ParsingContexts operator<<(ParsingContexts a, ParsingContext b) {
  return a << static_cast<uint32_t>(b);
}

/// \brief Parser state for tryParse operations
/// Similar to TypeScript's speculationHelper and tsgo's ParserState
struct ParserState {
  lexer::LexerState lexerState;
  lexer::Token token;

  ParserState(lexer::LexerState lexerState, lexer::Token token)
      : lexerState(lexerState), token(token) {}
};

/// \brief The parser class.
class Parser {
public:
  Parser(const source::SourceManager& sm, diagnostics::DiagnosticEngine& diagnosticEngine,
         const basic::LangOptions& langOpts, basic::StringPool& stringPool,
         const source::BufferId& bufferId);
  ~Parser() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(Parser);

  /// \brief Parse the source file and return the AST.
  /// \return The AST if parsing succeeded, zc::Nothing otherwise.
  zc::Maybe<zc::Own<ast::Node>> parse();

  /// \brief Look ahead n tokens without consuming them
  /// \param n The number of tokens to look ahead (1-based, 1 means next token)
  /// \return The token at position n, or EOF token if beyond end
  lexer::Token lookAhead(unsigned n);

  /// \brief Check if we can look ahead n tokens
  /// \param n The number of tokens to look ahead
  /// \return true if we can look ahead n tokens, false otherwise
  bool canLookAhead(unsigned n);

  /// \brief Look ahead and check if the nth token matches the given kind
  /// \param n The number of tokens to look ahead (1-based)
  /// \param kind The token kind to check for
  /// \return true if the nth token matches the kind, false otherwise
  bool isLookAhead(unsigned n, ast::SyntaxKind kind);

  // Lookahead functions
  template <typename T>
  T lookAhead(zc::Function<T()> callback);

private:
  ZC_ALWAYS_INLINE(diagnostics::DiagnosticEngine& getDiagnosticEngine() const);
  ZC_ALWAYS_INLINE(const source::SourceManager& getSourceManager() const);

  void initializeState();

  ParsingContexts getContext() const;
  void setContext(ParsingContexts value);
  bool isInSomeParsingContext();
  void parsingContextErrors(ParsingContext context);

  /// \brief Save the current parser state
  /// \return A ParserState that can be used to restore the parser later
  ParserState mark();

  /// \brief Restore the parser to a previously saved state
  /// \param state The state to restore to
  void rewind(const ParserState& state);

  ZC_ALWAYS_INLINE(void nextToken());
  ZC_ALWAYS_INLINE(const lexer::Token& currentToken() const);
  ZC_ALWAYS_INLINE(const source::SourceLoc currentLoc() const);
  ZC_NODISCARD source::SourceLoc getFullStartLoc() const;

  ZC_ALWAYS_INLINE(bool expectToken(ast::SyntaxKind kind));
  ZC_ALWAYS_INLINE(bool consumeExpectedToken(ast::SyntaxKind kind));
  bool parseExpected(ast::SyntaxKind kind, bool shouldAdvance = true);
  bool parseOptional(ast::SyntaxKind kind);

  void parseExpectedMatchingBrackets(ast::SyntaxKind openKind, ast::SyntaxKind closeKind,
                                     bool openParsed, source::SourceLoc openPos);

  bool canParseSemicolon() const;
  bool tryParseSemicolon();
  bool parseSemicolon();

  template <ast::NodeLike Node>
  zc::Own<Node> finishNode(zc::Own<Node>&& node, source::SourceLoc pos) {
    const source::SourceLoc end = getFullStartLoc();
    return finishNode(zc::mv(node), zc::mv(pos), zc::mv(end));
  }

  template <ast::NodeLike Node>
  zc::Own<Node> finishNode(zc::Own<Node>&& node, source::SourceLoc pos, source::SourceLoc end) {
    node->setSourceRange(source::SourceRange(pos, end));
    return zc::mv(node);
  }

  bool isListTerminator(ParsingContext context) const;
  bool isListElement(ParsingContext context, bool inErrorRecovery);
  bool abortParsingListOrMoveToNextToken(ParsingContext context);

  template <ast::NodeLike T>
  zc::Vector<zc::Own<T>> parseList(ParsingContext context,
                                   zc::Function<zc::Maybe<zc::Own<T>>()> parseElement) {
    const ParsingContexts saveContext = getContext();
    setContext(saveContext | (1ul << context));
    zc::Vector<zc::Own<T>> result;

    while (!isListTerminator(context)) {
      if (isListElement(context, /*inErrorRecovery*/ false)) {
        ZC_IF_SOME(element, parseElement()) { result.add(zc::mv(element)); }
        continue;
      }

      if (abortParsingListOrMoveToNextToken(context)) { break; }
    }

    setContext(saveContext);
    return result;
  }

  template <ast::NodeLike T>
  zc::Vector<zc::Own<T>> parseDelimitedList(ParsingContext context,
                                            zc::Function<zc::Maybe<zc::Own<T>>()> parseElement,
                                            bool considerSemicolonAsDelimiter = false) {
    const ParsingContexts saveContext = getContext();
    setContext(saveContext | (1ull << context));
    zc::Vector<zc::Own<T>> list;

    while (true) {
      if (isListElement(context, /*inErrorRecovery*/ false)) {
        const source::SourceLoc startPos = currentToken().getLocation();
        ZC_IF_SOME(result, parseElement()) {
          list.add(zc::mv(result));

          if (consumeExpectedToken(ast::SyntaxKind::Comma)) { continue; }

          if (isListTerminator(context)) { break; }

          parseExpected(ast::SyntaxKind::Comma);

          if (considerSemicolonAsDelimiter && expectToken(ast::SyntaxKind::Semicolon) &&
              !currentToken().hasPrecedingLineBreak()) {
            nextToken();
          }
          if (startPos == currentToken().getLocation()) { nextToken(); }
          continue;
        }
      }

      if (isListTerminator(context)) { break; }
      if (abortParsingListOrMoveToNextToken(context)) { break; }
    }

    setContext(saveContext);
    return list;
  }

  template <ast::NodeLike T>
  zc::Vector<zc::Own<T>> parseBracketedList(ParsingContext context,
                                            zc::Function<zc::Maybe<zc::Own<T>>()> parseElement,
                                            ast::SyntaxKind openKind, ast::SyntaxKind closeKind) {
    if (parseExpected(openKind)) {
      auto result = parseDelimitedList<T>(context, zc::mv(parseElement));
      parseExpected(closeKind);
      return result;
    }
    return zc::Vector<zc::Own<T>>();
  }

  /// \brief Report an error at the specified position.
  template <diagnostics::DiagID ID, typename... Args>
  diagnostics::InFlightDiagnostic diagnose(source::SourceLoc loc, Args&&... args) {
    return getDiagnosticEngine().diagnose<ID>(loc, zc::fwd<Args>(args)...);
  }

  /// \brief Report an error at the specified range.
  template <diagnostics::DiagID ID, typename... Args>
  diagnostics::InFlightDiagnostic parseErrorAtRange(source::SourceRange range, Args&&... args) {
    auto diag = getDiagnosticEngine().diagnose<ID>(range.getStart(), zc::fwd<Args>(args)...);
    diag.addRange(source::CharSourceRange(range.getStart(), range.getEnd()));
    return diag;
  }

  /// \brief Report an error at the specified start and end locations.
  template <diagnostics::DiagID ID, typename... Args>
  diagnostics::InFlightDiagnostic parseErrorAt(source::SourceLoc start, source::SourceLoc end,
                                               Args&&... args) {
    return parseErrorAtRange<ID>(source::SourceRange(start, end), zc::fwd<Args>(args)...);
  }

  /// \brief Report an error at the current token.
  template <diagnostics::DiagID ID, typename... Args>
  diagnostics::InFlightDiagnostic parseErrorAtCurrentToken(Args&&... args) {
    return parseErrorAtRange<ID>(currentToken().getRange(), zc::fwd<Args>(args)...);
  }

  /// \brief Report an error if a name is invalid.
  template <diagnostics::DiagID NameDiagnosticID, diagnostics::DiagID BlankDiagnosticID>
  void parseErrorForInvalidName(ast::SyntaxKind tokenIfBlankName) {
    if (currentToken().is(tokenIfBlankName)) {
      parseErrorAtCurrentToken<BlankDiagnosticID>();
    } else {
      parseErrorAtCurrentToken<NameDiagnosticID>(currentToken().getText(getSourceManager()));
    }
  }

  void parseErrorForMissingSemicolonAfter(const ast::Expression& expr);

  // --- Module and Source File ---
  zc::Maybe<zc::Own<ast::SourceFile>> parseSourceFile();
  zc::Maybe<zc::Own<ast::ImportDeclaration>> parseImportDeclaration();
  zc::Maybe<zc::Own<ast::ExportDeclaration>> parseExportDeclaration();
  zc::Maybe<zc::Own<ast::ModulePath>> parseModulePath();
  zc::Maybe<zc::Own<ast::Statement>> parseModuleItem();

  // --- Declarations ---
  zc::Maybe<zc::Own<ast::Statement>> parseDeclaration();
  zc::Maybe<zc::Own<ast::VariableStatement>> parseVariableStatement();
  zc::Maybe<zc::Own<ast::VariableDeclarationList>> parseVariableDeclarationList();
  zc::Maybe<zc::Own<ast::FunctionDeclaration>> parseFunctionDeclaration();

  // Class & Interface
  zc::Maybe<zc::Own<ast::ClassDeclaration>> parseClassDeclaration();
  zc::Maybe<zc::Own<ast::InterfaceDeclaration>> parseInterfaceDeclaration();
  zc::Maybe<zc::Own<ast::StructDeclaration>> parseStructDeclaration();
  zc::Maybe<zc::Own<ast::EnumDeclaration>> parseEnumDeclaration();

  zc::Maybe<zc::Own<ast::ClassElement>> parseClassElement();
  zc::Vector<zc::Own<ast::ClassElement>> parseClassMembers();
  zc::Maybe<zc::Own<ast::ClassElement>> parsePropertyDeclaration();

  zc::Vector<zc::Own<ast::InterfaceElement>> parseInterfaceMembers();
  zc::Maybe<zc::Own<ast::InterfaceElement>> parseInterfaceElement();

  // Other Declarations
  zc::Maybe<zc::Own<ast::ErrorDeclaration>> parseErrorDeclaration();
  zc::Maybe<zc::Own<ast::AliasDeclaration>> parseAliasDeclaration();

  // Modifiers & Heritage
  zc::Vector<ast::SyntaxKind> parseModifiers(bool allowDecorators = true,
                                             bool permitConstAsModifier = true,
                                             bool stopOnStartOfClassStaticBlock = true);
  zc::Maybe<zc::Vector<zc::Own<ast::HeritageClause>>> parseHeritageClauses();
  zc::Maybe<zc::Own<ast::HeritageClause>> parseHeritageClause();

  // --- Statements ---
  zc::Maybe<zc::Own<ast::Statement>> parseStatement();
  zc::Maybe<zc::Own<ast::BlockStatement>> parseBlockStatement();
  zc::Maybe<zc::Own<ast::EmptyStatement>> parseEmptyStatement();
  zc::Maybe<zc::Own<ast::ExpressionStatement>> parseExpressionStatement();
  zc::Maybe<zc::Own<ast::IfStatement>> parseIfStatement();
  zc::Maybe<zc::Own<ast::WhileStatement>> parseWhileStatement();
  zc::Maybe<zc::Own<ast::ForStatement>> parseForStatement();
  zc::Maybe<zc::Own<ast::BreakStatement>> parseBreakStatement();
  zc::Maybe<zc::Own<ast::ContinueStatement>> parseContinueStatement();
  zc::Maybe<zc::Own<ast::ReturnStatement>> parseReturnStatement();
  zc::Maybe<zc::Own<ast::MatchStatement>> parseMatchStatement();
  zc::Maybe<zc::Own<ast::DebuggerStatement>> parseDebuggerStatement();

  // --- Expressions ---
  zc::Maybe<zc::Own<ast::Expression>> parseExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseInitializer();
  zc::Maybe<zc::Own<ast::Expression>> parseAssignmentExpressionOrHigher();

  // Binary Expressions
  zc::Maybe<zc::Own<ast::Expression>> parseBinaryExpressionOrHigher(
      ast::OperatorPrecedence precedence);
  zc::Maybe<zc::Own<ast::Expression>> parseBinaryExpressionRest(
      zc::Own<ast::Expression> leftOperand, ast::OperatorPrecedence precedence,
      source::SourceLoc startLoc);

  zc::Maybe<zc::Own<ast::Expression>> parseUnaryExpressionOrHigher();

  // Conditional & Logical
  zc::Maybe<zc::Own<ast::ConditionalExpression>> parseConditionalExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseConditionalExpressionRest(
      zc::Own<ast::Expression> leftOperand, source::SourceLoc startLoc);
  zc::Maybe<zc::Own<ast::Expression>> parseShortCircuitExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseLogicalOrExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseLogicalAndExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseCoalesceExpression();

  // Bitwise
  zc::Maybe<zc::Own<ast::Expression>> parseBitwiseOrExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseBitwiseXorExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseBitwiseAndExpression();

  // Equality & Relational
  zc::Maybe<zc::Own<ast::Expression>> parseEqualityExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseRelationalExpression();

  // Arithmetic
  zc::Maybe<zc::Own<ast::Expression>> parseShiftExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseAdditiveExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseMultiplicativeExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseExponentiationExpression();

  // Unary & Cast
  zc::Maybe<zc::Own<ast::CastExpression>> parseCastExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseUnaryExpression();
  zc::Maybe<zc::Own<ast::UnaryExpression>> parseSimpleUnaryExpression();
  zc::Maybe<zc::Own<ast::UnaryExpression>> parsePrefixUnaryExpression();
  zc::Maybe<zc::Own<ast::VoidExpression>> parseVoidExpression();
  zc::Maybe<zc::Own<ast::TypeOfExpression>> parseTypeOfExpression();
  zc::Maybe<zc::Own<ast::UpdateExpression>> parseUpdateExpression();

  // Left-Hand Side
  zc::Maybe<zc::Own<ast::LeftHandSideExpression>> parseLeftHandSideExpressionOrHigher();
  zc::Maybe<zc::Own<ast::LeftHandSideExpression>> parseMemberExpressionOrHigher();
  zc::Maybe<zc::Own<ast::LeftHandSideExpression>> parseCallExpressionRest(
      source::SourceLoc loc, zc::Own<ast::LeftHandSideExpression> expr);
  zc::Maybe<zc::Own<ast::LeftHandSideExpression>> parseMemberExpressionRest(
      zc::Own<ast::LeftHandSideExpression> expr, source::SourceLoc startLoc,
      bool allowOptionalChain);
  zc::Maybe<zc::Own<ast::PropertyAccessExpression>> parsePropertyAccessExpressionRest(
      zc::Own<ast::LeftHandSideExpression> expression, bool questionDotToken,
      source::SourceLoc pos);
  zc::Maybe<zc::Own<ast::Identifier>> parseRightSideOfDot(
      bool allowIdentifierNames, bool allowUnicodeEscapeSequenceInIdentifierName);
  zc::Maybe<zc::Own<ast::ElementAccessExpression>> parseElementAccessExpressionRest(
      zc::Own<ast::LeftHandSideExpression> expression, bool questionDotToken,
      source::SourceLoc pos);

  // Others
  zc::Maybe<zc::Own<ast::ExpressionWithTypeArguments>> parseExpressionWithTypeArguments();
  zc::Maybe<zc::Own<ast::MemberExpression>> parseSuperExpression();
  zc::Maybe<zc::Own<ast::MemberExpression>> parseImportCallExpression();
  zc::Maybe<zc::Own<ast::AwaitExpression>> parseAwaitExpression();
  zc::Maybe<zc::Own<ast::LeftHandSideExpression>> parseLeftHandSideExpression();
  zc::Maybe<zc::Own<ast::NewExpression>> parseNewExpression();
  zc::Maybe<zc::Own<ast::PrimaryExpression>> parsePrimaryExpression();
  zc::Maybe<zc::Own<ast::ParenthesizedExpression>> parseParenthesizedExpression();

  // Literals
  zc::Maybe<zc::Own<ast::LiteralExpression>> parseLiteralExpression();
  zc::Maybe<zc::Own<ast::ArrayLiteralExpression>> parseArrayLiteralExpression();
  zc::Maybe<zc::Own<ast::ObjectLiteralExpression>> parseObjectLiteralExpression();
  zc::Maybe<zc::Own<ast::StringLiteral>> parseStringLiteral();
  zc::Maybe<zc::Own<ast::IntegerLiteral>> parseIntegerLiteral();
  zc::Maybe<zc::Own<ast::FloatLiteral>> parseFloatLiteral();
  zc::Maybe<zc::Own<ast::BooleanLiteral>> parseBooleanLiteral();
  zc::Maybe<zc::Own<ast::NullLiteral>> parseNullLiteral();

  zc::Maybe<zc::Own<ast::FunctionExpression>> parseFunctionExpression();
  zc::Maybe<zc::Own<ast::Identifier>> parseIdentifierExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseErrorDefaultExpression();

  // --- Types ---
  zc::Maybe<zc::Own<ast::TypeNode>> parseType();
  zc::Maybe<zc::Own<ast::TypeNode>> parseTypeAnnotation();
  zc::Maybe<zc::Own<ast::TypeNode>> parseUnionType();
  zc::Maybe<zc::Own<ast::TypeNode>> parseIntersectionType();
  zc::Maybe<zc::Own<ast::TypeNode>> parsePostfixType();
  zc::Maybe<zc::Own<ast::TypeNode>> parseTypeAtom();
  zc::Maybe<zc::Own<ast::ArrayTypeNode>> parseArrayType();
  zc::Maybe<zc::Own<ast::FunctionTypeNode>> parseFunctionType();
  zc::Maybe<zc::Own<ast::ReturnTypeNode>> parseReturnType();
  zc::Maybe<zc::Own<ast::ObjectTypeNode>> parseObjectType();
  zc::Maybe<zc::Own<ast::TupleTypeNode>> parseTupleType();
  zc::Maybe<zc::Own<ast::TypeReferenceNode>> parseTypeReference();
  zc::Maybe<zc::Own<ast::PredefinedTypeNode>> parsePredefinedType();
  zc::Maybe<zc::Own<ast::ParenthesizedTypeNode>> parseParenthesizedType();
  zc::Maybe<zc::Own<ast::TypeParameterDeclaration>> parseTypeParameter();
  zc::Maybe<zc::Own<ast::TypeQueryNode>> parseTypeQuery();
  zc::Maybe<zc::Own<ast::Expression>> parseTypeQueryExpression();
  zc::Maybe<zc::Own<ast::TypeNode>> parseRaisesClause();

  // --- Patterns & Identifiers ---
  zc::Maybe<zc::Own<ast::Pattern>> parsePattern();
  zc::Maybe<zc::Own<ast::Pattern>> parseWildcardPattern();
  zc::Maybe<zc::Own<ast::Pattern>> parseIdentifierPattern();
  zc::Maybe<zc::Own<ast::Pattern>> parseTuplePattern();
  zc::Maybe<zc::Own<ast::Pattern>> parseStructurePattern();
  zc::Maybe<zc::Own<ast::BindingPattern>> parseArrayBindingPattern();
  zc::Maybe<zc::Own<ast::BindingPattern>> parseObjectBindingPattern();

  zc::Maybe<zc::Own<ast::Identifier>> createIdentifier(bool isIdentifier);
  zc::Maybe<zc::Own<ast::Identifier>> parseIdentifier();
  zc::Maybe<zc::Own<ast::Identifier>> parseIdentifierName();
  zc::Maybe<zc::Own<ast::Identifier>> parseBindingIdentifier();
  zc::Maybe<zc::Own<ast::BindingElement>> parseBindingElement();

  // --- Lists & Arguments ---
  zc::Vector<zc::Own<ast::TypeParameterDeclaration>> parseTypeParameters();
  zc::Vector<zc::Own<ast::BindingElement>> parseParameters();
  zc::Vector<zc::Own<ast::CaptureElement>> parseCaptureClause();
  zc::Maybe<zc::Own<ast::CaptureElement>> parseCaptureElement();
  zc::Maybe<zc::Vector<zc::Own<ast::Expression>>> parseArgumentList();
  zc::Maybe<zc::Vector<zc::Own<ast::TypeNode>>> tryParseTypeArgumentsInExpression();
  zc::Maybe<zc::Vector<zc::Own<ast::TypeNode>>> parseTypeArguments();
  zc::Vector<zc::Own<ast::Statement>> parseEmptyNodeList();

  // --- Misc Parsing ---
  zc::Maybe<zc::Own<ast::TokenNode>> parseTokenNode();
  zc::Maybe<zc::Own<ast::TokenNode>> parseExpectedToken(ast::SyntaxKind kind);

  // --- Start Checks ---
  bool isStartOfStatement();
  bool isStartOfLeftHandSideExpression();
  bool isStartOfExpression();
  bool isStartOfDeclaration();
  bool scanStartOfDeclaration();
  bool isStartOfParameter();
  bool isStartOfType(bool inStartOfParameter = false);
  bool isStartOfInterfaceOrClassMember();
  bool isStartOfOptionalPropertyOrElementAccessChain();

  // --- Node/Context Checks ---
  bool isUpdateExpression() const;
  bool isBindingIdentifier() const;
  bool isIdentifier() const;
  bool isBindingIdentifierOrPattern() const;
  bool isHeritageClause() const;
  bool isModifier() const;
  bool isBinaryOperator() const;
  bool isIdentifierOrKeyword() const;
  bool isLiteralPropertyName() const;
  bool isImportAttributeName() const;
  bool isHeritageClauseExtendsOrImplementsKeyword() const;
  bool isValidHeritageClauseObjectLiteral();
  bool isUsingDeclaration();
  bool isAwaitUsingDeclaration();
  bool canFollowTypeArgumentsInExpression();

  // --- Lookahead Checks ---
  bool nextTokenIsTokenStringLiteral();
  bool nextTokenIsNumericLiteral();
  bool nextIsParenthesizedOrFunctionType();
  bool nextTokenIsLeftParenOrLessThanOrDot();
  bool nextTokenIsLeftParenOrLessThan();
  bool nextTokenIsIdentifierOrKeywordOrLeftBracket();
  bool nextTokenIsIdentifierOrKeywordOnSameLine();
  bool nextTokenIsIdentifierOnSameLine();
  bool nextTokenIsIdentifierOrStringLiteralOnSameLine();
  bool tokenIsIdentifierOrKeyword(const lexer::Token& token) const;
  bool nextTokenIsEqualsOrSemicolonOrColonToken();
  bool nextTokenIsBindingIdentifierOrStartOfDestructuringOnSameLine(bool disallowOf = false);
  bool nextIsUsingKeywordThenBindingIdentifierOrStartOfObjectDestructuringOnSameLine();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
