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

#include "zomlang/compiler/source/core-distribution.h"

#include "zc/core/debug.h"
#include "zc/core/encoding.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::source::core {
namespace {

constexpr uint64_t kCoreFileCount = 3;
constexpr uint64_t kCoreRoleCount = 2;
constexpr uint64_t kMaximumModulePathSegments = 128;
constexpr uint64_t kMaximumOwnerCount = 128;
constexpr uint64_t kMaximumPolicyEntries = 2;
constexpr uint64_t kMaximumPolicySubjects = 5;
constexpr uint64_t kMaximumPolicyPrimitives = 19;
constexpr uint64_t kMaximumPolicyReferenceRules = 2;
constexpr uint64_t kMaximumPolicyPointerRules = 2;
constexpr uint64_t kMaximumCoreDistributionRecordBytes = 4096;
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

bool lessBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  return left < right;
}

bool sameBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  return left == right;
}

zc::Array<uint8_t> encodePath(const identity::CanonicalRelativePath& path) {
  identity::CanonicalEncoder encoder;
  path.encode(encoder);
  return encoder.finish();
}

zc::Array<uint8_t> encodeRole(const CoreRoleIdentityTemplate& role) {
  identity::CanonicalEncoder encoder;
  role.encode(encoder);
  return encoder.finish();
}

zc::Array<uint8_t> encodePolicyEntry(const CoreMarkerPolicyTemplateEntry& entry) {
  identity::CanonicalEncoder encoder;
  entry.encode(encoder);
  return encoder.finish();
}

zc::Maybe<identity::Sha256Digest> digestFromHex(zc::StringPtr hex) {
  auto decoded = zc::decodeHex(hex.asArray());
  if (decoded == zc::none) { return zc::none; }
  return identity::Sha256Digest::fromBytes(ZC_ASSERT_NONNULL(decoded).asPtr());
}

