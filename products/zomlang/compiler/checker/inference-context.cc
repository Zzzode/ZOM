// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/checker/inference-context.h"

namespace zomlang::compiler::checker::inference {
namespace {

enum class ContextState : uint8_t { Open, Failed, Closed };
enum class BindingKind : uint8_t { Unbound, Known, Recovery };

bool lessOrdinal(InferenceVariableOrdinal left, InferenceVariableOrdinal right) noexcept {
  return left.schemaPreorder < right.schemaPreorder ||
         (left.schemaPreorder == right.schemaPreorder && left.localOrdinal < right.localOrdinal);
}

InferenceContextRejected rejected(InferenceContextInvariant invariant,
                                  uint32_t ordinal = 0) noexcept {
  return InferenceContextRejected{invariant, ordinal};
}

struct VariableBinding final {
  BindingKind kind = BindingKind::Unbound;
  identity::SemanticTypeId known;
  checked::TypeErrorId recovery;
};

struct VariableRecord final {
  uint32_t parent;
  InferenceVariableOrdinal ordinal;
  VariableBinding binding;
};

struct EqualityConstraint final {
  EqualityConstraint(uint32_t ordinal, InferenceType&& left, InferenceType&& right)
      : ordinal(ordinal), left(zc::mv(left)), right(zc::mv(right)) {}
  EqualityConstraint(EqualityConstraint&&) noexcept = default;
  EqualityConstraint& operator=(EqualityConstraint&&) noexcept = default;
  ZC_DISALLOW_COPY(EqualityConstraint);

  uint32_t ordinal;
  InferenceType left;
  InferenceType right;
};

using RootResult = zc::OneOf<uint32_t, InferenceContextRejected>;

}  // namespace

InferenceContextToken::IssueResult InferenceContextToken::issue(
    InferenceRecoveryContext& recoveryContext) {
  const auto context = recoveryContext.semanticContext();
  const auto issuer = recoveryContext.issuer();
  if (!context.isValid() || !issuer.belongsTo(context)) {
    return rejected(InferenceContextInvariant::InvalidToken);
  }
  if (!recoveryContext.claimInferenceContext()) {
    return rejected(InferenceContextInvariant::TokenConsumed);
  }
  auto token = InferenceContextToken(context, issuer);
  token.recoveryContext = recoveryContext;
  return token;
}

InferenceContextToken::InferenceContextToken(identity::SemanticContextBrand context,
                                             identity::RegistryBrand issuer) noexcept
    : context(context), issuer(issuer), available(true) {}

InferenceContextToken::InferenceContextToken(InferenceContextToken&& other) noexcept
    : context(other.context),
      issuer(other.issuer),
      recoveryContext(zc::mv(other.recoveryContext)),
      available(other.available) {
  other.context = identity::SemanticContextBrand();
  other.issuer = identity::RegistryBrand();
  other.available = false;
}

InferenceContextToken& InferenceContextToken::operator=(InferenceContextToken&& other) noexcept {
  if (this != &other) {
    context = other.context;
    issuer = other.issuer;
    recoveryContext = zc::mv(other.recoveryContext);
    available = other.available;
    other.context = identity::SemanticContextBrand();
    other.issuer = identity::RegistryBrand();
    other.available = false;
  }
  return *this;
}

bool InferenceContextToken::isValid() const noexcept {
  return available && context.isValid() && issuer.belongsTo(context) && recoveryContext != zc::none;
}

InferenceType InferenceType::known(identity::SemanticTypeId type) noexcept {
  return InferenceType(KnownInferenceType{type});
}

InferenceType InferenceType::variable(TypeVarId variable) noexcept {
  return InferenceType(VariableInferenceType{variable});
}

InferenceType InferenceType::recovery(checked::TypeErrorId error) noexcept {
  return InferenceType(RecoveryInferenceType{error});
}

struct InferenceContext::Impl final {
  Impl(identity::SemanticContextBrand context, identity::RegistryBrand issuer,
       InferenceRecoveryContext& recoveryContext, InferenceContextBudget budget) noexcept
      : context(context), issuer(issuer), recoveryContext(recoveryContext), budget(budget) {}

  InferenceContextRejected fail(InferenceContextInvariant invariant,
                                uint32_t ordinal = 0) noexcept {
    const auto failure = rejected(invariant, ordinal);
    if (state == ContextState::Open) {
      state = ContextState::Failed;
      latchedFailure = failure;
    }
    return failure;
  }

