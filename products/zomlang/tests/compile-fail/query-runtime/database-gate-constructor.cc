#include "zomlang/compiler/query/query-database.h"

struct QueryDatabaseGate final {};

void forbiddenDatabaseGateConstructor(
    zomlang::compiler::basic::ThreadPool& scheduler,
    zomlang::compiler::query::QueryDescriptorInventoryRef inventory, QueryDatabaseGate&& gate) {
  zomlang::compiler::query::QueryDatabase database(scheduler, inventory, zc::mv(gate));
}
