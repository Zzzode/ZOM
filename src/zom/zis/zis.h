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
  zc::Own<Expression> left_;
  zc::String op_;
  zc::Own<Expression> right_;
};

class VariableDeclaration : public Statement {
public:
  ~VariableDeclaration() noexcept override = default;  // 添加 noexcept

  zc::StringPtr type() const { return type_; }

  zc::StringPtr name() const { return name_; }

private:
  zc::String type_;
  zc::String name_;
  zc::Own<Expression> initializer_;
};

// Add more AST node types as needed

}  // namespace zis
}  // namespace zom

#endif  // ZOM_ZIS_ZIS_H_