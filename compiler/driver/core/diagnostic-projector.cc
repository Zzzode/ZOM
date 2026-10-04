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

#include "compiler/driver/core/diagnostic-projector.h"

namespace zomlang::compiler::driver::core {

basic::CompilerIncidentDescriptor CoreDiagnosticProjector::project(
    CoreSessionInvariantKind kind) noexcept {
  return basic::CompilerIncidentDescriptor(
      basic::CompilerIncidentDomain::Driver, basic::CompilerIncidentPhaseKey(0x0205),
      basic::CompilerIncidentKindKey(0x0600U + static_cast<uint32_t>(kind)),
      basic::CompilerIncidentProducerKey(0x0205), 1);
}

}  // namespace zomlang::compiler::driver::core
