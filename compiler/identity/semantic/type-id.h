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

#include "compiler/identity/handle.h"

namespace zomlang::compiler::type {

class SemanticTypeStore;

}  // namespace zomlang::compiler::type

namespace zomlang::compiler::identity {

/// \brief Issuance tag for the sole semantic type store in a semantic context.
struct SemanticTypeTag final {
private:
  ZC_NODISCARD static constexpr ContextHandle<SemanticTypeTag> issue(SemanticContextBrand context,
                                                                     uint32_t slot) noexcept {
    return ContextHandle<SemanticTypeTag>(context, slot);
  }

  ZC_NODISCARD static constexpr SemanticContextBrand context(
      ContextHandle<SemanticTypeTag> handle) noexcept {
    return handle.context;
  }

  ZC_NODISCARD static constexpr uint32_t slot(ContextHandle<SemanticTypeTag> handle) noexcept {
    return handle.slot;
  }

  friend class type::SemanticTypeStore;
};

using SemanticTypeId = ContextHandle<SemanticTypeTag>;

}  // namespace zomlang::compiler::identity
