// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/internal/binding-definition-fact-validator.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::binder {
namespace {

bool sameSpan(const identity::SourceSpan& left, const identity::SourceSpan& right) {
  return left.source().sameAs(right.source()) && left.byteStart() == right.byteStart() &&
         left.byteEnd() == right.byteEnd();
}

int compareCanonicalBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  const size_t count = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < count; ++index) {
    if (left[index] < right[index]) { return -1; }
    if (left[index] > right[index]) { return 1; }
  }
  if (left.size() < right.size()) { return -1; }
  if (left.size() > right.size()) { return 1; }
  return 0;
}

bool sameDefinitionSite(const DefinitionSite& left, const DefinitionSite& right) {
  const auto& leftValue = left.value();
  const auto& rightValue = right.value();
  if (leftValue.is<DeclarationDefinitionSite>() != rightValue.is<DeclarationDefinitionSite>()) {
    return false;
  }
  if (leftValue.is<DeclarationDefinitionSite>()) {
    return leftValue.get<DeclarationDefinitionSite>().node ==
           rightValue.get<DeclarationDefinitionSite>().node;
  }
  const auto& leftPattern = leftValue.get<PatternBindingSite>();
  const auto& rightPattern = rightValue.get<PatternBindingSite>();
  if (leftPattern.introducer != rightPattern.introducer ||
      leftPattern.patternPath.size() != rightPattern.patternPath.size()) {
    return false;
  }
  for (size_t index = 0; index < leftPattern.patternPath.size(); ++index) {
    if (leftPattern.patternPath[index] != rightPattern.patternPath[index]) { return false; }
  }
  return true;
}

bool sameDefinitionName(const identity::DeclaredDefinitionName& left,
                        const identity::DeclaredDefinitionName& right) {
  identity::CanonicalEncoder leftEncoder;
  identity::CanonicalEncoder rightEncoder;
  left.encode(leftEncoder);
  right.encode(rightEncoder);
  return leftEncoder.finish() == rightEncoder.finish();
}

bool sameMaybeDefinitionName(const zc::Maybe<identity::DeclaredDefinitionName>& left,
                             const zc::Maybe<identity::DeclaredDefinitionName>& right) {
  if ((left != zc::none) != (right != zc::none)) { return false; }
  if (left == zc::none) { return true; }
  return sameDefinitionName(ZC_ASSERT_NONNULL(left), ZC_ASSERT_NONNULL(right));
}

zc::Maybe<ScopeId> candidateScopeAt(const BindingMetadataCandidate& candidate, ast::NodeId node) {
  if (!node || node.value > candidate.nodeScopes.size()) { return zc::none; }
  const auto& fact = candidate.nodeScopes[node.value - 1];
  if (fact.node != node) { return zc::none; }
  return fact.scope;
}

BindingDefinitionFactValidationResult verifyGenericParameters(
    const VerifiedBindingInput& input, const BindingMetadataCandidate& candidate) {
  const auto inventory = input.definitions().genericParameters();
  if (candidate.genericParameters.size() < inventory.size()) {
    return BindingDefinitionFactValidationResult::MissingRequiredResolution;
  }
  if (candidate.genericParameters.size() > inventory.size()) {
    return BindingDefinitionFactValidationResult::InvalidBindingFact;
  }

  zc::Array<uint8_t> previousKey;
  bool hasPrevious = false;
  for (const auto& fact : candidate.genericParameters) {
    zc::Maybe<const FrozenGenericParameterEntry&> match;
    for (const auto& frozen : inventory) {
      if (frozen.parameter == fact.identity) {
        match = frozen;
        break;
      }
    }
    if (match == zc::none) { return BindingDefinitionFactValidationResult::InvalidBindingFact; }
    ZC_IF_SOME(frozen, match) {
      const auto encoded = frozen.key.encode();
      if ((hasPrevious && compareCanonicalBytes(previousKey.asPtr(), encoded.asPtr()) >= 0) ||
          !sameDefinitionSite(fact.site, frozen.site) ||
          !sameDefinitionName(fact.name, frozen.bindingName) ||
          !sameSpan(fact.source, frozen.source) ||
          candidateScopeAt(candidate, frozen.node) != fact.declaringScope) {
        return BindingDefinitionFactValidationResult::InvalidBindingFact;
      }
      previousKey = zc::heapArray(encoded.asPtr());
      hasPrevious = true;
    }
  }
  return BindingDefinitionFactValidationResult::Valid;
}

BindingDefinitionFactValidationResult verifyCallableParameters(
    const VerifiedBindingInput& input, const BindingMetadataCandidate& candidate) {
  const auto inventory = input.definitions().callableParameters();
  if (candidate.callableParameters.size() < inventory.size()) {
    return BindingDefinitionFactValidationResult::MissingRequiredResolution;
  }
  if (candidate.callableParameters.size() > inventory.size()) {
    return BindingDefinitionFactValidationResult::InvalidBindingFact;
  }

  zc::Array<uint8_t> previousKey;
  bool hasPrevious = false;
  for (const auto& fact : candidate.callableParameters) {
    zc::Maybe<const FrozenCallableParameterEntry&> match;
    for (const auto& frozen : inventory) {
      if (frozen.parameter == fact.identity) {
        match = frozen;
        break;
      }
    }
    if (match == zc::none) { return BindingDefinitionFactValidationResult::InvalidBindingFact; }
    ZC_IF_SOME(frozen, match) {
      const auto encoded = frozen.key.encode();
      const bool receiver =
          frozen.record.position().kind() == identity::CallableParameterPositionKind::Receiver;
      if ((hasPrevious && compareCanonicalBytes(previousKey.asPtr(), encoded.asPtr()) >= 0) ||
          !sameDefinitionSite(fact.site, frozen.site) ||
          !sameMaybeDefinitionName(fact.name, frozen.bindingName) ||
          !sameSpan(fact.source, frozen.source) || fact.receiver != receiver ||
          candidateScopeAt(candidate, frozen.node) != fact.declaringScope) {
        return BindingDefinitionFactValidationResult::InvalidBindingFact;
      }
      previousKey = zc::heapArray(encoded.asPtr());
      hasPrevious = true;
    }
  }
  return BindingDefinitionFactValidationResult::Valid;
}

