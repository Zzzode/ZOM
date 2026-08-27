#include "compiler/query/query-types.h"

void forbiddenMemoKindMutation(zomlang::compiler::query::RevisionLocalCapabilityMemoBase& memo) {
  memo.kind = 1;
}
