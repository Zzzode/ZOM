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
// See the License for the specific language governing permissions and
// limitations under the License.

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "compiler/identity/crypto/sha256.h"
#include "compiler/lir/lir-algebra-codec.h"

namespace zomlang::compiler::lir {
namespace {

/// \brief Returns the lowercase hex of `bytes`.
zc::String hex(zc::ArrayPtr<const uint8_t> bytes) { return zc::encodeHex(bytes); }

/// \brief Returns the SHA-256 hex of `bytes`.
zc::String digestHex(zc::ArrayPtr<const uint8_t> bytes) {
  auto digest = identity::sha256(bytes);
  ZC_REQUIRE(digest != zc::none);
  ZC_IF_SOME(value, digest) { return zc::encodeHex(value.bytes()); }
  ZC_UNREACHABLE
}

// RFC 0021 "LIR Algebra Registry": the empty-registry codec oracle is 56 bytes,
// documented preimage hex
//   7a6f6d2e6c69722d616c67656272610000000000000000107a6f6d2e6d69722d7265766973696f6e00000000000000000000000000000000
// with SHA-256
//   03106c3451b5e1adab5310b8643c8d59657e0635f804b5be8fc9b9754199e1c8
// The live encoder must reproduce both from the initial/empty registry state.
// The bytes are derived from the live encoder here and then independently
// asserted equal to the RFC's documented preimage and digest; the RFC hex is
// the oracle, not the source of the bytes.

ZC_TEST("LIR algebra empty registry reproduces the RFC 0021 56-byte oracle") {
  const auto registry = LirAlgebraRegistry::empty();
  ZC_EXPECT(registry.sourceMirRevisionDomain() == "zom.mir-revision"_zc);
  ZC_EXPECT(registry.recipeCount() == 0);
  ZC_EXPECT(registry.generatedRecipeCount() == 0);

  auto bytes = LirAlgebraCodec::encode(registry);
  ZC_EXPECT(bytes.size() == 56);
  ZC_EXPECT(hex(bytes.asPtr()) ==
            "7a6f6d2e6c69722d616c67656272610000000000000000107a6f6d2e6d69722d"
            "7265766973696f6e00000000000000000000000000000000");
  ZC_EXPECT(digestHex(bytes.asPtr()) ==
            "03106c3451b5e1adab5310b8643c8d59657e0635f804b5be8fc9b9754199e1c8");

  auto revision = LirAlgebraCodec::compute(registry);
  auto expected = identity::sha256(bytes.asPtr());
  ZC_REQUIRE(expected != zc::none);
  ZC_IF_SOME(value, expected) { ZC_EXPECT(revision.digest() == value); }
}

// The revision is sensitive to every framed input: the source-MIR domain, and
// each of the two framed recipe sequences. Any change to a framed field must
// change the digest away from the empty-registry oracle digest.

ZC_TEST("LIR algebra revision is field sensitive") {
  const auto baseDigest = LirAlgebraCodec::compute(LirAlgebraRegistry::empty()).digest();

  {
    // A different source-MIR revision domain changes the digest.
    auto altered = LirAlgebraRegistry::withSourceDomain("zom.mir-revision-x"_zc);
    ZC_REQUIRE(altered != zc::none);
    ZC_IF_SOME(registry, altered) {
      ZC_EXPECT(LirAlgebraCodec::compute(registry).digest() != baseDigest);
    }
  }
  {
    // One appended source recipe changes the digest (count and framed record).
    auto registry = LirAlgebraRegistry::empty();
    const uint8_t record[] = {0x01, 0x02, 0x03};
    registry.addRecipe(zc::arrayPtr(record, 3));
    ZC_EXPECT(registry.recipeCount() == 1);
    ZC_EXPECT(LirAlgebraCodec::compute(registry).digest() != baseDigest);
  }
  {
    // One appended generated recipe changes the digest.
    auto registry = LirAlgebraRegistry::empty();
    const uint8_t record[] = {0x0a};
    registry.addGeneratedRecipe(zc::arrayPtr(record, 1));
    ZC_EXPECT(registry.generatedRecipeCount() == 1);
    ZC_EXPECT(LirAlgebraCodec::compute(registry).digest() != baseDigest);
  }
  {
    // Sequence position matters: a source recipe and a generated recipe with the
    // same bytes are not interchangeable.
    auto asSource = LirAlgebraRegistry::empty();
    auto asGenerated = LirAlgebraRegistry::empty();
    const uint8_t record[] = {0x07, 0x07};
    asSource.addRecipe(zc::arrayPtr(record, 2));
    asGenerated.addGeneratedRecipe(zc::arrayPtr(record, 2));
    ZC_EXPECT(LirAlgebraCodec::compute(asSource).digest() !=
              LirAlgebraCodec::compute(asGenerated).digest());
  }
}

// The codec is deterministic and domain-framed: re-encoding an equal registry
// yields identical bytes that begin with the domain tag and a NUL separator.

ZC_TEST("LIR algebra codec is deterministic and domain framed") {
  auto first = LirAlgebraCodec::encode(LirAlgebraRegistry::empty());
  auto second = LirAlgebraCodec::encode(LirAlgebraRegistry::empty());
  ZC_EXPECT(first.asPtr() == second.asPtr());

  constexpr char domain[] = "zom.lir-algebra";
  ZC_REQUIRE(first.size() > sizeof(domain));
  for (size_t index = 0; index + 1 < sizeof(domain); ++index) {
    ZC_EXPECT(first[index] == static_cast<uint8_t>(domain[index]));
  }
  ZC_EXPECT(first[sizeof(domain) - 1] == 0x00);
}

// A non-ASCII or empty source-MIR revision domain fails closed.

ZC_TEST("LIR algebra registry rejects an invalid source domain") {
  ZC_EXPECT(LirAlgebraRegistry::withSourceDomain(""_zc) == zc::none);
  ZC_EXPECT(LirAlgebraRegistry::withSourceDomain("zom.\xff"_zc) == zc::none);
}

}  // namespace
}  // namespace zomlang::compiler::lir
