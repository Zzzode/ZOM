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

#include <errno.h>
#include <unistd.h>

#include "zc/core/common.h"
#include "zc/core/filesystem.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/basic/compiler-opts.h"
#include "zomlang/compiler/diagnostics/diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/compiler/type/type-env.h"

namespace zomlang {
namespace compiler {
namespace driver {

namespace {

class CapturingDiagnosticConsumer final : public diagnostics::DiagnosticConsumer {
public:
  zc::Vector<diagnostics::DiagID> ids;

  void handleDiagnostic(const source::SourceManager&,
                        const diagnostics::Diagnostic& diagnostic) override {
    ids.add(diagnostic.getId());
  }
};

bool containsDiagnosticId(const CapturingDiagnosticConsumer& consumer, diagnostics::DiagID id) {
  for (auto emitted : consumer.ids) {
    if (emitted == id) return true;
  }
  return false;
}

zc::String writeTempZomFile(zc::StringPtr source) {
  zc::String path = zc::str("/tmp/zom-driver-test.XXXXXX.zom");
  int fd = mkstemps(path.begin(), 4);
  ZC_IREQUIRE(fd >= 0, "mkstemps failed for driver test");
  const char* data = source.cStr();
  size_t remaining = source.size();
  while (remaining > 0) {
    ssize_t written = write(fd, data, remaining);
    ZC_IREQUIRE(written >= 0, "write failed for driver test source");
    remaining -= static_cast<size_t>(written);
    data += written;
  }
  close(fd);
  return path;
}

}  // namespace

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

ZC_TEST("DriverTest.GetBindingMetadataEmpty") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  auto driver = zc::heap<CompilerDriver>(langOpts, compilerOpts);

  const auto& metadata = driver->getBindingMetadata();
  ZC_EXPECT(metadata.size() == 0);
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

ZC_TEST("DriverTest.CheckSourcesStoresTypeEnv") {
  auto source = writeTempZomFile("let x: i32 = 42;"_zc);
  ZC_DEFER(unlink(source.cStr()));

  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  auto driver = zc::heap<CompilerDriver>(langOpts, compilerOpts);

  auto bufferId = driver->addSourceFile(source);
  ZC_EXPECT(bufferId != zc::none);
  ZC_EXPECT(driver->parseSources());
  ZC_EXPECT(driver->bindSources());
  ZC_EXPECT(driver->checkSources());
  ZC_EXPECT(!driver->getDiagnosticEngine().hasErrors());

  const auto& typeEnvs = driver->getTypeEnvs();
  ZC_EXPECT(typeEnvs.size() == 1);
  ZC_IF_SOME(id, bufferId) {
    auto env = typeEnvs.find(id);
    ZC_EXPECT(env != zc::none);
    ZC_IF_SOME(typeEnv, env) { ZC_EXPECT(typeEnv.nodeTypeCount() > 0); }
  }
}

ZC_TEST("DriverTest.CheckSourcesReportsUnreachableMatchArmWarning") {
  auto source = writeTempZomFile(
      "fun f(flag: bool) -> unit {\n"
      "    match (flag) {\n"
      "        default => { }\n"
      "        when true => { }\n"
      "    }\n"
      "}\n"_zc);
  ZC_DEFER(unlink(source.cStr()));

  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  auto driver = zc::heap<CompilerDriver>(langOpts, compilerOpts);
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  driver->getDiagnosticEngine().addConsumer(zc::mv(consumer));

  auto bufferId = driver->addSourceFile(source);
  ZC_EXPECT(bufferId != zc::none);
  ZC_EXPECT(driver->parseSources());
  ZC_EXPECT(driver->bindSources());
  ZC_EXPECT(driver->checkSources());
  ZC_EXPECT(!driver->getDiagnosticEngine().hasErrors());
  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::CheckerUnreachableMatchArm));
}

ZC_TEST("DriverTest.CheckSourcesRejectsTypeError") {
  auto source = writeTempZomFile("let x: i32 = \"bad\";"_zc);
  ZC_DEFER(unlink(source.cStr()));

  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  auto driver = zc::heap<CompilerDriver>(langOpts, compilerOpts);

  auto bufferId = driver->addSourceFile(source);
  ZC_EXPECT(bufferId != zc::none);
  ZC_EXPECT(driver->parseSources());
  ZC_EXPECT(driver->bindSources());
  ZC_EXPECT(!driver->checkSources());
  ZC_EXPECT(driver->getDiagnosticEngine().hasErrors());
}

}  // namespace driver
}  // namespace compiler
}  // namespace zomlang
