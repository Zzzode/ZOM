#include "zomlang/compiler/query/query-types.h"

void forbiddenMemoBaseCast(zomlang::compiler::query::QueryRequestResult& result) {
  result.memoAs<int>();
}
