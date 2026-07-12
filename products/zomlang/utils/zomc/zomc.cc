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
#include "zomlang/compiler/driver/package/lockfile.h"
#include "zomlang/compiler/driver/package/package-compilation-request.h"
#include "zomlang/compiler/driver/package/package-diagnostic.h"
#include "zomlang/compiler/driver/package/source-record.h"
#include "zomlang/compiler/irgen/ir-dump.h"
#include "zomlang/compiler/irgen/lowering.h"
#include "zomlang/compiler/source/manager.h"

#ifndef VERSION
#define VERSION "(unknown)"
#endif

namespace zomlang {
namespace compiler {
namespace utils {

namespace package = driver::package;

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
          requireScalar<identity::TargetComponentName>("zom-v1"_zc),
          static_cast<uint32_t>(sizeof(void*) * 8), identity::Endianness::Little,
          zc::mv(featureValues));
      ZC_IF_SOME(value, projection) { return zc::mv(value); }
    }
    ZC_UNREACHABLE;
  }

  static irgen::TargetRegistrySnapshot targetRegistry() {
    zc::StringPtr triple;
    irgen::ObjectFormat objectFormat;
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
    objectFormat = irgen::ObjectFormat::MachO;
#else
    objectFormat = irgen::ObjectFormat::Elf;
#endif
    const zc::StringPtr dataLayout = sizeof(void*) == 8 ? "e-p:64:64"_zc : "e-p:32:32"_zc;
    zc::Vector<irgen::CanonicalTargetFeature> backendFeatures;
    auto specification = irgen::CanonicalTargetSpec::from(
        triple, dataLayout, "generic"_zc, zc::mv(backendFeatures), "zom-v1"_zc,
        irgen::BackendPanicStrategy::Abort, objectFormat);
    auto name = package::RegisteredTargetProfileName::from("host"_zc);
    ZC_IF_SOME(profileName, name) {
      ZC_IF_SOME(specificationValue, specification) {
        zc::Vector<identity::TargetFeatureName> semanticFeatures;
        zc::Vector<irgen::CanonicalTargetSpec> specifications;
        specifications.add(zc::mv(specificationValue));
        auto profile = irgen::RegisteredTargetProfileRecord::from(
            profileName.clone(), hostSemanticProjection(), zc::mv(semanticFeatures),
            zc::mv(specifications));
        ZC_IF_SOME(profileValue, profile) {
          zc::Vector<irgen::RegisteredTargetProfileRecord> profiles;
          profiles.add(zc::mv(profileValue));
          auto registry =
              irgen::TargetRegistrySnapshot::from(zc::mv(profileName), zc::mv(profiles));
          ZC_IF_SOME(value, registry) { return zc::mv(value); }
        }
      }
    }
    ZC_UNREACHABLE;
  }

  static package::RegisteredTargetService packageTargetService(
      const irgen::TargetRegistrySnapshot& registry) {
    auto service = registry.packageTargetService();
    ZC_IF_SOME(value, service) { return zc::mv(value); }
    ZC_UNREACHABLE;
  }

  zc::MainBuilder::Validity diagnoseTargetSelectionIssue(
      irgen::TargetSelectionVerificationIssue issue,
      package::PackagePanicStrategy requestedPanicStrategy) {
    if (issue == irgen::TargetSelectionVerificationIssue::CapabilityUnavailable) {
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
      ZC_IF_SOME(value, admitted) { segments.add(zc::mv(value)); }
      else { ZC_UNREACHABLE; }
    }
    return identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
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
              members.add(package::WorkspaceMemberInput::from(
                  memberPath.clone(), zc::mv(memberSource), zc::mv(memberInventoryValue)));
            }
          }
        }
        auto normalized =
            package::normalizeWorkspace(rootSource, rootInventoryValue, zc::mv(members));
        if (normalized.is<package::NormalizedWorkspace>()) {
          return LoadedWorkspace{zc::mv(normalized.get<package::NormalizedWorkspace>()),
                                 zc::mv(rootPath)};
        }
      }
    } catch (const zc::Exception&) { return zc::none; }
    return zc::none;
  }

  bool addVerifiedRoots(const zc::Filesystem& filesystem, zc::PathPtr workspaceRoot,
                        zc::ArrayPtr<const package::FinalizedCompilationRoot> roots) {
    for (const auto& root : roots) {
      auto packageRoot = workspaceRoot.clone();
      const auto& source = root.packageKey().source();
      if (source.kind() != identity::PackageSourceKind::LocalPath) { return false; }
      auto packageRelative = filesystemPath(source.localPath());
      packageRoot = zc::mv(packageRoot).eval(packageRelative.toString());
      auto sourceRelative = filesystemPath(root.sourcePath());
      auto displayPath = zc::mv(packageRelative).append(sourceRelative.clone());
      auto sourcePath = zc::mv(packageRoot).append(zc::mv(sourceRelative));
      if (session->addPackageSourceFile(sourcePath.toString(true), displayPath.toString(), root) ==
          zc::none) {
        return false;
      }
    }
    return true;
  }

  static identity::PackageBaseKey packageBase(const identity::PackageKey& packageKey) {
    auto name = identity::PackageName::fromCanonical(packageKey.name());
    auto version = identity::ResolvedVersion::fromCanonical(packageKey.version());
    ZC_IF_SOME(nameValue, name) {
      ZC_IF_SOME(versionValue, version) {
        return identity::PackageBaseKey::from(packageKey.source().clone(), zc::mv(nameValue),
                                              zc::mv(versionValue));
      }
    }
    ZC_UNREACHABLE;
  }

  static identity::SortedFeatureSet packageFeatures(const identity::PackageKey& packageKey) {
    zc::Vector<identity::FeatureName> features(packageKey.features().size());
    for (const auto& feature : packageKey.features()) { features.add(feature.clone()); }
    auto result = identity::SortedFeatureSet::from(zc::mv(features));
    ZC_IF_SOME(value, result) { return zc::mv(value); }
    ZC_UNREACHABLE;
  }

  zc::Maybe<driver::VerifiedPackageSessionInput> resolvePackageInput(
      const zc::Filesystem& filesystem, zc::PathPtr workspaceRoot,
      const package::NormalizedPackageCompilationRequest& normalizedRequest,
      package::VerifiedPackageCompilationRequest&& request,
      irgen::VerifiedTargetSelection&& verifiedHostTarget,
      irgen::VerifiedTargetSelection&& verifiedTarget,
      const package::NormalizedWorkspace& workspace) {
    zc::Vector<package::ResolverRelease> releases;
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
      identity::PackageBaseKey base = packageBase(request.roots()[0].packageKey());
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
        releases.add(package::ResolverRelease::fromLocal(recordValue));
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
    zc::Vector<package::ResolverRoot> roots;
    roots.add(package::ResolverRoot::from(packageBase(request.roots()[0].packageKey()),
                                          packageFeatures(request.roots()[0].packageKey()), false,
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
      auto resolved =
          package::PackageResolver::resolveLocked(roots, releases, lockedGraph, metrics);
      if (resolved.is<package::PackageResolverFailure>() || metrics.solverInvocations != 0) {
        return zc::none;
      }
      auto& graph = resolved.get<package::ResolutionOutput>();
      zc::Vector<package::ResolvedPackageSourceSnapshot> selectedSnapshots;
      for (auto& snapshot : snapshots) {
        for (const auto& selected : graph.packages()) {
          if (snapshot.package().encode().asPtr() == packageBase(selected.key()).encode().asPtr()) {
            selectedSnapshots.add(zc::mv(snapshot));
            break;
          }
        }
      }
      return driver::VerifiedPackageSessionInput::from(zc::mv(request), zc::mv(verifiedHostTarget),
                                                       zc::mv(verifiedTarget), zc::mv(graph),
                                                       zc::mv(selectedSnapshots));
    }

    auto resolved = package::PackageResolver::resolve(roots, releases);
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
        if (snapshot.package().encode().asPtr() == packageBase(selected.key()).encode().asPtr()) {
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
    if (verifiedHostTarget.is<irgen::TargetSelectionVerificationIssue>()) {
      return diagnoseTargetSelectionIssue(
          verifiedHostTarget.get<irgen::TargetSelectionVerificationIssue>(),
          normalizedRequest.hostTarget().panicStrategy());
    }
    auto verifiedTarget = registry.verify(normalizedRequest.target());
    if (verifiedTarget.is<irgen::TargetSelectionVerificationIssue>()) {
      return diagnoseTargetSelectionIssue(
          verifiedTarget.get<irgen::TargetSelectionVerificationIssue>(),
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
      auto& verifiedRequest = verified.get<package::VerifiedPackageCompilationRequest>();
      auto packageInput = resolvePackageInput(
          *filesystem, workspace.rootPath, normalizedRequest, zc::mv(verifiedRequest),
          zc::mv(verifiedHostTarget.get<irgen::VerifiedTargetSelection>()),
          zc::mv(verifiedTarget.get<irgen::VerifiedTargetSelection>()), workspace.workspace);
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
      auto finalizedRoots = session->getFinalizedCompilationRoots();
      if (finalizedRoots.size() == 0 ||
          !addVerifiedRoots(*filesystem, workspace.rootPath, finalizedRoots)) {
        package::PackageDiagnosticAdapter::emitBuildScriptIssue(
            session->getDiagnosticEngine(),
            package::BuildScriptIssue::BuildResultIntegrityViolation);
        context.error(zc::StringPtr());
        return true;
      }
    }

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
          auto selectedTarget = session->getVerifiedTarget();
          if (selectedTarget == zc::none) {
            session->getDiagnosticEngine().diagnose<diagnostics::DiagID::LirInvariant>(
                emissionLocation(), zc::str(uint64_t{1}));
            context.error(zc::StringPtr());
            return true;
          }
          irgen::LoweringSourceContext sourceContext(session->getSourceManager(), bufferId);
          ZC_IF_SOME(target, selectedTarget) {
            const auto dataLayout = target.targetSpec().llvmDataLayout();
            const auto targetLayout = (dataLayout == "e-p:32:32"_zc || dataLayout == "E-p:32:32"_zc)
                                          ? irgen::TargetDataLayout::ilp32()
                                          : irgen::TargetDataLayout::lp64();
            auto lowering = irgen::lowerCheckedTree(checkedTree, bindingMetadata, checkedTypeEnv,
                                                    targetLayout, sourceContext);
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
                        irDumpFailureName(failure.kind),
                        failure.definition.isValid() ? "resolved"_zc : "none"_zc,
                        zc::str(static_cast<uint64_t>(failure.block.value)),
                        zc::str(static_cast<uint64_t>(failure.value.value)),
                        failure.type.isValid() ? "resolved"_zc : "none"_zc,
                        zc::str(static_cast<uint64_t>(failure.index)));
                context.error(zc::StringPtr());
                return true;
              }
              return true;
            }
            return diagnoseEmission<diagnostics::DiagID::IrOutputCreationFailed>(
                emissionLocation());
          }
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
  package::RawPackageCompilationRequest packageRequest;
  zc::Vector<zc::String> manifestPaths;
};

}  // namespace utils
}  // namespace compiler
}  // namespace zomlang

ZC_MAIN(zomlang::compiler::utils::CompilerMain)
