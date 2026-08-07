// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/stable-binding-facts.h"

#include "zc/core/debug.h"
#include "zc/core/map.h"
#include "zomlang/compiler/binder/stable-binding-codec.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::binder {
namespace {

inline constexpr size_t kStableBindingFactEntityCount = 0
#define ZOM_STABLE_BINDING_RECORD(...) +1
#define ZOM_STABLE_BINDING_NESTED_RECORD(...) +1
#define ZOM_STABLE_BINDING_SUM(...) +1
#define ZOM_STABLE_BINDING_RUNTIME_SUM(...) +1
#include "zomlang/compiler/binder/stable-binding-schema.def"
    ;
#undef ZOM_STABLE_BINDING_RUNTIME_SUM
#undef ZOM_STABLE_BINDING_SUM
#undef ZOM_STABLE_BINDING_NESTED_RECORD
#undef ZOM_STABLE_BINDING_RECORD

static_assert(kStableBindingFactEntityCount != 0);

bool sameModule(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

template <typename T>
zc::Maybe<T> cloneMaybe(const zc::Maybe<T>& value) {
  ZC_IF_SOME(retained, value) { return stable_binding_detail::cloneElement(retained); }
  return zc::none;
}

template <typename T>
bool sameMaybe(const zc::Maybe<T>& left, const zc::Maybe<T>& right) {
  if ((left == zc::none) != (right == zc::none)) { return false; }
  ZC_IF_SOME(value, left) {
    ZC_IF_SOME(other, right) { return stable_binding_detail::sameElement(value, other); }
  }
  return true;
}

bool samePosition(identity::CallableParameterPosition left,
                  identity::CallableParameterPosition right) {
  return left.kind() == right.kind() && left.ordinal() == right.ordinal();
}

bool sameDefinitionSite(const StableHeaderSite& site, const IdentitySyntaxSiteKey& authority) {
  return site.value().is<DefinitionAuthoritySite>() &&
         site.value().get<DefinitionAuthoritySite>().site.sameAs(authority);
}

bool validGenericParameters(zc::ArrayPtr<const StableHeaderGenericParameter> values,
                            const identity::DefinitionKey& owner,
                            const IdentitySyntaxSiteKey& authority) {
  for (size_t index = 0; index < values.size(); ++index) {
    const auto& value = values[index];
    const auto& parameterOwner = value.record().owner();
    if (parameterOwner.kind() != identity::StableGenericParameterOwnerKind::Definition ||
        ZC_ASSERT_NONNULL(parameterOwner.definitionKey()) != owner ||
        !sameDefinitionSite(value.site(), authority) || value.ordinal() >= values.size()) {
      return false;
    }
    for (size_t prior = 0; prior < index; ++prior) {
      if (values[prior].ordinal() == value.ordinal()) { return false; }
    }
  }
  return true;
}

bool validImplementationGenericParameters(zc::ArrayPtr<const StableHeaderGenericParameter> values,
                                          const identity::ImplKey& owner,
                                          const ImplSourceOccurrenceKey& occurrence) {
  for (size_t index = 0; index < values.size(); ++index) {
    const auto& value = values[index];
    const auto& parameterOwner = value.record().owner();
    if (parameterOwner.kind() != identity::StableGenericParameterOwnerKind::Implementation ||
        ZC_ASSERT_NONNULL(parameterOwner.implKey()) != owner ||
        !value.site().value().is<ImplementationOccurrenceSite>() ||
        !value.site().value().get<ImplementationOccurrenceSite>().site.sameAs(occurrence) ||
        value.ordinal() >= values.size()) {
      return false;
    }
    for (size_t prior = 0; prior < index; ++prior) {
      if (values[prior].ordinal() == value.ordinal()) { return false; }
    }
  }
  return true;
}

bool validCallableParameters(zc::ArrayPtr<const StableHeaderCallableParameter> values,
                             const identity::DefinitionKey& owner,
                             const IdentitySyntaxSiteKey& authority) {
  size_t ordinaryCount = 0;
  for (const auto& value : values) {
    if (value.position().kind() == identity::CallableParameterPositionKind::Ordinary) {
      ++ordinaryCount;
    }
  }
  bool receiverSeen = false;
  for (size_t index = 0; index < values.size(); ++index) {
    const auto& value = values[index];
    if (value.record().owner() != owner || !sameDefinitionSite(value.site(), authority)) {
      return false;
    }
    if (value.position().kind() == identity::CallableParameterPositionKind::Receiver) {
      if (receiverSeen) { return false; }
      receiverSeen = true;
      continue;
    }
    const auto ordinal = ZC_ASSERT_NONNULL(value.position().ordinal());
    if (ordinal >= ordinaryCount) { return false; }
    for (size_t prior = 0; prior < index; ++prior) {
      if (samePosition(values[prior].position(), value.position())) { return false; }
    }
  }
  return true;
}

bool validScopeRoles(zc::ArrayPtr<const ScopeRole> values) {
  for (const auto value : values) {
    if (!isStableBindingValue(value)) { return false; }
  }
  return true;
}

const identity::ModuleKey& moduleOf(const StableScopeOwnerKey& owner) {
  const auto& value = owner.value();
  if (value.is<StableModuleScope>()) { return value.get<StableModuleScope>().module; }
  if (value.is<StableDefinitionScope>()) {
    return value.get<StableDefinitionScope>().definition.module();
  }
  if (value.is<StableImplementationOccurrenceScope>()) {
    return value.get<StableImplementationOccurrenceScope>().occurrence.module();
  }
  return value.get<StableBodyScope>().owner.module();
}

const identity::ModuleKey& moduleOf(const StableHeaderSite& site) {
  if (site.value().is<DefinitionAuthoritySite>()) {
    return site.value().get<DefinitionAuthoritySite>().site.module();
  }
  return site.value().get<ImplementationOccurrenceSite>().site.site().module();
}

const identity::ModuleKey& moduleOf(const StableNodeSyntaxRoot& root) {
  const auto& value = root.value();
  if (value.is<StableModuleBodySyntaxRoot>()) {
    return value.get<StableModuleBodySyntaxRoot>().module;
  }
  if (value.is<StableDefinitionHeaderSyntaxRoot>()) {
    return value.get<StableDefinitionHeaderSyntaxRoot>().definition.module();
  }
  return value.get<StableImplementationHeaderSyntaxRoot>().occurrence.module();
}

const identity::ModuleKey& moduleOf(const BinderQueryOwner& owner) {
  const auto& value = owner.value();
  if (value.is<BinderModuleQueryOwner>()) { return value.get<BinderModuleQueryOwner>().module; }
  if (value.is<BinderDefinitionHeaderQueryOwner>()) {
    return value.get<BinderDefinitionHeaderQueryOwner>().definition.module();
  }
  if (value.is<BinderImplementationHeaderQueryOwner>()) {
    return value.get<BinderImplementationHeaderQueryOwner>().implementation.module();
  }
  return value.get<BinderBodyQueryOwner>().body.module();
}

bool ownsRoot(const StableNodeSyntaxRoot& root, const StableScopeOwnerKey& scope) {
  const auto& rootValue = root.value();
  const auto& scopeValue = scope.value();
  if (rootValue.is<StableModuleBodySyntaxRoot>()) { return scopeValue.is<StableModuleScope>(); }
  if (rootValue.is<StableDefinitionHeaderSyntaxRoot>()) {
    return scopeValue.is<StableDefinitionScope>() &&
           rootValue.get<StableDefinitionHeaderSyntaxRoot>().definition ==
               scopeValue.get<StableDefinitionScope>().definition;
  }
  return scopeValue.is<StableImplementationOccurrenceScope>() &&
         rootValue.get<StableImplementationHeaderSyntaxRoot>().occurrence ==
             scopeValue.get<StableImplementationOccurrenceScope>().occurrence;
}

bool isOwnedBodyScope(const StableOwnerBodyQueryKey& owner, const StableScopeOwnerKey& scope) {
  if (!scope.value().is<StableBodyScope>()) { return false; }
  return scope.value().get<StableBodyScope>().owner == owner;
}

bool isOwnedBodyParent(const StableOwnerBodyQueryKey& owner, const StableScopeOwnerKey& parent) {
  if (!sameModule(owner.module(), moduleOf(parent))) { return false; }
  if (parent.value().is<StableBodyScope>()) {
    return parent.value().get<StableBodyScope>().owner == owner;
  }
  if (owner.owner().kind() == StableBodyOwnerKind::Module) {
    return parent.value().is<StableModuleScope>();
  }
  if (!parent.value().is<StableDefinitionScope>()) { return false; }
  ZC_IF_SOME(definition, owner.owner().definitionKey()) {
    return parent.value().get<StableDefinitionScope>().definition.definition() == definition;
  }
  ZC_UNREACHABLE
}

bool isOwnedBodyNodeScope(const StableOwnerBodyQueryKey& owner, const StableScopeOwnerKey& scope) {
  return isOwnedBodyScope(owner, scope) ||
         (!scope.value().is<StableBodyScope>() && isOwnedBodyParent(owner, scope));
}

bool isOwnedBodyTarget(const StableOwnerBodyQueryKey& owner, const StableBindingTargetKey& target) {
  if (target.value().is<StableOwnerLocalBindingTarget>()) {
    return target.value().get<StableOwnerLocalBindingTarget>().owner == owner;
  }
  if (target.value().is<StableAnonymousOwnerBindingTarget>()) {
    return target.value().get<StableAnonymousOwnerBindingTarget>().owner == owner;
  }
  return true;
}

template <typename T>
bool inClosedRange(T value, T first, T last) {
  return value >= first && value <= last;
}

}  // namespace

bool isStableBindingValue(DefinitionBodyDisposition value) noexcept {
  return inClosedRange(value, DefinitionBodyDisposition::NoExecutableBody,
                       DefinitionBodyDisposition::ExecutableBody);
}

bool isStableBindingValue(ImplementationSourceForm value) noexcept {
  return inClosedRange(value, ImplementationSourceForm::Ordinary,
                       ImplementationSourceForm::BodylessMarker);
}

bool isStableBindingValue(ScopeRole value) noexcept {
  return inClosedRange(value, ScopeRole::Declaration, ScopeRole::Implementation);
}

bool isStableBindingValue(ScopeKind value) noexcept {
  return inClosedRange(value, ScopeKind::Module, ScopeKind::UnsafeBlock);
}

bool isStableBindingValue(ControlTransferKind value) noexcept {
  return inClosedRange(value, ControlTransferKind::Break, ControlTransferKind::Continue);
}
bool isStableBindingValue(StableExplicitCaptureMode value) noexcept {
  return inClosedRange(value, StableExplicitCaptureMode::ByValue, StableExplicitCaptureMode::This);
}

#define ZOM_DEFINE_STABLE_ROUTED_KEY(Name, OwnerType, OwnerName, KeyType, KeyName)            \
  Name::Name(OwnerType&& OwnerName, KeyType&& KeyName) noexcept                               \
      : OwnerName##Field(zc::mv(OwnerName)), KeyName##Field(zc::mv(KeyName)) {}               \
  Name Name::clone() const { return Name(OwnerName##Field.clone(), KeyName##Field.clone()); } \
  const OwnerType& Name::OwnerName() const noexcept { return OwnerName##Field; }              \
  const KeyType& Name::KeyName() const noexcept { return KeyName##Field; }                    \
  bool Name::operator==(const Name& other) const {                                            \
    return stable_binding_detail::sameElement(OwnerName##Field, other.OwnerName##Field) &&    \
           stable_binding_detail::sameElement(KeyName##Field, other.KeyName##Field);          \
  }

ZOM_DEFINE_STABLE_ROUTED_KEY(StableDefinitionQueryKey, identity::ModuleKey, module,
                             identity::DefinitionKey, definition)
ZOM_DEFINE_STABLE_ROUTED_KEY(StableImplementationQueryKey, identity::ModuleKey, module,
                             identity::ImplKey, implementation)
ZOM_DEFINE_STABLE_ROUTED_KEY(StableImplementationOccurrenceQueryKey, identity::ModuleKey, module,
                             ImplSourceOccurrenceKey, occurrence)
ZOM_DEFINE_STABLE_ROUTED_KEY(StableGenericParameterQueryKey, identity::ModuleKey, module,
                             identity::GenericParameterKey, parameter)
ZOM_DEFINE_STABLE_ROUTED_KEY(StableCallableParameterQueryKey, identity::ModuleKey, module,
                             identity::CallableParameterKey, parameter)
ZOM_DEFINE_STABLE_ROUTED_KEY(StableSemanticImportQueryKey, identity::ModuleKey, requester,
                             identity::SemanticImportBindingKey, binding)
ZOM_DEFINE_STABLE_ROUTED_KEY(StableOwnerBodyQueryKey, identity::ModuleKey, module,
                             StableBodyOwnerKey, owner)

#undef ZOM_DEFINE_STABLE_ROUTED_KEY

StableDefinitionQueryKey StableDefinitionQueryKey::from(identity::ModuleKey&& module,
                                                        identity::DefinitionKey&& definition) {
  return StableDefinitionQueryKey(zc::mv(module), zc::mv(definition));
}

StableImplementationQueryKey StableImplementationQueryKey::from(
    identity::ModuleKey&& module, identity::ImplKey&& implementation) {
  return StableImplementationQueryKey(zc::mv(module), zc::mv(implementation));
}

zc::Maybe<StableImplementationOccurrenceQueryKey> StableImplementationOccurrenceQueryKey::from(
    identity::ModuleKey&& module, ImplSourceOccurrenceKey&& occurrence) {
  if (!sameModule(module, occurrence.site().module())) { return zc::none; }
  StableImplementationOccurrenceQueryKey result(zc::mv(module), zc::mv(occurrence));
  if (result.encodeCanonical().size() > 65536) { return zc::none; }
  return zc::mv(result);
}

StableGenericParameterQueryKey StableGenericParameterQueryKey::from(
    identity::ModuleKey&& module, identity::GenericParameterKey&& parameter) {
  return StableGenericParameterQueryKey(zc::mv(module), zc::mv(parameter));
}

StableCallableParameterQueryKey StableCallableParameterQueryKey::from(
    identity::ModuleKey&& module, identity::CallableParameterKey&& parameter) {
  return StableCallableParameterQueryKey(zc::mv(module), zc::mv(parameter));
}

zc::Maybe<StableSemanticImportQueryKey> StableSemanticImportQueryKey::from(
    identity::ModuleKey&& requester, identity::SemanticImportBindingKey&& binding) {
  if (!sameModule(requester, binding.requester())) { return zc::none; }
  StableSemanticImportQueryKey result(zc::mv(requester), zc::mv(binding));
  if (result.encodeCanonical().size() > 65536) { return zc::none; }
  return zc::mv(result);
}

zc::Maybe<StableOwnerBodyQueryKey> StableOwnerBodyQueryKey::from(identity::ModuleKey&& module,
                                                                 StableBodyOwnerKey&& owner) {
  ZC_IF_SOME(ownerModule, owner.moduleKey()) {
    if (!sameModule(module, ownerModule)) { return zc::none; }
  }
  StableOwnerBodyQueryKey result(zc::mv(module), zc::mv(owner));
  if (result.encodeCanonical().size() > 65536) { return zc::none; }
  return zc::mv(result);
}

StableExportedBindingQueryKey::StableExportedBindingQueryKey(identity::ModuleKey&& module,
                                                             BindingNameKey&& name) noexcept
    : moduleField(zc::mv(module)), nameField(zc::mv(name)) {}

StableExportedBindingQueryKey StableExportedBindingQueryKey::from(identity::ModuleKey&& module,
                                                                  BindingNameKey&& name) {
  return StableExportedBindingQueryKey(zc::mv(module), zc::mv(name));
}

StableExportedBindingQueryKey StableExportedBindingQueryKey::clone() const {
  return from(moduleField.clone(), nameField.clone());
}
const identity::ModuleKey& StableExportedBindingQueryKey::module() const noexcept {
  return moduleField;
}
const BindingNameKey& StableExportedBindingQueryKey::name() const noexcept { return nameField; }
bool StableExportedBindingQueryKey::operator==(const StableExportedBindingQueryKey& other) const {
  return sameModule(moduleField, other.moduleField) &&
         stable_binding_detail::sameElement(nameField, other.nameField);
}

StableHeaderSite::StableHeaderSite(StableHeaderSiteValue&& value) noexcept
    : valueField(zc::mv(value)) {}

StableHeaderSite StableHeaderSite::definition(IdentitySyntaxSiteKey&& site) {
  return StableHeaderSite(StableHeaderSiteValue(DefinitionAuthoritySite{zc::mv(site)}));
}

StableHeaderSite StableHeaderSite::implementation(ImplSourceOccurrenceKey&& site) {
  return StableHeaderSite(StableHeaderSiteValue(ImplementationOccurrenceSite{zc::mv(site)}));
}

StableHeaderSite StableHeaderSite::clone() const {
  if (valueField.is<DefinitionAuthoritySite>()) {
    return definition(valueField.get<DefinitionAuthoritySite>().site.clone());
  }
  return implementation(valueField.get<ImplementationOccurrenceSite>().site.clone());
}

const StableHeaderSiteValue& StableHeaderSite::value() const noexcept { return valueField; }

bool StableHeaderSite::operator==(const StableHeaderSite& other) const {
  if (valueField.is<DefinitionAuthoritySite>()) {
    return other.valueField.is<DefinitionAuthoritySite>() &&
           valueField.get<DefinitionAuthoritySite>().site.sameAs(
               other.valueField.get<DefinitionAuthoritySite>().site);
  }
  return other.valueField.is<ImplementationOccurrenceSite>() &&
         valueField.get<ImplementationOccurrenceSite>().site.sameAs(
             other.valueField.get<ImplementationOccurrenceSite>().site);
}

StableScopeOwnerKey::StableScopeOwnerKey(StableScopeOwnerValue&& value) noexcept
    : valueField(zc::mv(value)) {}
StableScopeOwnerKey StableScopeOwnerKey::module(identity::ModuleKey&& module) {
  return StableScopeOwnerKey(StableScopeOwnerValue(StableModuleScope{zc::mv(module)}));
}
zc::Maybe<StableScopeOwnerKey> StableScopeOwnerKey::definition(
    StableDefinitionQueryKey&& definition, ScopeRole role) {
  if (!isStableBindingValue(role)) { return zc::none; }
  return StableScopeOwnerKey(
      StableScopeOwnerValue(StableDefinitionScope{zc::mv(definition), role}));
}
zc::Maybe<StableScopeOwnerKey> StableScopeOwnerKey::implementationOccurrence(
    StableImplementationOccurrenceQueryKey&& occurrence, ScopeRole role) {
  if (!isStableBindingValue(role)) { return zc::none; }
  return StableScopeOwnerKey(
      StableScopeOwnerValue(StableImplementationOccurrenceScope{zc::mv(occurrence), role}));
}
StableScopeOwnerKey StableScopeOwnerKey::body(StableOwnerBodyQueryKey&& owner,
                                              LocalSyntaxPath&& path) {
  return StableScopeOwnerKey(StableScopeOwnerValue(StableBodyScope{zc::mv(owner), zc::mv(path)}));
}
StableScopeOwnerKey StableScopeOwnerKey::clone() const {
  if (valueField.is<StableModuleScope>()) {
    return module(valueField.get<StableModuleScope>().module.clone());
  }
  if (valueField.is<StableDefinitionScope>()) {
    const auto& value = valueField.get<StableDefinitionScope>();
    return ZC_ASSERT_NONNULL(definition(value.definition.clone(), value.role));
  }
  if (valueField.is<StableImplementationOccurrenceScope>()) {
    const auto& value = valueField.get<StableImplementationOccurrenceScope>();
    return ZC_ASSERT_NONNULL(implementationOccurrence(value.occurrence.clone(), value.role));
  }
  const auto& value = valueField.get<StableBodyScope>();
  return body(value.owner.clone(), value.path.clone());
}
const StableScopeOwnerValue& StableScopeOwnerKey::value() const noexcept { return valueField; }
bool StableScopeOwnerKey::operator==(const StableScopeOwnerKey& other) const {
  if (valueField.is<StableModuleScope>()) {
    return other.valueField.is<StableModuleScope>() &&
           sameModule(valueField.get<StableModuleScope>().module,
                      other.valueField.get<StableModuleScope>().module);
  }
#define ZOM_SAME_SCOPE(Variant, First, Second)                                         \
  if (valueField.is<Variant>()) {                                                      \
    return other.valueField.is<Variant>() &&                                           \
           valueField.get<Variant>().First == other.valueField.get<Variant>().First && \
           valueField.get<Variant>().Second == other.valueField.get<Variant>().Second; \
  }
  ZOM_SAME_SCOPE(StableDefinitionScope, definition, role)
  ZOM_SAME_SCOPE(StableImplementationOccurrenceScope, occurrence, role)
  ZOM_SAME_SCOPE(StableBodyScope, owner, path)
#undef ZOM_SAME_SCOPE
  ZC_UNREACHABLE
}

StableScopeNameBucketQueryKey::StableScopeNameBucketQueryKey(StableScopeOwnerKey&& scope,
                                                             BindingNameKey&& name) noexcept
    : scopeField(zc::mv(scope)), nameField(zc::mv(name)) {}

zc::Maybe<StableScopeNameBucketQueryKey> StableScopeNameBucketQueryKey::from(
    StableScopeOwnerKey&& scope, BindingNameKey&& name) {
  if (scope.value().is<StableBodyScope>()) { return zc::none; }
  return StableScopeNameBucketQueryKey(zc::mv(scope), zc::mv(name));
}

StableScopeNameBucketQueryKey StableScopeNameBucketQueryKey::clone() const {
  return ZC_ASSERT_NONNULL(from(scopeField.clone(), nameField.clone()));
}

const StableScopeOwnerKey& StableScopeNameBucketQueryKey::scope() const noexcept {
  return scopeField;
}
const BindingNameKey& StableScopeNameBucketQueryKey::name() const noexcept { return nameField; }
bool StableScopeNameBucketQueryKey::operator==(const StableScopeNameBucketQueryKey& other) const {
  return scopeField == other.scopeField && nameField.nameSpace() == other.nameField.nameSpace() &&
         nameField.name() == other.nameField.name();
}

StableNodeSyntaxRoot::StableNodeSyntaxRoot(StableNodeSyntaxRootValue&& value) noexcept
    : valueField(zc::mv(value)) {}
StableNodeSyntaxRoot StableNodeSyntaxRoot::moduleBody(identity::ModuleKey&& module) {
  return StableNodeSyntaxRoot(
      StableNodeSyntaxRootValue(StableModuleBodySyntaxRoot{zc::mv(module)}));
}
StableNodeSyntaxRoot StableNodeSyntaxRoot::definitionHeader(StableDefinitionQueryKey&& definition) {
  return StableNodeSyntaxRoot(
      StableNodeSyntaxRootValue(StableDefinitionHeaderSyntaxRoot{zc::mv(definition)}));
}
StableNodeSyntaxRoot StableNodeSyntaxRoot::implementationHeader(
    StableImplementationOccurrenceQueryKey&& occurrence) {
  return StableNodeSyntaxRoot(
      StableNodeSyntaxRootValue(StableImplementationHeaderSyntaxRoot{zc::mv(occurrence)}));
}
StableNodeSyntaxRoot StableNodeSyntaxRoot::clone() const {
#define ZOM_CLONE_SYNTAX_ROOT(Variant, Factory, Field) \
  if (valueField.is<Variant>()) { return Factory(valueField.get<Variant>().Field.clone()); }
  ZOM_CLONE_SYNTAX_ROOT(StableModuleBodySyntaxRoot, moduleBody, module)
  ZOM_CLONE_SYNTAX_ROOT(StableDefinitionHeaderSyntaxRoot, definitionHeader, definition)
#undef ZOM_CLONE_SYNTAX_ROOT
  return implementationHeader(
      valueField.get<StableImplementationHeaderSyntaxRoot>().occurrence.clone());
}
const StableNodeSyntaxRootValue& StableNodeSyntaxRoot::value() const noexcept { return valueField; }
bool StableNodeSyntaxRoot::operator==(const StableNodeSyntaxRoot& other) const {
#define ZOM_SAME_SYNTAX_ROOT(Variant, Field)                                          \
  if (valueField.is<Variant>()) {                                                     \
    return other.valueField.is<Variant>() &&                                          \
           stable_binding_detail::sameElement(valueField.get<Variant>().Field,        \
                                              other.valueField.get<Variant>().Field); \
  }
  ZOM_SAME_SYNTAX_ROOT(StableModuleBodySyntaxRoot, module)
  ZOM_SAME_SYNTAX_ROOT(StableDefinitionHeaderSyntaxRoot, definition)
  ZOM_SAME_SYNTAX_ROOT(StableImplementationHeaderSyntaxRoot, occurrence)
#undef ZOM_SAME_SYNTAX_ROOT
  ZC_UNREACHABLE
}

struct StableScopeFact::Impl final {
  StableScopeOwnerKey owner;
  zc::Maybe<StableScopeOwnerKey> parent;
  ScopeKind kind;
};

StableScopeFact::~StableScopeFact() noexcept(false) = default;
StableScopeFact::StableScopeFact(StableScopeFact&&) noexcept = default;
StableScopeFact& StableScopeFact::operator=(StableScopeFact&&) noexcept = default;
StableScopeFact::StableScopeFact(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}

zc::Maybe<StableScopeFact> StableScopeFact::from(StableScopeOwnerKey&& owner,
                                                 zc::Maybe<StableScopeOwnerKey>&& parent,
                                                 ScopeKind kind) {
  const bool isModuleOwner = owner.value().is<StableModuleScope>();
  if (!isStableBindingValue(kind) || isModuleOwner != (kind == ScopeKind::Module) ||
      isModuleOwner != (parent == zc::none)) {
    return zc::none;
  }
  ZC_IF_SOME(parentOwner, parent) {
    if (!sameModule(moduleOf(owner), moduleOf(parentOwner)) || owner == parentOwner) {
      return zc::none;
    }
  }
  return StableScopeFact(zc::heap<Impl>(Impl{zc::mv(owner), zc::mv(parent), kind}));
}

StableScopeFact StableScopeFact::clone() const {
  return StableScopeFact(
      zc::heap<Impl>(Impl{impl->owner.clone(), cloneMaybe(impl->parent), impl->kind}));
}
const StableScopeOwnerKey& StableScopeFact::owner() const noexcept { return impl->owner; }
const zc::Maybe<StableScopeOwnerKey>& StableScopeFact::parent() const noexcept {
  return impl->parent;
}
ScopeKind StableScopeFact::kind() const noexcept { return impl->kind; }
bool StableScopeFact::operator==(const StableScopeFact& other) const {
  return impl->owner == other.impl->owner && sameMaybe(impl->parent, other.impl->parent) &&
         impl->kind == other.impl->kind;
}

struct StableNodeScopeFact::Impl final {
  StableNodeSyntaxRoot root;
  LocalSyntaxPath nodePath;
  StableScopeOwnerKey scope;
};

StableNodeScopeFact::~StableNodeScopeFact() noexcept(false) = default;
StableNodeScopeFact::StableNodeScopeFact(StableNodeScopeFact&&) noexcept = default;
StableNodeScopeFact& StableNodeScopeFact::operator=(StableNodeScopeFact&&) noexcept = default;
StableNodeScopeFact::StableNodeScopeFact(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}

zc::Maybe<StableNodeScopeFact> StableNodeScopeFact::from(StableNodeSyntaxRoot&& root,
                                                         LocalSyntaxPath&& nodePath,
                                                         StableScopeOwnerKey&& scope) {
  if (!sameModule(moduleOf(root), moduleOf(scope)) || !ownsRoot(root, scope)) { return zc::none; }
  return StableNodeScopeFact(zc::heap<Impl>(Impl{zc::mv(root), zc::mv(nodePath), zc::mv(scope)}));
}

StableNodeScopeFact StableNodeScopeFact::clone() const {
  return StableNodeScopeFact(
      zc::heap<Impl>(Impl{impl->root.clone(), impl->nodePath.clone(), impl->scope.clone()}));
}
const StableNodeSyntaxRoot& StableNodeScopeFact::root() const noexcept { return impl->root; }
const LocalSyntaxPath& StableNodeScopeFact::nodePath() const noexcept { return impl->nodePath; }
const StableScopeOwnerKey& StableNodeScopeFact::scope() const noexcept { return impl->scope; }
bool StableNodeScopeFact::operator==(const StableNodeScopeFact& other) const {
  return impl->root == other.impl->root && impl->nodePath == other.impl->nodePath &&
         impl->scope == other.impl->scope;
}

struct StableBodyScopeFact::Impl final {
  StableOwnerBodyQueryKey owner;
  StableScopeOwnerKey scope;
  StableScopeOwnerKey parent;
  ScopeKind kind;
};

StableBodyScopeFact::~StableBodyScopeFact() noexcept(false) = default;
StableBodyScopeFact::StableBodyScopeFact(StableBodyScopeFact&&) noexcept = default;
StableBodyScopeFact& StableBodyScopeFact::operator=(StableBodyScopeFact&&) noexcept = default;
StableBodyScopeFact::StableBodyScopeFact(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}

zc::Maybe<StableBodyScopeFact> StableBodyScopeFact::from(StableOwnerBodyQueryKey&& owner,
                                                         StableScopeOwnerKey&& scope,
                                                         StableScopeOwnerKey&& parent,
                                                         ScopeKind kind) {
  if (!isStableBindingValue(kind) || kind == ScopeKind::Module || !isOwnedBodyScope(owner, scope) ||
      !isOwnedBodyParent(owner, parent) || scope == parent) {
    return zc::none;
  }
  return StableBodyScopeFact(
      zc::heap<Impl>(Impl{zc::mv(owner), zc::mv(scope), zc::mv(parent), kind}));
}

StableBodyScopeFact StableBodyScopeFact::clone() const {
  return StableBodyScopeFact(zc::heap<Impl>(
      Impl{impl->owner.clone(), impl->scope.clone(), impl->parent.clone(), impl->kind}));
}
const StableOwnerBodyQueryKey& StableBodyScopeFact::owner() const noexcept { return impl->owner; }
const StableScopeOwnerKey& StableBodyScopeFact::scope() const noexcept { return impl->scope; }
const StableScopeOwnerKey& StableBodyScopeFact::parent() const noexcept { return impl->parent; }
ScopeKind StableBodyScopeFact::kind() const noexcept { return impl->kind; }
bool StableBodyScopeFact::operator==(const StableBodyScopeFact& other) const {
  return impl->owner == other.impl->owner && impl->scope == other.impl->scope &&
         impl->parent == other.impl->parent && impl->kind == other.impl->kind;
}

struct StableBodyNodeScopeFact::Impl final {
  StableOwnerBodyQueryKey owner;
  LocalSyntaxPath nodePath;
  StableScopeOwnerKey scope;
};

StableBodyNodeScopeFact::~StableBodyNodeScopeFact() noexcept(false) = default;
StableBodyNodeScopeFact::StableBodyNodeScopeFact(StableBodyNodeScopeFact&&) noexcept = default;
StableBodyNodeScopeFact& StableBodyNodeScopeFact::operator=(StableBodyNodeScopeFact&&) noexcept =
    default;
StableBodyNodeScopeFact::StableBodyNodeScopeFact(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}

zc::Maybe<StableBodyNodeScopeFact> StableBodyNodeScopeFact::from(StableOwnerBodyQueryKey&& owner,
                                                                 LocalSyntaxPath&& nodePath,
                                                                 StableScopeOwnerKey&& scope) {
  if (!isOwnedBodyNodeScope(owner, scope)) { return zc::none; }
  return StableBodyNodeScopeFact(
      zc::heap<Impl>(Impl{zc::mv(owner), zc::mv(nodePath), zc::mv(scope)}));
}

StableBodyNodeScopeFact StableBodyNodeScopeFact::clone() const {
  return StableBodyNodeScopeFact(
      zc::heap<Impl>(Impl{impl->owner.clone(), impl->nodePath.clone(), impl->scope.clone()}));
}
const StableOwnerBodyQueryKey& StableBodyNodeScopeFact::owner() const noexcept {
  return impl->owner;
}
const LocalSyntaxPath& StableBodyNodeScopeFact::nodePath() const noexcept { return impl->nodePath; }
const StableScopeOwnerKey& StableBodyNodeScopeFact::scope() const noexcept { return impl->scope; }
bool StableBodyNodeScopeFact::operator==(const StableBodyNodeScopeFact& other) const {
  return impl->owner == other.impl->owner && impl->nodePath == other.impl->nodePath &&
         impl->scope == other.impl->scope;
}

struct StableOwnerLocalBindingFact::Impl final {
  StableOwnerBodyQueryKey owner;
  OwnerLocalBindingKey key;
  OwnerLocalBindingKind kind;
  identity::DeclaredDefinitionName name;
  Namespace nameSpace;
  StableScopeOwnerKey declaringScope;
  DefinitionActivation activation;
};

StableOwnerLocalBindingFact::~StableOwnerLocalBindingFact() noexcept(false) = default;
StableOwnerLocalBindingFact::StableOwnerLocalBindingFact(StableOwnerLocalBindingFact&&) noexcept =
    default;
StableOwnerLocalBindingFact& StableOwnerLocalBindingFact::operator=(
    StableOwnerLocalBindingFact&&) noexcept = default;
StableOwnerLocalBindingFact::StableOwnerLocalBindingFact(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}

zc::Maybe<StableOwnerLocalBindingFact> StableOwnerLocalBindingFact::from(
    StableOwnerBodyQueryKey&& owner, OwnerLocalBindingKey&& key, OwnerLocalBindingKind kind,
    identity::DeclaredDefinitionName&& name, Namespace nameSpace,
    StableScopeOwnerKey&& declaringScope, DefinitionActivation activation) {
  if (owner.owner() != key.owner() || kind != key.kind() || name != key.name() ||
      static_cast<uint8_t>(nameSpace) != static_cast<uint8_t>(key.nameSpace()) ||
      !isOwnedBodyNodeScope(owner, declaringScope) ||
      !inClosedRange(activation, DefinitionActivation::GenericList,
                     DefinitionActivation::LoopPattern)) {
    return zc::none;
  }
  return StableOwnerLocalBindingFact(
      zc::heap<Impl>(Impl{zc::mv(owner), zc::mv(key), kind, zc::mv(name), nameSpace,
                          zc::mv(declaringScope), activation}));
}

StableOwnerLocalBindingFact StableOwnerLocalBindingFact::clone() const {
  return ZC_ASSERT_NONNULL(from(impl->owner.clone(), impl->key.clone(), impl->kind,
                                impl->name.clone(), impl->nameSpace, impl->declaringScope.clone(),
                                impl->activation));
}
const StableOwnerBodyQueryKey& StableOwnerLocalBindingFact::owner() const noexcept {
  return impl->owner;
}
const OwnerLocalBindingKey& StableOwnerLocalBindingFact::key() const noexcept { return impl->key; }
OwnerLocalBindingKind StableOwnerLocalBindingFact::kind() const noexcept { return impl->kind; }
const identity::DeclaredDefinitionName& StableOwnerLocalBindingFact::name() const noexcept {
  return impl->name;
}
Namespace StableOwnerLocalBindingFact::nameSpace() const noexcept { return impl->nameSpace; }
const StableScopeOwnerKey& StableOwnerLocalBindingFact::declaringScope() const noexcept {
  return impl->declaringScope;
}
DefinitionActivation StableOwnerLocalBindingFact::activation() const noexcept {
  return impl->activation;
}
bool StableOwnerLocalBindingFact::operator==(const StableOwnerLocalBindingFact& other) const {
  return impl->owner == other.impl->owner && impl->key == other.impl->key &&
         impl->kind == other.impl->kind && impl->name == other.impl->name &&
         impl->nameSpace == other.impl->nameSpace &&
         impl->declaringScope == other.impl->declaringScope &&
         impl->activation == other.impl->activation;
}

struct StableResolutionFact::Impl final {
  StableOwnerBodyQueryKey owner;
  LocalSyntaxPath usePath;
  Namespace nameSpace;
  StableBindingTargetKey binding;
  StableBindingTargetKey canonicalTarget;
  BindingOrigin origin;
};

StableResolutionFact::~StableResolutionFact() noexcept(false) = default;
StableResolutionFact::StableResolutionFact(StableResolutionFact&&) noexcept = default;
StableResolutionFact& StableResolutionFact::operator=(StableResolutionFact&&) noexcept = default;
StableResolutionFact::StableResolutionFact(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}

zc::Maybe<StableResolutionFact> StableResolutionFact::from(StableOwnerBodyQueryKey&& owner,
                                                           LocalSyntaxPath&& usePath,
                                                           Namespace nameSpace,
                                                           StableBindingTargetKey&& binding,
                                                           StableBindingTargetKey&& canonicalTarget,
                                                           BindingOrigin origin) {
  if (!inClosedRange(nameSpace, Namespace::Value, Namespace::Attribute) ||
      !inClosedRange(origin, BindingOrigin::LocalDeclaration, BindingOrigin::Prelude) ||
      !isOwnedBodyTarget(owner, binding) || !isOwnedBodyTarget(owner, canonicalTarget)) {
    return zc::none;
  }
  return StableResolutionFact(
      zc::heap<Impl>(Impl{zc::mv(owner), zc::mv(usePath), nameSpace, zc::mv(binding),
                          zc::mv(canonicalTarget), origin}));
}

StableResolutionFact StableResolutionFact::clone() const {
  return ZC_ASSERT_NONNULL(from(impl->owner.clone(), impl->usePath.clone(), impl->nameSpace,
                                impl->binding.clone(), impl->canonicalTarget.clone(),
                                impl->origin));
}
const StableOwnerBodyQueryKey& StableResolutionFact::owner() const noexcept { return impl->owner; }
const LocalSyntaxPath& StableResolutionFact::usePath() const noexcept { return impl->usePath; }
Namespace StableResolutionFact::nameSpace() const noexcept { return impl->nameSpace; }
const StableBindingTargetKey& StableResolutionFact::binding() const noexcept {
  return impl->binding;
}
const StableBindingTargetKey& StableResolutionFact::canonicalTarget() const noexcept {
  return impl->canonicalTarget;
}
BindingOrigin StableResolutionFact::origin() const noexcept { return impl->origin; }
bool StableResolutionFact::operator==(const StableResolutionFact& other) const {
  return impl->owner == other.impl->owner && impl->usePath == other.impl->usePath &&
         impl->nameSpace == other.impl->nameSpace && impl->binding == other.impl->binding &&
         impl->canonicalTarget == other.impl->canonicalTarget && impl->origin == other.impl->origin;
}

struct StableDeferredMemberFact::Impl final {
  StableOwnerBodyQueryKey owner;
  LocalSyntaxPath usePath;
  LocalSyntaxPath basePath;
  MemberAccessKind accessKind;
  identity::DeclaredDefinitionName member;
  CanonicalNonEmptySequence<Namespace> expectedNamespaces;
  CanonicalSequence<LocalSyntaxPath> genericArgumentPaths;
};

StableDeferredMemberFact::~StableDeferredMemberFact() noexcept(false) = default;
StableDeferredMemberFact::StableDeferredMemberFact(StableDeferredMemberFact&&) noexcept = default;
StableDeferredMemberFact& StableDeferredMemberFact::operator=(StableDeferredMemberFact&&) noexcept =
    default;
StableDeferredMemberFact::StableDeferredMemberFact(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}

zc::Maybe<StableDeferredMemberFact> StableDeferredMemberFact::from(
    StableOwnerBodyQueryKey&& owner, LocalSyntaxPath&& usePath, LocalSyntaxPath&& basePath,
    MemberAccessKind accessKind, identity::DeclaredDefinitionName&& member,
    CanonicalNonEmptySequence<Namespace>&& expectedNamespaces,
    CanonicalSequence<LocalSyntaxPath>&& genericArgumentPaths) {
  if (!inClosedRange(accessKind, MemberAccessKind::Dot, MemberAccessKind::Qualified) ||
      usePath == basePath) {
    return zc::none;
  }
  for (const auto& path : genericArgumentPaths.values()) {
    if (path == usePath || path == basePath) { return zc::none; }
  }
  return StableDeferredMemberFact(zc::heap<Impl>(
      Impl{zc::mv(owner), zc::mv(usePath), zc::mv(basePath), accessKind, zc::mv(member),
           zc::mv(expectedNamespaces), zc::mv(genericArgumentPaths)}));
}

StableDeferredMemberFact StableDeferredMemberFact::clone() const {
  return ZC_ASSERT_NONNULL(from(
      impl->owner.clone(), impl->usePath.clone(), impl->basePath.clone(), impl->accessKind,
      impl->member.clone(), impl->expectedNamespaces.clone(), impl->genericArgumentPaths.clone()));
}
const StableOwnerBodyQueryKey& StableDeferredMemberFact::owner() const noexcept {
  return impl->owner;
}
const LocalSyntaxPath& StableDeferredMemberFact::usePath() const noexcept { return impl->usePath; }
const LocalSyntaxPath& StableDeferredMemberFact::basePath() const noexcept {
  return impl->basePath;
}
MemberAccessKind StableDeferredMemberFact::accessKind() const noexcept { return impl->accessKind; }
const identity::DeclaredDefinitionName& StableDeferredMemberFact::member() const noexcept {
  return impl->member;
}
const CanonicalNonEmptySequence<Namespace>& StableDeferredMemberFact::expectedNamespaces()
    const noexcept {
  return impl->expectedNamespaces;
}
const CanonicalSequence<LocalSyntaxPath>& StableDeferredMemberFact::genericArgumentPaths()
    const noexcept {
  return impl->genericArgumentPaths;
}
bool StableDeferredMemberFact::operator==(const StableDeferredMemberFact& other) const {
  return impl->owner == other.impl->owner && impl->usePath == other.impl->usePath &&
         impl->basePath == other.impl->basePath && impl->accessKind == other.impl->accessKind &&
         impl->member == other.impl->member &&
         impl->expectedNamespaces == other.impl->expectedNamespaces &&
         impl->genericArgumentPaths == other.impl->genericArgumentPaths;
}

struct StableSelfOwner::Impl final {
  StableSelfOwnerValue value;
};

StableSelfOwner::~StableSelfOwner() noexcept(false) = default;
StableSelfOwner::StableSelfOwner(StableSelfOwner&&) noexcept = default;
StableSelfOwner& StableSelfOwner::operator=(StableSelfOwner&&) noexcept = default;
StableSelfOwner::StableSelfOwner(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}

StableSelfOwner StableSelfOwner::nominal(StableDefinitionQueryKey&& definition) {
  return StableSelfOwner(
      zc::heap<Impl>(Impl{StableSelfOwnerValue(StableNominalSelfOwner{zc::mv(definition)})}));
}
StableSelfOwner StableSelfOwner::interface(StableDefinitionQueryKey&& definition) {
  return StableSelfOwner(
      zc::heap<Impl>(Impl{StableSelfOwnerValue(StableInterfaceSelfOwner{zc::mv(definition)})}));
}
StableSelfOwner StableSelfOwner::implementationOccurrence(
    StableImplementationOccurrenceQueryKey&& occurrence) {
  return StableSelfOwner(zc::heap<Impl>(
      Impl{StableSelfOwnerValue(StableImplementationOccurrenceSelfOwner{zc::mv(occurrence)})}));
}
StableSelfOwner StableSelfOwner::clone() const {
  if (impl->value.is<StableNominalSelfOwner>()) {
    return nominal(impl->value.get<StableNominalSelfOwner>().definition.clone());
  }
  if (impl->value.is<StableInterfaceSelfOwner>()) {
    return interface(impl->value.get<StableInterfaceSelfOwner>().definition.clone());
  }
  return implementationOccurrence(
      impl->value.get<StableImplementationOccurrenceSelfOwner>().occurrence.clone());
}
const StableSelfOwnerValue& StableSelfOwner::value() const noexcept { return impl->value; }
bool StableSelfOwner::operator==(const StableSelfOwner& other) const {
  if (impl->value.is<StableNominalSelfOwner>()) {
    return other.impl->value.is<StableNominalSelfOwner>() &&
           impl->value.get<StableNominalSelfOwner>().definition ==
               other.impl->value.get<StableNominalSelfOwner>().definition;
  }
  if (impl->value.is<StableInterfaceSelfOwner>()) {
    return other.impl->value.is<StableInterfaceSelfOwner>() &&
           impl->value.get<StableInterfaceSelfOwner>().definition ==
               other.impl->value.get<StableInterfaceSelfOwner>().definition;
  }
  return other.impl->value.is<StableImplementationOccurrenceSelfOwner>() &&
         impl->value.get<StableImplementationOccurrenceSelfOwner>().occurrence ==
             other.impl->value.get<StableImplementationOccurrenceSelfOwner>().occurrence;
}

namespace {

const identity::ModuleKey& moduleOf(const StableSelfOwner& owner) {
  if (owner.value().is<StableNominalSelfOwner>()) {
    return owner.value().get<StableNominalSelfOwner>().definition.module();
  }
  if (owner.value().is<StableInterfaceSelfOwner>()) {
    return owner.value().get<StableInterfaceSelfOwner>().definition.module();
  }
  return owner.value().get<StableImplementationOccurrenceSelfOwner>().occurrence.module();
}

}  // namespace

struct StableSelfTypeFact::Impl final {
  StableOwnerBodyQueryKey owner;
  LocalSyntaxPath syntaxPath;
  StableSelfOwner selfOwner;
};

StableSelfTypeFact::~StableSelfTypeFact() noexcept(false) = default;
StableSelfTypeFact::StableSelfTypeFact(StableSelfTypeFact&&) noexcept = default;
StableSelfTypeFact& StableSelfTypeFact::operator=(StableSelfTypeFact&&) noexcept = default;
StableSelfTypeFact::StableSelfTypeFact(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
zc::Maybe<StableSelfTypeFact> StableSelfTypeFact::from(StableOwnerBodyQueryKey&& owner,
                                                       LocalSyntaxPath&& syntaxPath,
                                                       StableSelfOwner&& selfOwner) {
  if (!sameModule(owner.module(), moduleOf(selfOwner))) { return zc::none; }
  return StableSelfTypeFact(
      zc::heap<Impl>(Impl{zc::mv(owner), zc::mv(syntaxPath), zc::mv(selfOwner)}));
}
StableSelfTypeFact StableSelfTypeFact::clone() const {
  return ZC_ASSERT_NONNULL(
      from(impl->owner.clone(), impl->syntaxPath.clone(), impl->selfOwner.clone()));
}
const StableOwnerBodyQueryKey& StableSelfTypeFact::owner() const noexcept { return impl->owner; }
const LocalSyntaxPath& StableSelfTypeFact::syntaxPath() const noexcept { return impl->syntaxPath; }
const StableSelfOwner& StableSelfTypeFact::selfOwner() const noexcept { return impl->selfOwner; }
bool StableSelfTypeFact::operator==(const StableSelfTypeFact& other) const {
  return impl->owner == other.impl->owner && impl->syntaxPath == other.impl->syntaxPath &&
         impl->selfOwner == other.impl->selfOwner;
}

struct StableThisBindingFact::Impl final {
  StableOwnerBodyQueryKey owner;
  LocalSyntaxPath expressionPath;
  StableCallableParameterQueryKey receiver;
};

StableThisBindingFact::~StableThisBindingFact() noexcept(false) = default;
StableThisBindingFact::StableThisBindingFact(StableThisBindingFact&&) noexcept = default;
StableThisBindingFact& StableThisBindingFact::operator=(StableThisBindingFact&&) noexcept = default;
StableThisBindingFact::StableThisBindingFact(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
zc::Maybe<StableThisBindingFact> StableThisBindingFact::from(
    StableOwnerBodyQueryKey&& owner, LocalSyntaxPath&& expressionPath,
    StableCallableParameterQueryKey&& receiver) {
  if (!sameModule(owner.module(), receiver.module())) { return zc::none; }
  return StableThisBindingFact(
      zc::heap<Impl>(Impl{zc::mv(owner), zc::mv(expressionPath), zc::mv(receiver)}));
}
StableThisBindingFact StableThisBindingFact::clone() const {
  return ZC_ASSERT_NONNULL(
      from(impl->owner.clone(), impl->expressionPath.clone(), impl->receiver.clone()));
}
const StableOwnerBodyQueryKey& StableThisBindingFact::owner() const noexcept { return impl->owner; }
const LocalSyntaxPath& StableThisBindingFact::expressionPath() const noexcept {
  return impl->expressionPath;
}
const StableCallableParameterQueryKey& StableThisBindingFact::receiver() const noexcept {
  return impl->receiver;
}
bool StableThisBindingFact::operator==(const StableThisBindingFact& other) const {
  return impl->owner == other.impl->owner && impl->expressionPath == other.impl->expressionPath &&
         impl->receiver == other.impl->receiver;
}

struct StableShadowTargetFact::Impl final {
  StableOwnerBodyQueryKey owner;
  StableBindingTargetKey binding;
  StableBindingTargetKey shadowed;
};

StableShadowTargetFact::~StableShadowTargetFact() noexcept(false) = default;
StableShadowTargetFact::StableShadowTargetFact(StableShadowTargetFact&&) noexcept = default;
StableShadowTargetFact& StableShadowTargetFact::operator=(StableShadowTargetFact&&) noexcept =
    default;
StableShadowTargetFact::StableShadowTargetFact(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
zc::Maybe<StableShadowTargetFact> StableShadowTargetFact::from(StableOwnerBodyQueryKey&& owner,
                                                               StableBindingTargetKey&& binding,
                                                               StableBindingTargetKey&& shadowed) {
  if (!isOwnedBodyTarget(owner, binding) || !isOwnedBodyTarget(owner, shadowed) ||
      binding == shadowed) {
    return zc::none;
  }
  return StableShadowTargetFact(
      zc::heap<Impl>(Impl{zc::mv(owner), zc::mv(binding), zc::mv(shadowed)}));
}
StableShadowTargetFact StableShadowTargetFact::clone() const {
  return ZC_ASSERT_NONNULL(
      from(impl->owner.clone(), impl->binding.clone(), impl->shadowed.clone()));
}
const StableOwnerBodyQueryKey& StableShadowTargetFact::owner() const noexcept {
  return impl->owner;
}
const StableBindingTargetKey& StableShadowTargetFact::binding() const noexcept {
  return impl->binding;
}
const StableBindingTargetKey& StableShadowTargetFact::shadowed() const noexcept {
  return impl->shadowed;
}
bool StableShadowTargetFact::operator==(const StableShadowTargetFact& other) const {
  return impl->owner == other.impl->owner && impl->binding == other.impl->binding &&
         impl->shadowed == other.impl->shadowed;
}

struct StableLabelKey::Impl final {
  StableOwnerBodyQueryKey owner;
  LocalSyntaxPath declarationPath;
};

StableLabelKey::~StableLabelKey() noexcept(false) = default;
StableLabelKey::StableLabelKey(StableLabelKey&&) noexcept = default;
StableLabelKey& StableLabelKey::operator=(StableLabelKey&&) noexcept = default;
StableLabelKey::StableLabelKey(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
StableLabelKey StableLabelKey::from(StableOwnerBodyQueryKey&& owner,
                                    LocalSyntaxPath&& declarationPath) {
  return StableLabelKey(zc::heap<Impl>(Impl{zc::mv(owner), zc::mv(declarationPath)}));
}
StableLabelKey StableLabelKey::clone() const {
  return from(impl->owner.clone(), impl->declarationPath.clone());
}
const StableOwnerBodyQueryKey& StableLabelKey::owner() const noexcept { return impl->owner; }
const LocalSyntaxPath& StableLabelKey::declarationPath() const noexcept {
  return impl->declarationPath;
}
bool StableLabelKey::operator==(const StableLabelKey& other) const {
  return impl->owner == other.impl->owner && impl->declarationPath == other.impl->declarationPath;
}

struct StableLabelTarget::Impl final {
  StableLabelTargetValue value;
};

StableLabelTarget::~StableLabelTarget() noexcept(false) = default;
StableLabelTarget::StableLabelTarget(StableLabelTarget&&) noexcept = default;
StableLabelTarget& StableLabelTarget::operator=(StableLabelTarget&&) noexcept = default;
StableLabelTarget::StableLabelTarget(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
StableLabelTarget StableLabelTarget::block(StableScopeOwnerKey&& scope) {
  return StableLabelTarget(
      zc::heap<Impl>(Impl{StableLabelTargetValue(StableBlockLabelTarget{zc::mv(scope)})}));
}
StableLabelTarget StableLabelTarget::loop(StableScopeOwnerKey&& scope) {
  return StableLabelTarget(
      zc::heap<Impl>(Impl{StableLabelTargetValue(StableLoopLabelTarget{zc::mv(scope)})}));
}
StableLabelTarget StableLabelTarget::clone() const {
  return impl->value.is<StableBlockLabelTarget>()
             ? block(impl->value.get<StableBlockLabelTarget>().scope.clone())
             : loop(impl->value.get<StableLoopLabelTarget>().scope.clone());
}
const StableLabelTargetValue& StableLabelTarget::value() const noexcept { return impl->value; }
const StableScopeOwnerKey& StableLabelTarget::scope() const noexcept {
  if (impl->value.is<StableBlockLabelTarget>()) {
    return impl->value.get<StableBlockLabelTarget>().scope;
  }
  return impl->value.get<StableLoopLabelTarget>().scope;
}
bool StableLabelTarget::operator==(const StableLabelTarget& other) const {
  if (impl->value.is<StableBlockLabelTarget>()) {
    return other.impl->value.is<StableBlockLabelTarget>() &&
           impl->value.get<StableBlockLabelTarget>().scope ==
               other.impl->value.get<StableBlockLabelTarget>().scope;
  }
  return other.impl->value.is<StableLoopLabelTarget>() &&
         impl->value.get<StableLoopLabelTarget>().scope ==
             other.impl->value.get<StableLoopLabelTarget>().scope;
}

struct StableLabelFact::Impl final {
  StableLabelKey key;
  identity::DeclaredDefinitionName name;
  LocalSyntaxPath statementPath;
  StableLabelTarget target;
};

StableLabelFact::~StableLabelFact() noexcept(false) = default;
StableLabelFact::StableLabelFact(StableLabelFact&&) noexcept = default;
StableLabelFact& StableLabelFact::operator=(StableLabelFact&&) noexcept = default;
StableLabelFact::StableLabelFact(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
zc::Maybe<StableLabelFact> StableLabelFact::from(StableLabelKey&& key,
                                                 identity::DeclaredDefinitionName&& name,
                                                 LocalSyntaxPath&& statementPath,
                                                 StableLabelTarget&& target) {
  const auto& scope = target.scope();
  if (!isOwnedBodyScope(key.owner(), scope) ||
      scope.value().get<StableBodyScope>().path != statementPath ||
      key.declarationPath() == statementPath) {
    return zc::none;
  }
  return StableLabelFact(
      zc::heap<Impl>(Impl{zc::mv(key), zc::mv(name), zc::mv(statementPath), zc::mv(target)}));
}
StableLabelFact StableLabelFact::clone() const {
  return ZC_ASSERT_NONNULL(from(impl->key.clone(), impl->name.clone(), impl->statementPath.clone(),
                                impl->target.clone()));
}
const StableLabelKey& StableLabelFact::key() const noexcept { return impl->key; }
const identity::DeclaredDefinitionName& StableLabelFact::name() const noexcept {
  return impl->name;
}
const LocalSyntaxPath& StableLabelFact::statementPath() const noexcept {
  return impl->statementPath;
}
const StableLabelTarget& StableLabelFact::target() const noexcept { return impl->target; }
bool StableLabelFact::operator==(const StableLabelFact& other) const {
  return impl->key == other.impl->key && impl->name == other.impl->name &&
         impl->statementPath == other.impl->statementPath && impl->target == other.impl->target;
}

struct StableControlTarget::Impl final {
  StableControlTargetValue value;
};

StableControlTarget::~StableControlTarget() noexcept(false) = default;
StableControlTarget::StableControlTarget(StableControlTarget&&) noexcept = default;
StableControlTarget& StableControlTarget::operator=(StableControlTarget&&) noexcept = default;
StableControlTarget::StableControlTarget(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
StableControlTarget StableControlTarget::explicitLabel(StableLabelKey&& label) {
  return StableControlTarget(zc::heap<Impl>(
      Impl{StableControlTargetValue(StableExplicitLabelControlTarget{zc::mv(label)})}));
}
StableControlTarget StableControlTarget::loop(StableScopeOwnerKey&& scope) {
  return StableControlTarget(
      zc::heap<Impl>(Impl{StableControlTargetValue(StableLoopControlTarget{zc::mv(scope)})}));
}
StableControlTarget StableControlTarget::match(StableScopeOwnerKey&& scope) {
  return StableControlTarget(
      zc::heap<Impl>(Impl{StableControlTargetValue(StableMatchControlTarget{zc::mv(scope)})}));
}
StableControlTarget StableControlTarget::clone() const {
  if (impl->value.is<StableExplicitLabelControlTarget>()) {
    return explicitLabel(impl->value.get<StableExplicitLabelControlTarget>().label.clone());
  }
  if (impl->value.is<StableLoopControlTarget>()) {
    return loop(impl->value.get<StableLoopControlTarget>().scope.clone());
  }
  return match(impl->value.get<StableMatchControlTarget>().scope.clone());
}
const StableControlTargetValue& StableControlTarget::value() const noexcept { return impl->value; }
bool StableControlTarget::operator==(const StableControlTarget& other) const {
  if (impl->value.is<StableExplicitLabelControlTarget>()) {
    return other.impl->value.is<StableExplicitLabelControlTarget>() &&
           impl->value.get<StableExplicitLabelControlTarget>().label ==
               other.impl->value.get<StableExplicitLabelControlTarget>().label;
  }
  if (impl->value.is<StableLoopControlTarget>()) {
    return other.impl->value.is<StableLoopControlTarget>() &&
           impl->value.get<StableLoopControlTarget>().scope ==
               other.impl->value.get<StableLoopControlTarget>().scope;
  }
  return other.impl->value.is<StableMatchControlTarget>() &&
         impl->value.get<StableMatchControlTarget>().scope ==
             other.impl->value.get<StableMatchControlTarget>().scope;
}

struct StableControlTransferFact::Impl final {
  StableOwnerBodyQueryKey owner;
  LocalSyntaxPath transferPath;
  ControlTransferKind kind;
  StableControlTarget target;
};

StableControlTransferFact::~StableControlTransferFact() noexcept(false) = default;
StableControlTransferFact::StableControlTransferFact(StableControlTransferFact&&) noexcept =
    default;
StableControlTransferFact& StableControlTransferFact::operator=(
    StableControlTransferFact&&) noexcept = default;
StableControlTransferFact::StableControlTransferFact(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
zc::Maybe<StableControlTransferFact> StableControlTransferFact::from(
    StableOwnerBodyQueryKey&& owner, LocalSyntaxPath&& transferPath, ControlTransferKind kind,
    StableControlTarget&& target) {
  if (!isStableBindingValue(kind) ||
      (kind == ControlTransferKind::Continue && target.value().is<StableMatchControlTarget>())) {
    return zc::none;
  }
  if (target.value().is<StableExplicitLabelControlTarget>()) {
    const auto& label = target.value().get<StableExplicitLabelControlTarget>().label;
    if (label.owner() != owner || label.declarationPath() == transferPath) { return zc::none; }
  } else {
    const auto& scope = target.value().is<StableLoopControlTarget>()
                            ? target.value().get<StableLoopControlTarget>().scope
                            : target.value().get<StableMatchControlTarget>().scope;
    if (!isOwnedBodyScope(owner, scope) ||
        scope.value().get<StableBodyScope>().path == transferPath) {
      return zc::none;
    }
  }
  return StableControlTransferFact(
      zc::heap<Impl>(Impl{zc::mv(owner), zc::mv(transferPath), kind, zc::mv(target)}));
}
StableControlTransferFact StableControlTransferFact::clone() const {
  return ZC_ASSERT_NONNULL(
      from(impl->owner.clone(), impl->transferPath.clone(), impl->kind, impl->target.clone()));
}
const StableOwnerBodyQueryKey& StableControlTransferFact::owner() const noexcept {
  return impl->owner;
}
const LocalSyntaxPath& StableControlTransferFact::transferPath() const noexcept {
  return impl->transferPath;
}
ControlTransferKind StableControlTransferFact::kind() const noexcept { return impl->kind; }
const StableControlTarget& StableControlTransferFact::target() const noexcept {
  return impl->target;
}
bool StableControlTransferFact::operator==(const StableControlTransferFact& other) const {
  return impl->owner == other.impl->owner && impl->transferPath == other.impl->transferPath &&
         impl->kind == other.impl->kind && impl->target == other.impl->target;
}

struct StableClosureFact::Impl final {
  StableOwnerBodyQueryKey owner;
  AnonymousOwnerLocalKey closure;
  StableScopeOwnerKey scope;
};

StableClosureFact::~StableClosureFact() noexcept(false) = default;
StableClosureFact::StableClosureFact(StableClosureFact&&) noexcept = default;
StableClosureFact& StableClosureFact::operator=(StableClosureFact&&) noexcept = default;
StableClosureFact::StableClosureFact(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
zc::Maybe<StableClosureFact> StableClosureFact::from(StableOwnerBodyQueryKey&& owner,
                                                     AnonymousOwnerLocalKey&& closure,
                                                     StableScopeOwnerKey&& scope) {
  if (closure.owner() != owner.owner() || closure.role() != AnonymousOwnerLocalRole::Closure ||
      !isOwnedBodyScope(owner, scope) ||
      scope.value().get<StableBodyScope>().path != closure.path()) {
    return zc::none;
  }
  return StableClosureFact(zc::heap<Impl>(Impl{zc::mv(owner), zc::mv(closure), zc::mv(scope)}));
}
StableClosureFact StableClosureFact::clone() const {
  return ZC_ASSERT_NONNULL(from(impl->owner.clone(), impl->closure.clone(), impl->scope.clone()));
}
const StableOwnerBodyQueryKey& StableClosureFact::owner() const noexcept { return impl->owner; }
const AnonymousOwnerLocalKey& StableClosureFact::closure() const noexcept { return impl->closure; }
const StableScopeOwnerKey& StableClosureFact::scope() const noexcept { return impl->scope; }
bool StableClosureFact::operator==(const StableClosureFact& other) const {
  return impl->owner == other.impl->owner && impl->closure == other.impl->closure &&
         impl->scope == other.impl->scope;
}

struct StableClosureFreeVariable::Impl final {
  StableBindingTargetKey target;
  CanonicalNonEmptySequence<LocalSyntaxPath> referencePaths;
};

StableClosureFreeVariable::~StableClosureFreeVariable() noexcept(false) = default;
StableClosureFreeVariable::StableClosureFreeVariable(StableClosureFreeVariable&&) noexcept =
    default;
StableClosureFreeVariable& StableClosureFreeVariable::operator=(
    StableClosureFreeVariable&&) noexcept = default;
StableClosureFreeVariable::StableClosureFreeVariable(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
StableClosureFreeVariable StableClosureFreeVariable::from(
    StableBindingTargetKey&& target, CanonicalNonEmptySequence<LocalSyntaxPath>&& referencePaths) {
  return StableClosureFreeVariable(zc::heap<Impl>(Impl{zc::mv(target), zc::mv(referencePaths)}));
}
StableClosureFreeVariable StableClosureFreeVariable::clone() const {
  return from(impl->target.clone(), impl->referencePaths.clone());
}
const StableBindingTargetKey& StableClosureFreeVariable::target() const noexcept {
  return impl->target;
}
const CanonicalNonEmptySequence<LocalSyntaxPath>& StableClosureFreeVariable::referencePaths()
    const noexcept {
  return impl->referencePaths;
}
bool StableClosureFreeVariable::operator==(const StableClosureFreeVariable& other) const {
  return impl->target == other.impl->target && impl->referencePaths == other.impl->referencePaths;
}

struct StableClosureFreeVariableFact::Impl final {
  StableOwnerBodyQueryKey owner;
  AnonymousOwnerLocalKey closure;
  CanonicalSequence<StableClosureFreeVariable> variables;
};

StableClosureFreeVariableFact::~StableClosureFreeVariableFact() noexcept(false) = default;
StableClosureFreeVariableFact::StableClosureFreeVariableFact(
    StableClosureFreeVariableFact&&) noexcept = default;
StableClosureFreeVariableFact& StableClosureFreeVariableFact::operator=(
    StableClosureFreeVariableFact&&) noexcept = default;
StableClosureFreeVariableFact::StableClosureFreeVariableFact(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
zc::Maybe<StableClosureFreeVariableFact> StableClosureFreeVariableFact::from(
    StableOwnerBodyQueryKey&& owner, AnonymousOwnerLocalKey&& closure,
    CanonicalSequence<StableClosureFreeVariable>&& variables) {
  if (closure.owner() != owner.owner() || closure.role() != AnonymousOwnerLocalRole::Closure) {
    return zc::none;
  }
  for (const auto& variable : variables.values()) {
    if (!isOwnedBodyTarget(owner, variable.target())) { return zc::none; }
  }
  return StableClosureFreeVariableFact(
      zc::heap<Impl>(Impl{zc::mv(owner), zc::mv(closure), zc::mv(variables)}));
}
StableClosureFreeVariableFact StableClosureFreeVariableFact::clone() const {
  return ZC_ASSERT_NONNULL(
      from(impl->owner.clone(), impl->closure.clone(), impl->variables.clone()));
}
const StableOwnerBodyQueryKey& StableClosureFreeVariableFact::owner() const noexcept {
  return impl->owner;
}
const AnonymousOwnerLocalKey& StableClosureFreeVariableFact::closure() const noexcept {
  return impl->closure;
}
const CanonicalSequence<StableClosureFreeVariable>& StableClosureFreeVariableFact::variables()
    const noexcept {
  return impl->variables;
}
bool StableClosureFreeVariableFact::operator==(const StableClosureFreeVariableFact& other) const {
  return impl->owner == other.impl->owner && impl->closure == other.impl->closure &&
         impl->variables == other.impl->variables;
}

struct StableExplicitCaptureBindingFact::Impl final {
  LocalSyntaxPath itemPath;
  StableBindingTargetKey target;
  StableExplicitCaptureMode mode;
};

StableExplicitCaptureBindingFact::~StableExplicitCaptureBindingFact() noexcept(false) = default;
StableExplicitCaptureBindingFact::StableExplicitCaptureBindingFact(
    StableExplicitCaptureBindingFact&&) noexcept = default;
StableExplicitCaptureBindingFact& StableExplicitCaptureBindingFact::operator=(
    StableExplicitCaptureBindingFact&&) noexcept = default;
StableExplicitCaptureBindingFact::StableExplicitCaptureBindingFact(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
zc::Maybe<StableExplicitCaptureBindingFact> StableExplicitCaptureBindingFact::from(
    LocalSyntaxPath&& itemPath, StableBindingTargetKey&& target, StableExplicitCaptureMode mode) {
  if (!isStableBindingValue(mode) || (mode == StableExplicitCaptureMode::This &&
                                      !target.value().is<StableCallableParameterBindingTarget>())) {
    return zc::none;
  }
  return StableExplicitCaptureBindingFact(
      zc::heap<Impl>(Impl{zc::mv(itemPath), zc::mv(target), mode}));
}
StableExplicitCaptureBindingFact StableExplicitCaptureBindingFact::clone() const {
  return ZC_ASSERT_NONNULL(from(impl->itemPath.clone(), impl->target.clone(), impl->mode));
}
const LocalSyntaxPath& StableExplicitCaptureBindingFact::itemPath() const noexcept {
  return impl->itemPath;
}
const StableBindingTargetKey& StableExplicitCaptureBindingFact::target() const noexcept {
  return impl->target;
}
StableExplicitCaptureMode StableExplicitCaptureBindingFact::mode() const noexcept {
  return impl->mode;
}
bool StableExplicitCaptureBindingFact::operator==(
    const StableExplicitCaptureBindingFact& other) const {
  return impl->itemPath == other.impl->itemPath && impl->target == other.impl->target &&
         impl->mode == other.impl->mode;
}

struct StableExplicitClosureCaptureFact::Impl final {
  StableOwnerBodyQueryKey owner;
  AnonymousOwnerLocalKey closure;
  LocalSyntaxPath captureListPath;
  CanonicalSequence<StableExplicitCaptureBindingFact> captures;
};

StableExplicitClosureCaptureFact::~StableExplicitClosureCaptureFact() noexcept(false) = default;
StableExplicitClosureCaptureFact::StableExplicitClosureCaptureFact(
    StableExplicitClosureCaptureFact&&) noexcept = default;
StableExplicitClosureCaptureFact& StableExplicitClosureCaptureFact::operator=(
    StableExplicitClosureCaptureFact&&) noexcept = default;
StableExplicitClosureCaptureFact::StableExplicitClosureCaptureFact(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
zc::Maybe<StableExplicitClosureCaptureFact> StableExplicitClosureCaptureFact::from(
    StableOwnerBodyQueryKey&& owner, AnonymousOwnerLocalKey&& closure,
    LocalSyntaxPath&& captureListPath,
    CanonicalSequence<StableExplicitCaptureBindingFact>&& captures) {
  if (closure.owner() != owner.owner() ||
      closure.role() != AnonymousOwnerLocalRole::FunctionExpression ||
      closure.path() == captureListPath) {
    return zc::none;
  }
  for (size_t index = 0; index < captures.values().size(); ++index) {
    const auto& capture = captures.values()[index];
    if (!isOwnedBodyTarget(owner, capture.target()) || capture.itemPath() == closure.path() ||
        capture.itemPath() == captureListPath) {
      return zc::none;
    }
    for (size_t prior = 0; prior < index; ++prior) {
      if (captures.values()[prior].itemPath() == capture.itemPath()) { return zc::none; }
    }
  }
  return StableExplicitClosureCaptureFact(zc::heap<Impl>(
      Impl{zc::mv(owner), zc::mv(closure), zc::mv(captureListPath), zc::mv(captures)}));
}
StableExplicitClosureCaptureFact StableExplicitClosureCaptureFact::clone() const {
  return ZC_ASSERT_NONNULL(from(impl->owner.clone(), impl->closure.clone(),
                                impl->captureListPath.clone(), impl->captures.clone()));
}
const StableOwnerBodyQueryKey& StableExplicitClosureCaptureFact::owner() const noexcept {
  return impl->owner;
}
const AnonymousOwnerLocalKey& StableExplicitClosureCaptureFact::closure() const noexcept {
  return impl->closure;
}
const LocalSyntaxPath& StableExplicitClosureCaptureFact::captureListPath() const noexcept {
  return impl->captureListPath;
}
const CanonicalSequence<StableExplicitCaptureBindingFact>&
StableExplicitClosureCaptureFact::captures() const noexcept {
  return impl->captures;
}
bool StableExplicitClosureCaptureFact::operator==(
    const StableExplicitClosureCaptureFact& other) const {
  return impl->owner == other.impl->owner && impl->closure == other.impl->closure &&
         impl->captureListPath == other.impl->captureListPath &&
         impl->captures == other.impl->captures;
}

struct StableDeclarationFact::Impl final {
  StableDefinitionQueryKey queryKey;
  identity::DefinitionIdentityRecord record;
  StableScopeOwnerKey declaringScope;
  identity::DefinitionKind kind;
  Namespace nameSpace;
  identity::DeclaredDefinitionName name;
  DefinitionActivation activation;
  zc::Maybe<MemberVisibility> visibility;
};

StableDeclarationFact::~StableDeclarationFact() noexcept(false) = default;
StableDeclarationFact::StableDeclarationFact(StableDeclarationFact&&) noexcept = default;
StableDeclarationFact& StableDeclarationFact::operator=(StableDeclarationFact&&) noexcept = default;
StableDeclarationFact::StableDeclarationFact(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}

zc::Maybe<StableDeclarationFact> StableDeclarationFact::from(
    StableDefinitionQueryKey&& queryKey, identity::DefinitionIdentityRecord&& record,
    StableScopeOwnerKey&& declaringScope, identity::DefinitionKind kind, Namespace nameSpace,
    identity::DeclaredDefinitionName&& name, DefinitionActivation activation,
    zc::Maybe<MemberVisibility>&& visibility) {
  if (!sameModule(queryKey.module(), record.module()) ||
      !sameModule(queryKey.module(), moduleOf(declaringScope)) ||
      queryKey.definition() != identity::DefinitionKey::compute(record) || kind != record.kind() ||
      static_cast<uint8_t>(nameSpace) != static_cast<uint8_t>(record.nameSpace()) ||
      name.text() != record.name() ||
      !inClosedRange(activation, DefinitionActivation::ModuleSkeleton,
                     DefinitionActivation::LoopPattern)) {
    return zc::none;
  }
  ZC_IF_SOME(value, visibility) {
    if (!inClosedRange(value, MemberVisibility::Public, MemberVisibility::Protected)) {
      return zc::none;
    }
  }
  return StableDeclarationFact(
      zc::heap<Impl>(Impl{zc::mv(queryKey), zc::mv(record), zc::mv(declaringScope), kind, nameSpace,
                          zc::mv(name), activation, zc::mv(visibility)}));
}

StableDeclarationFact StableDeclarationFact::clone() const {
  return StableDeclarationFact(zc::heap<Impl>(
      Impl{impl->queryKey.clone(), impl->record.clone(), impl->declaringScope.clone(), impl->kind,
           impl->nameSpace, impl->name.clone(), impl->activation, cloneMaybe(impl->visibility)}));
}
const StableDefinitionQueryKey& StableDeclarationFact::queryKey() const noexcept {
  return impl->queryKey;
}
const identity::DefinitionIdentityRecord& StableDeclarationFact::record() const noexcept {
  return impl->record;
}
const StableScopeOwnerKey& StableDeclarationFact::declaringScope() const noexcept {
  return impl->declaringScope;
}
identity::DefinitionKind StableDeclarationFact::kind() const noexcept { return impl->kind; }
Namespace StableDeclarationFact::nameSpace() const noexcept { return impl->nameSpace; }
const identity::DeclaredDefinitionName& StableDeclarationFact::name() const noexcept {
  return impl->name;
}
DefinitionActivation StableDeclarationFact::activation() const noexcept { return impl->activation; }
const zc::Maybe<MemberVisibility>& StableDeclarationFact::visibility() const noexcept {
  return impl->visibility;
}
bool StableDeclarationFact::operator==(const StableDeclarationFact& other) const {
  return impl->queryKey == other.impl->queryKey &&
         stable_binding_detail::sameElement(impl->record, other.impl->record) &&
         impl->declaringScope == other.impl->declaringScope && impl->kind == other.impl->kind &&
         impl->nameSpace == other.impl->nameSpace && impl->name == other.impl->name &&
         impl->activation == other.impl->activation &&
         sameMaybe(impl->visibility, other.impl->visibility);
}

struct StableImplementationOccurrenceFact::Impl final {
  StableImplementationOccurrenceQueryKey occurrence;
  StableImplementationQueryKey authority;
  identity::ImplIdentityRecord record;
  StableScopeOwnerKey declaringScope;
};

StableImplementationOccurrenceFact::~StableImplementationOccurrenceFact() noexcept(false) = default;
StableImplementationOccurrenceFact::StableImplementationOccurrenceFact(
    StableImplementationOccurrenceFact&&) noexcept = default;
StableImplementationOccurrenceFact& StableImplementationOccurrenceFact::operator=(
    StableImplementationOccurrenceFact&&) noexcept = default;
StableImplementationOccurrenceFact::StableImplementationOccurrenceFact(
    zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}

zc::Maybe<StableImplementationOccurrenceFact> StableImplementationOccurrenceFact::from(
    StableImplementationOccurrenceQueryKey&& occurrence, StableImplementationQueryKey&& authority,
    identity::ImplIdentityRecord&& record, StableScopeOwnerKey&& declaringScope) {
  if (!sameModule(occurrence.module(), authority.module()) ||
      !sameModule(occurrence.module(), record.module()) ||
      !sameModule(occurrence.module(), moduleOf(declaringScope)) ||
      occurrence.occurrence().implementation() != authority.implementation() ||
      authority.implementation() != identity::ImplKey::compute(record)) {
    return zc::none;
  }
  return StableImplementationOccurrenceFact(zc::heap<Impl>(
      Impl{zc::mv(occurrence), zc::mv(authority), zc::mv(record), zc::mv(declaringScope)}));
}

StableImplementationOccurrenceFact StableImplementationOccurrenceFact::clone() const {
  return StableImplementationOccurrenceFact(
      zc::heap<Impl>(Impl{impl->occurrence.clone(), impl->authority.clone(), impl->record.clone(),
                          impl->declaringScope.clone()}));
}
const StableImplementationOccurrenceQueryKey& StableImplementationOccurrenceFact::occurrence()
    const noexcept {
  return impl->occurrence;
}
const StableImplementationQueryKey& StableImplementationOccurrenceFact::authority() const noexcept {
  return impl->authority;
}
const identity::ImplIdentityRecord& StableImplementationOccurrenceFact::record() const noexcept {
  return impl->record;
}
const StableScopeOwnerKey& StableImplementationOccurrenceFact::declaringScope() const noexcept {
  return impl->declaringScope;
}
bool StableImplementationOccurrenceFact::operator==(
    const StableImplementationOccurrenceFact& other) const {
  return impl->occurrence == other.impl->occurrence && impl->authority == other.impl->authority &&
         stable_binding_detail::sameElement(impl->record, other.impl->record) &&
         impl->declaringScope == other.impl->declaringScope;
}

struct StableGenericParameterDeclarationFact::Impl final {
  StableGenericParameterQueryKey queryKey;
  identity::GenericParameterIdentityRecord record;
  StableHeaderSite headerSite;
  StableScopeOwnerKey declaringScope;
  identity::DeclaredDefinitionName name;
};

StableGenericParameterDeclarationFact::~StableGenericParameterDeclarationFact() noexcept(false) =
    default;
StableGenericParameterDeclarationFact::StableGenericParameterDeclarationFact(
    StableGenericParameterDeclarationFact&&) noexcept = default;
StableGenericParameterDeclarationFact& StableGenericParameterDeclarationFact::operator=(
    StableGenericParameterDeclarationFact&&) noexcept = default;
StableGenericParameterDeclarationFact::StableGenericParameterDeclarationFact(
    zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}

zc::Maybe<StableGenericParameterDeclarationFact> StableGenericParameterDeclarationFact::from(
    StableGenericParameterQueryKey&& queryKey, identity::GenericParameterIdentityRecord&& record,
    StableHeaderSite&& headerSite, StableScopeOwnerKey&& declaringScope,
    identity::DeclaredDefinitionName&& name) {
  const auto& owner = record.owner();
  const bool validOwner =
      (headerSite.value().is<DefinitionAuthoritySite>() &&
       owner.kind() == identity::StableGenericParameterOwnerKind::Definition) ||
      (headerSite.value().is<ImplementationOccurrenceSite>() &&
       owner.kind() == identity::StableGenericParameterOwnerKind::Implementation &&
       ZC_ASSERT_NONNULL(owner.implKey()) ==
           headerSite.value().get<ImplementationOccurrenceSite>().site.implementation());
  if (!sameModule(queryKey.module(), moduleOf(headerSite)) ||
      !sameModule(queryKey.module(), moduleOf(declaringScope)) ||
      queryKey.parameter() != identity::GenericParameterKey::compute(record) || !validOwner) {
    return zc::none;
  }
  return StableGenericParameterDeclarationFact(zc::heap<Impl>(Impl{
      zc::mv(queryKey), zc::mv(record), zc::mv(headerSite), zc::mv(declaringScope), zc::mv(name)}));
}

StableGenericParameterDeclarationFact StableGenericParameterDeclarationFact::clone() const {
  return StableGenericParameterDeclarationFact(
      zc::heap<Impl>(Impl{impl->queryKey.clone(), impl->record.clone(), impl->headerSite.clone(),
                          impl->declaringScope.clone(), impl->name.clone()}));
}
const StableGenericParameterQueryKey& StableGenericParameterDeclarationFact::queryKey()
    const noexcept {
  return impl->queryKey;
}
const identity::GenericParameterIdentityRecord& StableGenericParameterDeclarationFact::record()
    const noexcept {
  return impl->record;
}
const StableHeaderSite& StableGenericParameterDeclarationFact::headerSite() const noexcept {
  return impl->headerSite;
}
const StableScopeOwnerKey& StableGenericParameterDeclarationFact::declaringScope() const noexcept {
  return impl->declaringScope;
}
const identity::DeclaredDefinitionName& StableGenericParameterDeclarationFact::name()
    const noexcept {
  return impl->name;
}
bool StableGenericParameterDeclarationFact::operator==(
    const StableGenericParameterDeclarationFact& other) const {
  return impl->queryKey == other.impl->queryKey &&
         stable_binding_detail::sameElement(impl->record, other.impl->record) &&
         impl->headerSite == other.impl->headerSite &&
         impl->declaringScope == other.impl->declaringScope && impl->name == other.impl->name;
}

struct StableCallableParameterDeclarationFact::Impl final {
  StableCallableParameterQueryKey queryKey;
  identity::CallableParameterIdentityRecord record;
  StableHeaderSite headerSite;
  StableScopeOwnerKey declaringScope;
  zc::Maybe<identity::DeclaredDefinitionName> name;
};

StableCallableParameterDeclarationFact::~StableCallableParameterDeclarationFact() noexcept(false) =
    default;
StableCallableParameterDeclarationFact::StableCallableParameterDeclarationFact(
    StableCallableParameterDeclarationFact&&) noexcept = default;
StableCallableParameterDeclarationFact& StableCallableParameterDeclarationFact::operator=(
    StableCallableParameterDeclarationFact&&) noexcept = default;
StableCallableParameterDeclarationFact::StableCallableParameterDeclarationFact(
    zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}

zc::Maybe<StableCallableParameterDeclarationFact> StableCallableParameterDeclarationFact::from(
    StableCallableParameterQueryKey&& queryKey, identity::CallableParameterIdentityRecord&& record,
    StableHeaderSite&& headerSite, StableScopeOwnerKey&& declaringScope,
    zc::Maybe<identity::DeclaredDefinitionName>&& name) {
  const bool isReceiver =
      record.position().kind() == identity::CallableParameterPositionKind::Receiver;
  if (!sameModule(queryKey.module(), moduleOf(headerSite)) ||
      !sameModule(queryKey.module(), moduleOf(declaringScope)) ||
      queryKey.parameter() != identity::CallableParameterKey::compute(record) ||
      !headerSite.value().is<DefinitionAuthoritySite>() || (isReceiver && name != zc::none) ||
      (!isReceiver && name == zc::none)) {
    return zc::none;
  }
  return StableCallableParameterDeclarationFact(zc::heap<Impl>(Impl{
      zc::mv(queryKey), zc::mv(record), zc::mv(headerSite), zc::mv(declaringScope), zc::mv(name)}));
}

StableCallableParameterDeclarationFact StableCallableParameterDeclarationFact::clone() const {
  return StableCallableParameterDeclarationFact(
      zc::heap<Impl>(Impl{impl->queryKey.clone(), impl->record.clone(), impl->headerSite.clone(),
                          impl->declaringScope.clone(), cloneMaybe(impl->name)}));
}
const StableCallableParameterQueryKey& StableCallableParameterDeclarationFact::queryKey()
    const noexcept {
  return impl->queryKey;
}
const identity::CallableParameterIdentityRecord& StableCallableParameterDeclarationFact::record()
    const noexcept {
  return impl->record;
}
const StableHeaderSite& StableCallableParameterDeclarationFact::headerSite() const noexcept {
  return impl->headerSite;
}
const StableScopeOwnerKey& StableCallableParameterDeclarationFact::declaringScope() const noexcept {
  return impl->declaringScope;
}
const zc::Maybe<identity::DeclaredDefinitionName>& StableCallableParameterDeclarationFact::name()
    const noexcept {
  return impl->name;
}
bool StableCallableParameterDeclarationFact::operator==(
    const StableCallableParameterDeclarationFact& other) const {
  return impl->queryKey == other.impl->queryKey &&
         stable_binding_detail::sameElement(impl->record, other.impl->record) &&
         impl->headerSite == other.impl->headerSite &&
         impl->declaringScope == other.impl->declaringScope &&
         sameMaybe(impl->name, other.impl->name);
}

StableBindingTargetKey::StableBindingTargetKey(StableBindingTargetValue&& value) noexcept
    : valueField(zc::mv(value)) {}
#define ZOM_DEFINE_STABLE_TARGET_FACTORY(Method, Variant, Type)                      \
  StableBindingTargetKey StableBindingTargetKey::Method(Type&& value) {              \
    return StableBindingTargetKey(StableBindingTargetValue(Variant{zc::mv(value)})); \
  }
ZOM_DEFINE_STABLE_TARGET_FACTORY(definition, StableDefinitionBindingTarget,
                                 StableDefinitionQueryKey)
ZOM_DEFINE_STABLE_TARGET_FACTORY(implementation, StableImplementationBindingTarget,
                                 StableImplementationQueryKey)
ZOM_DEFINE_STABLE_TARGET_FACTORY(module, StableModuleBindingTarget, identity::ModuleKey)
ZOM_DEFINE_STABLE_TARGET_FACTORY(semanticImport, StableSemanticImportBindingTarget,
                                 StableSemanticImportQueryKey)
ZOM_DEFINE_STABLE_TARGET_FACTORY(genericParameter, StableGenericParameterBindingTarget,
                                 StableGenericParameterQueryKey)
ZOM_DEFINE_STABLE_TARGET_FACTORY(callableParameter, StableCallableParameterBindingTarget,
                                 StableCallableParameterQueryKey)
#undef ZOM_DEFINE_STABLE_TARGET_FACTORY
zc::Maybe<StableBindingTargetKey> StableBindingTargetKey::ownerLocal(
    StableOwnerBodyQueryKey&& owner, OwnerLocalBindingKey&& binding) {
  if (owner.owner() != binding.owner()) { return zc::none; }
  return StableBindingTargetKey(
      StableBindingTargetValue(StableOwnerLocalBindingTarget{zc::mv(owner), zc::mv(binding)}));
}
zc::Maybe<StableBindingTargetKey> StableBindingTargetKey::anonymousOwner(
    StableOwnerBodyQueryKey&& owner, AnonymousOwnerLocalKey&& binding) {
  if (owner.owner() != binding.owner()) { return zc::none; }
  return StableBindingTargetKey(
      StableBindingTargetValue(StableAnonymousOwnerBindingTarget{zc::mv(owner), zc::mv(binding)}));
}
StableBindingTargetKey StableBindingTargetKey::clone() const {
#define ZOM_CLONE_STABLE_TARGET(Variant, Factory, Field) \
  if (valueField.is<Variant>()) { return Factory(valueField.get<Variant>().Field.clone()); }
  ZOM_CLONE_STABLE_TARGET(StableDefinitionBindingTarget, definition, definition)
  ZOM_CLONE_STABLE_TARGET(StableImplementationBindingTarget, implementation, implementation)
  ZOM_CLONE_STABLE_TARGET(StableModuleBindingTarget, module, module)
  ZOM_CLONE_STABLE_TARGET(StableSemanticImportBindingTarget, semanticImport, import)
  if (valueField.is<StableOwnerLocalBindingTarget>()) {
    const auto& value = valueField.get<StableOwnerLocalBindingTarget>();
    return ZC_ASSERT_NONNULL(ownerLocal(value.owner.clone(), value.binding.clone()));
  }
  if (valueField.is<StableAnonymousOwnerBindingTarget>()) {
    const auto& value = valueField.get<StableAnonymousOwnerBindingTarget>();
    return ZC_ASSERT_NONNULL(anonymousOwner(value.owner.clone(), value.binding.clone()));
  }
  ZOM_CLONE_STABLE_TARGET(StableGenericParameterBindingTarget, genericParameter, parameter)
#undef ZOM_CLONE_STABLE_TARGET
  return callableParameter(
      valueField.get<StableCallableParameterBindingTarget>().parameter.clone());
}
const StableBindingTargetValue& StableBindingTargetKey::value() const noexcept {
  return valueField;
}
bool StableBindingTargetKey::operator==(const StableBindingTargetKey& other) const {
#define ZOM_SAME_TARGET(Variant, Field)                                               \
  if (valueField.is<Variant>()) {                                                     \
    return other.valueField.is<Variant>() &&                                          \
           stable_binding_detail::sameElement(valueField.get<Variant>().Field,        \
                                              other.valueField.get<Variant>().Field); \
  }
  ZOM_SAME_TARGET(StableDefinitionBindingTarget, definition)
  ZOM_SAME_TARGET(StableImplementationBindingTarget, implementation)
  ZOM_SAME_TARGET(StableModuleBindingTarget, module)
  ZOM_SAME_TARGET(StableSemanticImportBindingTarget, import)
#define ZOM_SAME_TARGET_PAIR(Variant)                                                    \
  if (valueField.is<Variant>()) {                                                        \
    return other.valueField.is<Variant>() &&                                             \
           valueField.get<Variant>().owner == other.valueField.get<Variant>().owner &&   \
           valueField.get<Variant>().binding == other.valueField.get<Variant>().binding; \
  }
  ZOM_SAME_TARGET_PAIR(StableOwnerLocalBindingTarget)
  ZOM_SAME_TARGET_PAIR(StableAnonymousOwnerBindingTarget)
#undef ZOM_SAME_TARGET_PAIR
  ZOM_SAME_TARGET(StableGenericParameterBindingTarget, parameter)
  ZOM_SAME_TARGET(StableCallableParameterBindingTarget, parameter)
#undef ZOM_SAME_TARGET
  ZC_UNREACHABLE
}

struct StableExportedBinding::Impl final {
  BindingNameKey name;
  StableBindingTargetKey binding;
  StableBindingTargetKey canonicalTarget;
  zc::Maybe<MemberVisibility> visibility;
  bool exported;
};

StableExportedBinding::~StableExportedBinding() noexcept(false) = default;
StableExportedBinding::StableExportedBinding(StableExportedBinding&&) noexcept = default;
StableExportedBinding& StableExportedBinding::operator=(StableExportedBinding&&) noexcept = default;
StableExportedBinding::StableExportedBinding(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}

zc::Maybe<StableExportedBinding> StableExportedBinding::from(
    BindingNameKey&& name, StableBindingTargetKey&& binding,
    StableBindingTargetKey&& canonicalTarget, zc::Maybe<MemberVisibility>&& visibility,
    bool exported) {
  if (!exported) { return zc::none; }
  ZC_IF_SOME(value, visibility) {
    if (!inClosedRange(value, MemberVisibility::Public, MemberVisibility::Protected)) {
      return zc::none;
    }
  }
  return StableExportedBinding(zc::heap<Impl>(
      Impl{zc::mv(name), zc::mv(binding), zc::mv(canonicalTarget), zc::mv(visibility), exported}));
}

StableExportedBinding StableExportedBinding::clone() const {
  return ZC_ASSERT_NONNULL(from(impl->name.clone(), impl->binding.clone(),
                                impl->canonicalTarget.clone(), cloneMaybe(impl->visibility),
                                impl->exported));
}
const BindingNameKey& StableExportedBinding::name() const noexcept { return impl->name; }
const StableBindingTargetKey& StableExportedBinding::binding() const noexcept {
  return impl->binding;
}
const StableBindingTargetKey& StableExportedBinding::canonicalTarget() const noexcept {
  return impl->canonicalTarget;
}
const zc::Maybe<MemberVisibility>& StableExportedBinding::visibility() const noexcept {
  return impl->visibility;
}
bool StableExportedBinding::exported() const noexcept { return impl->exported; }
bool StableExportedBinding::operator==(const StableExportedBinding& other) const {
  return impl->name.nameSpace() == other.impl->name.nameSpace() &&
         impl->name.name() == other.impl->name.name() && impl->binding == other.impl->binding &&
         impl->canonicalTarget == other.impl->canonicalTarget &&
         sameMaybe(impl->visibility, other.impl->visibility) &&
         impl->exported == other.impl->exported;
}

struct StableImportFact::Impl final {
  StableSemanticImportQueryKey queryKey;
  StableScopeOwnerKey declaringScope;
  StableBindingTargetKey target;
  StableBindingTargetKey canonicalTarget;
  Namespace nameSpace;
  BindingOrigin origin;
  zc::Maybe<MemberVisibility> visibility;
  bool exported;
};

StableImportFact::~StableImportFact() noexcept(false) = default;
StableImportFact::StableImportFact(StableImportFact&&) noexcept = default;
StableImportFact& StableImportFact::operator=(StableImportFact&&) noexcept = default;
StableImportFact::StableImportFact(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}

zc::Maybe<StableImportFact> StableImportFact::from(
    StableSemanticImportQueryKey&& queryKey, StableScopeOwnerKey&& declaringScope,
    StableBindingTargetKey&& target, StableBindingTargetKey&& canonicalTarget, Namespace nameSpace,
    BindingOrigin origin, zc::Maybe<MemberVisibility>&& visibility, bool exported) {
  const bool reexport =
      queryKey.binding().operation() == identity::SemanticImportOperation::ForeignReexport;
  const bool validOrigin =
      reexport ? origin == BindingOrigin::ReexportAlias
               : origin == BindingOrigin::ImportAlias || origin == BindingOrigin::Prelude;
  if (!sameModule(queryKey.requester(), moduleOf(declaringScope)) ||
      static_cast<uint8_t>(nameSpace) !=
          static_cast<uint8_t>(queryKey.binding().localNamespace()) ||
      !validOrigin || exported != reexport) {
    return zc::none;
  }
  ZC_IF_SOME(value, visibility) {
    if (!inClosedRange(value, MemberVisibility::Public, MemberVisibility::Protected)) {
      return zc::none;
    }
  }
  return StableImportFact(zc::heap<Impl>(Impl{zc::mv(queryKey), zc::mv(declaringScope),
                                              zc::mv(target), zc::mv(canonicalTarget), nameSpace,
                                              origin, zc::mv(visibility), exported}));
}

StableImportFact StableImportFact::clone() const {
  return StableImportFact(
      zc::heap<Impl>(Impl{impl->queryKey.clone(), impl->declaringScope.clone(),
                          impl->target.clone(), impl->canonicalTarget.clone(), impl->nameSpace,
                          impl->origin, cloneMaybe(impl->visibility), impl->exported}));
}
const StableSemanticImportQueryKey& StableImportFact::queryKey() const noexcept {
  return impl->queryKey;
}
const StableScopeOwnerKey& StableImportFact::declaringScope() const noexcept {
  return impl->declaringScope;
}
const StableBindingTargetKey& StableImportFact::target() const noexcept { return impl->target; }
const StableBindingTargetKey& StableImportFact::canonicalTarget() const noexcept {
  return impl->canonicalTarget;
}
Namespace StableImportFact::nameSpace() const noexcept { return impl->nameSpace; }
BindingOrigin StableImportFact::origin() const noexcept { return impl->origin; }
const zc::Maybe<MemberVisibility>& StableImportFact::visibility() const noexcept {
  return impl->visibility;
}
bool StableImportFact::exported() const noexcept { return impl->exported; }
bool StableImportFact::operator==(const StableImportFact& other) const {
  return impl->queryKey == other.impl->queryKey &&
         impl->declaringScope == other.impl->declaringScope && impl->target == other.impl->target &&
         impl->canonicalTarget == other.impl->canonicalTarget &&
         impl->nameSpace == other.impl->nameSpace && impl->origin == other.impl->origin &&
         sameMaybe(impl->visibility, other.impl->visibility) &&
         impl->exported == other.impl->exported;
}

struct StableModuleAliasFact::Impl final {
  StableSemanticImportQueryKey queryKey;
  StableScopeOwnerKey declaringScope;
  StableDefinitionQueryKey alias;
  identity::ModuleKey canonicalModule;
  ModuleAliasExportNamesRevision targetExportNamesRevision;
};

StableModuleAliasFact::~StableModuleAliasFact() noexcept(false) = default;
StableModuleAliasFact::StableModuleAliasFact(StableModuleAliasFact&&) noexcept = default;
StableModuleAliasFact& StableModuleAliasFact::operator=(StableModuleAliasFact&&) noexcept = default;
StableModuleAliasFact::StableModuleAliasFact(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}

zc::Maybe<StableModuleAliasFact> StableModuleAliasFact::from(
    StableSemanticImportQueryKey&& queryKey, StableScopeOwnerKey&& declaringScope,
    StableDefinitionQueryKey&& alias, identity::ModuleKey&& canonicalModule,
    ModuleAliasExportNamesRevision targetExportNamesRevision) {
  if (!sameModule(queryKey.requester(), moduleOf(declaringScope)) ||
      !sameModule(queryKey.requester(), alias.module()) ||
      queryKey.binding().operation() != identity::SemanticImportOperation::ModuleAlias ||
      queryKey.binding().sourceNamespace() != identity::DefinitionNamespace::Module ||
      queryKey.binding().localNamespace() != identity::DefinitionNamespace::Module) {
    return zc::none;
  }
  return StableModuleAliasFact(
      zc::heap<Impl>(Impl{zc::mv(queryKey), zc::mv(declaringScope), zc::mv(alias),
                          zc::mv(canonicalModule), targetExportNamesRevision}));
}

StableModuleAliasFact StableModuleAliasFact::clone() const {
  return StableModuleAliasFact(
      zc::heap<Impl>(Impl{impl->queryKey.clone(), impl->declaringScope.clone(), impl->alias.clone(),
                          impl->canonicalModule.clone(), impl->targetExportNamesRevision}));
}
const StableSemanticImportQueryKey& StableModuleAliasFact::queryKey() const noexcept {
  return impl->queryKey;
}
const StableScopeOwnerKey& StableModuleAliasFact::declaringScope() const noexcept {
  return impl->declaringScope;
}
const StableDefinitionQueryKey& StableModuleAliasFact::alias() const noexcept {
  return impl->alias;
}
const identity::ModuleKey& StableModuleAliasFact::canonicalModule() const noexcept {
  return impl->canonicalModule;
}
const ModuleAliasExportNamesRevision& StableModuleAliasFact::targetExportNamesRevision()
    const noexcept {
  return impl->targetExportNamesRevision;
}
bool StableModuleAliasFact::operator==(const StableModuleAliasFact& other) const {
  return impl->queryKey == other.impl->queryKey &&
         impl->declaringScope == other.impl->declaringScope && impl->alias == other.impl->alias &&
         sameModule(impl->canonicalModule, other.impl->canonicalModule) &&
         impl->targetExportNamesRevision.digest() == other.impl->targetExportNamesRevision.digest();
}

struct StableReexportStep::Impl final {
  identity::ModuleKey module;
  LocalSyntaxPath exportPath;
  StableBindingTargetKey binding;
  StableBindingTargetKey canonicalTarget;
};

StableReexportStep::~StableReexportStep() noexcept(false) = default;
StableReexportStep::StableReexportStep(StableReexportStep&&) noexcept = default;
StableReexportStep& StableReexportStep::operator=(StableReexportStep&&) noexcept = default;
StableReexportStep::StableReexportStep(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}

StableReexportStep StableReexportStep::from(identity::ModuleKey&& module,
                                            LocalSyntaxPath&& exportPath,
                                            StableBindingTargetKey&& binding,
                                            StableBindingTargetKey&& canonicalTarget) {
  return StableReexportStep(zc::heap<Impl>(
      Impl{zc::mv(module), zc::mv(exportPath), zc::mv(binding), zc::mv(canonicalTarget)}));
}
StableReexportStep StableReexportStep::clone() const {
  return from(impl->module.clone(), impl->exportPath.clone(), impl->binding.clone(),
              impl->canonicalTarget.clone());
}
const identity::ModuleKey& StableReexportStep::module() const noexcept { return impl->module; }
const LocalSyntaxPath& StableReexportStep::exportPath() const noexcept { return impl->exportPath; }
const StableBindingTargetKey& StableReexportStep::binding() const noexcept { return impl->binding; }
const StableBindingTargetKey& StableReexportStep::canonicalTarget() const noexcept {
  return impl->canonicalTarget;
}
bool StableReexportStep::operator==(const StableReexportStep& other) const {
  return sameModule(impl->module, other.impl->module) &&
         impl->exportPath == other.impl->exportPath && impl->binding == other.impl->binding &&
         impl->canonicalTarget == other.impl->canonicalTarget;
}

struct StableLocalExportFact::Impl final {
  identity::ModuleKey declaringModule;
  LocalSyntaxPath exportPath;
  BindingNameKey name;
  StableBindingTargetKey binding;
  StableBindingTargetKey canonicalTarget;
  zc::Maybe<MemberVisibility> visibility;
  CanonicalSequence<StableReexportStep> reexportChain;
};

StableLocalExportFact::~StableLocalExportFact() noexcept(false) = default;
StableLocalExportFact::StableLocalExportFact(StableLocalExportFact&&) noexcept = default;
StableLocalExportFact& StableLocalExportFact::operator=(StableLocalExportFact&&) noexcept = default;
StableLocalExportFact::StableLocalExportFact(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}

zc::Maybe<StableLocalExportFact> StableLocalExportFact::from(
    identity::ModuleKey&& declaringModule, LocalSyntaxPath&& exportPath, BindingNameKey&& name,
    StableBindingTargetKey&& binding, StableBindingTargetKey&& canonicalTarget,
    zc::Maybe<MemberVisibility>&& visibility,
    CanonicalSequence<StableReexportStep>&& reexportChain) {
  ZC_IF_SOME(value, visibility) {
    if (!inClosedRange(value, MemberVisibility::Public, MemberVisibility::Protected)) {
      return zc::none;
    }
  }
  return StableLocalExportFact(zc::heap<Impl>(
      Impl{zc::mv(declaringModule), zc::mv(exportPath), zc::mv(name), zc::mv(binding),
           zc::mv(canonicalTarget), zc::mv(visibility), zc::mv(reexportChain)}));
}
StableLocalExportFact StableLocalExportFact::clone() const {
  return ZC_ASSERT_NONNULL(from(impl->declaringModule.clone(), impl->exportPath.clone(),
                                impl->name.clone(), impl->binding.clone(),
                                impl->canonicalTarget.clone(), cloneMaybe(impl->visibility),
                                impl->reexportChain.clone()));
}
const identity::ModuleKey& StableLocalExportFact::declaringModule() const noexcept {
  return impl->declaringModule;
}
const LocalSyntaxPath& StableLocalExportFact::exportPath() const noexcept {
  return impl->exportPath;
}
const BindingNameKey& StableLocalExportFact::name() const noexcept { return impl->name; }
const StableBindingTargetKey& StableLocalExportFact::binding() const noexcept {
  return impl->binding;
}
const StableBindingTargetKey& StableLocalExportFact::canonicalTarget() const noexcept {
  return impl->canonicalTarget;
}
const zc::Maybe<MemberVisibility>& StableLocalExportFact::visibility() const noexcept {
  return impl->visibility;
}
const CanonicalSequence<StableReexportStep>& StableLocalExportFact::reexportChain() const noexcept {
  return impl->reexportChain;
}
bool StableLocalExportFact::operator==(const StableLocalExportFact& other) const {
  return sameModule(impl->declaringModule, other.impl->declaringModule) &&
         impl->exportPath == other.impl->exportPath &&
         impl->name.nameSpace() == other.impl->name.nameSpace() &&
         impl->name.name() == other.impl->name.name() && impl->binding == other.impl->binding &&
         impl->canonicalTarget == other.impl->canonicalTarget &&
         sameMaybe(impl->visibility, other.impl->visibility) &&
         impl->reexportChain == other.impl->reexportChain;
}

BinderQueryOwner::BinderQueryOwner(BinderQueryOwnerValue&& value) noexcept
    : valueField(zc::mv(value)) {}

#define ZOM_DEFINE_BINDER_QUERY_OWNER(Name, Variant, Field)                   \
  BinderQueryOwner BinderQueryOwner::Name(decltype(Variant::Field)&& value) { \
    return BinderQueryOwner(BinderQueryOwnerValue(Variant{zc::mv(value)}));   \
  }

ZOM_DEFINE_BINDER_QUERY_OWNER(module, BinderModuleQueryOwner, module)
ZOM_DEFINE_BINDER_QUERY_OWNER(definitionHeader, BinderDefinitionHeaderQueryOwner, definition)
ZOM_DEFINE_BINDER_QUERY_OWNER(implementationHeader, BinderImplementationHeaderQueryOwner,
                              implementation)
ZOM_DEFINE_BINDER_QUERY_OWNER(body, BinderBodyQueryOwner, body)

#undef ZOM_DEFINE_BINDER_QUERY_OWNER

BinderQueryOwner BinderQueryOwner::clone() const {
  if (valueField.is<BinderModuleQueryOwner>()) {
    return module(valueField.get<BinderModuleQueryOwner>().module.clone());
  }
  if (valueField.is<BinderDefinitionHeaderQueryOwner>()) {
    return definitionHeader(valueField.get<BinderDefinitionHeaderQueryOwner>().definition.clone());
  }
  if (valueField.is<BinderImplementationHeaderQueryOwner>()) {
    return implementationHeader(
        valueField.get<BinderImplementationHeaderQueryOwner>().implementation.clone());
  }
  return body(valueField.get<BinderBodyQueryOwner>().body.clone());
}

const BinderQueryOwnerValue& BinderQueryOwner::value() const noexcept { return valueField; }

bool BinderQueryOwner::operator==(const BinderQueryOwner& other) const {
#define ZOM_SAME_BINDER_QUERY_OWNER(Variant, Field)                                   \
  if (valueField.is<Variant>()) {                                                     \
    return other.valueField.is<Variant>() &&                                          \
           stable_binding_detail::sameElement(valueField.get<Variant>().Field,        \
                                              other.valueField.get<Variant>().Field); \
  }
  ZOM_SAME_BINDER_QUERY_OWNER(BinderModuleQueryOwner, module)
  ZOM_SAME_BINDER_QUERY_OWNER(BinderDefinitionHeaderQueryOwner, definition)
  ZOM_SAME_BINDER_QUERY_OWNER(BinderImplementationHeaderQueryOwner, implementation)
  ZOM_SAME_BINDER_QUERY_OWNER(BinderBodyQueryOwner, body)
#undef ZOM_SAME_BINDER_QUERY_OWNER
  ZC_UNREACHABLE
}

struct StableFailedLookupOutcome::Impl final {
  StableFailedLookupOutcomeValue value;
};

StableFailedLookupOutcome::~StableFailedLookupOutcome() noexcept(false) = default;
StableFailedLookupOutcome::StableFailedLookupOutcome(StableFailedLookupOutcome&&) noexcept =
    default;
StableFailedLookupOutcome& StableFailedLookupOutcome::operator=(
    StableFailedLookupOutcome&&) noexcept = default;
StableFailedLookupOutcome::StableFailedLookupOutcome(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}

StableFailedLookupOutcome StableFailedLookupOutcome::missing() {
  return StableFailedLookupOutcome(
      zc::heap<Impl>(Impl{StableFailedLookupOutcomeValue(StableMissingLookupOutcome{})}));
}
StableFailedLookupOutcome StableFailedLookupOutcome::namespaceMismatch(
    CanonicalNonEmptySequence<Namespace>&& availableNamespaces) {
  return StableFailedLookupOutcome(zc::heap<Impl>(Impl{StableFailedLookupOutcomeValue(
      StableNamespaceMismatchLookupOutcome{zc::mv(availableNamespaces)})}));
}
zc::Maybe<StableFailedLookupOutcome> StableFailedLookupOutcome::ambiguous(
    CanonicalNonEmptySequence<StableBindingTargetKey>&& candidates) {
  if (candidates.values().size() < 2) { return zc::none; }
  return StableFailedLookupOutcome(zc::heap<Impl>(
      Impl{StableFailedLookupOutcomeValue(StableAmbiguousLookupOutcome{zc::mv(candidates)})}));
}
StableFailedLookupOutcome StableFailedLookupOutcome::clone() const {
  if (impl->value.is<StableMissingLookupOutcome>()) { return missing(); }
  if (impl->value.is<StableNamespaceMismatchLookupOutcome>()) {
    return namespaceMismatch(
        impl->value.get<StableNamespaceMismatchLookupOutcome>().availableNamespaces.clone());
  }
  return ZC_ASSERT_NONNULL(
      ambiguous(impl->value.get<StableAmbiguousLookupOutcome>().candidates.clone()));
}
const StableFailedLookupOutcomeValue& StableFailedLookupOutcome::value() const noexcept {
  return impl->value;
}
bool StableFailedLookupOutcome::operator==(const StableFailedLookupOutcome& other) const {
  if (impl->value.is<StableMissingLookupOutcome>()) {
    return other.impl->value.is<StableMissingLookupOutcome>();
  }
  if (impl->value.is<StableNamespaceMismatchLookupOutcome>()) {
    return other.impl->value.is<StableNamespaceMismatchLookupOutcome>() &&
           impl->value.get<StableNamespaceMismatchLookupOutcome>().availableNamespaces ==
               other.impl->value.get<StableNamespaceMismatchLookupOutcome>().availableNamespaces;
  }
  return other.impl->value.is<StableAmbiguousLookupOutcome>() &&
         impl->value.get<StableAmbiguousLookupOutcome>().candidates ==
             other.impl->value.get<StableAmbiguousLookupOutcome>().candidates;
}

struct StableFailedLookupFact::Impl final {
  BinderQueryOwner owner;
  LocalSyntaxPath usePath;
  Namespace nameSpace;
  identity::DeclaredDefinitionName name;
  StableFailedLookupOutcome outcome;
};

StableFailedLookupFact::~StableFailedLookupFact() noexcept(false) = default;
StableFailedLookupFact::StableFailedLookupFact(StableFailedLookupFact&&) noexcept = default;
StableFailedLookupFact& StableFailedLookupFact::operator=(StableFailedLookupFact&&) noexcept =
    default;
StableFailedLookupFact::StableFailedLookupFact(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}

zc::Maybe<StableFailedLookupFact> StableFailedLookupFact::from(
    BinderQueryOwner&& owner, LocalSyntaxPath&& usePath, Namespace nameSpace,
    identity::DeclaredDefinitionName&& name, StableFailedLookupOutcome&& outcome) {
  if (!inClosedRange(nameSpace, Namespace::Value, Namespace::Attribute)) { return zc::none; }
  if (outcome.value().is<StableNamespaceMismatchLookupOutcome>()) {
    for (const auto available :
         outcome.value().get<StableNamespaceMismatchLookupOutcome>().availableNamespaces.values()) {
      if (available == nameSpace) { return zc::none; }
    }
  }
  return StableFailedLookupFact(zc::heap<Impl>(
      Impl{zc::mv(owner), zc::mv(usePath), nameSpace, zc::mv(name), zc::mv(outcome)}));
}
StableFailedLookupFact StableFailedLookupFact::clone() const {
  return ZC_ASSERT_NONNULL(from(impl->owner.clone(), impl->usePath.clone(), impl->nameSpace,
                                impl->name.clone(), impl->outcome.clone()));
}
const BinderQueryOwner& StableFailedLookupFact::owner() const noexcept { return impl->owner; }
const LocalSyntaxPath& StableFailedLookupFact::usePath() const noexcept { return impl->usePath; }
Namespace StableFailedLookupFact::nameSpace() const noexcept { return impl->nameSpace; }
const identity::DeclaredDefinitionName& StableFailedLookupFact::name() const noexcept {
  return impl->name;
}
const StableFailedLookupOutcome& StableFailedLookupFact::outcome() const noexcept {
  return impl->outcome;
}
bool StableFailedLookupFact::operator==(const StableFailedLookupFact& other) const {
  return impl->owner == other.impl->owner && impl->usePath == other.impl->usePath &&
         impl->nameSpace == other.impl->nameSpace && impl->name == other.impl->name &&
         impl->outcome == other.impl->outcome;
}

namespace {
class StableFactIndex final {
public:
  explicit StableFactIndex(size_t capacity) { positions.reserve(capacity); }
  bool add(zc::Array<uint8_t>&& key, size_t position) {
    if (positions.find(key.asPtr()) != zc::none) { return false; }
    positions.insert(zc::mv(key), position);
    return true;
  }
  zc::Maybe<size_t> find(zc::ArrayPtr<const uint8_t> key) const {
    ZC_IF_SOME(position, positions.find(key)) { return position; }
    return zc::none;
  }

private:
  zc::HashMap<zc::Array<uint8_t>, size_t> positions;
};
template <typename Key>
zc::Maybe<size_t> find(const StableFactIndex& index, const Key& key) {
  auto bytes = StableBindingCodec<Key>::encode(key);
  return index.find(bytes.asPtr());
}
zc::Maybe<size_t> findDefinition(const StableFactIndex& declarations,
                                 const identity::ModuleKey& module,
                                 const identity::DefinitionKey& definition) {
  auto key = StableDefinitionQueryKey::from(module.clone(), definition.clone());
  return find(declarations, key);
}
bool validScopeGraph(const identity::ModuleKey& module, zc::ArrayPtr<const StableScopeFact> scopes,
                     StableFactIndex& index) {
  bool hasModuleScope = false;
  for (size_t position = 0; position < scopes.size(); ++position) {
    const auto& scope = scopes[position];
    if (!sameModule(module, moduleOf(scope.owner())) ||
        scope.owner().value().is<StableBodyScope>() ||
        !index.add(StableBindingCodec<StableScopeOwnerKey>::encode(scope.owner()), position)) {
      return false;
    }
    hasModuleScope = hasModuleScope || scope.kind() == ScopeKind::Module;
  }
  zc::Vector<size_t> parents(scopes.size());
  zc::Vector<uint8_t> colors(scopes.size());
  for (const auto& scope : scopes) {
    ZC_IF_SOME(parent, scope.parent()) {
      ZC_IF_SOME(position, find(index, parent)) {
        parents.add(position);
      } else {
        return false;
      }
    } else {
      parents.add(scopes.size());
    }
    colors.add(0);
  }
  for (size_t start = 0; start < scopes.size(); ++start) {
    size_t cursor = start;
    while (cursor != scopes.size() && colors[cursor] == 0) {
      colors[cursor] = 1;
      cursor = parents[cursor];
    }
    if (cursor != scopes.size() && colors[cursor] == 1) { return false; }
    cursor = start;
    while (cursor != scopes.size() && colors[cursor] == 1) {
      colors[cursor] = 2;
      cursor = parents[cursor];
    }
  }
  return hasModuleScope;
}

bool validOwnerScopeGraph(const StableOwnerBodyQueryKey& owner,
                          zc::ArrayPtr<const StableBodyScopeFact> scopes, StableFactIndex& index,
                          zc::Vector<size_t>& entries, zc::Vector<size_t>& exits,
                          zc::Vector<size_t>& callableRoots) {
  const auto sentinel = scopes.size();
  for (size_t position = 0; position < scopes.size(); ++position) {
    const auto& scope = scopes[position];
    if (scope.owner() != owner ||
        !index.add(StableBindingCodec<StableScopeOwnerKey>::encode(scope.scope()), position))
      return false;
  }
  zc::Vector<size_t> parents, firstChildren, nextSiblings, pending;
  for (const auto& scope : scopes) {
    if (scope.parent().value().is<StableBodyScope>()) {
      ZC_IF_SOME(position, find(index, scope.parent())) {
        parents.add(position);
      } else {
        return false;
      }
    } else {
      parents.add(sentinel);
    }
    firstChildren.add(sentinel);
    nextSiblings.add(sentinel);
    entries.add(sentinel);
    exits.add(sentinel);
    callableRoots.add(sentinel);
  }
  for (size_t child = 0; child < sentinel; ++child) {
    const auto parent = parents[child];
    if (parent != sentinel) {
      nextSiblings[child] = firstChildren[parent];
      firstChildren[parent] = child;
    }
  }
  size_t clock = 0;
  for (size_t root = 0; root < sentinel; ++root) {
    if (parents[root] == sentinel) pending.add(root);
    while (pending.size() != 0) {
      const auto visit = pending.back();
      pending.removeLast();
      if (visit >= sentinel) {
        exits[visit - sentinel] = clock;
        continue;
      }
      const auto parent = parents[visit];
      const auto kind = scopes[visit].kind();
      entries[visit] = clock++;
      callableRoots[visit] =
          parent == sentinel || kind == ScopeKind::Function || kind == ScopeKind::Closure
              ? visit
              : callableRoots[parent];
      pending.add(visit + sentinel);
      for (size_t child = firstChildren[visit]; child != sentinel; child = nextSiblings[child])
        pending.add(child);
    }
  }
  return clock == sentinel;
}
zc::Array<uint8_t> pathNamespaceKey(const LocalSyntaxPath& path, Namespace nameSpace) {
  identity::CanonicalEncoder encoder;
  path.encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(nameSpace));
  return encoder.finish();
}
using BodyScopeFacts = CanonicalSequence<StableBodyScopeFact>;
using BodyNodeScopeFacts = CanonicalSequence<StableBodyNodeScopeFact>;
using LocalBindingFacts = CanonicalSequence<StableOwnerLocalBindingFact>;
using ResolutionFacts = CanonicalSequence<StableResolutionFact>;
using Members = CanonicalSequence<StableDeferredMemberFact>;
using SelfTypeFacts = CanonicalSequence<StableSelfTypeFact>;
using ThisBindingFacts = CanonicalSequence<StableThisBindingFact>;
using ShadowFacts = CanonicalSequence<StableShadowTargetFact>;
using LabelFacts = CanonicalSequence<StableLabelFact>;
using ControlFacts = CanonicalSequence<StableControlTransferFact>;
using ClosureFacts = CanonicalSequence<StableClosureFact>;
using FreeVariableFacts = CanonicalSequence<StableClosureFreeVariableFact>;
using CaptureFacts = CanonicalSequence<StableExplicitClosureCaptureFact>;
using FailedFacts = CanonicalSequence<StableFailedLookupFact>;
}  // namespace
struct BoundOwnerBody::Impl final {
  StableOwnerBodyQueryKey owner;
  CanonicalSequence<StableBodyScopeFact> scopes;
  CanonicalSequence<StableBodyNodeScopeFact> nodeScopes;
  CanonicalSequence<StableOwnerLocalBindingFact> bindings;
  CanonicalSequence<StableResolutionFact> resolutions;
  CanonicalSequence<StableDeferredMemberFact> deferredMembers;
  CanonicalSequence<StableSelfTypeFact> selfTypes;
  CanonicalSequence<StableThisBindingFact> thisBindings;
  CanonicalSequence<StableShadowTargetFact> shadowTargets;
  CanonicalSequence<StableLabelFact> labels;
  CanonicalSequence<StableControlTransferFact> controlTransfers;
  CanonicalSequence<StableClosureFact> closures;
  CanonicalSequence<StableClosureFreeVariableFact> closureFreeVariables;
  CanonicalSequence<StableExplicitClosureCaptureFact> explicitClosureCaptures;
  CanonicalSequence<StableFailedLookupFact> failedLookups;
  bool operator==(const Impl& other) const = default;
};
BoundOwnerBody::~BoundOwnerBody() noexcept(false) = default;
BoundOwnerBody::BoundOwnerBody(BoundOwnerBody&&) noexcept = default;
BoundOwnerBody& BoundOwnerBody::operator=(BoundOwnerBody&&) noexcept = default;
BoundOwnerBody::BoundOwnerBody(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
zc::Maybe<BoundOwnerBody> BoundOwnerBody::from(
    StableOwnerBodyQueryKey&& owner, BodyScopeFacts&& scopes, BodyNodeScopeFacts&& nodeScopes,
    LocalBindingFacts&& bindings, ResolutionFacts&& resolutions, Members&& deferredMembers,
    SelfTypeFacts&& selfTypes, ThisBindingFacts&& thisBindings, ShadowFacts&& shadowTargets,
    LabelFacts&& labels, ControlFacts&& controlTransfers, ClosureFacts&& closures,
    FreeVariableFacts&& closureFreeVariables, CaptureFacts&& explicitClosureCaptures,
    FailedFacts&& failedLookups) {
  StableFactIndex scopeIndex(scopes.values().size()), nodeIndex(nodeScopes.values().size()),
      bindingIndex(bindings.values().size());
  StableFactIndex lookupIndex(resolutions.values().size() + deferredMembers.values().size() +
                              failedLookups.values().size());
  StableFactIndex deferredIndex(deferredMembers.values().size()),
      selfIndex(selfTypes.values().size()), thisIndex(thisBindings.values().size());
  StableFactIndex shadowIndex(shadowTargets.values().size()), labelIndex(labels.values().size()),
      controlIndex(controlTransfers.values().size());
  StableFactIndex closureIndex(closures.values().size()),
      freeVariableIndex(closureFreeVariables.values().size()),
      captureIndex(explicitClosureCaptures.values().size()),
      failedIndex(failedLookups.values().size());
  zc::Vector<size_t> scopeEntries, scopeExits, callableRoots;
  if (!validOwnerScopeGraph(owner, scopes.values(), scopeIndex, scopeEntries, scopeExits,
                            callableRoots))
    return zc::none;
  auto containsScope = [&](size_t ancestor, size_t node) {
    if (ancestor >= scopeEntries.size() || node >= scopeEntries.size()) return false;
    return scopeEntries[ancestor] <= scopeEntries[node] &&
           scopeEntries[node] < scopeExits[ancestor];
  };
  for (size_t position = 0; position < nodeScopes.values().size(); ++position) {
    const auto& value = nodeScopes.values()[position];
    const auto scopePosition = find(scopeIndex, value.scope());
    if (value.owner() != owner ||
        (scopePosition == zc::none &&
         (value.scope().value().is<StableBodyScope>() || !isOwnedBodyParent(owner, value.scope()))))
      return zc::none;
    const auto indexedScope =
        scopePosition == zc::none ? scopes.values().size() : ZC_ASSERT_NONNULL(scopePosition);
    if (!nodeIndex.add(value.nodePath().encode(), indexedScope)) return zc::none;
  }
  auto covered = [&](const LocalSyntaxPath& path) { return find(nodeIndex, path) != zc::none; };
  for (const auto& value : scopes.values())
    if (!covered(value.scope().value().get<StableBodyScope>().path)) return zc::none;
  for (size_t position = 0; position < bindings.values().size(); ++position) {
    const auto& value = bindings.values()[position];
    const auto declaringScope = find(scopeIndex, value.declaringScope());
    if (value.owner() != owner || !covered(value.key().path()) ||
        (declaringScope == zc::none && (value.declaringScope().value().is<StableBodyScope>() ||
                                        !isOwnedBodyParent(owner, value.declaringScope()))) ||
        !bindingIndex.add(value.key().encode(), position))
      return zc::none;
  }
  for (size_t closurePosition = 0; closurePosition < closures.values().size(); ++closurePosition) {
    const auto& value = closures.values()[closurePosition];
    const auto scope = find(scopeIndex, value.scope());
    if (value.owner() != owner || !covered(value.closure().path()) || scope == zc::none ||
        !closureIndex.add(value.closure().path().encode(), closurePosition))
      return zc::none;
    ZC_IF_SOME(position, scope) {
      if (scopes.values()[position].kind() != ScopeKind::Closure) return zc::none;
    }
  }
  for (const auto& value : explicitClosureCaptures.values()) {
    auto path = value.closure().path().encode();
    if (value.owner() != owner || closureIndex.find(path.asPtr()) == zc::none ||
        !covered(value.captureListPath()) || !captureIndex.add(zc::mv(path), 0))
      return zc::none;
  }
  auto targetExists = [&](const StableBindingTargetKey& target, Namespace expected) {
    const auto& value = target.value();
    if (value.is<StableOwnerLocalBindingTarget>()) {
      const auto& local = value.get<StableOwnerLocalBindingTarget>();
      const auto encoded = local.binding.encode();
      const auto position = bindingIndex.find(encoded.asPtr());
      return local.owner == owner && position != zc::none &&
             bindings.values()[ZC_ASSERT_NONNULL(position)].nameSpace() == expected;
    }
    if (value.is<StableAnonymousOwnerBindingTarget>()) {
      const auto& local = value.get<StableAnonymousOwnerBindingTarget>();
      if (local.owner != owner || expected != Namespace::Value) return false;
      return local.binding.role() == AnonymousOwnerLocalRole::Closure
                 ? find(closureIndex, local.binding.path()) != zc::none
                 : find(captureIndex, local.binding.path()) != zc::none;
    }
    if (value.is<StableGenericParameterBindingTarget>()) return expected == Namespace::Type;
    if (value.is<StableCallableParameterBindingTarget>()) return expected == Namespace::Value;
    if (value.is<StableModuleBindingTarget>()) return expected == Namespace::Module;
    if (value.is<StableSemanticImportBindingTarget>())
      return expected ==
             static_cast<Namespace>(
                 value.get<StableSemanticImportBindingTarget>().import.binding().localNamespace());
    return true;
  };
  for (const auto& value : resolutions.values()) {
    if (value.owner() != owner || !covered(value.usePath()) ||
        !lookupIndex.add(pathNamespaceKey(value.usePath(), value.nameSpace()), 0) ||
        !targetExists(value.binding(), value.nameSpace()) ||
        !targetExists(value.canonicalTarget(), value.nameSpace()))
      return zc::none;
  }
  for (const auto& value : deferredMembers.values()) {
    if (value.owner() != owner || !covered(value.usePath()) || !covered(value.basePath()) ||
        !deferredIndex.add(value.usePath().encode(), 0))
      return zc::none;
    for (const auto& path : value.genericArgumentPaths().values())
      if (!covered(path)) return zc::none;
    for (const auto nameSpace : value.expectedNamespaces().values())
      if (!lookupIndex.add(pathNamespaceKey(value.usePath(), nameSpace), 0)) return zc::none;
  }
  for (const auto& value : selfTypes.values())
    if (value.owner() != owner || !covered(value.syntaxPath()) ||
        !selfIndex.add(value.syntaxPath().encode(), 0))
      return zc::none;
  for (const auto& value : thisBindings.values())
    if (value.owner() != owner || !covered(value.expressionPath()) ||
        !thisIndex.add(value.expressionPath().encode(), 0))
      return zc::none;
  for (size_t labelPosition = 0; labelPosition < labels.values().size(); ++labelPosition) {
    const auto& value = labels.values()[labelPosition];
    const auto target = find(scopeIndex, value.target().scope());
    if (value.key().owner() != owner || !covered(value.key().declarationPath()) ||
        !covered(value.statementPath()) || target == zc::none ||
        !labelIndex.add(value.key().declarationPath().encode(), labelPosition))
      return zc::none;
    ZC_IF_SOME(position, target) {
      const auto required =
          value.target().value().is<StableLoopLabelTarget>() ? ScopeKind::Loop : ScopeKind::Block;
      if (scopes.values()[position].kind() != required) return zc::none;
    }
  }
  for (const auto& value : closureFreeVariables.values()) {
    auto path = value.closure().path().encode();
    const auto closurePosition = closureIndex.find(path.asPtr());
    if (value.owner() != owner || closurePosition == zc::none ||
        !freeVariableIndex.add(zc::mv(path), 0))
      return zc::none;
    const auto scopePosition =
        find(scopeIndex, closures.values()[ZC_ASSERT_NONNULL(closurePosition)].scope());
    StableFactIndex targetIndex(value.variables().values().size());
    size_t referenceCount = 0;
    for (const auto& variable : value.variables().values())
      referenceCount += variable.referencePaths().values().size();
    StableFactIndex referenceIndex(referenceCount);
    for (const auto& variable : value.variables().values()) {
      if (!targetExists(variable.target(), Namespace::Value) ||
          !targetIndex.add(StableBindingCodec<StableBindingTargetKey>::encode(variable.target()),
                           0))
        return zc::none;
      for (const auto& reference : variable.referencePaths().values()) {
        const auto source = find(nodeIndex, reference);
        if (source == zc::none ||
            !containsScope(ZC_ASSERT_NONNULL(scopePosition), ZC_ASSERT_NONNULL(source)) ||
            !referenceIndex.add(reference.encode(), 0))
          return zc::none;
      }
    }
  }
  if (closureFreeVariables.values().size() != closures.values().size()) return zc::none;
  for (const auto& value : explicitClosureCaptures.values()) {
    StableFactIndex targetIndex(value.captures().values().size());
    for (const auto& capture : value.captures().values())
      if (!covered(capture.itemPath()) || !targetExists(capture.target(), Namespace::Value) ||
          !targetIndex.add(StableBindingCodec<StableBindingTargetKey>::encode(capture.target()), 0))
        return zc::none;
  }
  for (const auto& value : controlTransfers.values()) {
    if (value.owner() != owner || !covered(value.transferPath()) ||
        !controlIndex.add(value.transferPath().encode(), 0))
      return zc::none;
    const auto& target = value.target().value();
    size_t targetPosition;
    if (target.is<StableExplicitLabelControlTarget>()) {
      const auto labelPosition =
          find(labelIndex, target.get<StableExplicitLabelControlTarget>().label.declarationPath());
      if (labelPosition == zc::none) return zc::none;
      const auto& label = labels.values()[ZC_ASSERT_NONNULL(labelPosition)];
      if (value.kind() == ControlTransferKind::Continue &&
          !label.target().value().is<StableLoopLabelTarget>())
        return zc::none;
      targetPosition = ZC_ASSERT_NONNULL(find(scopeIndex, label.target().scope()));
    } else {
      const auto& scope = target.is<StableLoopControlTarget>()
                              ? target.get<StableLoopControlTarget>().scope
                              : target.get<StableMatchControlTarget>().scope;
      const auto position = find(scopeIndex, scope);
      const auto required =
          target.is<StableLoopControlTarget>() ? ScopeKind::Loop : ScopeKind::Match;
      if (position == zc::none || scopes.values()[ZC_ASSERT_NONNULL(position)].kind() != required)
        return zc::none;
      targetPosition = ZC_ASSERT_NONNULL(position);
    }
    const auto sourcePosition = find(nodeIndex, value.transferPath());
    if (sourcePosition == zc::none ||
        !containsScope(targetPosition, ZC_ASSERT_NONNULL(sourcePosition)) ||
        callableRoots[targetPosition] != callableRoots[ZC_ASSERT_NONNULL(sourcePosition)])
      return zc::none;
  }
  for (const auto& value : shadowTargets.values()) {
    const bool valueTargets = targetExists(value.binding(), Namespace::Value) &&
                              targetExists(value.shadowed(), Namespace::Value);
    const bool typeTargets = targetExists(value.binding(), Namespace::Type) &&
                             targetExists(value.shadowed(), Namespace::Type);
    if (value.owner() != owner ||
        !shadowIndex.add(StableBindingCodec<StableBindingTargetKey>::encode(value.binding()), 0) ||
        (!valueTargets && !typeTargets))
      return zc::none;
  }
  for (const auto& value : failedLookups.values()) {
    if (!value.owner().value().is<BinderBodyQueryOwner>() ||
        value.owner().value().get<BinderBodyQueryOwner>().body != owner ||
        !covered(value.usePath()) || !failedIndex.add(value.usePath().encode(), 0) ||
        !lookupIndex.add(pathNamespaceKey(value.usePath(), value.nameSpace()), 0))
      return zc::none;
    if (value.outcome().value().is<StableAmbiguousLookupOutcome>())
      for (const auto& candidate :
           value.outcome().value().get<StableAmbiguousLookupOutcome>().candidates.values())
        if (!targetExists(candidate, value.nameSpace())) return zc::none;
  }
  return BoundOwnerBody(zc::heap<Impl>(
      Impl{zc::mv(owner), zc::mv(scopes), zc::mv(nodeScopes), zc::mv(bindings), zc::mv(resolutions),
           zc::mv(deferredMembers), zc::mv(selfTypes), zc::mv(thisBindings), zc::mv(shadowTargets),
           zc::mv(labels), zc::mv(controlTransfers), zc::mv(closures), zc::mv(closureFreeVariables),
           zc::mv(explicitClosureCaptures), zc::mv(failedLookups)}));
}
BoundOwnerBody BoundOwnerBody::clone() const {
  return ZC_ASSERT_NONNULL(from(
      impl->owner.clone(), impl->scopes.clone(), impl->nodeScopes.clone(), impl->bindings.clone(),
      impl->resolutions.clone(), impl->deferredMembers.clone(), impl->selfTypes.clone(),
      impl->thisBindings.clone(), impl->shadowTargets.clone(), impl->labels.clone(),
      impl->controlTransfers.clone(), impl->closures.clone(), impl->closureFreeVariables.clone(),
      impl->explicitClosureCaptures.clone(), impl->failedLookups.clone()));
}
const StableOwnerBodyQueryKey& BoundOwnerBody::owner() const noexcept { return impl->owner; }
const BodyScopeFacts& BoundOwnerBody::scopes() const noexcept { return impl->scopes; }
const BodyNodeScopeFacts& BoundOwnerBody::nodeScopes() const noexcept { return impl->nodeScopes; }
const LocalBindingFacts& BoundOwnerBody::bindings() const noexcept { return impl->bindings; }
const ResolutionFacts& BoundOwnerBody::resolutions() const noexcept { return impl->resolutions; }
const Members& BoundOwnerBody::deferredMembers() const noexcept { return impl->deferredMembers; }
const SelfTypeFacts& BoundOwnerBody::selfTypes() const noexcept { return impl->selfTypes; }
const ThisBindingFacts& BoundOwnerBody::thisBindings() const noexcept { return impl->thisBindings; }
const ShadowFacts& BoundOwnerBody::shadowTargets() const noexcept { return impl->shadowTargets; }
const LabelFacts& BoundOwnerBody::labels() const noexcept { return impl->labels; }
const ControlFacts& BoundOwnerBody::controlTransfers() const noexcept {
  return impl->controlTransfers;
}
const ClosureFacts& BoundOwnerBody::closures() const noexcept { return impl->closures; }
const FreeVariableFacts& BoundOwnerBody::closureFreeVariables() const noexcept {
  return impl->closureFreeVariables;
}
const CaptureFacts& BoundOwnerBody::explicitClosureCaptures() const noexcept {
  return impl->explicitClosureCaptures;
}
const FailedFacts& BoundOwnerBody::failedLookups() const noexcept { return impl->failedLookups; }
bool BoundOwnerBody::operator==(const BoundOwnerBody& other) const { return *impl == *other.impl; }

namespace {
bool fitsDenseRange(uint32_t begin, uint32_t count) {
  return static_cast<uint64_t>(begin) + static_cast<uint64_t>(count) <=
         stable_binding_codec_detail::kDenseLocalAllocationCount;
}

bool advanceDenseRange(uint32_t begin, uint32_t count, uint32_t& cursor) {
  if (begin != cursor || !fitsDenseRange(begin, count)) { return false; }
  cursor = static_cast<uint32_t>(static_cast<uint64_t>(begin) + count);
  return true;
}
}  // namespace

struct OwnerAllocationRange::Impl final {
  StableOwnerBodyQueryKey owner;
  uint32_t scopeBegin;
  uint32_t scopeCount;
  uint32_t ownerLocalBegin;
  uint32_t ownerLocalCount;
  uint32_t anonymousBegin;
  uint32_t anonymousCount;
  uint32_t labelBegin;
  uint32_t labelCount;
};

OwnerAllocationRange::~OwnerAllocationRange() noexcept(false) = default;
OwnerAllocationRange::OwnerAllocationRange(OwnerAllocationRange&&) noexcept = default;
OwnerAllocationRange& OwnerAllocationRange::operator=(OwnerAllocationRange&&) noexcept = default;
OwnerAllocationRange::OwnerAllocationRange(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}

zc::Maybe<OwnerAllocationRange> OwnerAllocationRange::from(
    StableOwnerBodyQueryKey&& owner, uint32_t scopeBegin, uint32_t scopeCount,
    uint32_t ownerLocalBegin, uint32_t ownerLocalCount, uint32_t anonymousBegin,
    uint32_t anonymousCount, uint32_t labelBegin, uint32_t labelCount) {
  if (!fitsDenseRange(scopeBegin, scopeCount) ||
      !fitsDenseRange(ownerLocalBegin, ownerLocalCount) ||
      !fitsDenseRange(anonymousBegin, anonymousCount) || !fitsDenseRange(labelBegin, labelCount)) {
    return zc::none;
  }
  return OwnerAllocationRange(
      zc::heap<Impl>(Impl{zc::mv(owner), scopeBegin, scopeCount, ownerLocalBegin, ownerLocalCount,
                          anonymousBegin, anonymousCount, labelBegin, labelCount}));
}

OwnerAllocationRange OwnerAllocationRange::clone() const {
  return ZC_ASSERT_NONNULL(from(impl->owner.clone(), impl->scopeBegin, impl->scopeCount,
                                impl->ownerLocalBegin, impl->ownerLocalCount, impl->anonymousBegin,
                                impl->anonymousCount, impl->labelBegin, impl->labelCount));
}
const StableOwnerBodyQueryKey& OwnerAllocationRange::owner() const noexcept { return impl->owner; }
uint32_t OwnerAllocationRange::scopeBegin() const noexcept { return impl->scopeBegin; }
uint32_t OwnerAllocationRange::scopeCount() const noexcept { return impl->scopeCount; }
uint32_t OwnerAllocationRange::ownerLocalBegin() const noexcept { return impl->ownerLocalBegin; }
uint32_t OwnerAllocationRange::ownerLocalCount() const noexcept { return impl->ownerLocalCount; }
uint32_t OwnerAllocationRange::anonymousBegin() const noexcept { return impl->anonymousBegin; }
uint32_t OwnerAllocationRange::anonymousCount() const noexcept { return impl->anonymousCount; }
uint32_t OwnerAllocationRange::labelBegin() const noexcept { return impl->labelBegin; }
uint32_t OwnerAllocationRange::labelCount() const noexcept { return impl->labelCount; }
bool OwnerAllocationRange::operator==(const OwnerAllocationRange& other) const {
  return impl->owner == other.impl->owner && impl->scopeBegin == other.impl->scopeBegin &&
         impl->scopeCount == other.impl->scopeCount &&
         impl->ownerLocalBegin == other.impl->ownerLocalBegin &&
         impl->ownerLocalCount == other.impl->ownerLocalCount &&
         impl->anonymousBegin == other.impl->anonymousBegin &&
         impl->anonymousCount == other.impl->anonymousCount &&
         impl->labelBegin == other.impl->labelBegin && impl->labelCount == other.impl->labelCount;
}

struct ModuleBindingAllocationPlan::Impl final {
  identity::ModuleKey key;
  uint32_t skeletonScopeCount;
  uint32_t implementationOccurrenceCount;
  CanonicalSequence<OwnerAllocationRange> owners;
};

ModuleBindingAllocationPlan::~ModuleBindingAllocationPlan() noexcept(false) = default;
ModuleBindingAllocationPlan::ModuleBindingAllocationPlan(ModuleBindingAllocationPlan&&) noexcept =
    default;
ModuleBindingAllocationPlan& ModuleBindingAllocationPlan::operator=(
    ModuleBindingAllocationPlan&&) noexcept = default;
ModuleBindingAllocationPlan::ModuleBindingAllocationPlan(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}

zc::Maybe<ModuleBindingAllocationPlan> ModuleBindingAllocationPlan::from(
    identity::ModuleKey&& key, uint32_t skeletonScopeCount, uint32_t implementationOccurrenceCount,
    CanonicalSequence<OwnerAllocationRange>&& owners) {
  uint32_t scopeCursor = skeletonScopeCount;
  uint32_t ownerLocalCursor = 0;
  uint32_t anonymousCursor = 0;
  uint32_t labelCursor = 0;
  const auto values = owners.values();
  for (size_t index = 0; index < values.size(); ++index) {
    const auto& range = values[index];
    if (!sameModule(key, range.owner().module()) ||
        !advanceDenseRange(range.scopeBegin(), range.scopeCount(), scopeCursor) ||
        !advanceDenseRange(range.ownerLocalBegin(), range.ownerLocalCount(), ownerLocalCursor) ||
        !advanceDenseRange(range.anonymousBegin(), range.anonymousCount(), anonymousCursor) ||
        !advanceDenseRange(range.labelBegin(), range.labelCount(), labelCursor)) {
      return zc::none;
    }
    if (index != 0) {
      const auto previous = values[index - 1].owner().encodeCanonical();
      const auto current = range.owner().encodeCanonical();
      if (stable_binding_codec_detail::compareBytes(previous.asPtr(), current.asPtr()) >= 0) {
        return zc::none;
      }
    }
  }
  return ModuleBindingAllocationPlan(zc::heap<Impl>(
      Impl{zc::mv(key), skeletonScopeCount, implementationOccurrenceCount, zc::mv(owners)}));
}

ModuleBindingAllocationPlan ModuleBindingAllocationPlan::clone() const {
  return ZC_ASSERT_NONNULL(from(impl->key.clone(), impl->skeletonScopeCount,
                                impl->implementationOccurrenceCount, impl->owners.clone()));
}
const identity::ModuleKey& ModuleBindingAllocationPlan::key() const noexcept { return impl->key; }
uint32_t ModuleBindingAllocationPlan::skeletonScopeCount() const noexcept {
  return impl->skeletonScopeCount;
}
uint32_t ModuleBindingAllocationPlan::implementationOccurrenceCount() const noexcept {
  return impl->implementationOccurrenceCount;
}
const CanonicalSequence<OwnerAllocationRange>& ModuleBindingAllocationPlan::owners()
    const noexcept {
  return impl->owners;
}
bool ModuleBindingAllocationPlan::operator==(const ModuleBindingAllocationPlan& other) const {
  return sameModule(impl->key, other.impl->key) &&
         impl->skeletonScopeCount == other.impl->skeletonScopeCount &&
         impl->implementationOccurrenceCount == other.impl->implementationOccurrenceCount &&
         impl->owners == other.impl->owners;
}

struct BoundModuleSkeleton::Impl final {
  identity::ModuleKey module;
  CanonicalSequence<StableScopeFact> scopes;
  CanonicalSequence<StableNodeScopeFact> nodeScopes;
  CanonicalSequence<StableDeclarationFact> declarations;
  CanonicalSequence<StableImplementationOccurrenceFact> implementationOccurrences;
  CanonicalSequence<StableGenericParameterDeclarationFact> genericParameterDeclarations;
  CanonicalSequence<StableCallableParameterDeclarationFact> callableParameterDeclarations;
  CanonicalSequence<StableModuleAliasFact> moduleAliases;
  CanonicalSequence<StableImportFact> imports;
  CanonicalSequence<StableLocalExportFact> localExports;
  CanonicalNonEmptySequence<StableOwnerBodyQueryKey> bodyOwners;
  CanonicalSequence<StableFailedLookupFact> failedLookups;
};

BoundModuleSkeleton::~BoundModuleSkeleton() noexcept(false) = default;
BoundModuleSkeleton::BoundModuleSkeleton(BoundModuleSkeleton&&) noexcept = default;
BoundModuleSkeleton& BoundModuleSkeleton::operator=(BoundModuleSkeleton&&) noexcept = default;
BoundModuleSkeleton::BoundModuleSkeleton(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}

zc::Maybe<BoundModuleSkeleton> BoundModuleSkeleton::from(
    identity::ModuleKey&& module, CanonicalSequence<StableScopeFact>&& scopes,
    CanonicalSequence<StableNodeScopeFact>&& nodeScopes,
    CanonicalSequence<StableDeclarationFact>&& declarations,
    CanonicalSequence<StableImplementationOccurrenceFact>&& implementationOccurrences,
    CanonicalSequence<StableGenericParameterDeclarationFact>&& genericParameterDeclarations,
    CanonicalSequence<StableCallableParameterDeclarationFact>&& callableParameterDeclarations,
    CanonicalSequence<StableModuleAliasFact>&& moduleAliases,
    CanonicalSequence<StableImportFact>&& imports,
    CanonicalSequence<StableLocalExportFact>&& localExports,
    CanonicalNonEmptySequence<StableOwnerBodyQueryKey>&& bodyOwners,
    CanonicalSequence<StableFailedLookupFact>&& failedLookups) {
  StableFactIndex scopeIndex(scopes.values().size());
  StableFactIndex declarationIndex(declarations.values().size());
  StableFactIndex occurrenceIndex(implementationOccurrences.values().size());
  if (!validScopeGraph(module, scopes.values(), scopeIndex)) { return zc::none; }
  for (const auto& value : declarations.values()) {
    if (!sameModule(module, value.queryKey().module()) ||
        find(scopeIndex, value.declaringScope()) == zc::none ||
        !declarationIndex.add(value.queryKey().encodeCanonical(), 0))
      return zc::none;
  }
  for (const auto& value : implementationOccurrences.values()) {
    if (!sameModule(module, value.occurrence().module()) ||
        find(scopeIndex, value.declaringScope()) == zc::none ||
        !occurrenceIndex.add(value.occurrence().encodeCanonical(), 0))
      return zc::none;
  }
  bool hasModuleBodyOwner = false;
  for (const auto& value : bodyOwners.values()) {
    if (!sameModule(module, value.module())) { return zc::none; }
    if (value.owner().kind() == StableBodyOwnerKind::Module) {
      hasModuleBodyOwner = true;
    } else if (findDefinition(declarationIndex, module,
                              ZC_ASSERT_NONNULL(value.owner().definitionKey())) == zc::none)
      return zc::none;
  }
  if (!hasModuleBodyOwner) { return zc::none; }
  for (const auto& scope : scopes.values()) {
    const auto& owner = scope.owner().value();
    if (owner.is<StableDefinitionScope>() &&
        find(declarationIndex, owner.get<StableDefinitionScope>().definition) == zc::none)
      return zc::none;
    if (owner.is<StableImplementationOccurrenceScope>() &&
        find(occurrenceIndex, owner.get<StableImplementationOccurrenceScope>().occurrence) ==
            zc::none)
      return zc::none;
  }
  for (size_t position = 0; position < nodeScopes.values().size(); ++position) {
    const auto& value = nodeScopes.values()[position];
    if (!sameModule(module, moduleOf(value.root())) ||
        find(scopeIndex, value.scope()) == zc::none ||
        (position != 0 && value.root() == nodeScopes.values()[position - 1].root() &&
         value.nodePath() == nodeScopes.values()[position - 1].nodePath()))
      return zc::none;
  }
  for (size_t position = 0; position < genericParameterDeclarations.values().size(); ++position) {
    const auto& value = genericParameterDeclarations.values()[position];
    const auto& owner = value.record().owner();
    const bool definitionExists =
        owner.kind() == identity::StableGenericParameterOwnerKind::Definition &&
        findDefinition(declarationIndex, module, ZC_ASSERT_NONNULL(owner.definitionKey())) !=
            zc::none;
    bool implementationExists =
        owner.kind() == identity::StableGenericParameterOwnerKind::Implementation;
    if (implementationExists) {
      const auto& site = value.headerSite().value().get<ImplementationOccurrenceSite>().site;
      auto occurrence = ZC_ASSERT_NONNULL(
          StableImplementationOccurrenceQueryKey::from(module.clone(), site.clone()));
      implementationExists = find(occurrenceIndex, occurrence) != zc::none;
    }
    if (!sameModule(module, value.queryKey().module()) ||
        find(scopeIndex, value.declaringScope()) == zc::none ||
        (!definitionExists && !implementationExists) ||
        (position != 0 &&
         value.queryKey() == genericParameterDeclarations.values()[position - 1].queryKey() &&
         value.headerSite() == genericParameterDeclarations.values()[position - 1].headerSite()))
      return zc::none;
  }
  for (size_t position = 0; position < callableParameterDeclarations.values().size(); ++position) {
    const auto& value = callableParameterDeclarations.values()[position];
    if (!sameModule(module, value.queryKey().module()) ||
        find(scopeIndex, value.declaringScope()) == zc::none ||
        findDefinition(declarationIndex, module, value.record().owner()) == zc::none ||
        (position != 0 &&
         value.queryKey() == callableParameterDeclarations.values()[position - 1].queryKey()))
      return zc::none;
  }
  StableFactIndex importIndex(moduleAliases.values().size() + imports.values().size());
  for (const auto& value : moduleAliases.values()) {
    if (!sameModule(module, value.queryKey().requester()) ||
        find(scopeIndex, value.declaringScope()) == zc::none ||
        find(declarationIndex, value.alias()) == zc::none ||
        !importIndex.add(value.queryKey().encodeCanonical(), 0))
      return zc::none;
  }
  for (const auto& value : imports.values()) {
    if (!sameModule(module, value.queryKey().requester()) ||
        find(scopeIndex, value.declaringScope()) == zc::none ||
        !importIndex.add(value.queryKey().encodeCanonical(), 0))
      return zc::none;
  }
  for (size_t position = 0; position < localExports.values().size(); ++position) {
    const auto& value = localExports.values()[position];
    if (!sameModule(module, value.declaringModule()) ||
        (position != 0 && value.exportPath() == localExports.values()[position - 1].exportPath() &&
         value.name().nameSpace() == localExports.values()[position - 1].name().nameSpace() &&
         value.name().name() == localExports.values()[position - 1].name().name()))
      return zc::none;
  }
  for (size_t position = 0; position < failedLookups.values().size(); ++position) {
    const auto& value = failedLookups.values()[position];
    const auto& owner = value.owner().value();
    if (!sameModule(module, moduleOf(value.owner())) || owner.is<BinderBodyQueryOwner>() ||
        (position != 0 && value.owner() == failedLookups.values()[position - 1].owner() &&
         value.usePath() == failedLookups.values()[position - 1].usePath()))
      return zc::none;
    if (owner.is<BinderDefinitionHeaderQueryOwner>() &&
        find(declarationIndex, owner.get<BinderDefinitionHeaderQueryOwner>().definition) ==
            zc::none)
      return zc::none;
    if (owner.is<BinderImplementationHeaderQueryOwner>() &&
        find(occurrenceIndex, owner.get<BinderImplementationHeaderQueryOwner>().implementation) ==
            zc::none)
      return zc::none;
  }
  return BoundModuleSkeleton(zc::heap<Impl>(
      Impl{zc::mv(module), zc::mv(scopes), zc::mv(nodeScopes), zc::mv(declarations),
           zc::mv(implementationOccurrences), zc::mv(genericParameterDeclarations),
           zc::mv(callableParameterDeclarations), zc::mv(moduleAliases), zc::mv(imports),
           zc::mv(localExports), zc::mv(bodyOwners), zc::mv(failedLookups)}));
}
BoundModuleSkeleton BoundModuleSkeleton::clone() const {
  return ZC_ASSERT_NONNULL(
      from(impl->module.clone(), impl->scopes.clone(), impl->nodeScopes.clone(),
           impl->declarations.clone(), impl->implementationOccurrences.clone(),
           impl->genericParameterDeclarations.clone(), impl->callableParameterDeclarations.clone(),
           impl->moduleAliases.clone(), impl->imports.clone(), impl->localExports.clone(),
           impl->bodyOwners.clone(), impl->failedLookups.clone()));
}
const identity::ModuleKey& BoundModuleSkeleton::module() const noexcept { return impl->module; }
#define ZOM_DEFINE_SKELETON_ACCESSOR(Type, Name) \
  const Type& BoundModuleSkeleton::Name() const noexcept { return impl->Name; }
ZOM_DEFINE_SKELETON_ACCESSOR(CanonicalSequence<StableScopeFact>, scopes)
ZOM_DEFINE_SKELETON_ACCESSOR(CanonicalSequence<StableNodeScopeFact>, nodeScopes)
ZOM_DEFINE_SKELETON_ACCESSOR(CanonicalSequence<StableDeclarationFact>, declarations)
ZOM_DEFINE_SKELETON_ACCESSOR(CanonicalSequence<StableImplementationOccurrenceFact>,
                             implementationOccurrences)
ZOM_DEFINE_SKELETON_ACCESSOR(CanonicalSequence<StableGenericParameterDeclarationFact>,
                             genericParameterDeclarations)
ZOM_DEFINE_SKELETON_ACCESSOR(CanonicalSequence<StableCallableParameterDeclarationFact>,
                             callableParameterDeclarations)
ZOM_DEFINE_SKELETON_ACCESSOR(CanonicalSequence<StableModuleAliasFact>, moduleAliases)
ZOM_DEFINE_SKELETON_ACCESSOR(CanonicalSequence<StableImportFact>, imports)
ZOM_DEFINE_SKELETON_ACCESSOR(CanonicalSequence<StableLocalExportFact>, localExports)
ZOM_DEFINE_SKELETON_ACCESSOR(CanonicalNonEmptySequence<StableOwnerBodyQueryKey>, bodyOwners)
ZOM_DEFINE_SKELETON_ACCESSOR(CanonicalSequence<StableFailedLookupFact>, failedLookups)
#undef ZOM_DEFINE_SKELETON_ACCESSOR
bool BoundModuleSkeleton::operator==(const BoundModuleSkeleton& other) const {
  return sameModule(impl->module, other.impl->module) && impl->scopes == other.impl->scopes &&
         impl->nodeScopes == other.impl->nodeScopes &&
         impl->declarations == other.impl->declarations &&
         impl->implementationOccurrences == other.impl->implementationOccurrences &&
         impl->genericParameterDeclarations == other.impl->genericParameterDeclarations &&
         impl->callableParameterDeclarations == other.impl->callableParameterDeclarations &&
         impl->moduleAliases == other.impl->moduleAliases && impl->imports == other.impl->imports &&
         impl->localExports == other.impl->localExports &&
         impl->bodyOwners == other.impl->bodyOwners &&
         impl->failedLookups == other.impl->failedLookups;
}

bool isStableBindingValue(BinderKeyFailureKind value) noexcept {
  return inClosedRange(value, BinderKeyFailureKind::MissingSelectedModuleSource,
                       BinderKeyFailureKind::CrossBoundaryPath);
}

BinderKeyFailure::BinderKeyFailure(BinderKeyFailureKind kind, BinderQueryOwner&& owner,
                                   zc::Maybe<LocalSyntaxPath>&& path) noexcept
    : kindField(kind), ownerField(zc::mv(owner)), pathField(zc::mv(path)) {}

zc::Maybe<BinderKeyFailure> BinderKeyFailure::from(BinderKeyFailureKind kind,
                                                   BinderQueryOwner&& owner,
                                                   zc::Maybe<LocalSyntaxPath>&& path) {
  if (!isStableBindingValue(kind)) { return zc::none; }
  const bool requiresPath = kind >= BinderKeyFailureKind::BoundaryMismatch;
  if (requiresPath != (path != zc::none)) { return zc::none; }
  return BinderKeyFailure(kind, zc::mv(owner), zc::mv(path));
}

BinderKeyFailure BinderKeyFailure::clone() const {
  return BinderKeyFailure(kindField, ownerField.clone(), cloneMaybe(pathField));
}

BinderKeyFailureKind BinderKeyFailure::kind() const noexcept { return kindField; }
const BinderQueryOwner& BinderKeyFailure::owner() const noexcept { return ownerField; }
const zc::Maybe<LocalSyntaxPath>& BinderKeyFailure::path() const noexcept { return pathField; }

bool BinderKeyFailure::operator==(const BinderKeyFailure& other) const {
  return kindField == other.kindField && ownerField == other.ownerField &&
         sameMaybe(pathField, other.pathField);
}

struct StableHeaderGenericParameter::Impl final {
  identity::GenericParameterKey key;
  identity::GenericParameterIdentityRecord record;
  StableHeaderSite site;
  identity::DeclaredDefinitionName name;
  uint32_t ordinal;
};

StableHeaderGenericParameter::~StableHeaderGenericParameter() noexcept(false) = default;
StableHeaderGenericParameter::StableHeaderGenericParameter(
    StableHeaderGenericParameter&&) noexcept = default;
StableHeaderGenericParameter& StableHeaderGenericParameter::operator=(
    StableHeaderGenericParameter&&) noexcept = default;
StableHeaderGenericParameter::StableHeaderGenericParameter(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}

zc::Maybe<StableHeaderGenericParameter> StableHeaderGenericParameter::from(
    identity::GenericParameterKey&& key, identity::GenericParameterIdentityRecord&& record,
    StableHeaderSite&& site, identity::DeclaredDefinitionName&& name, uint32_t ordinal) {
  if (key != identity::GenericParameterKey::compute(record) || ordinal != record.ordinal()) {
    return zc::none;
  }
  const auto& owner = record.owner();
  if ((site.value().is<DefinitionAuthoritySite>() &&
       owner.kind() != identity::StableGenericParameterOwnerKind::Definition) ||
      (site.value().is<ImplementationOccurrenceSite>() &&
       (owner.kind() != identity::StableGenericParameterOwnerKind::Implementation ||
        ZC_ASSERT_NONNULL(owner.implKey()) !=
            site.value().get<ImplementationOccurrenceSite>().site.implementation()))) {
    return zc::none;
  }
  return StableHeaderGenericParameter(
      zc::heap<Impl>(Impl{zc::mv(key), zc::mv(record), zc::mv(site), zc::mv(name), ordinal}));
}

StableHeaderGenericParameter StableHeaderGenericParameter::clone() const {
  return StableHeaderGenericParameter(
      zc::heap<Impl>(Impl{impl->key.clone(), impl->record.clone(), impl->site.clone(),
                          impl->name.clone(), impl->ordinal}));
}

const identity::GenericParameterKey& StableHeaderGenericParameter::key() const noexcept {
  return impl->key;
}
const identity::GenericParameterIdentityRecord& StableHeaderGenericParameter::record()
    const noexcept {
  return impl->record;
}
const StableHeaderSite& StableHeaderGenericParameter::site() const noexcept { return impl->site; }
const identity::DeclaredDefinitionName& StableHeaderGenericParameter::name() const noexcept {
  return impl->name;
}
uint32_t StableHeaderGenericParameter::ordinal() const noexcept { return impl->ordinal; }

bool StableHeaderGenericParameter::operator==(const StableHeaderGenericParameter& other) const {
  return impl->key == other.impl->key &&
         stable_binding_detail::sameElement(impl->record, other.impl->record) &&
         impl->site == other.impl->site && impl->name == other.impl->name &&
         impl->ordinal == other.impl->ordinal;
}

struct StableHeaderCallableParameter::Impl final {
  identity::CallableParameterKey key;
  identity::CallableParameterIdentityRecord record;
  StableHeaderSite site;
  zc::Maybe<identity::DeclaredDefinitionName> name;
  identity::CallableParameterPosition position;
};

StableHeaderCallableParameter::~StableHeaderCallableParameter() noexcept(false) = default;
StableHeaderCallableParameter::StableHeaderCallableParameter(
    StableHeaderCallableParameter&&) noexcept = default;
StableHeaderCallableParameter& StableHeaderCallableParameter::operator=(
    StableHeaderCallableParameter&&) noexcept = default;
StableHeaderCallableParameter::StableHeaderCallableParameter(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}

zc::Maybe<StableHeaderCallableParameter> StableHeaderCallableParameter::from(
    identity::CallableParameterKey&& key, identity::CallableParameterIdentityRecord&& record,
    StableHeaderSite&& site, zc::Maybe<identity::DeclaredDefinitionName>&& name,
    identity::CallableParameterPosition position) {
  if (key != identity::CallableParameterKey::compute(record) ||
      !samePosition(position, record.position()) || !site.value().is<DefinitionAuthoritySite>()) {
    return zc::none;
  }
  const bool isReceiver = position.kind() == identity::CallableParameterPositionKind::Receiver;
  if ((isReceiver && name != zc::none) || (!isReceiver && name == zc::none)) { return zc::none; }
  return StableHeaderCallableParameter(
      zc::heap<Impl>(Impl{zc::mv(key), zc::mv(record), zc::mv(site), zc::mv(name), position}));
}

StableHeaderCallableParameter StableHeaderCallableParameter::clone() const {
  return StableHeaderCallableParameter(
      zc::heap<Impl>(Impl{impl->key.clone(), impl->record.clone(), impl->site.clone(),
                          cloneMaybe(impl->name), impl->position}));
}

const identity::CallableParameterKey& StableHeaderCallableParameter::key() const noexcept {
  return impl->key;
}
const identity::CallableParameterIdentityRecord& StableHeaderCallableParameter::record()
    const noexcept {
  return impl->record;
}
const StableHeaderSite& StableHeaderCallableParameter::site() const noexcept { return impl->site; }
const zc::Maybe<identity::DeclaredDefinitionName>& StableHeaderCallableParameter::name()
    const noexcept {
  return impl->name;
}
identity::CallableParameterPosition StableHeaderCallableParameter::position() const noexcept {
  return impl->position;
}

bool StableHeaderCallableParameter::operator==(const StableHeaderCallableParameter& other) const {
  return impl->key == other.impl->key &&
         stable_binding_detail::sameElement(impl->record, other.impl->record) &&
         impl->site == other.impl->site && sameMaybe(impl->name, other.impl->name) &&
         samePosition(impl->position, other.impl->position);
}

struct StableDefinitionHeader::Impl final {
  StableDefinitionQueryKey queryKey;
  identity::DefinitionIdentityRecord record;
  IdentitySyntaxSiteKey authoritySite;
  identity::DefinitionKind kind;
  Namespace nameSpace;
  identity::DeclaredDefinitionName name;
  DefinitionActivation activation;
  zc::Maybe<MemberVisibility> visibility;
  DefinitionBodyDisposition bodyDisposition;
  CanonicalSequence<StableHeaderGenericParameter> genericParameters;
  CanonicalSequence<StableHeaderCallableParameter> callableParameters;
  CanonicalSequence<ScopeRole> declaredScopeRoles;
};

StableDefinitionHeader::~StableDefinitionHeader() noexcept(false) = default;
StableDefinitionHeader::StableDefinitionHeader(StableDefinitionHeader&&) noexcept = default;
StableDefinitionHeader& StableDefinitionHeader::operator=(StableDefinitionHeader&&) noexcept =
    default;
StableDefinitionHeader::StableDefinitionHeader(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}

zc::Maybe<StableDefinitionHeader> StableDefinitionHeader::from(
    StableDefinitionQueryKey&& queryKey, identity::DefinitionIdentityRecord&& record,
    IdentitySyntaxSiteKey&& authoritySite, identity::DefinitionKind kind, Namespace nameSpace,
    identity::DeclaredDefinitionName&& name, DefinitionActivation activation,
    zc::Maybe<MemberVisibility>&& visibility, DefinitionBodyDisposition bodyDisposition,
    CanonicalSequence<StableHeaderGenericParameter>&& genericParameters,
    CanonicalSequence<StableHeaderCallableParameter>&& callableParameters,
    CanonicalSequence<ScopeRole>&& declaredScopeRoles) {
  if (!sameModule(queryKey.module(), record.module()) ||
      queryKey.definition() != identity::DefinitionKey::compute(record) ||
      !sameModule(queryKey.module(), authoritySite.module()) || kind != record.kind() ||
      static_cast<uint8_t>(nameSpace) != static_cast<uint8_t>(record.nameSpace()) ||
      name.text() != record.name() ||
      !inClosedRange(activation, DefinitionActivation::ModuleSkeleton,
                     DefinitionActivation::LoopPattern) ||
      !isStableBindingValue(bodyDisposition) ||
      !validGenericParameters(genericParameters.values(), queryKey.definition(), authoritySite) ||
      !validCallableParameters(callableParameters.values(), queryKey.definition(), authoritySite) ||
      !validScopeRoles(declaredScopeRoles.values())) {
    return zc::none;
  }
  ZC_IF_SOME(value, visibility) {
    if (!inClosedRange(value, MemberVisibility::Public, MemberVisibility::Protected)) {
      return zc::none;
    }
  }
  return StableDefinitionHeader(zc::heap<Impl>(
      Impl{zc::mv(queryKey), zc::mv(record), zc::mv(authoritySite), kind, nameSpace, zc::mv(name),
           activation, zc::mv(visibility), bodyDisposition, zc::mv(genericParameters),
           zc::mv(callableParameters), zc::mv(declaredScopeRoles)}));
}

StableDefinitionHeader StableDefinitionHeader::clone() const {
  return StableDefinitionHeader(zc::heap<Impl>(
      Impl{impl->queryKey.clone(), impl->record.clone(), impl->authoritySite.clone(), impl->kind,
           impl->nameSpace, impl->name.clone(), impl->activation, cloneMaybe(impl->visibility),
           impl->bodyDisposition, impl->genericParameters.clone(), impl->callableParameters.clone(),
           impl->declaredScopeRoles.clone()}));
}

const StableDefinitionQueryKey& StableDefinitionHeader::queryKey() const noexcept {
  return impl->queryKey;
}
const identity::DefinitionIdentityRecord& StableDefinitionHeader::record() const noexcept {
  return impl->record;
}
const IdentitySyntaxSiteKey& StableDefinitionHeader::authoritySite() const noexcept {
  return impl->authoritySite;
}
identity::DefinitionKind StableDefinitionHeader::kind() const noexcept { return impl->kind; }
Namespace StableDefinitionHeader::nameSpace() const noexcept { return impl->nameSpace; }
const identity::DeclaredDefinitionName& StableDefinitionHeader::name() const noexcept {
  return impl->name;
}
DefinitionActivation StableDefinitionHeader::activation() const noexcept {
  return impl->activation;
}
const zc::Maybe<MemberVisibility>& StableDefinitionHeader::visibility() const noexcept {
  return impl->visibility;
}
DefinitionBodyDisposition StableDefinitionHeader::bodyDisposition() const noexcept {
  return impl->bodyDisposition;
}
const CanonicalSequence<StableHeaderGenericParameter>& StableDefinitionHeader::genericParameters()
    const noexcept {
  return impl->genericParameters;
}
const CanonicalSequence<StableHeaderCallableParameter>& StableDefinitionHeader::callableParameters()
    const noexcept {
  return impl->callableParameters;
}
const CanonicalSequence<ScopeRole>& StableDefinitionHeader::declaredScopeRoles() const noexcept {
  return impl->declaredScopeRoles;
}

bool StableDefinitionHeader::operator==(const StableDefinitionHeader& other) const {
  return impl->queryKey == other.impl->queryKey &&
         stable_binding_detail::sameElement(impl->record, other.impl->record) &&
         impl->authoritySite.sameAs(other.impl->authoritySite) && impl->kind == other.impl->kind &&
         impl->nameSpace == other.impl->nameSpace && impl->name == other.impl->name &&
         impl->activation == other.impl->activation &&
         sameMaybe(impl->visibility, other.impl->visibility) &&
         impl->bodyDisposition == other.impl->bodyDisposition &&
         impl->genericParameters == other.impl->genericParameters &&
         impl->callableParameters == other.impl->callableParameters &&
         impl->declaredScopeRoles == other.impl->declaredScopeRoles;
}

struct StableImplementationOccurrenceHeader::Impl final {
  StableImplementationOccurrenceQueryKey queryKey;
  StableImplementationQueryKey authority;
  identity::ImplIdentityRecord record;
  CanonicalSequence<StableHeaderGenericParameter> genericParameters;
  CanonicalSequence<ScopeRole> declaredScopeRoles;
  ImplementationSourceForm sourceForm;
};

StableImplementationOccurrenceHeader::~StableImplementationOccurrenceHeader() noexcept(false) =
    default;
StableImplementationOccurrenceHeader::StableImplementationOccurrenceHeader(
    StableImplementationOccurrenceHeader&&) noexcept = default;
StableImplementationOccurrenceHeader& StableImplementationOccurrenceHeader::operator=(
    StableImplementationOccurrenceHeader&&) noexcept = default;
StableImplementationOccurrenceHeader::StableImplementationOccurrenceHeader(
    zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}

zc::Maybe<StableImplementationOccurrenceHeader> StableImplementationOccurrenceHeader::from(
    StableImplementationOccurrenceQueryKey&& queryKey, StableImplementationQueryKey&& authority,
    identity::ImplIdentityRecord&& record,
    CanonicalSequence<StableHeaderGenericParameter>&& genericParameters,
    CanonicalSequence<ScopeRole>&& declaredScopeRoles, ImplementationSourceForm sourceForm) {
  if (!sameModule(queryKey.module(), authority.module()) ||
      !sameModule(queryKey.module(), record.module()) ||
      queryKey.occurrence().implementation() != authority.implementation() ||
      authority.implementation() != identity::ImplKey::compute(record) ||
      !validImplementationGenericParameters(genericParameters.values(), authority.implementation(),
                                            queryKey.occurrence()) ||
      !validScopeRoles(declaredScopeRoles.values()) || !isStableBindingValue(sourceForm)) {
    return zc::none;
  }
  return StableImplementationOccurrenceHeader(
      zc::heap<Impl>(Impl{zc::mv(queryKey), zc::mv(authority), zc::mv(record),
                          zc::mv(genericParameters), zc::mv(declaredScopeRoles), sourceForm}));
}

StableImplementationOccurrenceHeader StableImplementationOccurrenceHeader::clone() const {
  return StableImplementationOccurrenceHeader(zc::heap<Impl>(
      Impl{impl->queryKey.clone(), impl->authority.clone(), impl->record.clone(),
           impl->genericParameters.clone(), impl->declaredScopeRoles.clone(), impl->sourceForm}));
}

const StableImplementationOccurrenceQueryKey& StableImplementationOccurrenceHeader::queryKey()
    const noexcept {
  return impl->queryKey;
}
const StableImplementationQueryKey& StableImplementationOccurrenceHeader::authority()
    const noexcept {
  return impl->authority;
}
const identity::ImplIdentityRecord& StableImplementationOccurrenceHeader::record() const noexcept {
  return impl->record;
}
const CanonicalSequence<StableHeaderGenericParameter>&
StableImplementationOccurrenceHeader::genericParameters() const noexcept {
  return impl->genericParameters;
}
const CanonicalSequence<ScopeRole>& StableImplementationOccurrenceHeader::declaredScopeRoles()
    const noexcept {
  return impl->declaredScopeRoles;
}
ImplementationSourceForm StableImplementationOccurrenceHeader::sourceForm() const noexcept {
  return impl->sourceForm;
}

bool StableImplementationOccurrenceHeader::operator==(
    const StableImplementationOccurrenceHeader& other) const {
  return impl->queryKey == other.impl->queryKey && impl->authority == other.impl->authority &&
         stable_binding_detail::sameElement(impl->record, other.impl->record) &&
         impl->genericParameters == other.impl->genericParameters &&
         impl->declaredScopeRoles == other.impl->declaredScopeRoles &&
         impl->sourceForm == other.impl->sourceForm;
}

}  // namespace zomlang::compiler::binder
