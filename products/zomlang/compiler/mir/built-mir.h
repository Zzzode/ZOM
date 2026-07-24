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
#include "zc/core/vector.h"
#include "zomlang/compiler/checker/checked-facts.h"
#include "zomlang/compiler/hir/hir-module.h"
#include "zomlang/compiler/identity/semantic-context-fingerprint.h"
#include "zomlang/compiler/ir/ir-failure.h"
#include "zomlang/compiler/ir/ir-identity.h"

namespace zomlang::compiler::mir {

/// \brief Deterministic one-based identity of a local in one MIR body.
class MirLocalId final {
public:
  constexpr MirLocalId() noexcept = default;
  ZC_NODISCARD static zc::Maybe<MirLocalId> fromOrdinal(uint32_t ordinal) noexcept;
  ZC_NODISCARD constexpr bool isValid() const noexcept { return value != 0; }
  ZC_NODISCARD constexpr uint32_t ordinal() const noexcept { return value; }
  constexpr bool operator==(MirLocalId other) const noexcept { return value == other.value; }
  constexpr bool operator!=(MirLocalId other) const noexcept { return !(*this == other); }

private:
  explicit constexpr MirLocalId(uint32_t ordinal) noexcept : value(ordinal) {}
  uint32_t value = 0;
};

/// \brief Deterministic one-based identity of a lexical source scope in one MIR body.
class MirSourceScopeId final {
public:
  constexpr MirSourceScopeId() noexcept = default;
  ZC_NODISCARD static zc::Maybe<MirSourceScopeId> fromOrdinal(uint32_t ordinal) noexcept;
  ZC_NODISCARD constexpr bool isValid() const noexcept { return value != 0; }
  ZC_NODISCARD constexpr uint32_t ordinal() const noexcept { return value; }
  constexpr bool operator==(MirSourceScopeId other) const noexcept { return value == other.value; }
  constexpr bool operator!=(MirSourceScopeId other) const noexcept { return !(*this == other); }

private:
  explicit constexpr MirSourceScopeId(uint32_t ordinal) noexcept : value(ordinal) {}
  uint32_t value = 0;
};

enum class MirProjectionKind : uint8_t {
  Field = 0x01,
  Index = 0x02,
  Dereference = 0x03,
  Downcast = 0x04,
  Subslice = 0x05,
};

struct MirFieldProjection final {
  identity::DefId field;
};
struct MirIndexProjection final {
  MirLocalId index;
};
struct MirDereferenceProjection final {};
struct MirDowncastProjection final {
  identity::DefId variant;
};
struct MirSubsliceProjection final {
  uint32_t first;
  uint32_t pastLast;
};

/// \brief Closed target-independent place projection algebra.
class MirProjection final {
public:
  MirProjection(MirProjection&&) noexcept = default;
  MirProjection& operator=(MirProjection&&) noexcept = default;
  ZC_DISALLOW_COPY(MirProjection);

  ZC_NODISCARD static MirProjection field(identity::DefId field) noexcept;
  ZC_NODISCARD static MirProjection index(MirLocalId index) noexcept;
  ZC_NODISCARD static MirProjection dereference() noexcept;
  ZC_NODISCARD static MirProjection downcast(identity::DefId variant) noexcept;
  ZC_NODISCARD static zc::Maybe<MirProjection> subslice(uint32_t first, uint32_t pastLast) noexcept;
  ZC_NODISCARD MirProjection clone() const noexcept;
  ZC_NODISCARD MirProjectionKind kind() const noexcept;
  ZC_NODISCARD bool isStructurallyValid() const noexcept;
  ZC_NODISCARD const MirFieldProjection& fieldValue() const;
  ZC_NODISCARD const MirIndexProjection& indexValue() const;
  ZC_NODISCARD const MirDowncastProjection& downcastValue() const;
  ZC_NODISCARD const MirSubsliceProjection& subsliceValue() const;

private:
  explicit MirProjection(MirFieldProjection value) noexcept;
  explicit MirProjection(MirIndexProjection value) noexcept;
  explicit MirProjection(MirDereferenceProjection value) noexcept;
  explicit MirProjection(MirDowncastProjection value) noexcept;
  explicit MirProjection(MirSubsliceProjection value) noexcept;
  zc::OneOf<MirFieldProjection, MirIndexProjection, MirDereferenceProjection, MirDowncastProjection,
            MirSubsliceProjection>
      value;
};

/// \brief One typed storage location and its logical projection path.
class MirPlace final {
public:
  MirPlace(MirLocalId local, zc::Vector<MirProjection>&& projections) noexcept;
  ~MirPlace() noexcept(false);
  MirPlace(MirPlace&&) noexcept;
  MirPlace& operator=(MirPlace&&) noexcept;
  ZC_DISALLOW_COPY(MirPlace);

