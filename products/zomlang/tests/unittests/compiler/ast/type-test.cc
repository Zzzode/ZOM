// Copyright (c) 2025 Zode.Z. All rights reserved
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
// See the License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/ast/type.h"

#include "zc/core/memory.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/factory.h"

namespace zomlang {
namespace compiler {
namespace ast {

// Base test visitor that implements all required methods
struct BaseTestVisitor : public Visitor {
  void visit(const Node& node) override {}
  void visit(const Statement& statement) override {}
  void visit(const Expression& expression) override {}
  void visit(const Type& type) override {}
  void visit(const TypeParameter& node) override {}
  void visit(const BindingElement& node) override {}
  void visit(const VariableDeclaration& node) override {}
  void visit(const FunctionDeclaration& node) override {}
  void visit(const ClassDeclaration& node) override {}
  void visit(const InterfaceDeclaration& node) override {}
  void visit(const StructDeclaration& node) override {}
  void visit(const EnumDeclaration& node) override {}
  void visit(const ErrorDeclaration& node) override {}
  void visit(const AliasDeclaration& node) override {}
  void visit(const BlockStatement& node) override {}
  void visit(const EmptyStatement& node) override {}
  void visit(const ExpressionStatement& node) override {}
  void visit(const IfStatement& node) override {}
  void visit(const WhileStatement& node) override {}
  void visit(const ForStatement& node) override {}
  void visit(const BreakStatement& node) override {}
  void visit(const ContinueStatement& node) override {}
  void visit(const ReturnStatement& node) override {}
  void visit(const MatchStatement& node) override {}
  void visit(const DebuggerStatement& node) override {}
  void visit(const UnaryExpression& node) override {}
  void visit(const UpdateExpression& node) override {}
  void visit(const PrefixUnaryExpression& node) override {}
  void visit(const PostfixUnaryExpression& node) override {}
  void visit(const LeftHandSideExpression& node) override {}
  void visit(const MemberExpression& node) override {}
  void visit(const PrimaryExpression& node) override {}
  void visit(const Identifier& node) override {}
  void visit(const PropertyAccessExpression& node) override {}
  void visit(const ElementAccessExpression& node) override {}
  void visit(const NewExpression& node) override {}
  void visit(const ParenthesizedExpression& node) override {}
  void visit(const BinaryExpression& node) override {}
  void visit(const AssignmentExpression& node) override {}
  void visit(const ConditionalExpression& node) override {}
  void visit(const CallExpression& node) override {}
  void visit(const OptionalExpression& node) override {}
  void visit(const LiteralExpression& node) override {}
  void visit(const StringLiteral& node) override {}
  void visit(const IntegerLiteral& node) override {}
  void visit(const FloatLiteral& node) override {}
  void visit(const BooleanLiteral& node) override {}
  void visit(const NullLiteral& node) override {}
  void visit(const CastExpression& node) override {}
  void visit(const AsExpression& node) override {}
  void visit(const ForcedAsExpression& node) override {}
  void visit(const ConditionalAsExpression& node) override {}
  void visit(const VoidExpression& node) override {}
  void visit(const TypeOfExpression& node) override {}
  void visit(const AwaitExpression& node) override {}
  void visit(const FunctionExpression& node) override {}
  void visit(const ArrayLiteralExpression& node) override {}
  void visit(const ObjectLiteralExpression& node) override {}
  void visit(const Operator& op) override {}
  void visit(const BinaryOperator& binaryOp) override {}
  void visit(const UnaryOperator& unaryOp) override {}
  void visit(const AssignmentOperator& assignOp) override {}
  void visit(const SourceFile& sourceFile) override {}
  void visit(const ModulePath& modulePath) override {}
  void visit(const ImportDeclaration& importDecl) override {}
  void visit(const ExportDeclaration& exportDecl) override {}
  void visit(const TypeReference& typeRef) override {}
  void visit(const ArrayType& arrayType) override {}
  void visit(const UnionType& unionType) override {}
  void visit(const IntersectionType& intersectionType) override {}
  void visit(const ParenthesizedType& parenType) override {}
  void visit(const PredefinedType& predefinedType) override {}
  void visit(const ObjectType& objectType) override {}
  void visit(const TupleType& tupleType) override {}
  void visit(const ReturnType& returnType) override {}
  void visit(const FunctionType& functionType) override {}
  void visit(const OptionalType& optionalType) override {}
  void visit(const TypeQuery& typeQuery) override {}
};

ZC_TEST("TypeTest.TypeReference") {
  auto id = factory::createIdentifier(zc::str("Int"));
  auto type = factory::createTypeReference(zc::mv(id), zc::none);

  ZC_EXPECT(type->getKind() == SyntaxKind::kTypeReference);
  ZC_EXPECT(type->getName() == "Int");
}

ZC_TEST("TypeTest.ArrayType") {
  auto elemType = factory::createPredefinedType(zc::str("i32"));
  auto type = factory::createArrayType(zc::mv(elemType));

  ZC_EXPECT(type->getKind() == SyntaxKind::kArrayType);
  // getElementType() now returns reference, no need for null check
  ZC_EXPECT(type->getElementType().getKind() == SyntaxKind::kPredefinedType);
}

ZC_TEST("TypeTest.UnionType") {
  zc::Vector<zc::Own<Type>> types;
  types.add(factory::createPredefinedType(zc::str("i32")));
  types.add(factory::createPredefinedType(zc::str("str")));
  auto type = factory::createUnionType(zc::mv(types));

  ZC_EXPECT(type->getKind() == SyntaxKind::kUnionType);
  ZC_EXPECT(type->getTypes().size() == 2);
}

ZC_TEST("TypeTest.FunctionType") {
  zc::Vector<zc::Own<TypeParameter>> typeParams;
  zc::Vector<zc::Own<BindingElement>> params;
  auto returnType =
      factory::createReturnType(factory::createPredefinedType(zc::str("str")), zc::none);
  auto type = factory::createFunctionType(zc::mv(typeParams), zc::mv(params), zc::mv(returnType));

  ZC_EXPECT(type->getKind() == SyntaxKind::kFunctionType);
}

ZC_TEST("TypeTest.OptionalType") {
  auto baseType = factory::createPredefinedType(zc::str("i32"));
  auto type = factory::createOptionalType(zc::mv(baseType));

  ZC_EXPECT(type->getKind() == SyntaxKind::kOptionalType);
  ZC_EXPECT(type->getType().getKind() == SyntaxKind::kPredefinedType);
}

// Test TypeReference with type arguments
ZC_TEST("TypeTest.TypeReferenceWithTypeArguments") {
  auto id = factory::createIdentifier(zc::str("Array"));
  zc::Vector<zc::Own<Type>> typeArgs;
  typeArgs.add(factory::createPredefinedType(zc::str("i32")));
  auto type = factory::createTypeReference(zc::mv(id), zc::mv(typeArgs));

  ZC_EXPECT(type->getKind() == SyntaxKind::kTypeReference);
  ZC_EXPECT(type->getName() == "Array");
}

// Test TypeReference accept method
ZC_TEST("TypeTest.TypeReferenceAccept") {
  auto id = factory::createIdentifier(zc::str("String"));
  auto type = factory::createTypeReference(zc::mv(id), zc::none);

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const TypeReference& node) override { visited = true; }
  };

