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

#include "zomlang/compiler/parser/parser.h"

#include "zc/core/common.h"
#include "zc/core/debug.h"
#include "zc/core/function.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/cast.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/factory.h"
#include "zomlang/compiler/ast/kinds.h"
#include "zomlang/compiler/ast/module.h"
#include "zomlang/compiler/ast/operator.h"
#include "zomlang/compiler/ast/statement.h"
#include "zomlang/compiler/ast/type.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/diagnostics/diagnostic.h"
#include "zomlang/compiler/diagnostics/in-flight-diagnostic.h"
#include "zomlang/compiler/lexer/lexer.h"
#include "zomlang/compiler/lexer/token.h"
#include "zomlang/compiler/lexer/utils.h"
#include "zomlang/compiler/source/location.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/compiler/trace/trace.h"

namespace zomlang {
namespace compiler {
namespace parser {

namespace {

// Get AST binary operator precedence directly from token
ast::OperatorPrecedence getBinaryOperatorPrecedence(ast::SyntaxKind tokenKind) {
  switch (tokenKind) {
    case ast::SyntaxKind::BarBar:
      return ast::OperatorPrecedence::kLogicalOr;
    case ast::SyntaxKind::AmpersandAmpersand:
      return ast::OperatorPrecedence::kLogicalAnd;
    case ast::SyntaxKind::Bar:
      return ast::OperatorPrecedence::kBitwiseOr;
    case ast::SyntaxKind::Caret:
      return ast::OperatorPrecedence::kBitwiseXor;
    case ast::SyntaxKind::Ampersand:
      return ast::OperatorPrecedence::kBitwiseAnd;
    case ast::SyntaxKind::EqualsEquals:
    case ast::SyntaxKind::ExclamationEquals:
      return ast::OperatorPrecedence::kEquality;
    case ast::SyntaxKind::LessThan:
    case ast::SyntaxKind::GreaterThan:
    case ast::SyntaxKind::LessThanEquals:
    case ast::SyntaxKind::GreaterThanEquals:
      return ast::OperatorPrecedence::kRelational;
    case ast::SyntaxKind::LessThanLessThan:
    case ast::SyntaxKind::GreaterThanGreaterThan:
      return ast::OperatorPrecedence::kShift;
    case ast::SyntaxKind::Plus:
    case ast::SyntaxKind::Minus:
      return ast::OperatorPrecedence::kAdditive;
    case ast::SyntaxKind::Asterisk:
    case ast::SyntaxKind::Slash:
    case ast::SyntaxKind::Percent:
      return ast::OperatorPrecedence::kMultiplicative;
    case ast::SyntaxKind::AsteriskAsterisk:
      return ast::OperatorPrecedence::kExponentiation;
    default:
      return ast::OperatorPrecedence::kLowest;
  }
}

// Check if token is an assignment operator
bool isAssignmentOperator(ast::SyntaxKind tokenKind) {
  switch (tokenKind) {
    case ast::SyntaxKind::Equals:                                   // =
    case ast::SyntaxKind::PlusEquals:                               // +=
    case ast::SyntaxKind::MinusEquals:                              // -=
    case ast::SyntaxKind::AsteriskEquals:                           // *=
    case ast::SyntaxKind::SlashEquals:                              // /=
    case ast::SyntaxKind::PercentEquals:                            // %=
    case ast::SyntaxKind::AsteriskAsteriskEquals:                   // **=
    case ast::SyntaxKind::LessThanLessThanEquals:                   // <<=
    case ast::SyntaxKind::GreaterThanGreaterThanEquals:             // >>=
    case ast::SyntaxKind::GreaterThanGreaterThanGreaterThanEquals:  // >>>=
    case ast::SyntaxKind::AmpersandEquals:                          // &=
    case ast::SyntaxKind::BarEquals:                                // |=
    case ast::SyntaxKind::CaretEquals:                              // ^=
    case ast::SyntaxKind::AmpersandAmpersandEquals:                 // &&=
    case ast::SyntaxKind::BarBarEquals:                             // ||=
    case ast::SyntaxKind::QuestionQuestionEquals:                   // ??=
      return true;
    default:
      return false;
  }
}

// Check if expression is a left-hand side expression
bool isLeftHandSideExpression(const ast::Expression& expr) {
  // Left-hand side expression checking
  // Valid left-hand side expressions include:
  // - Identifiers
  // - Member expressions (obj.prop)
  // - Parenthesized expressions (if inner is valid LHS)
  // - Array/object destructuring patterns

  switch (expr.getKind()) {
    case ast::SyntaxKind::PropertyAccessExpression:
    case ast::SyntaxKind::ElementAccessExpression:
    case ast::SyntaxKind::NewExpression:
    case ast::SyntaxKind::CallExpression:
    case ast::SyntaxKind::ArrayLiteralExpression:
    case ast::SyntaxKind::ParenthesizedExpression:
    case ast::SyntaxKind::ObjectLiteralExpression:
    case ast::SyntaxKind::CastExpression:
    case ast::SyntaxKind::FunctionExpression:
    case ast::SyntaxKind::ThisExpression:
    case ast::SyntaxKind::SuperExpression:
    case ast::SyntaxKind::Identifier:
    case ast::SyntaxKind::IntegerLiteral:
    case ast::SyntaxKind::FloatLiteral:
    case ast::SyntaxKind::StringLiteral:
    case ast::SyntaxKind::NullLiteral:
    case ast::SyntaxKind::BooleanLiteral:
    case ast::SyntaxKind::ImportDeclaration:
    case ast::SyntaxKind::MissingDeclaration:
      return true;
    default:
      return false;
  }
}

}  // namespace

// ================================================================================
// Parser::Impl

struct Parser::Impl {
  Impl(const source::SourceManager& sourceMgr, diagnostics::DiagnosticEngine& diagnosticEngine,
       const basic::LangOptions& langOpts, basic::StringPool& stringPool,
       const source::BufferId& bufferId)
      : bufferId(bufferId),
        sourceMgr(sourceMgr),
        diagnosticEngine(diagnosticEngine),
        stringPool(stringPool),
        lexer(sourceMgr, diagnosticEngine, langOpts, stringPool, bufferId) {}
  ~Impl() noexcept(false) = default;

  ZC_DISALLOW_COPY_AND_MOVE(Impl);

  /// Helper to get the next token from the lexer
  void nextToken() { lexer.lex(token); }

  /// Helper to look at the current token
  const lexer::Token& peekToken() const { return token; }

  /// Helper to get the current token
  const lexer::Token& getCurrentToken() const { return token; }

  const source::BufferId& bufferId;
  const source::SourceManager& sourceMgr;
  diagnostics::DiagnosticEngine& diagnosticEngine;
  basic::StringPool& stringPool;
  lexer::Lexer lexer;
  lexer::Token token;

  ParsingContexts context;
};

// ================================================================================
// Parser

Parser::Parser(const source::SourceManager& sourceMgr,
               diagnostics::DiagnosticEngine& diagnosticEngine, const basic::LangOptions& langOpts,
               basic::StringPool& stringPool, const source::BufferId& bufferId)
    : impl(zc::heap<Impl>(sourceMgr, diagnosticEngine, langOpts, stringPool, bufferId)) {}

Parser::~Parser() noexcept(false) = default;

// ================================================================================
// Parser State Management (tryParse implementation)

ParserState Parser::mark() {
  auto lexerState = impl->lexer.getCurrentState();
  return ParserState{lexerState, impl->token};
}

void Parser::rewind(const ParserState& state) {
  impl->lexer.restoreState(state.lexerState);
  impl->token = state.token;
}

ParsingContexts Parser::getContext() const { return impl->context; }

void Parser::setContext(ParsingContexts value) { impl->context = value; }

bool Parser::isListTerminator(ParsingContext context) const {
  const lexer::Token token = currentToken();

  if (token.is(ast::SyntaxKind::EndOfFile)) { return true; }

  switch (context) {
    case ParsingContext::BlockElements:
    case ParsingContext::MatchClauses:
    case ParsingContext::InterfaceMembers:
    case ParsingContext::ClassMembers:
    case ParsingContext::EnumMembers:
    case ParsingContext::ObjectLiteralMembers:
    case ParsingContext::ObjectBindingElements:
      return token.is(ast::SyntaxKind::RightBrace);
    case ParsingContext::MatchClauseStatements:
      return token.is(ast::SyntaxKind::RightBrace) || token.is(ast::SyntaxKind::WhenKeyword) ||
             token.is(ast::SyntaxKind::DefaultKeyword);
    case ParsingContext::HeritageClauseElement:
      return token.is(ast::SyntaxKind::LeftBrace) || token.is(ast::SyntaxKind::ExtendsKeyword) ||
             token.is(ast::SyntaxKind::ImplementsKeyword);
    case ParsingContext::VariableDeclarations:
      // If we can consume a semicolon (either explicitly, or with ASI), then consider us done
      // with parsing the list of variable declarators.
      // In the case where we're parsing the variable declarator of a 'for-in' statement, we
      // are done if we see an 'in' keyword in front of us. Same with for-of
      // ERROR RECOVERY TWEAK:
      // For better error recovery, if we see an '->' then we just stop immediately.  We've got an
      // arrow function here and it's going to be very unlikely that we'll resynchronize and get
      // another variable declaration.
      return canParseSemicolon() || token.is(ast::SyntaxKind::InKeyword) ||
             token.is(ast::SyntaxKind::OfKeyword) || token.is(ast::SyntaxKind::Arrow);
    case ParsingContext::TypeParameters:
      // Tokens other than '>' are here for better error recovery
      return token.is(ast::SyntaxKind::GreaterThan) || token.is(ast::SyntaxKind::LeftParen) ||
             token.is(ast::SyntaxKind::LeftBrace) || token.is(ast::SyntaxKind::ExtendsKeyword) ||
             token.is(ast::SyntaxKind::ImplementsKeyword);
    case ParsingContext::ArgumentExpressions:
      // Tokens other than ')' are here for better error recovery
      return token.is(ast::SyntaxKind::RightParen) || token.is(ast::SyntaxKind::Semicolon);
    case ParsingContext::ArrayLiteralMembers:
    case ParsingContext::TupleElementTypes:
    case ParsingContext::ArrayBindingElements:
      return token.is(ast::SyntaxKind::RightBracket);
    case ParsingContext::Parameters:
    case ParsingContext::RestProperties:
      // Tokens other than ')' and ']' (the latter for index signatures) are here for better error
      // recovery
      return token.is(ast::SyntaxKind::RightParen) || token.is(ast::SyntaxKind::RightBracket) ||
             token.is(ast::SyntaxKind::LeftBrace);
    case ParsingContext::TypeArguments:
      // All other tokens should cause the type-argument to terminate except comma token
      return token.is(ast::SyntaxKind::Comma) == false;
    case ParsingContext::HeritageClauses:
      return token.is(ast::SyntaxKind::LeftBrace) || token.is(ast::SyntaxKind::RightBrace);
    default:
      return false;
  }
}

bool Parser::isListElement(ParsingContext context, bool inErrorRecovery) {
  const lexer::Token& token = currentToken();

  switch (context) {
    case ParsingContext::SourceElements:
    case ParsingContext::BlockElements:
    case ParsingContext::MatchClauseStatements:
      // If we're in error recovery, then we don't want to treat ';' as an empty statement.
      // The problem is that ';' can show up in far too many contexts, and if we see one
      // and assume it's a statement, then we may bail out inappropriately from whatever
      // we're parsing.  For example, if we have a semicolon in the middle of a class, then
      // we really don't want to assume the class is over and we're on a statement in the
      // outer module.  We just want to consume and move on.
      return !(token.is(ast::SyntaxKind::Semicolon) && inErrorRecovery) && isStartOfStatement();
    case ParsingContext::MatchClauses:
      return token.is(ast::SyntaxKind::WhenKeyword) || token.is(ast::SyntaxKind::CaseKeyword) ||
             token.is(ast::SyntaxKind::DefaultKeyword);
    case ParsingContext::InterfaceMembers:
    case ParsingContext::ClassMembers:
      // Try to check if this is the start of a member by attempting to parse it
      return lookAhead<bool>(ZC_BIND_METHOD(*this, isStartOfInterfaceOrClassMember));
    case ParsingContext::EnumMembers:
      return isLiteralPropertyName();
    case ParsingContext::ObjectLiteralMembers:
      switch (token.getKind()) {
        case ast::SyntaxKind::LeftBracket:
        case ast::SyntaxKind::Asterisk:
        case ast::SyntaxKind::DotDotDot:
        case ast::SyntaxKind::Period:  // Not an object literal member, but don't want to close the
                                       // object
          return true;
        default:
          return isLiteralPropertyName();
      }
    case ParsingContext::RestProperties:
      return isLiteralPropertyName();
    case ParsingContext::ObjectBindingElements:
      return token.is(ast::SyntaxKind::LeftBracket) || token.is(ast::SyntaxKind::DotDotDot) ||
             isLiteralPropertyName();
    case ParsingContext::HeritageClauseElement:
      // If we see `{ ... }` then only consume it as an expression if it is followed by `,` or `{`
      // That way we won't consume the body of a class in its heritage clause.
      if (token.is(ast::SyntaxKind::LeftBrace)) {
        return lookAhead<bool>(ZC_BIND_METHOD(*this, isValidHeritageClauseObjectLiteral));
      }

      if (!inErrorRecovery) {
        return isStartOfLeftHandSideExpression() && !isHeritageClauseExtendsOrImplementsKeyword();
      } else {
        // If we're in error recovery we tighten up what we're willing to match.
        // That way we don't treat something like "this" as a valid heritage clause
        // element during recovery.
        return isIdentifier() && !isHeritageClauseExtendsOrImplementsKeyword();
      }
    case ParsingContext::VariableDeclarations:
      return isBindingIdentifierOrPattern();
    case ParsingContext::ArrayBindingElements:
      return token.is(ast::SyntaxKind::Comma) || token.is(ast::SyntaxKind::DotDotDot) ||
             isBindingIdentifierOrPattern();
    case ParsingContext::TypeParameters:
      return token.is(ast::SyntaxKind::InKeyword) || token.is(ast::SyntaxKind::ConstKeyword) ||
             isIdentifier();
    case ParsingContext::ArrayLiteralMembers:
      switch (token.getKind()) {
        case ast::SyntaxKind::Comma:
        case ast::SyntaxKind::Period:  // Not an array literal member, but don't want to close the
                                       // array
          return true;
        default:
          break;
      }
      ZC_FALLTHROUGH;
    case ParsingContext::ArgumentExpressions:
      return token.is(ast::SyntaxKind::DotDotDot) || isStartOfExpression();
    case ParsingContext::Parameters:
      return isStartOfParameter();
    case ParsingContext::TypeArguments:
    case ParsingContext::TupleElementTypes:
      return token.is(ast::SyntaxKind::Comma) || isStartOfType();
    case ParsingContext::HeritageClauses:
      return isHeritageClause();
    case ParsingContext::Count:
    default:
      ZC_UNREACHABLE;
  }
}

bool Parser::abortParsingListOrMoveToNextToken(ParsingContext context) {
  parsingContextErrors(context);
  if (isInSomeParsingContext()) { return true; }
  nextToken();
  return false;
}

bool Parser::isInSomeParsingContext() {
  ZC_DASSERT(impl->context != 0, "Parser is not in any parsing context.");

  for (uint32_t i = 0; i < static_cast<uint32_t>(ParsingContext::Count); ++i) {
    if ((static_cast<uint32_t>(impl->context) & (1u << i)) != 0) {
      ParsingContext kind = static_cast<ParsingContext>(i);
      if (isListElement(kind, /*inErrorRecovery*/ true) || isListTerminator(kind)) { return true; }
    }
  }

  return false;
}

void Parser::parsingContextErrors(ParsingContext context) {
  switch (context) {
    case ParsingContext::SourceElements:
      if (currentToken().is(ast::SyntaxKind::DefaultKeyword)) {
        parseErrorAtCurrentToken<diagnostics::DiagID::ExpectedToken>("export"_zc);
      } else {
        parseErrorAtCurrentToken<diagnostics::DiagID::DeclarationOrStatementExpected>();
      }
      break;
    case ParsingContext::BlockElements:
      parseErrorAtCurrentToken<diagnostics::DiagID::DeclarationOrStatementExpected>();
      break;
    case ParsingContext::MatchClauses:
      parseErrorAtCurrentToken<diagnostics::DiagID::CaseOrDefaultExpected>();
      break;
    case ParsingContext::MatchClauseStatements:
      parseErrorAtCurrentToken<diagnostics::DiagID::StatementExpected>();
      break;
    case ParsingContext::RestProperties:
    case ParsingContext::InterfaceMembers:
      parseErrorAtCurrentToken<diagnostics::DiagID::PropertyOrSignatureExpected>();
      break;
    case ParsingContext::ClassMembers:
      parseErrorAtCurrentToken<diagnostics::DiagID::UnexpectedTokenClassMemberExpected>();
      break;
    case ParsingContext::EnumMembers:
      parseErrorAtCurrentToken<diagnostics::DiagID::EnumMemberExpected>();
      break;
    case ParsingContext::HeritageClauseElement:
      parseErrorAtCurrentToken<diagnostics::DiagID::ExpressionExpected>();
      break;
    case ParsingContext::VariableDeclarations: {
      const auto kind = currentToken().getKind();
      if (kind >= ast::SyntaxKind::FirstKeyword && kind <= ast::SyntaxKind::LastKeyword) {
        parseErrorAtCurrentToken<diagnostics::DiagID::VariableDeclarationNameNotAllowed>(
            currentToken().getText(getSourceManager()));
      } else {
        parseErrorAtCurrentToken<diagnostics::DiagID::VariableDeclarationExpected>();
      }
      break;
    }
    case ParsingContext::ObjectBindingElements:
      parseErrorAtCurrentToken<diagnostics::DiagID::PropertyDestructuringPatternExpected>();
      break;
    case ParsingContext::ArrayBindingElements:
      parseErrorAtCurrentToken<diagnostics::DiagID::ArrayElementDestructuringPatternExpected>();
      break;
    case ParsingContext::ArgumentExpressions:
      parseErrorAtCurrentToken<diagnostics::DiagID::ArgumentExpressionExpected>();
      break;
    case ParsingContext::ObjectLiteralMembers:
      parseErrorAtCurrentToken<diagnostics::DiagID::PropertyAssignmentExpected>();
      break;
    case ParsingContext::ArrayLiteralMembers:
      parseErrorAtCurrentToken<diagnostics::DiagID::ExpressionOrCommaExpected>();
      break;
    case ParsingContext::Parameters: {
      const auto kind = currentToken().getKind();
      if (kind >= ast::SyntaxKind::FirstKeyword && kind <= ast::SyntaxKind::LastKeyword) {
        parseErrorAtCurrentToken<diagnostics::DiagID::ParameterNameNotAllowed>(
            currentToken().getText(getSourceManager()));
      } else {
        parseErrorAtCurrentToken<diagnostics::DiagID::ParameterDeclarationExpected>();
      }
      break;
    }
    case ParsingContext::TypeParameters:
      parseErrorAtCurrentToken<diagnostics::DiagID::TypeParameterDeclarationExpected>();
      break;
    case ParsingContext::TypeArguments:
      parseErrorAtCurrentToken<diagnostics::DiagID::TypeArgumentExpected>();
      break;
    case ParsingContext::TupleElementTypes:
      parseErrorAtCurrentToken<diagnostics::DiagID::TypeExpected>();
      break;
    case ParsingContext::HeritageClauses:
      parseErrorAtCurrentToken<diagnostics::DiagID::UnexpectedTokenExpected>();
      break;
    default:
      break;
  }
}

void Parser::initializeState() {
  // Set initial context
  impl->context = 0;
}

zc::Maybe<zc::Own<ast::Node>> Parser::parse() {
  trace::FunctionTracer functionTracer(trace::TraceCategory::kParser, __FUNCTION__);

  initializeState();

  ZC_IF_SOME(sourceFileNode, parseSourceFile()) {
    trace::traceEvent(trace::TraceCategory::kParser, "Parse completed successfully");
    return zc::mv(sourceFileNode);
  }

  trace::traceEvent(trace::TraceCategory::kParser, "Parse failed");

  return zc::none;
}

zc::Maybe<zc::Own<ast::TypeQueryNode>> Parser::parseTypeQuery() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseTypeQuery");

  // Parse type query according to the grammar rule:
  // typeQuery: TYPEOF typeQueryExpression
  // This handles type queries like typeof myVar or typeof MyClass.prop

  const lexer::Token& token = currentToken();
  const source::SourceLoc startLoc = token.getLocation();

  if (!token.is(ast::SyntaxKind::TypeOfKeyword)) { return zc::none; }

  nextToken();  // consume 'typeof'