  ZC_NODISCARD MirPlace clone() const;
  ZC_NODISCARD MirLocalId local() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const MirProjection> projections() const noexcept;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

enum class MirOperandKind : uint8_t { Copy = 0x01, Move = 0x02, Constant = 0x03 };

struct MirCopyOperand final {
  MirPlace place;
};
struct MirMoveOperand final {
  MirPlace place;
};
struct MirConstantOperand final {
  identity::SemanticTypeId type;
  checker::checked::CanonicalConstValue value;
};

/// \brief Closed affine operand algebra; copy and move remain explicit uses.
class MirOperand final {
public:
  MirOperand(MirOperand&&) noexcept = default;
  MirOperand& operator=(MirOperand&&) noexcept = default;
  ZC_DISALLOW_COPY(MirOperand);

  ZC_NODISCARD static MirOperand copy(MirPlace&& place) noexcept;
  ZC_NODISCARD static MirOperand move(MirPlace&& place) noexcept;
  ZC_NODISCARD static MirOperand constant(identity::SemanticTypeId type,
                                          checker::checked::CanonicalConstValue&& value) noexcept;
  ZC_NODISCARD MirOperand clone() const;
  ZC_NODISCARD MirOperandKind kind() const noexcept;
  ZC_NODISCARD const MirPlace& place() const;
  ZC_NODISCARD const MirConstantOperand& constantValue() const;

private:
  explicit MirOperand(MirCopyOperand&& value) noexcept;
  explicit MirOperand(MirMoveOperand&& value) noexcept;
  explicit MirOperand(MirConstantOperand&& value) noexcept;
  zc::OneOf<MirCopyOperand, MirMoveOperand, MirConstantOperand> value;
};

enum class MirBorrowKind : uint8_t { Shared = 0x01, Mutable = 0x02 };
enum class MirRvalueKind : uint8_t { Use = 0x01 };

struct MirUseRvalue final {
  MirOperand operand;
};

/// \brief Canonical target-independent assignment value for the Built MIR boundary.
class MirRvalue final {
public:
  MirRvalue(MirRvalue&&) noexcept = default;
  MirRvalue& operator=(MirRvalue&&) noexcept = default;
  ZC_DISALLOW_COPY(MirRvalue);

  ZC_NODISCARD static MirRvalue use(MirOperand&& operand) noexcept;
  ZC_NODISCARD MirRvalue clone() const;
  ZC_NODISCARD MirRvalueKind kind() const noexcept;
  ZC_NODISCARD const MirUseRvalue& useValue() const;

private:
  explicit MirRvalue(MirUseRvalue&& value) noexcept;
  MirUseRvalue value;
};

enum class MirInitializationKind : uint8_t { Initialize = 0x01, Overwrite = 0x02 };
enum class MirStatementKind : uint8_t {
  Assign = 0x01,
  StorageLive = 0x02,
  StorageDead = 0x03,
  BorrowCreation = 0x04,
  SetDiscriminant = 0x05,
  Deinitialize = 0x06,
};

struct MirAssignmentStatement final {
  MirPlace destination;
  MirRvalue value;
  MirInitializationKind initialization;
};
struct MirStorageLiveStatement final {
  MirLocalId local;
};
struct MirStorageDeadStatement final {
  MirLocalId local;
};
struct MirBorrowCreationStatement final {
  MirPlace destination;
  MirBorrowKind kind;
  MirPlace source;
};
struct MirSetDiscriminantStatement final {
  MirPlace destination;
  identity::DefId variant;
};
struct MirDeinitializeStatement final {
  MirPlace destination;
};

/// \brief Closed statement algebra retaining explicit storage and initialization state.
class MirStatement final {
public:
  MirStatement(MirStatement&&) noexcept = default;
  MirStatement& operator=(MirStatement&&) noexcept = default;
  ZC_DISALLOW_COPY(MirStatement);

