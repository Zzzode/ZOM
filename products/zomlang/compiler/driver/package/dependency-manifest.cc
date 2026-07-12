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

#include "zomlang/compiler/driver/package/dependency-manifest.h"

#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::driver::package {
namespace {

bool validSelectorText(zc::StringPtr value) {
  if (value.size() == 0) { return false; }
  for (char byte : value) {
    const auto unsignedByte = static_cast<uint8_t>(byte);
    if (unsignedByte == 0 || unsignedByte < 0x20 || unsignedByte == 0x7f) { return false; }
  }
  return true;
}

bool validDomain(identity::DependencyDomain domain) {
  return domain == identity::DependencyDomain::Target ||
         domain == identity::DependencyDomain::Development ||
         domain == identity::DependencyDomain::Build;
}

zc::Maybe<SemVerConstraint> cloneConstraint(const zc::Maybe<SemVerConstraint>& value) {
  ZC_IF_SOME(admitted, value) { return admitted.clone(); }
  return zc::none;
}

zc::Maybe<SemVerConstraint> cloneConstraint(zc::MemoryResource& resource,
                                            const zc::Maybe<SemVerConstraint>& value) {
  ZC_IF_SOME(admitted, value) { return admitted.clone(resource); }
  return zc::none;
}

}  // namespace

VcsSelector::VcsSelector(RevisionVcsSelector&& selector) noexcept : value(zc::mv(selector)) {}
VcsSelector::VcsSelector(TagVcsSelector&& selector) noexcept : value(zc::mv(selector)) {}
VcsSelector::VcsSelector(BranchVcsSelector&& selector) noexcept : value(zc::mv(selector)) {}

VcsSelector VcsSelector::revision(identity::VcsRevision&& revision) {
  return VcsSelector(RevisionVcsSelector{zc::mv(revision)});
}

zc::Maybe<VcsSelector> VcsSelector::tag(zc::StringPtr tag) {
  if (!validSelectorText(tag)) { return zc::none; }
  return VcsSelector(TagVcsSelector{zc::heapString(tag)});
}

zc::Maybe<VcsSelector> VcsSelector::branch(zc::StringPtr branch) {
  if (!validSelectorText(branch)) { return zc::none; }
  return VcsSelector(BranchVcsSelector{zc::heapString(branch)});
}

VcsSelector VcsSelector::clone() const {
  ZC_SWITCH_ONEOF(value) {
    ZC_CASE_ONEOF(selector, RevisionVcsSelector) { return revision(selector.revision.clone()); }
    ZC_CASE_ONEOF(selector, TagVcsSelector) {
      auto result = tag(selector.tag);
      ZC_IF_SOME(admitted, result) { return zc::mv(admitted); }
    }
    ZC_CASE_ONEOF(selector, BranchVcsSelector) {
      auto result = branch(selector.branch);
      ZC_IF_SOME(admitted, result) { return zc::mv(admitted); }
    }
  }
  ZC_IREQUIRE(false, "admitted VCS selector must remain valid");
  ZC_UNREACHABLE
}

VcsSelector VcsSelector::clone(zc::MemoryResource& resource) const {
  if (value.is<RevisionVcsSelector>()) {
    const auto& selector = value.get<RevisionVcsSelector>();
    return VcsSelector(RevisionVcsSelector{selector.revision.clone(resource)});
  }
  if (value.is<TagVcsSelector>()) {
    const auto& selector = value.get<TagVcsSelector>();
    return VcsSelector(TagVcsSelector{zc::resourceHeapString(resource, selector.tag)});
  }
  const auto& selector = value.get<BranchVcsSelector>();
  return VcsSelector(BranchVcsSelector{zc::resourceHeapString(resource, selector.branch)});
}

VcsSelectorKind VcsSelector::kind() const noexcept {
  if (value.is<RevisionVcsSelector>()) { return VcsSelectorKind::Revision; }
  if (value.is<TagVcsSelector>()) { return VcsSelectorKind::Tag; }
  return VcsSelectorKind::Branch;
}

void VcsSelector::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(kind()));
  ZC_SWITCH_ONEOF(value) {
    ZC_CASE_ONEOF(selector, RevisionVcsSelector) { selector.revision.encode(encoder); }
    ZC_CASE_ONEOF(selector, TagVcsSelector) { encoder.encodeByteString(selector.tag.asBytes()); }
    ZC_CASE_ONEOF(selector, BranchVcsSelector) {
      encoder.encodeByteString(selector.branch.asBytes());
    }
  }
}

PackageSourceConstraint::PackageSourceConstraint(RegistrySourceConstraint&& source) noexcept
    : value(zc::mv(source)) {}
PackageSourceConstraint::PackageSourceConstraint(VcsSourceConstraint&& source) noexcept
    : value(zc::mv(source)) {}
