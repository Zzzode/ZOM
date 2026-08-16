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

#include "zomlang/compiler/driver/package/package-resolver.h"

#include "zomlang/compiler/driver/package/registry-record.h"
#include "zomlang/compiler/driver/package/source-record.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::driver::package {
namespace {

zc::Array<uint8_t> coordinateBytes(zc::MemoryResource& resource,
                                   const identity::PackageBaseKey& base) {
  identity::CanonicalEncoder encoder(resource);
  base.source().encode(encoder);
  auto name = identity::PackageName::fromCanonical(resource, base.name());
  ZC_IF_SOME(value, name) {
    value.encode(encoder);
  } else {
    ZC_UNREACHABLE
  }
  return encoder.finish();
}

bool bytesEqual(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  return left == right;
}

template <typename Value>
zc::Array<uint8_t> encoded(zc::MemoryResource& resource, const Value& value) {
  identity::CanonicalEncoder encoder(resource);
  value.encode(encoder);
  return encoder.finish();
}

zc::Array<uint8_t> copyBytes(zc::MemoryResource& resource, zc::ArrayPtr<const uint8_t> value) {
  zc::Vector<uint8_t> copy(resource, value.size());
  copy.addAll(value);
  return copy.releaseAsArray();
}

bool addFeature(zc::MemoryResource& resource, zc::Vector<identity::FeatureName>& values,
                zc::StringPtr name) {
  for (const auto& value : values) {
    if (value.text() == name) { return false; }
  }
  auto feature = identity::FeatureName::fromCanonical(resource, name);
  ZC_IF_SOME(value, feature) {
    values.add(zc::mv(value));
    return true;
  }
  ZC_UNREACHABLE
}

void sortByteArrays(zc::Vector<zc::Array<uint8_t>>& values) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    size_t insertion = index;
    while (insertion != 0 && current.asPtr() < values[insertion - 1].asPtr()) {
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
}

struct Selection final {
  zc::Array<uint8_t> coordinate;
  size_t releaseIndex;
};

struct ReleaseGroup final {
  ReleaseGroup(zc::Array<uint8_t>&& lookup, zc::Array<uint8_t>&& coordinate,
               zc::Vector<size_t>&& candidates) noexcept
      : lookup(zc::mv(lookup)), coordinate(zc::mv(coordinate)), candidates(zc::mv(candidates)) {}

  zc::Array<uint8_t> lookup;
  zc::Array<uint8_t> coordinate;
  zc::Vector<size_t> candidates;
};

struct ConstraintGroup final {
  explicit ConstraintGroup(zc::MemoryResource& resource, zc::Array<uint8_t>&& coordinate) noexcept
      : coordinate(zc::mv(coordinate)),
        candidates(resource),
        constraints(resource),
        causes(resource) {}

  ConstraintGroup(zc::MemoryResource& resource, zc::Array<uint8_t>&& coordinate,
                  zc::Vector<size_t>&& candidates) noexcept
      : coordinate(zc::mv(coordinate)),
        candidates(zc::mv(candidates)),
        constraints(resource),
        causes(resource) {}

  zc::Array<uint8_t> coordinate;
  zc::Vector<size_t> candidates;
  zc::Vector<SemVerConstraint> constraints;
  zc::Vector<zc::Array<uint8_t>> causes;
};

struct CoordinateIndexEntry final {
  zc::Array<uint8_t> key;
  size_t index;
};

struct Activation final {
  Activation(zc::MemoryResource& resource, zc::Array<uint8_t>&& coordinate,
             FeatureActivationDomain domain, zc::Vector<identity::FeatureName>&& requested,
             bool useDefaultFeatures, bool includeDevelopment) noexcept
      : coordinate(zc::mv(coordinate)),
        domain(domain),
        requested(zc::mv(requested)),
        useDefaultFeatures(useDefaultFeatures),
        includeDevelopment(includeDevelopment),
        expanded(resource),
        processed(false) {}

  zc::Array<uint8_t> coordinate;
  FeatureActivationDomain domain;
  zc::Vector<identity::FeatureName> requested;
  bool useDefaultFeatures;
  bool includeDevelopment;
  zc::Vector<identity::FeatureName> expanded;
  bool processed;
};

struct EdgeFact final {
  zc::Array<uint8_t> consumer;
  zc::Array<uint8_t> provider;
  zc::String alias;
  identity::DependencyDomain domain;
  FeatureActivationDomain consumerActivation;
  FeatureActivationDomain providerActivation;
};

struct Analysis final {
  explicit Analysis(zc::MemoryResource& resource)
      : groups(resource),
        groupIndex(resource),
        activations(resource),
        activationIndex(resource),
        edges(resource) {}