  TestVisitor visitor;
  type->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

// Test ArrayType accept method
ZC_TEST("TypeTest.ArrayTypeAccept") {
  auto elemType = factory::createPredefinedType(zc::str("str"));
  auto type = factory::createArrayType(zc::mv(elemType));

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const ArrayType& node) override { visited = true; }
  };

  TestVisitor visitor;
  type->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

// Test UnionType accept method
ZC_TEST("TypeTest.UnionTypeAccept") {
  zc::Vector<zc::Own<Type>> types;
  types.add(factory::createPredefinedType(zc::str("i32")));
  types.add(factory::createPredefinedType(zc::str("str")));
  auto type = factory::createUnionType(zc::mv(types));

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const UnionType& node) override { visited = true; }
  };

  TestVisitor visitor;
  type->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

// Test IntersectionType
ZC_TEST("TypeTest.IntersectionType") {
  zc::Vector<zc::Own<Type>> types;
  types.add(factory::createPredefinedType(zc::str("i32")));
  types.add(factory::createPredefinedType(zc::str("str")));
  auto type = factory::createIntersectionType(zc::mv(types));

  ZC_EXPECT(type->getKind() == SyntaxKind::kIntersectionType);
  ZC_EXPECT(type->getTypes().size() == 2);
}

// Test IntersectionType accept method
ZC_TEST("TypeTest.IntersectionTypeAccept") {
  zc::Vector<zc::Own<Type>> types;
  types.add(factory::createPredefinedType(zc::str("i32")));
  types.add(factory::createPredefinedType(zc::str("str")));
  auto type = factory::createIntersectionType(zc::mv(types));

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const IntersectionType& node) override { visited = true; }
  };

  TestVisitor visitor;
  type->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

