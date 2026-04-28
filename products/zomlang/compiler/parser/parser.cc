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
#include "zc/core/one-of.h"
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
    case ast::SyntaxKind::AsKeyword:
      return ast::OperatorPrecedence::kRelational;
    case ast::SyntaxKind::LessThanLessThan:
    case ast::SyntaxKind::GreaterThanGreaterThan:
    case ast::SyntaxKind::GreaterThanGreaterThanGreaterThan:
      return ast::OperatorPrecedence::kShift;
    case ast::SyntaxKind::QuestionQuestion:
      return ast::OperatorPrecedence::kLogicalOr;
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

zc::Maybe<const ast::Expression&> findOptionalChainBoundary(const ast::Expression& expression) {
  if (ast::hasFlag(expression.getFlags(), ast::NodeFlags::OptionalChain)) { return expression; }
  if (!ast::isa<ast::NonNullExpression>(expression)) { return zc::none; }
  return findOptionalChainBoundary(ast::cast<ast::NonNullExpression>(expression).getExpression());
}

void markNonNullOptionalChain(ast::Expression& expression) {
  if (!ast::isa<ast::NonNullExpression>(expression)) { return; }

  expression.setFlags(expression.getFlags() | ast::NodeFlags::OptionalChain);
  auto& next =
      const_cast<ast::Expression&>(ast::cast<ast::NonNullExpression>(expression).getExpression());
  markNonNullOptionalChain(next);
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
    case ParsingContext::StructMembers:
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
    case ParsingContext::ArrayBindingElements:
      return token.is(ast::SyntaxKind::RightBracket);
    case ParsingContext::TupleElementTypes:
      return token.is(ast::SyntaxKind::RightParen);
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
      return lookAhead<bool>(ZC_BIND_METHOD(*this, isStartOfInterfaceMember)) ||
             (expectToken(ast::SyntaxKind::Semicolon) && !inErrorRecovery);
    case ParsingContext::ClassMembers:
      return lookAhead<bool>(ZC_BIND_METHOD(*this, isStartOfClassMember)) ||
             (expectToken(ast::SyntaxKind::Semicolon) && !inErrorRecovery);
    case ParsingContext::StructMembers:
      return lookAhead<bool>(ZC_BIND_METHOD(*this, isStartOfStructMember)) ||
             (expectToken(ast::SyntaxKind::Semicolon) && !inErrorRecovery);
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
    case ParsingContext::StructMembers:
      parseErrorAtCurrentToken<diagnostics::DiagID::UnexpectedTokenStructMemberExpected>();
      break;
    case ParsingContext::EnumMembers:
      parseErrorAtCurrentToken<diagnostics::DiagID::EnumMemberExpected>();
      break;
    case ParsingContext::HeritageClauseElement:
      parseErrorAtCurrentToken<diagnostics::DiagID::ExpressionExpected>();
      break;
    case ParsingContext::VariableDeclarations: {
      const auto kind = currentKind();
      if (kind >= ast::SyntaxKind::FirstKeyword && kind <= ast::SyntaxKind::LastKeyword) {
        parseErrorAtCurrentToken<diagnostics::DiagID::VariableDeclarationNameNotAllowed>(
            currentToken().getValue());
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
      const auto kind = currentKind();
      if (kind >= ast::SyntaxKind::FirstKeyword && kind <= ast::SyntaxKind::LastKeyword) {
        parseErrorAtCurrentToken<diagnostics::DiagID::ParameterNameNotAllowed>(
            currentToken().getValue());
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
  const source::SourceLoc loc = token.getLocation();

  if (!token.is(ast::SyntaxKind::TypeOfKeyword)) { return zc::none; }

  nextToken();  // consume 'typeof'

  ZC_IF_SOME(queryExpr, parseTypeQueryExpression()) {
    return finishNode(ast::factory::createTypeQuery(zc::mv(queryExpr)), loc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseTypeQueryExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseTypeQueryExpression");

  // Parse type query expression according to the grammar rule:
  // typeQueryExpression: identifier (PERIOD identifier)*
  // This handles expressions like 'MyClass' or 'MyClass.field' in typeof queries

  const source::SourceLoc loc = currentLoc();

  zc::Own<ast::LeftHandSideExpression> result = parseIdentifierName();

  // Parse additional identifiers separated by periods
  while (expectToken(ast::SyntaxKind::Period)) {
    nextToken();  // consume '.'

    auto nextId = parseIdentifierName();
    auto propAccess = finishNode(
        ast::factory::createPropertyAccessExpression(zc::mv(result), zc::mv(nextId), false), loc);
    result = zc::mv(propAccess);
  }

  return finishNode(zc::mv(result), loc);
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

  // sourceFile: moduleDeclaration? moduleBody?;

  // Prim lexer to get the first token
  nextToken();

  source::SourceLoc loc = currentLoc();

  // Parse Module Declaration
  auto moduleDeclaration = parseModuleDeclaration(/*isStartOfSourceFile*/ true);
  // Parse Module Body
  auto statements = parseList<ast::Statement>(ParsingContext::SourceElements,
                                              ZC_BIND_METHOD(*this, parseStatement));

  trace::traceCounter(trace::TraceCategory::kParser, "Module items parsed"_zc,
                      zc::str(statements.size()));

  // Create the source file node
  zc::StringPtr fileName = getSourceManager().getIdentifierForBuffer(impl->bufferId);
  zc::Own<ast::SourceFile> sourceFile =
      finishNode(ast::factory::createSourceFile(zc::str(fileName), zc::mv(moduleDeclaration),
                                                zc::mv(statements)),
                 loc);

  trace::traceEvent(trace::TraceCategory::kParser, "Source file created"_zc, fileName);
  return finishNode(zc::mv(sourceFile), loc);
}

zc::Maybe<zc::Own<ast::ModuleDeclaration>> Parser::parseModuleDeclaration(
    bool isStartOfSourceFile) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseModuleDeclaration");

  const lexer::Token& token = currentToken();

  if (!token.is(ast::SyntaxKind::ModuleKeyword)) { return zc::none; }

  if (!isStartOfSourceFile) {
    parseErrorAtCurrentToken<diagnostics::DiagID::ModuleDeclarationMustBeFirst>();
  }

  source::SourceLoc loc = token.getLocation();
  nextToken();

  ZC_IF_SOME(modulePath, parseModulePath()) {
    parseSemicolon();
    return finishNode(ast::factory::createModuleDeclaration(zc::mv(modulePath)), loc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::ImportDeclaration>> Parser::parseImportDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseImportDeclaration");
  const lexer::Token& token = currentToken();
  if (!token.is(ast::SyntaxKind::ImportKeyword)) { return zc::none; }

  source::SourceLoc loc = token.getLocation();
  nextToken();

  ZC_IF_SOME(modulePath, parseModulePath()) {
    zc::Maybe<zc::Own<ast::Identifier>> alias = zc::none;
    zc::Vector<zc::Own<ast::ImportSpecifier>> specifiers;

    if (currentToken().is(ast::SyntaxKind::AsKeyword)) {
      nextToken();
      if (!tokenIsIdentifierOrKeyword(currentToken())) { return zc::none; }
      alias = parseIdentifierName();
    } else if (currentToken().is(ast::SyntaxKind::Period) &&
               lookAhead(1).is(ast::SyntaxKind::LeftBrace)) {
      nextToken();
      parseExpected(ast::SyntaxKind::LeftBrace);

      while (!expectToken(ast::SyntaxKind::RightBrace) &&
             !expectToken(ast::SyntaxKind::EndOfFile)) {
        ZC_IF_SOME(specifier, parseImportSpecifier()) { specifiers.add(zc::mv(specifier)); }
        if (!parseOptional(ast::SyntaxKind::Comma)) { break; }
      }

      parseExpected(ast::SyntaxKind::RightBrace);
    }

    parseSemicolon();
    return finishNode(ast::factory::createImportDeclaration(zc::mv(modulePath), zc::mv(alias),
                                                            zc::mv(specifiers)),
                      loc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::ModulePath>> Parser::parseModulePath() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseModulePath");
  if (!tokenIsIdentifierOrKeyword(currentToken())) { return zc::none; }

  source::SourceLoc loc = currentLoc();
  zc::Vector<zc::Own<ast::Identifier>> segments;
  segments.add(parseIdentifierName());

  while (currentToken().is(ast::SyntaxKind::Period) && tokenIsIdentifierOrKeyword(lookAhead(1))) {
    nextToken();
    segments.add(parseIdentifierName());
  }

  return finishNode(ast::factory::createModulePath(zc::mv(segments)), loc);
}

zc::Maybe<zc::Own<ast::ImportSpecifier>> Parser::parseImportSpecifier() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseImportSpecifier");
  if (!tokenIsIdentifierOrKeyword(currentToken())) { return zc::none; }

  source::SourceLoc loc = currentLoc();
  auto importedName = parseIdentifierName();
  zc::Maybe<zc::Own<ast::Identifier>> alias = zc::none;

  if (currentToken().is(ast::SyntaxKind::AsKeyword)) {
    nextToken();
    if (!tokenIsIdentifierOrKeyword(currentToken())) { return zc::none; }
    alias = parseIdentifierName();
  }

  return finishNode(ast::factory::createImportSpecifier(zc::mv(importedName), zc::mv(alias)), loc);
}

zc::Maybe<zc::Own<ast::ExportSpecifier>> Parser::parseExportSpecifier() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseExportSpecifier");
  if (!tokenIsIdentifierOrKeyword(currentToken())) { return zc::none; }

  source::SourceLoc loc = currentLoc();
  auto exportedName = parseIdentifierName();
  zc::Maybe<zc::Own<ast::Identifier>> alias = zc::none;

  if (currentToken().is(ast::SyntaxKind::AsKeyword)) {
    nextToken();
    if (!tokenIsIdentifierOrKeyword(currentToken())) { return zc::none; }
    alias = parseIdentifierName();
  }

  return finishNode(ast::factory::createExportSpecifier(zc::mv(exportedName), zc::mv(alias)), loc);
}

zc::Maybe<zc::Own<ast::ExportDeclaration>> Parser::parseExportDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseExportDeclaration");
  const lexer::Token& token = currentToken();
  if (!token.is(ast::SyntaxKind::ExportKeyword)) { return zc::none; }

  source::SourceLoc loc = token.getLocation();
  nextToken();

  if (isStartOfDeclaration()) {
    ZC_IF_SOME(declaration, parseDeclaration()) {
      return finishNode(ast::factory::createExportDeclaration(
                            zc::none, zc::Vector<zc::Own<ast::ExportSpecifier>>(),
                            zc::Maybe<zc::Own<ast::Statement>>(zc::mv(declaration))),
                        loc);
    }
  }

  zc::Maybe<zc::Own<ast::ModulePath>> modulePath = zc::none;
  zc::Vector<zc::Own<ast::ExportSpecifier>> specifiers;

  if (currentToken().is(ast::SyntaxKind::LeftBrace)) {
    nextToken();

    while (!expectToken(ast::SyntaxKind::RightBrace) && !expectToken(ast::SyntaxKind::EndOfFile)) {
      ZC_IF_SOME(specifier, parseExportSpecifier()) { specifiers.add(zc::mv(specifier)); }
      if (!parseOptional(ast::SyntaxKind::Comma)) { break; }
    }

    parseExpected(ast::SyntaxKind::RightBrace);
  } else {
    ZC_IF_SOME(parsedModulePath, parseModulePath()) {
      modulePath = zc::mv(parsedModulePath);
      parseExpected(ast::SyntaxKind::Period);
      parseExpected(ast::SyntaxKind::LeftBrace);

      while (!expectToken(ast::SyntaxKind::RightBrace) &&
             !expectToken(ast::SyntaxKind::EndOfFile)) {
        ZC_IF_SOME(specifier, parseExportSpecifier()) { specifiers.add(zc::mv(specifier)); }
        if (!parseOptional(ast::SyntaxKind::Comma)) { break; }
      }

      parseExpected(ast::SyntaxKind::RightBrace);
    }
  }

  parseSemicolon();
  return finishNode(
      ast::factory::createExportDeclaration(zc::mv(modulePath), zc::mv(specifiers), zc::none), loc);
}

zc::Maybe<zc::Own<ast::Statement>> Parser::parseStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseStatement");

  // Check for labeled statement
  if (currentToken().is(ast::SyntaxKind::Identifier) && isLookAhead(1, ast::SyntaxKind::Colon)) {
    return parseLabeledStatement();
  }

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
  switch (currentKind()) {
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
    case ast::SyntaxKind::ImportKeyword:
    case ast::SyntaxKind::ExportKeyword:
    case ast::SyntaxKind::ModuleKeyword:
      if (isStartOfDeclaration()) { return parseDeclaration(); }
      break;
    default:
      break;
  }
  // Try to parse as expression statement
  return parseExpressionStatement();
}

bool Parser::isStartOfStatement() {
  const lexer::Token& token = currentToken();

  switch (token.getKind()) {
    // Punctuation that can start statements
    case ast::SyntaxKind::At:         // @decorator
    case ast::SyntaxKind::Semicolon:  // empty statement
    case ast::SyntaxKind::LeftBrace:  // block statement
    // Keywords that start statements
    case ast::SyntaxKind::LetKeyword:        // let declaration
    case ast::SyntaxKind::ConstKeyword:      // const declaration
    case ast::SyntaxKind::FunKeyword:        // function declaration
    case ast::SyntaxKind::ClassKeyword:      // class declaration
    case ast::SyntaxKind::StructKeyword:     // struct declaration
    case ast::SyntaxKind::InterfaceKeyword:  // interface declaration
    case ast::SyntaxKind::EnumKeyword:       // enum declaration
    case ast::SyntaxKind::AliasKeyword:      // alias declaration
    case ast::SyntaxKind::IfKeyword:         // if statement
    case ast::SyntaxKind::DoKeyword:         // do statement
    case ast::SyntaxKind::WhileKeyword:      // while statement
    case ast::SyntaxKind::ForKeyword:        // for statement
    case ast::SyntaxKind::ContinueKeyword:   // continue statement
    case ast::SyntaxKind::BreakKeyword:      // break statement
    case ast::SyntaxKind::ReturnKeyword:     // return statement
    case ast::SyntaxKind::WithKeyword:       // with statement
    case ast::SyntaxKind::SwitchKeyword:     // switch statement
    case ast::SyntaxKind::MatchKeyword:      // match statement
    case ast::SyntaxKind::ThrowKeyword:      // throw statement
    case ast::SyntaxKind::TryKeyword:        // try statement
    case ast::SyntaxKind::DebuggerKeyword:   // debugger statement
      return true;

    // Keywords that might start statements depending on context
    case ast::SyntaxKind::ImportKeyword:
    case ast::SyntaxKind::ExportKeyword:
      return isStartOfDeclaration();

    // Access modifiers and other contextual keywords
    case ast::SyntaxKind::AsyncKeyword:
    case ast::SyntaxKind::DeclareKeyword:
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

ZC_ALWAYS_INLINE(ast::SyntaxKind Parser::currentKind() const) { return currentToken().getKind(); }

ast::SyntaxKind Parser::reScanGreaterToken() {
  ast::SyntaxKind kind = impl->lexer.reScanGreaterToken();
  impl->token = impl->lexer.getCurrentState().token;
  return kind;
}

ZC_ALWAYS_INLINE(void Parser::nextToken()) { impl->nextToken(); }

ZC_ALWAYS_INLINE(bool Parser::expectToken(ast::SyntaxKind kind)) {
  const lexer::Token& token = currentToken();
  return token.is(kind);
}

// Returns true if the current token matches any of the provided kinds.
template <typename... Kinds>
ZC_ALWAYS_INLINE(bool Parser::expectNToken(ast::SyntaxKind kind0, ast::SyntaxKind kind1,
                                           Kinds... kinds)) {
  const lexer::Token& token = currentToken();
  return token.is(kind0) || token.is(kind1) || (... || token.is(kinds));
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
  switch (currentKind()) {
    case ast::SyntaxKind::Plus:
    case ast::SyntaxKind::Minus:
    case ast::SyntaxKind::Tilde:
    case ast::SyntaxKind::Exclamation:
    case ast::SyntaxKind::DeleteKeyword:
    case ast::SyntaxKind::TypeOfKeyword:
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

bool Parser::isLiteralPropertyName() {
  // Token is Identifier or Keyword
  return expectNToken(ast::SyntaxKind::Identifier, ast::SyntaxKind::StringLiteral,
                      ast::SyntaxKind::IntegerLiteral, ast::SyntaxKind::FloatLiteral,
                      ast::SyntaxKind::BigIntLiteral) ||
         (currentKind() >= ast::SyntaxKind::FirstKeyword &&
          currentKind() <= ast::SyntaxKind::LastKeyword);
}

bool Parser::isBindingIdentifierOrPattern() const {
  const lexer::Token& token = currentToken();
  return token.is(ast::SyntaxKind::LeftBrace) || token.is(ast::SyntaxKind::LeftBracket) ||
         isBindingIdentifier();
}

bool Parser::isModifier() const {
  const lexer::Token& token = currentToken();
  return token.is(ast::SyntaxKind::AbstractKeyword) || token.is(ast::SyntaxKind::ExportKeyword) ||
         token.is(ast::SyntaxKind::PublicKeyword) || token.is(ast::SyntaxKind::PrivateKeyword) ||
         token.is(ast::SyntaxKind::ProtectedKeyword) || token.is(ast::SyntaxKind::StaticKeyword) ||
         token.is(ast::SyntaxKind::ReadonlyKeyword) || token.is(ast::SyntaxKind::MutatingKeyword) ||
         token.is(ast::SyntaxKind::AsyncKeyword) || token.is(ast::SyntaxKind::OverrideKeyword);
}

bool Parser::isBinaryOperator() const {
  return getBinaryOperatorPrecedence(currentKind()) > ast::OperatorPrecedence::kLowest;
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

bool Parser::isStartOfFunctionType() {
  if (expectToken(ast::SyntaxKind::LessThan)) { return true; }
  return expectToken(ast::SyntaxKind::LeftParen) &&
         lookAhead<bool>(ZC_BIND_METHOD(*this, isUnambiguouslyStartOfFunctionType));
}

bool Parser::skipFunctionTypeParameterStart() {
  if (isModifier()) {
    // Skip modifiers
    parseModifiers(/*allowDecorators*/ false);
  }

  if (isIdentifier() || expectToken(ast::SyntaxKind::ThisKeyword)) {
    nextToken();
    return true;
  }

  if (expectNToken(ast::SyntaxKind::LeftBracket, ast::SyntaxKind::LeftBrace)) {
    // Return true if we can parse an array or object binding pattern with no errors
    const bool hadErrors = getDiagnosticEngine().hasErrors();
    parseIdentifierOrBindingPattern();
    return hadErrors == getDiagnosticEngine().hasErrors();
  }
  return false;
}

bool Parser::isUnambiguouslyStartOfFunctionType() {
  nextToken();

  if (expectNToken(ast::SyntaxKind::RightParen, ast::SyntaxKind::DotDotDot)) {
    // ( )
    // ( ...
    return true;
  }

  if (skipFunctionTypeParameterStart()) {
    if (expectToken(ast::SyntaxKind::Comma)) {
      // ( xxx ,  — could be function or named tuple, skip to closing paren
      // and check for ->
      while (!expectNToken(ast::SyntaxKind::RightParen, ast::SyntaxKind::EndOfFile)) {
        nextToken();
      }
      if (expectToken(ast::SyntaxKind::RightParen)) {
        nextToken();
        return expectToken(ast::SyntaxKind::Arrow);
      }
      return false;
    }

    if (expectNToken(ast::SyntaxKind::Question, ast::SyntaxKind::Equals)) {
      // ( xxx ?
      // ( xxx =
      return true;
    }

    if (expectToken(ast::SyntaxKind::Colon)) {
      // ( xxx : — could be a named tuple element or function parameter
      // Skip past the type to find ) and check for ->
      nextToken();  // consume ':'
      int depth = 0;
      while (!expectToken(ast::SyntaxKind::EndOfFile)) {
        if (expectNToken(ast::SyntaxKind::LeftParen, ast::SyntaxKind::LeftBracket,
                         ast::SyntaxKind::LeftBrace, ast::SyntaxKind::LessThan)) {
          depth++;
        } else if (expectToken(ast::SyntaxKind::RightParen) && depth == 0) {
          break;
        } else if (expectNToken(ast::SyntaxKind::RightParen, ast::SyntaxKind::RightBracket,
                                ast::SyntaxKind::RightBrace, ast::SyntaxKind::GreaterThan)) {
          if (depth > 0) { depth--; }
        } else if (expectToken(ast::SyntaxKind::Comma) && depth == 0) {
          // More parameters/elements follow — continue to closing paren
        }
        nextToken();
      }
      if (expectToken(ast::SyntaxKind::RightParen)) {
        nextToken();
        return expectToken(ast::SyntaxKind::Arrow);
      }
      return false;
    }

    if (expectToken(ast::SyntaxKind::RightParen)) {
      nextToken();
      // ( xxx ) ->
      return expectToken(ast::SyntaxKind::Arrow);
    }
  }

  return false;
}

bool Parser::nextTokenIsLeftParenOrLessThanOrDot() {
  nextToken();
  switch (currentKind()) {
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
  return expectNToken(ast::SyntaxKind::LeftParen, ast::SyntaxKind::LessThan);
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

bool Parser::isStartOfClassMember() {
  while (isModifier()) { nextToken(); }

  if (expectNToken(ast::SyntaxKind::GetKeyword, ast::SyntaxKind::SetKeyword)) { return true; }
  if (expectToken(ast::SyntaxKind::FunKeyword)) { return true; }
  if (expectNToken(ast::SyntaxKind::LetKeyword, ast::SyntaxKind::ConstKeyword)) { return true; }
  if (expectNToken(ast::SyntaxKind::InitKeyword, ast::SyntaxKind::DeinitKeyword)) { return true; }

  return false;
}

bool Parser::isStartOfStructMember() {
  while (isModifier()) { nextToken(); }

  if (expectNToken(ast::SyntaxKind::GetKeyword, ast::SyntaxKind::SetKeyword)) { return true; }
  if (expectToken(ast::SyntaxKind::FunKeyword)) { return true; }
  if (expectNToken(ast::SyntaxKind::LetKeyword, ast::SyntaxKind::ConstKeyword)) { return true; }

  return false;
}

bool Parser::isStartOfInterfaceMember() {
  // Return true if we have the start of a accessor member.
  if (expectNToken(ast::SyntaxKind::GetKeyword, ast::SyntaxKind::SetKeyword)) { return true; }

  // Eat up all modifiers, but hold on to the last one in case it is actually an identifier
  while (isModifier()) { nextToken(); }

  if (expectNToken(ast::SyntaxKind::FunKeyword, ast::SyntaxKind::LetKeyword,
                   ast::SyntaxKind::ConstKeyword)) {
    return true;
  }

  return false;
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
      auto arg = parseAssignmentExpressionOrHigher();
      arguments.add(zc::mv(arg));
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
  //
  // Mirrors the tryParse speculation pattern: diagnostics emitted during the speculative parse
  // must not leak to consumers when the parse fails (e.g. when `<` is actually a comparison
  // operator, not a type argument opener).

  if (!expectToken(ast::SyntaxKind::LessThan)) { return zc::none; }
  // Save current parser state
  ParserState state = mark();

  // Suppress diagnostics during speculative parsing.
  // On success the type arguments were parsed correctly, so no errors were emitted.
  // On failure the speculative diagnostics are discarded.
  impl->diagnosticEngine.suppress();

  if (expectToken(ast::SyntaxKind::LessThan)) {
    nextToken();

    // Parse the type argument list
    zc::Vector<zc::Own<ast::TypeNode>> typeArguments;

    if (!expectToken(ast::SyntaxKind::GreaterThan)) {
      do { typeArguments.add(parseType()); } while (consumeExpectedToken(ast::SyntaxKind::Comma));
    }

    // If it doesn't have the closing `>` then it's definitely not a type argument list.
    if (!consumeExpectedToken(ast::SyntaxKind::GreaterThan)) {
      impl->diagnosticEngine.unsuppress();
      rewind(state);
      return zc::none;
    }

    // We successfully parsed a type argument list. The next token determines whether we want to
    // treat it as such. If the type argument list is followed by `(` or a template literal, as in
    // `f<number>(42)`, we favor the type argument interpretation even though JavaScript would view
    // it as a relational expression.
    if (canFollowTypeArgumentsInExpression()) {
      impl->diagnosticEngine.unsuppress();
      return zc::mv(typeArguments);
    }
  }

  // Parsing failed or doesn't follow expected pattern, restore state
  impl->diagnosticEngine.unsuppress();
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
  const source::SourceLoc loc = currentLoc();
  auto expression = parseLeftHandSideExpressionOrHigher();
  if (ast::isa<ast::ExpressionWithTypeArguments>(*expression)) {
    auto ewt = ast::cast<ast::ExpressionWithTypeArguments>(zc::mv(expression));
    return finishNode(zc::mv(ewt), loc);
  }
  auto typeArguments = parseTypeArguments();
  auto ewt =
      ast::factory::createExpressionWithTypeArguments(zc::mv(expression), zc::mv(typeArguments));
  return finishNode(zc::mv(ewt), loc);
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

zc::Maybe<zc::Own<ast::BlockStatement>> Parser::parseFunctionBlockOrSemicolon() {
  if (!expectToken(ast::SyntaxKind::LeftBrace)) {
    if (canParseSemicolon()) {
      parseSemicolon();
      return zc::none;
    }
    parseErrorAtCurrentToken<diagnostics::DiagID::ExpectedToken>("{"_zc);
    return zc::none;
  }

  return parseBlockStatement();
}

void Parser::parseSemicolonAfterPropertyName(
    const zc::Own<ast::Identifier>& name, const zc::Maybe<zc::Own<ast::TypeNode>>& typeNode,
    const zc::Maybe<zc::Own<ast::Expression>>& initializer) {
  if (expectToken(ast::SyntaxKind::LeftParen)) {
    parseErrorAtCurrentToken<diagnostics::DiagID::CannotStartFunctionCallInTypeAnnotation>();
    nextToken();
    return;
  }

  if (typeNode != zc::none && !canParseSemicolon()) {
    if (initializer != zc::none) {
      parseErrorAtCurrentToken<diagnostics::DiagID::ExpectedToken>(";"_zc);
    } else {
      parseErrorAtCurrentToken<diagnostics::DiagID::ExpectedForPropertyInitializer>();
    }
    return;
  }

  if (tryParseSemicolon()) { return; }

  if (initializer != zc::none) {
    parseErrorAtCurrentToken<diagnostics::DiagID::ExpectedToken>(";"_zc);
    return;
  }

  parseErrorForMissingSemicolonAfter(*name);
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

zc::Own<ast::TokenNode> Parser::parseTokenNode() {
  const source::SourceLoc loc = currentLoc();
  const ast::SyntaxKind kind = currentKind();
  nextToken();
  return finishNode(ast::factory::createTokenNode(kind), loc);
}

zc::Own<ast::Identifier> Parser::createIdentifier(bool isIdentifier) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "createIdentifier");

  // bindingIdentifier: identifier
  // identifier: identifierName
  //   where identifierName must not be a reserved word

  if (isIdentifier) {
    const lexer::Token token = currentToken();
    zc::StringPtr identifier = currentToken().getValue();

    nextToken();
    return finishNode(ast::factory::createIdentifier(identifier), token.getLocation());
  }

  const lexer::Token token = currentToken();
  if (lexer::isReservedKeyword(token.getKind())) {
    parseErrorAtCurrentToken<diagnostics::DiagID::ReservedKeywordAsIdentifier>(token.getValue());
    nextToken();
  } else {
    parseErrorAtCurrentToken<diagnostics::DiagID::ExceptedIdentifier>(token.getValue());
  }

  return finishNode(ast::factory::createMissingIdentifier(), currentLoc());
}

zc::Own<ast::Identifier> Parser::parsePropertyName() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parsePropertyName");

  if (expectNToken(ast::SyntaxKind::StringLiteral, ast::SyntaxKind::IntegerLiteral,
                   ast::SyntaxKind::FloatLiteral, ast::SyntaxKind::BigIntLiteral)) {
    source::SourceLoc loc = currentLoc();
    parseErrorAtCurrentToken<diagnostics::DiagID::ExceptedIdentifier>(currentToken().getValue());
    nextToken();
    return finishNode(ast::factory::createMissingIdentifier(), loc);
  }

  return parseIdentifierName();
}

zc::Own<ast::Identifier> Parser::parseIdentifier() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseIdentifier");
  return createIdentifier(isIdentifier());
}

zc::Own<ast::Identifier> Parser::parseIdentifierName() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseIdentifierName");
  return createIdentifier(lexer::isIdentifierOrKeyword(currentKind()));
}

zc::Own<ast::Identifier> Parser::parseIdentifierNameErrorOnUnicodeEscapeSequence() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser,
                                 "parseIdentifierNameErrorOnUnicodeEscapeSequence");

  if (currentToken().hasFlag(lexer::TokenFlags::UnicodeEscape) ||
      currentToken().hasFlag(lexer::TokenFlags::ExtendedUnicodeEscape)) {
    parseErrorAtCurrentToken<diagnostics::DiagID::UnicodeEscapeSequenceCannotAppearHere>();
  }

  return createIdentifier(lexer::isIdentifierOrKeyword(currentKind()));
}

zc::Own<ast::Identifier> Parser::parseBindingIdentifier() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBindingIdentifier");
  return createIdentifier(isBindingIdentifier());
}

zc::Maybe<zc::Own<ast::BindingElement>> Parser::parseBindingElement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBindingElement");

  // bindingElement:
  //   bindingIdentifier initializer?
  //   | bindingPattern initializer?;

  const source::SourceLoc loc = currentLoc();

  zc::Own<ast::Identifier> name = ast::factory::createMissingIdentifier();
  zc::Maybe<zc::Own<ast::BindingPattern>> bindingPattern = zc::none;

  if (currentToken().is(ast::SyntaxKind::LeftBrace) ||
      currentToken().is(ast::SyntaxKind::LeftBracket)) {
    bindingPattern = parseBindingPattern();
    if (bindingPattern == zc::none) { return zc::none; }
  } else {
    name = parseBindingIdentifier();
  }
  // Optional initializer
  zc::Maybe<zc::Own<ast::Expression>> initializer = parseInitializer();

  zc::OneOf<zc::Own<ast::Identifier>, zc::Own<ast::BindingPattern>> nameOrPattern;
  ZC_IF_SOME(bp, bindingPattern) { nameOrPattern = zc::mv(bp); }
  else { nameOrPattern = zc::mv(name); }

  return finishNode(ast::factory::createBindingElement(zc::none, zc::none, zc::mv(nameOrPattern),
                                                       zc::mv(initializer)),
                    loc);
}

zc::Maybe<zc::Own<ast::VariableDeclaration>> Parser::parseVariableDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseVariableDeclaration");

  // variableDeclaration:
  //   bindingIdentifier typeAnnotation? initializer?
  //   | bindingPattern typeAnnotation? initializer?;

  const source::SourceLoc loc = currentLoc();

  auto name = parseIdentifierOrBindingPattern();
  // Optional type annotation
  zc::Maybe<zc::Own<ast::TypeNode>> type = parseTypeAnnotation();
  // Optional initializer
  zc::Maybe<zc::Own<ast::Expression>> initializer = parseInitializer();

  return finishNode(
      ast::factory::createVariableDeclaration(zc::mv(name), zc::mv(type), zc::mv(initializer)),
      loc);
}

// ================================================================================
// Statement parsing implementations

zc::Maybe<zc::Own<ast::BlockStatement>> Parser::parseBlockStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBlockStatement");

  source::SourceLoc loc = currentLoc();
  source::SourceLoc openBraceLoc = getTokenStartLoc();
  const bool openBraceParsed = parseExpected(ast::SyntaxKind::LeftBrace, true /*shouldAdvance*/);

  if (openBraceParsed) {
    zc::Vector<zc::Own<ast::Statement>> statements = parseList<ast::Statement>(
        ParsingContext::BlockElements, ZC_BIND_METHOD(*this, parseStatement));

    parseExpectedMatchingBrackets(ast::SyntaxKind::LeftBrace, ast::SyntaxKind::RightBrace,
                                  /*openParsed*/ true, openBraceLoc);

    auto result = finishNode(ast::factory::createBlockStatement(zc::mv(statements)), loc);

    if (currentToken().is(ast::SyntaxKind::Equals)) {
      parseErrorAtCurrentToken<diagnostics::DiagID::DeclarationOrStatementExpectedAfterBlock>();
      nextToken();
    }

    return zc::mv(result);
  }

  auto result =
      finishNode(ast::factory::createBlockStatement(parseEmptyNodeList<ast::Statement>()), loc);
  return zc::mv(result);
}

zc::Own<ast::BlockStatement> Parser::parseFunctionBlock() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseFunctionBlock");

  ZC_IF_SOME(block, parseBlockStatement()) { return zc::mv(block); }
  return finishNode(ast::factory::createBlockStatement(parseEmptyNodeList<ast::Statement>()),
                    currentLoc());
}

void Parser::parseExpectedMatchingBrackets(ast::SyntaxKind openKind, ast::SyntaxKind closeKind,
                                           bool openParsed, source::SourceLoc openPos) {
  if (expectToken(closeKind)) {
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

  source::SourceLoc loc = currentLoc();
  if (!consumeExpectedToken(ast::SyntaxKind::Semicolon)) { return zc::none; }

  // Create empty statement AST node
  return finishNode(ast::factory::createEmptyStatement(), loc);
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

  source::SourceLoc loc = currentLoc();
  auto expr = parseExpression();

  if (!tryParseSemicolon()) { parseErrorForMissingSemicolonAfter(*expr); }

  // Create expression statement AST node
  return finishNode(ast::factory::createExpressionStatement(zc::mv(expr)), loc);
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
    const auto& id = cast<ast::Identifier>(expr);
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

  source::SourceLoc loc = currentLoc();
  if (!consumeExpectedToken(ast::SyntaxKind::IfKeyword)) { return zc::none; }

  if (!consumeExpectedToken(ast::SyntaxKind::LeftParen)) { return zc::none; }

  auto condition = parseExpression();
  if (!consumeExpectedToken(ast::SyntaxKind::RightParen)) { return zc::none; }

  ZC_IF_SOME(thenStmt, parseStatement()) {
    zc::Maybe<zc::Own<ast::Statement>> elseStmt = zc::none;

    if (expectToken(ast::SyntaxKind::ElseKeyword)) {
      nextToken();
      elseStmt = parseStatement();
    }

    return finishNode(
        ast::factory::createIfStatement(zc::mv(condition), zc::mv(thenStmt), zc::mv(elseStmt)),
        loc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::WhileStatement>> Parser::parseWhileStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseWhileStatement");

  source::SourceLoc loc = currentLoc();
  if (!consumeExpectedToken(ast::SyntaxKind::WhileKeyword)) { return zc::none; }

  if (!consumeExpectedToken(ast::SyntaxKind::LeftParen)) { return zc::none; }

  auto condition = parseExpression();
  if (!consumeExpectedToken(ast::SyntaxKind::RightParen)) { return zc::none; }

  ZC_IF_SOME(body, parseStatement()) {
    return finishNode(ast::factory::createWhileStatement(zc::mv(condition), zc::mv(body)), loc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::IterationStatement>> Parser::parseForStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseForStatement");

  source::SourceLoc loc = currentLoc();
  if (!consumeExpectedToken(ast::SyntaxKind::ForKeyword)) { return zc::none; }

  // Attempt to parse '(', but don't abort if missing (error already reported)
  consumeExpectedToken(ast::SyntaxKind::LeftParen);

  // Parse init (optional)
  zc::Maybe<zc::Own<ast::Statement>> initStmt = zc::none;
  if (!expectToken(ast::SyntaxKind::Semicolon)) {
    if (expectNToken(ast::SyntaxKind::LetKeyword, ast::SyntaxKind::ConstKeyword)) {
      initStmt = ast::factory::createVariableStatement(parseVariableDeclarationList());
    } else {
      initStmt = ast::factory::createExpressionStatement(parseExpression());
    }
  }

  // Check for 'in' keyword for for-in loop
  if (currentToken().is(ast::SyntaxKind::InKeyword)) {
    nextToken();  // consume 'in'

    auto expr = parseExpression();
    consumeExpectedToken(ast::SyntaxKind::RightParen);

    zc::Own<ast::Statement> body;
    ZC_IF_SOME(stmt, parseStatement()) { body = zc::mv(stmt); }
    else { body = ast::factory::createEmptyStatement(); }

    zc::Own<ast::Statement> initializer;
    ZC_IF_SOME(init, initStmt) { initializer = zc::mv(init); }
    else { initializer = ast::factory::createEmptyStatement(); }

    return finishNode(
        ast::factory::createForInStatement(zc::mv(initializer), zc::mv(expr), zc::mv(body)), loc);
  }

  // Expect semicolon, but continue if missing
  consumeExpectedToken(ast::SyntaxKind::Semicolon);

  // Parse condition (optional)
  zc::Maybe<zc::Own<ast::Expression>> condition = zc::none;
  if (!expectToken(ast::SyntaxKind::Semicolon)) { condition = parseExpression(); }

  // Expect semicolon, but continue if missing
  consumeExpectedToken(ast::SyntaxKind::Semicolon);

  // Parse update (optional)
  zc::Maybe<zc::Own<ast::Expression>> update = zc::none;
  if (!expectToken(ast::SyntaxKind::RightParen)) { update = parseExpression(); }

  // Expect ')', but continue if missing
  consumeExpectedToken(ast::SyntaxKind::RightParen);

  zc::Own<ast::Statement> body;
  ZC_IF_SOME(stmt, parseStatement()) { body = zc::mv(stmt); }
  else {
    // If body parsing fails, create empty statement to ensure we return a valid node
    body = ast::factory::createEmptyStatement();
  }

  return finishNode(ast::factory::createForStatement(zc::mv(initStmt), zc::mv(condition),
                                                     zc::mv(update), zc::mv(body)),
                    loc);
}

zc::Maybe<zc::Own<ast::LabeledStatement>> Parser::parseLabeledStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseLabeledStatement");

  source::SourceLoc loc = currentLoc();
  auto label = parseIdentifier();

  if (!consumeExpectedToken(ast::SyntaxKind::Colon)) { return zc::none; }

  ZC_IF_SOME(stmt, parseStatement()) {
    return finishNode(ast::factory::createLabeledStatement(zc::mv(label), zc::mv(stmt)), loc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::BreakStatement>> Parser::parseBreakStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBreakStatement");

  source::SourceLoc loc = currentLoc();
  if (!consumeExpectedToken(ast::SyntaxKind::BreakKeyword)) { return zc::none; }

  // Optional label
  zc::Maybe<zc::Own<ast::Identifier>> label = zc::none;
  if (expectToken(ast::SyntaxKind::Identifier)) { label = parseIdentifier(); }

  if (!consumeExpectedToken(ast::SyntaxKind::Semicolon)) { return zc::none; }

  return finishNode(ast::factory::createBreakStatement(zc::mv(label)), loc);
}

zc::Maybe<zc::Own<ast::ContinueStatement>> Parser::parseContinueStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseContinueStatement");

  source::SourceLoc loc = currentLoc();
  if (!consumeExpectedToken(ast::SyntaxKind::ContinueKeyword)) { return zc::none; }

  // Optional label
  zc::Maybe<zc::Own<ast::Identifier>> label = zc::none;
  if (expectToken(ast::SyntaxKind::Identifier)) { label = parseIdentifier(); }

  if (!consumeExpectedToken(ast::SyntaxKind::Semicolon)) { return zc::none; }

  return finishNode(ast::factory::createContinueStatement(zc::mv(label)), loc);
}

zc::Maybe<zc::Own<ast::ReturnStatement>> Parser::parseReturnStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseReturnStatement");

  source::SourceLoc loc = currentLoc();
  if (!consumeExpectedToken(ast::SyntaxKind::ReturnKeyword)) { return zc::none; }

  // Optional expression
  zc::Maybe<zc::Own<ast::Expression>> expr = zc::none;
  if (!expectToken(ast::SyntaxKind::Semicolon)) { expr = parseExpression(); }

  if (!consumeExpectedToken(ast::SyntaxKind::Semicolon)) { return zc::none; }

  // Create return statement AST node
  return finishNode(ast::factory::createReturnStatement(zc::mv(expr)), loc);
}

zc::Maybe<zc::Own<ast::MatchStatement>> Parser::parseMatchStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseMatchStatement");

  if (!consumeExpectedToken(ast::SyntaxKind::MatchKeyword)) { return zc::none; }

  if (!consumeExpectedToken(ast::SyntaxKind::LeftParen)) { return zc::none; }

  auto expr = parseExpression();
  if (!consumeExpectedToken(ast::SyntaxKind::RightParen)) { return zc::none; }

  zc::Vector<zc::Own<ast::Statement>> clauses;
  if (!consumeExpectedToken(ast::SyntaxKind::LeftBrace)) { return zc::none; }

  while (!expectToken(ast::SyntaxKind::RightBrace)) {
    if (expectToken(ast::SyntaxKind::DefaultKeyword)) {
      nextToken();  // consume 'default'
      if (consumeExpectedToken(ast::SyntaxKind::EqualsGreaterThan)) {
        zc::Vector<zc::Own<ast::Statement>> defaultStmts = parseList<ast::Statement>(
            ParsingContext::BlockElements, ZC_BIND_METHOD(*this, parseStatement));
        clauses.add(ast::factory::createDefaultClause(zc::mv(defaultStmts)));
      }
    } else if (expectToken(ast::SyntaxKind::WhenKeyword)) {
      nextToken();  // consume 'when'
      ZC_IF_SOME(pattern, parsePattern()) {
        zc::Maybe<zc::Own<ast::Expression>> guard;
        if (consumeExpectedToken(ast::SyntaxKind::IfKeyword)) { guard = parseExpression(); }

        if (consumeExpectedToken(ast::SyntaxKind::EqualsGreaterThan)) {
          ZC_IF_SOME(statement, parseStatement()) {
            clauses.add(
                ast::factory::createMatchClause(zc::mv(pattern), zc::mv(guard), zc::mv(statement)));
          }
        }
      }
    } else {
      nextToken();
    }
  }

  if (!consumeExpectedToken(ast::SyntaxKind::RightBrace)) { return zc::none; }

  source::SourceLoc loc = currentLoc();
  return finishNode(ast::factory::createMatchStatement(zc::mv(expr), zc::mv(clauses)), loc);
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
    case ast::SyntaxKind::ImportKeyword:
      return parseImportDeclaration();
    case ast::SyntaxKind::ExportKeyword:
      return parseExportDeclaration();
    case ast::SyntaxKind::ModuleKeyword:
      return parseModuleDeclaration();
    default:
      return zc::none;
  }
}

zc::Own<ast::VariableDeclarationList> Parser::parseVariableDeclarationList() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseVariableDeclarationList");

  // variableDeclarationList: LET_OR_CONST bindingList;
  // bindingList: bindingElement (COMMA bindingElement)*;
  // bindingElement: bindingIdentifier typeAnnotation? initializer?;

  source::SourceLoc loc = currentLoc();

  nextToken();

  // Parse bindingList: bindingElement (COMMA bindingElement)*
  zc::Vector<zc::Own<ast::VariableDeclaration>> declarations =
      parseDelimitedList<ast::VariableDeclaration>(ParsingContext::VariableDeclarations,
                                                   ZC_BIND_METHOD(*this, parseVariableDeclaration));

  // Create variable declaration AST node
  return finishNode(ast::factory::createVariableDeclarationList(zc::mv(declarations)), loc);
}

zc::Maybe<zc::Own<ast::FunctionDeclaration>> Parser::parseFunctionDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseFunctionDeclaration");

  // functionDeclaration:
  //   FUN bindingIdentifier callSignature LBRACE functionBody RBRACE;

  source::SourceLoc loc = currentLoc();
  if (!consumeExpectedToken(ast::SyntaxKind::FunKeyword)) { return zc::none; }

  auto name = parseBindingIdentifier();
  auto typeParameters = parseTypeParameters();
  auto parameters = parseParameters();
  zc::Maybe<zc::Own<ast::ReturnTypeNode>> returnType = parseReturnType();

  ZC_IF_SOME(body, parseBlockStatement()) {
    return finishNode(ast::factory::createFunctionDeclaration(zc::mv(name), zc::mv(typeParameters),
                                                              zc::mv(parameters),
                                                              zc::mv(returnType), zc::mv(body)),
                      loc);
  }

  return zc::none;
}

zc::Own<ast::ClassDeclaration> Parser::parseClassDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseClassDeclaration");

  source::SourceLoc classLoc = currentLoc();
  parseExpected(ast::SyntaxKind::ClassKeyword);

  auto name = parseBindingIdentifier();
  auto typeParameters = parseTypeParameters();
  zc::Maybe<zc::Vector<zc::Own<ast::HeritageClause>>> superClasses = parseHeritageClauses();

  zc::Vector<zc::Own<ast::ClassElement>> members;
  if (parseExpected(ast::SyntaxKind::LeftBrace)) {
    members = parseClassOrStructMembers(/*isStruct*/ false);
    parseExpected(ast::SyntaxKind::RightBrace);
  }

  return finishNode(ast::factory::createClassDeclaration(zc::mv(name), zc::mv(typeParameters),
                                                         zc::mv(superClasses), zc::mv(members)),
                    classLoc);
}

