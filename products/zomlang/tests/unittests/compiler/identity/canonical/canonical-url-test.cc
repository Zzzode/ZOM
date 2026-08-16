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

#include "zomlang/compiler/identity/canonical/canonical-url.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::identity {
namespace {

void expectUrl(zc::StringPtr input, zc::StringPtr expected) {
  auto admitted = CanonicalUrl::fromSource(input);
  bool matched = false;
  ZC_IF_SOME(value, admitted) {
    ZC_EXPECT(value.text() == expected);
    auto canonical = CanonicalUrl::fromCanonical(expected);
    ZC_IF_SOME(repeated, canonical) {
      ZC_EXPECT(repeated == value);
      auto duplicate = value.clone();
      ZC_EXPECT(duplicate == value);
      matched = true;
    }
  }
  ZC_EXPECT(matched);
}

void expectRejected(zc::StringPtr input) { ZC_EXPECT(CanonicalUrl::fromSource(input) == zc::none); }

}  // namespace

ZC_TEST("CanonicalUrl passes every normative RFC 0011 vector") {
  expectUrl("HTTPS://EXAMPLE.COM:443/a/./b/../c/%7euser"_zc, "https://example.com/a/c/~user"_zc);
  expectUrl("ssh://example.com:22/repo/"_zc, "ssh://example.com/repo/"_zc);
  expectUrl("https://example.com/a/%2e/b"_zc, "https://example.com/a/b"_zc);
  expectUrl("https://example.com/a/%2e%2e/b"_zc, "https://example.com/b"_zc);
  expectUrl("https://example.com/a/%252e%252e/b"_zc, "https://example.com/a/%252e%252e/b"_zc);
  expectRejected("https://example.com/index?access_token=x"_zc);
  expectRejected("https://user@example.com/index"_zc);
  expectRejected("https://example.com/index#mirror"_zc);
}

ZC_TEST("CanonicalUrl normalizes hosts and validates ports") {
  expectUrl("https://EXAMPLE.COM./"_zc, "https://example.com/"_zc);
  expectUrl("https://127.000.0.1:8443"_zc, "https://127.0.0.1:8443/"_zc);
  expectUrl("ssh://example.com:2222/repo"_zc, "ssh://example.com:2222/repo"_zc);
  expectUrl("https://[2001:0DB8:0:0:0:0:0:1]:443/a"_zc, "https://[2001:db8::1]/a"_zc);
  expectUrl("ssh://[2001:db8:0:1:0:0:0:1]:22/repo"_zc, "ssh://[2001:db8:0:1::1]/repo"_zc);
  expectUrl("https://[2001:0:0:1:0:0:1:1]/"_zc, "https://[2001::1:0:0:1:1]/"_zc);
  expectUrl("https://[2001:db8:0:1:2:3:4:5]/"_zc, "https://[2001:db8:0:1:2:3:4:5]/"_zc);
  expectUrl("https://[::ffff:192.0.2.1]/"_zc, "https://[::ffff:c000:201]/"_zc);

  expectRejected("https://example.com:0/"_zc);
  expectRejected("https://example.com:0443/"_zc);
  expectRejected("https://example.com:65536/"_zc);
  expectRejected("https://example.com:/"_zc);
  expectRejected("https://-example.com/"_zc);
  expectRejected("https://example-.com/"_zc);
  expectRejected("https://example..com/"_zc);
  expectRejected("https://exa_mple.com/"_zc);
  expectRejected("https://999.0.0.1/"_zc);
  expectRejected("https://[2001::db8::1]/"_zc);
  expectRejected("https://[1:2:3:4:5:6:7:8:]/"_zc);
  expectRejected("https://[:1:2:3:4:5:6:7:8]/"_zc);
  expectRejected("https://2001:db8::1/"_zc);
}

ZC_TEST("CanonicalUrl normalizes Unicode and preserves delimiter identity") {
  expectUrl("https://example.com/caf%65%CC%81/%2f/%3a"_zc,
            "https://example.com/caf%C3%A9/%2F/%3A"_zc);
  expectUrl("https://example.com/a//b/"_zc, "https://example.com/a//b/"_zc);
  expectUrl("https://example.com/a/%2E%2E//b"_zc, "https://example.com//b"_zc);
  expectUrl("https://example.com"_zc, "https://example.com/"_zc);

  expectRejected("https://example.com/%"_zc);
  expectRejected("https://example.com/%0"_zc);
  expectRejected("https://example.com/%GG"_zc);
  expectRejected("https://example.com/%C0%80"_zc);
  expectRejected("https://example.com/a b"_zc);
  expectRejected("https://\xC3\xA9xample.com/"_zc);
}

ZC_TEST("CanonicalUrl rejects every out-of-domain reference form") {
  expectRejected(""_zc);
  expectRejected("http://example.com/"_zc);
  expectRejected("git+ssh://example.com/"_zc);
  expectRejected("https:example.com/"_zc);
  expectRejected("//example.com/"_zc);
  expectRejected("https:///path"_zc);
  expectRejected("https://token@example.com/"_zc);
  expectRejected("https://example.com/?q=x"_zc);
  expectRejected("https://example.com/#fragment"_zc);
  ZC_EXPECT(CanonicalUrl::fromCanonical("HTTPS://EXAMPLE.COM/"_zc) == zc::none);
}

ZC_TEST("CanonicalUrl encoding uses the canonical URL text") {
  auto admitted = CanonicalUrl::fromSource("HTTPS://EXAMPLE.COM:443"_zc);
  bool encoded = false;
  ZC_IF_SOME(value, admitted) {
    constexpr char canonical[] = "https://example.com/";
    CanonicalEncoder encoder;
    value.encode(encoder);
    auto bytes = encoder.finish();
    ZC_REQUIRE(bytes.size() == sizeof(canonical) - 1 + 8);
    for (size_t index = 0; index < 8; ++index) {
      const uint8_t expectedLengthByte =
          index == 7 ? static_cast<uint8_t>(sizeof(canonical) - 1) : 0;
      ZC_EXPECT(bytes[index] == expectedLengthByte);
    }
    for (size_t index = 0; index < sizeof(canonical) - 1; ++index) {
      ZC_EXPECT(bytes[index + 8] == static_cast<uint8_t>(canonical[index]));
    }
    encoded = true;
  }
  ZC_EXPECT(encoded);
}

}  // namespace zomlang::compiler::identity
