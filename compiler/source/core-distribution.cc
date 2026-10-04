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

#include "compiler/source/core-distribution.h"

#include "compiler/identity/canonical/canonical-decoder.h"
#include "compiler/identity/canonical/canonical-encoder.h"
#include "zc/core/debug.h"
#include "zc/core/encoding.h"

namespace zomlang::compiler::source::core {
namespace {

constexpr uint64_t kMaximumPolicyEntries = 2;
constexpr uint64_t kMaximumPolicySubjects = 5;
constexpr uint64_t kMaximumPolicyPrimitives = 19;
constexpr uint64_t kMaximumPolicyReferenceRules = 2;
constexpr uint64_t kMaximumPolicyPointerRules = 2;
constexpr uint64_t kMaximumCorePolicyTemplateBytes = 4096;

bool isCoreRole(CoreSemanticRole role) {
  return role == CoreSemanticRole::Copy || role == CoreSemanticRole::Linear;
}

bool isStructuralSubject(CoreMarkerStructuralSubject subject) {
  return subject >= CoreMarkerStructuralSubject::Tuple &&
         subject <= CoreMarkerStructuralSubject::NominalEnum;
}

bool isPrimitive(type::semantic::PrimitiveKind primitive) {
  return primitive >= type::semantic::PrimitiveKind::I8 &&
         primitive <= type::semantic::PrimitiveKind::Null;
}

bool isMutability(type::semantic::Mutability mutability) {
  return mutability == type::semantic::Mutability::Const ||
         mutability == type::semantic::Mutability::Mutable;
}

bool sameBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  return left == right;
}

zc::Array<uint8_t> encodePolicyEntry(const CoreMarkerPolicyTemplateEntry& entry) {
  identity::CanonicalEncoder encoder;
  entry.encode(encoder);
  return encoder.finish();
}

zc::Maybe<CoreMarkerReferenceTemplateRule> decodeReferenceRule(
    identity::CanonicalDecoder& decoder) {
  auto kind = decoder.decodeUint8();
  if (kind == zc::none) { return zc::none; }
  switch (static_cast<CoreMarkerReferenceTemplateRuleKind>(ZC_ASSERT_NONNULL(kind))) {
    case CoreMarkerReferenceTemplateRuleKind::Unconditional:
      return CoreMarkerReferenceTemplateRule::unconditional();
    case CoreMarkerReferenceTemplateRuleKind::Requires: {
      auto role = decoder.decodeUint8();
      if (role == zc::none || !isCoreRole(static_cast<CoreSemanticRole>(ZC_ASSERT_NONNULL(role)))) {
        return zc::none;
      }
      return CoreMarkerReferenceTemplateRule::required(
          static_cast<CoreSemanticRole>(ZC_ASSERT_NONNULL(role)));
    }
  }
  return zc::none;
}

zc::Maybe<CoreMarkerPolicyTemplate> decodePolicy(identity::CanonicalDecoder& decoder) {
  auto subjectCount = decoder.decodeSequenceSize(kMaximumPolicySubjects);
  if (subjectCount == zc::none) { return zc::none; }
  zc::Vector<CoreMarkerStructuralSubject> subjects(
      static_cast<size_t>(ZC_ASSERT_NONNULL(subjectCount)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(subjectCount); ++index) {
    auto tag = decoder.decodeUint8();
    if (tag == zc::none) { return zc::none; }
    subjects.add(static_cast<CoreMarkerStructuralSubject>(ZC_ASSERT_NONNULL(tag)));
  }

  auto primitiveCount = decoder.decodeSequenceSize(kMaximumPolicyPrimitives);
  if (primitiveCount == zc::none) { return zc::none; }
  zc::Vector<type::semantic::PrimitiveKind> primitives(
      static_cast<size_t>(ZC_ASSERT_NONNULL(primitiveCount)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(primitiveCount); ++index) {
    auto tag = decoder.decodeUint8();
    if (tag == zc::none) { return zc::none; }
    primitives.add(static_cast<type::semantic::PrimitiveKind>(ZC_ASSERT_NONNULL(tag)));
  }

  auto referenceCount = decoder.decodeSequenceSize(kMaximumPolicyReferenceRules);
  if (referenceCount == zc::none) { return zc::none; }
  zc::Vector<CoreMarkerReferenceTemplateEntry> references(
      static_cast<size_t>(ZC_ASSERT_NONNULL(referenceCount)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(referenceCount); ++index) {
    auto mutability = decoder.decodeUint8();
    auto rule = decodeReferenceRule(decoder);
    if (mutability == zc::none || rule == zc::none) { return zc::none; }
    references.add(CoreMarkerReferenceTemplateEntry{
        static_cast<type::semantic::Mutability>(ZC_ASSERT_NONNULL(mutability)),
        zc::mv(ZC_ASSERT_NONNULL(rule))});
  }

  auto pointerCount = decoder.decodeSequenceSize(kMaximumPolicyPointerRules);
  if (pointerCount == zc::none) { return zc::none; }
  zc::Vector<type::semantic::Mutability> pointers(
      static_cast<size_t>(ZC_ASSERT_NONNULL(pointerCount)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(pointerCount); ++index) {
    auto tag = decoder.decodeUint8();
    if (tag == zc::none) { return zc::none; }
    pointers.add(static_cast<type::semantic::Mutability>(ZC_ASSERT_NONNULL(tag)));
  }
  return CoreMarkerPolicyTemplate::from(zc::mv(subjects), zc::mv(primitives), zc::mv(references),
                                        zc::mv(pointers));
}

}  // namespace

CoreMarkerReferenceTemplateRule::CoreMarkerReferenceTemplateRule(
    CoreMarkerReferenceTemplateRuleKind kind, zc::Maybe<CoreSemanticRole> role) noexcept
    : kindValue(kind), roleValue(role) {}

CoreMarkerReferenceTemplateRule CoreMarkerReferenceTemplateRule::unconditional() {
  return CoreMarkerReferenceTemplateRule(CoreMarkerReferenceTemplateRuleKind::Unconditional,
                                         zc::none);
}

CoreMarkerReferenceTemplateRule CoreMarkerReferenceTemplateRule::required(CoreSemanticRole role) {
  ZC_IREQUIRE(isCoreRole(role), "invalid core semantic role");
  return CoreMarkerReferenceTemplateRule(CoreMarkerReferenceTemplateRuleKind::Requires, role);
}

CoreMarkerReferenceTemplateRule CoreMarkerReferenceTemplateRule::clone() const {
  return CoreMarkerReferenceTemplateRule(kindValue, roleValue);
}

CoreMarkerReferenceTemplateRuleKind CoreMarkerReferenceTemplateRule::kind() const noexcept {
  return kindValue;
}

zc::Maybe<CoreSemanticRole> CoreMarkerReferenceTemplateRule::requiredRole() const noexcept {
  return roleValue;
}

void CoreMarkerReferenceTemplateRule::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(kindValue));
  ZC_IF_SOME(role, roleValue) { encoder.encodeUint8(static_cast<uint8_t>(role)); }
}

CoreMarkerReferenceTemplateEntry CoreMarkerReferenceTemplateEntry::clone() const {
  return CoreMarkerReferenceTemplateEntry{mutability, rule.clone()};
}

void CoreMarkerReferenceTemplateEntry::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(mutability));
  rule.encode(encoder);
}