zc::Maybe<zc::Own<ast::InterfaceDeclaration>> Parser::parseInterfaceDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseInterfaceDeclaration");

  source::SourceLoc loc = currentLoc();

  parseExpected(ast::SyntaxKind::InterfaceKeyword);

  auto name = parseIdentifier();
  auto typeParameters = parseTypeParameters();
  zc::Maybe<zc::Vector<zc::Own<ast::HeritageClause>>> heritageClauses = parseHeritageClauses();

  zc::Vector<zc::Own<ast::InterfaceElement>> members = parseInterfaceMembers();

  auto iface = ast::factory::createInterfaceDeclaration(zc::mv(name), zc::mv(typeParameters),
                                                        zc::mv(heritageClauses), zc::mv(members));
  return finishNode(zc::mv(iface), loc);
}

zc::Own<ast::StructDeclaration> Parser::parseStructDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseStructDeclaration");

  source::SourceLoc loc = currentLoc();
  parseExpected(ast::SyntaxKind::StructKeyword);

  auto name = parseBindingIdentifier();
  auto typeParameters = parseTypeParameters();

  zc::Maybe<zc::Vector<zc::Own<ast::HeritageClause>>> heritageClauses = parseHeritageClauses();

  zc::Vector<zc::Own<ast::ClassElement>> members;
  if (parseExpected(ast::SyntaxKind::LeftBrace)) {
    members = parseClassOrStructMembers(/*isStruct*/ true);
    parseExpected(ast::SyntaxKind::RightBrace);
  }

  auto decl = ast::factory::createStructDeclaration(zc::mv(name), zc::mv(typeParameters),
                                                    zc::mv(heritageClauses), zc::mv(members));
  return finishNode(zc::mv(decl), loc);
}

