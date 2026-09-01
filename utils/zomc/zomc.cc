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

#if defined(__linux__)
#include <unistd.h>

#include <cstdio>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <stdlib.h>
#endif

#include "compiler/ast/dump.h"
#include "compiler/ast/tree.h"
#include "compiler/basic/compiler-opts.h"
#include "compiler/basic/io-utils.h"
#include "compiler/basic/string-pool.h"
#include "compiler/basic/zomlang-opts.h"
#include "compiler/cst/lexeme-stream-builder.h"
#include "compiler/diagnostics/core/diagnostic-engine.h"
#include "compiler/driver/package/lockfile.h"
#include "compiler/driver/package/package-compilation-request.h"
#include "compiler/driver/package/package-diagnostic.h"
#include "compiler/driver/package/source-record.h"
#include "compiler/driver/session/compiler-session.h"
#include "compiler/format/lexeme-printer.h"
#include "compiler/identity/canonical/canonical-encoder.h"
#include "compiler/identity/crypto/sha256.h"
#include "compiler/ir/executable-publication.h"
#include "compiler/ir/host-execution-profile.h"
#include "compiler/ir/ir-diagnostic-adapter.h"
#include "compiler/ir/link-plan-codec.h"
#include "compiler/ir/target-registry.h"
#include "compiler/lexer/lexer.h"
#include "compiler/source/core-distribution.h"
#include "compiler/source/core-source-admission.h"
#include "compiler/source/manager.h"
#include "zc/core/common.h"
#include "zc/core/exception.h"
#include "zc/core/filesystem.h"
#include "zc/core/io.h"
#include "zc/core/main.h"
#include "zc/core/one-of.h"
#include "zc/core/string.h"
#include "zc/core/subprocess.h"
#include "zc/core/time.h"
#include "zc/core/vector.h"

#if ZOM_ENABLE_LLVM_BACKEND
// RFC 0021 O5/KR5.1: the object-emission path is compiled only when the LLVM
// backend is built. These headers stay behind the flag so the frontend-only
// build never references the isolation-wall shim or LIR lowering.
#include "compiler/backend/llvm/llvm-translator.h"
#include "compiler/lir/mir-to-lir.h"
#include "compiler/mir/built-mir.h"
#endif

#ifndef VERSION
#define VERSION "(unknown)"
#endif

namespace zomlang {
namespace compiler {
namespace utils {

namespace package = driver::package;

static constexpr char VERSION_STRING[] = "ZomLang Version " VERSION;

zc::Maybe<zc::Path> currentExecutablePath(const zc::Filesystem& filesystem) {
#if defined(__linux__)
  size_t capacity = 256;
  for (;;) {
    zc::Vector<char> path;
    path.resize(capacity);
    const ssize_t length = readlink("/proc/self/exe", path.begin(), path.size());
    if (length <= 0) return zc::none;
    if (static_cast<size_t>(length) < path.size()) {
      path[static_cast<size_t>(length)] = '\0';
      return filesystem.getCurrentPath().eval(zc::StringPtr(path.begin()));
    }
    if (capacity > static_cast<size_t>(-1) / 2) return zc::none;
    capacity *= 2;
  }
#elif defined(__APPLE__)
  uint32_t size = 0;
  if (_NSGetExecutablePath(nullptr, &size) != -1 || size == 0) { return zc::none; }
  zc::Vector<char> path(size);
  path.resize(size);
  if (_NSGetExecutablePath(path.begin(), &size) != 0 || size == 0) { return zc::none; }
  char* resolved = realpath(path.begin(), nullptr);
  if (resolved == nullptr) { return zc::none; }
  auto evaluated = filesystem.getCurrentPath().eval(zc::StringPtr(resolved));
  free(resolved);
  return evaluated;
#else
  (void)filesystem;
  return zc::none;
#endif
}

struct HostTargetConfiguration final {
  zc::StringPtr architecture;
  zc::StringPtr vendor;
  zc::StringPtr operatingSystem;
  zc::StringPtr environment;
  zc::StringPtr triple;
  zc::StringPtr dataLayout;
  ir::ObjectFormat objectFormat;
};

HostTargetConfiguration hostTargetConfiguration() {
#if defined(__aarch64__) || defined(__arm64__)
#if defined(__APPLE__)
  return {"aarch64"_zc,
          "apple"_zc,
          "darwin"_zc,
          "unknown"_zc,
          "aarch64-apple-darwin"_zc,
          "e-m:o-i64:64-i128:128-n32:64-S128-Fn32"_zc,
          ir::ObjectFormat::MachO};
#elif defined(__linux__)
  return {"aarch64"_zc,
          "unknown"_zc,
          "linux"_zc,
          "gnu"_zc,
          "aarch64-unknown-linux-gnu"_zc,
          "e-m:e-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128-Fn32"_zc,
          ir::ObjectFormat::Elf};
#else
#error "The compiler host target must have a registered profile."
#endif
#elif defined(__x86_64__)
#if defined(__APPLE__)
  return {"x86_64"_zc,
          "apple"_zc,
          "darwin"_zc,
          "unknown"_zc,
          "x86_64-apple-darwin"_zc,
          "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"_zc,
          ir::ObjectFormat::MachO};
#elif defined(__linux__)
  return {"x86_64"_zc,
          "unknown"_zc,
          "linux"_zc,
          "gnu"_zc,
          "x86_64-unknown-linux-gnu"_zc,
          "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"_zc,
          ir::ObjectFormat::Elf};
#else
#error "The compiler host target must have a registered profile."
#endif
#elif defined(__i386__) && defined(__linux__)
  return {
      "i686"_zc,
      "unknown"_zc,
      "linux"_zc,
      "gnu"_zc,
      "i686-unknown-linux-gnu"_zc,
      "e-m:e-p:32:32-p270:32:32-p271:32:32-p272:64:64-i128:128-f64:32:64-f80:32-n8:16:32-S128"_zc,
      ir::ObjectFormat::Elf};
#else
#error "The compiler host target must have a registered profile."
#endif
}

class TransientCoreSourceDirectory final : public package::FreshSourceDirectory {
public:
  explicit TransientCoreSourceDirectory(zc::Own<const zc::Directory>&& root)
      : rootValue(zc::mv(root)) {}
  ~TransientCoreSourceDirectory() noexcept override = default;
  const zc::Directory& root() const override { return *rootValue; }
  zc::Maybe<package::MaterializationIssue> finish() override { return zc::none; }

private:
  zc::Own<const zc::Directory> rootValue;
};

class TransientCoreSourceDirectoryFactory final : public package::FreshSourceDirectoryFactory {
public:
  package::FreshSourceDirectoryResult create() override {
    zc::Own<const zc::Directory> root = zc::newInMemoryDirectory(zc::nullClock());
    return zc::Own<package::FreshSourceDirectory>(
        zc::heap<TransientCoreSourceDirectory>(zc::mv(root)));
  }
};

class CompilerMain {
public:
  explicit CompilerMain(zc::ProcessContext& context) : context(context) {
    session = sessionSpace.construct(contextFactory, langOpts, compilerOpts);
  }

  zc::MainFunc getMain() {
    return zc::MainBuilder(context, VERSION_STRING, "Command-line tool for Zomlang Compiler.")
        .addSubCommand("compile", ZC_BIND_METHOD(*this, getCompileMain),
                       "Compiles source code in one or more target.")
        .addSubCommand("build", ZC_BIND_METHOD(*this, getBuildMain),
                       "Build a selected package target through verified Built MIR.")
        .addSubCommand("run", ZC_BIND_METHOD(*this, getRunMain),
                       "Run a zomlang program with project configuration.")
        .addSubCommand("fmt", ZC_BIND_METHOD(*this, getFmtMain),
                       "Format Zomlang sources in place, or check for drift with --check.")
        .build();
  }

  zc::MainFunc getCompileMain() {
    action = CompilationAction::Emit;
    zc::MainBuilder builder(context, VERSION_STRING,
                            "Compiles Zomlang sources and generates one or more targets.");
    addCompileOptions(builder);
    return builder.build();
  }

  ZC_NODISCARD zc::MainFunc getBuildMain() {
    action = CompilationAction::FrontendOnly;
    zc::MainBuilder builder(context, VERSION_STRING,
                            "Build one selected package target through verified Built MIR.");
    addBuildOptions(builder);
    return builder.build();
  }

  ZC_NODISCARD zc::MainFunc getRunMain() {
    action = CompilationAction::Run;
    zc::MainBuilder builder(context, VERSION_STRING,
                            "Compile, link, and execute one selected package target.");
    addCompileOptions(builder);
    return builder.build();
  }

  // =====================================================================================
  // "fmt" command
  //
  // RFC 0044 O6/KR6.2: the deterministic source formatter. `fmt` normalizes each
  // source in place; `fmt --check` reports formatting drift without writing. The
  // formatter consumes the RFC 0023 verified lexeme stream and applies only the
  // whitespace normalizations RFC 0044 permits (trailing-whitespace strip, one
  // final newline, one space after a comma) -- each proven to re-lex to the
  // identical token sequence. Structural indentation and line reflow are later
  // RFC 0044 refinements layered on this base.

  ZC_NODISCARD zc::MainFunc getFmtMain() {
    zc::MainBuilder builder(context, VERSION_STRING,
                            "Formats Zomlang sources with the fixed project style.");
    return builder
        .addOption(
            {"check"}, ZC_BIND_METHOD(*this, enableFmtCheck),
            "Report formatting drift without writing; exit non-zero if any file would change.")
        .expectOneOrMoreArgs("<source>", ZC_BIND_METHOD(*this, addFmtSource))
        .callAfterParsing(ZC_BIND_METHOD(*this, runFmt))
        .build();
  }