PackageSourceConstraint::PackageSourceConstraint(LocalPathSourceConstraint&& source) noexcept
    : value(zc::mv(source)) {}

PackageSourceConstraint PackageSourceConstraint::registry(identity::RegistryIdentity&& registry) {
  return PackageSourceConstraint(RegistrySourceConstraint{zc::mv(registry)});
}

PackageSourceConstraint PackageSourceConstraint::vcs(
    identity::CanonicalUrl&& repository, VcsSelector&& selector,
    identity::CanonicalRelativePath&& subdirectory) {
  return PackageSourceConstraint(
      VcsSourceConstraint{zc::mv(repository), zc::mv(selector), zc::mv(subdirectory)});
}

PackageSourceConstraint PackageSourceConstraint::localPath(
    identity::CanonicalWorkspaceRelativePath&& canonicalPath) {
  return PackageSourceConstraint(LocalPathSourceConstraint{zc::mv(canonicalPath)});
}

PackageSourceConstraint PackageSourceConstraint::clone() const {ZC_SWITCH_ONEOF(value){
    ZC_CASE_ONEOF(source, RegistrySourceConstraint){return registry(source.registry.clone());
}  // namespace zomlang::compiler::driver::package
ZC_CASE_ONEOF(source, VcsSourceConstraint) {
  return vcs(source.repository.clone(), source.selector.clone(), source.subdirectory.clone());
}
ZC_CASE_ONEOF(source, LocalPathSourceConstraint) { return localPath(source.canonicalPath.clone()); }
}
ZC_UNREACHABLE
}

PackageSourceConstraint PackageSourceConstraint::clone(zc::MemoryResource& resource) const {
  if (value.is<RegistrySourceConstraint>()) {
    const auto& source = value.get<RegistrySourceConstraint>();
    return PackageSourceConstraint(RegistrySourceConstraint{source.registry.clone(resource)});
  }
  if (value.is<VcsSourceConstraint>()) {
    const auto& source = value.get<VcsSourceConstraint>();
    return PackageSourceConstraint(VcsSourceConstraint{source.repository.clone(resource),
                                                       source.selector.clone(resource),
                                                       source.subdirectory.clone(resource)});
  }
  const auto& source = value.get<LocalPathSourceConstraint>();
  return PackageSourceConstraint(LocalPathSourceConstraint{source.canonicalPath.clone(resource)});
}

PackageSourceConstraintKind PackageSourceConstraint::kind() const noexcept {
  if (value.is<RegistrySourceConstraint>()) { return PackageSourceConstraintKind::Registry; }
  if (value.is<VcsSourceConstraint>()) { return PackageSourceConstraintKind::Vcs; }
  return PackageSourceConstraintKind::LocalPath;
}

void PackageSourceConstraint::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(kind()));
  ZC_SWITCH_ONEOF(value) {
    ZC_CASE_ONEOF(source, RegistrySourceConstraint) { source.registry.encode(encoder); }
    ZC_CASE_ONEOF(source, VcsSourceConstraint) {
      source.repository.encode(encoder);
      source.selector.encode(encoder);
      source.subdirectory.encode(encoder);
    }
    ZC_CASE_ONEOF(source, LocalPathSourceConstraint) { source.canonicalPath.encode(encoder); }
  }
}

DependencyRequirementWithoutOrigin::DependencyRequirementWithoutOrigin(
    identity::DependencyAlias&& alias, identity::PackageName&& requiredPackage,
    identity::DependencyDomain domain, PackageSourceConstraint&& source,
    zc::Maybe<SemVerConstraint>&& versionCheck, identity::SortedFeatureSet&& requestedFeatures,
    bool useDefaultFeatures, bool optional) noexcept
    : aliasValue(zc::mv(alias)),
      requiredPackageValue(zc::mv(requiredPackage)),
      domainValue(domain),
      sourceValue(zc::mv(source)),
      versionCheckValue(zc::mv(versionCheck)),
      requestedFeatureValues(zc::mv(requestedFeatures)),
      useDefaultFeaturesValue(useDefaultFeatures),
      optionalValue(optional) {}

zc::Maybe<DependencyRequirementWithoutOrigin> DependencyRequirementWithoutOrigin::from(
    identity::DependencyAlias&& alias, identity::PackageName&& requiredPackage,
    identity::DependencyDomain domain, PackageSourceConstraint&& source,
    zc::Maybe<SemVerConstraint>&& versionCheck, identity::SortedFeatureSet&& requestedFeatures,
    bool useDefaultFeatures, bool optional) {
  if (!validDomain(domain) || (optional && domain != identity::DependencyDomain::Target)) {
    return zc::none;
  }
  return DependencyRequirementWithoutOrigin(
      zc::mv(alias), zc::mv(requiredPackage), domain, zc::mv(source), zc::mv(versionCheck),
      zc::mv(requestedFeatures), useDefaultFeatures, optional);
}

