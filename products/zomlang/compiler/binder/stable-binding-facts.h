// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/binder/binding-metadata.h"
#include "zomlang/compiler/binder/identity-pre-admission.h"
#include "zomlang/compiler/binder/local-identity.h"
#include "zomlang/compiler/diagnostics/fact/diagnostic-fact.h"
#include "zomlang/compiler/identity/key/definition-key.h"
#include "zomlang/compiler/identity/key/import-binding-key.h"
#include "zomlang/compiler/identity/key/source-key.h"

namespace zomlang::compiler::binder {

template <typename T>
class StableBindingSequenceBuilder;

namespace stable_binding_detail {

inline bool sameElement(const BindingNameKey& left, const BindingNameKey& right) {
  return left.nameSpace() == right.nameSpace() && left.name() == right.name();
}

template <typename T>
T cloneElement(const T& value) {
  if constexpr (requires { value.clone(); }) {
    return value.clone();
  } else {
    return value;
  }
}

template <typename T>
bool sameElement(const T& left, const T& right) {
  if constexpr (requires { left == right; }) {
    return left == right;
  } else if constexpr (requires { left.sameAs(right); }) {
    return left.sameAs(right);
  } else if constexpr (requires { left.encode(); }) {
    return left.encode().asPtr() == right.encode().asPtr();
  } else {
    static_assert(sizeof(T) == 0, "canonical sequence elements require semantic equality");
  }
}

template <typename T>
zc::Vector<T> cloneElements(zc::ArrayPtr<const T> values) {
  zc::Vector<T> result(values.size());
  for (const auto& value : values) { result.add(cloneElement(value)); }
  return result;
}

template <typename T>
bool sameElements(zc::ArrayPtr<const T> left, zc::ArrayPtr<const T> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (!sameElement(left[index], right[index])) { return false; }
  }
  return true;
}

}  // namespace stable_binding_detail

/// \brief Move-only sequence admitted in canonical element order.
template <typename T>
class CanonicalSequence final {
public:
  CanonicalSequence(CanonicalSequence&&) noexcept = default;
  CanonicalSequence& operator=(CanonicalSequence&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalSequence);

  ZC_NODISCARD static CanonicalSequence empty() { return CanonicalSequence(zc::Vector<T>()); }
  ZC_NODISCARD CanonicalSequence clone() const {
    return CanonicalSequence(stable_binding_detail::cloneElements(valuesField.asPtr()));
  }
  ZC_NODISCARD zc::ArrayPtr<const T> values() const ZC_LIFETIMEBOUND { return valuesField.asPtr(); }
  bool operator==(const CanonicalSequence& other) const {
    return stable_binding_detail::sameElements(values(), other.values());
  }
  bool operator!=(const CanonicalSequence& other) const { return !(*this == other); }

private:
  explicit CanonicalSequence(zc::Vector<T>&& values) noexcept : valuesField(zc::mv(values)) {}

  zc::Vector<T> valuesField;
  friend class StableBindingSequenceBuilder<T>;
};

/// \brief Move-only nonempty sequence admitted in canonical element order.
template <typename T>
class CanonicalNonEmptySequence final {
public:
  CanonicalNonEmptySequence(CanonicalNonEmptySequence&&) noexcept = default;
  CanonicalNonEmptySequence& operator=(CanonicalNonEmptySequence&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalNonEmptySequence);

  ZC_NODISCARD CanonicalNonEmptySequence clone() const {
    return CanonicalNonEmptySequence(stable_binding_detail::cloneElements(valuesField.asPtr()));
  }
  ZC_NODISCARD zc::ArrayPtr<const T> values() const ZC_LIFETIMEBOUND { return valuesField.asPtr(); }
  bool operator==(const CanonicalNonEmptySequence& other) const {
    return stable_binding_detail::sameElements(values(), other.values());
  }
  bool operator!=(const CanonicalNonEmptySequence& other) const { return !(*this == other); }

private:
  explicit CanonicalNonEmptySequence(zc::Vector<T>&& values) noexcept
      : valuesField(zc::mv(values)) {}

  zc::Vector<T> valuesField;
  friend class StableBindingSequenceBuilder<T>;
};

#define ZOM_DECLARE_STABLE_ROUTED_KEY(Name, OwnerType, OwnerName, KeyType, KeyName, ResultType) \
  class Name final {                                                                            \
  public:                                                                                       \
    Name(Name&&) noexcept = default;                                                            \
    Name& operator=(Name&&) noexcept = default;                                                 \
    ZC_DISALLOW_COPY(Name);                                                                     \
    ZC_NODISCARD static ResultType from(OwnerType&& OwnerName, KeyType&& KeyName);              \
    ZC_NODISCARD Name clone() const;                                                            \
    ZC_NODISCARD const OwnerType& OwnerName() const noexcept;                                   \
    ZC_NODISCARD const KeyType& KeyName() const noexcept;                                       \
    ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;                                    \
    ZC_NODISCARD static zc::Maybe<Name> decodeCanonical(zc::ArrayPtr<const uint8_t> bytes);     \
    bool operator==(const Name& other) const;                                                   \
    bool operator!=(const Name& other) const { return !(*this == other); }                      \
                                                                                                \
  private:                                                                                      \
    Name(OwnerType&& OwnerName, KeyType&& KeyName) noexcept;                                    \
    OwnerType OwnerName##Field;                                                                 \
    KeyType KeyName##Field;                                                                     \
  }

ZOM_DECLARE_STABLE_ROUTED_KEY(StableDefinitionQueryKey, identity::ModuleKey, module,
                              identity::DefinitionKey, definition, StableDefinitionQueryKey);
ZOM_DECLARE_STABLE_ROUTED_KEY(StableImplementationQueryKey, identity::ModuleKey, module,
                              identity::ImplKey, implementation, StableImplementationQueryKey);
ZOM_DECLARE_STABLE_ROUTED_KEY(StableImplementationOccurrenceQueryKey, identity::ModuleKey, module,
                              ImplSourceOccurrenceKey, occurrence,
                              zc::Maybe<StableImplementationOccurrenceQueryKey>);
ZOM_DECLARE_STABLE_ROUTED_KEY(StableGenericParameterQueryKey, identity::ModuleKey, module,
                              identity::GenericParameterKey, parameter,
                              StableGenericParameterQueryKey);
ZOM_DECLARE_STABLE_ROUTED_KEY(StableCallableParameterQueryKey, identity::ModuleKey, module,
                              identity::CallableParameterKey, parameter,
                              StableCallableParameterQueryKey);
ZOM_DECLARE_STABLE_ROUTED_KEY(StableSemanticImportQueryKey, identity::ModuleKey, requester,
                              identity::ImportBindingKey, binding,
                              zc::Maybe<StableSemanticImportQueryKey>);
ZOM_DECLARE_STABLE_ROUTED_KEY(StableOwnerBodyQueryKey, identity::ModuleKey, module,
                              StableBodyOwnerKey, owner, zc::Maybe<StableOwnerBodyQueryKey>);

#undef ZOM_DECLARE_STABLE_ROUTED_KEY

/// \brief Stable module and binding name for one exported-binding projection.
class StableExportedBindingQueryKey final {
public:
  StableExportedBindingQueryKey(StableExportedBindingQueryKey&&) noexcept = default;
  StableExportedBindingQueryKey& operator=(StableExportedBindingQueryKey&&) noexcept = default;
  ZC_DISALLOW_COPY(StableExportedBindingQueryKey);
  ZC_NODISCARD static StableExportedBindingQueryKey from(identity::ModuleKey&& module,
                                                         BindingNameKey&& name);
  ZC_NODISCARD StableExportedBindingQueryKey clone() const;
  ZC_NODISCARD const identity::ModuleKey& module() const noexcept;
  ZC_NODISCARD const BindingNameKey& name() const noexcept;
  bool operator==(const StableExportedBindingQueryKey& other) const;
  bool operator!=(const StableExportedBindingQueryKey& other) const { return !(*this == other); }

private:
  StableExportedBindingQueryKey(identity::ModuleKey&& module, BindingNameKey&& name) noexcept;
  identity::ModuleKey moduleField;
  BindingNameKey nameField;
};

enum class DefinitionBodyDisposition : uint8_t { NoExecutableBody = 0x01, ExecutableBody = 0x02 };
enum class ImplementationSourceForm : uint8_t { Ordinary = 0x01, BodylessMarker = 0x02 };
enum class StableExplicitCaptureMode : uint8_t { ByValue = 0x01, ByReference = 0x02, This = 0x03 };
enum class ScopeRole : uint8_t {
  Declaration = 0x01,
  Generic = 0x02,
  Parameters = 0x03,
  Members = 0x04,
  Implementation = 0x05
};

ZC_NODISCARD bool isStableBindingValue(DefinitionBodyDisposition value) noexcept;
ZC_NODISCARD bool isStableBindingValue(ImplementationSourceForm value) noexcept;
ZC_NODISCARD bool isStableBindingValue(ScopeRole value) noexcept;
ZC_NODISCARD bool isStableBindingValue(ScopeKind value) noexcept;
ZC_NODISCARD bool isStableBindingValue(ControlTransferKind value) noexcept;
ZC_NODISCARD bool isStableBindingValue(StableExplicitCaptureMode value) noexcept;

struct DefinitionAuthoritySite final {
  IdentitySyntaxSiteKey site;
};
struct ImplementationOccurrenceSite final {
  ImplSourceOccurrenceKey site;
};
using StableHeaderSiteValue = zc::OneOf<DefinitionAuthoritySite, ImplementationOccurrenceSite>;

/// \brief Stable syntax authority for one definition or implementation header.
class StableHeaderSite final {
public:
  StableHeaderSite(StableHeaderSite&&) noexcept = default;
  StableHeaderSite& operator=(StableHeaderSite&&) noexcept = default;
  ZC_DISALLOW_COPY(StableHeaderSite);
  ZC_NODISCARD static StableHeaderSite definition(IdentitySyntaxSiteKey&& site);
  ZC_NODISCARD static StableHeaderSite implementation(ImplSourceOccurrenceKey&& site);
  ZC_NODISCARD StableHeaderSite clone() const;
  ZC_NODISCARD const StableHeaderSiteValue& value() const noexcept;
  bool operator==(const StableHeaderSite& other) const;
  bool operator!=(const StableHeaderSite& other) const { return !(*this == other); }

private:
  explicit StableHeaderSite(StableHeaderSiteValue&& value) noexcept;
  StableHeaderSiteValue valueField;
};

