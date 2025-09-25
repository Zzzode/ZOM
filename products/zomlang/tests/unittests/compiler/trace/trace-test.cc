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

ZC_TEST("TraceTest_RecursionDepthLimit") {
  TraceConfig config;
  config.enabled = true;
  config.categoryMask = TraceCategory::kAll;
  config.maxRecursionDepth = 3;  // Set very low limit for testing
  TraceManager::getInstance().configure(config);

  // Test normal depth tracking
  {
    ScopeTracer tracer1(TraceCategory::kParser, "level1");
    ZC_EXPECT(TraceManager::getInstance().getCurrentDepth() == 1);

    {
      ScopeTracer tracer2(TraceCategory::kParser, "level2");
      ZC_EXPECT(TraceManager::getInstance().getCurrentDepth() == 2);

      {
        ScopeTracer tracer3(TraceCategory::kParser, "level3");
        ZC_EXPECT(TraceManager::getInstance().getCurrentDepth() == 3);
      }
      ZC_EXPECT(TraceManager::getInstance().getCurrentDepth() == 2);
    }
    ZC_EXPECT(TraceManager::getInstance().getCurrentDepth() == 1);
  }
  ZC_EXPECT(TraceManager::getInstance().getCurrentDepth() == 0);

  TraceManager::getInstance().clear();
}

ZC_TEST("TraceTest_RecursionDepthBoundary") {
  TraceConfig config;
  config.enabled = true;
  config.categoryMask = TraceCategory::kAll;
  config.maxRecursionDepth = 2;  // Set very low limit for testing
  TraceManager::getInstance().configure(config);

  // Verify the configuration was applied
  ZC_EXPECT(TraceManager::getInstance().getMaxRecursionDepth() == 2);

  // Test that we can reach exactly the maximum depth
  {
    ScopeTracer tracer1(TraceCategory::kParser, "level1");
    ZC_EXPECT(TraceManager::getInstance().getCurrentDepth() == 1);

    {
      ScopeTracer tracer2(TraceCategory::kParser, "level2");
      ZC_EXPECT(TraceManager::getInstance().getCurrentDepth() == 2);
      // At this point we're at the maximum allowed depth
    }
    ZC_EXPECT(TraceManager::getInstance().getCurrentDepth() == 1);
  }
  ZC_EXPECT(TraceManager::getInstance().getCurrentDepth() == 0);

  // Note: We cannot test exceeding the limit because ZC_FAIL_REQUIRE
  // calls abort() rather than throwing an exception, and ScopeTracer
  // constructor is noexcept. The depth protection will terminate the
  // program if exceeded.

  TraceManager::getInstance().clear();
}

ZC_TEST("TraceTest_RecursionDepthConfiguration") {
  // Test recursion depth configuration and tracking
  TraceConfig config;
  config.enabled = true;
  config.categoryMask = TraceCategory::kAll;

  // Test different max recursion depth values
  config.maxRecursionDepth = 1000;
  TraceManager::getInstance().configure(config);
  ZC_EXPECT(TraceManager::getInstance().getMaxRecursionDepth() == 1000);

  // Change configuration
  config.maxRecursionDepth = 50;
  TraceManager::getInstance().configure(config);
  ZC_EXPECT(TraceManager::getInstance().getMaxRecursionDepth() == 50);

  // Test depth tracking
  {
    ScopeTracer tracer(TraceCategory::kParser, "test");
    ZC_EXPECT(TraceManager::getInstance().getCurrentDepth() == 1);
  }
  ZC_EXPECT(TraceManager::getInstance().getCurrentDepth() == 0);

  TraceManager::getInstance().clear();
}

