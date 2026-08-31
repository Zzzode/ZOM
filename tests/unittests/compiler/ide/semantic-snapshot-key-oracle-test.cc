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

// RFC 0023 "IDE Semantic Snapshots": prove SemanticSnapshotKey binds one stable
// source content identity to one document version, round-trips through its
// domain-separated canonical preimage, distinguishes versions in the encoded
// bytes and in equality, and fails closed on every malformed candidate. This
// composes the key as pure data over an already-verified StableSourceQueryKey;
// it registers no query and reaches no CompilerSession.

#include <cstdint>

#include "compiler/ide/document-version.h"
#include "compiler/ide/semantic-snapshot-key.h"
#include "compiler/identity/source-query-input.h"
#include "tests/unittests/compiler/test-semantic-identities.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::ide {
namespace {

identity::source_query::StableSourceQueryKey stableSourceKey() {
  auto key = identity::source_query::StableSourceQueryKey::fromVerified(
      tests::test_identity_detail::source());
  ZC_REQUIRE(key != zc::none);
  return ZC_REQUIRE_NONNULL(zc::mv(key));
}

SemanticSnapshotKey keyAtVersion(int32_t version) {
  return SemanticSnapshotKey::bind(stableSourceKey(), DocumentVersion::initial(version));
}

ZC_TEST("SemanticSnapshotKey round-trips through its canonical preimage") {
  auto key = keyAtVersion(7);
  auto encoded = key.encodeCanonical();
  auto decoded = SemanticSnapshotKey::decodeCanonical(encoded.asPtr());
  ZC_EXPECT(decoded != zc::none);
  auto& value = ZC_REQUIRE_NONNULL(decoded);
  ZC_EXPECT(value == key);
  ZC_EXPECT(value.documentVersion() == DocumentVersion::initial(7));
  ZC_EXPECT(value.sourceKey() == key.sourceKey());
  // Re-encoding the decoded key reproduces the exact bytes.
  ZC_EXPECT(value.encodeCanonical().asPtr() == encoded.asPtr());
}

ZC_TEST("SemanticSnapshotKey round-trips a negative document version") {
  auto key = keyAtVersion(-2147483648);  // INT32_MIN
  auto encoded = key.encodeCanonical();
  auto decoded = SemanticSnapshotKey::decodeCanonical(encoded.asPtr());
  ZC_EXPECT(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded).documentVersion() == DocumentVersion::initial(-2147483648));
}

ZC_TEST("SemanticSnapshotKey encodes the version into its identity") {
  auto lower = keyAtVersion(3);
  auto higher = keyAtVersion(4);
  // Same source, different version: distinct keys and distinct canonical bytes.
  ZC_EXPECT(lower != higher);
  ZC_EXPECT(lower.encodeCanonical().asPtr() != higher.encodeCanonical().asPtr());
  // Same source, same version: equal keys and identical bytes.
  ZC_EXPECT(keyAtVersion(3) == lower);
  ZC_EXPECT(keyAtVersion(3).encodeCanonical().asPtr() == lower.encodeCanonical().asPtr());
}

ZC_TEST("SemanticSnapshotKey canonical bytes match the frozen preimage layout") {
  auto key = keyAtVersion(7);
  auto encoded = key.encodeCanonical();
  auto sourceBytes = key.sourceKey().canonicalSourceBytes();

  // Preimage = ASCII("zom.ide.snapshot") 0x00, Frame(sourceBytes) [be-uint64 len
  // + bytes], then be-int32 version. Assert the exact structure and boundaries.
  const zc::StringPtr tag = "zom.ide.snapshot"_zc;
  zc::Vector<uint8_t> expected;
  expected.addAll(tag.asBytes());
  expected.add(0);
  for (int shift = 56; shift >= 0; shift -= 8) {
    expected.add(static_cast<uint8_t>(sourceBytes.size() >> static_cast<uint32_t>(shift)));
  }
  expected.addAll(sourceBytes);
  const uint32_t versionBits = static_cast<uint32_t>(7);
  for (int shift = 24; shift >= 0; shift -= 8) {
    expected.add(static_cast<uint8_t>(versionBits >> static_cast<uint32_t>(shift)));
  }
  ZC_EXPECT(encoded.asPtr() == expected.asPtr());
  // The total length is the tag, the separator, the 8-byte frame length, the
  // framed source bytes, and the 4-byte version.
  ZC_EXPECT(encoded.size() == tag.size() + 1 + 8 + sourceBytes.size() + 4);
}

ZC_TEST("SemanticSnapshotKey decode rejects a wrong domain tag") {
  auto encoded = keyAtVersion(7).encodeCanonical();
  auto bytes = zc::heapArray<uint8_t>(encoded.asPtr());
  bytes[0] ^= 0xFF;  // corrupt the first tag byte
  ZC_EXPECT(SemanticSnapshotKey::decodeCanonical(bytes.asPtr()) == zc::none);
}

ZC_TEST("SemanticSnapshotKey decode rejects a truncated frame") {
  auto encoded = keyAtVersion(7).encodeCanonical();
  // Drop the trailing version and one source byte so the frame overruns.
  auto truncated = zc::heapArray<uint8_t>(encoded.slice(0, encoded.size() - 5));
  ZC_EXPECT(SemanticSnapshotKey::decodeCanonical(truncated.asPtr()) == zc::none);
}

ZC_TEST("SemanticSnapshotKey decode rejects a missing version field") {
  auto encoded = keyAtVersion(7).encodeCanonical();
  // Drop only the 4 version bytes: the frame is intact but the version is absent.
  auto missingVersion = zc::heapArray<uint8_t>(encoded.slice(0, encoded.size() - 4));
  ZC_EXPECT(SemanticSnapshotKey::decodeCanonical(missingVersion.asPtr()) == zc::none);
}

ZC_TEST("SemanticSnapshotKey decode rejects trailing bytes") {
  auto encoded = keyAtVersion(7).encodeCanonical();
  zc::Vector<uint8_t> extended;
  extended.addAll(encoded.asPtr());
  extended.add(0);  // one extra byte beyond the canonical preimage
  ZC_EXPECT(SemanticSnapshotKey::decodeCanonical(extended.asPtr()) == zc::none);
}

ZC_TEST("SemanticSnapshotKey decode rejects a non-canonical inner source key") {
  auto encoded = keyAtVersion(7).encodeCanonical();
  auto bytes = zc::heapArray<uint8_t>(encoded.asPtr());
  // Corrupt a byte inside the framed source key so the bounded source decode
  // fails while the frame length and version remain well-formed.
  const zc::StringPtr tag = "zom.ide.snapshot"_zc;
  const size_t innerStart = tag.size() + 1 + 8;
  bytes[innerStart] ^= 0xFF;
  ZC_EXPECT(SemanticSnapshotKey::decodeCanonical(bytes.asPtr()) == zc::none);
}

}  // namespace
}  // namespace zomlang::compiler::ide
