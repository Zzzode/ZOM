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
// See the License for the specific language governing permissions and
// limitations under the License.

#include "compiler/ownership/diagnostics/ownership-source-diagnostic-projector.h"

#include "zc/core/debug.h"

namespace zomlang::compiler::ownership {
namespace {

using diagnostics::DiagID;
using diagnostics::SemanticDiagnosticBatchProjectionFailure;
using diagnostics::SemanticDiagnosticBatchProjectionResult;
using diagnostics::SemanticDiagnosticFactBatch;

void appendPoint(zc::Vector<uint32_t>& path, const MirPoint& point) {
  path.add(static_cast<uint32_t>(point.kind()));
  switch (point.kind()) {
    case MirPointKind::Entry:
      return;
    case MirPointKind::BeforeStatement: {
      const auto& value = point.beforeStatementValue();
      path.add(value.block.ordinal());
      path.add(value.ordinal);
      return;
    }
    case MirPointKind::AfterStatement: {
      const auto& value = point.afterStatementValue();
      path.add(value.block.ordinal());
      path.add(value.ordinal);
      return;
    }
    case MirPointKind::BeforeTerminator:
      path.add(point.beforeTerminatorValue().block.ordinal());
      return;
    case MirPointKind::Edge: {
      const auto& value = point.edgeValue();
      path.add(value.from.ordinal());
      path.add(value.edgeOrdinal);
      path.add(value.to.ordinal());
      return;
    }
    case MirPointKind::Exit: {
      const auto& value = point.exitValue();
      path.add(value.block.ordinal());
      path.add(static_cast<uint32_t>(value.kind));
      return;
    }
  }
  ZC_UNREACHABLE
}

zc::Vector<uint32_t> eventPath(uint32_t failureKind, const MirEventKey& event,
                               uint32_t traversalOrdinal) {
  zc::Vector<uint32_t> path;
  path.add(failureKind);
  appendPoint(path, event.location.point);
  path.add(event.operandOrdinal);
  path.add(traversalOrdinal);
  return path;
}

bool appendProjection(SemanticDiagnosticFactBatch& batch,
                      diagnostics::SemanticDiagnosticProjectionResult&& result) {
  if (!result.is<diagnostics::SemanticDiagnosticFactProjection>()) return false;
  auto projected = zc::mv(result).get<diagnostics::SemanticDiagnosticFactProjection>();
  batch.facts.add(zc::mv(projected.fact));
  for (auto& entry : projected.provenance) { batch.provenance.add(zc::mv(entry)); }
  return true;
}

template <typename Variant>
bool projectWithCauses(SemanticDiagnosticFactBatch& batch, const identity::ModuleKey& module,
                       zc::ArrayPtr<const uint8_t> owner, uint32_t failureKind,
                       const Variant& failure, DiagID primaryId, DiagID noteId) {
  auto path = eventPath(failureKind, failure.primary, failure.traversalOrdinal);
  zc::Vector<zc::String> arguments;
  zc::Vector<diagnostics::SemanticDiagnosticRelatedSite> related(failure.causes.size());
  for (const auto& cause : failure.causes) {
    related.add(diagnostics::SemanticDiagnosticRelatedSite{
        diagnostics::DiagnosticSecondaryRole::Note, noteId, cause.span.clone(),
        zc::Vector<zc::String>()});
  }
  return appendProjection(
      batch, diagnostics::projectSemanticDiagnosticFact(
                 module, diagnostics::SemanticDiagnosticDomain::Ownership, owner, path.asPtr(),
                 primaryId, zc::mv(arguments), failure.useSpan, zc::mv(related)));
}

template <typename Variant>
bool projectWithoutCauses(SemanticDiagnosticFactBatch& batch, const identity::ModuleKey& module,
                          zc::ArrayPtr<const uint8_t> owner, uint32_t failureKind,
                          const Variant& failure, DiagID primaryId) {
  auto path = eventPath(failureKind, failure.primary, failure.traversalOrdinal);
  zc::Vector<zc::String> arguments;
  zc::Vector<diagnostics::SemanticDiagnosticRelatedSite> related;
  return appendProjection(
      batch, diagnostics::projectSemanticDiagnosticFact(
                 module, diagnostics::SemanticDiagnosticDomain::Ownership, owner, path.asPtr(),
                 primaryId, zc::mv(arguments), failure.useSpan, zc::mv(related)));
}

template <typename Variant, typename Project>
zc::Maybe<SemanticDiagnosticBatchProjectionFailure> projectOwnedFailure(
    SemanticDiagnosticFactBatch& batch, const checker::CheckerIdentityAuthority& identities,
    const Variant& failure, Project&& project) {
  auto owner = identities.definition(failure.owner);
  if (owner == zc::none || failure.primary.location.owner != failure.owner) {
    return SemanticDiagnosticBatchProjectionFailure::InvalidOwnerIdentity;
  }
  ZC_IF_SOME(ownerEntry, owner) {
    auto module = identities.module(ownerEntry.record().module());
    if (module == zc::none) {
      return SemanticDiagnosticBatchProjectionFailure::InvalidModuleIdentity;
    }
    auto ownerBytes = ownerEntry.key().encode();
    ZC_IF_SOME(moduleEntry, module) {
      if (!project(moduleEntry.key(), ownerBytes.asPtr())) {
        return SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
      }
      return zc::none;
    }
    return SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
  }
  return SemanticDiagnosticBatchProjectionFailure::InvalidOwnerIdentity;
}

DiagID surfaceDiagnostic(SurfaceSyntaxKind kind) {
  switch (kind) {
    case SurfaceSyntaxKind::Spawn:
    case SurfaceSyntaxKind::Suspend:
      return DiagID::ConcurrencySemanticsUnavailable;
    case SurfaceSyntaxKind::VoidReturn:
      return DiagID::VoidReturnSemanticsUnavailable;
    case SurfaceSyntaxKind::ExpressionStatement:
      return DiagID::ExpressionStatementSemanticsUnavailable;
    case SurfaceSyntaxKind::FunctionBody:
      return DiagID::FunctionBodySemanticsUnavailable;
    case SurfaceSyntaxKind::Match:
    case SurfaceSyntaxKind::Loop:
    case SurfaceSyntaxKind::LoopControl:
    case SurfaceSyntaxKind::Label:
      return DiagID::ControlFlowSemanticsUnavailable;
  }
  ZC_UNREACHABLE
}

}  // namespace

SemanticDiagnosticBatchProjectionResult projectSurfaceSourceFailures(
    identity::ModuleId moduleId, const checker::CheckerIdentityAuthority& identities,
    zc::ArrayPtr<const SurfaceFailure> failures) {
  auto module = identities.module(moduleId);
  if (module == zc::none) return SemanticDiagnosticBatchProjectionFailure::InvalidModuleIdentity;
  SemanticDiagnosticFactBatch batch;
  ZC_IF_SOME(moduleEntry, module) {
    auto owner = moduleEntry.key().encode();
    for (const auto& failure : failures) {
      zc::Vector<uint32_t> path;
      path.add(static_cast<uint32_t>(failure.kind));
      path.add(failure.traversalOrdinal);
      zc::Vector<zc::String> arguments;
      zc::Vector<diagnostics::SemanticDiagnosticRelatedSite> related;
      if (!appendProjection(
              batch, diagnostics::projectSemanticDiagnosticFact(
                         moduleEntry.key(), diagnostics::SemanticDiagnosticDomain::OwnershipSurface,
                         owner.asPtr(), path.asPtr(), surfaceDiagnostic(failure.kind),
                         zc::mv(arguments), failure.primarySpan, zc::mv(related)))) {
        return SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
      }
    }
  }
  return batch;
}

SemanticDiagnosticBatchProjectionResult projectOwnershipSourceFailures(
    const checker::CheckerIdentityAuthority& identities,
    zc::ArrayPtr<const SourceFailure> failures) {
  SemanticDiagnosticFactBatch batch;
  for (const auto& failure : failures) {
    zc::Maybe<SemanticDiagnosticBatchProjectionFailure> projectionFailure;
    ZC_SWITCH_ONEOF(failure) {
      ZC_CASE_ONEOF(value, UseAfterMoveFailure) {
        projectionFailure = projectOwnedFailure(
            batch, identities, value, [&](const identity::ModuleKey& module, auto owner) {
              return projectWithCauses(batch, module, owner, 0x01, value, DiagID::UseAfterMove,
                                       DiagID::ValueMovedHere);
            });
      }
      ZC_CASE_ONEOF(value, MutableBorrowConflictFailure) {
        projectionFailure = projectOwnedFailure(
            batch, identities, value, [&](const identity::ModuleKey& module, auto owner) {
              return projectWithCauses(batch, module, owner, 0x02, value,
                                       DiagID::MutableBorrowConflicts, DiagID::BorrowOriginHere);
            });
      }
      ZC_CASE_ONEOF(value, UninitializedPlaceUseFailure) {
        projectionFailure = projectOwnedFailure(
            batch, identities, value, [&](const identity::ModuleKey& module, auto owner) {
              return projectWithCauses(batch, module, owner, 0x03, value,
                                       DiagID::UninitializedPlaceUse,
                                       DiagID::PlaceBecameUnavailableHere);
            });
      }
      ZC_CASE_ONEOF(value, SharedBorrowConflictFailure) {
        projectionFailure = projectOwnedFailure(
            batch, identities, value, [&](const identity::ModuleKey& module, auto owner) {
              return projectWithCauses(batch, module, owner, 0x04, value,
                                       DiagID::SharedBorrowConflicts, DiagID::BorrowOriginHere);
            });
      }
      ZC_CASE_ONEOF(value, BorrowDoesNotLiveLongEnoughFailure) {
        projectionFailure = projectOwnedFailure(
            batch, identities, value, [&](const identity::ModuleKey& module, auto owner) {
              return projectWithCauses(batch, module, owner, 0x05, value,
                                       DiagID::BorrowDoesNotLiveLongEnough,
                                       DiagID::BorrowReferentHere);
            });
      }
      ZC_CASE_ONEOF(value, LinearNotConsumedFailure) {
        projectionFailure = projectOwnedFailure(
            batch, identities, value, [&](const identity::ModuleKey& module, auto owner) {
              return projectWithCauses(batch, module, owner, 0x06, value, DiagID::LinearNotConsumed,
                                       DiagID::LinearInitializedHere);
            });
      }
      ZC_CASE_ONEOF(value, LinearConsumedTwiceFailure) {
        projectionFailure = projectOwnedFailure(
            batch, identities, value, [&](const identity::ModuleKey& module, auto owner) {
              return projectWithCauses(batch, module, owner, 0x07, value,
                                       DiagID::LinearConsumedTwice,
                                       DiagID::LinearFirstConsumedHere);
            });
      }
      ZC_CASE_ONEOF(value, RawPointerBoundaryRequiresUnsafeFailure) {
        projectionFailure = projectOwnedFailure(
            batch, identities, value, [&](const identity::ModuleKey& module, auto owner) {
              return projectWithoutCauses(batch, module, owner, 0x08, value,
                                          DiagID::RawPointerBoundaryRequiresUnsafe);
            });
      }
      ZC_CASE_ONEOF(value, MoveOutOfBorrowFailure) {
        projectionFailure = projectOwnedFailure(
            batch, identities, value, [&](const identity::ModuleKey& module, auto owner) {
              return projectWithCauses(batch, module, owner, 0x09, value, DiagID::MoveOutOfBorrow,
                                       DiagID::BorrowOriginHere);
            });
      }
    }
    ZC_IF_SOME(value, projectionFailure) { return value; }
  }
  return batch;
}

}  // namespace zomlang::compiler::ownership