  ZC_NODISCARD static MirStatement assign(MirPlace&& destination, MirRvalue&& value,
                                          MirInitializationKind initialization) noexcept;
  ZC_NODISCARD static MirStatement storageLive(MirLocalId local) noexcept;
  ZC_NODISCARD static MirStatement storageDead(MirLocalId local) noexcept;
  ZC_NODISCARD static MirStatement borrowCreation(MirPlace&& destination, MirBorrowKind kind,
                                                  MirPlace&& source) noexcept;
  ZC_NODISCARD static MirStatement setDiscriminant(MirPlace&& destination,
                                                   identity::DefId variant) noexcept;
  ZC_NODISCARD static MirStatement deinitialize(MirPlace&& destination) noexcept;
  ZC_NODISCARD MirStatement clone() const;
  ZC_NODISCARD MirStatementKind kind() const noexcept;
  ZC_NODISCARD const MirAssignmentStatement& assignmentValue() const;
  ZC_NODISCARD MirLocalId storageLocal() const;
  ZC_NODISCARD const MirBorrowCreationStatement& borrowCreationValue() const;
  ZC_NODISCARD const MirSetDiscriminantStatement& setDiscriminantValue() const;
  ZC_NODISCARD const MirDeinitializeStatement& deinitializeValue() const;

private:
  explicit MirStatement(MirAssignmentStatement&& value) noexcept;
  explicit MirStatement(MirStorageLiveStatement value) noexcept;
  explicit MirStatement(MirStorageDeadStatement value) noexcept;
  explicit MirStatement(MirBorrowCreationStatement&& value) noexcept;
  explicit MirStatement(MirSetDiscriminantStatement&& value) noexcept;
  explicit MirStatement(MirDeinitializeStatement&& value) noexcept;
  zc::OneOf<MirAssignmentStatement, MirStorageLiveStatement, MirStorageDeadStatement,
            MirBorrowCreationStatement, MirSetDiscriminantStatement, MirDeinitializeStatement>
      value;
};

enum class MirTerminatorKind : uint8_t { Return = 0x01, Unreachable = 0x02 };

struct MirReturnTerminator final {
  zc::Maybe<MirOperand> value;
};
struct MirUnreachableTerminator final {};

/// \brief Closed terminator algebra for the currently supported Built MIR subset.
class MirTerminator final {
public:
  MirTerminator(MirTerminator&&) noexcept = default;
  MirTerminator& operator=(MirTerminator&&) noexcept = default;
  ZC_DISALLOW_COPY(MirTerminator);

  ZC_NODISCARD static MirTerminator returnValue(MirOperand&& value) noexcept;
  ZC_NODISCARD static MirTerminator returnVoid() noexcept;
  ZC_NODISCARD static MirTerminator unreachable() noexcept;
  ZC_NODISCARD MirTerminator clone() const;
  ZC_NODISCARD MirTerminatorKind kind() const noexcept;
  ZC_NODISCARD const MirReturnTerminator& returnValue() const;

private:
  explicit MirTerminator(MirReturnTerminator&& value) noexcept;
  explicit MirTerminator(MirUnreachableTerminator value) noexcept;
  zc::OneOf<MirReturnTerminator, MirUnreachableTerminator> value;
};

enum class MirLocalKind : uint8_t { ModuleInitializerResult = 0x01, Temporary = 0x02 };
enum class MirFunctionKind : uint8_t { ModuleInitializer = 0x01, Function = 0x02 };

struct MirSourceScope final {
  MirSourceScopeId id;
  zc::Maybe<MirSourceScopeId> parent;
  identity::SourceSpan sourceSpan;
};

struct MirLocalDeclaration final {
  MirLocalId id;
  MirLocalKind kind;
  identity::SemanticTypeId type;
  MirSourceScopeId sourceScope;
  identity::SourceSpan sourceSpan;
};

struct MirBasicBlock final {
  MirBlockId id;
  MirSourceScopeId sourceScope;
  zc::Vector<MirStatement> statements;
  MirTerminator terminator;
};

/// \brief One verified source-owned body in Built MIR.
struct MirFunction final {
  identity::DefId owner;
  MirFunctionKind kind;
  identity::DefinitionKind sourceDefinitionKind;
  identity::SemanticTypeId resultType;
  identity::SourceSpan sourceSpan;
  zc::Vector<MirSourceScope> sourceScopes;
  zc::Vector<MirLocalDeclaration> locals;
  zc::Vector<MirBasicBlock> blocks;
};

/// \brief Domain-separated immutable revision of one complete MIR module.
class MirRevisionId final {
public:
  constexpr MirRevisionId() noexcept = default;

  ZC_NODISCARD static MirRevisionId fromDigest(const identity::Sha256Digest& digest) noexcept;
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept { return digestValue; }

private:
  explicit MirRevisionId(const identity::Sha256Digest& digest) noexcept : digestValue(digest) {}

