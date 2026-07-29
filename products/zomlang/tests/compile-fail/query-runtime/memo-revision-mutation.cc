#include "zomlang/compiler/query/query-types.h"

void forbiddenMemoRevisionMutation(
    zomlang::compiler::query::RevisionLocalCapabilityMemoBase& memo) {
  memo.revision() = zomlang::compiler::query::DatabaseRevision(9);
}
