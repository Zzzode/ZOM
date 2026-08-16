// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/binder/canonical/canonical-bound-syntax-occurrence.h"
#include "zomlang/compiler/binder/canonical/canonical-header-type-producer.h"
#include "zomlang/compiler/binder/metadata/definition-inventory.h"
#include "zomlang/compiler/identity/crypto/overload-header-digest.h"

namespace zomlang::compiler::binder {

using CanonicalDefinitionHeaderProduction =
    zc::OneOf<identity::OverloadHeaderAuthority, CanonicalHeaderSyntaxFailure>;

/// \brief Complete callable authority plus every pre-deduplication bound occurrence.
struct CanonicalDefinitionHeaderProvenance final {
  identity::OverloadHeaderAuthority authority;
  zc::Vector<CanonicalBoundSyntaxOccurrence> boundOccurrences;
};

using CanonicalDefinitionHeaderProvenanceProduction =
    zc::OneOf<CanonicalDefinitionHeaderProvenance, CanonicalHeaderSyntaxFailure>;

/// \brief Produces one complete RFC 0018 overload-header authority from callable AST syntax.
class CanonicalDefinitionHeaderProducer final {
public:
  /// \brief Requires a Function, Method, Constructor, or Extern definition inventory entry.
  /// Enclosing binders are ordered from the immediate stable owner outwards; the producer
  /// inserts the callable's own binder at depth zero, including an empty binder.
  ZC_NODISCARD static CanonicalDefinitionHeaderProduction produce(
      const ast::Tree& tree, const DefinitionInventoryEntry& definition,
      zc::ArrayPtr<const CanonicalGenericBinderFrame> enclosingBinders);

  /// \brief Produces the authority and the complete source occurrence stream for its bounds.
  ZC_NODISCARD static CanonicalDefinitionHeaderProvenanceProduction produceWithProvenance(
      const ast::Tree& tree, const DefinitionInventoryEntry& definition,
      zc::ArrayPtr<const CanonicalGenericBinderFrame> enclosingBinders);
};

}  // namespace zomlang::compiler::binder
