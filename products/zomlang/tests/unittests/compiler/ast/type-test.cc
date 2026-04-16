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

static void expectMutableFlagsRoundTrip(Node& node) {
  ZC_EXPECT(node.getFlags() == NodeFlags::None);
  node.setFlags(NodeFlags::OptionalChain);
  ZC_EXPECT(hasFlag(node.getFlags(), NodeFlags::OptionalChain));
}

static void expectNoOpFlags(Node& node) {
  ZC_EXPECT(node.getFlags() == NodeFlags::None);
  node.setFlags(NodeFlags::OptionalChain);
  ZC_EXPECT(node.getFlags() == NodeFlags::None);
}

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
  void visit(const ModuleDeclaration& moduleDecl) override {}
  void visit(const ModulePath& modulePath) override {}
  void visit(const ImportSpecifier& importSpecifier) override {}
  void visit(const ImportDeclaration& importDecl) override {}
  void visit(const ExportSpecifier& exportSpecifier) override {}
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

// ================================================================================
// Additional tests for comprehensive coverage
// ================================================================================

// Test IntersectionTypeNode with three types
ZC_TEST("TypeTest.IntersectionTypeNodeThreeTypes") {
  zc::Vector<zc::Own<TypeNode>> types;
  types.add(factory::createPredefinedType("i32"_zc));
  types.add(factory::createPredefinedType("str"_zc));
  types.add(factory::createPredefinedType("bool"_zc));
  auto type = factory::createIntersectionType(zc::mv(types));

  ZC_EXPECT(type->getKind() == SyntaxKind::IntersectionTypeNode);
  ZC_EXPECT(type->getTypes().size() == 3);
  ZC_EXPECT(type->getTypes()[0].getKind() == SyntaxKind::I32TypeNode);
  ZC_EXPECT(type->getTypes()[1].getKind() == SyntaxKind::StrTypeNode);
  ZC_EXPECT(type->getTypes()[2].getKind() == SyntaxKind::BoolTypeNode);
}

// Test IntersectionTypeNode with complex nested types
ZC_TEST("TypeTest.IntersectionTypeNodeNestedTypes") {
  zc::Vector<zc::Own<TypeNode>> types;

  // Add an array type
  auto elemType = factory::createPredefinedType("i32"_zc);
  types.add(factory::createArrayType(zc::mv(elemType)));

  // Add a parenthesized type
  auto innerType = factory::createPredefinedType("str"_zc);
  types.add(factory::createParenthesizedType(zc::mv(innerType)));

  auto type = factory::createIntersectionType(zc::mv(types));

  ZC_EXPECT(type->getKind() == SyntaxKind::IntersectionTypeNode);
  ZC_EXPECT(type->getTypes().size() == 2);
  ZC_EXPECT(type->getTypes()[0].getKind() == SyntaxKind::ArrayTypeNode);
  ZC_EXPECT(type->getTypes()[1].getKind() == SyntaxKind::ParenthesizedTypeNode);
}

// Test ParenthesizedTypeNode with union inner type
ZC_TEST("TypeTest.ParenthesizedTypeNodeWithUnionInner") {
  zc::Vector<zc::Own<TypeNode>> unionTypes;
  unionTypes.add(factory::createPredefinedType("i32"_zc));
  unionTypes.add(factory::createPredefinedType("str"_zc));
  auto innerUnion = factory::createUnionType(zc::mv(unionTypes));

  auto type = factory::createParenthesizedType(zc::mv(innerUnion));

  ZC_EXPECT(type->getKind() == SyntaxKind::ParenthesizedTypeNode);
  ZC_EXPECT(type->getType().getKind() == SyntaxKind::UnionTypeNode);
}

// ================================================================================
// PredefinedTypeNode getName() tests for all types
// ================================================================================

ZC_TEST("TypeTest.PredefinedTypeBool") {
  auto type = factory::createPredefinedType("bool"_zc);
  ZC_EXPECT(type->getKind() == SyntaxKind::BoolTypeNode);
  ZC_EXPECT(type->getName() == "bool");
}

ZC_TEST("TypeTest.PredefinedTypeI8") {
  auto type = factory::createPredefinedType("i8"_zc);
  ZC_EXPECT(type->getKind() == SyntaxKind::I8TypeNode);
  ZC_EXPECT(type->getName() == "i8");
}

