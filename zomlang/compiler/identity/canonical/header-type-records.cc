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

#include "zomlang/compiler/identity/canonical/canonical-encoder.h"
#include "zomlang/compiler/identity/canonical/header-type-data.h"
#include "zomlang/compiler/identity/canonical/header-type.h"

namespace zomlang::compiler::identity {

using canonical_header_type_detail::CanonicalAssociatedBindingData;
using canonical_header_type_detail::CanonicalNamedHeaderTypeData;
using canonical_header_type_detail::CanonicalObjectTypeMemberData;

CanonicalNamedHeaderType::CanonicalNamedHeaderType(
    zc::Own<CanonicalNamedHeaderTypeData>&& value) noexcept
    : impl(zc::mv(value)) {}
CanonicalNamedHeaderType::~CanonicalNamedHeaderType() noexcept(false) = default;
CanonicalNamedHeaderType::CanonicalNamedHeaderType(CanonicalNamedHeaderType&&) noexcept = default;
CanonicalNamedHeaderType& CanonicalNamedHeaderType::operator=(CanonicalNamedHeaderType&&) noexcept =
    default;

CanonicalNamedHeaderType CanonicalNamedHeaderType::from(
    CanonicalNameReference&& name, zc::Vector<CanonicalHeaderTypeSyntax>&& arguments) {
  return CanonicalNamedHeaderType(zc::heap<CanonicalNamedHeaderTypeData>(
      CanonicalNamedHeaderTypeData{zc::mv(name), zc::mv(arguments)}));
}

CanonicalNamedHeaderType CanonicalNamedHeaderType::clone() const {
  zc::Vector<CanonicalHeaderTypeSyntax> arguments(impl->arguments.size());
  for (const auto& argument : impl->arguments) { arguments.add(argument.clone()); }
  return from(impl->name.clone(), zc::mv(arguments));
}

const CanonicalNameReference& CanonicalNamedHeaderType::name() const noexcept { return impl->name; }

zc::ArrayPtr<const CanonicalHeaderTypeSyntax> CanonicalNamedHeaderType::arguments() const noexcept {
  return impl->arguments.asPtr();
}

void CanonicalNamedHeaderType::encode(CanonicalEncoder& encoder) const {
  impl->name.encode(encoder);
  encoder.encodeSequenceSize(impl->arguments.size());
  for (const auto& argument : impl->arguments) { argument.encode(encoder); }
}

zc::Array<uint8_t> CanonicalNamedHeaderType::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

CanonicalObjectTypeMember::CanonicalObjectTypeMember(
    zc::Own<CanonicalObjectTypeMemberData>&& value) noexcept
    : impl(zc::mv(value)) {}
CanonicalObjectTypeMember::~CanonicalObjectTypeMember() noexcept(false) = default;
CanonicalObjectTypeMember::CanonicalObjectTypeMember(CanonicalObjectTypeMember&&) noexcept =
    default;
CanonicalObjectTypeMember& CanonicalObjectTypeMember::operator=(
    CanonicalObjectTypeMember&&) noexcept = default;

CanonicalObjectTypeMember CanonicalObjectTypeMember::from(SemanticIdentifier&& name,
                                                          CanonicalHeaderTypeSyntax&& type,
                                                          bool isMutable, bool isOptional) {
  return CanonicalObjectTypeMember(zc::heap<CanonicalObjectTypeMemberData>(
      CanonicalObjectTypeMemberData{zc::mv(name), zc::mv(type), isMutable, isOptional}));
}

CanonicalObjectTypeMember CanonicalObjectTypeMember::clone() const {
  return from(impl->name.clone(), impl->type.clone(), impl->isMutable, impl->isOptional);
}

zc::StringPtr CanonicalObjectTypeMember::name() const noexcept { return impl->name.text(); }

const CanonicalHeaderTypeSyntax& CanonicalObjectTypeMember::type() const noexcept {
  return impl->type;
}

bool CanonicalObjectTypeMember::isMutable() const noexcept { return impl->isMutable; }

bool CanonicalObjectTypeMember::isOptional() const noexcept { return impl->isOptional; }

void CanonicalObjectTypeMember::encode(CanonicalEncoder& encoder) const {
  impl->name.encode(encoder);
  impl->type.encode(encoder);
  encoder.encodeBool(impl->isMutable);
  encoder.encodeBool(impl->isOptional);
}

zc::Array<uint8_t> CanonicalObjectTypeMember::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

CanonicalAssociatedBinding::CanonicalAssociatedBinding(
    zc::Own<CanonicalAssociatedBindingData>&& value) noexcept
    : impl(zc::mv(value)) {}
CanonicalAssociatedBinding::~CanonicalAssociatedBinding() noexcept(false) = default;
CanonicalAssociatedBinding::CanonicalAssociatedBinding(CanonicalAssociatedBinding&&) noexcept =
    default;
CanonicalAssociatedBinding& CanonicalAssociatedBinding::operator=(
    CanonicalAssociatedBinding&&) noexcept = default;

CanonicalAssociatedBinding CanonicalAssociatedBinding::from(SemanticIdentifier&& name,
                                                            CanonicalHeaderTypeSyntax&& type) {
  return CanonicalAssociatedBinding(zc::heap<CanonicalAssociatedBindingData>(
      CanonicalAssociatedBindingData{zc::mv(name), zc::mv(type)}));
}

CanonicalAssociatedBinding CanonicalAssociatedBinding::clone() const {
  return from(impl->name.clone(), impl->type.clone());
}

zc::StringPtr CanonicalAssociatedBinding::name() const noexcept { return impl->name.text(); }

const CanonicalHeaderTypeSyntax& CanonicalAssociatedBinding::type() const noexcept {
  return impl->type;
}

void CanonicalAssociatedBinding::encode(CanonicalEncoder& encoder) const {
  impl->name.encode(encoder);
  impl->type.encode(encoder);
}

zc::Array<uint8_t> CanonicalAssociatedBinding::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

}  // namespace zomlang::compiler::identity
