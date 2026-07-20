// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zomlang/compiler/ast/node-id.h"
#include "zomlang/compiler/identity/canonical-overload-header.h"

namespace zomlang::compiler::binder {

/// \brief One canonical bound paired with the syntax node that introduced it.
struct CanonicalBoundSyntaxOccurrence final {
  identity::CanonicalBoundObligation obligation;
  ast::NodeId node;
};

}  // namespace zomlang::compiler::binder
