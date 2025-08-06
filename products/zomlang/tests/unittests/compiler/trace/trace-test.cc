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

#include "zomlang/compiler/trace/trace.h"

#include "zc/core/string.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/trace/trace-config.h"

namespace zomlang {
namespace compiler {
namespace trace {

ZC_TEST("TraceTest_BasicTraceEvent") {
  TraceConfig config;
  config.enabled = true;
  config.categoryMask = TraceCategory::kAll;
  TraceManager::getInstance().configure(config);

  ZC_EXPECT(TraceManager::getInstance().isEnabled(TraceCategory::kParser));
  traceEvent(TraceCategory::kParser, "test event");
  ZC_EXPECT(TraceManager::getInstance().getEventCount() > 0);

  TraceManager::getInstance().clear();
}

ZC_TEST("TraceTest_TraceCounter") {
  TraceConfig config;
  config.enabled = true;
  config.categoryMask = TraceCategory::kPerformance;
  TraceManager::getInstance().configure(config);

  traceCounter(TraceCategory::kPerformance, "test_counter", "42");
  ZC_EXPECT(TraceManager::getInstance().getEventCount() > 0);

  TraceManager::getInstance().clear();
}

ZC_TEST("TraceTest_ScopeTracer") {
  TraceConfig config;
  config.enabled = true;
  config.categoryMask = TraceCategory::kLexer;
  TraceManager::getInstance().configure(config);

  { ScopeTracer tracer(TraceCategory::kLexer, "test scope"); }
  ZC_EXPECT(TraceManager::getInstance().getEventCount() >= 2);

  TraceManager::getInstance().clear();
}

ZC_TEST("TraceTest_CategoryFiltering") {
  TraceConfig config;
  config.enabled = true;
  config.categoryMask = TraceCategory::kParser;
  TraceManager::getInstance().configure(config);

  ZC_EXPECT(TraceManager::getInstance().isEnabled(TraceCategory::kParser));
  ZC_EXPECT(!TraceManager::getInstance().isEnabled(TraceCategory::kLexer));

  TraceManager::getInstance().clear();
}

ZC_TEST("TraceTest_TraceFlush") {
  TraceConfig config;
  config.enabled = true;
  config.categoryMask = TraceCategory::kDriver;
  TraceManager::getInstance().configure(config);

  traceEvent(TraceCategory::kDriver, "flush test");
  TraceManager::getInstance().flush();
  TraceManager::getInstance().clear();
}

}  // namespace trace
}  // namespace compiler
}  // namespace zomlang