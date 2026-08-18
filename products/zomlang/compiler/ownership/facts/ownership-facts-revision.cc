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

#include "zomlang/compiler/ownership/facts/ownership-facts-revision.h"

namespace zomlang::compiler::ownership::facts {

OwnershipFactsRevision OwnershipFactsRevision::fromDigest(
    const identity::Sha256Digest& digest) noexcept {
  return OwnershipFactsRevision(digest);
}

const identity::Sha256Digest& OwnershipFactsRevision::digest() const noexcept { return value; }

OwnershipFactsRevision::OwnershipFactsRevision(const identity::Sha256Digest& digest) noexcept
    : value(digest) {}

}  // namespace zomlang::compiler::ownership::facts