zc::Maybe<identity::CanonicalRelativePath> pathFromSegments(
    zc::ArrayPtr<const zc::StringPtr> texts) {
  zc::Vector<identity::CanonicalPathSegment> segments(texts.size());
  for (const auto text : texts) {
    auto segment = identity::CanonicalPathSegment::fromCanonical(text);
    if (segment == zc::none) { return zc::none; }
    ZC_IF_SOME(value, segment) { segments.add(zc::mv(value)); }
  }
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

zc::Maybe<zc::Vector<identity::ModulePathSegment>> modulePathFromSegments(
    zc::ArrayPtr<const zc::StringPtr> texts) {
  zc::Vector<identity::ModulePathSegment> segments(texts.size());
  for (const auto text : texts) {
    auto segment = identity::ModulePathSegment::fromCanonical(text);
    if (segment == zc::none) { return zc::none; }
    ZC_IF_SOME(value, segment) { segments.add(zc::mv(value)); }
  }
  return zc::mv(segments);
}

zc::Maybe<CoreSourceFile> decodeCoreSourceFile(identity::CanonicalDecoder& decoder) {
  auto path = identity::CanonicalRelativePath::decodeCanonical(decoder);
  auto digest = decoder.decodeDigest();
  if (path == zc::none || digest == zc::none) { return zc::none; }
  return CoreSourceFile::from(zc::mv(ZC_ASSERT_NONNULL(path)), ZC_ASSERT_NONNULL(digest));
}

zc::Maybe<CoreRoleIdentityTemplate> decodeCoreRole(identity::CanonicalDecoder& decoder) {
  auto roleTag = decoder.decodeUint8();
  auto moduleCount = decoder.decodeSequenceSize(kMaximumModulePathSegments);
  if (roleTag == zc::none || moduleCount == zc::none) { return zc::none; }
  zc::Vector<identity::ModulePathSegment> module(
      static_cast<size_t>(ZC_ASSERT_NONNULL(moduleCount)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(moduleCount); ++index) {
    auto segment = identity::ModulePathSegment::decodeCanonical(decoder);
    if (segment == zc::none) { return zc::none; }
    ZC_IF_SOME(value, segment) { module.add(zc::mv(value)); }
  }
  auto ownerCount = decoder.decodeSequenceSize(kMaximumOwnerCount);
  if (ownerCount == zc::none || ZC_ASSERT_NONNULL(ownerCount) != 0) { return zc::none; }
  zc::Vector<identity::EnclosingStableOwnerKey> owners;
  auto kind = decoder.decodeUint8();
  auto nameSpace = decoder.decodeUint8();
  auto name = identity::DeclaredDefinitionName::decodeCanonical(decoder);
  auto overloadPresence = decoder.decodeUint8();
  if (kind == zc::none || nameSpace == zc::none || name == zc::none ||
      overloadPresence == zc::none) {
    return zc::none;
  }
  zc::Maybe<identity::OverloadHeaderDigest> overload;
  if (ZC_ASSERT_NONNULL(overloadPresence) == 0x01) {
    auto digest = decoder.decodeDigest();
    if (digest == zc::none) { return zc::none; }
    overload = identity::OverloadHeaderDigest::fromBytes(ZC_ASSERT_NONNULL(digest).bytes());
  } else if (ZC_ASSERT_NONNULL(overloadPresence) != 0x00) {
    return zc::none;
  }
  return CoreRoleIdentityTemplate::from(
      static_cast<CoreSemanticRole>(ZC_ASSERT_NONNULL(roleTag)), zc::mv(module), zc::mv(owners),
      static_cast<identity::DefinitionKind>(ZC_ASSERT_NONNULL(kind)),
      static_cast<identity::DefinitionNamespace>(ZC_ASSERT_NONNULL(nameSpace)),
      zc::mv(ZC_ASSERT_NONNULL(name)), zc::mv(overload));
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

bool isInitialRole(const CoreRoleIdentityTemplate& role, CoreSemanticRole expectedRole,
                   zc::StringPtr expectedName) {
  return role.role() == expectedRole && role.module().size() == 2 &&
         role.module()[0].text() == "core"_zc && role.module()[1].text() == "marker"_zc &&
         role.owners().size() == 0 && role.kind() == identity::DefinitionKind::Interface &&
         role.nameSpace() == identity::DefinitionNamespace::Type &&
         role.declaredName() == expectedName && role.overloadHeader() == zc::none;
}

bool hasPath(const identity::CanonicalRelativePath& path,
             zc::ArrayPtr<const zc::StringPtr> expected) {
  if (path.segments().size() != expected.size()) { return false; }
  for (size_t index = 0; index < expected.size(); ++index) {
    if (path.segments()[index].text() != expected[index]) { return false; }
  }
  return true;
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

struct CoreRoleIdentityTemplate::Impl final {
  Impl(CoreSemanticRole role, zc::Vector<identity::ModulePathSegment>&& module,
       zc::Vector<identity::EnclosingStableOwnerKey>&& owners, identity::DefinitionKind kind,
       identity::DefinitionNamespace nameSpace, identity::DeclaredDefinitionName&& declaredName,
       zc::Maybe<identity::OverloadHeaderDigest>&& overloadHeader)
      : role(role),
        module(zc::mv(module)),
        owners(zc::mv(owners)),
        kind(kind),
        nameSpace(nameSpace),
        declaredName(zc::mv(declaredName)),
        overloadHeader(zc::mv(overloadHeader)) {}

  CoreSemanticRole role;
  zc::Vector<identity::ModulePathSegment> module;
  zc::Vector<identity::EnclosingStableOwnerKey> owners;
  identity::DefinitionKind kind;
  identity::DefinitionNamespace nameSpace;
  identity::DeclaredDefinitionName declaredName;
  zc::Maybe<identity::OverloadHeaderDigest> overloadHeader;
};

CoreRoleIdentityTemplate::CoreRoleIdentityTemplate(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
CoreRoleIdentityTemplate::~CoreRoleIdentityTemplate() noexcept(false) = default;
CoreRoleIdentityTemplate::CoreRoleIdentityTemplate(CoreRoleIdentityTemplate&&) noexcept = default;
CoreRoleIdentityTemplate& CoreRoleIdentityTemplate::operator=(CoreRoleIdentityTemplate&&) noexcept =
    default;

zc::Maybe<CoreRoleIdentityTemplate> CoreRoleIdentityTemplate::from(
    CoreSemanticRole role, zc::Vector<identity::ModulePathSegment>&& module,
    zc::Vector<identity::EnclosingStableOwnerKey>&& owners, identity::DefinitionKind kind,
    identity::DefinitionNamespace nameSpace, identity::DeclaredDefinitionName&& declaredName,
    zc::Maybe<identity::OverloadHeaderDigest>&& overloadHeader) {
  if (!isCoreRole(role) || module.empty() || !owners.empty() ||
      kind != identity::DefinitionKind::Interface ||
      nameSpace != identity::DefinitionNamespace::Type || overloadHeader != zc::none) {
    return zc::none;
  }
  return CoreRoleIdentityTemplate(zc::heap<Impl>(role, zc::mv(module), zc::mv(owners), kind,
                                                 nameSpace, zc::mv(declaredName),
                                                 zc::mv(overloadHeader)));
}

CoreRoleIdentityTemplate CoreRoleIdentityTemplate::clone() const {
  zc::Vector<identity::ModulePathSegment> moduleValue(impl->module.size());
  for (const auto& segment : impl->module) { moduleValue.add(segment.clone()); }
  zc::Vector<identity::EnclosingStableOwnerKey> ownerValues(impl->owners.size());
  for (const auto& owner : impl->owners) { ownerValues.add(owner.clone()); }
  zc::Maybe<identity::OverloadHeaderDigest> overload;
  ZC_IF_SOME(value, impl->overloadHeader) { overload = value.clone(); }
  auto result = from(impl->role, zc::mv(moduleValue), zc::mv(ownerValues), impl->kind,
                     impl->nameSpace, impl->declaredName.clone(), zc::mv(overload));
  return zc::mv(ZC_ASSERT_NONNULL(result));
}

CoreSemanticRole CoreRoleIdentityTemplate::role() const noexcept { return impl->role; }
zc::ArrayPtr<const identity::ModulePathSegment> CoreRoleIdentityTemplate::module() const noexcept {
  return impl->module.asPtr();
}
zc::ArrayPtr<const identity::EnclosingStableOwnerKey> CoreRoleIdentityTemplate::owners()
    const noexcept {
  return impl->owners.asPtr();
}
identity::DefinitionKind CoreRoleIdentityTemplate::kind() const noexcept { return impl->kind; }
identity::DefinitionNamespace CoreRoleIdentityTemplate::nameSpace() const noexcept {
  return impl->nameSpace;
}
zc::StringPtr CoreRoleIdentityTemplate::declaredName() const noexcept {
  return impl->declaredName.text();
}
zc::Maybe<const identity::OverloadHeaderDigest&> CoreRoleIdentityTemplate::overloadHeader()
    const noexcept {
  ZC_IF_SOME(value, impl->overloadHeader) { return value; }
  return zc::none;
}

void CoreRoleIdentityTemplate::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(impl->role));
  encoder.encodeSequenceSize(impl->module.size());
  for (const auto& segment : impl->module) { segment.encode(encoder); }
  encoder.encodeSequenceSize(impl->owners.size());
  for (const auto& owner : impl->owners) { owner.encode(encoder); }
  encoder.encodeUint8(static_cast<uint8_t>(impl->kind));
  encoder.encodeUint8(static_cast<uint8_t>(impl->nameSpace));
  impl->declaredName.encode(encoder);
  ZC_IF_SOME(value, impl->overloadHeader) {
    encoder.encodeSome();
    value.encode(encoder);
  } else {
    encoder.encodeNone();
  }
}

CoreSourceFile::CoreSourceFile(identity::CanonicalRelativePath&& path,
                               const identity::Sha256Digest& digest) noexcept
    : pathValue(zc::mv(path)), digestValue(digest) {}

CoreSourceFile CoreSourceFile::from(identity::CanonicalRelativePath&& path,
                                    const identity::Sha256Digest& digest) {
  return CoreSourceFile(zc::mv(path), digest);
}
CoreSourceFile CoreSourceFile::clone() const {
  return CoreSourceFile(pathValue.clone(), digestValue);
}
const identity::CanonicalRelativePath& CoreSourceFile::path() const noexcept { return pathValue; }
const identity::Sha256Digest& CoreSourceFile::digest() const noexcept { return digestValue; }
void CoreSourceFile::encode(identity::CanonicalEncoder& encoder) const {
  pathValue.encode(encoder);
  encoder.encodeDigest(digestValue);
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

struct CoreDistributionRecord::Impl final {
  Impl(uint32_t editionYear, identity::CanonicalRelativePath&& rootModule,
       identity::CanonicalRelativePath&& preludeModule, zc::Vector<CoreSourceFile>&& files,
       zc::Vector<CoreRoleIdentityTemplate>&& roles)
      : editionYear(editionYear),
        rootModule(zc::mv(rootModule)),
        preludeModule(zc::mv(preludeModule)),
        files(zc::mv(files)),
        roles(zc::mv(roles)) {}
  uint32_t editionYear;
  identity::CanonicalRelativePath rootModule;
  identity::CanonicalRelativePath preludeModule;
  zc::Vector<CoreSourceFile> files;
  zc::Vector<CoreRoleIdentityTemplate> roles;
};

CoreDistributionRecord::CoreDistributionRecord(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
CoreDistributionRecord::~CoreDistributionRecord() noexcept(false) = default;
CoreDistributionRecord::CoreDistributionRecord(CoreDistributionRecord&&) noexcept = default;
CoreDistributionRecord& CoreDistributionRecord::operator=(CoreDistributionRecord&&) noexcept =
    default;

zc::Maybe<CoreDistributionRecord> CoreDistributionRecord::from(
    uint32_t editionYear, identity::CanonicalRelativePath&& rootModule,
    identity::CanonicalRelativePath&& preludeModule, zc::Vector<CoreSourceFile>&& files,
    zc::Vector<CoreRoleIdentityTemplate>&& roles) {
  const zc::StringPtr rootPath[] = {"core.zom"_zc};
  const zc::StringPtr preludePath[] = {"core"_zc, "prelude.zom"_zc};
  if (editionYear != 2026 || !hasPath(rootModule, rootPath) ||
      !hasPath(preludeModule, preludePath) || files.size() != kCoreFileCount ||
      roles.size() != kCoreRoleCount) {
    return zc::none;
  }
  for (size_t index = 1; index < files.size(); ++index) {
    if (!lessBytes(encodePath(files[index - 1].path()).asPtr(),
                   encodePath(files[index].path()).asPtr())) {
      return zc::none;
    }
  }
  for (size_t index = 1; index < roles.size(); ++index) {
    if (!lessBytes(encodeRole(roles[index - 1]).asPtr(), encodeRole(roles[index]).asPtr())) {
      return zc::none;
    }
  }
  if (!isInitialRole(roles[0], CoreSemanticRole::Copy, "Copy"_zc) ||
      !isInitialRole(roles[1], CoreSemanticRole::Linear, "Linear"_zc)) {
    return zc::none;
  }
  return CoreDistributionRecord(zc::heap<Impl>(
      editionYear, zc::mv(rootModule), zc::mv(preludeModule), zc::mv(files), zc::mv(roles)));
}

CoreDistributionRecord CoreDistributionRecord::clone() const {
  zc::Vector<CoreSourceFile> filesValue(impl->files.size());
  for (const auto& file : impl->files) { filesValue.add(file.clone()); }
  zc::Vector<CoreRoleIdentityTemplate> rolesValue(impl->roles.size());
  for (const auto& role : impl->roles) { rolesValue.add(role.clone()); }
  auto result = from(impl->editionYear, impl->rootModule.clone(), impl->preludeModule.clone(),
                     zc::mv(filesValue), zc::mv(rolesValue));
  return zc::mv(ZC_ASSERT_NONNULL(result));
}
uint32_t CoreDistributionRecord::editionYear() const noexcept { return impl->editionYear; }
const identity::CanonicalRelativePath& CoreDistributionRecord::rootModule() const noexcept {
  return impl->rootModule;
}
const identity::CanonicalRelativePath& CoreDistributionRecord::preludeModule() const noexcept {
  return impl->preludeModule;
}
zc::ArrayPtr<const CoreSourceFile> CoreDistributionRecord::files() const noexcept {
  return impl->files.asPtr();
}
zc::ArrayPtr<const CoreRoleIdentityTemplate> CoreDistributionRecord::roles() const noexcept {
  return impl->roles.asPtr();
}
void CoreDistributionRecord::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeUint32(impl->editionYear);
  impl->rootModule.encode(encoder);
  impl->preludeModule.encode(encoder);
  encoder.encodeSequenceSize(impl->files.size());
  for (const auto& file : impl->files) { file.encode(encoder); }
  encoder.encodeSequenceSize(impl->roles.size());
  for (const auto& role : impl->roles) { role.encode(encoder); }
}
zc::Array<uint8_t> CoreDistributionRecord::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}
zc::Maybe<CoreDistributionRecord> CoreDistributionRecord::decodeCanonical(
    identity::CanonicalDecoder& decoder) {
  auto edition = decoder.decodeUint32();
  auto root = identity::CanonicalRelativePath::decodeCanonical(decoder);
  auto prelude = identity::CanonicalRelativePath::decodeCanonical(decoder);
  auto fileCount = decoder.decodeSequenceSize(kCoreFileCount);
  if (edition == zc::none || root == zc::none || prelude == zc::none || fileCount == zc::none) {
    return zc::none;
  }
  zc::Vector<CoreSourceFile> files(static_cast<size_t>(ZC_ASSERT_NONNULL(fileCount)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(fileCount); ++index) {
    auto file = decodeCoreSourceFile(decoder);
    if (file == zc::none) { return zc::none; }
    ZC_IF_SOME(value, file) { files.add(zc::mv(value)); }
  }
  auto roleCount = decoder.decodeSequenceSize(kCoreRoleCount);
  if (roleCount == zc::none) { return zc::none; }
  zc::Vector<CoreRoleIdentityTemplate> roles(static_cast<size_t>(ZC_ASSERT_NONNULL(roleCount)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(roleCount); ++index) {
    auto role = decodeCoreRole(decoder);
    if (role == zc::none) { return zc::none; }
    ZC_IF_SOME(value, role) { roles.add(zc::mv(value)); }
  }
  return from(ZC_ASSERT_NONNULL(edition), zc::mv(ZC_ASSERT_NONNULL(root)),
              zc::mv(ZC_ASSERT_NONNULL(prelude)), zc::mv(files), zc::mv(roles));
}

zc::Maybe<CoreDistributionRecord> CoreDistributionRecord::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumCoreDistributionRecordBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto result = decodeCanonical(decoder);
  if (!decoder.finished()) { return zc::none; }
  if (result == zc::none || !sameBytes(ZC_ASSERT_NONNULL(result).encode().asPtr(), bytes)) {
    return zc::none;
  }
  return zc::mv(result);
}

struct CoreDistributionInputRecord::Impl final {
  Impl(CoreDistributionRecord&& record, const identity::Sha256Digest& digest,
       CoreStandardMarkerPolicyTemplate&& policyTemplate)
      : record(zc::mv(record)), digest(digest), policyTemplate(zc::mv(policyTemplate)) {}
  CoreDistributionRecord record;
  identity::Sha256Digest digest;
  CoreStandardMarkerPolicyTemplate policyTemplate;
};
CoreDistributionInputRecord::CoreDistributionInputRecord(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
CoreDistributionInputRecord::~CoreDistributionInputRecord() noexcept(false) = default;
CoreDistributionInputRecord::CoreDistributionInputRecord(CoreDistributionInputRecord&&) noexcept =
    default;
CoreDistributionInputRecord& CoreDistributionInputRecord::operator=(
    CoreDistributionInputRecord&&) noexcept = default;

zc::Maybe<CoreDistributionInputRecord> CoreDistributionInputRecord::from(
    CoreDistributionRecord&& record, const identity::Sha256Digest& digest,
    CoreStandardMarkerPolicyTemplate&& policyTemplate) {
  auto computed = computeCoreDistributionDigest(record);
  if (computed == zc::none || ZC_ASSERT_NONNULL(computed) != digest) { return zc::none; }
  return CoreDistributionInputRecord(
      zc::heap<Impl>(zc::mv(record), digest, zc::mv(policyTemplate)));
}
zc::Maybe<CoreDistributionInputRecord> CoreDistributionInputRecord::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 ||
      bytes.size() > kMaximumCoreDistributionRecordBytes + 32 + kMaximumCorePolicyTemplateBytes) {
    return zc::none;
  }
  identity::CanonicalDecoder decoder(bytes);
  auto record = CoreDistributionRecord::decodeCanonical(decoder);
  auto digest = decoder.decodeDigest();
  auto policy = CoreStandardMarkerPolicyTemplate::decodeCanonical(decoder);
  if (record == zc::none || digest == zc::none || policy == zc::none || !decoder.finished()) {
    return zc::none;
  }
  auto result = from(zc::mv(ZC_ASSERT_NONNULL(record)), ZC_ASSERT_NONNULL(digest),
                     zc::mv(ZC_ASSERT_NONNULL(policy)));
  if (result == zc::none || !sameBytes(ZC_ASSERT_NONNULL(result).encode().asPtr(), bytes)) {
    return zc::none;
  }
  return zc::mv(result);
}
CoreDistributionInputRecord CoreDistributionInputRecord::clone() const {
  auto result = from(impl->record.clone(), impl->digest, impl->policyTemplate.clone());
  return zc::mv(ZC_ASSERT_NONNULL(result));
}
const CoreDistributionRecord& CoreDistributionInputRecord::record() const noexcept {
  return impl->record;
}
const identity::Sha256Digest& CoreDistributionInputRecord::digest() const noexcept {
  return impl->digest;
}
const CoreStandardMarkerPolicyTemplate& CoreDistributionInputRecord::policyTemplate()
    const noexcept {
  return impl->policyTemplate;
}
void CoreDistributionInputRecord::encode(identity::CanonicalEncoder& encoder) const {
  impl->record.encode(encoder);
  encoder.encodeDigest(impl->digest);
  impl->policyTemplate.encode(encoder);
}
zc::Array<uint8_t> CoreDistributionInputRecord::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

zc::Maybe<identity::Sha256Digest> computeCoreDistributionDigest(
    const CoreDistributionRecord& record) {
  auto bytes = record.encode();
  identity::Sha256Hasher hasher;
  const uint8_t separator = 0;
  if (!hasher.update("zom.core-distribution"_zc.asBytes()) ||
      !hasher.update(zc::arrayPtr(&separator, 1)) || !hasher.update(bytes.asPtr())) {
    return zc::none;
  }
  return hasher.finish();
}

zc::Maybe<CoreDistributionRecord> initialCoreDistributionRecord() {
  const zc::StringPtr rootSegments[] = {"core.zom"_zc};
  const zc::StringPtr markerSegments[] = {"core"_zc, "marker.zom"_zc};
  const zc::StringPtr preludeSegments[] = {"core"_zc, "prelude.zom"_zc};
  auto rootPath = pathFromSegments(rootSegments);
  auto markerPath = pathFromSegments(markerSegments);
  auto preludePath = pathFromSegments(preludeSegments);
  auto rootDigest =
      digestFromHex("63421b0e8a03da646d4e6427231bc743df2731122b56d7e23ebe4425c9c8e9d7"_zc);
  auto markerDigest =
      digestFromHex("0dcee31a4992b85ec803f7073e6c03519b6e963325559af28bed1443a86a9a0f"_zc);
  auto preludeDigest =
      digestFromHex("2431a21b2a9bec11481b2c56d4b7099865f44df38515155391e3c9b0b12dd357"_zc);
  if (rootPath == zc::none || markerPath == zc::none || preludePath == zc::none ||
      rootDigest == zc::none || markerDigest == zc::none || preludeDigest == zc::none) {
    return zc::none;
  }

  zc::Vector<CoreSourceFile> files(kCoreFileCount);
  files.add(
      CoreSourceFile::from(zc::mv(ZC_ASSERT_NONNULL(rootPath)), ZC_ASSERT_NONNULL(rootDigest)));
  files.add(
      CoreSourceFile::from(zc::mv(ZC_ASSERT_NONNULL(markerPath)), ZC_ASSERT_NONNULL(markerDigest)));
  files.add(CoreSourceFile::from(zc::mv(ZC_ASSERT_NONNULL(preludePath)),
                                 ZC_ASSERT_NONNULL(preludeDigest)));

  const zc::StringPtr markerModuleSegments[] = {"core"_zc, "marker"_zc};
  auto copyModule = modulePathFromSegments(markerModuleSegments);
  auto linearModule = modulePathFromSegments(markerModuleSegments);
  auto copyName = identity::DeclaredDefinitionName::fromCanonical("Copy"_zc);
  auto linearName = identity::DeclaredDefinitionName::fromCanonical("Linear"_zc);
  if (copyModule == zc::none || linearModule == zc::none || copyName == zc::none ||
      linearName == zc::none) {
    return zc::none;
  }
  zc::Vector<identity::EnclosingStableOwnerKey> copyOwners;
  zc::Vector<identity::EnclosingStableOwnerKey> linearOwners;
  zc::Maybe<identity::OverloadHeaderDigest> noCopyOverload;
  zc::Maybe<identity::OverloadHeaderDigest> noLinearOverload;
  auto copy = CoreRoleIdentityTemplate::from(
      CoreSemanticRole::Copy, zc::mv(ZC_ASSERT_NONNULL(copyModule)), zc::mv(copyOwners),
      identity::DefinitionKind::Interface, identity::DefinitionNamespace::Type,
      zc::mv(ZC_ASSERT_NONNULL(copyName)), zc::mv(noCopyOverload));
  auto linear = CoreRoleIdentityTemplate::from(
      CoreSemanticRole::Linear, zc::mv(ZC_ASSERT_NONNULL(linearModule)), zc::mv(linearOwners),
      identity::DefinitionKind::Interface, identity::DefinitionNamespace::Type,
      zc::mv(ZC_ASSERT_NONNULL(linearName)), zc::mv(noLinearOverload));
  if (copy == zc::none || linear == zc::none) { return zc::none; }
  zc::Vector<CoreRoleIdentityTemplate> roles(kCoreRoleCount);
  roles.add(zc::mv(ZC_ASSERT_NONNULL(copy)));
  roles.add(zc::mv(ZC_ASSERT_NONNULL(linear)));

  auto rootModule = pathFromSegments(rootSegments);
  auto preludeModule = pathFromSegments(preludeSegments);
  if (rootModule == zc::none || preludeModule == zc::none) { return zc::none; }
  return CoreDistributionRecord::from(2026, zc::mv(ZC_ASSERT_NONNULL(rootModule)),
                                      zc::mv(ZC_ASSERT_NONNULL(preludeModule)), zc::mv(files),
                                      zc::mv(roles));
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

zc::Maybe<CoreDistributionInputRecord> initialCoreDistributionInput() {
  auto record = initialCoreDistributionRecord();
  auto policy = initialCoreMarkerPolicyTemplate();
  if (record == zc::none || policy == zc::none) { return zc::none; }
  auto digest = computeCoreDistributionDigest(ZC_ASSERT_NONNULL(record));
  if (digest == zc::none) { return zc::none; }
  return CoreDistributionInputRecord::from(zc::mv(ZC_ASSERT_NONNULL(record)),
                                           ZC_ASSERT_NONNULL(digest),
                                           zc::mv(ZC_ASSERT_NONNULL(policy)));
}

}  // namespace zomlang::compiler::source::core
