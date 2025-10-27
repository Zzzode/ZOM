#include "zomlang/compiler/symbol/symbol.h"

#include "zc/core/debug.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/type.h"
#include "zomlang/compiler/source/location.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/compiler/symbol/type-symbol.h"
#include "zomlang/compiler/symbol/value-symbol.h"

namespace zomlang {
namespace compiler {
namespace symbol {

ZC_TEST("SymbolBase_ConstructionAndIdentity") {
  source::SourceLoc location;
  SymbolId id = SymbolId::create(1);

  Symbol symbol(id, "testSymbol", SymbolFlags::Public, location);

  ZC_EXPECT(symbol.getId() == id);
  ZC_EXPECT(symbol.getName() == "testSymbol");
  ZC_EXPECT(symbol.hasFlag(SymbolFlags::Public));
}

ZC_TEST("SymbolBase_FlagsManagement") {
  source::SourceLoc location;
  SymbolId id = SymbolId::create(2);

  Symbol symbol(id, "testSymbol", SymbolFlags::Public, location);

  ZC_EXPECT(symbol.hasFlag(SymbolFlags::Public));
  ZC_EXPECT(!symbol.hasFlag(SymbolFlags::Private));

  symbol.addFlag(SymbolFlags::Static);
  ZC_EXPECT(symbol.hasFlag(SymbolFlags::Static));
  ZC_EXPECT(symbol.hasAllFlags(SymbolFlags::Public | SymbolFlags::Static));

  symbol.removeFlag(SymbolFlags::Public);
  ZC_EXPECT(!symbol.hasFlag(SymbolFlags::Public));
  ZC_EXPECT(symbol.hasFlag(SymbolFlags::Static));
}

ZC_TEST("TypeSymbol_ConstructionAndClassification") {
  source::SourceLoc location;
  SymbolId id = SymbolId::create(3);

  TypeSymbol typeSymbol(id, "MyType", SymbolFlags::Public, location);

  ZC_EXPECT(typeSymbol.getKind() == SymbolKind::Type);
  ZC_EXPECT(typeSymbol.isTypeSymbol());
  ZC_EXPECT(!typeSymbol.isValueSymbol());
  ZC_EXPECT(typeSymbol.getName() == "MyType");
}

ZC_TEST("BuiltInTypeSymbol_PrimitiveTypes") {
  source::SourceLoc location;
  SymbolId id = SymbolId::create(4);

  auto intType = BuiltInTypeSymbol::createI32(id, location);
  auto floatType = BuiltInTypeSymbol::createF32(id, location);
  auto stringType = BuiltInTypeSymbol::createStr(id, location);
  auto boolType = BuiltInTypeSymbol::createBool(id, location);
  auto voidType = BuiltInTypeSymbol::createUnit(id, location);

  ZC_EXPECT(intType->isPrimitive());
  ZC_EXPECT(floatType->isPrimitive());
  ZC_EXPECT(stringType->isPrimitive());
  ZC_EXPECT(boolType->isPrimitive());
  ZC_EXPECT(voidType->isPrimitive());
}

ZC_TEST("ClassSymbol_ConstructionAndHierarchy") {
  source::SourceLoc location;
  SymbolId id1 = SymbolId::create(5);

  ClassSymbol classSymbol(id1, "MyClass", SymbolFlags::Public, location);

  ZC_EXPECT(classSymbol.getName() == "MyClass");
  ZC_EXPECT(classSymbol.hasFlag(SymbolFlags::Public));
}

ZC_TEST("SymbolId_CreationAndUniqueness") {
  SymbolId id1 = SymbolId::create(1);
  SymbolId id2 = SymbolId::create(2);
  SymbolId id3 = SymbolId::create(3);

  ZC_EXPECT(id1 != id2);
  ZC_EXPECT(id1 != id3);
  ZC_EXPECT(id2 != id3);
}

ZC_TEST("SymbolBase_SetNameAndGetters") {
  source::SourceLoc location;
  SymbolId id = SymbolId::create(10);

  Symbol symbol(id, "originalName"_zc, SymbolFlags::Public, location);

  // Test setName
  symbol.setName("newName"_zc);
  ZC_EXPECT(symbol.getName() == "newName"_zc);

  // Test getFileName and getLine (these return default values for now)
  source::SourceManager sourceManager;
  ZC_EXPECT(symbol.getFileName(sourceManager) == "<unknown>"_zc);
  ZC_EXPECT(symbol.getLine(sourceManager) == 0u);
}

ZC_TEST("Symbol_IsMethodsAndKindChecking") {
  source::SourceLoc location;
  SymbolId id = SymbolId::create(11);

  Symbol symbol(id, "testSymbol"_zc, SymbolFlags::Public, location);
  TypeSymbol typeSymbol(id, "TestType"_zc, SymbolFlags::Public, location);

  // Test is* methods on base Symbol
  ZC_EXPECT(!symbol.isTypeSymbol());
  ZC_EXPECT(!symbol.isValueSymbol());
  ZC_EXPECT(!symbol.isModuleSymbol());
  ZC_EXPECT(!symbol.isFunctionSymbol());
  ZC_EXPECT(!symbol.isVariableSymbol());
  ZC_EXPECT(!symbol.isClassSymbol());

  // Test TypeSymbol classification
  ZC_EXPECT(typeSymbol.isTypeSymbol());
  ZC_EXPECT(!typeSymbol.isValueSymbol());
}

ZC_TEST("Symbol_TypeManagement") {
  source::SourceLoc location;
  SymbolId id = SymbolId::create(12);
  SymbolId typeId = SymbolId::create(13);

  Symbol symbol(id, "testSymbol"_zc, SymbolFlags::Public, location);
  TypeSymbol typeSymbol(typeId, "TestType"_zc, SymbolFlags::Public, location);

  // Test setType and getType
  ZC_EXPECT(symbol.getType() == zc::none);
  symbol.setType(typeSymbol);
  ZC_EXPECT(symbol.getType() != zc::none);
}

ZC_TEST("Symbol_VisibilityMethods") {
  source::SourceLoc location;
  SymbolId id = SymbolId::create(14);

  Symbol publicSymbol(id, "publicSymbol"_zc, SymbolFlags::Public, location);
  Symbol privateSymbol(id, "privateSymbol"_zc, SymbolFlags::Private, location);
  Symbol protectedSymbol(id, "protectedSymbol"_zc, SymbolFlags::Protected, location);
  Symbol internalSymbol(id, "internalSymbol"_zc, SymbolFlags::Internal, location);

  // Test visibility methods
  ZC_EXPECT(publicSymbol.isPublic());
  ZC_EXPECT(!publicSymbol.isPrivate());
  ZC_EXPECT(!publicSymbol.isProtected());
  ZC_EXPECT(!publicSymbol.isInternal());

  ZC_EXPECT(!privateSymbol.isPublic());
  ZC_EXPECT(privateSymbol.isPrivate());
  ZC_EXPECT(!privateSymbol.isProtected());
  ZC_EXPECT(!privateSymbol.isInternal());

  ZC_EXPECT(!protectedSymbol.isPublic());
  ZC_EXPECT(!protectedSymbol.isPrivate());
  ZC_EXPECT(protectedSymbol.isProtected());
  ZC_EXPECT(!protectedSymbol.isInternal());

  ZC_EXPECT(!internalSymbol.isPublic());
  ZC_EXPECT(!internalSymbol.isPrivate());
  ZC_EXPECT(!internalSymbol.isProtected());
  ZC_EXPECT(internalSymbol.isInternal());
}

ZC_TEST("Symbol_StorageAndLifetimeAttributes") {
  source::SourceLoc location;
  SymbolId id = SymbolId::create(15);

  Symbol staticSymbol(id, "staticSymbol"_zc, SymbolFlags::Static, location);
  Symbol finalSymbol(id, "finalSymbol"_zc, SymbolFlags::Final, location);
  Symbol mutableSymbol(id, "mutableSymbol"_zc, SymbolFlags::Mutable, location);
  Symbol lazySymbol(id, "lazySymbol"_zc, SymbolFlags::Lazy, location);
  Symbol inlineSymbol(id, "inlineSymbol"_zc, SymbolFlags::Inline, location);
  Symbol localSymbol(id, "localSymbol"_zc, SymbolFlags::Local, location);
  Symbol globalSymbol(id, "globalSymbol"_zc, SymbolFlags::Global, location);

  // Test storage attribute methods
  ZC_EXPECT(staticSymbol.isStatic());
  ZC_EXPECT(!staticSymbol.isInstance());

  ZC_EXPECT(finalSymbol.isFinal());
  ZC_EXPECT(!finalSymbol.isMutable());

  ZC_EXPECT(mutableSymbol.isMutable());
  ZC_EXPECT(!mutableSymbol.isFinal());

  ZC_EXPECT(lazySymbol.isLazy());
  ZC_EXPECT(inlineSymbol.isInline());

  ZC_EXPECT(localSymbol.isLocal());
  ZC_EXPECT(!localSymbol.isGlobal());

  ZC_EXPECT(globalSymbol.isGlobal());
  ZC_EXPECT(!globalSymbol.isLocal());
}

ZC_TEST("Symbol_ToStringMethod") {
  source::SourceLoc location;
  SymbolId id = SymbolId::create(16);

  Symbol symbol(id, "testSymbol"_zc, SymbolFlags::Public, location);
  TypeSymbol typeSymbol(id, "TestType"_zc, SymbolFlags::Public, location);

  // Test toString method
  auto symbolStr = symbol.toString();
  auto typeStr = typeSymbol.toString();

  ZC_EXPECT(symbolStr.size() > 0);
  ZC_EXPECT(typeStr.size() > 0);

  // toString should contain the symbol name
  ZC_EXPECT(symbolStr.contains("testSymbol"));
  ZC_EXPECT(typeStr.contains("TestType"));
}

}  // namespace symbol
}  // namespace compiler
}  // namespace zomlang
