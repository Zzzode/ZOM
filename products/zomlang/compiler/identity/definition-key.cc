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

#include "zomlang/compiler/identity/definition-key.h"

#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::identity {
namespace {

bool isValid(DefinitionKind value) {
  return value >= DefinitionKind::ModuleAlias && value <= DefinitionKind::ReexportAlias;
}

bool isValid(AnonymousDefinitionRole value) {
  return value == AnonymousDefinitionRole::Lambda ||
         value == AnonymousDefinitionRole::FunctionExpression;
}

}  // namespace

DefinitionNameKey::DefinitionNameKey(DeclaredDefinitionNameKey&& name) noexcept
    : value(zc::mv(name)) {}

DefinitionNameKey::DefinitionNameKey(AnonymousDefinitionNameKey&& name) noexcept
    : value(zc::mv(name)) {}

DefinitionNameKey DefinitionNameKey::declared(DeclaredDefinitionName&& name) {
  return DefinitionNameKey(DeclaredDefinitionNameKey{zc::mv(name)});
}

zc::Maybe<DefinitionNameKey> DefinitionNameKey::anonymous(AnonymousDefinitionRole role) {
  if (!isValid(role)) { return zc::none; }
  return DefinitionNameKey(AnonymousDefinitionNameKey{role});
}

DefinitionNameKey DefinitionNameKey::clone() const {
  ZC_SWITCH_ONEOF(value) {
    ZC_CASE_ONEOF(name, DeclaredDefinitionNameKey) { return declared(name.name.clone()); }
    ZC_CASE_ONEOF(name, AnonymousDefinitionNameKey) {
      ZC_IF_SOME(result, anonymous(name.role)) { return zc::mv(result); }
      ZC_UNREACHABLE
    }
  }
  ZC_UNREACHABLE
}

void DefinitionNameKey::encode(CanonicalEncoder& encoder) const {
  if (value.is<DeclaredDefinitionNameKey>()) {
    encoder.encodeUint8(static_cast<uint8_t>(DefinitionNameKind::Declared));
    value.get<DeclaredDefinitionNameKey>().name.encode(encoder);
  } else {
    encoder.encodeUint8(static_cast<uint8_t>(DefinitionNameKind::Anonymous));
    encoder.encodeUint8(
        static_cast<uint8_t>(value.get<AnonymousDefinitionNameKey>().role));
  }
}

DefinitionPathSegment::DefinitionPathSegment(DefinitionKind kind, DefinitionNameKey&& name,
                                             SourceSpan&& sourceAnchor,
                                             uint32_t siblingOrdinal) noexcept
    : kindValue(kind),
      nameValue(zc::mv(name)),
      sourceAnchorValue(zc::mv(sourceAnchor)),
      siblingOrdinalValue(siblingOrdinal) {}

zc::Maybe<DefinitionPathSegment> DefinitionPathSegment::from(
    DefinitionKind kind, DefinitionNameKey&& name, SourceSpan&& sourceAnchor,
    uint32_t siblingOrdinal) {
  if (!isValid(kind)) { return zc::none; }
  return DefinitionPathSegment(kind, zc::mv(name), zc::mv(sourceAnchor), siblingOrdinal);
}

DefinitionPathSegment DefinitionPathSegment::clone() const {
  return DefinitionPathSegment(kindValue, nameValue.clone(), sourceAnchorValue.clone(),
                               siblingOrdinalValue);
}

bool DefinitionPathSegment::belongsTo(const ModuleKey& module) const {
  return module.contains(sourceAnchorValue);
}

void DefinitionPathSegment::encode(CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(kindValue));
  nameValue.encode(encoder);
  sourceAnchorValue.encode(encoder);
  encoder.encodeUint32(siblingOrdinalValue);
}

ImplPathSegment::ImplPathSegment(SourceSpan&& sourceAnchor, uint32_t siblingOrdinal) noexcept
    : sourceAnchorValue(zc::mv(sourceAnchor)), siblingOrdinalValue(siblingOrdinal) {}

ImplPathSegment ImplPathSegment::from(SourceSpan&& sourceAnchor, uint32_t siblingOrdinal) {
  return ImplPathSegment(zc::mv(sourceAnchor), siblingOrdinal);
}

ImplPathSegment ImplPathSegment::clone() const {
  return ImplPathSegment(sourceAnchorValue.clone(), siblingOrdinalValue);
}

bool ImplPathSegment::belongsTo(const ModuleKey& module) const {
  return module.contains(sourceAnchorValue);
}

void ImplPathSegment::encode(CanonicalEncoder& encoder) const {
  sourceAnchorValue.encode(encoder);
  encoder.encodeUint32(siblingOrdinalValue);
}

DefinitionPathComponent::DefinitionPathComponent(
    DefinitionPathDefinitionComponent&& component) noexcept
    : value(zc::mv(component)) {}

