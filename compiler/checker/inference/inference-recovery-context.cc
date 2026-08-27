// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/checker/inference/inference-recovery-context.h"

#include "compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::checker::inference {
namespace {

enum class ContextState : uint8_t { Open, Closed, Invalid };

bool lessBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] != right[index]) { return left[index] < right[index]; }
  }
  return left.size() < right.size();
}

bool lessOrdinal(const checked::CheckerEmitterOrdinal& left,
                 const checked::CheckerEmitterOrdinal& right) noexcept {
  if (left.stageTag != right.stageTag) { return left.stageTag < right.stageTag; }
  if (left.ownerSchemaPreorder != right.ownerSchemaPreorder) {
    return left.ownerSchemaPreorder < right.ownerSchemaPreorder;
  }
  if (left.siteSchemaPreorder != right.siteSchemaPreorder) {
    return left.siteSchemaPreorder < right.siteSchemaPreorder;
  }
  return left.itemOrdinal < right.itemOrdinal;
}

bool sameOrdinal(const checked::CheckerEmitterOrdinal& left,
                 const checked::CheckerEmitterOrdinal& right) noexcept {
  return left.stageTag == right.stageTag && left.ownerSchemaPreorder == right.ownerSchemaPreorder &&
         left.siteSchemaPreorder == right.siteSchemaPreorder &&
         left.itemOrdinal == right.itemOrdinal;
}

void encodeRaw(identity::CanonicalEncoder& encoder, zc::ArrayPtr<const uint8_t> bytes) {
  for (const auto byte : bytes) { encoder.encodeUint8(byte); }
}

void encodeOrdinal(identity::CanonicalEncoder& encoder,
                   const checked::CheckerEmitterOrdinal& ordinal) {
  encoder.encodeUint8(ordinal.stageTag);
  encoder.encodeUint32(ordinal.ownerSchemaPreorder);
  encoder.encodeUint32(ordinal.siteSchemaPreorder);
  encoder.encodeUint32(ordinal.itemOrdinal);
}

zc::Array<uint8_t> encodeSpan(const identity::SourceSpan& span) {
  identity::CanonicalEncoder encoder;
  span.encode(encoder);
  return encoder.finish();
}

InferenceRecoveryRejected makeRejected(InferenceRecoveryInvariant invariant,
                                       identity::ModuleId module,
                                       zc::Maybe<identity::DefId>&& owner,
                                       zc::Maybe<ast::NodeId>&& node,
                                       zc::Maybe<identity::SourceSpan>&& span,
                                       uint32_t traversalOrdinal = 0) {
  zc::Vector<uint32_t> path;
  path.add(static_cast<uint32_t>(invariant));
  zc::Maybe<identity::Sha256Digest> noExpected;
  zc::Maybe<identity::Sha256Digest> noActual;
  signature::CheckerInvariantFact fact{signature::CheckerInvariantKind::InferenceLifecycle,
                                       signature::CheckerInvariantStage::Body,
                                       module,
                                       zc::mv(owner),
                                       zc::mv(node),
                                       zc::mv(span),
                                       zc::mv(path),
                                       zc::mv(noExpected),
                                       zc::mv(noActual),
                                       traversalOrdinal};
  return InferenceRecoveryRejected{invariant, signature::CheckerVerificationFailure(zc::mv(fact))};
}

InferenceRecoveryRejected makeRejected(InferenceRecoveryInvariant invariant,
                                       identity::ModuleId module, uint32_t traversalOrdinal = 0) {
  zc::Maybe<identity::DefId> noOwner;
  zc::Maybe<ast::NodeId> noNode;
  zc::Maybe<identity::SourceSpan> noSpan;
  return makeRejected(invariant, module, zc::mv(noOwner), zc::mv(noNode), zc::mv(noSpan),
                      traversalOrdinal);
}

struct ValidatedOwner final {
  identity::ModuleId module;
  zc::Maybe<identity::DefId> diagnosticOwner;
  zc::Array<uint8_t> canonicalRecord;
};