  zc::Vector<ConstraintGroup> groups;
  zc::Vector<CoordinateIndexEntry> groupIndex;
  zc::Vector<Activation> activations;
  zc::Vector<CoordinateIndexEntry> activationIndex;
  zc::Vector<EdgeFact> edges;
};

using AnalysisResult = zc::OneOf<Analysis, PackageResolverFailure>;

size_t findSelection(zc::ArrayPtr<const Selection> selections,
                     zc::ArrayPtr<const uint8_t> coordinate) {
  size_t lower = 0;
  size_t upper = selections.size();
  while (lower < upper) {
    const size_t middle = lower + (upper - lower) / 2;
    if (selections[middle].coordinate.asPtr() < coordinate) {
      lower = middle + 1;
    } else {
      upper = middle;
    }
  }
  if (lower < selections.size() && bytesEqual(selections[lower].coordinate, coordinate)) {
    return lower;
  }
  return selections.size();
}

void addSelection(zc::Vector<Selection>& selections, Selection&& selection) {
  size_t lower = 0;
  size_t upper = selections.size();
  while (lower < upper) {
    const size_t middle = lower + (upper - lower) / 2;
    if (selections[middle].coordinate.asPtr() < selection.coordinate.asPtr()) {
      lower = middle + 1;
    } else {
      upper = middle;
    }
  }
  const size_t insertion = lower;
  selections.add(zc::mv(selection));
  auto current = zc::mv(selections.back());
  for (size_t index = selections.size() - 1; index > insertion; --index) {
    selections[index] = zc::mv(selections[index - 1]);
  }
  selections[insertion] = zc::mv(current);
}

void removeSelection(zc::Vector<Selection>& selections, zc::ArrayPtr<const uint8_t> coordinate) {
  const size_t index = findSelection(selections, coordinate);
  ZC_IREQUIRE(index < selections.size(), "removed resolver selection must exist");
  for (size_t cursor = index + 1; cursor < selections.size(); ++cursor) {
    selections[cursor - 1] = zc::mv(selections[cursor]);
  }
  selections.removeLast();
}

size_t findCoordinateIndex(zc::ArrayPtr<const CoordinateIndexEntry> entries,
                           zc::ArrayPtr<const uint8_t> key) {
  size_t lower = 0;
  size_t upper = entries.size();
  while (lower < upper) {
    const size_t middle = lower + (upper - lower) / 2;
    const zc::ArrayPtr<const uint8_t> middleKey = entries[middle].key;
    if (middleKey < key) {
      lower = middle + 1;
    } else {
      upper = middle;
    }
  }
  if (lower < entries.size() && entries[lower].key.asPtr() == key) { return entries[lower].index; }
  return zc::maxValue;
}

void addCoordinateIndex(zc::MemoryResource& resource, zc::Vector<CoordinateIndexEntry>& entries,
                        zc::ArrayPtr<const uint8_t> key, size_t valueIndex) {
  size_t lower = 0;
  size_t upper = entries.size();
  while (lower < upper) {
    const size_t middle = lower + (upper - lower) / 2;
    const zc::ArrayPtr<const uint8_t> middleKey = entries[middle].key;
    if (middleKey < key) {
      lower = middle + 1;
    } else {
      upper = middle;
    }
  }
  const size_t insertion = lower;
  zc::Vector<uint8_t> keyCopy(resource, key.size());
  keyCopy.addAll(key);
  entries.add(CoordinateIndexEntry{keyCopy.releaseAsArray(), valueIndex});
  auto current = zc::mv(entries.back());
  for (size_t index = entries.size() - 1; index > insertion; --index) {
    entries[index] = zc::mv(entries[index - 1]);
  }
  entries[insertion] = zc::mv(current);
}

zc::Array<uint8_t> activationKey(zc::MemoryResource& resource,
                                 zc::ArrayPtr<const uint8_t> coordinate,
                                 FeatureActivationDomain domain) {
  identity::CanonicalEncoder encoder(resource);
  encoder.encodeByteString(coordinate);
  encoder.encodeUint8(static_cast<uint8_t>(domain));
  return encoder.finish();
}

size_t findActivation(zc::MemoryResource& resource, const Analysis& analysis,
                      zc::ArrayPtr<const uint8_t> coordinate, FeatureActivationDomain domain) {
  const auto key = activationKey(resource, coordinate, domain);
  const size_t index = findCoordinateIndex(analysis.activationIndex, key);
  return index == zc::maxValue ? analysis.activations.size() : index;
}

bool eligible(zc::MemoryResource& resource, const ResolverRelease& release,
              zc::ArrayPtr<const SemVerConstraint> constraints) {
  if (release.yanked()) { return false; }
  auto version = identity::ResolvedVersion::fromCanonical(resource, release.base().version());
  ZC_IF_SOME(value, version) {
    for (const auto& constraint : constraints) {
      if (!constraint.allows(value)) { return false; }
    }
    return true;
  }
  ZC_UNREACHABLE
}

void sortCandidates(zc::MemoryResource& resource, zc::Vector<size_t>& candidates,
                    zc::ArrayPtr<const ResolverRelease> releases) {
  for (size_t index = 1; index < candidates.size(); ++index) {
    const size_t current = candidates[index];
    size_t insertion = index;
    auto currentVersion =
        identity::ResolvedVersion::fromCanonical(resource, releases[current].base().version());
    ZC_IF_SOME(currentValue, currentVersion) {
      while (insertion != 0) {
        auto previousVersion = identity::ResolvedVersion::fromCanonical(
            resource, releases[candidates[insertion - 1]].base().version());
        bool movePrevious = false;
        ZC_IF_SOME(previousValue, previousVersion) {
          if (previousValue < currentValue) {
            movePrevious = true;
          } else if (!(currentValue < previousValue)) {
            identity::CanonicalEncoder currentEncoder(resource);
            identity::CanonicalEncoder previousEncoder(resource);
            releases[current].encode(currentEncoder);
            releases[candidates[insertion - 1]].encode(previousEncoder);
            movePrevious = currentEncoder.finish().asPtr() < previousEncoder.finish().asPtr();
          }
        }
        if (!movePrevious) { break; }
        candidates[insertion] = candidates[insertion - 1];
        --insertion;
      }
    }
    candidates[insertion] = current;
  }
}

zc::Array<uint8_t> releaseLookupBytes(zc::MemoryResource& resource,
                                      const ResolverRelease& release) {
  identity::CanonicalEncoder encoder(resource);
  release.acceptedSource().encode(encoder);
  auto name = identity::PackageName::fromCanonical(resource, release.base().name());
  ZC_IF_SOME(value, name) {
    value.encode(encoder);
  } else {
    ZC_UNREACHABLE
  }
  return encoder.finish();
}

zc::Array<uint8_t> requirementLookupBytes(zc::MemoryResource& resource,
                                          const DependencyRequirementWithoutOrigin& requirement) {
  identity::CanonicalEncoder encoder(resource);
  requirement.source().encode(encoder);
  auto name = identity::PackageName::fromCanonical(resource, requirement.requiredPackage());
  ZC_IF_SOME(value, name) {
    value.encode(encoder);
  } else {
    ZC_UNREACHABLE
  }
  return encoder.finish();
}

zc::Vector<ReleaseGroup> sortReleaseGroups(zc::MemoryResource& resource,
                                           zc::Vector<ReleaseGroup>&& input) {
  if (input.size() < 2) { return zc::mv(input); }
  const size_t middle = input.size() / 2;
  zc::Vector<ReleaseGroup> left(resource);
  zc::Vector<ReleaseGroup> right(resource);
  for (size_t index = 0; index < input.size(); ++index) {
    if (index < middle) {
      left.add(zc::mv(input[index]));
    } else {
      right.add(zc::mv(input[index]));
    }
  }
  left = sortReleaseGroups(resource, zc::mv(left));
  right = sortReleaseGroups(resource, zc::mv(right));
  zc::Vector<ReleaseGroup> result(resource);
  size_t leftIndex = 0;
  size_t rightIndex = 0;
  while (leftIndex < left.size() || rightIndex < right.size()) {
    if (rightIndex == right.size() ||
        (leftIndex < left.size() &&
         left[leftIndex].lookup.asPtr() < right[rightIndex].lookup.asPtr())) {
      result.add(zc::mv(left[leftIndex++]));
    } else {
      result.add(zc::mv(right[rightIndex++]));
    }
  }
  return result;
}

zc::Vector<ReleaseGroup> buildReleaseGroups(zc::MemoryResource& resource,
                                            zc::ArrayPtr<const ResolverRelease> releases) {
  zc::Vector<ReleaseGroup> entries(resource);
  for (size_t releaseIndex = 0; releaseIndex < releases.size(); ++releaseIndex) {
    zc::Vector<size_t> candidates(resource);
    candidates.add(releaseIndex);
    entries.add(ReleaseGroup(releaseLookupBytes(resource, releases[releaseIndex]),
                             coordinateBytes(resource, releases[releaseIndex].base()),
                             zc::mv(candidates)));
  }
  entries = sortReleaseGroups(resource, zc::mv(entries));
  zc::Vector<ReleaseGroup> groups(resource);
  for (auto& entry : entries) {
    if (groups.size() == 0 || groups.back().lookup.asPtr() != entry.lookup.asPtr()) {
      groups.add(zc::mv(entry));
      continue;
    }
    ZC_IREQUIRE(groups.back().coordinate.asPtr() == entry.coordinate.asPtr(),
                "one accepted source must identify one package coordinate");
    groups.back().candidates.addAll(entry.candidates);
  }
  for (auto& group : groups) { sortCandidates(resource, group.candidates, releases); }
  return groups;
}

size_t findReleaseGroup(zc::ArrayPtr<const ReleaseGroup> groups,
                        zc::ArrayPtr<const uint8_t> lookup) {
  size_t lower = 0;
  size_t upper = groups.size();
  while (lower < upper) {
    const size_t middle = lower + (upper - lower) / 2;
    if (groups[middle].lookup.asPtr() < lookup) {
      lower = middle + 1;
    } else {
      upper = middle;
    }
  }
  if (lower < groups.size() && groups[lower].lookup.asPtr() == lookup) { return lower; }
  return groups.size();
}

PackageResolverFailure failure(zc::MemoryResource& resource, ResolverIssue issue,
                               zc::ArrayPtr<const uint8_t> coordinate,
                               zc::ArrayPtr<const zc::Array<uint8_t>> causes = {}) {
  zc::Vector<uint8_t> coordinateCopy(resource, coordinate.size());
  coordinateCopy.addAll(coordinate);
  zc::Vector<zc::Array<uint8_t>> causeCopies(resource);
  for (const auto& cause : causes) {
    zc::Vector<uint8_t> copy(resource, cause.size());
    copy.addAll(cause);
    causeCopies.add(copy.releaseAsArray());
  }
  sortByteArrays(causeCopies);
  return PackageResolverFailure::from(issue, coordinateCopy.releaseAsArray(), zc::mv(causeCopies));
}

identity::Sha256Digest incompatibilityId(zc::ArrayPtr<const uint8_t> record) {
  identity::Sha256Hasher hasher;
  if (!hasher.update("zom.incompatibility"_zc.asBytes())) { ZC_UNREACHABLE }
  const uint8_t separator = 0;
  if (!hasher.update(zc::arrayPtr(separator)) || !hasher.update(record)) { ZC_UNREACHABLE }
  auto digest = hasher.finish();
  ZC_IF_SOME(value, digest) { return value; }
  ZC_UNREACHABLE
}

IncompatibilityRecord dependencyIncompatibility(zc::MemoryResource& resource,
                                                zc::ArrayPtr<const uint8_t> coordinate,
                                                const SemVerConstraint& constraint,
                                                zc::ArrayPtr<const uint8_t> cause) {
  identity::CanonicalEncoder encoder(resource);
  encoder.encodeSequenceSize(1);
  encoder.encodeByteString(coordinate);
  encoder.encodeBool(true);
  constraint.encode(encoder);
  encoder.encodeUint8(0x02);
  encoder.encodeByteString(cause);
  auto bytes = encoder.finish();
  return IncompatibilityRecord::from(incompatibilityId(bytes), zc::mv(bytes));
}

IncompatibilityRecord noVersionsIncompatibility(zc::MemoryResource& resource,
                                                zc::ArrayPtr<const uint8_t> coordinate) {
  identity::CanonicalEncoder encoder(resource);
  encoder.encodeSequenceSize(0);
  encoder.encodeUint8(0x03);
  encoder.encodeByteString(coordinate);
  auto bytes = encoder.finish();
  return IncompatibilityRecord::from(incompatibilityId(bytes), zc::mv(bytes));
}

IncompatibilityRecord derivedIncompatibility(zc::MemoryResource& resource,
                                             const identity::Sha256Digest& left,
                                             const identity::Sha256Digest& right) {
  const auto& first = right.bytes() < left.bytes() ? right : left;
  const auto& second = right.bytes() < left.bytes() ? left : right;
  identity::CanonicalEncoder encoder(resource);
  encoder.encodeSequenceSize(0);
  encoder.encodeUint8(0x04);
  encoder.encodeDigest(first);
  encoder.encodeDigest(second);
  auto bytes = encoder.finish();
  return IncompatibilityRecord::from(incompatibilityId(bytes), zc::mv(bytes));
}

IncompatibilityGraph incompatibilityGraph(zc::MemoryResource& resource,
                                          const ConstraintGroup& group) {
  zc::Vector<IncompatibilityRecord> dependencyRecords(resource);
  for (size_t index = 0; index < group.constraints.size(); ++index) {
    const zc::ArrayPtr<const uint8_t> cause =
        index < group.causes.size() ? group.causes[index].asPtr() : zc::ArrayPtr<const uint8_t>();
    dependencyRecords.add(
        dependencyIncompatibility(resource, group.coordinate, group.constraints[index], cause));
  }
  for (size_t index = 1; index < dependencyRecords.size(); ++index) {
    auto current = zc::mv(dependencyRecords[index]);
    size_t insertion = index;
    while (insertion != 0 && current.id().bytes() < dependencyRecords[insertion - 1].id().bytes()) {
      dependencyRecords[insertion] = zc::mv(dependencyRecords[insertion - 1]);
      --insertion;
    }
    dependencyRecords[insertion] = zc::mv(current);
  }

  zc::Vector<IncompatibilityRecord> records(resource);
  records.add(noVersionsIncompatibility(resource, group.coordinate));
  identity::Sha256Digest root = records.back().id();
  for (auto& dependency : dependencyRecords) {
    const identity::Sha256Digest dependencyId = dependency.id();
    records.add(zc::mv(dependency));
    auto derived = derivedIncompatibility(resource, root, dependencyId);
    root = derived.id();
    records.add(zc::mv(derived));
  }
  for (size_t index = 1; index < records.size(); ++index) {
    auto current = zc::mv(records[index]);
    size_t insertion = index;
    while (insertion != 0 && current.id().bytes() < records[insertion - 1].id().bytes()) {
      records[insertion] = zc::mv(records[insertion - 1]);
      --insertion;
    }
    records[insertion] = zc::mv(current);
  }
  return IncompatibilityGraph::from(root, zc::mv(records));
}

PackageResolverFailure conflictFailure(zc::MemoryResource& resource, const ConstraintGroup& group) {
  zc::Vector<zc::Array<uint8_t>> causes(resource);
  for (const auto& cause : group.causes) { causes.add(copyBytes(resource, cause)); }
  sortByteArrays(causes);
  return PackageResolverFailure::withIncompatibility(
      ResolverIssue::NoVersionSatisfiesConstraints, copyBytes(resource, group.coordinate),
      zc::mv(causes), incompatibilityGraph(resource, group));
}

bool appendActivation(zc::MemoryResource& resource, Analysis& analysis,
                      zc::ArrayPtr<const uint8_t> coordinate, FeatureActivationDomain domain,
                      zc::ArrayPtr<const identity::FeatureName> requested, bool useDefaultFeatures,
                      bool includeDevelopment) {
  size_t index = findActivation(resource, analysis, coordinate, domain);
  if (index == analysis.activations.size()) {
    zc::Vector<uint8_t> coordinateCopy(resource, coordinate.size());
    coordinateCopy.addAll(coordinate);
    zc::Vector<identity::FeatureName> features(resource);
    for (const auto& feature : requested) { addFeature(resource, features, feature.text()); }
    analysis.activations.add(Activation(resource, coordinateCopy.releaseAsArray(), domain,
                                        zc::mv(features), useDefaultFeatures, includeDevelopment));
    const auto key = activationKey(resource, coordinate, domain);
    addCoordinateIndex(resource, analysis.activationIndex, key, analysis.activations.size() - 1);
    return true;
  }
  bool changed = false;
  for (const auto& feature : requested) {
    changed =
        addFeature(resource, analysis.activations[index].requested, feature.text()) || changed;
  }
  if (useDefaultFeatures && !analysis.activations[index].useDefaultFeatures) {
    analysis.activations[index].useDefaultFeatures = true;
    changed = true;
  }
  if (includeDevelopment && !analysis.activations[index].includeDevelopment) {
    analysis.activations[index].includeDevelopment = true;
    changed = true;
  }
  return changed;
}

AnalysisResult analyze(zc::MemoryResource& resource, zc::ArrayPtr<const ResolverRoot> roots,
                       zc::ArrayPtr<const ResolverRelease> releases,
                       zc::ArrayPtr<const ReleaseGroup> releaseGroups,
                       zc::ArrayPtr<const Selection> selections, zc::Maybe<Analysis>& previous) {
  Analysis analysis(resource);
  ZC_IF_SOME(value, previous) {
    analysis = zc::mv(value);
    previous = zc::none;
  } else {
    for (const auto& root : roots) {
      const auto coordinate = coordinateBytes(resource, root.base());
      const size_t selectionIndex = findSelection(selections, coordinate);
      if (selectionIndex == selections.size() || [&] {
            identity::CanonicalEncoder selectedEncoder(resource);
            identity::CanonicalEncoder rootEncoder(resource);
            releases[selections[selectionIndex].releaseIndex].base().encode(selectedEncoder);
            root.base().encode(rootEncoder);
            return selectedEncoder.finish().asPtr() != rootEncoder.finish().asPtr();
          }()) {
        return failure(resource, ResolverIssue::InvalidRoot, coordinate);
      }
      ConstraintGroup group(resource, coordinateBytes(resource, root.base()));
      group.candidates.add(selections[selectionIndex].releaseIndex);
      analysis.groups.add(zc::mv(group));
      addCoordinateIndex(resource, analysis.groupIndex, coordinate, analysis.groups.size() - 1);
      appendActivation(resource, analysis, coordinate, FeatureActivationDomain::Target,
                       root.requestedFeatures(), root.useDefaultFeatures(),
                       root.includeDevelopment());
    }
  }

  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t activationIndex = 0; activationIndex < analysis.activations.size();
         ++activationIndex) {
      auto& activation = analysis.activations[activationIndex];
      const size_t selectionIndex = findSelection(selections, activation.coordinate);
      if (selectionIndex == selections.size()) { continue; }
      const auto& release = releases[selections[selectionIndex].releaseIndex];
      auto sortedRequestedInput = zc::Vector<identity::FeatureName>(resource);
      for (const auto& feature : activation.requested) {
        auto cloned = identity::FeatureName::fromCanonical(resource, feature.text());
        ZC_IF_SOME(value, cloned) { sortedRequestedInput.add(zc::mv(value)); }
      }
      auto sortedRequested = identity::SortedFeatureSet::from(zc::mv(sortedRequestedInput));
      ZC_IF_SOME(requested, sortedRequested) {
        auto expansion = FeatureResolver::expand(resource, release.manifest(), activation.domain,
                                                 requested.values(), activation.useDefaultFeatures);
        if (expansion.template is<FeatureIssue>()) {
          zc::Vector<zc::Array<uint8_t>> causes(resource);
          identity::CanonicalEncoder causeEncoder(resource);
          causeEncoder.encodeUint8(static_cast<uint8_t>(expansion.template get<FeatureIssue>()));
          causes.add(causeEncoder.finish());
          return failure(resource, ResolverIssue::FeatureInvalid, activation.coordinate, causes);
        }
        auto& expanded = expansion.template get<ExpandedFeatureActivation>();
        bool expansionChanged =
            !activation.processed || expanded.activeFeatures().size() != activation.expanded.size();
        if (!expansionChanged) {
          for (const auto& feature : expanded.activeFeatures()) {
            bool found = false;
            for (const auto& old : activation.expanded) {
              if (old.text() == feature.text()) {
                found = true;
                break;
              }
            }
            if (!found) {
              expansionChanged = true;
              break;
            }
          }
        }
        if (!expansionChanged) { continue; }
        activation.expanded.clear();
        for (const auto& feature : expanded.activeFeatures()) {
          addFeature(resource, activation.expanded, feature.text());
        }
        activation.processed = true;
        auto consumerCoordinate = copyBytes(resource, activation.coordinate);
        const auto activationDomain = activation.domain;
        const bool includeDevelopment = activation.includeDevelopment;

        auto processRequirements =
            [&](zc::ArrayPtr<const DependencyRequirementWithoutOrigin> values,
                identity::DependencyDomain emittedDomain,
                FeatureActivationDomain providerDomain) -> zc::Maybe<PackageResolverFailure> {
          for (const auto& requirement : values) {
            bool activated = !requirement.optional();
            zc::Vector<identity::FeatureName> requestedFeatures(resource);
            for (const auto& feature : requirement.requestedFeatures()) {
              addFeature(resource, requestedFeatures, feature.text());
            }
            for (const auto& dependency : expanded.activatedDependencies()) {
              if (dependency.alias() != requirement.alias()) { continue; }
              activated = true;
              for (const auto& feature : dependency.requestedFeatures()) {
                addFeature(resource, requestedFeatures, feature.text());
              }
            }
            if (!activated) { continue; }

            const auto lookup = requirementLookupBytes(resource, requirement);
            const size_t releaseGroupIndex = findReleaseGroup(releaseGroups, lookup);
            if (releaseGroupIndex == releaseGroups.size()) {
              identity::CanonicalEncoder requirementEncoder(resource);
              requirement.encode(requirementEncoder);
              auto encoded = requirementEncoder.finish();
              return failure(resource, ResolverIssue::SourceBindingMissing, encoded);
            }
            const auto& releaseGroup = releaseGroups[releaseGroupIndex];
            const auto& providerCoordinate = releaseGroup.coordinate;
            size_t groupIndex = findCoordinateIndex(analysis.groupIndex, providerCoordinate);
            if (groupIndex == zc::maxValue) {
              zc::Vector<size_t> matching(resource);
              for (size_t candidate : releaseGroup.candidates) { matching.add(candidate); }
              analysis.groups.add(ConstraintGroup(resource, copyBytes(resource, providerCoordinate),
                                                  zc::mv(matching)));
              groupIndex = analysis.groups.size() - 1;
              addCoordinateIndex(resource, analysis.groupIndex, providerCoordinate, groupIndex);
              changed = true;
            }
            if (requirement.hasVersionCheck()) {
              identity::CanonicalEncoder constraintEncoder(resource);
              requirement.versionCheck().encode(constraintEncoder);
              auto encoded = constraintEncoder.finish();
              bool known = false;
              for (const auto& constraint : analysis.groups[groupIndex].constraints) {
                identity::CanonicalEncoder existingEncoder(resource);
                constraint.encode(existingEncoder);
                if (existingEncoder.finish().asPtr() == encoded.asPtr()) {
                  known = true;
                  break;
                }
              }
              if (!known) {
                analysis.groups[groupIndex].constraints.add(
                    requirement.versionCheck().clone(resource));
                changed = true;
              }
              identity::CanonicalEncoder requirementEncoder(resource);
              requirement.encode(requirementEncoder);
              auto cause = requirementEncoder.finish();
              bool causeKnown = false;
              for (const auto& existing : analysis.groups[groupIndex].causes) {
                if (existing.asPtr() == cause.asPtr()) {
                  causeKnown = true;
                  break;
                }
              }
              if (!causeKnown) { analysis.groups[groupIndex].causes.add(zc::mv(cause)); }
            }
            appendActivation(resource, analysis, providerCoordinate, providerDomain,
                             requestedFeatures, requirement.useDefaultFeatures(), false);

            zc::Vector<uint8_t> consumer(resource, consumerCoordinate.size());
            consumer.addAll(consumerCoordinate);
            zc::Vector<uint8_t> provider(resource, providerCoordinate.size());
            provider.addAll(providerCoordinate);
            analysis.edges.add(EdgeFact{consumer.releaseAsArray(), provider.releaseAsArray(),
                                        zc::resourceHeapString(resource, requirement.alias()),
                                        emittedDomain, activationDomain, providerDomain});
            changed = true;
          }
          return zc::none;
        };

        const auto providerDomain = activationDomain;
        auto targetFailure =
            processRequirements(release.manifest().targetDependencies(),
                                identity::DependencyDomain::Target, providerDomain);
        ZC_IF_SOME(value, targetFailure) { return zc::mv(value); }
        if (activationDomain == FeatureActivationDomain::Target && includeDevelopment) {
          auto developmentFailure = processRequirements(
              release.manifest().developmentDependencies(), identity::DependencyDomain::Development,
              FeatureActivationDomain::Target);
          ZC_IF_SOME(value, developmentFailure) { return zc::mv(value); }
        }
        if (activationDomain == FeatureActivationDomain::Target) {
          auto buildFailure = processRequirements(release.manifest().buildDependencies(),
                                                  identity::DependencyDomain::Build,
                                                  FeatureActivationDomain::Build);
          ZC_IF_SOME(value, buildFailure) { return zc::mv(value); }
        }
      }
    }
  }

  for (const auto& group : analysis.groups) {
    const size_t selectionIndex = findSelection(selections, group.coordinate);
    if (selectionIndex == selections.size()) { continue; }
    const auto& selected = releases[selections[selectionIndex].releaseIndex];
    if (!eligible(resource, selected, group.constraints)) {
      return conflictFailure(resource, group);
    }
  }
  return analysis;
}