ZC_TEST("TypeTest.PredefinedTypeI16") {
  auto type = factory::createPredefinedType("i16"_zc);
  ZC_EXPECT(type->getKind() == SyntaxKind::I16TypeNode);
  ZC_EXPECT(type->getName() == "i16");
}

ZC_TEST("TypeTest.PredefinedTypeI32") {
  auto type = factory::createPredefinedType("i32"_zc);
  ZC_EXPECT(type->getKind() == SyntaxKind::I32TypeNode);
  ZC_EXPECT(type->getName() == "i32");
}

ZC_TEST("TypeTest.PredefinedTypeI64") {
  auto type = factory::createPredefinedType("i64"_zc);
  ZC_EXPECT(type->getKind() == SyntaxKind::I64TypeNode);
  ZC_EXPECT(type->getName() == "i64");
}

ZC_TEST("TypeTest.PredefinedTypeU8") {
  auto type = factory::createPredefinedType("u8"_zc);
  ZC_EXPECT(type->getKind() == SyntaxKind::U8TypeNode);
  ZC_EXPECT(type->getName() == "u8");
}

ZC_TEST("TypeTest.PredefinedTypeU16") {
  auto type = factory::createPredefinedType("u16"_zc);
  ZC_EXPECT(type->getKind() == SyntaxKind::U16TypeNode);
  ZC_EXPECT(type->getName() == "u16");
}

ZC_TEST("TypeTest.PredefinedTypeU32") {
  auto type = factory::createPredefinedType("u32"_zc);
  ZC_EXPECT(type->getKind() == SyntaxKind::U32TypeNode);
  ZC_EXPECT(type->getName() == "u32");
}

ZC_TEST("TypeTest.PredefinedTypeU64") {
  auto type = factory::createPredefinedType("u64"_zc);
  ZC_EXPECT(type->getKind() == SyntaxKind::U64TypeNode);
  ZC_EXPECT(type->getName() == "u64");
}

ZC_TEST("TypeTest.PredefinedTypeF32") {
  auto type = factory::createPredefinedType("f32"_zc);
  ZC_EXPECT(type->getKind() == SyntaxKind::F32TypeNode);
  ZC_EXPECT(type->getName() == "f32");
}

ZC_TEST("TypeTest.PredefinedTypeF64") {
  auto type = factory::createPredefinedType("f64"_zc);
  ZC_EXPECT(type->getKind() == SyntaxKind::F64TypeNode);
  ZC_EXPECT(type->getName() == "f64");
}

ZC_TEST("TypeTest.PredefinedTypeStr") {
  auto type = factory::createPredefinedType("str"_zc);
  ZC_EXPECT(type->getKind() == SyntaxKind::StrTypeNode);
  ZC_EXPECT(type->getName() == "str");
}

ZC_TEST("TypeTest.PredefinedTypeUnit") {
  auto type = factory::createPredefinedType("unit"_zc);
  ZC_EXPECT(type->getKind() == SyntaxKind::UnitTypeNode);
  ZC_EXPECT(type->getName() == "unit");
}

ZC_TEST("TypeTest.PredefinedTypeNull") {
  auto type = factory::createPredefinedType("null"_zc);
  ZC_EXPECT(type->getKind() == SyntaxKind::NullTypeNode);
  ZC_EXPECT(type->getName() == "null");
}

// Test PredefinedTypeNode accept for BoolTypeNode
ZC_TEST("TypeTest.PredefinedTypeBoolAccept") {
  auto type = factory::createPredefinedType("bool"_zc);

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const BoolTypeNode& node) override { visited = true; }
  };

  TestVisitor visitor;
  type->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

// Test PredefinedTypeNode accept for I32TypeNode
ZC_TEST("TypeTest.PredefinedTypeI32Accept") {
  auto type = factory::createPredefinedType("i32"_zc);

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const I32TypeNode& node) override { visited = true; }
  };

  TestVisitor visitor;
  type->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

// Test PredefinedTypeNode accept for StrTypeNode
ZC_TEST("TypeTest.PredefinedTypeStrAccept") {
  auto type = factory::createPredefinedType("str"_zc);

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const StrTypeNode& node) override { visited = true; }
  };

  TestVisitor visitor;
  type->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

