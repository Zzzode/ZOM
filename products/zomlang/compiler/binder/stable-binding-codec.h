// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zomlang/compiler/binder/stable-binding-facts.h"

namespace zomlang::compiler::binder {

template <typename T>
struct StableBindingCodec final {
  ZC_NODISCARD static zc::Array<uint8_t> encode(const T& value) { return value.encodeCanonical(); }
  ZC_NODISCARD static zc::Maybe<T> decode(zc::ArrayPtr<const uint8_t> bytes) {
    return T::decodeCanonical(bytes);
  }
};

#define ZOM_DECLARE_STABLE_BINDING_CODEC(Type)                                     \
  template <>                                                                      \
  struct StableBindingCodec<Type> final {                                          \
    ZC_NODISCARD static zc::Array<uint8_t> encode(const Type& value);              \
    ZC_NODISCARD static zc::Maybe<Type> decode(zc::ArrayPtr<const uint8_t> bytes); \
  }

ZOM_DECLARE_STABLE_BINDING_CODEC(DefinitionBodyDisposition);
ZOM_DECLARE_STABLE_BINDING_CODEC(ImplementationSourceForm);
ZOM_DECLARE_STABLE_BINDING_CODEC(ScopeRole);
ZOM_DECLARE_STABLE_BINDING_CODEC(ScopeKind);
ZOM_DECLARE_STABLE_BINDING_CODEC(Namespace);
ZOM_DECLARE_STABLE_BINDING_CODEC(BindingNameKey);
ZOM_DECLARE_STABLE_BINDING_CODEC(MemberVisibility);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableHeaderSite);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableHeaderGenericParameter);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableHeaderCallableParameter);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableDefinitionHeader);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableImplementationOccurrenceHeader);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableScopeOwnerKey);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableNodeSyntaxRoot);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableScopeFact);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableNodeScopeFact);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableBodyScopeFact);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableBodyNodeScopeFact);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableOwnerLocalBindingFact);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableResolutionFact);
ZOM_DECLARE_STABLE_BINDING_CODEC(LocalSyntaxPath);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableDeferredMemberFact);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableSelfOwner);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableSelfTypeFact);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableThisBindingFact);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableShadowTargetFact);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableLabelKey);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableLabelTarget);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableLabelFact);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableControlTarget);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableControlTransferFact);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableClosureFact);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableClosureFreeVariable);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableClosureFreeVariableFact);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableExplicitCaptureMode);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableExplicitCaptureBindingFact);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableExplicitClosureCaptureFact);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableDeclarationFact);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableImplementationOccurrenceFact);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableGenericParameterDeclarationFact);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableCallableParameterDeclarationFact);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableBindingTargetKey);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableImportFact);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableModuleAliasFact);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableReexportStep);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableLocalExportFact);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableFailedLookupOutcome);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableFailedLookupFact);
ZOM_DECLARE_STABLE_BINDING_CODEC(BoundOwnerBody);
ZOM_DECLARE_STABLE_BINDING_CODEC(ModuleBindingAllocationPlan);
ZOM_DECLARE_STABLE_BINDING_CODEC(OwnerAllocationRange);
ZOM_DECLARE_STABLE_BINDING_CODEC(BoundModuleSkeleton);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableExportedBinding);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableExportedBindingQueryKey);
ZOM_DECLARE_STABLE_BINDING_CODEC(StableScopeNameBucketQueryKey);
ZOM_DECLARE_STABLE_BINDING_CODEC(CanonicalSequence<BindingNameKey>);
ZOM_DECLARE_STABLE_BINDING_CODEC(CanonicalSequence<StableImplementationOccurrenceFact>);
ZOM_DECLARE_STABLE_BINDING_CODEC(CanonicalSequence<StableBindingTargetKey>);
ZOM_DECLARE_STABLE_BINDING_CODEC(BinderKeyFailureKind);
ZOM_DECLARE_STABLE_BINDING_CODEC(BinderQueryOwner);
ZOM_DECLARE_STABLE_BINDING_CODEC(BinderKeyFailure);

#undef ZOM_DECLARE_STABLE_BINDING_CODEC

template <typename T>
zc::StringPtr binderQueryResultDomain();
template <>
inline zc::StringPtr binderQueryResultDomain<StableDefinitionHeader>() {
  return "zom.binder.result-definition-header-syntax"_zc;
}
template <>
inline zc::StringPtr binderQueryResultDomain<StableImplementationOccurrenceHeader>() {
  return "zom.binder.result-implementation-occurrence-header-syntax"_zc;
}
template <>
inline zc::StringPtr binderQueryResultDomain<BoundModuleSkeleton>() {
  return "zom.binder.result-bind-module-skeleton"_zc;
}
template <>
inline zc::StringPtr binderQueryResultDomain<BoundOwnerBody>() {
  return "zom.binder.result-bind-owner-body"_zc;
}
template <>
inline zc::StringPtr binderQueryResultDomain<ModuleBindingAllocationPlan>() {
  return "zom.binder.result-module-binding-allocation-plan"_zc;
}

