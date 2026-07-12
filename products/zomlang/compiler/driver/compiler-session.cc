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

#include "zomlang/compiler/driver/compiler-session.h"

#include "zc/core/filesystem.h"
#include "zc/core/map.h"
#include "zc/core/mutex.h"
#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/basic/compiler-opts.h"
#include "zomlang/compiler/basic/frontend.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/thread-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/binder/definition-identity-map.h"
#include "zomlang/compiler/binder/definition-inventory.h"
#include "zomlang/compiler/diagnostics/consoling-diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/driver/package/package-diagnostic.h"
#include "zomlang/compiler/identity/identity-diagnostic-adapter.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/compiler/symbol/symbol-table.h"
#include "zomlang/compiler/type/type-env.h"

namespace zomlang {
namespace compiler {
namespace driver {
namespace {

bool sameBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  return left == right;
}

zc::Maybe<identity::SourceOriginKey> sourceOriginFor(
    const package::FinalizedCompilationRoot& root) {
  const auto& packageSource = root.packageKey().source();
  switch (packageSource.kind()) {
    case identity::PackageSourceKind::LocalPath: {
      zc::Vector<identity::CanonicalPathSegment> segments;
      for (const auto& segment : packageSource.localPath().segments()) {
        segments.add(segment.clone());
      }
      for (const auto& segment : root.sourcePath().segments()) { segments.add(segment.clone()); }
      return identity::SourceOriginKey::localFile(identity::CanonicalWorkspaceRelativePath::from(
          packageSource.localPath().leadingParents(), zc::mv(segments)));
    }
    case identity::PackageSourceKind::Registry:
      return identity::SourceOriginKey::registryFile(root.packageKey().clone(),
                                                     root.sourcePath().clone());
    case identity::PackageSourceKind::Vcs:
      return identity::SourceOriginKey::vcsFile(root.packageKey().clone(),
                                                root.sourcePath().clone());
  }
  ZC_UNREACHABLE
}

void emitIdentityFailures(identity::SemanticIdentityRegistrySet& registries,
                          diagnostics::DiagnosticEngine& diagnostics) {
  registries.sortIdentityInvariants();
  const auto groups = identity::groupIdentityInvariants(registries.identityInvariants());
  identity::emitIdentityDiagnosticGroups(diagnostics, groups.asPtr());
}

}  // namespace
// ================================================================================
// CompilerSession::Impl

struct CompilerSession::Impl {
  Impl(identity::SemanticContextFactory& contextFactory, const basic::LangOptions& opts,
       const basic::CompilerOptions& compOpts)
      : langOpts(opts),
        compilerOpts(compOpts),
        stringPool(zc::heap<basic::StringPool>()),
        sourceManager(zc::heap<source::SourceManager>(*stringPool)),
        diagnosticEngine(zc::heap<diagnostics::DiagnosticEngine>(*sourceManager)),
        symbolTable(zc::heap<symbol::SymbolTable>()) {
    diagnosticEngine->addConsumer(zc::heap<diagnostics::ConsolingDiagnosticConsumer>());
    auto issuedContext = contextFactory.issue();
    if (issuedContext == zc::none) {
      diagnosticEngine->diagnose<diagnostics::DiagID::IdentityBrandExhausted>(source::SourceLoc(),
                                                                              zc::str(uint64_t{1}));
      return;
    }
    ZC_IF_SOME(context, issuedContext) {
      contextBrand = context;
      auto issuedRegistries =
          identity::SemanticIdentityRegistrySet::create(contextFactory, contextBrand);
      if (issuedRegistries == zc::none) {
        diagnosticEngine->diagnose<diagnostics::DiagID::IdentityDuplicateSingletonStore>(
            source::SourceLoc(), zc::str(uint64_t{1}));
        return;
      }
      ZC_IF_SOME(registries, issuedRegistries) { identityRegistries = zc::mv(registries); }
      auto issuedTypeStoreToken =
          contextFactory.issueSemanticTypeStoreConstructionToken(contextBrand);
      if (issuedTypeStoreToken == zc::none) {
        diagnosticEngine->diagnose<diagnostics::DiagID::IdentityDuplicateSingletonStore>(
            source::SourceLoc(), zc::str(uint64_t{1}));
        return;
      }
      ZC_IF_SOME(token, issuedTypeStoreToken) {
        semanticTypeStore = zc::heap<type::SemanticTypeStore>(zc::mv(token));
      }
    }
  }
  ~Impl() noexcept(false) = default;

  ZC_DISALLOW_COPY_AND_MOVE(Impl);

  struct OutputDirective {
    zc::ArrayPtr<zc::byte> name;
    zc::Maybe<zc::Path> dir;

    ZC_DISALLOW_COPY(OutputDirective);
    OutputDirective(OutputDirective&&) noexcept = default;
    OutputDirective(const zc::ArrayPtr<zc::byte> name, zc::Maybe<zc::Path> dir)
        : name(name), dir(zc::mv(dir)) {}
  };

