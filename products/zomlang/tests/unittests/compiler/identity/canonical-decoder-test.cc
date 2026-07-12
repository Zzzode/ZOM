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

#include "zomlang/compiler/identity/canonical-decoder.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::identity {

ZC_TEST("CanonicalDecoderTest.RoundTripsBoundedPrimitiveSequence") {
  CanonicalEncoder encoder;
  encoder.encodeUint8(0xa5);
  encoder.encodeUint32(0x01020304);
  encoder.encodeUint64(0x0102030405060708ULL);
  encoder.encodeBool(true);
  encoder.encodeByteString("abc"_zc.asBytes());
  auto bytes = encoder.finish();
  CanonicalDecoder decoder(bytes);
  ZC_EXPECT(decoder.decodeUint8() == 0xa5);
  ZC_EXPECT(decoder.decodeUint32() == 0x01020304);
  ZC_EXPECT(decoder.decodeUint64() == 0x0102030405060708ULL);
  ZC_EXPECT(decoder.decodeBool() == true);
  auto text = decoder.decodeByteString(3);
  ZC_REQUIRE(text != zc::none);
  ZC_IF_SOME(value, text) { ZC_EXPECT(value.asPtr() == "abc"_zc.asBytes()); }
  ZC_EXPECT(decoder.finished());
  ZC_EXPECT(decoder.remaining() == 0);
}

ZC_TEST("CanonicalDecoderTest.RejectsTruncationInvalidBooleanAndLimits") {
  const uint8_t truncated[] = {0x00, 0x01};
  CanonicalDecoder truncatedDecoder(zc::arrayPtr(truncated));
  ZC_EXPECT(truncatedDecoder.decodeUint32() == zc::none);

  const uint8_t invalidBoolean[] = {0x02};
  CanonicalDecoder booleanDecoder(zc::arrayPtr(invalidBoolean));
  ZC_EXPECT(booleanDecoder.decodeBool() == zc::none);

  CanonicalEncoder encoder;
  encoder.encodeByteString("four"_zc.asBytes());
  auto bytes = encoder.finish();
  CanonicalDecoder limitedDecoder(bytes);
  ZC_EXPECT(limitedDecoder.decodeByteString(3) == zc::none);
}

}  // namespace zomlang::compiler::identity
