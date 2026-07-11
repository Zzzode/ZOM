// Copyright (c) 2024-2025 Zode.Z. All rights reserved
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

#pragma once

#include <cstdint>

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/node-id.h"
#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/type/type-interner.h"

namespace zomlang {
namespace compiler {
namespace type {
class TypeEnv;
}  // namespace type
namespace diagnostics {
class DiagnosticEngine;
}  // namespace diagnostics
}  // namespace compiler
}  // namespace zomlang

namespace zomlang {
namespace compiler {
namespace checker {

/// \brief Stable borrow-checker place identifier.
struct PlaceId final {
  uint32_t value = 0;

  constexpr PlaceId() noexcept = default;
  constexpr explicit PlaceId(uint32_t raw) noexcept : value(raw) {}

  constexpr bool isValid() const noexcept { return value != 0; }
  constexpr bool operator==(PlaceId other) const noexcept { return value == other.value; }
  constexpr bool operator!=(PlaceId other) const noexcept { return value != other.value; }
};

/// \brief Stable inferred-region identifier.
struct RegionId final {
  uint32_t value = 0;

  constexpr RegionId() noexcept = default;
  constexpr explicit RegionId(uint32_t raw) noexcept : value(raw) {}

  constexpr bool isValid() const noexcept { return value != 0; }
  constexpr bool operator==(RegionId other) const noexcept { return value == other.value; }
  constexpr bool operator!=(RegionId other) const noexcept { return value != other.value; }
};

/// \brief Stable loan identifier.
struct LoanId final {
  uint32_t value = 0;

  constexpr LoanId() noexcept = default;
  constexpr explicit LoanId(uint32_t raw) noexcept : value(raw) {}

  constexpr bool isValid() const noexcept { return value != 0; }
  constexpr bool operator==(LoanId other) const noexcept { return value == other.value; }
  constexpr bool operator!=(LoanId other) const noexcept { return value != other.value; }
};

/// \brief Stable move identifier.
struct MoveId final {
  uint32_t value = 0;

  constexpr MoveId() noexcept = default;
  constexpr explicit MoveId(uint32_t raw) noexcept : value(raw) {}

  constexpr bool isValid() const noexcept { return value != 0; }
  constexpr bool operator==(MoveId other) const noexcept { return value == other.value; }
  constexpr bool operator!=(MoveId other) const noexcept { return value != other.value; }
};

/// \brief Root storage for a borrow-checker place.
enum class PlaceRootKind {
  Local,
  Parameter,
  Temporary,
  ClosureCapture,
  ReturnSlot,
};

/// \brief A place projection from a root or previous projection.
enum class PlaceProjectionKind {
  Field,
  Deref,
  Index,
};

/// \brief One projection component in a borrow-checker place.
struct PlaceProjection final {
  PlaceProjectionKind kind = PlaceProjectionKind::Field;
  zc::StringPtr name;
  uint32_t index = 0;

  static PlaceProjection field(zc::StringPtr name);
  static PlaceProjection deref();
  static PlaceProjection indexProjection(uint32_t index);

  bool equals(const PlaceProjection& other) const;
};

/// \brief A typed memory place used by move and loan analysis.
class Place final {
public:
  Place(PlaceId id, PlaceRootKind rootKind, uint32_t rootId, type::TypeId typeId);

  static Place local(PlaceId id, uint32_t localId, type::TypeId typeId);
  static Place parameter(PlaceId id, uint32_t parameterId, type::TypeId typeId);
  static Place temporary(PlaceId id, uint32_t temporaryId, type::TypeId typeId);
  static Place closureCapture(PlaceId id, uint32_t captureId, type::TypeId typeId);
  static Place returnSlot(PlaceId id, type::TypeId typeId);

  PlaceId getId() const;
  PlaceRootKind getRootKind() const;
  uint32_t getRootId() const;
  type::TypeId getTypeId() const;
  zc::ArrayPtr<const PlaceProjection> getProjections() const;

  void addFieldProjection(zc::StringPtr name);
  void addDerefProjection();
  void addIndexProjection(uint32_t index);

