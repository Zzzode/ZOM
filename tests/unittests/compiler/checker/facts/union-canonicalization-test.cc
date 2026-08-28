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

// RFC 0005 canonical union and intersection member canonicalization. `buildSet`
// and `buildOptional` in signature-facts.cc drive these results through the
// public `resolveClosedSourceType` seam. Each case declares a parameter whose
// annotation is the type under test, walks the bound module tree to the
// annotation's union/intersection/optional node, resolves it, and asserts on
// the interned semantic type's canonical key bytes -- never on diagnostic text.

#include "compiler/ast/tree.h"
#include "compiler/checker/facts/signature-facts.h"
#include "compiler/identity/crypto/sha256.h"
#include "compiler/type/semantic-type-data.h"
#include "compiler/type/semantic-type-store.h"
#include "tests/unittests/compiler/checker/checker-authority-test-fixture.h"
#include "zc/core/encoding.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::checker::signature {
namespace {

using type::semantic::PrimitiveKind;
using type::semantic::PrimitiveTypeData;
using type::semantic::TypeData;

// Resolves a type-expression node of the given kind in the fixture's bound
// module and returns the canonical key bytes of the interned semantic type.
// `outermost` selects the last matching node (highest NodeId), which the
// bottom-up parser assigns to the enclosing expression -- the outer union in a
// nested `(A | B) | C`. Returns none when the node is absent or resolution
// fails closed.
zc::Maybe<zc::Array<uint8_t>> resolveKindKey(
    tests::checker_fixture::CheckerAuthoritySession& fixture, ast::SyntaxKind kind,
    bool outermost = false) {
  const auto& boundModule = fixture.boundModule();
  const auto& tree = boundModule.tree();
  zc::Maybe<ast::NodeId> selected;
  for (uint32_t index = 0; index < tree.nodeCount(); ++index) {
    const ast::NodeId node(index);
    if (!tree.contains(node) || tree.node(node).kind != kind) { continue; }
    selected = node;
    if (!outermost) { break; }
  }
  ZC_IF_SOME(node, selected) {
    auto resolved = resolveClosedSourceType(boundModule, fixture.identityAuthority(),
                                            fixture.semanticTypes(), node);
    ZC_IF_SOME(id, resolved) {
      auto lookup = fixture.semanticTypes().get(id);
      if (!lookup.is<type::SemanticTypeLookup>()) { return zc::none; }
      return zc::heapArray<uint8_t>(lookup.get<type::SemanticTypeLookup>().key().bytes());
    }
  }
  return zc::none;
}

// Interns a bare primitive type in the fixture store and returns its canonical
// key bytes, so a canonicalized set result can be compared against the expected
// reduced primitive.
zc::Array<uint8_t> primitiveKey(tests::checker_fixture::CheckerAuthoritySession& fixture,
                                PrimitiveKind kind) {
  auto admitted = fixture.semanticTypes().canonicalizeClosed(TypeData(PrimitiveTypeData{kind}));
  ZC_REQUIRE(admitted.is<type::semantic::CanonicalTypeData>());
  auto interned =
      fixture.semanticTypes().intern(zc::mv(admitted.get<type::semantic::CanonicalTypeData>()));
  ZC_REQUIRE(interned.is<type::SemanticTypeInterned>());
  auto lookup = fixture.semanticTypes().get(interned.get<type::SemanticTypeInterned>().id);
  ZC_REQUIRE(lookup.is<type::SemanticTypeLookup>());
  return zc::heapArray<uint8_t>(lookup.get<type::SemanticTypeLookup>().key().bytes());
}

bool sameBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index] != right[index]) { return false; }
  }
  return true;
}

// The checker-authority fixture requires the user source to declare a
// definition named `RecoveryOwner`; every case prepends it to the type under
// test.
zc::String withOwner(zc::StringPtr declaration) {
  return zc::str("class RecoveryOwner {}\n", declaration);
}

// A union with the Never identity element drops it: `i32 | never` is `i32`.
ZC_TEST("Union absorbs the never identity element") {
  tests::checker_fixture::CheckerAuthoritySession fixture(
      withOwner("fun target(value: i32 | never) {}"_zc));
  auto key = resolveKindKey(fixture, ast::SyntaxKind::UnionTypeExpr);
  ZC_REQUIRE(key != zc::none);
  ZC_IF_SOME(bytes, key) {
    ZC_EXPECT(sameBytes(bytes.asPtr(), primitiveKey(fixture, PrimitiveKind::I32).asPtr()));
  }
}

// A union containing the Any annihilator collapses to Any: `i32 | any` is `any`.
ZC_TEST("Union collapses to the any annihilator") {
  tests::checker_fixture::CheckerAuthoritySession fixture(
      withOwner("fun target(value: i32 | any) {}"_zc));
  auto key = resolveKindKey(fixture, ast::SyntaxKind::UnionTypeExpr);
  ZC_REQUIRE(key != zc::none);
  ZC_IF_SOME(bytes, key) {
    ZC_EXPECT(sameBytes(bytes.asPtr(), primitiveKey(fixture, PrimitiveKind::Any).asPtr()));
  }
}