bool solve(zc::MemoryResource& resource, zc::ArrayPtr<const ResolverRoot> roots,
           zc::ArrayPtr<const ResolverRelease> releases,
           zc::ArrayPtr<const ReleaseGroup> releaseGroups, zc::Vector<Selection>& selections,
           zc::Maybe<Analysis>& solved, zc::Maybe<PackageResolverFailure>& lastFailure) {
  zc::Maybe<Analysis> previous;
  auto result = analyze(resource, roots, releases, releaseGroups, selections, previous);
  if (result.is<PackageResolverFailure>()) {
    lastFailure = zc::mv(result.get<PackageResolverFailure>());
    return false;
  }
  auto analysis = zc::mv(result.get<Analysis>());
  size_t chosenGroup = analysis.groups.size();
  zc::Vector<size_t> chosenEligible(resource);
  for (size_t groupIndex = 0; groupIndex < analysis.groups.size(); ++groupIndex) {
    const auto& group = analysis.groups[groupIndex];
    if (findSelection(selections, group.coordinate) != selections.size()) { continue; }
    zc::Vector<size_t> eligibleCandidates(resource);
    for (size_t candidate : group.candidates) {
      if (eligible(resource, releases[candidate], group.constraints)) {
        eligibleCandidates.add(candidate);
      }
    }
    if (eligibleCandidates.size() == 0) {
      lastFailure = conflictFailure(resource, group);
      return false;
    }
    if (chosenGroup == analysis.groups.size() ||
        eligibleCandidates.size() < chosenEligible.size() ||
        (eligibleCandidates.size() == chosenEligible.size() &&
         group.coordinate.asPtr() < analysis.groups[chosenGroup].coordinate.asPtr())) {
      chosenGroup = groupIndex;
      chosenEligible = zc::mv(eligibleCandidates);
    }
  }
  if (chosenGroup == analysis.groups.size()) {
    solved = zc::mv(analysis);
    return true;
  }
  sortCandidates(resource, chosenEligible, releases);
  for (size_t candidate : chosenEligible) {
    zc::Vector<uint8_t> coordinate(resource, analysis.groups[chosenGroup].coordinate.size());
    coordinate.addAll(analysis.groups[chosenGroup].coordinate);
    addSelection(selections, Selection{coordinate.releaseAsArray(), candidate});
    if (solve(resource, roots, releases, releaseGroups, selections, solved, lastFailure)) {
      return true;
    }
    removeSelection(selections, analysis.groups[chosenGroup].coordinate);
  }
  return false;
}