ZC_TEST("TraceTest_FileOutput") {
  TraceConfig config;
  config.enabled = true;
  config.categoryMask = TraceCategory::kAll;
  config.outputFile = "/tmp/zom_trace_test.json";
  TraceManager::getInstance().configure(config);

  // Add some test events
  traceEvent(TraceCategory::kLexer, "tokenize", "test.zom");
  traceEvent(TraceCategory::kParser, "parse_expression", "binary_op");
  traceCounter(TraceCategory::kMemory, "heap_usage", "1024");

  // Test scope tracer
  {
    ScopeTracer tracer(TraceCategory::kChecker, "type_check", "function_def");
    traceEvent(TraceCategory::kDiagnostics, "warning", "unused_variable");
  }

  // Flush to file
  TraceManager::getInstance().flush();

  // Verify file was created by checking if we can configure with the same file again
  // (This is a basic test - in a real scenario we'd read and validate the JSON)
  TraceConfig verifyConfig;
  verifyConfig.enabled = true;
  verifyConfig.outputFile = "/tmp/zom_trace_test.json";
  TraceManager::getInstance().configure(verifyConfig);

  TraceManager::getInstance().clear();
}

ZC_TEST("TraceTest_JsonStringEscaping") {
  TraceConfig config;
  config.enabled = true;
  config.categoryMask = TraceCategory::kAll;
  config.outputFile = "/tmp/zom_trace_escape_test.json";
  TraceManager::getInstance().configure(config);

  // Test various characters that need escaping in JSON
  traceEvent(TraceCategory::kLexer, "test\"quote", "details with \"quotes\"");
  traceEvent(TraceCategory::kParser, "test\\backslash", "path\\to\\file");
  traceEvent(TraceCategory::kChecker, "test\nnewline", "line1\nline2");
  traceEvent(TraceCategory::kDriver, "test\rcarriage", "text\rwith\rcarriage");
  traceEvent(TraceCategory::kDiagnostics, "test\ttab", "column1\tcolumn2");
  traceEvent(TraceCategory::kMemory, "mixed\"chars\\test", "quote\"slash\\newline\ntab\t");

  // Verify events were added
  ZC_EXPECT(TraceManager::getInstance().getEventCount() == 6);

  // Flush to file to trigger JSON generation
  TraceManager::getInstance().flush();

  // Events should still be present after flush (flush doesn't clear events)
  ZC_EXPECT(TraceManager::getInstance().getEventCount() == 6);

  TraceManager::getInstance().clear();
}

ZC_TEST("TraceTest_AllTraceCategories") {
  TraceConfig config;
  config.enabled = true;
  config.categoryMask = TraceCategory::kAll;
  config.outputFile = "/tmp/zom_trace_categories_test.json";
  TraceManager::getInstance().configure(config);

  // Test all trace categories to ensure enum mapping works correctly
  traceEvent(TraceCategory::kLexer, "lexer_test");
  traceEvent(TraceCategory::kParser, "parser_test");
  traceEvent(TraceCategory::kChecker, "checker_test");
  traceEvent(TraceCategory::kDriver, "driver_test");
  traceEvent(TraceCategory::kDiagnostics, "diagnostics_test");
  traceEvent(TraceCategory::kMemory, "memory_test");
  traceEvent(TraceCategory::kPerformance, "performance_test");

  // Verify all events were added
  ZC_EXPECT(TraceManager::getInstance().getEventCount() == 7);

  // Flush to file to trigger JSON generation with all categories
  TraceManager::getInstance().flush();

  TraceManager::getInstance().clear();
}

ZC_TEST("TraceTest_AllTraceEventTypes") {
  TraceConfig config;
  config.enabled = true;
  config.categoryMask = TraceCategory::kAll;
  config.outputFile = "/tmp/zom_trace_event_types_test.json";
  TraceManager::getInstance().configure(config);

  // Test different event types
  traceEvent(TraceCategory::kLexer, "instant_event");            // This creates kInstant events
  traceCounter(TraceCategory::kMemory, "counter_event", "100");  // This creates kCounter events

  // Test scope tracer which creates kEnter and kExit events
  {
    ScopeTracer tracer(TraceCategory::kParser, "scope_event");
    // The tracer will create kEnter on construction and kExit on destruction
  }

  // Verify events were created (instant + counter + enter + exit = 4 events)
  ZC_EXPECT(TraceManager::getInstance().getEventCount() >= 4);

  // Flush to file to trigger JSON generation with all event types
  TraceManager::getInstance().flush();

  TraceManager::getInstance().clear();
}

