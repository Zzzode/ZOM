// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/graph/module-graph-revision.h"

namespace zomlang::compiler::binder {

ModuleGraphRevision::ModuleGraphRevision(const identity::Sha256Digest& digest) noexcept
    : value(digest) {}

ModuleGraphRevision ModuleGraphRevision::fromCanonicalDigest(
    const identity::Sha256Digest& digest) noexcept {
  return ModuleGraphRevision(digest);
}

const identity::Sha256Digest& ModuleGraphRevision::digest() const noexcept { return value; }

}  // namespace zomlang::compiler::binder