// Test ParenthesizedType
ZC_TEST("TypeTest.ParenthesizedType") {
  auto innerType = factory::createPredefinedType(zc::str("i32"));
  auto type = factory::createParenthesizedType(zc::mv(innerType));

  ZC_EXPECT(type->getKind() == SyntaxKind::kParenthesizedType);
  ZC_EXPECT(type->getType().getKind() == SyntaxKind::kPredefinedType);
}

// Test ParenthesizedType accept method
ZC_TEST("TypeTest.ParenthesizedTypeAccept") {
  auto innerType = factory::createPredefinedType(zc::str("i32"));
  auto type = factory::createParenthesizedType(zc::mv(innerType));

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const ParenthesizedType& node) override { visited = true; }
  };

  TestVisitor visitor;
  type->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

// Test PredefinedType
ZC_TEST("TypeTest.PredefinedType") {
  auto type = factory::createPredefinedType(zc::str("bool"));

  ZC_EXPECT(type->getKind() == SyntaxKind::kPredefinedType);
  ZC_EXPECT(type->getName() == "bool");
}

// Test PredefinedType accept method
ZC_TEST("TypeTest.PredefinedTypeAccept") {
  auto type = factory::createPredefinedType(zc::str("f64"));

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const PredefinedType& node) override { visited = true; }
  };

  TestVisitor visitor;
  type->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

// Test ObjectType
ZC_TEST("TypeTest.ObjectType") {
  zc::Vector<zc::Own<Node>> members;
  auto type = factory::createObjectType(zc::mv(members));

  ZC_EXPECT(type->getKind() == SyntaxKind::kObjectType);
  ZC_EXPECT(type->getMembers().size() == 0);
}

// Test ObjectType accept method
ZC_TEST("TypeTest.ObjectTypeAccept") {
  zc::Vector<zc::Own<Node>> members;
  auto type = factory::createObjectType(zc::mv(members));

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const ObjectType& node) override { visited = true; }
  };

  TestVisitor visitor;
  type->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

// Test TupleType
ZC_TEST("TypeTest.TupleType") {
  zc::Vector<zc::Own<Type>> elementTypes;
  elementTypes.add(factory::createPredefinedType(zc::str("i32")));
  elementTypes.add(factory::createPredefinedType(zc::str("str")));
  auto type = factory::createTupleType(zc::mv(elementTypes));

  ZC_EXPECT(type->getKind() == SyntaxKind::kTupleType);
  ZC_EXPECT(type->getElementTypes().size() == 2);
}

// Test TupleType accept method
ZC_TEST("TypeTest.TupleTypeAccept") {
  zc::Vector<zc::Own<Type>> elementTypes;
  elementTypes.add(factory::createPredefinedType(zc::str("i32")));
  auto type = factory::createTupleType(zc::mv(elementTypes));

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const TupleType& node) override { visited = true; }
  };

  TestVisitor visitor;
  type->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

