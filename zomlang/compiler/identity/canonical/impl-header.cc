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

#include "zomlang/compiler/identity/canonical/impl-header.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/identity/canonical/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::identity {

namespace canonical_impl_header_detail {

struct CanonicalTraitReferenceData final {
  CanonicalNameReference name;
  zc::Vector<CanonicalHeaderTypeSyntax> arguments;
};

struct CanonicalImplHeaderData final {
  zc::Vector<CanonicalGenericParameter> genericParameters;
  ImplPolarity polarity;
  ImplSafety safety;
  CanonicalTraitReference trait;
  CanonicalHeaderTypeSyntax selfType;
  zc::Vector<CanonicalBoundObligation> obligations;
};

}  // namespace canonical_impl_header_detail

namespace {

constexpr uint64_t kMaximumCanonicalHeaderBytes = 4 * 1024 * 1024;
constexpr uint64_t kMinimumEncodedTypeBytes = 1;
constexpr uint64_t kMinimumEncodedGenericParameterBytes = 1;
constexpr uint64_t kMinimumEncodedBoundObligationBytes = 2;

zc::Maybe<uint64_t> decodeCount(CanonicalDecoder& decoder, uint64_t minimumElementBytes) {
  auto count = decoder.decodeSequenceSize(kMaximumCanonicalHeaderBytes / minimumElementBytes);
  if (count == zc::none || ZC_ASSERT_NONNULL(count) > decoder.remaining() / minimumElementBytes) {
    return zc::none;
  }
  return count;
}

bool lessBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] != right[index]) { return left[index] < right[index]; }
  }
  return left.size() < right.size();
}

struct EncodedObligation final {
  zc::Array<uint8_t> bytes;
  CanonicalBoundObligation value;
};

zc::Vector<CanonicalBoundObligation> sortUnique(
    zc::Vector<CanonicalBoundObligation>&& obligations) {
  zc::Vector<EncodedObligation> encoded(obligations.size());
  for (auto& obligation : obligations) {
    encoded.add(EncodedObligation{obligation.encode(), zc::mv(obligation)});
  }
  for (size_t index = 1; index < encoded.size(); ++index) {
    auto current = zc::mv(encoded[index]);
    size_t insertion = index;
    while (insertion > 0 &&
           lessBytes(current.bytes.asPtr(), encoded[insertion - 1].bytes.asPtr())) {
      encoded[insertion] = zc::mv(encoded[insertion - 1]);
      --insertion;
    }
    encoded[insertion] = zc::mv(current);
  }
  zc::Vector<CanonicalBoundObligation> result(encoded.size());
  for (size_t index = 0; index < encoded.size(); ++index) {
    if (index == 0 || encoded[index - 1].bytes.asPtr() != encoded[index].bytes.asPtr()) {
      result.add(zc::mv(encoded[index].value));
    }
  }
  return result;
}

template <typename Value>
void encodeSequence(CanonicalEncoder& encoder, zc::ArrayPtr<const Value> values) {
  encoder.encodeSequenceSize(values.size());
  for (const auto& value : values) { value.encode(encoder); }
}

}  // namespace

bool isCanonicalImplHeaderValue(ImplPolarity value) noexcept {
  return value >= ImplPolarity::Positive && value <= ImplPolarity::Negative;
}

bool isCanonicalImplHeaderValue(ImplSafety value) noexcept {
  return value >= ImplSafety::Safe && value <= ImplSafety::Unsafe;
}

CanonicalTraitReference::CanonicalTraitReference(
    zc::Own<canonical_impl_header_detail::CanonicalTraitReferenceData>&& value) noexcept
    : impl(zc::mv(value)) {}

CanonicalTraitReference::~CanonicalTraitReference() noexcept(false) = default;
CanonicalTraitReference::CanonicalTraitReference(CanonicalTraitReference&&) noexcept = default;
CanonicalTraitReference& CanonicalTraitReference::operator=(CanonicalTraitReference&&) noexcept =
    default;

