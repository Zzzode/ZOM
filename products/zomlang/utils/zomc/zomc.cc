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

#include <unistd.h>

#include "zc/core/common.h"
#include "zc/core/filesystem.h"
#include "zc/core/io.h"
#include "zc/core/main.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/dump.h"
#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/basic/compiler-opts.h"
#include "zomlang/compiler/basic/io-utils.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
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
    driver = driverSpace.construct(langOpts, compilerOpts);
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
                          "Specify the output directory or file path.")
        .addOptionWithArg({"emit"}, ZC_BIND_METHOD(*this, setEmitType), "<type>",
                          "Set output type: ast, ir, binary (default: binary)")
        .addOptionWithArg({"ast-format"}, ZC_BIND_METHOD(*this, setASTDumpFormat), "<format>",
                          "Set AST dump format: tree, json, raw (default: tree)")
        .addOption({"dump-ast"}, ZC_BIND_METHOD(*this, enableASTDump),
                   "Dump AST to stdout (shorthand for --emit=ast)")
        .addOption({"syntax-only"}, ZC_BIND_METHOD(*this, enableSyntaxOnly),
                   "Only perform syntax checking, no code generation")
        .addOptionWithArg({'O', "optimize"}, ZC_BIND_METHOD(*this, setOptimizationLevel), "<level>",
                          "Set optimization level: 0, 1, 2, 3 (default: 0)")
        .addOption({"no-unicode"}, ZC_BIND_METHOD(*this, disableUnicode),
                   "Disable Unicode support in identifiers")
        .addOption({"allow-dollar-identifiers"}, ZC_BIND_METHOD(*this, enableDollarIdentifiers),
                   "Allow dollar signs in identifiers")
        .addOption({"no-regex-literals"}, ZC_BIND_METHOD(*this, disableRegexLiterals),
                   "Disable regex literal syntax")
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

  zc::MainBuilder::Validity addOutput(zc::StringPtr spec) {
    compilerOpts.emission.outputPath = zc::str(spec);
    return true;
  }

  zc::MainBuilder::Validity setEmitType(zc::StringPtr type) {
    if (type == "ast") {
      compilerOpts.emission.outputType = basic::CompilerOptions::EmissionOptions::OutputType::AST;
    } else if (type == "ir") {
      compilerOpts.emission.outputType = basic::CompilerOptions::EmissionOptions::OutputType::IR;
    } else if (type == "binary") {
      compilerOpts.emission.outputType =
          basic::CompilerOptions::EmissionOptions::OutputType::Binary;
    } else {
      return zc::str("Invalid output type: ", type, ". Valid types are: ast, ir, binary");
    }
    return true;
  }

  zc::MainBuilder::Validity setASTDumpFormat(zc::StringPtr format) {
    if (format == "tree") {
      compilerOpts.emission.astDumpFormat =
          basic::CompilerOptions::EmissionOptions::ASTDumpFormat::Tree;
    } else if (format == "json") {
      compilerOpts.emission.astDumpFormat =
          basic::CompilerOptions::EmissionOptions::ASTDumpFormat::Json;
    } else if (format == "raw") {
      compilerOpts.emission.astDumpFormat =
          basic::CompilerOptions::EmissionOptions::ASTDumpFormat::Raw;
    } else {
      return zc::str("Invalid AST format: ", format, ". Valid formats are: tree, json, raw");
    }
    return true;
  }

  zc::MainBuilder::Validity enableASTDump() {
    compilerOpts.emission.outputType = basic::CompilerOptions::EmissionOptions::OutputType::AST;
    return true;
  }

  zc::MainBuilder::Validity enableSyntaxOnly() {
    compilerOpts.emission.syntaxOnly = true;
    return true;
  }

  zc::MainBuilder::Validity setOptimizationLevel(zc::StringPtr level) {
    if (level == "0") {
      compilerOpts.optimization.level = 0;
    } else if (level == "1") {
      compilerOpts.optimization.level = 1;
    } else if (level == "2") {
      compilerOpts.optimization.level = 2;
    } else if (level == "3") {
      compilerOpts.optimization.level = 3;
    } else {
      return zc::str("Invalid optimization level: ", level, ". Valid levels are: 0, 1, 2, 3");
    }
    return true;
  }

  zc::MainBuilder::Validity disableUnicode() {
    langOpts.useUnicode = false;
    return true;
  }

  zc::MainBuilder::Validity enableDollarIdentifiers() {
    langOpts.allowDollarIdentifiers = true;
    return true;
  }

  zc::MainBuilder::Validity disableRegexLiterals() {
    langOpts.supportRegexLiterals = false;
    return true;
  }

  zc::MainBuilder::Validity emitOutput() {
    // 1. Parsing
    if (!driver->parseSources() || driver->getDiagnosticEngine().hasErrors()) {
      return zc::str("Compilation failed due to parsing errors.");
    }

    const auto& options = driver->getCompilerOptions();

    // 2. Early AST Emission (skips binding)
    // We handle AST emission here to allow inspecting the syntax tree without requiring a
    // successful binding phase.
    if (options.emission.outputType == basic::CompilerOptions::EmissionOptions::OutputType::AST) {
      return emitAST();
    }

    // 3. Binding
    if (!driver->bindSources() || driver->getDiagnosticEngine().hasErrors()) {
      return zc::str("Compilation failed due to binding errors.");
    }

    // 4. Syntax Only Check
    if (options.emission.syntaxOnly) {
      context.warning("Syntax check completed successfully.");
      return true;
    }

    // 5. Final Emission
    switch (options.emission.outputType) {
      case basic::CompilerOptions::EmissionOptions::OutputType::IR:
        return emitIR();

      case basic::CompilerOptions::EmissionOptions::OutputType::Binary:
        return emitBinary();

      default:
        return zc::str("Unknown or unsupported output type specified.");
    }
  }

  zc::MainBuilder::Validity emitAST() {
    const auto& asts = driver->getASTs();
    const auto& options = driver->getCompilerOptions();

    zc::Maybe<zc::Own<zc::OutputStream>> outputStream =
        createOutputStream(options.emission.outputPath, options.emission.astDumpFormat);
    ZC_IF_SOME(stream, outputStream) {
      return dumpASTsToStream(*stream, asts, options.emission.astDumpFormat);
    }

    return "Failed to create output stream.";
  }