// A union with a duplicate member deduplicates and reduces to the sole member:
// `i32 | i32` is `i32`. This was a hard rejection before canonicalization.
ZC_TEST("Union deduplicates equal members") {
  tests::checker_fixture::CheckerAuthoritySession fixture(
      withOwner("fun target(value: i32 | i32) {}"_zc));
  auto key = resolveKindKey(fixture, ast::SyntaxKind::UnionTypeExpr);
  ZC_REQUIRE(key != zc::none);
  ZC_IF_SOME(bytes, key) {
    ZC_EXPECT(sameBytes(bytes.asPtr(), primitiveKey(fixture, PrimitiveKind::I32).asPtr()));
  }
}

// An intersection containing the Never annihilator collapses to Never:
// `i32 & never` is `never`.
ZC_TEST("Intersection collapses to the never annihilator") {
  tests::checker_fixture::CheckerAuthoritySession fixture(
      withOwner("fun target(value: i32 & never) {}"_zc));
  auto key = resolveKindKey(fixture, ast::SyntaxKind::IntersectionTypeExpr);
  ZC_REQUIRE(key != zc::none);
  ZC_IF_SOME(bytes, key) {
    ZC_EXPECT(sameBytes(bytes.asPtr(), primitiveKey(fixture, PrimitiveKind::Never).asPtr()));
  }
}

// An intersection with the Any identity element drops it: `i32 & any` is `i32`.
ZC_TEST("Intersection removes the any identity element") {
  tests::checker_fixture::CheckerAuthoritySession fixture(
      withOwner("fun target(value: i32 & any) {}"_zc));
  auto key = resolveKindKey(fixture, ast::SyntaxKind::IntersectionTypeExpr);
  ZC_REQUIRE(key != zc::none);
  ZC_IF_SOME(bytes, key) {
    ZC_EXPECT(sameBytes(bytes.asPtr(), primitiveKey(fixture, PrimitiveKind::I32).asPtr()));
  }
}

// `never?` normalizes to `never | null`, then the Never element drops, leaving
// the sole member `null`.
ZC_TEST("Optional never reduces to null") {
  tests::checker_fixture::CheckerAuthoritySession fixture(
      withOwner("fun target(value: never?) {}"_zc));
  auto key = resolveKindKey(fixture, ast::SyntaxKind::OptionalTypeExpr);
  ZC_REQUIRE(key != zc::none);
  ZC_IF_SOME(bytes, key) {
    ZC_EXPECT(sameBytes(bytes.asPtr(), primitiveKey(fixture, PrimitiveKind::Null).asPtr()));
  }
}

// The RFC 0005 golden vector: `i32 | null` interns to the exact 35-byte key
// (domain + NUL, Union tag, two-element sequence, Primitive(I32), Primitive(Null))
// with the published SHA-256. `i32?` normalizes to the same union and key.
ZC_TEST("Union golden vector matches the RFC 0005 canonical key") {
  const auto expectedKey =
      "7a6f6d2e73656d616e7469632d747970652d6b6579000a000000000000000201030113"_zc;
  const auto expectedDigest = "95145d7b4eefcf1afa1074973dc414f8d268b3a79d86cbb7be2b761a3f40c844"_zc;

  tests::checker_fixture::CheckerAuthoritySession unionFixture(
      withOwner("fun target(value: i32 | null) {}"_zc));
  auto unionKey = resolveKindKey(unionFixture, ast::SyntaxKind::UnionTypeExpr);
  ZC_REQUIRE(unionKey != zc::none);
  ZC_IF_SOME(bytes, unionKey) {
    ZC_EXPECT(bytes.size() == 35);
    ZC_EXPECT(zc::encodeHex(bytes.asPtr()) == expectedKey);
    auto digest = identity::sha256(bytes.asPtr());
    ZC_REQUIRE(digest != zc::none);
    ZC_IF_SOME(value, digest) { ZC_EXPECT(zc::encodeHex(value.bytes()) == expectedDigest); }
  }

  tests::checker_fixture::CheckerAuthoritySession optionalFixture(
      withOwner("fun target(value: i32?) {}"_zc));
  auto optionalKey = resolveKindKey(optionalFixture, ast::SyntaxKind::OptionalTypeExpr);
  ZC_REQUIRE(optionalKey != zc::none);
  ZC_IF_SOME(bytes, optionalKey) { ZC_EXPECT(zc::encodeHex(bytes.asPtr()) == expectedKey); }
}

// Deferred flatten stays fail-closed: a nested union member cannot be flattened
// through the opaque key-pattern rail, so `(i32 | str) | bool` is rejected
// rather than admitted. This documents the boundary without a crash.
ZC_TEST("Nested union member is rejected fail-closed") {
  tests::checker_fixture::CheckerAuthoritySession fixture(
      withOwner("fun target(value: (i32 | str) | bool) {}"_zc));
  auto key = resolveKindKey(fixture, ast::SyntaxKind::UnionTypeExpr, /*outermost=*/true);
  ZC_EXPECT(key == zc::none);
}

}  // namespace
}  // namespace zomlang::compiler::checker::signature
