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

#include "zomlang/compiler/ownership/facts/flow.h"

#include "zomlang/compiler/ir/ir-diagnostic-adapter.h"

namespace zomlang::compiler::ownership::facts {
namespace {

identity::IdentityInvariant invalidIdentity(identity::IdentityAllocationPhase phase,
                                            uint32_t ordinal) {
  zc::Maybe<zc::Array<uint8_t>> noKey;
  zc::Maybe<identity::UnbrandedSourceRange> noRange;
  auto invariant = identity::IdentityInvariant::from(
      identity::IdentityInvariantKind::InvalidHandle, phase, zc::mv(noKey), zc::mv(noRange),
      identity::IdentityApiSite::HandleLookup, ordinal);
  ZC_IF_SOME(value, invariant) { return zc::mv(value); }
  ZC_UNREACHABLE
}

class AuthorityIdentityResolver final : public ir::IrFailureIdentityResolver {
public:
  explicit AuthorityIdentityResolver(const checker::CheckerIdentityAuthority& identities) noexcept
      : identities(identities) {}

  ir::ExpandedIrIdentityResult expand(identity::ModuleId module) const override {
    auto key = identities.module(module);
    if (key == zc::none) {
      return ir::RejectedIrIdentityValue{
          invalidIdentity(identity::IdentityAllocationPhase::Module, 0)};
    }
    ZC_IF_SOME(value, key) {
      auto expanded = ir::ExpandedIrIdentity::from(value.key().encode());
      ZC_IF_SOME(bytes, expanded) { return ir::ExpandedIrIdentityValue{zc::mv(bytes)}; }
    }
    return ir::RejectedIrIdentityValue{
        invalidIdentity(identity::IdentityAllocationPhase::Encoding, 0)};
  }

  ir::ExpandedIrIdentityResult expand(identity::DefId definition) const override {
    auto key = identities.definition(definition);
    if (key == zc::none) {
      return ir::RejectedIrIdentityValue{
          invalidIdentity(identity::IdentityAllocationPhase::Definition, 0)};
    }
    ZC_IF_SOME(value, key) {
      auto expanded = ir::ExpandedIrIdentity::from(value.key().encode());
      ZC_IF_SOME(bytes, expanded) { return ir::ExpandedIrIdentityValue{zc::mv(bytes)}; }
    }
    return ir::RejectedIrIdentityValue{
        invalidIdentity(identity::IdentityAllocationPhase::Encoding, 0)};
  }

  ir::ExpandedIrIdentityResult expand(ir::InstanceId) const override {
    return ir::RejectedIrIdentityValue{
        invalidIdentity(identity::IdentityAllocationPhase::Definition, 0)};
  }

private:
  const checker::CheckerIdentityAuthority& identities;
};

template <typename Result>
ir::IrOperationResult<Result> reject(const mir::VerifiedBuiltMir& builtMir,
                                     const checker::CheckerIdentityAuthority& identities,
                                     ir::IrFailureKind kind, uint32_t ordinal) {
  identity::DefId definition;
  if (builtMir.functions().size() != 0) definition = builtMir.functions()[0].owner;
  AuthorityIdentityResolver resolver(identities);
  auto fallback = ir::IrFailureFallbackContext::from(ir::IrFailurePhase::OwnershipProofValidation,
                                                     ir::IrFailureOwner::definition(definition));
  ZC_IREQUIRE(fallback != zc::none, "Flow failure fallback must be legal");
  zc::Maybe<ir::IrFailureSite> noSite;
  zc::Maybe<identity::SourceSpan> noSpan;
  zc::Vector<uint32_t> noPath;
  auto descriptor = ir::IrFailureDescriptor::decoded(
      ir::IrRejectedBranch::IrInvariantRejected, ir::IrFailurePhase::OwnershipProofValidation, kind,
      ir::IrFailureOwner::definition(definition), zc::mv(noSite), ir::IrFailureDetail::none(),
      zc::mv(noSpan), zc::mv(noPath), ordinal);
  ZC_IF_SOME(fallbackValue, fallback) {
    auto admitted = ir::IrFailureFactory::admit(zc::mv(descriptor), fallbackValue, resolver);
    if (admitted.is<ir::IdentityRejectedIrFailureDescriptor>()) {
      zc::Vector<identity::IdentityInvariant> failures;
      failures.add(zc::mv(admitted).get<ir::IdentityRejectedIrFailureDescriptor>().failure);
      auto sorted = ir::SortedIdentityInvariantFacts::from(zc::mv(failures));
      ZC_IF_SOME(values, sorted) {
        return ir::IrOperationResult<Result>::identityInvariantRejected(zc::mv(values));
      }
      ZC_UNREACHABLE
    }
    zc::Vector<ir::IrFailureFact> failures;
    failures.add(zc::mv(admitted).get<ir::AcceptedIrFailureDescriptor>().fact);
    auto sorted = ir::SortedIrInvariantFailureFacts::from(zc::mv(failures));
    ZC_IF_SOME(values, sorted) {
      return ir::IrOperationResult<Result>::irInvariantRejected(zc::mv(values));
    }
  }
  ZC_UNREACHABLE
}

bool sameEdges(zc::ArrayPtr<const FlowEdge> left, zc::ArrayPtr<const FlowEdge> right) {
  if (left.size() != right.size()) return false;
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].from != right[index].from || left[index].to != right[index].to) {
      return false;
    }
  }
  return true;
}

