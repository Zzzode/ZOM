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

#include "zomlang/compiler/identity/canonical-overload-header.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/canonical-overload-header-data.h"

namespace zomlang::compiler::identity {
namespace detail = canonical_overload_header_detail;
namespace {

bool lessBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] != right[index]) { return left[index] < right[index]; }
  }
  return left.size() < right.size();
}

template <typename Data, typename Value>
zc::Own<Data> makeData(Value&& value) {
  return zc::heap<Data>(Data{zc::fwd<Value>(value)});
}

template <typename Value>
struct EncodedValue final {
  zc::Array<uint8_t> bytes;
  Value value;
};

template <typename Value>
zc::Vector<Value> sortUnique(zc::Vector<Value>&& values) {
  zc::Vector<EncodedValue<Value>> encoded(values.size());
  for (auto& value : values) { encoded.add(EncodedValue<Value>{value.encode(), zc::mv(value)}); }
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
  zc::Vector<Value> result(encoded.size());
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

void appendFlattenedUnion(CanonicalHeaderTypeSyntax&& value,
                          zc::Vector<CanonicalHeaderTypeSyntax>& output) {
  if (value.kind() != CanonicalHeaderTypeSyntaxKind::Union) {
    output.add(zc::mv(value));
    return;
  }
  ZC_IF_SOME(members, value.members()) {
    for (const auto& member : members) { appendFlattenedUnion(member.clone(), output); }
    return;
  }
  ZC_UNREACHABLE
}

}  // namespace

bool isCanonicalCallableResultKind(CanonicalCallableResultKind value) noexcept {
  return value >= CanonicalCallableResultKind::Unit && value <= CanonicalCallableResultKind::Type;
}

CanonicalCallableResult::CanonicalCallableResult(
    zc::Own<detail::CanonicalCallableResultData>&& value) noexcept
    : impl(zc::mv(value)) {}
CanonicalCallableResult::~CanonicalCallableResult() noexcept(false) = default;
CanonicalCallableResult::CanonicalCallableResult(CanonicalCallableResult&&) noexcept = default;
CanonicalCallableResult& CanonicalCallableResult::operator=(CanonicalCallableResult&&) noexcept =
    default;
CanonicalCallableResult CanonicalCallableResult::unit() {
  return CanonicalCallableResult(
      makeData<detail::CanonicalCallableResultData>(detail::CallableResultUnitData{}));
}
CanonicalCallableResult CanonicalCallableResult::constructorSelf() {
  return CanonicalCallableResult(
      makeData<detail::CanonicalCallableResultData>(detail::CallableResultConstructorSelfData{}));
}
CanonicalCallableResult CanonicalCallableResult::type(CanonicalHeaderTypeSyntax&& type) {
  ZC_IF_SOME(kind, type.predefinedKind()) {
    if (kind == PredefinedTypeKind::Unit) { return unit(); }
  }
  return CanonicalCallableResult(
      makeData<detail::CanonicalCallableResultData>(detail::CallableResultTypeData{zc::mv(type)}));
}
CanonicalCallableResult CanonicalCallableResult::clone() const {
  if (impl->value.is<detail::CallableResultUnitData>()) { return unit(); }
  if (impl->value.is<detail::CallableResultConstructorSelfData>()) { return constructorSelf(); }
  return type(impl->value.get<detail::CallableResultTypeData>().type.clone());
}
CanonicalCallableResultKind CanonicalCallableResult::kind() const noexcept {
  if (impl->value.is<detail::CallableResultUnitData>()) {
    return CanonicalCallableResultKind::Unit;
  }
  return impl->value.is<detail::CallableResultConstructorSelfData>()
             ? CanonicalCallableResultKind::ConstructorSelf
             : CanonicalCallableResultKind::Type;
}
zc::Maybe<const CanonicalHeaderTypeSyntax&> CanonicalCallableResult::type() const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<detail::CallableResultTypeData>()) { return value.type; }
  return zc::none;
}
void CanonicalCallableResult::encode(CanonicalEncoder& encoder) const {
  const auto resultKind = kind();
  encoder.encodeUint8(static_cast<uint8_t>(resultKind));
  if (resultKind == CanonicalCallableResultKind::Type) {
    impl->value.get<detail::CallableResultTypeData>().type.encode(encoder);
  }
}
zc::Array<uint8_t> CanonicalCallableResult::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

