#include "zomlang/compiler/query/query-database.h"

void forbiddenDatabaseIdentityConstructor(
    zomlang::compiler::basic::ThreadPool& scheduler,
    zomlang::compiler::query::QueryDescriptorInventoryRef inventory,
    zomlang::compiler::query::QueryDatabaseIdentity&& identity) {
  zomlang::compiler::query::QueryDatabase database(scheduler, inventory, zc::mv(identity));
}
