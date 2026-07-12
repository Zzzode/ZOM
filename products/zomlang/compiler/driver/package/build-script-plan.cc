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

#include "zomlang/compiler/driver/package/build-script-plan.h"

namespace zomlang::compiler::driver::package {
namespace {

void sortKeys(zc::Vector<BuildScriptPlanNodeKey>& keys) {
  for (size_t index = 1; index < keys.size(); ++index) {
    auto current = zc::mv(keys[index]);
    const auto currentBytes = current.encode();
    size_t insertion = index;
    while (insertion != 0 && currentBytes.asPtr() < keys[insertion - 1].encode().asPtr()) {
      keys[insertion] = zc::mv(keys[insertion - 1]);
      --insertion;
    }
    keys[insertion] = zc::mv(current);
  }
}

bool uniqueKeys(zc::ArrayPtr<const BuildScriptPlanNodeKey> keys) {
  for (size_t index = 1; index < keys.size(); ++index) {
    if (keys[index - 1].encode().asPtr() == keys[index].encode().asPtr()) { return false; }
  }
  return true;
}

void sortNodes(zc::Vector<BuildScriptPlanNode>& nodes) {
  for (size_t index = 1; index < nodes.size(); ++index) {
    auto current = zc::mv(nodes[index]);
    const auto currentBytes = current.key().encode();
    size_t insertion = index;
    while (insertion != 0 && currentBytes.asPtr() < nodes[insertion - 1].key().encode().asPtr()) {
      nodes[insertion] = zc::mv(nodes[insertion - 1]);
      --insertion;
    }
    nodes[insertion] = zc::mv(current);
  }
}

zc::Maybe<uint32_t> findNode(zc::ArrayPtr<const BuildScriptPlanNode> nodes,
                             const BuildScriptPlanNodeKey& key) {
  const auto bytes = key.encode();
  for (uint32_t index = 0; index < nodes.size(); ++index) {
    if (nodes[index].key().encode().asPtr() == bytes.asPtr()) { return index; }
  }
  return zc::none;
}

}  // namespace

BuildScriptPlanNodeKey::BuildScriptPlanNodeKey(
    identity::PreparatoryBuildScriptKey&& preparatory) noexcept
    : preparatoryValue(zc::mv(preparatory)) {}

BuildScriptPlanNodeKey BuildScriptPlanNodeKey::from(
    identity::PreparatoryBuildScriptKey&& preparatory) {
  return BuildScriptPlanNodeKey(zc::mv(preparatory));
}

BuildScriptPlanNodeKey BuildScriptPlanNodeKey::clone() const {
  return BuildScriptPlanNodeKey(preparatoryValue.clone());
}

const identity::PreparatoryBuildScriptKey& BuildScriptPlanNodeKey::preparatory() const noexcept {
  return preparatoryValue;
}

zc::Array<uint8_t> BuildScriptPlanNodeKey::encode() const { return preparatoryValue.encode(); }

struct BuildScriptPlanNode::Impl final {
  Impl(BuildScriptPlanNodeKey&& key, CanonicalBuildScriptManifest&& contract,
       zc::Vector<BuildScriptPlanNodeKey>&& predecessors) noexcept
      : keyValue(zc::mv(key)),
        contractValue(zc::mv(contract)),
        predecessorValues(zc::mv(predecessors)) {}

  BuildScriptPlanNodeKey keyValue;
  CanonicalBuildScriptManifest contractValue;
  zc::Vector<BuildScriptPlanNodeKey> predecessorValues;
};

BuildScriptPlanNode::BuildScriptPlanNode(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}

zc::Maybe<BuildScriptPlanNode> BuildScriptPlanNode::from(
    BuildScriptPlanNodeKey&& key, CanonicalBuildScriptManifest&& contract,
    zc::Vector<BuildScriptPlanNodeKey>&& predecessors) {
  sortKeys(predecessors);
  if (!uniqueKeys(predecessors)) { return zc::none; }
  return BuildScriptPlanNode(zc::heap<Impl>(zc::mv(key), zc::mv(contract), zc::mv(predecessors)));
}

BuildScriptPlanNode::~BuildScriptPlanNode() noexcept = default;
BuildScriptPlanNode::BuildScriptPlanNode(BuildScriptPlanNode&&) noexcept = default;
BuildScriptPlanNode& BuildScriptPlanNode::operator=(BuildScriptPlanNode&&) noexcept = default;

const BuildScriptPlanNodeKey& BuildScriptPlanNode::key() const noexcept { return impl->keyValue; }

const CanonicalBuildScriptManifest& BuildScriptPlanNode::contract() const noexcept {
  return impl->contractValue;
}

zc::ArrayPtr<const BuildScriptPlanNodeKey> BuildScriptPlanNode::predecessors() const noexcept {
  return impl->predecessorValues;
}

struct VerifiedBuildScriptPlan::Impl final {
  Impl(zc::Vector<BuildScriptPlanNode>&& nodes, zc::Vector<uint32_t>&& executionOrder) noexcept
      : nodeValues(zc::mv(nodes)), executionOrderValues(zc::mv(executionOrder)) {}

  zc::Vector<BuildScriptPlanNode> nodeValues;
  zc::Vector<uint32_t> executionOrderValues;
};

VerifiedBuildScriptPlan::VerifiedBuildScriptPlan(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}

zc::Maybe<VerifiedBuildScriptPlan> VerifiedBuildScriptPlan::from(
    zc::Vector<BuildScriptPlanNode>&& nodes) {
  sortNodes(nodes);
  for (size_t index = 1; index < nodes.size(); ++index) {
    if (nodes[index - 1].key().encode().asPtr() == nodes[index].key().encode().asPtr()) {
      return zc::none;
    }
  }
  for (const auto& node : nodes) {
    for (const auto& predecessor : node.predecessors()) {
      if (findNode(nodes, predecessor) == zc::none) { return zc::none; }
    }
  }

  zc::Vector<uint8_t> completed(nodes.size());
  for (size_t index = 0; index < nodes.size(); ++index) { completed.add(0); }
  zc::Vector<uint32_t> executionOrder(nodes.size());
  while (executionOrder.size() != nodes.size()) {
    bool progressed = false;
    for (uint32_t index = 0; index < nodes.size(); ++index) {
      if (completed[index] != 0) { continue; }
      bool ready = true;
      for (const auto& predecessor : nodes[index].predecessors()) {
        ZC_IF_SOME(predecessorIndex, findNode(nodes, predecessor)) {
          if (completed[predecessorIndex] == 0) {
            ready = false;
            break;
          }
        }
      }
      if (!ready) { continue; }
      completed[index] = 1;
      executionOrder.add(index);
      progressed = true;
      break;
    }
    if (!progressed) { return zc::none; }
  }
  return VerifiedBuildScriptPlan(zc::heap<Impl>(zc::mv(nodes), zc::mv(executionOrder)));
}

VerifiedBuildScriptPlan::~VerifiedBuildScriptPlan() noexcept = default;
VerifiedBuildScriptPlan::VerifiedBuildScriptPlan(VerifiedBuildScriptPlan&&) noexcept = default;
VerifiedBuildScriptPlan& VerifiedBuildScriptPlan::operator=(VerifiedBuildScriptPlan&&) noexcept =
    default;

zc::ArrayPtr<const BuildScriptPlanNode> VerifiedBuildScriptPlan::nodes() const noexcept {
  return impl->nodeValues;
}

zc::ArrayPtr<const uint32_t> VerifiedBuildScriptPlan::executionOrder() const noexcept {
  return impl->executionOrderValues;
}

}  // namespace zomlang::compiler::driver::package
