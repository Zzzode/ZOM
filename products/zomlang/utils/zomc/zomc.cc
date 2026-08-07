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
#include "zc/core/time.h"
#include "zomlang/compiler/ast/dump.h"
#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/basic/compiler-opts.h"
#include "zomlang/compiler/basic/io-utils.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/driver/compiler-session.h"
#include "zomlang/compiler/driver/package/lockfile.h"
#include "zomlang/compiler/driver/package/package-compilation-request.h"
#include "zomlang/compiler/driver/package/package-diagnostic.h"
#include "zomlang/compiler/driver/package/source-record.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/sha256.h"
#include "zomlang/compiler/ir/target-registry.h"
#include "zomlang/compiler/source/core-distribution.h"
#include "zomlang/compiler/source/core-source-admission.h"
#include "zomlang/compiler/source/manager.h"

#ifndef VERSION
#define VERSION "(unknown)"
#endif

namespace zomlang {
namespace compiler {
namespace utils {

namespace package = driver::package;

static constexpr char VERSION_STRING[] = "ZomLang Version " VERSION;

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
                   "Resolve and atomically update Zom.lock.")
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
        .addOption({"syntax-only"}, ZC_BIND_METHOD(*this, enableSyntaxOnly),
                   "Perform parsing and name binding without type checking or code generation")
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
    compilerOpts.emission.outputPath = zc::str(spec);
    return true;
  }

  zc::MainBuilder::Validity setEmitType(zc::StringPtr type) {
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
    zc::StringPtr architecture;
    zc::StringPtr vendor;
    zc::StringPtr operatingSystem;
#if defined(__aarch64__) || defined(__arm64__)
    architecture = "aarch64"_zc;
#elif defined(__x86_64__)
    architecture = "x86_64"_zc;
#else
#error "The compiler host architecture must have a registered target profile."
#endif
#if defined(__APPLE__)
    vendor = "apple"_zc;
    operatingSystem = "darwin"_zc;
#elif defined(__linux__)
    vendor = "unknown"_zc;
    operatingSystem = "linux"_zc;
#else
#error "The compiler host operating system must have a registered target profile."
#endif
    zc::Vector<identity::TargetFeatureName> features;
    auto sortedFeatures = identity::SortedTargetFeatureSet::from(zc::mv(features));
    ZC_IF_SOME(featureValues, sortedFeatures) {
      auto projection = identity::CanonicalTargetSpecificationKey::from(
          requireScalar<identity::TargetComponentName>(architecture),
          requireScalar<identity::TargetComponentName>(vendor),
          requireScalar<identity::TargetComponentName>(operatingSystem),
          requireScalar<identity::TargetComponentName>("unknown"_zc),
          requireScalar<identity::TargetComponentName>("zom"_zc),
          static_cast<uint32_t>(sizeof(void*) * 8), identity::Endianness::Little,
          zc::mv(featureValues));
      ZC_IF_SOME(value, projection) { return zc::mv(value); }
    }
    ZC_UNREACHABLE;
  }

  static ir::TargetRegistrySnapshot targetRegistry() {
    zc::StringPtr triple;
    ir::ObjectFormat objectFormat;
#if defined(__aarch64__) || defined(__arm64__)
#if defined(__APPLE__)
    triple = "aarch64-apple-darwin"_zc;
#else
    triple = "aarch64-unknown-linux"_zc;
#endif
#elif defined(__x86_64__)
#if defined(__APPLE__)
    triple = "x86_64-apple-darwin"_zc;
#else
    triple = "x86_64-unknown-linux"_zc;
#endif
#endif
#if defined(__APPLE__)
    objectFormat = ir::ObjectFormat::MachO;
#else
    objectFormat = ir::ObjectFormat::Elf;
#endif
    const zc::StringPtr dataLayout = sizeof(void*) == 8 ? "e-p:64:64"_zc : "e-p:32:32"_zc;
    zc::Vector<ir::CanonicalTargetFeature> backendFeatures;
    auto specification =
        ir::CanonicalTargetSpec::from(triple, dataLayout, "generic"_zc, zc::mv(backendFeatures),
                                      "zom"_zc, ir::BackendPanicStrategy::Abort, objectFormat);
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
      const auto executable =
          filesystem.getCurrentPath().eval(context.getProgramName()).parent().parent();
      const auto coreRoot =
          executable.clone().append(zc::Path({"share"_zc, "zom"_zc, "core"_zc, "src"_zc}));
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

    // 4. Syntax-only completion after verified parsing and name binding
    if (options.emission.syntaxOnly) {
      context.warning("Syntax and name binding checks completed successfully.");
      return true;
    }

    // 5. Type checking
    if (!session->checkSources() || session->getDiagnosticEngine().hasErrors()) {
      return zc::str("Compilation failed due to type checking errors.");
    }

    // 6. Dispatch Dump
    if (options.emission.outputType ==
        basic::CompilerOptions::EmissionOptions::OutputType::Dispatch) {
      return emitDispatch();
    }

    // 7. Final Emission
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
    return diagnoseEmission<diagnostics::DiagID::BinaryEmissionUnavailable>(emissionLocation());
  }

private:
  zc::ProcessContext& context;
  identity::SemanticContextFactory contextFactory;
  zc::Own<driver::CompilerSession> session;
  zc::SpaceFor<driver::CompilerSession> sessionSpace;
  basic::CompilerOptions compilerOpts;
  basic::LangOptions langOpts;
  package::RawPackageCompilationRequest packageRequest;
  zc::Vector<zc::String> manifestPaths;
  zc::Maybe<source::core::VerifiedCoreDistribution> coreDistribution;
};

}  // namespace utils
}  // namespace compiler
}  // namespace zomlang

ZC_MAIN(zomlang::compiler::utils::CompilerMain)
