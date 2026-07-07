// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Internal implementation header for the parser. Do NOT include from
// public headers or from outside the parser library.

#pragma once

#include <cstddef>
#include <cstdint>

#include "zc/core/debug.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-factory.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/schema-verifier.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/lexer/token.h"
#include "zomlang/compiler/lexer/utils.h"
#include "zomlang/compiler/parser/parser-context.h"
#include "zomlang/compiler/parser/parser.h"
#include "zomlang/compiler/parser/token-cursor.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/compiler/trace/trace.h"

namespace zomlang {
namespace compiler {
namespace parser {

// --- Shared helper function declarations ---
// Implementations live in parser-helpers.cc (RFC 0002: header = declarations only).

namespace parser_helpers {

bool isIdentifierLike(ast::SyntaxKind kind);

bool isExpressionIdentifierLike(ast::SyntaxKind kind);

bool isPropertyNameLike(ast::SyntaxKind kind);

bool isPrimitiveTypeKeyword(ast::SyntaxKind kind);

uint8_t predefinedTypeCode(ast::SyntaxKind kind);

uint8_t integerBase(zc::StringPtr text);

int32_t binaryPrecedence(ast::SyntaxKind kind);

ast::BinaryOperatorKind binaryOpCode(ast::SyntaxKind kind);

bool isPrefixUnaryOperator(ast::SyntaxKind kind);

ast::UnaryOperatorKind unaryOpCode(ast::SyntaxKind kind);

bool isPostfixOperator(ast::SyntaxKind kind);

uint8_t postfixOpCode(ast::SyntaxKind kind);

bool isMacroGroupOpen(ast::SyntaxKind kind);

ast::SyntaxKind macroGroupClose(ast::SyntaxKind kind);

zc::StringPtr macroGroupCloseLabel(ast::SyntaxKind kind);

ast::MacroBrace macroBraceCode(ast::SyntaxKind kind);

ast::BindingDeclarationKind bindingDeclarationKindCode(ast::SyntaxKind kind);

bool isAssignmentOperator(ast::SyntaxKind kind);

uint8_t assignmentOpCode(ast::SyntaxKind kind);

uint8_t castModeCode(ast::SyntaxKind kind);

bool canEndExpressionBeforeBinary(ast::SyntaxKind kind);

bool canStartStatementAfterBindingDeclaration(ast::SyntaxKind kind);

bool isUnsupportedStatementKeyword(ast::SyntaxKind kind);

bool isTemplateLiteralToken(ast::SyntaxKind kind);

bool canPrecedeTaggedTemplate(ast::SyntaxKind kind);

bool isInterfaceModifier(ast::SyntaxKind kind);

bool isDeclarationModifier(ast::SyntaxKind kind);

bool isDeclarationHead(ast::SyntaxKind kind);

bool isNamedTypeDeclarationHead(ast::SyntaxKind kind);

bool isInvalidObjectLiteralPropertyName(ast::SyntaxKind kind);

bool isLiteralPatternToken(ast::SyntaxKind kind);

bool isLiteralExpressionToken(ast::SyntaxKind kind);

bool isAttributePathSegment(ast::SyntaxKind kind);

bool isAttributeStart(ast::SyntaxKind first, ast::SyntaxKind second);

bool isTopLevelCfgAttributeTarget(ast::SyntaxKind kind);

zc::StringPtr tokenLabel(const lexer::Token& token);

}  // namespace parser_helpers

// Bring helper names into the parser namespace so all .cc files can use them
// unqualified (they are already in zomlang::compiler::parser).
using namespace parser_helpers;

class AstFactory final : public ast::TypedNodeFactory<AstFactory> {
public:
  ast::NodeList makeList(zc::ArrayPtr<const ast::NodeId> nodes) { return builder.makeList(nodes); }

  ast::IdentList makeIdentList(zc::ArrayPtr<const ast::IdentId> ids) {
    return builder.makeIdentList(ids);
  }