zc::Maybe<zc::Own<ast::EnumMember>> Parser::parseEnumMember() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseEnumMember");

  if (!isIdentifier()) { return zc::none; }

  source::SourceLoc loc = currentLoc();
  auto name = parseIdentifier();

  // Parse optional initializer or tuple type
  zc::Maybe<zc::Own<ast::Expression>> initializer = zc::none;
  zc::Maybe<zc::Own<ast::TupleTypeNode>> tupleType = zc::none;

  if (expectToken(ast::SyntaxKind::Equals)) {
    nextToken();
    initializer = parseAssignmentExpressionOrHigher();
  } else if (expectToken(ast::SyntaxKind::LeftParen)) {
    // Parse tuple type for enum member
    // The parseTupleType method expects current token to be LeftParen
    tupleType = parseTupleType();
  }

  return finishNode(
      ast::factory::createEnumMember(zc::mv(name), zc::mv(initializer), zc::mv(tupleType)), loc);
}

zc::Maybe<zc::Own<ast::EnumDeclaration>> Parser::parseEnumDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseEnumDeclaration");

  if (!consumeExpectedToken(ast::SyntaxKind::EnumKeyword)) { return zc::none; }

  auto name = parseIdentifier();
  source::SourceLoc loc = currentLoc();

  if (!consumeExpectedToken(ast::SyntaxKind::LeftBrace)) { return zc::none; }

  zc::Vector<zc::Own<ast::EnumMember>> members;
  while (!expectToken(ast::SyntaxKind::RightBrace)) {
    ZC_IF_SOME(member, parseEnumMember()) { members.add(zc::mv(member)); }
    else {
      if (!expectToken(ast::SyntaxKind::Comma)) {
        parseErrorAtCurrentToken<diagnostics::DiagID::EnumMemberExpected>();
        nextToken();
      }
    }
    if (expectToken(ast::SyntaxKind::Comma)) { nextToken(); }
  }

  if (!consumeExpectedToken(ast::SyntaxKind::RightBrace)) { return zc::none; }

  return finishNode(ast::factory::createEnumDeclaration(zc::mv(name), zc::mv(members)), loc);
}

