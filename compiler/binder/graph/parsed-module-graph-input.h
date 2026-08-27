// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/binder/graph/parsed-module.h"
#include "compiler/identity/handle.h"

namespace zomlang::compiler::binder {

/// \brief One verified parser result assigned to its selected semantic module.
struct ParsedModuleGraphInput final {
  identity::ModuleId module;
  const VerifiedParsedModule& parsedModule;
};

}  // namespace zomlang::compiler::binder
