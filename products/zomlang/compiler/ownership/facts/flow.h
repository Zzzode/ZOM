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

#pragma once

#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ownership/facts/points.h"

namespace zomlang::compiler::ownership::facts {

/// \brief One directed transition between adjacent ownership-analysis points.
struct FlowEdge final {
  OwnershipPoint from;
  OwnershipPoint to;
};

/// \brief Complete current-subset ownership flow graph for one MIR function.
struct FlowFunction final {
  identity::DefId owner;
  zc::Vector<OwnershipPoint> points;
  zc::Vector<FlowEdge> edges;
};

/// \brief Untrusted ownership flow inventory awaiting independent reconstruction.
class FlowCandidate final {
public:
  FlowCandidate(identity::SemanticContextBrand semanticContext,
                identity::ContextFingerprint&& contextFingerprint,
                identity::ModuleId module, mir::MirRevisionId builtRevision,
                OwnershipEventOverlayRevision overlayRevision,
                zc::Vector<FlowFunction>&& functions) noexcept;
  FlowCandidate(FlowCandidate&&) noexcept = default;
  FlowCandidate& operator=(FlowCandidate&&) noexcept = delete;
  ZC_DISALLOW_COPY(FlowCandidate);

  identity::SemanticContextBrand semanticContext;
  identity::ContextFingerprint contextFingerprint;
  identity::ModuleId module;
  mir::MirRevisionId builtRevision;
  OwnershipEventOverlayRevision overlayRevision;
  zc::Vector<FlowFunction> functions;
};

/// \brief Immutable reachable ownership point and edge inventory for Built MIR.
class VerifiedFlow final {
public:
  ~VerifiedFlow() noexcept(false);
  VerifiedFlow(VerifiedFlow&&) noexcept;
  VerifiedFlow& operator=(VerifiedFlow&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedFlow);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const mir::MirRevisionId& builtRevision() const noexcept;
  ZC_NODISCARD const OwnershipEventOverlayRevision& overlayRevision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const FlowFunction> functions() const noexcept;

private:
  struct Impl;
  explicit VerifiedFlow(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class FlowVerifier;
};

/// \brief Derives reachable CFG and event cutpoints for the supported MIR flow subset.
class FlowBuilder final {
public:
  ZC_NODISCARD static ir::IrOperationResult<FlowCandidate> build(
      const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay);
};

/// \brief Independently reconstructs and validates the ownership flow inventory.
class FlowVerifier final {
public:
  ZC_NODISCARD static ir::IrOperationResult<VerifiedFlow> verify(
      FlowCandidate&& candidate, const mir::VerifiedBuiltMir& builtMir,
      const VerifiedOwnershipEventOverlay& overlay);
};

}  // namespace zomlang::compiler::ownership::facts
