// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/node-id.h"
#include "zomlang/compiler/binder/immutable-definition-inventory.h"
#include "zomlang/compiler/binder/parsed-module.h"
#include "zomlang/compiler/checker/checker-diagnostic-id.h"
#include "zomlang/compiler/checker/checker-identity-authority.h"
#include "zomlang/compiler/checker/cross-module-facts.h"
#include "zomlang/compiler/checker/operator-kind.h"
#include "zomlang/compiler/checker/signature-facts.h"
#include "zomlang/compiler/diagnostics/core/diagnostic-ids.h"
#include "zomlang/compiler/identity/crate-key.h"
#include "zomlang/compiler/identity/handle.h"
#include "zomlang/compiler/identity/semantic-context-fingerprint.h"
#include "zomlang/compiler/identity/sha256.h"
#include "zomlang/compiler/type/semantic-type-store.h"

namespace zomlang::compiler::checker::inference {

class InferenceRecoveryContext;

}  // namespace zomlang::compiler::checker::inference

namespace zomlang::compiler::checker::checked {

using InterfaceInstantiation = signature::InterfaceInstantiation;
using CanonicalConstValue = signature::CanonicalConstValue;
using CanonicalLiteral = signature::CanonicalConstValue;
using Polarity = signature::Polarity;
using ProjectionKey = signature::ProjectionKey;
using MarkerEvidence = signature::MarkerEvidence;
using ReceiverMode = signature::ReceiverMode;

class CheckedFactsCanonicalCodec;

/// \brief One immutable candidate map entry and its independently produced canonical record.
template <typename Key, typename Value>
struct ImmutableFactMapEntry final {
  Key key;
  Value value;
  zc::Array<uint8_t> canonicalRecord;
};

/// \brief Move-only immutable fact map; ordering and uniqueness are verified at publication.
template <typename Key, typename Value>
class ImmutableFactMap final {
public:
  using Entry = ImmutableFactMapEntry<Key, Value>;

  ZC_NODISCARD static ImmutableFactMap fromEntries(zc::Vector<Entry>&& entries) {
    return ImmutableFactMap(zc::mv(entries));
  }
  ImmutableFactMap(ImmutableFactMap&&) noexcept = default;
  ImmutableFactMap& operator=(ImmutableFactMap&&) noexcept = default;
  ZC_DISALLOW_COPY(ImmutableFactMap);