DefinitionPathComponent::DefinitionPathComponent(DefinitionPathImplComponent&& component) noexcept
    : value(zc::mv(component)) {}

DefinitionPathComponent DefinitionPathComponent::definition(DefinitionPathSegment&& segment) {
  return DefinitionPathComponent(DefinitionPathDefinitionComponent{zc::mv(segment)});
}

DefinitionPathComponent DefinitionPathComponent::impl(ImplPathSegment&& segment) {
  return DefinitionPathComponent(DefinitionPathImplComponent{zc::mv(segment)});
}

DefinitionPathComponent DefinitionPathComponent::clone() const {
  ZC_SWITCH_ONEOF(value) {
    ZC_CASE_ONEOF(component, DefinitionPathDefinitionComponent) {
      return definition(component.segment.clone());
    }
    ZC_CASE_ONEOF(component, DefinitionPathImplComponent) {
      return impl(component.segment.clone());
    }
  }
  ZC_UNREACHABLE
}

DefinitionPathComponentKind DefinitionPathComponent::kind() const noexcept {
  return value.is<DefinitionPathDefinitionComponent>()
             ? DefinitionPathComponentKind::Definition
             : DefinitionPathComponentKind::Impl;
}

bool DefinitionPathComponent::belongsTo(const ModuleKey& module) const {
  if (value.is<DefinitionPathDefinitionComponent>()) {
    return value.get<DefinitionPathDefinitionComponent>().segment.belongsTo(module);
  }
  return value.get<DefinitionPathImplComponent>().segment.belongsTo(module);
}

void DefinitionPathComponent::encode(CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(kind()));
  ZC_SWITCH_ONEOF(value) {
    ZC_CASE_ONEOF(component, DefinitionPathDefinitionComponent) {
      component.segment.encode(encoder);
    }
    ZC_CASE_ONEOF(component, DefinitionPathImplComponent) {
      component.segment.encode(encoder);
    }
  }
}

DefinitionKey::DefinitionKey(ModuleKey&& module,
                             zc::Vector<DefinitionPathComponent>&& path) noexcept
    : moduleValue(zc::mv(module)), pathValue(zc::mv(path)) {}

zc::Maybe<DefinitionKey> DefinitionKey::from(ModuleKey&& module,
                                             zc::Vector<DefinitionPathComponent>&& path) {
  if (path.size() == 0 || path.back().kind() != DefinitionPathComponentKind::Definition) {
    return zc::none;
  }
  for (const auto& component : path) {
    if (!component.belongsTo(module)) { return zc::none; }
  }
  return DefinitionKey(zc::mv(module), zc::mv(path));
}

DefinitionKey DefinitionKey::clone() const {
  zc::Vector<DefinitionPathComponent> path(pathValue.size());
  for (const auto& component : pathValue) { path.add(component.clone()); }
  return DefinitionKey(moduleValue.clone(), zc::mv(path));
}

void DefinitionKey::encode(CanonicalEncoder& encoder) const {
  moduleValue.encode(encoder);
  encoder.encodeSequenceSize(pathValue.size());
  for (const auto& component : pathValue) { component.encode(encoder); }
}

zc::Array<uint8_t> DefinitionKey::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

ImplKey::ImplKey(ModuleKey&& module, zc::Vector<DefinitionPathSegment>&& parentPath,
                 SourceSpan&& source, uint32_t siblingOrdinal) noexcept
    : moduleValue(zc::mv(module)),
      parentPathValue(zc::mv(parentPath)),
      sourceValue(zc::mv(source)),
      siblingOrdinalValue(siblingOrdinal) {}

zc::Maybe<ImplKey> ImplKey::from(ModuleKey&& module,
                                 zc::Vector<DefinitionPathSegment>&& parentPath,
                                 SourceSpan&& source, uint32_t siblingOrdinal) {
  if (!module.contains(source)) { return zc::none; }
  for (const auto& segment : parentPath) {
    if (!segment.belongsTo(module)) { return zc::none; }
  }
  return ImplKey(zc::mv(module), zc::mv(parentPath), zc::mv(source), siblingOrdinal);
}

ImplKey ImplKey::clone() const {
  zc::Vector<DefinitionPathSegment> path(parentPathValue.size());
  for (const auto& segment : parentPathValue) { path.add(segment.clone()); }
  return ImplKey(moduleValue.clone(), zc::mv(path), sourceValue.clone(),
                 siblingOrdinalValue);
}

void ImplKey::encode(CanonicalEncoder& encoder) const {
  moduleValue.encode(encoder);
  encoder.encodeSequenceSize(parentPathValue.size());
  for (const auto& segment : parentPathValue) { segment.encode(encoder); }
  sourceValue.encode(encoder);
  encoder.encodeUint32(siblingOrdinalValue);
}

zc::Array<uint8_t> ImplKey::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

}  // namespace zomlang::compiler::identity
