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

#include "compiler/ast/generated/node-factory.h"
#include "compiler/ast/generated/node-payload.h"
#include "compiler/ast/schema-verifier.h"
#include "compiler/basic/string-pool.h"
#include "compiler/basic/zomlang-opts.h"
#include "compiler/cst/parser-event-stream.h"
#include "compiler/diagnostics/core/diagnostic-ids.h"
#include "compiler/diagnostics/fact/source-diagnostic-draft-buffer.h"
#include "compiler/lexer/token.h"
#include "compiler/lexer/utils.h"
#include "compiler/parser/parser-context.h"
#include "compiler/parser/parser.h"
#include "compiler/parser/token-cursor.h"
#include "compiler/source/manager.h"
#include "compiler/trace/trace.h"
#include "zc/core/debug.h"
#include "zc/core/vector.h"

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

ast::BindingDeclarationKind bindingDeclarationKindCode(ast::SyntaxKind kind);

bool isAssignmentOperator(ast::SyntaxKind kind);

uint8_t assignmentOpCode(ast::SyntaxKind kind);

uint8_t castModeCode(ast::SyntaxKind kind);

bool canEndExpressionBeforeBinary(ast::SyntaxKind kind);

bool canStartStatementAfterBindingDeclaration(ast::SyntaxKind kind);

bool isUnsupportedStatementKeyword(ast::SyntaxKind kind);

bool isTemplateLiteralToken(ast::SyntaxKind kind);

bool canPrecedeTaggedTemplate(ast::SyntaxKind kind);

bool isVisibilityModifier(ast::SyntaxKind kind);

bool isBehaviorModifier(ast::SyntaxKind kind);

bool isNamedDeclarationModifier(ast::SyntaxKind kind);

bool isMemberModifier(ast::SyntaxKind kind);

bool isInterfaceModifier(ast::SyntaxKind kind);

bool isDeclarationHead(ast::SyntaxKind kind);

bool isNamedTypeDeclarationHead(ast::SyntaxKind kind);

bool isInvalidObjectLiteralPropertyName(ast::SyntaxKind kind);

bool isLiteralPatternToken(ast::SyntaxKind kind);

bool isLiteralExpressionToken(ast::SyntaxKind kind);

bool isAttributePathSegment(ast::SyntaxKind kind);

bool isAttributeStart(ast::SyntaxKind first, ast::SyntaxKind second);

zc::StringPtr tokenLabel(const lexer::Token& token);

}  // namespace parser_helpers

// Bring helper names into the parser namespace so all .cc files can use them
// unqualified (they are already in zomlang::compiler::parser).
using namespace parser_helpers;

class ParserSyntaxFactory final : public ast::TypedNodeFactory<ParserSyntaxFactory> {
public:
  ParserSyntaxFactory(const source::SourceManager& sources, const source::BufferId& buffer)
      : builder(sources, buffer) {}

  ast::NodeList makeList(zc::ArrayPtr<const ast::NodeId> nodes) { return builder.makeList(nodes); }

  ast::IdentList makeIdentList(zc::ArrayPtr<const ast::IdentId> ids) {
    return builder.makeIdentList(ids);
  }

  ast::StringId internString(zc::StringPtr value) { return builder.internString(value); }

  ast::IdentId internIdent(zc::StringPtr value) { return builder.internIdent(value); }

  ast::BigIntId internBigInt(zc::StringPtr value) { return builder.internBigInt(value); }

  ast::FloatId internFloat(zc::StringPtr value) { return builder.internFloat(value); }

  void setRoot(ast::NodeId id) { builder.setRoot(id); }

  cst::ParserEventStreamRequest finish() { return builder.finish(); }

private:
  template <typename>
  friend class ast::TypedNodeFactory;

  ast::NodeId makeTypedNode(ast::SyntaxKind kind, source::SourceRange range,
                            ast::NodePayload payload = {}) {
    return builder.makeNode(kind, zc::mv(range), payload);
  }

  cst::ParserEventBuilder builder;
};

// --- Parser::Impl declarations ---

enum class RecoveryContext : uint8_t {
  SourceFile,
  Declaration,
  Statement,
  Expression,
  Type,
  Pattern,
};

struct Parser::Impl {
  Impl(const source::SourceManager& sourceMgr,
       diagnostics::SourceDiagnosticDraftBuffer& diagnosticFacts,
       const basic::LangOptions& langOpts, basic::StringPool& stringPool,
       const source::BufferId& bufferId);