  ast::StringId internString(zc::StringPtr value) { return builder.internString(value); }

  ast::IdentId internIdent(zc::StringPtr value) { return builder.internIdent(value); }

  ast::BigIntId internBigInt(zc::StringPtr value) { return builder.internBigInt(value); }

  ast::FloatId internFloat(zc::StringPtr value) { return builder.internFloat(value); }

  void setRoot(ast::NodeId id) { builder.setRoot(id); }

  ast::Tree finish() { return builder.finish(); }

private:
  template <typename>
  friend class ast::TypedNodeFactory;

  ast::NodeId makeTypedNode(ast::SyntaxKind kind, source::SourceRange range,
                            ast::NodePayload payload = {}) {
    return builder.makeNode(kind, zc::mv(range), payload);
  }

  ast::TreeBuilder builder;
};

// --- Parser::Impl declarations ---

struct Parser::Impl {
  Impl(const source::SourceManager& sourceMgr, diagnostics::DiagnosticEngine& diagnosticEngine,
       const basic::LangOptions& langOpts, basic::StringPool& stringPool,
       const source::BufferId& bufferId);

  const source::SourceManager& sourceMgr;
  diagnostics::DiagnosticEngine& diagnosticEngine;
  source::BufferId bufferId;
  ParserContext context;

  enum class RecoveryContext : uint8_t {
    SourceFile,
    Declaration,
    Statement,
    Expression,
    Type,
    Pattern,
  };

  struct RecoveryFrame {
    RecoveryContext context = RecoveryContext::SourceFile;
    size_t anchor = 0;
    ast::SyntaxKind syncSet[32] = {};
    uint8_t syncCount = 0;
    bool consumed = false;
    size_t suppressedUntil = 0;
  };

  class RecoveryFrameScope {
  public:
    RecoveryFrameScope(const Impl& parser, RecoveryContext context, size_t anchor);
    ~RecoveryFrameScope();

    size_t finish(size_t position) const;

    RecoveryFrameScope(const RecoveryFrameScope&) = delete;
    RecoveryFrameScope& operator=(const RecoveryFrameScope&) = delete;

  private:
    const Impl& parser;
  };

  mutable zc::Vector<RecoveryFrame> recoveryFrames;

  RecoveryFrame makeRecoveryFrame(RecoveryContext context, size_t anchor) const;

  void pushRecoveryFrame(RecoveryContext context, size_t anchor) const;

  void popRecoveryFrame() const;

  void markRecoveryConsumed(size_t position) const;

  /// Returns true if a diagnostic at the given token index should be
  /// suppressed because it falls within a region that has already been
  /// consumed by error recovery.  Checks all active recovery frames that
  /// have been marked consumed (i.e., their finish() was called with a
  /// position beyond the anchor).  Per RFC 0002, we examine all frames
  /// and suppress if any consumed frame's suppressedUntil covers the token.
  bool shouldSuppressDiagnostic(size_t tokenIndex) const;

  const lexer::Token& tokenAt(size_t index) const;

  ast::SyntaxKind kindAt(size_t index) const;

  bool isAtEnd(size_t index) const;

  TokenCursor tokenCursorAt(size_t index) const;

  source::SourceRange rangeFor(size_t start, size_t end) const;

  ast::IdentId internIdent(AstFactory& builder, size_t index) const;

  ast::StringId internString(AstFactory& builder, size_t index) const;

  bool tokenTextEquals(size_t index, zc::StringPtr expected) const;

  bool isSoftKeyword(size_t index, zc::StringPtr expected) const;

  bool isSoftKeywordModifier(size_t index) const;

  bool isExternDeclarationStart(size_t index, size_t limit) const;

  bool isSoftDeclarationHead(size_t index, size_t limit) const;

  bool parseExternAbi(size_t index, ast::Abi& abi) const;