  ZC_IF_SOME(queryExpr, parseTypeQueryExpression()) {
    return finishNode(ast::factory::createTypeQuery(zc::mv(queryExpr)), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseTypeQueryExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseTypeQueryExpression");

  // Parse type query expression according to the grammar rule:
  // typeQueryExpression: identifier (PERIOD identifier)*
  // This handles expressions like 'MyClass' or 'MyClass.field' in typeof queries

  const source::SourceLoc startLoc = currentToken().getLocation();

  ZC_IF_SOME(firstId, parseBindingIdentifier()) {
    zc::Own<ast::LeftHandSideExpression> result = zc::mv(firstId);

    // Parse additional identifiers separated by periods
    while (expectToken(ast::SyntaxKind::Period)) {
      nextToken();  // consume '.'

      ZC_IF_SOME(nextId, parseBindingIdentifier()) {
        // Use PropertyAccessExpression for member access
        auto propAccess = finishNode(
            ast::factory::createPropertyAccessExpression(zc::mv(result), zc::mv(nextId), false),
            startLoc);
        result = zc::mv(propAccess);
      }
      else {
        // Error: expected identifier after '.'
        return zc::none;
      }
    }

    return finishNode(zc::mv(result), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::TypeNode>> Parser::parseRaisesClause() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseRaisesClause");

  // Parse raises clause according to the grammar rule:
  // raisesClause: RAISES type
  // This handles error type specifications in function types and declarations
  // Example: (x: i32) -> i32 raises ErrorType

  if (!expectToken(ast::SyntaxKind::RaisesKeyword)) { return zc::none; }

  // Parse the first error type
  return parseType();
}

zc::Maybe<zc::Own<ast::SourceFile>> Parser::parseSourceFile() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseSourceFile");

  // sourceFile: module;
  // module: moduleBody?;
  // moduleBody: moduleItemList;
  // moduleItemList: moduleItem+;

  // Prim lexer to get the first token
  nextToken();

  source::SourceLoc startLoc = currentToken().getLocation();

  zc::Vector<zc::Own<ast::Statement>> statements = parseList<ast::Statement>(
      ParsingContext::SourceElements, ZC_BIND_METHOD(*this, parseModuleItem));

  trace::traceCounter(trace::TraceCategory::kParser, "Module items parsed"_zc,
                      zc::str(statements.size()));

  // Create the source file node
  zc::StringPtr fileName = getSourceManager().getIdentifierForBuffer(impl->bufferId);
  zc::Own<ast::SourceFile> sourceFile =
      finishNode(ast::factory::createSourceFile(zc::str(fileName), zc::mv(statements)), startLoc);

  trace::traceEvent(trace::TraceCategory::kParser, "Source file created"_zc, fileName);
  return finishNode(zc::mv(sourceFile), startLoc);
}

zc::Maybe<zc::Own<ast::Statement>> Parser::parseModuleItem() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseModuleItem");

  // moduleItem:
  //   statementListItem
  //   | exportDeclaration
  //   | importDeclaration;

  // Check for import declaration
  if (expectToken(ast::SyntaxKind::ImportKeyword)) {
    ZC_IF_SOME(importDecl, parseImportDeclaration()) { return zc::mv(importDecl); }
  }

  // Check for export declaration
  if (expectToken(ast::SyntaxKind::ExportKeyword)) {
    ZC_IF_SOME(exportDecl, parseExportDeclaration()) { return zc::mv(exportDecl); }
  }

  // Otherwise, parse as statement (statementListItem)
  return parseStatement();
}

zc::Maybe<zc::Own<ast::ImportDeclaration>> Parser::parseImportDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseImportDeclaration");

  // importDeclaration: IMPORT bindingIdentifier ASSIGN modulePath;
  const lexer::Token& token = currentToken();

  // Expect IMPORT token
  if (!token.is(ast::SyntaxKind::ImportKeyword)) { return zc::none; }

  source::SourceLoc startLoc = token.getLocation();
  nextToken();  // consume IMPORT

  // Parse bindingIdentifier
  ZC_IF_SOME(bindingIdentifier, parseBindingIdentifier()) {
    // Expect ASSIGN token
    if (!currentToken().is(ast::SyntaxKind::Equals)) {
      // Error: expected '=' after binding identifier
      return zc::none;
    }
    nextToken();  // consume ASSIGN (=)

    // Parse modulePath
    ZC_IF_SOME(modulePath, parseModulePath()) {
      // Create ImportDeclaration with bindingIdentifier and modulePath
      return finishNode(
          ast::factory::createImportDeclaration(zc::mv(modulePath), zc::mv(bindingIdentifier)),
          startLoc);
    }
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::ModulePath>> Parser::parseModulePath() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseModulePath");

  // modulePath: stringLiteral;

  const lexer::Token& token = currentToken();
  source::SourceLoc startLoc = token.getLocation();

  ZC_IF_SOME(stringLiteral, parseStringLiteral()) {
    // Create ModulePath with the string literal
    return finishNode(ast::factory::createModulePath(zc::mv(stringLiteral)), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::ExportDeclaration>> Parser::parseExportDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseExportDeclaration");

  // exportDeclaration: EXPORT bindingIdentifier (PERIOD bindingIdentifier)* (AS identifierName)?;
  const lexer::Token& token = currentToken();

  // Expect EXPORT token
  if (!token.is(ast::SyntaxKind::ExportKeyword)) { return zc::none; }

  source::SourceLoc startLoc = token.getLocation();
  nextToken();  // consume EXPORT

  // Parse first bindingIdentifier
  ZC_IF_SOME(identifier, parseBindingIdentifier()) {
    zc::Own<ast::LeftHandSideExpression> exportPath = zc::mv(identifier);

    // Parse optional (PERIOD bindingIdentifier)*
    while (currentToken().is(ast::SyntaxKind::Period)) {
      nextToken();  // consume PERIOD

      ZC_IF_SOME(nextIdentifier, parseBindingIdentifier()) {
        // Create PropertyAccessExpression for a.b.c pattern
        exportPath = ast::factory::createPropertyAccessExpression(zc::mv(exportPath),
                                                                  zc::mv(nextIdentifier), false);
      }
      else {
        return zc::none;  // Expected identifier after period
      }
    }

    // Parse optional (AS identifierName)?
    zc::Maybe<zc::Own<ast::Identifier>> alias = zc::none;
    if (currentToken().is(ast::SyntaxKind::AsKeyword)) {
      nextToken();  // consume AS

      ZC_IF_SOME(aliasIdentifier, parseIdentifierName()) { alias = zc::mv(aliasIdentifier); }
      else {
        return zc::none;  // Expected identifier after AS
      }
    }

    // Create ExportDeclaration - convert LeftHandSideExpression to Expression
    return finishNode(ast::factory::createExportDeclaration(zc::mv(exportPath), zc::mv(alias)),
                      startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Statement>> Parser::parseStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseStatement");

  // statementListItem: statement | declaration;
  // statement:
  //   blockStatement
  //   | emptyStatement
  //   | expressionStatement
  //   | ifStatement
  //   | matchStatement
  //   | breakableStatement
  //   | continueStatement
  //   | breakStatement
  //   | returnStatement
  //   | debuggerStatement;

  // Check for different statement types
  switch (currentToken().getKind()) {
    case ast::SyntaxKind::LeftBrace:
      return parseBlockStatement();
    case ast::SyntaxKind::Semicolon:
      return parseEmptyStatement();
    case ast::SyntaxKind::IfKeyword:
      return parseIfStatement();
    case ast::SyntaxKind::WhileKeyword:
      return parseWhileStatement();
    case ast::SyntaxKind::ForKeyword:
      return parseForStatement();
    case ast::SyntaxKind::BreakKeyword:
      return parseBreakStatement();
    case ast::SyntaxKind::ContinueKeyword:
      return parseContinueStatement();
    case ast::SyntaxKind::ReturnKeyword:
      return parseReturnStatement();
    case ast::SyntaxKind::MatchKeyword:
      return parseMatchStatement();
    case ast::SyntaxKind::LetKeyword:
    case ast::SyntaxKind::ConstKeyword:
      return parseVariableStatement();
    case ast::SyntaxKind::FunKeyword:
      return parseFunctionDeclaration();
    case ast::SyntaxKind::ClassKeyword:
      return parseClassDeclaration();
    case ast::SyntaxKind::InterfaceKeyword:
      return parseInterfaceDeclaration();
    case ast::SyntaxKind::StructKeyword:
      return parseStructDeclaration();
    case ast::SyntaxKind::EnumKeyword:
      return parseEnumDeclaration();
    case ast::SyntaxKind::ErrorKeyword:
      return parseErrorDeclaration();
    case ast::SyntaxKind::AliasKeyword:
      return parseAliasDeclaration();
    case ast::SyntaxKind::DebuggerKeyword:
      return parseDebuggerStatement();
    default:
      // Try to parse as expression statement
      return parseExpressionStatement();
  }
}

bool Parser::isStartOfStatement() {
  const lexer::Token& token = currentToken();

  switch (token.getKind()) {
    // Punctuation that can start statements
    case ast::SyntaxKind::At:         // @decorator
    case ast::SyntaxKind::Semicolon:  // empty statement
    case ast::SyntaxKind::LeftBrace:  // block statement
    // Keywords that start statements
    case ast::SyntaxKind::LetKeyword:       // let declaration
    case ast::SyntaxKind::FunKeyword:       // function declaration
    case ast::SyntaxKind::ClassKeyword:     // class declaration
    case ast::SyntaxKind::EnumKeyword:      // enum declaration
    case ast::SyntaxKind::IfKeyword:        // if statement
    case ast::SyntaxKind::DoKeyword:        // do statement
    case ast::SyntaxKind::WhileKeyword:     // while statement
    case ast::SyntaxKind::ForKeyword:       // for statement
    case ast::SyntaxKind::ContinueKeyword:  // continue statement
    case ast::SyntaxKind::BreakKeyword:     // break statement
    case ast::SyntaxKind::ReturnKeyword:    // return statement
    case ast::SyntaxKind::WithKeyword:      // with statement
    case ast::SyntaxKind::SwitchKeyword:    // switch statement
    case ast::SyntaxKind::MatchKeyword:     // match statement
    case ast::SyntaxKind::ThrowKeyword:     // throw statement
    case ast::SyntaxKind::TryKeyword:       // try statement
    case ast::SyntaxKind::DebuggerKeyword:  // debugger statement
      return true;

    // Keywords that might start statements depending on context
    case ast::SyntaxKind::ImportKeyword:
      return isStartOfDeclaration();

    case ast::SyntaxKind::ConstKeyword:
    case ast::SyntaxKind::ExportKeyword:
      return isStartOfDeclaration();

    // Access modifiers and other contextual keywords
    case ast::SyntaxKind::AsyncKeyword:
    case ast::SyntaxKind::DeclareKeyword:
    case ast::SyntaxKind::InterfaceKeyword:
    case ast::SyntaxKind::ModuleKeyword:
    case ast::SyntaxKind::NamespaceKeyword:
    case ast::SyntaxKind::GlobalKeyword:
      return true;

    case ast::SyntaxKind::AccessorKeyword:
    case ast::SyntaxKind::PublicKeyword:
    case ast::SyntaxKind::PrivateKeyword:
    case ast::SyntaxKind::ProtectedKeyword:
    case ast::SyntaxKind::StaticKeyword:
    case ast::SyntaxKind::ReadonlyKeyword:
    case ast::SyntaxKind::AbstractKeyword:
    case ast::SyntaxKind::OverrideKeyword:
      return isStartOfDeclaration() ||
             !lookAhead<bool>([this]() { return nextTokenIsIdentifierOrKeywordOnSameLine(); });

    // Using keyword for using declarations
    case ast::SyntaxKind::UsingKeyword:
      return true;

    default:
      // Check if it's the start of an expression (which can be an expression statement)
      return isStartOfExpression();
  }
}

bool Parser::isStartOfLeftHandSideExpression() {
  const lexer::Token& token = currentToken();

  switch (token.getKind()) {
    // Keywords that can start left-hand side expressions
    case ast::SyntaxKind::ThisKeyword:
    case ast::SyntaxKind::SuperKeyword:
    case ast::SyntaxKind::NullKeyword:
    case ast::SyntaxKind::TrueKeyword:
    case ast::SyntaxKind::FalseKeyword:
    case ast::SyntaxKind::IntegerLiteral:
    case ast::SyntaxKind::FloatLiteral:
    case ast::SyntaxKind::StringLiteral:
    case ast::SyntaxKind::LeftParen:    // Parenthesized expressions
    case ast::SyntaxKind::LeftBracket:  // Array literals
    case ast::SyntaxKind::LeftBrace:    // Object literals
    case ast::SyntaxKind::FunKeyword:
    case ast::SyntaxKind::ClassKeyword:
    case ast::SyntaxKind::NewKeyword:
    case ast::SyntaxKind::Slash:
    case ast::SyntaxKind::SlashEquals:
    case ast::SyntaxKind::Identifier:
      return true;
    case ast::SyntaxKind::ImportKeyword:
      // Check if next token is left paren, less than, or dot by attempting to parse
      return nextTokenIsLeftParenOrLessThanOrDot();
    default:
      return isIdentifier();
  }
}

bool Parser::isStartOfExpression() {
  // First check if it's a left-hand side expression
  if (isStartOfLeftHandSideExpression()) { return true; }

  const lexer::Token& token = currentToken();

  switch (token.getKind()) {
    // Unary operators
    case ast::SyntaxKind::Plus:
    case ast::SyntaxKind::Minus:
    case ast::SyntaxKind::Tilde:
    case ast::SyntaxKind::Exclamation:
    case ast::SyntaxKind::DeleteKeyword:
    case ast::SyntaxKind::TypeOfKeyword:
    case ast::SyntaxKind::VoidKeyword:
    case ast::SyntaxKind::PlusPlus:
    case ast::SyntaxKind::MinusMinus:
    case ast::SyntaxKind::LessThan:  // Type assertions
    case ast::SyntaxKind::AwaitKeyword:
    case ast::SyntaxKind::YieldKeyword:
    case ast::SyntaxKind::At:  // Decorators
      return true;

    default:
      // Error tolerance.  If we see the start of some binary operator, we consider
      // that the start of an expression.  That way we'll parse out a missing identifier,
      // give a good message about an identifier being missing, and then consume the
      // rest of the binary expression.
      if (isBinaryOperator()) { return true; }

      return isIdentifier();
  }
}

bool Parser::isStartOfDeclaration() {
  return lookAhead<bool>(ZC_BIND_METHOD(*this, scanStartOfDeclaration));
}

// ================================================================================
// Utility methods

ZC_ALWAYS_INLINE(diagnostics::DiagnosticEngine& Parser::getDiagnosticEngine() const) {
  return impl->diagnosticEngine;
}

ZC_ALWAYS_INLINE(const source::SourceManager& Parser::getSourceManager() const) {
  return impl->sourceMgr;
}

ZC_ALWAYS_INLINE(const lexer::Token& Parser::currentToken() const) { return impl->peekToken(); }

ZC_ALWAYS_INLINE(const source::SourceLoc Parser::currentLoc() const) {
  return impl->peekToken().getLocation();
}

ZC_ALWAYS_INLINE(void Parser::nextToken()) { impl->nextToken(); }

ZC_ALWAYS_INLINE(bool Parser::expectToken(ast::SyntaxKind kind)) {
  const lexer::Token& token = currentToken();
  return token.is(kind);
}

ZC_ALWAYS_INLINE(bool Parser::consumeExpectedToken(ast::SyntaxKind kind)) {
  if (expectToken(kind)) {
    nextToken();
    return true;
  }
  return false;
}

bool Parser::isUpdateExpression() const {
  // Check if the current token can possibly be an increment expression.
  // This function is called inside parseUnaryExpression to decide
  // whether to call parseSimpleUnaryExpression or call parseUpdateExpression directly
  switch (currentToken().getKind()) {
    case ast::SyntaxKind::Plus:
    case ast::SyntaxKind::Minus:
    case ast::SyntaxKind::Tilde:
    case ast::SyntaxKind::Exclamation:
    case ast::SyntaxKind::DeleteKeyword:
    case ast::SyntaxKind::TypeOfKeyword:
    case ast::SyntaxKind::VoidKeyword:
    case ast::SyntaxKind::AwaitKeyword:
      return false;
    case ast::SyntaxKind::LessThan:
      // < can be used for generic type arguments or comparison operators
      // Both are not unary expressions, so this should be handled as update expression
      ZC_FALLTHROUGH;
    default:
      return true;
  }
}

bool Parser::isBindingIdentifier() const { return currentToken().is(ast::SyntaxKind::Identifier); }

bool Parser::isIdentifier() const { return currentToken().is(ast::SyntaxKind::Identifier); }

bool Parser::isLiteralPropertyName() const {
  const lexer::Token& token = currentToken();
  // Token is Identifier or Keyword
  return token.is(ast::SyntaxKind::Identifier) ||
         (token.getKind() >= ast::SyntaxKind::FirstKeyword &&
          token.getKind() <= ast::SyntaxKind::LastKeyword);
}

bool Parser::isBindingIdentifierOrPattern() const {
  const lexer::Token& token = currentToken();
  return token.is(ast::SyntaxKind::LeftBrace) || token.is(ast::SyntaxKind::LeftBracket) ||
         isBindingIdentifier();
}

bool Parser::isModifier() const {
  const lexer::Token& token = currentToken();
  return token.is(ast::SyntaxKind::AbstractKeyword) || token.is(ast::SyntaxKind::ConstKeyword) ||
         token.is(ast::SyntaxKind::ExportKeyword) || token.is(ast::SyntaxKind::PublicKeyword) ||
         token.is(ast::SyntaxKind::PrivateKeyword) || token.is(ast::SyntaxKind::ProtectedKeyword) ||
         token.is(ast::SyntaxKind::StaticKeyword) || token.is(ast::SyntaxKind::ReadonlyKeyword) ||
         token.is(ast::SyntaxKind::AsyncKeyword) || token.is(ast::SyntaxKind::OverrideKeyword);
}

bool Parser::isBinaryOperator() const {
  return getBinaryOperatorPrecedence(currentToken().getKind()) > ast::OperatorPrecedence::kLowest;
}

bool Parser::isIdentifierOrKeyword() const {
  const lexer::Token token = currentToken();
  return token.is(ast::SyntaxKind::Identifier) ||
         (token.getKind() >= ast::SyntaxKind::FirstKeyword &&
          token.getKind() <= ast::SyntaxKind::LastKeyword);
}

bool Parser::isStartOfParameter() {
  const lexer::Token token = currentToken();
  return token.is(ast::SyntaxKind::DotDotDot) || isBindingIdentifierOrPattern() || isModifier() ||
         token.is(ast::SyntaxKind::At) || isStartOfType();
}

bool Parser::nextTokenIsNumericLiteral() {
  nextToken();
  return currentToken().is(ast::SyntaxKind::IntegerLiteral) ||
         currentToken().is(ast::SyntaxKind::FloatLiteral);
}

bool Parser::nextIsParenthesizedOrFunctionType() {
  const lexer::Token token = currentToken();
  return token.is(ast::SyntaxKind::RightParen) || isStartOfParameter() || isStartOfType();
}

bool Parser::nextTokenIsLeftParenOrLessThanOrDot() {
  nextToken();
  switch (currentToken().getKind()) {
    case ast::SyntaxKind::LeftParen:
    case ast::SyntaxKind::LessThan:
    case ast::SyntaxKind::Period:
      return true;
    default:
      return false;
  }
}

bool Parser::nextTokenIsLeftParenOrLessThan() {
  nextToken();
  return expectToken(ast::SyntaxKind::LeftParen) || expectToken(ast::SyntaxKind::LessThan);
}

bool Parser::nextTokenIsIdentifierOrKeywordOrLeftBracket() {
  nextToken();
  return isIdentifierOrKeyword() || expectToken(ast::SyntaxKind::LeftBracket);
}

bool Parser::isStartOfType(bool inStartOfParameter) {
  const lexer::Token& token = currentToken();
  switch (token.getKind()) {
    case ast::SyntaxKind::AnyKeyword:
    case ast::SyntaxKind::StrKeyword:
    case ast::SyntaxKind::I8Keyword:
    case ast::SyntaxKind::I16Keyword:
    case ast::SyntaxKind::I32Keyword:
    case ast::SyntaxKind::I64Keyword:
    case ast::SyntaxKind::U8Keyword:
    case ast::SyntaxKind::U16Keyword:
    case ast::SyntaxKind::U32Keyword:
    case ast::SyntaxKind::U64Keyword:
    case ast::SyntaxKind::BoolKeyword:
    case ast::SyntaxKind::ReadonlyKeyword:
    case ast::SyntaxKind::VoidKeyword:
    case ast::SyntaxKind::NullKeyword:
    case ast::SyntaxKind::ThisKeyword:
    case ast::SyntaxKind::TypeOfKeyword:
    case ast::SyntaxKind::LeftBrace:
    case ast::SyntaxKind::LeftBracket:
    case ast::SyntaxKind::LessThan:
    case ast::SyntaxKind::Bar:
    case ast::SyntaxKind::Ampersand:
    case ast::SyntaxKind::NewKeyword:
    case ast::SyntaxKind::StringLiteral:
    case ast::SyntaxKind::IntegerLiteral:
    case ast::SyntaxKind::FloatLiteral:
    case ast::SyntaxKind::TrueKeyword:
    case ast::SyntaxKind::FalseKeyword:
    case ast::SyntaxKind::ObjectKeyword:
    case ast::SyntaxKind::Asterisk:
    case ast::SyntaxKind::Question:
    case ast::SyntaxKind::Exclamation:
    case ast::SyntaxKind::DotDotDot:
    case ast::SyntaxKind::InferKeyword:
    case ast::SyntaxKind::ImportKeyword:
    case ast::SyntaxKind::AssertsKeyword:
      return true;
    case ast::SyntaxKind::FunKeyword:
      return !inStartOfParameter;
    case ast::SyntaxKind::Minus:
      return !inStartOfParameter && nextTokenIsNumericLiteral();
    case ast::SyntaxKind::LeftParen:
      // Only consider '(' the start of a type if followed by ')', '...', an identifier, a modifier,
      // or something that starts a type. We don't want to consider things like '(1)' a type.
      return !inStartOfParameter && nextIsParenthesizedOrFunctionType();
    default:
      return isIdentifier();
  }
}

bool Parser::isStartOfInterfaceOrClassMember() {
  while (isModifier()) { nextToken(); }

  const lexer::Token& token = currentToken();

  // If we found a fun, let or const keyword, then we're at the start of a member declaration
  if (token.is(ast::SyntaxKind::FunKeyword) || token.is(ast::SyntaxKind::LetKeyword) ||
      token.is(ast::SyntaxKind::ConstKeyword)) {
    return true;
  }

  // Consume fun/let/const keyword
  nextToken();
  // Then we found a literal property name, so we've got a statement like `fun/let/const id`
  if (!isLiteralPropertyName()) { return false; }

  // Consume property name
  nextToken();

  // Then we found a left paren, less than, question, colon, or semicolon, so we've got a
  // statement like `fun/let/const id()` or `fun/let/const id<T>` or `fun id?()`, etc.
  return token.is(ast::SyntaxKind::LeftParen) || token.is(ast::SyntaxKind::LessThan) ||
         token.is(ast::SyntaxKind::Question) || token.is(ast::SyntaxKind::Colon) ||
         canParseSemicolon();
}

bool Parser::isStartOfOptionalPropertyOrElementAccessChain() {
  return expectToken(ast::SyntaxKind::QuestionDot) && nextTokenIsIdentifierOrKeywordOrLeftBracket();
}

bool Parser::isHeritageClause() const {
  const lexer::Token& token = currentToken();
  return token.is(ast::SyntaxKind::ExtendsKeyword) || token.is(ast::SyntaxKind::ImplementsKeyword);
}

bool Parser::isImportAttributeName() const {
  const lexer::Token& token = currentToken();
  return token.is(ast::SyntaxKind::Identifier) || token.is(ast::SyntaxKind::StringLiteral);
}

bool Parser::isHeritageClauseExtendsOrImplementsKeyword() const {
  const lexer::Token& token = currentToken();
  return token.is(ast::SyntaxKind::ExtendsKeyword) || token.is(ast::SyntaxKind::ImplementsKeyword);
}

bool Parser::isValidHeritageClauseObjectLiteral() {
  ZC_IREQUIRE(currentToken().is(ast::SyntaxKind::LeftBrace));

  currentToken();
  if (currentToken().is(ast::SyntaxKind::RightBrace)) {
    // if we see "extends {}" then only treat the {} as what we're extending (and not
    // the class body) if we have:
    //
    //      extends {} {
    //      extends {},
    //      extends {} extends
    //      extends {} implements

    nextToken();
    const lexer::Token next = currentToken();
    return next.is(ast::SyntaxKind::Comma) || next.is(ast::SyntaxKind::LeftBrace) ||
           next.is(ast::SyntaxKind::ExtendsKeyword) || next.is(ast::SyntaxKind::ImplementsKeyword);
  }

  return true;
}

lexer::Token Parser::lookAhead(unsigned n) {
  if (n == 0) { return currentToken(); }

  ParserState state = mark();
  for (unsigned i = 0; i < n; ++i) { nextToken(); }
  lexer::Token token = currentToken();
  rewind(state);
  return token;
}

bool Parser::canLookAhead(unsigned n) { return true; }

bool Parser::isLookAhead(unsigned n, ast::SyntaxKind kind) { return lookAhead(n).is(kind); }

template <typename T>
T Parser::lookAhead(zc::Function<T()> callback) {
  ParserState state = mark();
  T result = callback();
  rewind(state);
  return result;
}

bool Parser::nextTokenIsTokenStringLiteral() {
  // Check if next token is string literal by saving state and trying to parse
  ParserState state = mark();
  nextToken();
  bool result = currentToken().is(ast::SyntaxKind::StringLiteral);
  rewind(state);
  return result;
}

zc::Maybe<zc::Vector<zc::Own<ast::Expression>>> Parser::parseArgumentList() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseArgumentList");

  // argumentList:
  //   (assignmentExpression | ELLIPSIS assignmentExpression) (
  //     COMMA (assignmentExpression | ELLIPSIS assignmentExpression)
  //   )*;

  if (!expectToken(ast::SyntaxKind::LeftParen)) { return zc::none; }

  nextToken();  // consume '('

  zc::Vector<zc::Own<ast::Expression>> arguments;

  if (!expectToken(ast::SyntaxKind::RightParen)) {
    do {
      ZC_IF_SOME(arg, parseAssignmentExpressionOrHigher()) { arguments.add(zc::mv(arg)); }
      else { return zc::none; }
    } while (consumeExpectedToken(ast::SyntaxKind::Comma));
  }

  if (!consumeExpectedToken(ast::SyntaxKind::RightParen)) { return zc::none; }

  return zc::mv(arguments);
}

zc::Maybe<zc::Vector<zc::Own<ast::TypeNode>>> Parser::tryParseTypeArgumentsInExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser,
                                 "tryParseTypeArgumentsInExpression");

  // typeArguments: LT typeArgumentList GT;
  // typeArgumentList: type (COMMA type)*;
  // This function parses type arguments in expression context, like f<number>(42)

  if (!expectToken(ast::SyntaxKind::LessThan)) { return zc::none; }
  // TypeArguments must not be parsed in JavaScript files to avoid ambiguity with binary operators.
  // Save current parser state
  ParserState state = mark();

  if (expectToken(ast::SyntaxKind::LessThan)) {
    nextToken();

    // Parse the type argument list
    zc::Vector<zc::Own<ast::TypeNode>> typeArguments;

    if (!expectToken(ast::SyntaxKind::GreaterThan)) {
      do {
        ZC_IF_SOME(typeArg, parseType()) { typeArguments.add(zc::mv(typeArg)); }
        else {
          // Parsing failed, restore state and return none
          rewind(state);
          return zc::none;
        }
      } while (consumeExpectedToken(ast::SyntaxKind::Comma));
    }

    // If it doesn't have the closing `>` then it's definitely not a type argument list.
    if (!consumeExpectedToken(ast::SyntaxKind::GreaterThan)) {
      rewind(state);
      return zc::none;
    }

    // We successfully parsed a type argument list. The next token determines whether we want to
    // treat it as such. If the type argument list is followed by `(` or a template literal, as in
    // `f<number>(42)`, we favor the type argument interpretation even though JavaScript would view
    // it as a relational expression.
    if (canFollowTypeArgumentsInExpression()) { return zc::mv(typeArguments); }
  }

  // Parsing failed or doesn't follow expected pattern, restore state
  rewind(state);
  return zc::none;
}

zc::Maybe<zc::Vector<zc::Own<ast::TypeNode>>> Parser::parseTypeArguments() {
  if (expectToken(ast::SyntaxKind::LessThan)) {
    return parseBracketedList<ast::TypeNode>(
        ParsingContext::TypeArguments, ZC_BIND_METHOD(*this, parseType), ast::SyntaxKind::LessThan,
        ast::SyntaxKind::GreaterThan);
  }
  return zc::none;
}

zc::Maybe<zc::Own<ast::ExpressionWithTypeArguments>> Parser::parseExpressionWithTypeArguments() {
  const source::SourceLoc startLoc = currentToken().getLocation();
  ZC_IF_SOME(expression, parseLeftHandSideExpressionOrHigher()) {
    if (ast::isa<ast::ExpressionWithTypeArguments>(*expression)) {
      auto ewt = ast::cast<ast::ExpressionWithTypeArguments>(zc::mv(expression));
      return finishNode(zc::mv(ewt), startLoc);
    }
    auto typeArguments = parseTypeArguments();
    auto ewt =
        ast::factory::createExpressionWithTypeArguments(zc::mv(expression), zc::mv(typeArguments));
    return finishNode(zc::mv(ewt), startLoc);
  }
  return zc::none;
}

bool Parser::canParseSemicolon() const {
  // If there's a real semicolon, then we can always parse it out.
  // We can parse out an optional semicolon in ASI cases in the following cases.
  const lexer::Token& token = currentToken();
  return token.is(ast::SyntaxKind::Semicolon) || token.is(ast::SyntaxKind::RightBrace) ||
         token.is(ast::SyntaxKind::EndOfFile) || token.hasPrecedingLineBreak();
}

bool Parser::tryParseSemicolon() {
  if (!canParseSemicolon()) { return false; }

  // Consume the semicolon if it was explicitly provided.
  if (expectToken(ast::SyntaxKind::Semicolon)) { nextToken(); }

  return true;
}

bool Parser::parseSemicolon() {
  return tryParseSemicolon() || parseExpected(ast::SyntaxKind::Semicolon);
}

bool Parser::canFollowTypeArgumentsInExpression() {
  // Based on TypeScript's implementation
  const lexer::Token& token = currentToken();

  switch (token.getKind()) {
    // These tokens can follow a type argument list in a call expression.
    case ast::SyntaxKind::LeftParen:  // foo<x>(
    case ast::SyntaxKind::Backtick:   // foo<T> `...` (template literal)
      return true;

    // A type argument list followed by `<` never makes sense, and a type argument list followed
    // by `>` is ambiguous with a (re-scanned) `>>` operator, so we disqualify both. Also, in
    // this context, `+` and `-` are unary operators, not binary operators.
    case ast::SyntaxKind::LessThan:
    case ast::SyntaxKind::GreaterThan:
    case ast::SyntaxKind::Plus:
    case ast::SyntaxKind::Minus:
      return false;

    default:
      break;
  }

  // We favor the type argument list interpretation when it is immediately followed by
  // a line break, a binary operator, or something that can't start an expression.
  return token.hasPrecedingLineBreak() || isBinaryOperator() || !isStartOfExpression();
}

zc::Maybe<zc::Own<ast::TokenNode>> Parser::parseTokenNode() {
  const source::SourceLoc loc = currentToken().getLocation();
  nextToken();
  return finishNode(ast::factory::createTokenNode(currentToken().getKind()), loc);
}

zc::Maybe<zc::Own<ast::Identifier>> Parser::createIdentifier(bool isIdentifier) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "createIdentifier");

  // bindingIdentifier: identifier
  // identifier: identifierName
  //   where identifierName must not be a reserved word

  if (isIdentifier) {
    const lexer::Token token = currentToken();
    zc::StringPtr identifier = currentToken().getText(getSourceManager());

    nextToken();
    return finishNode(ast::factory::createIdentifier(identifier), token.getLocation());
  }

  const lexer::Token token = currentToken();
  if (lexer::isReservedKeyword(token.getKind())) {
    parseErrorAtCurrentToken<diagnostics::DiagID::ReservedKeywordAsIdentifier>(
        token.getText(getSourceManager()));
  } else {
    parseErrorAtCurrentToken<diagnostics::DiagID::ExceptedIdentifier>(
        token.getText(getSourceManager()));
  }

  return finishNode(ast::factory::createMissingIdentifier(), currentLoc());
}

zc::Maybe<zc::Own<ast::Identifier>> Parser::parseIdentifier() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseIdentifier");
  return createIdentifier(isIdentifier());
}

zc::Maybe<zc::Own<ast::Identifier>> Parser::parseIdentifierName() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseIdentifierName");
  return createIdentifier(lexer::isIdentifierOrKeyword(currentToken().getKind()));
}

zc::Maybe<zc::Own<ast::Identifier>> Parser::parseBindingIdentifier() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBindingIdentifier");
  return createIdentifier(isBindingIdentifier());
}