  ZC_NODISCARD zc::ArrayPtr<const Entry> entries() const noexcept { return values.asPtr(); }
  ZC_NODISCARD size_t size() const noexcept { return values.size(); }

private:
  explicit ImmutableFactMap(zc::Vector<Entry>&& entries) : values(zc::mv(entries)) {}
  zc::Vector<Entry> values;
  friend class CheckedFactsCanonicalCodec;
};

class FrozenSubstitutionStore;
class FrozenWitnessStore;

struct CanonicalSubstitutionTag final {
private:
  ZC_NODISCARD static constexpr identity::StoreHandle<CanonicalSubstitutionTag> issue(
      identity::SemanticContextBrand context, identity::RegistryBrand issuer,
      uint32_t slot) noexcept {
    return identity::StoreHandle<CanonicalSubstitutionTag>(context, issuer, slot);
  }
  ZC_NODISCARD static constexpr uint32_t slot(
      identity::StoreHandle<CanonicalSubstitutionTag> handle) noexcept {
    return handle.slot;
  }
  friend class FrozenSubstitutionStore;
};
struct WitnessArgumentsTag final {
private:
  ZC_NODISCARD static constexpr identity::StoreHandle<WitnessArgumentsTag> issue(
      identity::SemanticContextBrand context, identity::RegistryBrand issuer,
      uint32_t slot) noexcept {
    return identity::StoreHandle<WitnessArgumentsTag>(context, issuer, slot);
  }
  ZC_NODISCARD static constexpr uint32_t slot(
      identity::StoreHandle<WitnessArgumentsTag> handle) noexcept {
    return handle.slot;
  }
  friend class FrozenWitnessStore;
};
class FrozenRecoveryLedger;
struct TypeErrorTag final {
private:
  ZC_NODISCARD static constexpr identity::StoreHandle<TypeErrorTag> issue(
      identity::SemanticContextBrand context, identity::RegistryBrand issuer,
      uint32_t slot) noexcept {
    return identity::StoreHandle<TypeErrorTag>(context, issuer, slot);
  }
  ZC_NODISCARD static constexpr uint32_t slot(identity::StoreHandle<TypeErrorTag> handle) noexcept {
    return handle.slot;
  }
  friend class FrozenRecoveryLedger;
  friend class inference::InferenceRecoveryContext;
};
using CanonicalSubstitutionId = identity::StoreHandle<CanonicalSubstitutionTag>;
using WitnessArgumentsId = identity::StoreHandle<WitnessArgumentsTag>;
using TypeErrorId = identity::StoreHandle<TypeErrorTag>;

struct SubstitutionData final {
  zc::Vector<identity::DefId> parameters;
  zc::Vector<identity::SemanticTypeId> arguments;
};

struct AssociatedTypeBindingData final {
  identity::DefId associated;
  identity::SemanticTypeId type;
};

struct WitnessEntry final {
  identity::SemanticTypeId subject;
  InterfaceInstantiation interface;
  identity::ImplId impl;
  zc::Vector<AssociatedTypeBindingData> associatedBindings;
  zc::Vector<WitnessArgumentsId> nested;
};

struct WitnessArgumentsData final {
  zc::Vector<WitnessEntry> entries;
};

template <typename Value>
struct FrozenStoreRecord final {
  Value value;
  zc::Array<uint8_t> canonicalRecord;
};

/// \brief Frozen context-and-issuer-bound canonical substitution store.
class FrozenSubstitutionStore final {
public:
  using Record = FrozenStoreRecord<SubstitutionData>;
  ZC_NODISCARD static zc::Maybe<FrozenSubstitutionStore> from(
      identity::SemanticContextBrand semanticContext, identity::RegistryBrand issuer,
      zc::Vector<Record>&& records);
  ~FrozenSubstitutionStore() noexcept(false);
  FrozenSubstitutionStore(FrozenSubstitutionStore&&) noexcept;
  FrozenSubstitutionStore& operator=(FrozenSubstitutionStore&&) noexcept;
  ZC_DISALLOW_COPY(FrozenSubstitutionStore);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD identity::RegistryBrand issuer() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const Record> records() const noexcept;
  ZC_NODISCARD zc::Maybe<CanonicalSubstitutionId> idAt(uint32_t index) const noexcept;
  ZC_NODISCARD bool contains(CanonicalSubstitutionId id) const noexcept;

private:
  struct Impl;
  explicit FrozenSubstitutionStore(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
  friend class CheckedFactsCanonicalCodec;
};

/// \brief Frozen context-and-issuer-bound canonical witness store.
class FrozenWitnessStore final {
public:
  using Record = FrozenStoreRecord<WitnessArgumentsData>;
  ZC_NODISCARD static zc::Maybe<FrozenWitnessStore> from(
      identity::SemanticContextBrand semanticContext, identity::RegistryBrand issuer,
      zc::Vector<Record>&& records);
  ~FrozenWitnessStore() noexcept(false);
  FrozenWitnessStore(FrozenWitnessStore&&) noexcept;
  FrozenWitnessStore& operator=(FrozenWitnessStore&&) noexcept;
  ZC_DISALLOW_COPY(FrozenWitnessStore);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD identity::RegistryBrand issuer() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const Record> records() const noexcept;
  ZC_NODISCARD zc::Maybe<WitnessArgumentsId> idAt(uint32_t index) const noexcept;
  ZC_NODISCARD bool contains(WitnessArgumentsId id) const noexcept;

private:
  struct Impl;
  explicit FrozenWitnessStore(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
  friend class CheckedFactsCanonicalCodec;
};

enum class CoercionSite : uint8_t {
  AnnotatedInitializer = 0x01,
  Argument = 0x02,
  Return = 0x03,
  AggregateField = 0x04,
  AssignmentRhs = 0x05,
  ConditionalThen = 0x06,
  ConditionalElse = 0x07,
  ExplicitDynAnnotation = 0x08
};

struct NeverToStep final {};
struct ToAnyStep final {};
struct ReborrowSharedStep final {};
struct ReferenceToRawConstStep final {};
struct ReferenceToRawMutableStep final {};
struct RawMutToConstStep final {};
struct UnionInjectStep final {
  uint32_t alternativeIndex;
  identity::SemanticTypeId alternative;
};
struct DynEraseStep final {
  InterfaceInstantiation interface;
  identity::ImplId impl;
  WitnessArgumentsId witnesses;
};
struct DynUpcastStep final {
  zc::Vector<identity::DefId> path;
};

/// \brief Closed implicit coercion step algebra in RFC tag order.
class CoercionStep final {
public:
  explicit CoercionStep(NeverToStep value) noexcept : value(value) {}
  explicit CoercionStep(ToAnyStep value) noexcept : value(value) {}
  explicit CoercionStep(ReborrowSharedStep value) noexcept : value(value) {}
  explicit CoercionStep(ReferenceToRawConstStep value) noexcept : value(value) {}
  explicit CoercionStep(ReferenceToRawMutableStep value) noexcept : value(value) {}
  explicit CoercionStep(RawMutToConstStep value) noexcept : value(value) {}
  explicit CoercionStep(UnionInjectStep value) noexcept : value(value) {}
  explicit CoercionStep(DynEraseStep&& value) : value(zc::mv(value)) {}
  explicit CoercionStep(DynUpcastStep&& value) : value(zc::mv(value)) {}
  CoercionStep(CoercionStep&&) noexcept = default;
  CoercionStep& operator=(CoercionStep&&) noexcept = default;
  ZC_DISALLOW_COPY(CoercionStep);
  ZC_NODISCARD const auto& variant() const noexcept { return value; }

private:
  zc::OneOf<NeverToStep, ToAnyStep, ReborrowSharedStep, ReferenceToRawConstStep,
            ReferenceToRawMutableStep, RawMutToConstStep, UnionInjectStep, DynEraseStep,
            DynUpcastStep>
      value;
};

struct CoercionAdjustment final {
  CoercionSite site;
  identity::SemanticTypeId source;
  identity::SemanticTypeId destination;
  zc::Vector<CoercionStep> steps;
  identity::SourceSpan sourceSpan;
};

enum class CastMode : uint8_t { Guaranteed = 0x01, OptionalChecked = 0x02, ForcedChecked = 0x03 };
enum class UnsafeRequirement : uint8_t { None = 0x01, RawPointerBoundary = 0x02 };
enum class CastKind : uint8_t {
  IntegerWiden = 0x01,
  IntegerNarrowChecked = 0x02,
  FloatWiden = 0x03,
  FloatNarrowChecked = 0x04,
  ReferenceToRawConst = 0x05,
  ReferenceToRawMutable = 0x06,
  RawMutToConst = 0x07,
  AnyDowncastChecked = 0x08,
  ErrorUnionExtractChecked = 0x09,
  DynErase = 0x0a,
  DynUpcast = 0x0b,
  UnionInject = 0x0c,
  RawPointerReinterpret = 0x0d
};

struct CheckedCastFact final {
  ast::NodeId node;
  CastMode mode;
  CastKind kind;
  identity::SemanticTypeId source;
  identity::SemanticTypeId target;
  identity::SemanticTypeId result;
  zc::Maybe<identity::ImplId> impl;
  zc::Maybe<WitnessArgumentsId> witnesses;
  zc::Vector<identity::DefId> dynPath;
  UnsafeRequirement unsafeRequirement;
  identity::SourceSpan sourceSpan;
};

struct DirectCallable final {
  identity::DefId callee;
};
struct ConcreteMethodCallable final {
  identity::DefId method;
};
struct ImplMethodCallable final {
  identity::ImplId impl;
  identity::DefId method;
};
struct WitnessMethodCallable final {
  identity::DefId witnessParameter;
  identity::DefId interface;
  identity::DefId method;
};
struct DynMethodCallable final {
  identity::DefId interface;
  identity::DefId method;
};
struct PrimitiveCallable final {
  checker::PrimitiveOperation operation;
};

class SelectedCallable final {
public:
  explicit SelectedCallable(DirectCallable value) noexcept : value(value) {}
  explicit SelectedCallable(ConcreteMethodCallable value) noexcept : value(value) {}
  explicit SelectedCallable(ImplMethodCallable value) noexcept : value(value) {}
  explicit SelectedCallable(WitnessMethodCallable value) noexcept : value(value) {}
  explicit SelectedCallable(DynMethodCallable value) noexcept : value(value) {}
  explicit SelectedCallable(PrimitiveCallable value) noexcept : value(value) {}
  SelectedCallable(SelectedCallable&&) noexcept = default;
  SelectedCallable& operator=(SelectedCallable&&) noexcept = default;
  ZC_DISALLOW_COPY(SelectedCallable);
  ZC_NODISCARD const auto& variant() const noexcept { return value; }

private:
  zc::OneOf<DirectCallable, ConcreteMethodCallable, ImplMethodCallable, WitnessMethodCallable,
            DynMethodCallable, PrimitiveCallable>
      value;
};

struct CheckedArgumentFact final {
  ast::NodeId sourceNode;
  identity::SemanticTypeId sourceType;
  identity::SemanticTypeId parameterType;
  zc::Maybe<CoercionAdjustment> adjustment;
};

enum class ReceiverAdjustmentStep : uint8_t {
  DereferenceShared = 0x01,
  DereferenceMutable = 0x02,
  BorrowShared = 0x03,
  ReborrowShared = 0x04,
  BorrowMutable = 0x05,
  ReborrowMutable = 0x06,
  MoveValue = 0x07,
  CopyValue = 0x08
};

struct ReceiverAdjustment final {
  identity::SemanticTypeId source;
  identity::SemanticTypeId destination;
  zc::Vector<ReceiverAdjustmentStep> steps;
  identity::SourceSpan sourceSpan;
};

struct CheckedCallEnvelope final {
  SelectedCallable selected;
  identity::SemanticTypeId calleeType;
  zc::Maybe<CheckedArgumentFact> receiver;
  zc::Maybe<ReceiverMode> receiverMode;
  zc::Maybe<ReceiverAdjustment> receiverAdjustment;
  zc::Vector<CheckedArgumentFact> arguments;
  identity::SemanticTypeId successType;
  identity::SemanticTypeId resultType;
  zc::Maybe<CanonicalSubstitutionId> substitutions;
  zc::Maybe<WitnessArgumentsId> witnesses;
  zc::Maybe<identity::SemanticTypeId> raises;
};

struct TypedCallFact final {
  ast::NodeId node;
  CheckedCallEnvelope invocation;
  identity::SourceSpan sourceSpan;
};

struct CheckedLiteralFact final {
  ast::NodeId node;
  CanonicalLiteral literal;
  identity::SemanticTypeId type;
  identity::SourceSpan sourceSpan;
};

struct ConstantEvaluationFact final {
  identity::DefId definition;
  ast::NodeId expression;
  CanonicalConstValue value;
  identity::SemanticTypeId type;
  ImmutableFactMap<identity::DefId, identity::Sha256Digest> dependencies;
  identity::Sha256Digest evaluationRevision;
};

struct TupleAggregate final {};
struct ArrayAggregate final {};
struct ObjectAggregate final {};
struct NominalAggregate final {
  identity::DefId definition;
};

class AggregateKind final {
public:
  explicit AggregateKind(TupleAggregate value) noexcept : value(value) {}
  explicit AggregateKind(ArrayAggregate value) noexcept : value(value) {}
  explicit AggregateKind(ObjectAggregate value) noexcept : value(value) {}
  explicit AggregateKind(NominalAggregate value) noexcept : value(value) {}
  AggregateKind(AggregateKind&&) noexcept = default;
  AggregateKind& operator=(AggregateKind&&) noexcept = default;
  ZC_DISALLOW_COPY(AggregateKind);
  ZC_NODISCARD const auto& variant() const noexcept { return value; }

private:
  zc::OneOf<TupleAggregate, ArrayAggregate, ObjectAggregate, NominalAggregate> value;
};

struct AggregateElementFact final {
  ast::NodeId sourceNode;
  zc::Maybe<identity::DefId> field;
  uint32_t index;
  identity::SemanticTypeId sourceType;
  identity::SemanticTypeId destinationType;
  zc::Maybe<CoercionAdjustment> adjustment;
};

struct CheckedAggregateFact final {
  ast::NodeId node;
  AggregateKind kind;
  identity::SemanticTypeId resultType;
  zc::Vector<AggregateElementFact> elements;
  identity::SourceSpan sourceSpan;
};

struct CompoundAssignmentFact final {
  ast::NodeId node;
  ast::NodeId placeNode;
  checker::CompoundAssignmentOperation operation;
  CheckedCallEnvelope invocation;
  zc::Maybe<CoercionAdjustment> writebackAdjustment;
  identity::SourceSpan sourceSpan;
};

struct DefinitionPlaceRoot final {
  identity::DefId definition;
};
struct OwnerLocalPlaceRoot final {
  binder::OwnerLocalBindingId binding;
};
struct CallableParameterPlaceRoot final {
  identity::CallableParameterId parameter;
};
struct DereferencePlaceRoot final {
  ast::NodeId node;
};
struct TemporaryPlaceRoot final {
  ast::NodeId node;
};

class PlaceRoot final {
public:
  explicit PlaceRoot(DefinitionPlaceRoot value) noexcept : value(value) {}
  explicit PlaceRoot(OwnerLocalPlaceRoot value) noexcept : value(value) {}
  explicit PlaceRoot(CallableParameterPlaceRoot value) noexcept : value(value) {}
  explicit PlaceRoot(DereferencePlaceRoot value) noexcept : value(value) {}
  explicit PlaceRoot(TemporaryPlaceRoot value) noexcept : value(value) {}
  PlaceRoot(PlaceRoot&&) noexcept = default;
  PlaceRoot& operator=(PlaceRoot&&) noexcept = default;
  ZC_DISALLOW_COPY(PlaceRoot);
  ZC_NODISCARD const auto& variant() const noexcept { return value; }

private:
  zc::OneOf<DefinitionPlaceRoot, OwnerLocalPlaceRoot, CallableParameterPlaceRoot,
            DereferencePlaceRoot, TemporaryPlaceRoot>
      value;
};

struct FieldProjection final {
  identity::DefId field;
};
struct TupleIndexProjection final {
  uint32_t index;
};
struct IndexProjection final {
  ast::NodeId index;
};

class PlaceProjection final {
public:
  explicit PlaceProjection(FieldProjection value) noexcept : value(value) {}
  explicit PlaceProjection(TupleIndexProjection value) noexcept : value(value) {}
  explicit PlaceProjection(IndexProjection value) noexcept : value(value) {}
  PlaceProjection(PlaceProjection&&) noexcept = default;
  PlaceProjection& operator=(PlaceProjection&&) noexcept = default;
  ZC_DISALLOW_COPY(PlaceProjection);
  ZC_NODISCARD const auto& variant() const noexcept { return value; }

private:
  zc::OneOf<FieldProjection, TupleIndexProjection, IndexProjection> value;
};

struct CheckedPlaceFact final {
  ast::NodeId node;
  PlaceRoot root;
  zc::Vector<PlaceProjection> projections;
  identity::SemanticTypeId type;
  bool mutablePlace;
  bool movable;
};

struct CheckedMemberFact final {
  ast::NodeId node;
  identity::SemanticTypeId receiverType;
  identity::DefId member;
  identity::SemanticTypeId memberType;
  zc::Maybe<CoercionAdjustment> adjustment;
};

enum class IndexAccessMode : uint8_t { Read = 0x01, MutablePlace = 0x02 };
struct CheckedIndexFact final {
  ast::NodeId node;
  identity::SemanticTypeId collectionType;
  identity::SemanticTypeId indexType;
  identity::SemanticTypeId elementType;
  IndexAccessMode accessMode;
  identity::SemanticTypeId accessResultType;
};

struct WildcardPattern final {};
struct LiteralPattern final {
  CanonicalLiteral value;
};
struct TuplePattern final {
  uint32_t arity;
};
struct ObjectPattern final {
  zc::Vector<identity::SemanticIdentifier> fields;
};
struct UnionAlternativePattern final {
  uint32_t index;
  identity::SemanticTypeId type;
};
struct EnumVariantPattern final {
  identity::DefId variant;
};
struct NominalPattern final {
  identity::DefId definition;
};

class PatternConstructor final {
public:
  explicit PatternConstructor(WildcardPattern value) noexcept : value(value) {}
  explicit PatternConstructor(LiteralPattern&& value) : value(zc::mv(value)) {}
  explicit PatternConstructor(TuplePattern value) noexcept : value(value) {}
  explicit PatternConstructor(ObjectPattern&& value) : value(zc::mv(value)) {}
  explicit PatternConstructor(UnionAlternativePattern value) noexcept : value(value) {}
  explicit PatternConstructor(EnumVariantPattern value) noexcept : value(value) {}
  explicit PatternConstructor(NominalPattern value) noexcept : value(value) {}
  PatternConstructor(PatternConstructor&&) noexcept = default;
  PatternConstructor& operator=(PatternConstructor&&) noexcept = default;
  ZC_DISALLOW_COPY(PatternConstructor);
  ZC_NODISCARD const auto& variant() const noexcept { return value; }

private:
  zc::OneOf<WildcardPattern, LiteralPattern, TuplePattern, ObjectPattern, UnionAlternativePattern,
            EnumVariantPattern, NominalPattern>
      value;
};

struct PatternBindingFact final {
  identity::DefId binding;
  identity::SemanticTypeId type;
};
struct PatternRefinementFact final {
  ast::NodeId node;
  identity::SemanticTypeId type;
};
struct CheckedPatternFact final {
  ast::NodeId node;
  identity::SemanticTypeId scrutineeType;
  PatternConstructor constructor;
  zc::Vector<PatternBindingFact> bindings;
  zc::Vector<PatternRefinementFact> refinements;
  bool reachable;
  zc::Maybe<identity::SemanticTypeId> guardMayRaise;
};

enum class ExhaustivenessDomain : uint8_t { Closed = 0x01, OpenRequiresCatchAll = 0x02 };
struct ExhaustivenessFact final {
  ast::NodeId node;
  identity::SemanticTypeId scrutineeType;
  ExhaustivenessDomain domain;
  zc::Vector<PatternConstructor> coveredConstructors;
  zc::Vector<PatternConstructor> missingConstructors;
  zc::Vector<ast::NodeId> unreachableArms;
};

enum class ObservedOperation : uint8_t {
  Raise = 0x01,
  MutateReceiver = 0x02,
  UnsafeBoundary = 0x03,
  Suspend = 0x04
};
struct ObservedOperationFact final {
  ast::NodeId node;
  ObservedOperation operation;
  zc::Maybe<identity::SemanticTypeId> raisedType;
  identity::SourceSpan sourceSpan;
};

enum class UnsafeOperation : uint8_t {
  RawDereference = 0x01,
  RawCast = 0x02,
  ExternCall = 0x03,
  Transmute = 0x04,
  PackedFieldAccess = 0x05
};
struct UnsafeScopeFact final {
  ast::NodeId operationNode;
  UnsafeOperation operation;
  zc::Maybe<ast::NodeId> enclosingUnsafeNode;
  bool acknowledged;
};

enum class CaptureMode : uint8_t {
  SharedReference = 0x01,
  MutableReference = 0x02,
  Move = 0x03,
  Copy = 0x04
};
enum class CaptureOrigin : uint8_t { Explicit = 0x01, Inferred = 0x02 };
struct CheckedCaptureFact final {
  binder::AnonymousOwnerLocalKey closure;
  binder::BindingTarget target;
  CheckedPlaceFact place;
  CaptureMode mode;
  CaptureOrigin origin;
  identity::SemanticTypeId capturedType;
  identity::SourceSpan sourceSpan;
};
struct CaptureKey final {
  binder::AnonymousOwnerLocalKey closure;
  binder::BindingTarget target;
  ZC_NODISCARD CaptureKey clone() const;
  bool operator==(const CaptureKey& other) const noexcept;
};

struct ProjectionFact final {
  ast::NodeId node;
  ProjectionKey key;
  identity::SemanticTypeId result;
  identity::ImplId impl;
  WitnessArgumentsId witnesses;
};

struct NoImplResolution final {};
struct UniqueImplResolution final {
  identity::ImplId impl;
  CanonicalSubstitutionId substitutions;
  WitnessArgumentsId witnesses;
};
struct AmbiguousImplResolution final {
  zc::Vector<identity::ImplId> candidates;
};
class ImplResolution final {
public:
  explicit ImplResolution(NoImplResolution value) noexcept : value(value) {}
  explicit ImplResolution(UniqueImplResolution value) noexcept : value(value) {}
  explicit ImplResolution(AmbiguousImplResolution&& value) : value(zc::mv(value)) {}
  ImplResolution(ImplResolution&&) noexcept = default;
  ImplResolution& operator=(ImplResolution&&) noexcept = default;
  ZC_DISALLOW_COPY(ImplResolution);
  ZC_NODISCARD const auto& variant() const noexcept { return value; }

private:
  zc::OneOf<NoImplResolution, UniqueImplResolution, AmbiguousImplResolution> value;
};

struct ObligationFact final {
  ast::NodeId node;
  identity::SemanticTypeId subject;
  InterfaceInstantiation interface;
  ImplResolution resolution;
};
struct MarkerObligationFact final {
  ast::NodeId node;
  identity::SemanticTypeId subject;
  identity::DefId marker;
  Polarity polarity;
  MarkerEvidence evidence;
};

enum class ErrorOperatorKind : uint8_t { Propagate = 0x01, ForcedUnwrap = 0x02 };
enum class ErrorUnionShapeOrigin : uint8_t {
  RaisingCall = 0x01,
  BindingFlow = 0x02,
  ControlFlowJoin = 0x03,
  Coercion = 0x04
};
struct ErrorUnionShapeFact final {
  ast::NodeId node;
  identity::SemanticTypeId valueType;
  identity::SemanticTypeId successType;
  identity::SemanticTypeId residualType;
  ErrorUnionShapeOrigin origin;
  identity::SourceSpan sourceSpan;
};
struct ErrorOperatorFact final {
  ast::NodeId node;
  ErrorOperatorKind kind;
  identity::SemanticTypeId operandType;
  identity::SemanticTypeId successType;
  identity::SemanticTypeId residualType;
  zc::Maybe<identity::SemanticTypeId> enclosingRaises;
  identity::SourceSpan sourceSpan;
};

enum class CheckerDiagnosticStage : uint8_t {
  Signature = 0x01,
  Coherence = 0x02,
  Body = 0x03,
  Exhaustiveness = 0x04,
  ConstantEvaluation = 0x05,
  Advisory = 0x06
};
enum class CheckerDiagnosticProducer : uint8_t {
  DynUse = 0x01,
  Inference = 0x02,
  Call = 0x03,
  Coherence = 0x04,
  Obligation = 0x05,
  Projection = 0x06,
  Exhaustiveness = 0x07,
  Mutation = 0x08,
  ErrorOperator = 0x09,
  Operator = 0x0a,
  Dereference = 0x0b,
  Index = 0x0c,
  Cast = 0x0d,
  Condition = 0x0e,
  Return = 0x0f,
  Aggregate = 0x10,
  Alias = 0x11,
  Orphan = 0x12,
  Constant = 0x13
};
struct CheckerEmitterOrdinal final {
  uint8_t stageTag;
  uint32_t ownerSchemaPreorder;
  uint32_t siteSchemaPreorder;
  uint32_t itemOrdinal;
};

enum class ConstraintReasonKind : uint8_t {
  Annotation = 0x01,
  Initializer = 0x02,
  Argument = 0x03,
  Return = 0x04,
  Assignment = 0x05,
  ConditionalJoin = 0x06,
  Operator = 0x07,
  Projection = 0x08,
  Bound = 0x09,
  Raises = 0x0a,
  Pattern = 0x0b,
  Cast = 0x0c
};

struct TypeDisplayArg final {
  identity::SemanticTypeId type;
  zc::Maybe<identity::SemanticIdentifier> sourceAlias;
};
struct PrimitiveTypeDisplayArg final {
  type::semantic::PrimitiveKind kind;
};
struct DefinitionDisplayArg final {
  identity::DefId definition;
};
struct IdentifierDisplayArg final {
  identity::SemanticIdentifier identifier;
};
struct CountDisplayArg final {
  uint64_t count;
};
struct ConstraintContextDisplayArg final {
  ConstraintReasonKind reason;
};
struct OperatorDisplayArg final {
  checker::OperatorKind operation;
};
struct LiteralDisplayArg final {
  CanonicalLiteral literal;
};
struct PatternsDisplayArg final {
  zc::Vector<PatternConstructor> patterns;
};

/// \brief Closed retained diagnostic argument algebra; rendering strings are not semantic facts.
class CheckerDisplayArgument final {
public:
  explicit CheckerDisplayArgument(TypeDisplayArg&& value) : value(zc::mv(value)) {}
  explicit CheckerDisplayArgument(PrimitiveTypeDisplayArg value) noexcept : value(value) {}
  explicit CheckerDisplayArgument(DefinitionDisplayArg value) noexcept : value(value) {}
  explicit CheckerDisplayArgument(IdentifierDisplayArg&& value) : value(zc::mv(value)) {}
  explicit CheckerDisplayArgument(CountDisplayArg value) noexcept : value(value) {}
  explicit CheckerDisplayArgument(ConstraintContextDisplayArg value) noexcept : value(value) {}
  explicit CheckerDisplayArgument(OperatorDisplayArg&& value) : value(zc::mv(value)) {}
  explicit CheckerDisplayArgument(LiteralDisplayArg&& value) : value(zc::mv(value)) {}
  explicit CheckerDisplayArgument(PatternsDisplayArg&& value) : value(zc::mv(value)) {}
  CheckerDisplayArgument(CheckerDisplayArgument&&) noexcept = default;
  CheckerDisplayArgument& operator=(CheckerDisplayArgument&&) noexcept = default;
  ZC_DISALLOW_COPY(CheckerDisplayArgument);
  ZC_NODISCARD const auto& variant() const noexcept { return value; }

private:
  zc::OneOf<TypeDisplayArg, PrimitiveTypeDisplayArg, DefinitionDisplayArg, IdentifierDisplayArg,
            CountDisplayArg, ConstraintContextDisplayArg, OperatorDisplayArg, LiteralDisplayArg,
            PatternsDisplayArg>
      value;
};

struct CheckerNoteRef final {
  CheckerNoteId diagnostic;
  identity::SourceSpan span;
  zc::Vector<CheckerDisplayArgument> arguments;
  zc::Maybe<identity::DefId> causeDefinition;
};

enum class CheckerRecoveryClass : uint8_t {
  TypeMismatch = 0x01,
  InvalidOperation = 0x02,
  InvalidTypeExpression = 0x03,
  FailedObligation = 0x04,
  FailedProjection = 0x05,
  FailedInference = 0x06
};
struct NoRecoveryPolicy final {};
struct CreateRootRecoveryPolicy final {
  CheckerRecoveryClass recoveryClass;
  bool suppressIfChildRecovery;
};
struct AdvisoryAfterSuccessRecoveryPolicy final {};

class CheckerRecoveryPolicy final {
public:
  explicit CheckerRecoveryPolicy(NoRecoveryPolicy value) noexcept : value(value) {}
  explicit CheckerRecoveryPolicy(CreateRootRecoveryPolicy value) noexcept : value(value) {}
  explicit CheckerRecoveryPolicy(AdvisoryAfterSuccessRecoveryPolicy value) noexcept
      : value(value) {}
  CheckerRecoveryPolicy(CheckerRecoveryPolicy&&) noexcept = default;
  CheckerRecoveryPolicy& operator=(CheckerRecoveryPolicy&&) noexcept = default;
  ZC_DISALLOW_COPY(CheckerRecoveryPolicy);
  ZC_NODISCARD const auto& variant() const noexcept { return value; }

private:
  zc::OneOf<NoRecoveryPolicy, CreateRootRecoveryPolicy, AdvisoryAfterSuccessRecoveryPolicy> value;
};

struct CheckerFailureRef final {
  CheckerErrorId diagnostic;
  CheckerDiagnosticStage stage;
  ast::NodeId primaryNode;
  identity::SourceSpan primarySpan;
  zc::Vector<CheckerDisplayArgument> arguments;
  zc::Vector<CheckerNoteRef> notes;
  CheckerDiagnosticProducer producer;
  CheckerRecoveryPolicy recoveryPolicy;
  CheckerEmitterOrdinal emitterOrdinal;
  zc::Maybe<TypeErrorId> recovery;
};
struct CheckerAdvisoryRef final {
  CheckerWarningId diagnostic;
  CheckerDiagnosticStage stage;
  ast::NodeId primaryNode;
  identity::SourceSpan primarySpan;
  zc::Vector<CheckerDisplayArgument> arguments;
  zc::Vector<CheckerNoteRef> notes;
  CheckerDiagnosticProducer producer;
  CheckerEmitterOrdinal emitterOrdinal;
};

/// \brief Opaque frozen recovery bytes retained only by source rejection.
class FrozenRecoveryLedger final {
public:
  ZC_NODISCARD static zc::Maybe<FrozenRecoveryLedger> from(
      identity::SemanticContextBrand semanticContext, identity::RegistryBrand issuer,
      uint32_t errorCount, zc::Array<uint8_t>&& canonicalRecord);
  FrozenRecoveryLedger(FrozenRecoveryLedger&&) noexcept = default;
  FrozenRecoveryLedger& operator=(FrozenRecoveryLedger&&) noexcept = default;
  ZC_DISALLOW_COPY(FrozenRecoveryLedger);
  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept { return context; }
  ZC_NODISCARD identity::RegistryBrand issuer() const noexcept { return registry; }
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> canonicalRecord() const noexcept {
    return record.asPtr();
  }
  ZC_NODISCARD uint32_t errorCount() const noexcept { return errorCountValue; }
  ZC_NODISCARD zc::Maybe<TypeErrorId> idAt(uint32_t index) const noexcept;
  ZC_NODISCARD bool contains(TypeErrorId id) const noexcept;

private:
  FrozenRecoveryLedger(identity::SemanticContextBrand context, identity::RegistryBrand registry,
                       uint32_t errorCount, zc::Array<uint8_t>&& record) noexcept
      : context(context), registry(registry), errorCountValue(errorCount), record(zc::mv(record)) {}
  identity::SemanticContextBrand context;
  identity::RegistryBrand registry;
  uint32_t errorCountValue;
  zc::Array<uint8_t> record;
};

using NodeTypeMap = ImmutableFactMap<ast::NodeId, identity::SemanticTypeId>;
using DefinitionTypeMap = ImmutableFactMap<identity::DefId, identity::SemanticTypeId>;
using LiteralFactMap = ImmutableFactMap<ast::NodeId, CheckedLiteralFact>;
using ConstantFactMap = ImmutableFactMap<identity::DefId, ConstantEvaluationFact>;
using AggregateFactMap = ImmutableFactMap<ast::NodeId, CheckedAggregateFact>;
using PlaceFactMap = ImmutableFactMap<ast::NodeId, CheckedPlaceFact>;
using CoercionFactMap = ImmutableFactMap<ast::NodeId, CoercionAdjustment>;
using CastFactMap = ImmutableFactMap<ast::NodeId, CheckedCastFact>;
using CallFactMap = ImmutableFactMap<ast::NodeId, TypedCallFact>;
using CompoundAssignmentFactMap = ImmutableFactMap<ast::NodeId, CompoundAssignmentFact>;
using MemberFactMap = ImmutableFactMap<ast::NodeId, CheckedMemberFact>;
using IndexFactMap = ImmutableFactMap<ast::NodeId, CheckedIndexFact>;
using PatternFactMap = ImmutableFactMap<ast::NodeId, CheckedPatternFact>;
using ObservedOperationFactMap = ImmutableFactMap<ast::NodeId, ObservedOperationFact>;
using CaptureFactMap = ImmutableFactMap<CaptureKey, CheckedCaptureFact>;
using MarkerObligationFactMap = ImmutableFactMap<ast::NodeId, MarkerObligationFact>;
using ExhaustivenessFactMap = ImmutableFactMap<ast::NodeId, ExhaustivenessFact>;
using UnsafeOperationFactMap = ImmutableFactMap<ast::NodeId, UnsafeScopeFact>;
using ProjectionFactMap = ImmutableFactMap<ast::NodeId, ProjectionFact>;
using ObligationFactMap = ImmutableFactMap<ast::NodeId, ObligationFact>;
using ErrorUnionShapeFactMap = ImmutableFactMap<ast::NodeId, ErrorUnionShapeFact>;
using ErrorOperatorFactMap = ImmutableFactMap<ast::NodeId, ErrorOperatorFact>;

struct CheckedFactsCandidate final {
  identity::SemanticContextBrand semanticContext;
  identity::SemanticContextFingerprint contextFingerprint;
  identity::ModuleId module;
  identity::Sha256Digest sourceContentDigest;
  binder::ParsedModuleReceipt parsedModuleReceipt;
  signature::SignatureFactsRevision signatureFactsRevision;
  cross_module::ImportedSignatureViewRevision importedSignatureViewRevision;
  cross_module::CoherenceViewRevision coherenceViewRevision;
  identity::SemanticCompilerOptionsKey semanticOptions;
  FrozenSubstitutionStore substitutionStore;
  FrozenWitnessStore witnessStore;
  NodeTypeMap nodeTypes;
  DefinitionTypeMap definitionTypes;
  LiteralFactMap literals;
  ConstantFactMap constants;
  AggregateFactMap aggregates;
  PlaceFactMap places;
  CoercionFactMap coercions;
  CastFactMap casts;
  CallFactMap calls;
  CompoundAssignmentFactMap compoundAssignments;
  MemberFactMap members;
  IndexFactMap indexes;
  PatternFactMap patterns;
  ObservedOperationFactMap observedOperations;
  CaptureFactMap captures;
  MarkerObligationFactMap markerObligations;
  ExhaustivenessFactMap exhaustiveness;
  UnsafeOperationFactMap unsafeOperations;
  ProjectionFactMap projections;
  ObligationFactMap obligations;
  ErrorUnionShapeFactMap errorUnionShapes;
  ErrorOperatorFactMap errorOperators;
  zc::Vector<FrozenRecoveryLedger> recoveryLedgers;
  zc::Vector<CheckerFailureRef> sourceFailures;
  zc::Vector<CheckerAdvisoryRef> advisories;
};

struct CheckedNodeKey final {
  uint32_t syntaxKind;
  uint32_t schemaPreorder;
  identity::SourceSpan sourceSpan;
};

/// \brief Exact ordered canonical record groups for RFC 0005 checked-facts framing.
struct CheckedFactsCanonicalGroups final {
  zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> substitutions;
  zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> witnesses;
  zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> nodeTypes;
  zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> definitionTypes;
  zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> literals;
  zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> constants;
  zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> aggregates;
  zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> places;
  zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> coercions;
  zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> casts;
  zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> calls;
  zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> compoundAssignments;
  zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> members;
  zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> indexes;
  zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> patterns;
  zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> observedOperations;
  zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> captures;
  zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> markerObligations;
  zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> exhaustiveness;
  zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> unsafeOperations;
  zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> projections;
  zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> obligations;
  zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> errorUnionShapes;
  zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> errorOperators;
};

/// \brief Domain-separated revision of one immutable verified checked-facts publication.
class CheckedFactsRevision final {
public:
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;
  ZC_NODISCARD static zc::Maybe<CheckedFactsRevision> computeFramed(
      const identity::Sha256Digest& contextFingerprint,
      zc::ArrayPtr<const uint8_t> expandedOwningModule,
      const identity::Sha256Digest& sourceContentDigest,
      const identity::Sha256Digest& parsedModuleReceipt,
      const identity::Sha256Digest& signatureFactsRevision,
      const identity::Sha256Digest& importedSignatureViewRevision,
      const identity::Sha256Digest& coherenceViewRevision,
      const identity::SemanticCompilerOptionsKey& semanticOptions,
      const CheckedFactsCanonicalGroups& groups);

private:
  explicit CheckedFactsRevision(const identity::Sha256Digest& value) noexcept;
  identity::Sha256Digest value;
  friend class CheckedFactsVerifier;
};

enum class CheckedFactGroup : uint8_t {
  NodeType = 0x01,
  DefinitionType = 0x02,
  Literal = 0x03,
  Constant = 0x04,
  Aggregate = 0x05,
  Place = 0x06,
  Coercion = 0x07,
  Cast = 0x08,
  Call = 0x09,
  CompoundAssignment = 0x0a,
  Member = 0x0b,
  Index = 0x0c,
  Pattern = 0x0d,
  ObservedOperation = 0x0e,
  Capture = 0x0f,
  MarkerObligation = 0x10,
  Exhaustiveness = 0x11,
  UnsafeOperation = 0x12,
  Projection = 0x13,
  Obligation = 0x14,
  ErrorUnionShape = 0x15,
  ErrorOperator = 0x16
};
struct NodeFactRequirement final {
  CheckedFactGroup group;
  ast::NodeId node;
  CheckedNodeKey key;
};
struct DefinitionFactRequirement final {
  CheckedFactGroup group;
  identity::DefId definition;
};
struct CaptureFactRequirement final {
  CaptureKey key;
};

struct CheckedFactsVerificationInput final {
  identity::SemanticContextBrand semanticContext;
  const identity::SemanticContextFingerprint& contextFingerprint;
  identity::ModuleId module;
  const identity::SourceFileKey& source;
  const identity::Sha256Digest& sourceContentDigest;
  const binder::ParsedModuleReceipt& parsedModuleReceipt;
  const signature::SignatureFactsRevision& signatureFactsRevision;
  const cross_module::ImportedSignatureViewRevision& importedSignatureViewRevision;
  const cross_module::CoherenceViewRevision& coherenceViewRevision;
  const identity::SemanticCompilerOptionsKey& semanticOptions;
  zc::ArrayPtr<const NodeFactRequirement> nodeRequirements;
  zc::ArrayPtr<const DefinitionFactRequirement> definitionRequirements;
  zc::ArrayPtr<const CaptureFactRequirement> captureRequirements;
  zc::ArrayPtr<const identity::DefId> importedDefinitions;
  zc::ArrayPtr<const identity::ImplId> coherentImpls;
  zc::ArrayPtr<const CheckerFailureRef> registeredPrimaryFailures;
  zc::ArrayPtr<const binder::MaterializedOwnerLocalBindingInventoryEntry> ownerLocalBindings;
  zc::ArrayPtr<const binder::MaterializedAnonymousEntityEntry> anonymousEntities;
  const CheckerIdentityAuthority& identities;
  const type::SemanticTypeStore& semanticTypes;
};

/// \brief The single canonical encoder for every RFC 0005 checked-fact record family.
class CheckedFactsCanonicalCodec final {
public:
  /// \brief Canonically encode one retained diagnostic argument without identity handle slots.
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encodeDisplayArgument(
      const CheckerDisplayArgument& argument, identity::ModuleId module,
      const CheckerIdentityAuthority& identities, const type::SemanticTypeStore& semanticTypes);
  /// \brief Populate every producer record from typed facts, then canonically order map entries.
  ZC_NODISCARD static bool writeCanonicalRecords(CheckedFactsCandidate& candidate,
                                                 const CheckedFactsVerificationInput& input);
  /// \brief Independently derive one constant evaluation revision without its stored digest.
  ZC_NODISCARD static zc::Maybe<identity::Sha256Digest> computeConstantEvaluationRevision(
      const ConstantEvaluationFact& fact, const CheckedFactsCandidate& candidate,
      const CheckedFactsVerificationInput& input);
  /// \brief Re-encode one substitution store record from its typed value.
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encodeSubstitution(
      uint32_t recordIndex, const CheckedFactsCandidate& candidate,
      const CheckedFactsVerificationInput& input);
  /// \brief Re-encode one witness store record, recursively expanding nested records.
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encodeWitness(
      uint32_t recordIndex, const CheckedFactsCandidate& candidate,
      const CheckedFactsVerificationInput& input);
  /// \brief Re-encode one node-keyed record using the verified CheckedNodeKey projection.
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encodeNodeFact(
      CheckedFactGroup group, ast::NodeId node, const CheckedFactsCandidate& candidate,
      const CheckedFactsVerificationInput& input);
  /// \brief Re-encode one definition-keyed record using its expanded DefinitionKey.
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encodeDefinitionFact(
      CheckedFactGroup group, identity::DefId definition, const CheckedFactsCandidate& candidate,
      const CheckedFactsVerificationInput& input);
  /// \brief Re-encode one capture record using expanded closure and binding identities.
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encodeCaptureFact(
      const CaptureKey& key, const CheckedFactsCandidate& candidate,
      const CheckedFactsVerificationInput& input);
  /// \brief Independently re-encode and compare every candidate record byte-for-byte.
  ZC_NODISCARD static bool recordsMatch(const CheckedFactsCandidate& candidate,
                                        const CheckedFactsVerificationInput& input);
};

/// \brief Immutable successful body fact capability; only the verifier may construct it.
class VerifiedCheckedFacts final {
public:
  ~VerifiedCheckedFacts() noexcept(false);
  VerifiedCheckedFacts(VerifiedCheckedFacts&&) noexcept;
  VerifiedCheckedFacts& operator=(VerifiedCheckedFacts&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedCheckedFacts);

