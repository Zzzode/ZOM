#ifndef ZOM_ZIS_ZIS_H_
#define ZOM_ZIS_ZIS_H_

#include "src/zc/strings/string.h"

namespace zom {
namespace zis {

class ZIS {
public:
  virtual ~ZIS() noexcept = default;  // 添加 noexcept
};

class Expression : public ZIS {
public:
  ~Expression() noexcept override = default;  // 添加 noexcept
};

class Statement : public ZIS {
public:
  ~Statement() noexcept override = default;  // 添加 noexcept
};

class BinaryExpression : public Expression {
public:
  ~BinaryExpression() noexcept override = default;  // 添加 noexcept

private:
  zc::Own<Expression> left;
  zc::String op;
  zc::Own<Expression> right;
};

class VariableDeclaration : public Statement {
public:
  ~VariableDeclaration() noexcept override = default;  // 添加 noexcept

  zc::StringPtr getType() const { return type; }

  zc::StringPtr getName() const { return name; }

private:
  zc::String type;
  zc::String name;
  zc::Own<Expression> initializer;
};

// Add more AST node types as needed

}  // namespace zis
}  // namespace zom

#endif  // ZOM_ZIS_ZIS_H_
