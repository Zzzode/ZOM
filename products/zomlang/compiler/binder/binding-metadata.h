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
#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/binder/definition-site.h"
#include "zomlang/compiler/identity/canonical-scalar.h"
#include "zomlang/compiler/identity/definition-key.h"
#include "zomlang/compiler/identity/source-snapshot.h"

namespace zomlang::compiler::diagnostics {
enum class DiagID : uint32_t;
class DiagnosticEngine;
}  // namespace zomlang::compiler::diagnostics

namespace zomlang::compiler::identity {
class IdentityDiagnosticLocationResolver;
}

namespace zomlang::compiler::binder {

class BodyBindingCursor;
class ControlTransferBuilder;
class LabelBuilder;

enum class Namespace : uint8_t {
  Value = 0x01,
  Type = 0x02,
  Module = 0x03,
  Label = 0x04,
  Attribute = 0x05
};
enum class ScopeKind : uint8_t {
  Module = 0x01,
  Function = 0x02,
  Closure = 0x03,
  TypeBody = 0x04,
  ImplBody = 0x05,
  Block = 0x06,
  Loop = 0x07,
  Match = 0x08,
  MatchArm = 0x09,
  UnsafeBlock = 0x0a
};
enum class BindingOrigin : uint8_t {
  LocalDeclaration = 0x01,
  ImportAlias = 0x02,
  ReexportAlias = 0x03,
  Prelude = 0x04
};
enum class DefinitionActivation : uint8_t {
  ModuleSkeleton = 0x01,
  ImportSurface = 0x02,
  ReexportSurface = 0x03,
  GenericList = 0x04,
  ParameterList = 0x05,
  ExpressionIntroduction = 0x06,
  AfterInitializer = 0x07,
  MatchPattern = 0x08,
  LoopPattern = 0x09
};

/// \brief Context-checked module-local scope identity.
class ScopeId final {
public:
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD uint32_t index() const noexcept;
  ZC_NODISCARD bool belongsTo(identity::SemanticContextBrand context) const noexcept;
  bool operator==(const ScopeId& other) const noexcept;
  bool operator!=(const ScopeId& other) const noexcept { return !(*this == other); }

private:
  ScopeId(identity::ModuleId module, uint32_t index) noexcept;
  identity::ModuleId moduleValue;
  uint32_t indexValue;
  friend class ScopeArenaBuilder;
};

struct ModuleScopeOwner final {
  identity::ModuleId module;
};
struct DefinitionScopeOwner final {
  identity::DefId definition;
};
struct ImplScopeOwner final {
  identity::ImplId implementation;
};
using ScopeOwnerValue = zc::OneOf<ModuleScopeOwner, DefinitionScopeOwner, ImplScopeOwner>;

class ScopeOwner final {
public:
  ZC_NODISCARD static ScopeOwner module(identity::ModuleId value);
  ZC_NODISCARD static ScopeOwner definition(identity::DefId value);
  ZC_NODISCARD static ScopeOwner implementation(identity::ImplId value);
  ZC_NODISCARD const ScopeOwnerValue& value() const noexcept;

private:
  explicit ScopeOwner(ScopeOwnerValue&& value) noexcept;
  ScopeOwnerValue valueValue;
};

struct DefinitionBindingTarget final {
  identity::DefId definition;
};
struct ModuleBindingTarget final {
  identity::ModuleId module;
};
using BindingTargetValue = zc::OneOf<DefinitionBindingTarget, ModuleBindingTarget>;

class BindingTarget final {
public:
  ZC_NODISCARD static BindingTarget definition(identity::DefId value);
  ZC_NODISCARD static BindingTarget module(identity::ModuleId value);
  ZC_NODISCARD const BindingTargetValue& value() const noexcept;

private:
  explicit BindingTarget(BindingTargetValue&& value) noexcept;
  BindingTargetValue valueValue;
};

class BindingNameKey final {
public:
  BindingNameKey(BindingNameKey&&) noexcept = default;
  BindingNameKey& operator=(BindingNameKey&&) noexcept = default;
  ZC_DISALLOW_COPY(BindingNameKey);
  ZC_NODISCARD BindingNameKey clone() const;
  ZC_NODISCARD Namespace nameSpace() const noexcept;
  ZC_NODISCARD const identity::SemanticIdentifier& name() const noexcept;

private:
  BindingNameKey(Namespace nameSpace, identity::SemanticIdentifier&& name) noexcept;
  Namespace namespaceValue;
  identity::SemanticIdentifier nameValue;
  friend class BodyBindingCursor;
  friend class BodyBindingBuilder;
  friend class BindingBuilder;
  friend class BindingSkeletonBuilder;
  friend class BindingVerifier;
};

struct NameBinding final {
  NameBinding(BindingTarget&& bindingIdentity, BindingTarget&& canonicalTarget, Namespace nameSpace,
              BindingOrigin origin, identity::SourceSpan&& declarationSpan,
              zc::Maybe<identity::SourceSpan>&& aliasSpan) noexcept;
  NameBinding(NameBinding&&) noexcept = default;
  NameBinding& operator=(NameBinding&&) noexcept = default;
  ZC_DISALLOW_COPY(NameBinding);
  BindingTarget bindingIdentity;
  BindingTarget canonicalTarget;
  Namespace nameSpace;
  BindingOrigin origin;
  identity::SourceSpan declarationSpan;
  zc::Maybe<identity::SourceSpan> aliasSpan;
};

struct ScopeBindingEntry final {
  ScopeBindingEntry(BindingNameKey&& name, NameBinding&& binding) noexcept;
  ScopeBindingEntry(ScopeBindingEntry&&) noexcept = default;
  ScopeBindingEntry& operator=(ScopeBindingEntry&&) noexcept = default;
  ZC_DISALLOW_COPY(ScopeBindingEntry);
  BindingNameKey name;
  NameBinding binding;
};

struct ScopeRecord final {
  ScopeRecord(ScopeId id, zc::Maybe<ScopeId>&& parent, ScopeOwner&& owner, ScopeKind kind,
              zc::Vector<ScopeBindingEntry>&& bindings, identity::SourceSpan&& source) noexcept;
  ScopeRecord(ScopeRecord&&) noexcept = default;
  ScopeRecord& operator=(ScopeRecord&&) noexcept = default;
  ZC_DISALLOW_COPY(ScopeRecord);
  ScopeId id;
  zc::Maybe<ScopeId> parent;
  ScopeOwner owner;
  ScopeKind kind;
  zc::Vector<ScopeBindingEntry> bindings;
  identity::SourceSpan source;
};

/// \brief Frames already-canonical scope and label records for the allocation oracle.
ZC_NODISCARD zc::Array<uint8_t> frameBindingAllocationDump(
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> scopeRecords,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> labelRecords);

/// \brief Frames already-canonical contextual Self and receiver records.
ZC_NODISCARD zc::Array<uint8_t> frameBindingExtensionSequences(
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> selfTypeRecords,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> thisBindingRecords);

struct DefinitionFact final {
  DefinitionFact(identity::DefId identity, DefinitionSite&& site, identity::DefinitionKind kind,
                 identity::DefinitionNameKey&& name, Namespace nameSpace, ScopeId declaringScope,
                 identity::SourceSpan&& source, DefinitionActivation activation) noexcept;
  DefinitionFact(DefinitionFact&&) noexcept = default;
  DefinitionFact& operator=(DefinitionFact&&) noexcept = default;
  ZC_DISALLOW_COPY(DefinitionFact);
  identity::DefId identity;
  DefinitionSite site;
  identity::DefinitionKind kind;
  identity::DefinitionNameKey name;
  Namespace nameSpace;
  ScopeId declaringScope;
  identity::SourceSpan source;
  DefinitionActivation activation;
};

struct ModuleVisibility final {
  identity::ModuleId module;
};
struct ExternalVisibility final {};
using VisibilityEnvelopeValue = zc::OneOf<ModuleVisibility, ExternalVisibility>;

class VisibilityEnvelope final {
public:
  ZC_NODISCARD static VisibilityEnvelope module(identity::ModuleId value);
  ZC_NODISCARD static VisibilityEnvelope external();
  ZC_NODISCARD const VisibilityEnvelopeValue& value() const noexcept;

private:
  explicit VisibilityEnvelope(VisibilityEnvelopeValue&& value) noexcept;
  VisibilityEnvelopeValue valueValue;
};

struct ReexportProvenanceStep final {
  identity::ModuleId module;
  identity::DefId alias;
  BindingTarget canonicalTarget;
  identity::SourceSpan exportSpan;
};

struct ExportSurfaceEntry final {
  ExportSurfaceEntry(BindingNameKey&& name, BindingTarget&& bindingIdentity,
                     BindingTarget&& canonicalTarget, VisibilityEnvelope&& visibility,
                     bool exported, identity::SourceSpan&& bindingSpan,
                     identity::SourceSpan&& canonicalDeclarationSpan,
                     zc::Maybe<identity::SourceSpan>&& aliasSpan,
                     zc::Maybe<identity::SourceSpan>&& exportSpan,
                     zc::Vector<ReexportProvenanceStep>&& reexportChain) noexcept;
  ExportSurfaceEntry(ExportSurfaceEntry&&) noexcept = default;
  ExportSurfaceEntry& operator=(ExportSurfaceEntry&&) noexcept = default;
  ZC_DISALLOW_COPY(ExportSurfaceEntry);
  BindingNameKey name;
  BindingTarget bindingIdentity;
  BindingTarget canonicalTarget;
  VisibilityEnvelope visibility;
  bool exported;
  identity::SourceSpan bindingSpan;
  identity::SourceSpan canonicalDeclarationSpan;
  zc::Maybe<identity::SourceSpan> aliasSpan;
  zc::Maybe<identity::SourceSpan> exportSpan;
  zc::Vector<ReexportProvenanceStep> reexportChain;
};

class ExportSurfaceRevision final {
public:
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;
  ZC_NODISCARD static zc::Maybe<ExportSurfaceRevision> computeFramed(
      const identity::Sha256Digest& semanticContextFingerprint,
      zc::ArrayPtr<const uint8_t> encodedModule, zc::ArrayPtr<const uint8_t> encodedPackage,
      zc::ArrayPtr<const uint8_t> encodedVisibleEntries,
      zc::ArrayPtr<const uint8_t> encodedExports);

private:
  explicit ExportSurfaceRevision(const identity::Sha256Digest& digest) noexcept;
  identity::Sha256Digest value;
  friend class BindingBuilder;
  friend class BindingVerifier;
};

enum class BinderDiagnosticCode : uint16_t {
  UndefinedIdentifier = 3001,
  SymbolNamespaceMismatch = 3002,
  RedeclareVariable = 3003,
  RedeclareParameter = 3004,
  RedeclareFunction = 3005,
  RedeclareClass = 3006,
  RedeclareInterface = 3007,
  RedeclareEnum = 3008,
  RedeclareTypeAlias = 3009,
  DuplicateIdentifier = 3010,
  PreviousDeclarationHere = 3017,
  BreakTargetNotFound = 3020,
  ContinueTargetNotFound = 3021,
  ContinueTargetNotLoop = 3022,
  ContextualSelfOutsideType = 3025
};

struct BindingDiagnosticNoteRef final {
  BinderDiagnosticCode diagnostic;
  identity::SourceSpan source;
};

struct BindingFailureRef final {
  BinderDiagnosticCode diagnostic;
  identity::SourceSpan primary;
  uint64_t emitterOrdinal;
  zc::Vector<BindingDiagnosticNoteRef> notes;
};

struct NodeScopeFact final {
  ast::NodeId node;
  ScopeId scope;
};

struct ModuleLabelOwner final {
  identity::ModuleId module;
};
struct CallableLabelOwner final {
  identity::DefId callable;
};
using LabelOwnerValue = zc::OneOf<ModuleLabelOwner, CallableLabelOwner>;

/// \brief Sealed context-checked owner of one flat label namespace.
class LabelOwner final {
public:
  ZC_NODISCARD const LabelOwnerValue& value() const noexcept;
  ZC_NODISCARD bool belongsTo(identity::SemanticContextBrand context) const noexcept;
  bool operator==(const LabelOwner& other) const noexcept;
  bool operator!=(const LabelOwner& other) const noexcept { return !(*this == other); }

private:
  explicit LabelOwner(LabelOwnerValue&& value) noexcept;
  ZC_NODISCARD static LabelOwner module(identity::ModuleId value);
  ZC_NODISCARD static LabelOwner callable(identity::DefId value);
  ZC_NODISCARD LabelOwner clone() const;
  LabelOwnerValue valueValue;
  friend class LabelId;
  friend class LabelBuilder;
};

/// \brief Sealed owner-local label identity.
class LabelId final {
public:
  ZC_NODISCARD const LabelOwner& owner() const noexcept;
  ZC_NODISCARD uint32_t index() const noexcept;
  ZC_NODISCARD bool belongsTo(identity::SemanticContextBrand context) const noexcept;
  bool operator==(const LabelId& other) const noexcept;
  bool operator!=(const LabelId& other) const noexcept { return !(*this == other); }

private:
  LabelId(LabelOwner&& owner, uint32_t index) noexcept;
  ZC_NODISCARD LabelId clone() const;
  LabelOwner ownerValue;
  uint32_t indexValue;
  friend class LabelBuilder;
  friend class ControlTransferBuilder;
};

struct BlockLabelTarget final {
  ScopeId scope;
};
struct LoopLabelTarget final {
  ScopeId scope;
};
using LabelTargetValue = zc::OneOf<BlockLabelTarget, LoopLabelTarget>;

/// \brief Sealed block-or-loop target selected from the verified scope arena.
class LabelTarget final {
public:
  ZC_NODISCARD const LabelTargetValue& value() const noexcept;
  ZC_NODISCARD bool belongsTo(identity::SemanticContextBrand context) const noexcept;
  bool operator==(const LabelTarget& other) const noexcept;
  bool operator!=(const LabelTarget& other) const noexcept { return !(*this == other); }

private:
  explicit LabelTarget(LabelTargetValue&& value) noexcept;
  ZC_NODISCARD static LabelTarget block(ScopeId scope);
  ZC_NODISCARD static LabelTarget loop(ScopeId scope);
  ZC_NODISCARD LabelTarget clone() const;
  LabelTargetValue valueValue;
  friend class LabelBuilder;
  friend class ControlTransferBuilder;
};
struct ExplicitLabelControlTarget final {
  LabelId label;
};
struct LoopControlTarget final {
  ScopeId scope;
};
struct MatchControlTarget final {
  ScopeId scope;
};
using ControlTarget = zc::OneOf<ExplicitLabelControlTarget, LoopControlTarget, MatchControlTarget>;

struct BoundNameResolution final {
  BindingTarget bindingIdentity;
  BindingTarget canonicalTarget;
  Namespace nameSpace;
  BindingOrigin origin;
};
struct BoundLabelResolution final {
  LabelId label;
  LabelTarget target;
};
struct FailedBindingResolution final {
  size_t failureIndex;
};

struct DeferredMemberFact final {
  ast::NodeId node;
  ast::NodeId base;
  identity::DeclaredDefinitionName member;
  zc::Vector<Namespace> expectedNamespaces;
  zc::Vector<ast::NodeId> genericArguments;
  identity::SourceSpan source;
};

using BindingResolutionValue = zc::OneOf<BoundNameResolution, BoundLabelResolution,
                                         DeferredMemberFact, FailedBindingResolution>;
struct BindingResolution final {
  ast::NodeId node;
  BindingResolutionValue value;
};

/// \brief Contextual Self owned by a nominal declaration.
struct NominalSelfOwner final {
  identity::DefId definition;
};

/// \brief Contextual Self owned by an interface declaration.
struct InterfaceSelfOwner final {
  identity::DefId definition;
};

/// \brief Contextual Self owned by an implementation declaration.
struct ImplSelfOwner final {
  identity::ImplId implementation;
};

using SelfOwner = zc::OneOf<NominalSelfOwner, InterfaceSelfOwner, ImplSelfOwner>;

/// \brief One lexical root Self bound to its nearest semantic owner.
struct BoundSelfType final {
  ast::NodeId syntax;
  SelfOwner owner;
  identity::SourceSpan source;
};

/// \brief Receiver parameter selected by a successful this expression binding.
struct ThisBinding final {
  identity::DefId receiverParameter;
};

/// \brief One this expression resolved in the receiver-only binding domain.
struct BoundThis final {
  ast::NodeId expression;
  ThisBinding binding;
  identity::SourceSpan source;
};

struct ImplBindingFact final {
  identity::ImplId identity;
  ast::NodeId node;
  ScopeId scope;
  zc::Vector<identity::DefId> members;
  identity::SourceSpan source;
};

struct ModuleAliasBindingFact final {
  ast::NodeId node;
  identity::DefId alias;
  identity::ModuleId canonicalTarget;
  ExportSurfaceRevision targetRevision;
  identity::SourceSpan declarationSpan;
  identity::SourceSpan targetSpan;
};

enum class ImportBindingKind : uint8_t { Import = 0x01, ForeignReexport = 0x02 };
struct ImportBindingFact final {
  ast::NodeId node;
  identity::DefId alias;
  BindingTarget canonicalTarget;
  identity::ModuleId sourceModule;
  ExportSurfaceRevision sourceRevision;
  ImportBindingKind kind;
  identity::SourceSpan declarationSpan;
  zc::Maybe<identity::SourceSpan> aliasSpan;
  zc::Vector<ReexportProvenanceStep> reexportChain;
};

struct LocalExportFact final {
  ast::NodeId node;
  identity::DefId alias;
  BindingTarget sourceBinding;
  BindingTarget canonicalTarget;
  identity::SourceSpan bindingSpan;
  identity::SourceSpan canonicalDeclarationSpan;
  zc::Maybe<identity::SourceSpan> aliasSpan;
  identity::SourceSpan exportSpan;
  zc::Vector<ReexportProvenanceStep> reexportChain;
};

struct LabelFact final {
  LabelId identity;
  identity::SemanticIdentifier name;
  LabelOwner owner;
  ast::NodeId statement;
  LabelTarget target;
  identity::SourceSpan source;
};

enum class ControlTransferKind : uint8_t { Break = 0x01, Continue = 0x02 };
struct ControlTransferFact final {
  ast::NodeId node;
  ControlTransferKind kind;
  ControlTarget target;
  identity::SourceSpan source;
};

struct ShadowTargetFact final {
  identity::DefId definition;
  BindingTarget target;
};
struct FreeVariableFact final {
  identity::DefId target;
  zc::Vector<ast::NodeId> referenceSites;
};
struct ClosureFreeVariableFact final {
  identity::DefId closure;
  zc::Vector<FreeVariableFact> variables;
};

/// \brief One syntax capture bound to enclosing runtime storage.
struct ExplicitCaptureBindingFact final {
  ast::NodeId item;
  identity::DefId target;
  identity::SourceSpan source;
};

/// \brief Source-ordered explicit capture bindings for one function expression.
struct ExplicitClosureCaptureFact final {
  identity::DefId closure;
  ast::NodeId captureList;
  identity::SourceSpan source;
  zc::Vector<ExplicitCaptureBindingFact> captures;
};

enum class BinderInvariantKind : uint8_t {
  MalformedScopeGraph = 0x01,
  MissingRequiredResolution = 0x02,
  AliasCycle = 0x03,
  InvalidBindingFact = 0x04,
  InvalidEmitterOrdinal = 0x05
};

enum class BinderEmitterSite : uint8_t {
  BindingInput = 0x01,
  ModuleSkeleton = 0x02,
  ImportBinding = 0x03,
  BodyBinding = 0x04,
  LabelAndClosure = 0x05,
  BindingVerifier = 0x06
};

struct BinderInvariantFact final {
  BinderInvariantKind kind;
  identity::ModuleId module;
  zc::Maybe<identity::UnbrandedSourceRange> diagnosticRange;
  BinderEmitterSite emitterSite;
  uint32_t schemaPreorderOrdinal;
};

ZC_NODISCARD diagnostics::DiagID binderInvariantDiagnosticId(BinderInvariantKind kind);

class BinderInvariantDiagnosticGroup final {
public:
  BinderInvariantDiagnosticGroup(BinderInvariantDiagnosticGroup&&) noexcept = default;
  BinderInvariantDiagnosticGroup& operator=(BinderInvariantDiagnosticGroup&&) noexcept = default;
  ZC_DISALLOW_COPY(BinderInvariantDiagnosticGroup);
  ZC_NODISCARD diagnostics::DiagID diagnosticId() const noexcept;
  ZC_NODISCARD zc::Maybe<const identity::UnbrandedSourceRange&> diagnosticRange() const;
  ZC_NODISCARD uint64_t occurrenceCount() const noexcept;

private:
  BinderInvariantDiagnosticGroup(diagnostics::DiagID diagnosticId,
                                 zc::Maybe<identity::UnbrandedSourceRange>&& diagnosticRange,
                                 uint64_t occurrenceCount) noexcept;
  diagnostics::DiagID idValue;
  zc::Maybe<identity::UnbrandedSourceRange> rangeValue;
  uint64_t countValue;
  friend zc::Maybe<zc::Vector<BinderInvariantDiagnosticGroup>> groupBinderInvariants(
      zc::ArrayPtr<const BinderInvariantFact> facts);
};

/// \brief Sorts and groups one module's invariant facts by registered ID and anchor.
ZC_NODISCARD zc::Maybe<zc::Vector<BinderInvariantDiagnosticGroup>> groupBinderInvariants(
    zc::ArrayPtr<const BinderInvariantFact> facts);

void emitBinderInvariantGroups(
    diagnostics::DiagnosticEngine& diagnostics,
    zc::ArrayPtr<const BinderInvariantDiagnosticGroup> groups,
    zc::Maybe<const identity::IdentityDiagnosticLocationResolver&> locationResolver = zc::none);

/// \brief Emits one registered fatal binder invariant without inventing an anchor.
void emitBinderInvariant(
    diagnostics::DiagnosticEngine& diagnostics, const BinderInvariantFact& fact,
    zc::Maybe<const identity::IdentityDiagnosticLocationResolver&> locationResolver = zc::none);

class VerifiedBindingMetadata final {
public:
  ~VerifiedBindingMetadata() noexcept(false);
  VerifiedBindingMetadata(VerifiedBindingMetadata&&) noexcept;
  VerifiedBindingMetadata& operator=(VerifiedBindingMetadata&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedBindingMetadata);
  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const NodeScopeFact> nodeScopes() const;
  ZC_NODISCARD zc::ArrayPtr<const BindingResolution> nodeBindings() const;
  ZC_NODISCARD zc::ArrayPtr<const BoundSelfType> selfTypes() const;
  ZC_NODISCARD zc::ArrayPtr<const BoundThis> thisBindings() const;
  ZC_NODISCARD zc::ArrayPtr<const ScopeRecord> scopes() const;
  ZC_NODISCARD zc::ArrayPtr<const DefinitionFact> definitions() const;
  ZC_NODISCARD zc::ArrayPtr<const ImplBindingFact> impls() const;
  ZC_NODISCARD zc::ArrayPtr<const ModuleAliasBindingFact> moduleAliases() const;
  ZC_NODISCARD zc::ArrayPtr<const ImportBindingFact> imports() const;
  ZC_NODISCARD zc::ArrayPtr<const LocalExportFact> localExports() const;
  ZC_NODISCARD zc::ArrayPtr<const DeferredMemberFact> deferredMembers() const;
  ZC_NODISCARD zc::ArrayPtr<const LabelFact> labels() const;
  ZC_NODISCARD zc::ArrayPtr<const ControlTransferFact> controlTransfers() const;
  ZC_NODISCARD zc::ArrayPtr<const ShadowTargetFact> shadowTargets() const;
  ZC_NODISCARD zc::ArrayPtr<const ClosureFreeVariableFact> closureFreeVariables() const;
  ZC_NODISCARD zc::ArrayPtr<const ExplicitClosureCaptureFact> explicitClosureCaptures() const;

private:
  struct Impl;
  explicit VerifiedBindingMetadata(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
  friend class BindingVerifier;
};

class VerifiedExportSurface final {
public:
  ~VerifiedExportSurface() noexcept(false);
  VerifiedExportSurface(VerifiedExportSurface&&) noexcept;
  VerifiedExportSurface& operator=(VerifiedExportSurface&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedExportSurface);
  ZC_NODISCARD identity::ModuleId sourceModule() const noexcept;
  ZC_NODISCARD identity::PackageId sourcePackage() const noexcept;
  ZC_NODISCARD const ExportSurfaceRevision& revision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ExportSurfaceEntry> visibleEntries() const;
  ZC_NODISCARD zc::ArrayPtr<const ExportSurfaceEntry> exports() const;

private:
  struct Impl;
  explicit VerifiedExportSurface(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
  friend class BindingVerifier;
};

}  // namespace zomlang::compiler::binder