  bool sameRoot(const Place& other) const;
  bool equals(const Place& other) const;

private:
  PlaceId id;
  PlaceRootKind rootKind;
  uint32_t rootId;
  type::TypeId typeId;
  zc::Vector<PlaceProjection> projections;
};

/// \brief Region lifetime category for borrow checking.
enum class RegionKind {
  Lexical,
  Temporary,
  Loop,
  Closure,
  TaskScope,
  Return,
};

/// \brief Inferred lifetime region.
struct Region final {
  RegionId id;
  RegionKind kind;
  RegionId parent;

  static Region make(RegionId id, RegionKind kind, RegionId parent = RegionId());
  bool hasParent() const;
};

/// \brief Loan permission kind.
enum class LoanKind {
  Shared,
  Mutable,
};

/// \brief Active or recorded borrow loan.
struct Loan final {
  LoanId id;
  PlaceId place;
  LoanKind kind;
  RegionId region;
  ast::NodeId origin;

  static Loan make(LoanId id, PlaceId place, LoanKind kind, RegionId region, ast::NodeId origin);
};

/// \brief Recorded ownership move.
struct Move final {
  MoveId id;
  PlaceId place;
  ast::NodeId origin;

  static Move make(MoveId id, PlaceId place, ast::NodeId origin);
};

/// \brief Structured region-escape fact for later diagnostics.
struct BorrowRegionEscapeReport final {
  RegionId targetRegion;
  RegionId referentRegion;
  ast::NodeId useNode;
  ast::NodeId referentNode;
};

/// \brief Structured scoped-task capture escape fact for later diagnostics.
struct BorrowScopedTaskCaptureReport final {
  RegionId taskRegion;
  RegionId referentRegion;
  ast::NodeId captureNode;
  ast::NodeId referentNode;
};

/// \brief Structured raw-pointer boundary fact for later diagnostics.
struct BorrowRawPointerBoundaryReport final {
  ast::NodeId boundaryNode;
};

/// \brief Precision used when comparing field projections.
enum class FieldOverlapMode {
  Conservative,
  ProvenDisjoint,
};

/// \brief Returns true when two places may refer to overlapping storage.
bool placesOverlap(const Place& lhs, const Place& rhs,
                   FieldOverlapMode fieldMode = FieldOverlapMode::ProvenDisjoint);

/// \brief Side-table owner for borrow-checker model data.
class BorrowModel final {
public:
  BorrowModel();
  ~BorrowModel() noexcept(false);

  ZC_DISALLOW_COPY(BorrowModel);
  BorrowModel(BorrowModel&& other) noexcept;
  BorrowModel& operator=(BorrowModel&& other) noexcept;

  PlaceId addLocalPlace(uint32_t localId, type::TypeId typeId);
  PlaceId addParameterPlace(uint32_t parameterId, type::TypeId typeId);
  PlaceId addTemporaryPlace(uint32_t temporaryId, type::TypeId typeId);
  PlaceId addClosureCapturePlace(uint32_t captureId, type::TypeId typeId);
  PlaceId addReturnSlotPlace(type::TypeId typeId);
  PlaceId addFieldPlace(PlaceId base, zc::StringPtr fieldName);
  PlaceId addDerefPlace(PlaceId base);
  PlaceId addIndexPlace(PlaceId base, uint32_t index);

  void addFieldProjection(PlaceId id, zc::StringPtr name);
  void addDerefProjection(PlaceId id);
  void addIndexProjection(PlaceId id, uint32_t index);

  zc::Maybe<const Place&> getPlace(PlaceId id) const;
  size_t placeCount() const;