zc::OneOf<ValidatedOwner, InferenceRecoveryRejected> validateOwner(
    const CheckerIdentityAuthority& identities, const InferenceOwner& owner) {
  if (!identities.semanticContext().isValid()) {
    return makeRejected(InferenceRecoveryInvariant::InvalidContext, identity::ModuleId());
  }

  identity::CanonicalEncoder encoder;
  identity::ModuleId module;
  zc::Maybe<identity::DefId> diagnosticOwner;
  const auto& variant = owner.variant();
  if (variant.is<SignatureGroupInferenceOwner>()) {
    const auto& group = variant.get<SignatureGroupInferenceOwner>();
    auto moduleEntry = identities.module(group.module);
    if (moduleEntry == zc::none || group.members.size() == 0) {
      return makeRejected(InferenceRecoveryInvariant::InvalidOwner, group.module);
    }
    module = group.module;
    encoder.encodeUint8(0x01);
    ZC_IF_SOME(moduleKey, moduleEntry) { moduleKey.key().encode(encoder); }
    encoder.encodeSequenceSize(group.members.size());
    zc::Array<uint8_t> previous;
    for (size_t index = 0; index < group.members.size(); ++index) {
      const auto definition = group.members[index];
      auto entry = identities.definition(definition);
      if (entry == zc::none) {
        return makeRejected(InferenceRecoveryInvariant::InvalidOwner, module,
                            static_cast<uint32_t>(index));
      }
      ZC_IF_SOME(definition, entry) {
        ZC_IF_SOME(moduleKey, moduleEntry) {
          if (definition.record().module().encode().asPtr() != moduleKey.key().encode().asPtr()) {
            return makeRejected(InferenceRecoveryInvariant::InvalidOwner, module,
                                static_cast<uint32_t>(index));
          }
        }
        auto encoded = definition.key().encode();
        if (index != 0 && !lessBytes(previous.asPtr(), encoded.asPtr())) {
          return makeRejected(InferenceRecoveryInvariant::InvalidOwner, module,
                              static_cast<uint32_t>(index));
        }
        previous = zc::mv(encoded);
        definition.key().encode(encoder);
      }
    }
  } else {
    identity::DefId definition;
    uint8_t tag = 0;
    if (variant.is<CallableBodyInferenceOwner>()) {
      definition = variant.get<CallableBodyInferenceOwner>().callable;
      tag = 0x02;
    } else {
      definition = variant.get<InitializerInferenceOwner>().definition;
      tag = 0x03;
    }
    auto entry = identities.definition(definition);
    if (entry == zc::none) {
      return makeRejected(InferenceRecoveryInvariant::InvalidOwner, identity::ModuleId());
    }
    ZC_IF_SOME(value, entry) {
      for (const auto& candidate : identities.modules()) {
        ZC_IF_SOME(moduleEntry, identities.module(candidate.module())) {
          if (moduleEntry.key().encode().asPtr() == value.record().module().encode().asPtr()) {
            module = candidate.module();
          }
        }
      }
      if (module == identity::ModuleId()) {
        return makeRejected(InferenceRecoveryInvariant::InvalidOwner, module);
      }
      diagnosticOwner = definition;
      encoder.encodeUint8(tag);
      value.key().encode(encoder);
    }
  }
  return ValidatedOwner{module, zc::mv(diagnosticOwner), encoder.finish()};
}

}  // namespace

InferenceOwner InferenceOwner::signatureGroup(identity::ModuleId module,
                                              zc::Vector<identity::DefId>&& members) {
  return InferenceOwner(SignatureGroupInferenceOwner{module, zc::mv(members)});
}

InferenceOwner InferenceOwner::callableBody(identity::DefId callable) noexcept {
  return InferenceOwner(CallableBodyInferenceOwner{callable});
}

InferenceOwner InferenceOwner::initializer(identity::DefId definition) noexcept {
  return InferenceOwner(InitializerInferenceOwner{definition});
}

struct InferenceRecoveryContext::Impl final {
  struct ErrorRecord final {
    checked::TypeErrorId id;
    checked::CheckerEmitterOrdinal ordinal;
    ast::NodeId node;
    identity::SourceSpan span;
    RecoveryClass recoveryClass;
  };

