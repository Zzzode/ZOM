#include "src/zom/source/manager.h"

namespace zom {
namespace source {

// ... 其他方法的实现 ...

CharSourceRange SourceManager::GetCharSourceRange(SourceRange range) const {
  return CharSourceRange(range.start(), GetLocForEndOfToken(range.end()));
}

char SourceManager::ExtractCharAfter(SourceLoc loc) const {
  auto text = ExtractText(SourceRange(loc, loc.GetAdvancedLoc(1)));
  return text.empty() ? '\f' : text[0];
}

SourceLoc SourceManager::GetLocForEndOfToken(SourceLoc loc) const {
  // 这里需要实现具体的逻辑来获取标记结束的位置
  // 可能需要使用词法分析器或其他方法来确定标记的结束位置
  // 这里只是一个简单的示例实现
  return loc.GetAdvancedLoc(1);  // 简单地返回下一个位置
}

// ... 其他方法的实现 ...

}  // namespace source
}  // namespace zom