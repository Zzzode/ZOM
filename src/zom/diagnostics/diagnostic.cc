#include "src/zom/diagnostics/diagnostic.h"

#include "src/zc/base/common.h"
#include "src/zc/memory/memory.h"

namespace zom {
namespace diagnostics {

void Diagnostic::addChildDiagnostic(zc::Own<Diagnostic> child) {}

void Diagnostic::addFixIt(const FixIt& fix_it) {}

}  // namespace diagnostics
}  // namespace zom