  const source::SourceManager& sourceMgr;
  diagnostics::SourceDiagnosticDraftBuffer& diagnosticFacts;
  diagnostics::DiagnosticEmitter& diagnosticEngine;
  source::BufferId bufferId;
  ParserContext context;
  bool parseAttempted = false;
  bool parseSucceeded = false;
  bool tokenSnapshotTaken = false;
  bool recoverableSyntaxTaken = false;
  zc::Maybe<cst::RecoverableSyntaxTree> recoverableSyntax;

  enum class SourceElementContext : uint8_t {
    ModuleItem,
    Statement,
    ExportedDeclaration,
  };

  struct RecoveryFrame {
    RecoveryContext context = RecoveryContext::SourceFile;
    size_t anchor = 0;
    ast::SyntaxKind syncSet[32] = {};
    uint8_t syncCount = 0;
    bool consumed = false;
    bool childRecorded = false;
    size_t suppressedUntil = 0;
    size_t errorCountAtEntry = 0;
  };

  enum class PendingRecoveryKind : uint8_t { MissingSubtree, SkippedTokens };

  struct PendingRecoveryRecord final {
    PendingRecoveryKind kind;
    RecoveryContext context;
    source::SourceLoc anchor;
    source::SourceRange range;
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
  mutable zc::Vector<PendingRecoveryRecord> pendingRecoveryRecords;

  enum class CallableParameterContext : uint8_t {
    Member,
    ModuleFunction,
    BlockFunction,
    ExternFunction,
    FunctionExpression,
    Lambda,
  };

  RecoveryFrame makeRecoveryFrame(RecoveryContext context, size_t anchor) const;

  void pushRecoveryFrame(RecoveryContext context, size_t anchor) const;

  void popRecoveryFrame() const;

  void markRecoveryConsumed(size_t position) const;

  ZC_NODISCARD zc::Array<cst::RecoveryElement> buildRecoveryElements(
      const cst::VerifiedLexemeStream& lexemes) const;

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

  ast::IdentId internIdent(ParserSyntaxFactory& builder, size_t index) const;

  ast::StringId internString(ParserSyntaxFactory& builder, size_t index) const;

  bool tokenTextEquals(size_t index, zc::StringPtr expected) const;

  bool isSoftKeyword(size_t index, zc::StringPtr expected) const;

  bool isUnsupportedVisibilityModifierSpelling(size_t index, size_t limit) const;

  bool isExternDeclarationStart(size_t index, size_t limit) const;

  bool isSoftDeclarationHead(size_t index, size_t limit) const;

  bool isMarkerImplDeclarationStart(size_t index, size_t limit) const;

  bool parseExternAbi(size_t index, ast::Abi& abi) const;

  bool rangeIsWrapped(size_t start, size_t end, ast::SyntaxKind open, ast::SyntaxKind close) const;

  size_t findMatchingRightBrace(size_t openIndex, size_t limit) const;

  size_t findMatchingRightBracket(size_t openIndex, size_t limit) const;

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