bool samePoints(zc::ArrayPtr<const OwnershipPoint> left, zc::ArrayPtr<const OwnershipPoint> right) {
  if (left.size() != right.size()) return false;
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index] != right[index]) return false;
  }
  return true;
}

bool sameFunctions(zc::ArrayPtr<const FlowFunction> left, zc::ArrayPtr<const FlowFunction> right) {
  if (left.size() != right.size()) return false;
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].owner != right[index].owner ||
        !samePoints(left[index].points.asPtr(), right[index].points.asPtr()) ||
        !sameEdges(left[index].edges.asPtr(), right[index].edges.asPtr())) {
      return false;
    }
  }
  return true;
}

bool inputsMatch(const mir::VerifiedBuiltMir& builtMir,
                 const VerifiedOwnershipEventOverlay& overlay) {
  return overlay.semanticContext() == builtMir.semanticContext() &&
         overlay.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         overlay.module() == builtMir.module() &&
         overlay.builtRevision().digest() == builtMir.revision().digest();
}

zc::Maybe<const OwnershipFunctionEventOverlay&> overlayFor(
    const VerifiedOwnershipEventOverlay& overlay, identity::DefId owner) {
  zc::Maybe<const OwnershipFunctionEventOverlay&> result;
  for (const auto& function : overlay.functions()) {
    if (function.owner != owner) continue;
    if (result != zc::none) return zc::none;
    result = function;
  }
  return result;
}

zc::Maybe<size_t> blockIndex(const mir::MirFunction& function, mir::MirBlockId id) {
  for (size_t index = 0; index < function.blocks.size(); ++index) {
    if (function.blocks[index].id == id) return index;
  }
  return zc::none;
}

zc::Maybe<MirPoint> blockStart(const mir::MirBasicBlock& block) {
  if (block.statements.size() != 0) return MirPoint::beforeStatement(block.id, 0);
  return MirPoint::beforeTerminator(block.id);
}

bool containsPoint(zc::ArrayPtr<const OwnershipPoint> points, const OwnershipPoint& point) {
  for (const auto& value : points) {
    if (value == point) return true;
  }
  return false;
}

bool appendPoint(FlowFunction& flow, OwnershipPoint point) {
  if (containsPoint(flow.points.asPtr(), point)) return true;
  flow.points.add(zc::mv(point));
  return true;
}

bool appendEdge(FlowFunction& flow, const OwnershipPoint& from, const OwnershipPoint& to) {
  if (from == to) return false;
  for (const auto& edge : flow.edges) {
    if (edge.from == from && edge.to == to) return false;
  }
  flow.edges.add(FlowEdge{from, to});
  return true;
}

bool appendLocation(FlowFunction& flow, const OwnershipFunctionEventOverlay& overlay,
                    MirPoint point, zc::Maybe<OwnershipPoint>& current) {
  OwnershipPoint cfg = OwnershipPoint::cfg(zc::mv(point));
  if (!appendPoint(flow, cfg)) return false;
  if (current != zc::none && ZC_ASSERT_NONNULL(current) != cfg &&
      !appendEdge(flow, ZC_ASSERT_NONNULL(current), cfg)) {
    return false;
  }
  current = cfg;
  for (const auto& slot : overlay.slots) {
    if (slot.key.location.point != cfg.cfgValue().point) continue;
    OwnershipPoint before = OwnershipPoint::beforeEvent(slot.key);
    OwnershipPoint after = OwnershipPoint::afterEvent(slot.key);
    if (!appendPoint(flow, before) || !appendPoint(flow, after) ||
        !appendEdge(flow, ZC_ASSERT_NONNULL(current), before) || !appendEdge(flow, before, after)) {
      return false;
    }
    current = zc::mv(after);
  }
  return true;
}

