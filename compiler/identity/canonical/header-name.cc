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

#include "compiler/identity/canonical/header-name.h"

#include "zc/core/debug.h"
#include "compiler/identity/canonical/canonical-decoder.h"
#include "compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::identity {
namespace {

template <typename Enum>
bool inClosedRange(Enum value, Enum first, Enum last) noexcept {
  return value >= first && value <= last;
}

constexpr uint64_t kMaximumCanonicalHeaderBytes = 4 * 1024 * 1024;
constexpr uint64_t kMinimumEncodedIdentifierBytes = sizeof(uint64_t) + 1;
constexpr uint64_t kMaximumNameSuffixCount =
    kMaximumCanonicalHeaderBytes / kMinimumEncodedIdentifierBytes;

}  // namespace

bool isCanonicalHeaderValue(CallableHeaderKind value) noexcept {
  return inClosedRange(value, CallableHeaderKind::Function, CallableHeaderKind::Constructor);
}

bool isCanonicalHeaderValue(ReceiverShape value) noexcept {
  return inClosedRange(value, ReceiverShape::Shared, ReceiverShape::Move);
}

bool isCanonicalHeaderValue(ExternalAbi value) noexcept {
  return inClosedRange(value, ExternalAbi::Cdecl, ExternalAbi::ZomNative);
}

bool isCanonicalHeaderValue(PredefinedTypeKind value) noexcept {
  return inClosedRange(value, PredefinedTypeKind::I8, PredefinedTypeKind::Any);
}

bool isCanonicalHeaderValue(ReferenceMutability value) noexcept {
  return inClosedRange(value, ReferenceMutability::Shared, ReferenceMutability::Mutable);
}

bool isCanonicalHeaderValue(RawPointerMutability value) noexcept {
  return inClosedRange(value, RawPointerMutability::Const, RawPointerMutability::Mutable);
}

bool isCanonicalHeaderValue(CanonicalNameRootKind value) noexcept {
  return inClosedRange(value, CanonicalNameRootKind::Absolute, CanonicalNameRootKind::Generic);
}

CanonicalNameRoot::CanonicalNameRoot(CanonicalNameRootKind kind, uint32_t binderDepth,
                                     uint32_t ordinal) noexcept
    : kindValue(kind), binderDepthValue(binderDepth), ordinalValue(ordinal) {}

CanonicalNameRoot CanonicalNameRoot::absolute() noexcept {
  return CanonicalNameRoot(CanonicalNameRootKind::Absolute, 0, 0);
}

CanonicalNameRoot CanonicalNameRoot::relative() noexcept {
  return CanonicalNameRoot(CanonicalNameRootKind::Relative, 0, 0);
}

CanonicalNameRoot CanonicalNameRoot::generic(uint32_t binderDepth, uint32_t ordinal) noexcept {
  return CanonicalNameRoot(CanonicalNameRootKind::Generic, binderDepth, ordinal);
}

zc::Maybe<CanonicalNameRoot> CanonicalNameRoot::decodeCanonical(CanonicalDecoder& decoder) {
  auto tag = decoder.decodeUint8();
  if (tag == zc::none) { return zc::none; }
  switch (static_cast<CanonicalNameRootKind>(ZC_ASSERT_NONNULL(tag))) {
    case CanonicalNameRootKind::Absolute:
      return absolute();
    case CanonicalNameRootKind::Relative:
      return relative();
    case CanonicalNameRootKind::Generic: {
      auto binderDepth = decoder.decodeUint32();
      auto ordinal = decoder.decodeUint32();
      if (binderDepth == zc::none || ordinal == zc::none) { return zc::none; }
      return generic(ZC_ASSERT_NONNULL(binderDepth), ZC_ASSERT_NONNULL(ordinal));
    }
  }
  return zc::none;
}

CanonicalNameRoot CanonicalNameRoot::clone() const noexcept {
  return CanonicalNameRoot(kindValue, binderDepthValue, ordinalValue);
}

CanonicalNameRootKind CanonicalNameRoot::kind() const noexcept { return kindValue; }

zc::Maybe<uint32_t> CanonicalNameRoot::binderDepth() const noexcept {
  return kindValue == CanonicalNameRootKind::Generic ? zc::Maybe<uint32_t>(binderDepthValue)
                                                     : zc::none;
}

zc::Maybe<uint32_t> CanonicalNameRoot::ordinal() const noexcept {
  return kindValue == CanonicalNameRootKind::Generic ? zc::Maybe<uint32_t>(ordinalValue) : zc::none;
}

void CanonicalNameRoot::encode(CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(kindValue));
  if (kindValue == CanonicalNameRootKind::Generic) {
    encoder.encodeUint32(binderDepthValue);
    encoder.encodeUint32(ordinalValue);
  }
}

CanonicalNameReference::CanonicalNameReference(CanonicalNameRoot&& root,
                                               zc::Vector<SemanticIdentifier>&& suffix) noexcept
    : rootValue(zc::mv(root)), suffixValue(zc::mv(suffix)) {}

zc::Maybe<CanonicalNameReference> CanonicalNameReference::from(
    CanonicalNameRoot&& root, zc::Vector<SemanticIdentifier>&& suffix) {
  if (root.kind() != CanonicalNameRootKind::Generic && suffix.size() == 0) { return zc::none; }
  return CanonicalNameReference(zc::mv(root), zc::mv(suffix));
}

zc::Maybe<CanonicalNameReference> CanonicalNameReference::decodeCanonical(
    CanonicalDecoder& decoder) {
  auto root = CanonicalNameRoot::decodeCanonical(decoder);
  auto count = decoder.decodeSequenceSize(kMaximumNameSuffixCount);
  if (root == zc::none || count == zc::none ||
      ZC_ASSERT_NONNULL(count) > decoder.remaining() / kMinimumEncodedIdentifierBytes) {
    return zc::none;
  }
  zc::Vector<SemanticIdentifier> suffix(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto segment = SemanticIdentifier::decodeCanonical(decoder);
    if (segment == zc::none) { return zc::none; }
    suffix.add(zc::mv(ZC_ASSERT_NONNULL(segment)));
  }
  return from(zc::mv(ZC_ASSERT_NONNULL(root)), zc::mv(suffix));
}

CanonicalNameReference CanonicalNameReference::clone() const {
  zc::Vector<SemanticIdentifier> suffix(suffixValue.size());
  for (const auto& segment : suffixValue) { suffix.add(segment.clone()); }
  return CanonicalNameReference(rootValue.clone(), zc::mv(suffix));
}

const CanonicalNameRoot& CanonicalNameReference::root() const noexcept { return rootValue; }

zc::ArrayPtr<const SemanticIdentifier> CanonicalNameReference::suffix() const noexcept {
  return suffixValue.asPtr();
}

void CanonicalNameReference::encode(CanonicalEncoder& encoder) const {
  rootValue.encode(encoder);
  encoder.encodeSequenceSize(suffixValue.size());
  for (const auto& segment : suffixValue) { segment.encode(encoder); }
}

zc::Array<uint8_t> CanonicalNameReference::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

}  // namespace zomlang::compiler::identity