bool solveGreedy(zc::MemoryResource& resource, zc::ArrayPtr<const ResolverRoot> roots,
                 zc::ArrayPtr<const ResolverRelease> releases,
                 zc::ArrayPtr<const ReleaseGroup> releaseGroups, zc::Vector<Selection>& selections,
                 zc::Maybe<Analysis>& solved) {
  zc::Maybe<Analysis> previous;
  while (true) {
    auto result = analyze(resource, roots, releases, releaseGroups, selections, previous);
    if (result.is<PackageResolverFailure>()) { return false; }
    auto analysis = zc::mv(result.get<Analysis>());
    bool addedSelection = false;
    for (const auto& group : analysis.groups) {
      if (findSelection(selections, group.coordinate) != selections.size()) { continue; }
      zc::Vector<size_t> candidates(resource);
      for (size_t candidate : group.candidates) {
        if (eligible(resource, releases[candidate], group.constraints)) {
          candidates.add(candidate);
        }
      }
      if (candidates.size() == 0) { return false; }
      sortCandidates(resource, candidates, releases);
      addSelection(selections, Selection{copyBytes(resource, group.coordinate), candidates[0]});
      addedSelection = true;
    }
    if (!addedSelection) {
      solved = zc::mv(analysis);
      return true;
    }
    previous = zc::mv(analysis);
  }
}

identity::SortedFeatureSet sortedFeatures(zc::MemoryResource& resource,
                                          zc::ArrayPtr<const identity::FeatureName> features) {
  zc::Vector<identity::FeatureName> copies(resource);
  for (const auto& feature : features) {
    auto copy = identity::FeatureName::fromCanonical(resource, feature.text());
    ZC_IF_SOME(value, copy) { copies.add(zc::mv(value)); }
  }
  auto result = identity::SortedFeatureSet::from(zc::mv(copies));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
}

identity::PackageKey packageKey(zc::MemoryResource& resource, const ResolverRelease& release,
                                zc::ArrayPtr<const identity::FeatureName> features) {
  auto name = identity::PackageName::fromCanonical(resource, release.base().name());
  auto version = identity::ResolvedVersion::fromCanonical(resource, release.base().version());
  ZC_IF_SOME(nameValue, name) {
    ZC_IF_SOME(versionValue, version) {
      return identity::PackageKey::from(release.base().source().clone(resource), zc::mv(nameValue),
                                        zc::mv(versionValue), sortedFeatures(resource, features));
    }
  }
  ZC_UNREACHABLE
}

identity::PackageBaseKey packageBaseKey(zc::MemoryResource& resource,
                                        const identity::PackageKey& key) {
  auto name = identity::PackageName::fromCanonical(resource, key.name());
  auto version = identity::ResolvedVersion::fromCanonical(resource, key.version());
  ZC_IF_SOME(nameValue, name) {
    ZC_IF_SOME(versionValue, version) {
      return identity::PackageBaseKey::from(key.source().clone(resource), zc::mv(nameValue),
                                            zc::mv(versionValue));
    }
  }
  ZC_UNREACHABLE
}

zc::ArrayPtr<const identity::FeatureName> featuresFor(zc::MemoryResource& resource,
                                                      const Analysis& analysis,
                                                      zc::ArrayPtr<const uint8_t> coordinate,
                                                      FeatureActivationDomain domain) {
  const size_t index = findActivation(resource, analysis, coordinate, domain);
  if (index == analysis.activations.size()) { return {}; }
  return analysis.activations[index].expanded;
}

template <typename Value>
struct CachedCanonicalValue final {
  zc::Array<uint8_t> key;
  Value value;
};

template <typename Value>
zc::Vector<CachedCanonicalValue<Value>> cachedCanonicalMergeSort(
    zc::MemoryResource& resource, zc::Vector<CachedCanonicalValue<Value>>&& input) {
  if (input.size() < 2) { return zc::mv(input); }
  const size_t middle = input.size() / 2;
  zc::Vector<CachedCanonicalValue<Value>> left(resource);
  zc::Vector<CachedCanonicalValue<Value>> right(resource);
  for (size_t index = 0; index < input.size(); ++index) {
    if (index < middle) {
      left.add(zc::mv(input[index]));
    } else {
      right.add(zc::mv(input[index]));
    }
  }
  left = cachedCanonicalMergeSort(resource, zc::mv(left));
  right = cachedCanonicalMergeSort(resource, zc::mv(right));
  zc::Vector<CachedCanonicalValue<Value>> result(resource);
  size_t leftIndex = 0;
  size_t rightIndex = 0;
  while (leftIndex < left.size() || rightIndex < right.size()) {
    bool takeLeft = rightIndex == right.size();
    if (!takeLeft && leftIndex < left.size()) {
      takeLeft = left[leftIndex].key.asPtr() < right[rightIndex].key.asPtr();
    }
    if (takeLeft) {
      result.add(zc::mv(left[leftIndex++]));
    } else {
      result.add(zc::mv(right[rightIndex++]));
    }
  }
  return result;
}

template <typename Value>
void canonicalSort(zc::MemoryResource& resource, zc::Vector<Value>& values) {
  zc::Vector<CachedCanonicalValue<Value>> cached(resource);
  for (auto& value : values) {
    identity::CanonicalEncoder encoder(resource);
    value.encode(encoder);
    cached.add(CachedCanonicalValue<Value>{encoder.finish(), zc::mv(value)});
  }
  cached = cachedCanonicalMergeSort(resource, zc::mv(cached));
  zc::Vector<Value> sorted(resource);
  for (auto& value : cached) { sorted.add(zc::mv(value.value)); }
  values = zc::mv(sorted);
}

zc::Array<uint8_t> edgeFactBytes(zc::MemoryResource& resource, const EdgeFact& edge) {
  identity::CanonicalEncoder encoder(resource);
  encoder.encodeByteString(edge.consumer);
  encoder.encodeByteString(edge.provider);
  encoder.encodeByteString(edge.alias.asBytes());
  encoder.encodeUint8(static_cast<uint8_t>(edge.domain));
  encoder.encodeUint8(static_cast<uint8_t>(edge.consumerActivation));
  encoder.encodeUint8(static_cast<uint8_t>(edge.providerActivation));
  return encoder.finish();
}

struct EncodedOrderEntry final {
  zc::Array<uint8_t> key;
  size_t index;

  void encode(identity::CanonicalEncoder& encoder) const { encoder.encodeByteString(key); }
};

zc::Maybe<PackageResolverFailure> detectDependencyCycle(zc::MemoryResource& resource,
                                                        zc::ArrayPtr<const Selection> selections,
                                                        zc::ArrayPtr<const EdgeFact> edges) {
  zc::Vector<EncodedOrderEntry> orderedEdges(resource);
  for (size_t index = 0; index < edges.size(); ++index) {
    orderedEdges.add(EncodedOrderEntry{edgeFactBytes(resource, edges[index]), index});
  }
  canonicalSort(resource, orderedEdges);
  zc::Vector<zc::Vector<size_t>> outgoing(resource, selections.size());
  for (size_t index = 0; index < selections.size(); ++index) {
    outgoing.add(zc::Vector<size_t>(resource));
  }
  for (const auto& entry : orderedEdges) {
    const size_t consumer = findSelection(selections, edges[entry.index].consumer);
    if (consumer != selections.size()) { outgoing[consumer].add(entry.index); }
  }
  zc::Vector<uint8_t> states(resource, selections.size());
  for (size_t index = 0; index < selections.size(); ++index) { states.add(0); }
  struct VisitFrame final {
    size_t node;
    size_t nextEdge;
  };
  zc::Vector<size_t> path(resource);
  zc::Vector<VisitFrame> frames(resource);
  for (size_t root = 0; root < selections.size(); ++root) {
    if (states[root] != 0) { continue; }
    states[root] = 1;
    path.add(root);
    frames.add(VisitFrame{root, 0});
    while (frames.size() != 0) {
      auto& frame = frames.back();
      if (frame.nextEdge == outgoing[frame.node].size()) {
        states[frame.node] = 2;
        frames.removeLast();
        path.removeLast();
        continue;
      }
      const auto& edge = edges[outgoing[frame.node][frame.nextEdge++]];
      const size_t provider = findSelection(selections, edge.provider);
      if (provider == selections.size() || states[provider] == 2) { continue; }
      if (states[provider] == 0) {
        states[provider] = 1;
        path.add(provider);
        frames.add(VisitFrame{provider, 0});
        continue;
      }
      zc::Vector<zc::Array<uint8_t>> cycle(resource);
      size_t start = 0;
      while (start < path.size() && path[start] != provider) { ++start; }
      for (size_t index = start; index < path.size(); ++index) {
        cycle.add(copyBytes(resource, selections[path[index]].coordinate));
      }
      cycle.add(copyBytes(resource, selections[provider].coordinate));
      return failure(resource, ResolverIssue::DependencyCycle, selections[root].coordinate, cycle);
    }
  }
  return zc::none;
}

}  // namespace

ResolverRelease::ResolverRelease(PackageSourceConstraint&& acceptedSource,
                                 identity::PackageBaseKey&& base,
                                 CanonicalManifestRecord&& manifest,
                                 const identity::Sha256Digest& manifestDigest,
                                 const identity::Sha256Digest& sourceTreeDigest,
                                 zc::Maybe<ArchiveFormat> archiveFormat,
                                 zc::Maybe<identity::Sha256Digest> archiveDigest,
                                 zc::Maybe<SigningKeyId> signingKey, bool yanked) noexcept
    : sourceValue(zc::mv(acceptedSource)),
      baseValue(zc::mv(base)),
      manifestValue(zc::mv(manifest)),
      manifestDigestValue(manifestDigest),
      sourceTreeDigestValue(sourceTreeDigest),
      archiveFormatValue(zc::mv(archiveFormat)),
      archiveDigestValue(zc::mv(archiveDigest)),
      signingKeyValue(zc::mv(signingKey)),
      yankedValue(yanked) {}