zc::Maybe<zc::Own<ast::ErrorDeclaration>> Parser::parseErrorDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseErrorDeclaration");

  if (!consumeExpectedToken(ast::SyntaxKind::ErrorKeyword)) { return zc::none; }

  auto name = parseIdentifier();
  source::SourceLoc loc = currentLoc();

  zc::Vector<zc::Own<ast::Statement>> fields;
  if (expectToken(ast::SyntaxKind::LeftBrace)) {
    nextToken();

    while (!expectToken(ast::SyntaxKind::RightBrace)) {
      ZC_IF_SOME(field, parseStatement()) { fields.add(zc::mv(field)); }
    }

    if (!consumeExpectedToken(ast::SyntaxKind::RightBrace)) { return zc::none; }
  }

  return finishNode(ast::factory::createErrorDeclaration(zc::mv(name), zc::mv(fields)), loc);
}

zc::Maybe<zc::Own<ast::AliasDeclaration>> Parser::parseAliasDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseAliasDeclaration");

  source::SourceLoc loc = currentLoc();
  if (!consumeExpectedToken(ast::SyntaxKind::AliasKeyword)) { return zc::none; }

  auto name = parseIdentifier();

  // Parse optional type parameters
  zc::Maybe<zc::Vector<zc::Own<ast::TypeParameterDeclaration>>> typeParameters = zc::none;
  if (currentToken().is(ast::SyntaxKind::LessThan)) { typeParameters = parseTypeParameters(); }

  if (!consumeExpectedToken(ast::SyntaxKind::Equals)) { return zc::none; }

  auto type = parseType();
  if (!consumeExpectedToken(ast::SyntaxKind::Semicolon)) { return zc::none; }
  source::SourceLoc endLoc = currentLoc();

  return finishNode(
      ast::factory::createAliasDeclaration(zc::mv(name), zc::mv(typeParameters), zc::mv(type)), loc,
      endLoc);
}

// ================================================================================
// Expression parsing implementations

zc::Own<ast::Expression> Parser::parseExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseExpression");

  // expression: assignmentExpression (COMMA assignmentExpression)*;
  //
  // Parses a comma-separated list of assignment expressions

  const source::SourceLoc loc = currentLoc();

  auto expression = parseAssignmentExpressionOrHigher();
  // Handle comma operator
  while (expectToken(ast::SyntaxKind::Comma)) {
    auto operatorToken = parseTokenNode();
    expression =
        finishNode(ast::factory::createBinaryExpression(zc::mv(expression), zc::mv(operatorToken),
                                                        parseAssignmentExpressionOrHigher()),
                   loc);
  }
  return zc::mv(expression);
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseInitializer() {
  if (parseOptional(ast::SyntaxKind::Equals)) { return parseAssignmentExpressionOrHigher(); }
  return zc::none;
}

// Assignment expression parsing
zc::Own<ast::Expression> Parser::parseAssignmentExpressionOrHigher() {
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
  if (expectToken(ast::SyntaxKind::FunKeyword)) { return parseFunctionExpression(); }

  // Parse binary expression with lowest precedence to get the left operand
  const source::SourceLoc loc = currentLoc();

  auto expr = parseBinaryExpressionOrHigher(ast::OperatorPrecedence::kLowest);

  if (isLeftHandSideExpression(*expr) && isAssignmentOperator(reScanGreaterToken())) {
    return finishNode(ast::factory::createBinaryExpression(zc::mv(expr), parseTokenNode(),
                                                           parseAssignmentExpressionOrHigher()),
                      loc);
  }

  // Not an assignment, check for conditional expression (ternary operator)
  return parseConditionalExpressionRest(zc::mv(expr), loc);
}

// conditional expression rest parsing
zc::Own<ast::Expression> Parser::parseConditionalExpressionRest(
    zc::Own<ast::Expression> leftOperand, source::SourceLoc loc) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseConditionalExpressionRest");

  // conditionalExpression:
  //   shortCircuitExpression (QUESTION assignmentExpression COLON assignmentExpression)?
  //
  // Check for ternary conditional operator

  zc::Maybe<zc::Own<ast::TokenNode>> questionToken = parseOptionalToken(ast::SyntaxKind::Question);
  if (questionToken == zc::none) {
    // No conditional , return the left operand as-is
    return zc::mv(leftOperand);
  }

  // Note: we explicitly 'allowIn' in the whenTrue part of the condition expression, and
  // we do not that for the 'whenFalse' part.
  auto whenTrue = parseAssignmentExpressionOrHigher();
  zc::Maybe<zc::Own<ast::TokenNode>> colonToken = parseExpectedToken(ast::SyntaxKind::Colon);
  if (colonToken == zc::none) {
    parseErrorAtCurrentToken<diagnostics::DiagID::ExpectedToken>(":"_zc);
  }
  return finishNode(
      ast::factory::createConditionalExpression(
          zc::mv(leftOperand), zc::mv(questionToken), zc::mv(whenTrue), zc::mv(colonToken),
          colonToken != zc::none ? parseAssignmentExpressionOrHigher()
                                 : ast::factory::createMissingIdentifier()),
      loc);
}

// Binary expression parsing
zc::Own<ast::Expression> Parser::parseBinaryExpressionOrHigher(ast::OperatorPrecedence precedence) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBinaryExpressionOrHigher");

  // Handles all binary expressions with precedence:
  //   bitwiseORExpression | bitwiseXORExpression | bitwiseANDExpression
  //   | equalityExpression | relationalExpression | shiftExpression
  //   | additiveExpression | multiplicativeExpression | exponentiationExpression
  //
  // Uses operator precedence parsing for left-to-right associativity

  const source::SourceLoc loc = currentLoc();
  // Parse the left operand (unary expression or higher)
  auto leftOperand = parseUnaryExpressionOrHigher();
  // Parse the rest of the binary expression with lowest precedence
  return parseBinaryExpressionRest(zc::mv(leftOperand), precedence, loc);
}

// Binary expression rest parsing
zc::Own<ast::Expression> Parser::parseBinaryExpressionRest(zc::Own<ast::Expression> leftOperand,
                                                           ast::OperatorPrecedence precedence,
                                                           source::SourceLoc loc) {
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
      const bool hasLineBreakBeforeAs = currentToken().hasPrecedingLineBreak();
      if (hasLineBreakBeforeAs) {
        parseErrorAtCurrentToken<diagnostics::DiagID::LineBreakNotAllowedBeforeAsCast>();
      }

      nextToken();

      const bool isForcedCast =
          expectToken(ast::SyntaxKind::Exclamation) && !currentToken().hasPrecedingLineBreak();
      const bool isConditionalCast =
          expectToken(ast::SyntaxKind::Question) && !currentToken().hasPrecedingLineBreak();

      if (isForcedCast || isConditionalCast) { nextToken(); }

      auto targetType = parseType();

      if (isForcedCast) {
        expr = finishNode(ast::factory::createForcedAsExpression(zc::mv(expr), zc::mv(targetType)),
                          loc);
      } else if (isConditionalCast) {
        expr = finishNode(
            ast::factory::createConditionalAsExpression(zc::mv(expr), zc::mv(targetType)), loc);
      } else {
        expr = finishNode(ast::factory::createAsExpression(zc::mv(expr), zc::mv(targetType)), loc);
      }
    } else {
      auto op = parseTokenNode();
      auto rightOperand = parseBinaryExpressionOrHigher(newPrecedence);
      expr = finishNode(
          ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(rightOperand)),
          loc);
    }
  }

  return finishNode(zc::mv(expr), loc);
}

// Unary expression parsing
zc::Own<ast::Expression> Parser::parseUnaryExpressionOrHigher() {
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
    const source::SourceLoc loc = currentLoc();
    auto updateExpression = parseUpdateExpression();
    return expectToken(ast::SyntaxKind::AsteriskAsterisk)
               ? parseBinaryExpressionRest(zc::mv(updateExpression),
                                           getBinaryOperatorPrecedence(currentKind()), loc)
               : zc::mv(updateExpression);
  }

  const lexer::Token unaryOperator = currentToken();
  zc::Own<ast::UnaryExpression> simpleUnaryExpression = parseSimpleUnaryExpression();
  // Check if followed by exponentiation operator
  if (expectToken(ast::SyntaxKind::AsteriskAsterisk)) {
    const source::SourceLoc pos = simpleUnaryExpression->getSourceRange().getStart();
    const source::SourceLoc end = getTokenStartLoc();
    zc::StringPtr operatorText = unaryOperator.getValue();
    parseErrorAt<diagnostics::DiagID::UnaryExpressionInExponentiation>(pos, end, operatorText);
  }
  return zc::mv(simpleUnaryExpression);
}

// Simple unary expression parsing
zc::Own<ast::UnaryExpression> Parser::parseSimpleUnaryExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseSimpleUnaryExpression");

  switch (currentKind()) {
    case ast::SyntaxKind::Plus:
    case ast::SyntaxKind::Minus:
    case ast::SyntaxKind::Tilde:
    case ast::SyntaxKind::Exclamation: {
      return parsePrefixUnaryExpression();
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
zc::Own<ast::UnaryExpression> Parser::parsePrefixUnaryExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parsePrefixUnaryExpression");

  // prefixUnaryExpression:
  //   PLUS unaryExpression
  //   | MINUS unaryExpression
  //   | TILDE unaryExpression
  //   | EXCLAMATION unaryExpression;
  //
  // Parse prefix unary operators: +, -, ~, !

  const source::SourceLoc loc = currentLoc();
  const ast::SyntaxKind op = currentKind();
  nextToken();

  // Parse the operand (recursive call to parseSimpleUnaryExpression)
  return finishNode(
      ast::factory::createPrefixUnaryExpression(zc::mv(op), parseSimpleUnaryExpression()), loc);
}

// Void expression parsing
zc::Maybe<zc::Own<ast::VoidExpression>> Parser::parseVoidExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseVoidExpression");

  // voidExpression:
  //   VOID unaryExpression;
  //
  // Parse void operator expression

  const source::SourceLoc loc = currentLoc();

  nextToken();

  // Parse the operand
  auto operand = parseSimpleUnaryExpression();

  // Create void expression
  auto voidExpr = ast::factory::createVoidExpression(zc::mv(operand));
  return finishNode(zc::mv(voidExpr), loc);
}

// TypeOf expression parsing
zc::Own<ast::TypeOfExpression> Parser::parseTypeOfExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseTypeOfExpression");

  // typeOfExpression:
  //   TYPEOF unaryExpression;
  //
  // Parse typeof operator expression

  const source::SourceLoc loc = currentLoc();

  nextToken();

  // Create typeof expression
  auto typeofExpr = ast::factory::createTypeOfExpression(parseSimpleUnaryExpression());
  return finishNode(zc::mv(typeofExpr), loc);
}

// Left-hand side expression parsing
zc::Own<ast::LeftHandSideExpression> Parser::parseLeftHandSideExpressionOrHigher() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser,
                                 "parseLeftHandSideExpressionOrHigher");

  // leftHandSideExpression:
  //   newExpression
  //   | callExpression
  //   | optionalExpression;

  zc::Own<ast::LeftHandSideExpression> expression;
  const source::SourceLoc loc = currentLoc();

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
    expression = parseSuperExpression();
  }
  // Parse regular member expression
  else {
    expression = parseMemberExpressionOrHigher();
  }

  // If we have an expression, parse call expression rest
  return parseCallExpressionRest(loc, zc::mv(expression));
}

// Helper method to parse member expression or higher
zc::Own<ast::LeftHandSideExpression> Parser::parseMemberExpressionOrHigher() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseMemberExpressionOrHigher");

  // memberExpression:
  //   (primaryExpression | superProperty | NEW memberExpression arguments)
  //   (LBRACK expression RBRACK | PERIOD identifier)*;
  //
  // Based on TypeScript implementation, we parse primary expression first,
  // then handle member access chains

  const source::SourceLoc loc = currentLoc();

  auto expression = parsePrimaryExpression();

  // Parse member expression rest (property access chains)
  return parseMemberExpressionRest(zc::mv(expression), loc, /*allowOptionalChain*/ true);
}

// Helper method to parse call expression rest
zc::Own<ast::LeftHandSideExpression> Parser::parseCallExpressionRest(
    source::SourceLoc loc, zc::Own<ast::LeftHandSideExpression> expression) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseCallExpressionRest");

  // callExpression:
  //   (memberExpression arguments | superCall | importCall)
  //   (arguments | LBRACK expression RBRACK | PERIOD identifier)*;
  //
  // This method handles the iterative parsing of call chains

  while (true) {
    // First parse member expression rest to handle property/element access chains
    expression = parseMemberExpressionRest(zc::mv(expression), loc, /*allowOptionalChain*/ true);

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
        const bool isOptionalChain =
            questionDotToken != zc::none || tryReparseOptionalChain(*expression);
        auto callExpr = ast::factory::createCallExpression(
            zc::mv(expression), zc::mv(questionDotToken), zc::mv(typeArguments), zc::mv(arguments),
            isOptionalChain);
        expression = finishNode(zc::mv(callExpr), loc);
        continue;
      }
    }

    // Handle optional chaining error case
    if (questionDotToken != zc::none) {
      // We parsed `?.` but then failed to parse anything, so report a missing identifier here.
      // TODO: Add proper diagnostic error reporting
      // parseErrorAtCurrentToken(diagnostics::Identifier_expected);

      // Create missing identifier and property access expression
      auto name = ast::factory::createMissingIdentifier();  // Missing identifier placeholder
      auto propAccessExpr = ast::factory::createPropertyAccessExpression(
          zc::mv(expression), zc::mv(name), /*questionDot*/ true, /*isOptionalChain*/ true);
      expression = finishNode(zc::mv(propAccessExpr), loc);
    }

    break;
  }

  return expression;
}

zc::Own<ast::UpdateExpression> Parser::parseUpdateExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseUpdateExpression");

  // updateExpression:
  //   leftHandSideExpression
  //   | leftHandSideExpression INC
  //   | leftHandSideExpression DEC
  //   | INC leftHandSideExpression
  //   | DEC leftHandSideExpression;

  const source::SourceLoc loc = currentLoc();

  // Check for prefix increment/decrement operators
  if (expectNToken(ast::SyntaxKind::PlusPlus, ast::SyntaxKind::MinusMinus)) {
    const lexer::Token token = currentToken();

    nextToken();
    return finishNode(ast::factory::createPrefixUnaryExpression(
                          token.getKind(), parseLeftHandSideExpressionOrHigher()),
                      loc);
  }

  // Parse leftHandSideExpression first
  auto expression = parseLeftHandSideExpressionOrHigher();

  // Check for postfix increment/decrement operators
  if (expectNToken(ast::SyntaxKind::PlusPlus, ast::SyntaxKind::MinusMinus) &&
      !hasPrecedingLineBreak()) {
    const lexer::Token token = currentToken();
    nextToken();

    return finishNode(
        ast::factory::createPostfixUnaryExpression(token.getKind(), zc::mv(expression)), loc);
  }

  // No update operators found, return the expression as-is
  return zc::mv(expression);
}

