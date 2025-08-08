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

#include "zomlang/compiler/driver/driver.h"

#include "zc/core/filesystem.h"
#include "zc/core/string.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/basic/compiler-opts.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {
namespace driver {

ZC_TEST("DriverTest.BasicInitialization") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  auto driver = zc::heap<CompilerDriver>(langOpts, compilerOpts);
  ZC_EXPECT(driver.get() != nullptr);
}

ZC_TEST("DriverTest.CompilerOptionsAccess") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  compilerOpts.emission.syntaxOnly = true;
  auto driver = zc::heap<CompilerDriver>(langOpts, compilerOpts);

  auto& opts = driver->getCompilerOptions();
  ZC_EXPECT(opts.emission.syntaxOnly);
}

ZC_TEST("DriverTest.GetDiagnosticEngine") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  auto driver = zc::heap<CompilerDriver>(langOpts, compilerOpts);

  const auto& diagnosticEngine = driver->getDiagnosticEngine();
  ZC_EXPECT(&diagnosticEngine != nullptr);
}

ZC_TEST("DriverTest.GetASTsEmpty") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  auto driver = zc::heap<CompilerDriver>(langOpts, compilerOpts);

  const auto& asts = driver->getASTs();
  ZC_EXPECT(asts.size() == 0);
}

ZC_TEST("DriverTest.GetSourceManager") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  auto driver = zc::heap<CompilerDriver>(langOpts, compilerOpts);

  const auto& sourceManager = driver->getSourceManager();
  ZC_EXPECT(&sourceManager != nullptr);
}

ZC_TEST("DriverTest.ParseSourcesEmpty") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  auto driver = zc::heap<CompilerDriver>(langOpts, compilerOpts);

  bool result = driver->parseSources();
  ZC_EXPECT(result);
}

ZC_TEST("DriverTest.AddSourceFileEmpty") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  auto driver = zc::heap<CompilerDriver>(langOpts, compilerOpts);

  auto result = driver->addSourceFile(zc::str(""));
  ZC_EXPECT(result == zc::none);
}

}  // namespace driver
}  // namespace compiler
}  // namespace zomlang