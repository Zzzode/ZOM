// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zomlang/compiler/binder/binding-input.h"

namespace zomlang::compiler::binder {

/// \brief Private storage shared only by the production publisher and focused Binder fixtures.
struct VerifiedModuleGraph::Impl final {
  Impl(identity::SemanticContextBrand context, identity::SemanticContextFingerprint&& fingerprint,
       zc::Vector<identity::ModuleKey>&& modules, zc::Vector<identity::ModuleId>&& handles,
       zc::Vector<identity::SourceFileKey>&& sources,
       zc::Vector<VerifiedModuleDependencyEdge>&& edges, ModuleGraphRevision revision)
      : context(context),
        fingerprint(zc::mv(fingerprint)),
        modules(zc::mv(modules)),
        handles(zc::mv(handles)),
        sources(zc::mv(sources)),
        edges(zc::mv(edges)),
        revision(revision) {}

  identity::SemanticContextBrand context;
  identity::SemanticContextFingerprint fingerprint;
  zc::Vector<identity::ModuleKey> modules;
  zc::Vector<identity::ModuleId> handles;
  zc::Vector<identity::SourceFileKey> sources;
  zc::Vector<VerifiedModuleDependencyEdge> edges;
  ModuleGraphRevision revision;
};

}  // namespace zomlang::compiler::binder