  struct JoinRecord final {
    ast::NodeId parentNode;
    uint32_t syntaxKind;
    uint32_t schemaPreorder;
    identity::SourceSpan span;
    zc::Vector<checked::TypeErrorId> inputs;
    checked::TypeErrorId selected;
  };

  Impl(identity::SemanticContextBrand semanticContext, identity::RegistryBrand issuer,
       identity::ModuleId module, zc::Maybe<identity::DefId>&& diagnosticOwner,
       identity::ModuleKey&& moduleKey, identity::SourceFileKey&& source,
       zc::Array<uint8_t>&& ownerRecord, InferenceRecoveryIssueBudget budget) noexcept
      : semanticContext(semanticContext),
        issuer(issuer),
        module(module),
        diagnosticOwner(zc::mv(diagnosticOwner)),
        moduleKey(zc::mv(moduleKey)),
        source(zc::mv(source)),
        ownerRecord(zc::mv(ownerRecord)),
        budget(budget) {}

  InferenceRecoveryRejected reject(InferenceRecoveryInvariant invariant,
                                   uint32_t traversalOrdinal = 0) {
    if (state == ContextState::Open) { state = ContextState::Invalid; }
    zc::Maybe<identity::DefId> ownerValue = diagnosticOwner;
    zc::Maybe<ast::NodeId> noNode;
    zc::Maybe<identity::SourceSpan> noSpan;
    return makeRejected(invariant, module, zc::mv(ownerValue), zc::mv(noNode), zc::mv(noSpan),
                        traversalOrdinal);
  }

  InferenceRecoveryRejected reject(InferenceRecoveryInvariant invariant, ast::NodeId node,
                                   const identity::SourceSpan& span,
                                   uint32_t traversalOrdinal = 0) {
    if (state == ContextState::Open) { state = ContextState::Invalid; }
    zc::Maybe<identity::DefId> ownerValue = diagnosticOwner;
    return makeRejected(invariant, module, zc::mv(ownerValue), zc::Maybe<ast::NodeId>(node),
                        zc::Maybe<identity::SourceSpan>(span.clone()), traversalOrdinal);
  }

  ZC_NODISCARD bool contains(checked::TypeErrorId id) const noexcept {
    if (!id.belongsTo(issuer)) { return false; }
    for (const auto& error : errors) {
      if (error.id == id) { return true; }
    }
    return false;
  }

  identity::SemanticContextBrand semanticContext;
  identity::RegistryBrand issuer;
  identity::ModuleId module;
  zc::Maybe<identity::DefId> diagnosticOwner;
  identity::ModuleKey moduleKey;
  identity::SourceFileKey source;
  zc::Array<uint8_t> ownerRecord;
  InferenceRecoveryIssueBudget budget;
  ContextState state = ContextState::Open;
  zc::Vector<ErrorRecord> errors;
  zc::Vector<JoinRecord> joins;
  bool inferenceContextClaimed = false;
  bool inferenceContextCompleted = false;
};

InferenceRecoveryCreationResult InferenceRecoveryContext::create(
    const CheckerIdentityAuthority& identities, const identity::RegistryBrandIssuer& registryBrands,
    const identity::SourceFileKey& source, InferenceOwner&& owner,
    InferenceRecoveryIssueBudget budget) {
  auto validated = validateOwner(identities, owner);
  if (validated.is<InferenceRecoveryRejected>()) {
    return zc::mv(validated).get<InferenceRecoveryRejected>();
  }
  auto ownerValue = zc::mv(validated).get<ValidatedOwner>();
  auto issued = registryBrands.issue();
  if (issued == zc::none) {
    return makeRejected(InferenceRecoveryInvariant::RegistryIssueFailed, ownerValue.module);
  }
  ZC_IF_SOME(issuer, issued) {
    if (!issuer.belongsTo(identities.semanticContext())) {
      return makeRejected(InferenceRecoveryInvariant::InvalidContext, ownerValue.module);
    }
    auto moduleKey = identities.module(ownerValue.module);
    if (moduleKey == zc::none) {
      return makeRejected(InferenceRecoveryInvariant::InvalidOwner, ownerValue.module);
    }
    ZC_IF_SOME(key, moduleKey) {
      if (identities.sourceFile(source) == zc::none || !source.belongsTo(key.key().crate())) {
        return makeRejected(InferenceRecoveryInvariant::InvalidOwner, ownerValue.module);
      }
      InferenceRecoveryContext context(
          zc::heap<Impl>(identities.semanticContext(), issuer, ownerValue.module,
                         zc::mv(ownerValue.diagnosticOwner), key.key().clone(), source.clone(),
                         zc::mv(ownerValue.canonicalRecord), budget));
      return zc::heap<InferenceRecoveryContext>(zc::mv(context));
    }
  }
  return makeRejected(InferenceRecoveryInvariant::InvalidContext, ownerValue.module);
}

