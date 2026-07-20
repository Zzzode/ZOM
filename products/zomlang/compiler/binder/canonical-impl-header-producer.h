// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/binder/canonical-bound-syntax-occurrence.h"
#include "zomlang/compiler/binder/canonical-header-type-producer.h"
#include "zomlang/compiler/binder/definition-inventory.h"
#include "zomlang/compiler/identity/canonical-impl-header.h"

namespace zomlang::compiler::binder {

using CanonicalImplHeaderProduction =
    zc::OneOf<identity::CanonicalImplHeader, CanonicalHeaderSyntaxFailure>;

/// \brief Complete implementation header plus every pre-deduplication bound occurrence.
struct CanonicalImplHeaderProvenance final {
  identity::CanonicalImplHeader header;
  zc::Vector<CanonicalBoundSyntaxOccurrence> boundOccurrences;
};

using CanonicalImplHeaderProvenanceProduction =
    zc::OneOf<CanonicalImplHeaderProvenance, CanonicalHeaderSyntaxFailure>;

/// \brief Produces the canonical syntax-owned suffix of one RFC 0018 impl identity record.
class CanonicalImplHeaderProducer final {
public:
  /// \brief Requires a StandaloneImplDecl or MarkerImpl inventory entry.
  /// Enclosing binders are ordered from the immediate stable owner outwards; this producer
  /// inserts the implementation's own binder at depth zero, including an empty binder.
  ZC_NODISCARD static CanonicalImplHeaderProduction produce(
      const ast::Tree& tree, const ImplInventoryEntry& implementation,
      zc::ArrayPtr<const CanonicalGenericBinderFrame> enclosingBinders);

  /// \brief Produces the header and the complete source occurrence stream for its bounds.
  ZC_NODISCARD static CanonicalImplHeaderProvenanceProduction produceWithProvenance(
      const ast::Tree& tree, const ImplInventoryEntry& implementation,
      zc::ArrayPtr<const CanonicalGenericBinderFrame> enclosingBinders);
};

}  // namespace zomlang::compiler::binder
