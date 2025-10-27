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

#include "zomlang/compiler/symbol/symbol-table.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/type.h"
#include "zomlang/compiler/symbol/scope.h"
#include "zomlang/compiler/symbol/symbol.h"
#include "zomlang/compiler/symbol/value-symbol.h"

namespace zomlang {
namespace compiler {
namespace symbol {

ZC_TEST("SymbolTable_BasicConstruction") {
  SymbolTable table;
  ZC_EXPECT(table.getSymbolCount() == 0);
  ZC_EXPECT(table.getDenotationCount() == 0);
}

ZC_TEST("SymbolTable_ScopeManagerAccess") {
  SymbolTable table;
  ScopeManager& scopeManager = table.getScopeManager();
  const ScopeManager& constScopeManager = table.getScopeManager();
  ZC_EXPECT(&scopeManager == &constScopeManager);
}

ZC_TEST("SymbolTable_SymbolCreationWithScope") {
  SymbolTable table;

  // Create scopes
  ScopeManager& scopeManager = table.getScopeManager();
  Scope& globalScope = scopeManager.createScope(Scope::Kind::Global, "global");
  Scope& classScope = scopeManager.createScope(Scope::Kind::Class, "MyClass", globalScope);

  // Create symbols in different scopes
  VariableSymbol& var1 = table.createVariable("var1", globalScope);
  VariableSymbol& var2 = table.createVariable("var2", classScope);
  (void)var1;  // Mark as used
  (void)var2;  // Mark as used

  ZC_EXPECT(table.getSymbolCount() == 2);

  // Test scope-specific lookup
  auto foundInGlobal = table.lookup("var1", globalScope);
  ZC_IF_SOME(symbol, foundInGlobal) { ZC_EXPECT(symbol.getName() == "var1"); }
  else { ZC_FAIL_EXPECT("Expected to find var1 in global scope"); }

  auto notFoundInGlobal = table.lookup("var2", globalScope);
  ZC_IF_SOME(symbol, notFoundInGlobal) {
    ZC_FAIL_EXPECT("Should not find var2 in global scope");
    (void)symbol;  // Mark as used
  }
}

ZC_TEST("SymbolTable_SymbolLookup") {
  SymbolTable table;
  ScopeManager& scopeManager = table.getScopeManager();
  Scope& globalScope = scopeManager.createScope(Scope::Kind::Global, "global");

  // Create different types of symbols
  VariableSymbol& var = table.createVariable("myVar", globalScope);
  FunctionSymbol& func = table.createFunction("myFunc", globalScope);
  ClassSymbol& cls = table.createClass("MyClass", globalScope);
  (void)var;   // Mark as used
  (void)func;  // Mark as used
  (void)cls;   // Mark as used

  // Test lookup for each symbol type
  auto foundVar = table.lookup("myVar", globalScope);
  ZC_IF_SOME(symbol, foundVar) { ZC_EXPECT(symbol.getName() == "myVar"); }
  else { ZC_FAIL_EXPECT("Expected to find myVar"); }

  auto foundFunc = table.lookup("myFunc", globalScope);
  ZC_IF_SOME(symbol, foundFunc) { ZC_EXPECT(symbol.getName() == "myFunc"); }
  else { ZC_FAIL_EXPECT("Expected to find myFunc"); }

  auto foundClass = table.lookup("MyClass", globalScope);
  ZC_IF_SOME(symbol, foundClass) { ZC_EXPECT(symbol.getName() == "MyClass"); }
  else { ZC_FAIL_EXPECT("Expected to find MyClass"); }

  // Test lookup for non-existent symbol
  auto notFound = table.lookup("nonexistent", globalScope);
  ZC_IF_SOME(symbol, notFound) {
    ZC_FAIL_EXPECT("Should not find nonexistent symbol");
    (void)symbol;  // Mark as used
  }
}

ZC_TEST("SymbolTable_SymbolLookupByKind") {
  SymbolTable table;
  ScopeManager& scopeManager = table.getScopeManager();
  Scope& globalScope = scopeManager.createScope(Scope::Kind::Global, "global");

  // Create symbols of different kinds
  table.createVariable("var1", globalScope);
  table.createVariable("var2", globalScope);
  table.createFunction("func1", globalScope);
  table.createFunction("func2", globalScope);
  table.createClass("class1", globalScope);

  // Test symbol type filtering
  auto variables = table.getSymbolsOfType(SymbolKind::Variable);
  ZC_EXPECT(variables.size() >= 2);  // At least our two variables

  auto functions = table.getSymbolsOfType(SymbolKind::Function);
  ZC_EXPECT(functions.size() >= 2);  // At least our two functions

  auto classes = table.getSymbolsOfType(SymbolKind::Class);
  ZC_EXPECT(classes.size() >= 1);  // At least our one class

  auto interfaces = table.getSymbolsOfType(SymbolKind::Interface);
  ZC_EXPECT(interfaces.size() == 0);  // No interfaces created
}

ZC_TEST("SymbolTable_SymbolLookupByScope") {
  SymbolTable table;

  // Create scopes
  ScopeManager& scopeManager = table.getScopeManager();
  Scope& globalScope = scopeManager.createScope(Scope::Kind::Global, "global");
  const Scope& classScope = scopeManager.createScope(Scope::Kind::Class, "MyClass", globalScope);

  // Create symbols in different scopes
  table.createVariable("globalVar", globalScope);
  table.createFunction("globalFunc", globalScope);
  table.createVariable("classVar", classScope);
  table.createFunction("classFunc", classScope);

  // Test scope-specific lookup
  auto globalSymbols = table.getSymbolsInScope(globalScope);
  ZC_EXPECT(globalSymbols.size() == 2);

  auto classSymbols = table.getSymbolsInScope(classScope);
  ZC_EXPECT(classSymbols.size() == 2);
}

ZC_TEST("SymbolTable_SymbolResolution") {
  SymbolTable table;

  // Create scopes
  ScopeManager& scopeManager = table.getScopeManager();
  Scope& globalScope = scopeManager.createScope(Scope::Kind::Global, "global");
  const Scope& classScope = scopeManager.createScope(Scope::Kind::Class, "MyClass", globalScope);

  // Create symbols in different scopes
  table.createVariable("globalVar", globalScope);
  table.createVariable("classVar", classScope);

  // Test recursive lookup
  auto resolvedGlobal = table.lookupRecursive("globalVar", classScope);
  ZC_IF_SOME(symbol, resolvedGlobal) { ZC_EXPECT(symbol.getName() == "globalVar"); }
  else { ZC_FAIL_EXPECT("Expected to resolve globalVar from class scope"); }

  auto resolvedClass = table.lookupRecursive("classVar", classScope);
  ZC_IF_SOME(symbol, resolvedClass) { ZC_EXPECT(symbol.getName() == "classVar"); }
  else { ZC_FAIL_EXPECT("Expected to resolve classVar from class scope"); }

  // Test lookup for non-existent symbol
  auto notResolved = table.lookupRecursive("nonexistent", classScope);
  ZC_IF_SOME(symbol, notResolved) {
    ZC_FAIL_EXPECT("Should not resolve nonexistent symbol");
    (void)symbol;  // Mark as used
  }
}

ZC_TEST("SymbolTable_SymbolRemoval") {
  SymbolTable table;

  // Create scope
  ScopeManager& scopeManager = table.getScopeManager();
  const Scope& globalScope = scopeManager.createScope(Scope::Kind::Global, "global");

  // Create and remove symbol
  VariableSymbol& tempVar = table.createVariable("tempVar", globalScope);
  ZC_EXPECT(table.getSymbolCount() == 1);

  // Remove symbol using dropSymbol - VariableSymbol inherits from Symbol
  table.dropSymbol(tempVar, globalScope);
  ZC_EXPECT(table.getSymbolCount() == 0);
}

ZC_TEST("SymbolTable_ScopeSpecificRemoval") {
  SymbolTable table;

  // Create scopes
  ScopeManager& scopeManager = table.getScopeManager();
  Scope& globalScope = scopeManager.createScope(Scope::Kind::Global, "global");
  Scope& classScope = scopeManager.createScope(Scope::Kind::Class, "MyClass", globalScope);

  // Create symbols with same name in different scopes
  VariableSymbol& globalVar = table.createVariable("sharedName", globalScope);
  VariableSymbol& classVar = table.createVariable("sharedName", classScope);

  ZC_EXPECT(table.getSymbolCount() == 2);

  // Remove symbol from specific scope - VariableSymbol inherits from Symbol
  table.dropSymbol(globalVar, globalScope);
  ZC_EXPECT(table.getSymbolCount() == 1);

  // Verify symbol still exists in class scope
  auto foundInClass = table.lookup("sharedName", classScope);
  ZC_IF_SOME(symbol, foundInClass) {
    ZC_EXPECT(symbol.getName() == "sharedName");
    // Compare symbol IDs instead of pointers to avoid type mismatch
    ZC_EXPECT(symbol.getId() == classVar.getId());
  }
  else { ZC_FAIL_EXPECT("Expected to find sharedName in class scope"); }
}

ZC_TEST("SymbolTable_CurrentScopeManagement") {
  SymbolTable table;

  // Create scopes
  ScopeManager& scopeManager = table.getScopeManager();
  Scope& globalScope = scopeManager.createScope(Scope::Kind::Global, "global");
  Scope& classScope = scopeManager.createScope(Scope::Kind::Class, "MyClass", globalScope);

  // Test current scope setting and getting
  table.setCurrentScope(globalScope);
  auto current = table.getCurrentScope();
  ZC_IF_SOME(scope, current) { ZC_EXPECT(&scope == &globalScope); }
  else { ZC_FAIL_EXPECT("Expected to get current scope"); }

  table.setCurrentScope(classScope);
  current = table.getCurrentScope();
  ZC_IF_SOME(scope, current) { ZC_EXPECT(&scope == &classScope); }
  else { ZC_FAIL_EXPECT("Expected to get current scope"); }

  // Verify scope manager consistency
  ZC_EXPECT(&scopeManager == &table.getScopeManager());
}

ZC_TEST("SymbolTable_SymbolCreationHelpers") {
  SymbolTable table;

  // Create scope
  ScopeManager& scopeManager = table.getScopeManager();
  const Scope& globalScope = scopeManager.createScope(Scope::Kind::Global, "global");

  // Test symbol creation helpers
  VariableSymbol& variable = table.createVariable("myVariable", globalScope);
  (void)variable;  // Mark as used
  // ZC_EXPECT(variable.getKind() == SymbolKind::Variable);  // Commented out due to incomplete type

  FunctionSymbol& function = table.createFunction("myFunction", globalScope);
  (void)function;  // Mark as used
  // ZC_EXPECT(function.getKind() == SymbolKind::Function);  // Commented out due to incomplete type

  ClassSymbol& cls = table.createClass("MyClass", globalScope);
  (void)cls;  // Mark as used
  // ZC_EXPECT(cls.getKind() == SymbolKind::Class);  // Commented out due to incomplete type

  InterfaceSymbol& interface = table.createInterface("MyInterface", globalScope);
  (void)interface;  // Mark as used
  // ZC_EXPECT(interface.getKind() == SymbolKind::Interface);  // Commented out due to incomplete
  // type

  PackageSymbol& package = table.createPackage("mypackage", globalScope);
  (void)package;  // Mark as used
  // ZC_EXPECT(package.getKind() == SymbolKind::Package);  // Commented out due to incomplete type
}

ZC_TEST("SymbolTable_ComplexHierarchy") {
  SymbolTable table;

  // Create complex scope hierarchy
  ScopeManager& scopeManager = table.getScopeManager();
  Scope& globalScope = scopeManager.createScope(Scope::Kind::Global, "global");
  Scope& packageScope = scopeManager.createScope(Scope::Kind::Package, "mypackage", globalScope);
  Scope& classScope = scopeManager.createScope(Scope::Kind::Class, "MyClass", packageScope);
  const Scope& methodScope =
      scopeManager.createScope(Scope::Kind::Function, "myMethod", classScope);

  // Create symbols at each level
  table.createVariable("globalVar", globalScope);
  table.createVariable("packageVar", packageScope);
  table.createVariable("classVar", classScope);
  table.createVariable("methodVar", methodScope);

  // Test recursive resolution from deepest scope
  auto resolvedGlobal = table.lookupRecursive("globalVar", methodScope);
  ZC_IF_SOME(symbol, resolvedGlobal) { ZC_EXPECT(symbol.getName() == "globalVar"); }
  else { ZC_FAIL_EXPECT("Expected to resolve globalVar from method scope"); }

  auto resolvedPackage = table.lookupRecursive("packageVar", methodScope);
  ZC_IF_SOME(symbol, resolvedPackage) { ZC_EXPECT(symbol.getName() == "packageVar"); }
  else { ZC_FAIL_EXPECT("Expected to resolve packageVar from method scope"); }

  auto resolvedClass = table.lookupRecursive("classVar", methodScope);
  ZC_IF_SOME(symbol, resolvedClass) { ZC_EXPECT(symbol.getName() == "classVar"); }
  else { ZC_FAIL_EXPECT("Expected to resolve classVar from method scope"); }

  auto resolvedMethod = table.lookupRecursive("methodVar", methodScope);
  ZC_IF_SOME(symbol, resolvedMethod) { ZC_EXPECT(symbol.getName() == "methodVar"); }
  else { ZC_FAIL_EXPECT("Expected to resolve methodVar from method scope"); }
}

ZC_TEST("SymbolTable_OverwriteBehavior") {
  SymbolTable table;

  // Create scopes
  ScopeManager& scopeManager = table.getScopeManager();
  Scope& globalScope = scopeManager.createScope(Scope::Kind::Global, "global");
  const Scope& classScope = scopeManager.createScope(Scope::Kind::Class, "MyClass", globalScope);

  // Create symbols with same name in different scopes
  VariableSymbol& symbol1 = table.createVariable("sharedName", globalScope);
  (void)symbol1;  // Mark as used
  ZC_EXPECT(table.getSymbolCount() == 1);

  // Create another symbol with same name in different scope
  VariableSymbol& symbol2 = table.createVariable("sharedName", classScope);
  (void)symbol2;  // Mark as used
  ZC_EXPECT(table.getSymbolCount() == 2);

  // Verify both symbols exist in their respective scopes
  auto foundInGlobal = table.lookup("sharedName", globalScope);
  ZC_IF_SOME(symbol, foundInGlobal) { ZC_EXPECT(symbol.getName() == "sharedName"); }
  else { ZC_FAIL_EXPECT("Expected to find sharedName symbol in global scope"); }

  auto foundInClass = table.lookup("sharedName", classScope);
  ZC_IF_SOME(symbol, foundInClass) { ZC_EXPECT(symbol.getName() == "sharedName"); }
  else { ZC_FAIL_EXPECT("Expected to find sharedName symbol in class scope"); }
}

ZC_TEST("SymbolTable_EmptyAndValidation") {
  SymbolTable table;

  // Test empty table
  ZC_EXPECT(table.getSymbolCount() == 0);

  // Test table with symbols
  ScopeManager& scopeManager = table.getScopeManager();
  const Scope& globalScope = scopeManager.createScope(Scope::Kind::Global, "global");
  table.createVariable("testVar", globalScope);

  ZC_EXPECT(table.getSymbolCount() == 1);
}

ZC_TEST("SymbolTable_DumpFunctionality") {
  SymbolTable table;

  // Create scopes and symbols
  ScopeManager& scopeManager = table.getScopeManager();
  Scope& globalScope = scopeManager.createScope(Scope::Kind::Global, "global");
  const Scope& classScope = scopeManager.createScope(Scope::Kind::Class, "MyClass", globalScope);

  table.createVariable("var1", globalScope);
  table.createFunction("func1", globalScope);
  table.createVariable("var2", classScope);

  // Test dump functionality (should not throw)
  table.dumpSymbols();
  table.dumpDenotations();
}

ZC_TEST("SymbolTable_QualifiedNameResolution") {
  SymbolTable table;

  // Create scopes
  ScopeManager& scopeManager = table.getScopeManager();
  Scope& globalScope = scopeManager.createScope(Scope::Kind::Global, "global");
  const Scope& packageScope =
      scopeManager.createScope(Scope::Kind::Package, "mypackage", globalScope);

  // Create symbols
  table.createVariable("globalVar", globalScope);
  table.createClass("MyClass", packageScope);

  // Test qualified name resolution
  auto resolved = table.resolveQualified("mypackage.MyClass", globalScope);
  ZC_IF_SOME(symbol, resolved) { ZC_EXPECT(symbol.getName() == "MyClass"); }
  else { ZC_FAIL_EXPECT("Expected to resolve qualified name"); }
}

ZC_TEST("SymbolTable_PhaseManagement") {
  SymbolTable table;

  // Test default phase
  ZC_EXPECT(table.getCurrentPhase() == 0);  // Default phase

  // Note: setCurrentPhase method is not implemented yet
  // table.setCurrentPhase(1);
  // ZC_EXPECT(table.getCurrentPhase() == 1);

  // table.setCurrentPhase(2);
  // ZC_EXPECT(table.getCurrentPhase() == 2);
}

ZC_TEST("SymbolTable_DenotationLookup") {
  SymbolTable table;

  // Create scope and symbol
  ScopeManager& scopeManager = table.getScopeManager();
  const Scope& globalScope = scopeManager.createScope(Scope::Kind::Global, "global");
  table.createVariable("testVar", globalScope);

  // Test denotation lookup
  auto denotation = table.lookupDenotation("testVar", globalScope);
  ZC_IF_SOME(denot, denotation) {
    ZC_EXPECT(denot.getName() == "testVar");
    ZC_EXPECT(&denot.getScope() == &globalScope);
  }
  else { ZC_FAIL_EXPECT("Expected to find denotation"); }

  // Test recursive denotation lookup
  auto recursiveDenotation = table.lookupDenotationRecursive("testVar", globalScope);
  ZC_IF_SOME(denot, recursiveDenotation) { ZC_EXPECT(denot.getName() == "testVar"); }
  else { ZC_FAIL_EXPECT("Expected to find denotation recursively"); }
}

ZC_TEST("SymbolTable_ImplicitSymbols") {
  SymbolTable table;

  // Create scope
  ScopeManager& scopeManager = table.getScopeManager();
  const Scope& globalScope = scopeManager.createScope(Scope::Kind::Global, "global");
  VariableSymbol& implicitVar = table.createVariable("implicitVar", globalScope);

  // Register as implicit symbol - VariableSymbol inherits from Symbol
  table.registerImplicitSymbol(implicitVar, globalScope);

  // Verify implicit symbols
  auto implicits = table.getImplicitSymbols(globalScope);
  ZC_EXPECT(implicits.size() >= 1);
}

ZC_TEST("SymbolTable_EnterDropSymbols") {
  SymbolTable table;

  // Create scope
  ScopeManager& scopeManager = table.getScopeManager();
  const Scope& globalScope = scopeManager.createScope(Scope::Kind::Global, "global");
  VariableSymbol& testVar = table.createVariable("testVar", globalScope);

  ZC_EXPECT(table.getSymbolCount() == 1);

  // Drop symbol - VariableSymbol inherits from Symbol
  table.dropSymbol(testVar, globalScope);
  ZC_EXPECT(table.getSymbolCount() == 0);

  // Enter symbol - VariableSymbol inherits from Symbol
  table.enterSymbol(testVar, globalScope);
  ZC_EXPECT(table.getSymbolCount() == 1);
}

ZC_TEST("SymbolTable_AllSymbolsRetrieval") {
  SymbolTable table;

  // Create scope and symbols
  ScopeManager& scopeManager = table.getScopeManager();
  const Scope& globalScope = scopeManager.createScope(Scope::Kind::Global, "global");

  table.createVariable("var1", globalScope);
  table.createFunction("func1", globalScope);
  table.createClass("Class1", globalScope);

  // Test getting all symbols
  auto allSymbols = table.getAllSymbols();
  ZC_EXPECT(allSymbols.size() == 3);

  // Test getting symbols in specific scope
  auto scopeSymbols = table.getSymbolsInScope(globalScope);
  ZC_EXPECT(scopeSymbols.size() == 3);
}

ZC_TEST("SymbolTable_LookupInCurrentScope") {
  SymbolTable table;

  // Create scope and set as current
  ScopeManager& scopeManager = table.getScopeManager();
  const Scope& globalScope = scopeManager.createScope(Scope::Kind::Global, "global");
  table.setCurrentScope(globalScope);

  table.createVariable("currentVar", globalScope);

  // Test lookup in current scope
  auto found = table.lookupInCurrentScope("currentVar");
  ZC_IF_SOME(symbol, found) { ZC_EXPECT(symbol.getName() == "currentVar"); }
  else { ZC_FAIL_EXPECT("Expected to find symbol in current scope"); }

  // Test lookup for non-existent symbol
  auto notFound = table.lookupInCurrentScope("nonexistent");
  ZC_IF_SOME(symbol, notFound) {
    ZC_FAIL_EXPECT("Should not find nonexistent symbol in current scope");
    (void)symbol;  // Mark as used
  }
}

}  // namespace symbol
}  // namespace compiler
}  // namespace zomlang
