#include "compiler/query/query-database.h"

struct QueryDatabaseCallback final {};

void forbiddenDatabaseCallbackConstructor(
    zomlang::compiler::basic::ThreadPool& scheduler,
    zomlang::compiler::query::QueryDescriptorInventoryRef inventory,
    QueryDatabaseCallback&& callback) {
  zomlang::compiler::query::QueryDatabase database(scheduler, inventory, zc::mv(callback));
}
