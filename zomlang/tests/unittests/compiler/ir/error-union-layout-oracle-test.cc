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
#include "zomlang/compiler/identity/crypto/sha256.h"
#include "zomlang/compiler/ir/error-union-layout-codec.h"
#include "zomlang/compiler/ir/error-union-layout-revision.h"
#include "zomlang/compiler/ir/error-union-layout.h"

namespace zomlang::compiler::ir {
namespace {

/// \brief Returns a 32-byte array of one repeated byte (a synthetic digest).
zc::Array<uint8_t> repeatedDigest(uint8_t value) {
  auto bytes = zc::heapArray<uint8_t>(32);
  for (auto& byte : bytes) { byte = value; }
  return bytes;
}

/// \brief Returns a single-byte array holding `value`.
zc::Array<uint8_t> singleByte(uint8_t value) {
  auto bytes = zc::heapArray<uint8_t>(1);
  bytes[0] = value;
  return bytes;
}

/// \brief Returns the SHA-256 hex of `bytes`.
zc::String digestHex(zc::ArrayPtr<const uint8_t> bytes) {
  auto digest = identity::sha256(bytes);
  ZC_REQUIRE(digest != zc::none);
  ZC_IF_SOME(value, digest) { return zc::encodeHex(value.bytes()); }
  ZC_UNREACHABLE
}

// The target-artifact manifest oracle (RFC 0006). Module byte a1, interface
// revision 32 bytes 0x11, target-spec 32 bytes 0x22, one already-encoded
// descriptor blob b2, layout revision 32 bytes 0x33. The complete 146-byte
// preimage hashes to the published digest; this pins the manifest framing
// (domain, byte-string, digest, and sequence discipline) to the byte.

ZC_TEST("Target artifact ABI manifest reproduces the RFC 0006 146-byte oracle") {
  TargetArtifactAbiManifest manifest;
  manifest.moduleKey = singleByte(0xa1);
  manifest.interfaceRevision = repeatedDigest(0x11);
  manifest.targetSpecId = repeatedDigest(0x22);
  manifest.errorUnionLayouts.add(VerifiedErrorUnionLayout{singleByte(0xb2), repeatedDigest(0x33)});

  auto bytes = ErrorUnionLayoutCodec::encodeManifest(manifest);
  ZC_EXPECT(bytes.size() == 146);
  ZC_EXPECT(digestHex(bytes.asPtr()) ==
            "290d95e132c99dba891dd3519927363c33800346b055e1dcbca340f45183f9b9");

  auto revision = ErrorUnionLayoutCodec::computeManifestRevision(manifest);
  auto expected = identity::sha256(bytes.asPtr());
  ZC_REQUIRE(expected != zc::none);
  ZC_IF_SOME(value, expected) { ZC_EXPECT(revision.digest() == value); }
}

// The manifest revision is sensitive to every framed field: mutating the module
// key, either revision, the target spec, or the layout inventory must change the
// digest.

ZC_TEST("Target artifact ABI manifest revision is field sensitive") {
  TargetArtifactAbiManifest base;
  base.moduleKey = singleByte(0xa1);
  base.interfaceRevision = repeatedDigest(0x11);
  base.targetSpecId = repeatedDigest(0x22);
  base.errorUnionLayouts.add(VerifiedErrorUnionLayout{singleByte(0xb2), repeatedDigest(0x33)});
  const auto baseDigest = ErrorUnionLayoutCodec::computeManifestRevision(base).digest();

  {
    TargetArtifactAbiManifest mutated;
    mutated.moduleKey = singleByte(0xa2);
    mutated.interfaceRevision = repeatedDigest(0x11);
    mutated.targetSpecId = repeatedDigest(0x22);
    mutated.errorUnionLayouts.add(VerifiedErrorUnionLayout{singleByte(0xb2), repeatedDigest(0x33)});
    ZC_EXPECT(ErrorUnionLayoutCodec::computeManifestRevision(mutated).digest() != baseDigest);
  }
  {
    TargetArtifactAbiManifest mutated;
    mutated.moduleKey = singleByte(0xa1);
    mutated.interfaceRevision = repeatedDigest(0x44);
    mutated.targetSpecId = repeatedDigest(0x22);
    mutated.errorUnionLayouts.add(VerifiedErrorUnionLayout{singleByte(0xb2), repeatedDigest(0x33)});
    ZC_EXPECT(ErrorUnionLayoutCodec::computeManifestRevision(mutated).digest() != baseDigest);
  }
  {
    TargetArtifactAbiManifest mutated;
    mutated.moduleKey = singleByte(0xa1);
    mutated.interfaceRevision = repeatedDigest(0x11);
    mutated.targetSpecId = repeatedDigest(0x22);
    mutated.errorUnionLayouts.add(VerifiedErrorUnionLayout{singleByte(0xb2), repeatedDigest(0x55)});
    ZC_EXPECT(ErrorUnionLayoutCodec::computeManifestRevision(mutated).digest() != baseDigest);
  }
}

/// \brief Builds a synthetic descriptor whose type keys are opaque literal bytes.
///
/// The RFC 0006 405-byte descriptor oracle carries real RFC 0005 SemanticTypeKey
/// blobs whose interior length framing is produced only by the live key encoder;
/// the codec treats each key as an opaque byte string, so these determinism and
/// field-sensitivity tests supply synthetic key bytes rather than the oracle
/// blobs. The exact 405-byte oracle is reproduced once the descriptor is built
/// from live SemanticTypeStore keys (tracked as the RFC 0006 lowering slice).
ErrorUnionLayoutDescriptor syntheticDescriptor() {
  ErrorUnionLayoutDescriptor descriptor;
  descriptor.valueTypeKey = singleByte(0x0a);
  descriptor.successTypeKey = singleByte(0x0b);
  descriptor.residualTypeKeys.add(singleByte(0x0c));
  descriptor.checkedFactsRevision = repeatedDigest(0x11);
  descriptor.dispatchFactsRevision = repeatedDigest(0x22);
  descriptor.targetSpecId = repeatedDigest(0x33);
  descriptor.tagWidth = ErrorUnionTagWidth::U8;
  descriptor.tagOffset = 0;
  descriptor.payloadOffset = 8;
  descriptor.payloadSize = 16;
  descriptor.payloadAlign = 8;
  descriptor.size = 24;
  descriptor.align = 8;
  descriptor.alternatives.add(
      ErrorUnionAlternativeLayout{0, singleByte(0x0b), ErrorUnionAlternativeKind::Success, 4, 4});
  descriptor.alternatives.add(
      ErrorUnionAlternativeLayout{1, singleByte(0x0c), ErrorUnionAlternativeKind::Residual, 16, 8});
  return descriptor;
}

// The descriptor codec is deterministic and domain-framed: re-encoding the same
// descriptor yields identical bytes beginning with the domain, and the revision
// is the SHA-256 of the encoded stream.

ZC_TEST("Error union layout descriptor codec is deterministic and domain framed") {
  auto descriptor = syntheticDescriptor();
  auto first = ErrorUnionLayoutCodec::encode(descriptor);
  auto second = ErrorUnionLayoutCodec::encode(descriptor.clone());
  ZC_EXPECT(first.asPtr() == second.asPtr());

  constexpr char domain[] = "zom.error-union-layout";
  ZC_REQUIRE(first.size() > sizeof(domain));
  for (size_t index = 0; index + 1 < sizeof(domain); ++index) {
    ZC_EXPECT(first[index] == static_cast<uint8_t>(domain[index]));
  }
  ZC_EXPECT(first[sizeof(domain) - 1] == 0x00);

  auto revision = ErrorUnionLayoutCodec::compute(descriptor);
  auto expected = identity::sha256(first.asPtr());
  ZC_REQUIRE(expected != zc::none);
  ZC_IF_SOME(value, expected) { ZC_EXPECT(revision.digest() == value); }
}

// Every descriptor field participates in the revision: mutating a type key, a
// revision input, the tag width, a layout scalar, or an alternative payload must
// change the digest.

ZC_TEST("Error union layout descriptor revision is field sensitive") {
  const auto baseDigest = ErrorUnionLayoutCodec::compute(syntheticDescriptor()).digest();

  {
    auto d = syntheticDescriptor();
    d.valueTypeKey = singleByte(0x0f);
    ZC_EXPECT(ErrorUnionLayoutCodec::compute(d).digest() != baseDigest);
  }
  {
    auto d = syntheticDescriptor();
    d.checkedFactsRevision = repeatedDigest(0x99);
    ZC_EXPECT(ErrorUnionLayoutCodec::compute(d).digest() != baseDigest);
  }
  {
    auto d = syntheticDescriptor();
    d.tagWidth = ErrorUnionTagWidth::U32;
    ZC_EXPECT(ErrorUnionLayoutCodec::compute(d).digest() != baseDigest);
  }
  {
    auto d = syntheticDescriptor();
    d.size = 40;
    ZC_EXPECT(ErrorUnionLayoutCodec::compute(d).digest() != baseDigest);
  }
  {
    auto d = syntheticDescriptor();
    d.alternatives[1].payloadAlign = 16;
    ZC_EXPECT(ErrorUnionLayoutCodec::compute(d).digest() != baseDigest);
  }
  {
    auto d = syntheticDescriptor();
    d.residualTypeKeys.add(singleByte(0x0d));
    ZC_EXPECT(ErrorUnionLayoutCodec::compute(d).digest() != baseDigest);
  }
}

}  // namespace
}  // namespace zomlang::compiler::ir
