#ifndef ZOM_ZIS_ZIS_H_
#define ZOM_ZIS_ZIS_H_

#include "zc/core/string.h"

namespace zomlang {
namespace compiler {
namespace zis {

class ZIS {
public:
  virtual ~ZIS() noexcept = default;
};

class Expression : public ZIS {
public:
  ~Expression() noexcept override = default;
};

class Statement : public ZIS {
public:
  ~Statement() noexcept override = default;
};

class BinaryExpression : public Expression {
public:
  ~BinaryExpression() noexcept override = default;

private:
  zc::Own<Expression> left;
  zc::String op;
  zc::Own<Expression> right;
};

class VariableDeclaration : public Statement {
public:
  ~VariableDeclaration() noexcept override = default;

  zc::StringPtr getType() const { return type; }

  zc::StringPtr getName() const { return name; }

private:
  zc::String type;
  zc::String name;
  zc::Own<Expression> initializer;
};

// Add more ZIS node types as needed

}  // namespace zis
}  // namespace compiler
}  // namespace zomlang

#endif  // ZOM_ZIS_ZIS_H_
