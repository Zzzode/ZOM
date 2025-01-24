#include "zomlang/compiler/diagnostics/diagnostic.h"

#include "zc/core/common.h"
#include "zc/core/memory.h"

namespace zom {
namespace diagnostics {

void Diagnostic::addChildDiagnostic(zc::Own<Diagnostic> child) {}

void Diagnostic::addFixIt(const FixIt& fix_it) {}

}  // namespace diagnostics
}  // namespace zom