private:
  using ASTDumpFormat = basic::CompilerOptions::EmissionOptions::ASTDumpFormat;

  static ast::AstDumpFormat toAstDumpFormat(ASTDumpFormat format) {
    switch (format) {
      case ASTDumpFormat::Tree:
        return ast::AstDumpFormat::Tree;
      case ASTDumpFormat::Json:
        return ast::AstDumpFormat::Json;
      case ASTDumpFormat::Raw:
        return ast::AstDumpFormat::Raw;
    }
    ZC_UNREACHABLE;
  }

  /// Creates an appropriate output stream based on the given path and format
  zc::Maybe<zc::Own<zc::OutputStream>> createOutputStream(
      const zc::Maybe<zc::StringPtr>& outputPath, ASTDumpFormat format) {
    ZC_IF_SOME(path, outputPath) { return createFileOutputStream(path, format); }
    // Use stdout file descriptor to ensure shell redirection works properly
    return zc::heap<zc::FdOutputStream>(STDOUT_FILENO);
  }

  /// Creates a file output stream, handling directory paths appropriately
  zc::Maybe<zc::Own<zc::OutputStream>> createFileOutputStream(zc::StringPtr outputPath,
                                                              ASTDumpFormat format) {
    auto filesystem = zc::newDiskFilesystem();
    bool isAbsolute = outputPath.size() > 0 && outputPath[0] == '/';

    const zc::Directory& baseDir = isAbsolute ? filesystem->getRoot() : filesystem->getCurrent();
    zc::StringPtr pathText = isAbsolute ? outputPath.slice(1) : outputPath;

    zc::Path path = resolveOutputPath(pathText, format, baseDir);

    auto file = baseDir.openFile(
        path, zc::WriteMode::CREATE | zc::WriteMode::MODIFY | zc::WriteMode::CREATE_PARENT);

    return zc::heap<basic::FileOutputStream>(zc::mv(file));
  }

  /// Resolves the final output path, generating filename if path is a directory
  zc::Path resolveOutputPath(zc::StringPtr outputPath, ASTDumpFormat format,
                             const zc::Directory& currentDir) {
    zc::Path path = zc::Path::parse(outputPath);

    if (currentDir.exists(path)) {
      auto stat = currentDir.lstat(path);
      if (stat.type == zc::FsNode::Type::DIRECTORY) {
        zc::String filename = generateDefaultFilename(format);
        path = path.append(zc::mv(filename));
      }
    }

    return path;
  }

  /// Generates a default filename based on the first source file and format
  zc::String generateDefaultFilename(ASTDumpFormat format) {
    static constexpr char kDefaultBaseName[] = "ast_dump";

    auto maybeBaseName = extractSourceBaseName();
    zc::String baseName;
    ZC_IF_SOME(name, maybeBaseName) { baseName = zc::mv(name); }
    else { baseName = zc::str(kDefaultBaseName); }
    zc::StringPtr extension = ast::astDumpFileExtension(toAstDumpFormat(format));

    return zc::str(baseName, extension);
  }

  /// Extracts base name from the first source file
  zc::Maybe<zc::String> extractSourceBaseName() {
    const auto& asts = driver->getASTs();
    if (asts.size() == 0) return zc::none;

    const auto& firstEntry = *asts.begin();
    const source::BufferId& firstBufferId = firstEntry.key;

    const auto& sourceManager = driver->getSourceManager();
    zc::StringPtr filePath = sourceManager.getIdentifierForBuffer(firstBufferId);

    zc::Path sourcePath = zc::Path::parse(filePath);
    auto basenamePath = sourcePath.basename();

    if (basenamePath.size() == 0) return zc::none;

    zc::StringPtr filename = basenamePath[0];
    return filename.endsWith(".zom") ? zc::str(filename.slice(0, filename.size() - 4))
                                     : zc::str(filename);
  }

  zc::MainBuilder::Validity dumpASTsToStream(zc::OutputStream& outputStream, const auto& asts,
                                             ASTDumpFormat format) {
    const auto& sourceManager = driver->getSourceManager();
    for (const auto& entry : asts) {
      const ast::Tree& tree = entry.value;

      ZC_IF_SOME(error, ast::dumpTree(outputStream, tree, sourceManager, toAstDumpFormat(format))) {
        return zc::mv(error);
      }
    }

    return true;
  }

  zc::MainBuilder::Validity emitIR() {
    // TODO: Implement IR generation and output
    return zc::str("IR emission is not yet implemented.");
  }

  zc::MainBuilder::Validity emitBinary() {
    // TODO: Implement binary generation
    return "Binary emission is not yet implemented.";
  }

private:
  zc::ProcessContext& context;
  zc::Own<driver::CompilerDriver> driver;
  zc::SpaceFor<driver::CompilerDriver> driverSpace;
  basic::CompilerOptions compilerOpts;
  basic::LangOptions langOpts;
};

}  // namespace utils
}  // namespace compiler
}  // namespace zomlang

ZC_MAIN(zomlang::compiler::utils::CompilerMain)