ResolverRelease ResolverRelease::fromRegistry(const VerifiedRegistryReleaseRecord& release) {
  auto name = identity::PackageName::fromCanonical(release.package());
  auto version = identity::ResolvedVersion::fromCanonical(release.version());
  ZC_IF_SOME(nameValue, name) {
    ZC_IF_SOME(versionValue, version) {
      return ResolverRelease(
          PackageSourceConstraint::registry(release.registry().clone()),
          identity::PackageBaseKey::from(
              identity::CanonicalPackageSource::registry(release.registry().clone()),
              zc::mv(nameValue), zc::mv(versionValue)),
          release.manifest().clone(), release.manifestDigest(), release.sourceTreeDigest(),
          ArchiveFormat::TarZstd, release.archiveDigest(),
          SigningKeyId::fromDigest(release.signingKey().digest()), release.yanked());
    }
  }
  ZC_UNREACHABLE
}
ResolverRelease ResolverRelease::fromRegistry(zc::MemoryResource& resource,
                                              const VerifiedRegistryReleaseRecord& release) {
  auto name = identity::PackageName::fromCanonical(resource, release.package());
  auto version = identity::ResolvedVersion::fromCanonical(resource, release.version());
  ZC_IF_SOME(nameValue, name) {
    ZC_IF_SOME(versionValue, version) {
      return ResolverRelease(
          PackageSourceConstraint::registry(release.registry().clone(resource)),
          identity::PackageBaseKey::from(
              identity::CanonicalPackageSource::registry(release.registry().clone(resource)),
              zc::mv(nameValue), zc::mv(versionValue)),
          release.manifest().clone(resource), release.manifestDigest(), release.sourceTreeDigest(),
          ArchiveFormat::TarZstd, release.archiveDigest(),
          SigningKeyId::fromDigest(release.signingKey().digest()), release.yanked());
    }
  }
  ZC_UNREACHABLE
}
ResolverRelease ResolverRelease::fromVcs(const VerifiedVcsPackageRecord& release) {
  const auto& source = release.base().source();
  ZC_IREQUIRE(source.kind() == identity::PackageSourceKind::Vcs,
              "VCS resolver record must carry a VCS package base");
  return fromVcs(release,
                 PackageSourceConstraint::vcs(source.vcsRepository().clone(),
                                              VcsSelector::revision(source.vcsRevision().clone()),
                                              source.vcsSubdirectory().clone()));
}
ResolverRelease ResolverRelease::fromVcs(zc::MemoryResource& resource,
                                         const VerifiedVcsPackageRecord& release) {
  const auto& source = release.base().source();
  ZC_IREQUIRE(source.kind() == identity::PackageSourceKind::Vcs,
              "VCS resolver record must carry a VCS package base");
  return fromVcs(
      resource, release,
      PackageSourceConstraint::vcs(source.vcsRepository().clone(resource),
                                   VcsSelector::revision(source.vcsRevision().clone(resource)),
                                   source.vcsSubdirectory().clone(resource)));
}
ResolverRelease ResolverRelease::fromVcs(const VerifiedVcsPackageRecord& release,
                                         PackageSourceConstraint&& acceptedSelector) {
  ZC_IREQUIRE(acceptedSelector.kind() == PackageSourceConstraintKind::Vcs,
              "VCS resolver record must accept a VCS selector");
  return ResolverRelease(zc::mv(acceptedSelector), release.base().clone(),
                         release.canonicalManifest().clone(), release.manifestDigest(),
                         release.sourceTreeDigest(), zc::none, zc::none, zc::none, false);
}
ResolverRelease ResolverRelease::fromVcs(zc::MemoryResource& resource,
                                         const VerifiedVcsPackageRecord& release,
                                         PackageSourceConstraint&& acceptedSelector) {
  ZC_IREQUIRE(acceptedSelector.kind() == PackageSourceConstraintKind::Vcs,
              "VCS resolver record must accept a VCS selector");
  return ResolverRelease(zc::mv(acceptedSelector), release.base().clone(resource),
                         release.canonicalManifest().clone(resource), release.manifestDigest(),
                         release.sourceTreeDigest(), zc::none, zc::none, zc::none, false);
}
ResolverRelease ResolverRelease::fromLocal(const LocalPackageRecord& release) {
  const auto& source = release.base().source();
  ZC_IREQUIRE(source.kind() == identity::PackageSourceKind::LocalPath,
              "local resolver record must carry a local package base");
  return ResolverRelease(PackageSourceConstraint::localPath(source.localPath().clone()),
                         release.base().clone(), release.canonicalManifest().clone(),
                         release.manifestDigest(), release.sourceTreeDigest(), zc::none, zc::none,
                         zc::none, false);
}
ResolverRelease ResolverRelease::fromLocal(zc::MemoryResource& resource,
                                           const LocalPackageRecord& release) {
  const auto& source = release.base().source();
  ZC_IREQUIRE(source.kind() == identity::PackageSourceKind::LocalPath,
              "local resolver record must carry a local package base");
  return ResolverRelease(PackageSourceConstraint::localPath(source.localPath().clone(resource)),
                         release.base().clone(resource),
                         release.canonicalManifest().clone(resource), release.manifestDigest(),
                         release.sourceTreeDigest(), zc::none, zc::none, zc::none, false);
}
ResolverRelease ResolverRelease::clone() const {
  zc::Maybe<ArchiveFormat> archiveFormat;
  ZC_IF_SOME(value, archiveFormatValue) { archiveFormat = value; }
  zc::Maybe<identity::Sha256Digest> archiveDigest;
  ZC_IF_SOME(value, archiveDigestValue) { archiveDigest = value; }
  zc::Maybe<SigningKeyId> signingKey;
  ZC_IF_SOME(value, signingKeyValue) { signingKey = SigningKeyId::fromDigest(value.digest()); }
  return ResolverRelease(sourceValue.clone(), baseValue.clone(), manifestValue.clone(),
                         manifestDigestValue, sourceTreeDigestValue, zc::mv(archiveFormat),
                         zc::mv(archiveDigest), zc::mv(signingKey), yankedValue);
}
ResolverRelease ResolverRelease::clone(zc::MemoryResource& resource) const {
  zc::Maybe<ArchiveFormat> archiveFormat;
  ZC_IF_SOME(value, archiveFormatValue) { archiveFormat = value; }
  zc::Maybe<identity::Sha256Digest> archiveDigest;
  ZC_IF_SOME(value, archiveDigestValue) { archiveDigest = value; }
  zc::Maybe<SigningKeyId> signingKey;
  ZC_IF_SOME(value, signingKeyValue) { signingKey = SigningKeyId::fromDigest(value.digest()); }
  return ResolverRelease(sourceValue.clone(resource), baseValue.clone(resource),
                         manifestValue.clone(resource), manifestDigestValue, sourceTreeDigestValue,
                         zc::mv(archiveFormat), zc::mv(archiveDigest), zc::mv(signingKey),
                         yankedValue);
}
const PackageSourceConstraint& ResolverRelease::acceptedSource() const noexcept {
  return sourceValue;
}
const identity::PackageBaseKey& ResolverRelease::base() const noexcept { return baseValue; }
const CanonicalManifestRecord& ResolverRelease::manifest() const noexcept { return manifestValue; }
const identity::Sha256Digest& ResolverRelease::manifestDigest() const noexcept {
  return manifestDigestValue;
}
const identity::Sha256Digest& ResolverRelease::sourceTreeDigest() const noexcept {
  return sourceTreeDigestValue;
}
bool ResolverRelease::hasArchive() const noexcept { return archiveFormatValue != zc::none; }
ArchiveFormat ResolverRelease::archiveFormat() const {
  ZC_IF_SOME(value, archiveFormatValue) { return value; }
  ZC_UNREACHABLE
}
const identity::Sha256Digest& ResolverRelease::archiveDigest() const {
  ZC_IF_SOME(value, archiveDigestValue) { return value; }
  ZC_UNREACHABLE
}
const SigningKeyId& ResolverRelease::signingKey() const {
  ZC_IF_SOME(value, signingKeyValue) { return value; }
  ZC_UNREACHABLE
}
bool ResolverRelease::yanked() const noexcept { return yankedValue; }
void ResolverRelease::encode(identity::CanonicalEncoder& encoder) const {
  sourceValue.encode(encoder);
  baseValue.encode(encoder);
  manifestValue.encode(encoder);
  encoder.encodeDigest(manifestDigestValue);
  encoder.encodeDigest(sourceTreeDigestValue);
  if (hasArchive()) {
    encoder.encodeSome();
    encoder.encodeUint8(static_cast<uint8_t>(archiveFormat()));
    encoder.encodeDigest(archiveDigest());
    signingKey().encode(encoder);
  } else {
    encoder.encodeNone();
  }
  encoder.encodeBool(yankedValue);
}

ResolverRoot::ResolverRoot(identity::PackageBaseKey&& base,
                           identity::SortedFeatureSet&& requestedFeatures, bool useDefaultFeatures,
                           bool includeDevelopment) noexcept
    : baseValue(zc::mv(base)),
      requestedFeatureValues(zc::mv(requestedFeatures)),
      useDefaultFeaturesValue(useDefaultFeatures),
      includeDevelopmentValue(includeDevelopment) {}
ResolverRoot ResolverRoot::from(identity::PackageBaseKey&& base,
                                identity::SortedFeatureSet&& requestedFeatures,
                                bool useDefaultFeatures, bool includeDevelopment) {
  return ResolverRoot(zc::mv(base), zc::mv(requestedFeatures), useDefaultFeatures,
                      includeDevelopment);
}
ResolverRoot ResolverRoot::clone() const {
  return from(baseValue.clone(), requestedFeatureValues.clone(), useDefaultFeaturesValue,
              includeDevelopmentValue);
}
ResolverRoot ResolverRoot::clone(zc::MemoryResource& resource) const {
  return from(baseValue.clone(resource), requestedFeatureValues.clone(resource),
              useDefaultFeaturesValue, includeDevelopmentValue);
}
const identity::PackageBaseKey& ResolverRoot::base() const noexcept { return baseValue; }
zc::ArrayPtr<const identity::FeatureName> ResolverRoot::requestedFeatures() const noexcept {
  return requestedFeatureValues.values();
}
bool ResolverRoot::useDefaultFeatures() const noexcept { return useDefaultFeaturesValue; }
bool ResolverRoot::includeDevelopment() const noexcept { return includeDevelopmentValue; }
void ResolverRoot::encode(identity::CanonicalEncoder& encoder) const {
  baseValue.encode(encoder);
  requestedFeatureValues.encode(encoder);
  encoder.encodeBool(useDefaultFeaturesValue);
  encoder.encodeBool(includeDevelopmentValue);
}

IncompatibilityRecord::IncompatibilityRecord(const identity::Sha256Digest& id,
                                             zc::Array<uint8_t>&& canonicalBytes) noexcept
    : idValue(id), byteValues(zc::mv(canonicalBytes)) {}
IncompatibilityRecord IncompatibilityRecord::from(const identity::Sha256Digest& id,
                                                  zc::Array<uint8_t>&& canonicalBytes) {
  return IncompatibilityRecord(id, zc::mv(canonicalBytes));
}
const identity::Sha256Digest& IncompatibilityRecord::id() const noexcept { return idValue; }
zc::ArrayPtr<const uint8_t> IncompatibilityRecord::canonicalBytes() const noexcept {
  return byteValues;
}
void IncompatibilityRecord::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeDigest(idValue);
  encoder.encodeByteString(byteValues);
}

IncompatibilityGraph::IncompatibilityGraph(const identity::Sha256Digest& root,
                                           zc::Vector<IncompatibilityRecord>&& records) noexcept
    : rootValue(root), recordValues(zc::mv(records)) {}
IncompatibilityGraph IncompatibilityGraph::from(const identity::Sha256Digest& root,
                                                zc::Vector<IncompatibilityRecord>&& records) {
  return IncompatibilityGraph(root, zc::mv(records));
}
const identity::Sha256Digest& IncompatibilityGraph::root() const noexcept { return rootValue; }
zc::ArrayPtr<const IncompatibilityRecord> IncompatibilityGraph::records() const noexcept {
  return recordValues;
}
void IncompatibilityGraph::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeDigest(rootValue);
  encoder.encodeSequenceSize(recordValues.size());
  for (const auto& record : recordValues) { record.encode(encoder); }
}
zc::Array<uint8_t> IncompatibilityGraph::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

PackageResolverFailure::PackageResolverFailure(ResolverIssue issue, zc::Array<uint8_t>&& coordinate,
                                               zc::Vector<zc::Array<uint8_t>>&& causes,
                                               zc::Maybe<IncompatibilityGraph>&& graph) noexcept
    : issueValue(issue),
      coordinateValue(zc::mv(coordinate)),
      causeValues(zc::mv(causes)),
      graphValue(zc::mv(graph)) {}
