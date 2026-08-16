// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/core/library.h"

namespace zomlang::compiler::driver::core {
namespace {

bool sameModule(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

bool modulesMatchGraph(zc::ArrayPtr<const VerifiedCoreModule> modules,
                       zc::ArrayPtr<const identity::ModuleKey> graphModules) {
  if (modules.size() != graphModules.size() || modules.size() == 0) { return false; }
  for (size_t index = 0; index < modules.size(); ++index) {
    if (!sameModule(modules[index].module(), graphModules[index])) { return false; }
  }
  return true;
}

}  // namespace

struct VerifiedCoreModule::Impl final {
  Impl(identity::ModuleKey&& module, InterfaceLease&& interface) noexcept
      : module(zc::mv(module)), interface(zc::mv(interface)) {}
  identity::ModuleKey module;
  InterfaceLease interface;
};

VerifiedCoreModule::VerifiedCoreModule(zc::Own<Impl>&& value) noexcept : impl(zc::mv(value)) {}
VerifiedCoreModule::~VerifiedCoreModule() noexcept(false) = default;
VerifiedCoreModule::VerifiedCoreModule(VerifiedCoreModule&&) noexcept = default;
VerifiedCoreModule& VerifiedCoreModule::operator=(VerifiedCoreModule&&) noexcept = default;

zc::Maybe<VerifiedCoreModule> VerifiedCoreModule::from(identity::ModuleKey&& module,
                                                       InterfaceLease&& interface) {
  const auto& record = interface.capability().record();
  if (!sameModule(module, record.module())) { return zc::none; }
  return VerifiedCoreModule(zc::heap<Impl>(zc::mv(module), zc::mv(interface)));
}

VerifiedCoreModule VerifiedCoreModule::clone() const {
  return VerifiedCoreModule(zc::heap<Impl>(impl->module.clone(), impl->interface.retain()));
}
const identity::ModuleKey& VerifiedCoreModule::module() const noexcept { return impl->module; }
const VerifiedCoreModule::InterfaceLease& VerifiedCoreModule::interfaceLease() const noexcept {
  return impl->interface;
}

struct VerifiedCoreLibrary::Impl final {
  Impl(identity::SemanticContextBrand context, identity::SemanticContextFingerprint&& fingerprint,
       incremental_binding_query::CompilationRootSetQueryKey&& contextRoots,
       query::DatabaseRevision revision, const identity::Sha256Digest& distribution,
       core_library_query::CoreModuleGraphRecord&& graph, zc::Vector<VerifiedCoreModule>&& modules,
       identity::ModuleKey&& prelude, AuthorityLease&& authority) noexcept
      : context(context),
        fingerprint(zc::mv(fingerprint)),
        contextRoots(zc::mv(contextRoots)),
        revision(revision),
        distribution(distribution),
        graph(zc::mv(graph)),
        modules(zc::mv(modules)),
        prelude(zc::mv(prelude)),
        authority(zc::mv(authority)) {}
  identity::SemanticContextBrand context;
  identity::SemanticContextFingerprint fingerprint;
  incremental_binding_query::CompilationRootSetQueryKey contextRoots;
  query::DatabaseRevision revision;
  identity::Sha256Digest distribution;
  core_library_query::CoreModuleGraphRecord graph;
  zc::Vector<VerifiedCoreModule> modules;
  identity::ModuleKey prelude;
  AuthorityLease authority;
};

VerifiedCoreLibrary::VerifiedCoreLibrary(zc::Own<Impl>&& value) noexcept : impl(zc::mv(value)) {}
VerifiedCoreLibrary::~VerifiedCoreLibrary() noexcept(false) = default;
VerifiedCoreLibrary::VerifiedCoreLibrary(VerifiedCoreLibrary&&) noexcept = default;
VerifiedCoreLibrary& VerifiedCoreLibrary::operator=(VerifiedCoreLibrary&&) noexcept = default;

zc::Maybe<VerifiedCoreLibrary> VerifiedCoreLibrary::from(
    identity::SemanticContextBrand context, identity::SemanticContextFingerprint&& fingerprint,
    incremental_binding_query::CompilationRootSetQueryKey&& contextRoots,
    query::DatabaseRevision revision, const identity::Sha256Digest& distribution,
    core_library_query::CoreModuleGraphRecord&& graph, zc::Vector<VerifiedCoreModule>&& modules,
    identity::ModuleKey&& prelude, AuthorityLease&& authority) {
  const auto& authorityValue = authority.capability();
  if (!context.isValid() || authority.revision() != revision ||
      authority.arenaRevision() != revision || authorityValue.context() != context ||
      authorityValue.fingerprint().digest() != fingerprint.digest() ||
      authorityValue.record().core().encode().asPtr() != graph.core().encode().asPtr() ||
      authorityValue.record().coreContext().digest() != graph.coreContext().digest() ||
      authorityValue.shapes().distribution() != distribution ||
      authorityValue.policies().distribution() != distribution ||
      !modulesMatchGraph(modules.asPtr(), graph.modules())) {
    return zc::none;
  }
  size_t preludeMatches = 0;
  for (const auto& module : modules) {
    if (module.interfaceLease().revision() != revision ||
        module.interfaceLease().arenaRevision() != revision ||
        module.interfaceLease().capability().context() != context ||
        module.interfaceLease().capability().fingerprint().digest() != fingerprint.digest() ||
        module.interfaceLease().capability().record().coreContext().digest() !=
            graph.coreContext().digest()) {
      return zc::none;
    }
    if (sameModule(module.module(), prelude)) { ++preludeMatches; }
  }
  if (preludeMatches != 1 ||
      authorityValue.authority().prelude().encode().asPtr() != prelude.encode().asPtr()) {
    return zc::none;
  }
  return VerifiedCoreLibrary(zc::heap<Impl>(context, zc::mv(fingerprint), zc::mv(contextRoots),
                                            revision, distribution, zc::mv(graph), zc::mv(modules),
                                            zc::mv(prelude), zc::mv(authority)));
}

identity::SemanticContextBrand VerifiedCoreLibrary::context() const noexcept {
  return impl->context;
}
const identity::SemanticContextFingerprint& VerifiedCoreLibrary::fingerprint() const noexcept {
  return impl->fingerprint;
}
const incremental_binding_query::CompilationRootSetQueryKey& VerifiedCoreLibrary::contextRoots()
    const noexcept {
  return impl->contextRoots;
}
query::DatabaseRevision VerifiedCoreLibrary::revision() const noexcept { return impl->revision; }
const identity::Sha256Digest& VerifiedCoreLibrary::distribution() const noexcept {
  return impl->distribution;
}
const core_library_query::CoreModuleGraphRecord& VerifiedCoreLibrary::graph() const noexcept {
  return impl->graph;
}
zc::ArrayPtr<const VerifiedCoreModule> VerifiedCoreLibrary::modules() const noexcept {
  return impl->modules.asPtr();
}
const identity::ModuleKey& VerifiedCoreLibrary::prelude() const noexcept { return impl->prelude; }
const VerifiedCoreLibrary::AuthorityLease& VerifiedCoreLibrary::authorityLease() const noexcept {
  return impl->authority;
}

}  // namespace zomlang::compiler::driver::core
