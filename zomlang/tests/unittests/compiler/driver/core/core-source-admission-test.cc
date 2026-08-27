// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/source/core-source-admission.h"

#include "zc/core/time.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/source/core-source-catalog.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::source::core {
namespace {

class MemoryFreshDirectory final : public driver::package::FreshSourceDirectory {
public:
  explicit MemoryFreshDirectory(zc::Own<const zc::Directory>&& root) : rootValue(zc::mv(root)) {}
  ~MemoryFreshDirectory() noexcept override = default;

  const zc::Directory& root() const override { return *rootValue; }
  zc::Maybe<driver::package::MaterializationIssue> finish() override { return zc::none; }

private:
  zc::Own<const zc::Directory> rootValue;
};

class MemoryFreshDirectoryFactory final : public driver::package::FreshSourceDirectoryFactory {
public:
  driver::package::FreshSourceDirectoryResult create() override {
    zc::Own<const zc::Directory> root = zc::newInMemoryDirectory(zc::nullClock());
    return zc::Own<driver::package::FreshSourceDirectory>(
        zc::heap<MemoryFreshDirectory>(zc::mv(root)));
  }
};

zc::Own<zc::Directory> sourceTree(zc::StringPtr markerSource) {
  auto root = zc::newInMemoryDirectory(zc::nullClock());
  auto core = root->openFile(zc::Path({"core.zom"_zc}),
                             zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT);
  core->writeAll("module core;\n"_zc);
  auto marker = root->openFile(zc::Path({"core"_zc, "marker.zom"_zc}),
                               zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT);
  marker->writeAll(markerSource);
  auto prelude = root->openFile(zc::Path({"core"_zc, "prelude.zom"_zc}),
                                zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT);
  prelude->writeAll("module prelude;\n\nexport core::marker::{Copy, Linear};\n"_zc);
  return root;
}

CoreDistributionAdmissionResult admit(zc::StringPtr markerSource) {
  auto expected = initialCoreDistributionInput();
  ZC_REQUIRE(expected != zc::none);
  auto root = sourceTree(markerSource);
  MemoryFreshDirectoryFactory factory;
  CoreDistributionAdmission admission;
  return admission.admit(*root, factory, ZC_REQUIRE_NONNULL(expected), 2026);
}

}  // namespace

ZC_TEST("Core source admission verifies the exact fixed distribution and catalog") {
  auto result =
      admit("module marker;\n\nexport interface Copy {}\nexport interface Linear {}\n"_zc);
  ZC_REQUIRE(result.is<VerifiedCoreDistribution>());
  auto distribution = zc::mv(result.get<VerifiedCoreDistribution>());
  ZC_EXPECT(distribution.record().editionYear() == 2026);
  ZC_EXPECT(distribution.snapshots().size() == 3);
  auto catalog =
      CoreSourceCatalogAdmission::admit(distribution, tests::test_identity_detail::coreCrate());
  ZC_REQUIRE(catalog.is<AdmittedCoreSourceCatalog>());
  const auto& admitted = catalog.get<AdmittedCoreSourceCatalog>();
  ZC_REQUIRE(admitted.entries().size() == 3);
  ZC_EXPECT(admitted.entries()[0].module()[0].text() == "core"_zc);
  ZC_EXPECT(admitted.entries()[1].module()[1].text() == "marker"_zc);
  ZC_EXPECT(admitted.entries()[2].module()[1].text() == "prelude"_zc);
  for (const auto& entry : admitted.entries()) {
    ZC_EXPECT(entry.source().crate().unit().kind() == identity::CompilationUnitKind::Toolchain);
    ZC_EXPECT(entry.source().origin().kind() == identity::SourceOriginKind::CoreFile);
  }
}

ZC_TEST("Core source admission rejects source drift without fallback") {
  auto result =
      admit("module marker;\n\nexport interface Copy {}\nexport interface Changed {}\n"_zc);
  ZC_REQUIRE(result.is<CoreDistributionAdmissionFailure>());
  ZC_EXPECT(result.get<CoreDistributionAdmissionFailure>().issue() ==
            CoreLibraryIssue::DistributionMismatch);
}

}  // namespace zomlang::compiler::source::core