CanonicalGenericParameter::CanonicalGenericParameter(
    zc::Own<detail::CanonicalGenericParameterData>&& value) noexcept
    : impl(zc::mv(value)) {}
CanonicalGenericParameter::~CanonicalGenericParameter() noexcept(false) = default;
CanonicalGenericParameter::CanonicalGenericParameter(CanonicalGenericParameter&&) noexcept =
    default;
CanonicalGenericParameter& CanonicalGenericParameter::operator=(
    CanonicalGenericParameter&&) noexcept = default;
CanonicalGenericParameter CanonicalGenericParameter::from(
    zc::Maybe<CanonicalHeaderTypeSyntax>&& defaultType) {
  return CanonicalGenericParameter(zc::heap<detail::CanonicalGenericParameterData>(
      detail::CanonicalGenericParameterData{zc::mv(defaultType)}));
}
zc::Maybe<CanonicalGenericParameter> CanonicalGenericParameter::decodeCanonical(
    CanonicalDecoder& decoder) {
  auto presence = decoder.decodeUint8();
  if (presence == zc::none) { return zc::none; }
  zc::Maybe<CanonicalHeaderTypeSyntax> defaultType;
  switch (ZC_ASSERT_NONNULL(presence)) {
    case 0x00:
      break;
    case 0x01: {
      auto value = CanonicalHeaderTypeSyntax::decodeCanonical(decoder);
      if (value == zc::none) { return zc::none; }
      defaultType = zc::mv(ZC_ASSERT_NONNULL(value));
      break;
    }
    default:
      return zc::none;
  }
  return from(zc::mv(defaultType));
}
CanonicalGenericParameter CanonicalGenericParameter::clone() const {
  zc::Maybe<CanonicalHeaderTypeSyntax> defaultType;
  ZC_IF_SOME(value, impl->defaultType) { defaultType = value.clone(); }
  return from(zc::mv(defaultType));
}
zc::Maybe<const CanonicalHeaderTypeSyntax&> CanonicalGenericParameter::defaultType()
    const noexcept {
  ZC_IF_SOME(value, impl->defaultType) { return value; }
  return zc::none;
}
void CanonicalGenericParameter::encode(CanonicalEncoder& encoder) const {
  ZC_IF_SOME(value, impl->defaultType) {
    encoder.encodeSome();
    value.encode(encoder);
  } else {
    encoder.encodeNone();
  }
}
zc::Array<uint8_t> CanonicalGenericParameter::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

CanonicalBoundObligation::CanonicalBoundObligation(
    zc::Own<detail::CanonicalBoundObligationData>&& value) noexcept
    : impl(zc::mv(value)) {}
CanonicalBoundObligation::~CanonicalBoundObligation() noexcept(false) = default;
CanonicalBoundObligation::CanonicalBoundObligation(CanonicalBoundObligation&&) noexcept = default;
CanonicalBoundObligation& CanonicalBoundObligation::operator=(CanonicalBoundObligation&&) noexcept =
    default;
CanonicalBoundObligation CanonicalBoundObligation::from(CanonicalHeaderTypeSyntax&& subject,
                                                        CanonicalHeaderTypeSyntax&& bound) {
  return CanonicalBoundObligation(zc::heap<detail::CanonicalBoundObligationData>(
      detail::CanonicalBoundObligationData{zc::mv(subject), zc::mv(bound)}));
}
zc::Maybe<CanonicalBoundObligation> CanonicalBoundObligation::decodeCanonical(
    CanonicalDecoder& decoder) {
  auto subject = CanonicalHeaderTypeSyntax::decodeCanonical(decoder);
  auto bound = CanonicalHeaderTypeSyntax::decodeCanonical(decoder);
  if (subject == zc::none || bound == zc::none) { return zc::none; }
  return from(zc::mv(ZC_ASSERT_NONNULL(subject)), zc::mv(ZC_ASSERT_NONNULL(bound)));
}
CanonicalBoundObligation CanonicalBoundObligation::clone() const {
  return from(impl->subject.clone(), impl->bound.clone());
}

const CanonicalHeaderTypeSyntax& CanonicalBoundObligation::subject() const noexcept {
  return impl->subject;
}

const CanonicalHeaderTypeSyntax& CanonicalBoundObligation::bound() const noexcept {
  return impl->bound;
}

