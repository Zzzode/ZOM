// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/binding-input.h"

#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/identity/sha256.h"

namespace zomlang::compiler::binder {
namespace {

constexpr char kGraphDomain[] = "zom.module-dependency-graph.v0";

void appendUint64(zc::Vector<uint8_t>& bytes, uint64_t value) {
  for (uint32_t index = 0; index < 8; ++index) {
    const uint32_t shift = 56 - index * 8;
    bytes.add(static_cast<uint8_t>((value >> shift) & 0xffu));
  }
}

ModuleGraphInvariantFact failure(ModuleGraphInvariantKind kind,
                                 zc::Maybe<identity::ModuleId>&& requester = zc::none) {
  return ModuleGraphInvariantFact{kind, zc::mv(requester), zc::Vector<uint32_t>(), 1};
}

bool hasUnresolvedDependency(const ast::Tree& tree) {
  for (const auto& node : tree.nodes()) {
    if (node.kind == ast::SyntaxKind::ImportDeclaration) { return true; }
    if (node.kind == ast::SyntaxKind::ModuleDeclaration &&
        static_cast<ast::ModuleDeclarationForm>(
            node.payload.words[ast::kModuleDeclarationFormWord]) ==
            ast::ModuleDeclarationForm::Alias) {
      return true;
    }
    if (node.kind == ast::SyntaxKind::ExportDeclaration &&
        tree.contains(ast::NodeId(node.payload.words[ast::kExportDeclarationPathWord]))) {
      return true;
    }
  }
  return false;
}

bool allRegistriesFrozen(const identity::SemanticIdentityRegistrySet& registries) {
  return registries.packages().isFrozen() && registries.crates().isFrozen() &&
         registries.sourceFiles().isFrozen() && registries.modules().isFrozen() &&
         registries.definitions().isFrozen() && registries.impls().isFrozen();
}

}  // namespace

zc::Maybe<ModuleGraphRevision> computeModuleGraphRevision(
    const identity::SemanticContextFingerprint& fingerprint, const identity::ModuleKey& module) {
  zc::Vector<uint8_t> bytes;
  for (size_t index = 0; index < sizeof(kGraphDomain) - 1; ++index) {
    bytes.add(static_cast<uint8_t>(kGraphDomain[index]));
  }
  bytes.add(0x00);
  bytes.addAll(fingerprint.digest().bytes());
  appendUint64(bytes, 1);
  const auto moduleBytes = module.encode();
  appendUint64(bytes, moduleBytes.size());
  bytes.addAll(moduleBytes.asPtr());
  appendUint64(bytes, 0);
  auto digest = identity::sha256(bytes.asPtr());
  ZC_IF_SOME(value, digest) { return ModuleGraphRevision(value); }
  return zc::none;
}

ModuleGraphRevision::ModuleGraphRevision(const identity::Sha256Digest& digest) noexcept
    : value(digest) {}
const identity::Sha256Digest& ModuleGraphRevision::digest() const noexcept { return value; }

struct VerifiedModuleGraphView::Impl final {
  Impl(identity::SemanticContextBrand context, identity::SemanticContextFingerprint&& fingerprint,
       identity::ModuleId requester, identity::ModuleKey&& module, ModuleGraphRevision revision)
      : context(context),
        fingerprint(zc::mv(fingerprint)),
        requester(requester),
        module(zc::mv(module)),
        revision(revision) {}

