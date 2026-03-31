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
#include "zomlang/compiler/ast/cast.h"
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
  void visit(const TypeNode& type) override {}
  void visit(const TokenNode& token) override {}

  // Interface node visitors (abstract base classes)
  void visit(const Declaration& node) override {}
  void visit(const IterationStatement& node) override {}
  void visit(const DeclarationStatement& node) override {}
  void visit(const NamedDeclaration& node) override {}
  void visit(const Pattern& node) override {}
  void visit(const PrimaryPattern& node) override {}
  void visit(const BindingPattern& node) override {}

  void visit(const BindingElement& node) override {}
  void visit(const VariableDeclarationList& node) override {}
  void visit(const FunctionDeclaration& node) override {}
  void visit(const ClassDeclaration& node) override {}
  void visit(const InterfaceDeclaration& node) override {}
  void visit(const StructDeclaration& node) override {}
  void visit(const EnumDeclaration& node) override {}
  void visit(const EnumMember& node) override {}
  void visit(const ErrorDeclaration& node) override {}
  void visit(const AliasDeclaration& node) override {}
  void visit(const VariableStatement& node) override {}
  void visit(const BlockStatement& node) override {}
  void visit(const EmptyStatement& node) override {}
  void visit(const ExpressionStatement& node) override {}
  void visit(const IfStatement& node) override {}
  void visit(const WhileStatement& node) override {}
  void visit(const ForStatement& node) override {}
  void visit(const ForInStatement& node) override {}
  void visit(const LabeledStatement& node) override {}
  void visit(const PatternProperty& node) override {}
  void visit(const BreakStatement& node) override {}
  void visit(const ContinueStatement& node) override {}
  void visit(const ReturnStatement& node) override {}
  void visit(const MatchStatement& node) override {}
  void visit(const DebuggerStatement& node) override {}
  void visit(const MatchClause& node) override {}
  void visit(const DefaultClause& node) override {}
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
  void visit(const ConditionalExpression& node) override {}
  void visit(const CallExpression& node) override {}
  void visit(const LiteralExpression& node) override {}
  void visit(const StringLiteral& node) override {}
  void visit(const IntegerLiteral& node) override {}
  void visit(const FloatLiteral& node) override {}
  void visit(const BigIntLiteral& node) override {}
  void visit(const BooleanLiteral& node) override {}
  void visit(const NullLiteral& node) override {}
  void visit(const CastExpression& node) override {}
  void visit(const AsExpression& node) override {}
  void visit(const ForcedAsExpression& node) override {}
  void visit(const ConditionalAsExpression& node) override {}
  void visit(const VoidExpression& node) override {}
  void visit(const NonNullExpression& node) override {}
  void visit(const ExpressionWithTypeArguments& node) override {}
  void visit(const HeritageClause& node) override {}
  void visit(const TypeOfExpression& node) override {}
  void visit(const AwaitExpression& node) override {}
  void visit(const FunctionExpression& node) override {}
  void visit(const ArrayLiteralExpression& node) override {}
  void visit(const ObjectLiteralExpression& node) override {}
  void visit(const SourceFile& sourceFile) override {}
  void visit(const ModulePath& modulePath) override {}
  void visit(const ImportDeclaration& importDecl) override {}
  void visit(const ExportDeclaration& exportDecl) override {}
  void visit(const ClassElement& node) override {}
  void visit(const ObjectLiteralElement& node) override {}
  void visit(const PropertyAssignment& node) override {}
  void visit(const ShorthandPropertyAssignment& node) override {}
  void visit(const SpreadAssignment& node) override {}
  void visit(const SpreadElement& node) override {}
  void visit(const InterfaceElement& node) override {}
  void visit(const TypeReferenceNode& typeRef) override {}
  void visit(const ArrayTypeNode& arrayType) override {}
  void visit(const UnionTypeNode& unionType) override {}
  void visit(const IntersectionTypeNode& intersectionType) override {}
  void visit(const ParenthesizedTypeNode& parenType) override {}
  void visit(const ObjectTypeNode& objectType) override {}
  void visit(const TupleTypeNode& tupleType) override {}
  void visit(const ReturnTypeNode& returnType) override {}
  void visit(const FunctionTypeNode& functionType) override {}
  void visit(const OptionalTypeNode& optionalType) override {}
  void visit(const TypeQueryNode& typeQuery) override {}
  void visit(const NamedTupleElement& node) override {}

  // Add all missing visitor methods from ast-nodes.def
  void visit(const Module& node) override {}
  void visit(const VariableDeclaration& node) override {}
  void visit(const TypeParameterDeclaration& node) override {}
  void visit(const MethodDeclaration& node) override {}
  void visit(const GetAccessor& node) override {}
  void visit(const SetAccessor& node) override {}
  void visit(const InitDeclaration& node) override {}
  void visit(const DeinitDeclaration& node) override {}
  void visit(const ParameterDeclaration& node) override {}
  void visit(const PropertyDeclaration& node) override {}
  void visit(const MissingDeclaration& node) override {}
  void visit(const SemicolonClassElement& node) override {}
  void visit(const SemicolonInterfaceElement& node) override {}
  void visit(const ArrayBindingPattern& node) override {}
  void visit(const ObjectBindingPattern& node) override {}
  void visit(const ThisExpression& node) override {}
  void visit(const SuperExpression& node) override {}
  void visit(const BoolTypeNode& node) override {}
  void visit(const I8TypeNode& node) override {}
  void visit(const I16TypeNode& node) override {}
  void visit(const I32TypeNode& node) override {}
  void visit(const I64TypeNode& node) override {}
  void visit(const U8TypeNode& node) override {}
  void visit(const U16TypeNode& node) override {}
  void visit(const U32TypeNode& node) override {}
  void visit(const U64TypeNode& node) override {}
  void visit(const F32TypeNode& node) override {}
  void visit(const F64TypeNode& node) override {}
  void visit(const StrTypeNode& node) override {}
  void visit(const UnitTypeNode& node) override {}
  void visit(const NullTypeNode& node) override {}
  void visit(const WildcardPattern& node) override {}
  void visit(const CaptureElement& node) override {}
  void visit(const IdentifierPattern& node) override {}
  void visit(const TuplePattern& node) override {}
  void visit(const StructurePattern& node) override {}
  void visit(const ArrayPattern& node) override {}
  void visit(const IsPattern& node) override {}
  void visit(const ExpressionPattern& node) override {}
  void visit(const EnumPattern& node) override {}
  void visit(const PropertySignature& node) override {}
  void visit(const MethodSignature& node) override {}
  void visit(const ClassBody& node) override {}
  void visit(const InterfaceBody& node) override {}
  void visit(const StructBody& node) override {}
  void visit(const ErrorBody& node) override {}
  void visit(const EnumBody& node) override {}
  void visit(const PredefinedTypeNode& node) override {}

  // Add missing TemplateLiteralExpression and TemplateSpan visitors
  void visit(const TemplateLiteralExpression& node) override {}
  void visit(const TemplateSpan& node) override {}
};