  InferenceContextRejected currentFailure() const noexcept {
    if (state == ContextState::Failed) { return latchedFailure; }
    return rejected(InferenceContextInvariant::ContextClosed);
  }

  bool complete() noexcept {
    if (!recoveryContext.completeInferenceContext()) { return false; }
    state = ContextState::Closed;
    return true;
  }

  bool typeIsValid(const InferenceType& type, InferenceContextRejected& failure) const noexcept {
    const auto& value = type.variant();
    if (value.is<KnownInferenceType>()) {
      const auto known = value.get<KnownInferenceType>().type;
      if (!known.belongsTo(context)) {
        failure = rejected(InferenceContextInvariant::ForeignSemanticType);
        return false;
      }
      return true;
    }
    if (value.is<RecoveryInferenceType>()) {
      const auto recovery = value.get<RecoveryInferenceType>().error;
      if (!recovery.belongsTo(context)) {
        failure = rejected(InferenceContextInvariant::ForeignRecovery);
        return false;
      }
      return true;
    }
    const auto variable = value.get<VariableInferenceType>().variable;
    if (!variable.belongsTo(issuer)) {
      failure = rejected(InferenceContextInvariant::ForeignVariable);
      return false;
    }
    const auto slot = TypeVarTag::slot(variable);
    if (slot >= variables.size()) {
      failure = rejected(InferenceContextInvariant::UnknownVariable, slot);
      return false;
    }
    return true;
  }

  RootResult findRoot(uint32_t slot) const noexcept {
    if (slot >= variables.size()) {
      return rejected(InferenceContextInvariant::UnknownVariable, slot);
    }
    uint32_t current = slot;
    for (size_t hops = 0; hops <= variables.size(); ++hops) {
      const auto parent = variables[current].parent;
      if (parent == current) { return current; }
      if (parent >= current || parent >= variables.size()) {
        return rejected(InferenceContextInvariant::SolverStateInvalid, current);
      }
      current = parent;
    }
    return rejected(InferenceContextInvariant::OccursCheckFailed, slot);
  }

  zc::Maybe<InferenceContextRejected> mergeBindings(uint32_t representative, uint32_t absorbed,
                                                    uint32_t ordinal) noexcept {
    auto& selected = variables[representative].binding;
    const auto& other = variables[absorbed].binding;
    if (selected.kind == BindingKind::Unbound) {
      selected = other;
      return zc::none;
    }
    if (other.kind == BindingKind::Unbound) { return zc::none; }
    if (selected.kind == BindingKind::Recovery || other.kind == BindingKind::Recovery) {
      if (selected.kind == BindingKind::Recovery && other.kind == BindingKind::Recovery &&
          selected.recovery != other.recovery) {
        return rejected(InferenceContextInvariant::SolverStateInvalid, ordinal);
      }
      if (selected.kind != BindingKind::Recovery) { selected = other; }
      return zc::none;
    }
    if (selected.known != other.known) {
      return rejected(InferenceContextInvariant::SolverStateInvalid, ordinal);
    }
    return zc::none;
  }

  zc::Maybe<InferenceContextRejected> bind(uint32_t root, const InferenceType& value,
                                           uint32_t ordinal) noexcept {
    auto& binding = variables[root].binding;
    const auto& variant = value.variant();
    if (variant.is<KnownInferenceType>()) {
      const auto known = variant.get<KnownInferenceType>().type;
      if (binding.kind == BindingKind::Unbound) {
        binding.kind = BindingKind::Known;
        binding.known = known;
        return zc::none;
      }
      if (binding.kind == BindingKind::Recovery) { return zc::none; }
      if (binding.known != known) {
        return rejected(InferenceContextInvariant::SolverStateInvalid, ordinal);
      }
      return zc::none;
    }
    ZC_IREQUIRE(variant.is<RecoveryInferenceType>(), "variable binding requires a closed value");
    const auto recovery = variant.get<RecoveryInferenceType>().error;
    if (binding.kind == BindingKind::Unbound || binding.kind == BindingKind::Known) {
      binding.kind = BindingKind::Recovery;
      binding.recovery = recovery;
      return zc::none;
    }
    if (binding.recovery != recovery) {
      return rejected(InferenceContextInvariant::SolverStateInvalid, ordinal);
    }
    return zc::none;
  }

