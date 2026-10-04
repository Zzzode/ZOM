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

#include "compiler/checker/checker-identity-authority.h"
#include "compiler/driver/session/compiler-session.h"
#include "zc/core/string.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::driver::core_library_test {

/// \brief Returns the three core source inputs used by session tests.
inline zc::Vector<CoreSourceInput> coreSourceInputs() {
  zc::Vector<CoreSourceInput> inputs(3);
  inputs.add(CoreSourceInput{zc::heapString("core"_zc), zc::heapString("core.zom"_zc),
                             zc::heapArray<zc::byte>(zc::StringPtr("module core;\n").asBytes())});
  inputs.add(CoreSourceInput{
      zc::heapString("core.marker"_zc), zc::heapString("core/marker.zom"_zc),
      zc::heapArray<zc::byte>(
          zc::StringPtr("module marker;\n\nexport interface Copy {}\nexport interface Linear {}\n")
              .asBytes())});
  inputs.add(CoreSourceInput{
      zc::heapString("core.prelude"_zc), zc::heapString("core/prelude.zom"_zc),
      zc::heapArray<zc::byte>(
          zc::StringPtr("module prelude;\n\nexport core::marker::{Copy, Linear};\n").asBytes())});
  return inputs;
}

/// \brief Installs the standard core sources into one session.
inline void installCoreSources(CompilerSession& session) {
  auto inputs = coreSourceInputs();
  ZC_REQUIRE(session.installCoreSources(zc::mv(inputs)));
}

/// \brief Returns the verified standard marker authority after successful checking.
inline zc::Maybe<const core::VerifiedCoreStandardMarkerAuthority&> standardMarkerAuthority(
    CompilerSession& session) {
  return session.getStandardMarkerAuthority();
}

inline bool isUserPackageModule(const checker::CheckerIdentityAuthority& authority,
                                const checker::CheckerIdentityAuthority::BoundModuleView& module) {
  auto crate = authority.crate(module.crate());
  return crate != zc::none &&
         ZC_ASSERT_NONNULL(crate).key().unit().kind() == identity::CompilationUnitKind::UserPackage;
}

inline size_t userBoundModuleCount(const checker::CheckerIdentityAuthority& authority) {
  size_t count = 0;
  for (const auto& module : authority.modules()) {
    if (isUserPackageModule(authority, module)) { ++count; }
  }
  return count;
}

inline const checker::CheckerIdentityAuthority::BoundModuleView& soleUserBoundModule(
    const checker::CheckerIdentityAuthority& authority) {
  zc::Maybe<size_t> selected;
  const auto modules = authority.modules();
  for (size_t index = 0; index < modules.size(); ++index) {
    if (!isUserPackageModule(authority, modules[index])) { continue; }
    ZC_REQUIRE(selected == zc::none);
    selected = index;
  }
  return modules[ZC_REQUIRE_NONNULL(selected)];
}

}  // namespace zomlang::compiler::driver::core_library_test