  RegionId addRegion(RegionKind kind, RegionId parent = RegionId());
  zc::Maybe<const Region&> getRegion(RegionId id) const;
  size_t regionCount() const;
  bool regionOutlives(RegionId longer, RegionId shorter) const;
  zc::Maybe<BorrowRegionEscapeReport> checkRegionEscape(RegionId targetRegion,
                                                        RegionId referentRegion,
                                                        ast::NodeId useNode,
                                                        ast::NodeId referentNode) const;
  zc::Maybe<BorrowScopedTaskCaptureReport> checkScopedTaskCapture(RegionId taskRegion,
                                                                  RegionId referentRegion,
                                                                  ast::NodeId captureNode,
                                                                  ast::NodeId referentNode) const;
  zc::Maybe<BorrowRawPointerBoundaryReport> checkRawPointerBoundary(ast::NodeId boundaryNode,
                                                                    bool unsafeAcknowledged) const;

  LoanId addLoan(PlaceId place, LoanKind kind, RegionId region, ast::NodeId origin);
  zc::Maybe<const Loan&> getLoan(LoanId id) const;
  size_t loanCount() const;
  zc::Maybe<const Loan&> findConflictingLoan(
      PlaceId place, LoanKind requestedKind,
      FieldOverlapMode fieldMode = FieldOverlapMode::ProvenDisjoint) const;

  MoveId addMove(PlaceId place, ast::NodeId origin);
  zc::Maybe<const Move&> getMove(MoveId id) const;
  size_t moveCount() const;

  bool placesOverlap(PlaceId lhs, PlaceId rhs,
                     FieldOverlapMode fieldMode = FieldOverlapMode::ProvenDisjoint) const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

/// \brief Builds initial borrow-model places from typed AST declarations.
class BorrowPlaceBuilder final {
public:
  BorrowPlaceBuilder(BorrowModel& model, const ast::Tree& tree, const type::TypeEnv& typeEnv,
                     zc::Maybe<const ast::BindingMetadata&> metadata = zc::none) noexcept;

  ~BorrowPlaceBuilder() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(BorrowPlaceBuilder);

  void buildFunctionPlaces(ast::NodeId functionDecl);
  zc::Maybe<PlaceId> getPlaceForNode(ast::NodeId node) const;
  size_t mappedNodeCount() const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

/// \brief Result of the initial borrow-place collection phase slice.
class BorrowPlaceCollectionResult final {
public:
  BorrowPlaceCollectionResult();
  ~BorrowPlaceCollectionResult() noexcept(false);

  ZC_DISALLOW_COPY(BorrowPlaceCollectionResult);
  BorrowPlaceCollectionResult(BorrowPlaceCollectionResult&& other) noexcept;
  BorrowPlaceCollectionResult& operator=(BorrowPlaceCollectionResult&& other) noexcept;

  BorrowModel& getModel();
  const BorrowModel& getModel() const;
  void setPlaceForNode(ast::NodeId node, PlaceId place);
  zc::Maybe<PlaceId> getPlaceForNode(ast::NodeId node) const;
  size_t mappedNodeCount() const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

BorrowPlaceCollectionResult collectBorrowPlaces(
    const ast::Tree& tree, const type::TypeEnv& typeEnv,
    zc::Maybe<const ast::BindingMetadata&> metadata = zc::none);

enum class BorrowCfgNodeKind {
  Entry,
  Statement,
  Branch,
  Join,
  Return,
  Exit,
};

struct BorrowCfgNodeId final {
  uint32_t value = 0;

  constexpr BorrowCfgNodeId() noexcept = default;
  constexpr explicit BorrowCfgNodeId(uint32_t raw) noexcept : value(raw) {}

