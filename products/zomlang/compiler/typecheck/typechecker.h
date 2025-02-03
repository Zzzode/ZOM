#ifndef ZOM_TYPECHECK_TYPECHECKER_H_
#define ZOM_TYPECHECK_TYPECHECKER_H_

#include "zomlang/compiler/typecheck/symbol-table.h"
#include "zomlang/compiler/zis/zis.h"

namespace zomlang {
namespace typecheck {

// class TypeChecker : public CompilerStage<zc::Own<zis::ZIS>,
// zc::String> {
//  protected:
//   void process(const zc::Own<zis::ZIS>& input,
//                zc::Vector<zc::String>& outputs) override;
//
//  private:
//   SymbolTable symbol_table_;
//
//  public:
//   TypeChecker() = default;
//   ~TypeChecker() noexcept override = default;
// };

}  // namespace typecheck
}  // namespace zomlang

#endif  // ZOM_TYPECHECK_TYPECHECKER_H_
