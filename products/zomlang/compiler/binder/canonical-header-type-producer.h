// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/one-of.h"
#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/identity/canonical/header-type.h"

namespace zomlang::compiler::binder {

/// \brief One generic binder frame, ordered from the current item outwards.
struct CanonicalGenericBinderFrame final {
  /// Zero denotes an intentionally empty binder and still reserves its depth.
  ast::NodeId genericParameters;
};

enum class CanonicalHeaderSyntaxFailureKind : uint8_t {
  InvalidBinderStack = 0x01,
  InvalidTypeSyntax = 0x02,
  InvalidConstantExpression = 0x03,
  InvalidCallableSyntax = 0x04,
  InvalidReceiver = 0x05,
  InvalidBoundSyntax = 0x06,
  InvalidImplSyntax = 0x07
};

/// \brief Fail-closed result for syntax-to-canonical-header production.
struct CanonicalHeaderSyntaxFailure final {
  CanonicalHeaderSyntaxFailureKind kind;
  ast::NodeId node;
};

using CanonicalHeaderTypeProduction =
    zc::OneOf<identity::CanonicalHeaderTypeSyntax, CanonicalHeaderSyntaxFailure>;

/// \brief Produces RFC 0018 canonical header type syntax directly from the verified AST shape.
class CanonicalHeaderTypeProducer final {
public:
  /// \brief Validate every binder frame even when no produced type references a generic.
  ZC_NODISCARD static zc::Maybe<CanonicalHeaderSyntaxFailure> validateBinderStack(
      const ast::Tree& tree, zc::ArrayPtr<const CanonicalGenericBinderFrame> binders);
  /// \brief Normalize one complete type syntax node using innermost-first generic binders.
  ZC_NODISCARD static CanonicalHeaderTypeProduction produceType(
      const ast::Tree& tree, ast::NodeId type,
      zc::ArrayPtr<const CanonicalGenericBinderFrame> binders);
};

}  // namespace zomlang::compiler::binder
