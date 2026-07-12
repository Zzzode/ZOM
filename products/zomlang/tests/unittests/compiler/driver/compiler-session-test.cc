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

#include "zomlang/compiler/driver/compiler-session.h"

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

class UnexpectedBuildScriptPlanExecutor final : public package::BuildScriptPlanExecutor {
public:
  package::BuildScriptExecutionResult execute(
      const package::BuildScriptPlanNode&, const VerifiedPreparatoryCrateGraph&,
      zc::ArrayPtr<const package::VerifiedBuildScriptResult>) override {
    return package::BuildScriptIssue::ExecutionFailed;
  }
};

bool containsDiagnosticId(const CapturingDiagnosticConsumer& consumer, diagnostics::DiagID id) {
  for (auto emitted : consumer.ids) {
    if (emitted == id) return true;
  }
  return false;
}

zc::String writeTempZomFile(zc::StringPtr source) {
  zc::String path = zc::str("/tmp/zom-session-test.XXXXXX.zom");
  int fd = mkstemps(path.begin(), 4);
  ZC_IREQUIRE(fd >= 0, "mkstemps failed for compiler session test");
  const char* data = source.cStr();
  size_t remaining = source.size();
  while (remaining > 0) {
    ssize_t written = write(fd, data, remaining);
    ZC_IREQUIRE(written >= 0, "write failed for compiler session test source");
    remaining -= static_cast<size_t>(written);
    data += written;
  }
  close(fd);
  return path;
}

zc::Own<CompilerSession> makeSession(const basic::LangOptions& langOpts,
                                     const basic::CompilerOptions& compilerOpts) {
  identity::SemanticContextFactory contextFactory;
  return zc::heap<CompilerSession>(contextFactory, langOpts, compilerOpts);
}

}  // namespace

ZC_TEST("CompilerSessionTest.BasicInitialization") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  auto session = makeSession(langOpts, compilerOpts);
  ZC_EXPECT(session.get() != nullptr);
}

ZC_TEST("CompilerSessionTest.RejectsBuildPlanBeforePackageHandoff") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  auto session = makeSession(langOpts, compilerOpts);
  UnexpectedBuildScriptPlanExecutor executor;
  ZC_EXPECT(session->executeBuildScripts(executor) ==
            package::BuildScriptIssue::BuildResultIntegrityViolation);
  ZC_EXPECT(session->getBuildScriptPlan() == zc::none);
  ZC_EXPECT(session->getBuildScriptResults() == zc::none);
}

ZC_TEST("CompilerSessionTest.OwnsDistinctContextRegistriesAndSemanticTypeStore") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  identity::SemanticContextFactory contextFactory;
  auto first = zc::heap<CompilerSession>(contextFactory, langOpts, compilerOpts);
  auto second = zc::heap<CompilerSession>(contextFactory, langOpts, compilerOpts);
  ZC_EXPECT(first->getSemanticContextBrand().isValid());
  ZC_EXPECT(second->getSemanticContextBrand().isValid());
  ZC_EXPECT(first->getSemanticContextBrand() != second->getSemanticContextBrand());
  auto firstRegistries = first->getIdentityRegistries();
  auto secondRegistries = second->getIdentityRegistries();
  ZC_EXPECT(firstRegistries != zc::none);
  ZC_EXPECT(secondRegistries != zc::none);
  auto firstTypeStore = first->getSemanticTypeStore();
  auto secondTypeStore = second->getSemanticTypeStore();
  ZC_EXPECT(firstTypeStore != zc::none);
  ZC_EXPECT(secondTypeStore != zc::none);
  ZC_IF_SOME(firstRegistrySet, firstRegistries) {
    ZC_IF_SOME(secondRegistrySet, secondRegistries) {
      ZC_EXPECT(&firstRegistrySet != &secondRegistrySet);
    }
  }
  ZC_IF_SOME(firstStore, firstTypeStore) {
    ZC_IF_SOME(secondStore, secondTypeStore) {
      ZC_EXPECT(&firstStore != &secondStore);
      ZC_EXPECT(firstStore.context() == first->getSemanticContextBrand());
      ZC_EXPECT(secondStore.context() == second->getSemanticContextBrand());
    }
  }
}

ZC_TEST("CompilerSessionTest.BrandExhaustionUsesRegisteredDiagnostic") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  identity::SemanticContextFactory contextFactory(identity::SemanticContextIssueBudget{0, 1});
  auto session = zc::heap<CompilerSession>(contextFactory, langOpts, compilerOpts);
  ZC_EXPECT(!session->getSemanticContextBrand().isValid());
  ZC_EXPECT(session->getIdentityRegistries() == zc::none);
  ZC_EXPECT(session->getSemanticTypeStore() == zc::none);
  ZC_EXPECT(session->getDiagnosticEngine().hasErrors());
  ZC_EXPECT(!session->parseSources());
}

ZC_TEST("CompilerSessionTest.CompilerOptionsAccess") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  compilerOpts.emission.syntaxOnly = true;
  auto session = makeSession(langOpts, compilerOpts);

  auto& opts = session->getCompilerOptions();
  ZC_EXPECT(opts.emission.syntaxOnly);
}