  zc::MainBuilder::Validity enableFmtCheck() {
    fmtCheckOnly = true;
    return true;
  }

  zc::MainBuilder::Validity addFmtSource(zc::StringPtr value) {
    fmtSources.add(zc::str(value));
    return true;
  }

  zc::MainBuilder::Validity runFmt() {
    uint64_t driftedFiles = 0;
    for (const zc::String& sourcePath : fmtSources) {
      auto formatted = formatSourceFile(sourcePath);
      ZC_IF_SOME(canonical, formatted) {
        if (canonical.changed) {
          ++driftedFiles;
          if (fmtCheckOnly) {
            context.warning(zc::str(sourcePath, ": formatting drift"));
          } else if (!writeSourceFile(sourcePath, canonical.text)) {
            return zc::str("Failed to write formatted output to ", sourcePath, ".");
          }
        }
      } else {
        return zc::str("Failed to format ", sourcePath, ".");
      }
    }
    if (fmtCheckOnly && driftedFiles > 0) {
      return zc::str(driftedFiles, " file(s) are not formatted; run `zomc fmt` to fix.");
    }
    return true;
  }

  // The canonical bytes for one formatted source plus whether they differ from
  // the input.
  struct FormattedSource final {
    zc::String text;
    bool changed;
  };

  // Reads, lexes, and normalizes one source file. Returns none only when the file
  // cannot be read or its bytes do not partition into a verified lexeme stream.
  ZC_NODISCARD zc::Maybe<FormattedSource> formatSourceFile(zc::StringPtr sourcePath) {
    zc::Maybe<zc::String> sourceBytes = readSourceFile(sourcePath);
    ZC_IF_SOME(source, sourceBytes) {
      source::SourceManager sourceManager;
      basic::StringPool stringPool;
      diagnostics::DiagnosticEngine diagnosticEngine(sourceManager);
      basic::LangOptions options;
      auto bufferId = sourceManager.addMemBufferCopy(source.asBytes(), sourcePath);
      lexer::Lexer lexer(sourceManager, diagnosticEngine, options, stringPool, bufferId);
      zc::Vector<lexer::Token> tokens;
      lexer::Token token;
      do {
        lexer.lex(token);
        tokens.add(token);
      } while (token.getKind() != ast::SyntaxKind::EndOfFile);

      auto bufferBytes = sourceManager.getEntireTextForBuffer(bufferId);
      auto built = cst::buildLexemeStreamFromTokens(bufferBytes, tokens.asPtr());
      if (!built.is<cst::VerifiedLexemeStream>()) { return zc::none; }
      const cst::VerifiedLexemeStream& stream = built.get<cst::VerifiedLexemeStream>();

      auto result = format::normalizeTriviaWhitespace(stream);
      zc::String canonical = result.apply(source);
      const bool changed = canonical != source;
      return FormattedSource{zc::mv(canonical), changed};
    }
    return zc::none;
  }

  // Reads a source file into a String, resolving absolute and relative paths the
  // same way the object writer does. Returns none if the file cannot be opened.
  ZC_NODISCARD zc::Maybe<zc::String> readSourceFile(zc::StringPtr sourcePath) {
    zc::Maybe<zc::String> contents;
    auto exception = zc::runCatchingExceptions([&]() {
      auto filesystem = zc::newDiskFilesystem();
      const bool isAbsolute = sourcePath.size() > 0 && sourcePath[0] == '/';
      const zc::Directory& baseDir = isAbsolute ? filesystem->getRoot() : filesystem->getCurrent();
      const zc::StringPtr pathText = isAbsolute ? sourcePath.slice(1) : sourcePath;
      auto file = baseDir.openFile(zc::Path::parse(pathText));
      contents = file->readAllText();
    });
    if (exception != zc::none) { return zc::none; }
    return contents;
  }

  // Replaces a source file with the formatted bytes, resolving paths like the
  // object writer. Returns false if the write fails.
  ZC_NODISCARD bool writeSourceFile(zc::StringPtr sourcePath, zc::StringPtr text) {
    bool ok = false;
    auto exception = zc::runCatchingExceptions([&]() {
      auto filesystem = zc::newDiskFilesystem();
      const bool isAbsolute = sourcePath.size() > 0 && sourcePath[0] == '/';
      const zc::Directory& baseDir = isAbsolute ? filesystem->getRoot() : filesystem->getCurrent();
      const zc::StringPtr pathText = isAbsolute ? sourcePath.slice(1) : sourcePath;
      auto file = baseDir.openFile(zc::Path::parse(pathText),
                                   zc::WriteMode::MODIFY | zc::WriteMode::CREATE);
      file->writeAll(text);
      ok = true;
    });
    if (exception != zc::none) { return false; }
    return ok;
  }

  void addPackageSelectionOptions(zc::MainBuilder& builder) {
    builder
        .addOptionWithArg({"manifest-path"}, ZC_BIND_METHOD(*this, setManifestPath),
                          "<path-to-Zom.toml>", "Select the package manifest.")
        .addOptionWithArg({"package"}, ZC_BIND_METHOD(*this, addPackageSelection), "<name>",
                          "Select one workspace package.")
        .addOption({"lib"}, ZC_BIND_METHOD(*this, addLibraryTarget),
                   "Compile the package library target.")
        .addOptionWithArg({"bin"}, ZC_BIND_METHOD(*this, addBinaryTarget), "<name>",
                          "Compile a named binary target.")
        .addOptionWithArg({"test"}, ZC_BIND_METHOD(*this, addTestTarget), "<name>",
                          "Compile a named test target.")
        .addOptionWithArg({"bench"}, ZC_BIND_METHOD(*this, addBenchmarkTarget), "<name>",
                          "Compile a named benchmark target.")
        .addOptionWithArg({"example"}, ZC_BIND_METHOD(*this, addExampleTarget), "<name>",
                          "Compile a named example target.")
        .addOptionWithArg({"features"}, ZC_BIND_METHOD(*this, addFeatureList), "<name[,name...]>",
                          "Enable package features.")
        .addOption({"no-default-features"}, ZC_BIND_METHOD(*this, disableDefaultFeatures),
                   "Disable the package default feature set.")
        .addOptionWithArg({"target"}, ZC_BIND_METHOD(*this, addTargetProfile), "<profile>",
                          "Select a registered target profile.")
        .addOption({"locked"}, ZC_BIND_METHOD(*this, enableLockedMode),
                   "Require the existing canonical lock graph.")
        .addOption({"update-lock"}, ZC_BIND_METHOD(*this, enableUpdateLockMode),
                   "Resolve and atomically update Zom.lock.");
  }

  void addCompilationPolicyOptions(zc::MainBuilder& builder) {
    builder
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
        .expectZeroOrMoreArgs("<source>", ZC_BIND_METHOD(*this, rejectPositionalSource))
        .callAfterParsing(ZC_BIND_METHOD(*this, emitOutput));
  }

  void addCompileOptions(zc::MainBuilder& builder) {
    addPackageSelectionOptions(builder);
    builder
        .addOptionWithArg({'o', "output"}, ZC_BIND_METHOD(*this, addOutput), "<dir>",
                          "Specify the output directory or file path.")
        .addOptionWithArg({"emit"}, ZC_BIND_METHOD(*this, setEmitType), "<type>",
                          "Set output type: ast, dispatch, binary (default: binary)")
        .addOptionWithArg({"ast-format"}, ZC_BIND_METHOD(*this, setASTDumpFormat), "<format>",
                          "Set AST dump format: tree, json, raw (default: tree)")
        .addOption({"dump-ast"}, ZC_BIND_METHOD(*this, enableASTDump),
                   "Dump AST to stdout (shorthand for --emit=ast)")
        .addOption({"dump-dispatch"}, ZC_BIND_METHOD(*this, enableDispatchDump),
                   "Dump checked call dispatch records to stdout (shorthand for --emit=dispatch)")
        .addOption({"check"}, ZC_BIND_METHOD(*this, enableCheck),
                   "Check sources without emitting an artifact");
    addCompilationPolicyOptions(builder);
  }

  void addBuildOptions(zc::MainBuilder& builder) {
    addPackageSelectionOptions(builder);
    addCompilationPolicyOptions(builder);
  }

  // =====================================================================================
  // "compile" command

  zc::MainBuilder::Validity rejectPositionalSource(zc::StringPtr value) {
    packageRequest.positionalArguments.add(zc::str(value));
    return true;
  }

  zc::MainBuilder::Validity setManifestPath(zc::StringPtr value) {
    manifestPaths.add(zc::str(value));
    return true;
  }

  zc::MainBuilder::Validity addPackageSelection(zc::StringPtr value) {
    packageRequest.packageSelections.add(zc::str(value));
    return true;
  }

  zc::MainBuilder::Validity addLibraryTarget() {
    zc::Maybe<identity::TargetName> noName;
    packageRequest.targetSelections.add(
        package::RequestedTargetSelection(identity::CrateTargetKind::Library, zc::mv(noName)));
    return true;
  }

  zc::MainBuilder::Validity addNamedTarget(identity::CrateTargetKind kind, zc::StringPtr value) {
    auto name = identity::TargetName::fromSource(value);
    if (name == zc::none) {
      package::PackageDiagnosticAdapter::emitInvocationIssue(
          session->getDiagnosticEngine(), package::InvocationIssue::DuplicateTargetSelection);
      return zc::str("Invalid target name.");
    }
    ZC_IF_SOME(admitted, name) {
      zc::Maybe<identity::TargetName> selected = zc::mv(admitted);
      packageRequest.targetSelections.add(
          package::RequestedTargetSelection(kind, zc::mv(selected)));
    }
    return true;
  }