zc::Own<ast::LeftHandSideExpression> Parser::parseLeftHandSideExpression() {
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

zc::Own<ast::PrimaryExpression> Parser::parsePrimaryExpression() {
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

  switch (currentKind()) {
    case ast::SyntaxKind::IntegerLiteral:
    case ast::SyntaxKind::FloatLiteral:
    case ast::SyntaxKind::BigIntLiteral:
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
    case ast::SyntaxKind::ThisKeyword: {
      const source::SourceLoc loc = currentLoc();
      nextToken();
      return finishNode(ast::factory::createThisExpression(), loc);
    }
    case ast::SyntaxKind::FunKeyword:
      return parseFunctionExpression();
    case ast::SyntaxKind::NewKeyword:
      return parseNewExpression();
    default:
      break;
  }

  return parseIdentifier();
}

zc::Own<ast::LiteralExpression> Parser::parseLiteralExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseLiteralExpression");

  // literal:
  //   nullLiteral
  //   | booleanLiteral
  //   | numericLiteral
  //   | bigIntLiteral
  //   | stringLiteral;
  //
  // Handles all literal values (numbers, strings, booleans, null)

  const source::SourceLoc loc = currentLoc();

  const lexer::Token token = currentToken();
  const zc::StringPtr value = token.getValue();

  switch (token.getKind()) {
    case ast::SyntaxKind::StringLiteral: {
      nextToken();
      return finishNode(ast::factory::createStringLiteral(value), loc);
    }
    case ast::SyntaxKind::NoSubstitutionTemplateLiteral: {
      nextToken();
      auto head = finishNode(ast::factory::createStringLiteral(value), loc);
      return finishNode(ast::factory::createTemplateLiteralExpression(
                            zc::mv(head), zc::Vector<zc::Own<ast::TemplateSpan>>()),
                        loc);
    }
    case ast::SyntaxKind::IntegerLiteral: {
      nextToken();
      return finishNode(ast::factory::createIntegerLiteral(value.parseAs<int64_t>()), loc);
    }
    case ast::SyntaxKind::FloatLiteral: {
      nextToken();
      return finishNode(ast::factory::createFloatLiteral(value.parseAs<double>()), loc);
    }
    case ast::SyntaxKind::BigIntLiteral: {
      nextToken();
      return finishNode(zc::heap<ast::BigIntLiteral>(value), loc);
    }
    case ast::SyntaxKind::TrueKeyword:
      ZC_FALLTHROUGH;
    case ast::SyntaxKind::FalseKeyword: {
      nextToken();
      return finishNode(ast::factory::createBooleanLiteral(token.is(ast::SyntaxKind::TrueKeyword)),
                        loc);
    }
    case ast::SyntaxKind::NullKeyword: {
      nextToken();
      return finishNode(ast::factory::createNullLiteral(), loc);
    }
    default: {
      ZC_FAIL_ASSERT("Unhandled case in parseLiteralExpression");
      ZC_UNREACHABLE;
    }
  }
}

zc::Own<ast::Expression> Parser::parseSpreadElement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseSpreadElement");

  // spreadElement:
  //   DOTDOTDOT expression;
  //
  // Handles spread elements in array literals

  const source::SourceLoc loc = currentLoc();
  parseExpected(ast::SyntaxKind::DotDotDot);
  auto expression = parseAssignmentExpressionOrHigher();
  return finishNode(ast::factory::createSpreadElement(zc::mv(expression)), loc);
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseArrayLiteralElement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseArrayLiteralElement");

  // arrayLiteralElement:
  //   expression
  //   | spreadElement;
  //
  // Handles individual elements in array literals

  return expectToken(ast::SyntaxKind::DotDotDot) ? parseSpreadElement()
                                                 : parseAssignmentExpressionOrHigher();
}

zc::Own<ast::ArrayLiteralExpression> Parser::parseArrayLiteralExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseArrayLiteralExpression");

  // arrayLiteral:
  //   LBRACK RBRACK
  //   | LBRACK elementList RBRACK
  //   | LBRACK elementList COMMA RBRACK;
  //
  // Handles array literal expressions [1, 2, 3]

  const source::SourceLoc loc = currentLoc();
  const source::SourceLoc openBracketLoc = getTokenStartLoc();
  const bool openBracketParsed = parseExpected(ast::SyntaxKind::LeftBracket);
  const bool multiLine = hasPrecedingLineBreak();

  auto elements = parseDelimitedList<ast::Expression>(
      ParsingContext::ArrayLiteralMembers, ZC_BIND_METHOD(*this, parseArrayLiteralElement));
  parseExpectedMatchingBrackets(ast::SyntaxKind::LeftBracket, ast::SyntaxKind::RightBracket,
                                openBracketParsed, openBracketLoc);

  return finishNode(ast::factory::createArrayLiteralExpression(zc::mv(elements), multiLine), loc);
}

zc::Own<ast::ObjectLiteralExpression> Parser::parseObjectLiteralExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseObjectLiteralExpression");

  // objectLiteral:
  //   LBRACE RBRACE
  //   | LBRACE propertyDefinitionList RBRACE
  //   | LBRACE propertyDefinitionList COMMA RBRACE;
  //
  // Handles object literal expressions {key: value}

  source::SourceLoc loc = currentLoc();
  source::SourceLoc openBraceLoc = getTokenStartLoc();

  const bool openBraceParsed = parseExpected(ast::SyntaxKind::LeftBrace);
  const bool multiLine = hasPrecedingLineBreak();

  auto properties = parseDelimitedList<ast::ObjectLiteralElement>(
      ParsingContext::ObjectLiteralMembers, ZC_BIND_METHOD(*this, parseObjectLiteralElement));
  parseExpectedMatchingBrackets(ast::SyntaxKind::LeftBrace, ast::SyntaxKind::RightBrace,
                                openBraceParsed, openBraceLoc);

  return finishNode(ast::factory::createObjectLiteralExpression(zc::mv(properties), multiLine),
                    loc);
}

zc::Maybe<zc::Own<ast::ObjectLiteralElement>> Parser::parseObjectLiteralElement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseObjectLiteralElement");

  const source::SourceLoc pos = currentLoc();

  if (parseOptional(ast::SyntaxKind::DotDotDot)) {
    auto expression = parseAssignmentExpressionOrHigher();
    return finishNode(ast::factory::createSpreadAssignment(zc::mv(expression)), pos);
  }

  const bool tokenIsIdentifier = isIdentifier();
  auto name = parsePropertyName();
  auto questionToken = parseOptionalToken(ast::SyntaxKind::Question);

  // Shorthand property assignment
  if (tokenIsIdentifier && !expectToken(ast::SyntaxKind::Colon)) {
    auto equalsToken = parseOptionalToken(ast::SyntaxKind::Equals);
    zc::Maybe<zc::Own<ast::Expression>> objectAssignmentInitializer;
    if (equalsToken != zc::none) {
      objectAssignmentInitializer = parseAssignmentExpressionOrHigher();
    }

    return finishNode(ast::factory::createShorthandPropertyAssignment(
                          zc::mv(name), zc::mv(objectAssignmentInitializer), zc::mv(equalsToken)),
                      pos);
  }

  parseExpected(ast::SyntaxKind::Colon);

  auto initializer = parseAssignmentExpressionOrHigher();

  return finishNode(ast::factory::createPropertyAssignment(zc::mv(name), zc::mv(initializer),
                                                           zc::mv(questionToken)),
                    pos);
}

// ================================================================================
// Type parsing implementations

zc::Own<ast::TypeNode> Parser::parseType() {
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
  if (isStartOfFunctionType()) { return parseFunctionType(); }

  return parseUnionTypeOrHigher();
}

zc::Maybe<zc::Own<ast::TypeNode>> Parser::parseTypeAnnotation() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseTypeAnnotation");

  if (!parseOptional(ast::SyntaxKind::Colon)) { return zc::none; }
  return parseType();
}

zc::Maybe<zc::Own<ast::TypeNode>> Parser::parseFunctionTypeToError(bool isUnionType) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseFunctionTypeToError");

  if (!isStartOfFunctionType()) { return zc::none; }

  // Function type notation is not allowed directly as a constituent of a union
  // or intersection. Parse it anyway so the AST stays recoverable, then attach a
  // targeted diagnostic asking the user to add parentheses.
  zc::Own<ast::TypeNode> type = parseFunctionType();
  if (isUnionType) {
    parseErrorAtRange<diagnostics::DiagID::FunctionTypeNotationMustBeParenthesizedInUnionType>(
        type->getSourceRange());
  } else {
    parseErrorAtRange<
        diagnostics::DiagID::FunctionTypeNotationMustBeParenthesizedInIntersectionType>(
        type->getSourceRange());
  }

  return type;
}

zc::Own<ast::TypeNode> Parser::parseUnionOrIntersectionType(
    ast::SyntaxKind operatorToken, zc::Function<zc::Own<ast::TypeNode>()> parseConstituentType) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseUnionOrIntersectionType");
  const source::SourceLoc loc = currentLoc();
  const bool isUnionType = operatorToken == ast::SyntaxKind::Bar;
  const bool hasLeadingOperator = parseOptional(operatorToken);

  auto type = hasLeadingOperator ? parseFunctionTypeToError(isUnionType).orDefault([&]() {
    return parseConstituentType();
  })
                                 : parseConstituentType();

  if (expectToken(operatorToken) || hasLeadingOperator) {
    zc::Vector<zc::Own<ast::TypeNode>> types;
    types.add(zc::mv(type));

    while (parseOptional(operatorToken)) {
      types.add(parseFunctionTypeToError(isUnionType).orDefault([&]() {
        return parseConstituentType();
      }));
    }

    switch (operatorToken) {
      case ast::SyntaxKind::Bar:
        return finishNode(ast::factory::createUnionType(zc::mv(types)), loc);
      case ast::SyntaxKind::Ampersand:
        return finishNode(ast::factory::createIntersectionType(zc::mv(types)), loc);
      default:
        ZC_UNREACHABLE;
    }
  }

  return type;
}

zc::Own<ast::TypeNode> Parser::parseUnionTypeOrHigher() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseUnionTypeOrHigher");

  // unionTypeOrHigher:
  //   intersectionTypeOrHigher (PIPE intersectionTypeOrHigher)*;
  //
  // Shares the same skeleton as intersection parsing so leading operators and
  // function-type constituents are handled consistently.
  return parseUnionOrIntersectionType(ast::SyntaxKind::Bar,
                                      ZC_BIND_METHOD(*this, parseIntersectionTypeOrHigher));
}

zc::Own<ast::TypeNode> Parser::parseIntersectionTypeOrHigher() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseIntersectionTypeOrHigher");

  // intersectionTypeOrHigher:
  //   postfixTypeOrHigher (AMPERSAND postfixTypeOrHigher)*;
  //
  // Handles intersection types like A & B & C

  return parseUnionOrIntersectionType(ast::SyntaxKind::Ampersand,
                                      ZC_BIND_METHOD(*this, parsePostfixTypeOrHigher));
}

zc::Own<ast::TypeNode> Parser::parsePostfixTypeOrHigher() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parsePostfixTypeOrHigher");

  ZC_IF_SOME(type, parsePostfixType()) { return zc::mv(type); }

  parseErrorAtCurrentToken<diagnostics::DiagID::TypeExpected>();
  return finishNode(ast::factory::createPredefinedType("unit"_zc), currentLoc());
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
  const source::SourceLoc loc = token.getLocation();

  // Parse the base type atom
  zc::Maybe<zc::Own<ast::TypeNode>> maybeBase;
  switch (currentKind()) {
    case ast::SyntaxKind::LeftParen:
      // Parenthesized type: (T) or tuple type: (T, U) or (name: T, other: U)
      maybeBase = parseParenthesizedOrTupleType();
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
        result = finishNode(ast::factory::createOptionalType(zc::mv(result)), loc);
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

  const source::SourceLoc loc = currentLoc();

  ZC_IF_SOME(elementType, parsePostfixType()) {
    if (expectToken(ast::SyntaxKind::LeftBracket)) {
      nextToken();  // consume '['

      if (!consumeExpectedToken(ast::SyntaxKind::RightBracket)) {
        // Error: expected closing bracket ']' for array type
        return zc::none;
      }

      // Create array type node with the element type
      return finishNode(ast::factory::createArrayType(zc::mv(elementType)), loc);
    }
  }

  return zc::none;
}

zc::Own<ast::FunctionTypeNode> Parser::parseFunctionType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseFunctionType");

  // Parse function type according to the grammar rule:
  //
  // functionType: typeParameters? parameterClause (ARROW type raisesClause?)?
  //
  // This handles function types like (a: T, b: U) -> R or (a: T) -> R raises E
  // where raisesClause is optional and specifies error types that the function can raise

  source::SourceLoc loc = currentLoc();

  // Parse optional type parameters: <T, U>
  auto typeParameters = parseTypeParameters();
  // Parse parameter clause: (param1: Type1, param2: Type2)
  auto parameters = parseParameters();
  // Parse return type: -> type raises error
  return finishNode(ast::factory::createFunctionType(zc::mv(typeParameters), zc::mv(parameters),
                                                     parseRequiredReturnType()),
                    loc);
}

zc::Maybe<zc::Own<ast::TypeNode>> Parser::parseParenthesizedOrTupleType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseParenthesizedOrTupleType");

  // Parses either a parenthesized type (T) or a tuple type (T, U) or (name: T, other: U).
  // Disambiguation: if there is exactly one element with no comma and no named prefix,
  // it is a parenthesized type; otherwise it is a tuple type.

  const source::SourceLoc loc = currentLoc();

  if (!consumeExpectedToken(ast::SyntaxKind::LeftParen)) { return zc::none; }

  // Empty parentheses: ()
  if (expectToken(ast::SyntaxKind::RightParen)) {
    nextToken();  // consume ')'
    return finishNode(ast::factory::createTupleType(zc::Vector<zc::Own<ast::TypeNode>>{}), loc);
  }

  // Try to parse the first element, checking for named tuple element pattern: identifier ':'
  zc::Vector<zc::Own<ast::TypeNode>> elements;
  bool hasNamedElement = false;

  // Try named element: identifier ':' type
  if (currentToken().is(ast::SyntaxKind::Identifier)) {
    ParserState state = mark();
    auto name = parseIdentifier();
    if (currentToken().is(ast::SyntaxKind::Colon)) {
      nextToken();  // consume ':'
      hasNamedElement = true;
      elements.add(ast::factory::createNamedTupleElement(zc::mv(name), parseType()));
    } else {
      // Not a named element, rewind and parse as a regular type
      rewind(state);
    }
  }

  // If not a named element, parse as a regular type
  if (!hasNamedElement) { elements.add(parseType()); }

  // Check if there are more elements (comma-separated)
  if (expectToken(ast::SyntaxKind::Comma)) {
    // It's a tuple type — parse remaining elements
    while (consumeExpectedToken(ast::SyntaxKind::Comma)) {
      // Try named element
      if (currentToken().is(ast::SyntaxKind::Identifier)) {
        ParserState state = mark();
        auto name = parseIdentifier();
        if (currentToken().is(ast::SyntaxKind::Colon)) {
          nextToken();  // consume ':'
          elements.add(ast::factory::createNamedTupleElement(zc::mv(name), parseType()));
          continue;
        } else {
          rewind(state);
        }
      }

      elements.add(parseType());
    }
  }

  if (!consumeExpectedToken(ast::SyntaxKind::RightParen)) { return zc::none; }

  // Single unnamed element with no comma → parenthesized type
  if (elements.size() == 1 && !hasNamedElement) {
    auto type = zc::mv(elements[0]);
    return finishNode(ast::factory::createParenthesizedType(zc::mv(type)), loc);
  }

  return finishNode(ast::factory::createTupleType(zc::mv(elements)), loc);
}