bool hasAllSlotPoints(const FlowFunction& flow, const OwnershipFunctionEventOverlay& overlay) {
  for (const auto& slot : overlay.slots) {
    if (!containsPoint(flow.points.asPtr(), OwnershipPoint::beforeEvent(slot.key)) ||
        !containsPoint(flow.points.asPtr(), OwnershipPoint::afterEvent(slot.key))) {
      return false;
    }
  }
  return true;
}

zc::Maybe<FlowFunction> deriveFunction(const mir::MirFunction& function,
                                       const OwnershipFunctionEventOverlay& overlay) {
  if (function.blocks.size() == 0) return zc::none;
  FlowFunction flow{function.owner, zc::Vector<OwnershipPoint>(), zc::Vector<FlowEdge>()};
  zc::Maybe<OwnershipPoint> current;
  if (!appendLocation(flow, overlay, MirPoint::entry(), current)) return zc::none;

  zc::Vector<mir::MirBlockId> pending;
  zc::Vector<mir::MirBlockId> visited;
  pending.add(function.blocks[0].id);
  while (pending.size() != 0) {
    const auto blockId = pending[pending.size() - 1];
    pending.removeLast();
    bool alreadyVisited = false;
    for (const auto id : visited) {
      if (id == blockId) alreadyVisited = true;
    }
    if (alreadyVisited) continue;
    auto blockPosition = blockIndex(function, blockId);
    if (blockPosition == zc::none) return zc::none;
    size_t currentBlock = 0;
    ZC_IF_SOME(value, blockPosition) { currentBlock = value; }
    const auto& block = function.blocks[currentBlock];
    auto start = blockStart(block);
    if (start == zc::none) return zc::none;
    OwnershipPoint startPoint = OwnershipPoint::cfg(zc::mv(ZC_ASSERT_NONNULL(start)));
    if (block.id == function.blocks[0].id &&
        !appendEdge(flow, ZC_ASSERT_NONNULL(current), startPoint)) {
      return zc::none;
    }
    current = zc::mv(startPoint);
    for (uint32_t ordinal = 0; ordinal < block.statements.size(); ++ordinal) {
      if (!appendLocation(flow, overlay, MirPoint::beforeStatement(block.id, ordinal), current) ||
          !appendLocation(flow, overlay, MirPoint::afterStatement(block.id, ordinal), current)) {
        return zc::none;
      }
    }
    if (!appendLocation(flow, overlay, MirPoint::beforeTerminator(block.id), current))
      return zc::none;
    if (block.terminator.kind() == mir::MirTerminatorKind::Return) {
      if (!appendLocation(flow, overlay, MirPoint::exit(block.id, MirExitKind::Return), current) ||
          current == zc::none) {
        return zc::none;
      }
      visited.add(block.id);
      continue;
    }
    if (block.terminator.kind() == mir::MirTerminatorKind::Unreachable) {
      if (!appendLocation(flow, overlay, MirPoint::exit(block.id, MirExitKind::Unreachable),
                          current) ||
          current == zc::none) {
        return zc::none;
      }
      visited.add(block.id);
      continue;
    }
    if (block.terminator.kind() != mir::MirTerminatorKind::Call) return zc::none;
    const auto& call = block.terminator.callValue();
    if (call.unwindTarget != zc::none ||
        !appendLocation(flow, overlay, MirPoint::edge(block.id, 0, call.normalTarget), current)) {
      return zc::none;
    }
    auto next = blockIndex(function, call.normalTarget);
    if (next == zc::none || current == zc::none) return zc::none;
    size_t nextBlock = 0;
    ZC_IF_SOME(value, next) { nextBlock = value; }
    auto nextStart = blockStart(function.blocks[nextBlock]);
    if (nextStart == zc::none ||
        !appendEdge(flow, ZC_ASSERT_NONNULL(current),
                    OwnershipPoint::cfg(zc::mv(ZC_ASSERT_NONNULL(nextStart))))) {
      return zc::none;
    }
    pending.add(call.normalTarget);
    visited.add(block.id);
  }
  if (!hasAllSlotPoints(flow, overlay)) return zc::none;
  return flow;
}

