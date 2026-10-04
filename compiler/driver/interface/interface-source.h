// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include "compiler/driver/interface/module-interface.h"

namespace zomlang::compiler::driver {

/// \brief One verified interface originating from any compilation unit.
///
/// Core library modules now go through the normal parser-binder-checker pipeline
/// and produce ordinary VerifiedModuleInterface objects, so no separate
/// toolchain-core wrapper is needed.
struct VerifiedInterfaceSource final {
  const VerifiedModuleInterface& interface;
};

}  // namespace zomlang::compiler::driver
