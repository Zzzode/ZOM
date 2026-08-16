// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/filesystem.h"
#include "zc/core/time.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/checker/checker-identity-authority.h"
#include "zomlang/compiler/driver/session/compiler-session.h"
#include "zomlang/compiler/driver/package/source-snapshot.h"
#include "zomlang/compiler/source/core-source-admission.h"

namespace zomlang::compiler::driver::core_library_test {

class CoreLibraryFreshDirectory final : public package::FreshSourceDirectory {
public:
  explicit CoreLibraryFreshDirectory(zc::Own<const zc::Directory>&& root) noexcept
      : rootValue(zc::mv(root)) {}
  ~CoreLibraryFreshDirectory() noexcept override = default;

  const zc::Directory& root() const override { return *rootValue; }
  zc::Maybe<package::MaterializationIssue> finish() override { return zc::none; }

private:
  zc::Own<const zc::Directory> rootValue;
};

class CoreLibraryFreshDirectoryFactory final : public package::FreshSourceDirectoryFactory {
public:
  package::FreshSourceDirectoryResult create() override {
    zc::Own<const zc::Directory> root = zc::newInMemoryDirectory(zc::nullClock());
    return zc::Own<package::FreshSourceDirectory>(
        zc::heap<CoreLibraryFreshDirectory>(zc::mv(root)));
  }
};

inline source::core::VerifiedCoreDistribution admittedCoreDistribution() {
  auto root = zc::newInMemoryDirectory(zc::nullClock());
  root->openFile(zc::Path("core.zom"_zc), zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll("module core;\n"_zc);
  root->openFile(zc::Path({"core"_zc, "marker.zom"_zc}),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll("module marker;\n\nexport interface Copy {}\nexport interface Linear {}\n"_zc);
  root->openFile(zc::Path({"core"_zc, "prelude.zom"_zc}),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll("module prelude;\n\nexport core::marker::{Copy, Linear};\n"_zc);
  auto expected = source::core::initialCoreDistributionInput();
  ZC_REQUIRE(expected != zc::none);
  CoreLibraryFreshDirectoryFactory factory;
  source::core::CoreDistributionAdmission admission;
  auto admitted = admission.admit(*root, factory, ZC_REQUIRE_NONNULL(expected), 2026);
  ZC_REQUIRE(admitted.is<source::core::VerifiedCoreDistribution>());
  return zc::mv(admitted.get<source::core::VerifiedCoreDistribution>());
}

inline void installCoreDistribution(CompilerSession& session) {
  auto distribution = admittedCoreDistribution();
  ZC_REQUIRE(session.installVerifiedCoreDistribution(distribution));
}

inline zc::Maybe<core::VerifiedCoreLibrary> materializeCoreLibrary(
    CompilerSession& session, const checker::CheckerIdentityAuthority& authority) {
  for (const auto& crate : authority.graphLease().capability().crates()) {
    if (crate.key().unit().kind() != identity::CompilationUnitKind::Toolchain ||
        crate.key().unit().toolchain().component() != identity::ToolchainComponent::Core) {
      continue;
    }
    return session.materializeCoreLibrary(crate.key());
  }
  return zc::none;
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