  /// Language options
  const basic::LangOptions& langOpts;
  /// Compiler options
  const basic::CompilerOptions& compilerOpts;
  /// Process-unique identity for this semantic compilation.
  identity::SemanticContextBrand contextBrand;
  /// Sole RFC 0011 identity registry family for this session.
  zc::Maybe<identity::SemanticIdentityRegistrySet> identityRegistries;
  /// Sole RFC 0005 canonical semantic type store for this session.
  zc::Own<type::SemanticTypeStore> semanticTypeStore;
  /// Workspace-verified package roots and their semantic identities.
  zc::Maybe<package::VerifiedPackageCompilationRequest> packageRequest;
  /// Post-build roots whose complete CrateKey values are safe to freeze.
  zc::Vector<package::FinalizedCompilationRoot> finalizedRoots;
  zc::Maybe<irgen::VerifiedTargetSelection> verifiedHostTarget;
  zc::Maybe<irgen::VerifiedTargetSelection> verifiedTarget;
  zc::Maybe<package::PackageResolution> packageGraph;
  zc::Vector<package::ResolvedPackageSourceSnapshot> packageSnapshots;
  zc::Maybe<package::VerifiedBuildScriptPlan> buildScriptPlan;
  zc::Maybe<package::VerifiedBuildScriptResultSet> buildScriptResults;
  /// Canonical source identities retained from package admission until source freeze.
  zc::HashMap<source::BufferId, identity::SourceFileKey> pendingSourceIdentities;
  /// Frozen source handles indexed by the phase-local SourceManager buffer handle.
  zc::HashMap<source::BufferId, identity::SourceFileId> sourceIdentities;
  struct ModuleIdentityBinding final {
    source::BufferId buffer;
    ast::NodeId node;
    identity::ModuleId identity;
  };
  /// Frozen module handles indexed by one tree-local node, with node zero denoting an implicit
  /// root.
  zc::Vector<ModuleIdentityBinding> moduleIdentities;
  struct DefinitionIdentityBinding final {
    source::BufferId buffer;
    ast::NodeId node;
    identity::DefId identity;
  };
  struct ImplIdentityBinding final {
    source::BufferId buffer;
    ast::NodeId node;
    identity::ImplId identity;
  };
  zc::Vector<DefinitionIdentityBinding> definitionIdentities;
  zc::Vector<ImplIdentityBinding> implIdentities;
  zc::HashMap<source::BufferId, binder::DefinitionIdentityMap> definitionIdentityMaps;
  /// String pool to manage interned strings.
  zc::Own<basic::StringPool> stringPool;
  /// Source manager to manage source files.
  zc::Own<source::SourceManager> sourceManager;
  /// Diagnostic engine to report diagnostics.
  zc::Own<diagnostics::DiagnosticEngine> diagnosticEngine;
  /// Symbol table to manage symbols and scopes.
  zc::Own<symbol::SymbolTable> symbolTable;
  /// Mutex-guarded map from BufferId to parsed syntax tree.
  zc::MutexGuarded<zc::HashMap<source::BufferId, ast::Tree>> astMutex;
  /// Mutex-guarded map from BufferId to binder metadata side tables.
  zc::MutexGuarded<zc::HashMap<source::BufferId, ast::BindingMetadata>> bindingMetadataMutex;
  /// Mutex-guarded prebinding definition inventory for every parsed syntax tree.
  zc::MutexGuarded<zc::HashMap<source::BufferId, binder::DefinitionInventory>>
      definitionInventoryMutex;
  /// Mutex-guarded map from BufferId to type environments from type checking.
  zc::MutexGuarded<zc::HashMap<source::BufferId, type::TypeEnv>> typeEnvMutex;

  bool freezePackageInputIdentities() {
    if (packageRequest == zc::none) { return true; }
    if (identityRegistries == zc::none || packageGraph == zc::none) { return false; }

    bool failed = false;
    ZC_IF_SOME(registries, identityRegistries) {
      zc::Vector<zc::Array<uint8_t>> collectedCrateKeys;
      uint32_t traversalOrdinal = 0;
      if (finalizedRoots.size() == 0) {
        package::PackageDiagnosticAdapter::emitBuildScriptIssue(
            *diagnosticEngine, package::BuildScriptIssue::BuildResultIntegrityViolation);
        return false;
      }
      for (const auto& root : finalizedRoots) {
        auto encoded = root.crateKey().encode();
        bool alreadyCollected = false;
        for (const auto& prior : collectedCrateKeys) {
          if (sameBytes(prior.asPtr(), encoded.asPtr())) {
            alreadyCollected = true;
            break;
          }
        }
        if (alreadyCollected) { continue; }
        collectedCrateKeys.add(zc::mv(encoded));
        failed = registries.collectCrate(root.crateKey().clone(), traversalOrdinal++) !=
                     identity::FrozenRegistryFailure::None ||
                 failed;
      }
      failed = registries.freezeCrates() != identity::FrozenRegistryFailure::None || failed;

      const auto buffers = sourceManager->getManagedBufferIds();
      traversalOrdinal = 0;
      for (const auto& buffer : buffers) {
        auto pending = pendingSourceIdentities.find(buffer);
        if (pending == zc::none) {
          failed = true;
          continue;
        }
        ZC_IF_SOME(sourceKey, pending) {
          auto snapshot = identity::ImmutableSourceSnapshot::from(
              sourceKey.clone(), zc::heapArray(sourceManager->getEntireTextForBuffer(buffer)));
          if (snapshot == zc::none) {
            failed = true;
            continue;
          }
          ZC_IF_SOME(value, snapshot) {
            failed = registries.collectSourceFile(zc::mv(value), traversalOrdinal++) !=
                         identity::FrozenRegistryFailure::None ||
                     failed;
          }
        }
      }
      failed = registries.freezeSourceFiles() != identity::FrozenRegistryFailure::None || failed;
      if (!failed) {
        for (const auto& entry : pendingSourceIdentities) {
          auto source = registries.sourceFiles().find(entry.value);
          ZC_IF_SOME(value, source) { sourceIdentities.upsert(entry.key, value); }
          else { failed = true; }
        }
      }
      if (failed) { emitIdentityFailures(registries, *diagnosticEngine); }
    }
    return !failed;
  }

  zc::Maybe<identity::SourceFileId> sourceIdentity(const source::BufferId& buffer) const {
    ZC_IF_SOME(value, sourceIdentities.find(buffer)) { return value; }
    return zc::none;
  }

  zc::Maybe<identity::ModuleId> moduleIdentity(const source::BufferId& buffer,
                                               ast::NodeId node) const {
    for (const auto& binding : moduleIdentities) {
      if (binding.buffer == buffer && binding.node == node) { return binding.identity; }
    }
    return zc::none;
  }

