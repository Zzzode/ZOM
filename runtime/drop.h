// Copyright (c) 2026 Zode.Z. All rights reserved.
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

namespace zomlang {
namespace runtime {

// The drop ABI is the raw-symbol boundary the ownership rail lowers a logical
// builtin drop to. `__zom_drop` receives the address of the value being
// dropped; the receiver parameter is fixed now so the eventual lowering call
// site introduces no ABI change. A builtin (trivial) drop has no runtime
// effect, so the current glue is a no-op that must accept any address,
// including nullptr, and return normally.

}  // namespace runtime
}  // namespace zomlang

extern "C" {

/// \brief Runtime drop glue for a logical builtin drop of the value at `value`.
/// \param value Address of the value being dropped; nullptr is accepted.
/// A builtin drop has no runtime effect, so this call returns without acting on
/// `value`. It never throws across the ABI boundary.
void __zom_drop(void* value) noexcept;
}
