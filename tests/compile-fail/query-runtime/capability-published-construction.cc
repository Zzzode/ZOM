#include "compiler/query/query-types.h"

void forbiddenCapabilityPublishedConstruction() {
  zomlang::compiler::query::CapabilityPublished published;
}