  identity::Sha256Digest digestValue;
};

/// \brief Exact canonical MIR revision framing codec.
class MirRevisionCodec final {
public:
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encodeBuiltFramed(
      const identity::Sha256Digest& contextFingerprint,
      zc::ArrayPtr<const uint8_t> expandedModuleKey,
      const identity::Sha256Digest& checkedFactsRevision,
      const identity::Sha256Digest& dispatchFactsRevision,
      const identity::Sha256Digest& borrowEvidenceRevision,
      zc::ArrayPtr<const zc::Array<uint8_t>> canonicalFunctions);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encodeBuilt(
      const identity::SemanticContextFingerprint& contextFingerprint,
      zc::ArrayPtr<const uint8_t> expandedModuleKey,
      const checker::checked::CheckedFactsRevision& checkedFactsRevision,
      const checker::dispatch::DispatchFactsRevision& dispatchFactsRevision,
      const driver::borrow_evidence::BorrowEvidenceRevision& borrowEvidenceRevision,
      zc::ArrayPtr<const zc::Array<uint8_t>> canonicalFunctions);
  ZC_NODISCARD static zc::Maybe<MirRevisionId> computeBuilt(
      const identity::SemanticContextFingerprint& contextFingerprint,
      zc::ArrayPtr<const uint8_t> expandedModuleKey,
      const checker::checked::CheckedFactsRevision& checkedFactsRevision,
      const checker::dispatch::DispatchFactsRevision& dispatchFactsRevision,
      const driver::borrow_evidence::BorrowEvidenceRevision& borrowEvidenceRevision,
      zc::ArrayPtr<const zc::Array<uint8_t>> canonicalFunctions);
};

/// \brief Untrusted mutable Built MIR product admitted only by the independent verifier.
class BuiltMirCandidate final {
public:
  BuiltMirCandidate(const hir::VerifiedHirModule& sourceHir, zc::Vector<MirFunction>&& functions,
                    zc::Vector<zc::Array<uint8_t>>&& canonicalFunctions,
                    MirRevisionId revision) noexcept;
  BuiltMirCandidate(BuiltMirCandidate&&) noexcept = default;
  BuiltMirCandidate& operator=(BuiltMirCandidate&&) noexcept = delete;
  ZC_DISALLOW_COPY(BuiltMirCandidate);

  const hir::VerifiedHirModule& sourceHir;
  zc::Vector<MirFunction> functions;
  zc::Vector<zc::Array<uint8_t>> canonicalFunctions;
  MirRevisionId revision;
};

/// \brief Immutable target-independent Built MIR capability with exact frontend lineage.
class VerifiedBuiltMir final {
public:
  ~VerifiedBuiltMir() noexcept(false);
  VerifiedBuiltMir(VerifiedBuiltMir&&) noexcept;
  VerifiedBuiltMir& operator=(VerifiedBuiltMir&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedBuiltMir);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::SemanticContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD identity::PackageId package() const noexcept;
  ZC_NODISCARD identity::CrateId crate() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const checker::checked::CheckedFactsRevision& checkedFactsRevision() const noexcept;
  ZC_NODISCARD const checker::dispatch::DispatchFactsRevision& dispatchFactsRevision()
      const noexcept;
  ZC_NODISCARD const driver::borrow_evidence::BorrowEvidenceRevision& borrowEvidenceRevision()
      const noexcept;
  ZC_NODISCARD const driver::borrow_evidence::VerifiedBorrowEvidenceLease& borrowEvidenceLease()
      const noexcept;
  ZC_NODISCARD const MirRevisionId& revision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const MirFunction> functions() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const zc::Array<uint8_t>> canonicalFunctionRecords() const noexcept;

private:
  struct Impl;
  explicit VerifiedBuiltMir(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class BuiltMirVerifier;
};

/// \brief Lowers the complete currently supported HIR scalar slice into Built MIR.
class BuiltMirBuilder final {
public:
  ZC_NODISCARD static ir::IrOperationResult<BuiltMirCandidate> build(
      const hir::VerifiedHirModule& hirModule);
};

/// \brief Sole publisher of immutable revision-checked Built MIR modules.
class BuiltMirVerifier final {
public:
  ZC_NODISCARD static ir::IrOperationResult<VerifiedBuiltMir> verify(BuiltMirCandidate&& candidate);
};

}  // namespace zomlang::compiler::mir
