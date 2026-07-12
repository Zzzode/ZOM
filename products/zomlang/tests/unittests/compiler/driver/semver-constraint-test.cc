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

#include "zomlang/compiler/driver/package/semver-constraint.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/sha256.h"

namespace zomlang::compiler::driver::package {
namespace {

SemVerConstraint constraint(zc::StringPtr source) {
  auto result = SemVerConstraint::parse(source);
  ZC_IF_SOME(admitted, result) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid SemVer constraint fixture");
}

void expectInterval(const SemVerConstraint& value, zc::StringPtr lower, bool lowerInclusive,
                    zc::StringPtr upper, bool upperInclusive) {
  ZC_REQUIRE(value.intervals().size() == 1);
  const auto& interval = value.intervals()[0];
  ZC_REQUIRE(interval.hasLower());
  ZC_REQUIRE(interval.hasUpper());
  ZC_EXPECT(interval.lower().version() == lower);
  ZC_EXPECT(interval.lower().inclusive() == lowerInclusive);
  ZC_EXPECT(interval.upper().version() == upper);
  ZC_EXPECT(interval.upper().inclusive() == upperInclusive);
}

identity::ResolvedVersion version(zc::StringPtr text) {
  auto value = identity::ResolvedVersion::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid resolved version fixture");
}

}  // namespace

ZC_TEST("SemVerConstraint.NormalizesBareCaretAndTildeRequirements") {
  auto bare = constraint("1.2.3"_zc);
  expectInterval(bare, "1.2.3"_zc, true, "2.0.0"_zc, false);

  auto zeroMinor = constraint("^0.2.3"_zc);
  expectInterval(zeroMinor, "0.2.3"_zc, true, "0.3.0"_zc, false);

  auto zeroPatch = constraint("^0.0.3"_zc);
  expectInterval(zeroPatch, "0.0.3"_zc, true, "0.0.4"_zc, false);

  auto allZero = constraint("^0.0.0"_zc);
  expectInterval(allZero, "0.0.0"_zc, true, "0.0.1"_zc, false);

  auto tilde = constraint("~1.2.3"_zc);
  expectInterval(tilde, "1.2.3"_zc, true, "1.3.0"_zc, false);
}

ZC_TEST("SemVerConstraint.SupportsUnboundedCoreComponents") {
  auto caret = constraint("^999999999999999999999999999999.2.3"_zc);
  expectInterval(caret, "999999999999999999999999999999.2.3"_zc, true,
                 "1000000000000000000000000000000.0.0"_zc, false);

  auto tilde = constraint("~1.999999999999999999999999999999.3"_zc);
  expectInterval(tilde, "1.999999999999999999999999999999.3"_zc, true,
                 "1.1000000000000000000000000000000.0"_zc, false);
}

ZC_TEST("SemVerConstraint.IntersectsComparatorsAndRetainsEmptyResults") {
  auto intersection = constraint(">1.0.0,<=2.0.0"_zc);
  expectInterval(intersection, "1.0.0"_zc, false, "2.0.0"_zc, true);

  auto exact = constraint(">=1.0.0,<=1.0.0"_zc);
  expectInterval(exact, "1.0.0"_zc, true, "1.0.0"_zc, true);

  auto empty = constraint(">2.0.0,<1.0.0"_zc);
  ZC_EXPECT(empty.intervals().size() == 0);

  auto exclusivePoint = constraint(">1.0.0,<=1.0.0"_zc);
  ZC_EXPECT(exclusivePoint.intervals().size() == 0);
}

ZC_TEST("SemVerConstraint.IntersectsNormalizedIntervalsAndTestsMembership") {
  auto left = constraint(">=1.0.0,<3.0.0"_zc);
  auto right = constraint(">2.0.0,<=4.0.0"_zc);
  auto intersection = SemVerConstraint::intersect(left, right);
  expectInterval(intersection, "2.0.0"_zc, false, "3.0.0"_zc, false);
  ZC_EXPECT(!intersection.allows(version("2.0.0"_zc)));
  ZC_EXPECT(intersection.allows(version("2.5.0"_zc)));
  ZC_EXPECT(!intersection.allows(version("3.0.0"_zc)));

  auto empty = SemVerConstraint::intersect(constraint("<1.0.0"_zc), constraint(">2.0.0"_zc));
  ZC_EXPECT(empty.intervals().size() == 0);
  ZC_EXPECT(!empty.allows(version("1.5.0"_zc)));
}

ZC_TEST("SemVerConstraint.RequiresPrereleaseCoreAdmissionFromEveryConstraint") {
  auto prerelease = version("1.0.0-alpha"_zc);
  ZC_EXPECT(constraint(">=1.0.0-alpha,<2.0.0"_zc).allows(prerelease));
  ZC_EXPECT(!constraint(">=0.9.0,<2.0.0"_zc).allows(prerelease));
  auto intersection = SemVerConstraint::intersect(constraint(">=1.0.0-alpha,<2.0.0"_zc),
                                                  constraint(">=0.9.0,<2.0.0"_zc));
  ZC_EXPECT(!intersection.allows(prerelease));
}

ZC_TEST("SemVerConstraint.UsesSemVerPrereleaseOrderingAndCanonicalCores") {
  auto numericOrder = constraint(">=1.0.0-alpha.10,<=1.0.0-alpha.2"_zc);
  ZC_EXPECT(numericOrder.intervals().size() == 0);
  ZC_REQUIRE(numericOrder.prereleaseCores().size() == 1);
  ZC_EXPECT(numericOrder.prereleaseCores()[0].major() == "1"_zc);
  ZC_EXPECT(numericOrder.prereleaseCores()[0].minor() == "0"_zc);
  ZC_EXPECT(numericOrder.prereleaseCores()[0].patch() == "0"_zc);

  auto sorted = constraint(">=10.0.0-alpha,>=2.0.0-beta"_zc);
  ZC_REQUIRE(sorted.prereleaseCores().size() == 2);
  ZC_EXPECT(sorted.prereleaseCores()[0].major() == "2"_zc);
  ZC_EXPECT(sorted.prereleaseCores()[1].major() == "10"_zc);
}

ZC_TEST("SemVerConstraint.RejectsNonGrammarAndBuildMetadata") {
  for (const auto invalid :
       {""_zc, " 1.2.3"_zc, "1.2.3 "_zc, ">= 1.2.3"_zc, "1.2.3+build"_zc, "1.2"_zc, ">"_zc,
        ",1.2.3"_zc, "1.2.3,"_zc, "1.2.3,,2.0.0"_zc, "==1.2.3"_zc}) {
    ZC_EXPECT(SemVerConstraint::parse(invalid) == zc::none);
  }
}

ZC_TEST("SemVerConstraint.PassesFixedCanonicalCodecVector") {
  const uint8_t expected[] = {
      0, 0, 0, 0, 0, 0, 0, 1, 1, 0,   0,   0,   0,   0,   0, 0, 5, '1', '.', '2', '.', '3', 1,
      1, 0, 0, 0, 0, 0, 0, 0, 5, '2', '.', '0', '.', '0', 0, 0, 0, 0,   0,   0,   0,   0,   0,
  };
  auto value = constraint("1.2.3"_zc);
  auto encoded = value.encode();
  ZC_EXPECT(encoded.size() == 46);
  ZC_EXPECT(encoded.asPtr() == zc::arrayPtr(expected));
  auto digest = identity::sha256(encoded.asPtr());
  ZC_REQUIRE(digest != zc::none);
  ZC_IF_SOME(admitted, digest) {
    ZC_EXPECT(zc::encodeHex(admitted.bytes()) ==
              "70e3b84b061844753bfd04eccf9924963c468242e2cf487ba1a4e2db1bbbf9aa"_zc);
  }
}

}  // namespace zomlang::compiler::driver::package
