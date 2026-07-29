#include "zomlang/compiler/query/query-types.h"

void forbiddenMemoDatabaseMutation(zomlang::compiler::query::RevisionLocalCapabilityMemoBase& memo,
                                   zomlang::compiler::query::QueryDatabaseIdentity&& identity) {
  memo.database() = zc::mv(identity);
}
