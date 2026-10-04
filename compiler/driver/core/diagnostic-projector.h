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

#include <cstdint>

#include "compiler/basic/incident/compiler-incident.h"
#include "zc/core/common.h"

namespace zomlang::compiler::driver::core {

/// \brief Closed session-local invariant failures around core installation and publication.
enum class CoreSessionInvariantKind : uint8_t {
  InvalidInstallationState = 0x01,
  CoreCrateProjectionRejected = 0x02,
  CoreSourceRegistrationRejected = 0x03,
  CoreMarkerNotFound = 0x04,
  CoreAuthorityRejected = 0x05,
};

/// \brief Projects owner-local core invariants to registered compiler incidents.
class CoreDiagnosticProjector final {
public:
  ZC_NODISCARD static basic::CompilerIncidentDescriptor project(
      CoreSessionInvariantKind kind) noexcept;
};

}  // namespace zomlang::compiler::driver::core
