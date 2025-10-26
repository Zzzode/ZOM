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

namespace parser {

// Concept to define valid AST node types for NodeList
template <typename T>
concept NodeLike =
    std::is_base_of_v<ast::Node, T> || std::is_base_of_v<ast::Statement, T> ||
    std::is_base_of_v<ast::Expression, T> || std::is_base_of_v<ast::Declaration, T> ||
    std::is_base_of_v<ast::TypeNode, T> || std::is_base_of_v<ast::TokenNode, T>;

enum ParsingContext {
  kSourceElements = 0,  // Parsing source elements (statements, declarations, etc.)
  kBlockElements,       // Parsing block elements (statements, declarations, etc.)
  kClassMembers,        // Parsing class members (fields, methods, etc.)
  kEnumMembers,         // Parsing enum members (values, etc.)
  kCount,
};

/// \brief The parser class.
class Parser {
public:
  Parser(const source::SourceManager& sourceMgr, diagnostics::DiagnosticEngine& diagnosticEngine,
         const basic::LangOptions& langOpts, const source::BufferId& bufferId) noexcept;
  ~Parser() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(Parser);

  /// \brief Parse the source file and return the AST.
  /// \return The AST if parsing succeeded, zc::Nothing otherwise.
  zc::Maybe<zc::Own<ast::Node>> parse();

  /// Lookahead functionality
  /// \brief Look ahead n tokens without consuming them
  /// \param n The number of tokens to look ahead (1-based, 1 means next token)
  /// \return The token at position n, or EOF token if beyond end
  const lexer::Token& lookAhead(unsigned n);

  /// \brief Check if we can look ahead n tokens
  /// \param n The number of tokens to look ahead
  /// \return true if we can look ahead n tokens, false otherwise
  bool canLookAhead(unsigned n);

  /// \brief Look ahead and check if the nth token matches the given kind
  /// \param n The number of tokens to look ahead (1-based)
  /// \param kind The token kind to check for
  /// \return true if the nth token matches the kind, false otherwise
  bool isLookAhead(unsigned n, ast::SyntaxKind kind);

private:
  template <NodeLike Node>
  zc::Own<Node> finishNode(zc::Own<Node>&& node, source::SourceLoc pos) {
    const source::SourceLoc end = getFullStartLoc();
    return finishNode(zc::mv(node), zc::mv(pos), zc::mv(end));
  }

  template <NodeLike Node>
  zc::Own<Node> finishNode(zc::Own<Node>&& node, source::SourceLoc pos, source::SourceLoc end) {
    node->setSourceRange(source::SourceRange(pos, end));
    return zc::mv(node);
  }

  template <NodeLike Node>
  zc::Vector<zc::Own<Node>> parseList(ParsingContext context,
                                      zc::Function<zc::Maybe<zc::Own<Node>>()> parseElement) {
    zc::Vector<zc::Own<Node>> result;

    while (!isListTerminator(context)) {
      if (isListElement(context, /*inErrorRecovery*/ false)) {
        ZC_IF_SOME(element, parseElement()) { result.add(zc::mv(element)); }
        continue;
      }

      if (abortParsingListOrMoveToNextToken(context)) { break; }
    }

    return result;
  }

  bool isListTerminator(ParsingContext context) const;
  bool isListElement(ParsingContext context, bool inErrorRecovery) const;

  bool abortParsingListOrMoveToNextToken(ParsingContext context);

private:
  zc::Maybe<zc::Own<ast::Identifier>> createIdentifier(bool isIdentifier);
  zc::Maybe<zc::Own<ast::Identifier>> parseIdentifier();
  zc::Maybe<zc::Own<ast::Identifier>> parseIdentifierName();
  zc::Maybe<zc::Own<ast::Identifier>> parseBindingIdentifier();
  zc::Maybe<zc::Own<ast::BindingElement>> parseBindingElement();

