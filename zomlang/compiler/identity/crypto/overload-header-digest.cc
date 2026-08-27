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

#include "zc/core/vector.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::identity {
namespace {

constexpr auto kOverloadHeaderDomain = "zom.overload-header"_zc;

}  // namespace

namespace overload_header_digest_detail {

struct OverloadHeaderAuthorityData final {
  OverloadHeaderDigest digest;
  OverloadHeader header;
};

}  // namespace overload_header_digest_detail

OverloadHeaderDigest::OverloadHeaderDigest(const Sha256Digest& digest) noexcept
    : digestValue(digest) {}

OverloadHeaderDigest OverloadHeaderDigest::compute(const OverloadHeader& header) {
  const auto encodedHeader = header.encode();
  zc::Vector<uint8_t> preimage(kOverloadHeaderDomain.size() + 1 + encodedHeader.size());
  preimage.addAll(kOverloadHeaderDomain.asBytes());
  preimage.add(0x00);
  preimage.addAll(encodedHeader);
  ZC_IF_SOME(digest, sha256(preimage.asPtr())) { return OverloadHeaderDigest(digest); }
  ZC_UNREACHABLE
}

zc::Maybe<OverloadHeaderDigest> OverloadHeaderDigest::fromBytes(zc::ArrayPtr<const uint8_t> bytes) {
  ZC_IF_SOME(digest, Sha256Digest::fromBytes(bytes)) { return OverloadHeaderDigest(digest); }
  return zc::none;
}

OverloadHeaderDigest OverloadHeaderDigest::clone() const noexcept {
  return OverloadHeaderDigest(digestValue);
}

zc::ArrayPtr<const uint8_t> OverloadHeaderDigest::bytes() const { return digestValue.bytes(); }

void OverloadHeaderDigest::encode(CanonicalEncoder& encoder) const {
  encoder.encodeDigest(digestValue);
}

zc::Array<uint8_t> OverloadHeaderDigest::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

bool OverloadHeaderDigest::operator==(const OverloadHeaderDigest& other) const noexcept {
  return digestValue == other.digestValue;
}

OverloadHeaderAuthority::OverloadHeaderAuthority(
    zc::Own<overload_header_digest_detail::OverloadHeaderAuthorityData>&& value) noexcept
    : impl(zc::mv(value)) {}

OverloadHeaderAuthority::~OverloadHeaderAuthority() noexcept(false) = default;
OverloadHeaderAuthority::OverloadHeaderAuthority(OverloadHeaderAuthority&&) noexcept = default;
OverloadHeaderAuthority& OverloadHeaderAuthority::operator=(OverloadHeaderAuthority&&) noexcept =
    default;

OverloadHeaderAuthority OverloadHeaderAuthority::from(OverloadHeader&& header) {
  auto digest = OverloadHeaderDigest::compute(header);
  return OverloadHeaderAuthority(
      zc::heap<overload_header_digest_detail::OverloadHeaderAuthorityData>(
          overload_header_digest_detail::OverloadHeaderAuthorityData{zc::mv(digest),
                                                                     zc::mv(header)}));
}

OverloadHeaderAuthority OverloadHeaderAuthority::clone() const {
  return OverloadHeaderAuthority(
      zc::heap<overload_header_digest_detail::OverloadHeaderAuthorityData>(
          overload_header_digest_detail::OverloadHeaderAuthorityData{impl->digest.clone(),
                                                                     impl->header.clone()}));
}

const OverloadHeaderDigest& OverloadHeaderAuthority::digest() const noexcept {
  return impl->digest;
}

const OverloadHeader& OverloadHeaderAuthority::header() const noexcept {
  return impl->header;
}

bool OverloadHeaderAuthority::verify() const {
  return OverloadHeaderDigest::compute(impl->header) == impl->digest;
}

bool OverloadHeaderAuthority::sameRecordAs(const OverloadHeaderAuthority& other) const {
  const auto left = impl->header.encode();
  const auto right = other.impl->header.encode();
  return left.asPtr() == right.asPtr();
}

}  // namespace zomlang::compiler::identity
