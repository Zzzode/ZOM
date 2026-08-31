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

// RFC 0023 "IDE Semantic Snapshots" document-version contract (L305, L370-377):
// prove the signed 32-bit value accepts every LSP integer including negatives
// and the boundary values, and that `succeeds` decides strict, not necessarily
// consecutive, monotonicity within one open lifecycle. This is a pure immutable
// value; it constructs no query, session, or AST.

#include "compiler/ide/document-version.h"

#include <cstdint>

#include "zc/ztest/test.h"

namespace zomlang::compiler::ide {
namespace {

ZC_TEST("DocumentVersion accepts every signed 32-bit LSP integer including negatives") {
  // didOpen may be any LspInteger; negative values are valid (RFC 0023 L370-377).
  ZC_EXPECT(DocumentVersion::initial(-5).raw() == -5);
  ZC_EXPECT(DocumentVersion::initial(0).raw() == 0);
  ZC_EXPECT(DocumentVersion::initial(7).raw() == 7);
  ZC_EXPECT(DocumentVersion::initial(INT32_MIN).raw() == INT32_MIN);
  ZC_EXPECT(DocumentVersion::initial(INT32_MAX).raw() == INT32_MAX);
}

ZC_TEST("DocumentVersion::succeeds decides strict monotonicity") {
  ZC_EXPECT(DocumentVersion::initial(5).succeeds(DocumentVersion::initial(3)));
  // Monotonicity holds within the negative subrange.
  ZC_EXPECT(DocumentVersion::initial(-1).succeeds(DocumentVersion::initial(-5)));
  // Equal and lower versions do not succeed.
  ZC_EXPECT(!DocumentVersion::initial(5).succeeds(DocumentVersion::initial(5)));
  ZC_EXPECT(!DocumentVersion::initial(3).succeeds(DocumentVersion::initial(5)));
}

ZC_TEST("DocumentVersion::succeeds does not require consecutive versions") {
  // didChange versions need not be consecutive; only strict `>` is required.
  ZC_EXPECT(DocumentVersion::initial(100).succeeds(DocumentVersion::initial(2)));
  ZC_EXPECT(!DocumentVersion::initial(2).succeeds(DocumentVersion::initial(100)));
}

ZC_TEST("DocumentVersion::succeeds spans the full signed 32-bit range") {
  ZC_EXPECT(DocumentVersion::initial(INT32_MAX).succeeds(DocumentVersion::initial(INT32_MIN)));
  ZC_EXPECT(!DocumentVersion::initial(INT32_MIN).succeeds(DocumentVersion::initial(INT32_MAX)));
}

ZC_TEST("DocumentVersion equality reflects the underlying value") {
  ZC_EXPECT(DocumentVersion::initial(9) == DocumentVersion::initial(9));
  ZC_EXPECT(DocumentVersion::initial(9) != DocumentVersion::initial(8));
  ZC_EXPECT(DocumentVersion::initial(INT32_MIN) == DocumentVersion::initial(INT32_MIN));
  ZC_EXPECT(!(DocumentVersion::initial(-1) == DocumentVersion::initial(1)));
}

}  // namespace
}  // namespace zomlang::compiler::ide