  ZC_NODISCARD const CheckedFactsRevision& revision() const noexcept;
  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const signature::SignatureFactsRevision& signatureFactsRevision() const noexcept;
  ZC_NODISCARD const cross_module::ImportedSignatureViewRevision& importedSignatureViewRevision()
      const noexcept;
  ZC_NODISCARD const cross_module::CoherenceViewRevision& coherenceViewRevision() const noexcept;
  ZC_NODISCARD const FrozenSubstitutionStore& substitutionStore() const noexcept;
  ZC_NODISCARD const FrozenWitnessStore& witnessStore() const noexcept;

#define ZOM_CHECKED_FACT_ACCESSOR(name, Type) ZC_NODISCARD const Type& name() const noexcept
  ZOM_CHECKED_FACT_ACCESSOR(nodeTypes, NodeTypeMap);
  ZOM_CHECKED_FACT_ACCESSOR(definitionTypes, DefinitionTypeMap);
  ZOM_CHECKED_FACT_ACCESSOR(literals, LiteralFactMap);
  ZOM_CHECKED_FACT_ACCESSOR(constants, ConstantFactMap);
  ZOM_CHECKED_FACT_ACCESSOR(aggregates, AggregateFactMap);
  ZOM_CHECKED_FACT_ACCESSOR(places, PlaceFactMap);
  ZOM_CHECKED_FACT_ACCESSOR(coercions, CoercionFactMap);
  ZOM_CHECKED_FACT_ACCESSOR(casts, CastFactMap);
  ZOM_CHECKED_FACT_ACCESSOR(calls, CallFactMap);
  ZOM_CHECKED_FACT_ACCESSOR(compoundAssignments, CompoundAssignmentFactMap);
  ZOM_CHECKED_FACT_ACCESSOR(members, MemberFactMap);
  ZOM_CHECKED_FACT_ACCESSOR(indexes, IndexFactMap);
  ZOM_CHECKED_FACT_ACCESSOR(patterns, PatternFactMap);
  ZOM_CHECKED_FACT_ACCESSOR(observedOperations, ObservedOperationFactMap);
  ZOM_CHECKED_FACT_ACCESSOR(captures, CaptureFactMap);
  ZOM_CHECKED_FACT_ACCESSOR(markerObligations, MarkerObligationFactMap);
  ZOM_CHECKED_FACT_ACCESSOR(exhaustiveness, ExhaustivenessFactMap);
  ZOM_CHECKED_FACT_ACCESSOR(unsafeOperations, UnsafeOperationFactMap);
  ZOM_CHECKED_FACT_ACCESSOR(projections, ProjectionFactMap);
  ZOM_CHECKED_FACT_ACCESSOR(obligations, ObligationFactMap);
  ZOM_CHECKED_FACT_ACCESSOR(errorUnionShapes, ErrorUnionShapeFactMap);
  ZOM_CHECKED_FACT_ACCESSOR(errorOperators, ErrorOperatorFactMap);
#undef ZOM_CHECKED_FACT_ACCESSOR