ZC_TEST("TraceTest_CategoryFiltering_Individual") {
  // Test individual category filtering to ensure enum values work correctly
  TraceConfig config;
  config.enabled = true;

  // Test each category individually
  config.categoryMask = TraceCategory::kLexer;
  TraceManager::getInstance().configure(config);
  ZC_EXPECT(TraceManager::getInstance().isEnabled(TraceCategory::kLexer));
  ZC_EXPECT(!TraceManager::getInstance().isEnabled(TraceCategory::kParser));

  config.categoryMask = TraceCategory::kParser;
  TraceManager::getInstance().configure(config);
  ZC_EXPECT(TraceManager::getInstance().isEnabled(TraceCategory::kParser));
  ZC_EXPECT(!TraceManager::getInstance().isEnabled(TraceCategory::kLexer));

  config.categoryMask = TraceCategory::kChecker;
  TraceManager::getInstance().configure(config);
  ZC_EXPECT(TraceManager::getInstance().isEnabled(TraceCategory::kChecker));
  ZC_EXPECT(!TraceManager::getInstance().isEnabled(TraceCategory::kParser));

  config.categoryMask = TraceCategory::kDriver;
  TraceManager::getInstance().configure(config);
  ZC_EXPECT(TraceManager::getInstance().isEnabled(TraceCategory::kDriver));
  ZC_EXPECT(!TraceManager::getInstance().isEnabled(TraceCategory::kChecker));

  config.categoryMask = TraceCategory::kDiagnostics;
  TraceManager::getInstance().configure(config);
  ZC_EXPECT(TraceManager::getInstance().isEnabled(TraceCategory::kDiagnostics));
  ZC_EXPECT(!TraceManager::getInstance().isEnabled(TraceCategory::kDriver));

  config.categoryMask = TraceCategory::kMemory;
  TraceManager::getInstance().configure(config);
  ZC_EXPECT(TraceManager::getInstance().isEnabled(TraceCategory::kMemory));
  ZC_EXPECT(!TraceManager::getInstance().isEnabled(TraceCategory::kDiagnostics));

  config.categoryMask = TraceCategory::kPerformance;
  TraceManager::getInstance().configure(config);
  ZC_EXPECT(TraceManager::getInstance().isEnabled(TraceCategory::kPerformance));
  ZC_EXPECT(!TraceManager::getInstance().isEnabled(TraceCategory::kMemory));

  TraceManager::getInstance().clear();
}

ZC_TEST("TraceTest_EventTypeVariations") {
  TraceConfig config;
  config.enabled = true;
  config.categoryMask = TraceCategory::kAll;
  TraceManager::getInstance().configure(config);

  // Test instant events (default from traceEvent)
  traceEvent(TraceCategory::kLexer, "instant_test");
  size_t countAfterInstant = TraceManager::getInstance().getEventCount();
  ZC_EXPECT(countAfterInstant == 1);

  // Test counter events
  traceCounter(TraceCategory::kMemory, "counter_test", "42");
  size_t countAfterCounter = TraceManager::getInstance().getEventCount();
  ZC_EXPECT(countAfterCounter == 2);

  // Test scope events (enter + exit)
  size_t countBeforeScope = TraceManager::getInstance().getEventCount();
  {
    ScopeTracer tracer(TraceCategory::kParser, "scope_test");
    // Should have added enter event
    ZC_EXPECT(TraceManager::getInstance().getEventCount() == countBeforeScope + 1);
  }
  // Should have added exit event
  ZC_EXPECT(TraceManager::getInstance().getEventCount() == countBeforeScope + 2);

  TraceManager::getInstance().clear();
}

}  // namespace trace
}  // namespace compiler
}  // namespace zomlang
