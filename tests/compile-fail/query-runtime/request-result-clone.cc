#include "compiler/query/query-types.h"

void forbiddenRequestResultClone(const zomlang::compiler::query::QueryRequestResult& result) {
  result.clone();
}