zc::Maybe<CanonicalTraitReference> CanonicalTraitReference::from(
    CanonicalNameReference&& name, zc::Vector<CanonicalHeaderTypeSyntax>&& arguments) {
  const auto rootKind = name.root().kind();
  if (rootKind != CanonicalNameRootKind::Absolute && rootKind != CanonicalNameRootKind::Relative) {
    return zc::none;
  }
  return CanonicalTraitReference(
      zc::heap<canonical_impl_header_detail::CanonicalTraitReferenceData>(
          canonical_impl_header_detail::CanonicalTraitReferenceData{zc::mv(name),
                                                                    zc::mv(arguments)}));
}

zc::Maybe<CanonicalTraitReference> CanonicalTraitReference::decodeCanonical(
    CanonicalDecoder& decoder) {
  auto name = CanonicalNameReference::decodeCanonical(decoder);
  auto count = decodeCount(decoder, kMinimumEncodedTypeBytes);
  if (name == zc::none || count == zc::none) { return zc::none; }
  zc::Vector<CanonicalHeaderTypeSyntax> arguments(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto argument = CanonicalHeaderTypeSyntax::decodeCanonical(decoder);
    if (argument == zc::none) { return zc::none; }
    arguments.add(zc::mv(ZC_ASSERT_NONNULL(argument)));
  }
  return from(zc::mv(ZC_ASSERT_NONNULL(name)), zc::mv(arguments));
}

CanonicalTraitReference CanonicalTraitReference::clone() const {
  zc::Vector<CanonicalHeaderTypeSyntax> arguments(impl->arguments.size());
  for (const auto& argument : impl->arguments) { arguments.add(argument.clone()); }
  auto cloned = from(impl->name.clone(), zc::mv(arguments));
  ZC_IF_SOME(value, cloned) { return zc::mv(value); }
  ZC_UNREACHABLE
}

const CanonicalNameReference& CanonicalTraitReference::name() const noexcept { return impl->name; }

zc::ArrayPtr<const CanonicalHeaderTypeSyntax> CanonicalTraitReference::arguments() const noexcept {
  return impl->arguments.asPtr();
}

void CanonicalTraitReference::encode(CanonicalEncoder& encoder) const {
  impl->name.encode(encoder);
  encoder.encodeSequenceSize(impl->arguments.size());
  for (const auto& argument : impl->arguments) { argument.encode(encoder); }
}

zc::Array<uint8_t> CanonicalTraitReference::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

ImplHeader::ImplHeader(
    zc::Own<canonical_impl_header_detail::CanonicalImplHeaderData>&& value) noexcept
    : impl(zc::mv(value)) {}

ImplHeader::~ImplHeader() noexcept(false) = default;
ImplHeader::ImplHeader(ImplHeader&&) noexcept = default;
ImplHeader& ImplHeader::operator=(ImplHeader&&) noexcept = default;

zc::Maybe<ImplHeader> ImplHeader::from(
    zc::Vector<CanonicalGenericParameter>&& genericParameters, ImplPolarity polarity,
    ImplSafety safety, CanonicalTraitReference&& trait, CanonicalHeaderTypeSyntax&& selfType,
    zc::Vector<CanonicalBoundObligation>&& obligations) {
  if (!isCanonicalImplHeaderValue(polarity) || !isCanonicalImplHeaderValue(safety)) {
    return zc::none;
  }
  if (polarity == ImplPolarity::Negative && safety == ImplSafety::Unsafe) { return zc::none; }
  obligations = sortUnique(zc::mv(obligations));
  return ImplHeader(zc::heap<canonical_impl_header_detail::CanonicalImplHeaderData>(
      canonical_impl_header_detail::CanonicalImplHeaderData{zc::mv(genericParameters), polarity,
                                                            safety, zc::mv(trait), zc::mv(selfType),
                                                            zc::mv(obligations)}));
}