  zc::MainBuilder::Validity addBinaryTarget(zc::StringPtr value) {
    return addNamedTarget(identity::CrateTargetKind::Binary, value);
  }
  zc::MainBuilder::Validity addTestTarget(zc::StringPtr value) {
    return addNamedTarget(identity::CrateTargetKind::Test, value);
  }
  zc::MainBuilder::Validity addBenchmarkTarget(zc::StringPtr value) {
    return addNamedTarget(identity::CrateTargetKind::Benchmark, value);
  }
  zc::MainBuilder::Validity addExampleTarget(zc::StringPtr value) {
    return addNamedTarget(identity::CrateTargetKind::Example, value);
  }

  zc::MainBuilder::Validity addFeatureList(zc::StringPtr value) {
    packageRequest.featureLists.add(zc::str(value));
    return true;
  }
  zc::MainBuilder::Validity disableDefaultFeatures() {
    packageRequest.useDefaultFeatures = false;
    return true;
  }
  zc::MainBuilder::Validity addTargetProfile(zc::StringPtr value) {
    packageRequest.targetProfiles.add(zc::str(value));
    return true;
  }
  zc::MainBuilder::Validity enableLockedMode() {
    ++packageRequest.lockedCount;
    return true;
  }
  zc::MainBuilder::Validity enableUpdateLockMode() {
    ++packageRequest.updateLockCount;
    return true;
  }

  zc::MainBuilder::Validity addOutput(zc::StringPtr spec) {
    if (action == CompilationAction::FrontendOnly) {
      return "Cannot combine --check with an output selector.";
    }
    outputActionRequested = true;
    compilerOpts.emission.outputPath = zc::str(spec);
    return true;
  }

