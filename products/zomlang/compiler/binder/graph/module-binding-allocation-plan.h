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
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/one-of.h"
#include "zomlang/compiler/binder/stable/stable-binding-facts.h"

namespace zomlang::compiler::binder {

/// \brief Builds deterministic dense allocation ranges from canonical binding facts.
class ModuleBindingAllocationPlanner final {
public:
  /// \brief Computes one range for every canonical skeleton body owner.
  ZC_NODISCARD static zc::Maybe<ModuleBindingAllocationPlan> from(
      const BoundModuleSkeleton& skeleton, zc::ArrayPtr<const BoundOwnerBody> bodies);
  /// \brief Independently reconstructs and checks the complete allocation plan.
  ZC_NODISCARD static bool verify(const BoundModuleSkeleton& skeleton,
                                  zc::ArrayPtr<const BoundOwnerBody> bodies,
                                  const ModuleBindingAllocationPlan& plan);
};

}  // namespace zomlang::compiler::binder
