#include "compiler/driver/interface/borrow-evidence.h"

// VerifiedBorrowEvidenceLease::clone is private and friend-restricted to the
// repository, Built MIR, and its capability. A non-friend TU must not be able
// to clone a lease.
void forbiddenLeaseClone(
    const zomlang::compiler::driver::borrow_evidence::VerifiedBorrowEvidenceLease& lease) {
  auto copy = lease.clone();
  (void)copy;
}
