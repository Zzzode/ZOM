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

#include "zomlang/compiler/identity/crypto/overload-header-digest.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::identity {
namespace {

DeclaredDefinitionName declaredName(zc::StringPtr text) {
  auto value = DeclaredDefinitionName::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid overload digest test name");
}

OverloadHeader header(zc::StringPtr name) {
  zc::Maybe<ReceiverShape> receiver;
  zc::Vector<CanonicalGenericParameter> generics;
  zc::Vector<CanonicalBoundObligation> obligations;
  zc::Vector<CanonicalCallableParameter> parameters;
  zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>> raises;
  zc::Maybe<ExternalAbi> externalAbi;
  auto value = OverloadHeader::from(
      CallableHeaderKind::Function, declaredName(name), zc::mv(receiver), zc::mv(generics),
      zc::mv(obligations), zc::mv(parameters), CanonicalCallableResult::unit(), zc::mv(raises),
      zc::mv(externalAbi));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid overload digest test header was rejected");
}

}  // namespace

ZC_TEST("OverloadHeaderDigest passes the exact domain SHA and raw codec vector") {
  auto value = OverloadHeaderDigest::compute(header("f"_zc));
  ZC_EXPECT(zc::encodeHex(value.bytes()) ==
            "311a7707c91317c488448e3f407308246bc6ad8f627e73a019a9303a83ff1f2d"_zc);
  ZC_EXPECT(value.bytes().size() == 32);
  ZC_EXPECT(value.encode().asPtr() == value.bytes());
  ZC_EXPECT(value.clone() == value);
}

ZC_TEST("OverloadHeaderDigest admits exactly thirty-two verified bytes") {
  auto computed = OverloadHeaderDigest::compute(header("f"_zc));
  uint8_t bytes[33] = {};
  for (size_t index = 0; index < computed.bytes().size(); ++index) {
    bytes[index] = computed.bytes()[index];
  }
  bytes[32] = 0xff;

  ZC_EXPECT(OverloadHeaderDigest::fromBytes(zc::arrayPtr(bytes, 31)) == zc::none);
  ZC_EXPECT(OverloadHeaderDigest::fromBytes(zc::arrayPtr(bytes, 33)) == zc::none);
  auto admitted = OverloadHeaderDigest::fromBytes(zc::arrayPtr(bytes, 32));
  ZC_REQUIRE(admitted != zc::none);
  ZC_IF_SOME(value, admitted) {
    ZC_EXPECT(value == computed);
    ZC_EXPECT(value.encode().size() == 32);
  }
}

ZC_TEST("OverloadHeaderAuthority retains verifies clones and compares complete headers") {
  auto authority = OverloadHeaderAuthority::from(header("f"_zc));
  auto cloned = authority.clone();
  auto different = OverloadHeaderAuthority::from(header("g"_zc));

  ZC_EXPECT(authority.header().name() == "f"_zc);
  ZC_EXPECT(different.header().name() == "g"_zc);
  ZC_EXPECT(authority.verify());
  ZC_EXPECT(cloned.verify());
  ZC_EXPECT(different.verify());
  ZC_EXPECT(authority.digest() == cloned.digest());
  ZC_EXPECT(authority.sameRecordAs(cloned));
  ZC_EXPECT(!authority.sameRecordAs(different));
  ZC_EXPECT(authority.header().encode().asPtr() != different.header().encode().asPtr());
}

}  // namespace zomlang::compiler::identity