struct CoreMarkerPolicyTemplate::Impl final {
  Impl(zc::Vector<CoreMarkerStructuralSubject>&& structuralSubjects,
       zc::Vector<type::semantic::PrimitiveKind>&& builtinPrimitives,
       zc::Vector<CoreMarkerReferenceTemplateEntry>&& referenceRules,
       zc::Vector<type::semantic::Mutability>&& rawPointerMutabilities)
      : structuralSubjects(zc::mv(structuralSubjects)),
        builtinPrimitives(zc::mv(builtinPrimitives)),
        referenceRules(zc::mv(referenceRules)),
        rawPointerMutabilities(zc::mv(rawPointerMutabilities)) {}

  zc::Vector<CoreMarkerStructuralSubject> structuralSubjects;
  zc::Vector<type::semantic::PrimitiveKind> builtinPrimitives;
  zc::Vector<CoreMarkerReferenceTemplateEntry> referenceRules;
  zc::Vector<type::semantic::Mutability> rawPointerMutabilities;
};

CoreMarkerPolicyTemplate::CoreMarkerPolicyTemplate(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
CoreMarkerPolicyTemplate::~CoreMarkerPolicyTemplate() noexcept(false) = default;
CoreMarkerPolicyTemplate::CoreMarkerPolicyTemplate(CoreMarkerPolicyTemplate&&) noexcept = default;
CoreMarkerPolicyTemplate& CoreMarkerPolicyTemplate::operator=(CoreMarkerPolicyTemplate&&) noexcept =
    default;

zc::Maybe<CoreMarkerPolicyTemplate> CoreMarkerPolicyTemplate::from(
    zc::Vector<CoreMarkerStructuralSubject>&& structuralSubjects,
    zc::Vector<type::semantic::PrimitiveKind>&& builtinPrimitives,
    zc::Vector<CoreMarkerReferenceTemplateEntry>&& referenceRules,
    zc::Vector<type::semantic::Mutability>&& rawPointerMutabilities) {
  uint8_t previous = 0;
  for (const auto subject : structuralSubjects) {
    const auto tag = static_cast<uint8_t>(subject);
    if (!isStructuralSubject(subject) || tag <= previous) { return zc::none; }
    previous = tag;
  }
  previous = 0;
  for (const auto primitive : builtinPrimitives) {
    const auto tag = static_cast<uint8_t>(primitive);
    if (!isPrimitive(primitive) || tag <= previous) { return zc::none; }
    previous = tag;
  }
  previous = 0;
  for (const auto& entry : referenceRules) {
    const auto tag = static_cast<uint8_t>(entry.mutability);
    if (!isMutability(entry.mutability) || tag <= previous ||
        (entry.rule.kind() == CoreMarkerReferenceTemplateRuleKind::Unconditional &&
         entry.mutability != type::semantic::Mutability::Const) ||
        (entry.rule.kind() == CoreMarkerReferenceTemplateRuleKind::Unconditional &&
         entry.rule.requiredRole() != zc::none) ||
        (entry.rule.kind() == CoreMarkerReferenceTemplateRuleKind::Requires &&
         entry.rule.requiredRole() == zc::none)) {
      return zc::none;
    }
    previous = tag;
  }
  previous = 0;
  for (const auto mutability : rawPointerMutabilities) {
    const auto tag = static_cast<uint8_t>(mutability);
    if (!isMutability(mutability) || tag <= previous) { return zc::none; }
    previous = tag;
  }
  return CoreMarkerPolicyTemplate(zc::heap<Impl>(zc::mv(structuralSubjects),
                                                 zc::mv(builtinPrimitives), zc::mv(referenceRules),
                                                 zc::mv(rawPointerMutabilities)));
}

CoreMarkerPolicyTemplate CoreMarkerPolicyTemplate::clone() const {
  zc::Vector<CoreMarkerStructuralSubject> subjects(impl->structuralSubjects.size());
  for (const auto value : impl->structuralSubjects) { subjects.add(value); }
  zc::Vector<type::semantic::PrimitiveKind> primitives(impl->builtinPrimitives.size());
  for (const auto value : impl->builtinPrimitives) { primitives.add(value); }
  zc::Vector<CoreMarkerReferenceTemplateEntry> references(impl->referenceRules.size());
  for (const auto& value : impl->referenceRules) { references.add(value.clone()); }
  zc::Vector<type::semantic::Mutability> pointers(impl->rawPointerMutabilities.size());
  for (const auto value : impl->rawPointerMutabilities) { pointers.add(value); }
  auto result = from(zc::mv(subjects), zc::mv(primitives), zc::mv(references), zc::mv(pointers));
  return zc::mv(ZC_ASSERT_NONNULL(result));
}

zc::ArrayPtr<const CoreMarkerStructuralSubject> CoreMarkerPolicyTemplate::structuralSubjects()
    const noexcept {
  return impl->structuralSubjects.asPtr();
}
zc::ArrayPtr<const type::semantic::PrimitiveKind> CoreMarkerPolicyTemplate::builtinPrimitives()
    const noexcept {
  return impl->builtinPrimitives.asPtr();
}
zc::ArrayPtr<const CoreMarkerReferenceTemplateEntry> CoreMarkerPolicyTemplate::referenceRules()
    const noexcept {
  return impl->referenceRules.asPtr();
}
zc::ArrayPtr<const type::semantic::Mutability> CoreMarkerPolicyTemplate::rawPointerMutabilities()
    const noexcept {
  return impl->rawPointerMutabilities.asPtr();
}
void CoreMarkerPolicyTemplate::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeSequenceSize(impl->structuralSubjects.size());
  for (const auto value : impl->structuralSubjects) {
    encoder.encodeUint8(static_cast<uint8_t>(value));
  }
  encoder.encodeSequenceSize(impl->builtinPrimitives.size());
  for (const auto value : impl->builtinPrimitives) {
    encoder.encodeUint8(static_cast<uint8_t>(value));
  }
  encoder.encodeSequenceSize(impl->referenceRules.size());
  for (const auto& value : impl->referenceRules) { value.encode(encoder); }
  encoder.encodeSequenceSize(impl->rawPointerMutabilities.size());
  for (const auto value : impl->rawPointerMutabilities) {
    encoder.encodeUint8(static_cast<uint8_t>(value));
  }
}