zc::Maybe<zc::Own<ast::BindingElement>> Parser::parseBindingElement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBindingElement");

  // bindingElement: bindingIdentifier typeAnnotation? initializer?;

  const source::SourceLoc startLoc = currentToken().getLocation();

  ZC_IF_SOME(name, parseBindingIdentifier()) {
    // Optional type annotation
    zc::Maybe<zc::Own<ast::TypeNode>> type = parseTypeAnnotation();
    // Optional initializer
    zc::Maybe<zc::Own<ast::Expression>> initializer = parseInitializer();

    return finishNode(
        ast::factory::createBindingElement(zc::mv(name), zc::mv(type), zc::mv(initializer)),
        startLoc);
  }

  return zc::none;
}

// ================================================================================
// Statement parsing implementations

zc::Maybe<zc::Own<ast::BlockStatement>> Parser::parseBlockStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBlockStatement");

  source::SourceLoc startLoc = currentToken().getLocation();
  if (!consumeExpectedToken(ast::SyntaxKind::LeftBrace)) { return zc::none; }

  zc::Vector<zc::Own<ast::Statement>> statements = parseList<ast::Statement>(
      ParsingContext::BlockElements, ZC_BIND_METHOD(*this, parseStatement));

  parseExpectedMatchingBrackets(ast::SyntaxKind::LeftBrace, ast::SyntaxKind::RightBrace,
                                /*openParsed*/ true, startLoc);

  // Create block statement AST node
  return finishNode(ast::factory::createBlockStatement(zc::mv(statements)), startLoc);
}

void Parser::parseExpectedMatchingBrackets(ast::SyntaxKind openKind, ast::SyntaxKind closeKind,
                                           bool openParsed, source::SourceLoc openPos) {
  if (currentToken().is(closeKind)) {
    nextToken();
    return;
  }

  zc::String closeText =
      zc::str(ZC_ASSERT_NONNULL(lexer::Token::getStaticTextForTokenKind(closeKind)));
  auto err = parseErrorAtCurrentToken<diagnostics::DiagID::ExpectedToken>(zc::str(closeText));

  if (!openParsed) { return; }

  zc::String openText =
      zc::str(ZC_ASSERT_NONNULL(lexer::Token::getStaticTextForTokenKind(openKind)));
  auto related = zc::heap<diagnostics::Diagnostic>(diagnostics::DiagID::ExpectedMatchingTokenHere,
                                                   openPos, zc::mv(closeText), zc::mv(openText));
  err.addChild(zc::mv(related));
}

zc::Maybe<zc::Own<ast::EmptyStatement>> Parser::parseEmptyStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseEmptyStatement");

  source::SourceLoc startLoc = currentToken().getLocation();
  if (!consumeExpectedToken(ast::SyntaxKind::Semicolon)) { return zc::none; }

  // Create empty statement AST node
  return finishNode(ast::factory::createEmptyStatement(), startLoc);
}

zc::Maybe<zc::Own<ast::ExpressionStatement>> Parser::parseExpressionStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseExpressionStatement");

  // expressionStatement: expression ";"
  //   where first token is not one of:
  //     "{" | "fun" | "class" | "let"
  //
  // This grammar rule means:
  // 1. An expression statement consists of an expression followed by semicolon
  // 2. The expression cannot start with:
  //    - A left brace (to avoid confusion with block statements)
  //    - The "fun" keyword (to avoid confusion with function declarations)
  //    - The "class" keyword (to avoid confusion with class declarations)
  //    - The "let" keyword (to avoid confusion with variable declarations)

  source::SourceLoc startLoc = currentToken().getLocation();
  ZC_IF_SOME(expr, parseExpression()) {
    if (!tryParseSemicolon()) { parseErrorForMissingSemicolonAfter(*expr); }

    // Create expression statement AST node
    return finishNode(ast::factory::createExpressionStatement(zc::mv(expr)), startLoc);
  }

  return zc::none;
}

namespace {

static const zc::StringPtr kViableKeywordSuggestions[] = {
    "let"_zc,       "const"_zc,      "fun"_zc,      "class"_zc,  "interface"_zc, "module"_zc,
    "namespace"_zc, "type"_zc,       "import"_zc,   "export"_zc, "return"_zc,    "if"_zc,
    "else"_zc,      "for"_zc,        "while"_zc,    "switch"_zc, "new"_zc,       "try"_zc,
    "throw"_zc,     "break"_zc,      "continue"_zc, "async"_zc,  "await"_zc,     "yield"_zc,
    "extends"_zc,   "implements"_zc, "this"_zc,     "super"_zc,  "package"_zc,   "void"_zc,
    "private"_zc,   "protected"_zc,  "public"_zc,   "static"_zc, "readonly"_zc,  "abstract"_zc,
    "override"_zc,  "declare"_zc,    "get"_zc,      "set"_zc,    "enum"_zc};

size_t levenshteinDistance(zc::StringPtr s1, zc::StringPtr s2) {
  const size_t n = s1.size();
  const size_t m = s2.size();

  if (n == 0) { return m; }
  if (m == 0) { return n; }

  zc::Array<size_t> prevRow = zc::heapArray<size_t>(m + 1);
  zc::Array<size_t> currentRow = zc::heapArray<size_t>(m + 1);

  for (size_t j = 0; j <= m; ++j) { prevRow[j] = j; }

  for (size_t i = 1; i <= n; ++i) {
    currentRow[0] = i;
    for (size_t j = 1; j <= m; ++j) {
      size_t cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
      currentRow[j] =
          zc::min(zc::min(currentRow[j - 1] + 1, prevRow[j] + 1), prevRow[j - 1] + cost);
    }
    for (size_t j = 0; j <= m; ++j) { prevRow[j] = currentRow[j]; }
  }

  return currentRow[m];
}

zc::String getSpellingSuggestion(zc::StringPtr name, zc::ArrayPtr<const zc::StringPtr> candidates) {
  size_t bestDistance = zc::max<size_t>(3, name.size() / 3);
  zc::StringPtr bestCandidate;
  bool found = false;

  for (const auto& candidate : candidates) {
    // Skip if length difference is too large
    size_t lenDiff = name.size() - candidate.size();
    if (lenDiff < 0) { lenDiff = -lenDiff; }
    if (lenDiff > bestDistance) { continue; }

    size_t dist = levenshteinDistance(name, candidate);
    if (dist < bestDistance) {
      bestDistance = dist;
      bestCandidate = candidate;
      found = true;
    }
  }

  return found ? zc::str(bestCandidate) : zc::str("");
}

zc::String getSpaceSuggestion(zc::StringPtr text) {
  for (const auto& kw : kViableKeywordSuggestions) {
    if (text.startsWith(kw) && text.size() > kw.size() + 2) {
      return zc::str(kw, " ", text.slice(kw.size()));
    }
  }
  return zc::str("");
}
}  // namespace

void Parser::parseErrorForMissingSemicolonAfter(const ast::Expression& expr) {
  // We reach here when an expression statement is not followed by a semicolon.
  // Provide contextual diagnostics based on the next token to help recovery.

  // TODO: Check for TaggedTemplateExpression when supported.
  // Tagged template literals are sometimes used in places where only simple strings are allowed.

  // Check if the expression is a keyword-like identifier that might indicate a
  // malformed declaration.
  if (expr.getKind() == ast::SyntaxKind::Identifier) {
    const auto& id = static_cast<const ast::Identifier&>(expr);
    zc::StringPtr text = id.getText();

    if (text == "const"_zc || text == "let"_zc) {
      parseErrorAtRange<diagnostics::DiagID::VariableDeclarationNotAllowedHere>(
          expr.getSourceRange());
      return;
    }

    if (text == "declare"_zc) {
      // If a declared node failed to parse, it would have emitted a diagnostic already.
      return;
    }

    if (text == "interface"_zc) {
      parseErrorForInvalidName<diagnostics::DiagID::InterfaceNameCannotBeKeyword,
                               diagnostics::DiagID::InterfaceMustBeGivenAName>(
          ast::SyntaxKind::LeftBrace);
      return;
    }

    if (text == "is"_zc) {
      parseErrorAtRange<diagnostics::DiagID::TypePredicateOnlyInReturn>(expr.getSourceRange());
      return;
    }

    if (text == "module"_zc || text == "namespace"_zc) {
      parseErrorForInvalidName<diagnostics::DiagID::NamespaceNameCannotBeKeyword,
                               diagnostics::DiagID::NamespaceMustBeGivenAName>(
          ast::SyntaxKind::LeftBrace);
      return;
    }

    if (text == "type"_zc) {
      parseErrorForInvalidName<diagnostics::DiagID::TypeAliasNameCannotBeKeyword,
                               diagnostics::DiagID::TypeAliasMustBeGivenAName>(
          ast::SyntaxKind::Equals);
      return;
    }

    // The user alternatively might have misspelled or forgotten to add a space after a common
    // keyword.
    zc::String suggestion = getSpellingSuggestion(text, kViableKeywordSuggestions);
    if (suggestion == ""_zc) { suggestion = getSpaceSuggestion(text); }

    if (suggestion != ""_zc) {
      parseErrorAtRange<diagnostics::DiagID::DidYouMean>(expr.getSourceRange(), zc::mv(suggestion));
      return;
    }
  }

  if (expr.getKind() != ast::SyntaxKind::Identifier) {
    parseErrorAtCurrentToken<diagnostics::DiagID::ExpectedToken>(";"_zc);
    return;
  }

  // Unknown tokens are handled with their own errors in the scanner
  if (currentToken().is(ast::SyntaxKind::Unknown)) { return; }

  // Generic fallback: always report unexpected keyword or identifier on the expression itself.
  parseErrorAtRange<diagnostics::DiagID::UnexpectedKeywordOrIdentifier>(expr.getSourceRange());
}