ZC_TEST("TypeTest.TypeReference") {
  auto id = factory::createIdentifier("Int"_zc);
  auto type = factory::createTypeReference(zc::mv(id), zc::none);

  ZC_EXPECT(type->getKind() == SyntaxKind::TypeReferenceNode);
  ZC_EXPECT(type->getName().getText() == "Int");
}

ZC_TEST("TypeTest.ArrayType") {
  auto elemType = factory::createPredefinedType("i32"_zc);
  auto type = factory::createArrayType(zc::mv(elemType));

  ZC_EXPECT(type->getKind() == SyntaxKind::ArrayTypeNode);
  // getElementType() now returns reference, no need for null check
  ZC_EXPECT(type->getElementType().getKind() == SyntaxKind::I32TypeNode);
}

ZC_TEST("TypeTest.UnionType") {
  zc::Vector<zc::Own<TypeNode>> types;
  types.add(factory::createPredefinedType("i32"_zc));
  types.add(factory::createPredefinedType("str"_zc));
  auto type = factory::createUnionType(zc::mv(types));

  ZC_EXPECT(type->getKind() == SyntaxKind::UnionTypeNode);
  ZC_EXPECT(type->getTypes().size() == 2);
}

ZC_TEST("TypeTest.FunctionType") {
  zc::Vector<zc::Own<TypeParameterDeclaration>> typeParams;
  zc::Vector<zc::Own<ParameterDeclaration>> params;
  auto returnType = factory::createReturnType(factory::createPredefinedType("str"_zc), zc::none);
  auto type = factory::createFunctionType(zc::mv(typeParams), zc::mv(params), zc::mv(returnType));

  ZC_EXPECT(type->getKind() == SyntaxKind::FunctionTypeNode);
}