struct StableModuleScope final {
  identity::ModuleKey module;
};
struct StableDefinitionScope final {
  StableDefinitionQueryKey definition;
  ScopeRole role;
};
struct StableImplementationOccurrenceScope final {
  StableImplementationOccurrenceQueryKey occurrence;
  ScopeRole role;
};
struct StableBodyScope final {
  StableOwnerBodyQueryKey owner;
  LocalSyntaxPath path;
};
using StableScopeOwnerValue = zc::OneOf<StableModuleScope, StableDefinitionScope,
                                        StableImplementationOccurrenceScope, StableBodyScope>;
/// \brief Closed stable owner algebra for module, header, and body scopes.
class StableScopeOwnerKey final {
public:
  StableScopeOwnerKey(StableScopeOwnerKey&&) noexcept = default;
  StableScopeOwnerKey& operator=(StableScopeOwnerKey&&) noexcept = default;
  ZC_DISALLOW_COPY(StableScopeOwnerKey);
  ZC_NODISCARD static StableScopeOwnerKey module(identity::ModuleKey&& module);
  ZC_NODISCARD static zc::Maybe<StableScopeOwnerKey> definition(
      StableDefinitionQueryKey&& definition, ScopeRole role);
  ZC_NODISCARD static zc::Maybe<StableScopeOwnerKey> implementationOccurrence(
      StableImplementationOccurrenceQueryKey&& occurrence, ScopeRole role);
  ZC_NODISCARD static StableScopeOwnerKey body(StableOwnerBodyQueryKey&& owner,
                                               LocalSyntaxPath&& path);
  ZC_NODISCARD StableScopeOwnerKey clone() const;
  ZC_NODISCARD const StableScopeOwnerValue& value() const noexcept;
  bool operator==(const StableScopeOwnerKey& other) const;
  bool operator!=(const StableScopeOwnerKey& other) const { return !(*this == other); }

private:
  explicit StableScopeOwnerKey(StableScopeOwnerValue&& value) noexcept;
  StableScopeOwnerValue valueField;
};

/// \brief Stable non-body scope and binding name for one scope-bucket projection.
class StableScopeNameBucketQueryKey final {
public:
  StableScopeNameBucketQueryKey(StableScopeNameBucketQueryKey&&) noexcept = default;
  StableScopeNameBucketQueryKey& operator=(StableScopeNameBucketQueryKey&&) noexcept = default;
  ZC_DISALLOW_COPY(StableScopeNameBucketQueryKey);
  ZC_NODISCARD static zc::Maybe<StableScopeNameBucketQueryKey> from(StableScopeOwnerKey&& scope,
                                                                    BindingNameKey&& name);
  ZC_NODISCARD StableScopeNameBucketQueryKey clone() const;
  ZC_NODISCARD const StableScopeOwnerKey& scope() const noexcept;
  ZC_NODISCARD const BindingNameKey& name() const noexcept;
  bool operator==(const StableScopeNameBucketQueryKey& other) const;
  bool operator!=(const StableScopeNameBucketQueryKey& other) const { return !(*this == other); }

private:
  StableScopeNameBucketQueryKey(StableScopeOwnerKey&& scope, BindingNameKey&& name) noexcept;
  StableScopeOwnerKey scopeField;
  BindingNameKey nameField;
};

struct StableModuleBodySyntaxRoot final {
  identity::ModuleKey module;
};
struct StableDefinitionHeaderSyntaxRoot final {
  StableDefinitionQueryKey definition;
};
struct StableImplementationHeaderSyntaxRoot final {
  StableImplementationOccurrenceQueryKey occurrence;
};
using StableNodeSyntaxRootValue =
    zc::OneOf<StableModuleBodySyntaxRoot, StableDefinitionHeaderSyntaxRoot,
              StableImplementationHeaderSyntaxRoot>;
/// \brief Closed stable syntax root algebra for node-scope facts.
class StableNodeSyntaxRoot final {
public:
  StableNodeSyntaxRoot(StableNodeSyntaxRoot&&) noexcept = default;
  StableNodeSyntaxRoot& operator=(StableNodeSyntaxRoot&&) noexcept = default;
  ZC_DISALLOW_COPY(StableNodeSyntaxRoot);
  ZC_NODISCARD static StableNodeSyntaxRoot moduleBody(identity::ModuleKey&& module);
  ZC_NODISCARD static StableNodeSyntaxRoot definitionHeader(StableDefinitionQueryKey&& definition);
  ZC_NODISCARD static StableNodeSyntaxRoot implementationHeader(
      StableImplementationOccurrenceQueryKey&& occurrence);
  ZC_NODISCARD StableNodeSyntaxRoot clone() const;
  ZC_NODISCARD const StableNodeSyntaxRootValue& value() const noexcept;
  bool operator==(const StableNodeSyntaxRoot& other) const;
  bool operator!=(const StableNodeSyntaxRoot& other) const { return !(*this == other); }

private:
  explicit StableNodeSyntaxRoot(StableNodeSyntaxRootValue&& value) noexcept;
  StableNodeSyntaxRootValue valueField;
};

/// \brief Stable module-skeleton scope admitted with its local parent relation.
class StableScopeFact final {
public:
  ~StableScopeFact() noexcept(false);
  StableScopeFact(StableScopeFact&&) noexcept;
  StableScopeFact& operator=(StableScopeFact&&) noexcept;
  ZC_DISALLOW_COPY(StableScopeFact);

