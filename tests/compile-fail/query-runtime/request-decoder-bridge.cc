#include "compiler/query/query-database.h"

void forbiddenRequestDecoderBridge() {
  zomlang::compiler::query::test::QueryRuntimeTestAccess::decode();
}