// Test PredefinedTypeNode accept for F64TypeNode
ZC_TEST("TypeTest.PredefinedTypeF64Accept") {
  auto type = factory::createPredefinedType("f64"_zc);

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const F64TypeNode& node) override { visited = true; }
  };

  TestVisitor visitor;
  type->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

// Test PredefinedTypeNode accept for NullTypeNode
ZC_TEST("TypeTest.PredefinedTypeNullAccept") {
  auto type = factory::createPredefinedType("null"_zc);

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const NullTypeNode& node) override { visited = true; }
  };

  TestVisitor visitor;
  type->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

// ================================================================================
// ObjectTypeNode with PropertySignature members
// ================================================================================

ZC_TEST("TypeTest.ObjectTypeNodeWithMembers") {
  zc::Vector<zc::Own<Node>> members;

  // Add a PropertySignature member: name: str
  auto propName = factory::createIdentifier("name"_zc);
  auto propType = factory::createPredefinedType("str"_zc);
  auto propSig =
      factory::createPropertySignature({}, zc::mv(propName), zc::none, zc::mv(propType), zc::none);
  members.add(zc::mv(propSig));

  // Add another PropertySignature member: age: i32
  auto ageName = factory::createIdentifier("age"_zc);
  auto ageType = factory::createPredefinedType("i32"_zc);
  auto ageSig =
      factory::createPropertySignature({}, zc::mv(ageName), zc::none, zc::mv(ageType), zc::none);
  members.add(zc::mv(ageSig));

  auto objectType = factory::createObjectType(zc::mv(members));

  ZC_EXPECT(objectType->getKind() == SyntaxKind::ObjectTypeNode);
  ZC_EXPECT(objectType->getMembers().size() == 2);
  ZC_EXPECT(objectType->getMembers()[0].getKind() == SyntaxKind::PropertySignature);
  ZC_EXPECT(objectType->getMembers()[1].getKind() == SyntaxKind::PropertySignature);
}

// Test ObjectTypeNode with optional property
ZC_TEST("TypeTest.ObjectTypeNodeWithOptionalProperty") {
  zc::Vector<zc::Own<Node>> members;

  auto propName = factory::createIdentifier("value"_zc);
  auto propType = factory::createPredefinedType("i32"_zc);
  auto questionToken = factory::createTokenNode(SyntaxKind::Question);
  auto propSig = factory::createPropertySignature({}, zc::mv(propName), zc::mv(questionToken),
                                                  zc::mv(propType), zc::none);
  members.add(zc::mv(propSig));

  auto objectType = factory::createObjectType(zc::mv(members));

  ZC_EXPECT(objectType->getKind() == SyntaxKind::ObjectTypeNode);
  ZC_EXPECT(objectType->getMembers().size() == 1);
  ZC_EXPECT(objectType->getMembers()[0].getKind() == SyntaxKind::PropertySignature);
}

// ================================================================================
// NamedTupleElement tests
// ================================================================================

ZC_TEST("TypeTest.NamedTupleElement") {
  auto name = factory::createIdentifier("x"_zc);
  auto type = factory::createPredefinedType("i32"_zc);
  auto elem = factory::createNamedTupleElement(zc::mv(name), zc::mv(type));

  ZC_EXPECT(elem->getKind() == SyntaxKind::NamedTupleElement);
  ZC_EXPECT(elem->getName().getText() == "x");
  ZC_EXPECT(elem->getType().getKind() == SyntaxKind::I32TypeNode);
}

ZC_TEST("TypeTest.NamedTupleElementWithComplexType") {
  auto name = factory::createIdentifier("items"_zc);
  auto innerType = factory::createPredefinedType("str"_zc);
  auto arrayType = factory::createArrayType(zc::mv(innerType));
  auto elem = factory::createNamedTupleElement(zc::mv(name), zc::mv(arrayType));

  ZC_EXPECT(elem->getKind() == SyntaxKind::NamedTupleElement);
  ZC_EXPECT(elem->getName().getText() == "items");
  ZC_EXPECT(elem->getType().getKind() == SyntaxKind::ArrayTypeNode);
}