  constexpr bool isValid() const noexcept { return value != 0; }
  constexpr bool operator==(BorrowCfgNodeId other) const noexcept { return value == other.value; }
  constexpr bool operator!=(BorrowCfgNodeId other) const noexcept { return value != other.value; }
};

struct BorrowCfgEdge final {
  BorrowCfgNodeId from;
  BorrowCfgNodeId to;
};

/// \brief Structured use-after-move fact for later diagnostics.
struct BorrowUseAfterMoveReport final {
  BorrowCfgNodeId node;
  PlaceId place;
  BorrowCfgNodeId moveOrigin;
};

/// \brief Structured move-out-of-borrow fact for later diagnostics.
struct BorrowMoveOutOfBorrowReport final {
  BorrowCfgNodeId moveNode;
  PlaceId place;
  ast::NodeId borrowOrigin;
};

/// \brief Structured missing-linear-consume fact for later diagnostics.
struct BorrowMissingConsumeReport final {
  BorrowCfgNodeId node;
  PlaceId place;
  BorrowCfgNodeId initializeOrigin;
};

/// \brief Structured double-linear-consume fact for later diagnostics.
struct BorrowDoubleConsumeReport final {
  BorrowCfgNodeId node;
  PlaceId place;
  BorrowCfgNodeId consumeOrigin;
};

/// \brief Structured borrow-conflict fact for later diagnostics.
struct BorrowConflictReport final {
  BorrowCfgNodeId node;
  PlaceId requestedPlace;
  LoanKind requestedKind;
  LoanId loanId;
  PlaceId loanPlace;
  LoanKind loanKind;
  RegionId region;
  ast::NodeId origin;
};

class BorrowCfg final {
public:
  BorrowCfg();
  ~BorrowCfg() noexcept(false);

  ZC_DISALLOW_COPY(BorrowCfg);
  BorrowCfg(BorrowCfg&& other) noexcept;
  BorrowCfg& operator=(BorrowCfg&& other) noexcept;

  BorrowCfgNodeId addNode(BorrowCfgNodeKind kind, ast::NodeId astNode = ast::NodeId());
  void addEdge(BorrowCfgNodeId from, BorrowCfgNodeId to);

  size_t nodeCount() const;
  size_t edgeCount() const;
  BorrowCfgNodeKind getNodeKind(BorrowCfgNodeId id) const;
  ast::NodeId getNodeAst(BorrowCfgNodeId id) const;
  const BorrowCfgEdge& getEdge(size_t index) const;
  BorrowCfgNodeId getEntry() const;
  BorrowCfgNodeId getExit() const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

BorrowCfg buildStraightLineBorrowCfg(const ast::Tree& tree, ast::NodeId functionDecl);

class BorrowCheckerResult final {
public:
  BorrowCheckerResult();
  ~BorrowCheckerResult() noexcept(false);

  ZC_DISALLOW_COPY(BorrowCheckerResult);
  BorrowCheckerResult(BorrowCheckerResult&& other) noexcept;
  BorrowCheckerResult& operator=(BorrowCheckerResult&& other) noexcept;