InferenceRecoveryContext::InferenceRecoveryContext(zc::Own<Impl>&& contextImpl) noexcept
    : impl(zc::mv(contextImpl)) {}
InferenceRecoveryContext::~InferenceRecoveryContext() noexcept(false) {
  if (impl.get() != nullptr) {
    ZC_IREQUIRE(impl->state != ContextState::Open,
                "InferenceRecoveryContext destroyed before finish()");
  }
}
InferenceRecoveryContext::InferenceRecoveryContext(InferenceRecoveryContext&& other) noexcept
    : impl(zc::mv(other.impl)) {}
InferenceRecoveryContext& InferenceRecoveryContext::operator=(
    InferenceRecoveryContext&& other) noexcept {
  if (this != &other) {
    ZC_IREQUIRE(impl.get() == nullptr || impl->state != ContextState::Open,
                "InferenceRecoveryContext overwritten before finish()");
    impl = zc::mv(other.impl);
  }
  return *this;
}

TypeErrorIssueResult InferenceRecoveryContext::issueRoot(
    const checked::CheckerEmitterOrdinal& rootFailureOrdinal, ast::NodeId rootNode,
    const identity::SourceSpan& rootSpan, RecoveryClass recoveryClass) {
  if (impl->state != ContextState::Open) {
    return impl->reject(InferenceRecoveryInvariant::ContextClosed);
  }
  const auto classTag = static_cast<uint8_t>(recoveryClass);
  if (!rootNode || !rootSpan.belongsTo(impl->source) || rootFailureOrdinal.stageTag < 0x01 ||
      rootFailureOrdinal.stageTag > 0x05 || classTag < 0x01 || classTag > 0x06) {
    return impl->reject(InferenceRecoveryInvariant::InvalidRoot, rootNode, rootSpan,
                        rootFailureOrdinal.siteSchemaPreorder);
  }
  if (impl->errors.size() >= impl->budget.errorIds || impl->errors.size() >= UINT32_MAX) {
    return impl->reject(InferenceRecoveryInvariant::ErrorIdSpaceExhausted,
                        rootFailureOrdinal.siteSchemaPreorder);
  }
  if (impl->errors.size() != 0) {
    const auto& previous = impl->errors.back().ordinal;
    if (sameOrdinal(previous, rootFailureOrdinal)) {
      return impl->reject(InferenceRecoveryInvariant::DuplicateRootOrdinal, rootNode, rootSpan,
                          rootFailureOrdinal.siteSchemaPreorder);
    }
    if (!lessOrdinal(previous, rootFailureOrdinal)) {
      return impl->reject(InferenceRecoveryInvariant::InvalidRoot, rootNode, rootSpan,
                          rootFailureOrdinal.siteSchemaPreorder);
    }
  }
  const auto slot = static_cast<uint32_t>(impl->errors.size());
  const auto id = checked::TypeErrorTag::issue(impl->semanticContext, impl->issuer, slot);
  impl->errors.add(
      Impl::ErrorRecord{id, rootFailureOrdinal, rootNode, rootSpan.clone(), recoveryClass});
  return id;
}