  zc::MainBuilder::Validity setEmitType(zc::StringPtr type) {
    if (action == CompilationAction::FrontendOnly) {
      return "Cannot combine --check with an output selector.";
    }
    outputActionRequested = true;
    if (type == "ast") {
      compilerOpts.emission.outputType = basic::CompilerOptions::EmissionOptions::OutputType::AST;
    } else if (type == "dispatch") {
      compilerOpts.emission.outputType =
          basic::CompilerOptions::EmissionOptions::OutputType::Dispatch;
    } else if (type == "binary") {
      compilerOpts.emission.outputType =
          basic::CompilerOptions::EmissionOptions::OutputType::Binary;
    } else {
      return zc::str("Invalid output type: ", type, ". Valid types are: ast, dispatch, binary");
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
    if (action == CompilationAction::FrontendOnly) {
      return "Cannot combine --check with an output selector.";
    }
    outputActionRequested = true;
    compilerOpts.emission.outputType = basic::CompilerOptions::EmissionOptions::OutputType::AST;
    return true;
  }

  zc::MainBuilder::Validity enableDispatchDump() {
    if (action == CompilationAction::FrontendOnly) {
      return "Cannot combine --check with an output selector.";
    }
    outputActionRequested = true;
    compilerOpts.emission.outputType =
        basic::CompilerOptions::EmissionOptions::OutputType::Dispatch;
    return true;
  }

  zc::MainBuilder::Validity enableCheck() {
    if (outputActionRequested) { return "Cannot combine --check with an output selector."; }
    action = CompilationAction::FrontendOnly;
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
    ++packageRequest.panicCount;
    packageRequest.panicStrategy = zc::str(strategy);
    if (strategy == "abort") {
      compilerOpts.panicStrategy = basic::CompilerOptions::PanicStrategy::Abort;
    } else if (strategy == "unwind") {
      compilerOpts.panicStrategy = basic::CompilerOptions::PanicStrategy::Unwind;
    } else {
      return true;
    }
    return true;
  }

  zc::MainBuilder::Validity disableUnicode() {
    langOpts.useUnicode = false;
    packageRequest.languageOptions.useUnicode = false;
    return true;
  }

  zc::MainBuilder::Validity enableDollarIdentifiers() {
    langOpts.allowDollarIdentifiers = true;
    packageRequest.languageOptions.allowDollarIdentifiers = true;
    return true;
  }

  zc::MainBuilder::Validity disableRegexLiterals() {
    langOpts.supportRegexLiterals = false;
    packageRequest.languageOptions.supportRegexLiterals = false;
    return true;
  }

  template <typename Scalar>
  static Scalar requireScalar(zc::StringPtr text) {
    auto result = Scalar::fromCanonical(text);
    ZC_IF_SOME(value, result) { return zc::mv(value); }
    ZC_UNREACHABLE;
  }

  static identity::CanonicalTargetSpecificationKey hostSemanticProjection() {
    const auto host = hostTargetConfiguration();
    zc::Vector<identity::TargetFeatureName> features;
    auto sortedFeatures = identity::SortedTargetFeatureSet::from(zc::mv(features));
    ZC_IF_SOME(featureValues, sortedFeatures) {
      auto projection = identity::CanonicalTargetSpecificationKey::from(
          requireScalar<identity::TargetComponentName>(host.architecture),
          requireScalar<identity::TargetComponentName>(host.vendor),
          requireScalar<identity::TargetComponentName>(host.operatingSystem),
          requireScalar<identity::TargetComponentName>(host.environment),
          requireScalar<identity::TargetComponentName>("zom"_zc),
          static_cast<uint32_t>(sizeof(void*) * 8), identity::Endianness::Little,
          zc::mv(featureValues));
      ZC_IF_SOME(value, projection) { return zc::mv(value); }
    }
    ZC_UNREACHABLE;
  }

  static ir::TargetRegistrySnapshot targetRegistry() {
    const auto host = hostTargetConfiguration();
    zc::Vector<ir::CanonicalTargetFeature> backendFeatures;
    auto specification = ir::CanonicalTargetSpec::from(
        host.triple, host.dataLayout, "generic"_zc, zc::mv(backendFeatures), "zom"_zc,
        ir::BackendPanicStrategy::Abort, host.objectFormat);
    auto name = package::RegisteredTargetProfileName::from("host"_zc);
    ZC_IF_SOME(profileName, name) {
      ZC_IF_SOME(specificationValue, specification) {
        zc::Vector<identity::TargetFeatureName> semanticFeatures;
        zc::Vector<ir::CanonicalTargetSpec> specifications;
        specifications.add(zc::mv(specificationValue));
        auto profile = ir::RegisteredTargetProfileRecord::from(
            profileName.clone(), hostSemanticProjection(), zc::mv(semanticFeatures),
            zc::mv(specifications));
        ZC_IF_SOME(profileValue, profile) {
          zc::Vector<ir::RegisteredTargetProfileRecord> profiles;
          profiles.add(zc::mv(profileValue));
          auto registry = ir::TargetRegistrySnapshot::from(zc::mv(profileName), zc::mv(profiles));
          ZC_IF_SOME(value, registry) { return zc::mv(value); }
        }
      }
    }
    ZC_UNREACHABLE;
  }

  static package::RegisteredTargetService packageTargetService(
      const ir::TargetRegistrySnapshot& registry) {
    auto service = registry.packageTargetService();
    ZC_IF_SOME(value, service) { return zc::mv(value); }
    ZC_UNREACHABLE;
  }

  zc::MainBuilder::Validity diagnoseTargetSelectionIssue(
      ir::TargetSelectionVerificationIssue issue,
      package::PackagePanicStrategy requestedPanicStrategy) {
    if (issue == ir::TargetSelectionVerificationIssue::CapabilityUnavailable) {
      if (requestedPanicStrategy == package::PackagePanicStrategy::Unwind) {
        return diagnoseEmission<diagnostics::DiagID::PanicUnwindUnsupported>(source::SourceLoc());
      }
      return diagnoseEmission<diagnostics::DiagID::TargetCapabilityUnavailable>(
          source::SourceLoc());
    }
    session->getDiagnosticEngine().diagnose<diagnostics::DiagID::LirInvariant>(
        source::SourceLoc(), zc::str(uint64_t{1}));
    context.error(zc::StringPtr());
    return true;
  }

  static identity::CanonicalWorkspaceRelativePath workspacePath(zc::PathPtr path) {
    zc::Vector<identity::CanonicalPathSegment> segments(path.size());
    for (const auto& component : path) {
      auto admitted = identity::CanonicalPathSegment::fromSource(component);
      ZC_IF_SOME(value, admitted) {
        segments.add(zc::mv(value));
      } else {
        ZC_UNREACHABLE;
      }
    }
    return identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
  }

  static zc::Maybe<package::PackageDiagnosticDocument> packageDiagnosticDocument(
      identity::CanonicalWorkspaceRelativePath&& path, zc::ArrayPtr<const zc::byte> source) {
    auto digest = identity::sha256(source);
    if (digest == zc::none) { return zc::none; }
    ZC_IF_SOME(digestValue, digest) {
      auto key = package::InputDocumentKey::from(
          package::InputDocumentKind::Manifest,
          package::DiagnosticDocumentPath::workspace(zc::mv(path)), digestValue);
      ZC_IF_SOME(keyValue, key) {
        return package::PackageDiagnosticDocument::from(zc::mv(keyValue), source);
      }
    }
    return zc::none;
  }

  static zc::Path filesystemPath(const identity::CanonicalWorkspaceRelativePath& path) {
    zc::Path result(nullptr);
    for (uint32_t index = 0; index < path.leadingParents(); ++index) {
      result = zc::mv(result).eval(".."_zc);
    }
    for (const auto& segment : path.segments()) { result = zc::mv(result).append(segment.text()); }
    return result;
  }

  static zc::Path filesystemPath(const identity::CanonicalRelativePath& path) {
    zc::Path result(nullptr);
    for (const auto& segment : path.segments()) { result = zc::mv(result).append(segment.text()); }
    return result;
  }

  zc::Maybe<source::core::VerifiedCoreDistribution> admitCoreDistribution(
      const zc::Filesystem& filesystem) const {
    try {
      auto executable = currentExecutablePath(filesystem);
      if (executable == zc::none) { return zc::none; }
      const auto coreRoot = ZC_ASSERT_NONNULL(executable)
                                .parent()
                                .parent()
                                .append(zc::Path({"share"_zc, "zom"_zc, "core"_zc, "src"_zc}));
      auto directory = filesystem.getRoot().tryOpenSubdir(coreRoot);
      auto expected = source::core::initialCoreDistributionInput();
      if (directory == zc::none || expected == zc::none) { return zc::none; }
      TransientCoreSourceDirectoryFactory factory;
      source::core::CoreDistributionAdmission admission;
      ZC_IF_SOME(root, directory) {
        ZC_IF_SOME(authority, expected) {
          auto admitted = admission.admit(*root, factory, authority, 2026);
          if (admitted.is<source::core::VerifiedCoreDistribution>()) {
            return zc::mv(admitted.get<source::core::VerifiedCoreDistribution>());
          }
        }
      }
      return zc::none;
    } catch (const zc::Exception&) { return zc::none; }
  }

  static zc::Maybe<package::PackageSourceInventory> inventory(const zc::ReadableDirectory& root) {
    zc::Vector<identity::CanonicalRelativePath> files;
    zc::Vector<zc::Path> pending;
    pending.add(zc::Path(nullptr));
    while (pending.size() != 0) {
      auto relativeDirectory = zc::mv(pending.back());
      pending.removeLast();
      zc::Own<const zc::ReadableDirectory> directory =
          relativeDirectory.size() == 0 ? root.clone() : root.openSubdir(relativeDirectory);
      for (auto& entry : directory->listEntries()) {
        auto path = relativeDirectory.clone().append(zc::mv(entry.name));
        if (entry.type == zc::FsNode::Type::DIRECTORY) {
          pending.add(zc::mv(path));
          continue;
        }
        if (entry.type != zc::FsNode::Type::FILE) { continue; }
        zc::Vector<identity::CanonicalPathSegment> segments(path.size());
        bool valid = true;
        for (const auto& component : path) {
          auto admitted = identity::CanonicalPathSegment::fromSource(component);
          if (admitted == zc::none) {
            valid = false;
            break;
          }
          ZC_IF_SOME(value, admitted) { segments.add(zc::mv(value)); }
        }
        if (valid) { files.add(identity::CanonicalRelativePath::from(zc::mv(segments))); }
      }
    }
    return package::PackageSourceInventory::from(zc::mv(files));
  }

  zc::OneOf<zc::Path, package::InvocationIssue> discoverManifestPath(
      const zc::Filesystem& filesystem) const {
    try {
      if (manifestPaths.size() == 1) {
        auto path = filesystem.getCurrentPath().eval(manifestPaths[0]);
        if (path.basename().size() != 1 || path.basename()[0] != "Zom.toml"_zc) {
          return package::InvocationIssue::InvalidManifestPath;
        }
        auto metadata = filesystem.getRoot().tryLstat(path);
        if (metadata == zc::none) { return package::InvocationIssue::InvalidManifestPath; }
        ZC_IF_SOME(value, metadata) {
          if (value.type != zc::FsNode::Type::FILE) {
            return package::InvocationIssue::InvalidManifestPath;
          }
        }
        return zc::mv(path);
      }
      auto directory = filesystem.getCurrentPath().clone();
      for (;;) {
        auto candidate = directory.clone().append("Zom.toml"_zc);
        auto metadata = filesystem.getRoot().tryLstat(candidate);
        ZC_IF_SOME(value, metadata) {
          if (value.type == zc::FsNode::Type::FILE) { return zc::mv(candidate); }
        }
        if (directory.size() == 0) { break; }
        directory = zc::mv(directory).parent();
      }
    } catch (const zc::Exception&) { return package::InvocationIssue::InvalidManifestPath; }
    return package::InvocationIssue::ManifestNotFound;
  }

  struct LoadedWorkspace final {
    package::NormalizedWorkspace workspace;
    zc::Path rootPath;
    zc::Vector<package::PackageDiagnosticDocument> diagnosticDocuments;
  };

  zc::Maybe<LoadedWorkspace> loadWorkspace(const zc::Filesystem& filesystem,
                                           zc::Path&& manifestPath) {
    try {
      auto rootPath = manifestPath.parent().clone();
      auto rootDirectory = filesystem.getRoot().openSubdir(rootPath);
      auto rootSource = rootDirectory->openFile(zc::Path("Zom.toml"_zc))->readAllText();
      auto rootInventory = inventory(*rootDirectory);
      if (rootInventory == zc::none) { return zc::none; }
      ZC_IF_SOME(rootInventoryValue, rootInventory) {
        zc::Vector<package::PackageDiagnosticDocument> diagnosticDocuments;
        auto rootDiagnosticDocument =
            packageDiagnosticDocument(workspacePath(zc::Path("Zom.toml"_zc)), rootSource.asBytes());
        if (rootDiagnosticDocument == zc::none) { return zc::none; }
        ZC_IF_SOME(document, rootDiagnosticDocument) { diagnosticDocuments.add(zc::mv(document)); }
        package::ManifestParser parser;
        auto parsed = parser.parseWorkspaceManifest(workspacePath(zc::Path("Zom.toml"_zc)),
                                                    rootSource, rootInventoryValue);
        if (parsed.is<package::ManifestFailure>()) { return zc::none; }
        const auto& rootManifest = parsed.get<package::NormalizedManifest>();
        zc::Vector<package::WorkspaceMemberInput> members;
        if (rootManifest.hasWorkspace()) {
          for (const auto& memberPath : rootManifest.workspaceMembers()) {
            auto relative = filesystemPath(memberPath);
            auto memberDirectory = rootDirectory->openSubdir(relative);
            auto memberSource = memberDirectory->openFile(zc::Path("Zom.toml"_zc))->readAllText();
            auto memberInventory = inventory(*memberDirectory);
            if (memberInventory == zc::none) { return zc::none; }
            ZC_IF_SOME(memberInventoryValue, memberInventory) {
              auto memberManifestPath = relative.clone().append("Zom.toml"_zc);
              auto memberDiagnosticDocument = packageDiagnosticDocument(
                  workspacePath(memberManifestPath), memberSource.asBytes());
              if (memberDiagnosticDocument == zc::none) { return zc::none; }
              ZC_IF_SOME(document, memberDiagnosticDocument) {
                diagnosticDocuments.add(zc::mv(document));
              }
              members.add(package::WorkspaceMemberInput::from(
                  memberPath.clone(), zc::mv(memberSource), zc::mv(memberInventoryValue)));
            }
          }
        }
        auto normalized =
            package::normalizeWorkspace(rootSource, rootInventoryValue, zc::mv(members));
        if (normalized.is<package::NormalizedWorkspace>()) {
          return LoadedWorkspace{zc::mv(normalized.get<package::NormalizedWorkspace>()),
                                 zc::mv(rootPath), zc::mv(diagnosticDocuments)};
        }
      }
    } catch (const zc::Exception&) { return zc::none; }
    return zc::none;
  }

  static identity::PackageBaseKey packageBase(zc::MemoryResource& resource,
                                              const identity::PackageKey& packageKey) {
    auto name = identity::PackageName::fromCanonical(resource, packageKey.name());
    auto version = identity::ResolvedVersion::fromCanonical(resource, packageKey.version());
    ZC_IF_SOME(nameValue, name) {
      ZC_IF_SOME(versionValue, version) {
        return identity::PackageBaseKey::from(packageKey.source().clone(resource),
                                              zc::mv(nameValue), zc::mv(versionValue));
      }
    }
    ZC_UNREACHABLE;
  }

  static identity::SortedFeatureSet packageFeatures(zc::MemoryResource& resource,
                                                    const identity::PackageKey& packageKey) {
    zc::Vector<identity::FeatureName> features(resource, packageKey.features().size());
    for (const auto& feature : packageKey.features()) { features.add(feature.clone(resource)); }
    auto result = identity::SortedFeatureSet::from(zc::mv(features));
    ZC_IF_SOME(value, result) { return zc::mv(value); }
    ZC_UNREACHABLE;
  }

  zc::Maybe<driver::VerifiedPackageSessionInput> resolvePackageInput(
      const zc::Filesystem& filesystem, zc::PathPtr workspaceRoot,
      const package::NormalizedPackageCompilationRequest& normalizedRequest,
      package::VerifiedPackageCompilationRequest&& request,
      ir::VerifiedTargetSelection&& verifiedHostTarget,
      ir::VerifiedTargetSelection&& verifiedTarget, const package::NormalizedWorkspace& workspace) {
    auto& resolverMemory = session->getPackageResolutionMemoryResource();
    zc::Vector<package::ResolverRelease> releases(resolverMemory);
    zc::Vector<package::ResolvedPackageSourceSnapshot> snapshots;
    auto snapshotParent =
        filesystem.getRoot().tryOpenSubdir(workspaceRoot.parent(), zc::WriteMode::MODIFY);
    if (snapshotParent == zc::none) { return zc::none; }
    zc::Own<const zc::Directory> snapshotParentDirectory;
    ZC_IF_SOME(directory, snapshotParent) { snapshotParentDirectory = zc::mv(directory); }
    package::ReplacementFreshSourceDirectoryFactory factory(*snapshotParentDirectory);

    auto admitPackage = [&](const package::NormalizedManifest& manifest,
                            identity::CanonicalWorkspaceRelativePath&& relativePath) -> bool {
      auto name = identity::PackageName::fromCanonical(manifest.packageName());
      auto version = identity::ResolvedVersion::fromCanonical(manifest.packageVersion());
      if (name == zc::none || version == zc::none) { return false; }
      identity::PackageBaseKey base = packageBase(resolverMemory, request.roots()[0].packageKey());
      ZC_IF_SOME(nameValue, name) {
        ZC_IF_SOME(versionValue, version) {
          base = identity::PackageBaseKey::from(
              identity::CanonicalPackageSource::localPath(relativePath.clone()), zc::mv(nameValue),
              zc::mv(versionValue));
        }
      }
      auto packageRoot = workspaceRoot.clone().eval(filesystemPath(relativePath).toString());
      auto directory = filesystem.getRoot().openSubdir(packageRoot);
      package::SourceDirectoryMaterializer materializer;
      auto snapshot = materializer.materialize(*directory, factory);
      if (snapshot.is<package::MaterializationIssue>()) {
        package::PackageDiagnosticAdapter::emitMaterializationIssue(
            session->getDiagnosticEngine(), snapshot.get<package::MaterializationIssue>());
        return false;
      }
      auto& snapshotValue = snapshot.get<package::DigestVerifiedSourceSnapshot>();
      auto record =
          package::LocalPackageRecord::from(base.clone(), manifest.clone(), snapshotValue);
      if (record == zc::none) { return false; }
      ZC_IF_SOME(recordValue, record) {
        releases.add(package::ResolverRelease::fromLocal(resolverMemory, recordValue));
      }
      snapshots.add(
          package::ResolvedPackageSourceSnapshot::from(base.clone(), zc::mv(snapshotValue)));
      return true;
    };

    if (workspace.root().hasPackage()) {
      zc::Vector<identity::CanonicalPathSegment> noSegments;
      if (!admitPackage(workspace.root(),
                        identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(noSegments)))) {
        return zc::none;
      }
    }
    for (const auto& member : workspace.members()) {
      if (!admitPackage(member.manifest(), member.packageDirectory().clone())) { return zc::none; }
    }

    const auto workspaceDirectory =
        filesystem.getRoot().openSubdir(workspaceRoot, zc::WriteMode::MODIFY);
    bool includeDevelopment = false;
    for (const auto& target : normalizedRequest.requestedTargets()) {
      includeDevelopment = includeDevelopment || target.kind == identity::CrateTargetKind::Test ||
                           target.kind == identity::CrateTargetKind::Benchmark ||
                           target.kind == identity::CrateTargetKind::Example;
    }
    zc::Vector<package::ResolverRoot> roots(resolverMemory);
    roots.add(package::ResolverRoot::from(
        packageBase(resolverMemory, request.roots()[0].packageKey()),
        packageFeatures(resolverMemory, request.roots()[0].packageKey()), false,
        includeDevelopment));
    const bool useLocked =
        normalizedRequest.lockMode() == package::PackageLockMode::LockedOnly ||
        (normalizedRequest.lockMode() == package::PackageLockMode::PreferLocked &&
         workspaceDirectory->exists(zc::Path("Zom.lock"_zc)));
    if (useLocked) {
      auto locked = package::LockfileCodec::read(*workspaceDirectory);
      if (locked.is<package::LockIssue>()) { return zc::none; }
      const auto& lockedGraph = locked.get<package::VerifiedLockGraph>();
      package::LockReplayMetrics metrics;
      auto resolved = package::PackageResolver::resolveLocked(resolverMemory, roots, releases,
                                                              lockedGraph, metrics);
      if (resolved.is<package::PackageResolverFailure>() || metrics.solverInvocations != 0) {
        return zc::none;
      }
      auto& graph = resolved.get<package::ResolutionOutput>();
      zc::Vector<package::ResolvedPackageSourceSnapshot> selectedSnapshots;
      for (auto& snapshot : snapshots) {
        for (const auto& selected : graph.packages()) {
          identity::CanonicalEncoder snapshotEncoder(resolverMemory);
          identity::CanonicalEncoder selectedEncoder(resolverMemory);
          snapshot.package().encode(snapshotEncoder);
          packageBase(resolverMemory, selected.key()).encode(selectedEncoder);
          if (snapshotEncoder.finish().asPtr() == selectedEncoder.finish().asPtr()) {
            selectedSnapshots.add(zc::mv(snapshot));
            break;
          }
        }
      }
      return driver::VerifiedPackageSessionInput::from(zc::mv(request), zc::mv(verifiedHostTarget),
                                                       zc::mv(verifiedTarget), zc::mv(graph),
                                                       zc::mv(selectedSnapshots));
    }

    auto resolved = package::PackageResolver::resolve(resolverMemory, roots, releases);
    if (resolved.is<package::PackageResolverFailure>()) { return zc::none; }
    auto& graph = resolved.get<package::ResolutionOutput>();
    if (normalizedRequest.lockMode() == package::PackageLockMode::Update) {
      const auto canonical = package::LockfileCodec::write(graph.lockGraph());
      if (package::AtomicLockfileWriter::write(*workspaceDirectory, canonical) != zc::none) {
        return zc::none;
      }
    }
    zc::Vector<package::ResolvedPackageSourceSnapshot> selectedSnapshots;
    for (auto& snapshot : snapshots) {
      for (const auto& selected : graph.packages()) {
        identity::CanonicalEncoder snapshotEncoder(resolverMemory);
        identity::CanonicalEncoder selectedEncoder(resolverMemory);
        snapshot.package().encode(snapshotEncoder);
        packageBase(resolverMemory, selected.key()).encode(selectedEncoder);
        if (snapshotEncoder.finish().asPtr() == selectedEncoder.finish().asPtr()) {
          selectedSnapshots.add(zc::mv(snapshot));
          break;
        }
      }
    }
    return driver::VerifiedPackageSessionInput::from(zc::mv(request), zc::mv(verifiedHostTarget),
                                                     zc::mv(verifiedTarget), zc::mv(graph),
                                                     zc::mv(selectedSnapshots));
  }