  zc::Maybe<InferenceContextRejected> unify(const EqualityConstraint& constraint) noexcept {
    const auto& left = constraint.left.variant();
    const auto& right = constraint.right.variant();
    if (left.is<VariableInferenceType>() && right.is<VariableInferenceType>()) {
      const auto leftSlot = TypeVarTag::slot(left.get<VariableInferenceType>().variable);
      const auto rightSlot = TypeVarTag::slot(right.get<VariableInferenceType>().variable);
      auto leftRootResult = findRoot(leftSlot);
      auto rightRootResult = findRoot(rightSlot);
      if (leftRootResult.is<InferenceContextRejected>()) {
        return leftRootResult.get<InferenceContextRejected>();
      }
      if (rightRootResult.is<InferenceContextRejected>()) {
        return rightRootResult.get<InferenceContextRejected>();
      }
      const auto leftRoot = leftRootResult.get<uint32_t>();
      const auto rightRoot = rightRootResult.get<uint32_t>();
      if (leftRoot == rightRoot) {
        // The occurs check succeeds: equality with the same variable adds no edge.
        return zc::none;
      }
      const auto representative = leftRoot < rightRoot ? leftRoot : rightRoot;
      const auto absorbed = leftRoot < rightRoot ? rightRoot : leftRoot;
      auto merge = mergeBindings(representative, absorbed, constraint.ordinal);
      if (merge != zc::none) { return zc::mv(merge); }
      variables[absorbed].parent = representative;
      return zc::none;
    }

    if (left.is<VariableInferenceType>() || right.is<VariableInferenceType>()) {
      const auto& variableSide = left.is<VariableInferenceType>() ? left : right;
      const auto variable = variableSide.get<VariableInferenceType>().variable;
      auto rootResult = findRoot(TypeVarTag::slot(variable));
      if (rootResult.is<InferenceContextRejected>()) {
        return rootResult.get<InferenceContextRejected>();
      }
      return bind(rootResult.get<uint32_t>(),
                  left.is<VariableInferenceType>() ? constraint.right : constraint.left,
                  constraint.ordinal);
    }

    if (left.is<RecoveryInferenceType>() || right.is<RecoveryInferenceType>()) {
      if (left.is<RecoveryInferenceType>() && right.is<RecoveryInferenceType>() &&
          left.get<RecoveryInferenceType>().error != right.get<RecoveryInferenceType>().error) {
        return rejected(InferenceContextInvariant::SolverStateInvalid, constraint.ordinal);
      }
      return zc::none;
    }

    if (left.get<KnownInferenceType>().type != right.get<KnownInferenceType>().type) {
      return rejected(InferenceContextInvariant::SolverStateInvalid, constraint.ordinal);
    }
    return zc::none;
  }