PackageResolverFailure PackageResolverFailure::from(ResolverIssue issue,
                                                    zc::Array<uint8_t>&& coordinate,
                                                    zc::Vector<zc::Array<uint8_t>>&& causes) {
  return PackageResolverFailure(issue, zc::mv(coordinate), zc::mv(causes), zc::none);
}
PackageResolverFailure PackageResolverFailure::withIncompatibility(
    ResolverIssue issue, zc::Array<uint8_t>&& coordinate, zc::Vector<zc::Array<uint8_t>>&& causes,
    IncompatibilityGraph&& graph) {
  return PackageResolverFailure(issue, zc::mv(coordinate), zc::mv(causes), zc::mv(graph));
}
ResolverIssue PackageResolverFailure::issue() const noexcept { return issueValue; }
zc::ArrayPtr<const uint8_t> PackageResolverFailure::coordinate() const noexcept {
  return coordinateValue;
}
zc::ArrayPtr<const zc::Array<uint8_t>> PackageResolverFailure::causes() const noexcept {
  return causeValues;
}
bool PackageResolverFailure::hasIncompatibilityGraph() const noexcept {
  return graphValue != zc::none;
}
const IncompatibilityGraph& PackageResolverFailure::incompatibilityGraph() const {
  ZC_IF_SOME(value, graphValue) { return value; }
  ZC_UNREACHABLE
}
void PackageResolverFailure::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeUint8(static_cast<uint8_t>(issueValue));
  encoder.encodeByteString(coordinateValue);
  encoder.encodeSequenceSize(causeValues.size());
  for (const auto& cause : causeValues) { encoder.encodeByteString(cause); }
  ZC_IF_SOME(graph, graphValue) {
    encoder.encodeSome();
    graph.encode(encoder);
  } else {
    encoder.encodeNone();
  }
}
zc::Array<uint8_t> PackageResolverFailure::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

SourceViewKey::SourceViewKey(identity::CanonicalPackageSource&& source,
                             const identity::Sha256Digest& sourceTreeDigest) noexcept
    : sourceValue(zc::mv(source)), sourceTreeDigestValue(sourceTreeDigest) {}
SourceViewKey SourceViewKey::from(identity::CanonicalPackageSource&& source,
                                  const identity::Sha256Digest& sourceTreeDigest) {
  return SourceViewKey(zc::mv(source), sourceTreeDigest);
}
SourceViewKey SourceViewKey::clone() const {
  return SourceViewKey(sourceValue.clone(), sourceTreeDigestValue);
}
SourceViewKey SourceViewKey::clone(zc::MemoryResource& resource) const {
  return SourceViewKey(sourceValue.clone(resource), sourceTreeDigestValue);
}
const identity::CanonicalPackageSource& SourceViewKey::source() const noexcept {
  return sourceValue;
}
const identity::Sha256Digest& SourceViewKey::sourceTreeDigest() const noexcept {
  return sourceTreeDigestValue;
}
void SourceViewKey::encode(identity::CanonicalEncoder& encoder) const {
  sourceValue.encode(encoder);
  encoder.encodeDigest(sourceTreeDigestValue);
}

ResolvedPackageRecord::ResolvedPackageRecord(identity::PackageKey&& key,
                                             CanonicalManifestRecord&& manifest,
                                             const identity::Sha256Digest& manifestDigest,
                                             const identity::Sha256Digest& sourceTreeDigest,
                                             SourceViewKey&& sourceView,
                                             zc::Maybe<identity::TargetName> libraryTarget) noexcept
    : keyValue(zc::mv(key)),
      manifestValue(zc::mv(manifest)),
      manifestDigestValue(manifestDigest),
      sourceTreeDigestValue(sourceTreeDigest),
      sourceViewValue(zc::mv(sourceView)),
      libraryTargetValue(zc::mv(libraryTarget)) {}
zc::Maybe<ResolvedPackageRecord> ResolvedPackageRecord::from(
    zc::MemoryResource& resource, identity::PackageKey&& key, CanonicalManifestRecord&& manifest,
    const identity::Sha256Digest& manifestDigest, const identity::Sha256Digest& sourceTreeDigest,
    SourceViewKey&& sourceView, zc::Maybe<identity::TargetName> libraryTarget) {
  identity::CanonicalEncoder keySource(resource);
  identity::CanonicalEncoder viewSource(resource);
  key.source().encode(keySource);
  sourceView.source().encode(viewSource);
  if (keySource.finish().asPtr() != viewSource.finish().asPtr() ||
      sourceTreeDigest != sourceView.sourceTreeDigest()) {
    return zc::none;
  }
  identity::Sha256Hasher hasher;
  const uint8_t separator = 0;
  identity::CanonicalEncoder manifestEncoder(resource);
  manifest.encode(manifestEncoder);
  const auto manifestBytes = manifestEncoder.finish();
  if (!hasher.update("zom.normalized-manifest"_zc.asBytes()) ||
      !hasher.update(zc::arrayPtr(separator)) || !hasher.update(manifestBytes.asPtr())) {
    return zc::none;
  }
  auto computedDigest = hasher.finish();
  if (computedDigest == zc::none) { return zc::none; }
  ZC_IF_SOME(value, computedDigest) {
    if (value != manifestDigest) { return zc::none; }
  }
  const auto library = manifest.library();
  if ((library == zc::none) != (libraryTarget == zc::none)) { return zc::none; }
  ZC_IF_SOME(manifestLibrary, library) {
    ZC_IF_SOME(target, libraryTarget) {
      if (manifestLibrary.name() != target.text()) { return zc::none; }
    }
  }
  return ResolvedPackageRecord(zc::mv(key), zc::mv(manifest), manifestDigest, sourceTreeDigest,
                               zc::mv(sourceView), zc::mv(libraryTarget));
}
zc::Maybe<ResolvedPackageRecord> ResolvedPackageRecord::from(
    identity::PackageKey&& key, CanonicalManifestRecord&& manifest,
    const identity::Sha256Digest& manifestDigest, const identity::Sha256Digest& sourceTreeDigest,
    SourceViewKey&& sourceView, zc::Maybe<identity::TargetName> libraryTarget) {
  identity::CanonicalEncoder keySource;
  identity::CanonicalEncoder viewSource;
  key.source().encode(keySource);
  sourceView.source().encode(viewSource);
  if (keySource.finish().asPtr() != viewSource.finish().asPtr() ||
      sourceTreeDigest != sourceView.sourceTreeDigest()) {
    return zc::none;
  }
  identity::Sha256Hasher hasher;
  const uint8_t separator = 0;
  const auto manifestBytes = manifest.encode();
  if (!hasher.update("zom.normalized-manifest"_zc.asBytes()) ||
      !hasher.update(zc::arrayPtr(separator)) || !hasher.update(manifestBytes.asPtr())) {
    return zc::none;
  }
  auto computedDigest = hasher.finish();
  if (computedDigest == zc::none) { return zc::none; }
  ZC_IF_SOME(value, computedDigest) {
    if (value != manifestDigest) { return zc::none; }
  }
  const auto library = manifest.library();
  if ((library == zc::none) != (libraryTarget == zc::none)) { return zc::none; }
  ZC_IF_SOME(manifestLibrary, library) {
    ZC_IF_SOME(target, libraryTarget) {
      if (manifestLibrary.name() != target.text()) { return zc::none; }
    }
  }
  return ResolvedPackageRecord(zc::mv(key), zc::mv(manifest), manifestDigest, sourceTreeDigest,
                               zc::mv(sourceView), zc::mv(libraryTarget));
}
ResolvedPackageRecord ResolvedPackageRecord::clone() const {
  zc::Maybe<identity::TargetName> libraryTarget;
  ZC_IF_SOME(value, libraryTargetValue) { libraryTarget = value.clone(); }
  auto result = from(keyValue.clone(), manifestValue.clone(), manifestDigestValue,
                     sourceTreeDigestValue, sourceViewValue.clone(), zc::mv(libraryTarget));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
}
ResolvedPackageRecord ResolvedPackageRecord::clone(zc::MemoryResource& resource) const {
  zc::Maybe<identity::TargetName> libraryTarget;
  ZC_IF_SOME(value, libraryTargetValue) { libraryTarget = value.clone(resource); }
  auto result =
      from(resource, keyValue.clone(resource), manifestValue.clone(resource), manifestDigestValue,
           sourceTreeDigestValue, sourceViewValue.clone(resource), zc::mv(libraryTarget));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
}
const identity::PackageKey& ResolvedPackageRecord::key() const noexcept { return keyValue; }
const CanonicalManifestRecord& ResolvedPackageRecord::manifest() const noexcept {
  return manifestValue;
}
const identity::Sha256Digest& ResolvedPackageRecord::manifestDigest() const noexcept {
  return manifestDigestValue;
}
const identity::Sha256Digest& ResolvedPackageRecord::sourceTreeDigest() const noexcept {
  return sourceTreeDigestValue;
}
const SourceViewKey& ResolvedPackageRecord::sourceView() const noexcept { return sourceViewValue; }
zc::Maybe<const identity::TargetName&> ResolvedPackageRecord::libraryTarget() const noexcept {
  ZC_IF_SOME(value, libraryTargetValue) { return value; }
  return zc::none;
}
void ResolvedPackageRecord::encode(identity::CanonicalEncoder& encoder) const {
  keyValue.encode(encoder);
  manifestValue.encode(encoder);
  encoder.encodeDigest(manifestDigestValue);
  encoder.encodeDigest(sourceTreeDigestValue);
  sourceViewValue.encode(encoder);
  ZC_IF_SOME(target, libraryTargetValue) {
    encoder.encodeSome();
    target.encode(encoder);
  } else {
    encoder.encodeNone();
  }
}

ResolvedFeatureSet::ResolvedFeatureSet(identity::PackageBaseKey&& base,
                                       FeatureActivationDomain domain,
                                       identity::SortedFeatureSet&& features) noexcept
    : baseValue(zc::mv(base)), domainValue(domain), featureValues(zc::mv(features)) {}
zc::Maybe<ResolvedFeatureSet> ResolvedFeatureSet::from(identity::PackageBaseKey&& base,
                                                       FeatureActivationDomain domain,
                                                       identity::SortedFeatureSet&& features) {
  if (domain != FeatureActivationDomain::Target && domain != FeatureActivationDomain::Build) {
    return zc::none;
  }
  return ResolvedFeatureSet(zc::mv(base), domain, zc::mv(features));
}
ResolvedFeatureSet ResolvedFeatureSet::clone() const {
  auto result = from(baseValue.clone(), domainValue, featureValues.clone());
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
}
ResolvedFeatureSet ResolvedFeatureSet::clone(zc::MemoryResource& resource) const {
  auto result = from(baseValue.clone(resource), domainValue, featureValues.clone(resource));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
}
const identity::PackageBaseKey& ResolvedFeatureSet::base() const noexcept { return baseValue; }
FeatureActivationDomain ResolvedFeatureSet::domain() const noexcept { return domainValue; }
zc::ArrayPtr<const identity::FeatureName> ResolvedFeatureSet::features() const noexcept {
  return featureValues.values();
}
void ResolvedFeatureSet::encode(identity::CanonicalEncoder& encoder) const {
  baseValue.encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(domainValue));
  featureValues.encode(encoder);
}