zc::Maybe<ImplHeader> ImplHeader::decodeCanonical(CanonicalDecoder& decoder) {
  auto genericCount = decodeCount(decoder, kMinimumEncodedGenericParameterBytes);
  if (genericCount == zc::none) { return zc::none; }
  zc::Vector<CanonicalGenericParameter> genericParameters(
      static_cast<size_t>(ZC_ASSERT_NONNULL(genericCount)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(genericCount); ++index) {
    auto parameter = CanonicalGenericParameter::decodeCanonical(decoder);
    if (parameter == zc::none) { return zc::none; }
    genericParameters.add(zc::mv(ZC_ASSERT_NONNULL(parameter)));
  }
  auto polarity = decoder.decodeUint8();
  auto safety = decoder.decodeUint8();
  auto trait = CanonicalTraitReference::decodeCanonical(decoder);
  auto selfType = CanonicalHeaderTypeSyntax::decodeCanonical(decoder);
  auto obligationCount = decodeCount(decoder, kMinimumEncodedBoundObligationBytes);
  if (polarity == zc::none || safety == zc::none || trait == zc::none || selfType == zc::none ||
      obligationCount == zc::none) {
    return zc::none;
  }
  zc::Vector<CanonicalBoundObligation> obligations(
      static_cast<size_t>(ZC_ASSERT_NONNULL(obligationCount)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(obligationCount); ++index) {
    auto obligation = CanonicalBoundObligation::decodeCanonical(decoder);
    if (obligation == zc::none) { return zc::none; }
    obligations.add(zc::mv(ZC_ASSERT_NONNULL(obligation)));
  }
  return from(zc::mv(genericParameters), static_cast<ImplPolarity>(ZC_ASSERT_NONNULL(polarity)),
              static_cast<ImplSafety>(ZC_ASSERT_NONNULL(safety)), zc::mv(ZC_ASSERT_NONNULL(trait)),
              zc::mv(ZC_ASSERT_NONNULL(selfType)), zc::mv(obligations));
}

ImplHeader ImplHeader::clone() const {
  zc::Vector<CanonicalGenericParameter> genericParameters(impl->genericParameters.size());
  for (const auto& generic : impl->genericParameters) { genericParameters.add(generic.clone()); }
  zc::Vector<CanonicalBoundObligation> obligations(impl->obligations.size());
  for (const auto& obligation : impl->obligations) { obligations.add(obligation.clone()); }
  auto cloned = from(zc::mv(genericParameters), impl->polarity, impl->safety, impl->trait.clone(),
                     impl->selfType.clone(), zc::mv(obligations));
  ZC_IF_SOME(value, cloned) { return zc::mv(value); }
  ZC_UNREACHABLE
}

zc::ArrayPtr<const CanonicalGenericParameter> ImplHeader::genericParameters()
    const noexcept {
  return impl->genericParameters.asPtr();
}

ImplPolarity ImplHeader::polarity() const noexcept { return impl->polarity; }

ImplSafety ImplHeader::safety() const noexcept { return impl->safety; }

const CanonicalTraitReference& ImplHeader::trait() const noexcept { return impl->trait; }

const CanonicalHeaderTypeSyntax& ImplHeader::selfType() const noexcept {
  return impl->selfType;
}

zc::ArrayPtr<const CanonicalBoundObligation> ImplHeader::obligations() const noexcept {
  return impl->obligations.asPtr();
}

void ImplHeader::encode(CanonicalEncoder& encoder) const {
  encodeSequence(encoder, impl->genericParameters.asPtr());
  encoder.encodeUint8(static_cast<uint8_t>(impl->polarity));
  encoder.encodeUint8(static_cast<uint8_t>(impl->safety));
  impl->trait.encode(encoder);
  impl->selfType.encode(encoder);
  encodeSequence(encoder, impl->obligations.asPtr());
}

zc::Array<uint8_t> ImplHeader::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

}  // namespace zomlang::compiler::identity
