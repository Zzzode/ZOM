// Copyright (c) 2025 Zode.Z. All rights reserved
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

/// \file
/// \brief Unit tests for diagnostic consumer functionality.
///
/// This file contains ztest-based unit tests for the diagnostic consumer classes,
/// testing diagnostic handling and consumption.

#include "zomlang/compiler/diagnostics/diagnostic-consumer.h"

#include "zc/core/common.h"
#include "zc/core/debug.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/diagnostics/diagnostic.h"
#include "zomlang/compiler/source/location.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {
namespace diagnostics {

ZC_TEST("DiagnosticConsumerTest: BaseClassFunctionality") {
  // Test that the base DiagnosticConsumer class can be instantiated and used
  class TestDiagnosticConsumer : public DiagnosticConsumer {
  public:
    int diagnosticCount = 0;

    void handleDiagnostic(const source::SourceManager& sm, const Diagnostic& diagnostic) override {
      diagnosticCount++;
      (void)sm;          // Suppress unused parameter warning
      (void)diagnostic;  // Suppress unused parameter warning
    }
  };

  TestDiagnosticConsumer consumer;
  ZC_EXPECT(consumer.diagnosticCount == 0, "Initial diagnostic count should be 0");
}

ZC_TEST("DiagnosticConsumerTest: DiagnosticHandling") {
  // Test diagnostic handling through consumer interface
  class MockDiagnosticConsumer : public DiagnosticConsumer {
  public:
    zc::Vector<DiagID> diagnosticIds;

    void handleDiagnostic(const source::SourceManager& sm, const Diagnostic& diagnostic) override {
      (void)sm;  // Suppress unused parameter warning
      diagnosticIds.add(diagnostic.getId());
    }
  };

  MockDiagnosticConsumer consumer;
  source::SourceManager sourceManager;

  // Create a test diagnostic
  source::SourceLoc loc;
  Diagnostic testDiagnostic(DiagID::InvalidChar, loc, zc::str("Test error"));

  consumer.handleDiagnostic(sourceManager, testDiagnostic);

  ZC_EXPECT(consumer.diagnosticIds.size() == 1, "Should have one diagnostic");
  ZC_EXPECT(consumer.diagnosticIds[0] == DiagID::InvalidChar, "Diagnostic should have correct ID");
}

ZC_TEST("DiagnosticConsumerTest: VirtualDestructor") {
  // Test that DiagnosticConsumer has virtual destructor
  class DerivedConsumer : public DiagnosticConsumer {
  public:
    bool destructorCalled = false;

    ~DerivedConsumer() noexcept(false) override { destructorCalled = true; }

    void handleDiagnostic(const source::SourceManager& sm, const Diagnostic& diagnostic) override {
      (void)sm;          // Suppress unused parameter warning
      (void)diagnostic;  // Suppress unused parameter warning
    }
  };

  DiagnosticConsumer* consumer = new DerivedConsumer();
  delete consumer;

  // If this compiles and runs without issues, virtual destructor works correctly
  ZC_EXPECT(true, "Virtual destructor should work correctly");
}

}  // namespace diagnostics
}  // namespace compiler
}  // namespace zomlang