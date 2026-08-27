#include "compiler/query/query-types.h"

void forbiddenIdentityTokenConstruction() {
  zomlang::compiler::query::_query_detail::QueryDatabaseIdentityToken token;
}