  zc::MainBuilder::Validity emitOutput() {
    auto result = emitOutputImpl();
    ZC_IF_SOME(issue, session->finishResolvedPackageSnapshots()) {
      package::PackageDiagnosticAdapter::emitMaterializationIssue(session->getDiagnosticEngine(),
                                                                  issue);
      context.error(zc::StringPtr());
      return true;
    }
    return result;
  }

  zc::MainBuilder::Validity emitOutputImpl() {
    if (manifestPaths.size() > 1) {
      package::PackageDiagnosticAdapter::emitInvocationIssue(
          session->getDiagnosticEngine(), package::InvocationIssue::InvalidManifestPath);
      context.error(zc::StringPtr());
      return true;
    }
    auto registry = targetRegistry();
    auto targets = packageTargetService(registry);
    auto normalized = package::normalizePackageCompilationRequest(zc::mv(packageRequest), targets);
    if (normalized.is<package::InvocationIssue>()) {
      package::PackageDiagnosticAdapter::emitInvocationIssue(
          session->getDiagnosticEngine(), normalized.get<package::InvocationIssue>());
      context.error(zc::StringPtr());
      return true;
    }
    const auto& normalizedRequest = normalized.get<package::NormalizedPackageCompilationRequest>();
    auto verifiedHostTarget = registry.verify(normalizedRequest.hostTarget());
    if (verifiedHostTarget.is<ir::TargetSelectionVerificationIssue>()) {
      return diagnoseTargetSelectionIssue(
          verifiedHostTarget.get<ir::TargetSelectionVerificationIssue>(),
          normalizedRequest.hostTarget().panicStrategy());
    }
    auto verifiedTarget = registry.verify(normalizedRequest.target());
    if (verifiedTarget.is<ir::TargetSelectionVerificationIssue>()) {
      return diagnoseTargetSelectionIssue(
          verifiedTarget.get<ir::TargetSelectionVerificationIssue>(),
          normalizedRequest.target().panicStrategy());
    }
    auto filesystem = zc::newDiskFilesystem();
    auto manifest = discoverManifestPath(*filesystem);
    if (manifest.is<package::InvocationIssue>()) {
      package::PackageDiagnosticAdapter::emitInvocationIssue(
          session->getDiagnosticEngine(), manifest.get<package::InvocationIssue>());
      context.error(zc::StringPtr());
      return true;
    }
    auto loaded = loadWorkspace(*filesystem, zc::mv(manifest.get<zc::Path>()));
    if (loaded == zc::none) {
      context.error("Package manifest or workspace normalization failed."_zc);
      return true;
    }
    ZC_IF_SOME(workspace, loaded) {
      auto verified = package::verifyPackageCompilationRequest(
          normalized.get<package::NormalizedPackageCompilationRequest>(), workspace.workspace);
      if (verified.is<package::TargetSelectionIssue>()) {
        session->getDiagnosticEngine().diagnose<diagnostics::DiagID::PackageTargetSelectionInvalid>(
            source::SourceLoc(),
            zc::str(static_cast<uint64_t>(verified.get<package::TargetSelectionIssue>())));
        context.error(zc::StringPtr());
        return true;
      }
      if (verified.is<package::PackageToolchainModuleRootFailure>()) {
        const auto& failure = verified.get<package::PackageToolchainModuleRootFailure>();
        ZC_REQUIRE(
            package::PackageDiagnosticAdapter::emitToolchainModuleRootFailure(
                session->getDiagnosticEngine(), workspace.diagnosticDocuments.asPtr(), failure),
            "verified package reservation failure must resolve its retained manifest");
        context.error(zc::StringPtr());
        return true;
      }
      auto& verifiedRequest = verified.get<package::VerifiedPackageCompilationRequest>();
      auto packageInput = resolvePackageInput(
          *filesystem, workspace.rootPath, normalizedRequest, zc::mv(verifiedRequest),
          zc::mv(verifiedHostTarget.get<ir::VerifiedTargetSelection>()),
          zc::mv(verifiedTarget.get<ir::VerifiedTargetSelection>()), workspace.workspace);
      if (packageInput == zc::none) {
        context.error("Failed to resolve and verify the atomic package session input."_zc);
        return true;
      }
      ZC_IF_SOME(input, packageInput) {
        if (!session->installVerifiedPackageInput(zc::mv(input))) {
          context.error("Failed to install the atomic package session input."_zc);
          return true;
        }
      }
      coreDistribution = admitCoreDistribution(*filesystem);
      bool coreInstalled = false;
      ZC_IF_SOME(distribution, coreDistribution) {
        coreInstalled = session->installVerifiedCoreDistribution(distribution);
      }
      if (!coreInstalled) {
        context.error("Failed to admit and install the source-backed core distribution."_zc);
        return true;
      }
      auto finalizedRoots = session->getFinalizedCompilationRoots();
      bool rootsAdmitted = finalizedRoots.size() != 0;
      for (const auto& root : finalizedRoots) {
        rootsAdmitted = session->addVerifiedPackageRoot(root) != zc::none && rootsAdmitted;
      }
      if (!rootsAdmitted) {
        package::PackageDiagnosticAdapter::emitBuildScriptIssue(
            session->getDiagnosticEngine(),
            package::BuildScriptIssue::BuildResultIntegrityViolation);
        context.error(zc::StringPtr());
        return true;
      }
    }

    const auto& options = session->getCompilerOptions();

    // 1. Parsing and structural discovery
    const bool frontendReady = session->parseSources();

    // 2. Early AST emission skips graph, binding, and checker failures after verified parsing.
    if (options.emission.outputType == basic::CompilerOptions::EmissionOptions::OutputType::AST &&
        session->hasVerifiedParsedSyntax()) {
      return emitAST();
    }
    if (!frontendReady || session->getDiagnosticEngine().hasErrors()) {
      return zc::str("Compilation failed during parsing or module discovery.");
    }

    // 3. Binding
    if (!session->bindSources() || session->getDiagnosticEngine().hasErrors()) {
      return zc::str("Compilation failed due to binding errors.");
    }

    // 4. Type checking
    if (!session->checkSources() || session->getDiagnosticEngine().hasErrors()) {
      return zc::str("Compilation failed due to type checking errors.");
    }

    if (action == CompilationAction::Run) {
#if ZOM_ENABLE_LLVM_BACKEND
      return runNativeExecutable();
#else
      return "The run command requires native code generation.";
#endif
    }
    if (action == CompilationAction::FrontendOnly) return true;

    // 5. Dispatch Dump
    if (options.emission.outputType ==
        basic::CompilerOptions::EmissionOptions::OutputType::Dispatch) {
      return emitDispatch();
    }

    // 6. Final Emission
    if (options.panicStrategy == basic::CompilerOptions::PanicStrategy::Unwind) {
      return diagnoseEmission<diagnostics::DiagID::PanicUnwindUnsupported>(emissionLocation());
    }
    switch (options.emission.outputType) {
      case basic::CompilerOptions::EmissionOptions::OutputType::Binary:
        return emitBinary();

      default:
        return zc::str("Unknown or unsupported output type specified.");
    }
  }

