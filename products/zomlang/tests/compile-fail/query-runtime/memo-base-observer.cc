#include "zomlang/compiler/query/query-types.h"

void forbiddenMemoBaseObserver(const zomlang::compiler::query::QueryRequestResult& result) {
  result.memoBase();
}