ZC_TEST("TypeTest.OptionalType") {
  auto baseType = factory::createPredefinedType("i32"_zc);
  auto type = factory::createOptionalType(zc::mv(baseType));

  ZC_EXPECT(type->getKind() == SyntaxKind::OptionalTypeNode);
  ZC_EXPECT(type->getType().getKind() == SyntaxKind::I32TypeNode);
}

// Test TypeReference with type arguments
ZC_TEST("TypeTest.TypeReferenceWithTypeArguments") {
  auto id = factory::createIdentifier("Array"_zc);
  zc::Vector<zc::Own<TypeNode>> typeArgs;
  typeArgs.add(factory::createPredefinedType("i32"_zc));
  auto type = factory::createTypeReference(zc::mv(id), zc::mv(typeArgs));

  ZC_EXPECT(type->getKind() == SyntaxKind::TypeReferenceNode);
  ZC_EXPECT(type->getName().getText() == "Array");
}

// Test TypeReference accept method
ZC_TEST("TypeTest.TypeReferenceAccept") {
  auto id = factory::createIdentifier("String"_zc);
  auto type = factory::createTypeReference(zc::mv(id), zc::none);

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const TypeReferenceNode& node) override { visited = true; }
  };

  TestVisitor visitor;
  type->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

// Test ArrayType accept method
ZC_TEST("TypeTest.ArrayTypeAccept") {
  auto elemType = factory::createPredefinedType("str"_zc);
  auto type = factory::createArrayType(zc::mv(elemType));

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const ArrayTypeNode& node) override { visited = true; }
  };

  TestVisitor visitor;
  type->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

// Test UnionType accept method
ZC_TEST("TypeTest.UnionTypeAccept") {
  zc::Vector<zc::Own<TypeNode>> types;
  types.add(factory::createPredefinedType("i32"_zc));
  types.add(factory::createPredefinedType("str"_zc));
  auto type = factory::createUnionType(zc::mv(types));

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const UnionTypeNode& node) override { visited = true; }
  };

  TestVisitor visitor;
  type->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

// Test IntersectionTypeNode
ZC_TEST("TypeTest.IntersectionTypeNode") {
  zc::Vector<zc::Own<TypeNode>> types;
  types.add(factory::createPredefinedType("i32"_zc));
  types.add(factory::createPredefinedType("str"_zc));
  auto type = factory::createIntersectionType(zc::mv(types));

  ZC_EXPECT(type->getKind() == SyntaxKind::IntersectionTypeNode);
  ZC_EXPECT(type->getTypes().size() == 2);
}

// Test IntersectionTypeNode accept method
ZC_TEST("TypeTest.IntersectionTypeAccept") {
  zc::Vector<zc::Own<TypeNode>> types;
  types.add(factory::createPredefinedType("i32"_zc));
  types.add(factory::createPredefinedType("str"_zc));
  auto type = factory::createIntersectionType(zc::mv(types));

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const IntersectionTypeNode& node) override { visited = true; }
  };

  TestVisitor visitor;
  type->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

// Test ParenthesizedType
ZC_TEST("TypeTest.ParenthesizedType") {
  auto innerType = factory::createPredefinedType("i32"_zc);
  auto type = factory::createParenthesizedType(zc::mv(innerType));

  ZC_EXPECT(type->getKind() == SyntaxKind::ParenthesizedTypeNode);
  ZC_EXPECT(type->getType().getKind() == SyntaxKind::I32TypeNode);
}

// Test ParenthesizedType accept method
ZC_TEST("TypeTest.ParenthesizedTypeAccept") {
  auto innerType = factory::createPredefinedType("i32"_zc);
  auto type = factory::createParenthesizedType(zc::mv(innerType));

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const ParenthesizedTypeNode& node) override { visited = true; }
  };

  TestVisitor visitor;
  type->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

// Test PredefinedTypeNode
ZC_TEST("TypeTest.PredefinedTypeNode") {
  auto type = factory::createPredefinedType("bool"_zc);

  ZC_EXPECT(type->getKind() == SyntaxKind::BoolTypeNode);
  ZC_EXPECT(type->getName() == "bool");
}

// Test ObjectTypeNode
ZC_TEST("TypeTest.ObjectTypeNode") {
  zc::Vector<zc::Own<Node>> members;
  auto type = factory::createObjectType(zc::mv(members));

  ZC_EXPECT(type->getKind() == SyntaxKind::ObjectTypeNode);
  ZC_EXPECT(type->getMembers().size() == 0);
}