  // Module and source file parsing
  zc::Maybe<zc::Own<ast::SourceFile>> parseSourceFile();
  zc::Maybe<zc::Own<ast::ImportDeclaration>> parseImportDeclaration();
  zc::Maybe<zc::Own<ast::ExportDeclaration>> parseExportDeclaration();
  zc::Maybe<zc::Own<ast::ModulePath>> parseModulePath();
  zc::Maybe<zc::Own<ast::Statement>> parseModuleItem();

  // Statement parsing
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

  // Declaration parsing
  zc::Maybe<zc::Own<ast::Statement>> parseDeclaration();
  zc::Maybe<zc::Own<ast::VariableStatement>> parseVariableStatement();
  zc::Maybe<zc::Own<ast::VariableDeclarationList>> parseVariableDeclarationList();
  zc::Maybe<zc::Own<ast::FunctionDeclaration>> parseFunctionDeclaration();
  zc::Maybe<zc::Own<ast::ClassDeclaration>> parseClassDeclaration();
  zc::Maybe<zc::Own<ast::InterfaceDeclaration>> parseInterfaceDeclaration();
  zc::Maybe<zc::Own<ast::StructDeclaration>> parseStructDeclaration();
  zc::Maybe<zc::Own<ast::EnumDeclaration>> parseEnumDeclaration();
  zc::Maybe<zc::Own<ast::ErrorDeclaration>> parseErrorDeclaration();
  zc::Maybe<zc::Own<ast::AliasDeclaration>> parseAliasDeclaration();

  // Expression parsing
  zc::Maybe<zc::Own<ast::Expression>> parseExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseInitializer();
  zc::Maybe<zc::Own<ast::Expression>> parseAssignmentExpressionOrHigher();
  zc::Maybe<zc::Own<ast::Expression>> parseBinaryExpressionOrHigher(
      ast::OperatorPrecedence precedence);
  zc::Maybe<zc::Own<ast::Expression>> parseBinaryExpressionRest(
      zc::Own<ast::Expression> leftOperand, ast::OperatorPrecedence precedence,
      source::SourceLoc startLoc);
  zc::Maybe<zc::Own<ast::Expression>> parseUnaryExpressionOrHigher();
  zc::Maybe<zc::Own<ast::ConditionalExpression>> parseConditionalExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseConditionalExpressionRest(
      zc::Own<ast::Expression> leftOperand, source::SourceLoc startLoc);
  zc::Maybe<zc::Own<ast::Expression>> parseShortCircuitExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseLogicalOrExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseLogicalAndExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseCoalesceExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseErrorDefaultExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseBitwiseOrExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseBitwiseXorExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseBitwiseAndExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseEqualityExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseRelationalExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseShiftExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseAdditiveExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseMultiplicativeExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseExponentiationExpression();
  zc::Maybe<zc::Own<ast::CastExpression>> parseCastExpression();
  zc::Maybe<zc::Own<ast::Expression>> parseUnaryExpression();
  zc::Maybe<zc::Own<ast::UnaryExpression>> parseSimpleUnaryExpression();
  zc::Maybe<zc::Own<ast::UnaryExpression>> parsePrefixUnaryExpression();
  zc::Maybe<zc::Own<ast::VoidExpression>> parseVoidExpression();
  zc::Maybe<zc::Own<ast::TypeOfExpression>> parseTypeOfExpression();
  zc::Maybe<zc::Own<ast::UpdateExpression>> parseUpdateExpression();
  zc::Maybe<zc::Own<ast::LeftHandSideExpression>> parseLeftHandSideExpressionOrHigher();
  zc::Maybe<zc::Own<ast::MemberExpression>> parseMemberExpressionOrHigher();
  zc::Maybe<zc::Own<ast::LeftHandSideExpression>> parseCallExpressionRest(
      source::SourceLoc loc, zc::Own<ast::LeftHandSideExpression> expr);
  zc::Maybe<zc::Own<ast::MemberExpression>> parseMemberExpressionRest(
      zc::Own<ast::LeftHandSideExpression> expr, source::SourceLoc startLoc,
      bool allowOptionalChain);
  zc::Maybe<zc::Own<ast::MemberExpression>> parseSuperExpression();
  zc::Maybe<zc::Own<ast::AwaitExpression>> parseAwaitExpression();
  zc::Maybe<zc::Own<ast::LeftHandSideExpression>> parseLeftHandSideExpression();
  zc::Maybe<zc::Own<ast::NewExpression>> parseNewExpression();
  zc::Maybe<zc::Own<ast::PrimaryExpression>> parsePrimaryExpression();
  zc::Maybe<zc::Own<ast::ParenthesizedExpression>> parseParenthesizedExpression();
  zc::Maybe<zc::Own<ast::LiteralExpression>> parseLiteralExpression();
  zc::Maybe<zc::Own<ast::ArrayLiteralExpression>> parseArrayLiteralExpression();
  zc::Maybe<zc::Own<ast::ObjectLiteralExpression>> parseObjectLiteralExpression();
  zc::Maybe<zc::Own<ast::Identifier>> parseIdentifierExpression();
  zc::Maybe<zc::Own<ast::StringLiteral>> parseStringLiteral();
  zc::Maybe<zc::Own<ast::IntegerLiteral>> parseIntegerLiteral();
  zc::Maybe<zc::Own<ast::FloatLiteral>> parseFloatLiteral();
  zc::Maybe<zc::Own<ast::BooleanLiteral>> parseBooleanLiteral();
  zc::Maybe<zc::Own<ast::NullLiteral>> parseNullLiteral();
  zc::Maybe<zc::Own<ast::FunctionExpression>> parseFunctionExpression();