ResolutionOutput::ResolutionOutput(zc::Vector<ResolvedPackageRecord>&& packages,
                                   zc::Vector<identity::PackageDependencyEdgeKey>&& edges,
                                   zc::Vector<ResolvedFeatureSet>&& featureSets,
                                   VerifiedLockGraph&& lockGraph) noexcept
    : packageValues(zc::mv(packages)),
      edgeValues(zc::mv(edges)),
      featureSetValues(zc::mv(featureSets)),
      lockGraphValue(zc::mv(lockGraph)) {}
zc::OneOf<ResolutionOutput, ResolutionOutputIssue> ResolutionOutput::from(
    zc::MemoryResource& resource, zc::Vector<ResolvedPackageRecord>&& packages,
    zc::Vector<identity::PackageDependencyEdgeKey>&& edges,
    zc::Vector<ResolvedFeatureSet>&& featureSets, VerifiedLockGraph&& lockGraph) {
  canonicalSort(resource, packages);
  canonicalSort(resource, edges);
  canonicalSort(resource, featureSets);
  if (packages.size() == 0) { return ResolutionOutputIssue::EmptyPackages; }
  zc::Vector<zc::Array<uint8_t>> packageKeys(resource);
  for (const auto& package : packages) { packageKeys.add(encoded(resource, package.key())); }
  sortByteArrays(packageKeys);
  for (size_t index = 1; index < packages.size(); ++index) {
    if (packageKeys[index - 1].asPtr() == packageKeys[index].asPtr()) {
      return ResolutionOutputIssue::DuplicatePackage;
    }
  }
  for (size_t index = 1; index < edges.size(); ++index) {
    if (encoded(resource, edges[index - 1]).asPtr() == encoded(resource, edges[index]).asPtr()) {
      return ResolutionOutputIssue::DuplicateEdge;
    }
  }
  zc::Vector<zc::Array<uint8_t>> activationKeys(resource);
  zc::Vector<zc::Array<uint8_t>> featurePackageKeys(resource);
  for (const auto& featureSet : featureSets) {
    identity::CanonicalEncoder activation(resource);
    featureSet.base().encode(activation);
    activation.encodeUint8(static_cast<uint8_t>(featureSet.domain()));
    activationKeys.add(activation.finish());
    auto name = identity::PackageName::fromCanonical(resource, featureSet.base().name());
    auto version = identity::ResolvedVersion::fromCanonical(resource, featureSet.base().version());
    ZC_IF_SOME(nameValue, name) {
      ZC_IF_SOME(versionValue, version) {
        featurePackageKeys.add(encoded(
            resource, identity::PackageKey::from(featureSet.base().source().clone(resource),
                                                 zc::mv(nameValue), zc::mv(versionValue),
                                                 sortedFeatures(resource, featureSet.features()))));
      }
    }
  }
  sortByteArrays(activationKeys);
  sortByteArrays(featurePackageKeys);
  for (size_t index = 1; index < activationKeys.size(); ++index) {
    if (activationKeys[index - 1].asPtr() == activationKeys[index].asPtr()) {
      return ResolutionOutputIssue::DuplicateFeatureSet;
    }
  }
  auto containsBytes = [](zc::ArrayPtr<const zc::Array<uint8_t>> values,
                          zc::ArrayPtr<const uint8_t> key) {
    size_t lower = 0;
    size_t upper = values.size();
    while (lower < upper) {
      const size_t middle = lower + (upper - lower) / 2;
      if (values[middle].asPtr() < key) {
        lower = middle + 1;
      } else {
        upper = middle;
      }
    }
    return lower < values.size() && values[lower].asPtr() == key;
  };
  for (const auto& edge : edges) {
    if (!containsBytes(packageKeys, encoded(resource, edge.consumer())) ||
        !containsBytes(packageKeys, encoded(resource, edge.provider()))) {
      return ResolutionOutputIssue::DanglingEdge;
    }
  }
  for (const auto& key : featurePackageKeys) {
    if (!containsBytes(packageKeys, key)) { return ResolutionOutputIssue::MissingPackageRecord; }
  }
  for (const auto& key : packageKeys) {
    if (!containsBytes(featurePackageKeys, key)) {
      return ResolutionOutputIssue::MissingPackageRecord;
    }
  }
  if (lockGraph.packages().size() != packages.size() || lockGraph.edges().size() != edges.size()) {
    return ResolutionOutputIssue::LockGraphMismatch;
  }
  for (const auto& package : packages) {
    const auto key = encoded(resource, package.key());
    size_t lower = 0;
    size_t upper = lockGraph.packages().size();
    while (lower < upper) {
      const size_t middle = lower + (upper - lower) / 2;
      const auto middleBytes = encoded(resource, lockGraph.packages()[middle].key());
      const zc::ArrayPtr<const uint8_t> middleKey = middleBytes;
      if (middleKey < key.asPtr()) {
        lower = middle + 1;
      } else {
        upper = middle;
      }
    }
    if (lower == lockGraph.packages().size()) { return ResolutionOutputIssue::LockGraphMismatch; }
    const auto& locked = lockGraph.packages()[lower];
    if (encoded(resource, locked.key()).asPtr() != key.asPtr() ||
        package.manifestDigest() != locked.manifestDigest() ||
        package.sourceTreeDigest() != locked.sourceTreeDigest()) {
      return ResolutionOutputIssue::LockGraphMismatch;
    }
  }
  for (size_t index = 0; index < edges.size(); ++index) {
    if (encoded(resource, edges[index]).asPtr() !=
        encoded(resource, lockGraph.edges()[index]).asPtr()) {
      return ResolutionOutputIssue::LockGraphMismatch;
    }
  }
  return ResolutionOutput(zc::mv(packages), zc::mv(edges), zc::mv(featureSets), zc::mv(lockGraph));
}
zc::ArrayPtr<const ResolvedPackageRecord> ResolutionOutput::packages() const noexcept {
  return packageValues;
}
zc::ArrayPtr<const identity::PackageDependencyEdgeKey> ResolutionOutput::edges() const noexcept {
  return edgeValues;
}
zc::ArrayPtr<const ResolvedFeatureSet> ResolutionOutput::featureSets() const noexcept {
  return featureSetValues;
}
const VerifiedLockGraph& ResolutionOutput::lockGraph() const noexcept { return lockGraphValue; }
void ResolutionOutput::encode(identity::CanonicalEncoder& encoder) const {
  for (uint8_t value : "zom.resolution-output"_zc.asBytes()) { encoder.encodeUint8(value); }
  encoder.encodeUint8(0);
  encoder.encodeSequenceSize(packageValues.size());
  for (const auto& package : packageValues) { package.encode(encoder); }
  encoder.encodeSequenceSize(edgeValues.size());
  for (const auto& edge : edgeValues) { edge.encode(encoder); }
  encoder.encodeSequenceSize(featureSetValues.size());
  for (const auto& featureSet : featureSetValues) { featureSet.encode(encoder); }
  lockGraphValue.encode(encoder);
}
zc::Array<uint8_t> ResolutionOutput::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}
zc::Array<uint8_t> ResolutionOutput::encode(zc::MemoryResource& resource) const {
  identity::CanonicalEncoder encoder(resource);
  encode(encoder);
  return encoder.finish();
}

namespace {

struct ResolutionParts final {
  ResolutionParts(zc::Vector<ResolvedPackageRecord>&& packages,
                  zc::Vector<identity::PackageDependencyEdgeKey>&& edges,
                  zc::Vector<ResolvedFeatureSet>&& featureSets,
                  VerifiedLockGraph&& lockGraph) noexcept
      : packages(zc::mv(packages)),
        edges(zc::mv(edges)),
        featureSets(zc::mv(featureSets)),
        lockGraph(zc::mv(lockGraph)) {}

  zc::Vector<ResolvedPackageRecord> packages;
  zc::Vector<identity::PackageDependencyEdgeKey> edges;
  zc::Vector<ResolvedFeatureSet> featureSets;
  VerifiedLockGraph lockGraph;
};

zc::OneOf<ResolutionParts, PackageResolverFailure> buildResolutionParts(
    zc::MemoryResource& resource, zc::ArrayPtr<const Selection> selections,
    const Analysis& analysis, zc::ArrayPtr<const ResolverRelease> releases) {
  auto cycle = detectDependencyCycle(resource, selections, analysis.edges);
  ZC_IF_SOME(value, cycle) { return zc::mv(value); }
  struct PackageCandidate final {
    zc::Array<uint8_t> keyBytes;
    size_t selectionIndex;
    identity::SortedFeatureSet features;

    void encode(identity::CanonicalEncoder& encoder) const {
      for (uint8_t value : keyBytes) { encoder.encodeUint8(value); }
    }
  };
  zc::Vector<PackageCandidate> candidates(resource);
  zc::Vector<ResolvedFeatureSet> featureSets(resource);
  for (const auto& activation : analysis.activations) {
    const size_t selectionIndex = findSelection(selections, activation.coordinate);
    if (selectionIndex == selections.size()) {
      return failure(resource, ResolverIssue::InvalidResolutionOutput, activation.coordinate);
    }
    const auto& selection = selections[selectionIndex];
    const auto& release = releases[selection.releaseIndex];
    auto features = sortedFeatures(
        resource, featuresFor(resource, analysis, selection.coordinate, activation.domain));
    auto featureSet = ResolvedFeatureSet::from(release.base().clone(resource), activation.domain,
                                               features.clone(resource));
    if (featureSet == zc::none) {
      return failure(resource, ResolverIssue::InvalidResolutionOutput, selection.coordinate);
    }
    ZC_IF_SOME(value, featureSet) { featureSets.add(zc::mv(value)); }
    auto key = packageKey(resource, release, features.values());
    candidates.add(PackageCandidate{encoded(resource, key), selectionIndex, zc::mv(features)});
  }
  canonicalSort(resource, candidates);
  zc::Vector<ResolvedPackageRecord> uniquePackages(resource);
  zc::Vector<LockPackageRecord> uniqueLockPackages(resource);
  for (size_t candidateIndex = 0; candidateIndex < candidates.size(); ++candidateIndex) {
    const auto& candidate = candidates[candidateIndex];
    if (candidateIndex != 0 &&
        candidates[candidateIndex - 1].keyBytes.asPtr() == candidate.keyBytes.asPtr()) {
      continue;
    }
    const auto& selection = selections[candidate.selectionIndex];
    const auto& release = releases[selection.releaseIndex];
    auto key = packageKey(resource, release, candidate.features.values());
    zc::Maybe<identity::TargetName> libraryTarget;
    ZC_IF_SOME(library, release.manifest().library()) {
      auto target = identity::TargetName::fromCanonical(resource, library.name());
      if (target == zc::none) {
        return failure(resource, ResolverIssue::InvalidResolutionOutput, selection.coordinate);
      }
      ZC_IF_SOME(value, target) { libraryTarget = zc::mv(value); }
    }
    auto record = ResolvedPackageRecord::from(
        resource, key.clone(resource), release.manifest().clone(resource), release.manifestDigest(),
        release.sourceTreeDigest(),
        SourceViewKey::from(release.base().source().clone(resource), release.sourceTreeDigest()),
        zc::mv(libraryTarget));
    if (record == zc::none) {
      return failure(resource, ResolverIssue::InvalidResolutionOutput, selection.coordinate);
    }
    zc::Maybe<ArchiveFormat> archiveFormat;
    zc::Maybe<identity::Sha256Digest> archiveDigest;
    zc::Maybe<SigningKeyId> signingKey;
    if (release.hasArchive()) {
      archiveFormat = release.archiveFormat();
      archiveDigest = release.archiveDigest();
      signingKey = SigningKeyId::fromDigest(release.signingKey().digest());
    }
    auto lockPackage =
        LockPackageRecord::from(zc::mv(key), release.manifestDigest(), release.sourceTreeDigest(),
                                zc::mv(archiveFormat), zc::mv(archiveDigest), zc::mv(signingKey));
    if (lockPackage == zc::none) {
      return failure(resource, ResolverIssue::InvalidResolutionOutput, selection.coordinate);
    }
    ZC_IF_SOME(value, record) { uniquePackages.add(zc::mv(value)); }
    ZC_IF_SOME(value, lockPackage) { uniqueLockPackages.add(zc::mv(value)); }
  }

  zc::Vector<identity::PackageDependencyEdgeKey> edges(resource);
  for (const auto& fact : analysis.edges) {
    const size_t consumerSelection = findSelection(selections, fact.consumer);
    const size_t providerSelection = findSelection(selections, fact.provider);
    if (consumerSelection == selections.size() || providerSelection == selections.size()) {
      return failure(resource, ResolverIssue::InvalidResolutionOutput, fact.provider);
    }
    const auto& consumer = releases[selections[consumerSelection].releaseIndex];
    const auto& provider = releases[selections[providerSelection].releaseIndex];
    if (!provider.manifest().hasLibrary()) {
      return failure(resource, ResolverIssue::DependencyLibraryTargetMissing, fact.provider);
    }
    auto alias = identity::DependencyAlias::fromCanonical(resource, fact.alias);
    ZC_IF_SOME(aliasValue, alias) {
      auto edge = identity::PackageDependencyEdgeKey::from(
          packageKey(resource, consumer,
                     featuresFor(resource, analysis, fact.consumer, fact.consumerActivation)),
          zc::mv(aliasValue), fact.domain,
          packageKey(resource, provider,
                     featuresFor(resource, analysis, fact.provider, fact.providerActivation)));
      ZC_IF_SOME(value, edge) { edges.add(zc::mv(value)); }
    }
  }
  canonicalSort(resource, edges);
  zc::Vector<identity::PackageDependencyEdgeKey> uniqueEdges(resource);
  zc::Array<uint8_t> previousEdge;
  for (auto& edge : edges) {
    const auto edgeBytes = encoded(resource, edge);
    if (previousEdge != nullptr && previousEdge.asPtr() == edgeBytes.asPtr()) { continue; }
    previousEdge = copyBytes(resource, edgeBytes);
    uniqueEdges.add(zc::mv(edge));
  }
  zc::Vector<identity::PackageDependencyEdgeKey> lockEdges(resource);
  for (const auto& edge : uniqueEdges) { lockEdges.add(edge.clone(resource)); }
  auto lockGraph = VerifiedLockGraph::from(resource, zc::mv(uniqueLockPackages), zc::mv(lockEdges));
  if (lockGraph.is<LockIssue>()) {
    return failure(resource, ResolverIssue::InvalidResolutionOutput, {});
  }
  return ResolutionParts{zc::mv(uniquePackages), zc::mv(uniqueEdges), zc::mv(featureSets),
                         zc::mv(lockGraph.get<VerifiedLockGraph>())};
}

}  // namespace

