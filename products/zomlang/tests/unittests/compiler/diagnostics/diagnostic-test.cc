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

#include "zc/core/io.h"
#include "zc/core/string.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/diagnostics/consoling-diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {
namespace diagnostics {

ZC_TEST("DiagnosticTest.BasicDiagnosticReporting") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  auto consumer = zc::heap<ConsolingDiagnosticConsumer>();
  diagnosticEngine->addConsumer(zc::mv(consumer));

  auto bufferId = sourceManager->addMemBufferCopy(zc::StringPtr("let x = ;").asBytes(), "test.zom");
  auto loc = sourceManager->getLocFromExternalSource("test.zom", 1, 1);
  
  diagnosticEngine->diagnose<DiagID::InvalidChar>(loc, zc::str("test"));
  ZC_EXPECT(diagnosticEngine->hasErrors());
}

ZC_TEST("DiagnosticTest.MultipleDiagnostics") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  auto consumer = zc::heap<ConsolingDiagnosticConsumer>();
  diagnosticEngine->addConsumer(zc::mv(consumer));

  auto bufferId = sourceManager->addMemBufferCopy(zc::StringPtr("invalid code").asBytes(), "test.zom");
  auto loc = sourceManager->getLocFromExternalSource("test.zom", 1, 1);
  
  diagnosticEngine->diagnose<DiagID::InvalidChar>(loc, zc::str("@"));
  diagnosticEngine->diagnose<DiagID::UnterminatedString>(loc);
  ZC_EXPECT(diagnosticEngine->hasErrors());
}

ZC_TEST("DiagnosticTest.DiagnosticConsumer") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  auto consumer = zc::heap<ConsolingDiagnosticConsumer>();
  diagnosticEngine->addConsumer(zc::mv(consumer));

  auto bufferId = sourceManager->addMemBufferCopy(zc::StringPtr("code with error").asBytes(), "test.zom");
  auto loc = sourceManager->getLocFromExternalSource("test.zom", 1, 1);
  
  diagnosticEngine->diagnose<DiagID::InvalidChar>(loc, zc::str("i32"));
  ZC_EXPECT(diagnosticEngine->hasErrors());
}

ZC_TEST("DiagnosticTest.SourceLocationReporting") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  auto consumer = zc::heap<ConsolingDiagnosticConsumer>();
  diagnosticEngine->addConsumer(zc::mv(consumer));

  auto bufferId = sourceManager->addMemBufferCopy(zc::StringPtr("line1\nline2\nline3").asBytes(), "test.zom");
  auto loc = sourceManager->getLocFromExternalSource("test.zom", 1, 1);
  
  diagnosticEngine->diagnose<DiagID::InvalidChar>(loc, zc::str("test"));
  ZC_EXPECT(diagnosticEngine->hasErrors());
}

ZC_TEST("DiagnosticTest.NoErrors") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  auto consumer = zc::heap<ConsolingDiagnosticConsumer>();
  diagnosticEngine->addConsumer(zc::mv(consumer));

  ZC_EXPECT(!diagnosticEngine->hasErrors());
}

}  // namespace diagnostics
}  // namespace compiler
}  // namespace zomlang