zc::Maybe<zc::Own<ast::IfStatement>> Parser::parseIfStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseIfStatement");

  source::SourceLoc startLoc = currentToken().getLocation();
  if (!consumeExpectedToken(ast::SyntaxKind::IfKeyword)) { return zc::none; }

  if (!consumeExpectedToken(ast::SyntaxKind::LeftParen)) { return zc::none; }

  ZC_IF_SOME(condition, parseExpression()) {
    if (!consumeExpectedToken(ast::SyntaxKind::RightParen)) { return zc::none; }

    ZC_IF_SOME(thenStmt, parseStatement()) {
      zc::Maybe<zc::Own<ast::Statement>> elseStmt = zc::none;

      if (expectToken(ast::SyntaxKind::ElseKeyword)) {
        nextToken();
        elseStmt = parseStatement();
      }

      // Create if statement AST node
      return finishNode(
          ast::factory::createIfStatement(zc::mv(condition), zc::mv(thenStmt), zc::mv(elseStmt)),
          startLoc);
    }
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::WhileStatement>> Parser::parseWhileStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseWhileStatement");

  source::SourceLoc startLoc = currentToken().getLocation();
  if (!consumeExpectedToken(ast::SyntaxKind::WhileKeyword)) { return zc::none; }

  if (!consumeExpectedToken(ast::SyntaxKind::LeftParen)) { return zc::none; }

  ZC_IF_SOME(condition, parseExpression()) {
    if (!consumeExpectedToken(ast::SyntaxKind::RightParen)) { return zc::none; }

    ZC_IF_SOME(body, parseStatement()) {
      // Create while statement AST node
      return finishNode(ast::factory::createWhileStatement(zc::mv(condition), zc::mv(body)),
                        startLoc);
    }
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::ForStatement>> Parser::parseForStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseForStatement");

  source::SourceLoc startLoc = currentToken().getLocation();
  if (!consumeExpectedToken(ast::SyntaxKind::ForKeyword)) { return zc::none; }

  if (!consumeExpectedToken(ast::SyntaxKind::LeftParen)) { return zc::none; }

  // Parse init (optional)
  zc::Maybe<zc::Own<ast::Expression>> init = zc::none;
  if (!expectToken(ast::SyntaxKind::Semicolon)) { init = parseExpression(); }

  if (!consumeExpectedToken(ast::SyntaxKind::Semicolon)) { return zc::none; }

  // Parse condition (optional)
  zc::Maybe<zc::Own<ast::Expression>> condition = zc::none;
  if (!expectToken(ast::SyntaxKind::Semicolon)) { condition = parseExpression(); }

  if (!consumeExpectedToken(ast::SyntaxKind::Semicolon)) { return zc::none; }

  // Parse update (optional)
  zc::Maybe<zc::Own<ast::Expression>> update = zc::none;
  if (!expectToken(ast::SyntaxKind::RightParen)) { update = parseExpression(); }

  if (!consumeExpectedToken(ast::SyntaxKind::RightParen)) { return zc::none; }

  ZC_IF_SOME(body, parseStatement()) {
    // Convert init expression to statement if needed
    zc::Maybe<zc::Own<ast::Statement>> initStmt = zc::none;
    ZC_IF_SOME(initExpr, init) {
      initStmt = ast::factory::createExpressionStatement(zc::mv(initExpr));
    }

    return finishNode(ast::factory::createForStatement(zc::mv(initStmt), zc::mv(condition),
                                                       zc::mv(update), zc::mv(body)),
                      startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::BreakStatement>> Parser::parseBreakStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBreakStatement");

  source::SourceLoc startLoc = currentToken().getLocation();
  if (!consumeExpectedToken(ast::SyntaxKind::BreakKeyword)) { return zc::none; }

  // Optional label
  zc::Maybe<zc::Own<ast::Identifier>> label = zc::none;
  if (expectToken(ast::SyntaxKind::Identifier)) { label = parseIdentifier(); }

  if (!consumeExpectedToken(ast::SyntaxKind::Semicolon)) { return zc::none; }

  return finishNode(ast::factory::createBreakStatement(zc::mv(label)), startLoc);
}

zc::Maybe<zc::Own<ast::ContinueStatement>> Parser::parseContinueStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseContinueStatement");

  source::SourceLoc startLoc = currentToken().getLocation();
  if (!consumeExpectedToken(ast::SyntaxKind::ContinueKeyword)) { return zc::none; }

  // Optional label
  zc::Maybe<zc::Own<ast::Identifier>> label = zc::none;
  if (expectToken(ast::SyntaxKind::Identifier)) { label = parseIdentifier(); }

  if (!consumeExpectedToken(ast::SyntaxKind::Semicolon)) { return zc::none; }

  return finishNode(ast::factory::createContinueStatement(zc::mv(label)), startLoc);
}

zc::Maybe<zc::Own<ast::ReturnStatement>> Parser::parseReturnStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseReturnStatement");

  source::SourceLoc startLoc = currentToken().getLocation();
  if (!consumeExpectedToken(ast::SyntaxKind::ReturnKeyword)) { return zc::none; }

  // Optional expression
  zc::Maybe<zc::Own<ast::Expression>> expr = zc::none;
  if (!expectToken(ast::SyntaxKind::Semicolon)) { expr = parseExpression(); }

  if (!consumeExpectedToken(ast::SyntaxKind::Semicolon)) { return zc::none; }

  // Create return statement AST node
  return finishNode(ast::factory::createReturnStatement(zc::mv(expr)), startLoc);
}

zc::Maybe<zc::Own<ast::MatchStatement>> Parser::parseMatchStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseMatchStatement");

  if (!consumeExpectedToken(ast::SyntaxKind::MatchKeyword)) { return zc::none; }

  if (!consumeExpectedToken(ast::SyntaxKind::LeftParen)) { return zc::none; }

  ZC_IF_SOME(expr, parseExpression()) {
    if (!consumeExpectedToken(ast::SyntaxKind::RightParen)) { return zc::none; }

    // Parse match clauses
    zc::Vector<zc::Own<ast::Statement>> clauses;
    if (!consumeExpectedToken(ast::SyntaxKind::LeftBrace)) { return zc::none; }

    while (!expectToken(ast::SyntaxKind::RightBrace)) {
      if (expectToken(ast::SyntaxKind::DefaultKeyword)) {
        // Parse default clause
        nextToken();  // consume 'default'
        if (consumeExpectedToken(ast::SyntaxKind::EqualsGreaterThan)) {
          zc::Vector<zc::Own<ast::Statement>> defaultStmts = parseList<ast::Statement>(
              ParsingContext::BlockElements, ZC_BIND_METHOD(*this, parseStatement));
          clauses.add(ast::factory::createDefaultClause(zc::mv(defaultStmts)));
        }
      } else if (expectToken(ast::SyntaxKind::CaseKeyword)) {
        // Parse case clause
        nextToken();  // consume 'case'
        ZC_IF_SOME(pattern, parsePattern()) {
          if (consumeExpectedToken(ast::SyntaxKind::EqualsGreaterThan)) {
            // Parse optional guard clause
            zc::Maybe<zc::Own<ast::Expression>> guard;
            if (consumeExpectedToken(ast::SyntaxKind::IfKeyword)) { guard = parseExpression(); }
            ZC_IF_SOME(statement, parseStatement()) {
              clauses.add(ast::factory::createMatchClause(zc::mv(pattern), zc::mv(guard),
                                                          zc::mv(statement)));
            }
          }
        }
      } else {
        // Parse regular match clause (without case keyword, e.g. pattern => statement)
        ZC_IF_SOME(pattern, parsePattern()) {
          if (consumeExpectedToken(ast::SyntaxKind::EqualsGreaterThan)) {
            // Parse optional guard clause
            zc::Maybe<zc::Own<ast::Expression>> guard;
            if (consumeExpectedToken(ast::SyntaxKind::IfKeyword)) { guard = parseExpression(); }
            ZC_IF_SOME(statement, parseStatement()) {
              clauses.add(ast::factory::createMatchClause(zc::mv(pattern), zc::mv(guard),
                                                          zc::mv(statement)));
            }
          }
        }
      }
    }

    if (!consumeExpectedToken(ast::SyntaxKind::RightBrace)) { return zc::none; }

    source::SourceLoc startLoc = currentToken().getLocation();
    return finishNode(ast::factory::createMatchStatement(zc::mv(expr), zc::mv(clauses)), startLoc);
  }

  return zc::none;
}

// ================================================================================
// Declaration parsing implementations

zc::Maybe<zc::Own<ast::Statement>> Parser::parseDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseDeclaration");

  // declaration:
  //   functionDeclaration
  //   | classDeclaration
  //   | interfaceDeclaration
  //   | aliasDeclaration
  //   | structDeclaration
  //   | errorDeclaration
  //   | enumDeclaration
  //   | variableDeclaration;

  const lexer::Token& token = currentToken();

  switch (token.getKind()) {
    case ast::SyntaxKind::LetKeyword:
    case ast::SyntaxKind::ConstKeyword:
      return parseVariableStatement();
    case ast::SyntaxKind::FunKeyword:
      return parseFunctionDeclaration();
    case ast::SyntaxKind::ClassKeyword:
      return parseClassDeclaration();
    case ast::SyntaxKind::InterfaceKeyword:
      return parseInterfaceDeclaration();
    case ast::SyntaxKind::StructKeyword:
      return parseStructDeclaration();
    case ast::SyntaxKind::EnumKeyword:
      return parseEnumDeclaration();
    case ast::SyntaxKind::ErrorKeyword:
      return parseErrorDeclaration();
    case ast::SyntaxKind::AliasKeyword:
      return parseAliasDeclaration();
    default:
      return zc::none;
  }
}

zc::Maybe<zc::Own<ast::VariableDeclarationList>> Parser::parseVariableDeclarationList() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseVariableDeclarationList");

  // variableDeclarationList: LET_OR_CONST bindingList;
  // bindingList: bindingElement (COMMA bindingElement)*;
  // bindingElement: bindingIdentifier typeAnnotation? initializer?;

  source::SourceLoc startLoc = currentToken().getLocation();

  nextToken();

  // Parse bindingList: bindingElement (COMMA bindingElement)*
  zc::Vector<zc::Own<ast::BindingElement>> bindings = parseDelimitedList<ast::BindingElement>(
      ParsingContext::VariableDeclarations, ZC_BIND_METHOD(*this, parseBindingElement));

  // Create variable declaration AST node
  return finishNode(ast::factory::createVariableDeclarationList(zc::mv(bindings)), startLoc);
}

zc::Maybe<zc::Own<ast::FunctionDeclaration>> Parser::parseFunctionDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseFunctionDeclaration");

  // functionDeclaration:
  //   FUN bindingIdentifier callSignature LBRACE functionBody RBRACE;

  source::SourceLoc startLoc = currentToken().getLocation();
  if (!consumeExpectedToken(ast::SyntaxKind::FunKeyword)) { return zc::none; }

  ZC_IF_SOME(name, parseBindingIdentifier()) {
    // Parse function signature (type parameters, parameters and return type)
    zc::Vector<zc::Own<ast::TypeParameterDeclaration>> typeParameters = parseTypeParameters();
    zc::Vector<zc::Own<ast::BindingElement>> parameters = parseParameters();
    zc::Maybe<zc::Own<ast::ReturnTypeNode>> returnType = parseReturnType();

    // Parse function body
    ZC_IF_SOME(body, parseBlockStatement()) {
      // Create function declaration AST node
      return finishNode(ast::factory::createFunctionDeclaration(
                            zc::mv(name), zc::mv(typeParameters), zc::mv(parameters),
                            zc::mv(returnType), zc::mv(body)),
                        startLoc);
    }
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::ClassDeclaration>> Parser::parseClassDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseClassDeclaration");

  source::SourceLoc classLoc = currentToken().getLocation();
  if (!consumeExpectedToken(ast::SyntaxKind::ClassKeyword)) { return zc::none; }

  ZC_IF_SOME(name, parseBindingIdentifier()) {
    zc::Vector<zc::Own<ast::TypeParameterDeclaration>> typeParameters = parseTypeParameters();
    zc::Maybe<zc::Vector<zc::Own<ast::HeritageClause>>> superClasses = parseHeritageClauses();

    zc::Vector<zc::Own<ast::ClassElement>> members;
    if (parseExpected(ast::SyntaxKind::LeftBrace)) {
      members = parseClassMembers();
      parseExpected(ast::SyntaxKind::RightBrace);
    }

    return finishNode(ast::factory::createClassDeclaration(zc::mv(name), zc::mv(typeParameters),
                                                           zc::mv(superClasses), zc::mv(members)),
                      classLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::InterfaceDeclaration>> Parser::parseInterfaceDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseInterfaceDeclaration");

  source::SourceLoc startLoc = currentToken().getLocation();

  parseExpected(ast::SyntaxKind::InterfaceKeyword);

  ZC_IF_SOME(name, parseBindingIdentifier()) {
    zc::Vector<zc::Own<ast::TypeParameterDeclaration>> typeParameters = parseTypeParameters();
    zc::Maybe<zc::Vector<zc::Own<ast::HeritageClause>>> heritageClauses = parseHeritageClauses();

    zc::Vector<zc::Own<ast::InterfaceElement>> members = parseInterfaceMembers();

    auto iface = ast::factory::createInterfaceDeclaration(zc::mv(name), zc::mv(typeParameters),
                                                          zc::mv(heritageClauses), zc::mv(members));
    return finishNode(zc::mv(iface), startLoc);
  }

  return zc::none;
}

zc::Vector<zc::Own<ast::Statement>> Parser::parseEmptyNodeList() {
  zc::Vector<zc::Own<ast::Statement>> nodes;
  return nodes;
}

zc::Maybe<zc::Own<ast::StructDeclaration>> Parser::parseStructDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseStructDeclaration");

  if (!consumeExpectedToken(ast::SyntaxKind::StructKeyword)) { return zc::none; }

  ZC_IF_SOME(name, parseBindingIdentifier()) {
    source::SourceLoc startLoc = currentToken().getLocation();

    // Parse struct body
    if (!consumeExpectedToken(ast::SyntaxKind::LeftBrace)) { return zc::none; }

    zc::Vector<zc::Own<ast::Statement>> fields;
    while (!expectToken(ast::SyntaxKind::RightBrace)) {
      // Parse struct fields (simplified as statements)
      ZC_IF_SOME(field, parseStatement()) { fields.add(zc::mv(field)); }
    }

    if (!consumeExpectedToken(ast::SyntaxKind::RightBrace)) { return zc::none; }

    return finishNode(ast::factory::createStructDeclaration(zc::mv(name), zc::mv(fields)),
                      startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::EnumDeclaration>> Parser::parseEnumDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseEnumDeclaration");

  if (!consumeExpectedToken(ast::SyntaxKind::EnumKeyword)) { return zc::none; }

  ZC_IF_SOME(name, parseBindingIdentifier()) {
    source::SourceLoc startLoc = currentToken().getLocation();

    // Parse enum body
    if (!consumeExpectedToken(ast::SyntaxKind::LeftBrace)) { return zc::none; }

    zc::Vector<zc::Own<ast::Statement>> members;
    while (!expectToken(ast::SyntaxKind::RightBrace)) {
      // Parse enum members (simplified as statements)
      ZC_IF_SOME(member, parseStatement()) { members.add(zc::mv(member)); }
      // Optional comma
      if (expectToken(ast::SyntaxKind::Comma)) { nextToken(); }
    }

    if (!consumeExpectedToken(ast::SyntaxKind::RightBrace)) { return zc::none; }

    return finishNode(ast::factory::createEnumDeclaration(zc::mv(name), zc::mv(members)), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::ErrorDeclaration>> Parser::parseErrorDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseErrorDeclaration");

  if (!consumeExpectedToken(ast::SyntaxKind::ErrorKeyword)) { return zc::none; }

  ZC_IF_SOME(name, parseBindingIdentifier()) {
    source::SourceLoc startLoc = currentToken().getLocation();

    // Parse error body (optional)
    zc::Vector<zc::Own<ast::Statement>> fields;
    if (expectToken(ast::SyntaxKind::LeftBrace)) {
      nextToken();

      while (!expectToken(ast::SyntaxKind::RightBrace)) {
        // Parse error fields (simplified as statements)
        ZC_IF_SOME(field, parseStatement()) { fields.add(zc::mv(field)); }
      }

      if (!consumeExpectedToken(ast::SyntaxKind::RightBrace)) { return zc::none; }
    }

    return finishNode(ast::factory::createErrorDeclaration(zc::mv(name), zc::mv(fields)), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::AliasDeclaration>> Parser::parseAliasDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseAliasDeclaration");

  source::SourceLoc startLoc = currentToken().getLocation();
  if (!consumeExpectedToken(ast::SyntaxKind::AliasKeyword)) { return zc::none; }

  ZC_IF_SOME(name, parseBindingIdentifier()) {
    if (!consumeExpectedToken(ast::SyntaxKind::Equals)) { return zc::none; }

    ZC_IF_SOME(type, parseType()) {
      if (!consumeExpectedToken(ast::SyntaxKind::Semicolon)) { return zc::none; }
      source::SourceLoc endLoc = currentToken().getLocation();

      return finishNode(ast::factory::createAliasDeclaration(zc::mv(name), zc::mv(type)), startLoc,
                        endLoc);
    }
  }

  return zc::none;
}

// ================================================================================
// Expression parsing implementations

zc::Maybe<zc::Own<ast::Expression>> Parser::parseExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseExpression");

  // expression: assignmentExpression (COMMA assignmentExpression)*;
  //
  // Parses a comma-separated list of assignment expressions

  const source::SourceLoc startLoc = currentToken().getLocation();

  ZC_IF_SOME(assignExpr, parseAssignmentExpressionOrHigher()) {
    zc::Own<ast::Expression> expr = zc::mv(assignExpr);
    // Handle comma operator
    while (expectToken(ast::SyntaxKind::Comma)) {
      ZC_IF_SOME(op, parseTokenNode()) {
        ZC_IF_SOME(right, parseAssignmentExpressionOrHigher()) {
          // Create comma expression AST node
          zc::Own<ast::Expression> newExpr =
              ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right));
          expr = zc::mv(newExpr);
        }
      }
    }
    return finishNode(zc::mv(expr), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseInitializer() {
  if (parseOptional(ast::SyntaxKind::Equals)) { return parseAssignmentExpressionOrHigher(); }
  return zc::none;
}

// Assignment expression parsing
zc::Maybe<zc::Own<ast::Expression>> Parser::parseAssignmentExpressionOrHigher() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser,
                                 "parseAssignmentExpressionOrHigher");

  // assignmentExpression:
  //   conditionalExpression
  //   | functionExpression
  //   | leftHandSideExpression ASSIGN assignmentExpression
  //   | leftHandSideExpression assignmentOperator assignmentExpression
  //   | leftHandSideExpression AND_ASSIGN assignmentExpression
  //   | leftHandSideExpression OR_ASSIGN assignmentExpression
  //   | leftHandSideExpression NULL_COALESCE_ASSIGN assignmentExpression;
  //
  // Try to parse function expression first, then binary expression
  if (currentToken().is(ast::SyntaxKind::FunKeyword)) {
    ZC_IF_SOME(funcExpr, parseFunctionExpression()) { return zc::mv(funcExpr); }
  }

  // Parse binary expression with lowest precedence to get the left operand
  const source::SourceLoc startLoc = currentToken().getLocation();

  ZC_IF_SOME(expr, parseBinaryExpressionOrHigher(ast::OperatorPrecedence::kLowest)) {
    // Check for assignment operators - if found and expr is left-hand side, parse assignment
    if (isAssignmentOperator(currentToken().getKind()) && isLeftHandSideExpression(*expr)) {
      ZC_IF_SOME(op, parseTokenNode()) {
        // Right-associative: recursively parse assignment expression
        ZC_IF_SOME(right, parseAssignmentExpressionOrHigher()) {
          auto assignExpr =
              ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right));
          return finishNode(zc::mv(assignExpr), startLoc);
        }
      }
      return zc::none;
    }

    // Not an assignment, check for conditional expression (ternary operator)
    return parseConditionalExpressionRest(zc::mv(expr), startLoc);
  }

  return zc::none;
}

// conditional expression rest parsing
zc::Maybe<zc::Own<ast::Expression>> Parser::parseConditionalExpressionRest(
    zc::Own<ast::Expression> leftOperand, source::SourceLoc startLoc) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseConditionalExpressionRest");

  // conditionalExpression:
  //   shortCircuitExpression (QUESTION assignmentExpression COLON assignmentExpression)?
  //
  // Check for ternary conditional operator
  if (!expectToken(ast::SyntaxKind::Question)) {
    // No conditional , return the left operand as-is
    return zc::mv(leftOperand);
  }

  nextToken();  // consume '?'

  // Parse the 'then' expression
  ZC_IF_SOME(thenExpr, parseAssignmentExpressionOrHigher()) {
    // Expect ':' token
    if (!consumeExpectedToken(ast::SyntaxKind::Colon)) { return zc::none; }

    // Parse the 'else' expression
    ZC_IF_SOME(elseExpr, parseAssignmentExpressionOrHigher()) {
      // Create conditional expression AST node
      auto conditionalExpr = ast::factory::createConditionalExpression(
          zc::mv(leftOperand), zc::mv(thenExpr), zc::mv(elseExpr));
      return finishNode(zc::mv(conditionalExpr), startLoc);
    }
  }

  return zc::none;
}

// Binary expression parsing
zc::Maybe<zc::Own<ast::Expression>> Parser::parseBinaryExpressionOrHigher(
    ast::OperatorPrecedence precedence) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBinaryExpressionOrHigher");

  // Handles all binary expressions with precedence:
  //   bitwiseORExpression | bitwiseXORExpression | bitwiseANDExpression
  //   | equalityExpression | relationalExpression | shiftExpression
  //   | additiveExpression | multiplicativeExpression | exponentiationExpression
  //
  // Uses operator precedence parsing for left-to-right associativity

  const source::SourceLoc startLoc = currentToken().getLocation();
  // Parse the left operand (unary expression or higher)
  ZC_IF_SOME(leftOperand, parseUnaryExpressionOrHigher()) {
    // Parse the rest of the binary expression with lowest precedence
    return parseBinaryExpressionRest(zc::mv(leftOperand), precedence, startLoc);
  }

  return zc::none;
}

// Binary expression rest parsing
zc::Maybe<zc::Own<ast::Expression>> Parser::parseBinaryExpressionRest(
    zc::Own<ast::Expression> leftOperand, ast::OperatorPrecedence precedence,
    source::SourceLoc startLoc) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBinaryExpressionRest");

  zc::Own<ast::Expression> expr = zc::mv(leftOperand);

  while (true) {
    const lexer::Token& token = currentToken();

    ast::OperatorPrecedence newPrecedence = getBinaryOperatorPrecedence(token.getKind());

    // Check the precedence to see if we should "take" this operator
    // - For left associative operator (all operator but **), consume the ,
    //   recursively call the function below, and parse binaryExpression as a rightOperand
    //   of the caller if the new precedence of the operator is strictly greater than the current
    //   precedence.
    // - For right associative operator (**), consume the , recursively call the function
    //   and parse binaryExpression as a rightOperand of the caller if the new precedence of
    //   the operator is greater than or equal to the current precedence
    bool isRightAssociative = token.is(ast::SyntaxKind::AsteriskAsterisk);
    bool consumeCurrentOperator =
        isRightAssociative ? newPrecedence >= precedence : newPrecedence > precedence;

    if (!consumeCurrentOperator) { break; }

    if (expectToken(ast::SyntaxKind::AsKeyword)) {
      // Make sure we *do* perform ASI for constructs like this:
      //    let x = foo
      //    as (Bar)
      // This should be parsed as an initialized variable, followed
      // by a function call to 'as' with the argument 'Bar'
      if (currentToken().hasPrecedingLineBreak()) {
        // 'as' keyword must be on the same line as the variable declaration
        break;
      }

      ZC_IF_SOME(castExpr, parseCastExpression()) { expr = zc::mv(castExpr); }
      else {
        // 'as', 'as!', 'as?' parse failed
        break;
      }
    } else {
      ZC_IF_SOME(op, parseTokenNode()) {
        ZC_IF_SOME(rightOperand, parseBinaryExpressionOrHigher(newPrecedence)) {
          expr = finishNode(
              ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(rightOperand)),
              startLoc);
        }
      }
    }
  }

  return finishNode(zc::mv(expr), startLoc);
}

