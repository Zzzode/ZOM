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

#include "zc/ztest/test.h"
#include "compiler/ir/ir-failure.h"
#include "compiler/ownership/facts/borrow-source.h"
#include "compiler/ownership/facts/capture.h"
#include "compiler/ownership/facts/escape.h"
#include "compiler/ownership/facts/flow.h"
#include "compiler/ownership/facts/init.h"
#include "compiler/ownership/facts/inputs.h"
#include "compiler/ownership/facts/linear-source.h"
#include "compiler/ownership/facts/loans.h"
#include "compiler/ownership/facts/paths.h"
#include "compiler/ownership/facts/refs.h"
#include "compiler/ownership/facts/regions.h"
#include "compiler/ownership/facts/resources.h"
#include "compiler/ownership/facts/states.h"
#include "compiler/ownership/ownership-checked-mir.h"
#include "compiler/ownership/ownership-event-overlay.h"

namespace zomlang::compiler::ownership {
namespace {

ZC_TEST("OwnershipVerifiedTypes.HaveNoPublicConstructionPath") {
  // Every verified inventory and successor stores its sole value constructor
  // behind a private zc::Own<Impl>&& entry point whose Impl is a private
  // nested type. External code cannot name the argument, so only the
  // befriended verifier can construct. The move constructor is the sole
  // public construction path.

  static_assert(!__is_constructible(facts::VerifiedOwnershipInputs));
  static_assert(
      !__is_constructible(facts::VerifiedOwnershipInputs, const facts::VerifiedOwnershipInputs&));
  static_assert(
      __is_constructible(facts::VerifiedOwnershipInputs, facts::VerifiedOwnershipInputs&&));

  static_assert(!__is_constructible(facts::VerifiedMovePaths));
  static_assert(!__is_constructible(facts::VerifiedMovePaths, const facts::VerifiedMovePaths&));
  static_assert(__is_constructible(facts::VerifiedMovePaths, facts::VerifiedMovePaths&&));

  static_assert(!__is_constructible(facts::VerifiedFlow));
  static_assert(!__is_constructible(facts::VerifiedFlow, const facts::VerifiedFlow&));
  static_assert(__is_constructible(facts::VerifiedFlow, facts::VerifiedFlow&&));

  static_assert(!__is_constructible(facts::VerifiedInitializationFacts));
  static_assert(!__is_constructible(facts::VerifiedInitializationFacts,
                                    const facts::VerifiedInitializationFacts&));
  static_assert(
      __is_constructible(facts::VerifiedInitializationFacts, facts::VerifiedInitializationFacts&&));

  static_assert(!__is_constructible(facts::VerifiedLoanFacts));
  static_assert(!__is_constructible(facts::VerifiedLoanFacts, const facts::VerifiedLoanFacts&));
  static_assert(__is_constructible(facts::VerifiedLoanFacts, facts::VerifiedLoanFacts&&));

  static_assert(!__is_constructible(facts::VerifiedReferenceDefinitions));
  static_assert(!__is_constructible(facts::VerifiedReferenceDefinitions,
                                    const facts::VerifiedReferenceDefinitions&));
  static_assert(__is_constructible(facts::VerifiedReferenceDefinitions,
                                   facts::VerifiedReferenceDefinitions&&));

  static_assert(!__is_constructible(facts::VerifiedReborrowRegions));
  static_assert(
      !__is_constructible(facts::VerifiedReborrowRegions, const facts::VerifiedReborrowRegions&));
  static_assert(
      __is_constructible(facts::VerifiedReborrowRegions, facts::VerifiedReborrowRegions&&));

  static_assert(!__is_constructible(facts::VerifiedReborrowStates));
  static_assert(
      !__is_constructible(facts::VerifiedReborrowStates, const facts::VerifiedReborrowStates&));
  static_assert(__is_constructible(facts::VerifiedReborrowStates, facts::VerifiedReborrowStates&&));

  static_assert(!__is_constructible(facts::VerifiedEscapeFacts));
  static_assert(!__is_constructible(facts::VerifiedEscapeFacts, const facts::VerifiedEscapeFacts&));
  static_assert(__is_constructible(facts::VerifiedEscapeFacts, facts::VerifiedEscapeFacts&&));

  static_assert(!__is_constructible(facts::VerifiedCaptureFacts));
  static_assert(
      !__is_constructible(facts::VerifiedCaptureFacts, const facts::VerifiedCaptureFacts&));
  static_assert(__is_constructible(facts::VerifiedCaptureFacts, facts::VerifiedCaptureFacts&&));

  static_assert(!__is_constructible(facts::VerifiedOwnershipResourceFacts));
  static_assert(!__is_constructible(facts::VerifiedOwnershipResourceFacts,
                                    const facts::VerifiedOwnershipResourceFacts&));
  static_assert(__is_constructible(facts::VerifiedOwnershipResourceFacts,
                                   facts::VerifiedOwnershipResourceFacts&&));

  static_assert(!__is_constructible(VerifiedOwnershipEventOverlay));
  static_assert(
      !__is_constructible(VerifiedOwnershipEventOverlay, const VerifiedOwnershipEventOverlay&));
  static_assert(__is_constructible(VerifiedOwnershipEventOverlay, VerifiedOwnershipEventOverlay&&));

  static_assert(!__is_constructible(OwnershipCheckedMir));
  static_assert(!__is_constructible(OwnershipCheckedMir, const OwnershipCheckedMir&));
  static_assert(__is_constructible(OwnershipCheckedMir, OwnershipCheckedMir&&));

  // Source verification results expose private constructors whose argument
  // types include public IR rejection operations. External code can name the
  // argument types but the constructor itself is inaccessible; only the
  // befriended verifier may construct. The move constructor remains public.

  static_assert(!__is_constructible(facts::BorrowSourceVerificationResult));
  static_assert(!__is_constructible(facts::BorrowSourceVerificationResult,
                                    const facts::BorrowSourceVerificationResult&));
  static_assert(__is_constructible(facts::BorrowSourceVerificationResult,
                                   facts::BorrowSourceVerificationResult&&));
  static_assert(!__is_constructible(facts::BorrowSourceVerificationResult,
                                    ir::IdentityInvariantRejectedIrOperation&&));
  static_assert(!__is_constructible(facts::BorrowSourceVerificationResult,
                                    ir::IrInvariantRejectedIrOperation&&));

  static_assert(!__is_constructible(facts::InitializationSourceVerificationResult));
  static_assert(!__is_constructible(facts::InitializationSourceVerificationResult,
                                    const facts::InitializationSourceVerificationResult&));
  static_assert(__is_constructible(facts::InitializationSourceVerificationResult,
                                   facts::InitializationSourceVerificationResult&&));
  static_assert(!__is_constructible(facts::InitializationSourceVerificationResult,
                                    ir::IdentityInvariantRejectedIrOperation&&));
  static_assert(!__is_constructible(facts::InitializationSourceVerificationResult,
                                    ir::IrInvariantRejectedIrOperation&&));

  static_assert(!__is_constructible(facts::LinearSourceVerificationResult));
  static_assert(!__is_constructible(facts::LinearSourceVerificationResult,
                                    const facts::LinearSourceVerificationResult&));
  static_assert(__is_constructible(facts::LinearSourceVerificationResult,
                                   facts::LinearSourceVerificationResult&&));
  static_assert(!__is_constructible(facts::LinearSourceVerificationResult,
                                    ir::IdentityInvariantRejectedIrOperation&&));
  static_assert(!__is_constructible(facts::LinearSourceVerificationResult,
                                    ir::IrInvariantRejectedIrOperation&&));
}

}  // namespace
}  // namespace zomlang::compiler::ownership