  BorrowPlaceCollectionResult& getPlaces();
  const BorrowPlaceCollectionResult& getPlaces() const;
  void setPlaces(BorrowPlaceCollectionResult places);
  void addFunctionSummary(ast::NodeId functionDecl, BorrowCfg cfg);
  void addFunctionMoveFact(size_t index, BorrowCfgNodeId node, PlaceId place);
  void addFunctionReinitializeFact(size_t index, BorrowCfgNodeId node, PlaceId place);
  size_t functionCount() const;
  ast::NodeId getFunctionDecl(size_t index) const;
  const BorrowCfg& getFunctionCfg(size_t index) const;
  bool isFunctionPlaceMovedAt(size_t index, BorrowCfgNodeId node, PlaceId place) const;
  zc::Maybe<BorrowCfgNodeId> getFunctionMoveOriginBefore(size_t index, BorrowCfgNodeId node,
                                                         PlaceId place) const;
  zc::Maybe<BorrowCfgNodeId> getFunctionOverlappingMoveOriginBefore(size_t index,
                                                                    BorrowCfgNodeId node,
                                                                    PlaceId place) const;
  zc::Maybe<BorrowCfgNodeId> getFunctionMoveOrigin(size_t index, BorrowCfgNodeId node,
                                                   PlaceId place) const;
  void addFunctionUseAfterMoveReport(size_t index, BorrowUseAfterMoveReport report);
  size_t functionUseAfterMoveReportCount(size_t index) const;
  const BorrowUseAfterMoveReport& getFunctionUseAfterMoveReport(size_t index,
                                                                size_t reportIndex) const;
  void addFunctionMoveOutOfBorrowReport(size_t index, BorrowMoveOutOfBorrowReport report);
  size_t functionMoveOutOfBorrowReportCount(size_t index) const;
  const BorrowMoveOutOfBorrowReport& getFunctionMoveOutOfBorrowReport(size_t index,
                                                                      size_t reportIndex) const;
  void addFunctionBorrowConflictReport(size_t index, BorrowConflictReport report);
  size_t functionBorrowConflictReportCount(size_t index) const;
  const BorrowConflictReport& getFunctionBorrowConflictReport(size_t index,
                                                              size_t reportIndex) const;
  void addFunctionRegionEscapeReport(size_t index, BorrowRegionEscapeReport report);
  size_t functionRegionEscapeReportCount(size_t index) const;
  const BorrowRegionEscapeReport& getFunctionRegionEscapeReport(size_t index,
                                                                size_t reportIndex) const;
  void addFunctionRawPointerBoundaryReport(size_t index, BorrowRawPointerBoundaryReport report);
  size_t functionRawPointerBoundaryReportCount(size_t index) const;
  const BorrowRawPointerBoundaryReport& getFunctionRawPointerBoundaryReport(
      size_t index, size_t reportIndex) const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class BorrowCheckerPhase final {
public:
  BorrowCheckerPhase(const ast::Tree& tree, const type::TypeEnv& typeEnv,
                     zc::Maybe<const ast::BindingMetadata&> metadata = zc::none) noexcept;
  ~BorrowCheckerPhase() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(BorrowCheckerPhase);

  BorrowCheckerResult run() const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

/// \brief Emit diagnostics for facts already inferred by a borrow-checker result.
///
/// This helper deliberately consumes an existing result instead of running the
/// phase. Driver scheduling remains a separate policy decision.
size_t emitBorrowDiagnostics(const ast::Tree& tree, const BorrowCheckerResult& result,
                             diagnostics::DiagnosticEngine& diags);

/// \brief Emit one borrow-conflict diagnostic fact.
void emitBorrowConflictDiagnostic(const ast::Tree& tree, const BorrowCfg& cfg,
                                  const BorrowConflictReport& report,
                                  diagnostics::DiagnosticEngine& diags);

/// \brief Emit one move-out-of-borrow diagnostic fact.
void emitBorrowMoveOutOfBorrowDiagnostic(const ast::Tree& tree, const BorrowCfg& cfg,
                                         const BorrowMoveOutOfBorrowReport& report,
                                         diagnostics::DiagnosticEngine& diags);

/// \brief Emit one region-escape diagnostic fact.
void emitBorrowRegionEscapeDiagnostic(const ast::Tree& tree, const BorrowRegionEscapeReport& report,
                                      diagnostics::DiagnosticEngine& diags);

/// \brief Emit one missing linear-consume diagnostic fact.
void emitBorrowMissingConsumeDiagnostic(const ast::Tree& tree, const BorrowCfg& cfg,
                                        const BorrowMissingConsumeReport& report,
                                        diagnostics::DiagnosticEngine& diags);

/// \brief Emit one double linear-consume diagnostic fact.
void emitBorrowDoubleConsumeDiagnostic(const ast::Tree& tree, const BorrowCfg& cfg,
                                       const BorrowDoubleConsumeReport& report,
                                       diagnostics::DiagnosticEngine& diags);

/// \brief Emit one scoped-task capture diagnostic fact.
void emitBorrowScopedTaskCaptureDiagnostic(const ast::Tree& tree,
                                           const BorrowScopedTaskCaptureReport& report,
                                           diagnostics::DiagnosticEngine& diags);

/// \brief Emit one raw-pointer safe-boundary diagnostic fact.
void emitBorrowRawPointerBoundaryDiagnostic(const ast::Tree& tree,
                                            const BorrowRawPointerBoundaryReport& report,
                                            diagnostics::DiagnosticEngine& diags);

class BorrowMoveState final {
public:
  explicit BorrowMoveState(const BorrowCfg& cfg);
  ~BorrowMoveState() noexcept(false);

  ZC_DISALLOW_COPY(BorrowMoveState);
  BorrowMoveState(BorrowMoveState&& other) noexcept;
  BorrowMoveState& operator=(BorrowMoveState&& other) noexcept;

  void addMove(BorrowCfgNodeId node, PlaceId place);
  void addReinitialize(BorrowCfgNodeId node, PlaceId place);
  void propagate();
  bool isMovedBefore(BorrowCfgNodeId node, PlaceId place) const;
  bool isMovedAt(BorrowCfgNodeId node, PlaceId place) const;
  zc::Maybe<BorrowCfgNodeId> getMoveOriginBefore(BorrowCfgNodeId node, PlaceId place) const;
  zc::Maybe<BorrowCfgNodeId> getMoveOrigin(BorrowCfgNodeId node, PlaceId place) const;
  zc::Maybe<BorrowUseAfterMoveReport> checkUseAfterMoveAt(BorrowCfgNodeId node,
                                                          PlaceId place) const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class BorrowLinearState final {
public:
  explicit BorrowLinearState(const BorrowCfg& cfg);
  ~BorrowLinearState() noexcept(false);

  ZC_DISALLOW_COPY(BorrowLinearState);
  BorrowLinearState(BorrowLinearState&& other) noexcept;
  BorrowLinearState& operator=(BorrowLinearState&& other) noexcept;

  void addInitialize(BorrowCfgNodeId node, PlaceId place);
  void addConsume(BorrowCfgNodeId node, PlaceId place);
  void propagate();
  bool isOutstandingAt(BorrowCfgNodeId node, PlaceId place) const;
  zc::Maybe<BorrowCfgNodeId> getInitializeOrigin(BorrowCfgNodeId node, PlaceId place) const;
  zc::Maybe<BorrowMissingConsumeReport> checkMissingConsumeAt(BorrowCfgNodeId node,
                                                              PlaceId place) const;
  zc::Maybe<BorrowDoubleConsumeReport> checkDoubleConsumeAt(BorrowCfgNodeId node,
                                                            PlaceId place) const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class BorrowLoanState final {
public:
  BorrowLoanState(const BorrowCfg& cfg, const BorrowModel& model);
  ~BorrowLoanState() noexcept(false);

  ZC_DISALLOW_COPY(BorrowLoanState);
  BorrowLoanState(BorrowLoanState&& other) noexcept;
  BorrowLoanState& operator=(BorrowLoanState&& other) noexcept;

  void addActiveLoan(BorrowCfgNodeId node, LoanId loan);
  void addEndLoan(BorrowCfgNodeId node, LoanId loan);
  void addSuspendLoan(BorrowCfgNodeId node, LoanId loan);
  void addResumeLoan(BorrowCfgNodeId node, LoanId loan);
  void propagate();
  zc::Maybe<BorrowConflictReport> checkBorrowConflictAt(
      BorrowCfgNodeId node, PlaceId place, LoanKind requestedKind,
      FieldOverlapMode fieldMode = FieldOverlapMode::ProvenDisjoint) const;
  zc::Maybe<const Loan&> findConflictingLoanAt(
      BorrowCfgNodeId node, PlaceId place, LoanKind requestedKind,
      FieldOverlapMode fieldMode = FieldOverlapMode::ProvenDisjoint) const;
  zc::Maybe<LoanId> findConflictingLoanIdAt(
      BorrowCfgNodeId node, PlaceId place, LoanKind requestedKind,
      FieldOverlapMode fieldMode = FieldOverlapMode::ProvenDisjoint) const;
  zc::Maybe<ast::NodeId> findConflictingLoanOriginAt(
      BorrowCfgNodeId node, PlaceId place, LoanKind requestedKind,
      FieldOverlapMode fieldMode = FieldOverlapMode::ProvenDisjoint) const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class BorrowLoanBuilder final {
public:
  BorrowLoanBuilder(BorrowModel& model, const ast::Tree& tree,
                    const BorrowPlaceCollectionResult& places) noexcept;
  ~BorrowLoanBuilder() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(BorrowLoanBuilder);

  void markMutableBorrow(ast::NodeId borrowExpr);
  void buildFunctionLoans(ast::NodeId functionDecl);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace checker
}  // namespace compiler
}  // namespace zomlang
