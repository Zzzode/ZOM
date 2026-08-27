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

#include "compiler/ownership/source-suppression.h"

namespace zomlang::compiler::ownership {
namespace {

/// \brief Collects the primary events of every LinearConsumedTwice and
/// MoveOutOfBorrow primary. A UseAfterMove at one of these events is a cascade
/// that the authoritative primary suppresses.
zc::Vector<MirEventKey> suppressingEvents(const zc::Vector<OwnershipSourceFailure>& failures) {
  zc::Vector<MirEventKey> events;
  for (const auto& failure : failures) {
    if (failure.is<LinearConsumedTwiceFailure>()) {
      events.add(failure.get<LinearConsumedTwiceFailure>().primary);
    } else if (failure.is<MoveOutOfBorrowFailure>()) {
      events.add(failure.get<MoveOutOfBorrowFailure>().primary);
    }
  }
  return events;
}

bool isSuppressedUseAfterMove(const UseAfterMoveFailure& failure,
                              const zc::Vector<MirEventKey>& suppressing) {
  for (const auto& event : suppressing) {
    if (failure.primary == event) return true;
  }
  return false;
}

}  // namespace

zc::Vector<OwnershipSourceFailure> SourceSuppression::suppress(
    zc::Vector<OwnershipSourceFailure>&& failures) noexcept {
  const auto suppressing = suppressingEvents(failures);
  if (suppressing.size() == 0) return zc::mv(failures);
  zc::Vector<OwnershipSourceFailure> retained;
  retained.reserve(failures.size());
  for (auto& failure : failures) {
    if (failure.is<UseAfterMoveFailure>() &&
        isSuppressedUseAfterMove(failure.get<UseAfterMoveFailure>(), suppressing)) {
      continue;
    }
    retained.add(zc::mv(failure));
  }
  return retained;
}

}  // namespace zomlang::compiler::ownership