// Test ReturnType
ZC_TEST("TypeTest.ReturnType") {
  auto returnType =
      factory::createReturnType(factory::createPredefinedType(zc::str("i32")), zc::none);

  ZC_EXPECT(returnType->getKind() == SyntaxKind::kReturnType);
  ZC_EXPECT(returnType->getType().getKind() == SyntaxKind::kPredefinedType);
  ZC_EXPECT(returnType->getErrorType() == zc::none);
}

// Test ReturnType with error type
ZC_TEST("TypeTest.ReturnTypeWithError") {
  auto errorType = factory::createPredefinedType(zc::str("str"));
  auto returnType =
      factory::createReturnType(factory::createPredefinedType(zc::str("i32")), zc::mv(errorType));

  ZC_EXPECT(returnType->getKind() == SyntaxKind::kReturnType);
  ZC_EXPECT(returnType->getType().getKind() == SyntaxKind::kPredefinedType);
  ZC_IF_SOME(error, returnType->getErrorType()) {
    ZC_EXPECT(error.getKind() == SyntaxKind::kPredefinedType);
  }
  else { ZC_FAIL_EXPECT("Expected error type to be present"); }
}

// Test ReturnType accept method
ZC_TEST("TypeTest.ReturnTypeAccept") {
  auto returnType =
      factory::createReturnType(factory::createPredefinedType(zc::str("str")), zc::none);

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const ReturnType& node) override { visited = true; }
  };

  TestVisitor visitor;
  returnType->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

// Test FunctionType with parameters
ZC_TEST("TypeTest.FunctionTypeWithParameters") {
  zc::Vector<zc::Own<TypeParameter>> typeParams;
  zc::Vector<zc::Own<BindingElement>> params;

  auto param =
      factory::createBindingElement(factory::createIdentifier(zc::str("x")),
                                    factory::createPredefinedType(zc::str("i32")), zc::none);
  params.add(zc::mv(param));

  auto returnType =
      factory::createReturnType(factory::createPredefinedType(zc::str("str")), zc::none);
  auto type = factory::createFunctionType(zc::mv(typeParams), zc::mv(params), zc::mv(returnType));

  ZC_EXPECT(type->getKind() == SyntaxKind::kFunctionType);
  ZC_EXPECT(type->getParameters().size() == 1);
  ZC_EXPECT(type->getTypeParameters().size() == 0);
  ZC_EXPECT(type->getReturnType().getKind() == SyntaxKind::kReturnType);
}

// Test FunctionType accept method
ZC_TEST("TypeTest.FunctionTypeAccept") {
  zc::Vector<zc::Own<TypeParameter>> typeParams;
  zc::Vector<zc::Own<BindingElement>> params;
  auto returnType =
      factory::createReturnType(factory::createPredefinedType(zc::str("str")), zc::none);
  auto type = factory::createFunctionType(zc::mv(typeParams), zc::mv(params), zc::mv(returnType));

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const FunctionType& node) override { visited = true; }
  };

  TestVisitor visitor;
  type->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

// Test OptionalType accept method
ZC_TEST("TypeTest.OptionalTypeAccept") {
  auto baseType = factory::createPredefinedType(zc::str("i32"));
  auto type = factory::createOptionalType(zc::mv(baseType));

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const OptionalType& node) override { visited = true; }
  };

  TestVisitor visitor;
  type->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

// Test TypeQuery
ZC_TEST("TypeTest.TypeQuery") {
  auto expr = factory::createIdentifier(zc::str("someVariable"));
  auto type = factory::createTypeQuery(zc::mv(expr));

  ZC_EXPECT(type->getKind() == SyntaxKind::kTypeQuery);
  ZC_EXPECT(type->getExpression().getKind() == SyntaxKind::kIdentifier);
}

