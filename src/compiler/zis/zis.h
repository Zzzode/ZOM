#pragma once

#include <memory>
#include <vector>
#include <string>

struct ASTNode {
  virtual ~ASTNode() = default;
};

struct Expression : ASTNode {};

struct Statement : ASTNode {};

struct BinaryExpression : Expression {
  std::unique_ptr<Expression> left;
  std::string op;
  std::unique_ptr<Expression> right;
};

struct VariableDeclaration : Statement {
  std::string type;
  std::string name;
  std::unique_ptr<Expression> initializer;
};

// Add more AST node types as needed