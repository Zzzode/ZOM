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
#include "zomlang/compiler/ast/dumper.h"
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
        .addOptionWithArg({"format"}, ZC_BIND_METHOD(*this, setOutputFormat), "<format>",
                          "Set output format: text, json, xml (default: text)")
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
      compilerOpts.emission.dumpASTEnabled = true;
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

  zc::MainBuilder::Validity setOutputFormat(zc::StringPtr format) {
    if (format == "text") {
      compilerOpts.emission.dumpFormat = ast::DumpFormat::kTEXT;
    } else if (format == "json") {
      compilerOpts.emission.dumpFormat = ast::DumpFormat::kJSON;
    } else if (format == "xml") {
      compilerOpts.emission.dumpFormat = ast::DumpFormat::kXML;
    } else {
      return zc::str("Invalid format: ", format, ". Valid formats are: text, json, xml");
    }
    return true;
  }

  zc::MainBuilder::Validity enableASTDump() {
    compilerOpts.emission.outputType = basic::CompilerOptions::EmissionOptions::OutputType::AST;
    compilerOpts.emission.dumpASTEnabled = true;
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
    // Trigger the parallel parsing process
    bool success = driver->parseSources();

    if (!success || driver->getDiagnosticEngine().hasErrors()) {
      return zc::str("Compilation failed due to parsing errors.");
    }

    // If syntax-only mode, we're done after parsing
    const auto& options = driver->getCompilerOptions();
    if (options.emission.syntaxOnly) {
      context.warning("Syntax check completed successfully.");
      return true;
    }

    // Proceed with output generation based on type
    switch (options.emission.outputType) {
      case basic::CompilerOptions::EmissionOptions::OutputType::AST:
        return emitAST();

      case basic::CompilerOptions::EmissionOptions::OutputType::IR:
        return emitIR();

      case basic::CompilerOptions::EmissionOptions::OutputType::Binary:
        return emitBinary();

      default:
        return zc::str("Unknown output type specified.");
    }
  }

  zc::MainBuilder::Validity emitAST() {
    const auto& asts = driver->getASTs();
    const auto& options = driver->getCompilerOptions();

    zc::Maybe<zc::Own<zc::OutputStream>> outputStream =
        createOutputStream(options.emission.outputPath, options.emission.dumpFormat);
    ZC_IF_SOME(stream, outputStream) {
      return dumpASTsToStream(*stream, asts, options.emission.dumpFormat);
    }

    return "Failed to create output stream.";
  }

private:
  /// Creates an appropriate output stream based on the given path and format
  zc::Maybe<zc::Own<zc::OutputStream>> createOutputStream(
      const zc::Maybe<zc::StringPtr>& outputPath, ast::DumpFormat format) {
    ZC_IF_SOME(path, outputPath) { return createFileOutputStream(path, format); }
    // Use stdout file descriptor to ensure shell redirection works properly
    return zc::heap<zc::FdOutputStream>(STDOUT_FILENO);
  }

  /// Creates a file output stream, handling directory paths appropriately
  zc::Maybe<zc::Own<zc::OutputStream>> createFileOutputStream(zc::StringPtr outputPath,
                                                              ast::DumpFormat format) {
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
  zc::Path resolveOutputPath(zc::StringPtr outputPath, ast::DumpFormat format,
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
  zc::String generateDefaultFilename(ast::DumpFormat format) {
    static constexpr char kDefaultBaseName[] = "ast_dump";

    auto maybeBaseName = extractSourceBaseName();
    zc::String baseName;
    ZC_IF_SOME(name, maybeBaseName) { baseName = zc::mv(name); }
    else { baseName = zc::str(kDefaultBaseName); }
    zc::StringPtr extension = getFileExtensionForFormat(format);

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

  /// Returns the appropriate file extension for the given dump format
  static constexpr zc::StringPtr getFileExtensionForFormat(ast::DumpFormat format) {
    switch (format) {
      case ast::DumpFormat::kJSON:
        return ".json";
      case ast::DumpFormat::kXML:
        return ".xml";
      default:
        return ".ast";
    }
  }

  /// Dumps all ASTs to the given output stream
  zc::MainBuilder::Validity dumpASTsToStream(zc::OutputStream& outputStream, const auto& asts,
                                             ast::DumpFormat format) {
    ast::ASTDumper dumper(outputStream, format);

    for (const auto& entry : asts) {
      const source::BufferId& bufferId = entry.key;
      const ast::Node& astNode = *entry.value;

      writeBufferHeader(outputStream, bufferId, format);
      dumper.dump(astNode);
      writeBufferFooter(outputStream, format);
    }

    return true;
  }

  /// Writes buffer header for text format
  static void writeBufferHeader(zc::OutputStream& outputStream, const source::BufferId& bufferId,
                                ast::DumpFormat format) {
    if (format == ast::DumpFormat::kTEXT) {
      outputStream.write(
          zc::str("\n=== AST for BufferId: ", static_cast<uint64_t>(bufferId), " ===\n").asBytes());
    }
  }

  /// Writes buffer footer for text format
  static void writeBufferFooter(zc::OutputStream& outputStream, ast::DumpFormat format) {
    if (format == ast::DumpFormat::kTEXT) { outputStream.write("\n"_zcb); }
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