  ast::IdentList makeIdentList(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId makeModulePath(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  bool isModulePathSeparatorAt(size_t index, size_t end) const;

  size_t modulePathSeparatorWidth(size_t index, size_t end) const;

  bool modulePathSeparatorPrecedesGroup(size_t index, size_t end) const;

  size_t findModulePathEnd(size_t start, size_t end) const;

  size_t findModuleSpecifierGroupOpen(size_t pathEnd, size_t end) const;

  ast::NodeId makeImportSpecifier(ParserSyntaxFactory& builder, size_t nameIndex, size_t aliasIndex,
                                  size_t end) const;

  ast::NodeId makeExportSpecifier(ParserSyntaxFactory& builder, size_t nameIndex, size_t aliasIndex,
                                  size_t end) const;

  void recoverModuleSpecifier(TokenCursor& cursor, size_t end) const;

  ast::NodeId parseImportSpecifier(ParserSyntaxFactory& builder, TokenCursor& cursor,
                                   size_t end) const;

  ast::NodeId parseExportSpecifier(ParserSyntaxFactory& builder, TokenCursor& cursor,
                                   size_t end) const;

  zc::Vector<ast::NodeId> parseImportSpecifierList(ParserSyntaxFactory& builder, size_t start,
                                                   size_t end) const;

  zc::Vector<ast::NodeId> parseExportSpecifierList(ParserSyntaxFactory& builder, size_t start,
                                                   size_t end) const;

  ast::NodeId makeAttributePath(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  size_t findAttributePathEnd(size_t start, size_t end) const;

  uint32_t attributePathSegmentCount(size_t start, size_t end) const;

  bool isWhitelistedBareAttribute(size_t start, size_t end) const;

  void diagnoseImportPathSyntax(size_t clauseStart, size_t clauseEnd, size_t pathEnd,
                                size_t groupOpen) const;

  bool isUnavailableConditionalAttributePath(size_t start, size_t end) const;

  bool containsUnmodeledRangeOperator(size_t start, size_t end) const;

  ast::NodeId parseAttribute(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseOuterAttributeList(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId makeStatementListItem(ParserSyntaxFactory& builder, ast::NodeId item,
                                    source::SourceRange range,
                                    ast::NodeId attrs = ast::NodeId()) const;

  void emitUnexpected(const lexer::Token& where) const;

  source::SourceLoc diagnosticLoc(size_t index) const;

  void diagnoseExpressionExpected(size_t index) const;

  bool modifierGroupContains(size_t start, size_t end, ast::SyntaxKind needle, size_t& found) const;

  void diagnoseDeclarationModifierGroup(size_t start, size_t end) const;

  bool diagnoseUnsupportedVarianceInTypeParameters(size_t openAngle, size_t closeAngle) const;

  bool diagnoseTypeParameterListSyntax(size_t openAngle, size_t closeAngle) const;

  void diagnoseDeclarationTypeParameterSyntax(size_t afterName, size_t limit) const;

  ast::NodeId parseWherePredicate(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseWhereClause(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseTypeParameters(ParserSyntaxFactory& builder, size_t start, size_t limit,
                                  ast::NodeId whereClause = ast::NodeId()) const;

  ast::NodeId parseRequiredExpression(ParserSyntaxFactory& builder, size_t start, size_t end) const;

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

  ast::NodeList parseFunctionTypeParameters(ParserSyntaxFactory& builder, size_t start,
                                            size_t end) const;

  ast::NodeId parseTupleTypeRange(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseObjectTypeRange(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  TypeParseResult parseTypeExpression(ParserSyntaxFactory& builder, TokenCursor& cursor,
                                      size_t limit) const;

  TypeParseResult parseTypeExpressionAt(ParserSyntaxFactory& builder, size_t start,
                                        size_t limit) const;

  TypeParseResult parseUnionType(ParserSyntaxFactory& builder, TokenCursor& cursor,
                                 size_t limit) const;

  TypeParseResult parseIntersectionType(ParserSyntaxFactory& builder, TokenCursor& cursor,
                                        size_t limit) const;

  TypeParseResult parsePostfixType(ParserSyntaxFactory& builder, TokenCursor& cursor,
                                   size_t limit) const;

  TypeParseResult parseFunctionType(ParserSyntaxFactory& builder, TokenCursor& cursor,
                                    size_t limit) const;

  bool isDynAssocBindingArgList(size_t openAngle, size_t closeAngle) const;

  TypeParseResult parseDynType(ParserSyntaxFactory& builder, TokenCursor& cursor,
                               size_t limit) const;

  TypeParseResult parseAssociatedTypeProjection(ParserSyntaxFactory& builder, TokenCursor& cursor,
                                                size_t limit) const;

  TypeParseResult parseParenthesizedOrTupleType(ParserSyntaxFactory& builder, TokenCursor& cursor,
                                                size_t limit) const;

  TypeParseResult parseBracketedType(ParserSyntaxFactory& builder, TokenCursor& cursor,
                                     size_t limit) const;

  TypeParseResult parseTypeQuery(ParserSyntaxFactory& builder, TokenCursor& cursor,
                                 size_t limit) const;

  ast::NodeList parseTypeArgumentList(ParserSyntaxFactory& builder, TokenCursor& cursor,
                                      size_t limit, size_t& physicalEnd) const;

  TypeParseResult parseNamedType(ParserSyntaxFactory& builder, TokenCursor& cursor,
                                 size_t limit) const;

  TypeParseResult parseAtomType(ParserSyntaxFactory& builder, TokenCursor& cursor,
                                size_t limit) const;

  ast::NodeId parseTypeRange(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeList parseBoundListMembers(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseTypeParameterBoundList(ParserSyntaxFactory& builder, size_t start,
                                          size_t end) const;

  ast::NodeId parseAssociatedTypeBoundList(ParserSyntaxFactory& builder, size_t start,
                                           size_t end) const;

  size_t consumeCommaDelimitedItem(TokenCursor& cursor, size_t end) const;

  ast::NodeId parseExpressionList(ParserSyntaxFactory& builder, size_t start, size_t end,
                                  ast::SyntaxKind containerKind) const;

  ast::NodeId parseCommaExpression(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeList parseExpressionArguments(ParserSyntaxFactory& builder, size_t start,
                                         size_t end) const;

  ast::NodeList parseTypeArguments(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseNewExpression(ParserSyntaxFactory& builder, size_t start, size_t calleeEnd,
                                 size_t typeArgsEnd, size_t end) const;

  ast::NodeId parseUnsafeBlockExpression(ParserSyntaxFactory& builder, size_t start,
                                         size_t end) const;

  ast::NodeId parseSpawnExpression(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseCastExpression(ParserSyntaxFactory& builder, size_t start, size_t asIndex,
                                  size_t end) const;

  ast::NodeId parseImportCallExpression(ParserSyntaxFactory& builder, size_t start,
                                        size_t openParen, size_t end) const;

  ast::NodeId parseCaptureItem(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseCaptureList(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseFunctionExpression(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseLambdaExpression(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeList parseObjectLiteralProperties(ParserSyntaxFactory& builder, size_t start,
                                             size_t end) const;

  ast::NodeId parseObjectLiteralExpression(ParserSyntaxFactory& builder, size_t start,
                                           size_t end) const;

  ast::NodeId parseStructLiteralExpression(ParserSyntaxFactory& builder, size_t start, size_t brace,
                                           size_t end) const;

  ast::NodeId parseTemplateLiteralExpression(ParserSyntaxFactory& builder, size_t start,
                                             size_t end) const;

  struct ExpressionParseResult {
    ast::NodeId node;
    size_t next = 0;
  };

  ExpressionParseResult parseExpression(ParserSyntaxFactory& builder, TokenCursor& cursor,
                                        size_t limit) const;

  ExpressionParseResult parseExpressionAt(ParserSyntaxFactory& builder, size_t start,
                                          size_t limit) const;

  ExpressionParseResult parseCommaExpressionAt(ParserSyntaxFactory& builder, size_t start,
                                               size_t limit) const;

  ExpressionParseResult parseAssignmentExpressionAt(ParserSyntaxFactory& builder, size_t start,
                                                    size_t limit) const;

  ExpressionParseResult parseConditionalExpressionAt(ParserSyntaxFactory& builder, size_t start,
                                                     size_t limit) const;

  ExpressionParseResult parseErrorDefaultExpressionAt(ParserSyntaxFactory& builder, size_t start,
                                                      size_t limit) const;

  ExpressionParseResult parseNullCoalesceExpressionAt(ParserSyntaxFactory& builder, size_t start,
                                                      size_t limit) const;

  ExpressionParseResult parseBinaryExpressionAt(ParserSyntaxFactory& builder, size_t start,
                                                size_t limit, int32_t minPrecedence) const;

  ExpressionParseResult parseUnaryExpressionAt(ParserSyntaxFactory& builder, size_t start,
                                               size_t limit) const;

  ExpressionParseResult parsePostfixExpressionAt(ParserSyntaxFactory& builder, size_t start,
                                                 size_t limit) const;

  ExpressionParseResult parsePrimaryExpressionAt(ParserSyntaxFactory& builder, size_t start,
                                                 size_t limit) const;

  ast::NodeId parseExpressionRange(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parsePatternRange(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId makeEmptyClassMemberList(ParserSyntaxFactory& builder,
                                       source::SourceRange range) const;

  ast::NodeId makeEmptyEnumVariantList(ParserSyntaxFactory& builder,
                                       source::SourceRange range) const;

  ast::NodeId parseClassMemberList(ParserSyntaxFactory& builder, size_t bodyOpen, size_t bodyClose,
                                   ast::SyntaxKind parentKind) const;

  ast::NodeId parseEnumVariantList(ParserSyntaxFactory& builder, size_t bodyOpen,
                                   size_t bodyClose) const;

  size_t recoverFunctionParameter(TokenCursor& cursor, size_t closeParen) const;

  ast::NodeId parseFunctionParameter(ParserSyntaxFactory& builder, TokenCursor& cursor,
                                     size_t closeParen, size_t parameterOrdinal,
                                     CallableParameterContext context) const;

  ast::NodeList parseFunctionParameterNodeList(ParserSyntaxFactory& builder, size_t openParen,
                                               size_t closeParen,
                                               CallableParameterContext context) const;

  ast::NodeId parseFunctionParameterList(ParserSyntaxFactory& builder, size_t openParen,
                                         size_t closeParen, CallableParameterContext context) const;

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

  SourceElementParseResult parseSourceElement(ParserSyntaxFactory& builder, TokenCursor& cursor,
                                              size_t limit,
                                              SourceElementContext elementContext) const;

  ast::NodeId parseBlock(ParserSyntaxFactory& builder, size_t openBrace, size_t limit,
                         bool allowFinalExpression = false) const;

  ast::NodeId parseStatementBody(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseModuleDeclaration(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseImportDeclaration(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseExportDeclaration(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  struct VariableDeclaratorParseResult {
    ast::NodeId node;
    size_t next = 0;
  };

  size_t consumeVariableDeclaratorPattern(TokenCursor& cursor, size_t limit) const;

  bool isInitializerGenericAngle(size_t openAngle, size_t limit) const;

  size_t consumeVariableInitializer(TokenCursor& cursor, size_t limit) const;

  VariableDeclaratorParseResult parseVariableDeclarator(ParserSyntaxFactory& builder,
                                                        TokenCursor& cursor, size_t limit) const;

  ast::NodeId parseVariableDeclaratorList(ParserSyntaxFactory& builder, size_t start,
                                          size_t end) const;

  ast::NodeId parseLetStatement(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseReturnStatement(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseSuspendStatement(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  size_t findMatchingRightParen(size_t openParen, size_t limit) const;

  void conditionRangeAfterKeyword(size_t start, size_t end, size_t& condStart, size_t& condEnd,
                                  size_t& bodyStart) const;

  ast::NodeId parseIfStatement(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseWhileStatement(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseDoWhileStatement(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseBreakStatement(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseContinueStatement(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseLabeledStatement(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseForStatement(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseForInStatement(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseMatchStatement(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseExternFunctionDecl(ParserSyntaxFactory& builder, size_t start, size_t end,
                                      ast::Abi abi) const;

  ast::NodeId parseExternVarDecl(ParserSyntaxFactory& builder, size_t start, size_t end,
                                 ast::Abi abi) const;

  ast::NodeId parseExternBlockDeclaration(ParserSyntaxFactory& builder, size_t start,
                                          size_t end) const;

  ast::NodeId parseFunctionDeclaration(ParserSyntaxFactory& builder, size_t start, size_t end,
                                       bool isBlockFunction = false) const;

  ast::NodeId parseNamedTypeDeclaration(ParserSyntaxFactory& builder, size_t start, size_t end,
                                        ast::SyntaxKind kind) const;

  ast::NodeId parseErrorDeclaration(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseAliasDeclaration(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseImplInterfaceBound(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId makeImplIfaceList(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseInterfaceHeritage(ParserSyntaxFactory& builder, size_t headerStart,
                                     size_t headerEnd) const;

  ast::NodeId parseStandaloneImplDeclaration(ParserSyntaxFactory& builder, size_t start,
                                             size_t end) const;

  ast::NodeId parseMarkerImplDeclaration(ParserSyntaxFactory& builder, size_t start,
                                         size_t end) const;

  ast::NodeId parseExpressionStatement(ParserSyntaxFactory& builder, size_t start,
                                       size_t end) const;

  ast::NodeId parseExpressionStatementWithoutSemicolon(ParserSyntaxFactory& builder, size_t start,
                                                       size_t end) const;

  ast::NodeId parseSpawnStatement(ParserSyntaxFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseSourceElementOfKind(ParserSyntaxFactory& builder, size_t start, size_t end,
                                       ast::SyntaxKind kind,
                                       SourceElementContext elementContext) const;

  bool canContinueLetInitializerBefore(size_t index) const;

  cst::ParserEventStreamRequest buildEventStream();
};

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
