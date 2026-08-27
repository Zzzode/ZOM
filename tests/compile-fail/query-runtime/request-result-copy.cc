#include "compiler/query/query-types.h"

void forbiddenRequestResultCopy(const zomlang::compiler::query::QueryRequestResult& result) {
  auto copy = result;
}
