// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/symbol/symbol-id.h"

#include "zc/core/debug.h"
#include "zc/ztest/test.h"

namespace zomlang {
namespace compiler {
namespace symbol {

ZC_TEST("SymbolId_BasicUsage") {
  // Test default construction
  SymbolId id1;
  SymbolId id2;

  // Default IDs should be equal and invalid
  ZC_EXPECT(id1 == id2);
  ZC_EXPECT(!id1.isValid());
  ZC_EXPECT(!id2.isValid());

  // Test copy construction
  SymbolId id3(id1);
  ZC_EXPECT(id1 == id3);

  // Test assignment
  SymbolId id4;
  id4 = id1;
  ZC_EXPECT(id1 == id4);
}

ZC_TEST("SymbolId_Creation") {
  // Create IDs with different indices
  SymbolId id1 = SymbolId::create(1);
  SymbolId id2 = SymbolId::create(2);
  SymbolId id3 = SymbolId::create(3);

  // All created IDs should be valid and unique
  ZC_EXPECT(id1.isValid());
  ZC_EXPECT(id2.isValid());
  ZC_EXPECT(id3.isValid());

  ZC_EXPECT(id1 != id2);
  ZC_EXPECT(id1 != id3);
  ZC_EXPECT(id2 != id3);

  // Test that they have correct indices
  ZC_EXPECT(id1.index() == 1);
  ZC_EXPECT(id2.index() == 2);
  ZC_EXPECT(id3.index() == 3);
}

ZC_TEST("SymbolId_FromRaw") {
  // Create IDs from raw values
  SymbolId id1 = SymbolId::fromRaw(100);
  SymbolId id2 = SymbolId::fromRaw(200);
  SymbolId id3 = SymbolId::fromRaw(0);  // Invalid

  ZC_EXPECT(id1.isValid());
  ZC_EXPECT(id2.isValid());
  ZC_EXPECT(!id3.isValid());

  ZC_EXPECT(id1.getRaw() == 100);
  ZC_EXPECT(id2.getRaw() == 200);
  ZC_EXPECT(id3.getRaw() == 0);

  ZC_EXPECT(id1.index() == 100);
  ZC_EXPECT(id2.index() == 200);
  ZC_EXPECT(id3.index() == 0);
}

ZC_TEST("SymbolId_Comparison") {
  SymbolId id1 = SymbolId::create(10);
  SymbolId id2 = SymbolId::create(20);
  SymbolId id1_copy = SymbolId::create(10);

  // Test equality
  ZC_EXPECT(id1 == id1_copy);
  ZC_EXPECT(!(id1 == id2));

  // Test inequality
  ZC_EXPECT(id1 != id2);
  ZC_EXPECT(!(id1 != id1_copy));

  // Test less than (for ordering)
  ZC_EXPECT(id1 < id2);
  ZC_EXPECT(!(id2 < id1));
  ZC_EXPECT(!(id1 < id1_copy));
}

ZC_TEST("SymbolId_Ordering") {
  // Create multiple IDs with known order
  SymbolId id1 = SymbolId::create(1);
  SymbolId id2 = SymbolId::create(2);
  SymbolId id3 = SymbolId::create(3);

  // Test ordering
  ZC_EXPECT(id1 < id2);
  ZC_EXPECT(id2 < id3);
  ZC_EXPECT(id1 < id3);  // Transitivity

  // Test reverse ordering
  ZC_EXPECT(!(id2 < id1));
  ZC_EXPECT(!(id3 < id2));
  ZC_EXPECT(!(id3 < id1));
}

ZC_TEST("SymbolId_Hash") {
  SymbolId id1 = SymbolId::create(42);
  SymbolId id2 = SymbolId::create(84);
  SymbolId id1_copy = SymbolId::create(42);

  SymbolId::Hash hasher;

  // Same IDs should have same hash
  ZC_EXPECT(hasher(id1) == hasher(id1_copy));

  // Different IDs should likely have different hashes
  ZC_EXPECT(hasher(id1) != hasher(id2));

  // Hash should be consistent
  auto hash1 = hasher(id1);
  auto hash2 = hasher(id1);
  ZC_EXPECT(hash1 == hash2);
}

ZC_TEST("SymbolId_Validity") {
  // Test default ID validity
  SymbolId defaultId;
  ZC_EXPECT(!defaultId.isValid());
  ZC_EXPECT(defaultId.getRaw() == 0);
  ZC_EXPECT(defaultId.index() == 0);

  // Test created ID validity
  SymbolId validId = SymbolId::create(1);
  ZC_EXPECT(validId.isValid());
  ZC_EXPECT(validId.getRaw() == 1);
  ZC_EXPECT(validId.index() == 1);

  // Test zero raw value
  SymbolId zeroId = SymbolId::fromRaw(0);
  ZC_EXPECT(!zeroId.isValid());
  ZC_EXPECT(zeroId == defaultId);
}

ZC_TEST("SymbolId_LargeValues") {
  // Test with large values
  constexpr uint64_t LARGE_VALUE = 0xFFFFFFFFFFFFFFFFULL - 1;
  SymbolId largeId = SymbolId::fromRaw(LARGE_VALUE);

  ZC_EXPECT(largeId.isValid());
  ZC_EXPECT(largeId.getRaw() == LARGE_VALUE);
  ZC_EXPECT(largeId.index() == LARGE_VALUE);

  // Test maximum value (should still be valid)
  SymbolId maxId = SymbolId::fromRaw(0xFFFFFFFFFFFFFFFFULL);
  ZC_EXPECT(maxId.isValid());
  ZC_EXPECT(maxId.getRaw() == 0xFFFFFFFFFFFFFFFFULL);
}

ZC_TEST("SymbolId_EdgeCases") {
  // Test boundary values
  SymbolId id1 = SymbolId::fromRaw(1);  // Minimum valid
  SymbolId id0 = SymbolId::fromRaw(0);  // Invalid

  ZC_EXPECT(id1.isValid());
  ZC_EXPECT(!id0.isValid());
  ZC_EXPECT(id0 < id1);

  // Test self-comparison
  ZC_EXPECT(id1 == id1);
  ZC_EXPECT(!(id1 != id1));
  ZC_EXPECT(!(id1 < id1));

  // Test hash consistency
  SymbolId::Hash hasher;
  auto hash1 = hasher(id1);
  auto hash2 = hasher(id1);
  ZC_EXPECT(hash1 == hash2);
}

ZC_TEST("SymbolId_Performance") {
  // Test that ID operations are fast
  constexpr int NUM_OPERATIONS = 10000;

  // Create many IDs
  for (int i = 1; i <= NUM_OPERATIONS; ++i) {
    SymbolId id = SymbolId::create(static_cast<uint64_t>(i));
    ZC_EXPECT(id.isValid());
    ZC_EXPECT(id.index() == static_cast<uint64_t>(i));
  }

  // Test comparison performance
  SymbolId id1 = SymbolId::create(1);
  SymbolId id2 = SymbolId::create(2);

  for (int i = 0; i < NUM_OPERATIONS; ++i) {
    bool equal = (id1 == id2);
    bool less = (id1 < id2);
    ZC_EXPECT(!equal);  // They should not be equal
    ZC_EXPECT(less);    // id1 should be less than id2
  }
}

ZC_TEST("SymbolId_ContainerUsage") {
  // Test that SymbolId can be used in containers
  zc::Array<SymbolId> ids = zc::heapArray<SymbolId>(5);

  for (int i = 0; i < 5; ++i) { ids[i] = SymbolId::create(static_cast<uint64_t>(i + 1)); }

  // Verify all IDs are different and valid
  for (int i = 0; i < 5; ++i) {
    ZC_EXPECT(ids[i].isValid());
    ZC_EXPECT(ids[i].index() == static_cast<uint64_t>(i + 1));

    for (int j = i + 1; j < 5; ++j) {
      ZC_EXPECT(ids[i] != ids[j]);
      ZC_EXPECT(ids[i] < ids[j]);  // Should be ordered
    }
  }
}

}  // namespace symbol
}  // namespace compiler
}  // namespace zomlang