  ZC_NODISCARD static zc::Maybe<StableScopeFact> from(StableScopeOwnerKey&& owner,
                                                      zc::Maybe<StableScopeOwnerKey>&& parent,
                                                      ScopeKind kind);
  ZC_NODISCARD StableScopeFact clone() const;
  ZC_NODISCARD const StableScopeOwnerKey& owner() const noexcept;
  ZC_NODISCARD const zc::Maybe<StableScopeOwnerKey>& parent() const noexcept;
  ZC_NODISCARD ScopeKind kind() const noexcept;
  bool operator==(const StableScopeFact& other) const;
  bool operator!=(const StableScopeFact& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableScopeFact(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable syntax-node to scope relation within one module.
class StableNodeScopeFact final {
public:
  ~StableNodeScopeFact() noexcept(false);
  StableNodeScopeFact(StableNodeScopeFact&&) noexcept;
  StableNodeScopeFact& operator=(StableNodeScopeFact&&) noexcept;
  ZC_DISALLOW_COPY(StableNodeScopeFact);

  ZC_NODISCARD static zc::Maybe<StableNodeScopeFact> from(StableNodeSyntaxRoot&& root,
                                                          LocalSyntaxPath&& nodePath,
                                                          StableScopeOwnerKey&& scope);
  ZC_NODISCARD StableNodeScopeFact clone() const;
  ZC_NODISCARD const StableNodeSyntaxRoot& root() const noexcept;
  ZC_NODISCARD const LocalSyntaxPath& nodePath() const noexcept;
  ZC_NODISCARD const StableScopeOwnerKey& scope() const noexcept;
  bool operator==(const StableNodeScopeFact& other) const;
  bool operator!=(const StableNodeScopeFact& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableNodeScopeFact(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable scope owned by one exact semantic body.
class StableBodyScopeFact final {
public:
  ~StableBodyScopeFact() noexcept(false);
  StableBodyScopeFact(StableBodyScopeFact&&) noexcept;
  StableBodyScopeFact& operator=(StableBodyScopeFact&&) noexcept;
  ZC_DISALLOW_COPY(StableBodyScopeFact);

  ZC_NODISCARD static zc::Maybe<StableBodyScopeFact> from(StableOwnerBodyQueryKey&& owner,
                                                          StableScopeOwnerKey&& scope,
                                                          StableScopeOwnerKey&& parent,
                                                          ScopeKind kind);
  ZC_NODISCARD StableBodyScopeFact clone() const;
  ZC_NODISCARD const StableOwnerBodyQueryKey& owner() const noexcept;
  ZC_NODISCARD const StableScopeOwnerKey& scope() const noexcept;
  ZC_NODISCARD const StableScopeOwnerKey& parent() const noexcept;
  ZC_NODISCARD ScopeKind kind() const noexcept;
  bool operator==(const StableBodyScopeFact& other) const;
  bool operator!=(const StableBodyScopeFact& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableBodyScopeFact(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable syntax-node to scope relation within one exact semantic body.
class StableBodyNodeScopeFact final {
public:
  ~StableBodyNodeScopeFact() noexcept(false);
  StableBodyNodeScopeFact(StableBodyNodeScopeFact&&) noexcept;
  StableBodyNodeScopeFact& operator=(StableBodyNodeScopeFact&&) noexcept;
  ZC_DISALLOW_COPY(StableBodyNodeScopeFact);

  ZC_NODISCARD static zc::Maybe<StableBodyNodeScopeFact> from(StableOwnerBodyQueryKey&& owner,
                                                              LocalSyntaxPath&& nodePath,
                                                              StableScopeOwnerKey&& scope);
  ZC_NODISCARD StableBodyNodeScopeFact clone() const;
  ZC_NODISCARD const StableOwnerBodyQueryKey& owner() const noexcept;
  ZC_NODISCARD const LocalSyntaxPath& nodePath() const noexcept;
  ZC_NODISCARD const StableScopeOwnerKey& scope() const noexcept;
  bool operator==(const StableBodyNodeScopeFact& other) const;
  bool operator!=(const StableBodyNodeScopeFact& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableBodyNodeScopeFact(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable body-local binding admitted against its exact owner and declaring scope.
class StableOwnerLocalBindingFact final {
public:
  ~StableOwnerLocalBindingFact() noexcept(false);
  StableOwnerLocalBindingFact(StableOwnerLocalBindingFact&&) noexcept;
  StableOwnerLocalBindingFact& operator=(StableOwnerLocalBindingFact&&) noexcept;
  ZC_DISALLOW_COPY(StableOwnerLocalBindingFact);

  ZC_NODISCARD static zc::Maybe<StableOwnerLocalBindingFact> from(
      StableOwnerBodyQueryKey&& owner, OwnerLocalBindingKey&& key, OwnerLocalBindingKind kind,
      identity::DeclaredDefinitionName&& name, Namespace nameSpace,
      StableScopeOwnerKey&& declaringScope, DefinitionActivation activation);
  ZC_NODISCARD StableOwnerLocalBindingFact clone() const;
  ZC_NODISCARD const StableOwnerBodyQueryKey& owner() const noexcept;
  ZC_NODISCARD const OwnerLocalBindingKey& key() const noexcept;
  ZC_NODISCARD OwnerLocalBindingKind kind() const noexcept;
  ZC_NODISCARD const identity::DeclaredDefinitionName& name() const noexcept;
  ZC_NODISCARD Namespace nameSpace() const noexcept;
  ZC_NODISCARD const StableScopeOwnerKey& declaringScope() const noexcept;
  ZC_NODISCARD DefinitionActivation activation() const noexcept;
  bool operator==(const StableOwnerLocalBindingFact& other) const;
  bool operator!=(const StableOwnerLocalBindingFact& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableOwnerLocalBindingFact(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

class StableBindingTargetKey;

/// \brief Stable resolved body use with its selected and canonical targets.
class StableResolutionFact final {
public:
  ~StableResolutionFact() noexcept(false);
  StableResolutionFact(StableResolutionFact&&) noexcept;
  StableResolutionFact& operator=(StableResolutionFact&&) noexcept;
  ZC_DISALLOW_COPY(StableResolutionFact);

  ZC_NODISCARD static zc::Maybe<StableResolutionFact> from(StableOwnerBodyQueryKey&& owner,
                                                           LocalSyntaxPath&& usePath,
                                                           Namespace nameSpace,
                                                           StableBindingTargetKey&& binding,
                                                           StableBindingTargetKey&& canonicalTarget,
                                                           BindingOrigin origin);
  ZC_NODISCARD StableResolutionFact clone() const;
  ZC_NODISCARD const StableOwnerBodyQueryKey& owner() const noexcept;
  ZC_NODISCARD const LocalSyntaxPath& usePath() const noexcept;
  ZC_NODISCARD Namespace nameSpace() const noexcept;
  ZC_NODISCARD const StableBindingTargetKey& binding() const noexcept;
  ZC_NODISCARD const StableBindingTargetKey& canonicalTarget() const noexcept;
  ZC_NODISCARD BindingOrigin origin() const noexcept;
  bool operator==(const StableResolutionFact& other) const;
  bool operator!=(const StableResolutionFact& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableResolutionFact(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Closed member-access syntax retained by deferred semantic lookup.
enum class MemberAccessKind : uint8_t { Dot = 0x01, Optional = 0x02, Qualified = 0x03 };

/// \brief Stable deferred member lookup rooted in one exact semantic body.
class StableDeferredMemberFact final {
public:
  ~StableDeferredMemberFact() noexcept(false);
  StableDeferredMemberFact(StableDeferredMemberFact&&) noexcept;
  StableDeferredMemberFact& operator=(StableDeferredMemberFact&&) noexcept;
  ZC_DISALLOW_COPY(StableDeferredMemberFact);

  ZC_NODISCARD static zc::Maybe<StableDeferredMemberFact> from(
      StableOwnerBodyQueryKey&& owner, LocalSyntaxPath&& usePath, LocalSyntaxPath&& basePath,
      MemberAccessKind accessKind, identity::DeclaredDefinitionName&& member,
      CanonicalNonEmptySequence<Namespace>&& expectedNamespaces,
      CanonicalSequence<LocalSyntaxPath>&& genericArgumentPaths);
  ZC_NODISCARD StableDeferredMemberFact clone() const;
  ZC_NODISCARD const StableOwnerBodyQueryKey& owner() const noexcept;
  ZC_NODISCARD const LocalSyntaxPath& usePath() const noexcept;
  ZC_NODISCARD const LocalSyntaxPath& basePath() const noexcept;
  ZC_NODISCARD MemberAccessKind accessKind() const noexcept;
  ZC_NODISCARD const identity::DeclaredDefinitionName& member() const noexcept;
  ZC_NODISCARD const CanonicalNonEmptySequence<Namespace>& expectedNamespaces() const noexcept;
  ZC_NODISCARD const CanonicalSequence<LocalSyntaxPath>& genericArgumentPaths() const noexcept;
  bool operator==(const StableDeferredMemberFact& other) const;
  bool operator!=(const StableDeferredMemberFact& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableDeferredMemberFact(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

struct StableNominalSelfOwner final {
  StableDefinitionQueryKey definition;
};
struct StableInterfaceSelfOwner final {
  StableDefinitionQueryKey definition;
};
struct StableImplementationOccurrenceSelfOwner final {
  StableImplementationOccurrenceQueryKey occurrence;
};
using StableSelfOwnerValue = zc::OneOf<StableNominalSelfOwner, StableInterfaceSelfOwner,
                                       StableImplementationOccurrenceSelfOwner>;

/// \brief Closed stable semantic owner of contextual Self.
class StableSelfOwner final {
public:
  ~StableSelfOwner() noexcept(false);
  StableSelfOwner(StableSelfOwner&&) noexcept;
  StableSelfOwner& operator=(StableSelfOwner&&) noexcept;
  ZC_DISALLOW_COPY(StableSelfOwner);

  ZC_NODISCARD static StableSelfOwner nominal(StableDefinitionQueryKey&& definition);
  ZC_NODISCARD static StableSelfOwner interface(StableDefinitionQueryKey&& definition);
  ZC_NODISCARD static StableSelfOwner implementationOccurrence(
      StableImplementationOccurrenceQueryKey&& occurrence);
  ZC_NODISCARD StableSelfOwner clone() const;
  ZC_NODISCARD const StableSelfOwnerValue& value() const noexcept;
  bool operator==(const StableSelfOwner& other) const;
  bool operator!=(const StableSelfOwner& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableSelfOwner(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable contextual Self use routed within one semantic module.
class StableSelfTypeFact final {
public:
  ~StableSelfTypeFact() noexcept(false);
  StableSelfTypeFact(StableSelfTypeFact&&) noexcept;
  StableSelfTypeFact& operator=(StableSelfTypeFact&&) noexcept;
  ZC_DISALLOW_COPY(StableSelfTypeFact);

  ZC_NODISCARD static zc::Maybe<StableSelfTypeFact> from(StableOwnerBodyQueryKey&& owner,
                                                         LocalSyntaxPath&& syntaxPath,
                                                         StableSelfOwner&& selfOwner);
  ZC_NODISCARD StableSelfTypeFact clone() const;
  ZC_NODISCARD const StableOwnerBodyQueryKey& owner() const noexcept;
  ZC_NODISCARD const LocalSyntaxPath& syntaxPath() const noexcept;
  ZC_NODISCARD const StableSelfOwner& selfOwner() const noexcept;
  bool operator==(const StableSelfTypeFact& other) const;
  bool operator!=(const StableSelfTypeFact& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableSelfTypeFact(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable this-expression binding to one receiver in the owning module.
class StableThisBindingFact final {
public:
  ~StableThisBindingFact() noexcept(false);
  StableThisBindingFact(StableThisBindingFact&&) noexcept;
  StableThisBindingFact& operator=(StableThisBindingFact&&) noexcept;
  ZC_DISALLOW_COPY(StableThisBindingFact);

  ZC_NODISCARD static zc::Maybe<StableThisBindingFact> from(
      StableOwnerBodyQueryKey&& owner, LocalSyntaxPath&& expressionPath,
      StableCallableParameterQueryKey&& receiver);
  ZC_NODISCARD StableThisBindingFact clone() const;
  ZC_NODISCARD const StableOwnerBodyQueryKey& owner() const noexcept;
  ZC_NODISCARD const LocalSyntaxPath& expressionPath() const noexcept;
  ZC_NODISCARD const StableCallableParameterQueryKey& receiver() const noexcept;
  bool operator==(const StableThisBindingFact& other) const;
  bool operator!=(const StableThisBindingFact& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableThisBindingFact(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable binding-to-shadowed relation routed within one semantic body.
class StableShadowTargetFact final {
public:
  ~StableShadowTargetFact() noexcept(false);
  StableShadowTargetFact(StableShadowTargetFact&&) noexcept;
  StableShadowTargetFact& operator=(StableShadowTargetFact&&) noexcept;
  ZC_DISALLOW_COPY(StableShadowTargetFact);

  ZC_NODISCARD static zc::Maybe<StableShadowTargetFact> from(StableOwnerBodyQueryKey&& owner,
                                                             StableBindingTargetKey&& binding,
                                                             StableBindingTargetKey&& shadowed);
  ZC_NODISCARD StableShadowTargetFact clone() const;
  ZC_NODISCARD const StableOwnerBodyQueryKey& owner() const noexcept;
  ZC_NODISCARD const StableBindingTargetKey& binding() const noexcept;
  ZC_NODISCARD const StableBindingTargetKey& shadowed() const noexcept;
  bool operator==(const StableShadowTargetFact& other) const;
  bool operator!=(const StableShadowTargetFact& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableShadowTargetFact(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable identity of one label declaration within an owner body.
class StableLabelKey final {
public:
  ~StableLabelKey() noexcept(false);
  StableLabelKey(StableLabelKey&&) noexcept;
  StableLabelKey& operator=(StableLabelKey&&) noexcept;
  ZC_DISALLOW_COPY(StableLabelKey);

  ZC_NODISCARD static StableLabelKey from(StableOwnerBodyQueryKey&& owner,
                                          LocalSyntaxPath&& declarationPath);
  ZC_NODISCARD StableLabelKey clone() const;
  ZC_NODISCARD const StableOwnerBodyQueryKey& owner() const noexcept;
  ZC_NODISCARD const LocalSyntaxPath& declarationPath() const noexcept;
  bool operator==(const StableLabelKey& other) const;
  bool operator!=(const StableLabelKey& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableLabelKey(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

struct StableBlockLabelTarget final {
  StableScopeOwnerKey scope;
};
struct StableLoopLabelTarget final {
  StableScopeOwnerKey scope;
};
using StableLabelTargetValue = zc::OneOf<StableBlockLabelTarget, StableLoopLabelTarget>;

/// \brief Closed stable block-or-loop target of one label.
class StableLabelTarget final {
public:
  ~StableLabelTarget() noexcept(false);
  StableLabelTarget(StableLabelTarget&&) noexcept;
  StableLabelTarget& operator=(StableLabelTarget&&) noexcept;
  ZC_DISALLOW_COPY(StableLabelTarget);

  ZC_NODISCARD static StableLabelTarget block(StableScopeOwnerKey&& scope);
  ZC_NODISCARD static StableLabelTarget loop(StableScopeOwnerKey&& scope);
  ZC_NODISCARD StableLabelTarget clone() const;
  ZC_NODISCARD const StableLabelTargetValue& value() const noexcept;
  ZC_NODISCARD const StableScopeOwnerKey& scope() const noexcept;
  bool operator==(const StableLabelTarget& other) const;
  bool operator!=(const StableLabelTarget& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableLabelTarget(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable label declaration admitted against its owner and target scope.
class StableLabelFact final {
public:
  ~StableLabelFact() noexcept(false);
  StableLabelFact(StableLabelFact&&) noexcept;
  StableLabelFact& operator=(StableLabelFact&&) noexcept;
  ZC_DISALLOW_COPY(StableLabelFact);

  ZC_NODISCARD static zc::Maybe<StableLabelFact> from(StableLabelKey&& key,
                                                      identity::DeclaredDefinitionName&& name,
                                                      LocalSyntaxPath&& statementPath,
                                                      StableLabelTarget&& target);
  ZC_NODISCARD StableLabelFact clone() const;
  ZC_NODISCARD const StableLabelKey& key() const noexcept;
  ZC_NODISCARD const identity::DeclaredDefinitionName& name() const noexcept;
  ZC_NODISCARD const LocalSyntaxPath& statementPath() const noexcept;
  ZC_NODISCARD const StableLabelTarget& target() const noexcept;
  bool operator==(const StableLabelFact& other) const;
  bool operator!=(const StableLabelFact& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableLabelFact(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

struct StableExplicitLabelControlTarget final {
  StableLabelKey label;
};
struct StableLoopControlTarget final {
  StableScopeOwnerKey scope;
};
struct StableMatchControlTarget final {
  StableScopeOwnerKey scope;
};
using StableControlTargetValue =
    zc::OneOf<StableExplicitLabelControlTarget, StableLoopControlTarget, StableMatchControlTarget>;

/// \brief Closed stable target of a break or continue transfer.
class StableControlTarget final {
public:
  ~StableControlTarget() noexcept(false);
  StableControlTarget(StableControlTarget&&) noexcept;
  StableControlTarget& operator=(StableControlTarget&&) noexcept;
  ZC_DISALLOW_COPY(StableControlTarget);

  ZC_NODISCARD static StableControlTarget explicitLabel(StableLabelKey&& label);
  ZC_NODISCARD static StableControlTarget loop(StableScopeOwnerKey&& scope);
  ZC_NODISCARD static StableControlTarget match(StableScopeOwnerKey&& scope);
  ZC_NODISCARD StableControlTarget clone() const;
  ZC_NODISCARD const StableControlTargetValue& value() const noexcept;
  bool operator==(const StableControlTarget& other) const;
  bool operator!=(const StableControlTarget& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableControlTarget(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable control transfer admitted against its owner and target class.
class StableControlTransferFact final {
public:
  ~StableControlTransferFact() noexcept(false);
  StableControlTransferFact(StableControlTransferFact&&) noexcept;
  StableControlTransferFact& operator=(StableControlTransferFact&&) noexcept;
  ZC_DISALLOW_COPY(StableControlTransferFact);

  ZC_NODISCARD static zc::Maybe<StableControlTransferFact> from(StableOwnerBodyQueryKey&& owner,
                                                                LocalSyntaxPath&& transferPath,
                                                                ControlTransferKind kind,
                                                                StableControlTarget&& target);
  ZC_NODISCARD StableControlTransferFact clone() const;
  ZC_NODISCARD const StableOwnerBodyQueryKey& owner() const noexcept;
  ZC_NODISCARD const LocalSyntaxPath& transferPath() const noexcept;
  ZC_NODISCARD ControlTransferKind kind() const noexcept;
  ZC_NODISCARD const StableControlTarget& target() const noexcept;
  bool operator==(const StableControlTransferFact& other) const;
  bool operator!=(const StableControlTransferFact& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableControlTransferFact(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable closure declaration and its body scope.
class StableClosureFact final {
public:
  ~StableClosureFact() noexcept(false);
  StableClosureFact(StableClosureFact&&) noexcept;
  StableClosureFact& operator=(StableClosureFact&&) noexcept;
  ZC_DISALLOW_COPY(StableClosureFact);

  ZC_NODISCARD static zc::Maybe<StableClosureFact> from(StableOwnerBodyQueryKey&& owner,
                                                        AnonymousOwnerLocalKey&& closure,
                                                        StableScopeOwnerKey&& scope);
  ZC_NODISCARD StableClosureFact clone() const;
  ZC_NODISCARD const StableOwnerBodyQueryKey& owner() const noexcept;
  ZC_NODISCARD const AnonymousOwnerLocalKey& closure() const noexcept;
  ZC_NODISCARD const StableScopeOwnerKey& scope() const noexcept;
  bool operator==(const StableClosureFact& other) const;
  bool operator!=(const StableClosureFact& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableClosureFact(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable captured target and its nonempty canonical reference paths.
class StableClosureFreeVariable final {
public:
  ~StableClosureFreeVariable() noexcept(false);
  StableClosureFreeVariable(StableClosureFreeVariable&&) noexcept;
  StableClosureFreeVariable& operator=(StableClosureFreeVariable&&) noexcept;
  ZC_DISALLOW_COPY(StableClosureFreeVariable);

  ZC_NODISCARD static StableClosureFreeVariable from(
      StableBindingTargetKey&& target, CanonicalNonEmptySequence<LocalSyntaxPath>&& referencePaths);
  ZC_NODISCARD StableClosureFreeVariable clone() const;
  ZC_NODISCARD const StableBindingTargetKey& target() const noexcept;
  ZC_NODISCARD const CanonicalNonEmptySequence<LocalSyntaxPath>& referencePaths() const noexcept;
  bool operator==(const StableClosureFreeVariable& other) const;
  bool operator!=(const StableClosureFreeVariable& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableClosureFreeVariable(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable canonical free-variable inventory for one closure.
class StableClosureFreeVariableFact final {
public:
  ~StableClosureFreeVariableFact() noexcept(false);
  StableClosureFreeVariableFact(StableClosureFreeVariableFact&&) noexcept;
  StableClosureFreeVariableFact& operator=(StableClosureFreeVariableFact&&) noexcept;
  ZC_DISALLOW_COPY(StableClosureFreeVariableFact);

  ZC_NODISCARD static zc::Maybe<StableClosureFreeVariableFact> from(
      StableOwnerBodyQueryKey&& owner, AnonymousOwnerLocalKey&& closure,
      CanonicalSequence<StableClosureFreeVariable>&& variables);
  ZC_NODISCARD StableClosureFreeVariableFact clone() const;
  ZC_NODISCARD const StableOwnerBodyQueryKey& owner() const noexcept;
  ZC_NODISCARD const AnonymousOwnerLocalKey& closure() const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableClosureFreeVariable>& variables() const noexcept;
  bool operator==(const StableClosureFreeVariableFact& other) const;
  bool operator!=(const StableClosureFreeVariableFact& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableClosureFreeVariableFact(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable explicit capture item and its closed capture mode.
class StableExplicitCaptureBindingFact final {
public:
  ~StableExplicitCaptureBindingFact() noexcept(false);
  StableExplicitCaptureBindingFact(StableExplicitCaptureBindingFact&&) noexcept;
  StableExplicitCaptureBindingFact& operator=(StableExplicitCaptureBindingFact&&) noexcept;
  ZC_DISALLOW_COPY(StableExplicitCaptureBindingFact);

  ZC_NODISCARD static zc::Maybe<StableExplicitCaptureBindingFact> from(
      LocalSyntaxPath&& itemPath, StableBindingTargetKey&& target, StableExplicitCaptureMode mode);
  ZC_NODISCARD StableExplicitCaptureBindingFact clone() const;
  ZC_NODISCARD const LocalSyntaxPath& itemPath() const noexcept;
  ZC_NODISCARD const StableBindingTargetKey& target() const noexcept;
  ZC_NODISCARD StableExplicitCaptureMode mode() const noexcept;
  bool operator==(const StableExplicitCaptureBindingFact& other) const;
  bool operator!=(const StableExplicitCaptureBindingFact& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableExplicitCaptureBindingFact(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable canonical explicit capture list for one function expression.
class StableExplicitClosureCaptureFact final {
public:
  ~StableExplicitClosureCaptureFact() noexcept(false);
  StableExplicitClosureCaptureFact(StableExplicitClosureCaptureFact&&) noexcept;
  StableExplicitClosureCaptureFact& operator=(StableExplicitClosureCaptureFact&&) noexcept;
  ZC_DISALLOW_COPY(StableExplicitClosureCaptureFact);

  ZC_NODISCARD static zc::Maybe<StableExplicitClosureCaptureFact> from(
      StableOwnerBodyQueryKey&& owner, AnonymousOwnerLocalKey&& closure,
      LocalSyntaxPath&& captureListPath,
      CanonicalSequence<StableExplicitCaptureBindingFact>&& captures);
  ZC_NODISCARD StableExplicitClosureCaptureFact clone() const;
  ZC_NODISCARD const StableOwnerBodyQueryKey& owner() const noexcept;
  ZC_NODISCARD const AnonymousOwnerLocalKey& closure() const noexcept;
  ZC_NODISCARD const LocalSyntaxPath& captureListPath() const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableExplicitCaptureBindingFact>& captures() const noexcept;
  bool operator==(const StableExplicitClosureCaptureFact& other) const;
  bool operator!=(const StableExplicitClosureCaptureFact& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableExplicitClosureCaptureFact(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable declaration fact admitted against its identity and declaring scope.
class StableDeclarationFact final {
public:
  ~StableDeclarationFact() noexcept(false);
  StableDeclarationFact(StableDeclarationFact&&) noexcept;
  StableDeclarationFact& operator=(StableDeclarationFact&&) noexcept;
  ZC_DISALLOW_COPY(StableDeclarationFact);

  ZC_NODISCARD static zc::Maybe<StableDeclarationFact> from(
      StableDefinitionQueryKey&& queryKey, identity::DefinitionIdentityRecord&& record,
      StableScopeOwnerKey&& declaringScope, identity::DefinitionKind kind, Namespace nameSpace,
      identity::DeclaredDefinitionName&& name, DefinitionActivation activation,
      zc::Maybe<MemberVisibility>&& visibility);
  ZC_NODISCARD StableDeclarationFact clone() const;
  ZC_NODISCARD const StableDefinitionQueryKey& queryKey() const noexcept;
  ZC_NODISCARD const identity::DefinitionIdentityRecord& record() const noexcept;
  ZC_NODISCARD const StableScopeOwnerKey& declaringScope() const noexcept;
  ZC_NODISCARD identity::DefinitionKind kind() const noexcept;
  ZC_NODISCARD Namespace nameSpace() const noexcept;
  ZC_NODISCARD const identity::DeclaredDefinitionName& name() const noexcept;
  ZC_NODISCARD DefinitionActivation activation() const noexcept;
  ZC_NODISCARD const zc::Maybe<MemberVisibility>& visibility() const noexcept;
  bool operator==(const StableDeclarationFact& other) const;
  bool operator!=(const StableDeclarationFact& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableDeclarationFact(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable implementation occurrence admitted against its authority and scope.
class StableImplementationOccurrenceFact final {
public:
  ~StableImplementationOccurrenceFact() noexcept(false);
  StableImplementationOccurrenceFact(StableImplementationOccurrenceFact&&) noexcept;
  StableImplementationOccurrenceFact& operator=(StableImplementationOccurrenceFact&&) noexcept;
  ZC_DISALLOW_COPY(StableImplementationOccurrenceFact);

  ZC_NODISCARD static zc::Maybe<StableImplementationOccurrenceFact> from(
      StableImplementationOccurrenceQueryKey&& occurrence, StableImplementationQueryKey&& authority,
      identity::ImplIdentityRecord&& record, StableScopeOwnerKey&& declaringScope);
  ZC_NODISCARD StableImplementationOccurrenceFact clone() const;
  ZC_NODISCARD const StableImplementationOccurrenceQueryKey& occurrence() const noexcept;
  ZC_NODISCARD const StableImplementationQueryKey& authority() const noexcept;
  ZC_NODISCARD const identity::ImplIdentityRecord& record() const noexcept;
  ZC_NODISCARD const StableScopeOwnerKey& declaringScope() const noexcept;
  bool operator==(const StableImplementationOccurrenceFact& other) const;
  bool operator!=(const StableImplementationOccurrenceFact& other) const {
    return !(*this == other);
  }

private:
  struct Impl;
  explicit StableImplementationOccurrenceFact(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable generic-parameter declaration admitted against its header and scope.
class StableGenericParameterDeclarationFact final {
public:
  ~StableGenericParameterDeclarationFact() noexcept(false);
  StableGenericParameterDeclarationFact(StableGenericParameterDeclarationFact&&) noexcept;
  StableGenericParameterDeclarationFact& operator=(
      StableGenericParameterDeclarationFact&&) noexcept;
  ZC_DISALLOW_COPY(StableGenericParameterDeclarationFact);

  ZC_NODISCARD static zc::Maybe<StableGenericParameterDeclarationFact> from(
      StableGenericParameterQueryKey&& queryKey, identity::GenericParameterIdentityRecord&& record,
      StableHeaderSite&& headerSite, StableScopeOwnerKey&& declaringScope,
      identity::DeclaredDefinitionName&& name);
  ZC_NODISCARD StableGenericParameterDeclarationFact clone() const;
  ZC_NODISCARD const StableGenericParameterQueryKey& queryKey() const noexcept;
  ZC_NODISCARD const identity::GenericParameterIdentityRecord& record() const noexcept;
  ZC_NODISCARD const StableHeaderSite& headerSite() const noexcept;
  ZC_NODISCARD const StableScopeOwnerKey& declaringScope() const noexcept;
  ZC_NODISCARD const identity::DeclaredDefinitionName& name() const noexcept;
  bool operator==(const StableGenericParameterDeclarationFact& other) const;
  bool operator!=(const StableGenericParameterDeclarationFact& other) const {
    return !(*this == other);
  }

private:
  struct Impl;
  explicit StableGenericParameterDeclarationFact(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable callable-parameter declaration admitted against its header and scope.
class StableCallableParameterDeclarationFact final {
public:
  ~StableCallableParameterDeclarationFact() noexcept(false);
  StableCallableParameterDeclarationFact(StableCallableParameterDeclarationFact&&) noexcept;
  StableCallableParameterDeclarationFact& operator=(
      StableCallableParameterDeclarationFact&&) noexcept;
  ZC_DISALLOW_COPY(StableCallableParameterDeclarationFact);

  ZC_NODISCARD static zc::Maybe<StableCallableParameterDeclarationFact> from(
      StableCallableParameterQueryKey&& queryKey,
      identity::CallableParameterIdentityRecord&& record, StableHeaderSite&& headerSite,
      StableScopeOwnerKey&& declaringScope, zc::Maybe<identity::DeclaredDefinitionName>&& name);
  ZC_NODISCARD StableCallableParameterDeclarationFact clone() const;
  ZC_NODISCARD const StableCallableParameterQueryKey& queryKey() const noexcept;
  ZC_NODISCARD const identity::CallableParameterIdentityRecord& record() const noexcept;
  ZC_NODISCARD const StableHeaderSite& headerSite() const noexcept;
  ZC_NODISCARD const StableScopeOwnerKey& declaringScope() const noexcept;
  ZC_NODISCARD const zc::Maybe<identity::DeclaredDefinitionName>& name() const noexcept;
  bool operator==(const StableCallableParameterDeclarationFact& other) const;
  bool operator!=(const StableCallableParameterDeclarationFact& other) const {
    return !(*this == other);
  }

private:
  struct Impl;
  explicit StableCallableParameterDeclarationFact(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

struct StableDefinitionBindingTarget final {
  StableDefinitionQueryKey definition;
};
struct StableImplementationBindingTarget final {
  StableImplementationQueryKey implementation;
};
struct StableModuleBindingTarget final {
  identity::ModuleKey module;
};
struct StableSemanticImportBindingTarget final {
  StableSemanticImportQueryKey import;
};
struct StableOwnerLocalBindingTarget final {
  StableOwnerBodyQueryKey owner;
  OwnerLocalBindingKey binding;
};
struct StableAnonymousOwnerBindingTarget final {
  StableOwnerBodyQueryKey owner;
  AnonymousOwnerLocalKey binding;
};
struct StableGenericParameterBindingTarget final {
  StableGenericParameterQueryKey parameter;
};
struct StableCallableParameterBindingTarget final {
  StableCallableParameterQueryKey parameter;
};
using StableBindingTargetValue =
    zc::OneOf<StableDefinitionBindingTarget, StableImplementationBindingTarget,
              StableModuleBindingTarget, StableSemanticImportBindingTarget,
              StableOwnerLocalBindingTarget, StableAnonymousOwnerBindingTarget,
              StableGenericParameterBindingTarget, StableCallableParameterBindingTarget>;
/// \brief Closed routable target algebra for every stable Binder binding.
class StableBindingTargetKey final {
public:
  StableBindingTargetKey(StableBindingTargetKey&&) noexcept = default;
  StableBindingTargetKey& operator=(StableBindingTargetKey&&) noexcept = default;
  ZC_DISALLOW_COPY(StableBindingTargetKey);
  ZC_NODISCARD static StableBindingTargetKey definition(StableDefinitionQueryKey&& value);
  ZC_NODISCARD static StableBindingTargetKey implementation(StableImplementationQueryKey&& value);
  ZC_NODISCARD static StableBindingTargetKey module(identity::ModuleKey&& value);
  ZC_NODISCARD static StableBindingTargetKey semanticImport(StableSemanticImportQueryKey&& value);
  ZC_NODISCARD static zc::Maybe<StableBindingTargetKey> ownerLocal(StableOwnerBodyQueryKey&& owner,
                                                                   OwnerLocalBindingKey&& binding);
  ZC_NODISCARD static zc::Maybe<StableBindingTargetKey> anonymousOwner(
      StableOwnerBodyQueryKey&& owner, AnonymousOwnerLocalKey&& binding);
  ZC_NODISCARD static StableBindingTargetKey genericParameter(
      StableGenericParameterQueryKey&& value);
  ZC_NODISCARD static StableBindingTargetKey callableParameter(
      StableCallableParameterQueryKey&& value);
  ZC_NODISCARD StableBindingTargetKey clone() const;
  ZC_NODISCARD const StableBindingTargetValue& value() const noexcept;
  bool operator==(const StableBindingTargetKey& other) const;
  bool operator!=(const StableBindingTargetKey& other) const { return !(*this == other); }

private:
  explicit StableBindingTargetKey(StableBindingTargetValue&& value) noexcept;
  StableBindingTargetValue valueField;
};

/// \brief Stable result of resolving one exported module binding.
class StableExportedBinding final {
public:
  ~StableExportedBinding() noexcept(false);
  StableExportedBinding(StableExportedBinding&&) noexcept;
  StableExportedBinding& operator=(StableExportedBinding&&) noexcept;
  ZC_DISALLOW_COPY(StableExportedBinding);

  ZC_NODISCARD static zc::Maybe<StableExportedBinding> from(
      BindingNameKey&& name, StableBindingTargetKey&& binding,
      StableBindingTargetKey&& canonicalTarget, zc::Maybe<MemberVisibility>&& visibility,
      bool exported);
  ZC_NODISCARD StableExportedBinding clone() const;
  ZC_NODISCARD const BindingNameKey& name() const noexcept;
  ZC_NODISCARD const StableBindingTargetKey& binding() const noexcept;
  ZC_NODISCARD const StableBindingTargetKey& canonicalTarget() const noexcept;
  ZC_NODISCARD const zc::Maybe<MemberVisibility>& visibility() const noexcept;
  ZC_NODISCARD bool exported() const noexcept;
  bool operator==(const StableExportedBinding& other) const;
  bool operator!=(const StableExportedBinding& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableExportedBinding(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable selected import or re-export binding admitted in its requester scope.
class StableImportFact final {
public:
  ~StableImportFact() noexcept(false);
  StableImportFact(StableImportFact&&) noexcept;
  StableImportFact& operator=(StableImportFact&&) noexcept;
  ZC_DISALLOW_COPY(StableImportFact);

  ZC_NODISCARD static zc::Maybe<StableImportFact> from(StableSemanticImportQueryKey&& queryKey,
                                                       StableScopeOwnerKey&& declaringScope,
                                                       StableBindingTargetKey&& target,
                                                       StableBindingTargetKey&& canonicalTarget,
                                                       Namespace nameSpace, BindingOrigin origin,
                                                       zc::Maybe<MemberVisibility>&& visibility,
                                                       bool exported);
  ZC_NODISCARD StableImportFact clone() const;
  ZC_NODISCARD const StableSemanticImportQueryKey& queryKey() const noexcept;
  ZC_NODISCARD const StableScopeOwnerKey& declaringScope() const noexcept;
  ZC_NODISCARD const StableBindingTargetKey& target() const noexcept;
  ZC_NODISCARD const StableBindingTargetKey& canonicalTarget() const noexcept;
  ZC_NODISCARD Namespace nameSpace() const noexcept;
  ZC_NODISCARD BindingOrigin origin() const noexcept;
  ZC_NODISCARD const zc::Maybe<MemberVisibility>& visibility() const noexcept;
  ZC_NODISCARD bool exported() const noexcept;
  bool operator==(const StableImportFact& other) const;
  bool operator!=(const StableImportFact& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableImportFact(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable module alias admitted against its requester and target surface.
class StableModuleAliasFact final {
public:
  ~StableModuleAliasFact() noexcept(false);
  StableModuleAliasFact(StableModuleAliasFact&&) noexcept;
  StableModuleAliasFact& operator=(StableModuleAliasFact&&) noexcept;
  ZC_DISALLOW_COPY(StableModuleAliasFact);

  ZC_NODISCARD static zc::Maybe<StableModuleAliasFact> from(
      StableSemanticImportQueryKey&& queryKey, StableScopeOwnerKey&& declaringScope,
      StableDefinitionQueryKey&& alias, identity::ModuleKey&& canonicalModule,
      ModuleAliasExportNamesRevision targetExportNamesRevision);
  ZC_NODISCARD StableModuleAliasFact clone() const;
  ZC_NODISCARD const StableSemanticImportQueryKey& queryKey() const noexcept;
  ZC_NODISCARD const StableScopeOwnerKey& declaringScope() const noexcept;
  ZC_NODISCARD const StableDefinitionQueryKey& alias() const noexcept;
  ZC_NODISCARD const identity::ModuleKey& canonicalModule() const noexcept;
  ZC_NODISCARD const ModuleAliasExportNamesRevision& targetExportNamesRevision() const noexcept;
  bool operator==(const StableModuleAliasFact& other) const;
  bool operator!=(const StableModuleAliasFact& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableModuleAliasFact(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable provenance step for one module-local re-export.
class StableReexportStep final {
public:
  ~StableReexportStep() noexcept(false);
  StableReexportStep(StableReexportStep&&) noexcept;
  StableReexportStep& operator=(StableReexportStep&&) noexcept;
  ZC_DISALLOW_COPY(StableReexportStep);

  ZC_NODISCARD static StableReexportStep from(identity::ModuleKey&& module,
                                              LocalSyntaxPath&& exportPath,
                                              StableBindingTargetKey&& binding,
                                              StableBindingTargetKey&& canonicalTarget);
  ZC_NODISCARD StableReexportStep clone() const;
  ZC_NODISCARD const identity::ModuleKey& module() const noexcept;
  ZC_NODISCARD const LocalSyntaxPath& exportPath() const noexcept;
  ZC_NODISCARD const StableBindingTargetKey& binding() const noexcept;
  ZC_NODISCARD const StableBindingTargetKey& canonicalTarget() const noexcept;
  bool operator==(const StableReexportStep& other) const;
  bool operator!=(const StableReexportStep& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableReexportStep(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable module-local export fact with complete re-export provenance.
class StableLocalExportFact final {
public:
  ~StableLocalExportFact() noexcept(false);
  StableLocalExportFact(StableLocalExportFact&&) noexcept;
  StableLocalExportFact& operator=(StableLocalExportFact&&) noexcept;
  ZC_DISALLOW_COPY(StableLocalExportFact);

  ZC_NODISCARD static zc::Maybe<StableLocalExportFact> from(
      identity::ModuleKey&& declaringModule, LocalSyntaxPath&& exportPath, BindingNameKey&& name,
      StableBindingTargetKey&& binding, StableBindingTargetKey&& canonicalTarget,
      zc::Maybe<MemberVisibility>&& visibility,
      CanonicalSequence<StableReexportStep>&& reexportChain);
  ZC_NODISCARD StableLocalExportFact clone() const;
  ZC_NODISCARD const identity::ModuleKey& declaringModule() const noexcept;
  ZC_NODISCARD const LocalSyntaxPath& exportPath() const noexcept;
  ZC_NODISCARD const BindingNameKey& name() const noexcept;
  ZC_NODISCARD const StableBindingTargetKey& binding() const noexcept;
  ZC_NODISCARD const StableBindingTargetKey& canonicalTarget() const noexcept;
  ZC_NODISCARD const zc::Maybe<MemberVisibility>& visibility() const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableReexportStep>& reexportChain() const noexcept;
  bool operator==(const StableLocalExportFact& other) const;
  bool operator!=(const StableLocalExportFact& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableLocalExportFact(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

struct BinderModuleQueryOwner final {
  identity::ModuleKey module;
};
struct BinderDefinitionHeaderQueryOwner final {
  StableDefinitionQueryKey definition;
};
struct BinderImplementationHeaderQueryOwner final {
  StableImplementationOccurrenceQueryKey implementation;
};
struct BinderBodyQueryOwner final {
  StableOwnerBodyQueryKey body;
};
using BinderQueryOwnerValue = zc::OneOf<BinderModuleQueryOwner, BinderDefinitionHeaderQueryOwner,
                                        BinderImplementationHeaderQueryOwner, BinderBodyQueryOwner>;

/// \brief Stable owner attached to a Binder key-admission failure.
class BinderQueryOwner final {
public:
  BinderQueryOwner(BinderQueryOwner&&) noexcept = default;
  BinderQueryOwner& operator=(BinderQueryOwner&&) noexcept = default;
  ZC_DISALLOW_COPY(BinderQueryOwner);
  ZC_NODISCARD static BinderQueryOwner module(identity::ModuleKey&& value);
  ZC_NODISCARD static BinderQueryOwner definitionHeader(StableDefinitionQueryKey&& value);
  ZC_NODISCARD static BinderQueryOwner implementationHeader(
      StableImplementationOccurrenceQueryKey&& value);
  ZC_NODISCARD static BinderQueryOwner body(StableOwnerBodyQueryKey&& value);
  ZC_NODISCARD BinderQueryOwner clone() const;
  ZC_NODISCARD const BinderQueryOwnerValue& value() const noexcept;
  bool operator==(const BinderQueryOwner& other) const;
  bool operator!=(const BinderQueryOwner& other) const { return !(*this == other); }

private:
  explicit BinderQueryOwner(BinderQueryOwnerValue&& value) noexcept;
  BinderQueryOwnerValue valueField;
};

struct StableMissingLookupOutcome final {};
struct StableNamespaceMismatchLookupOutcome final {
  CanonicalNonEmptySequence<Namespace> availableNamespaces;
};
struct StableAmbiguousLookupOutcome final {
  CanonicalNonEmptySequence<StableBindingTargetKey> candidates;
};
using StableFailedLookupOutcomeValue =
    zc::OneOf<StableMissingLookupOutcome, StableNamespaceMismatchLookupOutcome,
              StableAmbiguousLookupOutcome>;

/// \brief Closed semantic outcome for one failed Binder name lookup.
class StableFailedLookupOutcome final {
public:
  ~StableFailedLookupOutcome() noexcept(false);
  StableFailedLookupOutcome(StableFailedLookupOutcome&&) noexcept;
  StableFailedLookupOutcome& operator=(StableFailedLookupOutcome&&) noexcept;
  ZC_DISALLOW_COPY(StableFailedLookupOutcome);

  ZC_NODISCARD static StableFailedLookupOutcome missing();
  ZC_NODISCARD static StableFailedLookupOutcome namespaceMismatch(
      CanonicalNonEmptySequence<Namespace>&& availableNamespaces);
  ZC_NODISCARD static zc::Maybe<StableFailedLookupOutcome> ambiguous(
      CanonicalNonEmptySequence<StableBindingTargetKey>&& candidates);
  ZC_NODISCARD StableFailedLookupOutcome clone() const;
  ZC_NODISCARD const StableFailedLookupOutcomeValue& value() const noexcept;
  bool operator==(const StableFailedLookupOutcome& other) const;
  bool operator!=(const StableFailedLookupOutcome& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableFailedLookupOutcome(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable semantic lookup failure retained at its exact owner and syntax path.
class StableFailedLookupFact final {
public:
  ~StableFailedLookupFact() noexcept(false);
  StableFailedLookupFact(StableFailedLookupFact&&) noexcept;
  StableFailedLookupFact& operator=(StableFailedLookupFact&&) noexcept;
  ZC_DISALLOW_COPY(StableFailedLookupFact);

  ZC_NODISCARD static zc::Maybe<StableFailedLookupFact> from(
      BinderQueryOwner&& owner, LocalSyntaxPath&& usePath, Namespace nameSpace,
      identity::DeclaredDefinitionName&& name, StableFailedLookupOutcome&& outcome);
  ZC_NODISCARD StableFailedLookupFact clone() const;
  ZC_NODISCARD const BinderQueryOwner& owner() const noexcept;
  ZC_NODISCARD const LocalSyntaxPath& usePath() const noexcept;
  ZC_NODISCARD Namespace nameSpace() const noexcept;
  ZC_NODISCARD const identity::DeclaredDefinitionName& name() const noexcept;
  ZC_NODISCARD const StableFailedLookupOutcome& outcome() const noexcept;
  bool operator==(const StableFailedLookupFact& other) const;
  bool operator!=(const StableFailedLookupFact& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableFailedLookupFact(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Runtime projection of one stable failed lookup without diagnostic ownership.
struct MaterializedFailedLookupFact final {
  ast::NodeId node;
  Namespace nameSpace;
  identity::DeclaredDefinitionName name;
  StableFailedLookupOutcome outcome;
};

/// \brief Complete stable semantic fact inventory for one owner body.
class BoundOwnerBody final {
public:
  ~BoundOwnerBody() noexcept(false);
  BoundOwnerBody(BoundOwnerBody&&) noexcept;
  BoundOwnerBody& operator=(BoundOwnerBody&&) noexcept;
  ZC_DISALLOW_COPY(BoundOwnerBody);
  ZC_NODISCARD static zc::Maybe<BoundOwnerBody> from(
      StableOwnerBodyQueryKey&& owner, CanonicalSequence<StableBodyScopeFact>&& scopes,
      CanonicalSequence<StableBodyNodeScopeFact>&& nodeScopes,
      CanonicalSequence<StableOwnerLocalBindingFact>&& bindings,
      CanonicalSequence<StableResolutionFact>&& resolutions,
      CanonicalSequence<StableDeferredMemberFact>&& deferredMembers,
      CanonicalSequence<StableSelfTypeFact>&& selfTypes,
      CanonicalSequence<StableThisBindingFact>&& thisBindings,
      CanonicalSequence<StableShadowTargetFact>&& shadowTargets,
      CanonicalSequence<StableLabelFact>&& labels,
      CanonicalSequence<StableControlTransferFact>&& controlTransfers,
      CanonicalSequence<StableClosureFact>&& closures,
      CanonicalSequence<StableClosureFreeVariableFact>&& closureFreeVariables,
      CanonicalSequence<StableExplicitClosureCaptureFact>&& explicitClosureCaptures,
      CanonicalSequence<StableFailedLookupFact>&& failedLookups);
  ZC_NODISCARD BoundOwnerBody clone() const;
  ZC_NODISCARD const StableOwnerBodyQueryKey& owner() const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableBodyScopeFact>& scopes() const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableBodyNodeScopeFact>& nodeScopes() const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableOwnerLocalBindingFact>& bindings() const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableResolutionFact>& resolutions() const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableDeferredMemberFact>& deferredMembers() const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableSelfTypeFact>& selfTypes() const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableThisBindingFact>& thisBindings() const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableShadowTargetFact>& shadowTargets() const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableLabelFact>& labels() const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableControlTransferFact>& controlTransfers()
      const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableClosureFact>& closures() const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableClosureFreeVariableFact>& closureFreeVariables()
      const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableExplicitClosureCaptureFact>& explicitClosureCaptures()
      const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableFailedLookupFact>& failedLookups() const noexcept;
  bool operator==(const BoundOwnerBody& other) const;

private:
  struct Impl;
  explicit BoundOwnerBody(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Dense module-local allocation ranges assigned to one stable body owner.
class OwnerAllocationRange final {
public:
  ~OwnerAllocationRange() noexcept(false);
  OwnerAllocationRange(OwnerAllocationRange&&) noexcept;
  OwnerAllocationRange& operator=(OwnerAllocationRange&&) noexcept;
  ZC_DISALLOW_COPY(OwnerAllocationRange);
  ZC_NODISCARD static zc::Maybe<OwnerAllocationRange> from(
      StableOwnerBodyQueryKey&& owner, uint32_t scopeBegin, uint32_t scopeCount,
      uint32_t ownerLocalBegin, uint32_t ownerLocalCount, uint32_t anonymousBegin,
      uint32_t anonymousCount, uint32_t labelBegin, uint32_t labelCount);
  ZC_NODISCARD OwnerAllocationRange clone() const;
  ZC_NODISCARD const StableOwnerBodyQueryKey& owner() const noexcept;
  ZC_NODISCARD uint32_t scopeBegin() const noexcept;
  ZC_NODISCARD uint32_t scopeCount() const noexcept;
  ZC_NODISCARD uint32_t ownerLocalBegin() const noexcept;
  ZC_NODISCARD uint32_t ownerLocalCount() const noexcept;
  ZC_NODISCARD uint32_t anonymousBegin() const noexcept;
  ZC_NODISCARD uint32_t anonymousCount() const noexcept;
  ZC_NODISCARD uint32_t labelBegin() const noexcept;
  ZC_NODISCARD uint32_t labelCount() const noexcept;
  bool operator==(const OwnerAllocationRange& other) const;
  bool operator!=(const OwnerAllocationRange& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit OwnerAllocationRange(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Complete deterministic module-local allocation plan.
class ModuleBindingAllocationPlan final {
public:
  ~ModuleBindingAllocationPlan() noexcept(false);
  ModuleBindingAllocationPlan(ModuleBindingAllocationPlan&&) noexcept;
  ModuleBindingAllocationPlan& operator=(ModuleBindingAllocationPlan&&) noexcept;
  ZC_DISALLOW_COPY(ModuleBindingAllocationPlan);
  ZC_NODISCARD static zc::Maybe<ModuleBindingAllocationPlan> from(
      identity::ModuleKey&& key, uint32_t skeletonScopeCount,
      uint32_t implementationOccurrenceCount, CanonicalSequence<OwnerAllocationRange>&& owners);
  ZC_NODISCARD ModuleBindingAllocationPlan clone() const;
  ZC_NODISCARD const identity::ModuleKey& key() const noexcept;
  ZC_NODISCARD uint32_t skeletonScopeCount() const noexcept;
  ZC_NODISCARD uint32_t implementationOccurrenceCount() const noexcept;
  ZC_NODISCARD const CanonicalSequence<OwnerAllocationRange>& owners() const noexcept;
  bool operator==(const ModuleBindingAllocationPlan& other) const;
  bool operator!=(const ModuleBindingAllocationPlan& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit ModuleBindingAllocationPlan(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Complete stable semantic skeleton for one module.
class BoundModuleSkeleton final {
public:
  ~BoundModuleSkeleton() noexcept(false);
  BoundModuleSkeleton(BoundModuleSkeleton&&) noexcept;
  BoundModuleSkeleton& operator=(BoundModuleSkeleton&&) noexcept;
  ZC_DISALLOW_COPY(BoundModuleSkeleton);
  ZC_NODISCARD static zc::Maybe<BoundModuleSkeleton> from(
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
      CanonicalSequence<StableFailedLookupFact>&& failedLookups);
  ZC_NODISCARD BoundModuleSkeleton clone() const;
  ZC_NODISCARD const identity::ModuleKey& module() const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableScopeFact>& scopes() const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableNodeScopeFact>& nodeScopes() const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableDeclarationFact>& declarations() const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableImplementationOccurrenceFact>&
  implementationOccurrences() const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableGenericParameterDeclarationFact>&
  genericParameterDeclarations() const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableCallableParameterDeclarationFact>&
  callableParameterDeclarations() const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableModuleAliasFact>& moduleAliases() const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableImportFact>& imports() const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableLocalExportFact>& localExports() const noexcept;
  ZC_NODISCARD const CanonicalNonEmptySequence<StableOwnerBodyQueryKey>& bodyOwners()
      const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableFailedLookupFact>& failedLookups() const noexcept;
  bool operator==(const BoundModuleSkeleton& other) const;
  bool operator!=(const BoundModuleSkeleton& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit BoundModuleSkeleton(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

enum class BinderKeyFailureKind : uint8_t {
  MissingSelectedModuleSource = 0x01,
  InactiveOwner = 0x02,
  ForeignOwner = 0x03,
  DefinitionWithoutBody = 0x04,
  BoundaryMismatch = 0x05,
  NonSelectedSource = 0x06,
  CrossBoundaryPath = 0x07
};

ZC_NODISCARD bool isStableBindingValue(BinderKeyFailureKind value) noexcept;

/// \brief Structurally admitted Binder key failure without a diagnostic payload.
class BinderKeyFailure final {
public:
  BinderKeyFailure(BinderKeyFailure&&) noexcept = default;
  BinderKeyFailure& operator=(BinderKeyFailure&&) noexcept = default;
  ZC_DISALLOW_COPY(BinderKeyFailure);
  ZC_NODISCARD static zc::Maybe<BinderKeyFailure> from(BinderKeyFailureKind kind,
                                                       BinderQueryOwner&& owner,
                                                       zc::Maybe<LocalSyntaxPath>&& path);
  ZC_NODISCARD BinderKeyFailure clone() const;
  ZC_NODISCARD BinderKeyFailureKind kind() const noexcept;
  ZC_NODISCARD const BinderQueryOwner& owner() const noexcept;
  ZC_NODISCARD const zc::Maybe<LocalSyntaxPath>& path() const noexcept;
  bool operator==(const BinderKeyFailure& other) const;
  bool operator!=(const BinderKeyFailure& other) const { return !(*this == other); }

private:
  BinderKeyFailure(BinderKeyFailureKind kind, BinderQueryOwner&& owner,
                   zc::Maybe<LocalSyntaxPath>&& path) noexcept;
  BinderKeyFailureKind kindField;
  BinderQueryOwner ownerField;
  zc::Maybe<LocalSyntaxPath> pathField;
};

template <typename T>
struct BinderQueryValue final {
  T value;
  CanonicalSequence<diagnostics::DiagnosticFact> diagnostics;
  ZC_NODISCARD BinderQueryValue clone() const {
    return {stable_binding_detail::cloneElement(value), diagnostics.clone()};
  }
  bool operator==(const BinderQueryValue& other) const {
    return stable_binding_detail::sameElement(value, other.value) &&
           diagnostics == other.diagnostics;
  }
};

struct BinderSourceRejected final {
  CanonicalNonEmptySequence<diagnostics::DiagnosticFact> diagnostics;
  ZC_NODISCARD BinderSourceRejected clone() const { return {diagnostics.clone()}; }
  bool operator==(const BinderSourceRejected& other) const {
    return diagnostics == other.diagnostics;
  }
};

struct BinderKeyRejected final {
  BinderKeyFailure failure;
  ZC_NODISCARD BinderKeyRejected clone() const { return {failure.clone()}; }
  bool operator==(const BinderKeyRejected& other) const { return failure == other.failure; }
};

template <typename T>
using BinderQueryResultValue =
    zc::OneOf<BinderQueryValue<T>, BinderSourceRejected, BinderKeyRejected>;

/// \brief Exclusive success, source-rejection, or key-rejection Binder result.
template <typename T>
class BinderQueryResult final {
public:
  BinderQueryResult(BinderQueryResult&&) noexcept = default;
  BinderQueryResult& operator=(BinderQueryResult&&) noexcept = default;
  ZC_DISALLOW_COPY(BinderQueryResult);
  ZC_NODISCARD static BinderQueryResult value(
      T&& value, CanonicalSequence<diagnostics::DiagnosticFact>&& diagnostics) {
    return BinderQueryResult(BinderQueryValue<T>{zc::mv(value), zc::mv(diagnostics)});
  }
  ZC_NODISCARD static BinderQueryResult sourceRejected(
      CanonicalNonEmptySequence<diagnostics::DiagnosticFact>&& diagnostics) {
    return BinderQueryResult(BinderSourceRejected{zc::mv(diagnostics)});
  }
  ZC_NODISCARD static BinderQueryResult keyRejected(BinderKeyFailure&& failure) {
    return BinderQueryResult(BinderKeyRejected{zc::mv(failure)});
  }
  ZC_NODISCARD BinderQueryResult clone() const {
    if (valueField.template is<BinderQueryValue<T>>()) {
      return BinderQueryResult(valueField.template get<BinderQueryValue<T>>().clone());
    }
    if (valueField.template is<BinderSourceRejected>()) {
      return BinderQueryResult(valueField.template get<BinderSourceRejected>().clone());
    }
    return BinderQueryResult(valueField.template get<BinderKeyRejected>().clone());
  }
  ZC_NODISCARD const BinderQueryResultValue<T>& storage() const noexcept { return valueField; }
  bool operator==(const BinderQueryResult& other) const {
    if (valueField.template is<BinderQueryValue<T>>()) {
      return other.valueField.template is<BinderQueryValue<T>>() &&
             valueField.template get<BinderQueryValue<T>>() ==
                 other.valueField.template get<BinderQueryValue<T>>();
    }
    if (valueField.template is<BinderSourceRejected>()) {
      return other.valueField.template is<BinderSourceRejected>() &&
             valueField.template get<BinderSourceRejected>() ==
                 other.valueField.template get<BinderSourceRejected>();
    }
    return other.valueField.template is<BinderKeyRejected>() &&
           valueField.template get<BinderKeyRejected>() ==
               other.valueField.template get<BinderKeyRejected>();
  }
  bool operator!=(const BinderQueryResult& other) const { return !(*this == other); }

private:
  template <typename Variant>
  explicit BinderQueryResult(Variant&& value) : valueField(zc::fwd<Variant>(value)) {}
  BinderQueryResultValue<T> valueField;
};

/// \brief Stable generic-parameter header fact admitted against its identity record and site.
class StableHeaderGenericParameter final {
public:
  ~StableHeaderGenericParameter() noexcept(false);
  StableHeaderGenericParameter(StableHeaderGenericParameter&&) noexcept;
  StableHeaderGenericParameter& operator=(StableHeaderGenericParameter&&) noexcept;
  ZC_DISALLOW_COPY(StableHeaderGenericParameter);

  ZC_NODISCARD static zc::Maybe<StableHeaderGenericParameter> from(
      identity::GenericParameterKey&& key, identity::GenericParameterIdentityRecord&& record,
      StableHeaderSite&& site, identity::DeclaredDefinitionName&& name, uint32_t ordinal);
  ZC_NODISCARD StableHeaderGenericParameter clone() const;
  ZC_NODISCARD const identity::GenericParameterKey& key() const noexcept;
  ZC_NODISCARD const identity::GenericParameterIdentityRecord& record() const noexcept;
  ZC_NODISCARD const StableHeaderSite& site() const noexcept;
  ZC_NODISCARD const identity::DeclaredDefinitionName& name() const noexcept;
  ZC_NODISCARD uint32_t ordinal() const noexcept;
  bool operator==(const StableHeaderGenericParameter& other) const;
  bool operator!=(const StableHeaderGenericParameter& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableHeaderGenericParameter(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable callable-parameter header fact admitted against its identity record and site.
class StableHeaderCallableParameter final {
public:
  ~StableHeaderCallableParameter() noexcept(false);
  StableHeaderCallableParameter(StableHeaderCallableParameter&&) noexcept;
  StableHeaderCallableParameter& operator=(StableHeaderCallableParameter&&) noexcept;
  ZC_DISALLOW_COPY(StableHeaderCallableParameter);

  ZC_NODISCARD static zc::Maybe<StableHeaderCallableParameter> from(
      identity::CallableParameterKey&& key, identity::CallableParameterIdentityRecord&& record,
      StableHeaderSite&& site, zc::Maybe<identity::DeclaredDefinitionName>&& name,
      identity::CallableParameterPosition position);
  ZC_NODISCARD StableHeaderCallableParameter clone() const;
  ZC_NODISCARD const identity::CallableParameterKey& key() const noexcept;
  ZC_NODISCARD const identity::CallableParameterIdentityRecord& record() const noexcept;
  ZC_NODISCARD const StableHeaderSite& site() const noexcept;
  ZC_NODISCARD const zc::Maybe<identity::DeclaredDefinitionName>& name() const noexcept;
  ZC_NODISCARD identity::CallableParameterPosition position() const noexcept;
  bool operator==(const StableHeaderCallableParameter& other) const;
  bool operator!=(const StableHeaderCallableParameter& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableHeaderCallableParameter(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Complete stable definition header admitted against its identity and parameter facts.
class StableDefinitionHeader final {
public:
  ~StableDefinitionHeader() noexcept(false);
  StableDefinitionHeader(StableDefinitionHeader&&) noexcept;
  StableDefinitionHeader& operator=(StableDefinitionHeader&&) noexcept;
  ZC_DISALLOW_COPY(StableDefinitionHeader);

  ZC_NODISCARD static zc::Maybe<StableDefinitionHeader> from(
      StableDefinitionQueryKey&& queryKey, identity::DefinitionIdentityRecord&& record,
      IdentitySyntaxSiteKey&& authoritySite, identity::DefinitionKind kind, Namespace nameSpace,
      identity::DeclaredDefinitionName&& name, DefinitionActivation activation,
      zc::Maybe<MemberVisibility>&& visibility, DefinitionBodyDisposition bodyDisposition,
      CanonicalSequence<StableHeaderGenericParameter>&& genericParameters,
      CanonicalSequence<StableHeaderCallableParameter>&& callableParameters,
      CanonicalSequence<ScopeRole>&& declaredScopeRoles);
  ZC_NODISCARD StableDefinitionHeader clone() const;
  ZC_NODISCARD const StableDefinitionQueryKey& queryKey() const noexcept;
  ZC_NODISCARD const identity::DefinitionIdentityRecord& record() const noexcept;
  ZC_NODISCARD const IdentitySyntaxSiteKey& authoritySite() const noexcept;
  ZC_NODISCARD identity::DefinitionKind kind() const noexcept;
  ZC_NODISCARD Namespace nameSpace() const noexcept;
  ZC_NODISCARD const identity::DeclaredDefinitionName& name() const noexcept;
  ZC_NODISCARD DefinitionActivation activation() const noexcept;
  ZC_NODISCARD const zc::Maybe<MemberVisibility>& visibility() const noexcept;
  ZC_NODISCARD DefinitionBodyDisposition bodyDisposition() const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableHeaderGenericParameter>& genericParameters()
      const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableHeaderCallableParameter>& callableParameters()
      const noexcept;
  ZC_NODISCARD const CanonicalSequence<ScopeRole>& declaredScopeRoles() const noexcept;
  bool operator==(const StableDefinitionHeader& other) const;
  bool operator!=(const StableDefinitionHeader& other) const { return !(*this == other); }

private:
  struct Impl;
  explicit StableDefinitionHeader(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Complete stable header for one source occurrence of an implementation.
class StableImplementationOccurrenceHeader final {
public:
  ~StableImplementationOccurrenceHeader() noexcept(false);
  StableImplementationOccurrenceHeader(StableImplementationOccurrenceHeader&&) noexcept;
  StableImplementationOccurrenceHeader& operator=(StableImplementationOccurrenceHeader&&) noexcept;
  ZC_DISALLOW_COPY(StableImplementationOccurrenceHeader);

  ZC_NODISCARD static zc::Maybe<StableImplementationOccurrenceHeader> from(
      StableImplementationOccurrenceQueryKey&& queryKey, StableImplementationQueryKey&& authority,
      identity::ImplIdentityRecord&& record,
      CanonicalSequence<StableHeaderGenericParameter>&& genericParameters,
      CanonicalSequence<ScopeRole>&& declaredScopeRoles, ImplementationSourceForm sourceForm);
  ZC_NODISCARD StableImplementationOccurrenceHeader clone() const;
  ZC_NODISCARD const StableImplementationOccurrenceQueryKey& queryKey() const noexcept;
  ZC_NODISCARD const StableImplementationQueryKey& authority() const noexcept;
  ZC_NODISCARD const identity::ImplIdentityRecord& record() const noexcept;
  ZC_NODISCARD const CanonicalSequence<StableHeaderGenericParameter>& genericParameters()
      const noexcept;
  ZC_NODISCARD const CanonicalSequence<ScopeRole>& declaredScopeRoles() const noexcept;
  ZC_NODISCARD ImplementationSourceForm sourceForm() const noexcept;
  bool operator==(const StableImplementationOccurrenceHeader& other) const;
  bool operator!=(const StableImplementationOccurrenceHeader& other) const {
    return !(*this == other);
  }

private:
  struct Impl;
  explicit StableImplementationOccurrenceHeader(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::binder