// Test ObjectTypeNode accept method
ZC_TEST("TypeTest.ObjectTypeAccept") {
  zc::Vector<zc::Own<Node>> members;
  auto type = factory::createObjectType(zc::mv(members));

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const ObjectTypeNode& node) override { visited = true; }
  };

  TestVisitor visitor;
  type->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

// Test TupleType
ZC_TEST("TypeTest.TupleType") {
  zc::Vector<zc::Own<TypeNode>> elementTypes;
  elementTypes.add(factory::createPredefinedType("i32"_zc));
  elementTypes.add(factory::createPredefinedType("str"_zc));
  auto type = factory::createTupleType(zc::mv(elementTypes));

  ZC_EXPECT(type->getKind() == SyntaxKind::TupleTypeNode);
  ZC_EXPECT(type->getElementTypes().size() == 2);
}

// Test TupleType accept method
ZC_TEST("TypeTest.TupleTypeAccept") {
  zc::Vector<zc::Own<TypeNode>> elementTypes;
  elementTypes.add(factory::createPredefinedType("i32"_zc));
  auto type = factory::createTupleType(zc::mv(elementTypes));

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const TupleTypeNode& node) override { visited = true; }
  };

  TestVisitor visitor;
  type->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

// Test ReturnTypeNode
ZC_TEST("TypeTest.ReturnTypeNode") {
  auto returnType = factory::createReturnType(factory::createPredefinedType("i32"_zc), zc::none);

  ZC_EXPECT(returnType->getKind() == SyntaxKind::ReturnTypeNode);
  ZC_EXPECT(returnType->getType().getKind() == SyntaxKind::I32TypeNode);
  ZC_EXPECT(returnType->getErrorType() == zc::none);
}

// Test ReturnTypeNode with error type
ZC_TEST("TypeTest.ReturnTypeWithError") {
  auto errorType = factory::createPredefinedType("str"_zc);
  auto returnType =
      factory::createReturnType(factory::createPredefinedType("i32"_zc), zc::mv(errorType));

  ZC_EXPECT(returnType->getKind() == SyntaxKind::ReturnTypeNode);
  ZC_EXPECT(returnType->getType().getKind() == SyntaxKind::I32TypeNode);
  ZC_IF_SOME(error, returnType->getErrorType()) {
    ZC_EXPECT(error.getKind() == SyntaxKind::StrTypeNode);
  }
  else { ZC_FAIL_EXPECT("Expected error type to be present"); }
}

// Test ReturnTypeNode accept method
ZC_TEST("TypeTest.ReturnTypeAccept") {
  auto returnType = factory::createReturnType(factory::createPredefinedType("str"_zc), zc::none);

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const ReturnTypeNode& node) override { visited = true; }
  };

  TestVisitor visitor;
  returnType->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

// Test FunctionType with parameters
ZC_TEST("TypeTest.FunctionTypeWithParameters") {
  zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParams = zc::none;
  zc::Vector<zc::Own<ParameterDeclaration>> params;

  auto param =
      factory::createParameterDeclaration({}, zc::none, factory::createIdentifier("x"_zc), zc::none,
                                          factory::createPredefinedType("i32"_zc), zc::none);
  params.add(zc::mv(param));

  auto returnType = factory::createReturnType(factory::createPredefinedType("str"_zc), zc::none);
  auto type = factory::createFunctionType(zc::mv(typeParams), zc::mv(params), zc::mv(returnType));

  ZC_EXPECT(type->getKind() == SyntaxKind::FunctionTypeNode);
  ZC_EXPECT(type->getParameters().size() == 1);
  ZC_EXPECT(type->getTypeParameters() == zc::none);
  ZC_EXPECT(type->getReturnType().getKind() == SyntaxKind::ReturnTypeNode);
}

// Test FunctionType accept method
ZC_TEST("TypeTest.FunctionTypeAccept") {
  zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParams = zc::none;
  zc::Vector<zc::Own<ParameterDeclaration>> params;
  auto returnType = factory::createReturnType(factory::createPredefinedType("str"_zc), zc::none);
  auto type = factory::createFunctionType(zc::mv(typeParams), zc::mv(params), zc::mv(returnType));

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const FunctionTypeNode& node) override { visited = true; }
  };

  TestVisitor visitor;
  type->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

