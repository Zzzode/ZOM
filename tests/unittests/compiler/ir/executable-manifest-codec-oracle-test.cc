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

// RFC 0043 O5/KR5.3 slice: prove the executable artifact manifest codec and its
// independent verifier are deterministic, field sensitive, and fail closed on
// each manifest invariant. This slice constructs and verifies the manifest as
// pure data and computes its deterministic ExecutableManifestId; it links no
// executable, reads no filesystem, and binds no live capability.

#include "compiler/identity/crypto/sha256.h"
#include "compiler/ir/executable-manifest-codec.h"
#include "compiler/ir/ir-failure.h"
#include "compiler/ir/link-plan-codec.h"
#include "zc/core/encoding.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::ir {
namespace {

identity::Sha256Digest digestOf(zc::StringPtr seed) {
  auto digest = identity::sha256(seed.asBytes());
  ZC_REQUIRE(digest != zc::none);
  return ZC_REQUIRE_NONNULL(digest);
}

zc::Array<uint8_t> bytes(zc::StringPtr text) { return zc::heapArray<uint8_t>(text.asBytes()); }

// The canonical minimal manifest request: an executable published at
// `/out/app`, two ordered input digests, and non-empty target/toolchain
// identities. Digests are seeded so the fixture is deterministic.
ExecutableManifestRequest minimalRequest() {
  zc::Vector<identity::Sha256Digest> inputs(2);
  auto first = digestOf("input-a"_zc);
  auto second = digestOf("input-b"_zc);
  // Order the two inputs so the request is already canonical.
  if (first < second) {
    inputs.add(first);
    inputs.add(second);
  } else {
    inputs.add(second);
    inputs.add(first);
  }
  return ExecutableManifestRequest{zc::str("/out/app"),
                                   zc::str("/out"),
                                   bytes("x86_64-zom-none"_zc),
                                   digestOf("executable"_zc),
                                   4096,
                                   LinkPlanId::fromDigest(digestOf("link-plan"_zc)),
                                   inputs.releaseAsArray(),
                                   bytes("toolchain-closure"_zc)};
}

VerifiedExecutableManifest verifiedManifest() {
  auto result = ExecutableManifestVerifier::verify(minimalRequest());
  ZC_REQUIRE(result.isVerified());
  return zc::mv(result).takeVerified();
}

// The verified manifest encodes to a fixed preimage: assert its exact byte
// length, full lowercase hex, and ExecutableManifestId. The bytes are produced
// by the live encoder; the asserted values are the frozen oracle.
ZC_TEST("Executable manifest encodes to the frozen oracle") {
  auto manifest = verifiedManifest();
  auto encoded = ExecutableManifestCodec::encode(manifest);
  ZC_EXPECT(encoded.size() == 264);
  ZC_EXPECT(
      zc::encodeHex(encoded.asPtr()) ==
      "7a6f6d2e65786563757461626c652d6d616e69666573740000000000000000082f6f75742f617070000000000"
      "000000f7838365f36342d7a6f6d2d6e6f6e650000000000000020a29c2123a01df1d0febb9d308a20d8a2fe3d"
      "40a91ca8b1294f59f08f9773ebea0000000000001000000000000000002078c97dcefcb104d93cc213c382972"
      "d8adadcef179ec46198267f3013bbe748ee00000000000000020000000000000020410ea61566cc3693b1be7a"
      "fd1a77f2597a9164c4a7b3bca5c5522cec9fcdaa040000000000000020aa94fd5c7c686a031ea06465c1daaa3e"
      "cb89acaaf351de34292c6a5dbe40cd9b0000000000000011746f6f6c636861696e2d636c6f73757265"_zc);
  ZC_EXPECT(zc::encodeHex(manifest.id().digest().bytes()) ==
            "4764b8841aed9b65fd0674cc10716cc7da5eca4b3ae0a1ed44289ac2a6004a1d"_zc);
}

// The domain tag prefixes the preimage and re-encoding is byte-identical.
ZC_TEST("Executable manifest encoding is deterministic and domain-separated") {
  auto first = ExecutableManifestCodec::encode(verifiedManifest());
  auto second = ExecutableManifestCodec::encode(verifiedManifest());
  ZC_EXPECT(first.asPtr() == second.asPtr());
  const char domain[] = "zom.executable-manifest";
  ZC_REQUIRE(first.size() >= sizeof(domain));
  for (size_t index = 0; index < sizeof(domain) - 1; ++index) {
    ZC_EXPECT(first[index] == static_cast<uint8_t>(domain[index]));
  }
  ZC_EXPECT(first[sizeof(domain) - 1] == 0x00);
}

ZC_TEST("Executable manifest decoder accepts only the canonical complete encoding") {
  auto verified = ExecutableManifestVerifier::verify(minimalRequest());
  ZC_REQUIRE(verified.isVerified());
  VerifiedExecutableManifest manifest = zc::mv(verified).takeVerified();
  zc::Array<uint8_t> bytes = ExecutableManifestCodec::encode(manifest);

  auto decoded = ExecutableManifestCodec::decode(bytes.asPtr(), "/out"_zc);
  ZC_REQUIRE(decoded.isVerified());
  ZC_EXPECT(ExecutableManifestCodec::encode(zc::mv(decoded).takeVerified()).asPtr() ==
            bytes.asPtr());

  auto truncated = zc::heapArray<uint8_t>(bytes.size() - 1);
  for (size_t index = 0; index < truncated.size(); ++index) { truncated[index] = bytes[index]; }
  auto rejected = ExecutableManifestCodec::decode(truncated.asPtr(), "/out"_zc);
  ZC_REQUIRE(rejected.isIrInvariantRejected());
  ZC_EXPECT(rejected.invariantFailures().facts()[0].kind() ==
            IrFailureKind::CanonicalCodecMismatch);
}

// The ExecutableManifestId is sensitive to every framed field.
ZC_TEST("Executable manifest id is field sensitive") {
  const auto baseline = verifiedManifest().id();

  {
    auto request = minimalRequest();
    request.executableByteCount = 8192;
    auto result = ExecutableManifestVerifier::verify(zc::mv(request));
    ZC_REQUIRE(result.isVerified());
    ZC_EXPECT(result.verifiedValue().id() != baseline);
  }
  {
    auto request = minimalRequest();
    request.executableDigest = digestOf("other-executable"_zc);
    auto result = ExecutableManifestVerifier::verify(zc::mv(request));
    ZC_REQUIRE(result.isVerified());
    ZC_EXPECT(result.verifiedValue().id() != baseline);
  }
  {
    auto request = minimalRequest();
    request.toolchainIdentity = bytes("other-toolchain"_zc);
    auto result = ExecutableManifestVerifier::verify(zc::mv(request));
    ZC_REQUIRE(result.isVerified());
    ZC_EXPECT(result.verifiedValue().id() != baseline);
  }
  {
    auto request = minimalRequest();
    request.linkPlanId = LinkPlanId::fromDigest(digestOf("other-plan"_zc));
    auto result = ExecutableManifestVerifier::verify(zc::mv(request));
    ZC_REQUIRE(result.isVerified());
    ZC_EXPECT(result.verifiedValue().id() != baseline);
  }
}

// A non-normalized or out-of-root final destination is InvalidFact.
ZC_TEST("Executable manifest verifier rejects a bad final destination") {
  {
    auto request = minimalRequest();
    request.finalDestination = zc::str("/out/../out/app");
    auto result = ExecutableManifestVerifier::verify(zc::mv(request));
    ZC_REQUIRE(result.isIrInvariantRejected());
    ZC_EXPECT(result.invariantFailures().facts()[0].phase() ==
              IrFailurePhase::ExecutablePublication);
    ZC_EXPECT(result.invariantFailures().facts()[0].kind() == IrFailureKind::InvalidFact);
  }
  {
    auto request = minimalRequest();
    request.finalDestination = zc::str("/elsewhere/app");
    auto result = ExecutableManifestVerifier::verify(zc::mv(request));
    ZC_REQUIRE(result.isIrInvariantRejected());
    ZC_EXPECT(result.invariantFailures().facts()[0].kind() == IrFailureKind::InvalidFact);
  }
}

// A zero executable byte count is InvalidFact.
ZC_TEST("Executable manifest verifier rejects a zero byte count") {
  auto request = minimalRequest();
  request.executableByteCount = 0;
  auto result = ExecutableManifestVerifier::verify(zc::mv(request));
  ZC_REQUIRE(result.isIrInvariantRejected());
  ZC_EXPECT(result.invariantFailures().facts()[0].kind() == IrFailureKind::InvalidFact);
}

// An empty target or toolchain identity is MissingRequiredFact.
ZC_TEST("Executable manifest verifier rejects a missing identity") {
  {
    auto request = minimalRequest();
    request.targetSpecificationIdentity = zc::Array<uint8_t>();
    auto result = ExecutableManifestVerifier::verify(zc::mv(request));
    ZC_REQUIRE(result.isIrInvariantRejected());
    ZC_EXPECT(result.invariantFailures().facts()[0].kind() == IrFailureKind::MissingRequiredFact);
  }
  {
    auto request = minimalRequest();
    request.toolchainIdentity = zc::Array<uint8_t>();
    auto result = ExecutableManifestVerifier::verify(zc::mv(request));
    ZC_REQUIRE(result.isIrInvariantRejected());
    ZC_EXPECT(result.invariantFailures().facts()[0].kind() == IrFailureKind::MissingRequiredFact);
  }
}

// Input digest order is semantic and duplicate content at distinct positions is legal.
ZC_TEST("Executable manifest verifier preserves semantic input digest order") {
  auto request = minimalRequest();
  zc::Vector<identity::Sha256Digest> ordered(3);
  auto a = digestOf("input-a"_zc);
  auto b = digestOf("input-b"_zc);
  ordered.add(b);
  ordered.add(a);
  ordered.add(b);
  request.inputArtifactDigests = ordered.releaseAsArray();
  auto result = ExecutableManifestVerifier::verify(zc::mv(request));
  ZC_REQUIRE(result.isVerified());
  VerifiedExecutableManifest manifest = zc::mv(result).takeVerified();
  ZC_REQUIRE(manifest.inputArtifactDigests().size() == 3);
  ZC_EXPECT(manifest.inputArtifactDigests()[0] == b);
  ZC_EXPECT(manifest.inputArtifactDigests()[1] == a);
  ZC_EXPECT(manifest.inputArtifactDigests()[2] == b);
}

}  // namespace
}  // namespace zomlang::compiler::ir