void CanonicalBoundObligation::encode(CanonicalEncoder& encoder) const {
  impl->subject.encode(encoder);
  impl->bound.encode(encoder);
}

zc::Array<uint8_t> CanonicalBoundObligation::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

CanonicalCallableParameter::CanonicalCallableParameter(
    zc::Own<detail::CanonicalCallableParameterData>&& value) noexcept
    : impl(zc::mv(value)) {}
CanonicalCallableParameter::~CanonicalCallableParameter() noexcept(false) = default;
CanonicalCallableParameter::CanonicalCallableParameter(CanonicalCallableParameter&&) noexcept =
    default;
CanonicalCallableParameter& CanonicalCallableParameter::operator=(
    CanonicalCallableParameter&&) noexcept = default;

CanonicalCallableParameter CanonicalCallableParameter::from(SemanticIdentifier&& label,
                                                            CanonicalHeaderTypeSyntax&& type,
                                                            bool hasDefault) {
  return CanonicalCallableParameter(zc::heap<detail::CanonicalCallableParameterData>(
      detail::CanonicalCallableParameterData{zc::mv(label), zc::mv(type), hasDefault}));
}

CanonicalCallableParameter CanonicalCallableParameter::clone() const {
  return from(impl->label.clone(), impl->type.clone(), impl->hasDefault);
}

zc::StringPtr CanonicalCallableParameter::label() const noexcept { return impl->label.text(); }

const CanonicalHeaderTypeSyntax& CanonicalCallableParameter::type() const noexcept {
  return impl->type;
}

bool CanonicalCallableParameter::hasDefault() const noexcept { return impl->hasDefault; }

void CanonicalCallableParameter::encode(CanonicalEncoder& encoder) const {
  impl->label.encode(encoder);
  impl->type.encode(encoder);
  encoder.encodeBool(impl->hasDefault);
}

zc::Array<uint8_t> CanonicalCallableParameter::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

CanonicalOverloadHeader::CanonicalOverloadHeader(
    zc::Own<detail::CanonicalOverloadHeaderData>&& value) noexcept
    : impl(zc::mv(value)) {}
CanonicalOverloadHeader::~CanonicalOverloadHeader() noexcept(false) = default;
CanonicalOverloadHeader::CanonicalOverloadHeader(CanonicalOverloadHeader&&) noexcept = default;
CanonicalOverloadHeader& CanonicalOverloadHeader::operator=(CanonicalOverloadHeader&&) noexcept =
    default;

zc::Maybe<CanonicalOverloadHeader> CanonicalOverloadHeader::from(
    CallableHeaderKind callableKind, DeclaredDefinitionName&& name,
    zc::Maybe<ReceiverShape>&& receiver, zc::Vector<CanonicalGenericParameter>&& genericParameters,
    zc::Vector<CanonicalBoundObligation>&& obligations,
    zc::Vector<CanonicalCallableParameter>&& parameters, CanonicalCallableResult&& result,
    zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>>&& raises,
    zc::Maybe<ExternalAbi>&& externalAbi) {
  if (!isCanonicalHeaderValue(callableKind)) { return zc::none; }
  ZC_IF_SOME(value, receiver) {
    if (!isCanonicalHeaderValue(value)) { return zc::none; }
  }
  ZC_IF_SOME(value, externalAbi) {
    if (!isCanonicalHeaderValue(value)) { return zc::none; }
  }
  ZC_IF_SOME(values, raises) {
    if (values.size() == 0) { return zc::none; }
    zc::Vector<CanonicalHeaderTypeSyntax> flattened(values.size());
    for (auto& value : values) { appendFlattenedUnion(zc::mv(value), flattened); }
    values = sortUnique(zc::mv(flattened));
    if (values.size() == 0) { return zc::none; }
  }

  const bool constructorResult = result.kind() == CanonicalCallableResultKind::ConstructorSelf;
  if (callableKind == CallableHeaderKind::Function) {
    if (receiver != zc::none || constructorResult) { return zc::none; }
  } else if (callableKind == CallableHeaderKind::Method) {
    if (constructorResult || externalAbi != zc::none) { return zc::none; }
  } else if (receiver != zc::none || externalAbi != zc::none || !constructorResult) {
    return zc::none;
  }

  obligations = sortUnique(zc::mv(obligations));
  return CanonicalOverloadHeader(
      zc::heap<detail::CanonicalOverloadHeaderData>(detail::CanonicalOverloadHeaderData{
          callableKind, zc::mv(name), zc::mv(receiver), zc::mv(genericParameters),
          zc::mv(obligations), zc::mv(parameters), zc::mv(result), zc::mv(raises),
          zc::mv(externalAbi)}));
}