ZC_TEST("CompilerSessionTest.GetDiagnosticEngine") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  auto session = makeSession(langOpts, compilerOpts);

  const auto& diagnosticEngine = session->getDiagnosticEngine();
  ZC_EXPECT(&diagnosticEngine != nullptr);
}

ZC_TEST("CompilerSessionTest.GetASTsEmpty") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  auto session = makeSession(langOpts, compilerOpts);

  const auto& asts = session->getASTs();
  ZC_EXPECT(asts.size() == 0);
}

ZC_TEST("CompilerSessionTest.GetBindingMetadataEmpty") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  auto session = makeSession(langOpts, compilerOpts);

  const auto& metadata = session->getBindingMetadata();
  ZC_EXPECT(metadata.size() == 0);
}

ZC_TEST("CompilerSessionTest.ParseSourcesStoresDefinitionInventory") {
  auto source = writeTempZomFile("let session_value: i32 = 42;"_zc);
  ZC_DEFER(unlink(source.cStr()));

  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  auto session = makeSession(langOpts, compilerOpts);

  auto bufferId = session->addSourceFile(source);
  ZC_EXPECT(bufferId != zc::none);
  ZC_EXPECT(session->parseSources());

  ZC_EXPECT(session->getDefinitionInventoryCount() == 1);
  ZC_IF_SOME(id, bufferId) {
    auto inventory = session->getDefinitionInventory(id);
    ZC_EXPECT(inventory != zc::none);
    ZC_IF_SOME(value, inventory) {
      ZC_EXPECT(value.modules().size() == 0);
      ZC_EXPECT(value.definitions().size() == 1);
      ZC_EXPECT(value.definitions()[0].kind == identity::DefinitionKind::Static);
      ZC_EXPECT(value.impls().size() == 0);
    }
  }
}

ZC_TEST("CompilerSessionTest.GetSourceManager") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  auto session = makeSession(langOpts, compilerOpts);

  const auto& sourceManager = session->getSourceManager();
  ZC_EXPECT(&sourceManager != nullptr);
}

ZC_TEST("CompilerSessionTest.ParseSourcesEmpty") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  auto session = makeSession(langOpts, compilerOpts);

  bool result = session->parseSources();
  ZC_EXPECT(result);
}

ZC_TEST("CompilerSessionTest.AddSourceFileEmpty") {
  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  auto session = makeSession(langOpts, compilerOpts);

  auto result = session->addSourceFile(zc::str(""));
  ZC_EXPECT(result == zc::none);
}

ZC_TEST("CompilerSessionTest.CheckSourcesStoresTypeEnv") {
  auto source = writeTempZomFile("let x: i32 = 42;"_zc);
  ZC_DEFER(unlink(source.cStr()));

  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  auto session = makeSession(langOpts, compilerOpts);

  auto bufferId = session->addSourceFile(source);
  ZC_EXPECT(bufferId != zc::none);
  ZC_EXPECT(session->parseSources());
  ZC_EXPECT(session->bindSources());
  ZC_EXPECT(session->checkSources());
  ZC_EXPECT(!session->getDiagnosticEngine().hasErrors());

  const auto& typeEnvs = session->getTypeEnvs();
  ZC_EXPECT(typeEnvs.size() == 1);
  ZC_IF_SOME(id, bufferId) {
    auto env = typeEnvs.find(id);
    ZC_EXPECT(env != zc::none);
    ZC_IF_SOME(typeEnv, env) { ZC_EXPECT(typeEnv.nodeTypeCount() > 0); }
  }
}

ZC_TEST("CompilerSessionTest.CheckSourcesReportsUnreachableMatchArmWarning") {
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
  auto session = makeSession(langOpts, compilerOpts);
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  session->getDiagnosticEngine().addConsumer(zc::mv(consumer));

  auto bufferId = session->addSourceFile(source);
  ZC_EXPECT(bufferId != zc::none);
  ZC_EXPECT(session->parseSources());
  ZC_EXPECT(session->bindSources());
  ZC_EXPECT(session->checkSources());
  ZC_EXPECT(!session->getDiagnosticEngine().hasErrors());
  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::CheckerUnreachableMatchArm));
}

ZC_TEST("CompilerSessionTest.CheckSourcesRejectsTypeError") {
  auto source = writeTempZomFile("let x: i32 = \"bad\";"_zc);
  ZC_DEFER(unlink(source.cStr()));

  auto langOpts = basic::LangOptions();
  auto compilerOpts = basic::CompilerOptions();
  auto session = makeSession(langOpts, compilerOpts);

  auto bufferId = session->addSourceFile(source);
  ZC_EXPECT(bufferId != zc::none);
  ZC_EXPECT(session->parseSources());
  ZC_EXPECT(session->bindSources());
  ZC_EXPECT(!session->checkSources());
  ZC_EXPECT(session->getDiagnosticEngine().hasErrors());
}

}  // namespace driver
}  // namespace compiler
}  // namespace zomlang