  zc::MainBuilder::Validity emitAST() {
    const auto modules = session->retainedParsedModules();
    if (modules.size() == 0) { return "Failed to retain verified parser results."; }
    const auto& options = session->getCompilerOptions();

    zc::Maybe<zc::Own<zc::OutputStream>> outputStream = createOutputStream(
        options.emission.outputPath, options.emission.astDumpFormat, DumpOutputKind::Ast);
    ZC_IF_SOME(stream, outputStream) {
      return dumpASTsToStream(*stream, modules, options.emission.astDumpFormat);
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
  enum class CompilationAction : uint8_t { Emit, FrontendOnly, Run };

  using ASTDumpFormat = basic::CompilerOptions::EmissionOptions::ASTDumpFormat;

  enum class DumpOutputKind {
    Ast,
    Dispatch,
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
    ZC_IF_SOME(name, maybeBaseName) {
      baseName = zc::mv(name);
    } else {
      baseName = zc::str(kDefaultBaseName);
    }
    zc::StringPtr extension;
    switch (kind) {
      case DumpOutputKind::Ast:
        extension = ast::astDumpFileExtension(toAstDumpFormat(format));
        break;
      case DumpOutputKind::Dispatch:
        extension = ".dispatch.txt"_zc;
        break;
    }

    return zc::str(baseName, extension);
  }

  /// Extracts base name from the first source file
  zc::Maybe<zc::String> extractSourceBaseName() {
    auto modules = session->materializeParsedModules();
    if (modules == zc::none || ZC_ASSERT_NONNULL(modules).size() == 0) { return zc::none; }

    const source::BufferId& firstBufferId = ZC_ASSERT_NONNULL(modules)[0].buffer();

    const auto& sourceManager = session->getSourceManager();
    zc::StringPtr filePath = sourceManager.getIdentifierForBuffer(firstBufferId);

    zc::Path sourcePath = zc::Path::parse(filePath);
    auto basenamePath = sourcePath.basename();

    if (basenamePath.size() == 0) return zc::none;

    zc::StringPtr filename = basenamePath[0];
    return filename.endsWith(".zom") ? zc::str(filename.slice(0, filename.size() - 4))
                                     : zc::str(filename);
  }

  zc::MainBuilder::Validity dumpASTsToStream(zc::OutputStream& outputStream, const auto& modules,
                                             ASTDumpFormat format) {
    for (const auto& module : modules) {
      const auto& parsedModule = module.parsedModule();
      if (parsedModule.source().crate().unit().kind() !=
          identity::CompilationUnitKind::UserPackage) {
        continue;
      }
      const auto syntax = parsedModule.sourceBackedSyntax();

      ZC_IF_SOME(error, ast::dumpTree(outputStream, syntax.tree(), syntax.sourceManager(),
                                      toAstDumpFormat(format))) {
        return zc::mv(error);
      }
    }

    return true;
  }

  zc::MainBuilder::Validity dumpDispatchToStream(zc::OutputStream&) {
    return "Verified checked dispatch facts are unavailable.";
  }

  source::SourceLoc emissionLocation() const {
    auto modules = session->materializeParsedModules();
    if (modules == zc::none || ZC_ASSERT_NONNULL(modules).size() == 0) {
      return source::SourceLoc();
    }

    const auto& parsedModule = ZC_ASSERT_NONNULL(modules)[0].parsedModule();
    auto location = parsedModule.sourceLocFor(parsedModule.rootSpan());
    ZC_IF_SOME(value, location) { return value; }
    return source::SourceLoc();
  }

  template <diagnostics::DiagID Id>
  zc::MainBuilder::Validity diagnoseEmission(source::SourceLoc loc) {
    session->getDiagnosticEngine().diagnose<Id>(loc);
    context.error(zc::StringPtr());
    return true;
  }

  zc::MainBuilder::Validity emitBinary() {
#if ZOM_ENABLE_LLVM_BACKEND
    return emitNativeObject();
#else
    return diagnoseEmission<diagnostics::DiagID::BinaryEmissionUnavailable>(emissionLocation());
#endif
  }

#if ZOM_ENABLE_LLVM_BACKEND
  using NativeObjectResult = zc::OneOf<zc::Array<uint8_t>, zc::String>;

  NativeObjectResult buildNativeObject() {
    const auto mirModules = session->getOwnershipCheckedMirModules();
    if (mirModules.size() != 1) {
      return NativeObjectResult(zc::str(
          "Binary emission currently supports exactly one module; got ", mirModules.size(), "."));
    }
    const auto functions = mirModules[0].builtMir().functions();
    if (functions.size() != 1 && functions.size() != 2 && functions.size() != 3) {
      return NativeObjectResult(
          zc::str("Binary emission currently supports one, two, or three functions; got ",
                  functions.size(), "."));
    }
    auto semanticTypes = session->getSemanticTypeStore();
    if (semanticTypes == zc::none) {
      return NativeObjectResult(zc::str("No semantic type store is available."));
    }
    // Select the MIR -> LIR lowering by module shape. A single module initializer
    // lowers through the scalar slice (RFC 0021 KR2.4); a single `Function` is
    // tried against the boolean-conditional diamond slice (KR5.2 C1), then the
    // reducible while-loop slice (KR5.2 C2), then the comparison-driven
    // conditional slice (KR5.2 C3), then the aggregate field-return slice (gap#2).
    // The three multi-block single-function slices have mutually exclusive shapes
    // (the diamond entry is a one-statement SwitchInt, the loop entry is a Goto,
    // the comparison entry is a three-statement SwitchInt on a computed boolean
    // temporary) and all require four blocks, while the aggregate field-return has
    // one block, so trying them in order selects at most one. A two-`Function`
    // module is tried against the same-module direct-call slices (KR5.2 C4/C5):
    // the caller (two blocks) and the callee (one block) are identified by block
    // count, requiring exactly one of each, and the deeper `lowerCallModule` /
    // `lowerCallModuleWithArgument` gates (including the call targeting the
    // identified callee and the argument count) fail-close any residual mismatch.
    // The multi-block and call slices keep their own parameterized symbols
    // (`zom.conditional`, `zom.loop`, `zom.conditional_cmp`, `zom.caller` /
    // `zom.callee`), so they produce a relocatable object only. The scalar and
    // aggregate field-return slices fold to the reserved no-argument `zom.module_init`
    // entry the runtime `_start` calls. Every other shape stays fail-closed here,
    // and `zomc run` remains gated on the host-compatibility path regardless.
    zc::Maybe<lir::LirModule> lir;
    ZC_IF_SOME(types, semanticTypes) {
      if (functions.size() == 1) {
        if (functions[0].kind == mir::MirFunctionKind::ModuleInitializer) {
          lir = lir::MirToLirLowering::lowerScalarInitializer(functions[0], types);
        } else if (functions[0].kind == mir::MirFunctionKind::Function) {
          lir = lir::MirToLirLowering::lowerConditionalReturn(functions[0], types);
          if (lir == zc::none) {
            lir = lir::MirToLirLowering::lowerLoopReturn(functions[0], types);
          }
          if (lir == zc::none) {
            lir = lir::MirToLirLowering::lowerEqualityConditionalReturn(functions[0], types);
          }
          if (lir == zc::none) {
            lir = lir::MirToLirLowering::lowerAggregateFieldInitializer(functions[0], types);
          }
        }
      } else if (functions.size() == 2) {
        // Two functions: identify the unique direct-call caller/callee pair by
        // block count. The caller is a `Function` with two blocks (entry Call,
        // continuation Return); the callee is a `Function` with one block. If both
        // roles are filled by exactly one distinct function, lower the pair --
        // first as a zero-argument call (KR5.2 C4), then a single-argument call
        // (KR5.2 C5), then a two-argument call (multi-argument slice). The three
        // lowerings' internal gates (callee local/parameter count, call argument
        // count, and the call targeting the identified callee) select at most one;
        // any ambiguity (same-shape pair, missing role) or residual mismatch leaves
        // `lir` as none.
        auto isCaller = [](const mir::MirFunction& fn) {
          return fn.kind == mir::MirFunctionKind::Function && fn.blocks.size() == 2;
        };
        auto isCallee = [](const mir::MirFunction& fn) {
          return fn.kind == mir::MirFunctionKind::Function && fn.blocks.size() == 1;
        };
        zc::Maybe<size_t> callerIndex;
        zc::Maybe<size_t> calleeIndex;
        for (size_t index = 0; index < functions.size(); ++index) {
          if (isCaller(functions[index])) {
            if (callerIndex != zc::none) {
              callerIndex = zc::none;
              break;
            }
            callerIndex = index;
          } else if (isCallee(functions[index])) {
            if (calleeIndex != zc::none) {
              calleeIndex = zc::none;
              break;
            }
            calleeIndex = index;
          }
        }
        ZC_IF_SOME(caller, callerIndex) {
          ZC_IF_SOME(callee, calleeIndex) {
            lir =
                lir::MirToLirLowering::lowerCallModule(functions[caller], functions[callee], types);
            if (lir == zc::none) {
              lir = lir::MirToLirLowering::lowerCallModuleWithArgument(functions[caller],
                                                                       functions[callee], types);
            }
            if (lir == zc::none) {
              lir = lir::MirToLirLowering::lowerCallModuleWithArguments(functions[caller],
                                                                        functions[callee], types);
            }
          }
        }
      } else {
        // Three functions: a single same-module direct-call caller plus its
        // callee plus one standalone leaf (S-call). Identify the unique caller by
        // the two-block Call+Return shape, locate its callee among the other two
        // functions by owner match (`call.callee == other.owner`), and require the
        // remaining third function to be the unique scalar leaf. The emitted LIR
        // module fixes the order [caller, callee, leaf], so the caller's call
        // index (1) is the callee's emission-order position, independent of the
        // MIR array order. Any ambiguity -- more than one caller-shaped function,
        // no or multiple owner-matched callees, or a non-leaf third function --
        // leaves `lir` as none and the module fails closed.
        auto isCaller = [](const mir::MirFunction& fn) {
          return fn.kind == mir::MirFunctionKind::Function && fn.blocks.size() == 2;
        };
        zc::Maybe<size_t> callerIndex;
        bool callerAmbiguous = false;
        for (size_t index = 0; index < functions.size(); ++index) {
          if (isCaller(functions[index])) {
            if (callerIndex != zc::none) {
              callerAmbiguous = true;
              break;
            }
            callerIndex = index;
          }
        }
        if (!callerAmbiguous) {
          ZC_IF_SOME(caller, callerIndex) {
            // The caller's single call terminator names the callee's owner. Locate
            // the unique other function whose owner matches; the last remaining
            // function is the leaf candidate.
            const auto& callerFn = functions[caller];
            zc::Maybe<size_t> calleeIndex;
            bool calleeAmbiguous = false;
            if (callerFn.blocks.size() == 2 &&
                callerFn.blocks[0].terminator.kind() == mir::MirTerminatorKind::Call) {
              const auto& call = callerFn.blocks[0].terminator.callValue();
              for (size_t index = 0; index < functions.size(); ++index) {
                if (index == caller) { continue; }
                if (functions[index].owner == call.callee) {
                  if (calleeIndex != zc::none) {
                    calleeAmbiguous = true;
                    break;
                  }
                  calleeIndex = index;
                }
              }
            }
            if (!calleeAmbiguous) {
              ZC_IF_SOME(callee, calleeIndex) {
                // The leaf is the one remaining function that is neither the caller
                // nor the owner-matched callee.
                zc::Maybe<size_t> leafIndex;
                for (size_t index = 0; index < functions.size(); ++index) {
                  if (index != caller && index != callee) {
                    leafIndex = index;
                    break;
                  }
                }
                ZC_IF_SOME(leaf, leafIndex) {
                  lir = lir::MirToLirLowering::lowerCallModuleWithLeaf(
                      functions[caller], functions[callee], functions[leaf], types);
                }
              }
            }
          }
        }
      }
    }
    if (lir == zc::none) {
      return NativeObjectResult(
          zc::str("MIR -> LIR lowering rejected this module (outside the scalar-initializer, "
                  "boolean-conditional, reducible while-loop, comparison-driven conditional, "
                  "aggregate field-return, same-module direct-call, and three-function "
                  "direct-call-with-leaf slices)."));
    }
    backend::llvm::LlvmTranslator translator;
    ZC_IF_SOME(lirModule, lir) {
      auto result = translator.translate(lirModule);
      if (!result.verified()) {
        return NativeObjectResult(zc::str("LLVM translation failed: ", result.diagnostic()));
      }
      const auto object = result.objectCode();
      auto owned = zc::heapArray<uint8_t>(object.size());
      for (size_t index = 0; index < object.size(); ++index) owned[index] = object[index];
      return NativeObjectResult(zc::mv(owned));
    }
    ZC_UNREACHABLE;
  }

  zc::MainBuilder::Validity emitNativeObject() {
    NativeObjectResult result = buildNativeObject();
    if (result.is<zc::String>()) return zc::mv(result).get<zc::String>();
    const auto& options = session->getCompilerOptions();
    if (options.emission.outputPath == zc::none) {
      return zc::str("Binary emission requires an output path (-o <file>).");
    }
    ZC_IF_SOME(path, options.emission.outputPath) {
      auto bytes = zc::mv(result).get<zc::Array<uint8_t>>();
      if (!writeObjectFile(path, bytes.asPtr())) {
        return zc::str("Failed to write the object file to ", path, ".");
      }
    }
    return true;
  }

  ZC_NODISCARD zc::Maybe<zc::Array<zc::byte>> readAbsoluteBytes(zc::StringPtr path) {
    if (path.size() < 2 || path[0] != '/') return zc::none;
    zc::Maybe<zc::Array<zc::byte>> result;
    auto exception = zc::runCatchingExceptions([&]() {
      auto filesystem = zc::newDiskFilesystem();
      result = filesystem->getRoot().openFile(zc::Path::parse(path.slice(1)))->readAllBytes();
    });
    if (exception != zc::none) return zc::none;
    return result;
  }

  ZC_NODISCARD zc::Maybe<zc::String> absoluteParent(zc::StringPtr path) {
    if (path.size() < 2 || path[0] != '/') return zc::none;
    size_t slash = path.size();
    while (slash > 0 && path[slash - 1] != '/') --slash;
    if (slash == 0 || slash == path.size()) return zc::none;
    return slash == 1 ? zc::str("/") : zc::heapString(path.slice(0, slash - 1));
  }

  // Routes an RFC 0010 IR operation rejection (a failed link-plan verification or
  // executable publication) into the session diagnostic engine so its
  // LinkPlanConstruction / LinkerInvocation / ExecutablePublication failure facts
  // materialize as their canonical ZOMxxxx diagnostics, instead of being dropped
  // for a bare error string. A verified result carries no failure and is ignored.
  template <typename VerifiedValue>
  void materializeIrRejection(const ir::IrOperationResult<VerifiedValue>& result) {
    diagnostics::DiagnosticEngine& engine = session->getDiagnosticEngine();
    if (result.isCapabilityRejected()) {
      auto groups = ir::groupIrCapabilityFailures(result.capabilityFailures());
      ir::emitIrDiagnosticGroups(engine, groups.asPtr());
    } else if (result.isIrInvariantRejected()) {
      auto groups = ir::groupIrInvariantFailures(result.invariantFailures());
      ir::emitIrDiagnosticGroups(engine, groups.asPtr());
    } else if (result.isIdentityInvariantRejected()) {
      ir::emitIrIdentityInvariantFailures(engine, result.identityFailures());
    }
  }

  zc::MainBuilder::Validity runNativeExecutable() {
#if defined(__linux__) && defined(__x86_64__) && defined(ZOM_RUNTIME_ENTRY_OBJECT) && \
    defined(ZOM_HOST_LINKER)
    NativeObjectResult objectResult = buildNativeObject();
    if (objectResult.is<zc::String>()) return zc::mv(objectResult).get<zc::String>();
    zc::Array<uint8_t> moduleObject = zc::mv(objectResult).get<zc::Array<uint8_t>>();
    auto entryBytesMaybe = readAbsoluteBytes(ZOM_RUNTIME_ENTRY_OBJECT ""_zc);
    auto linkerBytesMaybe = readAbsoluteBytes(ZOM_HOST_LINKER ""_zc);
    if (entryBytesMaybe == zc::none || linkerBytesMaybe == zc::none) {
      return zc::str("Native runtime entry or linker bytes are unavailable.");
    }
    zc::Array<zc::byte> entryBytes = ZC_REQUIRE_NONNULL(zc::mv(entryBytesMaybe));
    zc::Array<zc::byte> linkerBytes = ZC_REQUIRE_NONNULL(zc::mv(linkerBytesMaybe));
    auto moduleDigest = identity::sha256(moduleObject.asPtr());
    auto entryDigest = identity::sha256(entryBytes.asPtr());
    auto linkerDigest = identity::sha256(linkerBytes.asPtr());
    if (moduleDigest == zc::none || entryDigest == zc::none || linkerDigest == zc::none) {
      return zc::str("Native artifact digest computation failed.");
    }

    zc::String runRoot = zc::str("/tmp/zom-run-", getpid());
    auto filesystem = zc::newDiskFilesystem();
    zc::Path runRootPath = zc::Path::parse(runRoot.slice(1));
    (void)filesystem->getRoot().tryRemove(runRootPath);
    auto runDirectory = filesystem->getRoot().openSubdir(
        runRootPath, zc::WriteMode::CREATE | zc::WriteMode::MODIFY | zc::WriteMode::CREATE_PARENT |
                         zc::WriteMode::PRIVATE);
    runDirectory->openFile(zc::Path("module.o"_zc), zc::WriteMode::CREATE)
        ->writeAll(moduleObject.asPtr());
    runDirectory->openFile(zc::Path("entry.o"_zc), zc::WriteMode::CREATE)
        ->writeAll(entryBytes.asPtr());

    const auto host = hostTargetConfiguration();
    zc::Vector<ir::CanonicalTargetFeature> backendFeatures;
    auto specification = ir::CanonicalTargetSpec::from(
        host.triple, host.dataLayout, "generic"_zc, zc::mv(backendFeatures), "zom"_zc,
        ir::BackendPanicStrategy::Abort, host.objectFormat);
    auto linkerParent = absoluteParent(ZOM_HOST_LINKER ""_zc);
    if (specification == zc::none || linkerParent == zc::none) {
      return zc::str("Host target or linker identity is unavailable.");
    }
    const auto& target = ZC_REQUIRE_NONNULL(specification);
    auto closure = ir::ToolchainClosureRecord::make(
        target.targetSpecId().bytes(), ZC_REQUIRE_NONNULL(linkerParent),
        ir::LinkerDriverKind::ElfDriver, ZOM_HOST_LINKER ""_zc, ZC_REQUIRE_NONNULL(linkerDigest),
        linkerBytes.size(), zc::Array<ir::LinkInputRecord>(), zc::Array<ir::LinkInputRecord>());
    if (closure == zc::none) return zc::str("Host linker closure verification failed.");

    auto objectRecords = zc::heapArrayBuilder<ir::LinkInputRecord>(2);
    objectRecords.add(ZC_REQUIRE_NONNULL(
        ir::LinkInputRecord::make(zc::str(runRoot, "/entry.o"), ir::LinkInputRole::ObjectArtifact,
                                  ZC_REQUIRE_NONNULL(entryDigest), entryBytes.size())));
    objectRecords.add(ZC_REQUIRE_NONNULL(
        ir::LinkInputRecord::make(zc::str(runRoot, "/module.o"), ir::LinkInputRole::ObjectArtifact,
                                  ZC_REQUIRE_NONNULL(moduleDigest), moduleObject.size())));
    zc::Array<zc::String> runtimeSymbols;
    auto inspection =
        ir::ExecutableInspectionProfile::make(ir::ObjectFormat::Elf, ir::ExecutableMachine::X86_64,
                                              64, zc::mv(runtimeSymbols), zc::str("__zom_"));
    if (inspection == zc::none) return zc::str("Host inspection profile verification failed.");
    // Build the artifact side of the host-compatibility comparison from the
    // artifact's real target now, before the inspection profile is moved into the
    // link request: the CPU architecture and operating system are parsed from the
    // canonical target triple and the machine, object format, and pointer width
    // come from these inspection facts. It must never be derived from the running
    // host, or a cross-target artifact would compare host-vs-host and never be
    // rejected.
    auto artifactProfile =
        ir::artifactExecutionProfileFromInspection(target.triple(), ZC_REQUIRE_NONNULL(inspection));
    ir::ExecutableLinkRequest request{ZC_REQUIRE_NONNULL(zc::mv(closure)),
                                      ZC_REQUIRE_NONNULL(zc::mv(inspection)),
                                      zc::heapArray<uint8_t>({'_', 's', 't', 'a', 'r', 't'}),
                                      objectRecords.finish(),
                                      zc::Array<ir::LinkInputRecord>(),
                                      zc::str(runRoot),
                                      zc::str(runRoot, "/program")};
    auto plan = ir::LinkPlanVerifier::verify(zc::mv(request));
    if (!plan.isVerified()) {
      materializeIrRejection(plan);
      return zc::str("Native link plan verification failed.");
    }

    ir::PublicationOutcome publication =
        ir::linkAndPublish(zc::mv(plan).takeVerified(), *filesystem);
    if (publication.isRecoveryRequired()) {
      // A recovery-required outcome still carries the failure algebra that forced
      // recovery: the snapshot arm always carries a primary rejection, and the
      // publication arm carries one when a primary cause was recorded. Route it
      // through the diagnostic engine before returning the neutral recovery
      // message, so the RFC 0010 facts are not discarded.
      ir::LinkRecoveryRequired recovery = zc::mv(publication).takeRecoveryRequired();
      if (recovery.isSnapshotRecoveryRequired()) {
        ir::SnapshotRecoveryRequired snapshot = zc::mv(recovery).takeSnapshot();
        materializeIrRejection(snapshot.primary);
      } else {
        ir::PublicationRecoveryRequired pub = zc::mv(recovery).takePublication();
        ZC_IF_SOME(primary, pub.primary) { materializeIrRejection(primary); }
      }
      return zc::str("Native publication requires explicit recovery.");
    }
    if (publication.isRejected()) {
      ir::PublicationRejection rejection = zc::mv(publication).takeRejected();
      materializeIrRejection(rejection);
      return zc::str("Native linking or publication failed.");
    }
    ir::PublishedExecutableArtifact artifact = zc::mv(publication).takePublished();

    // The host side describes the running compiler host. A genuine cross-target
    // artifact (a differing OS, CPU architecture, object format, or pointer
    // width) is rejected before any process spawn.
    auto hostProfile = ir::currentHostExecutionProfile();
    if (artifactProfile == zc::none || hostProfile == zc::none ||
        !ir::runCompatibility(ZC_REQUIRE_NONNULL(artifactProfile), ZC_REQUIRE_NONNULL(hostProfile))
             .isCompatible()) {
      return zc::str("Published executable is not compatible with this host.");
    }

    zc::SubprocessCommand command(artifact.finalDestination());
    command.envPolicy(zc::SubprocessEnvPolicy::Inherit);
    zc::SubprocessResult executed = command.run();
    // A spawn failure or a signal termination is a run error: the program never
    // produced an exit status to propagate.
    if (!executed.spawned()) { return zc::str("Published executable failed to start."); }
    const zc::SubprocessOutput& output = executed.output();
    if (output.terminationKind != zc::SubprocessTerminationKind::Exited) {
      return zc::str("Published executable was terminated by signal ", output.signal, ".");
    }
    // Clean up the transaction root before exiting so no artifact leaks; a
    // cleanup failure is a run error and does not propagate the exit code.
    if (!filesystem->getRoot().tryRemove(runRootPath)) {
      return zc::str("Published executable ran, but temporary artifact cleanup failed.");
    }
    // The program exited normally: propagate its exit status as this process's
    // own. The Validity framework can only express 0/1, so exit directly after
    // flushing output. POSIX exit statuses are in [0, 255].
    ::fflush(nullptr);
    _exit(output.code & 0xff);
#else
    return "The run command requires the Linux x86-64 LLVM backend and runtime entry object.";
#endif
  }

  // Writes raw object bytes to `outputPath`, honoring the same absolute/relative
  // base-directory resolution as the text dump writer but without formatting.
  ZC_NODISCARD bool writeObjectFile(zc::StringPtr outputPath, zc::ArrayPtr<const uint8_t> bytes) {
    bool ok = false;
    auto exception = zc::runCatchingExceptions([&]() {
      auto filesystem = zc::newDiskFilesystem();
      const bool isAbsolute = outputPath.size() > 0 && outputPath[0] == '/';
      const zc::Directory& baseDir = isAbsolute ? filesystem->getRoot() : filesystem->getCurrent();
      const zc::StringPtr pathText = isAbsolute ? outputPath.slice(1) : outputPath;
      auto file = baseDir.openFile(zc::Path::parse(pathText), zc::WriteMode::CREATE |
                                                                  zc::WriteMode::MODIFY |
                                                                  zc::WriteMode::CREATE_PARENT);
      file->writeAll(bytes);
      ok = true;
    });
    if (exception != zc::none) return false;
    return ok;
  }
#endif

private:
  zc::ProcessContext& context;
  identity::SemanticContextFactory contextFactory;
  zc::Own<driver::CompilerSession> session;
  zc::SpaceFor<driver::CompilerSession> sessionSpace;
  basic::CompilerOptions compilerOpts;
  basic::LangOptions langOpts;
  CompilationAction action = CompilationAction::Emit;
  bool outputActionRequested = false;
  package::RawPackageCompilationRequest packageRequest;
  zc::Vector<zc::String> manifestPaths;
  zc::Maybe<source::core::VerifiedCoreDistribution> coreDistribution;
  bool fmtCheckOnly = false;
  zc::Vector<zc::String> fmtSources;
};

}  // namespace utils
}  // namespace compiler
}  // namespace zomlang

ZC_MAIN(zomlang::compiler::utils::CompilerMain)
