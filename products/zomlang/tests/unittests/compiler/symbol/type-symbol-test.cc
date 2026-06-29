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

#include "zomlang/compiler/symbol/type-symbol.h"

#include "zc/core/debug.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/source/location.h"
#include "zomlang/compiler/symbol/scope.h"
#include "zomlang/compiler/symbol/symbol-table.h"

namespace zomlang {
namespace compiler {
namespace symbol {

ZC_TEST("TypeSymbol_ClassCreation") {
  SymbolTable symbolTable;
  ScopeManager& scopeManager = symbolTable.getScopeManager();
  const Scope& globalScope = scopeManager.createScope(Scope::Kind::Global, "global");

  auto& classSymbol = symbolTable.createClass("TestClass", globalScope);

  ZC_EXPECT(classSymbol.getName() == "TestClass");
  ZC_EXPECT(classSymbol.getKind() == SymbolKind::Class);
}

ZC_TEST("TypeSymbol_InterfaceCreation") {
  SymbolTable symbolTable;
  ScopeManager& scopeManager = symbolTable.getScopeManager();
  const Scope& globalScope = scopeManager.createScope(Scope::Kind::Global, "global");

  auto& interfaceSymbol = symbolTable.createInterface("TestInterface", globalScope);

  ZC_EXPECT(interfaceSymbol.getName() == "TestInterface");
  ZC_EXPECT(interfaceSymbol.getKind() == SymbolKind::Interface);
}

ZC_TEST("ClassSymbol_MemberManagement") {
  SymbolTable table;
  ScopeManager& scopeManager = table.getScopeManager();
  const Scope& globalScope = scopeManager.createScope(Scope::Kind::Global, "global");

  // Create a class symbol
  ClassSymbol& classSymbol = table.createClass("TestClass", globalScope);
  ZC_EXPECT(classSymbol.getName() == "TestClass");
  ZC_EXPECT(classSymbol.getKind() == SymbolKind::Class);

  // Test initial state
  ZC_EXPECT(classSymbol.getMembers().size() == 0);
  ZC_EXPECT(classSymbol.getConstructors().size() == 0);
  ZC_EXPECT(classSymbol.getInterfaces().size() == 0);
  ZC_EXPECT(classSymbol.getSuperclass() == zc::none);
}

ZC_TEST("ClassSymbol_Hierarchy") {
  SymbolTable table;
  ScopeManager& scopeManager = table.getScopeManager();
  const Scope& globalScope = scopeManager.createScope(Scope::Kind::Global, "global");

  // Create class symbols
  ClassSymbol& baseClass = table.createClass("BaseClass", globalScope);
  ClassSymbol& derivedClass = table.createClass("DerivedClass", globalScope);
  InterfaceSymbol& interfaceSymbol = table.createInterface("TestInterface", globalScope);

  // Verify symbol kinds using SymbolKind instead of dynamic_cast
  ZC_EXPECT(baseClass.getKind() == SymbolKind::Class);
  ZC_EXPECT(derivedClass.getKind() == SymbolKind::Class);
  ZC_EXPECT(interfaceSymbol.getKind() == SymbolKind::Interface);

  // Set up inheritance
  derivedClass.setSuperclass(baseClass);

  // Test inheritance
  auto superclass = derivedClass.getSuperclass();
  ZC_EXPECT(superclass != zc::none);
  ZC_EXPECT(&ZC_ASSERT_NONNULL(superclass) == &baseClass);
}

ZC_TEST("BuiltInTypeSymbol_Creation") {
  source::SourceLoc defaultLoc;

  auto intType = BuiltInTypeSymbol::createI32(SymbolId::create(1), defaultLoc);
  ZC_EXPECT(intType->getName() == "i32");
  ZC_EXPECT(intType->getKind() == SymbolKind::Type);
  ZC_EXPECT(intType->isPrimitive());
  ZC_EXPECT(intType->isNumeric());
  ZC_EXPECT(!intType->isString());
  ZC_EXPECT(!intType->isBoolean());
  ZC_EXPECT(!intType->isVoid());

  auto floatType = BuiltInTypeSymbol::createF32(SymbolId::create(2), defaultLoc);
  ZC_EXPECT(floatType->getName() == "f32");
  ZC_EXPECT(floatType->getKind() == SymbolKind::Type);
  ZC_EXPECT(floatType->isPrimitive());
  ZC_EXPECT(floatType->isNumeric());

  auto stringType = BuiltInTypeSymbol::createStr(SymbolId::create(3), defaultLoc);
  ZC_EXPECT(stringType->getName() == "str");
  ZC_EXPECT(stringType->getKind() == SymbolKind::Type);
  ZC_EXPECT(stringType->isPrimitive());
  ZC_EXPECT(stringType->isString());
  ZC_EXPECT(!stringType->isNumeric());

  auto boolType = BuiltInTypeSymbol::createBool(SymbolId::create(4), defaultLoc);
  ZC_EXPECT(boolType->getName() == "bool");
  ZC_EXPECT(boolType->getKind() == SymbolKind::Type);
  ZC_EXPECT(boolType->isPrimitive());
  ZC_EXPECT(boolType->isBoolean());
  ZC_EXPECT(!boolType->isNumeric());

  auto voidType = BuiltInTypeSymbol::createUnit(SymbolId::create(5), defaultLoc);
  ZC_EXPECT(voidType->getName() == "unit");
  ZC_EXPECT(voidType->getKind() == SymbolKind::Type);
  ZC_EXPECT(voidType->isPrimitive());
  ZC_EXPECT(voidType->isVoid());
  ZC_EXPECT(!voidType->isNumeric());
}

ZC_TEST("BuiltInTypeSymbol_Properties") {
  source::SourceLoc defaultLoc;

  auto intType = BuiltInTypeSymbol::createI32(SymbolId::create(1), defaultLoc);
  auto floatType = BuiltInTypeSymbol::createF32(SymbolId::create(2), defaultLoc);
  auto stringType = BuiltInTypeSymbol::createStr(SymbolId::create(3), defaultLoc);
  auto boolType = BuiltInTypeSymbol::createBool(SymbolId::create(4), defaultLoc);
  auto voidType = BuiltInTypeSymbol::createUnit(SymbolId::create(5), defaultLoc);

  // Test builtin flag
  ZC_EXPECT(intType->hasFlag(SymbolFlags::Builtin));
  ZC_EXPECT(floatType->hasFlag(SymbolFlags::Builtin));
  ZC_EXPECT(stringType->hasFlag(SymbolFlags::Builtin));
  ZC_EXPECT(boolType->hasFlag(SymbolFlags::Builtin));
  ZC_EXPECT(voidType->hasFlag(SymbolFlags::Builtin));

  // Test type kind flag
  ZC_EXPECT(intType->hasFlag(SymbolFlags::TypeKind));
  ZC_EXPECT(floatType->hasFlag(SymbolFlags::TypeKind));
  ZC_EXPECT(stringType->hasFlag(SymbolFlags::TypeKind));
  ZC_EXPECT(boolType->hasFlag(SymbolFlags::TypeKind));
  ZC_EXPECT(voidType->hasFlag(SymbolFlags::TypeKind));

  // Test not term kind
  ZC_EXPECT(!intType->hasFlag(SymbolFlags::TermKind));
  ZC_EXPECT(!floatType->hasFlag(SymbolFlags::TermKind));
  ZC_EXPECT(!stringType->hasFlag(SymbolFlags::TermKind));
  ZC_EXPECT(!boolType->hasFlag(SymbolFlags::TermKind));
  ZC_EXPECT(!voidType->hasFlag(SymbolFlags::TermKind));
}

ZC_TEST("TypeSymbol_SymbolFlags") {
  source::SourceLoc defaultLoc;

  // Test basic type flags
  auto intType = BuiltInTypeSymbol::createI32(SymbolId::create(1), defaultLoc);
  ZC_EXPECT(intType->hasFlag(SymbolFlags::TypeKind));
  ZC_EXPECT(!intType->hasFlag(SymbolFlags::TermKind));
  ZC_EXPECT(intType->hasFlag(SymbolFlags::Builtin));

  // Test class with different flags
  SymbolTable symbolTable;
  auto globalScope = Scope::createGlobalScope();
  auto& classSymbol = symbolTable.createClass("TestClass"_zc, *globalScope);

  ZC_EXPECT(classSymbol.hasFlag(SymbolFlags::TypeKind));
  ZC_EXPECT(classSymbol.hasFlag(SymbolFlags::Class));
  ZC_EXPECT(!classSymbol.hasFlag(SymbolFlags::Interface));
  ZC_EXPECT(!classSymbol.hasFlag(SymbolFlags::TermKind));
}

ZC_TEST("TypeSymbol_VisibilityFlags") {
  SymbolTable symbolTable;
  auto globalScope = Scope::createGlobalScope();

  // Create class with public visibility
  auto& publicClass = symbolTable.createClass("PublicClass"_zc, *globalScope);
  publicClass.addFlag(SymbolFlags::Public);

  // Create interface with private visibility
  auto& privateInterface = symbolTable.createInterface("PrivateInterface"_zc, *globalScope);
  privateInterface.addFlag(SymbolFlags::Private);

  // Create class with protected visibility
  auto& protectedClass = symbolTable.createClass("ProtectedClass"_zc, *globalScope);
  protectedClass.addFlag(SymbolFlags::Protected);

  ZC_EXPECT(publicClass.isPublic());
  ZC_EXPECT(!publicClass.isPrivate());
  ZC_EXPECT(!publicClass.isProtected());

  ZC_EXPECT(!privateInterface.isPublic());
  ZC_EXPECT(privateInterface.isPrivate());
  ZC_EXPECT(!privateInterface.isProtected());

  ZC_EXPECT(!protectedClass.isPublic());
  ZC_EXPECT(!protectedClass.isPrivate());
  ZC_EXPECT(protectedClass.isProtected());
}

ZC_TEST("TypeSymbol_InheritanceFlags") {
  SymbolTable symbolTable;
  auto globalScope = Scope::createGlobalScope();

  // Create abstract class
  auto& abstractClass = symbolTable.createClass("AbstractClass"_zc, *globalScope);
  abstractClass.addFlag(SymbolFlags::Abstract);

  // Create final class
  auto& finalClass = symbolTable.createClass("FinalClass"_zc, *globalScope);
  finalClass.addFlag(SymbolFlags::Final);

  // Create sealed class (test flag but not method since isSealed() doesn't exist)
  auto& sealedClass = symbolTable.createClass("SealedClass"_zc, *globalScope);
  sealedClass.addFlag(SymbolFlags::Sealed);

  ZC_EXPECT(abstractClass.isAbstract());
  ZC_EXPECT(!abstractClass.isFinal());
  ZC_EXPECT(abstractClass.hasFlag(SymbolFlags::Abstract));

  ZC_EXPECT(!finalClass.isAbstract());
  ZC_EXPECT(finalClass.isFinal());
  ZC_EXPECT(finalClass.hasFlag(SymbolFlags::Final));

  // Test sealed flag (no isSealed() method available)
  ZC_EXPECT(!sealedClass.isAbstract());
  ZC_EXPECT(!sealedClass.isFinal());
  ZC_EXPECT(sealedClass.hasFlag(SymbolFlags::Sealed));
}

ZC_TEST("TypeSymbol_GenericFlags") {
  SymbolTable symbolTable;
  auto globalScope = Scope::createGlobalScope();

  // Create generic class with Generic flag
  auto& genericClass = symbolTable.createClass("GenericClass"_zc, *globalScope);
  genericClass.addFlag(SymbolFlags::Generic);

  // Create generic interface with Generic flag
  auto& genericInterface = symbolTable.createInterface("GenericInterface"_zc, *globalScope);
  genericInterface.addFlag(SymbolFlags::Generic);

  // Test flag checking (isGeneric() checks typeParameters.size(), not the flag)
  ZC_EXPECT(genericClass.hasFlag(SymbolFlags::Generic));
  ZC_EXPECT(genericInterface.hasFlag(SymbolFlags::Generic));

  // Note: isGeneric() method checks typeParameters.size() != 0, not the Generic flag
  // So we test the flag directly instead
  ZC_EXPECT(!genericClass.isGeneric());      // No type parameters added yet
  ZC_EXPECT(!genericInterface.isGeneric());  // No type parameters added yet
}

ZC_TEST("TypeParameterSymbol_Creation") {
  source::SourceLoc defaultLoc;
  TypeParameterSymbol typeParam(SymbolId::create(1), "T"_zc,
                                SymbolFlags::TypeKind | SymbolFlags::Generic, defaultLoc);

  ZC_EXPECT(typeParam.getName() == "T");
  ZC_EXPECT(typeParam.getKind() == SymbolKind::Type);
  // Note: isGeneric() checks typeParameters.size() != 0, not the Generic flag
  ZC_EXPECT(!typeParam.isGeneric());  // No type parameters added to this type parameter
  ZC_EXPECT(typeParam.hasFlag(SymbolFlags::TypeKind));
  ZC_EXPECT(typeParam.hasFlag(SymbolFlags::Generic));
}

ZC_TEST("TypeParameterSymbol_Variance") {
  source::SourceLoc defaultLoc;

  // Test covariant type parameter
  TypeParameterSymbol covariantParam(SymbolId::create(1), "T"_zc,
                                     SymbolFlags::TypeKind | SymbolFlags::Covariant, defaultLoc);
  covariantParam.setVariance(TypeParameterSymbol::Variance::Covariant);

  // Test contravariant type parameter
  TypeParameterSymbol contravariantParam(
      SymbolId::create(2), "U"_zc, SymbolFlags::TypeKind | SymbolFlags::Contravariant, defaultLoc);
  contravariantParam.setVariance(TypeParameterSymbol::Variance::Contravariant);

  // Test invariant type parameter
  TypeParameterSymbol invariantParam(SymbolId::create(3), "V"_zc,
                                     SymbolFlags::TypeKind | SymbolFlags::Invariant, defaultLoc);
  invariantParam.setVariance(TypeParameterSymbol::Variance::Invariant);

  ZC_EXPECT(covariantParam.getVariance() == TypeParameterSymbol::Variance::Covariant);
  ZC_EXPECT(covariantParam.hasFlag(SymbolFlags::Covariant));

  ZC_EXPECT(contravariantParam.getVariance() == TypeParameterSymbol::Variance::Contravariant);
  ZC_EXPECT(contravariantParam.hasFlag(SymbolFlags::Contravariant));

  ZC_EXPECT(invariantParam.getVariance() == TypeParameterSymbol::Variance::Invariant);
  ZC_EXPECT(invariantParam.hasFlag(SymbolFlags::Invariant));
}

ZC_TEST("FunctionTypeSymbol_Creation") {
  source::SourceLoc defaultLoc;
  FunctionTypeSymbol funcType(SymbolId::create(1), "TestFunction"_zc,
                              SymbolFlags::TypeKind | SymbolFlags::Function, defaultLoc);

  ZC_EXPECT(funcType.getName() == "TestFunction");
  ZC_EXPECT(funcType.getKind() == SymbolKind::Type);
  ZC_EXPECT(funcType.isFunction());
  ZC_EXPECT(funcType.hasFlag(SymbolFlags::TypeKind));
  ZC_EXPECT(funcType.hasFlag(SymbolFlags::Function));
  ZC_EXPECT(!funcType.isVariadic());
}

ZC_TEST("FunctionTypeSymbol_Parameters") {
  source::SourceLoc defaultLoc;
  FunctionTypeSymbol funcType(SymbolId::create(1), "TestFunction"_zc,
                              SymbolFlags::TypeKind | SymbolFlags::Function, defaultLoc);

  // Set return type
  auto returnType = BuiltInTypeSymbol::createI32(SymbolId::create(2), defaultLoc);
  funcType.setReturnType(*returnType);

  // Add parameter types
  auto param1Type = BuiltInTypeSymbol::createStr(SymbolId::create(3), defaultLoc);
  auto param2Type = BuiltInTypeSymbol::createBool(SymbolId::create(4), defaultLoc);

  funcType.addParameterType(*param1Type);
  funcType.addParameterType(*param2Type);

  // Test variadic function
  funcType.setVariadic(true);
  ZC_EXPECT(funcType.isVariadic());

  auto paramTypes = funcType.getParameterTypes();
  ZC_EXPECT(paramTypes.size() == 2);

  ZC_IF_SOME(returnTypeRef, funcType.getReturnType()) {
    ZC_EXPECT(returnTypeRef.getName() == "i32");
  }
  else { ZC_FAIL_REQUIRE("Should have return type"); }
}

ZC_TEST("TypeSymbol_Classification") {
  source::SourceLoc defaultLoc;
  auto intType = BuiltInTypeSymbol::createI32(SymbolId::create(1), defaultLoc);

  ZC_EXPECT(intType->isPrimitive());
  ZC_EXPECT(!intType->isClass());
  ZC_EXPECT(!intType->isInterface());
  ZC_EXPECT(!intType->isGeneric());
  ZC_EXPECT(!intType->isAlias());
  ZC_EXPECT(!intType->isFunction());
}

ZC_TEST("TypeSymbol_SymbolTypeChecking") {
  source::SourceLoc defaultLoc;
  auto intType = BuiltInTypeSymbol::createI32(SymbolId::create(1), defaultLoc);

  ZC_EXPECT(intType->isTypeSymbol());
  ZC_EXPECT(intType->getKind() == SymbolKind::Type);
  ZC_EXPECT(intType->isType());
}

ZC_TEST("InterfaceSymbol_Creation") {
  SymbolTable symbolTable;
  auto globalScope = Scope::createGlobalScope();
  auto& interface = symbolTable.createInterface("ITestInterface"_zc, *globalScope);

  ZC_EXPECT(interface.getName() == "ITestInterface");
  ZC_EXPECT(interface.isInterface());
  ZC_EXPECT(!interface.isClass());
  ZC_EXPECT(interface.getKind() == SymbolKind::Interface);
}

ZC_TEST("ClassSymbol_Creation") {
  SymbolTable symbolTable;
  auto globalScope = Scope::createGlobalScope();
  auto& classSymbol = symbolTable.createClass("TestClass"_zc, *globalScope);

  ZC_EXPECT(classSymbol.getName() == "TestClass");
  ZC_EXPECT(classSymbol.isClass());
  ZC_EXPECT(!classSymbol.isInterface());
  ZC_EXPECT(classSymbol.getKind() == SymbolKind::Class);
}

ZC_TEST("ClassSymbol_Hierarchy") {
  SymbolTable symbolTable;
  auto globalScope = Scope::createGlobalScope();
  auto& baseClass = symbolTable.createClass("BaseClass"_zc, *globalScope);
  auto& derivedClass = symbolTable.createClass("DerivedClass"_zc, *globalScope);

  // Test initial state
  ZC_EXPECT(derivedClass.getSuperclass() == zc::none);

  // Test setting superclass
  derivedClass.setSuperclass(baseClass);
  auto superclass = derivedClass.getSuperclass();
  ZC_EXPECT(superclass != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(superclass).getName() == "BaseClass");
}

ZC_TEST("ClassSymbol_MemberManagement") {
  SymbolTable symbolTable;
  auto globalScope = Scope::createGlobalScope();
  auto& classSymbol = symbolTable.createClass("TestClass"_zc, *globalScope);

  // Test initial state
  auto members = classSymbol.getMembers();
  ZC_EXPECT(members.size() == 0);

  auto constructors = classSymbol.getConstructors();
  ZC_EXPECT(constructors.size() == 0);
}

ZC_TEST("TypeSymbol_GenericSupport") {
  source::SourceLoc defaultLoc;
  auto intType = BuiltInTypeSymbol::createI32(SymbolId::create(1), defaultLoc);

  // Test type parameters
  auto typeParams = intType->getTypeParameters();
  ZC_EXPECT(typeParams.size() == 0);

  // Test declaring type
  ZC_EXPECT(intType->getDeclaringType() == zc::none);
}

ZC_TEST("TypeSymbol_QualifiedName") {
  source::SourceLoc defaultLoc;
  auto intType = BuiltInTypeSymbol::createI32(SymbolId::create(1), defaultLoc);
  auto qualifiedName = intType->getQualifiedName();
  ZC_EXPECT(qualifiedName == "i32");
}

ZC_TEST("TypeSymbol_BasicProperties") {
  source::SourceLoc defaultLoc;
  auto intType = BuiltInTypeSymbol::createI32(SymbolId::create(1), defaultLoc);
  auto floatType = BuiltInTypeSymbol::createF32(SymbolId::create(2), defaultLoc);

  // Test basic properties
  ZC_EXPECT(intType->getName() == "i32");
  ZC_EXPECT(floatType->getName() == "f32");

  // Test type classification
  ZC_EXPECT(intType->isPrimitive());
  ZC_EXPECT(floatType->isPrimitive());
  ZC_EXPECT(intType->isNumeric());
  ZC_EXPECT(floatType->isNumeric());
}

ZC_TEST("TypeSymbol_MoveOperations") {
  source::SourceLoc defaultLoc;

  // Test TypeParameterSymbol move constructor
  TypeParameterSymbol originalParam(SymbolId::create(1), "T"_zc,
                                    SymbolFlags::TypeKind | SymbolFlags::Generic, defaultLoc);
  originalParam.setVariance(TypeParameterSymbol::Variance::Covariant);

  TypeParameterSymbol movedParam = zc::mv(originalParam);
  ZC_EXPECT(movedParam.getName() == "T");
  ZC_EXPECT(movedParam.getVariance() == TypeParameterSymbol::Variance::Covariant);
  ZC_EXPECT(movedParam.hasFlag(SymbolFlags::Generic));

  // Test FunctionTypeSymbol move constructor
  FunctionTypeSymbol originalFunc(SymbolId::create(2), "TestFunc"_zc,
                                  SymbolFlags::TypeKind | SymbolFlags::Function, defaultLoc);
  originalFunc.setVariadic(true);

  FunctionTypeSymbol movedFunc = zc::mv(originalFunc);
  ZC_EXPECT(movedFunc.getName() == "TestFunc");
  ZC_EXPECT(movedFunc.isVariadic());
  ZC_EXPECT(movedFunc.hasFlag(SymbolFlags::Function));
}

ZC_TEST("TypeSymbol_TypeClassification") {
  source::SourceLoc defaultLoc;
  SymbolTable symbolTable;
  auto globalScope = Scope::createGlobalScope();

  // Create different type symbols
  auto& classSymbol = symbolTable.createClass("TestClass"_zc, *globalScope);
  auto& interfaceSymbol = symbolTable.createInterface("TestInterface"_zc, *globalScope);
  auto builtinType = BuiltInTypeSymbol::createI32(SymbolId::create(1), defaultLoc);

  TypeParameterSymbol typeParam(SymbolId::create(2), "T"_zc,
                                SymbolFlags::TypeKind | SymbolFlags::Generic, defaultLoc);

  FunctionTypeSymbol funcType(SymbolId::create(3), "TestFunc"_zc,
                              SymbolFlags::TypeKind | SymbolFlags::Function, defaultLoc);

  // Test type kind flags
  ZC_EXPECT(classSymbol.hasFlag(SymbolFlags::TypeKind));
  ZC_EXPECT(interfaceSymbol.hasFlag(SymbolFlags::TypeKind));
  ZC_EXPECT(builtinType->hasFlag(SymbolFlags::TypeKind));
  ZC_EXPECT(typeParam.hasFlag(SymbolFlags::TypeKind));
  ZC_EXPECT(funcType.hasFlag(SymbolFlags::TypeKind));

  // Test specific type flags
  ZC_EXPECT(builtinType->hasFlag(SymbolFlags::Builtin));
  ZC_EXPECT(typeParam.hasFlag(SymbolFlags::Generic));
  ZC_EXPECT(funcType.hasFlag(SymbolFlags::Function));
}

ZC_TEST("TypeSymbol_CompilerFlags") {
  source::SourceLoc defaultLoc;
  SymbolTable symbolTable;
  auto globalScope = Scope::createGlobalScope();

  // Test synthetic type
  auto& syntheticClass = symbolTable.createClass("SyntheticClass"_zc, *globalScope);
  syntheticClass.addFlag(SymbolFlags::Synthetic);

  // Test template type
  auto& templateClass = symbolTable.createClass("TemplateClass"_zc, *globalScope);
  templateClass.addFlag(SymbolFlags::Template);

  // Test exported type
  auto& exportedInterface = symbolTable.createInterface("ExportedInterface"_zc, *globalScope);
  exportedInterface.addFlag(SymbolFlags::Export);

  ZC_EXPECT(syntheticClass.hasFlag(SymbolFlags::Synthetic));
  ZC_EXPECT(templateClass.hasFlag(SymbolFlags::Template));
  ZC_EXPECT(exportedInterface.hasFlag(SymbolFlags::Export));
}

ZC_TEST("TypeSymbol_StatusFlags") {
  source::SourceLoc defaultLoc;
  SymbolTable symbolTable;
  auto globalScope = Scope::createGlobalScope();

  // Test deprecated type
  auto& deprecatedClass = symbolTable.createClass("DeprecatedClass"_zc, *globalScope);
  deprecatedClass.addFlag(SymbolFlags::Deprecated);

  // Test experimental type
  auto& experimentalInterface =
      symbolTable.createInterface("ExperimentalInterface"_zc, *globalScope);
  experimentalInterface.addFlag(SymbolFlags::Experimental);

  // Test unsafe type
  auto& unsafeClass = symbolTable.createClass("UnsafeClass"_zc, *globalScope);
  unsafeClass.addFlag(SymbolFlags::Unsafe);

  ZC_EXPECT(deprecatedClass.hasFlag(SymbolFlags::Deprecated));
  ZC_EXPECT(experimentalInterface.hasFlag(SymbolFlags::Experimental));
  ZC_EXPECT(unsafeClass.hasFlag(SymbolFlags::Unsafe));
}

ZC_TEST("TypeSymbol_UnionTypeDetection") {
  source::SourceLoc defaultLoc;
  SymbolTable symbolTable;
  auto globalScope = Scope::createGlobalScope();

  // Create a TypeSymbol without AST (should return false)
  TypeSymbol regularTypeSymbol(SymbolId::create(1), "RegularType"_zc, SymbolFlags::TypeKind,
                               defaultLoc);

  // Create a regular class for comparison
  auto& classSymbol = symbolTable.createClass("RegularClass"_zc, *globalScope);

  ZC_EXPECT(!regularTypeSymbol.isUnionType());
  ZC_EXPECT(!classSymbol.isUnionType());
}

ZC_TEST("TypeSymbol_IntersectionTypeDetection") {
  source::SourceLoc defaultLoc;
  SymbolTable symbolTable;
  auto globalScope = Scope::createGlobalScope();

  // Create a TypeSymbol without AST (should return false)
  TypeSymbol regularTypeSymbol(SymbolId::create(2), "RegularType"_zc, SymbolFlags::TypeKind,
                               defaultLoc);

  // Create a regular interface for comparison
  auto& interfaceSymbol = symbolTable.createInterface("RegularInterface"_zc, *globalScope);

  ZC_EXPECT(!regularTypeSymbol.isIntersectionType());
  ZC_EXPECT(!interfaceSymbol.isIntersectionType());
}

ZC_TEST("TypeSymbol_SubtypeChecking") {
  source::SourceLoc defaultLoc;
  SymbolTable symbolTable;
  auto globalScope = Scope::createGlobalScope();

  // Create base and derived classes
  auto& baseClass = symbolTable.createClass("BaseClass"_zc, *globalScope);
  auto& derivedClass = symbolTable.createClass("DerivedClass"_zc, *globalScope);
  auto& unrelatedClass = symbolTable.createClass("UnrelatedClass"_zc, *globalScope);

  // Set up inheritance relationship
  derivedClass.setSuperclass(baseClass);

  // Test subtype relationships
  ZC_EXPECT(derivedClass.isSubtypeOf(baseClass));     // Derived is subtype of base
  ZC_EXPECT(baseClass.isSubtypeOf(baseClass));        // Same type is subtype of itself
  ZC_EXPECT(!baseClass.isSubtypeOf(derivedClass));    // Base is not subtype of derived
  ZC_EXPECT(!unrelatedClass.isSubtypeOf(baseClass));  // Unrelated class is not subtype

  // Test with built-in types
  auto intType = BuiltInTypeSymbol::createI32(SymbolId::create(1), defaultLoc);
  auto floatType = BuiltInTypeSymbol::createF32(SymbolId::create(2), defaultLoc);

  ZC_EXPECT(intType->isSubtypeOf(*intType));     // Same type
  ZC_EXPECT(!intType->isSubtypeOf(*floatType));  // Different built-in types
}

ZC_TEST("TypeSymbol_AssignabilityChecking") {
  source::SourceLoc defaultLoc;
  SymbolTable symbolTable;
  ScopeManager& scopeManager = symbolTable.getScopeManager();
  const Scope& globalScope = scopeManager.createScope(Scope::Kind::Global, "global");

  // Create base and derived classes
  auto& baseClass = symbolTable.createClass("BaseClass"_zc, globalScope);
  auto& derivedClass = symbolTable.createClass("DerivedClass"_zc, globalScope);
  derivedClass.setSuperclass(zc::Maybe<const ClassSymbol&>(baseClass));

  // Test assignability for class hierarchy
  ZC_EXPECT(baseClass.isAssignableFrom(derivedClass));   // Base can accept derived
  ZC_EXPECT(baseClass.isAssignableFrom(baseClass));      // Same type is assignable
  ZC_EXPECT(!derivedClass.isAssignableFrom(baseClass));  // Derived cannot accept base

  // Test with built-in types
  auto intType = BuiltInTypeSymbol::createI32(SymbolId::create(1), defaultLoc);
  auto floatType = BuiltInTypeSymbol::createF32(SymbolId::create(2), defaultLoc);
  auto stringType = BuiltInTypeSymbol::createStr(SymbolId::create(3), defaultLoc);

  ZC_EXPECT(intType->isAssignableFrom(*intType));      // Same type
  ZC_EXPECT(!intType->isAssignableFrom(*floatType));   // int cannot accept float (narrowing)
  ZC_EXPECT(!stringType->isAssignableFrom(*intType));  // string cannot accept int
}

}  // namespace symbol
}  // namespace compiler
}  // namespace zomlang
