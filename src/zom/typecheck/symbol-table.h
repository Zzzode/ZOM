#ifndef ZOM_TYPECHECK_SYMBOL_TABLE_H_
#define ZOM_TYPECHECK_SYMBOL_TABLE_H_

#include "src/zc/base/common.h"
#include "src/zc/containers/map.h"
#include "src/zc/strings/string.h"

namespace zom {
namespace typecheck {

struct Symbol {
  zc::String name;
  zc::String type;
  // Add more properties as needed
};

class SymbolTable {
public:
  void Insert(zc::String name, zc::Own<Symbol> symbol) {
    symbols.insert(zc::mv(name), zc::mv(symbol));
  }

  Symbol* Lookup(const zc::String& name) {
    ZC_IF_SOME(it, symbols.find(name)) { return it.get(); }
    return nullptr;
  }

private:
  zc::HashMap<zc::String, zc::Own<Symbol>> symbols;
};

}  // namespace typecheck
}  // namespace zom

#endif  // ZOM_TYPECHECK_SYMBOL_TABLE_H_
