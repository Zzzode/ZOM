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

namespace zomlang {
namespace compiler {
namespace ast {

// Forward declarations for all AST node types
#define AST_ELEMENT_NODE(ClassName) class ClassName;
#define AST_INTERFACE_NODE(ClassName) class ClassName;
#include "zomlang/compiler/ast/ast-nodes.def"
#undef AST_ELEMENT_NODE
#undef AST_INTERFACE_NODE

/// \brief Base visitor interface for AST traversal using the visitor pattern.
///
/// This class provides a generic visitor interface that can be used to traverse
/// and process AST nodes. Concrete visitors should inherit from this class and
/// override the visit methods for the node types they are interested in.
///
/// The visitor pattern allows for separation of concerns between the AST structure
/// and the operations performed on it, making it easier to add new operations
/// without modifying the AST node classes.
class Visitor {
public:
  ZC_DISALLOW_COPY_AND_MOVE(Visitor);

#define AST_ELEMENT_NODE(ClassName) virtual void visit(const ClassName& node) = 0;
#include "zomlang/compiler/ast/ast-nodes.def"
#undef AST_ELEMENT_NODE

  // Base visit methods
  virtual void visit(const Node& node) = 0;
  virtual void visit(const Statement& statement) = 0;
  virtual void visit(const Expression& expression) = 0;
  virtual void visit(const TypeNode& type) = 0;

  // Expression visitor methods
  virtual void visit(const UnaryExpression& node) = 0;
  virtual void visit(const UpdateExpression& node) = 0;

  virtual void visit(const LeftHandSideExpression& node) = 0;
  virtual void visit(const MemberExpression& node) = 0;
  virtual void visit(const PrimaryExpression& node) = 0;

  virtual void visit(const LiteralExpression& node) = 0;

  virtual void visit(const CastExpression& node) = 0;

  // Module visitor methods

  // Type visitor methods

protected:
  Visitor() = default;
};

/// \brief Utility class for visitors that need to return values.
///
/// Since virtual functions cannot be templates, visitors that need to return
/// values should store their results in member variables and access them
/// after the visit operation completes.
///
/// Example usage:
/// \code
/// class EvaluationVisitor : public Visitor {
/// private:
///   int result = 0;
/// public:
///   void visit(const IntegerLiteral& node) override {
///     result = node.getValue();
///   }
///   int getResult() const { return result; }
/// };
/// \endcode
class VisitorResult {
public:
  ZC_DISALLOW_COPY_AND_MOVE(VisitorResult);

protected:
  VisitorResult() = default;
};

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
