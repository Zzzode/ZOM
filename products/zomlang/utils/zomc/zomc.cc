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

#include "zc/core/main.h"
#include "zc/core/string.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/driver/driver.h"
#include "zomlang/compiler/source/manager.h"

#ifndef VERSION
#define VERSION "(unknown)"
#endif

namespace zomlang {
namespace compiler {
namespace utils {

static constexpr char VERSION_STRING[] = "ZomLang Version " VERSION;

class CompilerMain {
public:
  explicit CompilerMain(zc::ProcessContext& context) : context(context) {
    basic::LangOptions langOpts;
    driver = driverSpace.construct(langOpts);
  }

  zc::MainFunc getMain() {
    return zc::MainBuilder(context, VERSION_STRING, "Command-line tool for Zomlang Compiler.")
        .addSubCommand("compile", ZC_BIND_METHOD(*this, getCompileMain),
                       "Compiles source code in one or more target.")
        .addSubCommand("run", ZC_BIND_METHOD(*this, getRunMain),
                       "Run a zomlang program with project configuration.")
        .build();
  }

  zc::MainFunc getCompileMain() {
    zc::MainBuilder builder(context, VERSION_STRING,
                            "Compiles Zomlang sources and generates one or more targets.");
    addCompileOptions(builder);
    return builder.build();
  }

  ZC_NODISCARD zc::MainFunc getRunMain() const {
    zc::MainBuilder builder(context, VERSION_STRING, "");
    return builder.build();
  }

  void addCompileOptions(zc::MainBuilder& builder) {
    builder
        .addOptionWithArg({'o', "output"}, ZC_BIND_METHOD(*this, addOutput), "<dir>",
                          "Specify the output path.")
        .addOptionWithArg({'e', "emit"}, ZC_BIND_METHOD(*this, setEmitType), "<type>",
                          "Set output type (ast|ir|binary)")
        .addOption({'d', "dump-ast"}, ZC_BIND_METHOD(*this, enableDumpAST),
                   "Dump the Abstract Syntax Tree to stdout.")
        .expectOneOrMoreArgs("<source>", ZC_BIND_METHOD(*this, addSource))
        .callAfterParsing(ZC_BIND_METHOD(*this, emitOutput));
  }

  // =====================================================================================
  // "compile" command

  zc::MainBuilder::Validity addSource(const zc::StringPtr file) {
    if (!file.endsWith(".zom")) { return "Error: zomc: source file must have .zom extension"; }
    if (const zc::Maybe<source::BufferId> bufferId = driver->addSourceFile(file);
        bufferId == zc::none) {
      return zc::str("Failed to load source file.");
    }
    return true;
  }

  zc::MainBuilder::Validity setEmitType(zc::StringPtr emitType) { return true; }

  zc::MainBuilder::Validity addOutput(zc::StringPtr spec) { return true; }

  zc::MainBuilder::Validity enableDumpAST() { return true; }

  zc::MainBuilder::Validity emitOutput() {
    // Trigger the parallel parsing process
    bool success = driver->parseSources();

    if (!success || driver->getDiagnosticEngine().hasErrors()) {
      // Handle parsing errors, maybe return an error message
      return zc::str("Compilation failed due to parsing errors.");
    }

    // Parsing succeeded, proceed with further steps (e.g., type checking, IR gen)
    // if (dumpASTEnabled) { // Check if dump AST flag is set
    //    const auto& asts = driver->getASTs();
    //    // Iterate through asts and dump them
    // }

    // ... rest of the emit logic based on emitType ...

    return true;
  }

private:
  zc::ProcessContext& context;
  zc::Own<driver::CompilerDriver> driver;
  zc::SpaceFor<driver::CompilerDriver> driverSpace;
};

}  // namespace utils
}  // namespace compiler
}  // namespace zomlang

ZC_MAIN(zomlang::compiler::utils::CompilerMain)
