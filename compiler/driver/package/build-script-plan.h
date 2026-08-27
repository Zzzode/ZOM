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
// See the License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "compiler/driver/package/build-script-runtime.h"
#include "compiler/driver/package/manifest-model.h"
#include "compiler/identity/key/build-script-key.h"

namespace zomlang::compiler::driver {
class VerifiedPreparatoryCrateGraph;
}

namespace zomlang::compiler::driver::package {

/// \brief Canonical key of one preparatory build-script plan node.
class BuildScriptPlanNodeKey final {
public:
  ZC_NODISCARD static BuildScriptPlanNodeKey from(
      identity::PreparatoryBuildScriptKey&& preparatory);
  BuildScriptPlanNodeKey(BuildScriptPlanNodeKey&&) noexcept = default;
  BuildScriptPlanNodeKey& operator=(BuildScriptPlanNodeKey&&) noexcept = default;
  ZC_DISALLOW_COPY(BuildScriptPlanNodeKey);

  ZC_NODISCARD BuildScriptPlanNodeKey clone() const;
  ZC_NODISCARD const identity::PreparatoryBuildScriptKey& preparatory() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit BuildScriptPlanNodeKey(identity::PreparatoryBuildScriptKey&& preparatory) noexcept;
  identity::PreparatoryBuildScriptKey preparatoryValue;
};

/// \brief One verified build-script contract and its direct predecessor keys.
class BuildScriptPlanNode final {
public:
  ZC_NODISCARD static zc::Maybe<BuildScriptPlanNode> from(
      BuildScriptPlanNodeKey&& key, CanonicalBuildScriptManifest&& contract,
      zc::Vector<BuildScriptPlanNodeKey>&& predecessors);
  ~BuildScriptPlanNode() noexcept;
  BuildScriptPlanNode(BuildScriptPlanNode&&) noexcept;
  BuildScriptPlanNode& operator=(BuildScriptPlanNode&&) noexcept;
  ZC_DISALLOW_COPY(BuildScriptPlanNode);

  ZC_NODISCARD const BuildScriptPlanNodeKey& key() const noexcept;
  ZC_NODISCARD const CanonicalBuildScriptManifest& contract() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const BuildScriptPlanNodeKey> predecessors() const noexcept;

private:
  struct Impl;
  explicit BuildScriptPlanNode(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Canonically ordered acyclic build plan with a stable execution order.
class VerifiedBuildScriptPlan final {
public:
  ZC_NODISCARD static zc::Maybe<VerifiedBuildScriptPlan> from(
      zc::Vector<BuildScriptPlanNode>&& nodes);
  ~VerifiedBuildScriptPlan() noexcept;
  VerifiedBuildScriptPlan(VerifiedBuildScriptPlan&&) noexcept;
  VerifiedBuildScriptPlan& operator=(VerifiedBuildScriptPlan&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedBuildScriptPlan);

  ZC_NODISCARD zc::ArrayPtr<const BuildScriptPlanNode> nodes() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint32_t> executionOrder() const noexcept;

private:
  struct Impl;
  explicit VerifiedBuildScriptPlan(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Injected node executor used by CompilerSession after predecessors complete.
class BuildScriptPlanExecutor {
public:
  virtual ~BuildScriptPlanExecutor() noexcept(false) = default;
  ZC_NODISCARD virtual BuildScriptExecutionResult execute(
      const BuildScriptPlanNode& node, const VerifiedPreparatoryCrateGraph& crateGraph,
      zc::ArrayPtr<const VerifiedBuildScriptResult> completedResults) = 0;
};

}  // namespace zomlang::compiler::driver::package