  bool freezeModuleIdentities() {
    if (packageRequest == zc::none) { return true; }
    if (identityRegistries == zc::none) { return false; }

    struct PendingModule final {
      source::BufferId buffer;
      ast::NodeId node;
      zc::Vector<identity::ModulePathSegment> path;
      identity::ModuleKey key;

      PendingModule(source::BufferId buffer, ast::NodeId node,
                    zc::Vector<identity::ModulePathSegment>&& path, identity::ModuleKey&& key)
          : buffer(zc::mv(buffer)), node(node), path(zc::mv(path)), key(zc::mv(key)) {}
      PendingModule(PendingModule&&) noexcept = default;
      PendingModule& operator=(PendingModule&&) noexcept = default;
      ZC_DISALLOW_COPY(PendingModule);
    };

    bool failed = false;
    zc::Vector<PendingModule> pending;
    auto lockedAsts = astMutex.lockShared();
    auto lockedInventories = definitionInventoryMutex.lockShared();
    ZC_IF_SOME(registries, identityRegistries) {
      uint32_t traversalOrdinal = 0;
      for (const auto& astEntry : *lockedAsts) {
        auto sourceId = sourceIdentity(astEntry.key);
        auto inventory = lockedInventories->find(astEntry.key);
        if (sourceId == zc::none || inventory == zc::none) {
          failed = true;
          continue;
        }
        ZC_IF_SOME(sourceHandle, sourceId) {
          auto sourceKey = registries.sourceFiles().lookup(sourceHandle);
          auto snapshot = registries.sourceSnapshot(sourceHandle);
          if (sourceKey == zc::none || snapshot == zc::none) {
            failed = true;
            continue;
          }
          ZC_IF_SOME(sourceValue, sourceKey) {
            ZC_IF_SOME(sourceSnapshot, snapshot) {
              ZC_IF_SOME(inventoryValue, inventory) {
                bool hasRootDeclaration = false;
                for (const auto& module : inventoryValue.modules()) {
                  hasRootDeclaration = hasRootDeclaration ||
                                       module.form == ast::ModuleDeclarationForm::RootDeclaration;
                }
                if (!hasRootDeclaration) {
                  auto rootName =
                      identity::ModulePathSegment::fromCanonical(sourceValue.crate().targetName());
                  if (rootName == zc::none) {
                    failed = true;
                  } else {
                    zc::Vector<identity::ModulePathSegment> path;
                    ZC_IF_SOME(value, rootName) { path.add(zc::mv(value)); }
                    zc::Vector<identity::ModulePathSegment> keyPath;
                    for (const auto& segment : path) { keyPath.add(segment.clone()); }
                    zc::Maybe<identity::SourceSpan> noAnchor;
                    auto key =
                        identity::ModuleKey::from(sourceValue.crate().clone(), zc::mv(keyPath),
                                                  sourceValue.clone(), zc::mv(noAnchor));
                    ZC_IF_SOME(value, key) {
                      auto retained = value.clone();
                      failed = registries.collectModule(zc::mv(value), traversalOrdinal++) !=
                                   identity::FrozenRegistryFailure::None ||
                               failed;
                      pending.add(PendingModule(astEntry.key, ast::NodeId(), zc::mv(path),
                                                zc::mv(retained)));
                    }
                    else { failed = true; }
                  }
                }

                for (const auto& module : inventoryValue.modules()) {
                  zc::Vector<identity::ModulePathSegment> path;
                  if (module.parentModuleNode) {
                    bool foundParent = false;
                    for (const auto& candidate : pending) {
                      if (candidate.buffer != astEntry.key ||
                          candidate.node != module.parentModuleNode) {
                        continue;
                      }
                      for (const auto& segment : candidate.path) { path.add(segment.clone()); }
                      foundParent = true;
                      break;
                    }
                    if (!foundParent) {
                      failed = true;
                      continue;
                    }
                  } else if (!hasRootDeclaration) {
                    for (const auto& candidate : pending) {
                      if (candidate.buffer != astEntry.key || candidate.node) { continue; }
                      for (const auto& segment : candidate.path) { path.add(segment.clone()); }
                      break;
                    }
                  }
                  auto name = identity::ModulePathSegment::fromSource(
                      astEntry.value.ident(module.declaredName));
                  if (name == zc::none) {
                    failed = true;
                    continue;
                  }
                  ZC_IF_SOME(value, name) { path.add(zc::mv(value)); }
                  const auto start =
                      sourceManager->getLocOffsetInBuffer(module.source.getStart(), astEntry.key);
                  const auto end =
                      sourceManager->getLocOffsetInBuffer(module.source.getEnd(), astEntry.key);
                  auto anchor = sourceSnapshot.span(start, end);
                  if (anchor == zc::none) {
                    failed = true;
                    continue;
                  }
                  zc::Vector<identity::ModulePathSegment> keyPath;
                  for (const auto& segment : path) { keyPath.add(segment.clone()); }
                  auto key = identity::ModuleKey::from(sourceValue.crate().clone(), zc::mv(keyPath),
                                                       sourceValue.clone(), zc::mv(anchor));
                  ZC_IF_SOME(value, key) {
                    auto retained = value.clone();
                    failed = registries.collectModule(zc::mv(value), traversalOrdinal++) !=
                                 identity::FrozenRegistryFailure::None ||
                             failed;
                    pending.add(
                        PendingModule(astEntry.key, module.node, zc::mv(path), zc::mv(retained)));
                  }
                  else { failed = true; }
                }
              }
            }
          }
        }
      }
      failed = registries.freezeModules() != identity::FrozenRegistryFailure::None || failed;
      if (!failed) {
        for (const auto& module : pending) {
          auto handle = registries.modules().find(module.key);
          ZC_IF_SOME(value, handle) {
            moduleIdentities.add(ModuleIdentityBinding{module.buffer, module.node, value});
          }
          else { failed = true; }
        }
      }
      if (failed) { emitIdentityFailures(registries, *diagnosticEngine); }
    }
    return !failed;
  }