// Test OptionalType accept method
ZC_TEST("TypeTest.OptionalTypeAccept") {
  auto baseType = factory::createPredefinedType("i32"_zc);
  auto type = factory::createOptionalType(zc::mv(baseType));

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const OptionalTypeNode& node) override { visited = true; }
  };

  TestVisitor visitor;
  type->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

// Test TypeQuery
ZC_TEST("TypeTest.TypeQuery") {
  auto expr = factory::createIdentifier("someVariable"_zc);
  auto type = factory::createTypeQuery(zc::mv(expr));

  ZC_EXPECT(type->getKind() == SyntaxKind::TypeQueryNode);
  ZC_EXPECT(type->getExpression().getKind() == SyntaxKind::Identifier);
}

// Test TypeQuery accept method
ZC_TEST("TypeTest.TypeQueryAccept") {
  auto expr = factory::createIdentifier("someVariable"_zc);
  auto type = factory::createTypeQuery(zc::mv(expr));

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const TypeQueryNode& node) override { visited = true; }
  };

  TestVisitor visitor;
  type->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

ZC_TEST("TypeTest.NestedArrayTypes") {
  // Test nested array types: i32[][]
  auto innerElementType = factory::createPredefinedType("i32"_zc);
  auto innerArrayType = factory::createArrayType(zc::mv(innerElementType));
  auto outerArrayType = factory::createArrayType(zc::mv(innerArrayType));

  ZC_EXPECT(outerArrayType->getKind() == SyntaxKind::ArrayTypeNode);
  ZC_EXPECT(outerArrayType->getElementType().getKind() == SyntaxKind::ArrayTypeNode);

  // Cast to access nested array type
  const auto& innerArray = ast::cast<ArrayTypeNode>(outerArrayType->getElementType());
  ZC_EXPECT(innerArray.getElementType().getKind() == SyntaxKind::I32TypeNode);
}

ZC_TEST("TypeTest.ComplexUnionTypes") {
  // Test union with multiple complex types: (i32 | str | bool[])
  zc::Vector<zc::Own<TypeNode>> unionTypes;

  auto intType = factory::createPredefinedType("i32"_zc);
  auto strType = factory::createPredefinedType("str"_zc);
  auto boolType = factory::createPredefinedType("bool"_zc);
  auto boolArrayType = factory::createArrayType(zc::mv(boolType));

  unionTypes.add(zc::mv(intType));
  unionTypes.add(zc::mv(strType));
  unionTypes.add(zc::mv(boolArrayType));

  auto unionType = factory::createUnionType(zc::mv(unionTypes));

  ZC_EXPECT(unionType->getKind() == SyntaxKind::UnionTypeNode);
  ZC_EXPECT(unionType->getTypes().size() == 3);
  ZC_EXPECT(unionType->getTypes()[2].getKind() == SyntaxKind::ArrayTypeNode);
}

ZC_TEST("TypeTest.ComplexIntersectionTypes") {
  // Test intersection of object type and function type
  zc::Vector<zc::Own<TypeNode>> intersectionTypes;

  // Create object type
  zc::Vector<zc::Own<Node>> objectMembers;
  auto objectType = factory::createObjectType(zc::mv(objectMembers));

  // Create function type
  zc::Vector<zc::Own<TypeParameterDeclaration>> typeParams;
  zc::Vector<zc::Own<ParameterDeclaration>> params;
  auto returnType = factory::createReturnType(factory::createPredefinedType("unit"_zc), zc::none);
  auto functionType =
      factory::createFunctionType(zc::mv(typeParams), zc::mv(params), zc::mv(returnType));

  intersectionTypes.add(zc::mv(objectType));
  intersectionTypes.add(zc::mv(functionType));

  auto intersectionType = factory::createIntersectionType(zc::mv(intersectionTypes));

  ZC_EXPECT(intersectionType->getKind() == SyntaxKind::IntersectionTypeNode);
  ZC_EXPECT(intersectionType->getTypes().size() == 2);
  ZC_EXPECT(intersectionType->getTypes()[0].getKind() == SyntaxKind::ObjectTypeNode);
  ZC_EXPECT(intersectionType->getTypes()[1].getKind() == SyntaxKind::FunctionTypeNode);
}

