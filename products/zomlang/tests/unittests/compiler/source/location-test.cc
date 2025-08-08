#include "zomlang/compiler/source/location.h"

#include "zc/core/debug.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {
namespace source {

ZC_TEST("SourceLocation InvalidLocation") {
  zomlang::compiler::source::SourceLoc loc;
  ZC_EXPECT(loc.isInvalid());
}

ZC_TEST("SourceLocation ValidLocation") {
  zomlang::compiler::source::SourceManager sm;
  zc::String code = zc::str("test");
  // auto bufferId = sm.addBuffer(zc::str("test.zom"), code);

  // zomlang::compiler::source::SourceLoc loc = sm.getLocForOffset(bufferId, 0);
  // ZC_EXPECT(!loc.isInvalid());
}

ZC_TEST("SourceLocation ToStringInvalid") {
  zomlang::compiler::source::SourceLoc loc;
  zomlang::compiler::source::SourceManager sm;
  uint64_t lastBufferId = 0;

  zc::String result = loc.toString(sm, lastBufferId);
  ZC_EXPECT(result.contains("invalid"));
}

ZC_TEST("SourceLocation SourceRangeInvalid") {
  zomlang::compiler::source::SourceRange range;
  ZC_EXPECT(range.isInvalid());
}

ZC_TEST("SourceLocation SourceRangeValid") {
  zomlang::compiler::source::SourceManager sm;
  zc::String code = zc::str("test");
  // auto bufferId = sm.addBuffer(zc::str("test.zom"), code);

  // zomlang::compiler::source::SourceLoc start = sm.getLocForOffset(bufferId, 0);
  // zomlang::compiler::source::SourceLoc end = sm.getLocForOffset(bufferId, 4);
  // zomlang::compiler::source::SourceRange range(start, end);

  // ZC_EXPECT(!range.isInvalid());
}

ZC_TEST("SourceLocation SourceRangeGetText") {
  zomlang::compiler::source::SourceManager sm;
  zc::String code = zc::str("hello world");
  // auto bufferId = sm.addBuffer(zc::str("test.zom"), code);

  // zomlang::compiler::source::SourceLoc start = sm.getLocForOffset(bufferId, 0);
  // zomlang::compiler::source::SourceLoc end = sm.getLocForOffset(bufferId, 5);
  // zomlang::compiler::source::SourceRange range(start, end);

  // zc::String text = range.getText(sm);
  // ZC_EXPECT(text == "hello");
}

ZC_TEST("SourceLocation CharSourceRangeInvalid") {
  zomlang::compiler::source::CharSourceRange range;
  zomlang::compiler::source::SourceManager sm;

  zc::String text = range.getText(sm);
  ZC_EXPECT(text.size() == 0);
}

}  // namespace source
}  // namespace compiler
}  // namespace zomlang