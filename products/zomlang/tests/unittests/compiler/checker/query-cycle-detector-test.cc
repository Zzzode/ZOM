// Copyright (c) 2025 Zode.Z. All rights reserved
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
// See the License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/checker/query-cycle-detector.h"

#include "zc/ztest/test.h"

namespace zomlang {
namespace compiler {
namespace checker {

ZC_TEST("QueryCycleDetector.EnterAndLeave") {
  QueryCycleDetector detector;
  {
    auto guard = detector.enter(QueryKey::signatureOf(1));
    ZC_EXPECT(!guard.hasCycle());
    ZC_EXPECT(detector.depth() == 1);
  }
  ZC_EXPECT(detector.depth() == 0);
}

ZC_TEST("QueryCycleDetector.DetectsSameSignatureCycle") {
  QueryCycleDetector detector;
  auto outer = detector.enter(QueryKey::signatureOf(1));
  auto inner = detector.enter(QueryKey::signatureOf(1));

  ZC_EXPECT(!outer.hasCycle());
  ZC_EXPECT(inner.hasCycle());
  ZC_EXPECT(detector.depth() == 1);
}

ZC_TEST("QueryCycleDetector.AllowsDifferentQueries") {
  QueryCycleDetector detector;
  auto signature = detector.enter(QueryKey::signatureOf(1));
  auto alias = detector.enter(QueryKey::typeAliasOf(1));

  ZC_EXPECT(!signature.hasCycle());
  ZC_EXPECT(!alias.hasCycle());
  ZC_EXPECT(detector.depth() == 2);
}

ZC_TEST("QueryCycleDetector.DetectsAssociatedProjectionCycle") {
  QueryCycleDetector detector;
  auto key = QueryKey::associatedProjection(type::TypeId(7), "Item"_zc);

  auto outer = detector.enter(key);
  auto inner = detector.enter(key);

  ZC_EXPECT(!outer.hasCycle());
  ZC_EXPECT(inner.hasCycle());
}

ZC_TEST("QueryCycleDetector.DetectsMarkerDerivationCycle") {
  QueryCycleDetector detector;
  auto key = QueryKey::markerDerivation(type::TypeId(8), 42);

  auto outer = detector.enter(key);
  auto inner = detector.enter(key);

  ZC_EXPECT(!outer.hasCycle());
  ZC_EXPECT(inner.hasCycle());
}

ZC_TEST("QueryKey.ToStringIncludesKind") {
  auto key = QueryKey::associatedProjection(type::TypeId(7), "Item"_zc);
  auto text = key.toString();

  ZC_EXPECT(text.contains("AssociatedProjection"_zc));
  ZC_EXPECT(text.contains("Item"_zc));
}

}  // namespace checker
}  // namespace compiler
}  // namespace zomlang