CanonicalOverloadHeader CanonicalOverloadHeader::clone() const {
  zc::Maybe<ReceiverShape> receiver;
  ZC_IF_SOME(value, impl->receiver) { receiver = value; }
  zc::Vector<CanonicalGenericParameter> generics(impl->genericParameters.size());
  for (const auto& value : impl->genericParameters) { generics.add(value.clone()); }
  zc::Vector<CanonicalBoundObligation> obligations(impl->obligations.size());
  for (const auto& value : impl->obligations) { obligations.add(value.clone()); }
  zc::Vector<CanonicalCallableParameter> parameters(impl->parameters.size());
  for (const auto& value : impl->parameters) { parameters.add(value.clone()); }
  zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>> raises;
  ZC_IF_SOME(values, impl->raises) {
    zc::Vector<CanonicalHeaderTypeSyntax> cloned(values.size());
    for (const auto& value : values) { cloned.add(value.clone()); }
    raises = zc::mv(cloned);
  }
  zc::Maybe<ExternalAbi> externalAbi;
  ZC_IF_SOME(value, impl->externalAbi) { externalAbi = value; }
  auto cloned = from(impl->callableKind, impl->name.clone(), zc::mv(receiver), zc::mv(generics),
                     zc::mv(obligations), zc::mv(parameters), impl->result.clone(), zc::mv(raises),
                     zc::mv(externalAbi));
  ZC_IF_SOME(value, cloned) { return zc::mv(value); }
  ZC_UNREACHABLE
}

CallableHeaderKind CanonicalOverloadHeader::callableKind() const noexcept {
  return impl->callableKind;
}

zc::StringPtr CanonicalOverloadHeader::name() const noexcept { return impl->name.text(); }

zc::Maybe<ReceiverShape> CanonicalOverloadHeader::receiver() const noexcept {
  ZC_IF_SOME(value, impl->receiver) { return value; }
  return zc::none;
}

zc::ArrayPtr<const CanonicalGenericParameter> CanonicalOverloadHeader::genericParameters()
    const noexcept {
  return impl->genericParameters.asPtr();
}

zc::ArrayPtr<const CanonicalBoundObligation> CanonicalOverloadHeader::obligations() const noexcept {
  return impl->obligations.asPtr();
}

zc::ArrayPtr<const CanonicalCallableParameter> CanonicalOverloadHeader::parameters()
    const noexcept {
  return impl->parameters.asPtr();
}

const CanonicalCallableResult& CanonicalOverloadHeader::result() const noexcept {
  return impl->result;
}

zc::Maybe<zc::ArrayPtr<const CanonicalHeaderTypeSyntax>> CanonicalOverloadHeader::raises()
    const noexcept {
  ZC_IF_SOME(values, impl->raises) { return values.asPtr(); }
  return zc::none;
}

zc::Maybe<ExternalAbi> CanonicalOverloadHeader::externalAbi() const noexcept {
  ZC_IF_SOME(value, impl->externalAbi) { return value; }
  return zc::none;
}

void CanonicalOverloadHeader::encode(CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(impl->callableKind));
  impl->name.encode(encoder);
  ZC_IF_SOME(value, impl->receiver) {
    encoder.encodeSome();
    encoder.encodeUint8(static_cast<uint8_t>(value));
  } else {
    encoder.encodeNone();
  }
  encodeSequence(encoder, impl->genericParameters.asPtr());
  encodeSequence(encoder, impl->obligations.asPtr());
  encodeSequence(encoder, impl->parameters.asPtr());
  impl->result.encode(encoder);
  ZC_IF_SOME(values, impl->raises) {
    encoder.encodeSome();
    encodeSequence(encoder, values.asPtr());
  } else {
    encoder.encodeNone();
  }
  ZC_IF_SOME(value, impl->externalAbi) {
    encoder.encodeSome();
    encoder.encodeUint8(static_cast<uint8_t>(value));
  } else {
    encoder.encodeNone();
  }
}

zc::Array<uint8_t> CanonicalOverloadHeader::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

}  // namespace zomlang::compiler::identity