zc::Maybe<zc::Own<ast::ObjectTypeNode>> Parser::parseObjectType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseObjectType");

  // objectType:
  //   LBRACE typeMemberList? RBRACE;
  //
  // Handles object types like {prop: T, method(): U}

  const source::SourceLoc loc = currentLoc();

  if (!consumeExpectedToken(ast::SyntaxKind::LeftBrace)) { return zc::none; }

  zc::Vector<zc::Own<ast::Node>> members;

  // Parse object type members
  if (!expectToken(ast::SyntaxKind::RightBrace)) {
    while (true) {
      const source::SourceLoc memberLoc = currentLoc();

      if (!isLiteralPropertyName()) {
        parseErrorAtCurrentToken<diagnostics::DiagID::TypeExpected>();
        return zc::none;
      }

      auto name = parsePropertyName();
      auto questionToken = parseOptionalToken(ast::SyntaxKind::Question);

      if (expectNToken(ast::SyntaxKind::LeftParen, ast::SyntaxKind::LessThan)) {
        auto typeParameters = parseTypeParameters();
        auto parameters = parseParameters();
        auto type = parseReturnType();
        zc::Own<ast::Node> member =
            finishNode(ast::factory::createMethodSignature(
                           zc::Vector<ast::SyntaxKind>{}, zc::mv(name), zc::mv(questionToken),
                           zc::mv(typeParameters), zc::mv(parameters), zc::mv(type)),
                       memberLoc);
        members.add(zc::mv(member));
      } else {
        auto type = parseTypeAnnotation();
        zc::Own<ast::Node> member = finishNode(
            ast::factory::createPropertySignature(zc::Vector<ast::SyntaxKind>{}, zc::mv(name),
                                                  zc::mv(questionToken), zc::mv(type), zc::none),
            memberLoc);
        members.add(zc::mv(member));
      }

      if (consumeExpectedToken(ast::SyntaxKind::Comma) ||
          consumeExpectedToken(ast::SyntaxKind::Semicolon)) {
        if (expectToken(ast::SyntaxKind::RightBrace)) { break; }
        continue;
      }

      if (expectToken(ast::SyntaxKind::RightBrace)) { break; }

      parseExpected(ast::SyntaxKind::Comma);
      if (expectToken(ast::SyntaxKind::RightBrace)) { break; }
    }
  }

  if (!consumeExpectedToken(ast::SyntaxKind::RightBrace)) { return zc::none; }

  return finishNode(ast::factory::createObjectType(zc::mv(members)), loc);
}

zc::Maybe<zc::Own<ast::TupleTypeNode>> Parser::parseTupleType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseTupleType");

  // Parse tuple type according to the grammar rule:
  //
  // tupleType: LPAREN tupleElementTypes? RPAREN
  //
  // This handles tuple types like (T, U, V) for function parameters and return types
  // Each element can be a named tuple element: (name: T, other: U)

  source::SourceLoc loc = currentLoc();

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

      auto type = parseType();
      ZC_IF_SOME(n, zc::mv(name)) {
        elements.add(ast::factory::createNamedTupleElement(zc::mv(n), zc::mv(type)));
      }
      else { elements.add(zc::mv(type)); }
    } while (consumeExpectedToken(ast::SyntaxKind::Comma));
  }

  if (!consumeExpectedToken(ast::SyntaxKind::RightParen)) { return zc::none; }

  return finishNode(ast::factory::createTupleType(zc::mv(elements)), loc);
}

zc::Maybe<zc::Own<ast::TypeReferenceNode>> Parser::parseTypeReference() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseTypeReference");

  // typeReference: typeName typeArguments?
  //
  // This handles type references like MyType or generic types like MyType<T, U>
  // where typeArguments are optional type parameters in angle brackets

  source::SourceLoc loc = currentLoc();

  if (!isIdentifier()) { return zc::none; }
  auto typeName = parseIdentifier();

  zc::Maybe<zc::Vector<zc::Own<ast::TypeNode>>> typeArguments = zc::none;
  if (expectToken(ast::SyntaxKind::LessThan)) {
    zc::Vector<zc::Own<ast::TypeNode>> args;
    nextToken();  // consume '<'
    while (!expectToken(ast::SyntaxKind::GreaterThan)) {
      args.add(parseType());
      if (!expectToken(ast::SyntaxKind::GreaterThan) &&
          !consumeExpectedToken(ast::SyntaxKind::Comma)) {
        break;
      }
    }
    if (!consumeExpectedToken(ast::SyntaxKind::GreaterThan)) { return zc::none; }
    typeArguments = zc::mv(args);
  }

  return finishNode(ast::factory::createTypeReference(zc::mv(typeName), zc::mv(typeArguments)),
                    loc);
}

zc::Maybe<zc::Own<ast::PredefinedTypeNode>> Parser::parsePredefinedType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parsePredefinedType");

  // Parse predefined type according to the grammar rule:
  // predefinedType: I8 | I32 | I64 | U8 | U16 | U32 | U64 | F32 | F64 | STR | BOOL | NIL | UNIT
  // These are the built-in primitive types in the ZOM language

  const lexer::Token& token = currentToken();
  const source::SourceLoc loc = token.getLocation();

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
      zc::StringPtr typeName = token.getValue();
      nextToken();
      return finishNode(ast::factory::createPredefinedType(typeName), loc);
    }

    default:
      return zc::none;
  }
}

zc::Maybe<zc::Own<ast::Pattern>> Parser::parsePattern() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parsePattern");

  switch (currentKind()) {
    case ast::SyntaxKind::Underscore:
      return parseWildcardPattern();
    case ast::SyntaxKind::Identifier:
      return parseIdentifierPattern();
    case ast::SyntaxKind::LeftParen:
      return parseTuplePattern();
    case ast::SyntaxKind::LeftBrace:
      return parseStructurePattern();
    case ast::SyntaxKind::LeftBracket:
      return parseArrayPattern();
    case ast::SyntaxKind::IsKeyword:
      return parseIsPattern();
    default:
      return ast::factory::createExpressionPattern(parseExpression());
  }
}

zc::Maybe<zc::Own<ast::Pattern>> Parser::parseWildcardPattern() {
  const source::SourceLoc loc = currentLoc();
  nextToken();  // consume '_'

  zc::Maybe<zc::Own<ast::TypeNode>> type;
  if (consumeExpectedToken(ast::SyntaxKind::Colon)) { type = parseType(); }

  // TODO: Handle type annotation for wildcard patterns
  static_cast<void>(type);
  return finishNode(ast::factory::createWildcardPattern(), loc);
}

zc::Maybe<zc::Own<ast::Pattern>> Parser::parseIdentifierPattern() {
  auto id = parseIdentifier();
  zc::Maybe<zc::Own<ast::TypeNode>> type;
  if (consumeExpectedToken(ast::SyntaxKind::Colon)) {
    type = parseType();
    static_cast<void>(type);
  }
  return ast::factory::createIdentifierPattern(zc::mv(id));
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
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseStructurePattern");
  source::SourceLoc loc = currentLoc();

  if (!consumeExpectedToken(ast::SyntaxKind::LeftBrace)) { return zc::none; }

  zc::Vector<zc::Own<ast::Pattern>> properties;
  while (!expectToken(ast::SyntaxKind::RightBrace)) {
    source::SourceLoc propStartLoc = currentLoc();
    auto name = parseIdentifier();

    zc::Maybe<zc::Own<ast::Pattern>> nestedPattern = zc::none;
    if (consumeExpectedToken(ast::SyntaxKind::Colon)) {
      // Structure pattern properties are type annotations in Zom
      // e.g. { x: i32, y: str }
      nestedPattern = ast::factory::createIsPattern(parseType());
    }

    auto prop = finishNode(ast::factory::createPatternProperty(zc::mv(name), zc::mv(nestedPattern)),
                           propStartLoc);
    properties.add(zc::mv(prop));

    if (!expectToken(ast::SyntaxKind::RightBrace) &&
        !consumeExpectedToken(ast::SyntaxKind::Comma)) {
      break;
    }
  }

  if (!consumeExpectedToken(ast::SyntaxKind::RightBrace)) { return zc::none; }

  return finishNode(ast::factory::createStructurePattern(zc::mv(properties)), loc);
}

zc::Maybe<zc::Own<ast::Pattern>> Parser::parseArrayPattern() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseArrayPattern");
  source::SourceLoc loc = currentLoc();

  if (!consumeExpectedToken(ast::SyntaxKind::LeftBracket)) { return zc::none; }

  zc::Vector<zc::Own<ast::Pattern>> elements;
  while (!expectToken(ast::SyntaxKind::RightBracket)) {
    ZC_IF_SOME(pattern, parsePattern()) { elements.add(zc::mv(pattern)); }
    if (!expectToken(ast::SyntaxKind::RightBracket) &&
        !consumeExpectedToken(ast::SyntaxKind::Comma)) {
      break;
    }
  }

  if (!consumeExpectedToken(ast::SyntaxKind::RightBracket)) { return zc::none; }
  return finishNode(ast::factory::createArrayPattern(zc::mv(elements)), loc);
}

zc::Maybe<zc::Own<ast::Pattern>> Parser::parseIsPattern() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseIsPattern");
  source::SourceLoc loc = currentLoc();

  if (!consumeExpectedToken(ast::SyntaxKind::IsKeyword)) { return zc::none; }

  return finishNode(ast::factory::createIsPattern(parseType()), loc);
}

zc::OneOf<zc::Own<ast::BindingPattern>, zc::Own<ast::Identifier>>
Parser::parseIdentifierOrBindingPattern() {
  if (expectToken(ast::SyntaxKind::LeftBracket)) { return parseArrayBindingPattern(); }
  if (expectToken(ast::SyntaxKind::LeftBrace)) { return parseObjectBindingPattern(); }
  return parseBindingIdentifier();
}

zc::Maybe<zc::Own<ast::BindingPattern>> Parser::parseBindingPattern() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBindingPattern");

  switch (currentKind()) {
    case ast::SyntaxKind::LeftBracket:
      return parseArrayBindingPattern();
    case ast::SyntaxKind::LeftBrace:
      return parseObjectBindingPattern();
    default:
      return zc::none;
  }
}

zc::Maybe<zc::Own<ast::BindingElement>> Parser::parseArrayBindingElement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseArrayBindingElement");

  const source::SourceLoc pos = currentLoc();

  auto dotDotDotToken = parseOptionalToken(ast::SyntaxKind::DotDotDot);
  auto name = parseIdentifierOrBindingPattern();
  auto initializer = parseInitializer();

  return finishNode(ast::factory::createBindingElement(zc::mv(dotDotDotToken), zc::none,
                                                       zc::mv(name), zc::mv(initializer)),
                    pos);
}

zc::Own<ast::BindingPattern> Parser::parseArrayBindingPattern() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseArrayBindingPattern");

  const source::SourceLoc loc = currentLoc();

  parseExpected(ast::SyntaxKind::LeftBracket);

  auto elements = parseDelimitedList<ast::BindingElement>(
      ParsingContext::ArrayBindingElements, ZC_BIND_METHOD(*this, parseArrayBindingElement));

  parseExpected(ast::SyntaxKind::RightBracket);

  return finishNode(ast::factory::createArrayBindingPattern(zc::mv(elements)), loc);
}

zc::Maybe<zc::Own<ast::BindingElement>> Parser::parseObjectBindingElement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseObjectBindingElement");

  const source::SourceLoc loc = currentLoc();
  auto dotDotDotToken = parseOptionalToken(ast::SyntaxKind::DotDotDot);
  bool tokenIsIdentifier = isBindingIdentifier();
  zc::Maybe<zc::Own<ast::Identifier>> propertyName = parsePropertyName();
  zc::OneOf<zc::Own<ast::Identifier>, zc::Own<ast::BindingPattern>> nameOrPattern;

  if (tokenIsIdentifier && !expectToken(ast::SyntaxKind::Colon)) {
    ZC_IF_SOME(name, propertyName) { nameOrPattern = zc::mv(name); }
    else { nameOrPattern = ast::factory::createMissingIdentifier(); }
    propertyName = zc::none;
  } else {
    parseExpected(ast::SyntaxKind::Colon);
    nameOrPattern = parseIdentifierOrBindingPattern();
  }

  zc::Maybe<zc::Own<ast::Expression>> initializer = parseInitializer();
  return finishNode(ast::factory::createBindingElement(zc::mv(dotDotDotToken), zc::mv(propertyName),
                                                       zc::mv(nameOrPattern), zc::mv(initializer)),
                    loc);
}

zc::Own<ast::BindingPattern> Parser::parseObjectBindingPattern() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseObjectBindingPattern");
  const source::SourceLoc pos = currentLoc();

  parseExpected(ast::SyntaxKind::LeftBrace);

  auto elements = parseDelimitedList<ast::BindingElement>(
      ParsingContext::ObjectBindingElements, ZC_BIND_METHOD(*this, parseObjectBindingElement));

  parseExpected(ast::SyntaxKind::RightBrace);
  return finishNode(ast::factory::createObjectBindingPattern(zc::mv(elements)), pos);
}

zc::Own<ast::AwaitExpression> Parser::parseAwaitExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseAwaitExpression");

  // awaitExpression: AWAIT unaryExpression;

  const source::SourceLoc loc = currentLoc();
  nextToken();

  auto expr = parseSimpleUnaryExpression();
  return finishNode(ast::factory::createAwaitExpression(zc::mv(expr)), loc);
}

zc::Maybe<zc::Own<ast::DebuggerStatement>> Parser::parseDebuggerStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseDebuggerStatement");

  source::SourceLoc loc = currentLoc();
  if (!consumeExpectedToken(ast::SyntaxKind::DebuggerKeyword)) { return zc::none; }

  if (!consumeExpectedToken(ast::SyntaxKind::Semicolon)) { return zc::none; }

  return finishNode(ast::factory::createDebuggerStatement(), loc);
}

zc::Own<ast::NewExpression> Parser::parseNewExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseNewExpression");

  // newExpression: memberExpression | NEW newExpression;
  // memberExpression: (primaryExpression | superProperty | NEW memberExpression arguments)
  //                   (LBRACK expression RBRACK | PERIOD identifier)*;

  const source::SourceLoc loc = currentLoc();
  parseExpected(ast::SyntaxKind::NewKeyword);
  const source::SourceLoc exprLoc = currentLoc();

  // Use parseMemberExpressionRest to handle member access chains allowOptionalChain is set to false
  // for new expressions
  zc::Own<ast::LeftHandSideExpression> expression =
      parseMemberExpressionRest(parsePrimaryExpression(), exprLoc, false);

  zc::Maybe<zc::Vector<zc::Own<ast::TypeNode>>> typeArguments = zc::none;
  if (ast::isa<ast::ExpressionWithTypeArguments>(*expression)) {
    auto exprWithTypeArgs = ast::cast<ast::ExpressionWithTypeArguments>(zc::mv(expression));
    typeArguments = exprWithTypeArgs->takeTypeArguments();
    expression = exprWithTypeArgs->takeExpression();
  }
  // Check for invalid optional chain from new expression
  if (expectToken(ast::SyntaxKind::QuestionDot)) {
    parseErrorAtCurrentToken<diagnostics::DiagID::InvalidOptionalChainFromNewExpression>();
  }

  auto argumentList = expectToken(ast::SyntaxKind::LeftParen) ? parseArgumentList() : zc::none;
  return finishNode(ast::factory::createNewExpression(zc::mv(expression), zc::mv(typeArguments),
                                                      zc::mv(argumentList)),
                    loc);
}

zc::Own<ast::ParenthesizedExpression> Parser::parseParenthesizedExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseParenthesizedExpression");

  // parenthesizedExpression: LPAREN expression RPAREN;

  const source::SourceLoc loc = currentLoc();

  parseExpected(ast::SyntaxKind::LeftParen);

  auto expression = parseExpression();
  parseExpected(ast::SyntaxKind::RightParen);

  // Create a parenthesized expression with the parsed expression
  return finishNode(ast::factory::createParenthesizedExpression(zc::mv(expression)), loc);
}

