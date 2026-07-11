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
#include "zc/core/exception.h"
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
#include "zomlang/compiler/driver/compiler-session.h"
#include "zomlang/compiler/irgen/ir-dump.h"
#include "zomlang/compiler/irgen/lowering.h"
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
    session = sessionSpace.construct(contextFactory, langOpts, compilerOpts);
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
                          "Set output type: ast, dispatch, ir, binary (default: binary)")
        .addOptionWithArg({"ast-format"}, ZC_BIND_METHOD(*this, setASTDumpFormat), "<format>",
                          "Set AST dump format: tree, json, raw (default: tree)")
        .addOption({"dump-ast"}, ZC_BIND_METHOD(*this, enableASTDump),
                   "Dump AST to stdout (shorthand for --emit=ast)")
        .addOption({"dump-dispatch"}, ZC_BIND_METHOD(*this, enableDispatchDump),
                   "Dump checked call dispatch records to stdout (shorthand for --emit=dispatch)")
        .addOption({"syntax-only"}, ZC_BIND_METHOD(*this, enableSyntaxOnly),
                   "Only perform syntax checking, no code generation")
        .addOptionWithArg({'O', "optimize"}, ZC_BIND_METHOD(*this, setOptimizationLevel), "<level>",
                          "Set optimization level: 0, 1, 2, 3 (default: 0)")
        .addOptionWithArg({"panic"}, ZC_BIND_METHOD(*this, setPanicStrategy), "<strategy>",
                          "Set panic strategy: abort, unwind (default: abort)")
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

    if (const zc::Maybe<source::BufferId> bufferId = session->addSourceFile(file);
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
    } else if (type == "dispatch") {
      compilerOpts.emission.outputType =
          basic::CompilerOptions::EmissionOptions::OutputType::Dispatch;
    } else if (type == "ir") {
      compilerOpts.emission.outputType = basic::CompilerOptions::EmissionOptions::OutputType::IR;
    } else if (type == "binary") {
      compilerOpts.emission.outputType =
          basic::CompilerOptions::EmissionOptions::OutputType::Binary;
    } else {
      return zc::str("Invalid output type: ", type, ". Valid types are: ast, dispatch, ir, binary");
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

  zc::MainBuilder::Validity enableDispatchDump() {
    compilerOpts.emission.outputType =
        basic::CompilerOptions::EmissionOptions::OutputType::Dispatch;
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

  zc::MainBuilder::Validity setPanicStrategy(zc::StringPtr strategy) {
    if (strategy == "abort") {
      compilerOpts.panicStrategy = basic::CompilerOptions::PanicStrategy::Abort;
    } else if (strategy == "unwind") {
      compilerOpts.panicStrategy = basic::CompilerOptions::PanicStrategy::Unwind;
    } else {
      return zc::str("Invalid panic strategy: ", strategy, ". Valid strategies are: abort, unwind");
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
    if (!session->parseSources() || session->getDiagnosticEngine().hasErrors()) {
      return zc::str("Compilation failed due to parsing errors.");
    }

    const auto& options = session->getCompilerOptions();

    // 2. Early AST Emission (skips binding)
    // We handle AST emission here to allow inspecting the syntax tree without requiring a
    // successful binding phase.
    if (options.emission.outputType == basic::CompilerOptions::EmissionOptions::OutputType::AST) {
      return emitAST();
    }

    // 3. Binding
    if (!session->bindSources() || session->getDiagnosticEngine().hasErrors()) {
      return zc::str("Compilation failed due to binding errors.");
    }

    // 4. Type checking
    if (!session->checkSources() || session->getDiagnosticEngine().hasErrors()) {
      return zc::str("Compilation failed due to type checking errors.");
    }

    // 5. Dispatch Dump
    if (options.emission.outputType ==
        basic::CompilerOptions::EmissionOptions::OutputType::Dispatch) {
      return emitDispatch();
    }

    // 6. Syntax Only Check
    if (options.emission.syntaxOnly) {
      context.warning("Syntax check completed successfully.");
      return true;
    }

    // 7. Final Emission
    if (options.panicStrategy == basic::CompilerOptions::PanicStrategy::Unwind) {
      return diagnoseEmission<diagnostics::DiagID::PanicUnwindUnsupported>(emissionLocation());
    }
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
    const auto& asts = session->getASTs();
    const auto& options = session->getCompilerOptions();

    zc::Maybe<zc::Own<zc::OutputStream>> outputStream = createOutputStream(
        options.emission.outputPath, options.emission.astDumpFormat, DumpOutputKind::Ast);
    ZC_IF_SOME(stream, outputStream) {
      return dumpASTsToStream(*stream, asts, options.emission.astDumpFormat);
    }

    return "Failed to create output stream.";
  }

  zc::MainBuilder::Validity emitDispatch() {
    zc::Maybe<zc::Own<zc::OutputStream>> outputStream = createOutputStream(
        compilerOpts.emission.outputPath, ASTDumpFormat::Tree, DumpOutputKind::Dispatch);
    ZC_IF_SOME(stream, outputStream) { return dumpDispatchToStream(*stream); }

    return "Failed to create output stream.";
  }

private:
  using ASTDumpFormat = basic::CompilerOptions::EmissionOptions::ASTDumpFormat;

  enum class DumpOutputKind {
    Ast,
    Dispatch,
    Ir,
  };

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

  static zc::StringPtr loweringPhaseName(irgen::LoweringPhase phase) {
    switch (phase) {
      case irgen::LoweringPhase::CheckedInput:
        return "checked-input"_zc;
      case irgen::LoweringPhase::FunctionSignature:
        return "function-signature"_zc;
      case irgen::LoweringPhase::FunctionBody:
        return "function-body"_zc;
      case irgen::LoweringPhase::Expression:
        return "expression"_zc;
      case irgen::LoweringPhase::ErrorPropagation:
        return "error-propagation"_zc;
      case irgen::LoweringPhase::ForcedUnwrap:
        return "forced-unwrap"_zc;
      case irgen::LoweringPhase::TargetLayout:
        return "target-layout"_zc;
      case irgen::LoweringPhase::ModuleVerification:
        return "module-verification"_zc;
    }
    ZC_UNREACHABLE;
  }

  static zc::StringPtr loweringFailureName(irgen::LoweringFailureKind kind) {
    switch (kind) {
      case irgen::LoweringFailureKind::UnsupportedSourceShape:
        return "unsupported-source-shape"_zc;
      case irgen::LoweringFailureKind::UnsupportedExpression:
        return "unsupported-expression"_zc;
      case irgen::LoweringFailureKind::UnknownTargetLayout:
        return "unknown-target-layout"_zc;
      case irgen::LoweringFailureKind::UnsupportedCrossSourceTarget:
        return "unsupported-cross-source-target"_zc;
      case irgen::LoweringFailureKind::DispatchNotFrozen:
        return "dispatch-not-frozen"_zc;
      case irgen::LoweringFailureKind::InvalidSourceRoot:
        return "invalid-source-root"_zc;
      case irgen::LoweringFailureKind::InvalidStatementList:
        return "invalid-statement-list"_zc;
      case irgen::LoweringFailureKind::InvalidBindingMetadata:
        return "invalid-binding-metadata"_zc;
      case irgen::LoweringFailureKind::MissingBindingSymbol:
        return "missing-binding-symbol"_zc;
      case irgen::LoweringFailureKind::MissingTypeFact:
        return "missing-type-fact"_zc;
      case irgen::LoweringFailureKind::InvalidTypeFact:
        return "invalid-type-fact"_zc;
      case irgen::LoweringFailureKind::MissingDispatchFact:
        return "missing-dispatch-fact"_zc;
      case irgen::LoweringFailureKind::InvalidDispatchFact:
        return "invalid-dispatch-fact"_zc;
      case irgen::LoweringFailureKind::ErrorUnionLayoutMismatch:
        return "error-union-layout-mismatch"_zc;
      case irgen::LoweringFailureKind::MissingErrorAlternative:
        return "missing-error-alternative"_zc;
      case irgen::LoweringFailureKind::MissingSourceContext:
        return "missing-source-context"_zc;
      case irgen::LoweringFailureKind::InvalidSourceRange:
        return "invalid-source-range"_zc;
      case irgen::LoweringFailureKind::DuplicateFunctionSymbol:
        return "duplicate-function-symbol"_zc;
    }
    ZC_UNREACHABLE;
  }

  static zc::StringPtr irDumpFailureName(irgen::IrDumpFailureKind kind) {
    switch (kind) {
      case irgen::IrDumpFailureKind::InvalidTypeReference:
        return "invalid-type-reference"_zc;
      case irgen::IrDumpFailureKind::InvalidLayout:
        return "invalid-layout"_zc;
      case irgen::IrDumpFailureKind::InvalidLayoutReference:
        return "invalid-layout-reference"_zc;
      case irgen::IrDumpFailureKind::InvalidFunction:
        return "invalid-function"_zc;
      case irgen::IrDumpFailureKind::DuplicateFunctionSymbol:
        return "duplicate-function-symbol"_zc;
      case irgen::IrDumpFailureKind::InvalidBlockReference:
        return "invalid-block-reference"_zc;
      case irgen::IrDumpFailureKind::DuplicateBlock:
        return "duplicate-block"_zc;
      case irgen::IrDumpFailureKind::InvalidValueReference:
        return "invalid-value-reference"_zc;
      case irgen::IrDumpFailureKind::DuplicateValue:
        return "duplicate-value"_zc;
      case irgen::IrDumpFailureKind::InvalidInstruction:
        return "invalid-instruction"_zc;
      case irgen::IrDumpFailureKind::InvalidTerminator:
        return "invalid-terminator"_zc;
      case irgen::IrDumpFailureKind::UnresolvedCallTarget:
        return "unresolved-call-target"_zc;
    }
    ZC_UNREACHABLE;
  }

  static zc::StringPtr irDumpVerifierSiteName(irgen::IrDumpVerifierSite site) {
    switch (site) {
      case irgen::IrDumpVerifierSite::Module:
        return "module"_zc;
      case irgen::IrDumpVerifierSite::Layout:
        return "layout"_zc;
      case irgen::IrDumpVerifierSite::Function:
        return "function"_zc;
      case irgen::IrDumpVerifierSite::Block:
        return "block"_zc;
      case irgen::IrDumpVerifierSite::Instruction:
        return "instruction"_zc;
      case irgen::IrDumpVerifierSite::Terminator:
        return "terminator"_zc;
    }
    ZC_UNREACHABLE;
  }

  /// Creates an appropriate output stream based on the given path and format
  zc::Maybe<zc::Own<zc::OutputStream>> createOutputStream(
      const zc::Maybe<zc::StringPtr>& outputPath, ASTDumpFormat format, DumpOutputKind kind) {
    ZC_IF_SOME(path, outputPath) { return createFileOutputStream(path, format, kind); }
    // Use stdout file descriptor to ensure shell redirection works properly
    return zc::heap<zc::FdOutputStream>(STDOUT_FILENO);
  }

  /// Creates a file output stream, handling directory paths appropriately
  zc::Maybe<zc::Own<zc::OutputStream>> createFileOutputStream(zc::StringPtr outputPath,
                                                              ASTDumpFormat format,
                                                              DumpOutputKind kind) {
    zc::Maybe<zc::Own<zc::OutputStream>> result;
    auto exception = zc::runCatchingExceptions([&]() {
      auto filesystem = zc::newDiskFilesystem();
      const bool isAbsolute = outputPath.size() > 0 && outputPath[0] == '/';

      const zc::Directory& baseDir = isAbsolute ? filesystem->getRoot() : filesystem->getCurrent();
      const zc::StringPtr pathText = isAbsolute ? outputPath.slice(1) : outputPath;
      const zc::Path path = resolveOutputPath(pathText, format, kind, baseDir);

      auto file = baseDir.openFile(
          path, zc::WriteMode::CREATE | zc::WriteMode::MODIFY | zc::WriteMode::CREATE_PARENT);
      result = zc::heap<basic::FileOutputStream>(zc::mv(file));
    });
    if (exception != zc::none) { return zc::none; }
    return result;
  }

  /// Resolves the final output path, generating filename if path is a directory
  zc::Path resolveOutputPath(zc::StringPtr outputPath, ASTDumpFormat format, DumpOutputKind kind,
                             const zc::Directory& currentDir) {
    zc::Path path = zc::Path::parse(outputPath);

    if (currentDir.exists(path)) {
      auto stat = currentDir.lstat(path);
      if (stat.type == zc::FsNode::Type::DIRECTORY) {
        zc::String filename = generateDefaultFilename(format, kind);
        path = path.append(zc::mv(filename));
      }
    }

    return path;
  }

  /// Generates a default filename based on the first source file and format
  zc::String generateDefaultFilename(ASTDumpFormat format, DumpOutputKind kind) {
    static constexpr char kDefaultBaseName[] = "ast_dump";

    auto maybeBaseName = extractSourceBaseName();
    zc::String baseName;
    ZC_IF_SOME(name, maybeBaseName) { baseName = zc::mv(name); }
    else { baseName = zc::str(kDefaultBaseName); }
    zc::StringPtr extension;
    switch (kind) {
      case DumpOutputKind::Ast:
        extension = ast::astDumpFileExtension(toAstDumpFormat(format));
        break;
      case DumpOutputKind::Dispatch:
        extension = ".dispatch.txt"_zc;
        break;
      case DumpOutputKind::Ir:
        extension = ".ir"_zc;
        break;
    }

    return zc::str(baseName, extension);
  }

  /// Extracts base name from the first source file
  zc::Maybe<zc::String> extractSourceBaseName() {
    const auto& asts = session->getASTs();
    if (asts.size() == 0) return zc::none;

    const auto& firstEntry = *asts.begin();
    const source::BufferId& firstBufferId = firstEntry.key;

    const auto& sourceManager = session->getSourceManager();
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
    const auto& sourceManager = session->getSourceManager();
    for (const auto& entry : asts) {
      const ast::Tree& tree = entry.value;

      ZC_IF_SOME(error, ast::dumpTree(outputStream, tree, sourceManager, toAstDumpFormat(format))) {
        return zc::mv(error);
      }
    }

    return true;
  }

  zc::MainBuilder::Validity dumpDispatchToStream(zc::OutputStream& outputStream) {
    const auto& typeEnvs = session->getTypeEnvs();
    for (const source::BufferId& bufferId : session->getSourceManager().getManagedBufferIds()) {
      ZC_IF_SOME(typeEnv, typeEnvs.find(bufferId)) { typeEnv.dumpDispatch(outputStream); }
    }
    return true;
  }

  source::SourceLoc emissionLocation() const {
    const auto& asts = session->getASTs();
    if (asts.size() == 0) { return source::SourceLoc(); }

    const auto& firstEntry = *asts.begin();
    const auto& tree = firstEntry.value;
    const auto root = tree.root();
    if (!root || !tree.contains(root) || tree.node(root).range.isInvalid()) {
      return source::SourceLoc();
    }
    return tree.node(root).range.getStart();
  }

  template <diagnostics::DiagID Id>
  zc::MainBuilder::Validity diagnoseEmission(source::SourceLoc loc) {
    session->getDiagnosticEngine().diagnose<Id>(loc);
    context.error(zc::StringPtr());
    return true;
  }

  zc::MainBuilder::Validity diagnoseLoweringFailure(const ast::Tree& tree,
                                                    const irgen::LoweringFailure& failure) {
    auto loc = emissionLocation();
    if (failure.node && tree.contains(failure.node) && tree.node(failure.node).range.isValid()) {
      loc = tree.node(failure.node).range.getStart();
    }

    switch (failure.kind) {
      case irgen::LoweringFailureKind::UnsupportedSourceShape:
        return diagnoseEmission<diagnostics::DiagID::IrUnsupportedSourceShape>(loc);
      case irgen::LoweringFailureKind::UnsupportedExpression:
        return diagnoseEmission<diagnostics::DiagID::IrUnsupportedExpression>(loc);
      case irgen::LoweringFailureKind::UnknownTargetLayout:
        return diagnoseEmission<diagnostics::DiagID::IrUnknownTargetLayout>(loc);
      case irgen::LoweringFailureKind::UnsupportedCrossSourceTarget:
        return diagnoseEmission<diagnostics::DiagID::IrCrossSourceCallUnsupported>(loc);
      case irgen::LoweringFailureKind::DispatchNotFrozen:
      case irgen::LoweringFailureKind::InvalidSourceRoot:
      case irgen::LoweringFailureKind::InvalidStatementList:
      case irgen::LoweringFailureKind::InvalidBindingMetadata:
      case irgen::LoweringFailureKind::MissingBindingSymbol:
      case irgen::LoweringFailureKind::MissingTypeFact:
      case irgen::LoweringFailureKind::InvalidTypeFact:
      case irgen::LoweringFailureKind::MissingDispatchFact:
      case irgen::LoweringFailureKind::InvalidDispatchFact:
      case irgen::LoweringFailureKind::ErrorUnionLayoutMismatch:
      case irgen::LoweringFailureKind::MissingErrorAlternative:
      case irgen::LoweringFailureKind::MissingSourceContext:
      case irgen::LoweringFailureKind::InvalidSourceRange:
      case irgen::LoweringFailureKind::DuplicateFunctionSymbol:
        session->getDiagnosticEngine().diagnose<diagnostics::DiagID::IrLoweringInvariantViolation>(
            loc, loweringPhaseName(failure.phase), loweringFailureName(failure.kind),
            zc::str(static_cast<uint64_t>(failure.node.value)));
        context.error(zc::StringPtr());
        return true;
    }

    ZC_UNREACHABLE;
  }

  zc::MainBuilder::Validity emitIR() {
    const auto bufferIds = session->getSourceManager().getManagedBufferIds();
    if (bufferIds.size() != 1) {
      return diagnoseEmission<diagnostics::DiagID::IrSingleSourceRequired>(emissionLocation());
    }

    const auto bufferId = bufferIds[0];
    const auto& asts = session->getASTs();
    const auto& metadataByBuffer = session->getBindingMetadata();
    const auto& typeEnvs = session->getTypeEnvs();
    const auto maybeTree = asts.find(bufferId);
    const auto maybeMetadata = metadataByBuffer.find(bufferId);
    const auto maybeTypeEnv = typeEnvs.find(bufferId);
    if (maybeTree == zc::none || maybeMetadata == zc::none || maybeTypeEnv == zc::none) {
      return diagnoseEmission<diagnostics::DiagID::IrCheckedInputMissing>(emissionLocation());
    }

    zc::Maybe<const ast::Tree&> tree = maybeTree;
    zc::Maybe<const ast::BindingMetadata&> metadata = maybeMetadata;
    zc::Maybe<const type::TypeEnv&> typeEnv = maybeTypeEnv;
    ZC_IF_SOME(checkedTree, tree) {
      ZC_IF_SOME(bindingMetadata, metadata) {
        ZC_IF_SOME(checkedTypeEnv, typeEnv) {
          irgen::LoweringSourceContext sourceContext(session->getSourceManager(), bufferId);
          auto lowering = irgen::lowerCheckedTree(checkedTree, bindingMetadata, checkedTypeEnv,
                                                  irgen::TargetDataLayout::lp64(), sourceContext);
          if (lowering.is<irgen::LoweringFailure>()) {
            return diagnoseLoweringFailure(checkedTree, lowering.get<irgen::LoweringFailure>());
          }

          zc::Maybe<zc::Own<zc::OutputStream>> outputStream = createOutputStream(
              compilerOpts.emission.outputPath, ASTDumpFormat::Tree, DumpOutputKind::Ir);
          ZC_IF_SOME(stream, outputStream) {
            auto dumpResult = irgen::dumpModule(*stream, lowering.get<irgen::Module>());
            ZC_IF_SOME(failure, dumpResult) {
              session->getDiagnosticEngine()
                  .diagnose<diagnostics::DiagID::IrDumpInvariantViolation>(
                      emissionLocation(), irDumpVerifierSiteName(failure.site),
                      irDumpFailureName(failure.kind), zc::str(failure.symbol.getRaw()),
                      zc::str(static_cast<uint64_t>(failure.block.value)),
                      zc::str(static_cast<uint64_t>(failure.value.value)),
                      zc::str(static_cast<uint64_t>(failure.type.value)),
                      zc::str(static_cast<uint64_t>(failure.index)));
              context.error(zc::StringPtr());
              return true;
            }
            return true;
          }
          return diagnoseEmission<diagnostics::DiagID::IrOutputCreationFailed>(emissionLocation());
        }
      }
    }

    ZC_UNREACHABLE;
  }

  zc::MainBuilder::Validity emitBinary() {
    return diagnoseEmission<diagnostics::DiagID::BinaryEmissionUnavailable>(emissionLocation());
  }

private:
  zc::ProcessContext& context;
  identity::SemanticContextFactory contextFactory;
  zc::Own<driver::CompilerSession> session;
  zc::SpaceFor<driver::CompilerSession> sessionSpace;
  basic::CompilerOptions compilerOpts;
  basic::LangOptions langOpts;
};

}  // namespace utils
}  // namespace compiler
}  // namespace zomlang

ZC_MAIN(zomlang::compiler::utils::CompilerMain)
