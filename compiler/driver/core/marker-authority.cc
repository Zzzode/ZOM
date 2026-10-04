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

#include "compiler/driver/core/marker-authority.h"

#include "zc/core/debug.h"

namespace zomlang::compiler::driver::core {

struct VerifiedCoreStandardMarkerAuthority::Impl final {
  Impl(identity::SemanticContextBrand context, identity::ContextFingerprint&& fingerprint,
       identity::DefId copyDefId, identity::DefinitionKey&& copyDefKey, identity::DefId linearDefId,
       identity::DefinitionKey&& linearDefKey) noexcept
      : context(context),
        fingerprint(zc::mv(fingerprint)),
        copyDefId(copyDefId),
        copyDefKey(zc::mv(copyDefKey)),
        linearDefId(linearDefId),
        linearDefKey(zc::mv(linearDefKey)) {}

  identity::SemanticContextBrand context;
  identity::ContextFingerprint fingerprint;
  identity::DefId copyDefId;
  identity::DefinitionKey copyDefKey;
  identity::DefId linearDefId;
  identity::DefinitionKey linearDefKey;
};

VerifiedCoreStandardMarkerAuthority::VerifiedCoreStandardMarkerAuthority(
    zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
VerifiedCoreStandardMarkerAuthority::~VerifiedCoreStandardMarkerAuthority() noexcept(false) =
    default;
VerifiedCoreStandardMarkerAuthority::VerifiedCoreStandardMarkerAuthority(
    VerifiedCoreStandardMarkerAuthority&&) noexcept = default;
VerifiedCoreStandardMarkerAuthority& VerifiedCoreStandardMarkerAuthority::operator=(
    VerifiedCoreStandardMarkerAuthority&&) noexcept = default;

zc::Maybe<VerifiedCoreStandardMarkerAuthority> VerifiedCoreStandardMarkerAuthority::from(
    identity::SemanticContextBrand context, identity::ContextFingerprint&& fingerprint,
    identity::DefId copyDefId, identity::DefinitionKey&& copyDefKey, identity::DefId linearDefId,
    identity::DefinitionKey&& linearDefKey) {
  if (!context.isValid() || !copyDefId.isValid() || !linearDefId.isValid() ||
      copyDefId == linearDefId || copyDefKey == linearDefKey) {
    return zc::none;
  }
  return VerifiedCoreStandardMarkerAuthority(zc::heap<Impl>(context, zc::mv(fingerprint), copyDefId,
                                                            zc::mv(copyDefKey), linearDefId,
                                                            zc::mv(linearDefKey)));
}

identity::SemanticContextBrand VerifiedCoreStandardMarkerAuthority::context() const noexcept {
  return impl->context;
}

const identity::ContextFingerprint& VerifiedCoreStandardMarkerAuthority::fingerprint()
    const noexcept {
  return impl->fingerprint;
}

identity::DefId VerifiedCoreStandardMarkerAuthority::copy() const noexcept {
  return impl->copyDefId;
}

identity::DefId VerifiedCoreStandardMarkerAuthority::linear() const noexcept {
  return impl->linearDefId;
}

const identity::DefinitionKey& VerifiedCoreStandardMarkerAuthority::copyKey() const noexcept {
  return impl->copyDefKey;
}

const identity::DefinitionKey& VerifiedCoreStandardMarkerAuthority::linearKey() const noexcept {
  return impl->linearDefKey;
}

zc::Maybe<checker::signature::MarkerPolicyConfiguration> buildMarkerPolicyConfiguration(
    const source::core::CoreStandardMarkerPolicyTemplate& policyTemplate,
    const identity::DefinitionKey& copyKey, const identity::DefinitionKey& linearKey) {
  zc::Vector<checker::signature::MarkerPolicyConfigurationEntry> entries(
      policyTemplate.entries().size());
  for (const auto& entry : policyTemplate.entries()) {
    if (entry.role != source::core::CoreSemanticRole::Copy) { return zc::none; }
    zc::Vector<checker::signature::MarkerStructuralSubject> subjects(
        entry.policy.structuralSubjects().size());
    for (const auto subject : entry.policy.structuralSubjects()) {
      subjects.add(static_cast<checker::signature::MarkerStructuralSubject>(subject));
    }
    zc::Vector<type::semantic::PrimitiveKind> primitives(entry.policy.builtinPrimitives().size());
    for (const auto primitive : entry.policy.builtinPrimitives()) { primitives.add(primitive); }
    zc::Vector<checker::signature::MarkerPolicyReferenceConfiguration> references(
        entry.policy.referenceRules().size());
    for (const auto& rule : entry.policy.referenceRules()) {
      zc::Maybe<identity::DefinitionKey> required;
      if (rule.rule.kind() == source::core::CoreMarkerReferenceTemplateRuleKind::Requires) {
        auto requiredRole = rule.rule.requiredRole();
        if (requiredRole == zc::none) { return zc::none; }
        if (ZC_ASSERT_NONNULL(requiredRole) == source::core::CoreSemanticRole::Linear) {
          required = linearKey.clone();
        } else {
          return zc::none;
        }
      } else if (rule.rule.kind() !=
                     source::core::CoreMarkerReferenceTemplateRuleKind::Unconditional ||
                 rule.rule.requiredRole() != zc::none) {
        return zc::none;
      }
      references.add(checker::signature::MarkerPolicyReferenceConfiguration{rule.mutability,
                                                                            zc::mv(required)});
    }
    zc::Vector<type::semantic::Mutability> pointers(entry.policy.rawPointerMutabilities().size());
    for (const auto pointer : entry.policy.rawPointerMutabilities()) { pointers.add(pointer); }
    entries.add(checker::signature::MarkerPolicyConfigurationEntry{
        copyKey.clone(), zc::mv(subjects), zc::mv(primitives), zc::mv(references),
        zc::mv(pointers)});
  }
  return checker::signature::MarkerPolicyConfiguration::from(zc::mv(entries));
}

}  // namespace zomlang::compiler::driver::core