  ZC_NODISCARD zc::ArrayPtr<const CheckerAdvisoryRef> advisories() const noexcept;

private:
  struct Impl;
  explicit VerifiedCheckedFacts(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
  friend class CheckedFactsVerifier;
};

struct CheckedFactsSourceRejected final {
  zc::Vector<CheckerFailureRef> failures;
  zc::Vector<CheckerAdvisoryRef> advisories;
  zc::Vector<FrozenRecoveryLedger> recoveryLedgers;
};
struct CheckedFactsInvariantRejected final {
  zc::Vector<signature::CheckerVerificationFailure> failures;
};
using CheckedFactsVerificationResult =
    zc::OneOf<VerifiedCheckedFacts, CheckedFactsSourceRejected, CheckedFactsInvariantRejected>;
using CheckedFactsSourceRejectionVerificationResult =
    zc::OneOf<CheckedFactsSourceRejected, CheckedFactsInvariantRejected>;

/// \brief Verifies a fail-closed body rejection before diagnostics may observe its recovery IDs.
class CheckedFactsSourceRejectionVerifier final {
public:
  ZC_NODISCARD static CheckedFactsSourceRejectionVerificationResult verify(
      CheckedFactsSourceRejected&& rejection, const CheckedFactsVerificationInput& input);
};

/// \brief Verifies exact lineage, fact inventory, immutable records, and revision framing.
class CheckedFactsVerifier final {
public:
  ZC_NODISCARD static CheckedFactsVerificationResult verify(
      CheckedFactsCandidate&& candidate, const CheckedFactsVerificationInput& input);
};

}  // namespace zomlang::compiler::checker::checked