CoreMarkerPolicyTemplateEntry CoreMarkerPolicyTemplateEntry::clone() const {
  return CoreMarkerPolicyTemplateEntry{role, policy.clone()};
}
void CoreMarkerPolicyTemplateEntry::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(role));
  policy.encode(encoder);
}

struct CoreStandardMarkerPolicyTemplate::Impl final {
  Impl(zc::Vector<CoreMarkerPolicyTemplateEntry>&& entries, const identity::Sha256Digest& revision)
      : entries(zc::mv(entries)), revision(revision) {}
  zc::Vector<CoreMarkerPolicyTemplateEntry> entries;
  identity::Sha256Digest revision;
};

CoreStandardMarkerPolicyTemplate::CoreStandardMarkerPolicyTemplate(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
CoreStandardMarkerPolicyTemplate::~CoreStandardMarkerPolicyTemplate() noexcept(false) = default;
CoreStandardMarkerPolicyTemplate::CoreStandardMarkerPolicyTemplate(
    CoreStandardMarkerPolicyTemplate&&) noexcept = default;
CoreStandardMarkerPolicyTemplate& CoreStandardMarkerPolicyTemplate::operator=(
    CoreStandardMarkerPolicyTemplate&&) noexcept = default;

zc::Maybe<CoreStandardMarkerPolicyTemplate> CoreStandardMarkerPolicyTemplate::from(
    zc::Vector<CoreMarkerPolicyTemplateEntry>&& entries) {
  if (entries.size() != 1 || entries[0].role != CoreSemanticRole::Copy) { return zc::none; }
  zc::Vector<zc::Array<uint8_t>> records(entries.size());
  uint8_t previous = 0;
  for (const auto& entry : entries) {
    const auto tag = static_cast<uint8_t>(entry.role);
    if (!isCoreRole(entry.role) || tag <= previous) { return zc::none; }
    previous = tag;
    records.add(encodePolicyEntry(entry));
  }
  identity::CanonicalEncoder framed;
  framed.encodeSequenceSize(records.size());
  for (const auto& record : records) {
    framed.encodeSequenceSize(record.size());
    for (const auto value : record) { framed.encodeUint8(value); }
  }
  auto framing = framed.finish();
  identity::Sha256Hasher hasher;
  const uint8_t separator = 0;
  if (!hasher.update("zom.core-marker-policy-template"_zc.asBytes()) ||
      !hasher.update(zc::arrayPtr(&separator, 1)) || !hasher.update(framing.asPtr())) {
    return zc::none;
  }
  auto revision = hasher.finish();
  if (revision == zc::none) { return zc::none; }
  return CoreStandardMarkerPolicyTemplate(
      zc::heap<Impl>(zc::mv(entries), ZC_ASSERT_NONNULL(revision)));
}

CoreStandardMarkerPolicyTemplate CoreStandardMarkerPolicyTemplate::clone() const {
  zc::Vector<CoreMarkerPolicyTemplateEntry> entriesValue(impl->entries.size());
  for (const auto& entry : impl->entries) { entriesValue.add(entry.clone()); }
  auto result = from(zc::mv(entriesValue));
  return zc::mv(ZC_ASSERT_NONNULL(result));
}
zc::ArrayPtr<const CoreMarkerPolicyTemplateEntry> CoreStandardMarkerPolicyTemplate::entries()
    const noexcept {
  return impl->entries.asPtr();
}
const identity::Sha256Digest& CoreStandardMarkerPolicyTemplate::revision() const noexcept {
  return impl->revision;
}
void CoreStandardMarkerPolicyTemplate::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeSequenceSize(impl->entries.size());
  for (const auto& entry : impl->entries) { entry.encode(encoder); }
}
zc::Array<uint8_t> CoreStandardMarkerPolicyTemplate::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}
zc::Maybe<CoreStandardMarkerPolicyTemplate> CoreStandardMarkerPolicyTemplate::decodeCanonical(
    identity::CanonicalDecoder& decoder) {
  auto count = decoder.decodeSequenceSize(kMaximumPolicyEntries);
  if (count == zc::none) { return zc::none; }
  zc::Vector<CoreMarkerPolicyTemplateEntry> entries(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto role = decoder.decodeUint8();
    auto policy = decodePolicy(decoder);
    if (role == zc::none || policy == zc::none) { return zc::none; }
    entries.add(CoreMarkerPolicyTemplateEntry{
        static_cast<CoreSemanticRole>(ZC_ASSERT_NONNULL(role)), zc::mv(ZC_ASSERT_NONNULL(policy))});
  }
  return from(zc::mv(entries));
}