  bool rangeIsWrapped(size_t start, size_t end, ast::SyntaxKind open, ast::SyntaxKind close) const;

  size_t findMatchingRightBrace(size_t openIndex, size_t limit) const;

  size_t findMatchingRightBracket(size_t openIndex, size_t limit) const;

  size_t findMatchingMacroGroup(size_t openIndex, size_t limit) const;

  bool isMacroInvocationStart(size_t start, size_t limit) const;

  size_t findMacroInvocationEnd(size_t start, size_t limit) const;

  bool isOuterAttributeStart(size_t index, size_t limit) const;

  size_t skipOuterAttributePrefix(size_t start, size_t end) const;

  bool isIdentifierText(size_t index, zc::StringPtr text) const;

  bool isStandaloneDynTypeRange(size_t start, size_t end) const;

  size_t consumeBalancedGroupEnd(TokenCursor& cursor, size_t limit, ast::SyntaxKind open,
                                 ast::SyntaxKind close) const;

  size_t consumeBalancedUntil(TokenCursor& cursor, size_t limit, ast::SyntaxKind needle) const;

  size_t consumeBalancedTypeUntil(TokenCursor& cursor, size_t limit, ast::SyntaxKind needle) const;

  size_t consumeBalancedIdentifierUntil(TokenCursor& cursor, size_t limit,
                                        zc::StringPtr text) const;

  size_t consumeBalancedTypeIdentifierUntil(TokenCursor& cursor, size_t limit,
                                            zc::StringPtr text) const;

  void addNodeIfPresent(zc::Vector<ast::NodeId>& nodes, ast::NodeId node) const;