zc::Own<ast::LeftHandSideExpression> Parser::parseMemberExpressionRest(
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
      expression = parsePropertyAccessExpressionRest(zc::mv(expression), questionDotToken, pos);
      continue;
    }

    // Check for element access: obj[expr] or obj?.[expr]
    if (parseOptional(ast::SyntaxKind::LeftBracket)) {
      expression = parseElementAccessExpressionRest(zc::mv(expression), questionDotToken, pos);
      continue;
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

  ZC_UNREACHABLE;
}

zc::Own<ast::MemberExpression> Parser::parseSuperExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseSuperExpression");

  const lexer::Token token = currentToken();
  const source::SourceLoc loc = token.getLocation();

  ZC_ASSERT(expectToken(ast::SyntaxKind::SuperKeyword));

  nextToken();  // consume 'super'

  // Create the super identifier as the base expression
  zc::Own<ast::Identifier> expression = ast::factory::createIdentifier("super"_zc);

  // Check for type arguments (e.g., super<T>)
  if (expectToken(ast::SyntaxKind::LessThan)) {
    source::SourceLoc errorLoc = currentLoc();
    impl->diagnosticEngine.diagnose<diagnostics::DiagID::InvalidCharacter>(errorLoc);
  }

  // Check what follows the super keyword
  if (expectToken(ast::SyntaxKind::LeftParen) || expectToken(ast::SyntaxKind::Period) ||
      expectToken(ast::SyntaxKind::LeftBracket)) {
    // Valid super usage - return the base expression
    // The caller will handle member access or call expressions
    return finishNode(zc::mv(expression), loc);
  }

  // If we reach here, super must be followed by '(', '.', or '['
  // Report an error and try to recover by parsing a dot
  source::SourceLoc errorLoc = currentLoc();
  impl->diagnosticEngine.diagnose<diagnostics::DiagID::InvalidCharacter>(errorLoc);

  // Try to recover by expecting a dot and parsing the right side
  if (expectToken(ast::SyntaxKind::Period)) {
    nextToken();  // consume '.'
    auto property = parseIdentifier();
    return finishNode(
        ast::factory::createPropertyAccessExpression(zc::mv(expression), zc::mv(property), false),
        loc);
  }

  return finishNode(zc::mv(expression), loc);
}

zc::Own<ast::MemberExpression> Parser::parseImportCallExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseImportCallExpression");

  const lexer::Token token = currentToken();
  const source::SourceLoc loc = token.getLocation();

  nextToken();  // consume 'import'

  // Create the import identifier as the base expression
  zc::Own<ast::Identifier> expression = ast::factory::createIdentifier("import"_zc);

  // Check for type arguments (e.g., import<T>)
  if (expectToken(ast::SyntaxKind::LessThan)) {
    source::SourceLoc errorLoc = currentLoc();
    impl->diagnosticEngine.diagnose<diagnostics::DiagID::InvalidCharacter>(errorLoc);
  }

  // Check what follows the import keyword
  if (expectToken(ast::SyntaxKind::LeftParen) || expectToken(ast::SyntaxKind::Period) ||
      expectToken(ast::SyntaxKind::LeftBracket)) {
    // Valid import usage - return the base expression
    // The caller will handle member access or call expressions
    return finishNode(zc::mv(expression), loc);
  }

  // If we reach here, import must be followed by '(', '.', or '['
  // Report an error and try to recover by parsing a dot
  source::SourceLoc errorLoc = currentLoc();
  impl->diagnosticEngine.diagnose<diagnostics::DiagID::InvalidCharacter>(errorLoc);

  // Try to recover by expecting a dot and parsing the right side
  if (expectToken(ast::SyntaxKind::Period)) {
    nextToken();  // consume '.'
    auto property = parseIdentifier();
    return finishNode(
        ast::factory::createPropertyAccessExpression(zc::mv(expression), zc::mv(property), false),
        loc);
  }

  return finishNode(zc::mv(expression), loc);
}

source::SourceLoc Parser::getFullStartLoc() const { return impl->lexer.getFullStartLoc(); }

source::SourceLoc Parser::getTokenStartLoc() const { return impl->lexer.getTokenStartLoc(); }

bool Parser::hasPrecedingLineBreak() const { return impl->lexer.hasPrecedingLineBreak(); }

// ================================================================================
// Literal parsing implementations

zc::Maybe<zc::Own<ast::StringLiteral>> Parser::parseStringLiteral() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseStringLiteral");

  const lexer::Token& token = currentToken();
  if (!token.is(ast::SyntaxKind::StringLiteral)) { return zc::none; }

  source::SourceLoc loc = token.getLocation();
  zc::StringPtr value = token.getValue();
  nextToken();

  return finishNode(ast::factory::createStringLiteral(value), loc);
}

zc::Maybe<zc::Own<ast::IntegerLiteral>> Parser::parseIntegerLiteral() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseIntegerLiteral");

  const lexer::Token& token = currentToken();
  if (!token.is(ast::SyntaxKind::IntegerLiteral)) { return zc::none; }

  source::SourceLoc loc = token.getLocation();
  zc::StringPtr value = token.getValue();
  nextToken();

  int64_t numValue = value.parseAs<int64_t>();
  return finishNode(ast::factory::createIntegerLiteral(numValue), loc);
}

zc::Maybe<zc::Own<ast::FloatLiteral>> Parser::parseFloatLiteral() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseFloatLiteral");

  const lexer::Token& token = currentToken();
  if (!token.is(ast::SyntaxKind::FloatLiteral)) { return zc::none; }

  source::SourceLoc loc = token.getLocation();
  zc::StringPtr value = token.getValue();
  nextToken();

  double numValue = value.parseAs<double>();
  return finishNode(ast::factory::createFloatLiteral(numValue), loc);
}

zc::Maybe<zc::Own<ast::BooleanLiteral>> Parser::parseBooleanLiteral() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBooleanLiteral");

  const lexer::Token& token = currentToken();
  if (!token.is(ast::SyntaxKind::TrueKeyword) && !token.is(ast::SyntaxKind::FalseKeyword)) {
    return zc::none;
  }

  source::SourceLoc loc = token.getLocation();
  bool value = token.is(ast::SyntaxKind::TrueKeyword);
  nextToken();

  return finishNode(ast::factory::createBooleanLiteral(value), loc);
}

zc::Maybe<zc::Own<ast::NullLiteral>> Parser::parseNullLiteral() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseNullLiteral");

  const lexer::Token& token = currentToken();
  if (!token.is(ast::SyntaxKind::NullKeyword)) { return zc::none; }

  source::SourceLoc loc = token.getLocation();
  nextToken();

  return finishNode(ast::factory::createNullLiteral(), loc);
}

zc::Own<ast::FunctionExpression> Parser::parseFunctionExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseFunctionExpression");

  // functionExpression:
  //   FUN callSignature LBRACE functionBody RBRACE;
  //
  // callSignature:
  //   typeParameters? parameterClause (ARROW type raisesClause?)?;

  const lexer::Token& token = currentToken();
  source::SourceLoc loc = token.getLocation();

  parseOptional(ast::SyntaxKind::FunKeyword);

  // Parse callSignature
  auto typeParameters = parseTypeParameters();
  // Parse parameter list
  auto parameters = parseParameters();
  // Parse capture clause
  auto captures = parseCaptureClause();
  // Parse optional return type or error return clause
  auto returnType = parseReturnType();

  auto body = parseFunctionBlock();

  return finishNode(
      ast::factory::createFunctionExpression(zc::mv(typeParameters), zc::mv(parameters),
                                             zc::mv(captures), zc::mv(returnType), zc::mv(body)),
      loc);
}

zc::Maybe<zc::Own<ast::TypeParameterDeclaration>> Parser::parseTypeParameter() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseTypeParameter");

  // typeParameter: identifier constraint?;
  // constraint: EXTENDS type;

  source::SourceLoc loc = currentLoc();

  if (!isIdentifier()) { return zc::none; }
  auto name = parseIdentifier();

  zc::Maybe<zc::Own<ast::TypeNode>> constraint = zc::none;
  if (expectToken(ast::SyntaxKind::ExtendsKeyword)) {
    nextToken();
    constraint = parseType();
  }

  return finishNode(ast::factory::createTypeParameterDeclaration(zc::mv(name), zc::mv(constraint)),
                    loc);
}

zc::Maybe<zc::Vector<zc::Own<ast::TypeParameterDeclaration>>> Parser::parseTypeParameters() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseTypeParameters");

  if (!expectToken(ast::SyntaxKind::LessThan)) { return zc::none; }

  return parseBracketedList<ast::TypeParameterDeclaration>(
      ParsingContext::TypeParameters, ZC_BIND_METHOD(*this, parseTypeParameter),
      ast::SyntaxKind::LessThan, ast::SyntaxKind::GreaterThan);
}

zc::Maybe<zc::Own<ast::ParameterDeclaration>> Parser::parseParameterDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseParameterDeclaration");

  const source::SourceLoc loc = currentLoc();

  auto modifiers = parseModifiers(/*allowDecorators*/ true);
  auto dotDotDotToken = parseOptionalToken(ast::SyntaxKind::DotDotDot);

  return finishNode(
      ast::factory::createParameterDeclaration(
          zc::mv(modifiers), zc::mv(dotDotDotToken), parseIdentifierOrBindingPattern(),
          parseOptionalToken(ast::SyntaxKind::Question), parseTypeAnnotation(), parseInitializer()),
      loc);
}

zc::Vector<zc::Own<ast::ParameterDeclaration>> Parser::parseParameters() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseParameters");

  if (ZC_LIKELY(parseExpected(ast::SyntaxKind::LeftParen))) {
    auto parameters = parseDelimitedList<ast::ParameterDeclaration>(
        ParsingContext::Parameters, ZC_BIND_METHOD(*this, parseParameterDeclaration));
    parseExpected(ast::SyntaxKind::RightParen);
    return zc::mv(parameters);
  }

  return parseEmptyNodeList<ast::ParameterDeclaration>();
}

zc::Vector<zc::Own<ast::CaptureElement>> Parser::parseCaptureClause() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseCaptureClause");
  zc::Vector<zc::Own<ast::CaptureElement>> captures;

  if (expectToken(ast::SyntaxKind::Identifier) && currentToken().getValue() == "use"_zc) {
    nextToken();  // consume 'use'

    if (consumeExpectedToken(ast::SyntaxKind::LeftBracket)) {
      if (!expectToken(ast::SyntaxKind::RightBracket)) {
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
  source::SourceLoc loc = currentLoc();

  if (expectToken(ast::SyntaxKind::Ampersand)) {
    isByReference = true;
    nextToken();
  }

  if (expectToken(ast::SyntaxKind::ThisKeyword)) {
    nextToken();
    return finishNode(ast::factory::createCaptureElement(isByReference, zc::none, true), loc);
  } else if (expectToken(ast::SyntaxKind::Identifier)) {
    auto text = currentToken().getValue();
    auto id = ast::factory::createIdentifier(text);
    nextToken();
    return finishNode(ast::factory::createCaptureElement(isByReference, zc::mv(id), false), loc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::ReturnTypeNode>> Parser::parseReturnType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseReturnType");

  if (expectToken(ast::SyntaxKind::Arrow)) { return parseRequiredReturnType(); }
  return zc::none;
}

zc::Own<ast::ReturnTypeNode> Parser::parseRequiredReturnType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseRequiredReturnType");

  // Parse optional return type or error return clause
  //
  // callSignature:
  //   typeParameters? parameterClause (ARROW type raisesClause?)?;

  const lexer::Token token = currentToken();
  const source::SourceLoc loc = token.getLocation();

  parseExpected(ast::SyntaxKind::Arrow);

  auto type = parseType();

  zc::Maybe<zc::Own<ast::TypeNode>> errorType = zc::none;
  if (consumeExpectedToken(ast::SyntaxKind::RaisesKeyword)) { errorType = parseType(); }

  return finishNode(ast::factory::createReturnType(zc::mv(type), zc::mv(errorType)), loc);
}

zc::Own<ast::VariableStatement> Parser::parseVariableStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseVariableStatement");

  // variableStatement: variableDeclarationList ';'
  // This handles variable declarations like: let x = 5; or const y: i32 = 10;

  source::SourceLoc loc = currentLoc();
  auto declarationList = parseVariableDeclarationList();
  parseSemicolon();

  return finishNode(ast::factory::createVariableStatement(zc::mv(declarationList)), loc);
}

zc::Own<ast::Identifier> Parser::parseRightSideOfDot(
    bool allowIdentifierNames, bool allowUnicodeEscapeSequenceInIdentifierName) {
  // Technically a keyword is valid here as all identifiers and keywords are identifier names.
  // However, often we'll encounter this in error situations when the identifier or keyword
  // is actually starting another valid construct.
  //
  // So, we check for the following specific case:
  //
  //      name.
  //      identifierOrKeyword identifierNameOrKeyword
  //
  // Note: the newlines are important here.  For example, if that above code
  // were rewritten into:
  //
  //      name.identifierOrKeyword
  //      identifierNameOrKeyword
  //
  // Then we would consider it valid.  That's because ASI would take effect and
  // the code would be implicitly: "name.identifierOrKeyword; identifierNameOrKeyword".
  // In the first case though, ASI will not take effect because there is not a
  // line terminator after the identifier or keyword.
  if (hasPrecedingLineBreak() && tokenIsIdentifierOrKeyword(currentToken())) {
    const bool matchesPattern =
        lookAhead<bool>(ZC_BIND_METHOD(*this, nextTokenIsIdentifierOrKeywordOnSameLine));

    if (matchesPattern) {
      // Report that we need an identifier.  However, report it right after the dot,
      // and not on the next token.  This is because the next token might actually
      // be an identifier and the error would be quite confusing.
      parseErrorAt<diagnostics::DiagID::IdentifierExpected>(currentLoc(), currentLoc());
      return ast::factory::createMissingIdentifier();
    }
  }

  if (allowIdentifierNames) {
    return allowUnicodeEscapeSequenceInIdentifierName
               ? parseIdentifierName()
               : parseIdentifierNameErrorOnUnicodeEscapeSequence();
  }

  return parseIdentifier();
}

zc::Own<ast::PropertyAccessExpression> Parser::parsePropertyAccessExpressionRest(
    zc::Own<ast::LeftHandSideExpression> expression, bool questionDot, source::SourceLoc pos) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser,
                                 "parsePropertyAccessExpressionRest");

  auto name = parseRightSideOfDot(/*allowIdentifierNames*/ true,
                                  /*allowUnicodeEscapeSequenceInIdentifierName*/ true);
  const bool isOptionalChain = questionDot || tryReparseOptionalChain(*expression);

  if (ast::isa<ast::ExpressionWithTypeArguments>(*expression)) {
    const auto& typeArguments =
        ast::cast<ast::ExpressionWithTypeArguments>(*expression).getTypeArguments();

    ZC_IF_SOME(t, typeArguments) {
      const auto tbp = t.begin()->getSourceRange().getStart() - 1;
      const auto tep = t.back()->getSourceRange().getEnd() + 1;
      parseErrorAtRange<
          diagnostics::DiagID::AnInstantiationExpressionCannotBeFollowedByAPropertyAccess>(
          source::SourceRange(tbp, tep));
    }
  }

  auto propertyAccess = ast::factory::createPropertyAccessExpression(
      zc::mv(expression), zc::mv(name), questionDot, isOptionalChain);

  return finishNode(zc::mv(propertyAccess), pos);
}

bool Parser::tryReparseOptionalChain(ast::LeftHandSideExpression& node) {
  if (ast::hasFlag(node.getFlags(), ast::NodeFlags::OptionalChain)) { return true; }
  if (!ast::isa<ast::NonNullExpression>(node)) { return false; }

  const auto boundary =
      findOptionalChainBoundary(ast::cast<ast::NonNullExpression>(node).getExpression());
  if (boundary == zc::none) { return false; }

  markNonNullOptionalChain(node);
  return true;
}