// Unary expression parsing
zc::Maybe<zc::Own<ast::Expression>> Parser::parseUnaryExpressionOrHigher() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseUnaryExpressionOrHigher");

  // postfixUnaryExpression:
  //   leftHandSideExpression (
  //     ERROR_PROPAGATE
  //     | FORCE_UNWRAP
  //     | INC
  //     | DEC
  //   )*;
  //
  // prefixUnaryExpression:
  //   postfixUnaryExpression
  //   | VOID prefixUnaryExpression
  //   | TYPEOF prefixUnaryExpression
  //   | PLUS prefixUnaryExpression
  //   | MINUS prefixUnaryExpression
  //   | BIT_NOT prefixUnaryExpression
  //   | NOT prefixUnaryExpression
  //   | INC prefixUnaryExpression
  //   | DEC prefixUnaryExpression
  //   | AWAIT prefixUnaryExpression;
  //
  // Handles prefix unary operators and update expressions

  if (isUpdateExpression()) {
    const source::SourceLoc startLoc = currentToken().getLocation();
    ZC_IF_SOME(updateExpr, parseUpdateExpression()) {
      return expectToken(ast::SyntaxKind::AsteriskAsterisk)
                 ? parseBinaryExpressionRest(zc::mv(updateExpr),
                                             getBinaryOperatorPrecedence(currentToken().getKind()),
                                             startLoc)
                 : finishNode(zc::mv(updateExpr), startLoc);
    }
  }

  const lexer::Token unaryOperator = currentToken();
  const source::SourceLoc startLoc = unaryOperator.getLocation();
  ZC_IF_SOME(simpleUnaryExpression, parseSimpleUnaryExpression()) {
    // Check if followed by exponentiation operator
    if (expectToken(ast::SyntaxKind::AsteriskAsterisk)) {
      // This is a unary expression with operator
      zc::StringPtr operatorText = unaryOperator.getText(getSourceManager());
      impl->diagnosticEngine.diagnose<diagnostics::DiagID::UnaryExpressionInExponentiation>(
          startLoc, operatorText);
    }
    return zc::mv(simpleUnaryExpression);
  }

  return zc::none;
}

// Simple unary expression parsing
zc::Maybe<zc::Own<ast::UnaryExpression>> Parser::parseSimpleUnaryExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseSimpleUnaryExpression");

  switch (currentToken().getKind()) {
    case ast::SyntaxKind::Plus:
    case ast::SyntaxKind::Minus:
    case ast::SyntaxKind::Tilde:
    case ast::SyntaxKind::Exclamation: {
      return parsePrefixUnaryExpression();
    }
    case ast::SyntaxKind::VoidKeyword: {
      return parseVoidExpression();
    }
    case ast::SyntaxKind::TypeOfKeyword: {
      return parseTypeOfExpression();
    }
    case ast::SyntaxKind::AwaitKeyword: {
      // TODO: Add support for await expression
      // if (isAwaitExpression()) { return parseAwaitExpression(); }
      ZC_FALLTHROUGH;
    }
    default:
      // Parse update expression for other cases
      return parseUpdateExpression();
  }
}

// Prefix unary expression parsing
zc::Maybe<zc::Own<ast::UnaryExpression>> Parser::parsePrefixUnaryExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parsePrefixUnaryExpression");

  // prefixUnaryExpression:
  //   PLUS unaryExpression
  //   | MINUS unaryExpression
  //   | TILDE unaryExpression
  //   | EXCLAMATION unaryExpression;
  //
  // Parse prefix unary operators: +, -, ~, !

  const source::SourceLoc startLoc = currentToken().getLocation();
  const ast::SyntaxKind op = currentToken().getKind();
  nextToken();

  // Parse the operand (recursive call to parseSimpleUnaryExpression)
  ZC_IF_SOME(operand, parseSimpleUnaryExpression()) {
    return finishNode(ast::factory::createPrefixUnaryExpression(zc::mv(op), zc::mv(operand)),
                      startLoc);
  }

  return zc::none;
}

// Void expression parsing
zc::Maybe<zc::Own<ast::VoidExpression>> Parser::parseVoidExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseVoidExpression");

  // voidExpression:
  //   VOID unaryExpression;
  //
  // Parse void operator expression

  const source::SourceLoc startLoc = currentToken().getLocation();

  nextToken();

  // Parse the operand
  ZC_IF_SOME(operand, parseSimpleUnaryExpression()) {
    // Create void expression
    auto voidExpr = ast::factory::createVoidExpression(zc::mv(operand));
    return finishNode(zc::mv(voidExpr), startLoc);
  }

  return zc::none;
}

// TypeOf expression parsing
zc::Maybe<zc::Own<ast::TypeOfExpression>> Parser::parseTypeOfExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseTypeOfExpression");

  // typeOfExpression:
  //   TYPEOF unaryExpression;
  //
  // Parse typeof operator expression

  const source::SourceLoc startLoc = currentToken().getLocation();

  nextToken();

  // Parse the operand
  ZC_IF_SOME(operand, parseSimpleUnaryExpression()) {
    // Create typeof expression
    auto typeofExpr = ast::factory::createTypeOfExpression(zc::mv(operand));
    return finishNode(zc::mv(typeofExpr), startLoc);
  }

  return zc::none;
}

// Left-hand side expression parsing
zc::Maybe<zc::Own<ast::LeftHandSideExpression>> Parser::parseLeftHandSideExpressionOrHigher() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser,
                                 "parseLeftHandSideExpressionOrHigher");

  // leftHandSideExpression:
  //   newExpression
  //   | callExpression
  //   | optionalExpression;

  zc::Maybe<zc::Own<ast::LeftHandSideExpression>> expression = zc::none;
  const source::SourceLoc startLoc = currentToken().getLocation();

  if (expectToken(ast::SyntaxKind::ImportKeyword)) {
    // Check if next token is left paren or less than by attempting to parse
    if (nextTokenIsLeftParenOrLessThan()) {
      expression = parseImportCallExpression();
    } else {
      expression = parseMemberExpressionOrHigher();
    }
  }

  // Handle super keyword
  if (expectToken(ast::SyntaxKind::SuperKeyword)) {
    ZC_IF_SOME(superExpr, parseSuperExpression()) { expression = zc::mv(superExpr); }
  }
  // Parse regular member expression
  else {
    ZC_IF_SOME(memberExpr, parseMemberExpressionOrHigher()) { expression = zc::mv(memberExpr); }
  }

  // If we have an expression, parse call expression rest
  ZC_IF_SOME(expr, expression) { return parseCallExpressionRest(startLoc, zc::mv(expr)); }

  return zc::none;
}

// Helper method to parse member expression or higher
zc::Maybe<zc::Own<ast::LeftHandSideExpression>> Parser::parseMemberExpressionOrHigher() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseMemberExpressionOrHigher");

  // memberExpression:
  //   (primaryExpression | superProperty | NEW memberExpression arguments)
  //   (LBRACK expression RBRACK | PERIOD identifier)*;
  //
  // Based on TypeScript implementation, we parse primary expression first,
  // then handle member access chains

  const source::SourceLoc startLoc = currentToken().getLocation();

  zc::Maybe<zc::Own<ast::PrimaryExpression>> expression = zc::none;

  ZC_IF_SOME(primaryExpr, parsePrimaryExpression()) { expression = zc::mv(primaryExpr); }

  // Parse member expression rest (property access chains)
  ZC_IF_SOME(expr, expression) {
    return parseMemberExpressionRest(zc::mv(expr), startLoc, /*allowOptionalChain*/ true);
  }

  return zc::none;
}

// Helper method to parse call expression rest
zc::Maybe<zc::Own<ast::LeftHandSideExpression>> Parser::parseCallExpressionRest(
    source::SourceLoc loc, zc::Own<ast::LeftHandSideExpression> expression) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseCallExpressionRest");

  // callExpression:
  //   (memberExpression arguments | superCall | importCall)
  //   (arguments | LBRACK expression RBRACK | PERIOD identifier)*;
  //
  // This method handles the iterative parsing of call chains

  while (true) {
    // First parse member expression rest to handle property/element access chains
    ZC_IF_SOME(memberExpr,
               parseMemberExpressionRest(zc::mv(expression), loc, /*allowOptionalChain*/ true)) {
      expression = zc::mv(memberExpr);
    }
    else { return zc::none; }

    // Parse optional type arguments for generic calls
    zc::Maybe<zc::Vector<zc::Own<ast::TypeNode>>> typeArguments = zc::none;
    zc::Maybe<zc::Own<ast::TokenNode>> questionDotToken =
        parseExpectedToken(ast::SyntaxKind::QuestionDot);

    // Check for optional chaining (?.)
    if (questionDotToken != zc::none) {
      // Try to parse type arguments after ?.
      typeArguments = tryParseTypeArgumentsInExpression();
    }

    // Handle function calls with optional type arguments
    if (typeArguments != zc::none || expectToken(ast::SyntaxKind::LeftParen)) {
      ZC_IF_SOME(arguments, parseArgumentList()) {
        auto callExpr = ast::factory::createCallExpression(
            zc::mv(expression), zc::mv(questionDotToken), zc::mv(typeArguments), zc::mv(arguments));
        expression = finishNode(zc::mv(callExpr), loc);
        continue;
      }
      else { return zc::none; }
    }

    // Handle optional chaining error case
    if (questionDotToken != zc::none) {
      // We parsed `?.` but then failed to parse anything, so report a missing identifier here.
      // TODO: Add proper diagnostic error reporting
      // parseErrorAtCurrentToken(diagnostics::Identifier_expected);

      // Create missing identifier and property access expression
      auto name = ast::factory::createMissingIdentifier();  // Missing identifier placeholder
      auto propAccessExpr = ast::factory::createPropertyAccessExpression(
          zc::mv(expression), zc::mv(name), true /* questionDot */);
      expression = finishNode(zc::mv(propAccessExpr), loc);
    }

    break;
  }

  return expression;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseShortCircuitExpression() {
  return parseErrorDefaultExpression();
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseErrorDefaultExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseErrorDefaultExpression");

  const source::SourceLoc startLoc = currentToken().getLocation();
  ZC_IF_SOME(expr, parseCoalesceExpression()) {
    while (expectToken(ast::SyntaxKind::ErrorDefault)) {
      ZC_IF_SOME(op, parseTokenNode()) {
        ZC_IF_SOME(right, parseCoalesceExpression()) {
          expr = finishNode(
              ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right)),
              startLoc);
        }
      }
    }
    return finishNode(zc::mv(expr), startLoc);
  }
  return zc::none;
}

