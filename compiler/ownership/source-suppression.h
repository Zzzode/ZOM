// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "zc/core/vector.h"
#include "compiler/ownership/ownership-source-failure.h"

namespace zomlang::compiler::ownership {

/// \brief Applies RFC 0007 source suppression rules to the full computed failure set.
///
/// The pass runs after every independent producer has emitted its primaries and
/// removes only the cascading failures named by the event-local transition
/// rules:
///
/// - Rule 3: a `LinearConsumedTwice` primary at event E suppresses
///   `UseAfterMove` at the same event E. The second consumption is the
///   authoritative diagnostic; the moved-use cascade is redundant.
/// - Rule 4: a `MoveOutOfBorrow` primary at event E suppresses `UseAfterMove`
///   at the same event E. A blocked move does not move, drop, or consume the
///   place, so no moved-use cascade follows.
/// - Rule 7: unsafe-acknowledgement primaries are independent. They neither
///   suppress another unsafe occurrence nor any independent safe ownership
///   failure, so the pass retains them untouched.
///
/// Rules 5, 6, and 8 are producer invariants (a rejected borrow issues no loan,
/// a rejected escape extends no provenance, and `LinearNotConsumed` is emitted
/// once per obligation) and require no post-hoc suppression.
class SourceSuppression final {
public:
  ZC_NODISCARD static zc::Vector<OwnershipSourceFailure> suppress(
      zc::Vector<OwnershipSourceFailure>&& failures) noexcept;
};

}  // namespace zomlang::compiler::ownership
