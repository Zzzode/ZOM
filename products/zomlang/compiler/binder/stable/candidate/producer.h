// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/node-id.h"
#include "zomlang/compiler/binder/canonical-header-type-producer.h"
#include "zomlang/compiler/binder/identity-pre-admission.h"
#include "zomlang/compiler/binder/parsed-module.h"

namespace zomlang::compiler::binder {

class CandidateProducerImpl;

enum class StableIdentityCandidateFailureKind : uint8_t {
  InvalidModule = 0x01,
  InvalidSite = 0x02,
  InvalidOwner = 0x03,
  InvalidHeader = 0x04,
  InvalidRecord = 0x05,
  DuplicateNode = 0x06
};

struct StableIdentityCandidateFailure final {
  StableIdentityCandidateFailureKind kind;
  ast::NodeId node;
  CanonicalHeaderSyntaxFailureKind headerKind;
};

struct ProducedDefinitionIdentity final {
  ast::NodeId node;
  identity::DefinitionKey key;
};

struct ProducedImplIdentity final {
  ast::NodeId node;
  identity::ImplKey key;
};

struct ProducedDefinitionIdentitySite final {
  ast::NodeId node;
  identity::DefinitionKey key;
  IdentitySyntaxSiteKey site;
  identity::SourceSpan source;
};

struct ProducedImplIdentitySite final {
  ast::NodeId node;
  identity::ImplKey key;
  IdentitySyntaxSiteKey site;
  identity::SourceSpan source;
};

/// \brief Complete AST-produced pre-admission inventory for one parsed module.
class StableIdentityCandidateInventory final {
public:
  ~StableIdentityCandidateInventory() noexcept(false);
  StableIdentityCandidateInventory(StableIdentityCandidateInventory&&) noexcept;
  StableIdentityCandidateInventory& operator=(StableIdentityCandidateInventory&&) noexcept;
  ZC_DISALLOW_COPY(StableIdentityCandidateInventory);

  ZC_NODISCARD zc::ArrayPtr<const IdentitySyntaxSite> sites() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const PreAdmissionIdentityCandidate> candidates() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ProducedDefinitionIdentity> definitions() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ProducedImplIdentity> implementations() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ProducedDefinitionIdentitySite> definitionSites() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ProducedImplIdentitySite> implementationSites() const noexcept;

private:
  struct Impl;
  explicit StableIdentityCandidateInventory(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class CandidateProducer;
  friend class CandidateProducerImpl;
};

using StableIdentityCandidateProduction =
    zc::OneOf<StableIdentityCandidateInventory, StableIdentityCandidateFailure>;

/// \brief Produces complete revision-local identity syntax provenance without identity admission.
class IdentitySyntaxSiteInventoryProducer final {
public:
  ZC_NODISCARD static zc::Maybe<IdentitySyntaxSiteInventory> produce(
      const CanonicalParsedModule& parsedModule, const identity::ModuleKey& module,
      ast::NodeId moduleNode);
};

/// \brief Produces complete RFC 0018 records directly from parser AST authority.
class CandidateProducer final {
public:
  /// \brief Produces stable candidates in lexical topology order for one module syntax root.
  ZC_NODISCARD static StableIdentityCandidateProduction produce(
      const CanonicalParsedModule& parsedModule, const identity::ModuleKey& module,
      ast::NodeId moduleNode);
};

}  // namespace zomlang::compiler::binder