// Test NamedTupleElement accept method
ZC_TEST("TypeTest.NamedTupleElementAccept") {
  auto name = factory::createIdentifier("field"_zc);
  auto type = factory::createPredefinedType("bool"_zc);
  auto elem = factory::createNamedTupleElement(zc::mv(name), zc::mv(type));

  struct TestVisitor : public BaseTestVisitor {
    bool visited = false;
    void visit(const NamedTupleElement& node) override { visited = true; }
  };

  TestVisitor visitor;
  elem->accept(visitor);
  ZC_EXPECT(visitor.visited);
}

// ================================================================================
// TupleTypeNode with NamedTupleElement
// ================================================================================

ZC_TEST("TypeTest.TupleTypeWithNamedElements") {
  zc::Vector<zc::Own<TypeNode>> elementTypes;

  auto elem1Name = factory::createIdentifier("x"_zc);
  auto elem1Type = factory::createPredefinedType("i32"_zc);
  elementTypes.add(factory::createNamedTupleElement(zc::mv(elem1Name), zc::mv(elem1Type)));

  auto elem2Name = factory::createIdentifier("y"_zc);
  auto elem2Type = factory::createPredefinedType("str"_zc);
  elementTypes.add(factory::createNamedTupleElement(zc::mv(elem2Name), zc::mv(elem2Type)));

  auto tupleType = factory::createTupleType(zc::mv(elementTypes));

  ZC_EXPECT(tupleType->getKind() == SyntaxKind::TupleTypeNode);
  ZC_EXPECT(tupleType->getElementTypes().size() == 2);
  ZC_EXPECT(tupleType->getElementTypes()[0].getKind() == SyntaxKind::NamedTupleElement);
  ZC_EXPECT(tupleType->getElementTypes()[1].getKind() == SyntaxKind::NamedTupleElement);
}

// ================================================================================
// ReturnTypeNode additional tests
// ================================================================================

// Test ReturnTypeNode with complex return type (union)
ZC_TEST("TypeTest.ReturnTypeNodeWithUnionType") {
  zc::Vector<zc::Own<TypeNode>> unionTypes;
  unionTypes.add(factory::createPredefinedType("i32"_zc));
  unionTypes.add(factory::createPredefinedType("null"_zc));
  auto unionType = factory::createUnionType(zc::mv(unionTypes));

  auto returnType = factory::createReturnType(zc::mv(unionType), zc::none);

  ZC_EXPECT(returnType->getKind() == SyntaxKind::ReturnTypeNode);
  ZC_EXPECT(returnType->getType().getKind() == SyntaxKind::UnionTypeNode);
  ZC_EXPECT(returnType->getErrorType() == zc::none);
}

// Test ReturnTypeNode with predefined error type
ZC_TEST("TypeTest.ReturnTypeNodeWithPredefinedErrorType") {
  auto errorType = factory::createPredefinedType("str"_zc);
  auto returnType =
      factory::createReturnType(factory::createPredefinedType("bool"_zc), zc::mv(errorType));

  ZC_EXPECT(returnType->getKind() == SyntaxKind::ReturnTypeNode);
  ZC_EXPECT(returnType->getType().getKind() == SyntaxKind::BoolTypeNode);
  ZC_IF_SOME(err, returnType->getErrorType()) {
    ZC_EXPECT(err.getKind() == SyntaxKind::StrTypeNode);
  }
  else { ZC_FAIL_EXPECT("Expected error type to be present"); }
}

// ================================================================================
// FunctionTypeNode additional tests
// ================================================================================

// Test FunctionTypeNode with type parameters
ZC_TEST("TypeTest.FunctionTypeWithTypeParameters") {
  zc::Vector<zc::Own<TypeParameterDeclaration>> typeParams;
  auto tpName1 = factory::createIdentifier("T"_zc);
  typeParams.add(factory::createTypeParameterDeclaration(zc::mv(tpName1), zc::none));

  auto tpName2 = factory::createIdentifier("U"_zc);
  typeParams.add(factory::createTypeParameterDeclaration(zc::mv(tpName2), zc::none));

  zc::Vector<zc::Own<ParameterDeclaration>> params;

  auto returnType = factory::createReturnType(factory::createPredefinedType("unit"_zc), zc::none);
  auto type = factory::createFunctionType(zc::mv(typeParams), zc::mv(params), zc::mv(returnType));

  ZC_EXPECT(type->getKind() == SyntaxKind::FunctionTypeNode);
  ZC_EXPECT(type->getParameters().size() == 0);
  ZC_ASSERT(ZC_ASSERT_NONNULL(type->getTypeParameters()).size() == 2);
  ZC_EXPECT(type->getReturnType().getType().getKind() == SyntaxKind::UnitTypeNode);
}

