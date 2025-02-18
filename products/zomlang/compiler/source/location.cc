#include "zomlang/compiler/source/location.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {
namespace source {

ZC_NODISCARD zc::String SourceLoc::toString(const SourceManager& sm, uint64_t& lastBufferId) const {
  if (isInvalid()) { return zc::str("SourceLoc(invalid)"); }

  auto bufferId = ZC_ASSERT_NONNULL(sm.findBufferContainingLoc(*this));

  zc::StringPtr prefix;
  if (bufferId != lastBufferId) {
    prefix = sm.getIdentifierForBuffer(bufferId);
    lastBufferId = bufferId;
  } else {
    prefix = "line";
  }

  auto lineAndCol = sm.getPresumedLineAndColumnForLoc(*this, bufferId);

  return zc::str("SourceLoc(", prefix, ":", lineAndCol.line, ":", lineAndCol.column, " @ 0x",
                 zc::hex(reinterpret_cast<uintptr_t>(ptr)), ")");
}

void SourceLoc::print(zc::OutputStream& os, const SourceManager& sm) const {
  uint64_t tmp = ~0ULL;
  os.write(toString(sm, tmp).asBytes());
}

}  // namespace source
}  // namespace compiler
}  // namespace zomlang
