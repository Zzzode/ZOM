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

#include "zomlang/compiler/symbol/value-symbol.h"

#include "zc/core/debug.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/source/location.h"
#include "zomlang/compiler/symbol/scope.h"
#include "zomlang/compiler/symbol/symbol-table.h"
#include "zomlang/compiler/symbol/type-symbol.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang {
namespace compiler {
namespace symbol {

ZC_TEST("ValueSymbol_BasicCreation") {
  SymbolTable symbolTable;
  ScopeManager& scopeManager = symbolTable.getScopeManager();

  ZC_IF_SOME(globalScope, scopeManager.getCurrentScope()) {
    auto& variable = symbolTable.createVariable(tests::testDefinition(0), "testVar", globalScope);

    ZC_EXPECT(variable.getName() == "testVar");
    ZC_EXPECT(variable.getKind() == SymbolKind::Variable);
  }
  else { ZC_FAIL_REQUIRE("Should have global scope"); }
}

ZC_TEST("VariableSymbol_Properties") {
  SymbolTable symbolTable;
  ScopeManager& scopeManager = symbolTable.getScopeManager();

  ZC_IF_SOME(globalScope, scopeManager.getCurrentScope()) {
    auto& variable = symbolTable.createVariable(tests::testDefinition(0), "testVar", globalScope);

    // Use SymbolKind checks before downcasting.
    ZC_EXPECT(variable.getKind() == SymbolKind::Variable);

    // Type-safe casting using SymbolKind check
    if (variable.getKind() == SymbolKind::Variable) {
      VariableSymbol& varSymbol = static_cast<VariableSymbol&>(variable);
      ZC_EXPECT(varSymbol.isMutable());
      ZC_EXPECT(!varSymbol.isConstant());
    }
  }
  else { ZC_FAIL_REQUIRE("Should have global scope"); }
}

ZC_TEST("FunctionSymbol_Creation") {
  SymbolTable symbolTable;
  ScopeManager& scopeManager = symbolTable.getScopeManager();

  ZC_IF_SOME(globalScope, scopeManager.getCurrentScope()) {
    auto& function = symbolTable.createFunction(tests::testDefinition(0), "testFunc", globalScope);

    ZC_EXPECT(function.getName() == "testFunc");
    ZC_EXPECT(function.getKind() == SymbolKind::Function);
  }
  else { ZC_FAIL_REQUIRE("Should have global scope"); }
}

ZC_TEST("ValueSymbol_BasicProperties") {
  SymbolTable symbolTable;
  ScopeManager& scopeManager = symbolTable.getScopeManager();

  ZC_IF_SOME(globalScope, scopeManager.getCurrentScope()) {
    auto& variableSymbol =
        symbolTable.createVariable(tests::testDefinition(0), "testVar", globalScope);

    // Test that we can access basic symbol properties
    ZC_EXPECT(variableSymbol.getName().size() > 0);

    // Test flags
    ZC_EXPECT(variableSymbol.getFlags() != SymbolFlags::None);
  }
  else { ZC_FAIL_REQUIRE("Should have global scope"); }
}

ZC_TEST("ValueSymbol_Casting") {
  SymbolTable symbolTable;
  ScopeManager& scopeManager = symbolTable.getScopeManager();

  ZC_IF_SOME(globalScope, scopeManager.getCurrentScope()) {
    auto& variableSymbol =
        symbolTable.createVariable(tests::testDefinition(0), "testVar", globalScope);
    auto& functionSymbol =
        symbolTable.createFunction(tests::testDefinition(0), "testFunc", globalScope);

    // Test symbol kind checking instead of dynamic casting
    ZC_EXPECT(variableSymbol.getKind() == SymbolKind::Variable);
    ZC_EXPECT(functionSymbol.getKind() == SymbolKind::Function);

    // Verify type safety using SymbolKind
    ZC_EXPECT(variableSymbol.getKind() != SymbolKind::Function);
    ZC_EXPECT(functionSymbol.getKind() != SymbolKind::Variable);
  }
  else { ZC_FAIL_REQUIRE("Should have global scope"); }
}

ZC_TEST("ValueSymbol_SymbolTable_Integration") {
  SymbolTable symbolTable;
  ScopeManager& scopeManager = symbolTable.getScopeManager();

  ZC_IF_SOME(globalScope, scopeManager.getCurrentScope()) {
    // Create multiple symbols
    symbolTable.createVariable(tests::testDefinition(0), "var1", globalScope);
    symbolTable.createVariable(tests::testDefinition(0), "var2", globalScope);
    symbolTable.createFunction(tests::testDefinition(0), "func1", globalScope);

    // Test lookup
    auto foundVar1 = symbolTable.lookup("var1", globalScope);
    auto foundVar2 = symbolTable.lookup("var2", globalScope);
    auto foundFunc1 = symbolTable.lookup("func1", globalScope);

    ZC_EXPECT(foundVar1 != zc::none);
    ZC_EXPECT(foundVar2 != zc::none);
    ZC_EXPECT(foundFunc1 != zc::none);

    ZC_IF_SOME(symbol, foundVar1) {
      ZC_EXPECT(symbol.getName() == "var1");
      ZC_EXPECT(symbol.getKind() == SymbolKind::Variable);
    }

    ZC_IF_SOME(symbol, foundFunc1) {
      ZC_EXPECT(symbol.getName() == "func1");
      ZC_EXPECT(symbol.getKind() == SymbolKind::Function);
    }
  }
  else { ZC_FAIL_REQUIRE("Should have global scope"); }
}

ZC_TEST("ValueSymbol_SymbolKind_Lookup") {
  SymbolTable symbolTable;
  ScopeManager& scopeManager = symbolTable.getScopeManager();

  ZC_IF_SOME(globalScope, scopeManager.getCurrentScope()) {
    // Create multiple symbols
    symbolTable.createVariable(tests::testDefinition(0), "var1", globalScope);
    symbolTable.createVariable(tests::testDefinition(0), "var2", globalScope);
    symbolTable.createFunction(tests::testDefinition(0), "func1", globalScope);
    symbolTable.createFunction(tests::testDefinition(0), "func2", globalScope);

    // Test lookup by kind - using getSymbolsOfType with SymbolKind
    auto variables = symbolTable.getSymbolsOfType(SymbolKind::Variable, globalScope);
    auto functions = symbolTable.getSymbolsOfType(SymbolKind::Function, globalScope);

    ZC_EXPECT(variables.size() >= 2);  // At least our two variables
    ZC_EXPECT(functions.size() >= 2);  // At least our two functions
  }
  else { ZC_FAIL_REQUIRE("Should have global scope"); }
}

ZC_TEST("ValueSymbol_NameAvailability") {
  SymbolTable symbolTable;
  ScopeManager& scopeManager = symbolTable.getScopeManager();

  ZC_IF_SOME(globalScope, scopeManager.getCurrentScope()) {
    // Test that names are available before creation
    auto foundNewVar = symbolTable.lookup("newVar", globalScope);
    auto foundNewFunc = symbolTable.lookup("newFunc", globalScope);
    ZC_EXPECT(foundNewVar == zc::none);
    ZC_EXPECT(foundNewFunc == zc::none);

    // Create symbols
    symbolTable.createVariable(tests::testDefinition(0), "newVar", globalScope);
    symbolTable.createFunction(tests::testDefinition(0), "newFunc", globalScope);

    // Test that names are no longer available after creation
    auto foundNewVar2 = symbolTable.lookup("newVar", globalScope);
    auto foundNewFunc2 = symbolTable.lookup("newFunc", globalScope);
    ZC_EXPECT(foundNewVar2 != zc::none);
    ZC_EXPECT(foundNewFunc2 != zc::none);

    // Test that other names are still available
    auto foundAnotherVar = symbolTable.lookup("anotherVar", globalScope);
    auto foundAnotherFunc = symbolTable.lookup("anotherFunc", globalScope);
    ZC_EXPECT(foundAnotherVar == zc::none);
    ZC_EXPECT(foundAnotherFunc == zc::none);
  }
  else { ZC_FAIL_REQUIRE("Should have global scope"); }
}

ZC_TEST("ValueSymbol_UniqueNameGeneration") {
  SymbolTable symbolTable;
  ScopeManager& scopeManager = symbolTable.getScopeManager();

  ZC_IF_SOME(globalScope, scopeManager.getCurrentScope()) {
    // Create symbols with similar base names
    auto& symbol1 = symbolTable.createVariable(tests::testDefinition(0), "test", globalScope);
    auto& symbol2 = symbolTable.createVariable(tests::testDefinition(0), "test_1", globalScope);
    auto& symbol3 = symbolTable.createVariable(tests::testDefinition(0), "var", globalScope);

    // Test that names are different
    ZC_EXPECT(symbol1.getName() != symbol2.getName());
    ZC_EXPECT(symbol1.getName() != symbol3.getName());
    ZC_EXPECT(symbol2.getName() != symbol3.getName());

    // Test that names start with expected prefixes
    ZC_EXPECT(symbol1.getName().startsWith("test"));
    ZC_EXPECT(symbol2.getName().startsWith("test"));
    ZC_EXPECT(symbol3.getName().startsWith("var"));
  }
  else { ZC_FAIL_REQUIRE("Should have global scope"); }
}

// Test ValueSymbol constructor (L34-36)
ZC_TEST("ValueSymbol_Constructor") {
  auto type = BuiltInTypeSymbol::createI32(tests::testDefinition(1), source::SourceLoc{});
  ValueSymbol valueSymbol(tests::testDefinition(2), "testValue"_zc, SymbolFlags::TermKind,
                          source::SourceLoc{}, zc::mv(type));

  ZC_EXPECT(valueSymbol.getName() == "testValue");
  ZC_EXPECT(valueSymbol.getKind() == SymbolKind::Value);
  ZC_EXPECT(valueSymbol.isValueSymbol());
  ZC_EXPECT(valueSymbol.getType().getName() == "i32");
}

// Test ConstantSymbol constructor (L89-93)
ZC_TEST("ConstantSymbol_Constructor") {
  auto type = BuiltInTypeSymbol::createStr(tests::testDefinition(3), source::SourceLoc{});
  ConstantSymbol constantSymbol(tests::testDefinition(4), "testConstant"_zc, SymbolFlags::TermKind,
                                source::SourceLoc{}, zc::mv(type));

  ZC_EXPECT(constantSymbol.getName() == "testConstant");
  ZC_EXPECT(constantSymbol.getKind() == SymbolKind::Constant);
  ZC_EXPECT(constantSymbol.isConstant());
  ZC_EXPECT(constantSymbol.getType().getName() == "str");
  ZC_EXPECT(constantSymbol.hasFlag(SymbolFlags::Constant));
}

// Test ParameterSymbol constructor (L118-123)
ZC_TEST("ParameterSymbol_Constructor") {
  auto type = BuiltInTypeSymbol::createBool(tests::testDefinition(5), source::SourceLoc{});
  ParameterSymbol parameterSymbol(tests::testDefinition(6), "testParam"_zc, SymbolFlags::TermKind,
                                  source::SourceLoc{}, zc::mv(type), true);

  ZC_EXPECT(parameterSymbol.getName() == "testParam");
  ZC_EXPECT(parameterSymbol.getKind() == SymbolKind::Parameter);
  ZC_EXPECT(parameterSymbol.isOptional());
  ZC_EXPECT(parameterSymbol.getType().getName() == "bool");
  ZC_EXPECT(parameterSymbol.hasFlag(SymbolFlags::Parameter));
}

// Test FunctionSymbol constructor (L149-153)
ZC_TEST("FunctionSymbol_Constructor") {
  auto type = BuiltInTypeSymbol::createUnit(tests::testDefinition(7), source::SourceLoc{});
  FunctionSymbol functionSymbol(tests::testDefinition(8), "testFunction"_zc, SymbolFlags::TermKind,
                                source::SourceLoc{}, zc::mv(type));

  ZC_EXPECT(functionSymbol.getName() == "testFunction");
  ZC_EXPECT(functionSymbol.getKind() == SymbolKind::Function);
  ZC_EXPECT(functionSymbol.getParameterCount() == 0);
  ZC_EXPECT(functionSymbol.getType().getName() == "unit");
  ZC_EXPECT(functionSymbol.hasFlag(SymbolFlags::Function));
}

// Test FieldSymbol constructor (L195-200)
ZC_TEST("FieldSymbol_Constructor") {
  auto type = BuiltInTypeSymbol::createF32(tests::testDefinition(9), source::SourceLoc{});
  FieldSymbol fieldSymbol(tests::testDefinition(10), "testField"_zc, SymbolFlags::TermKind,
                          source::SourceLoc{}, zc::mv(type), false);

  ZC_EXPECT(fieldSymbol.getName() == "testField");
  ZC_EXPECT(fieldSymbol.getKind() == SymbolKind::Field);
  ZC_EXPECT(!fieldSymbol.isMutable());  // Created as immutable
  ZC_EXPECT(fieldSymbol.getType().getName() == "f32");
}

// Test EnumCaseSymbol constructor (L228-232)
ZC_TEST("EnumCaseSymbol_Constructor") {
  auto type = BuiltInTypeSymbol::createI32(tests::testDefinition(11), source::SourceLoc{});
  EnumCaseSymbol enumCaseSymbol(tests::testDefinition(12), "testCase"_zc, SymbolFlags::TermKind,
                                source::SourceLoc{}, zc::mv(type));

  ZC_EXPECT(enumCaseSymbol.getName() == "testCase");
  ZC_EXPECT(enumCaseSymbol.getKind() == SymbolKind::EnumCase);
  ZC_EXPECT(enumCaseSymbol.isConstant());
  ZC_EXPECT(enumCaseSymbol.getType().getName() == "i32");
  ZC_EXPECT(enumCaseSymbol.hasFlag(SymbolFlags::Constant));
  ZC_EXPECT(enumCaseSymbol.getAssociatedValue() == zc::none);
}

// Additional ValueSymbol tests
ZC_TEST("ValueSymbol_SetType") {
  auto initialType = BuiltInTypeSymbol::createI32(tests::testDefinition(13), source::SourceLoc{});
  ValueSymbol valueSymbol(tests::testDefinition(14), "testValue"_zc, SymbolFlags::TermKind,
                          source::SourceLoc{}, zc::mv(initialType));

  ZC_EXPECT(valueSymbol.getType().getName() == "i32");

  auto newType = BuiltInTypeSymbol::createStr(tests::testDefinition(15), source::SourceLoc{});
  valueSymbol.setType(zc::mv(newType));
  ZC_EXPECT(valueSymbol.getType().getName() == "str");
}

ZC_TEST("ValueSymbol_MutableType") {
  auto type = BuiltInTypeSymbol::createF32(tests::testDefinition(16), source::SourceLoc{});
  ValueSymbol valueSymbol(tests::testDefinition(17), "testValue"_zc, SymbolFlags::TermKind,
                          source::SourceLoc{}, zc::mv(type));

  // Test non-const getType()
  TypeSymbol& mutableType = valueSymbol.getType();
  ZC_EXPECT(mutableType.getName() == "f32");
}

ZC_TEST("ValueSymbol_MutabilityFlags") {
  auto type1 = BuiltInTypeSymbol::createI32(tests::testDefinition(18), source::SourceLoc{});
  ValueSymbol mutableSymbol(tests::testDefinition(19), "mutable"_zc,
                            SymbolFlags::TermKind | SymbolFlags::Mutable, source::SourceLoc{},
                            zc::mv(type1));

  auto type2 = BuiltInTypeSymbol::createI32(tests::testDefinition(20), source::SourceLoc{});
  ValueSymbol immutableSymbol(tests::testDefinition(21), "immutable"_zc, SymbolFlags::TermKind,
                              source::SourceLoc{}, zc::mv(type2));

  ZC_EXPECT(mutableSymbol.isMutable());
  ZC_EXPECT(!immutableSymbol.isMutable());
}

ZC_TEST("ValueSymbol_MoveOperations") {
  auto type1 = BuiltInTypeSymbol::createI32(tests::testDefinition(102), source::SourceLoc{});
  const auto originalDefinition = tests::testDefinition(103);
  ValueSymbol originalSymbol(originalDefinition, "original"_zc, SymbolFlags::Mutable,
                             source::SourceLoc{}, zc::mv(type1));

  // Test move constructor
  ValueSymbol movedSymbol = zc::mv(originalSymbol);
  ZC_EXPECT(movedSymbol.getName() == "original"_zc);
  ZC_EXPECT(movedSymbol.getId() == originalDefinition);
  ZC_EXPECT(movedSymbol.isMutable());

  // Test move assignment
  auto type2 = BuiltInTypeSymbol::createI32(tests::testDefinition(104), source::SourceLoc{});
  const auto sourceDefinition = tests::testDefinition(105);
  ValueSymbol sourceSymbol(sourceDefinition, "source"_zc, SymbolFlags::TermKind,
                           source::SourceLoc{}, zc::mv(type2));

  auto type3 = BuiltInTypeSymbol::createI32(tests::testDefinition(106), source::SourceLoc{});
  ValueSymbol targetSymbol(tests::testDefinition(107), "target"_zc, SymbolFlags::TermKind,
                           source::SourceLoc{}, zc::mv(type3));

  targetSymbol = zc::mv(sourceSymbol);
  ZC_EXPECT(targetSymbol.getName() == "source"_zc);
  ZC_EXPECT(targetSymbol.getId() == sourceDefinition);
}

// Additional VariableSymbol tests
ZC_TEST("VariableSymbol_Properties") {
  auto type = BuiltInTypeSymbol::createI32(tests::testDefinition(22), source::SourceLoc{});
  VariableSymbol variableSymbol(tests::testDefinition(23), "testVar"_zc, SymbolFlags::TermKind,
                                source::SourceLoc{}, zc::mv(type), true);

  ZC_EXPECT(!variableSymbol.isParameter());
  ZC_EXPECT(!variableSymbol.isCaptured());
  ZC_EXPECT(variableSymbol.isLocal());
}

ZC_TEST("VariableSymbol_ParameterFlags") {
  auto type = BuiltInTypeSymbol::createI32(tests::testDefinition(24), source::SourceLoc{});
  VariableSymbol parameterSymbol(tests::testDefinition(25), "param"_zc,
                                 SymbolFlags::TermKind | SymbolFlags::Parameter,
                                 source::SourceLoc{}, zc::mv(type), false);

  ZC_EXPECT(parameterSymbol.isParameter());
  ZC_EXPECT(!parameterSymbol.isLocal());
}

ZC_TEST("VariableSymbol_CapturedFlags") {
  auto type = BuiltInTypeSymbol::createI32(tests::testDefinition(26), source::SourceLoc{});
  VariableSymbol capturedSymbol(tests::testDefinition(27), "captured"_zc,
                                SymbolFlags::TermKind | SymbolFlags::Local, source::SourceLoc{},
                                zc::mv(type), true);

  ZC_EXPECT(capturedSymbol.isCaptured());
}

ZC_TEST("VariableSymbol_Classof") {
  auto type = BuiltInTypeSymbol::createI32(tests::testDefinition(28), source::SourceLoc{});
  VariableSymbol variableSymbol(tests::testDefinition(29), "testVar"_zc, SymbolFlags::TermKind,
                                source::SourceLoc{}, zc::mv(type), true);

  ZC_EXPECT(VariableSymbol::classof(variableSymbol));

  auto funcType = BuiltInTypeSymbol::createUnit(tests::testDefinition(30), source::SourceLoc{});
  FunctionSymbol functionSymbol(tests::testDefinition(31), "testFunc"_zc, SymbolFlags::TermKind,
                                source::SourceLoc{}, zc::mv(funcType));
  ZC_EXPECT(!VariableSymbol::classof(functionSymbol));
}

ZC_TEST("VariableSymbol_MoveOperations") {
  auto type1 = BuiltInTypeSymbol::createI32(tests::testDefinition(108), source::SourceLoc{});
  const auto originalDefinition = tests::testDefinition(109);
  VariableSymbol originalSymbol(originalDefinition, "originalVar"_zc, SymbolFlags::Parameter,
                                source::SourceLoc{}, zc::mv(type1), true);

  // Test move constructor
  VariableSymbol movedSymbol = zc::mv(originalSymbol);
  ZC_EXPECT(movedSymbol.getName() == "originalVar"_zc);
  ZC_EXPECT(movedSymbol.getId() == originalDefinition);
  ZC_EXPECT(movedSymbol.isParameter());
  ZC_EXPECT(movedSymbol.isMutable());

  // Test move assignment
  auto type2 = BuiltInTypeSymbol::createI32(tests::testDefinition(110), source::SourceLoc{});
  const auto sourceDefinition = tests::testDefinition(111);
  VariableSymbol sourceSymbol(sourceDefinition, "sourceVar"_zc, SymbolFlags::Local,
                              source::SourceLoc{}, zc::mv(type2), false);

  auto type3 = BuiltInTypeSymbol::createI32(tests::testDefinition(112), source::SourceLoc{});
  VariableSymbol targetSymbol(tests::testDefinition(113), "targetVar"_zc, SymbolFlags::TermKind,
                              source::SourceLoc{}, zc::mv(type3), true);

  targetSymbol = zc::mv(sourceSymbol);
  ZC_EXPECT(targetSymbol.getName() == "sourceVar"_zc);
  ZC_EXPECT(targetSymbol.getId() == sourceDefinition);
  ZC_EXPECT(targetSymbol.isLocal());
  ZC_EXPECT(!targetSymbol.isMutable());
}

// Additional ConstantSymbol tests
ZC_TEST("ConstantSymbol_ValueText") {
  auto type = BuiltInTypeSymbol::createStr(tests::testDefinition(32), source::SourceLoc{});
  ConstantSymbol constantSymbol(tests::testDefinition(33), "testConstant"_zc, SymbolFlags::TermKind,
                                source::SourceLoc{}, zc::mv(type));

  // Test initial empty value text
  ZC_EXPECT(constantSymbol.getValueText() == "");

  // Test setting value text
  constantSymbol.setValueText("Hello World"_zc);
  ZC_EXPECT(constantSymbol.getValueText() == "Hello World");

  // Test changing value text
  constantSymbol.setValueText("New Value"_zc);
  ZC_EXPECT(constantSymbol.getValueText() == "New Value");
}

ZC_TEST("ConstantSymbol_Classof") {
  auto type = BuiltInTypeSymbol::createI32(tests::testDefinition(31), source::SourceLoc{});
  ConstantSymbol constantSymbol(tests::testDefinition(32), "testConst"_zc, SymbolFlags::TermKind,
                                source::SourceLoc{}, zc::mv(type));

  ZC_EXPECT(ConstantSymbol::classof(constantSymbol));

  auto varType = BuiltInTypeSymbol::createI32(tests::testDefinition(33), source::SourceLoc{});
  VariableSymbol variableSymbol(tests::testDefinition(34), "testVar"_zc, SymbolFlags::TermKind,
                                source::SourceLoc{}, zc::mv(varType), true);
  ZC_EXPECT(!ConstantSymbol::classof(variableSymbol));
}

ZC_TEST("ConstantSymbol_MoveOperations") {
  auto type1 = BuiltInTypeSymbol::createI32(tests::testDefinition(114), source::SourceLoc{});
  const auto originalDefinition = tests::testDefinition(115);
  ConstantSymbol originalSymbol(originalDefinition, "originalConst"_zc, SymbolFlags::TermKind,
                                source::SourceLoc{}, zc::mv(type1));
  originalSymbol.setValueText("42"_zc);

  // Test move constructor
  ConstantSymbol movedSymbol = zc::mv(originalSymbol);
  ZC_EXPECT(movedSymbol.getName() == "originalConst"_zc);
  ZC_EXPECT(movedSymbol.getId() == originalDefinition);
  ZC_EXPECT(movedSymbol.getValueText() == "42"_zc);

  // Test move assignment
  auto type2 = BuiltInTypeSymbol::createI32(tests::testDefinition(116), source::SourceLoc{});
  const auto sourceDefinition = tests::testDefinition(117);
  ConstantSymbol sourceSymbol(sourceDefinition, "sourceConst"_zc, SymbolFlags::TermKind,
                              source::SourceLoc{}, zc::mv(type2));
  sourceSymbol.setValueText("100"_zc);

  auto type3 = BuiltInTypeSymbol::createI32(tests::testDefinition(118), source::SourceLoc{});
  ConstantSymbol targetSymbol(tests::testDefinition(119), "targetConst"_zc, SymbolFlags::TermKind,
                              source::SourceLoc{}, zc::mv(type3));

  targetSymbol = zc::mv(sourceSymbol);
  ZC_EXPECT(targetSymbol.getName() == "sourceConst"_zc);
  ZC_EXPECT(targetSymbol.getId() == sourceDefinition);
  ZC_EXPECT(targetSymbol.getValueText() == "100"_zc);
}

// Additional ParameterSymbol tests
ZC_TEST("ParameterSymbol_IndexManagement") {
  auto type = BuiltInTypeSymbol::createI32(tests::testDefinition(38), source::SourceLoc{});
  ParameterSymbol parameterSymbol(tests::testDefinition(39), "param"_zc, SymbolFlags::TermKind,
                                  source::SourceLoc{}, zc::mv(type), false);

  // Test initial index (should be 0)
  ZC_EXPECT(parameterSymbol.getIndex() == 0);

  // Test setting index
  parameterSymbol.setIndex(5);
  ZC_EXPECT(parameterSymbol.getIndex() == 5);

  // Test changing index
  parameterSymbol.setIndex(10);
  ZC_EXPECT(parameterSymbol.getIndex() == 10);
}

ZC_TEST("ParameterSymbol_OptionalParameter") {
  auto type1 = BuiltInTypeSymbol::createI32(tests::testDefinition(40), source::SourceLoc{});
  ParameterSymbol requiredParam(tests::testDefinition(41), "required"_zc, SymbolFlags::TermKind,
                                source::SourceLoc{}, zc::mv(type1), false);

  auto type2 = BuiltInTypeSymbol::createI32(tests::testDefinition(42), source::SourceLoc{});
  ParameterSymbol optionalParam(tests::testDefinition(43), "optional"_zc, SymbolFlags::TermKind,
                                source::SourceLoc{}, zc::mv(type2), true);

  ZC_EXPECT(!requiredParam.isOptional());
  ZC_EXPECT(optionalParam.isOptional());
}

ZC_TEST("ParameterSymbol_Classof") {
  auto type = BuiltInTypeSymbol::createI32(tests::testDefinition(43), source::SourceLoc{});
  ParameterSymbol parameterSymbol(tests::testDefinition(43), "testParam"_zc, SymbolFlags::TermKind,
                                  source::SourceLoc{}, zc::mv(type), false);

  ZC_EXPECT(ParameterSymbol::classof(parameterSymbol));

  auto varType = BuiltInTypeSymbol::createI32(tests::testDefinition(45), source::SourceLoc{});
  VariableSymbol variableSymbol(tests::testDefinition(46), "testVar"_zc, SymbolFlags::TermKind,
                                source::SourceLoc{}, zc::mv(varType), true);
  ZC_EXPECT(!ParameterSymbol::classof(variableSymbol));
}

ZC_TEST("ParameterSymbol_MoveOperations") {
  auto type1 = BuiltInTypeSymbol::createI32(tests::testDefinition(120), source::SourceLoc{});
  const auto originalDefinition = tests::testDefinition(121);
  ParameterSymbol originalSymbol(originalDefinition, "originalParam"_zc, SymbolFlags::TermKind,
                                 source::SourceLoc{}, zc::mv(type1), true);

  // Test move constructor
  ParameterSymbol movedSymbol = zc::mv(originalSymbol);
  ZC_EXPECT(movedSymbol.getName() == "originalParam"_zc);
  ZC_EXPECT(movedSymbol.getId() == originalDefinition);
  ZC_EXPECT(movedSymbol.isOptional());  // Should be true since originalSymbol was created with true

  // Test move assignment
  auto type2 = BuiltInTypeSymbol::createI32(tests::testDefinition(122), source::SourceLoc{});
  const auto sourceDefinition = tests::testDefinition(123);
  ParameterSymbol sourceSymbol(sourceDefinition, "sourceParam"_zc, SymbolFlags::TermKind,
                               source::SourceLoc{}, zc::mv(type2), false);

  auto type3 = BuiltInTypeSymbol::createI32(tests::testDefinition(124), source::SourceLoc{});
  ParameterSymbol targetSymbol(tests::testDefinition(125), "targetParam"_zc, SymbolFlags::TermKind,
                               source::SourceLoc{}, zc::mv(type3), true);

  targetSymbol = zc::mv(sourceSymbol);
  ZC_EXPECT(targetSymbol.getName() == "sourceParam"_zc);
  ZC_EXPECT(targetSymbol.getId() == sourceDefinition);
  ZC_EXPECT(!targetSymbol.isOptional());
}

// Additional FunctionSymbol tests
ZC_TEST("FunctionSymbol_ParameterManagement") {
  auto funcType = BuiltInTypeSymbol::createUnit(tests::testDefinition(48), source::SourceLoc{});
  FunctionSymbol functionSymbol(tests::testDefinition(49), "testFunc"_zc, SymbolFlags::TermKind,
                                source::SourceLoc{}, zc::mv(funcType));

  ZC_EXPECT(functionSymbol.getParameterCount() == 0);
  ZC_EXPECT(functionSymbol.getParameters().size() == 0);

  // Add first parameter
  auto param1Type = BuiltInTypeSymbol::createI32(tests::testDefinition(50), source::SourceLoc{});
  auto param1 =
      zc::heap<ParameterSymbol>(tests::testDefinition(51), "param1"_zc, SymbolFlags::TermKind,
                                source::SourceLoc{}, zc::mv(param1Type), false);
  functionSymbol.addParameter(zc::mv(param1));

  ZC_EXPECT(functionSymbol.getParameterCount() == 1);
  ZC_EXPECT(functionSymbol.getParameters().size() == 1);
  ZC_EXPECT(functionSymbol.getParameters()[0].getName() == "param1");
  ZC_EXPECT(functionSymbol.getParameters()[0].getIndex() == 0);

  // Add second parameter
  auto param2Type = BuiltInTypeSymbol::createStr(tests::testDefinition(52), source::SourceLoc{});
  auto param2 =
      zc::heap<ParameterSymbol>(tests::testDefinition(53), "param2"_zc, SymbolFlags::TermKind,
                                source::SourceLoc{}, zc::mv(param2Type), true);
  functionSymbol.addParameter(zc::mv(param2));

  ZC_EXPECT(functionSymbol.getParameterCount() == 2);
  ZC_EXPECT(functionSymbol.getParameters().size() == 2);
  ZC_EXPECT(functionSymbol.getParameters()[1].getName() == "param2");
  ZC_EXPECT(functionSymbol.getParameters()[1].getIndex() == 1);
}

ZC_TEST("FunctionSymbol_FunctionProperties") {
  auto methodType = BuiltInTypeSymbol::createUnit(tests::testDefinition(54), source::SourceLoc{});
  FunctionSymbol methodSymbol(tests::testDefinition(55), "method"_zc,
                              SymbolFlags::TermKind | SymbolFlags::Method, source::SourceLoc{},
                              zc::mv(methodType));

  auto constructorType =
      BuiltInTypeSymbol::createUnit(tests::testDefinition(56), source::SourceLoc{});
  FunctionSymbol constructorSymbol(tests::testDefinition(57), "constructor"_zc,
                                   SymbolFlags::TermKind | SymbolFlags::Constructor,
                                   source::SourceLoc{}, zc::mv(constructorType));

  auto syntheticType =
      BuiltInTypeSymbol::createUnit(tests::testDefinition(58), source::SourceLoc{});
  FunctionSymbol builtinSymbol(tests::testDefinition(59), "builtin"_zc,
                               SymbolFlags::TermKind | SymbolFlags::Builtin, source::SourceLoc{},
                               zc::mv(syntheticType));

  auto variadicType = BuiltInTypeSymbol::createUnit(tests::testDefinition(60), source::SourceLoc{});
  FunctionSymbol variadicSymbol(tests::testDefinition(61), "variadic"_zc,
                                SymbolFlags::TermKind | SymbolFlags::Implicit, source::SourceLoc{},
                                zc::mv(variadicType));

  ZC_EXPECT(methodSymbol.isMethod());
  ZC_EXPECT(!methodSymbol.isConstructor());
  ZC_EXPECT(!methodSymbol.isBuiltin());
  ZC_EXPECT(!methodSymbol.isVariadic());
  ZC_EXPECT(!methodSymbol.isDestructor());
  ZC_EXPECT(!methodSymbol.isOperator());  // Functions are not operators by default

  ZC_EXPECT(constructorSymbol.isConstructor());
  ZC_EXPECT(!constructorSymbol.isMethod());
  ZC_EXPECT(!constructorSymbol.isDestructor());  // Constructor should not be destructor
  ZC_EXPECT(!constructorSymbol.isOperator());    // Functions are not operators by default

  ZC_EXPECT(builtinSymbol.isBuiltin());
  ZC_EXPECT(!builtinSymbol.isDestructor());
  ZC_EXPECT(!builtinSymbol.isOperator());  // Functions are not operators by default

  ZC_EXPECT(variadicSymbol.isVariadic());
  ZC_EXPECT(!variadicSymbol.isDestructor());
  ZC_EXPECT(!variadicSymbol.isOperator());  // Functions are not operators by default
}

ZC_TEST("FunctionSymbol_Classof") {
  auto type = BuiltInTypeSymbol::createI32(tests::testDefinition(76), source::SourceLoc{});
  FunctionSymbol functionSymbol(tests::testDefinition(77), "testFunction"_zc, SymbolFlags::TermKind,
                                source::SourceLoc{}, zc::mv(type));

  ZC_EXPECT(FunctionSymbol::classof(functionSymbol));

  auto varType = BuiltInTypeSymbol::createI32(tests::testDefinition(78), source::SourceLoc{});
  VariableSymbol variableSymbol(tests::testDefinition(79), "testVar"_zc, SymbolFlags::TermKind,
                                source::SourceLoc{}, zc::mv(varType), true);
  ZC_EXPECT(!FunctionSymbol::classof(variableSymbol));
}

ZC_TEST("FunctionSymbol_MoveOperations") {
  auto type1 = BuiltInTypeSymbol::createI32(tests::testDefinition(126), source::SourceLoc{});
  const auto originalDefinition = tests::testDefinition(127);
  FunctionSymbol originalSymbol(originalDefinition, "originalFunc"_zc, SymbolFlags::Method,
                                source::SourceLoc{}, zc::mv(type1));

  // Add a parameter to test parameter preservation
  auto paramType = BuiltInTypeSymbol::createI32(tests::testDefinition(128), source::SourceLoc{});
  auto param =
      zc::heap<ParameterSymbol>(tests::testDefinition(129), "param1"_zc, SymbolFlags::TermKind,
                                source::SourceLoc{}, zc::mv(paramType), false);
  originalSymbol.addParameter(zc::mv(param));

  // Test move constructor
  FunctionSymbol movedSymbol = zc::mv(originalSymbol);
  ZC_EXPECT(movedSymbol.getName() == "originalFunc"_zc);
  ZC_EXPECT(movedSymbol.getId() == originalDefinition);
  ZC_EXPECT(movedSymbol.isMethod());
  ZC_EXPECT(movedSymbol.getParameters().size() == 1);

  // Test move assignment
  auto type2 = BuiltInTypeSymbol::createI32(tests::testDefinition(130), source::SourceLoc{});
  const auto sourceDefinition = tests::testDefinition(131);
  FunctionSymbol sourceSymbol(sourceDefinition, "sourceFunc"_zc, SymbolFlags::Constructor,
                              source::SourceLoc{}, zc::mv(type2));

  auto type3 = BuiltInTypeSymbol::createI32(tests::testDefinition(132), source::SourceLoc{});
  FunctionSymbol targetSymbol(tests::testDefinition(133), "targetFunc"_zc, SymbolFlags::TermKind,
                              source::SourceLoc{}, zc::mv(type3));

  targetSymbol = zc::mv(sourceSymbol);
  ZC_EXPECT(targetSymbol.getName() == "sourceFunc"_zc);
  ZC_EXPECT(targetSymbol.getId() == sourceDefinition);
  ZC_EXPECT(targetSymbol.isConstructor());
}

// Additional FieldSymbol tests
ZC_TEST("FieldSymbol_VisibilityProperties") {
  auto publicType = BuiltInTypeSymbol::createI32(tests::testDefinition(66), source::SourceLoc{});
  FieldSymbol publicField(tests::testDefinition(67), "publicField"_zc,
                          SymbolFlags::TermKind | SymbolFlags::Public, source::SourceLoc{},
                          zc::mv(publicType), true);

  auto privateType = BuiltInTypeSymbol::createI32(tests::testDefinition(68), source::SourceLoc{});
  FieldSymbol privateField(tests::testDefinition(69), "privateField"_zc,
                           SymbolFlags::TermKind | SymbolFlags::Private, source::SourceLoc{},
                           zc::mv(privateType), false);

  auto protectedType = BuiltInTypeSymbol::createI32(tests::testDefinition(70), source::SourceLoc{});
  FieldSymbol protectedField(tests::testDefinition(71), "protectedField"_zc,
                             SymbolFlags::TermKind | SymbolFlags::Protected, source::SourceLoc{},
                             zc::mv(protectedType), true);

  ZC_EXPECT(publicField.isPublic());
  ZC_EXPECT(!publicField.isPrivate());
  ZC_EXPECT(!publicField.isProtected());

  ZC_EXPECT(!privateField.isPublic());
  ZC_EXPECT(privateField.isPrivate());
  ZC_EXPECT(!privateField.isProtected());

  ZC_EXPECT(!protectedField.isPublic());
  ZC_EXPECT(!protectedField.isPrivate());
  ZC_EXPECT(protectedField.isProtected());
}

ZC_TEST("FieldSymbol_StaticProperty") {
  auto staticType = BuiltInTypeSymbol::createI32(tests::testDefinition(72), source::SourceLoc{});
  FieldSymbol staticField(tests::testDefinition(73), "staticField"_zc,
                          SymbolFlags::TermKind | SymbolFlags::Static, source::SourceLoc{},
                          zc::mv(staticType), false);

  auto instanceType = BuiltInTypeSymbol::createI32(tests::testDefinition(74), source::SourceLoc{});
  FieldSymbol instanceField(tests::testDefinition(75), "instanceField"_zc, SymbolFlags::TermKind,
                            source::SourceLoc{}, zc::mv(instanceType), true);

  ZC_EXPECT(staticField.isStatic());
  ZC_EXPECT(!instanceField.isStatic());
}

ZC_TEST("FieldSymbol_MutabilityOverride") {
  // Test that FieldSymbol uses Symbol::isMutable() instead of ValueSymbol::isMutable()
  auto type = BuiltInTypeSymbol::createI32(tests::testDefinition(76), source::SourceLoc{});
  FieldSymbol fieldSymbol(tests::testDefinition(77), "testField"_zc, SymbolFlags::TermKind,
                          source::SourceLoc{}, zc::mv(type), false);

  // Should return false because isMutable=false and no Final flag
  ZC_EXPECT(!fieldSymbol.isMutable());
}

ZC_TEST("FieldSymbol_Classof") {
  auto type = BuiltInTypeSymbol::createI32(tests::testDefinition(78), source::SourceLoc{});
  FieldSymbol fieldSymbol(tests::testDefinition(79), "testField"_zc, SymbolFlags::TermKind,
                          source::SourceLoc{}, zc::mv(type), true);

  ZC_EXPECT(FieldSymbol::classof(fieldSymbol));

  auto varType = BuiltInTypeSymbol::createI32(tests::testDefinition(80), source::SourceLoc{});
  VariableSymbol variableSymbol(tests::testDefinition(81), "testVar"_zc, SymbolFlags::TermKind,
                                source::SourceLoc{}, zc::mv(varType), true);
  ZC_EXPECT(!FieldSymbol::classof(variableSymbol));
}

ZC_TEST("FieldSymbol_MoveOperations") {
  auto type1 = BuiltInTypeSymbol::createI32(tests::testDefinition(134), source::SourceLoc{});
  const auto originalDefinition = tests::testDefinition(135);
  FieldSymbol originalSymbol(originalDefinition, "originalField"_zc,
                             SymbolFlags::Public | SymbolFlags::Static, source::SourceLoc{},
                             zc::mv(type1), true);

  // Test move constructor
  FieldSymbol movedSymbol = zc::mv(originalSymbol);
  ZC_EXPECT(movedSymbol.getName() == "originalField"_zc);
  ZC_EXPECT(movedSymbol.getId() == originalDefinition);
  ZC_EXPECT(movedSymbol.isPublic());
  ZC_EXPECT(movedSymbol.isStatic());
  ZC_EXPECT(movedSymbol.isMutable());

  // Test move assignment
  auto type2 = BuiltInTypeSymbol::createI32(tests::testDefinition(136), source::SourceLoc{});
  const auto sourceDefinition = tests::testDefinition(137);
  FieldSymbol sourceSymbol(sourceDefinition, "sourceField"_zc, SymbolFlags::Private,
                           source::SourceLoc{}, zc::mv(type2), false);

  auto type3 = BuiltInTypeSymbol::createI32(tests::testDefinition(138), source::SourceLoc{});
  FieldSymbol targetSymbol(tests::testDefinition(139), "targetField"_zc, SymbolFlags::TermKind,
                           source::SourceLoc{}, zc::mv(type3), true);

  targetSymbol = zc::mv(sourceSymbol);
  ZC_EXPECT(targetSymbol.getName() == "sourceField"_zc);
  ZC_EXPECT(targetSymbol.getId() == sourceDefinition);
  ZC_EXPECT(targetSymbol.isPrivate());
  ZC_EXPECT(!targetSymbol.isMutable());
}

// Additional EnumCaseSymbol tests
ZC_TEST("EnumCaseSymbol_AssociatedValue") {
  auto type = BuiltInTypeSymbol::createI32(tests::testDefinition(82), source::SourceLoc{});
  EnumCaseSymbol enumCaseSymbol(tests::testDefinition(83), "testCase"_zc, SymbolFlags::TermKind,
                                source::SourceLoc{}, zc::mv(type));

  // Test initial state (no associated value)
  ZC_EXPECT(enumCaseSymbol.getAssociatedValue() == zc::none);

  // Test setting associated value
  enumCaseSymbol.setAssociatedValue(42);
  ZC_IF_SOME(value, enumCaseSymbol.getAssociatedValue()) { ZC_EXPECT(value == 42); }
  else { ZC_FAIL_REQUIRE("Should have associated value"); }

  // Test changing associated value
  enumCaseSymbol.setAssociatedValue(-100);
  ZC_IF_SOME(value, enumCaseSymbol.getAssociatedValue()) { ZC_EXPECT(value == -100); }
  else { ZC_FAIL_REQUIRE("Should have associated value"); }
}

ZC_TEST("EnumCaseSymbol_Classof") {
  auto type = BuiltInTypeSymbol::createI32(tests::testDefinition(84), source::SourceLoc{});
  EnumCaseSymbol enumCaseSymbol(tests::testDefinition(85), "testCase"_zc, SymbolFlags::TermKind,
                                source::SourceLoc{}, zc::mv(type));

  ZC_EXPECT(EnumCaseSymbol::classof(enumCaseSymbol));

  auto varType = BuiltInTypeSymbol::createI32(tests::testDefinition(86), source::SourceLoc{});
  VariableSymbol variableSymbol(tests::testDefinition(87), "testVar"_zc, SymbolFlags::TermKind,
                                source::SourceLoc{}, zc::mv(varType), true);
  ZC_EXPECT(!EnumCaseSymbol::classof(variableSymbol));
}

ZC_TEST("EnumCaseSymbol_MoveOperations") {
  auto type1 = BuiltInTypeSymbol::createI32(tests::testDefinition(140), source::SourceLoc{});
  const auto originalDefinition = tests::testDefinition(141);
  EnumCaseSymbol originalSymbol(originalDefinition, "originalCase"_zc, SymbolFlags::TermKind,
                                source::SourceLoc{}, zc::mv(type1));
  originalSymbol.setAssociatedValue(42);

  // Test move constructor
  EnumCaseSymbol movedSymbol = zc::mv(originalSymbol);
  ZC_EXPECT(movedSymbol.getName() == "originalCase"_zc);
  ZC_EXPECT(movedSymbol.getId() == originalDefinition);
  ZC_IF_SOME(value, movedSymbol.getAssociatedValue()) { ZC_EXPECT(value == 42); }
  else { ZC_FAIL_REQUIRE("Should have associated value after move"); }

  // Test move assignment
  auto type2 = BuiltInTypeSymbol::createI32(tests::testDefinition(142), source::SourceLoc{});
  const auto sourceDefinition = tests::testDefinition(143);
  EnumCaseSymbol sourceSymbol(sourceDefinition, "sourceCase"_zc, SymbolFlags::TermKind,
                              source::SourceLoc{}, zc::mv(type2));
  sourceSymbol.setAssociatedValue(-100);

  auto type3 = BuiltInTypeSymbol::createI32(tests::testDefinition(144), source::SourceLoc{});
  EnumCaseSymbol targetSymbol(tests::testDefinition(145), "targetCase"_zc, SymbolFlags::TermKind,
                              source::SourceLoc{}, zc::mv(type3));

  targetSymbol = zc::mv(sourceSymbol);
  ZC_EXPECT(targetSymbol.getName() == "sourceCase"_zc);
  ZC_EXPECT(targetSymbol.getId() == sourceDefinition);
  ZC_IF_SOME(value, targetSymbol.getAssociatedValue()) { ZC_EXPECT(value == -100); }
  else { ZC_FAIL_REQUIRE("Should have associated value after move assignment"); }
}

ZC_TEST("Symbol_ClassofComprehensive") {
  // Create different types of symbols for comprehensive classof testing
  auto type1 = BuiltInTypeSymbol::createI32(tests::testDefinition(90), source::SourceLoc{});
  auto type2 = BuiltInTypeSymbol::createI32(tests::testDefinition(91), source::SourceLoc{});
  auto type3 = BuiltInTypeSymbol::createI32(tests::testDefinition(92), source::SourceLoc{});
  auto type4 = BuiltInTypeSymbol::createI32(tests::testDefinition(93), source::SourceLoc{});
  auto type5 = BuiltInTypeSymbol::createI32(tests::testDefinition(94), source::SourceLoc{});
  auto type6 = BuiltInTypeSymbol::createI32(tests::testDefinition(95), source::SourceLoc{});

  VariableSymbol variableSymbol(tests::testDefinition(96), "testVar"_zc, SymbolFlags::TermKind,
                                source::SourceLoc{}, zc::mv(type1), true);
  ConstantSymbol constantSymbol(tests::testDefinition(97), "testConst"_zc, SymbolFlags::TermKind,
                                source::SourceLoc{}, zc::mv(type2));
  ParameterSymbol parameterSymbol(tests::testDefinition(98), "testParam"_zc, SymbolFlags::TermKind,
                                  source::SourceLoc{}, zc::mv(type3), false);
  FunctionSymbol functionSymbol(tests::testDefinition(99), "testFunc"_zc, SymbolFlags::TermKind,
                                source::SourceLoc{}, zc::mv(type4));
  FieldSymbol fieldSymbol(tests::testDefinition(100), "testField"_zc, SymbolFlags::TermKind,
                          source::SourceLoc{}, zc::mv(type5), true);
  EnumCaseSymbol enumCaseSymbol(tests::testDefinition(101), "testCase"_zc, SymbolFlags::TermKind,
                                source::SourceLoc{}, zc::mv(type6));

  // Test VariableSymbol classof false cases
  ZC_EXPECT(!VariableSymbol::classof(constantSymbol));
  ZC_EXPECT(!VariableSymbol::classof(parameterSymbol));
  ZC_EXPECT(!VariableSymbol::classof(functionSymbol));
  ZC_EXPECT(!VariableSymbol::classof(fieldSymbol));
  ZC_EXPECT(!VariableSymbol::classof(enumCaseSymbol));

  // Test ConstantSymbol classof false cases
  ZC_EXPECT(!ConstantSymbol::classof(variableSymbol));
  ZC_EXPECT(!ConstantSymbol::classof(parameterSymbol));
  ZC_EXPECT(!ConstantSymbol::classof(functionSymbol));
  ZC_EXPECT(!ConstantSymbol::classof(fieldSymbol));
  ZC_EXPECT(!ConstantSymbol::classof(enumCaseSymbol));

  // Test ParameterSymbol classof false cases
  ZC_EXPECT(!ParameterSymbol::classof(variableSymbol));
  ZC_EXPECT(!ParameterSymbol::classof(constantSymbol));
  ZC_EXPECT(!ParameterSymbol::classof(functionSymbol));
  ZC_EXPECT(!ParameterSymbol::classof(fieldSymbol));
  ZC_EXPECT(!ParameterSymbol::classof(enumCaseSymbol));

  // Test FunctionSymbol classof false cases
  ZC_EXPECT(!FunctionSymbol::classof(variableSymbol));
  ZC_EXPECT(!FunctionSymbol::classof(constantSymbol));
  ZC_EXPECT(!FunctionSymbol::classof(parameterSymbol));
  ZC_EXPECT(!FunctionSymbol::classof(fieldSymbol));
  ZC_EXPECT(!FunctionSymbol::classof(enumCaseSymbol));

  // Test FieldSymbol classof false cases
  ZC_EXPECT(!FieldSymbol::classof(variableSymbol));
  ZC_EXPECT(!FieldSymbol::classof(constantSymbol));
  ZC_EXPECT(!FieldSymbol::classof(parameterSymbol));
  ZC_EXPECT(!FieldSymbol::classof(functionSymbol));
  ZC_EXPECT(!FieldSymbol::classof(enumCaseSymbol));

  // Test EnumCaseSymbol classof false cases
  ZC_EXPECT(!EnumCaseSymbol::classof(variableSymbol));
  ZC_EXPECT(!EnumCaseSymbol::classof(constantSymbol));
  ZC_EXPECT(!EnumCaseSymbol::classof(parameterSymbol));
  ZC_EXPECT(!EnumCaseSymbol::classof(functionSymbol));
  ZC_EXPECT(!EnumCaseSymbol::classof(fieldSymbol));
}

}  // namespace symbol
}  // namespace compiler
}  // namespace zomlang