// Test FunctionTypeNode with multiple parameters
ZC_TEST("TypeTest.FunctionTypeWithMultipleParameters") {
  zc::Vector<zc::Own<ParameterDeclaration>> params;

  auto param1 =
      factory::createParameterDeclaration({}, zc::none, factory::createIdentifier("a"_zc), zc::none,
                                          factory::createPredefinedType("i32"_zc), zc::none);
  params.add(zc::mv(param1));

  auto param2 =
      factory::createParameterDeclaration({}, zc::none, factory::createIdentifier("b"_zc), zc::none,
                                          factory::createPredefinedType("str"_zc), zc::none);
  params.add(zc::mv(param2));

  auto param3 =
      factory::createParameterDeclaration({}, zc::none, factory::createIdentifier("c"_zc), zc::none,
                                          factory::createPredefinedType("bool"_zc), zc::none);
  params.add(zc::mv(param3));

  auto returnType = factory::createReturnType(factory::createPredefinedType("f64"_zc), zc::none);
  auto type = factory::createFunctionType(zc::none, zc::mv(params), zc::mv(returnType));

  ZC_EXPECT(type->getKind() == SyntaxKind::FunctionTypeNode);
  ZC_EXPECT(type->getParameters().size() == 3);
  ZC_EXPECT(type->getTypeParameters() == zc::none);
  ZC_EXPECT(type->getReturnType().getType().getKind() == SyntaxKind::F64TypeNode);
}

// Test FunctionTypeNode with error return type
ZC_TEST("TypeTest.FunctionTypeWithErrorReturnType") {
  zc::Vector<zc::Own<ParameterDeclaration>> params;

  auto returnType = factory::createReturnType(factory::createPredefinedType("i32"_zc),
                                              factory::createPredefinedType("str"_zc));
  auto type = factory::createFunctionType(zc::none, zc::mv(params), zc::mv(returnType));

  ZC_EXPECT(type->getKind() == SyntaxKind::FunctionTypeNode);
  ZC_EXPECT(type->getReturnType().getType().getKind() == SyntaxKind::I32TypeNode);
  ZC_IF_SOME(err, type->getReturnType().getErrorType()) {
    ZC_EXPECT(err.getKind() == SyntaxKind::StrTypeNode);
  }
  else { ZC_FAIL_EXPECT("Expected error type in return type"); }
}

// ================================================================================
// OptionalTypeNode additional tests
// ================================================================================

// Test OptionalTypeNode with predefined type
ZC_TEST("TypeTest.OptionalTypeWithPredefinedType") {
  auto baseType = factory::createPredefinedType("str"_zc);
  auto type = factory::createOptionalType(zc::mv(baseType));

  ZC_EXPECT(type->getKind() == SyntaxKind::OptionalTypeNode);
  ZC_EXPECT(type->getType().getKind() == SyntaxKind::StrTypeNode);
}

// Test OptionalTypeNode with union type
ZC_TEST("TypeTest.OptionalTypeWithUnionType") {
  zc::Vector<zc::Own<TypeNode>> unionTypes;
  unionTypes.add(factory::createPredefinedType("i32"_zc));
  unionTypes.add(factory::createPredefinedType("str"_zc));
  auto unionType = factory::createUnionType(zc::mv(unionTypes));

  auto optionalType = factory::createOptionalType(zc::mv(unionType));

  ZC_EXPECT(optionalType->getKind() == SyntaxKind::OptionalTypeNode);
  ZC_EXPECT(optionalType->getType().getKind() == SyntaxKind::UnionTypeNode);
}

// Test nested optional type
ZC_TEST("TypeTest.NestedOptionalType") {
  auto innerBase = factory::createPredefinedType("i32"_zc);
  auto innerOptional = factory::createOptionalType(zc::mv(innerBase));
  auto outerOptional = factory::createOptionalType(zc::mv(innerOptional));

  ZC_EXPECT(outerOptional->getKind() == SyntaxKind::OptionalTypeNode);
  ZC_EXPECT(outerOptional->getType().getKind() == SyntaxKind::OptionalTypeNode);
}