  bool freezeDefinitionAndImplIdentities() {
    if (packageRequest == zc::none) { return true; }
    if (identityRegistries == zc::none) { return false; }

    struct PendingDefinition final {
      source::BufferId buffer;
      ast::NodeId node;
      identity::DefinitionKey key;
      PendingDefinition(source::BufferId buffer, ast::NodeId node, identity::DefinitionKey&& key)
          : buffer(zc::mv(buffer)), node(node), key(zc::mv(key)) {}
      PendingDefinition(PendingDefinition&&) noexcept = default;
      PendingDefinition& operator=(PendingDefinition&&) noexcept = default;
      ZC_DISALLOW_COPY(PendingDefinition);
    };
    struct PendingImpl final {
      source::BufferId buffer;
      ast::NodeId node;
      identity::ImplKey key;
      PendingImpl(source::BufferId buffer, ast::NodeId node, identity::ImplKey&& key)
          : buffer(zc::mv(buffer)), node(node), key(zc::mv(key)) {}
      PendingImpl(PendingImpl&&) noexcept = default;
      PendingImpl& operator=(PendingImpl&&) noexcept = default;
      ZC_DISALLOW_COPY(PendingImpl);
    };

    const auto sameParentPath = [](zc::ArrayPtr<const binder::StructuralIdentityParent> left,
                                   zc::ArrayPtr<const binder::StructuralIdentityParent> right) {
      if (left.size() != right.size()) { return false; }
      for (size_t index = 0; index < left.size(); ++index) {
        if (left[index].kind != right[index].kind || left[index].node != right[index].node) {
          return false;
        }
      }
      return true;
    };
    const auto siblingOrdinal =
        [&](const binder::DefinitionInventory& inventory, ast::NodeId moduleNode,
            zc::ArrayPtr<const binder::StructuralIdentityParent> parents, ast::NodeId node) {
          uint32_t ordinal = 0;
          for (const auto& candidate : inventory.definitions()) {
            if (candidate.node.value < node.value && candidate.moduleNode == moduleNode &&
                sameParentPath(candidate.parentPath.asPtr(), parents)) {
              ++ordinal;
            }
          }
          for (const auto& candidate : inventory.impls()) {
            if (candidate.node.value < node.value && candidate.moduleNode == moduleNode &&
                sameParentPath(candidate.parentPath.asPtr(), parents)) {
              ++ordinal;
            }
          }
          return ordinal;
        };

    bool failed = false;
    zc::Vector<PendingDefinition> pendingDefinitions;
    zc::Vector<PendingImpl> pendingImpls;
    auto lockedAsts = astMutex.lockShared();
    auto lockedInventories = definitionInventoryMutex.lockShared();
    ZC_IF_SOME(registries, identityRegistries) {
      uint32_t traversalOrdinal = 0;
      for (const auto& astEntry : *lockedAsts) {
        auto inventory = lockedInventories->find(astEntry.key);
        auto sourceId = sourceIdentity(astEntry.key);
        if (inventory == zc::none || sourceId == zc::none) {
          failed = true;
          continue;
        }
        ZC_IF_SOME(sourceHandle, sourceId) {
          auto snapshot = registries.sourceSnapshot(sourceHandle);
          if (snapshot == zc::none) {
            failed = true;
            continue;
          }
          ZC_IF_SOME(sourceSnapshot, snapshot) {
            ZC_IF_SOME(inventoryValue, inventory) {
              const auto definitionEntry =
                  [&](ast::NodeId node) -> zc::Maybe<const binder::DefinitionInventoryEntry&> {
                for (const auto& candidate : inventoryValue.definitions()) {
                  if (candidate.node == node) { return candidate; }
                }
                return zc::none;
              };
              const auto implEntry =
                  [&](ast::NodeId node) -> zc::Maybe<const binder::ImplInventoryEntry&> {
                for (const auto& candidate : inventoryValue.impls()) {
                  if (candidate.node == node) { return candidate; }
                }
                return zc::none;
              };
              const auto spanFor = [&](source::SourceRange range) {
                const auto start =
                    sourceManager->getLocOffsetInBuffer(range.getStart(), astEntry.key);
                const auto end = sourceManager->getLocOffsetInBuffer(range.getEnd(), astEntry.key);
                return sourceSnapshot.span(start, end);
              };
              const auto segmentFor = [&](const binder::DefinitionInventoryEntry& entry)
                  -> zc::Maybe<identity::DefinitionPathSegment> {
                zc::Maybe<identity::DefinitionNameKey> name;
                if (entry.nameKind == binder::InventoryDefinitionNameKind::Declared) {
                  auto declared = identity::DeclaredDefinitionName::fromSource(
                      astEntry.value.ident(entry.declaredName));
                  ZC_IF_SOME(value, declared) {
                    name = identity::DefinitionNameKey::declared(zc::mv(value));
                  }
                } else {
                  ZC_IF_SOME(role, entry.anonymousRole) {
                    name = identity::DefinitionNameKey::anonymous(role);
                  }
                }
                auto span = spanFor(entry.source);
                if (name == zc::none || span == zc::none) { return zc::none; }
                ZC_IF_SOME(nameValue, name) {
                  ZC_IF_SOME(spanValue, span) {
                    return identity::DefinitionPathSegment::from(
                        entry.kind, zc::mv(nameValue), zc::mv(spanValue),
                        siblingOrdinal(inventoryValue, entry.moduleNode, entry.parentPath.asPtr(),
                                       entry.node));
                  }
                }
                return zc::none;
              };
              const auto implSegmentFor = [&](const binder::ImplInventoryEntry& entry)
                  -> zc::Maybe<identity::ImplPathSegment> {
                auto span = spanFor(entry.source);
                ZC_IF_SOME(value, span) {
                  return identity::ImplPathSegment::from(
                      zc::mv(value), siblingOrdinal(inventoryValue, entry.moduleNode,
                                                    entry.parentPath.asPtr(), entry.node));
                }
                return zc::none;
              };

              for (const auto& definition : inventoryValue.definitions()) {
                auto moduleHandle = moduleIdentity(astEntry.key, definition.moduleNode);
                if (moduleHandle == zc::none) { moduleHandle = moduleIdentity(astEntry.key, {}); }
                if (moduleHandle == zc::none) {
                  failed = true;
                  continue;
                }
                zc::Vector<identity::DefinitionPathComponent> path;
                bool pathValid = true;
                for (const auto& parent : definition.parentPath) {
                  if (parent.kind == binder::StructuralIdentityParentKind::Definition) {
                    auto entry = definitionEntry(parent.node);
                    if (entry == zc::none) {
                      pathValid = false;
                      break;
                    }
                    ZC_IF_SOME(value, entry) {
                      auto segment = segmentFor(value);
                      if (segment == zc::none) {
                        pathValid = false;
                        break;
                      }
                      ZC_IF_SOME(segmentValue, segment) {
                        path.add(
                            identity::DefinitionPathComponent::definition(zc::mv(segmentValue)));
                      }
                    }
                  } else {
                    auto entry = implEntry(parent.node);
                    if (entry == zc::none) {
                      pathValid = false;
                      break;
                    }
                    ZC_IF_SOME(value, entry) {
                      auto segment = implSegmentFor(value);
                      if (segment == zc::none) {
                        pathValid = false;
                        break;
                      }
                      ZC_IF_SOME(segmentValue, segment) {
                        path.add(identity::DefinitionPathComponent::impl(zc::mv(segmentValue)));
                      }
                    }
                  }
                }
                auto current = segmentFor(definition);
                if (!pathValid || current == zc::none) {
                  failed = true;
                  continue;
                }
                ZC_IF_SOME(value, current) {
                  path.add(identity::DefinitionPathComponent::definition(zc::mv(value)));
                }
                ZC_IF_SOME(handle, moduleHandle) {
                  auto module = registries.modules().lookup(handle);
                  if (module == zc::none) {
                    failed = true;
                    continue;
                  }
                  ZC_IF_SOME(moduleValue, module) {
                    auto key = identity::DefinitionKey::from(moduleValue.clone(), zc::mv(path));
                    ZC_IF_SOME(value, key) {
                      auto retained = value.clone();
                      failed = registries.collectDefinition(zc::mv(value), traversalOrdinal++) !=
                                   identity::FrozenRegistryFailure::None ||
                               failed;
                      pendingDefinitions.add(
                          PendingDefinition(astEntry.key, definition.node, zc::mv(retained)));
                    }
                    else { failed = true; }
                  }
                }
              }
            }
          }
        }
      }
      failed = registries.freezeDefinitions() != identity::FrozenRegistryFailure::None || failed;
      if (!failed) {
        for (const auto& definition : pendingDefinitions) {
          auto handle = registries.definitions().find(definition.key);
          ZC_IF_SOME(value, handle) {
            definitionIdentities.add(
                DefinitionIdentityBinding{definition.buffer, definition.node, value});
          }
          else { failed = true; }
        }
      }

      traversalOrdinal = 0;
      if (!failed) {
        for (const auto& astEntry : *lockedAsts) {
          auto inventory = lockedInventories->find(astEntry.key);
          auto sourceId = sourceIdentity(astEntry.key);
          ZC_IF_SOME(inventoryValue, inventory) {
            ZC_IF_SOME(sourceHandle, sourceId) {
              ZC_IF_SOME(sourceSnapshot, registries.sourceSnapshot(sourceHandle)) {
                for (const auto& implementation : inventoryValue.impls()) {
                  auto moduleHandle = moduleIdentity(astEntry.key, implementation.moduleNode);
                  if (moduleHandle == zc::none) { moduleHandle = moduleIdentity(astEntry.key, {}); }
                  if (moduleHandle == zc::none) {
                    failed = true;
                    break;
                  }
                  zc::Vector<identity::DefinitionPathSegment> parentPath;
                  bool pathValid = true;
                  for (const auto& parent : implementation.parentPath) {
                    if (parent.kind == binder::StructuralIdentityParentKind::Impl) {
                      pathValid = false;
                      break;
                    }
                    for (const auto& definition : inventoryValue.definitions()) {
                      if (definition.node != parent.node) { continue; }
                      zc::Maybe<identity::DefinitionNameKey> name;
                      if (definition.nameKind == binder::InventoryDefinitionNameKind::Declared) {
                        auto declared = identity::DeclaredDefinitionName::fromSource(
                            astEntry.value.ident(definition.declaredName));
                        ZC_IF_SOME(value, declared) {
                          name = identity::DefinitionNameKey::declared(zc::mv(value));
                        }
                      } else {
                        ZC_IF_SOME(role, definition.anonymousRole) {
                          name = identity::DefinitionNameKey::anonymous(role);
                        }
                      }
                      const auto start = sourceManager->getLocOffsetInBuffer(
                          definition.source.getStart(), astEntry.key);
                      const auto end = sourceManager->getLocOffsetInBuffer(
                          definition.source.getEnd(), astEntry.key);
                      auto span = sourceSnapshot.span(start, end);
                      ZC_IF_SOME(nameValue, name) {
                        ZC_IF_SOME(spanValue, span) {
                          auto segment = identity::DefinitionPathSegment::from(
                              definition.kind, zc::mv(nameValue), zc::mv(spanValue),
                              siblingOrdinal(inventoryValue, definition.moduleNode,
                                             definition.parentPath.asPtr(), definition.node));
                          ZC_IF_SOME(value, segment) { parentPath.add(zc::mv(value)); }
                        }
                      }
                      break;
                    }
                  }
                  const auto start = sourceManager->getLocOffsetInBuffer(
                      implementation.source.getStart(), astEntry.key);
                  const auto end = sourceManager->getLocOffsetInBuffer(
                      implementation.source.getEnd(), astEntry.key);
                  auto span = sourceSnapshot.span(start, end);
                  ZC_IF_SOME(handle, moduleHandle) {
                    auto module = registries.modules().lookup(handle);
                    if (!pathValid || span == zc::none || module == zc::none) {
                      failed = true;
                      break;
                    }
                    ZC_IF_SOME(moduleValue, module) {
                      ZC_IF_SOME(spanValue, span) {
                        auto key = identity::ImplKey::from(
                            moduleValue.clone(), zc::mv(parentPath), zc::mv(spanValue),
                            siblingOrdinal(inventoryValue, implementation.moduleNode,
                                           implementation.parentPath.asPtr(), implementation.node));
                        ZC_IF_SOME(value, key) {
                          auto retained = value.clone();
                          failed = registries.collectImpl(zc::mv(value), traversalOrdinal++) !=
                                       identity::FrozenRegistryFailure::None ||
                                   failed;
                          pendingImpls.add(
                              PendingImpl(astEntry.key, implementation.node, zc::mv(retained)));
                        }
                        else { failed = true; }
                      }
                    }
                  }
                }
              }
            }
          }
        }
        for (const auto& astEntry : *lockedAsts) {
          binder::DefinitionIdentityMap identities;
          for (const auto& definition : definitionIdentities) {
            if (definition.buffer == astEntry.key &&
                !identities.insert(definition.node, definition.identity)) {
              failed = true;
            }
          }
          definitionIdentityMaps.upsert(astEntry.key, zc::mv(identities));
        }
      }
      failed = registries.freezeImpls() != identity::FrozenRegistryFailure::None || failed;
      if (!failed) {
        for (const auto& implementation : pendingImpls) {
          auto handle = registries.impls().find(implementation.key);
          ZC_IF_SOME(value, handle) {
            implIdentities.add(
                ImplIdentityBinding{implementation.buffer, implementation.node, value});
          }
          else { failed = true; }
        }
      }
      if (failed) { emitIdentityFailures(registries, *diagnosticEngine); }
    }
    return !failed;
  }
};

// ================================================================================
// CompilerSession

CompilerSession::CompilerSession(identity::SemanticContextFactory& contextFactory,
                                 const basic::LangOptions& langOpts,
                                 const basic::CompilerOptions& compilerOpts)
    : impl(zc::heap<Impl>(contextFactory, langOpts, compilerOpts)) {}
CompilerSession::~CompilerSession() noexcept(false) = default;

zc::Maybe<source::BufferId> CompilerSession::addSourceFile(const zc::StringPtr file) {
  const zc::Maybe<source::BufferId> bufferId =
      impl->sourceManager->getFileSystemSourceBufferID(file);
  if (bufferId == zc::none) {
    impl->diagnosticEngine->diagnose<diagnostics::DiagID::InvalidPath>(source::SourceLoc(), file);
  }
  return bufferId;
}

zc::Maybe<source::BufferId> CompilerSession::addPackageSourceFile(
    const zc::StringPtr file, const zc::StringPtr displayIdentifier,
    const package::FinalizedCompilationRoot& root) {
  const zc::Maybe<source::BufferId> bufferId =
      impl->sourceManager->getFileSystemSourceBufferID(file, displayIdentifier);
  if (bufferId == zc::none) {
    impl->diagnosticEngine->diagnose<diagnostics::DiagID::InvalidPath>(source::SourceLoc(),
                                                                       displayIdentifier);
  }
  ZC_IF_SOME(buffer, bufferId) {
    auto origin = sourceOriginFor(root);
    ZC_IF_SOME(value, origin) {
      impl->pendingSourceIdentities.upsert(
          buffer, identity::SourceFileKey::from(root.crateKey().clone(), zc::mv(value)));
    }
  }
  return bufferId;
}

const diagnostics::DiagnosticEngine& CompilerSession::getDiagnosticEngine() const {
  return *impl->diagnosticEngine;
}

diagnostics::DiagnosticEngine& CompilerSession::getDiagnosticEngine() {
  return *impl->diagnosticEngine;
}

const zc::HashMap<source::BufferId, ast::Tree>& CompilerSession::getASTs() const {
  auto lockedAsts = impl->astMutex.lockShared();
  return *lockedAsts;
}

const zc::HashMap<source::BufferId, ast::BindingMetadata>& CompilerSession::getBindingMetadata()
    const {
  auto lockedMetadata = impl->bindingMetadataMutex.lockShared();
  return *lockedMetadata;
}

size_t CompilerSession::getDefinitionInventoryCount() const {
  auto lockedInventories = impl->definitionInventoryMutex.lockShared();
  return lockedInventories->size();
}

zc::Maybe<binder::DefinitionInventory> CompilerSession::getDefinitionInventory(
    const source::BufferId& buffer) const {
  auto lockedInventories = impl->definitionInventoryMutex.lockShared();
  ZC_IF_SOME(inventory, lockedInventories->find(buffer)) { return inventory.clone(); }
  return zc::none;
}

const zc::HashMap<source::BufferId, type::TypeEnv>& CompilerSession::getTypeEnvs() const {
  auto lockedTypeEnvs = impl->typeEnvMutex.lockShared();
  return *lockedTypeEnvs;
}

bool CompilerSession::parseSources() {
  if (impl->diagnosticEngine->hasErrors()) { return false; }
  if (!impl->freezePackageInputIdentities()) { return false; }
  // Get BufferIds directly from SourceManager
  zc::Vector<source::BufferId> bufferIds = impl->sourceManager->getManagedBufferIds();

  {
    basic::ThreadPool threadPool;

    for (const source::BufferId& bufferId : bufferIds) {  // Iterate over the retrieved vector
      // Create a thread for each buffer ID
      threadPool.enqueue([this, bufferId]() -> void {
        // Perform lexing and parsing for the buffer.
        zc::Maybe<ast::Tree> maybeAst =
            basic::performParse(*impl->sourceManager, *impl->diagnosticEngine, impl->langOpts,
                                *impl->stringPool, bufferId);

        // Store the result if successful
        ZC_IF_SOME(ast, maybeAst) {
          auto inventory = binder::DefinitionInventory::collect(ast);
          {
            auto lockedInventories = impl->definitionInventoryMutex.lockExclusive();
            lockedInventories->upsert(bufferId, zc::mv(inventory));
          }
          // Lock the mutex to safely access the shared map
          auto lockedAsts = impl->astMutex.lockExclusive();
          // Insert or update the AST in the map
          lockedAsts->upsert(bufferId, zc::mv(ast));
        }
        // Errors during parsing should be reported via the DiagnosticEngine
      });
    }
  }

  if (impl->diagnosticEngine->hasErrors() || !impl->freezeModuleIdentities() ||
      !impl->freezeDefinitionAndImplIdentities()) {
    return false;
  }
  if (impl->packageRequest == zc::none) {
    auto lockedAsts = impl->astMutex.lockShared();
    for (const auto& entry : *lockedAsts) {
      if (impl->definitionIdentityMaps.find(entry.key) == zc::none) {
        impl->definitionIdentityMaps.upsert(entry.key, binder::DefinitionIdentityMap());
      }
    }
  }

  // Return true if no errors were reported
  return !impl->diagnosticEngine->hasErrors();
}

bool CompilerSession::bindSources() {
  if (impl->diagnosticEngine->hasErrors()) { return false; }
  auto lockedAsts = impl->astMutex.lockShared();
  auto lockedMetadata = impl->bindingMetadataMutex.lockExclusive();
  for (const auto& entry : *lockedAsts) {
    auto identities = impl->definitionIdentityMaps.find(entry.key);
    if (identities == zc::none) { return false; }
    ast::BindingMetadata metadata;
    ZC_IF_SOME(identityMap, identities) {
      basic::performBind(*impl->symbolTable, *impl->diagnosticEngine, entry.value, identityMap,
                         metadata);
    }
    lockedMetadata->upsert(entry.key, zc::mv(metadata));
  }

  // Return true if no errors were reported
  return !impl->diagnosticEngine->hasErrors();
}

bool CompilerSession::checkSources() {
  if (impl->diagnosticEngine->hasErrors()) { return false; }
  auto lockedAsts = impl->astMutex.lockShared();
  auto lockedMetadata = impl->bindingMetadataMutex.lockShared();
  auto lockedTypeEnvs = impl->typeEnvMutex.lockExclusive();

  for (const auto& entry : *lockedAsts) {
    // Find the corresponding binding metadata
    auto metadataMaybe = lockedMetadata->find(entry.key);
    if (metadataMaybe == zc::none) continue;

    type::TypeEnv typeEnv(*impl->semanticTypeStore);
    ZC_IF_SOME(metadata, metadataMaybe) {
      basic::performCheck(*impl->symbolTable, *impl->diagnosticEngine, entry.value, metadata,
                          typeEnv);
      lockedTypeEnvs->upsert(entry.key, zc::mv(typeEnv));
    }
  }

  // Return true if no errors were reported
  return !impl->diagnosticEngine->hasErrors();
}

const symbol::SymbolTable& CompilerSession::getSymbolTable() const { return *impl->symbolTable; }

basic::StringPool& CompilerSession::getStringPool() { return *impl->stringPool; }

const basic::StringPool& CompilerSession::getStringPool() const { return *impl->stringPool; }

const basic::CompilerOptions& CompilerSession::getCompilerOptions() const {
  return impl->compilerOpts;
}

const source::SourceManager& CompilerSession::getSourceManager() const {
  return *impl->sourceManager;
}

identity::SemanticContextBrand CompilerSession::getSemanticContextBrand() const noexcept {
  return impl->contextBrand;
}

zc::Maybe<const identity::SemanticIdentityRegistrySet&> CompilerSession::getIdentityRegistries()
    const noexcept {
  ZC_IF_SOME(registries, impl->identityRegistries) { return registries; }
  return zc::none;
}

zc::Maybe<const type::SemanticTypeStore&> CompilerSession::getSemanticTypeStore() const noexcept {
  if (impl->semanticTypeStore == nullptr) { return zc::none; }
  return *impl->semanticTypeStore;
}

bool CompilerSession::installPackageCompilationRequest(
    package::VerifiedPackageCompilationRequest&& request) {
  if (impl->packageRequest != zc::none) { return false; }
  auto finalized = request.finalizeRoots(zc::none);
  ZC_IF_SOME(roots, finalized) { impl->finalizedRoots = zc::mv(roots); }
  impl->packageRequest = zc::mv(request);
  return true;
}

zc::Maybe<const package::VerifiedPackageCompilationRequest&>
CompilerSession::getPackageCompilationRequest() const noexcept {
  ZC_IF_SOME(request, impl->packageRequest) { return request; }
  return zc::none;
}

zc::ArrayPtr<const package::FinalizedCompilationRoot>
CompilerSession::getFinalizedCompilationRoots() const noexcept {
  return impl->finalizedRoots;
}

bool CompilerSession::installVerifiedTargetSelections(irgen::VerifiedTargetSelection&& host,
                                                      irgen::VerifiedTargetSelection&& target) {
  if (impl->verifiedHostTarget != zc::none || impl->verifiedTarget != zc::none ||
      host.packageSelection().registryRevision() != target.packageSelection().registryRevision()) {
    return false;
  }
  impl->verifiedHostTarget = zc::mv(host);
  impl->verifiedTarget = zc::mv(target);
  return true;
}

zc::Maybe<const irgen::VerifiedTargetSelection&> CompilerSession::getVerifiedHostTarget()
    const noexcept {
  ZC_IF_SOME(target, impl->verifiedHostTarget) { return target; }
  return zc::none;
}

zc::Maybe<const irgen::VerifiedTargetSelection&> CompilerSession::getVerifiedTarget()
    const noexcept {
  ZC_IF_SOME(target, impl->verifiedTarget) { return target; }
  return zc::none;
}

bool CompilerSession::installResolvedPackageGraph(
    package::PackageResolution&& graph,
    zc::Vector<package::ResolvedPackageSourceSnapshot>&& snapshots) {
  if (impl->packageGraph != zc::none || graph.packages().size() == 0 || snapshots.size() == 0) {
    return false;
  }
  for (const auto& selected : graph.packages()) {
    bool found = false;
    for (const auto& snapshot : snapshots) {
      if (selected.base().encode().asPtr() == snapshot.package().encode().asPtr()) {
        found = true;
        break;
      }
    }
    if (!found) { return false; }
  }
  for (const auto& snapshot : snapshots) {
    bool found = false;
    for (const auto& selected : graph.packages()) {
      if (selected.base().encode().asPtr() == snapshot.package().encode().asPtr()) {
        found = true;
        break;
      }
    }
    if (!found) { return false; }
  }

  bool identityFailure = false;
  ZC_IF_SOME(registries, impl->identityRegistries) {
    zc::Vector<zc::Array<uint8_t>> collectedPackageKeys;
    uint32_t traversalOrdinal = 0;
    for (const auto& selected : graph.packages()) {
      auto packageKey = selected.packageKey();
      auto encoded = packageKey.encode();
      bool alreadyCollected = false;
      for (const auto& prior : collectedPackageKeys) {
        if (sameBytes(prior.asPtr(), encoded.asPtr())) {
          alreadyCollected = true;
          break;
        }
      }
      if (alreadyCollected) { continue; }
      collectedPackageKeys.add(zc::mv(encoded));
      identityFailure = registries.collectPackage(zc::mv(packageKey), traversalOrdinal++) !=
                            identity::FrozenRegistryFailure::None ||
                        identityFailure;
    }
    identityFailure =
        registries.freezePackages() != identity::FrozenRegistryFailure::None || identityFailure;
    if (identityFailure) { emitIdentityFailures(registries, *impl->diagnosticEngine); }
  }
  else { identityFailure = true; }
  if (identityFailure) { return false; }

  impl->packageGraph = zc::mv(graph);
  impl->packageSnapshots = zc::mv(snapshots);
  return true;
}

zc::Maybe<const package::PackageResolution&> CompilerSession::getResolvedPackageGraph()
    const noexcept {
  ZC_IF_SOME(graph, impl->packageGraph) { return graph; }
  return zc::none;
}

zc::ArrayPtr<const package::ResolvedPackageSourceSnapshot>
CompilerSession::getResolvedPackageSnapshots() const noexcept {
  return impl->packageSnapshots;
}

zc::Maybe<package::MaterializationIssue> CompilerSession::finishResolvedPackageSnapshots() {
  zc::Maybe<package::MaterializationIssue> firstIssue;
  for (auto& snapshot : impl->packageSnapshots) {
    ZC_IF_SOME(issue, snapshot.finish()) {
      if (firstIssue == zc::none) { firstIssue = issue; }
    }
  }
  impl->packageSnapshots.clear();
  return firstIssue;
}

zc::Maybe<package::BuildScriptIssue> CompilerSession::executeBuildScriptPlan(
    package::VerifiedBuildScriptPlan&& plan, package::BuildScriptPlanExecutor& executor) {
  if (impl->packageRequest == zc::none || impl->verifiedHostTarget == zc::none ||
      impl->verifiedTarget == zc::none || impl->packageGraph == zc::none ||
      impl->packageSnapshots.size() == 0 || impl->buildScriptPlan != zc::none ||
      impl->buildScriptResults != zc::none) {
    return package::BuildScriptIssue::BuildResultIntegrityViolation;
  }

  zc::Vector<package::VerifiedBuildScriptResult> completed(plan.nodes().size());
  for (const auto& node : plan.nodes()) {
    bool packageFound = false;
    ZC_IF_SOME(graph, impl->packageGraph) {
      for (const auto& selected : graph.packages()) {
        if (selected.packageKey().encode().asPtr() ==
            node.key().preparatory().package().encode().asPtr()) {
          packageFound = true;
          break;
        }
      }
    }
    if (!packageFound || node.key().preparatory().targetName() != node.contract().target().name()) {
      return package::BuildScriptIssue::BuildResultIntegrityViolation;
    }
  }
  for (const auto nodeIndex : plan.executionOrder()) {
    const auto& node = plan.nodes()[nodeIndex];
    auto executed = executor.execute(node, completed);
    if (executed.is<package::BuildScriptIssue>()) {
      return executed.get<package::BuildScriptIssue>();
    }
    auto result = zc::mv(executed.get<package::VerifiedBuildScriptResult>());
    if (result.output().preparatoryKey().encode().asPtr() !=
        node.key().preparatory().encode().asPtr()) {
      return package::BuildScriptIssue::BuildResultIntegrityViolation;
    }
    completed.add(zc::mv(result));
  }

  zc::Vector<identity::PreparatoryBuildScriptKey> planKeys(plan.nodes().size());
  for (const auto& node : plan.nodes()) { planKeys.add(node.key().preparatory().clone()); }
  auto results = package::VerifiedBuildScriptResultSet::from(zc::mv(planKeys), zc::mv(completed));
  if (results == zc::none) { return package::BuildScriptIssue::BuildResultIntegrityViolation; }
  ZC_IF_SOME(value, results) {
    zc::Maybe<const package::VerifiedBuildScriptResultSet&> resultView = value;
    zc::Maybe<zc::Vector<package::FinalizedCompilationRoot>> finalized;
    ZC_IF_SOME(request, impl->packageRequest) { finalized = request.finalizeRoots(resultView); }
    if (finalized == zc::none) { return package::BuildScriptIssue::BuildResultIntegrityViolation; }
    ZC_IF_SOME(roots, finalized) { impl->finalizedRoots = zc::mv(roots); }
  }
  impl->buildScriptPlan = zc::mv(plan);
  ZC_IF_SOME(value, results) { impl->buildScriptResults = zc::mv(value); }
  return zc::none;
}

zc::Maybe<const package::VerifiedBuildScriptPlan&> CompilerSession::getBuildScriptPlan()
    const noexcept {
  ZC_IF_SOME(plan, impl->buildScriptPlan) { return plan; }
  return zc::none;
}

zc::Maybe<const package::VerifiedBuildScriptResultSet&> CompilerSession::getBuildScriptResults()
    const noexcept {
  ZC_IF_SOME(results, impl->buildScriptResults) { return results; }
  return zc::none;
}

}  // namespace driver
}  // namespace compiler
}  // namespace zomlang