  ast::IdentList makeIdentList(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId makeModulePath(AstFactory& builder, size_t start, size_t end) const;

  bool isModulePathSeparatorAt(size_t index, size_t end) const;

  size_t modulePathSeparatorWidth(size_t index, size_t end) const;

  bool modulePathSeparatorPrecedesGroup(size_t index, size_t end) const;

  size_t findModulePathEnd(size_t start, size_t end) const;

  size_t findModuleSpecifierGroupOpen(size_t pathEnd, size_t end) const;

  ast::NodeId makeImportSpecifier(AstFactory& builder, size_t nameIndex, size_t aliasIndex,
                                  size_t end) const;

  ast::NodeId makeExportSpecifier(AstFactory& builder, size_t nameIndex, size_t aliasIndex,
                                  size_t end) const;

  void recoverModuleSpecifier(TokenCursor& cursor, size_t end) const;

  ast::NodeId parseImportSpecifier(AstFactory& builder, TokenCursor& cursor, size_t end) const;

  ast::NodeId parseExportSpecifier(AstFactory& builder, TokenCursor& cursor, size_t end) const;

  zc::Vector<ast::NodeId> parseImportSpecifierList(AstFactory& builder, size_t start,
                                                   size_t end) const;

  zc::Vector<ast::NodeId> parseExportSpecifierList(AstFactory& builder, size_t start,
                                                   size_t end) const;

  ast::NodeId makeAttributePath(AstFactory& builder, size_t start, size_t end) const;

  size_t findAttributePathEnd(size_t start, size_t end) const;

  uint32_t attributePathSegmentCount(size_t start, size_t end) const;

  bool isWhitelistedBareAttribute(size_t start, size_t end) const;

  void diagnoseImportPathSyntax(size_t clauseStart, size_t clauseEnd, size_t pathEnd,
                                size_t groupOpen) const;

  bool isZomCfgAttributePath(size_t start, size_t end) const;

  bool containsUnmodeledRangeOperator(size_t start, size_t end) const;

  void diagnoseCfgAttributeArgs(size_t start, size_t end) const;

  ast::NodeId parseAttribute(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseOuterAttributeList(AstFactory& builder, size_t start, size_t end) const;

  bool outerAttributePrefixContainsZomCfg(size_t start, size_t end) const;

  ast::NodeId makeStatementListItem(AstFactory& builder, ast::NodeId item,
                                    source::SourceRange range,
                                    ast::NodeId attrs = ast::NodeId()) const;

  void emitUnexpected(const lexer::Token& where) const;

  source::SourceLoc diagnosticLoc(size_t index) const;

  void diagnoseExpressionExpected(size_t index) const;

  bool modifierGroupContains(size_t start, size_t end, ast::SyntaxKind needle, size_t& found) const;

  bool modifierGroupContainsSoftKeyword(size_t start, size_t end, zc::StringPtr expected,
                                        size_t& found) const;

  void diagnoseDeclarationModifierGroup(size_t start, size_t end) const;

  bool diagnoseUnsupportedVarianceInTypeParameters(size_t openAngle, size_t closeAngle) const;

  bool diagnoseTypeParameterListSyntax(size_t openAngle, size_t closeAngle) const;

  void diagnoseDeclarationTypeParameterSyntax(size_t afterName, size_t limit) const;

  ast::NodeId parseWherePredicate(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseWhereClause(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseTypeParameters(AstFactory& builder, size_t start, size_t limit,
                                  ast::NodeId whereClause = ast::NodeId()) const;

  ast::NodeId parseRequiredExpression(AstFactory& builder, size_t start, size_t end) const;

  bool requireTrailingSemicolon(size_t start, size_t end) const;

  bool followsFieldTypeColonWithoutSemicolon(size_t index) const;

  size_t consumeMemberBoundary(size_t start, size_t limit) const;

  void diagnoseMissingFieldMemberSemicolon(size_t start, size_t end) const;

  void diagnoseNamedTypeBody(size_t bodyOpen, size_t bodyClose, ast::SyntaxKind kind) const;

  bool isStructLiteralTypeReference(size_t start, size_t end) const;

  bool consumeTypePath(TokenCursor& cursor, size_t limit) const;

  size_t findTypePathEnd(size_t start, size_t end) const;

  bool isDefinitelyNonLValueOperand(size_t start, size_t end) const;

  void diagnoseTokenPatterns();

  size_t effectiveStatementStart(size_t start, size_t end) const;

  struct TypeParseResult {
    ast::NodeId node;
    size_t next = 0;
  };

  size_t findMatchingAngleClose(size_t openIndex, size_t limit) const;

  bool consumeBalancedAngleList(TokenCursor& cursor, size_t limit) const;

  bool consumeFunctionTypeHead(TokenCursor& cursor, size_t limit, size_t& openParen,
                               size_t& closeParen) const;

  size_t functionTypeParameterTypeStart(TokenCursor& cursor, size_t limit) const;

  ast::NodeList parseFunctionTypeParameters(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseTupleTypeRange(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseObjectTypeRange(AstFactory& builder, size_t start, size_t end) const;

  TypeParseResult parseTypeExpression(AstFactory& builder, TokenCursor& cursor, size_t limit) const;

  TypeParseResult parseTypeExpressionAt(AstFactory& builder, size_t start, size_t limit) const;

  TypeParseResult parseUnionType(AstFactory& builder, TokenCursor& cursor, size_t limit) const;

  TypeParseResult parseIntersectionType(AstFactory& builder, TokenCursor& cursor,
                                        size_t limit) const;

  TypeParseResult parsePostfixType(AstFactory& builder, TokenCursor& cursor, size_t limit) const;

  TypeParseResult parseFunctionType(AstFactory& builder, TokenCursor& cursor, size_t limit) const;

  TypeParseResult parseDynType(AstFactory& builder, TokenCursor& cursor, size_t limit) const;

  TypeParseResult parseAssociatedTypeProjection(AstFactory& builder, TokenCursor& cursor,
                                                size_t limit) const;

  TypeParseResult parseParenthesizedOrTupleType(AstFactory& builder, TokenCursor& cursor,
                                                size_t limit) const;

  TypeParseResult parseBracketedType(AstFactory& builder, TokenCursor& cursor, size_t limit) const;

  TypeParseResult parseTypeQuery(AstFactory& builder, TokenCursor& cursor, size_t limit) const;

  ast::NodeList parseTypeArgumentList(AstFactory& builder, TokenCursor& cursor, size_t limit,
                                      size_t& physicalEnd) const;

  TypeParseResult parseNamedType(AstFactory& builder, TokenCursor& cursor, size_t limit) const;

  TypeParseResult parseAtomType(AstFactory& builder, TokenCursor& cursor, size_t limit) const;

  ast::NodeId parseTypeRange(AstFactory& builder, size_t start, size_t end) const;

  size_t consumeCommaDelimitedItem(TokenCursor& cursor, size_t end) const;

  ast::NodeId parseExpressionList(AstFactory& builder, size_t start, size_t end,
                                  ast::SyntaxKind containerKind) const;

  ast::NodeId parseCommaExpression(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeList parseExpressionArguments(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeList parseTypeArguments(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseNewExpression(AstFactory& builder, size_t start, size_t calleeEnd,
                                 size_t typeArgsEnd, size_t end) const;

  ast::NodeId makeEmptyMacroPattern(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId makeEmptyMacroTokenTree(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseMacroInvocationExpression(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseUnsafeBlockExpression(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseSpawnExpression(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseCastExpression(AstFactory& builder, size_t start, size_t asIndex,
                                  size_t end) const;

  ast::NodeId parseImportCallExpression(AstFactory& builder, size_t start, size_t openParen,
                                        size_t end) const;

  ast::NodeId parseCaptureItem(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseCaptureList(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseFunctionExpression(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseLambdaExpression(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeList parseObjectLiteralProperties(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseObjectLiteralExpression(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseStructLiteralExpression(AstFactory& builder, size_t start, size_t brace,
                                           size_t end) const;

  ast::NodeId parseTemplateLiteralExpression(AstFactory& builder, size_t start, size_t end) const;

  struct ExpressionParseResult {
    ast::NodeId node;
    size_t next = 0;
  };

  ExpressionParseResult parseExpression(AstFactory& builder, TokenCursor& cursor,
                                        size_t limit) const;

  ExpressionParseResult parseExpressionAt(AstFactory& builder, size_t start, size_t limit) const;

  ExpressionParseResult parseCommaExpressionAt(AstFactory& builder, size_t start,
                                               size_t limit) const;

  ExpressionParseResult parseAssignmentExpressionAt(AstFactory& builder, size_t start,
                                                    size_t limit) const;

  ExpressionParseResult parseConditionalExpressionAt(AstFactory& builder, size_t start,
                                                     size_t limit) const;

  ExpressionParseResult parseErrorDefaultExpressionAt(AstFactory& builder, size_t start,
                                                      size_t limit) const;

  ExpressionParseResult parseNullCoalesceExpressionAt(AstFactory& builder, size_t start,
                                                      size_t limit) const;

  ExpressionParseResult parseBinaryExpressionAt(AstFactory& builder, size_t start, size_t limit,
                                                int32_t minPrecedence) const;

  ExpressionParseResult parseUnaryExpressionAt(AstFactory& builder, size_t start,
                                               size_t limit) const;

  ExpressionParseResult parsePostfixExpressionAt(AstFactory& builder, size_t start,
                                                 size_t limit) const;

  ExpressionParseResult parsePrimaryExpressionAt(AstFactory& builder, size_t start,
                                                 size_t limit) const;

  ast::NodeId parseExpressionRange(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parsePatternRange(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId makeEmptyClassMemberList(AstFactory& builder, source::SourceRange range) const;

  ast::NodeId makeEmptyEnumVariantList(AstFactory& builder, source::SourceRange range) const;

  ast::NodeId parseClassMemberList(AstFactory& builder, size_t bodyOpen, size_t bodyClose,
                                   ast::SyntaxKind parentKind) const;

  ast::NodeId parseEnumVariantList(AstFactory& builder, size_t bodyOpen, size_t bodyClose) const;

  size_t recoverFunctionParameter(TokenCursor& cursor, size_t closeParen) const;

  ast::NodeId parseFunctionParameter(AstFactory& builder, TokenCursor& cursor,
                                     size_t closeParen) const;

  ast::NodeList parseFunctionParameterNodeList(AstFactory& builder, size_t openParen,
                                               size_t closeParen) const;

  ast::NodeId parseFunctionParameterList(AstFactory& builder, size_t openParen,
                                         size_t closeParen) const;

  size_t consumeSimpleStatementEnd(size_t start, size_t limit) const;

  size_t consumeSpawnStatementEnd(size_t start, size_t limit) const;

  size_t consumeExternDeclarationEnd(size_t start, size_t limit) const;

  size_t findBindingDeclarationRecoveryStart(size_t start, size_t limit) const;

  size_t consumeBindingDeclarationEnd(size_t start, size_t limit) const;

  size_t consumeBracedBodyEnd(size_t bodyOpen, size_t limit) const;

  size_t consumeStatementBodyEnd(size_t bodyStart, size_t limit) const;

  size_t consumeConditionBodyStart(size_t start, size_t limit) const;

  struct IfStatementParts {
    size_t condStart = 0;
    size_t condEnd = 0;
    size_t thenStart = 0;
    size_t thenEnd = 0;
    size_t elseIndex = 0;
    size_t elseStart = 0;
    size_t end = 0;
  };

  IfStatementParts parseIfStatementParts(size_t start, size_t limit) const;

  size_t consumeIfStatementEnd(size_t start, size_t limit) const;

  struct WhileStatementParts {
    size_t condStart = 0;
    size_t condEnd = 0;
    size_t bodyStart = 0;
    size_t bodyEnd = 0;
    size_t end = 0;
  };

  WhileStatementParts parseWhileStatementParts(size_t start, size_t limit) const;

  size_t consumeWhileStatementEnd(size_t start, size_t limit) const;

  struct MatchStatementParts {
    size_t scrutineeStart = 0;
    size_t scrutineeEnd = 0;
    size_t bodyOpen = 0;
    size_t bodyClose = 0;
    size_t end = 0;
  };

  MatchStatementParts parseMatchStatementParts(size_t start, size_t limit) const;

  size_t consumeMatchStatementEnd(size_t start, size_t limit) const;

  struct DoWhileStatementParts {
    size_t bodyStart = 0;
    size_t bodyEnd = 0;
    size_t whileIndex = 0;
    size_t condStart = 0;
    size_t condEnd = 0;
    size_t end = 0;
  };

  DoWhileStatementParts parseDoWhileStatementParts(size_t start, size_t limit) const;

  size_t consumeDoWhileStatementEnd(size_t start, size_t limit) const;

  size_t consumeLabeledStatementEnd(size_t start, size_t limit) const;

  size_t consumeTypeLike(size_t start, size_t limit) const;

  size_t findFunctionBodyOpenAfterParams(size_t closeParen, size_t limit) const;

  struct FunctionDeclarationParts {
    size_t nameIndex = 0;
    size_t openParen = 0;
    size_t closeParen = 0;
    size_t headerEnd = 0;
    size_t bodyOpen = 0;
    size_t end = 0;
    size_t arrow = 0;
    size_t raises = 0;
  };

  FunctionDeclarationParts parseFunctionDeclarationParts(size_t start, size_t limit) const;

  size_t consumeFunctionDeclarationEnd(size_t start, size_t limit) const;

  size_t consumeExportDeclarationEnd(size_t start, size_t limit) const;

  size_t consumeNamedTypeDeclarationEnd(size_t start, size_t limit) const;

  size_t consumeBracedDeclarationEnd(size_t start, size_t limit) const;

  struct SourceElementBoundary {
    size_t start = 0;
    size_t nodeStart = 0;
    size_t head = 0;
    size_t end = 0;
    ast::SyntaxKind kind = ast::SyntaxKind::Unknown;
  };

  struct SourceElementParseResult {
    ast::NodeId node;
    ast::NodeId attrs;
    SourceElementBoundary boundary;
  };

  struct ForStatementParts {
    size_t headerStart = 0;
    size_t headerEnd = 0;
    size_t bodyStart = 0;
    size_t bodyEnd = 0;
    size_t end = 0;
    size_t firstSemi = 0;
    size_t secondSemi = 0;
    size_t inIndex = 0;
    ast::SyntaxKind kind = ast::SyntaxKind::ForStmt;
  };

  ForStatementParts parseForStatementParts(size_t head, size_t limit) const;

  SourceElementBoundary consumeSourceElement(TokenCursor& cursor, size_t limit) const;

  SourceElementParseResult parseSourceElement(AstFactory& builder, TokenCursor& cursor,
                                              size_t limit) const;

  ast::NodeId parseBlock(AstFactory& builder, size_t openBrace, size_t limit,
                         bool allowFinalExpression = false) const;

  ast::NodeId parseStatementBody(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseModuleDeclaration(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseImportDeclaration(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseExportDeclaration(AstFactory& builder, size_t start, size_t end) const;

  struct VariableDeclaratorParseResult {
    ast::NodeId node;
    size_t next = 0;
  };

  size_t consumeVariableDeclaratorPattern(TokenCursor& cursor, size_t limit) const;

  bool isInitializerGenericAngle(size_t openAngle, size_t limit) const;

  size_t consumeVariableInitializer(TokenCursor& cursor, size_t limit) const;

  VariableDeclaratorParseResult parseVariableDeclarator(AstFactory& builder, TokenCursor& cursor,
                                                        size_t limit) const;

  ast::NodeId parseVariableDeclaratorList(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseLetStatement(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseReturnStatement(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseSuspendStatement(AstFactory& builder, size_t start, size_t end) const;

  size_t findMatchingRightParen(size_t openParen, size_t limit) const;

  void conditionRangeAfterKeyword(size_t start, size_t end, size_t& condStart, size_t& condEnd,
                                  size_t& bodyStart) const;

  ast::NodeId parseIfStatement(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseWhileStatement(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseDoWhileStatement(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseBreakStatement(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseContinueStatement(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseLabeledStatement(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseForStatement(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseForInStatement(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseMatchStatement(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseExternFunctionDecl(AstFactory& builder, size_t start, size_t end,
                                      ast::Abi abi) const;

  ast::NodeId parseExternVarDecl(AstFactory& builder, size_t start, size_t end, ast::Abi abi) const;

  ast::NodeId parseExternTypeAliasDecl(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseExternBlockDeclaration(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseMacroRulesDeclaration(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseFunctionDeclaration(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseNamedTypeDeclaration(AstFactory& builder, size_t start, size_t end,
                                        ast::SyntaxKind kind) const;

  ast::NodeId parseErrorDeclaration(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseAliasDeclaration(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseImplInterfaceBound(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId makeImplIfaceList(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseInterfaceHeritage(AstFactory& builder, size_t headerStart,
                                     size_t headerEnd) const;

  ast::NodeId parseStandaloneImplDeclaration(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseExpressionStatement(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseExpressionStatementWithoutSemicolon(AstFactory& builder, size_t start,
                                                       size_t end) const;

  ast::NodeId parseSpawnStatement(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseSourceElementOfKind(AstFactory& builder, size_t start, size_t end,
                                       ast::SyntaxKind kind) const;

  bool canContinueLetInitializerBefore(size_t index) const;

  ast::Tree buildTree();
};

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