zc::Maybe<zc::Vector<FlowFunction>> derive(const mir::VerifiedBuiltMir& builtMir,
                                           const VerifiedOwnershipEventOverlay& overlay) {
  if (builtMir.functions().size() != overlay.functions().size()) return zc::none;
  zc::Vector<FlowFunction> functions;
  for (const auto& function : builtMir.functions()) {
    auto functionOverlay = overlayFor(overlay, function.owner);
    if (functionOverlay == zc::none) return zc::none;
    ZC_IF_SOME(value, functionOverlay) {
      auto flow = deriveFunction(function, value);
      if (flow == zc::none) return zc::none;
      ZC_IF_SOME(derived, flow) { functions.add(zc::mv(derived)); }
    }
  }
  return functions;
}

}  // namespace

FlowCandidate::FlowCandidate(identity::SemanticContextBrand semanticContext,
                             identity::SemanticContextFingerprint&& contextFingerprint,
                             identity::ModuleId module, mir::MirRevisionId builtRevision,
                             OwnershipEventOverlayRevision overlayRevision,
                             zc::Vector<FlowFunction>&& functions) noexcept
    : semanticContext(semanticContext),
      contextFingerprint(zc::mv(contextFingerprint)),
      module(module),
      builtRevision(builtRevision),
      overlayRevision(overlayRevision),
      functions(zc::mv(functions)) {}

struct VerifiedFlow::Impl final {
  explicit Impl(FlowCandidate&& candidate) noexcept : candidate(zc::mv(candidate)) {}
  FlowCandidate candidate;
};

VerifiedFlow::VerifiedFlow(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
VerifiedFlow::~VerifiedFlow() noexcept(false) = default;
VerifiedFlow::VerifiedFlow(VerifiedFlow&&) noexcept = default;
VerifiedFlow& VerifiedFlow::operator=(VerifiedFlow&&) noexcept = default;
identity::SemanticContextBrand VerifiedFlow::semanticContext() const noexcept {
  return impl->candidate.semanticContext;
}
const identity::SemanticContextFingerprint& VerifiedFlow::contextFingerprint() const noexcept {
  return impl->candidate.contextFingerprint;
}
identity::ModuleId VerifiedFlow::module() const noexcept { return impl->candidate.module; }
const mir::MirRevisionId& VerifiedFlow::builtRevision() const noexcept {
  return impl->candidate.builtRevision;
}
const OwnershipEventOverlayRevision& VerifiedFlow::overlayRevision() const noexcept {
  return impl->candidate.overlayRevision;
}
zc::ArrayPtr<const FlowFunction> VerifiedFlow::functions() const noexcept {
  return impl->candidate.functions.asPtr();
}

ir::IrOperationResult<FlowCandidate> FlowBuilder::build(
    const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay) {
  const auto identities = builtMir.retainIdentityAuthority();
  if (!inputsMatch(builtMir, overlay)) {
    return reject<FlowCandidate>(builtMir, identities, ir::IrFailureKind::InputRevisionMismatch, 0);
  }
  auto functions = derive(builtMir, overlay);
  if (functions == zc::none) {
    return reject<FlowCandidate>(builtMir, identities, ir::IrFailureKind::InvalidOwnershipProof, 1);
  }
  ZC_IF_SOME(value, functions) {
    return ir::IrOperationResult<FlowCandidate>::verified(
        FlowCandidate(builtMir.semanticContext(), builtMir.contextFingerprint().clone(),
                      builtMir.module(), builtMir.revision(), overlay.revision(), zc::mv(value)));
  }
  ZC_UNREACHABLE
}

ir::IrOperationResult<VerifiedFlow> FlowVerifier::verify(
    FlowCandidate&& candidate, const mir::VerifiedBuiltMir& builtMir,
    const VerifiedOwnershipEventOverlay& overlay) {
  const auto identities = builtMir.retainIdentityAuthority();
  if (candidate.semanticContext != builtMir.semanticContext() ||
      candidate.contextFingerprint.digest() != builtMir.contextFingerprint().digest() ||
      candidate.module != builtMir.module() ||
      candidate.builtRevision.digest() != builtMir.revision().digest() ||
      candidate.overlayRevision.digest() != overlay.revision().digest() ||
      !inputsMatch(builtMir, overlay)) {
    return reject<VerifiedFlow>(builtMir, identities, ir::IrFailureKind::InputRevisionMismatch, 0);
  }
  auto expected = derive(builtMir, overlay);
  if (expected == zc::none || !sameFunctions(candidate.functions, ZC_ASSERT_NONNULL(expected))) {
    return reject<VerifiedFlow>(builtMir, identities, ir::IrFailureKind::InvalidOwnershipProof, 1);
  }
  return ir::IrOperationResult<VerifiedFlow>::verified(
      VerifiedFlow(zc::heap<VerifiedFlow::Impl>(zc::mv(candidate))));
}

}  // namespace zomlang::compiler::ownership::facts
