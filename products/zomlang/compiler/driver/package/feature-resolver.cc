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

#include "zomlang/compiler/driver/package/feature-resolver.h"

#include "zomlang/compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::driver::package {
namespace {

bool containsFeature(zc::ArrayPtr<const identity::FeatureName> values, zc::StringPtr name) {
  for (const auto& value : values) {
    if (value.text() == name) { return true; }
  }
  return false;
}

struct DefaultFeatureAllocation final {
  template <typename Value>
  zc::Vector<Value> vector() const {
    return zc::Vector<Value>();
  }

  zc::Maybe<identity::FeatureName> feature(zc::StringPtr name) const {
    return identity::FeatureName::fromCanonical(name);
  }

  zc::Maybe<identity::DependencyAlias> alias(zc::StringPtr name) const {
    return identity::DependencyAlias::fromCanonical(name);
  }

  identity::CanonicalEncoder encoder() const { return identity::CanonicalEncoder(); }
};

struct ResourceFeatureAllocation final {
  explicit ResourceFeatureAllocation(zc::MemoryResource& resource) : resource(resource) {}

  template <typename Value>
  zc::Vector<Value> vector() const {
    return zc::Vector<Value>(resource);
  }

  zc::Maybe<identity::FeatureName> feature(zc::StringPtr name) const {
    return identity::FeatureName::fromCanonical(resource, name);
  }

  zc::Maybe<identity::DependencyAlias> alias(zc::StringPtr name) const {
    return identity::DependencyAlias::fromCanonical(resource, name);
  }

  identity::CanonicalEncoder encoder() const { return identity::CanonicalEncoder(resource); }

  zc::MemoryResource& resource;
};

template <typename Allocation>
bool addFeature(Allocation& allocation, zc::Vector<identity::FeatureName>& values,
                zc::StringPtr name) {
  if (containsFeature(values, name)) { return false; }
  auto feature = allocation.feature(name);
  ZC_IF_SOME(value, feature) {
    values.add(zc::mv(value));
    return true;
  }
  ZC_UNREACHABLE
}

zc::Maybe<const FeatureManifest&> findFeature(const NormalizedManifest& manifest,
                                              zc::StringPtr name) {
  for (const auto& feature : manifest.features()) {
    if (feature.name() == name) { return feature; }
  }
  return zc::none;
}

zc::Maybe<const DependencyRequirementWithoutOrigin&> findDependency(
    const NormalizedManifest& manifest, zc::StringPtr alias) {
  for (const auto& dependency : manifest.targetDependencies()) {
    if (dependency.withoutOrigin().alias() == alias) { return dependency.withoutOrigin(); }
  }
  return zc::none;
}

zc::Maybe<const CanonicalFeatureManifest&> findFeature(const CanonicalManifestRecord& manifest,
                                                       zc::StringPtr name) {
  for (const auto& feature : manifest.features()) {
    if (feature.name() == name) { return feature; }
  }
  return zc::none;
}

zc::Maybe<const DependencyRequirementWithoutOrigin&> findDependency(
    const CanonicalManifestRecord& manifest, zc::StringPtr alias) {
  for (const auto& dependency : manifest.targetDependencies()) {
    if (dependency.alias() == alias) { return dependency; }
  }
  return zc::none;
}

struct MutableDependency final {
  identity::DependencyAlias alias;
  zc::Vector<identity::FeatureName> features;
};

template <typename Allocation>
size_t activateDependency(Allocation& allocation, zc::Vector<MutableDependency>& dependencies,
                          zc::StringPtr alias) {
  for (size_t index = 0; index < dependencies.size(); ++index) {
    if (dependencies[index].alias.text() == alias) { return index; }
  }
  auto admitted = allocation.alias(alias);
  ZC_IF_SOME(value, admitted) {
    dependencies.add(
        MutableDependency{zc::mv(value), allocation.template vector<identity::FeatureName>()});
    return dependencies.size() - 1;
  }
  ZC_UNREACHABLE
}

template <typename Allocation>
bool dependencyLess(Allocation& allocation, const ActivatedDependency& left,
                    const ActivatedDependency& right) {
  auto leftEncoder = allocation.encoder();
  auto rightEncoder = allocation.encoder();
  left.encode(leftEncoder);
  right.encode(rightEncoder);
  return leftEncoder.finish().asPtr() < rightEncoder.finish().asPtr();
}

template <typename Allocation, typename Manifest>
FeatureExpansionResult expandFeatures(Allocation& allocation, const Manifest& manifest,
                                      FeatureActivationDomain domain,
                                      zc::ArrayPtr<const identity::FeatureName> requested,
                                      bool useDefaultFeatures) {
  if (domain != FeatureActivationDomain::Target && domain != FeatureActivationDomain::Build) {
    return FeatureIssue::RequestedFeatureMissing;
  }
  auto active = allocation.template vector<identity::FeatureName>();
  for (const auto& feature : requested) {
    if (findFeature(manifest, feature.text()) == zc::none) {
      return FeatureIssue::RequestedFeatureMissing;
    }
    addFeature(allocation, active, feature.text());
  }
  if (useDefaultFeatures && findFeature(manifest, "default"_zc) != zc::none) {
    addFeature(allocation, active, "default"_zc);
  }

  auto dependencies = allocation.template vector<MutableDependency>();
  for (size_t cursor = 0; cursor < active.size(); ++cursor) {
    ZC_IF_SOME(definition, findFeature(manifest, active[cursor].text())) {
      for (const auto& edgeRecord : definition.edges()) {
        const auto& edge = [&]() -> const FeatureEdge& {
          if constexpr (requires { edgeRecord.edge(); }) {
            return edgeRecord.edge();
          } else {
            return edgeRecord;
          }
        }();
        if (edge.kind() == FeatureEdgeKind::Local) {
          if (findFeature(manifest, edge.localFeature()) == zc::none) {
            return FeatureIssue::UnknownFeature;
          }
          addFeature(allocation, active, edge.localFeature());
          continue;
        }
        ZC_IF_SOME(requirement, findDependency(manifest, edge.dependencyAlias())) {
          if (edge.kind() == FeatureEdgeKind::EnableDependency && !requirement.optional()) {
            return FeatureIssue::DependencyNotOptional;
          }
          const size_t dependency =
              activateDependency(allocation, dependencies, edge.dependencyAlias());
          if (edge.kind() == FeatureEdgeKind::EnableDependencyFeature) {
            addFeature(allocation, dependencies[dependency].features, edge.dependencyFeature());
          }
        }
        else { return FeatureIssue::UnknownDependency; }
      }
    }
    else { return FeatureIssue::UnknownFeature; }
  }

  auto sortedActive = identity::SortedFeatureSet::from(zc::mv(active));
  if (sortedActive == zc::none) { return FeatureIssue::DuplicateEdge; }
  auto activated = allocation.template vector<ActivatedDependency>();
  for (auto& dependency : dependencies) {
    auto features = identity::SortedFeatureSet::from(zc::mv(dependency.features));
    if (features == zc::none) { return FeatureIssue::DuplicateEdge; }
    ZC_IF_SOME(value, features) {
      activated.add(ActivatedDependency::from(zc::mv(dependency.alias), zc::mv(value)));
    }
  }
  for (size_t index = 1; index < activated.size(); ++index) {
    auto current = zc::mv(activated[index]);
    size_t insertion = index;
    while (insertion != 0 && dependencyLess(allocation, current, activated[insertion - 1])) {
      activated[insertion] = zc::mv(activated[insertion - 1]);
      --insertion;
    }
    activated[insertion] = zc::mv(current);
  }
  ZC_IF_SOME(features, sortedActive) {
    return ExpandedFeatureActivation::from(domain, zc::mv(features), zc::mv(activated));
  }
  ZC_UNREACHABLE
}

}  // namespace