// Test TypeQuery accept method
ZC_TEST("TypeTest.TypeQueryAccept") {
  auto expr = factory::createIdentifier(zc::str("someVariable"));
  auto type = factory::createTypeQuery(zc::mv(expr));

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const TypeQuery& node) override { visited = true; }
  };

  TestVisitor visitor;
  type->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

ZC_TEST("TypeTest.NestedArrayTypes") {
  // Test nested array types: i32[][]
  auto innerElementType = factory::createPredefinedType(zc::str("i32"));
  auto innerArrayType = factory::createArrayType(zc::mv(innerElementType));
  auto outerArrayType = factory::createArrayType(zc::mv(innerArrayType));

  ZC_EXPECT(outerArrayType->getKind() == SyntaxKind::kArrayType);
  ZC_EXPECT(outerArrayType->getElementType().getKind() == SyntaxKind::kArrayType);

  // Cast to access nested array type
  const auto& innerArray = static_cast<const ArrayType&>(outerArrayType->getElementType());
  ZC_EXPECT(innerArray.getElementType().getKind() == SyntaxKind::kPredefinedType);
}

ZC_TEST("TypeTest.ComplexUnionTypes") {
  // Test union with multiple complex types: (i32 | str | bool[])
  zc::Vector<zc::Own<Type>> unionTypes;

  auto intType = factory::createPredefinedType(zc::str("i32"));
  auto strType = factory::createPredefinedType(zc::str("str"));
  auto boolType = factory::createPredefinedType(zc::str("bool"));
  auto boolArrayType = factory::createArrayType(zc::mv(boolType));

  unionTypes.add(zc::mv(intType));
  unionTypes.add(zc::mv(strType));
  unionTypes.add(zc::mv(boolArrayType));

  auto unionType = factory::createUnionType(zc::mv(unionTypes));

  ZC_EXPECT(unionType->getKind() == SyntaxKind::kUnionType);
  ZC_EXPECT(unionType->getTypes().size() == 3);
  ZC_EXPECT(unionType->getTypes()[2].getKind() == SyntaxKind::kArrayType);
}

ZC_TEST("TypeTest.ComplexIntersectionTypes") {
  // Test intersection with object and function types
  zc::Vector<zc::Own<Type>> intersectionTypes;

  // Create object type with members
  zc::Vector<zc::Own<Node>> objectMembers;
  auto objectType = factory::createObjectType(zc::mv(objectMembers));

  // Create function type
  zc::Vector<zc::Own<TypeParameter>> typeParams;
  zc::Vector<zc::Own<BindingElement>> params;
  auto returnType =
      factory::createReturnType(factory::createPredefinedType(zc::str("unit")), zc::none);
  auto functionType =
      factory::createFunctionType(zc::mv(typeParams), zc::mv(params), zc::mv(returnType));

  intersectionTypes.add(zc::mv(objectType));
  intersectionTypes.add(zc::mv(functionType));

  auto intersectionType = factory::createIntersectionType(zc::mv(intersectionTypes));

  ZC_EXPECT(intersectionType->getKind() == SyntaxKind::kIntersectionType);
  ZC_EXPECT(intersectionType->getTypes().size() == 2);
  ZC_EXPECT(intersectionType->getTypes()[0].getKind() == SyntaxKind::kObjectType);
  ZC_EXPECT(intersectionType->getTypes()[1].getKind() == SyntaxKind::kFunctionType);
}

