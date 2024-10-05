#ifndef ZOM_TYPECHECK_TYPECHECKER_H_
#define ZOM_TYPECHECK_TYPECHECKER_H_

#include "src/zom/basic/stage.h"
#include "src/zom/typecheck/symbol_table.h"
#include "src/zom/zis/zis.h"

namespace zom {
namespace typecheck {

// class TypeChecker : public basic::CompilerStage<zc::Own<zis::ZIS>,
// zc::String> {
//  protected:
//   void Process(const zc::Own<zis::ZIS>& input,
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
}  // namespace zom

#endif  // ZOM_TYPECHECK_TYPECHECKER_H_