  identity::SemanticContextBrand context;
  identity::RegistryBrand issuer;
  InferenceRecoveryContext& recoveryContext;
  InferenceContextBudget budget;
  ContextState state = ContextState::Open;
  InferenceContextRejected latchedFailure{InferenceContextInvariant::SolverStateInvalid, 0};
  zc::Vector<VariableRecord> variables;
  zc::Vector<EqualityConstraint> constraints;
  size_t workCursor = 0;
};

InferenceContextCreationResult InferenceContext::create(InferenceContextToken&& token,
                                                        InferenceContextBudget budget) {
  if (!token.available) { return rejected(InferenceContextInvariant::TokenConsumed); }
  if (!token.isValid()) {
    token.available = false;
    return rejected(InferenceContextInvariant::InvalidToken);
  }
  const auto context = token.context;
  const auto issuer = token.issuer;
  ZC_IREQUIRE(token.recoveryContext != zc::none,
              "valid inference token must retain its recovery context");
  token.context = identity::SemanticContextBrand();
  token.issuer = identity::RegistryBrand();
  token.available = false;
  ZC_IF_SOME(recoveryContext, token.recoveryContext) {
    token.recoveryContext = zc::none;
    InferenceContext result(zc::heap<Impl>(context, issuer, recoveryContext, budget));
    return zc::heap<InferenceContext>(zc::mv(result));
  }
  return rejected(InferenceContextInvariant::InvalidToken);
}

InferenceContext::InferenceContext(zc::Own<Impl>&& contextImpl) noexcept
    : impl(zc::mv(contextImpl)) {}

InferenceContext::~InferenceContext() noexcept(false) {
  if (impl.get() != nullptr) {
    ZC_IREQUIRE(impl->state == ContextState::Closed, "InferenceContext destroyed before finish()");
  }
}

InferenceContext::InferenceContext(InferenceContext&& other) noexcept : impl(zc::mv(other.impl)) {}

InferenceContext& InferenceContext::operator=(InferenceContext&& other) noexcept {
  if (this != &other) {
    ZC_IREQUIRE(impl.get() == nullptr || impl->state == ContextState::Closed,
                "InferenceContext overwritten before finish()");
    impl = zc::mv(other.impl);
  }
  return *this;
}

TypeVariableIssueResult InferenceContext::issueVariable(InferenceVariableOrdinal ordinal) {
  if (impl->state != ContextState::Open) { return impl->currentFailure(); }
  if (impl->variables.size() >= impl->budget.variables || impl->variables.size() >= UINT32_MAX) {
    return impl->fail(InferenceContextInvariant::VariableSpaceExhausted,
                      static_cast<uint32_t>(impl->variables.size()));
  }
  if (!impl->variables.empty() && !lessOrdinal(impl->variables.back().ordinal, ordinal)) {
    return impl->fail(InferenceContextInvariant::InvalidVariableOrdinal, ordinal.schemaPreorder);
  }
  const auto slot = static_cast<uint32_t>(impl->variables.size());
  const auto id = TypeVarTag::issue(impl->context, impl->issuer, slot);
  impl->variables.add(VariableRecord{slot, ordinal, VariableBinding{}});
  return id;
}

InferenceConstraintResult InferenceContext::addEquality(uint32_t ordinal, InferenceType&& left,
                                                        InferenceType&& right) {
  if (impl->state != ContextState::Open) { return impl->currentFailure(); }
  if (impl->workCursor != 0) {
    return impl->fail(InferenceContextInvariant::SolverStateInvalid, ordinal);
  }
  if (impl->constraints.size() >= impl->budget.constraints ||
      impl->constraints.size() >= UINT32_MAX) {
    return impl->fail(InferenceContextInvariant::ConstraintSpaceExhausted, ordinal);
  }
  InferenceContextRejected failure{InferenceContextInvariant::SolverStateInvalid, 0};
  if (!impl->typeIsValid(left, failure) || !impl->typeIsValid(right, failure)) {
    return impl->fail(failure.invariant, failure.ordinal);
  }
  for (const auto& constraint : impl->constraints) {
    if (constraint.ordinal == ordinal) {
      return impl->fail(InferenceContextInvariant::DuplicateConstraintOrdinal, ordinal);
    }
  }
  impl->constraints.add(EqualityConstraint(ordinal, zc::mv(left), zc::mv(right)));
  return InferenceConstraintAccepted{};
}

InferenceSolveResult InferenceContext::solve() {
  if (impl->state != ContextState::Open) { return impl->currentFailure(); }
  if (impl->workCursor != 0) {
    return impl->fail(InferenceContextInvariant::SolverStateInvalid,
                      static_cast<uint32_t>(impl->workCursor));
  }
  for (size_t index = 1; index < impl->constraints.size(); ++index) {
    auto current = zc::mv(impl->constraints[index]);
    size_t insertion = index;
    while (insertion > 0 && current.ordinal < impl->constraints[insertion - 1].ordinal) {
      impl->constraints[insertion] = zc::mv(impl->constraints[insertion - 1]);
      --insertion;
    }
    impl->constraints[insertion] = zc::mv(current);
  }
  while (impl->workCursor < impl->constraints.size()) {
    const auto ordinal = impl->constraints[impl->workCursor].ordinal;
    auto failure = impl->unify(impl->constraints[impl->workCursor]);
    if (failure != zc::none) {
      ZC_IF_SOME(value, failure) {
        return impl->fail(value.invariant, value.ordinal == 0 ? ordinal : value.ordinal);
      }
    }
    ++impl->workCursor;
  }
  return InferenceSolveComplete{};
}

InferenceRepresentativeResult InferenceContext::representative(TypeVarId variable) const {
  if (impl->state != ContextState::Open) { return impl->currentFailure(); }
  if (!variable.belongsTo(impl->issuer)) {
    return rejected(InferenceContextInvariant::ForeignVariable);
  }
  auto root = impl->findRoot(TypeVarTag::slot(variable));
  if (root.is<InferenceContextRejected>()) { return root.get<InferenceContextRejected>(); }
  return TypeVarTag::issue(impl->context, impl->issuer, root.get<uint32_t>());
}

InferenceMaterializationResult InferenceContext::materialize(const InferenceType& type) const {
  if (impl->state != ContextState::Open) { return impl->currentFailure(); }
  if (impl->workCursor != impl->constraints.size()) {
    return rejected(InferenceContextInvariant::PendingWork,
                    static_cast<uint32_t>(impl->workCursor));
  }
  InferenceContextRejected failure{InferenceContextInvariant::SolverStateInvalid, 0};
  if (!impl->typeIsValid(type, failure)) { return failure; }
  const auto& value = type.variant();
  if (value.is<KnownInferenceType>()) {
    return MaterializedInferenceType{value.get<KnownInferenceType>().type};
  }
  if (value.is<RecoveryInferenceType>()) {
    return SourceRejectedInferenceType{value.get<RecoveryInferenceType>().error};
  }
  const auto variable = value.get<VariableInferenceType>().variable;
  auto root = impl->findRoot(TypeVarTag::slot(variable));
  if (root.is<InferenceContextRejected>()) { return root.get<InferenceContextRejected>(); }
  const auto& binding = impl->variables[root.get<uint32_t>()].binding;
  if (binding.kind == BindingKind::Known) { return MaterializedInferenceType{binding.known}; }
  if (binding.kind == BindingKind::Recovery) {
    return SourceRejectedInferenceType{binding.recovery};
  }
  return rejected(InferenceContextInvariant::UnresolvedVariable, TypeVarTag::slot(variable));
}

InferenceContextFinishResult InferenceContext::finish() {
  if (impl->state == ContextState::Closed) {
    return rejected(InferenceContextInvariant::ContextClosed);
  }
  if (impl->state == ContextState::Failed) {
    const auto failure = impl->latchedFailure;
    if (!impl->complete()) {
      impl->state = ContextState::Closed;
      return rejected(InferenceContextInvariant::SolverStateInvalid, failure.ordinal);
    }
    return failure;
  }
  if (impl->workCursor != impl->constraints.size()) {
    const auto failure =
        rejected(InferenceContextInvariant::PendingWork, static_cast<uint32_t>(impl->workCursor));
    if (!impl->complete()) {
      impl->state = ContextState::Closed;
      return rejected(InferenceContextInvariant::SolverStateInvalid, failure.ordinal);
    }
    return failure;
  }
  for (size_t index = 0; index < impl->variables.size(); ++index) {
    auto root = impl->findRoot(static_cast<uint32_t>(index));
    if (root.is<InferenceContextRejected>()) {
      const auto failure = root.get<InferenceContextRejected>();
      if (!impl->complete()) {
        impl->state = ContextState::Closed;
        return rejected(InferenceContextInvariant::SolverStateInvalid, failure.ordinal);
      }
      return failure;
    }
    const auto rootSlot = root.get<uint32_t>();
    if (rootSlot > index) {
      if (!impl->complete()) {
        impl->state = ContextState::Closed;
        return rejected(InferenceContextInvariant::SolverStateInvalid,
                        static_cast<uint32_t>(index));
      }
      return rejected(InferenceContextInvariant::SolverStateInvalid, static_cast<uint32_t>(index));
    }
    if (impl->variables[rootSlot].binding.kind == BindingKind::Unbound) {
      if (!impl->complete()) {
        impl->state = ContextState::Closed;
        return rejected(InferenceContextInvariant::SolverStateInvalid,
                        static_cast<uint32_t>(index));
      }
      return rejected(InferenceContextInvariant::UnresolvedVariable, static_cast<uint32_t>(index));
    }
  }
  if (!impl->complete()) {
    impl->state = ContextState::Closed;
    return rejected(InferenceContextInvariant::SolverStateInvalid);
  }
  return InferenceContextClosed{};
}

identity::SemanticContextBrand InferenceContext::semanticContext() const noexcept {
  return impl->context;
}

identity::RegistryBrand InferenceContext::issuer() const noexcept { return impl->issuer; }

}  // namespace zomlang::compiler::checker::inference