BindingDefinitionFactValidationResult verifyOwnerLocalBindings(
    const VerifiedBindingInput& input, const BindingMetadataCandidate& candidate) {
  const auto inventory = input.definitions().ownerLocalBindings();
  if (candidate.ownerLocalBindings.size() < inventory.size()) {
    return BindingDefinitionFactValidationResult::MissingRequiredResolution;
  }
  if (candidate.ownerLocalBindings.size() > inventory.size()) {
    return BindingDefinitionFactValidationResult::InvalidBindingFact;
  }

  zc::Array<uint8_t> previousKey;
  bool hasPrevious = false;
  for (const auto& fact : candidate.ownerLocalBindings) {
    zc::Maybe<const FrozenOwnerLocalBindingEntry&> match;
    for (const auto& frozen : inventory) {
      if (frozen.binding == fact.identity) {
        match = frozen;
        break;
      }
    }
    if (match == zc::none) { return BindingDefinitionFactValidationResult::InvalidBindingFact; }
    ZC_IF_SOME(frozen, match) {
      const auto encoded = frozen.key.encode();
      DefinitionActivation expectedActivation = DefinitionActivation::AfterInitializer;
      Namespace expectedNamespace = Namespace::Value;
      switch (frozen.key.kind()) {
        case OwnerLocalBindingKind::CallableParameter:
          expectedActivation = DefinitionActivation::ParameterList;
          if (!fact.site.value().is<DeclarationDefinitionSite>()) {
            return BindingDefinitionFactValidationResult::InvalidBindingFact;
          }
          break;
        case OwnerLocalBindingKind::GenericParameter:
          expectedActivation = DefinitionActivation::GenericList;
          expectedNamespace = Namespace::Type;
          if (!fact.site.value().is<DeclarationDefinitionSite>()) {
            return BindingDefinitionFactValidationResult::InvalidBindingFact;
          }
          break;
        case OwnerLocalBindingKind::Local:
          if (!fact.site.value().is<PatternBindingSite>()) {
            return BindingDefinitionFactValidationResult::InvalidBindingFact;
          }
          if (!input.tree().contains(fact.site.value().get<PatternBindingSite>().introducer) ||
              input.tree().node(fact.site.value().get<PatternBindingSite>().introducer).kind !=
                  ast::SyntaxKind::VariableDeclarator) {
            return BindingDefinitionFactValidationResult::InvalidBindingFact;
          }
          break;
        case OwnerLocalBindingKind::PatternBinding: {
          if (!fact.site.value().is<PatternBindingSite>()) {
            return BindingDefinitionFactValidationResult::InvalidBindingFact;
          }
          const auto introducer = fact.site.value().get<PatternBindingSite>().introducer;
          if (!input.tree().contains(introducer)) {
            return BindingDefinitionFactValidationResult::InvalidBindingFact;
          }
          const auto introducerKind = input.tree().node(introducer).kind;
          if (introducerKind == ast::SyntaxKind::ForInStatement) {
            expectedActivation = DefinitionActivation::LoopPattern;
          } else if (introducerKind == ast::SyntaxKind::MatchArmStmt) {
            expectedActivation = DefinitionActivation::MatchPattern;
          } else {
            return BindingDefinitionFactValidationResult::InvalidBindingFact;
          }
          break;
        }
        default:
          return BindingDefinitionFactValidationResult::InvalidBindingFact;
      }
      if ((hasPrevious && compareCanonicalBytes(previousKey.asPtr(), encoded.asPtr()) >= 0) ||
          !sameDefinitionSite(fact.site, frozen.site) || fact.kind != frozen.key.kind() ||
          !sameDefinitionName(fact.name, frozen.key.name()) ||
          fact.nameSpace != expectedNamespace || fact.activation != expectedActivation ||
          !sameSpan(fact.source, frozen.source) ||
          candidateScopeAt(candidate, frozen.node) != fact.declaringScope) {
        return BindingDefinitionFactValidationResult::InvalidBindingFact;
      }
      previousKey = zc::heapArray(encoded.asPtr());
      hasPrevious = true;
    }
  }
  return BindingDefinitionFactValidationResult::Valid;
}

}  // namespace

BindingDefinitionFactValidationResult verifyBindingDefinitionFacts(
    const VerifiedBindingInput& input, const BindingMetadataCandidate& candidate) {
  const auto genericResult = verifyGenericParameters(input, candidate);
  if (genericResult != BindingDefinitionFactValidationResult::Valid) { return genericResult; }
  const auto callableResult = verifyCallableParameters(input, candidate);
  if (callableResult != BindingDefinitionFactValidationResult::Valid) { return callableResult; }
  return verifyOwnerLocalBindings(input, candidate);
}

}  // namespace zomlang::compiler::binder