// ================================================================================
// TypeQueryNode additional tests
// ================================================================================

// Test TypeQueryNode with property access expression
ZC_TEST("TypeTest.TypeQueryWithPropertyAccess") {
  auto obj = factory::createIdentifier("obj"_zc);
  auto prop = factory::createIdentifier("field"_zc);
  auto expr = factory::createPropertyAccessExpression(zc::mv(obj), zc::mv(prop));
  auto type = factory::createTypeQuery(zc::mv(expr));

  ZC_EXPECT(type->getKind() == SyntaxKind::TypeQueryNode);
  ZC_EXPECT(type->getExpression().getKind() == SyntaxKind::PropertyAccessExpression);
}

// ================================================================================
// IntersectionTypeNode accept with concrete type verification
// ================================================================================

// Test that each intersection member can be cast to its concrete type
ZC_TEST("TypeTest.IntersectionTypeNodeMemberCasting") {
  zc::Vector<zc::Own<TypeNode>> types;
  types.add(factory::createPredefinedType("i32"_zc));
  types.add(factory::createTypeReference(factory::createIdentifier("Serializable"_zc), zc::none));
  auto type = factory::createIntersectionType(zc::mv(types));

  ZC_EXPECT(type->getTypes().size() == 2);

  // Verify first member can be cast to PredefinedTypeNode
  const auto& first = type->getTypes()[0];
  ZC_EXPECT(first.getKind() == SyntaxKind::I32TypeNode);

  // Verify second member is a TypeReferenceNode
  const auto& second = type->getTypes()[1];
  ZC_EXPECT(second.getKind() == SyntaxKind::TypeReferenceNode);
}

// ================================================================================
// TupleTypeNode with mixed element types
// ================================================================================

ZC_TEST("TypeTest.TupleTypeNodeMixedElements") {
  zc::Vector<zc::Own<TypeNode>> elementTypes;
  elementTypes.add(factory::createPredefinedType("i32"_zc));
  elementTypes.add(factory::createPredefinedType("str"_zc));
  elementTypes.add(factory::createPredefinedType("bool"_zc));
  elementTypes.add(factory::createPredefinedType("f64"_zc));

  auto type = factory::createTupleType(zc::mv(elementTypes));

  ZC_EXPECT(type->getKind() == SyntaxKind::TupleTypeNode);
  ZC_EXPECT(type->getElementTypes().size() == 4);
  ZC_EXPECT(type->getElementTypes()[0].getKind() == SyntaxKind::I32TypeNode);
  ZC_EXPECT(type->getElementTypes()[1].getKind() == SyntaxKind::StrTypeNode);
  ZC_EXPECT(type->getElementTypes()[2].getKind() == SyntaxKind::BoolTypeNode);
  ZC_EXPECT(type->getElementTypes()[3].getKind() == SyntaxKind::F64TypeNode);
}

// ================================================================================
// ParenthesizedTypeNode with nested parenthesized type
// ================================================================================

ZC_TEST("TypeTest.NestedParenthesizedType") {
  auto innermost = factory::createPredefinedType("i32"_zc);
  auto inner = factory::createParenthesizedType(zc::mv(innermost));
  auto outer = factory::createParenthesizedType(zc::mv(inner));

  ZC_EXPECT(outer->getKind() == SyntaxKind::ParenthesizedTypeNode);
  ZC_EXPECT(outer->getType().getKind() == SyntaxKind::ParenthesizedTypeNode);

  // Verify the innermost type through casting
  const auto& innerType = ast::cast<ParenthesizedTypeNode>(outer->getType());
  ZC_EXPECT(innerType.getType().getKind() == SyntaxKind::I32TypeNode);
}

// ================================================================================
// UnionTypeNode accept with all unique type members
// ================================================================================

ZC_TEST("TypeTest.UnionTypeNodeAllPredefinedTypes") {
  zc::Vector<zc::Own<TypeNode>> types;
  types.add(factory::createPredefinedType("i32"_zc));
  types.add(factory::createPredefinedType("str"_zc));
  types.add(factory::createPredefinedType("bool"_zc));
  types.add(factory::createPredefinedType("f64"_zc));
  types.add(factory::createPredefinedType("unit"_zc));

  auto type = factory::createUnionType(zc::mv(types));

  ZC_EXPECT(type->getKind() == SyntaxKind::UnionTypeNode);
  ZC_EXPECT(type->getTypes().size() == 5);
}