zc::Maybe<CoreStandardMarkerPolicyTemplate> CoreStandardMarkerPolicyTemplate::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumCorePolicyTemplateBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto result = decodeCanonical(decoder);
  if (!decoder.finished()) { return zc::none; }
  if (result == zc::none || !sameBytes(ZC_ASSERT_NONNULL(result).encode().asPtr(), bytes)) {
    return zc::none;
  }
  return zc::mv(result);
}

zc::Maybe<CoreStandardMarkerPolicyTemplate> initialCoreMarkerPolicyTemplate() {
  zc::Vector<CoreMarkerStructuralSubject> subjects(5);
  subjects.add(CoreMarkerStructuralSubject::Tuple);
  subjects.add(CoreMarkerStructuralSubject::Object);
  subjects.add(CoreMarkerStructuralSubject::FixedArray);
  subjects.add(CoreMarkerStructuralSubject::NominalStruct);
  subjects.add(CoreMarkerStructuralSubject::NominalEnum);

  zc::Vector<type::semantic::PrimitiveKind> primitives(18);
  for (uint8_t tag = static_cast<uint8_t>(type::semantic::PrimitiveKind::I8);
       tag <= static_cast<uint8_t>(type::semantic::PrimitiveKind::Never); ++tag) {
    primitives.add(static_cast<type::semantic::PrimitiveKind>(tag));
  }
  primitives.add(type::semantic::PrimitiveKind::Null);

  zc::Vector<CoreMarkerReferenceTemplateEntry> references(1);
  references.add(CoreMarkerReferenceTemplateEntry{
      type::semantic::Mutability::Const, CoreMarkerReferenceTemplateRule::unconditional()});

  zc::Vector<type::semantic::Mutability> pointers(2);
  pointers.add(type::semantic::Mutability::Const);
  pointers.add(type::semantic::Mutability::Mutable);

  auto policy = CoreMarkerPolicyTemplate::from(zc::mv(subjects), zc::mv(primitives),
                                               zc::mv(references), zc::mv(pointers));
  if (policy == zc::none) { return zc::none; }
  zc::Vector<CoreMarkerPolicyTemplateEntry> entries(1);
  entries.add(
      CoreMarkerPolicyTemplateEntry{CoreSemanticRole::Copy, zc::mv(ZC_ASSERT_NONNULL(policy))});
  return CoreStandardMarkerPolicyTemplate::from(zc::mv(entries));
}

}  // namespace zomlang::compiler::source::core