  // Type parsing
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
  zc::Maybe<zc::Own<ast::Pattern>> parsePattern();
  zc::Maybe<zc::Own<ast::Pattern>> parseWildcardPattern();
  zc::Maybe<zc::Own<ast::Pattern>> parseIdentifierPattern();
  zc::Maybe<zc::Own<ast::Pattern>> parseTuplePattern();
  zc::Maybe<zc::Own<ast::Pattern>> parseStructurePattern();
  zc::Maybe<zc::Own<ast::BindingPattern>> parseArrayBindingPattern();
  zc::Maybe<zc::Own<ast::BindingPattern>> parseObjectBindingPattern();

  // Utility parsing methods
  ZC_ALWAYS_INLINE(bool expectToken(ast::SyntaxKind kind));
  ZC_ALWAYS_INLINE(bool consumeExpectedToken(ast::SyntaxKind kind));
  ZC_ALWAYS_INLINE(const lexer::Token& currentToken() const);
  ZC_ALWAYS_INLINE(void consumeToken());

  ZC_NODISCARD source::SourceLoc getFullStartLoc() const;

  // Argument list parsing
  zc::Vector<zc::Own<ast::TypeParameterDeclaration>> parseTypeParameters();
  zc::Vector<zc::Own<ast::BindingElement>> parseParameters();
  zc::Maybe<zc::Vector<zc::Own<ast::Expression>>> parseArgumentList();
  zc::Maybe<zc::Vector<zc::Own<ast::TypeNode>>> parseTypeArgumentsInExpression();

  // Token node parsing
  zc::Maybe<zc::Own<ast::TokenNode>> parseTokenNode();
  zc::Maybe<zc::Own<ast::TokenNode>> parseExpectedToken(ast::SyntaxKind kind);

  /// Helper functions for parsing
  bool isStartOfStatement() const;
  bool isStartOfLeftHandSideExpression() const;
  bool isStartOfExpression() const;
  bool isStartOfDeclaration() const;
  bool isUpdateExpression(ast::SyntaxKind tokenKind) const;
  bool isBindingIdentifier() const;
  bool isIdentifier() const;

  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