ZC_TEST("TypeTest.NestedTupleTypes") {
  // Test nested tuple types: (i32, (str, bool))
  zc::Vector<zc::Own<Type>> outerTupleTypes;
  zc::Vector<zc::Own<Type>> innerTupleTypes;

  auto intType = factory::createPredefinedType(zc::str("i32"));
  auto strType = factory::createPredefinedType(zc::str("str"));
  auto boolType = factory::createPredefinedType(zc::str("bool"));

  innerTupleTypes.add(zc::mv(strType));
  innerTupleTypes.add(zc::mv(boolType));
  auto innerTupleType = factory::createTupleType(zc::mv(innerTupleTypes));

  outerTupleTypes.add(zc::mv(intType));
  outerTupleTypes.add(zc::mv(innerTupleType));
  auto outerTupleType = factory::createTupleType(zc::mv(outerTupleTypes));

  ZC_EXPECT(outerTupleType->getKind() == SyntaxKind::kTupleType);
  ZC_EXPECT(outerTupleType->getElementTypes().size() == 2);
  ZC_EXPECT(outerTupleType->getElementTypes()[0].getKind() == SyntaxKind::kPredefinedType);
  ZC_EXPECT(outerTupleType->getElementTypes()[1].getKind() == SyntaxKind::kTupleType);
}

ZC_TEST("TypeTest.ComplexFunctionTypes") {
  // Test function type with type parameters and complex return type
  zc::Vector<zc::Own<TypeParameter>> typeParams;
  zc::Vector<zc::Own<BindingElement>> params;

  // Add type parameter T
  auto typeParamName = factory::createIdentifier(zc::str("T"));
  auto typeParam = factory::createTypeParameterDeclaration(zc::mv(typeParamName), zc::none);
  typeParams.add(zc::mv(typeParam));

  // Add parameter of type T[]
  auto paramName = factory::createIdentifier(zc::str("items"));
  auto paramTypeRef =
      factory::createTypeReference(factory::createIdentifier(zc::str("T")), zc::none);
  auto paramArrayType = factory::createArrayType(zc::mv(paramTypeRef));
  auto param = factory::createBindingElement(zc::mv(paramName), zc::mv(paramArrayType), zc::none);
  params.add(zc::mv(param));

  // Return type: T | null
  zc::Vector<zc::Own<Type>> unionTypes;
  auto returnTypeRef =
      factory::createTypeReference(factory::createIdentifier(zc::str("T")), zc::none);
  auto nullType = factory::createPredefinedType(zc::str("null"));
  unionTypes.add(zc::mv(returnTypeRef));
  unionTypes.add(zc::mv(nullType));
  auto unionReturnType = factory::createUnionType(zc::mv(unionTypes));
  auto returnType = factory::createReturnType(zc::mv(unionReturnType), zc::none);

  auto functionType =
      factory::createFunctionType(zc::mv(typeParams), zc::mv(params), zc::mv(returnType));

  ZC_EXPECT(functionType->getKind() == SyntaxKind::kFunctionType);
  ZC_EXPECT(functionType->getTypeParameters().size() == 1);
  ZC_EXPECT(functionType->getParameters().size() == 1);
  ZC_EXPECT(functionType->getReturnType().getType().getKind() == SyntaxKind::kUnionType);
}

ZC_TEST("TypeTest.OptionalComplexTypes") {
  // Test optional array type: i32[]?
  auto elementType = factory::createPredefinedType(zc::str("i32"));
  auto arrayType = factory::createArrayType(zc::mv(elementType));
  auto optionalArrayType = factory::createOptionalType(zc::mv(arrayType));

  ZC_EXPECT(optionalArrayType->getKind() == SyntaxKind::kOptionalType);
  ZC_EXPECT(optionalArrayType->getType().getKind() == SyntaxKind::kArrayType);

  // Test optional function type
  zc::Vector<zc::Own<TypeParameter>> typeParams;
  zc::Vector<zc::Own<BindingElement>> params;
  auto returnType =
      factory::createReturnType(factory::createPredefinedType(zc::str("unit")), zc::none);
  auto functionType =
      factory::createFunctionType(zc::mv(typeParams), zc::mv(params), zc::mv(returnType));
  auto optionalFunctionType = factory::createOptionalType(zc::mv(functionType));

  ZC_EXPECT(optionalFunctionType->getKind() == SyntaxKind::kOptionalType);
  ZC_EXPECT(optionalFunctionType->getType().getKind() == SyntaxKind::kFunctionType);
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
