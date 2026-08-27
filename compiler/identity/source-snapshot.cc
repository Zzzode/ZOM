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

#include "compiler/identity/source-snapshot.h"

#include "compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::identity {

UnbrandedSourceRange::UnbrandedSourceRange(SourceFileKey&& source,
                                           const Sha256Digest& contentDigest, uint64_t byteStart,
                                           uint64_t byteEnd) noexcept
    : sourceValue(zc::mv(source)),
      digestValue(contentDigest),
      startValue(byteStart),
      endValue(byteEnd) {}

UnbrandedSourceRange UnbrandedSourceRange::clone() const {
  return UnbrandedSourceRange(sourceValue.clone(), digestValue, startValue, endValue);
}

bool UnbrandedSourceRange::belongsTo(const SourceFileKey& source) const {
  return sourceValue.sameAs(source);
}

const Sha256Digest& UnbrandedSourceRange::contentDigest() const noexcept { return digestValue; }
uint64_t UnbrandedSourceRange::byteStart() const noexcept { return startValue; }
uint64_t UnbrandedSourceRange::byteEnd() const noexcept { return endValue; }

zc::Array<uint8_t> UnbrandedSourceRange::encode() const {
  CanonicalEncoder encoder;
  sourceValue.encode(encoder);
  encoder.encodeDigest(digestValue);
  encoder.encodeUint64(startValue);
  encoder.encodeUint64(endValue);
  return encoder.finish();
}

ImmutableSourceSnapshot::ImmutableSourceSnapshot(SourceFileKey&& source,
                                                 const Sha256Digest& contentDigest,
                                                 zc::Array<uint8_t>&& bytes) noexcept
    : sourceValue(zc::mv(source)), digestValue(contentDigest), bytesValue(zc::mv(bytes)) {}

zc::Maybe<ImmutableSourceSnapshot> ImmutableSourceSnapshot::from(SourceFileKey&& source,
                                                                 zc::Array<uint8_t>&& bytes) {
  auto digest = sha256(bytes.asPtr());
  ZC_IF_SOME(value, digest) {
    return ImmutableSourceSnapshot(zc::mv(source), value, zc::mv(bytes));
  }
  return zc::none;
}

ImmutableSourceSnapshot ImmutableSourceSnapshot::clone() const {
  return ImmutableSourceSnapshot(sourceValue.clone(), digestValue,
                                 zc::heapArray(bytesValue.asPtr()));
}

const SourceFileKey& ImmutableSourceSnapshot::source() const noexcept { return sourceValue; }
const Sha256Digest& ImmutableSourceSnapshot::contentDigest() const noexcept { return digestValue; }
zc::ArrayPtr<const uint8_t> ImmutableSourceSnapshot::bytes() const noexcept {
  return bytesValue.asPtr();
}

zc::Maybe<UnbrandedSourceRange> ImmutableSourceSnapshot::unbrandedRange(uint64_t byteStart,
                                                                        uint64_t byteEnd) const {
  if (byteStart > byteEnd || byteEnd > bytesValue.size()) { return zc::none; }
  return UnbrandedSourceRange(sourceValue.clone(), digestValue, byteStart, byteEnd);
}

zc::Maybe<SourceSpan> ImmutableSourceSnapshot::span(uint64_t byteStart, uint64_t byteEnd) const {
  if (byteStart > byteEnd || byteEnd > bytesValue.size()) { return zc::none; }
  return SourceSpan(sourceValue.clone(), byteStart, byteEnd);
}

}  // namespace zomlang::compiler::identity
