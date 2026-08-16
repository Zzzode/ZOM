// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/one-of.h"
#include "zomlang/compiler/driver/module-interface.h"

namespace zomlang::compiler::driver::core_library_query {
class VerifiedCoreModuleInterface;
}

namespace zomlang::compiler::driver {

/// \brief One verified interface originating from a user compilation unit.
struct UserVerifiedInterfaceSource final {
  const VerifiedModuleInterface& interface;
};

/// \brief One final verified interface originating from the toolchain core library.
struct ToolchainCoreVerifiedInterfaceSource final {
  const core_library_query::VerifiedCoreModuleInterface& interface;
};

/// \brief Closed borrowed source algebra accepted by ordinary interface consumers.
using VerifiedInterfaceSource =
    zc::OneOf<UserVerifiedInterfaceSource, ToolchainCoreVerifiedInterfaceSource>;

}  // namespace zomlang::compiler::driver
