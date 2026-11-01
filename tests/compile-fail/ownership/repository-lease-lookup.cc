#include "compiler/driver/interface/borrow-evidence.h"

// Evidence lookup is a capability operation: callers hold a
// BorrowEvidenceRepositoryCapability, never lease-only access on the
// repository itself. BorrowEvidenceRepository deliberately exposes no
// lookup(lease) method.
void forbiddenRepositoryLeaseLookup(
    const zomlang::compiler::driver::borrow_evidence::BorrowEvidenceRepository& repository,
    const zomlang::compiler::driver::borrow_evidence::VerifiedBorrowEvidenceLease& lease) {
  (void)repository.lookup(lease);
}