TypeErrorIssueResult InferenceRecoveryContext::reuse(checked::TypeErrorId recovery) {
  if (impl->state != ContextState::Open) {
    return impl->reject(InferenceRecoveryInvariant::ContextClosed);
  }
  if (!recovery.belongsTo(impl->issuer)) {
    return impl->reject(InferenceRecoveryInvariant::ForeignRecovery);
  }
  if (!impl->contains(recovery)) {
    return impl->reject(InferenceRecoveryInvariant::UnknownRecovery);
  }
  return recovery;
}

TypeErrorIssueResult InferenceRecoveryContext::join(
    ast::NodeId parentNode, const checked::CheckedNodeKey& parent,
    zc::ArrayPtr<const checked::TypeErrorId> inputs) {
  if (impl->state != ContextState::Open) {
    return impl->reject(InferenceRecoveryInvariant::ContextClosed);
  }
  if (!parentNode || parent.syntaxKind == 0 || parent.schemaPreorder == UINT32_MAX ||
      !parent.sourceSpan.belongsTo(impl->source) || inputs.size() < 2) {
    return impl->reject(InferenceRecoveryInvariant::InvalidJoin, parentNode, parent.sourceSpan,
                        parent.schemaPreorder);
  }
  for (const auto& join : impl->joins) {
    if (join.parentNode == parentNode) {
      return impl->reject(InferenceRecoveryInvariant::DuplicateJoinParent, parentNode,
                          parent.sourceSpan, parent.schemaPreorder);
    }
  }
  zc::Vector<checked::TypeErrorId> ordered(inputs.size());
  for (const auto input : inputs) {
    if (!input.belongsTo(impl->issuer)) {
      return impl->reject(InferenceRecoveryInvariant::ForeignRecovery, parentNode,
                          parent.sourceSpan, parent.schemaPreorder);
    }
    if (!impl->contains(input)) {
      return impl->reject(InferenceRecoveryInvariant::UnknownRecovery, parentNode,
                          parent.sourceSpan, parent.schemaPreorder);
    }
    ordered.add(input);
  }
  for (size_t index = 1; index < ordered.size(); ++index) {
    const auto current = ordered[index];
    size_t insertion = index;
    while (insertion > 0 &&
           lessOrdinal(impl->errors[checked::TypeErrorTag::slot(current)].ordinal,
                       impl->errors[checked::TypeErrorTag::slot(ordered[insertion - 1])].ordinal)) {
      ordered[insertion] = ordered[insertion - 1];
      --insertion;
    }
    ordered[insertion] = current;
  }
  for (size_t index = 1; index < ordered.size(); ++index) {
    if (ordered[index - 1] == ordered[index]) {
      return impl->reject(InferenceRecoveryInvariant::InvalidJoin, parentNode, parent.sourceSpan,
                          parent.schemaPreorder);
    }
  }
  const auto selected = ordered[0];
  impl->joins.add(Impl::JoinRecord{parentNode, parent.syntaxKind, parent.schemaPreorder,
                                   parent.sourceSpan.clone(), zc::mv(ordered), selected});
  return selected;
}