template <typename T>
struct StableBindingCodec<BinderQueryResult<T>> final {
  ZC_NODISCARD static zc::Array<uint8_t> encode(const BinderQueryResult<T>& value);
  ZC_NODISCARD static zc::Maybe<BinderQueryResult<T>> decode(zc::ArrayPtr<const uint8_t> bytes);
};

namespace stable_binding_codec_detail {

#define ZOM_STABLE_BINDING_BOUND(Name, Limit, Rule) inline constexpr uint64_t k##Name = Limit;
#include "zomlang/compiler/binder/stable-binding-schema.def"
#undef ZOM_STABLE_BINDING_BOUND

inline constexpr diagnostics::DiagnosticFactCodecLimits kBinderDiagnosticLimits{
    kDiagnosticFactsPerResult, kDiagnosticPayloadBytes, 3, 64 * 1024 * 1024, 128};

inline int compareBytes(zc::ArrayPtr<const uint8_t> left,
                        zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] < right[index]) { return -1; }
    if (left[index] > right[index]) { return 1; }
  }
  if (left.size() < right.size()) { return -1; }
  if (left.size() > right.size()) { return 1; }
  return 0;
}

template <typename T>
bool isCanonical(zc::ArrayPtr<const T> values) {
  if (values.size() > kBinderSemanticSequenceRecords) { return false; }
  if (values.size() == 0) { return true; }
  auto previous = StableBindingCodec<T>::encode(values[0]);
  if (previous.size() == 0) { return false; }
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = StableBindingCodec<T>::encode(values[index]);
    if (current.size() == 0 || compareBytes(previous.asPtr(), current.asPtr()) >= 0) {
      return false;
    }
    previous = zc::mv(current);
  }
  return true;
}

}  // namespace stable_binding_codec_detail

template <typename T>
class StableBindingSequenceBuilder final {
public:
  ZC_NODISCARD static zc::Maybe<CanonicalSequence<T>> from(zc::Vector<T>&& values) {
    if (!stable_binding_codec_detail::isCanonical<T>(values.asPtr().asConst())) { return zc::none; }
    return CanonicalSequence<T>(zc::mv(values));
  }

  ZC_NODISCARD static zc::Maybe<CanonicalNonEmptySequence<T>> fromNonEmpty(zc::Vector<T>&& values) {
    if (values.size() == 0 ||
        !stable_binding_codec_detail::isCanonical<T>(values.asPtr().asConst())) {
      return zc::none;
    }
    return CanonicalNonEmptySequence<T>(zc::mv(values));
  }
};

template <>
class StableBindingSequenceBuilder<diagnostics::DiagnosticFact> final {
public:
  ZC_NODISCARD static zc::Maybe<CanonicalSequence<diagnostics::DiagnosticFact>> from(
      zc::Vector<diagnostics::DiagnosticFact>&& values) {
    if (!admitted(values.asPtr())) { return zc::none; }
    return CanonicalSequence<diagnostics::DiagnosticFact>(zc::mv(values));
  }
  ZC_NODISCARD static zc::Maybe<CanonicalNonEmptySequence<diagnostics::DiagnosticFact>>
  fromNonEmpty(zc::Vector<diagnostics::DiagnosticFact>&& values) {
    if (values.size() == 0 || !admitted(values.asPtr())) { return zc::none; }
    return CanonicalNonEmptySequence<diagnostics::DiagnosticFact>(zc::mv(values));
  }

private:
  static bool admitted(zc::ArrayPtr<const diagnostics::DiagnosticFact> values) {
    auto encoded = diagnostics::encodeDiagnosticFacts(
        zc::none, values, stable_binding_codec_detail::kBinderDiagnosticLimits);
    ZC_IF_SOME(bytes, encoded) {
      auto decoded = diagnostics::decodeDiagnosticFacts(
          zc::none, bytes.asPtr(), stable_binding_codec_detail::kBinderDiagnosticLimits);
      ZC_IF_SOME(facts, decoded) {
        if (!stable_binding_detail::sameElements(values, facts.asPtr().asConst())) { return false; }
        auto reencoded = diagnostics::encodeDiagnosticFacts(
            zc::none, facts.asPtr().asConst(),
            stable_binding_codec_detail::kBinderDiagnosticLimits);
        ZC_IF_SOME(canonicalBytes, reencoded) { return canonicalBytes.asPtr() == bytes.asPtr(); }
        return false;
      }
    }
    return false;
  }
};

}  // namespace zomlang::compiler::binder