zc::Own<ast::ElementAccessExpression> Parser::parseElementAccessExpressionRest(
    zc::Own<ast::LeftHandSideExpression> expression, bool questionDot, source::SourceLoc pos) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseElementAccessExpression");

  zc::Maybe<zc::Own<ast::Expression>> assignmentExpression;
  if (expectToken(ast::SyntaxKind::RightBracket)) {
    // Error: expected expression inside brackets
    assignmentExpression = ast::factory::createMissingIdentifier();
  }

  // Parse the index expression inside brackets
  auto indexExpression = parseExpression();
  parseExpected(ast::SyntaxKind::RightBracket);

  const bool isOptionalChain = questionDot || tryReparseOptionalChain(*expression);
  return finishNode(ast::factory::createElementAccessExpression(
                        zc::mv(expression), zc::mv(indexExpression), questionDot, isOptionalChain),
                    pos);
}

zc::Maybe<zc::Own<ast::TokenNode>> Parser::parseExpectedToken(ast::SyntaxKind kind) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseExpectedToken");

  // parseExpectedToken: consume a token of the expected kind and create a TokenNode
  // This is used when we need to parse a specific token and create an AST node for it

  if (!expectToken(kind)) {
    // Error: expected token of kind 'kind' but found something else
    return zc::none;
  }

  source::SourceLoc loc = currentLoc();
  nextToken();

  return finishNode(ast::factory::createTokenNode(kind), loc);
}

bool Parser::parseOptional(ast::SyntaxKind kind) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseOptional");

  // parseOptional: consume a token of the expected kind if it exists
  // This is used when we want to parse a token if it exists, but don't care about the result

  if (!expectToken(kind)) { return false; }

  nextToken();
  return true;
}

zc::Maybe<zc::Own<ast::TokenNode>> Parser::parseOptionalToken(ast::SyntaxKind kind) {
  if (expectToken(kind)) { return parseTokenNode(); }
  return zc::none;
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

zc::Own<ast::ClassElement> Parser::parseClassElement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseClassElement");

  const source::SourceLoc loc = currentLoc();

  if (expectToken(ast::SyntaxKind::Semicolon)) {
    nextToken();
    return finishNode(ast::factory::createSemicolonClassElement(), loc);
  }

  zc::Vector<ast::SyntaxKind> modifiers = parseModifiers(/*allowDecorators*/ true,
                                                         /*permitConstAsModifier*/ false,
                                                         /*stopOnStartOfClassStaticBlock*/ true);

  if (expectToken(ast::SyntaxKind::InitKeyword)) {
    return parseInitDeclaration(loc, zc::mv(modifiers));
  }
  if (expectToken(ast::SyntaxKind::DeinitKeyword)) {
    return parseDeinitDeclaration(loc, zc::mv(modifiers));
  }
  if (expectNToken(ast::SyntaxKind::GetKeyword, ast::SyntaxKind::SetKeyword)) {
    return parseAccessorDeclaration(loc, zc::mv(modifiers), currentKind());
  }

  if (!parseNOptional(ast::SyntaxKind::FunKeyword, ast::SyntaxKind::LetKeyword,
                      ast::SyntaxKind::ConstKeyword)) {
    parseErrorAtCurrentToken<diagnostics::DiagID::ExpectedToken>(
        "let', 'const', 'fun', 'init', 'deinit', 'get', or 'set"_zc);
  }

  if (tokenIsIdentifierOrKeyword(currentToken())) {
    return parsePropertyOrMethodDeclaration(loc, zc::mv(modifiers));
  }

  if (!modifiers.empty()) {
    // Treat this as a property declaration with a missing name.
    auto name = ast::factory::createMissingIdentifier();
    parseErrorAt<diagnostics::DiagID::DeclarationExpected>(getFullStartLoc(), getFullStartLoc());
    return parsePropertyDeclaration(loc, zc::mv(modifiers), zc::mv(name), zc::none);
  }

  // 'isStartOfClassMember' should have hinted not to attempt parsing.
  ZC_FAIL_ASSERT("Should not have attempted to parse class member declaration.");
  ZC_UNREACHABLE;
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
    result.add(currentKind());
    nextToken();
  }
  return result;
}

zc::Own<ast::ClassElement> Parser::parsePropertyOrMethodDeclaration(
    source::SourceLoc loc, zc::Vector<ast::SyntaxKind> modifiers) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parsePropertyOrMethodDeclaration");

  auto name = parseIdentifierName();
  zc::Maybe<zc::Own<ast::TokenNode>> questionToken = parseOptionalToken(ast::SyntaxKind::Question);

  if (expectNToken(ast::SyntaxKind::LeftParen, ast::SyntaxKind::LessThan)) {
    return parseMethodDeclaration(loc, zc::mv(modifiers), zc::mv(name), zc::mv(questionToken));
  }

  return parsePropertyDeclaration(loc, zc::mv(modifiers), zc::mv(name), zc::mv(questionToken));
}

zc::Own<ast::ClassElement> Parser::parsePropertyDeclaration(
    source::SourceLoc loc, zc::Vector<ast::SyntaxKind> modifiers, zc::Own<ast::Identifier> name,
    zc::Maybe<zc::Own<ast::TokenNode>> questionToken) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parsePropertyDeclaration");

  zc::Maybe<zc::Own<ast::TypeNode>> type = zc::none;
  type = parseTypeAnnotation();
  zc::Maybe<zc::Own<ast::Expression>> init = parseInitializer();

  parseSemicolonAfterPropertyName(name, type, init);
  auto propDecl = ast::factory::createPropertyDeclaration(zc::mv(modifiers), zc::mv(name),
                                                          zc::mv(type), zc::mv(init));
  return finishNode(zc::mv(propDecl), loc);
}

zc::Own<ast::ClassElement> Parser::parseMethodDeclaration(
    source::SourceLoc loc, zc::Vector<ast::SyntaxKind> modifiers, zc::Own<ast::Identifier> name,
    zc::Maybe<zc::Own<ast::TokenNode>> questionToken) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseMethodDeclaration");

  auto typeParameters = parseTypeParameters();
  auto parameters = parseParameters();
  auto returnType = parseReturnType();

  auto body = parseFunctionBlockOrSemicolon();
  return finishNode(
      ast::factory::createMethodDeclaration(zc::mv(modifiers), zc::mv(name), zc::mv(questionToken),
                                            zc::mv(typeParameters), zc::mv(parameters),
                                            zc::mv(returnType), zc::mv(body)),
      loc);
}

zc::Own<ast::ClassElement> Parser::parseInitDeclaration(source::SourceLoc loc,
                                                        zc::Vector<ast::SyntaxKind> modifiers) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseInitDeclaration");

  nextToken();

  auto typeParameters = parseTypeParameters();
  auto parameters = parseParameters();
  auto returnType = parseReturnType();
  auto body = parseFunctionBlockOrSemicolon();

  return finishNode(
      ast::factory::createInitDeclaration(zc::mv(modifiers), zc::mv(typeParameters),
                                          zc::mv(parameters), zc::mv(returnType), zc::mv(body)),
      loc);
}

zc::Own<ast::ClassElement> Parser::parseDeinitDeclaration(source::SourceLoc loc,
                                                          zc::Vector<ast::SyntaxKind> modifiers) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseDeinitDeclaration");

  nextToken();
  auto body = parseFunctionBlockOrSemicolon();

  return finishNode(ast::factory::createDeinitDeclaration(zc::mv(modifiers), zc::mv(body)), loc);
}

zc::Own<ast::ClassElement> Parser::parseAccessorDeclaration(source::SourceLoc loc,
                                                            zc::Vector<ast::SyntaxKind> modifiers,
                                                            ast::SyntaxKind accessorKind) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseAccessorDeclaration");

  nextToken();

  auto name = parsePropertyName();
  auto typeParameters = parseTypeParameters();
  auto parameters = parseParameters();
  auto returnType = parseReturnType();
  auto body = parseFunctionBlockOrSemicolon();

  if (accessorKind == ast::SyntaxKind::GetKeyword) {
    return finishNode(ast::factory::createGetAccessorDeclaration(
                          zc::mv(modifiers), zc::mv(name), zc::mv(typeParameters),
                          zc::mv(parameters), zc::mv(returnType), zc::mv(body)),
                      loc);
  } else {
    return finishNode(ast::factory::createSetAccessorDeclaration(
                          zc::mv(modifiers), zc::mv(name), zc::mv(typeParameters),
                          zc::mv(parameters), zc::mv(returnType), zc::mv(body)),
                      loc);
  }
}

zc::Vector<zc::Own<ast::ClassElement>> Parser::parseClassOrStructMembers(bool isStruct) {
  return parseList<ast::ClassElement>(
      isStruct ? ParsingContext::StructMembers : ParsingContext::ClassMembers,
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

  const source::SourceLoc loc = currentLoc();

  if (expectToken(ast::SyntaxKind::Semicolon)) {
    nextToken();
    return finishNode(ast::factory::createSemicolonInterfaceElement(), loc);
  }

  auto modifiers = parseModifiers(/*allowDecorators*/ true,
                                  /*permitConstAsModifier*/ false,
                                  /*stopOnStartOfClassStaticBlock*/ false);

  if (!parseNOptional(ast::SyntaxKind::LetKeyword, ast::SyntaxKind::ConstKeyword,
                      ast::SyntaxKind::FunKeyword)) {
    parseErrorAtCurrentToken<diagnostics::DiagID::ExpectedToken>("let', 'const', or 'fun"_zc);
  }

  if (tokenIsIdentifierOrKeyword(currentToken())) {
    return parsePropertyOrMethodSignature(loc, zc::mv(modifiers));
  }

  // Treat this as a property signature with a missing name.
  auto name = ast::factory::createMissingIdentifier();
  parseErrorAt<diagnostics::DiagID::IdentifierExpected>(getFullStartLoc(), getFullStartLoc());
  return parsePropertySignature(loc, zc::mv(modifiers), zc::mv(name), zc::none);
}

zc::Maybe<zc::Own<ast::InterfaceElement>> Parser::parsePropertyOrMethodSignature(
    source::SourceLoc loc, zc::Vector<ast::SyntaxKind> modifiers) {
  auto name = parsePropertyName();
  zc::Maybe<zc::Own<ast::TokenNode>> questionToken = parseOptionalToken(ast::SyntaxKind::Question);

  if (expectNToken(ast::SyntaxKind::LeftParen, ast::SyntaxKind::LessThan)) {
    auto typeParameters = parseTypeParameters();
    auto parameters = parseParameters();
    auto type = parseReturnType();
    auto method = finishNode(ast::factory::createMethodSignature(
                                 zc::mv(modifiers), zc::mv(name), zc::mv(questionToken),
                                 zc::mv(typeParameters), zc::mv(parameters), zc::mv(type)),
                             loc);
    parseSemicolon();
    return zc::mv(method);
  } else {
    auto type = parseTypeAnnotation();
    auto initializer = parseInitializer();
    auto prop = finishNode(ast::factory::createPropertySignature(zc::mv(modifiers), zc::mv(name),
                                                                 zc::mv(questionToken),
                                                                 zc::mv(type), zc::mv(initializer)),
                           loc);
    parseSemicolon();
    return zc::mv(prop);
  }
}

zc::Maybe<zc::Own<ast::InterfaceElement>> Parser::parsePropertySignature(
    source::SourceLoc start, zc::Vector<ast::SyntaxKind> modifiers, zc::Own<ast::Identifier> name,
    zc::Maybe<zc::Own<ast::TokenNode>> questionToken) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parsePropertySignature");

  auto type = parseTypeAnnotation();
  auto initializer = parseInitializer();
  parseSemicolonAfterPropertyName(name, type, initializer);

  auto prop = finishNode(
      ast::factory::createPropertySignature(zc::mv(modifiers), zc::mv(name), zc::mv(questionToken),
                                            zc::mv(type), zc::mv(initializer)),
      start);
  zc::Own<ast::InterfaceElement> node = zc::mv(prop);
  return zc::mv(node);
}

zc::Maybe<zc::Own<ast::InterfaceElement>> Parser::parseMethodSignature() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseMethodSignature");

  const source::SourceLoc loc = currentLoc();

  if (!isLiteralPropertyName()) { return zc::none; }

  auto name = parseIdentifierName();
  auto questionToken = parseOptionalToken(ast::SyntaxKind::Question);
  auto typeParameters = parseTypeParameters();

  if (!expectToken(ast::SyntaxKind::LeftParen)) { return zc::none; }
  auto parameters = parseParameters();

  zc::Maybe<zc::Own<ast::ReturnTypeNode>> returnType = zc::none;
  if (expectToken(ast::SyntaxKind::Arrow)) { returnType = parseReturnType(); }

  zc::Vector<ast::SyntaxKind> modifiers;
  auto fn = finishNode(ast::factory::createMethodSignature(
                           zc::mv(modifiers), zc::mv(name), zc::mv(questionToken),
                           zc::mv(typeParameters), zc::mv(parameters), zc::mv(returnType)),
                       loc);
  if (expectToken(ast::SyntaxKind::Equals)) {
    parseErrorAtCurrentToken<diagnostics::DiagID::InterfaceMethodSignatureInitializerNotAllowed>();
    parseInitializer();
  }
  if (canParseSemicolon()) { tryParseSemicolon(); }
  zc::Own<ast::InterfaceElement> node = zc::mv(fn);
  return zc::mv(node);

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

zc::Own<ast::HeritageClause> Parser::parseHeritageClause() {
  source::SourceLoc loc = currentLoc();
  ast::SyntaxKind token = currentKind();
  nextToken();

  zc::Vector<zc::Own<ast::ExpressionWithTypeArguments>> types =
      parseDelimitedList<ast::ExpressionWithTypeArguments>(
          ParsingContext::HeritageClauseElement,
          ZC_BIND_METHOD(*this, parseExpressionWithTypeArguments));

  return finishNode(ast::factory::createHeritageClause(token, zc::mv(types)), loc);
}

bool Parser::scanStartOfDeclaration() {
  while (true) {
    switch (currentKind()) {
      case ast::SyntaxKind::LetKeyword:
      case ast::SyntaxKind::ConstKeyword:
      case ast::SyntaxKind::FunKeyword:
      case ast::SyntaxKind::ClassKeyword:
      case ast::SyntaxKind::EnumKeyword:
      case ast::SyntaxKind::AliasKeyword:
        return true;

      case ast::SyntaxKind::UsingKeyword:
        return isUsingDeclaration();

      case ast::SyntaxKind::AwaitKeyword:
        return isAwaitUsingDeclaration();

      case ast::SyntaxKind::InterfaceKeyword:
      case ast::SyntaxKind::TypeKeyword:
      case ast::SyntaxKind::ModuleKeyword:
      case ast::SyntaxKind::NamespaceKeyword:
        return nextTokenIsIdentifierOnSameLine();

      case ast::SyntaxKind::AbstractKeyword:
      case ast::SyntaxKind::AccessorKeyword:
      case ast::SyntaxKind::AsyncKeyword:
      case ast::SyntaxKind::DeclareKeyword:
      case ast::SyntaxKind::PrivateKeyword:
      case ast::SyntaxKind::ProtectedKeyword:
      case ast::SyntaxKind::PublicKeyword:
      case ast::SyntaxKind::ReadonlyKeyword:
      case ast::SyntaxKind::OverrideKeyword: {
        ast::SyntaxKind previousTokenKind = currentKind();
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
        return expectNToken(ast::SyntaxKind::LeftBrace, ast::SyntaxKind::Identifier,
                            ast::SyntaxKind::ExportKeyword);

      case ast::SyntaxKind::ImportKeyword:
        nextToken();
        return isIdentifier() && !currentToken().hasPrecedingLineBreak();

      case ast::SyntaxKind::ExportKeyword:
        nextToken();
        if (currentToken().hasPrecedingLineBreak()) { return false; }
        if (currentToken().is(ast::SyntaxKind::LeftBrace)) { return true; }
        if (isIdentifier()) { return true; }
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
