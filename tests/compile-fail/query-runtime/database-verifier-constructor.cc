#include "compiler/query/query-database.h"

struct QueryDatabaseVerifier final {};

void forbiddenDatabaseVerifierConstructor(
    zomlang::compiler::basic::ThreadPool& scheduler,
    zomlang::compiler::query::QueryDescriptorInventoryRef inventory,
    QueryDatabaseVerifier&& verifier) {
  zomlang::compiler::query::QueryDatabase database(scheduler, inventory, zc::mv(verifier));
}