InferenceRecoveryFinishResult InferenceRecoveryContext::finish() {
  if (impl->state != ContextState::Open) {
    return impl->reject(InferenceRecoveryInvariant::ContextClosed);
  }
  if (impl->inferenceContextClaimed && !impl->inferenceContextCompleted) {
    return impl->reject(InferenceRecoveryInvariant::UnclosedInferenceContext);
  }
  impl->state = ContextState::Closed;
  if (impl->errors.size() == 0) { return InferenceRecoverySolved{}; }

  for (size_t index = 1; index < impl->joins.size(); ++index) {
    auto current = zc::mv(impl->joins[index]);
    auto currentSpan = encodeSpan(current.span);
    size_t insertion = index;
    while (insertion > 0) {
      const auto& previous = impl->joins[insertion - 1];
      auto previousSpan = encodeSpan(previous.span);
      bool currentLess = current.syntaxKind < previous.syntaxKind ||
                         (current.syntaxKind == previous.syntaxKind &&
                          (current.schemaPreorder < previous.schemaPreorder ||
                           (current.schemaPreorder == previous.schemaPreorder &&
                            lessBytes(currentSpan.asPtr(), previousSpan.asPtr()))));
      const bool sameParentKey = current.syntaxKind == previous.syntaxKind &&
                                 current.schemaPreorder == previous.schemaPreorder &&
                                 !lessBytes(currentSpan.asPtr(), previousSpan.asPtr()) &&
                                 !lessBytes(previousSpan.asPtr(), currentSpan.asPtr());
      if (sameParentKey) {
        const size_t shared = current.inputs.size() < previous.inputs.size()
                                  ? current.inputs.size()
                                  : previous.inputs.size();
        for (size_t inputIndex = 0; inputIndex < shared; ++inputIndex) {
          const auto currentSlot = checked::TypeErrorTag::slot(current.inputs[inputIndex]);
          const auto previousSlot = checked::TypeErrorTag::slot(previous.inputs[inputIndex]);
          if (currentSlot != previousSlot) {
            currentLess = currentSlot < previousSlot;
            break;
          }
        }
        if (!currentLess && current.inputs.size() != previous.inputs.size()) {
          currentLess = current.inputs.size() < previous.inputs.size();
        }
        if (!currentLess && current.inputs.size() == previous.inputs.size()) {
          currentLess = checked::TypeErrorTag::slot(current.selected) <
                        checked::TypeErrorTag::slot(previous.selected);
        }
      }
      if (!currentLess) { break; }
      impl->joins[insertion] = zc::mv(impl->joins[insertion - 1]);
      --insertion;
    }
    impl->joins[insertion] = zc::mv(current);
  }

  identity::CanonicalEncoder encoder;
  encodeRaw(encoder, impl->ownerRecord.asPtr());
  encoder.encodeSequenceSize(impl->errors.size());
  for (size_t index = 0; index < impl->errors.size(); ++index) {
    const auto& error = impl->errors[index];
    if (checked::TypeErrorTag::slot(error.id) != index) {
      return impl->reject(InferenceRecoveryInvariant::CanonicalLedgerRejected,
                          static_cast<uint32_t>(index));
    }
    encoder.encodeUint32(static_cast<uint32_t>(index));
    encodeRaw(encoder, impl->ownerRecord.asPtr());
    encodeOrdinal(encoder, error.ordinal);
    encoder.encodeUint32(error.node.value);
    error.span.encode(encoder);
    encoder.encodeUint8(static_cast<uint8_t>(error.recoveryClass));
  }
  encoder.encodeSequenceSize(impl->joins.size());
  for (const auto& join : impl->joins) {
    encoder.encodeUint32(join.parentNode.value);
    encoder.encodeSequenceSize(join.inputs.size());
    for (const auto input : join.inputs) {
      encoder.encodeUint32(checked::TypeErrorTag::slot(input));
    }
    encoder.encodeUint32(checked::TypeErrorTag::slot(join.selected));
  }
  auto ledger = checked::FrozenRecoveryLedger::from(impl->semanticContext, impl->issuer,
                                                    static_cast<uint32_t>(impl->errors.size()),
                                                    encoder.finish());
  if (ledger == zc::none) {
    return impl->reject(InferenceRecoveryInvariant::CanonicalLedgerRejected);
  }
  ZC_IF_SOME(value, ledger) { return InferenceRecoveryRecovered{zc::mv(value)}; }
  return impl->reject(InferenceRecoveryInvariant::CanonicalLedgerRejected);
}

identity::SemanticContextBrand InferenceRecoveryContext::semanticContext() const noexcept {
  return impl->semanticContext;
}

identity::RegistryBrand InferenceRecoveryContext::issuer() const noexcept { return impl->issuer; }

bool InferenceRecoveryContext::claimInferenceContext() noexcept {
  if (impl->state != ContextState::Open || impl->inferenceContextClaimed) { return false; }
  impl->inferenceContextClaimed = true;
  return true;
}

bool InferenceRecoveryContext::completeInferenceContext() noexcept {
  if (impl->state != ContextState::Open || !impl->inferenceContextClaimed ||
      impl->inferenceContextCompleted) {
    return false;
  }
  impl->inferenceContextCompleted = true;
  return true;
}

}  // namespace zomlang::compiler::checker::inference
