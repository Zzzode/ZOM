#include "compiler/driver/interface/borrow-evidence.h"

// BorrowEvidenceRepositoryCapability::clone is private to the repository and
// the HIR/MIR artifacts that embed a capability. A non-friend TU must not be
// able to duplicate a capability.
void forbiddenCapabilityClone(
    const zomlang::compiler::driver::borrow_evidence::BorrowEvidenceRepositoryCapability&
        capability) {
  auto copy = capability.clone();
  (void)copy;
}
