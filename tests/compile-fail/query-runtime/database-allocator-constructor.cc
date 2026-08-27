#include "compiler/query/query-database.h"

struct QueryDatabaseAllocator final {};

void forbiddenDatabaseAllocatorConstructor(
    zomlang::compiler::basic::ThreadPool& scheduler,
    zomlang::compiler::query::QueryDescriptorInventoryRef inventory,
    QueryDatabaseAllocator&& allocator) {
  zomlang::compiler::query::QueryDatabase database(scheduler, inventory, zc::mv(allocator));
}