DependencyRequirementWithoutOrigin DependencyRequirementWithoutOrigin::clone() const {
  return DependencyRequirementWithoutOrigin(
      aliasValue.clone(), requiredPackageValue.clone(), domainValue, sourceValue.clone(),
      cloneConstraint(versionCheckValue), requestedFeatureValues.clone(), useDefaultFeaturesValue,
      optionalValue);
}

DependencyRequirementWithoutOrigin DependencyRequirementWithoutOrigin::clone(
    zc::MemoryResource& resource) const {
  return DependencyRequirementWithoutOrigin(
      aliasValue.clone(resource), requiredPackageValue.clone(resource), domainValue,
      sourceValue.clone(resource), cloneConstraint(resource, versionCheckValue),
      requestedFeatureValues.clone(resource), useDefaultFeaturesValue, optionalValue);
}

zc::StringPtr DependencyRequirementWithoutOrigin::alias() const noexcept {
  return aliasValue.text();
}
zc::StringPtr DependencyRequirementWithoutOrigin::requiredPackage() const noexcept {
  return requiredPackageValue.text();
}
identity::DependencyDomain DependencyRequirementWithoutOrigin::domain() const noexcept {
  return domainValue;
}
PackageSourceConstraintKind DependencyRequirementWithoutOrigin::sourceKind() const noexcept {
  return sourceValue.kind();
}
const PackageSourceConstraint& DependencyRequirementWithoutOrigin::source() const noexcept {
  return sourceValue;
}
bool DependencyRequirementWithoutOrigin::hasVersionCheck() const noexcept {
  return versionCheckValue != zc::none;
}
const SemVerConstraint& DependencyRequirementWithoutOrigin::versionCheck() const {
  ZC_IF_SOME(value, versionCheckValue) { return value; }
  ZC_IREQUIRE(false, "versionCheck requires a present constraint");
  ZC_UNREACHABLE
}
zc::ArrayPtr<const identity::FeatureName> DependencyRequirementWithoutOrigin::requestedFeatures()
    const noexcept {
  return requestedFeatureValues.values();
}
bool DependencyRequirementWithoutOrigin::useDefaultFeatures() const noexcept {
  return useDefaultFeaturesValue;
}
bool DependencyRequirementWithoutOrigin::optional() const noexcept { return optionalValue; }

void DependencyRequirementWithoutOrigin::encode(identity::CanonicalEncoder& encoder) const {
  aliasValue.encode(encoder);
  requiredPackageValue.encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(domainValue));
  sourceValue.encode(encoder);
  ZC_IF_SOME(value, versionCheckValue) {
    encoder.encodeSome();
    value.encode(encoder);
  }
  else { encoder.encodeNone(); }
  requestedFeatureValues.encode(encoder);
  encoder.encodeBool(useDefaultFeaturesValue);
  encoder.encodeBool(optionalValue);
}

zc::Array<uint8_t> DependencyRequirementWithoutOrigin::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

DependencyRequirement::DependencyRequirement(DependencyRequirementWithoutOrigin&& value,
                                             DiagnosticAnchor&& origin) noexcept
    : value(zc::mv(value)), originValue(zc::mv(origin)) {}

DependencyRequirement DependencyRequirement::from(DependencyRequirementWithoutOrigin&& value,
                                                  DiagnosticAnchor&& origin) {
  return DependencyRequirement(zc::mv(value), zc::mv(origin));
}

DependencyRequirement DependencyRequirement::clone() const {
  return DependencyRequirement(value.clone(), originValue.clone());
}

const DependencyRequirementWithoutOrigin& DependencyRequirement::withoutOrigin() const noexcept {
  return value;
}

void DependencyRequirement::encode(identity::CanonicalEncoder& encoder) const {
  value.encode(encoder);
  originValue.encode(encoder);
}

bool sortDependencyRequirements(zc::Vector<DependencyRequirement>& requirements) {
  for (size_t index = 1; index < requirements.size(); ++index) {
    auto current = zc::mv(requirements[index]);
    size_t insertion = index;
    while (insertion > 0 && current.withoutOrigin().encode().asPtr() <
                                requirements[insertion - 1].withoutOrigin().encode().asPtr()) {
      requirements[insertion] = zc::mv(requirements[insertion - 1]);
      --insertion;
    }
    requirements[insertion] = zc::mv(current);
  }
  for (size_t index = 1; index < requirements.size(); ++index) {
    if (requirements[index - 1].withoutOrigin().encode().asPtr() ==
        requirements[index].withoutOrigin().encode().asPtr()) {
      return false;
    }
  }
  return true;
}

}  // namespace zomlang::compiler::driver::package