ActivatedDependency::ActivatedDependency(identity::DependencyAlias&& alias,
                                         identity::SortedFeatureSet&& requestedFeatures) noexcept
    : aliasValue(zc::mv(alias)), featureValues(zc::mv(requestedFeatures)) {}
ActivatedDependency ActivatedDependency::from(identity::DependencyAlias&& alias,
                                              identity::SortedFeatureSet&& requestedFeatures) {
  return ActivatedDependency(zc::mv(alias), zc::mv(requestedFeatures));
}
zc::StringPtr ActivatedDependency::alias() const noexcept { return aliasValue.text(); }
zc::ArrayPtr<const identity::FeatureName> ActivatedDependency::requestedFeatures() const noexcept {
  return featureValues.values();
}
void ActivatedDependency::encode(identity::CanonicalEncoder& encoder) const {
  aliasValue.encode(encoder);
  featureValues.encode(encoder);
}

ExpandedFeatureActivation::ExpandedFeatureActivation(
    FeatureActivationDomain domain, identity::SortedFeatureSet&& activeFeatures,
    zc::Vector<ActivatedDependency>&& activatedDependencies) noexcept
    : domainValue(domain),
      activeFeatureValues(zc::mv(activeFeatures)),
      activatedDependencyValues(zc::mv(activatedDependencies)) {}
ExpandedFeatureActivation ExpandedFeatureActivation::from(
    FeatureActivationDomain domain, identity::SortedFeatureSet&& activeFeatures,
    zc::Vector<ActivatedDependency>&& activatedDependencies) {
  return ExpandedFeatureActivation(domain, zc::mv(activeFeatures), zc::mv(activatedDependencies));
}
FeatureActivationDomain ExpandedFeatureActivation::domain() const noexcept { return domainValue; }
zc::ArrayPtr<const identity::FeatureName> ExpandedFeatureActivation::activeFeatures()
    const noexcept {
  return activeFeatureValues.values();
}
zc::ArrayPtr<const ActivatedDependency> ExpandedFeatureActivation::activatedDependencies()
    const noexcept {
  return activatedDependencyValues;
}
void ExpandedFeatureActivation::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(domainValue));
  activeFeatureValues.encode(encoder);
  encoder.encodeSequenceSize(activatedDependencyValues.size());
  for (const auto& dependency : activatedDependencyValues) { dependency.encode(encoder); }
}
zc::Array<uint8_t> ExpandedFeatureActivation::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

FeatureExpansionResult FeatureResolver::expand(const NormalizedManifest& manifest,
                                               FeatureActivationDomain domain,
                                               zc::ArrayPtr<const identity::FeatureName> requested,
                                               bool useDefaultFeatures) {
  DefaultFeatureAllocation allocation;
  return expandFeatures(allocation, manifest, domain, requested, useDefaultFeatures);
}

FeatureExpansionResult FeatureResolver::expand(const CanonicalManifestRecord& manifest,
                                               FeatureActivationDomain domain,
                                               zc::ArrayPtr<const identity::FeatureName> requested,
                                               bool useDefaultFeatures) {
  DefaultFeatureAllocation allocation;
  return expandFeatures(allocation, manifest, domain, requested, useDefaultFeatures);
}

FeatureExpansionResult FeatureResolver::expand(zc::MemoryResource& resource,
                                               const CanonicalManifestRecord& manifest,
                                               FeatureActivationDomain domain,
                                               zc::ArrayPtr<const identity::FeatureName> requested,
                                               bool useDefaultFeatures) {
  ResourceFeatureAllocation allocation(resource);
  return expandFeatures(allocation, manifest, domain, requested, useDefaultFeatures);
}

}  // namespace zomlang::compiler::driver::package