ResolutionResult PackageResolver::resolve(zc::MemoryResource& resource,
                                          zc::ArrayPtr<const ResolverRoot> roots,
                                          zc::ArrayPtr<const ResolverRelease> releases) {
  auto releaseGroups = buildReleaseGroups(resource, releases);
  zc::Vector<Selection> selections(resource);
  for (const auto& root : roots) {
    size_t found = releases.size();
    for (size_t index = 0; index < releases.size(); ++index) {
      if (encoded(resource, releases[index].base()).asPtr() ==
          encoded(resource, root.base()).asPtr()) {
        found = index;
        break;
      }
    }
    if (found == releases.size()) {
      return failure(resource, ResolverIssue::InvalidRoot, coordinateBytes(resource, root.base()));
    }
    const auto coordinate = coordinateBytes(resource, root.base());
    if (findSelection(selections, coordinate) == selections.size()) {
      addSelection(selections, Selection{coordinateBytes(resource, root.base()), found});
    }
  }

  zc::Maybe<Analysis> solved;
  zc::Maybe<PackageResolverFailure> lastFailure;
  if (!solveGreedy(resource, roots, releases, releaseGroups, selections, solved)) {
    selections.clear();
    for (const auto& root : roots) {
      size_t found = releases.size();
      for (size_t index = 0; index < releases.size(); ++index) {
        if (encoded(resource, releases[index].base()).asPtr() ==
            encoded(resource, root.base()).asPtr()) {
          found = index;
          break;
        }
      }
      if (found == releases.size()) {
        return failure(resource, ResolverIssue::InvalidRoot,
                       coordinateBytes(resource, root.base()));
      }
      const auto coordinate = coordinateBytes(resource, root.base());
      if (findSelection(selections, coordinate) == selections.size()) {
        addSelection(selections, Selection{coordinateBytes(resource, root.base()), found});
      }
    }
    solved = zc::none;
  }
  if (solved == zc::none &&
      !solve(resource, roots, releases, releaseGroups, selections, solved, lastFailure)) {
    ZC_IF_SOME(value, lastFailure) { return zc::mv(value); }
    return failure(resource, ResolverIssue::NoVersionSatisfiesConstraints, {});
  }
  ZC_IF_SOME(analysis, solved) {
    auto parts = buildResolutionParts(resource, selections, analysis, releases);
    if (parts.is<PackageResolverFailure>()) { return zc::mv(parts.get<PackageResolverFailure>()); }
    auto& value = parts.get<ResolutionParts>();
    auto output = ResolutionOutput::from(resource, zc::mv(value.packages), zc::mv(value.edges),
                                         zc::mv(value.featureSets), zc::mv(value.lockGraph));
    if (output.is<ResolutionOutputIssue>()) {
      return failure(resource, ResolverIssue::InvalidResolutionOutput, {});
    }
    return zc::mv(output.get<ResolutionOutput>());
  }
  ZC_UNREACHABLE
}

ResolutionResult PackageResolver::resolve(zc::MemoryResource& resource,
                                          zc::ArrayPtr<const ResolverRoot> roots,
                                          zc::ArrayPtr<const ResolverRelease> releases,
                                          PackageResolverMetrics& metrics) {
  metrics = {};
  auto result = resolve(resource, roots, releases);
  if (result.is<ResolutionOutput>()) {
    const auto& resolution = result.get<ResolutionOutput>();
    metrics.selectedPackages = resolution.packages().size();
    metrics.emittedEdges = resolution.edges().size();
    zc::Array<uint8_t> previousCoordinate;
    for (const auto& featureSet : resolution.featureSets()) {
      auto coordinate = coordinateBytes(resource, featureSet.base());
      if (previousCoordinate == nullptr || previousCoordinate.asPtr() != coordinate.asPtr()) {
        ++metrics.decisions;
        previousCoordinate = zc::mv(coordinate);
      }
    }
  }
  return result;
}

ResolutionResult PackageResolver::resolveLocked(zc::MemoryResource& resource,
                                                zc::ArrayPtr<const ResolverRoot> roots,
                                                zc::ArrayPtr<const ResolverRelease> releases,
                                                const VerifiedLockGraph& locked,
                                                LockReplayMetrics& metrics) {
  metrics = {};
  auto releaseGroups = buildReleaseGroups(resource, releases);
  zc::Vector<EncodedOrderEntry> releaseOrder(resource);
  for (size_t index = 0; index < releases.size(); ++index) {
    releaseOrder.add(EncodedOrderEntry{encoded(resource, releases[index].base()), index});
  }
  canonicalSort(resource, releaseOrder);
  for (size_t index = 1; index < releaseOrder.size(); ++index) {
    if (releaseOrder[index - 1].key.asPtr() == releaseOrder[index].key.asPtr()) {
      return failure(resource, ResolverIssue::LockInputMismatch, releaseOrder[index].key);
    }
  }
  zc::Vector<Selection> selections(resource);
  for (const auto& lockedPackage : locked.packages()) {
    ++metrics.packageVisits;
    const auto base = packageBaseKey(resource, lockedPackage.key());
    const auto key = encoded(resource, base);
    size_t lower = 0;
    size_t upper = releaseOrder.size();
    while (lower < upper) {
      const size_t middle = lower + (upper - lower) / 2;
      const zc::ArrayPtr<const uint8_t> middleKey = releaseOrder[middle].key;
      if (middleKey < key.asPtr()) {
        lower = middle + 1;
      } else {
        upper = middle;
      }
    }
    if (lower == releaseOrder.size() || releaseOrder[lower].key.asPtr() != key.asPtr()) {
      return failure(resource, ResolverIssue::LockInputMismatch, key);
    }
    const auto& release = releases[releaseOrder[lower].index];
    if (release.manifestDigest() != lockedPackage.manifestDigest() ||
        release.sourceTreeDigest() != lockedPackage.sourceTreeDigest() ||
        release.hasArchive() != lockedPackage.hasArchive()) {
      return failure(resource, ResolverIssue::LockInputMismatch, key);
    }
    if (release.hasArchive() &&
        (release.archiveFormat() != lockedPackage.archiveFormat() ||
         release.archiveDigest() != lockedPackage.archiveDigest() ||
         release.signingKey().digest() != lockedPackage.signingKey().digest())) {
      return failure(resource, ResolverIssue::LockInputMismatch, key);
    }
    const auto coordinate = coordinateBytes(resource, release.base());
    if (findSelection(selections, coordinate) == selections.size()) {
      addSelection(selections,
                   Selection{copyBytes(resource, coordinate), releaseOrder[lower].index});
    }
  }
  metrics.edgeVisits = locked.edges().size();
  zc::Maybe<Analysis> previous;
  auto analyzed = analyze(resource, roots, releases, releaseGroups, selections, previous);
  if (analyzed.is<PackageResolverFailure>()) {
    return zc::mv(analyzed.get<PackageResolverFailure>());
  }
  auto parts = buildResolutionParts(resource, selections, analyzed.get<Analysis>(), releases);
  if (parts.is<PackageResolverFailure>()) { return zc::mv(parts.get<PackageResolverFailure>()); }
  auto& value = parts.get<ResolutionParts>();
  auto output = ResolutionOutput::from(resource, zc::mv(value.packages), zc::mv(value.edges),
                                       zc::mv(value.featureSets), zc::mv(value.lockGraph));
  if (output.is<ResolutionOutputIssue>()) {
    return failure(resource, ResolverIssue::InvalidResolutionOutput, {});
  }
  auto& resolution = output.get<ResolutionOutput>();
  if (resolution.lockGraph().encode(resource).asPtr() != locked.encode(resource).asPtr()) {
    return failure(resource, ResolverIssue::LockInputMismatch, {});
  }
  return zc::mv(resolution);
}

}  // namespace zomlang::compiler::driver::package