ZC_TEST("TypeTest.TypeNodeFlagsRoundTrip") {
  auto typeReference =
      factory::createTypeReference(factory::createIdentifier("Vector"_zc), zc::none);
  expectMutableFlagsRoundTrip(*typeReference);

  auto arrayType = factory::createArrayType(factory::createPredefinedType("i32"_zc));
  expectMutableFlagsRoundTrip(*arrayType);

  zc::Vector<zc::Own<TypeNode>> unionTypes;
  unionTypes.add(factory::createPredefinedType("i32"_zc));
  unionTypes.add(factory::createPredefinedType("str"_zc));
  auto unionType = factory::createUnionType(zc::mv(unionTypes));
  expectMutableFlagsRoundTrip(*unionType);

  zc::Vector<zc::Own<TypeNode>> intersectionTypes;
  intersectionTypes.add(factory::createPredefinedType("bool"_zc));
  intersectionTypes.add(factory::createPredefinedType("str"_zc));
  auto intersectionType = factory::createIntersectionType(zc::mv(intersectionTypes));
  expectMutableFlagsRoundTrip(*intersectionType);

  auto parenthesizedType =
      factory::createParenthesizedType(factory::createPredefinedType("u32"_zc));
  expectMutableFlagsRoundTrip(*parenthesizedType);

  auto objectType = factory::createObjectType(zc::Vector<zc::Own<Node>>());
  expectNoOpFlags(*objectType);

  auto namedTupleElement = factory::createNamedTupleElement(
      factory::createIdentifier("field"_zc), factory::createPredefinedType("str"_zc));
  expectMutableFlagsRoundTrip(*namedTupleElement);

  zc::Vector<zc::Own<TypeNode>> tupleElements;
  tupleElements.add(factory::createPredefinedType("i32"_zc));
  tupleElements.add(factory::createPredefinedType("bool"_zc));
  auto tupleType = factory::createTupleType(zc::mv(tupleElements));
  expectMutableFlagsRoundTrip(*tupleType);

  auto returnType = factory::createReturnType(factory::createPredefinedType("unit"_zc), zc::none);
  expectMutableFlagsRoundTrip(*returnType);

  auto functionReturnType =
      factory::createReturnType(factory::createPredefinedType("i32"_zc), zc::none);
  auto functionType = factory::createFunctionType(
      zc::none, zc::Vector<zc::Own<ParameterDeclaration>>(), zc::mv(functionReturnType));
  expectMutableFlagsRoundTrip(*functionType);

  auto optionalType = factory::createOptionalType(factory::createPredefinedType("i64"_zc));
  expectMutableFlagsRoundTrip(*optionalType);

  auto typeQuery = factory::createTypeQuery(factory::createIdentifier("value"_zc));
  expectMutableFlagsRoundTrip(*typeQuery);

  auto typeParameter = factory::createTypeParameterDeclaration(
      factory::createIdentifier("T"_zc), factory::createPredefinedType("i32"_zc));
  expectMutableFlagsRoundTrip(*typeParameter);
}

ZC_TEST("TypeTest.PredefinedTypeFlagsRemainNone") {
  expectNoOpFlags(*factory::createPredefinedType("bool"_zc));
  expectNoOpFlags(*factory::createPredefinedType("i8"_zc));
  expectNoOpFlags(*factory::createPredefinedType("i16"_zc));
  expectNoOpFlags(*factory::createPredefinedType("i32"_zc));
  expectNoOpFlags(*factory::createPredefinedType("i64"_zc));
  expectNoOpFlags(*factory::createPredefinedType("u8"_zc));
  expectNoOpFlags(*factory::createPredefinedType("u16"_zc));
  expectNoOpFlags(*factory::createPredefinedType("u32"_zc));
  expectNoOpFlags(*factory::createPredefinedType("u64"_zc));
  expectNoOpFlags(*factory::createPredefinedType("f32"_zc));
  expectNoOpFlags(*factory::createPredefinedType("f64"_zc));
  expectNoOpFlags(*factory::createPredefinedType("str"_zc));
  expectNoOpFlags(*factory::createPredefinedType("unit"_zc));
  expectNoOpFlags(*factory::createPredefinedType("null"_zc));
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
