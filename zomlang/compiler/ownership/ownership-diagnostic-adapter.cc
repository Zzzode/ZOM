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

#include "zomlang/compiler/ownership/ownership-diagnostic-adapter.h"

#include "zc/core/memory.h"
#include "zomlang/compiler/binder/graph/parsed-module.h"
#include "zomlang/compiler/diagnostics/core/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/core/diagnostic.h"
#include "zomlang/compiler/source/location.h"

namespace zomlang::compiler::ownership {
namespace {

using diagnostics::DiagID;

source::SourceLoc locationFor(const binder::VerifiedParsedModule& parsedModule,
                              const identity::SourceSpan& span) {
  ZC_IF_SOME(location, parsedModule.sourceLocFor(span)) { return location; }
  return source::SourceLoc();
}

template <typename Variant>
void emitWithCauses(diagnostics::DiagnosticEngine& diagnostics,
                    const binder::VerifiedParsedModule& parsedModule, const Variant& failure,
                    DiagID primaryId, DiagID noteId) {
  diagnostics::Diagnostic primary(primaryId, locationFor(parsedModule, failure.useSpan));
  for (const auto& cause : failure.causes) {
    primary.addChildDiagnostic(
        zc::heap<diagnostics::Diagnostic>(noteId, locationFor(parsedModule, cause.span)));
  }
  diagnostics.emit(primary);
}

}  // namespace

void emitOwnershipSourceFailures(diagnostics::DiagnosticEngine& diagnostics,
                                 const binder::VerifiedParsedModule& parsedModule,
                                 zc::ArrayPtr<const OwnershipSourceFailure> failures) {
  for (const auto& failure : failures) {
    ZC_SWITCH_ONEOF(failure) {
      ZC_CASE_ONEOF(value, UseAfterMoveFailure) {
        emitWithCauses(diagnostics, parsedModule, value, DiagID::UseAfterMove,
                       DiagID::ValueMovedHere);
      }
      ZC_CASE_ONEOF(value, MutableBorrowConflictFailure) {
        emitWithCauses(diagnostics, parsedModule, value, DiagID::MutableBorrowConflicts,
                       DiagID::BorrowOriginHere);
      }
      ZC_CASE_ONEOF(value, UninitializedPlaceUseFailure) {
        emitWithCauses(diagnostics, parsedModule, value, DiagID::UninitializedPlaceUse,
                       DiagID::PlaceBecameUnavailableHere);
      }
      ZC_CASE_ONEOF(value, SharedBorrowConflictFailure) {
        emitWithCauses(diagnostics, parsedModule, value, DiagID::SharedBorrowConflicts,
                       DiagID::BorrowOriginHere);
      }
      ZC_CASE_ONEOF(value, BorrowDoesNotLiveLongEnoughFailure) {
        emitWithCauses(diagnostics, parsedModule, value, DiagID::BorrowDoesNotLiveLongEnough,
                       DiagID::BorrowReferentHere);
      }
      ZC_CASE_ONEOF(value, LinearNotConsumedFailure) {
        emitWithCauses(diagnostics, parsedModule, value, DiagID::LinearNotConsumed,
                       DiagID::LinearInitializedHere);
      }
      ZC_CASE_ONEOF(value, LinearConsumedTwiceFailure) {
        emitWithCauses(diagnostics, parsedModule, value, DiagID::LinearConsumedTwice,
                       DiagID::LinearFirstConsumedHere);
      }
      ZC_CASE_ONEOF(value, RawPointerBoundaryRequiresUnsafeFailure) {
        diagnostics.emit(diagnostics::Diagnostic(DiagID::RawPointerBoundaryRequiresUnsafe,
                                                 locationFor(parsedModule, value.useSpan)));
      }
      ZC_CASE_ONEOF(value, MoveOutOfBorrowFailure) {
        emitWithCauses(diagnostics, parsedModule, value, DiagID::MoveOutOfBorrow,
                       DiagID::BorrowOriginHere);
      }
    }
  }
}

}  // namespace zomlang::compiler::ownership