ZC_TEST("TypeTest.NestedTupleTypes") {
  // Test nested tuple types: (i32, (str, bool))
  zc::Vector<zc::Own<TypeNode>> outerTupleTypes;
  zc::Vector<zc::Own<TypeNode>> innerTupleTypes;

  auto intType = factory::createPredefinedType("i32"_zc);
  auto strType = factory::createPredefinedType("str"_zc);
  auto boolType = factory::createPredefinedType("bool"_zc);

  innerTupleTypes.add(zc::mv(strType));
  innerTupleTypes.add(zc::mv(boolType));
  auto innerTupleType = factory::createTupleType(zc::mv(innerTupleTypes));

  outerTupleTypes.add(zc::mv(intType));
  outerTupleTypes.add(zc::mv(innerTupleType));
  auto outerTupleType = factory::createTupleType(zc::mv(outerTupleTypes));

  ZC_EXPECT(outerTupleType->getKind() == SyntaxKind::TupleTypeNode);
  ZC_EXPECT(outerTupleType->getElementTypes().size() == 2);
  ZC_EXPECT(outerTupleType->getElementTypes()[0].getKind() == SyntaxKind::I32TypeNode);
  ZC_EXPECT(outerTupleType->getElementTypes()[1].getKind() == SyntaxKind::TupleTypeNode);
}

ZC_TEST("TypeTest.ComplexFunctionTypes") {
  // Test function type with type parameters and complex return type
  zc::Vector<zc::Own<TypeParameterDeclaration>> typeParams;
  zc::Vector<zc::Own<ParameterDeclaration>> params;

  // Add type parameter T
  auto typeParamName = factory::createIdentifier("T"_zc);
  auto typeParam = factory::createTypeParameterDeclaration(zc::mv(typeParamName), zc::none);
  typeParams.add(zc::mv(typeParam));

  // Add parameter of type T[]
  auto paramName = factory::createIdentifier("items"_zc);
  auto paramTypeRef = factory::createTypeReference(factory::createIdentifier("T"_zc), zc::none);
  auto paramArrayType = factory::createArrayType(zc::mv(paramTypeRef));
  auto param = factory::createParameterDeclaration({}, zc::none, zc::mv(paramName), zc::none,
                                                   zc::mv(paramArrayType), zc::none);
  params.add(zc::mv(param));

  // Return type: T | null
  zc::Vector<zc::Own<TypeNode>> unionTypes;
  auto returnTypeRef = factory::createTypeReference(factory::createIdentifier("T"_zc), zc::none);
  auto nullType = factory::createPredefinedType("null"_zc);
  unionTypes.add(zc::mv(returnTypeRef));
  unionTypes.add(zc::mv(nullType));
  auto unionReturnType = factory::createUnionType(zc::mv(unionTypes));
  auto returnType = factory::createReturnType(zc::mv(unionReturnType), zc::none);

  auto functionType =
      factory::createFunctionType(zc::mv(typeParams), zc::mv(params), zc::mv(returnType));

  ZC_EXPECT(functionType->getKind() == SyntaxKind::FunctionTypeNode);
  ZC_EXPECT(ZC_ASSERT_NONNULL(functionType->getTypeParameters()).size() == 1);
  ZC_EXPECT(functionType->getParameters().size() == 1);
  ZC_EXPECT(functionType->getReturnType().getType().getKind() == SyntaxKind::UnionTypeNode);
}

ZC_TEST("TypeTest.OptionalComplexTypes") {
  // Test optional array type: i32[]?
  auto elementType = factory::createPredefinedType("i32"_zc);
  auto arrayType = factory::createArrayType(zc::mv(elementType));
  auto optionalArrayType = factory::createOptionalType(zc::mv(arrayType));

  ZC_EXPECT(optionalArrayType->getKind() == SyntaxKind::OptionalTypeNode);
  ZC_EXPECT(optionalArrayType->getType().getKind() == SyntaxKind::ArrayTypeNode);

  // Test optional function type
  zc::Vector<zc::Own<ParameterDeclaration>> params;
  auto returnType = factory::createReturnType(factory::createPredefinedType("unit"_zc), zc::none);
  auto functionType = factory::createFunctionType(zc::none, zc::mv(params), zc::mv(returnType));
  auto optionalFunctionType = factory::createOptionalType(zc::mv(functionType));

  ZC_EXPECT(optionalFunctionType->getKind() == SyntaxKind::OptionalTypeNode);
  ZC_EXPECT(optionalFunctionType->getType().getKind() == SyntaxKind::FunctionTypeNode);
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