  identity::SemanticContextBrand context;
  identity::SemanticContextFingerprint fingerprint;
  identity::ModuleId requester;
  identity::ModuleKey module;
  ModuleGraphRevision revision;
};

VerifiedModuleGraphView::VerifiedModuleGraphView(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
VerifiedModuleGraphView::~VerifiedModuleGraphView() noexcept(false) = default;
VerifiedModuleGraphView::VerifiedModuleGraphView(VerifiedModuleGraphView&&) noexcept = default;
VerifiedModuleGraphView& VerifiedModuleGraphView::operator=(VerifiedModuleGraphView&&) noexcept =
    default;
identity::SemanticContextBrand VerifiedModuleGraphView::semanticContext() const noexcept {
  return impl->context;
}
identity::ModuleId VerifiedModuleGraphView::requester() const noexcept { return impl->requester; }
const ModuleGraphRevision& VerifiedModuleGraphView::revision() const noexcept {
  return impl->revision;
}

ModuleGraphVerificationResult ModuleGraphVerifier::verifySingleModule(
    identity::SemanticContextBrand context,
    const identity::SemanticContextFingerprint& expectedFingerprint,
    const identity::SemanticIdentityRegistrySet& registries, identity::ModuleId requester,
    const VerifiedParsedModule& parsedModule) {
  const auto& tree = parsedModule.tree();
  zc::Maybe<identity::ModuleId> requesterFact = requester;
  if (!context.isValid() || !requester.belongsTo(context) ||
      !parsedModule.sourceFile().belongsTo(context) || !allRegistriesFrozen(registries) ||
      registries.modules().size() != 1 ||
      registries.modules().validate(requester) != identity::FrozenRegistryFailure::None ||
      !tree.contains(tree.root()) || tree.node(tree.root()).kind != ast::SyntaxKind::SourceFile) {
    return failure(ModuleGraphInvariantKind::InputMismatch, zc::mv(requesterFact));
  }
  if (hasUnresolvedDependency(tree)) {
    return failure(ModuleGraphInvariantKind::IncompleteResolution, zc::mv(requesterFact));
  }
  auto module = registries.modules().lookup(requester);
  auto source = registries.sourceFiles().lookup(parsedModule.sourceFile());
  auto snapshot = registries.sourceSnapshot(parsedModule.sourceFile());
  auto fingerprint = identity::SemanticContextFingerprint::compute(
      registries, zc::ArrayPtr<const identity::PackageDependencyEdgeKey>(),
      zc::ArrayPtr<const identity::CrateDependencyEdgeKey>());
  if (module == zc::none || source == zc::none || snapshot == zc::none || fingerprint == zc::none) {
    return failure(ModuleGraphInvariantKind::InputMismatch, zc::mv(requesterFact));
  }
  ZC_IF_SOME(fingerprintValue, fingerprint) {
    if (fingerprintValue.digest() != expectedFingerprint.digest()) {
      return failure(ModuleGraphInvariantKind::RevisionMismatch, zc::mv(requesterFact));
    }
    ZC_IF_SOME(moduleValue, module) {
      ZC_IF_SOME(sourceValue, source) {
        if (!moduleValue.source().sameAs(sourceValue)) {
          return failure(ModuleGraphInvariantKind::InputMismatch, zc::mv(requesterFact));
        }
      }
      ZC_IF_SOME(snapshotValue, snapshot) {
        if (snapshotValue.contentDigest() != parsedModule.contentDigest() ||
            snapshotValue.bytes().size() != parsedModule.byteLength()) {
          return failure(ModuleGraphInvariantKind::InputMismatch, zc::mv(requesterFact));
        }
      }
      auto revision = computeModuleGraphRevision(fingerprintValue, moduleValue);
      ZC_IF_SOME(revisionValue, revision) {
        return VerifiedModuleGraphView(zc::heap<VerifiedModuleGraphView::Impl>(
            context, zc::mv(fingerprintValue), requester, moduleValue.clone(), revisionValue));
      }
    }
  }
  return failure(ModuleGraphInvariantKind::RevisionMismatch, zc::mv(requesterFact));
}

void emitModuleGraphInvariant(diagnostics::DiagnosticEngine& diagnostics,
                              const ModuleGraphInvariantFact& fact) {
  diagnostics.diagnose<diagnostics::DiagID::ModuleGraphInvariant>(source::SourceLoc(),
                                                                  zc::str(fact.occurrence));
}

struct VerifiedBindingInput::Impl final {
  explicit Impl(const BindingInputCandidate& candidate)
      : module(candidate.module),
        parsedModule(candidate.parsedModule),
        definitions(candidate.definitions) {}

  identity::ModuleId module;
  const VerifiedParsedModule& parsedModule;
  const FrozenDefinitionInventoryView& definitions;
};

VerifiedBindingInput::VerifiedBindingInput(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
VerifiedBindingInput::~VerifiedBindingInput() noexcept(false) = default;
VerifiedBindingInput::VerifiedBindingInput(VerifiedBindingInput&&) noexcept = default;
VerifiedBindingInput& VerifiedBindingInput::operator=(VerifiedBindingInput&&) noexcept = default;
identity::ModuleId VerifiedBindingInput::module() const noexcept { return impl->module; }
const ast::Tree& VerifiedBindingInput::tree() const noexcept { return impl->parsedModule.tree(); }
const VerifiedParsedModule& VerifiedBindingInput::parsedModule() const noexcept {
  return impl->parsedModule;
}
const FrozenDefinitionInventoryView& VerifiedBindingInput::definitions() const noexcept {
  return impl->definitions;
}

BindingInputVerificationResult BindingInputVerifier::verify(
    const BindingInputCandidate& candidate) {
  const auto& registries = candidate.registries;
  zc::Maybe<identity::ModuleId> requester = candidate.module;
  const auto inputFailure = [&]() {
    return failure(ModuleGraphInvariantKind::InputMismatch, candidate.module);
  };
  const auto incomplete = [&]() {
    return failure(ModuleGraphInvariantKind::IncompleteResolution, candidate.module);
  };
  if (!candidate.semanticContext.isValid() || !allRegistriesFrozen(registries) ||
      !candidate.package.belongsTo(candidate.semanticContext) ||
      !candidate.crate.belongsTo(candidate.semanticContext) ||
      !candidate.module.belongsTo(candidate.semanticContext) ||
      !candidate.parsedModule.sourceFile().belongsTo(candidate.semanticContext) ||
      candidate.moduleGraph.semanticContext() != candidate.semanticContext ||
      candidate.moduleGraph.requester() != candidate.module ||
      candidate.definitions.semanticContext() != candidate.semanticContext ||
      candidate.definitions.module() != candidate.module) {
    return inputFailure();
  }
  auto package = registries.packages().lookup(candidate.package);
  auto crate = registries.crates().lookup(candidate.crate);
  auto source = registries.sourceFiles().lookup(candidate.parsedModule.sourceFile());
  auto module = registries.modules().lookup(candidate.module);
  if (package == zc::none || crate == zc::none || source == zc::none || module == zc::none ||
      registries.sourceSnapshot(candidate.parsedModule.sourceFile()) == zc::none) {
    return inputFailure();
  }
  ZC_IF_SOME(packageValue, package) {
    ZC_IF_SOME(crateValue, crate) {
      ZC_IF_SOME(sourceValue, source) {
        ZC_IF_SOME(moduleValue, module) {
          if (packageValue.encode() != crateValue.package().encode() ||
              !sourceValue.belongsTo(crateValue) ||
              moduleValue.crate().encode() != crateValue.encode() ||
              !moduleValue.source().sameAs(sourceValue)) {
            return inputFailure();
          }
          auto fingerprint = identity::SemanticContextFingerprint::compute(
              registries, zc::ArrayPtr<const identity::PackageDependencyEdgeKey>(),
              zc::ArrayPtr<const identity::CrateDependencyEdgeKey>());
          if (fingerprint == zc::none) { return inputFailure(); }
          ZC_IF_SOME(fingerprintValue, fingerprint) {
            auto revision = computeModuleGraphRevision(fingerprintValue, moduleValue);
            if (revision == zc::none) { return inputFailure(); }
            ZC_IF_SOME(revisionValue, revision) {
              if (revisionValue.digest() != candidate.moduleGraph.revision().digest()) {
                return failure(ModuleGraphInvariantKind::RevisionMismatch, zc::mv(requester));
              }
            }
          }
          const auto& tree = candidate.parsedModule.tree();
          if (!tree.contains(tree.root()) ||
              tree.node(tree.root()).kind != ast::SyntaxKind::SourceFile) {
            return inputFailure();
          }
          if (hasUnresolvedDependency(tree)) { return incomplete(); }
          for (const auto& definition : candidate.definitions.definitions()) {
            if (!definition.definition.belongsTo(candidate.semanticContext)) {
              return inputFailure();
            }
            auto key = registries.definitions().lookup(definition.definition);
            if (key == zc::none) { return inputFailure(); }
            ZC_IF_SOME(keyValue, key) {
              if (keyValue.module().encode() != moduleValue.encode()) { return inputFailure(); }
            }
          }
        }
      }
    }
  }
  return VerifiedBindingInput(zc::heap<VerifiedBindingInput::Impl>(candidate));
}

}  // namespace zomlang::compiler::binder