zc::Maybe<zc::Own<ast::ConditionalExpression>> Parser::parseConditionalExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseConditionalExpression");

  // conditionalExpression:
  //   shortCircuitExpression (QUESTION assignmentExpression COLON assignmentExpression)?;
  //
  // Traditional parsing approach

  const source::SourceLoc startLoc = currentToken().getLocation();

  ZC_IF_SOME(expr, parseShortCircuitExpression()) {
    if (expectToken(ast::SyntaxKind::Question)) {
      nextToken();

      ZC_IF_SOME(thenExpr, parseAssignmentExpressionOrHigher()) {
        if (!consumeExpectedToken(ast::SyntaxKind::Colon)) { return zc::none; }

        ZC_IF_SOME(elseExpr, parseAssignmentExpressionOrHigher()) {
          return finishNode(ast::factory::createConditionalExpression(
                                zc::mv(expr), zc::mv(thenExpr), zc::mv(elseExpr)),
                            startLoc);
        }
      }
    }

    // Not a conditional expression, return none since this method expects ConditionalExpression
    return zc::none;
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseLogicalOrExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseLogicalOrExpression");

  // logicalORExpression:
  //   logicalANDExpression (OR logicalANDExpression)*;
  //
  // Handles logical OR operations with left-to-right associativity

  const source::SourceLoc startLoc = currentToken().getLocation();

  ZC_IF_SOME(expr, parseLogicalAndExpression()) {
    while (expectToken(ast::SyntaxKind::Bar)) {
      ZC_IF_SOME(op, parseTokenNode()) {
        ZC_IF_SOME(right, parseLogicalAndExpression()) {
          // Create binary expression AST node
          auto newExpr = finishNode(
              ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right)),
              startLoc);
          expr = zc::mv(newExpr);
        }
      }
      else {
        // Right operand parsing failed, but we already consumed the operator token.
        // This is a parse error - we should report it and break to avoid infinite loop.
        // TODO: Add proper error reporting here
        break;
      }
    }
    return finishNode(zc::mv(expr), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseLogicalAndExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseLogicalAndExpression");

  // logicalANDExpression:
  //   bitwiseORExpression (AND bitwiseORExpression)*;
  //
  // Handles logical AND operations with left-to-right associativity

  const source::SourceLoc startLoc = currentToken().getLocation();

  ZC_IF_SOME(expr, parseBitwiseOrExpression()) {
    while (expectToken(ast::SyntaxKind::AmpersandAmpersand)) {
      ZC_IF_SOME(op, parseTokenNode()) {
        ZC_IF_SOME(right, parseBitwiseOrExpression()) {
          // Create binary expression AST node
          auto newExpr = finishNode(
              ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right)),
              startLoc);
          expr = zc::mv(newExpr);
        }
      }
      else {
        // Right operand parsing failed, but we already consumed the operator token.
        // This is a parse error - we should report it and break to avoid infinite loop.
        // TODO: Add proper error reporting here
        break;
      }
    }
    return finishNode(zc::mv(expr), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseBitwiseOrExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBitwiseOrExpression");

  // bitwiseORExpression:
  //   bitwiseXORExpression (BITWISE_OR bitwiseXORExpression)*;
  //
  // Handles bitwise OR operations with left-to-right associativity

  const source::SourceLoc startLoc = currentToken().getLocation();

  ZC_IF_SOME(expr, parseBitwiseXorExpression()) {
    while (expectToken(ast::SyntaxKind::Bar)) {
      ZC_IF_SOME(op, parseTokenNode()) {
        ZC_IF_SOME(right, parseBitwiseXorExpression()) {
          // Create binary expression AST node
          auto newExpr = finishNode(
              ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right)),
              startLoc);
          expr = zc::mv(newExpr);
        }
      }
    }
    return finishNode(zc::mv(expr), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseBitwiseXorExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBitwiseXorExpression");

  // bitwiseXORExpression:
  //   bitwiseANDExpression (BITWISE_XOR bitwiseANDExpression)*;
  //
  // Handles bitwise XOR operations with left-to-right associativity

  const source::SourceLoc startLoc = currentToken().getLocation();

  ZC_IF_SOME(expr, parseBitwiseAndExpression()) {
    while (expectToken(ast::SyntaxKind::Caret)) {
      ZC_IF_SOME(op, parseTokenNode()) {
        ZC_IF_SOME(right, parseBitwiseAndExpression()) {
          // Create binary expression AST node
          auto newExpr = finishNode(
              ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right)),
              startLoc);
          expr = zc::mv(newExpr);
        }
      }
    }
    return finishNode(zc::mv(expr), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseBitwiseAndExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBitwiseAndExpression");

  // bitwiseANDExpression:
  //   equalityExpression (BITWISE_AND equalityExpression)*;
  //
  // Handles bitwise AND operations with left-to-right associativity

  const source::SourceLoc startLoc = currentToken().getLocation();

  ZC_IF_SOME(expr, parseEqualityExpression()) {
    while (expectToken(ast::SyntaxKind::Ampersand)) {
      ZC_IF_SOME(op, parseTokenNode()) {
        ZC_IF_SOME(right, parseEqualityExpression()) {
          // Create binary expression AST node
          auto newExpr = finishNode(
              ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right)),
              startLoc);
          expr = zc::mv(newExpr);
        }
      }
    }
    return finishNode(zc::mv(expr), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseEqualityExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseEqualityExpression");

  // equalityExpression:
  //   relationalExpression (equalityOperator relationalExpression)*;
  //
  // Handles equality and inequality comparisons with left-to-right associativity

  const source::SourceLoc startLoc = currentToken().getLocation();

  ZC_IF_SOME(expr, parseRelationalExpression()) {
    while (expectToken(ast::SyntaxKind::EqualsEquals) ||
           expectToken(ast::SyntaxKind::ExclamationEquals)) {
      ZC_IF_SOME(op, parseTokenNode()) {
        ZC_IF_SOME(right, parseRelationalExpression()) {
          // Create binary expression AST node
          auto newExpr = finishNode(
              ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right)),
              startLoc);
          expr = zc::mv(newExpr);
        }
      }
    }
    return finishNode(zc::mv(expr), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseRelationalExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseRelationalExpression");

  // relationalExpression:
  //   shiftExpression (relationalOperator shiftExpression)*;
  //
  // Handles relational comparisons (<, >, <=, >=) with left-to-right associativity

  const source::SourceLoc startLoc = currentToken().getLocation();

  ZC_IF_SOME(expr, parseShiftExpression()) {
    while (expectToken(ast::SyntaxKind::LessThan) || expectToken(ast::SyntaxKind::GreaterThan) ||
           expectToken(ast::SyntaxKind::LessThanEquals) ||
           expectToken(ast::SyntaxKind::GreaterThanEquals)) {
      ZC_IF_SOME(op, parseTokenNode()) {
        ZC_IF_SOME(right, parseShiftExpression()) {
          // Create binary expression AST node
          auto newExpr = finishNode(
              ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right)),
              startLoc);
          expr = zc::mv(newExpr);
        }
      }
    }
    return finishNode(zc::mv(expr), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseShiftExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseShiftExpression");

  // shiftExpression:
  //   additiveExpression (shiftOperator additiveExpression)*;
  //
  // Handles bit shift operations (<<, >>) with left-to-right associativity

  const source::SourceLoc startLoc = currentToken().getLocation();

  ZC_IF_SOME(expr, parseAdditiveExpression()) {
    while (expectToken(ast::SyntaxKind::LessThanLessThan) ||
           expectToken(ast::SyntaxKind::GreaterThanGreaterThan)) {
      ZC_IF_SOME(op, parseTokenNode()) {
        ZC_IF_SOME(right, parseAdditiveExpression()) {
          // Create binary expression AST node
          auto newExpr = finishNode(
              ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right)),
              startLoc);
          expr = zc::mv(newExpr);
        }
      }
    }
    return finishNode(zc::mv(expr), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseAdditiveExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseAdditiveExpression");

  // additiveExpression:
  //   multiplicativeExpression ((PLUS | MINUS) multiplicativeExpression)*;
  //
  // Handles addition and subtraction with left-to-right associativity

  const source::SourceLoc startLoc = currentToken().getLocation();

  ZC_IF_SOME(expr, parseMultiplicativeExpression()) {
    while (expectToken(ast::SyntaxKind::Plus) || expectToken(ast::SyntaxKind::Minus)) {
      ZC_IF_SOME(op, parseTokenNode()) {
        ZC_IF_SOME(right, parseMultiplicativeExpression()) {
          // Create binary expression AST node
          auto newExpr = finishNode(
              ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right)),
              startLoc);
          expr = zc::mv(newExpr);
        }
      }
    }
    return finishNode(zc::mv(expr), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseMultiplicativeExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseMultiplicativeExpression");

  // multiplicativeExpression:
  //   exponentiationExpression (multiplicativeOperator exponentiationExpression)*;
  //
  // Handles multiplication, division, and modulo with left-to-right associativity

  const lexer::Token token = currentToken();
  const source::SourceLoc startLoc = token.getLocation();

  ZC_IF_SOME(expr, parseExponentiationExpression()) {
    while (token.is(ast::SyntaxKind::Asterisk) || expectToken(ast::SyntaxKind::Slash) ||
           expectToken(ast::SyntaxKind::Percent)) {
      ZC_IF_SOME(op, parseTokenNode()) {
        ZC_IF_SOME(right, parseExponentiationExpression()) {
          // Create binary expression AST node
          auto newExpr = finishNode(
              ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right)),
              startLoc);
          expr = zc::mv(newExpr);
        }
      }
    }

    return finishNode(zc::mv(expr), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseExponentiationExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseExponentiationExpression");

  // exponentiationExpression:
  //   castExpression
  //   | updateExpression POW exponentiationExpression;
  //
  // Handles exponentiation with right-to-left associativity

  const lexer::Token token = currentToken();
  const source::SourceLoc startLoc = token.getLocation();

  // Try to parse castExpression first
  ZC_IF_SOME(expr, parseCastExpression()) { return zc::mv(expr); }

  // Try updateExpression POW exponentiationExpression (right-associative)
  ZC_IF_SOME(left, parseUpdateExpression()) {
    const lexer::Token token = currentToken();
    if (token.is(ast::SyntaxKind::AsteriskAsterisk)) {  // POW operator
      ZC_IF_SOME(op, parseTokenNode()) {
        ZC_IF_SOME(right, parseExponentiationExpression()) {  // Right-associative
          auto binaryExpr =
              ast::factory::createBinaryExpression(zc::mv(left), zc::mv(op), zc::mv(right));
          return finishNode(zc::mv(binaryExpr), startLoc);
        }
      }
    }
    return zc::mv(left);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::UpdateExpression>> Parser::parseUpdateExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseUpdateExpression");

  // updateExpression:
  //   leftHandSideExpression
  //   | leftHandSideExpression INC
  //   | leftHandSideExpression DEC
  //   | INC leftHandSideExpression
  //   | DEC leftHandSideExpression;

  const lexer::Token token = currentToken();
  const source::SourceLoc startLoc = token.getLocation();

  // Check for prefix increment/decrement operators
  if (token.is(ast::SyntaxKind::PlusPlus) || token.is(ast::SyntaxKind::MinusMinus)) {
    nextToken();

    // For prefix operators, parse unaryExpression (not leftHandSideExpression)
    ZC_IF_SOME(operand, parseLeftHandSideExpressionOrHigher()) {
      ast::SyntaxKind op = token.getKind();
      auto prefixExpr = ast::factory::createPrefixUnaryExpression(zc::mv(op), zc::mv(operand));
      return finishNode(zc::mv(prefixExpr), startLoc);
    }

    return zc::none;
  }

  // Parse leftHandSideExpression first
  ZC_IF_SOME(expression, parseLeftHandSideExpressionOrHigher()) {
    const lexer::Token postToken = currentToken();

    // Check for postfix increment/decrement operators
    if ((postToken.is(ast::SyntaxKind::PlusPlus) || postToken.is(ast::SyntaxKind::MinusMinus))) {
      nextToken();

      ast::SyntaxKind op = postToken.getKind();
      return finishNode(ast::factory::createPostfixUnaryExpression(zc::mv(op), zc::mv(expression)),
                        startLoc);
    }

    // No update operators found, return the expression as-is
    return finishNode(zc::mv(expression), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::LeftHandSideExpression>> Parser::parseLeftHandSideExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseLeftHandSideExpression");

  // leftHandSideExpression:
  //   newExpression
  //   | callExpression
  //   | optionalExpression;
  //
  // This is the main entry point for parsing left-hand side expressions
  // We delegate to parseLeftHandSideExpressionOrHigher for the actual implementation

  return parseLeftHandSideExpressionOrHigher();
}

zc::Maybe<zc::Own<ast::PrimaryExpression>> Parser::parsePrimaryExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parsePrimaryExpression");

  // primaryExpression:
  //   THIS
  //   | identifier
  //   | literal
  //   | arrayLiteral
  //   | objectLiteral
  //   | LPAREN expression RPAREN;
  //
  // Handles basic atomic expressions

  switch (currentToken().getKind()) {
    case ast::SyntaxKind::IntegerLiteral:
    case ast::SyntaxKind::FloatLiteral:
    case ast::SyntaxKind::StringLiteral:
    case ast::SyntaxKind::TrueKeyword:
    case ast::SyntaxKind::FalseKeyword:
    case ast::SyntaxKind::NullKeyword:
      return parseLiteralExpression();
    case ast::SyntaxKind::LeftParen:
      return parseParenthesizedExpression();
    case ast::SyntaxKind::LeftBracket:
      return parseArrayLiteralExpression();
    case ast::SyntaxKind::LeftBrace:
      return parseObjectLiteralExpression();
    case ast::SyntaxKind::FunKeyword:
      return parseFunctionExpression();
    case ast::SyntaxKind::NewKeyword:
      return parseNewExpression();
    default:
      break;
  }

  return parseIdentifier();
}

zc::Maybe<zc::Own<ast::LiteralExpression>> Parser::parseLiteralExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseLiteralExpression");

  // literal:
  //   nullLiteral
  //   | booleanLiteral
  //   | numericLiteral
  //   | stringLiteral;
  //
  // Handles all literal values (numbers, strings, booleans, null)

  const lexer::Token& token = currentToken();
  source::SourceLoc startLoc = token.getLocation();

  switch (token.getKind()) {
    case ast::SyntaxKind::IntegerLiteral: {
      zc::StringPtr value = token.getText(getSourceManager());
      nextToken();
      int64_t numValue = value.parseAs<int64_t>();
      return finishNode(ast::factory::createIntegerLiteral(numValue), startLoc);
    }
    case ast::SyntaxKind::FloatLiteral: {
      zc::StringPtr value = token.getText(getSourceManager());
      nextToken();
      double numValue = value.parseAs<double>();
      return finishNode(ast::factory::createFloatLiteral(numValue), startLoc);
    }
    case ast::SyntaxKind::StringLiteral: {
      zc::StringPtr value = token.getText(getSourceManager());
      nextToken();
      return finishNode(ast::factory::createStringLiteral(value), startLoc);
    }
    case ast::SyntaxKind::TrueKeyword: {
      nextToken();
      return finishNode(ast::factory::createBooleanLiteral(true), startLoc);
    }
    case ast::SyntaxKind::FalseKeyword: {
      nextToken();
      return finishNode(ast::factory::createBooleanLiteral(false), startLoc);
    }
    case ast::SyntaxKind::NullKeyword: {
      nextToken();
      return finishNode(ast::factory::createNullLiteral(), startLoc);
    }

    default:
      return zc::none;
  }
}

zc::Maybe<zc::Own<ast::ArrayLiteralExpression>> Parser::parseArrayLiteralExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseArrayLiteralExpression");

  // arrayLiteral:
  //   LBRACK RBRACK
  //   | LBRACK elementList RBRACK
  //   | LBRACK elementList COMMA RBRACK;
  //
  // Handles array literal expressions [1, 2, 3]

  source::SourceLoc startLoc = currentToken().getLocation();

  if (!consumeExpectedToken(ast::SyntaxKind::LeftBracket)) { return zc::none; }

  zc::Vector<zc::Own<ast::Expression>> elements;

  if (!expectToken(ast::SyntaxKind::RightBracket)) {
    do {
      ZC_IF_SOME(element, parseAssignmentExpressionOrHigher()) { elements.add(zc::mv(element)); }
    } while (consumeExpectedToken(ast::SyntaxKind::Comma));
  }

  if (!consumeExpectedToken(ast::SyntaxKind::RightBracket)) { return zc::none; }

  return finishNode(ast::factory::createArrayLiteralExpression(zc::mv(elements)), startLoc);
}

zc::Maybe<zc::Own<ast::ObjectLiteralExpression>> Parser::parseObjectLiteralExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseObjectLiteralExpression");

  // objectLiteral:
  //   LBRACE RBRACE
  //   | LBRACE propertyDefinitionList RBRACE
  //   | LBRACE propertyDefinitionList COMMA RBRACE;
  //
  // Handles object literal expressions {key: value}

  source::SourceLoc startLoc = currentToken().getLocation();

  if (!consumeExpectedToken(ast::SyntaxKind::LeftBrace)) { return zc::none; }

  zc::Vector<zc::Own<ast::Expression>> properties;

  // Parse object properties if not empty
  if (!expectToken(ast::SyntaxKind::RightBrace)) {
    do {
      // For now, parse simple property assignments
      ZC_IF_SOME(property, parseAssignmentExpressionOrHigher()) {
        properties.add(zc::mv(property));
      }
    } while (consumeExpectedToken(ast::SyntaxKind::Comma));
  }

  if (!consumeExpectedToken(ast::SyntaxKind::RightBrace)) { return zc::none; }

  return finishNode(ast::factory::createObjectLiteralExpression(zc::mv(properties)), startLoc);
}

// ================================================================================
// Type parsing implementations

zc::Maybe<zc::Own<ast::TypeNode>> Parser::parseType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseType");

  // type: unionType;
  //
  // unionType: intersectionType (BIT_OR intersectionType)*;
  //
  // intersectionType: postfixType (BIT_AND postfixType)*;
  //
  // postfixType: typeAtom (LBRACK RBRACK | QUESTION)*;
  //
  // typeAtom:
  //   parenthesizedType
  //   | predefinedType
  //   | typeReference
  //   | objectType
  //   | tupleType
  //   | typeQuery;
  //
  // parenthesizedType: LPAREN type RPAREN;
  //
  // predefinedType:
  //   I8
  //   | I16
  //   | I32
  //   | I64
  //   | U8
  //   | U16
  //   | U32
  //   | U64
  //   | F32
  //   | F64
  //   | STR
  //   | BOOL
  //   | NIL
  //   | UNIT;
  //
  // Return the parsed `unionType` directly, the suffix processing is completed in
  // `parsePrimaryType`.
  ZC_IF_SOME(type, parseUnionType()) { return zc::mv(type); }

  return zc::none;
}

zc::Maybe<zc::Own<ast::TypeNode>> Parser::parseTypeAnnotation() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseTypeAnnotation");

  if (!consumeExpectedToken(ast::SyntaxKind::Colon)) { return zc::none; }

  ZC_IF_SOME(type, parseType()) { return zc::mv(type); }

  return zc::none;
}

