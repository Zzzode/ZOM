#include "zomlang/compiler/source/core-distribution.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::source::core {
namespace {

ZC_TEST("Embedded core library inventory preserves the accepted source and role authority") {
  auto input = initialCoreDistributionInput();
  ZC_REQUIRE(input != zc::none);
  const auto& distribution = ZC_REQUIRE_NONNULL(input);
  const auto& record = distribution.record();

  ZC_EXPECT(record.editionYear() == 2026);
  ZC_REQUIRE(record.rootModule().segments().size() == 1);
  ZC_EXPECT(record.rootModule().segments()[0].text() == "core.zom"_zc);
  ZC_REQUIRE(record.preludeModule().segments().size() == 2);
  ZC_EXPECT(record.preludeModule().segments()[0].text() == "core"_zc);
  ZC_EXPECT(record.preludeModule().segments()[1].text() == "prelude.zom"_zc);

  const auto files = record.files();
  ZC_REQUIRE(files.size() == 3);
  ZC_REQUIRE(files[0].path().segments().size() == 1);
  ZC_EXPECT(files[0].path().segments()[0].text() == "core.zom"_zc);
  ZC_EXPECT(zc::encodeHex(files[0].digest().bytes()) ==
            "63421b0e8a03da646d4e6427231bc743df2731122b56d7e23ebe4425c9c8e9d7"_zc);
  ZC_REQUIRE(files[1].path().segments().size() == 2);
  ZC_EXPECT(files[1].path().segments()[0].text() == "core"_zc);
  ZC_EXPECT(files[1].path().segments()[1].text() == "marker.zom"_zc);
  ZC_EXPECT(zc::encodeHex(files[1].digest().bytes()) ==
            "0dcee31a4992b85ec803f7073e6c03519b6e963325559af28bed1443a86a9a0f"_zc);
  ZC_REQUIRE(files[2].path().segments().size() == 2);
  ZC_EXPECT(files[2].path().segments()[0].text() == "core"_zc);
  ZC_EXPECT(files[2].path().segments()[1].text() == "prelude.zom"_zc);
  ZC_EXPECT(zc::encodeHex(files[2].digest().bytes()) ==
            "2431a21b2a9bec11481b2c56d4b7099865f44df38515155391e3c9b0b12dd357"_zc);

  const auto roles = record.roles();
  ZC_REQUIRE(roles.size() == 2);
  ZC_EXPECT(roles[0].role() == CoreSemanticRole::Copy);
  ZC_EXPECT(roles[0].declaredName() == "Copy"_zc);
  ZC_REQUIRE(roles[0].module().size() == 2);
  ZC_EXPECT(roles[0].module()[0].text() == "core"_zc);
  ZC_EXPECT(roles[0].module()[1].text() == "marker"_zc);
  ZC_EXPECT(roles[1].role() == CoreSemanticRole::Linear);
  ZC_EXPECT(roles[1].declaredName() == "Linear"_zc);
  ZC_REQUIRE(roles[1].module().size() == 2);
  ZC_EXPECT(roles[1].module()[0].text() == "core"_zc);
  ZC_EXPECT(roles[1].module()[1].text() == "marker"_zc);

  const auto policy = distribution.policyTemplate().entries();
  ZC_REQUIRE(policy.size() == 1);
  ZC_EXPECT(policy[0].role == CoreSemanticRole::Copy);
  ZC_EXPECT(policy[0].policy.referenceRules().size() == 1);
  ZC_EXPECT(policy[0].policy.rawPointerMutabilities().size() == 2);

  auto recomputed = computeCoreDistributionDigest(record);
  ZC_REQUIRE(recomputed != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(recomputed) == distribution.digest());
  auto decoded = CoreDistributionInputRecord::decodeCanonical(distribution.encode().asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded).encode().asPtr() == distribution.encode().asPtr());
}

}  // namespace
}  // namespace zomlang::compiler::source::core
