#include "zomlang/compiler/checker/symbol-table.h"

#include "zc/core/debug.h"
#include "zc/ztest/test.h"

namespace zomlang {
namespace compiler {
namespace checker {

ZC_TEST("SymbolTable InsertAndLookup") {
  SymbolTable table;

  auto symbol = zc::heap<Symbol>();
  symbol->name = zc::str("test");
  symbol->type = zc::str("int");

  table.Insert(zc::str("test"), zc::mv(symbol));

  auto* found = table.Lookup(zc::str("test"));
  ZC_EXPECT(found != nullptr);
  ZC_EXPECT(found->name == "test");
  ZC_EXPECT(found->type == "int");
}

ZC_TEST("SymbolTable LookupNonExistent") {
  SymbolTable table;

  auto* found = table.Lookup(zc::str("nonexistent"));
  ZC_EXPECT(found == nullptr);
}

ZC_TEST("SymbolTable MultipleSymbols") {
  SymbolTable table;

  auto symbol1 = zc::heap<Symbol>();
  symbol1->name = zc::str("x");
  symbol1->type = zc::str("int");

  auto symbol2 = zc::heap<Symbol>();
  symbol2->name = zc::str("y");
  symbol2->type = zc::str("string");

  table.Insert(zc::str("x"), zc::mv(symbol1));
  table.Insert(zc::str("y"), zc::mv(symbol2));

  auto* found1 = table.Lookup(zc::str("x"));
  auto* found2 = table.Lookup(zc::str("y"));

  ZC_EXPECT(found1 != nullptr);
  ZC_EXPECT(found2 != nullptr);
  ZC_EXPECT(found1->name == "x");
  ZC_EXPECT(found1->type == "int");
  ZC_EXPECT(found2->name == "y");
  ZC_EXPECT(found2->type == "string");
}

ZC_TEST("SymbolTable OverwriteSymbol") {
  SymbolTable table;

  auto symbol1 = zc::heap<Symbol>();
  symbol1->name = zc::str("test");
  symbol1->type = zc::str("int");

  auto symbol2 = zc::heap<Symbol>();
  symbol2->name = zc::str("test");
  symbol2->type = zc::str("string");

  table.Insert(zc::str("test"), zc::mv(symbol1));
  table.Insert(zc::str("test"), zc::mv(symbol2));

  auto* found = table.Lookup(zc::str("test"));
  ZC_EXPECT(found != nullptr);
  ZC_EXPECT(found->type == "string");
}

}  // namespace checker
}  // namespace compiler
}  // namespace zomlang