zc::Maybe<zc::Own<ast::TypeNode>> Parser::parseUnionType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseUnionType");

  // unionType:
  //   intersectionType (PIPE intersectionType)*;
  //
  // Handles union types like A | B | C

  const source::SourceLoc startLoc = currentToken().getLocation();

  ZC_IF_SOME(type, parseIntersectionType()) {
    zc::Vector<zc::Own<ast::TypeNode>> types;
    types.add(zc::mv(type));

    while (expectToken(ast::SyntaxKind::Bar)) {
      nextToken();
      ZC_IF_SOME(rightType, parseIntersectionType()) { types.add(zc::mv(rightType)); }
    }

    // Only create UnionType if there are multiple types
    if (types.size() == 1) { return zc::mv(types[0]); }

    return finishNode(ast::factory::createUnionType(zc::mv(types)), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::TypeNode>> Parser::parseIntersectionType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseIntersectionType");

  // intersectionType:
  //   primaryType (AMPERSAND primaryType)*;
  //
  // Handles intersection types like A & B & C

  const source::SourceLoc startLoc = currentToken().getLocation();

  ZC_IF_SOME(type, parsePostfixType()) {
    zc::Vector<zc::Own<ast::TypeNode>> types;
    types.add(zc::mv(type));

    while (expectToken(ast::SyntaxKind::Ampersand)) {
      nextToken();
      ZC_IF_SOME(rightType, parsePostfixType()) { types.add(zc::mv(rightType)); }
    }

    // Only create IntersectionType if there are multiple types
    if (types.size() == 1) { return zc::mv(types[0]); }

    return finishNode(ast::factory::createIntersectionType(zc::mv(types)), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::TypeNode>> Parser::parsePostfixType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parsePostfixType");

  // Parse postfix type according to the grammar rule:
  //
  // postfixType: typeAtom (LBRACK RBRACK | QUESTION)*
  //
  // This handles type atoms followed by optional array brackets [] or optional marker ?
  // Examples: T[], T?, T[]?, MyType[][]

  const lexer::Token token = currentToken();
  const source::SourceLoc startLoc = token.getLocation();

  // Parse the base type atom
  zc::Maybe<zc::Own<ast::TypeNode>> maybeBase;
  switch (currentToken().getKind()) {
    case ast::SyntaxKind::LeftParen:
      // Parenthesized type: (T)
      maybeBase = parseParenthesizedType();
      break;
    case ast::SyntaxKind::LeftBrace:
      // Object type: {prop: T}
      maybeBase = parseObjectType();
      break;
    case ast::SyntaxKind::Identifier:
      // Type reference: MyType or MyType<T>
      maybeBase = parseTypeReference();
      break;
    case ast::SyntaxKind::TypeOfKeyword:
      // Type query: typeof expr
      maybeBase = parseTypeQuery();
      break;
    default:
      // Predefined type: i32, str, bool, etc.
      maybeBase = parsePredefinedType();
      break;
  }

  ZC_IF_SOME(result, zc::mv(maybeBase)) {
    // Apply postfix operators: [] for arrays and ? for optional types
    while (true) {
      // Array type suffix: []
      if (expectToken(ast::SyntaxKind::LeftBracket)) {
        nextToken();  // consume '['
        if (!consumeExpectedToken(ast::SyntaxKind::RightBracket)) {
          // Error: expected closing bracket ']'
          return zc::none;
        }
        result = ast::factory::createArrayType(zc::mv(result));
        continue;
      }

      // Optional type suffix: ?
      if (expectToken(ast::SyntaxKind::Question)) {
        nextToken();  // consume '?'
        result = finishNode(ast::factory::createOptionalType(zc::mv(result)), startLoc);
        continue;
      }

      break;
    }

    return zc::mv(result);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::ArrayTypeNode>> Parser::parseArrayType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseArrayType");

  // Parse array type according to the grammar rule:
  // arrayType: postfixType LBRACK RBRACK
  // This handles array types like T[], MyType[], etc.
  // The array suffix [] can be chained: T[][] for multi-dimensional arrays

  const source::SourceLoc startLoc = currentToken().getLocation();

  ZC_IF_SOME(elementType, parsePostfixType()) {
    if (expectToken(ast::SyntaxKind::LeftBracket)) {
      nextToken();  // consume '['

      if (!consumeExpectedToken(ast::SyntaxKind::RightBracket)) {
        // Error: expected closing bracket ']' for array type
        return zc::none;
      }

      // Create array type node with the element type
      return finishNode(ast::factory::createArrayType(zc::mv(elementType)), startLoc);
    }
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::FunctionTypeNode>> Parser::parseFunctionType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseFunctionType");

  // Parse function type according to the grammar rule:
  //
  // functionType: typeParameters? parameterClause (ARROW type raisesClause?)?
  //
  // This handles function types like (a: T, b: U) -> R or (a: T) -> R raises E
  // where raisesClause is optional and specifies error types that the function can raise

  source::SourceLoc startLoc = currentToken().getLocation();

  // Parse optional type parameters: <T, U>
  zc::Vector<zc::Own<ast::TypeParameterDeclaration>> typeParameters = parseTypeParameters();
  // Parse parameter clause: (param1: Type1, param2: Type2)
  zc::Vector<zc::Own<ast::BindingElement>> parameters = parseParameters();
  // Parse return type: -> type raises error
  ZC_IF_SOME(returnType, parseReturnType()) {
    return finishNode(ast::factory::createFunctionType(zc::mv(typeParameters), zc::mv(parameters),
                                                       zc::mv(returnType)),
                      startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::ParenthesizedTypeNode>> Parser::parseParenthesizedType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseParenthesizedType");

  source::SourceLoc startLoc = currentToken().getLocation();

  if (!consumeExpectedToken(ast::SyntaxKind::LeftParen)) { return zc::none; }

  ZC_IF_SOME(type, parseType()) {
    if (!consumeExpectedToken(ast::SyntaxKind::RightParen)) { return zc::none; }
    return finishNode(ast::factory::createParenthesizedType(zc::mv(type)), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::ObjectTypeNode>> Parser::parseObjectType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseObjectType");

  // objectType:
  //   LBRACE typeMemberList? RBRACE;
  //
  // Handles object types like {prop: T, method(): U}

  if (!consumeExpectedToken(ast::SyntaxKind::LeftBrace)) { return zc::none; }

  zc::Vector<zc::Own<ast::Node>> members;

  // Parse object type members
  if (!expectToken(ast::SyntaxKind::RightBrace)) {
    do {
      // Parse property signature: identifier COLON type
      ZC_IF_SOME(propertyName, parseIdentifier()) {
        if (consumeExpectedToken(ast::SyntaxKind::Colon)) {
          ZC_IF_SOME(propertyType, parseType()) {
            // TODO: Implement PropertySignature properly
            // For now, skip adding property signatures to avoid compilation error
            (void)propertyName;  // Suppress unused variable warning
            (void)propertyType;  // Suppress unused variable warning
          }
        }
      }
    } while (consumeExpectedToken(ast::SyntaxKind::Comma) ||
             consumeExpectedToken(ast::SyntaxKind::Semicolon));
  }

  if (!consumeExpectedToken(ast::SyntaxKind::RightBrace)) { return zc::none; }

  return ast::factory::createObjectType(zc::mv(members));
}

zc::Maybe<zc::Own<ast::TupleTypeNode>> Parser::parseTupleType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseTupleType");

  // Parse tuple type according to the grammar rule:
  //
  // tupleType: LPAREN tupleElementTypes? RPAREN
  //
  // This handles tuple types like (T, U, V) for function parameters and return types
  // Each element can be a named tuple element: (name: T, other: U)

  source::SourceLoc startLoc = currentToken().getLocation();

  if (!consumeExpectedToken(ast::SyntaxKind::LeftParen)) { return zc::none; }

  // TODO: Implement TupleElement class
  // zc::Vector<zc::Own<ast::TupleElement>> elements;
  zc::Vector<zc::Own<ast::TypeNode>> elements;

  if (!expectToken(ast::SyntaxKind::RightParen)) {
    do {
      // Parse named or unnamed tuple element
      zc::Maybe<zc::Own<ast::Identifier>> name;
      // Try to parse identifier followed by colon for named tuple elements
      if (currentToken().is(ast::SyntaxKind::Identifier)) {
        // Save state to check if next token is colon
        ParserState state = mark();
        name = parseIdentifier();
        if (currentToken().is(ast::SyntaxKind::Colon)) {
          nextToken();  // consume ':'
        } else {
          // Not a named element, restore state and continue
          rewind(state);
          name = zc::none;
        }
      }
      ZC_IF_SOME(type, parseType()) {
        // TODO: Implement TupleElement creation
        // if (name.hasValue()) {
        //   elements.add(ast::factory::createNamedTupleElement(zc::mv(name).value(),
        //   zc::mv(type)));
        // } else {
        //   elements.add(ast::factory::createTupleElement(zc::mv(type)));
        // }
        ZC_IF_SOME(n, name) { static_cast<void>(n); }
        elements.add(zc::mv(type));
      }
    } while (consumeExpectedToken(ast::SyntaxKind::Comma));
  }

  if (!consumeExpectedToken(ast::SyntaxKind::RightParen)) { return zc::none; }

  return finishNode(ast::factory::createTupleType(zc::mv(elements)), startLoc);
}

zc::Maybe<zc::Own<ast::TypeReferenceNode>> Parser::parseTypeReference() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseTypeReference");

  // typeReference: typeName typeArguments?
  //
  // This handles type references like MyType or generic types like MyType<T, U>
  // where typeArguments are optional type parameters in angle brackets

  source::SourceLoc startLoc = currentToken().getLocation();

  ZC_IF_SOME(typeName, parseIdentifier()) {
    // Handle optional type arguments: <T, U, V>
    zc::Maybe<zc::Vector<zc::Own<ast::TypeNode>>> typeArguments = zc::none;
    if (expectToken(ast::SyntaxKind::LessThan)) {
      zc::Vector<zc::Own<ast::TypeNode>> args;
      nextToken();  // consume '<'
      while (!expectToken(ast::SyntaxKind::GreaterThan)) {
        ZC_IF_SOME(arg, parseType()) { args.add(zc::mv(arg)); }
        else { return zc::none; }
        if (!expectToken(ast::SyntaxKind::GreaterThan) &&
            !consumeExpectedToken(ast::SyntaxKind::Comma)) {
          break;
        }
      }
      if (!consumeExpectedToken(ast::SyntaxKind::GreaterThan)) {
        // Error: expected closing angle bracket '>'
        return zc::none;
      }
      typeArguments = zc::mv(args);
    }
    return finishNode(ast::factory::createTypeReference(zc::mv(typeName), zc::mv(typeArguments)),
                      startLoc);
  }
  return zc::none;
}

zc::Maybe<zc::Own<ast::PredefinedTypeNode>> Parser::parsePredefinedType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parsePredefinedType");

  // Parse predefined type according to the grammar rule:
  // predefinedType: I8 | I32 | I64 | U8 | U16 | U32 | U64 | F32 | F64 | STR | BOOL | NIL | UNIT
  // These are the built-in primitive types in the ZOM language

  const lexer::Token& token = currentToken();
  const source::SourceLoc startLoc = token.getLocation();

  switch (token.getKind()) {
    case ast::SyntaxKind::I8Keyword:
    case ast::SyntaxKind::I32Keyword:
    case ast::SyntaxKind::I64Keyword:
    case ast::SyntaxKind::U8Keyword:
    case ast::SyntaxKind::U16Keyword:
    case ast::SyntaxKind::U32Keyword:
    case ast::SyntaxKind::U64Keyword:
    case ast::SyntaxKind::F32Keyword:
    case ast::SyntaxKind::F64Keyword:
    case ast::SyntaxKind::StrKeyword:
    case ast::SyntaxKind::BoolKeyword:
    case ast::SyntaxKind::NullKeyword:
    case ast::SyntaxKind::UnitKeyword: {
      zc::StringPtr typeName = token.getText(getSourceManager());
      nextToken();
      return finishNode(ast::factory::createPredefinedType(typeName), startLoc);
    }

    default:
      return zc::none;
  }
}

zc::Maybe<zc::Own<ast::Pattern>> Parser::parsePattern() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parsePattern");

  switch (currentToken().getKind()) {
    case ast::SyntaxKind::Underscore:
      return parseWildcardPattern();
    case ast::SyntaxKind::Identifier:
      return parseIdentifierPattern();
    case ast::SyntaxKind::LeftParen:
      return parseTuplePattern();
    case ast::SyntaxKind::LeftBrace:
      return parseStructurePattern();
    default:
      // Handle other pattern types (array, is-pattern, etc.)
      return zc::none;
  }
}

zc::Maybe<zc::Own<ast::Pattern>> Parser::parseWildcardPattern() {
  const source::SourceLoc startLoc = currentToken().getLocation();
  nextToken();  // consume '_'

  zc::Maybe<zc::Own<ast::TypeNode>> type;
  if (consumeExpectedToken(ast::SyntaxKind::Colon)) { type = parseType(); }

  // TODO: Handle type annotation for wildcard patterns
  static_cast<void>(type);
  return finishNode(ast::factory::createWildcardPattern(), startLoc);
}

zc::Maybe<zc::Own<ast::Pattern>> Parser::parseIdentifierPattern() {
  ZC_IF_SOME(id, parseIdentifier()) {
    zc::Maybe<zc::Own<ast::TypeNode>> type;
    if (consumeExpectedToken(ast::SyntaxKind::Colon)) {
      type = parseType();
      // TODO: Handle type annotation for identifier patterns
      static_cast<void>(type);
    }
    return ast::factory::createIdentifierPattern(zc::mv(id));
  }
  return zc::none;
}

zc::Maybe<zc::Own<ast::Pattern>> Parser::parseTuplePattern() {
  if (!consumeExpectedToken(ast::SyntaxKind::LeftParen)) { return zc::none; }

  zc::Vector<zc::Own<ast::Pattern>> elements;
  while (!expectToken(ast::SyntaxKind::RightParen)) {
    ZC_IF_SOME(pattern, parsePattern()) { elements.add(zc::mv(pattern)); }
    if (!expectToken(ast::SyntaxKind::RightParen) &&
        !consumeExpectedToken(ast::SyntaxKind::Comma)) {
      break;
    }
  }

  if (!consumeExpectedToken(ast::SyntaxKind::RightParen)) { return zc::none; }
  return ast::factory::createTuplePattern(zc::mv(elements));
}

zc::Maybe<zc::Own<ast::Pattern>> Parser::parseStructurePattern() {
  if (!consumeExpectedToken(ast::SyntaxKind::LeftBrace)) { return zc::none; }

  zc::Vector<zc::Own<ast::Pattern>> properties;
  while (!expectToken(ast::SyntaxKind::RightBrace)) {
    ZC_IF_SOME(id, parseIdentifier()) {
      if (consumeExpectedToken(ast::SyntaxKind::Colon)) {
        ZC_IF_SOME(nestedPattern, parsePattern()) {
          // TODO: Implement createPropertyPattern
          // properties.add(ast::factory::createPropertyPattern(zc::mv(id), zc::mv(nestedPattern)));
          static_cast<void>(id);
          static_cast<void>(nestedPattern);
        }
      } else {
        // TODO: Implement createPropertyPattern
        // properties.add(ast::factory::createPropertyPattern(zc::mv(id)));
        static_cast<void>(id);
      }
    }
    if (!expectToken(ast::SyntaxKind::RightBrace) &&
        !consumeExpectedToken(ast::SyntaxKind::Comma)) {
      break;
    }
  }

  if (!consumeExpectedToken(ast::SyntaxKind::RightBrace)) { return zc::none; }
  // TODO: Implement proper structure pattern parsing with type reference
  return zc::none;
}

zc::Maybe<zc::Own<ast::BindingPattern>> Parser::parseArrayBindingPattern() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseArrayBindingPattern");

  if (!consumeExpectedToken(ast::SyntaxKind::LeftBracket)) { return zc::none; }

  zc::Vector<zc::Own<ast::BindingElement>> elements;
  while (!expectToken(ast::SyntaxKind::RightBracket)) {
    // TODO: Handle ellipsis/spread patterns
    if (false) {  // Temporarily disabled
      // Parse spread element
      nextToken();  // consume '...'
      ZC_IF_SOME(expr, parseAssignmentExpressionOrHigher()) {
        static_cast<void>(expr);
        // elements.add(ast::factory::createSpreadElement(zc::mv(expr)));
      }
    } else {
      ZC_IF_SOME(elem, parseBindingElement()) { elements.add(zc::mv(elem)); }
    }
    if (!expectToken(ast::SyntaxKind::RightBracket) &&
        !consumeExpectedToken(ast::SyntaxKind::Comma)) {
      break;
    }
  }

  if (!consumeExpectedToken(ast::SyntaxKind::RightBracket)) { return zc::none; }
  return ast::factory::createArrayBindingPattern(zc::mv(elements));
}

zc::Maybe<zc::Own<ast::BindingPattern>> Parser::parseObjectBindingPattern() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseObjectBindingPattern");

  if (!consumeExpectedToken(ast::SyntaxKind::LeftBrace)) { return zc::none; }

  // TODO: Implement full object binding pattern parsing
  // For now, just consume tokens until closing brace
  while (!expectToken(ast::SyntaxKind::RightBrace)) { nextToken(); }

  zc::Vector<zc::Own<ast::BindingElement>> properties;

  if (!consumeExpectedToken(ast::SyntaxKind::RightBrace)) { return zc::none; }
  return ast::factory::createObjectBindingPattern(zc::mv(properties));
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseCoalesceExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseCoalesceExpression");

  // coalesceExpression: bitwiseORExpression (NULL_COALESCE bitwiseORExpression)*;

  const source::SourceLoc startLoc = currentToken().getLocation();

  ZC_IF_SOME(expr, parseBitwiseOrExpression()) {
    while (expectToken(ast::SyntaxKind::QuestionQuestion)) {
      ZC_IF_SOME(op, parseTokenNode()) {
        ZC_IF_SOME(right, parseBitwiseOrExpression()) {
          // Create binary expression AST node
          auto newExpr =
              ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right));
          expr = zc::mv(newExpr);
        }
      }
    }
    return finishNode(zc::mv(expr), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::CastExpression>> Parser::parseCastExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseCastExpression");

  // castExpression: unaryExpression (AS (QUESTION | NOT)? type)*;

  const source::SourceLoc startLoc = currentToken().getLocation();

  ZC_IF_SOME(expr, parseSimpleUnaryExpression()) {
    while (expectToken(ast::SyntaxKind::AsKeyword)) {
      nextToken();

      bool isOptional = false;
      bool isNonNull = false;

      if (expectToken(ast::SyntaxKind::Question)) {
        isOptional = true;
        nextToken();
      } else if (expectToken(ast::SyntaxKind::Exclamation)) {
        isNonNull = true;
        nextToken();
      }

      ZC_IF_SOME(type, parseType()) {
        if (isOptional) {
          return finishNode(ast::factory::createConditionalAsExpression(zc::mv(expr), zc::mv(type)),
                            startLoc);
        } else if (isNonNull) {
          return finishNode(ast::factory::createForcedAsExpression(zc::mv(expr), zc::mv(type)),
                            startLoc);
        } else {
          return finishNode(ast::factory::createAsExpression(zc::mv(expr), zc::mv(type)), startLoc);
        }
      }
    }

    // If no cast, return the unary expression as a cast expression
    // This might need adjustment based on the actual AST hierarchy
    return zc::none;
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::AwaitExpression>> Parser::parseAwaitExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseAwaitExpression");

  // awaitExpression: AWAIT unaryExpression;

  const source::SourceLoc startLoc = currentToken().getLocation();
  nextToken();

  ZC_IF_SOME(expr, parseSimpleUnaryExpression()) {
    return finishNode(ast::factory::createAwaitExpression(zc::mv(expr)), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::DebuggerStatement>> Parser::parseDebuggerStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseDebuggerStatement");

  source::SourceLoc startLoc = currentToken().getLocation();
  if (!consumeExpectedToken(ast::SyntaxKind::DebuggerKeyword)) { return zc::none; }

  if (!consumeExpectedToken(ast::SyntaxKind::Semicolon)) { return zc::none; }

  return finishNode(ast::factory::createDebuggerStatement(), startLoc);
}

zc::Maybe<zc::Own<ast::NewExpression>> Parser::parseNewExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseNewExpression");

  // newExpression: memberExpression | NEW newExpression;
  // memberExpression: (primaryExpression | superProperty | NEW memberExpression arguments)
  //                   (LBRACK expression RBRACK | PERIOD identifier)*;

  const source::SourceLoc startLoc = currentToken().getLocation();

  if (!expectToken(ast::SyntaxKind::NewKeyword)) { return zc::none; }

  nextToken();

  // Parse the expression part using parseMemberExpressionRest
  zc::Maybe<zc::Own<ast::LeftHandSideExpression>> expression = zc::none;

  const source::SourceLoc exprLoc = currentToken().getLocation();

  // Parse primary expression first
  ZC_IF_SOME(primaryExpr, parsePrimaryExpression()) {
    // Use parseMemberExpressionRest to handle member access chains
    // allowOptionalChain is set to false for new expressions
    ZC_IF_SOME(memberExpr, parseMemberExpressionRest(zc::mv(primaryExpr), exprLoc, false)) {
      expression = zc::mv(memberExpr);
    }
    else { return zc::none; }
  }
  else { return zc::none; }

  // Check for invalid optional chain from new expression
  if (expectToken(ast::SyntaxKind::QuestionDot)) {
    // TODO: Add proper diagnostic error reporting
    // parseErrorAtCurrentToken(Diagnostics.Invalid_optional_chain_from_new_expression_Did_you_mean_to_call_0,
    // getTextOfNodeFromSourceText(sourceText, expression));
    return zc::none;
  }

  // Parse optional arguments (argumentList)
  zc::Vector<zc::Own<ast::Expression>> arguments;
  if (expectToken(ast::SyntaxKind::LeftParen)) {
    ZC_IF_SOME(argList, parseArgumentList()) { arguments = zc::mv(argList); }
    else { return zc::none; }
  }

  ZC_IF_SOME(expr, expression) {
    return finishNode(ast::factory::createNewExpression(zc::mv(expr), zc::mv(arguments)), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::ParenthesizedExpression>> Parser::parseParenthesizedExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseParenthesizedExpression");

  // parenthesizedExpression: LPAREN expression RPAREN;

  const source::SourceLoc startLoc = currentToken().getLocation();

  if (!consumeExpectedToken(ast::SyntaxKind::LeftParen)) { return zc::none; }

  ZC_IF_SOME(expr, parseExpression()) {
    if (!consumeExpectedToken(ast::SyntaxKind::RightParen)) { return zc::none; }

    // Create a parenthesized expression with the parsed expression
    return finishNode(ast::factory::createParenthesizedExpression(zc::mv(expr)), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::LeftHandSideExpression>> Parser::parseMemberExpressionRest(
    zc::Own<ast::LeftHandSideExpression> expression, source::SourceLoc pos,
    bool allowOptionalChain) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseMemberExpressionRest");

  while (true) {
    bool questionDotToken = false;
    bool isPropertyAccess = false;

    if (allowOptionalChain && isStartOfOptionalPropertyOrElementAccessChain()) {
      // After ?., check if next token is identifier (property access) or '[' (element access)
      questionDotToken = parseOptional(ast::SyntaxKind::QuestionDot);
      isPropertyAccess = isIdentifierOrKeyword();
    } else {
      // Check for regular property access
      isPropertyAccess = parseOptional(ast::SyntaxKind::Period);
    }

    if (isPropertyAccess) {
      // Property access: obj.prop or obj?.prop
      ZC_IF_SOME(propertyAccess,
                 parsePropertyAccessExpressionRest(zc::mv(expression), questionDotToken, pos)) {
        expression = zc::mv(propertyAccess);
        continue;
      }
      return zc::none;
    }

    // Check for element access: obj[expr] or obj?.[expr]
    if (parseOptional(ast::SyntaxKind::LeftBracket)) {
      ZC_IF_SOME(elementAccess,
                 parseElementAccessExpressionRest(zc::mv(expression), questionDotToken, pos)) {
        if (!consumeExpectedToken(ast::SyntaxKind::RightBracket)) { return zc::none; }
        expression = zc::mv(elementAccess);
        continue;
      }
      return zc::none;
    }

    // If we had a questionDotToken but couldn't parse property or element access, it's an error
    if (!questionDotToken) {
      if (expectToken(ast::SyntaxKind::Exclamation) && !currentToken().hasPrecedingLineBreak()) {
        nextToken();
        expression = finishNode(ast::factory::createNonNullExpression(zc::mv(expression)), pos);
        continue;
      }
      auto typeArguments = tryParseTypeArgumentsInExpression();
      if (typeArguments != zc::none) {
        expression = finishNode(ast::factory::createExpressionWithTypeArguments(
                                    zc::mv(expression), zc::mv(typeArguments)),
                                pos);
        continue;
      }
    }

    // No more member expressions, only return if expr is already a LeftHandSideExpression
    return zc::mv(expression);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::MemberExpression>> Parser::parseSuperExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseSuperExpression");

  const lexer::Token token = currentToken();
  const source::SourceLoc startLoc = token.getLocation();

  if (!expectToken(ast::SyntaxKind::SuperKeyword)) { return zc::none; }

  nextToken();  // consume 'super'

  // Create the super identifier as the base expression
  zc::Own<ast::Identifier> expression = ast::factory::createIdentifier("super"_zc);

  // Check for type arguments (e.g., super<T>)
  if (expectToken(ast::SyntaxKind::LessThan)) {
    source::SourceLoc errorLoc = currentToken().getLocation();
    impl->diagnosticEngine.diagnose<diagnostics::DiagID::InvalidCharacter>(errorLoc);
  }

  // Check what follows the super keyword
  if (expectToken(ast::SyntaxKind::LeftParen) || expectToken(ast::SyntaxKind::Period) ||
      expectToken(ast::SyntaxKind::LeftBracket)) {
    // Valid super usage - return the base expression
    // The caller will handle member access or call expressions
    return finishNode(zc::mv(expression), startLoc);
  }

  // If we reach here, super must be followed by '(', '.', or '['
  // Report an error and try to recover by parsing a dot
  source::SourceLoc errorLoc = currentToken().getLocation();
  impl->diagnosticEngine.diagnose<diagnostics::DiagID::InvalidCharacter>(errorLoc);

  // Try to recover by expecting a dot and parsing the right side
  if (expectToken(ast::SyntaxKind::Period)) {
    nextToken();  // consume '.'
    ZC_IF_SOME(property, parseIdentifier()) {
      return finishNode(
          ast::factory::createPropertyAccessExpression(zc::mv(expression), zc::mv(property), false),
          startLoc);
    }
  }

  return finishNode(zc::mv(expression), startLoc);
}

zc::Maybe<zc::Own<ast::MemberExpression>> Parser::parseImportCallExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseImportCallExpression");

  const lexer::Token token = currentToken();
  const source::SourceLoc startLoc = token.getLocation();

  if (!expectToken(ast::SyntaxKind::ImportKeyword)) { return zc::none; }

  nextToken();  // consume 'import'

  // Create the import identifier as the base expression
  zc::Own<ast::Identifier> expression = ast::factory::createIdentifier("import"_zc);

  // Check for type arguments (e.g., import<T>)
  if (expectToken(ast::SyntaxKind::LessThan)) {
    source::SourceLoc errorLoc = currentToken().getLocation();
    impl->diagnosticEngine.diagnose<diagnostics::DiagID::InvalidCharacter>(errorLoc);
  }

  // Check what follows the import keyword
  if (expectToken(ast::SyntaxKind::LeftParen) || expectToken(ast::SyntaxKind::Period) ||
      expectToken(ast::SyntaxKind::LeftBracket)) {
    // Valid import usage - return the base expression
    // The caller will handle member access or call expressions
    return finishNode(zc::mv(expression), startLoc);
  }

  // If we reach here, import must be followed by '(', '.', or '['
  // Report an error and try to recover by parsing a dot
  source::SourceLoc errorLoc = currentToken().getLocation();
  impl->diagnosticEngine.diagnose<diagnostics::DiagID::InvalidCharacter>(errorLoc);

  // Try to recover by expecting a dot and parsing the right side
  if (expectToken(ast::SyntaxKind::Period)) {
    nextToken();  // consume '.'
    ZC_IF_SOME(property, parseIdentifier()) {
      return finishNode(
          ast::factory::createPropertyAccessExpression(zc::mv(expression), zc::mv(property), false),
          startLoc);
    }
  }

  return finishNode(zc::mv(expression), startLoc);
}

source::SourceLoc Parser::getFullStartLoc() const { return impl->lexer.getFullStartLoc(); }

// ================================================================================
// Literal parsing implementations

zc::Maybe<zc::Own<ast::StringLiteral>> Parser::parseStringLiteral() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseStringLiteral");

  const lexer::Token& token = currentToken();
  if (!token.is(ast::SyntaxKind::StringLiteral)) { return zc::none; }

  source::SourceLoc startLoc = token.getLocation();
  zc::StringPtr value = token.getText(getSourceManager());
  nextToken();

  return finishNode(ast::factory::createStringLiteral(value), startLoc);
}

zc::Maybe<zc::Own<ast::IntegerLiteral>> Parser::parseIntegerLiteral() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseIntegerLiteral");

  const lexer::Token& token = currentToken();
  if (!token.is(ast::SyntaxKind::IntegerLiteral)) { return zc::none; }

  source::SourceLoc startLoc = token.getLocation();
  zc::StringPtr value = token.getText(getSourceManager());
  nextToken();

  int64_t numValue = value.parseAs<int64_t>();
  return finishNode(ast::factory::createIntegerLiteral(numValue), startLoc);
}

zc::Maybe<zc::Own<ast::FloatLiteral>> Parser::parseFloatLiteral() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseFloatLiteral");

  const lexer::Token& token = currentToken();
  if (!token.is(ast::SyntaxKind::FloatLiteral)) { return zc::none; }

  source::SourceLoc startLoc = token.getLocation();
  zc::StringPtr value = token.getText(getSourceManager());
  nextToken();

  double numValue = value.parseAs<double>();
  return finishNode(ast::factory::createFloatLiteral(numValue), startLoc);
}

zc::Maybe<zc::Own<ast::BooleanLiteral>> Parser::parseBooleanLiteral() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBooleanLiteral");

  const lexer::Token& token = currentToken();
  if (!token.is(ast::SyntaxKind::TrueKeyword) && !token.is(ast::SyntaxKind::FalseKeyword)) {
    return zc::none;
  }

  source::SourceLoc startLoc = token.getLocation();
  bool value = token.is(ast::SyntaxKind::TrueKeyword);
  nextToken();

  return finishNode(ast::factory::createBooleanLiteral(value), startLoc);
}

zc::Maybe<zc::Own<ast::NullLiteral>> Parser::parseNullLiteral() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseNullLiteral");

  const lexer::Token& token = currentToken();
  if (!token.is(ast::SyntaxKind::NullKeyword)) { return zc::none; }

  source::SourceLoc startLoc = token.getLocation();
  nextToken();

  return finishNode(ast::factory::createNullLiteral(), startLoc);
}

zc::Maybe<zc::Own<ast::FunctionExpression>> Parser::parseFunctionExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseFunctionExpression");

  // functionExpression:
  //   FUN callSignature LBRACE functionBody RBRACE;
  //
  // callSignature:
  //   typeParameters? parameterClause (ARROW type raisesClause?)?;

  if (!expectToken(ast::SyntaxKind::FunKeyword)) { return zc::none; }

  const lexer::Token& token = currentToken();
  source::SourceLoc startLoc = token.getLocation();
  nextToken();  // consume 'fun'

  // Parse callSignature
  zc::Vector<zc::Own<ast::TypeParameterDeclaration>> typeParameters = parseTypeParameters();
  // Parse parameter list
  zc::Vector<zc::Own<ast::BindingElement>> parameters = parseParameters();
  // Parse capture clause
  zc::Vector<zc::Own<ast::CaptureElement>> captures = parseCaptureClause();

  // Parse optional return type or error return clause
  zc::Maybe<zc::Own<ast::TypeNode>> returnType = parseReturnType();

  // Parse function body: LBRACE functionBody RBRACE
  if (!expectToken(ast::SyntaxKind::LeftBrace)) { return zc::none; }

  ZC_IF_SOME(body, parseBlockStatement()) {
    return finishNode(
        ast::factory::createFunctionExpression(zc::mv(typeParameters), zc::mv(parameters),
                                               zc::mv(captures), zc::mv(returnType), zc::mv(body)),
        startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::TypeParameterDeclaration>> Parser::parseTypeParameter() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseTypeParameter");

  // typeParameter: identifier constraint?;
  // constraint: EXTENDS type;

  source::SourceLoc startLoc = currentToken().getLocation();

  ZC_IF_SOME(name, parseIdentifier()) {
    // Optional constraint
    zc::Maybe<zc::Own<ast::TypeNode>> constraint = zc::none;
    if (expectToken(ast::SyntaxKind::ExtendsKeyword)) {
      nextToken();
      constraint = parseType();
    }

    return finishNode(
        ast::factory::createTypeParameterDeclaration(zc::mv(name), zc::mv(constraint)), startLoc);
  }

  return zc::none;
}

zc::Vector<zc::Own<ast::TypeParameterDeclaration>> Parser::parseTypeParameters() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseTypeParameters");

  zc::Vector<zc::Own<ast::TypeParameterDeclaration>> typeParameters;

  if (consumeExpectedToken(ast::SyntaxKind::LessThan)) {
    do {
      ZC_IF_SOME(typeParameter, parseTypeParameter()) { typeParameters.add(zc::mv(typeParameter)); }
      else { return zc::Vector<zc::Own<ast::TypeParameterDeclaration>>(); }
    } while (consumeExpectedToken(ast::SyntaxKind::Comma));

    if (!consumeExpectedToken(ast::SyntaxKind::GreaterThan)) {
      return zc::Vector<zc::Own<ast::TypeParameterDeclaration>>();
    }
  }

  return typeParameters;
}

zc::Vector<zc::Own<ast::BindingElement>> Parser::parseParameters() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseParameters");

  zc::Vector<zc::Own<ast::BindingElement>> parameters;
  if (consumeExpectedToken(ast::SyntaxKind::LeftParen)) {
    // Parse parameterList: parameter (COMMA parameter)*
    do {
      // parameter: bindingIdentifier typeAnnotation? initializer?
      ZC_IF_SOME(param, parseBindingElement()) { parameters.add(zc::mv(param)); }
      else { return zc::Vector<zc::Own<ast::BindingElement>>(); }
    } while (consumeExpectedToken(ast::SyntaxKind::Comma));

    if (!consumeExpectedToken(ast::SyntaxKind::RightParen)) {
      return zc::Vector<zc::Own<ast::BindingElement>>();
    }
  }

  return parameters;
}

zc::Vector<zc::Own<ast::CaptureElement>> Parser::parseCaptureClause() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseCaptureClause");
  zc::Vector<zc::Own<ast::CaptureElement>> captures;

  if (currentToken().getKind() == ast::SyntaxKind::Identifier &&
      currentToken().getText(getSourceManager()) == "use"_zc) {
    nextToken();  // consume 'use'

    if (consumeExpectedToken(ast::SyntaxKind::LeftBracket)) {
      if (currentToken().getKind() != ast::SyntaxKind::RightBracket) {
        do {
          ZC_IF_SOME(capture, parseCaptureElement()) { captures.add(zc::mv(capture)); }
          else {
            // Error recovery
            break;
          }
        } while (consumeExpectedToken(ast::SyntaxKind::Comma));
      }
      consumeExpectedToken(ast::SyntaxKind::RightBracket);
    }
  }
  return captures;
}

zc::Maybe<zc::Own<ast::CaptureElement>> Parser::parseCaptureElement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseCaptureElement");
  bool isByReference = false;
  source::SourceLoc startLoc = currentToken().getLocation();

  if (currentToken().getKind() == ast::SyntaxKind::Ampersand) {
    isByReference = true;
    nextToken();
  }

  if (currentToken().getKind() == ast::SyntaxKind::ThisKeyword) {
    nextToken();
    return finishNode(ast::factory::createCaptureElement(isByReference, zc::none, true), startLoc);
  } else if (currentToken().getKind() == ast::SyntaxKind::Identifier) {
    auto text = currentToken().getText(getSourceManager());
    auto id = ast::factory::createIdentifier(text);
    nextToken();
    return finishNode(ast::factory::createCaptureElement(isByReference, zc::mv(id), false),
                      startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::ReturnTypeNode>> Parser::parseReturnType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseReturnType");

  // Parse optional return type or error return clause
  //
  // callSignature:
  //   typeParameters? parameterClause (ARROW type raisesClause?)?;

  const lexer::Token token = currentToken();
  const source::SourceLoc startLoc = token.getLocation();

  if (!consumeExpectedToken(ast::SyntaxKind::Arrow)) { return zc::none; }

  ZC_IF_SOME(type, parseType()) {
    zc::Maybe<zc::Own<ast::TypeNode>> errorType = zc::none;
    if (consumeExpectedToken(ast::SyntaxKind::RaisesKeyword)) { errorType = parseType(); }
    return finishNode(ast::factory::createReturnType(zc::mv(type), zc::mv(errorType)), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::VariableStatement>> Parser::parseVariableStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseVariableStatement");

  // variableStatement: variableDeclarationList ';'
  // This handles variable declarations like: let x = 5; or const y: i32 = 10;

  source::SourceLoc startLoc = currentToken().getLocation();

  ZC_IF_SOME(declarationList, parseVariableDeclarationList()) {
    parseSemicolon();
    return finishNode(ast::factory::createVariableStatement(zc::mv(declarationList)), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::PropertyAccessExpression>> Parser::parsePropertyAccessExpressionRest(
    zc::Own<ast::LeftHandSideExpression> expression, bool questionDot, source::SourceLoc pos) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser,
                                 "parsePropertyAccessExpressionRest");

  // Parse the property name (identifier)
  ZC_IF_SOME(identifier, parseIdentifier()) {
    return finishNode(ast::factory::createPropertyAccessExpression(zc::mv(expression),
                                                                   zc::mv(identifier), questionDot),
                      pos);
  }

  // If we can't parse an identifier, create a missing identifier placeholder
  auto missingIdentifier = ast::factory::createMissingIdentifier();
  return finishNode(ast::factory::createPropertyAccessExpression(
                        zc::mv(expression), zc::mv(missingIdentifier), questionDot),
                    pos);
}

zc::Maybe<zc::Own<ast::ElementAccessExpression>> Parser::parseElementAccessExpressionRest(
    zc::Own<ast::LeftHandSideExpression> expression, bool questionDot, source::SourceLoc pos) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseElementAccessExpression");

  zc::Maybe<zc::Own<ast::Expression>> assignmentExpression;
  if (expectToken(ast::SyntaxKind::RightBracket)) {
    // Error: expected expression inside brackets
    assignmentExpression = ast::factory::createMissingIdentifier();
  }

  // Parse the index expression inside brackets
  ZC_IF_SOME(indexExpression, parseExpression()) {
    return finishNode(ast::factory::createElementAccessExpression(
                          zc::mv(expression), zc::mv(indexExpression), questionDot),
                      pos);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::TokenNode>> Parser::parseExpectedToken(ast::SyntaxKind kind) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseExpectedToken");

  // parseExpectedToken: consume a token of the expected kind and create a TokenNode
  // This is used when we need to parse a specific token and create an AST node for it

  if (!expectToken(kind)) {
    // Error: expected token of kind 'kind' but found something else
    return zc::none;
  }

  source::SourceLoc startLoc = currentToken().getLocation();
  nextToken();

  return finishNode(ast::factory::createTokenNode(kind), startLoc);
}

bool Parser::parseOptional(ast::SyntaxKind kind) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseOptional");

  // parseOptional: consume a token of the expected kind if it exists
  // This is used when we want to parse a token if it exists, but don't care about the result

  if (!expectToken(kind)) { return false; }

  nextToken();
  return true;
}

bool Parser::parseExpected(ast::SyntaxKind kind, bool shouldAdvance) {
  if (!expectToken(kind)) {
    ZC_IF_SOME(text, lexer::Token::getStaticTextForTokenKind(kind)) {
      parseErrorAtCurrentToken<diagnostics::DiagID::ExpectedToken>(zc::str(text));
    }
    else { parseErrorAtCurrentToken<diagnostics::DiagID::ExpectedToken>(zc::str(""_zc)); }
    return false;
  }
  if (shouldAdvance) { nextToken(); }
  return true;
}

zc::Maybe<zc::Own<ast::ClassElement>> Parser::parseClassElement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseClassElement");

  if (expectToken(ast::SyntaxKind::Semicolon)) {
    source::SourceLoc startLoc = currentToken().getLocation();
    nextToken();
    return finishNode(ast::factory::createSemicolonClassElement(), startLoc);
  }

  zc::Vector<ast::SyntaxKind> modifiers = parseModifiers(/*allowDecorators*/ true,
                                                         /*permitConstAsModifier*/ true,
                                                         /*stopOnStartOfClassStaticBlock*/ true);

  ZC_IF_SOME(prop, parsePropertyDeclaration()) { return zc::mv(prop); }
  return zc::none;
}

zc::Vector<ast::SyntaxKind> Parser::parseModifiers(bool allowDecorators, bool permitConstAsModifier,
                                                   bool stopOnStartOfClassStaticBlock) {
  zc::Vector<ast::SyntaxKind> result;
  while (isModifier()) {
    if (stopOnStartOfClassStaticBlock && expectToken(ast::SyntaxKind::StaticKeyword)) {
      // Check if next token is LeftBrace by trying to parse it
      ParserState state = mark();
      nextToken();  // Move past static
      if (currentToken().is(ast::SyntaxKind::LeftBrace)) {
        rewind(state);  // Restore if it's a static block
        break;
      }
      rewind(state);  // Restore and continue parsing normally
    }
    result.add(currentToken().getKind());
    nextToken();
  }
  return result;
}

zc::Maybe<zc::Own<ast::ClassElement>> Parser::parsePropertyDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parsePropertyDeclaration");

  const source::SourceLoc startLoc = currentToken().getLocation();
  if (!isLiteralPropertyName()) { return zc::none; }

  ZC_IF_SOME(name, parseIdentifierName()) {
    zc::Maybe<zc::Own<ast::TypeNode>> type = parseTypeAnnotation();
    zc::Maybe<zc::Own<ast::Expression>> init = parseInitializer();

    if (!consumeExpectedToken(ast::SyntaxKind::Semicolon)) {
      const lexer::Token& next = currentToken();
      impl->diagnosticEngine.diagnose<diagnostics::DiagID::MissingSemicolon>(
          next.getLocation(), next.getText(getSourceManager()));
    }

    auto propDecl =
        ast::factory::createPropertyDeclaration(zc::mv(name), zc::mv(type), zc::mv(init));
    return finishNode(zc::mv(propDecl), startLoc);
  }

  return zc::none;
}

zc::Vector<zc::Own<ast::ClassElement>> Parser::parseClassMembers() {
  return parseList<ast::ClassElement>(ParsingContext::ClassMembers,
                                      ZC_BIND_METHOD(*this, parseClassElement));
}

zc::Vector<zc::Own<ast::InterfaceElement>> Parser::parseInterfaceMembers() {
  zc::Vector<zc::Own<ast::InterfaceElement>> members;
  if (parseExpected(ast::SyntaxKind::LeftBrace)) {
    members = parseList<ast::InterfaceElement>(ParsingContext::InterfaceMembers,
                                               ZC_BIND_METHOD(*this, parseInterfaceElement));
    parseExpected(ast::SyntaxKind::RightBrace);
  }

  return members;
}

zc::Maybe<zc::Own<ast::InterfaceElement>> Parser::parseInterfaceElement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseInterfaceElement");

  const source::SourceLoc startLoc = currentToken().getLocation();

  if (!isLiteralPropertyName()) { return zc::none; }

  ZC_IF_SOME(name, parseIdentifierName()) {
    parseOptional(ast::SyntaxKind::Question);

    // methodSignature: propertyName QUESTION? callSignature
    if (expectToken(ast::SyntaxKind::LeftParen) || expectToken(ast::SyntaxKind::LessThan)) {
      zc::Vector<zc::Own<ast::TypeParameterDeclaration>> typeParameters = parseTypeParameters();
      zc::Vector<zc::Own<ast::BindingElement>> parameters = parseParameters();
      zc::Maybe<zc::Own<ast::ReturnTypeNode>> returnType = parseReturnType();

      auto fn = ast::factory::createMethodSignature(zc::mv(name), /*optional*/ false,
                                                    zc::mv(typeParameters), zc::mv(parameters),
                                                    zc::mv(returnType));
      parseOptional(ast::SyntaxKind::Semicolon);
      return finishNode(zc::mv(fn), startLoc);
    }

    // propertySignature: propertyName QUESTION? typeAnnotation
    zc::Maybe<zc::Own<ast::TypeNode>> type = parseTypeAnnotation();
    auto prop =
        ast::factory::createPropertySignature(zc::mv(name), /*optional*/ false, zc::mv(type));
    parseOptional(ast::SyntaxKind::Semicolon);
    return finishNode(zc::mv(prop), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Vector<zc::Own<ast::HeritageClause>>> Parser::parseHeritageClauses() {
  if (isHeritageClause()) {
    auto clauses = parseList<ast::HeritageClause>(ParsingContext::HeritageClauses,
                                                  ZC_BIND_METHOD(*this, parseHeritageClause));
    return zc::mv(clauses);
  }
  return zc::none;
}

zc::Maybe<zc::Own<ast::HeritageClause>> Parser::parseHeritageClause() {
  ast::SyntaxKind token = currentToken().getKind();
  if (token != ast::SyntaxKind::ExtendsKeyword && token != ast::SyntaxKind::ImplementsKeyword) {
    return zc::none;
  }
  source::SourceLoc startLoc = currentToken().getLocation();
  nextToken();

  zc::Vector<zc::Own<ast::ExpressionWithTypeArguments>> types;
  while (true) {
    ZC_IF_SOME(ewt, parseExpressionWithTypeArguments()) { types.add(zc::mv(ewt)); }
    else { break; }

    if (expectToken(ast::SyntaxKind::Comma)) {
      nextToken();
    } else {
      break;
    }
  }

  return finishNode(ast::factory::createHeritageClause(token, zc::mv(types)), startLoc);
}

bool Parser::scanStartOfDeclaration() {
  while (true) {
    const lexer::Token& token = currentToken();
    switch (token.getKind()) {
      case ast::SyntaxKind::LetKeyword:
      case ast::SyntaxKind::ConstKeyword:
      case ast::SyntaxKind::FunKeyword:
      case ast::SyntaxKind::ClassKeyword:
      case ast::SyntaxKind::EnumKeyword:
        return true;

      case ast::SyntaxKind::UsingKeyword:
        return isUsingDeclaration();

      case ast::SyntaxKind::AwaitKeyword:
        return isAwaitUsingDeclaration();

      case ast::SyntaxKind::InterfaceKeyword:
      case ast::SyntaxKind::TypeKeyword:
        return nextTokenIsIdentifierOnSameLine();

      case ast::SyntaxKind::ModuleKeyword:
      case ast::SyntaxKind::NamespaceKeyword:
        return nextTokenIsIdentifierOrStringLiteralOnSameLine();

      case ast::SyntaxKind::AbstractKeyword:
      case ast::SyntaxKind::AccessorKeyword:
      case ast::SyntaxKind::AsyncKeyword:
      case ast::SyntaxKind::DeclareKeyword:
      case ast::SyntaxKind::PrivateKeyword:
      case ast::SyntaxKind::ProtectedKeyword:
      case ast::SyntaxKind::PublicKeyword:
      case ast::SyntaxKind::ReadonlyKeyword:
      case ast::SyntaxKind::OverrideKeyword: {
        ast::SyntaxKind previousTokenKind = token.getKind();
        nextToken();
        // ASI takes effect for this modifier.
        if (currentToken().hasPrecedingLineBreak()) { return false; }
        if (previousTokenKind == ast::SyntaxKind::DeclareKeyword &&
            currentToken().is(ast::SyntaxKind::TypeKeyword)) {
          return true;
        }
        continue;
      }

      case ast::SyntaxKind::GlobalKeyword:
        nextToken();
        return currentToken().is(ast::SyntaxKind::LeftBrace) ||
               currentToken().is(ast::SyntaxKind::Identifier) ||
               currentToken().is(ast::SyntaxKind::ExportKeyword);

      case ast::SyntaxKind::ImportKeyword:
        nextToken();
        return currentToken().is(ast::SyntaxKind::StringLiteral) ||
               currentToken().is(ast::SyntaxKind::Asterisk) ||
               currentToken().is(ast::SyntaxKind::LeftBrace) ||
               tokenIsIdentifierOrKeyword(currentToken());

      case ast::SyntaxKind::ExportKeyword:
        nextToken();
        if (currentToken().is(ast::SyntaxKind::Equals) ||
            currentToken().is(ast::SyntaxKind::Asterisk) ||
            currentToken().is(ast::SyntaxKind::LeftBrace) ||
            currentToken().is(ast::SyntaxKind::DefaultKeyword) ||
            currentToken().is(ast::SyntaxKind::AsKeyword) ||
            currentToken().is(ast::SyntaxKind::At)) {
          return true;
        }
        if (currentToken().is(ast::SyntaxKind::TypeKeyword)) {
          nextToken();
          return currentToken().is(ast::SyntaxKind::Asterisk) ||
                 currentToken().is(ast::SyntaxKind::LeftBrace) ||
                 (isIdentifier() && !currentToken().hasPrecedingLineBreak());
        }
        continue;

      case ast::SyntaxKind::StaticKeyword:
        nextToken();
        continue;

      default:
        return false;
    }
  }
}

bool Parser::nextTokenIsIdentifierOrKeywordOnSameLine() {
  nextToken();
  if (currentToken().hasPrecedingLineBreak()) return false;
  return isIdentifierOrKeyword();
}

bool Parser::nextTokenIsIdentifierOnSameLine() {
  nextToken();
  if (currentToken().hasPrecedingLineBreak()) return false;
  return isIdentifier();
}

bool Parser::nextTokenIsIdentifierOrStringLiteralOnSameLine() {
  nextToken();
  if (currentToken().hasPrecedingLineBreak()) return false;
  return isIdentifier() || currentToken().is(ast::SyntaxKind::StringLiteral);
}

bool Parser::tokenIsIdentifierOrKeyword(const lexer::Token& token) const {
  return token.is(ast::SyntaxKind::Identifier) ||
         (token.getKind() >= ast::SyntaxKind::FirstKeyword &&
          token.getKind() <= ast::SyntaxKind::LastKeyword);
}

bool Parser::nextTokenIsEqualsOrSemicolonOrColonToken() {
  nextToken();
  const lexer::Token& token = currentToken();
  return token.is(ast::SyntaxKind::Equals) || token.is(ast::SyntaxKind::Semicolon) ||
         token.is(ast::SyntaxKind::Colon);
}

bool Parser::nextTokenIsBindingIdentifierOrStartOfDestructuringOnSameLine(bool disallowOf) {
  nextToken();
  const lexer::Token& token = currentToken();
  if (disallowOf && token.is(ast::SyntaxKind::OfKeyword)) {
    return lookAhead<bool>(ZC_BIND_METHOD(*this, nextTokenIsEqualsOrSemicolonOrColonToken));
  }
  return isBindingIdentifier() ||
         (token.is(ast::SyntaxKind::LeftBrace) && !token.hasPrecedingLineBreak());
}

bool Parser::nextIsUsingKeywordThenBindingIdentifierOrStartOfObjectDestructuringOnSameLine() {
  nextToken();
  if (!currentToken().is(ast::SyntaxKind::UsingKeyword)) { return false; }
  return nextTokenIsBindingIdentifierOrStartOfDestructuringOnSameLine(false);
}

bool Parser::isUsingDeclaration() {
  return lookAhead<bool>([this]() {
    return nextTokenIsBindingIdentifierOrStartOfDestructuringOnSameLine(/*disallowOf=*/false);
  });
}

bool Parser::isAwaitUsingDeclaration() {
  return lookAhead<bool>(ZC_BIND_METHOD(
      *this, nextIsUsingKeywordThenBindingIdentifierOrStartOfObjectDestructuringOnSameLine));
}

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
