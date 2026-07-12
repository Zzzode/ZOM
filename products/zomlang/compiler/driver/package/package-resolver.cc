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
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::driver::package {
namespace {

zc::Array<uint8_t> coordinateBytes(const identity::PackageBaseKey& base) {
  identity::CanonicalEncoder encoder;
  base.source().encode(encoder);
  auto name = identity::PackageName::fromCanonical(base.name());
  ZC_IF_SOME(value, name) { value.encode(encoder); }
  else { ZC_UNREACHABLE }
  return encoder.finish();
}

bool bytesEqual(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  return left == right;
}

zc::Array<uint8_t> copyBytes(zc::ArrayPtr<const uint8_t> value) {
  zc::Vector<uint8_t> copy(value.size());
  copy.addAll(value);
  return copy.releaseAsArray();
}

bool addFeature(zc::Vector<identity::FeatureName>& values, zc::StringPtr name) {
  for (const auto& value : values) {
    if (value.text() == name) { return false; }
  }
  auto feature = identity::FeatureName::fromCanonical(name);
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
  zc::Array<uint8_t> lookup;
  zc::Array<uint8_t> coordinate;
  zc::Vector<size_t> candidates;
};

struct ConstraintGroup final {
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

void addCoordinateIndex(zc::Vector<CoordinateIndexEntry>& entries, zc::ArrayPtr<const uint8_t> key,
                        size_t valueIndex) {
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
  zc::Vector<uint8_t> keyCopy(key.size());
  keyCopy.addAll(key);
  entries.add(CoordinateIndexEntry{keyCopy.releaseAsArray(), valueIndex});
  auto current = zc::mv(entries.back());
  for (size_t index = entries.size() - 1; index > insertion; --index) {
    entries[index] = zc::mv(entries[index - 1]);
  }
  entries[insertion] = zc::mv(current);
}

zc::Array<uint8_t> activationKey(zc::ArrayPtr<const uint8_t> coordinate,
                                 FeatureActivationDomain domain) {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(coordinate);
  encoder.encodeUint8(static_cast<uint8_t>(domain));
  return encoder.finish();
}

size_t findActivation(const Analysis& analysis, zc::ArrayPtr<const uint8_t> coordinate,
                      FeatureActivationDomain domain) {
  const auto key = activationKey(coordinate, domain);
  const size_t index = findCoordinateIndex(analysis.activationIndex, key);
  return index == zc::maxValue ? analysis.activations.size() : index;
}

bool eligible(const ResolverRelease& release, zc::ArrayPtr<const SemVerConstraint> constraints) {
  if (release.yanked()) { return false; }
  auto version = identity::ResolvedVersion::fromCanonical(release.base().version());
  ZC_IF_SOME(value, version) {
    for (const auto& constraint : constraints) {
      if (!constraint.allows(value)) { return false; }
    }
    return true;
  }
  ZC_UNREACHABLE
}

void sortCandidates(zc::Vector<size_t>& candidates, zc::ArrayPtr<const ResolverRelease> releases) {
  for (size_t index = 1; index < candidates.size(); ++index) {
    const size_t current = candidates[index];
    size_t insertion = index;
    auto currentVersion =
        identity::ResolvedVersion::fromCanonical(releases[current].base().version());
    ZC_IF_SOME(currentValue, currentVersion) {
      while (insertion != 0) {
        auto previousVersion = identity::ResolvedVersion::fromCanonical(
            releases[candidates[insertion - 1]].base().version());
        bool movePrevious = false;
        ZC_IF_SOME(previousValue, previousVersion) {
          if (previousValue < currentValue) {
            movePrevious = true;
          } else if (!(currentValue < previousValue)) {
            identity::CanonicalEncoder currentEncoder;
            identity::CanonicalEncoder previousEncoder;
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

zc::Array<uint8_t> releaseLookupBytes(const ResolverRelease& release) {
  identity::CanonicalEncoder encoder;
  release.acceptedSource().encode(encoder);
  auto name = identity::PackageName::fromCanonical(release.base().name());
  ZC_IF_SOME(value, name) { value.encode(encoder); }
  else { ZC_UNREACHABLE }
  return encoder.finish();
}

zc::Array<uint8_t> requirementLookupBytes(const DependencyRequirementWithoutOrigin& requirement) {
  identity::CanonicalEncoder encoder;
  requirement.source().encode(encoder);
  auto name = identity::PackageName::fromCanonical(requirement.requiredPackage());
  ZC_IF_SOME(value, name) { value.encode(encoder); }
  else { ZC_UNREACHABLE }
  return encoder.finish();
}

zc::Vector<ReleaseGroup> sortReleaseGroups(zc::Vector<ReleaseGroup>&& input) {
  if (input.size() < 2) { return zc::mv(input); }
  const size_t middle = input.size() / 2;
  zc::Vector<ReleaseGroup> left;
  zc::Vector<ReleaseGroup> right;
  for (size_t index = 0; index < input.size(); ++index) {
    if (index < middle) {
      left.add(zc::mv(input[index]));
    } else {
      right.add(zc::mv(input[index]));
    }
  }
  left = sortReleaseGroups(zc::mv(left));
  right = sortReleaseGroups(zc::mv(right));
  zc::Vector<ReleaseGroup> result;
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

zc::Vector<ReleaseGroup> buildReleaseGroups(zc::ArrayPtr<const ResolverRelease> releases) {
  zc::Vector<ReleaseGroup> entries;
  for (size_t releaseIndex = 0; releaseIndex < releases.size(); ++releaseIndex) {
    zc::Vector<size_t> candidates;
    candidates.add(releaseIndex);
    entries.add(ReleaseGroup{releaseLookupBytes(releases[releaseIndex]),
                             coordinateBytes(releases[releaseIndex].base()), zc::mv(candidates)});
  }
  entries = sortReleaseGroups(zc::mv(entries));
  zc::Vector<ReleaseGroup> groups;
  for (auto& entry : entries) {
    if (groups.size() == 0 || groups.back().lookup.asPtr() != entry.lookup.asPtr()) {
      groups.add(zc::mv(entry));
      continue;
    }
    ZC_IREQUIRE(groups.back().coordinate.asPtr() == entry.coordinate.asPtr(),
                "one accepted source must identify one package coordinate");
    groups.back().candidates.addAll(entry.candidates);
  }
  for (auto& group : groups) { sortCandidates(group.candidates, releases); }
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

PackageResolverFailure failure(ResolverIssue issue, zc::ArrayPtr<const uint8_t> coordinate,
                               zc::ArrayPtr<const zc::Array<uint8_t>> causes = {}) {
  zc::Vector<uint8_t> coordinateCopy(coordinate.size());
  coordinateCopy.addAll(coordinate);
  zc::Vector<zc::Array<uint8_t>> causeCopies;
  for (const auto& cause : causes) {
    zc::Vector<uint8_t> copy(cause.size());
    copy.addAll(cause);
    causeCopies.add(copy.releaseAsArray());
  }
  sortByteArrays(causeCopies);
  return PackageResolverFailure::from(issue, coordinateCopy.releaseAsArray(), zc::mv(causeCopies));
}

identity::Sha256Digest incompatibilityId(zc::ArrayPtr<const uint8_t> record) {
  identity::Sha256Hasher hasher;
  if (!hasher.update("zom.incompatibility.v0"_zc.asBytes())) { ZC_UNREACHABLE }
  const uint8_t separator = 0;
  if (!hasher.update(zc::arrayPtr(separator)) || !hasher.update(record)) { ZC_UNREACHABLE }
  auto digest = hasher.finish();
  ZC_IF_SOME(value, digest) { return value; }
  ZC_UNREACHABLE
}

IncompatibilityRecord dependencyIncompatibility(zc::ArrayPtr<const uint8_t> coordinate,
                                                const SemVerConstraint& constraint,
                                                zc::ArrayPtr<const uint8_t> cause) {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(1);
  encoder.encodeByteString(coordinate);
  encoder.encodeBool(true);
  constraint.encode(encoder);
  encoder.encodeUint8(0x02);
  encoder.encodeByteString(cause);
  auto bytes = encoder.finish();
  return IncompatibilityRecord::from(incompatibilityId(bytes), zc::mv(bytes));
}

IncompatibilityRecord noVersionsIncompatibility(zc::ArrayPtr<const uint8_t> coordinate) {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(0);
  encoder.encodeUint8(0x03);
  encoder.encodeByteString(coordinate);
  auto bytes = encoder.finish();
  return IncompatibilityRecord::from(incompatibilityId(bytes), zc::mv(bytes));
}

IncompatibilityRecord derivedIncompatibility(const identity::Sha256Digest& left,
                                             const identity::Sha256Digest& right) {
  const auto& first = right.bytes() < left.bytes() ? right : left;
  const auto& second = right.bytes() < left.bytes() ? left : right;
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(0);
  encoder.encodeUint8(0x04);
  encoder.encodeDigest(first);
  encoder.encodeDigest(second);
  auto bytes = encoder.finish();
  return IncompatibilityRecord::from(incompatibilityId(bytes), zc::mv(bytes));
}

IncompatibilityGraph incompatibilityGraph(const ConstraintGroup& group) {
  zc::Vector<IncompatibilityRecord> dependencyRecords;
  for (size_t index = 0; index < group.constraints.size(); ++index) {
    const zc::ArrayPtr<const uint8_t> cause =
        index < group.causes.size() ? group.causes[index].asPtr() : zc::ArrayPtr<const uint8_t>();
    dependencyRecords.add(
        dependencyIncompatibility(group.coordinate, group.constraints[index], cause));
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

  zc::Vector<IncompatibilityRecord> records;
  records.add(noVersionsIncompatibility(group.coordinate));
  identity::Sha256Digest root = records.back().id();
  for (auto& dependency : dependencyRecords) {
    const identity::Sha256Digest dependencyId = dependency.id();
    records.add(zc::mv(dependency));
    auto derived = derivedIncompatibility(root, dependencyId);
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

PackageResolverFailure conflictFailure(const ConstraintGroup& group) {
  zc::Vector<zc::Array<uint8_t>> causes;
  for (const auto& cause : group.causes) { causes.add(copyBytes(cause)); }
  sortByteArrays(causes);
  return PackageResolverFailure::withIncompatibility(ResolverIssue::NoVersionSatisfiesConstraints,
                                                     copyBytes(group.coordinate), zc::mv(causes),
                                                     incompatibilityGraph(group));
}

bool appendActivation(Analysis& analysis, zc::ArrayPtr<const uint8_t> coordinate,
                      FeatureActivationDomain domain,
                      zc::ArrayPtr<const identity::FeatureName> requested, bool useDefaultFeatures,
                      bool includeDevelopment) {
  size_t index = findActivation(analysis, coordinate, domain);
  if (index == analysis.activations.size()) {
    zc::Vector<uint8_t> coordinateCopy(coordinate.size());
    coordinateCopy.addAll(coordinate);
    zc::Vector<identity::FeatureName> features;
    for (const auto& feature : requested) { addFeature(features, feature.text()); }
    analysis.activations.add(Activation{coordinateCopy.releaseAsArray(),
                                        domain,
                                        zc::mv(features),
                                        useDefaultFeatures,
                                        includeDevelopment,
                                        {},
                                        false});
    const auto key = activationKey(coordinate, domain);
    addCoordinateIndex(analysis.activationIndex, key, analysis.activations.size() - 1);
    return true;
  }
  bool changed = false;
  for (const auto& feature : requested) {
    changed = addFeature(analysis.activations[index].requested, feature.text()) || changed;
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

AnalysisResult analyze(zc::ArrayPtr<const ResolverRoot> roots,
                       zc::ArrayPtr<const ResolverRelease> releases,
                       zc::ArrayPtr<const ReleaseGroup> releaseGroups,
                       zc::ArrayPtr<const Selection> selections, zc::Maybe<Analysis>& previous) {
  Analysis analysis;
  ZC_IF_SOME(value, previous) {
    analysis = zc::mv(value);
    previous = zc::none;
  }
  else {
    for (const auto& root : roots) {
      const auto coordinate = coordinateBytes(root.base());
      const size_t selectionIndex = findSelection(selections, coordinate);
      if (selectionIndex == selections.size() ||
          releases[selections[selectionIndex].releaseIndex].base().encode().asPtr() !=
              root.base().encode().asPtr()) {
        return failure(ResolverIssue::InvalidRoot, coordinate);
      }
      ConstraintGroup group{coordinateBytes(root.base()), {}, {}, {}};
      group.candidates.add(selections[selectionIndex].releaseIndex);
      analysis.groups.add(zc::mv(group));
      addCoordinateIndex(analysis.groupIndex, coordinate, analysis.groups.size() - 1);
      appendActivation(analysis, coordinate, FeatureActivationDomain::Target,
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
      auto sortedRequestedInput = zc::Vector<identity::FeatureName>();
      for (const auto& feature : activation.requested) {
        auto cloned = identity::FeatureName::fromCanonical(feature.text());
        ZC_IF_SOME(value, cloned) { sortedRequestedInput.add(zc::mv(value)); }
      }
      auto sortedRequested = identity::SortedFeatureSet::from(zc::mv(sortedRequestedInput));
      ZC_IF_SOME(requested, sortedRequested) {
        auto expansion = FeatureResolver::expand(release.manifest(), activation.domain,
                                                 requested.values(), activation.useDefaultFeatures);
        if (expansion.template is<FeatureIssue>()) {
          zc::Vector<zc::Array<uint8_t>> causes;
          identity::CanonicalEncoder causeEncoder;
          causeEncoder.encodeUint8(static_cast<uint8_t>(expansion.template get<FeatureIssue>()));
          causes.add(causeEncoder.finish());
          return failure(ResolverIssue::FeatureInvalid, activation.coordinate, causes);
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
          addFeature(activation.expanded, feature.text());
        }
        activation.processed = true;
        auto consumerCoordinate = copyBytes(activation.coordinate);
        const auto activationDomain = activation.domain;
        const bool includeDevelopment = activation.includeDevelopment;

        auto processRequirements =
            [&](zc::ArrayPtr<const DependencyRequirementWithoutOrigin> values,
                identity::DependencyDomain emittedDomain,
                FeatureActivationDomain providerDomain) -> zc::Maybe<PackageResolverFailure> {
          for (const auto& requirement : values) {
            bool activated = !requirement.optional();
            zc::Vector<identity::FeatureName> requestedFeatures;
            for (const auto& feature : requirement.requestedFeatures()) {
              addFeature(requestedFeatures, feature.text());
            }
            for (const auto& dependency : expanded.activatedDependencies()) {
              if (dependency.alias() != requirement.alias()) { continue; }
              activated = true;
              for (const auto& feature : dependency.requestedFeatures()) {
                addFeature(requestedFeatures, feature.text());
              }
            }
            if (!activated) { continue; }

            const auto lookup = requirementLookupBytes(requirement);
            const size_t releaseGroupIndex = findReleaseGroup(releaseGroups, lookup);
            if (releaseGroupIndex == releaseGroups.size()) {
              identity::CanonicalEncoder requirementEncoder;
              requirement.encode(requirementEncoder);
              auto encoded = requirementEncoder.finish();
              return failure(ResolverIssue::SourceBindingMissing, encoded);
            }
            const auto& releaseGroup = releaseGroups[releaseGroupIndex];
            const auto& providerCoordinate = releaseGroup.coordinate;
            size_t groupIndex = findCoordinateIndex(analysis.groupIndex, providerCoordinate);
            if (groupIndex == zc::maxValue) {
              zc::Vector<size_t> matching;
              for (size_t candidate : releaseGroup.candidates) { matching.add(candidate); }
              analysis.groups.add(
                  ConstraintGroup{copyBytes(providerCoordinate), zc::mv(matching), {}, {}});
              groupIndex = analysis.groups.size() - 1;
              addCoordinateIndex(analysis.groupIndex, providerCoordinate, groupIndex);
              changed = true;
            }
            if (requirement.hasVersionCheck()) {
              auto encoded = requirement.versionCheck().encode();
              bool known = false;
              for (const auto& constraint : analysis.groups[groupIndex].constraints) {
                if (constraint.encode().asPtr() == encoded.asPtr()) {
                  known = true;
                  break;
                }
              }
              if (!known) {
                analysis.groups[groupIndex].constraints.add(requirement.versionCheck().clone());
                changed = true;
              }
              identity::CanonicalEncoder requirementEncoder;
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
            appendActivation(analysis, providerCoordinate, providerDomain, requestedFeatures,
                             requirement.useDefaultFeatures(), false);

            zc::Vector<uint8_t> consumer(consumerCoordinate.size());
            consumer.addAll(consumerCoordinate);
            zc::Vector<uint8_t> provider(providerCoordinate.size());
            provider.addAll(providerCoordinate);
            analysis.edges.add(EdgeFact{consumer.releaseAsArray(), provider.releaseAsArray(),
                                        zc::heapString(requirement.alias()), emittedDomain,
                                        activationDomain, providerDomain});
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
    if (!eligible(selected, group.constraints)) { return conflictFailure(group); }
  }
  return analysis;
}

bool solve(zc::ArrayPtr<const ResolverRoot> roots, zc::ArrayPtr<const ResolverRelease> releases,
           zc::ArrayPtr<const ReleaseGroup> releaseGroups, zc::Vector<Selection>& selections,
           zc::Maybe<Analysis>& solved, zc::Maybe<PackageResolverFailure>& lastFailure) {
  zc::Maybe<Analysis> previous;
  auto result = analyze(roots, releases, releaseGroups, selections, previous);
  if (result.is<PackageResolverFailure>()) {
    lastFailure = zc::mv(result.get<PackageResolverFailure>());
    return false;
  }
  auto analysis = zc::mv(result.get<Analysis>());
  size_t chosenGroup = analysis.groups.size();
  zc::Vector<size_t> chosenEligible;
  for (size_t groupIndex = 0; groupIndex < analysis.groups.size(); ++groupIndex) {
    const auto& group = analysis.groups[groupIndex];
    if (findSelection(selections, group.coordinate) != selections.size()) { continue; }
    zc::Vector<size_t> eligibleCandidates;
    for (size_t candidate : group.candidates) {
      if (eligible(releases[candidate], group.constraints)) { eligibleCandidates.add(candidate); }
    }
    if (eligibleCandidates.size() == 0) {
      lastFailure = conflictFailure(group);
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
  sortCandidates(chosenEligible, releases);
  for (size_t candidate : chosenEligible) {
    zc::Vector<uint8_t> coordinate(analysis.groups[chosenGroup].coordinate.size());
    coordinate.addAll(analysis.groups[chosenGroup].coordinate);
    addSelection(selections, Selection{coordinate.releaseAsArray(), candidate});
    if (solve(roots, releases, releaseGroups, selections, solved, lastFailure)) { return true; }
    removeSelection(selections, analysis.groups[chosenGroup].coordinate);
  }
  return false;
}

bool solveGreedy(zc::ArrayPtr<const ResolverRoot> roots,
                 zc::ArrayPtr<const ResolverRelease> releases,
                 zc::ArrayPtr<const ReleaseGroup> releaseGroups, zc::Vector<Selection>& selections,
                 zc::Maybe<Analysis>& solved) {
  zc::Maybe<Analysis> previous;
  while (true) {
    auto result = analyze(roots, releases, releaseGroups, selections, previous);
    if (result.is<PackageResolverFailure>()) { return false; }
    auto analysis = zc::mv(result.get<Analysis>());
    bool addedSelection = false;
    for (const auto& group : analysis.groups) {
      if (findSelection(selections, group.coordinate) != selections.size()) { continue; }
      zc::Vector<size_t> candidates;
      for (size_t candidate : group.candidates) {
        if (eligible(releases[candidate], group.constraints)) { candidates.add(candidate); }
      }
      if (candidates.size() == 0) { return false; }
      sortCandidates(candidates, releases);
      addSelection(selections, Selection{copyBytes(group.coordinate), candidates[0]});
      addedSelection = true;
    }
    if (!addedSelection) {
      solved = zc::mv(analysis);
      return true;
    }
    previous = zc::mv(analysis);
  }
}

identity::SortedFeatureSet sortedFeatures(zc::ArrayPtr<const identity::FeatureName> features) {
  zc::Vector<identity::FeatureName> copies;
  for (const auto& feature : features) {
    auto copy = identity::FeatureName::fromCanonical(feature.text());
    ZC_IF_SOME(value, copy) { copies.add(zc::mv(value)); }
  }
  auto result = identity::SortedFeatureSet::from(zc::mv(copies));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
}

identity::PackageKey packageKey(const ResolverRelease& release,
                                zc::ArrayPtr<const identity::FeatureName> features) {
  auto name = identity::PackageName::fromCanonical(release.base().name());
  auto version = identity::ResolvedVersion::fromCanonical(release.base().version());
  ZC_IF_SOME(nameValue, name) {
    ZC_IF_SOME(versionValue, version) {
      return identity::PackageKey::from(release.base().source().clone(), zc::mv(nameValue),
                                        zc::mv(versionValue), sortedFeatures(features));
    }
  }
  ZC_UNREACHABLE
}

zc::ArrayPtr<const identity::FeatureName> featuresFor(const Analysis& analysis,
                                                      zc::ArrayPtr<const uint8_t> coordinate,
                                                      FeatureActivationDomain domain) {
  const size_t index = findActivation(analysis, coordinate, domain);
  if (index == analysis.activations.size()) { return {}; }
  return analysis.activations[index].expanded;
}

template <typename Value>
zc::Vector<Value> canonicalMergeSort(zc::Vector<Value>&& input) {
  if (input.size() < 2) { return zc::mv(input); }
  const size_t middle = input.size() / 2;
  zc::Vector<Value> left;
  zc::Vector<Value> right;
  for (size_t index = 0; index < input.size(); ++index) {
    if (index < middle) {
      left.add(zc::mv(input[index]));
    } else {
      right.add(zc::mv(input[index]));
    }
  }
  left = canonicalMergeSort(zc::mv(left));
  right = canonicalMergeSort(zc::mv(right));
  zc::Vector<Value> result;
  size_t leftIndex = 0;
  size_t rightIndex = 0;
  while (leftIndex < left.size() || rightIndex < right.size()) {
    bool takeLeft = rightIndex == right.size();
    if (!takeLeft && leftIndex < left.size()) {
      identity::CanonicalEncoder leftEncoder;
      identity::CanonicalEncoder rightEncoder;
      left[leftIndex].encode(leftEncoder);
      right[rightIndex].encode(rightEncoder);
      takeLeft = leftEncoder.finish().asPtr() < rightEncoder.finish().asPtr();
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
void canonicalSort(zc::Vector<Value>& values) {
  values = canonicalMergeSort(zc::mv(values));
}

zc::Array<uint8_t> edgeFactBytes(const EdgeFact& edge) {
  identity::CanonicalEncoder encoder;
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

bool visitCycle(size_t node, zc::ArrayPtr<const Selection> selections,
                zc::ArrayPtr<const EdgeFact> edges, zc::ArrayPtr<const size_t> edgeOrder,
                zc::Vector<uint8_t>& states, zc::Vector<size_t>& stack,
                zc::Vector<zc::Array<uint8_t>>& cycle) {
  states[node] = 1;
  stack.add(node);
  for (size_t orderedIndex : edgeOrder) {
    const auto& edge = edges[orderedIndex];
    if (edge.consumer.asPtr() != selections[node].coordinate.asPtr()) { continue; }
    const size_t provider = findSelection(selections, edge.provider);
    if (provider == selections.size()) { continue; }
    if (states[provider] == 0 &&
        visitCycle(provider, selections, edges, edgeOrder, states, stack, cycle)) {
      return true;
    }
    if (states[provider] != 1) { continue; }
    size_t start = 0;
    while (start < stack.size() && stack[start] != provider) { ++start; }
    for (size_t index = start; index < stack.size(); ++index) {
      cycle.add(copyBytes(selections[stack[index]].coordinate));
    }
    cycle.add(copyBytes(selections[provider].coordinate));
    return true;
  }
  stack.removeLast();
  states[node] = 2;
  return false;
}

zc::Maybe<PackageResolverFailure> detectDependencyCycle(zc::ArrayPtr<const Selection> selections,
                                                        zc::ArrayPtr<const EdgeFact> edges) {
  zc::Vector<size_t> edgeOrder;
  zc::Vector<EncodedOrderEntry> orderedEdges;
  for (size_t index = 0; index < edges.size(); ++index) {
    orderedEdges.add(EncodedOrderEntry{edgeFactBytes(edges[index]), index});
  }
  canonicalSort(orderedEdges);
  for (const auto& entry : orderedEdges) { edgeOrder.add(entry.index); }
  zc::Vector<size_t> nodeOrder;
  for (size_t index = 0; index < selections.size(); ++index) { nodeOrder.add(index); }
  zc::Vector<uint8_t> states(selections.size());
  for (size_t index = 0; index < selections.size(); ++index) { states.add(0); }
  zc::Vector<size_t> stack;
  for (size_t node : nodeOrder) {
    if (states[node] != 0) { continue; }
    zc::Vector<zc::Array<uint8_t>> cycle;
    if (visitCycle(node, selections, edges, edgeOrder, states, stack, cycle)) {
      return failure(ResolverIssue::DependencyCycle, selections[node].coordinate, cycle);
    }
  }
  return zc::none;
}

}  // namespace

ResolverRelease::ResolverRelease(PackageSourceConstraint&& acceptedSource,
                                 identity::PackageBaseKey&& base,
                                 CanonicalManifestRecord&& manifest, bool yanked) noexcept
    : sourceValue(zc::mv(acceptedSource)),
      baseValue(zc::mv(base)),
      manifestValue(zc::mv(manifest)),
      yankedValue(yanked) {}
ResolverRelease ResolverRelease::from(PackageSourceConstraint&& acceptedSource,
                                      identity::PackageBaseKey&& base,
                                      CanonicalManifestRecord&& manifest, bool yanked) {
  return ResolverRelease(zc::mv(acceptedSource), zc::mv(base), zc::mv(manifest), yanked);
}
ResolverRelease ResolverRelease::fromRegistry(const VerifiedRegistryReleaseRecord& release) {
  auto name = identity::PackageName::fromCanonical(release.package());
  auto version = identity::ResolvedVersion::fromCanonical(release.version());
  ZC_IF_SOME(nameValue, name) {
    ZC_IF_SOME(versionValue, version) {
      return from(PackageSourceConstraint::registry(release.registry().clone()),
                  identity::PackageBaseKey::from(
                      identity::CanonicalPackageSource::registry(release.registry().clone()),
                      zc::mv(nameValue), zc::mv(versionValue)),
                  release.manifest().clone(), release.yanked());
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
ResolverRelease ResolverRelease::fromVcs(const VerifiedVcsPackageRecord& release,
                                         PackageSourceConstraint&& acceptedSelector) {
  ZC_IREQUIRE(acceptedSelector.kind() == PackageSourceConstraintKind::Vcs,
              "VCS resolver record must accept a VCS selector");
  return from(zc::mv(acceptedSelector), release.base().clone(), release.canonicalManifest().clone(),
              false);
}
ResolverRelease ResolverRelease::fromLocal(const LocalPackageRecord& release) {
  const auto& source = release.base().source();
  ZC_IREQUIRE(source.kind() == identity::PackageSourceKind::LocalPath,
              "local resolver record must carry a local package base");
  return from(PackageSourceConstraint::localPath(source.localPath().clone()),
              release.base().clone(), release.canonicalManifest().clone(), false);
}
ResolverRelease ResolverRelease::clone() const {
  return from(sourceValue.clone(), baseValue.clone(), manifestValue.clone(), yankedValue);
}
const PackageSourceConstraint& ResolverRelease::acceptedSource() const noexcept {
  return sourceValue;
}
const identity::PackageBaseKey& ResolverRelease::base() const noexcept { return baseValue; }
const CanonicalManifestRecord& ResolverRelease::manifest() const noexcept { return manifestValue; }
bool ResolverRelease::yanked() const noexcept { return yankedValue; }
void ResolverRelease::encode(identity::CanonicalEncoder& encoder) const {
  sourceValue.encode(encoder);
  baseValue.encode(encoder);
  manifestValue.encode(encoder);
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
  }
  else { encoder.encodeNone(); }
}
zc::Array<uint8_t> PackageResolverFailure::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

ResolvedPackageSelection::ResolvedPackageSelection(identity::PackageBaseKey&& base,
                                                   FeatureActivationDomain domain,
                                                   identity::SortedFeatureSet&& features) noexcept
    : baseValue(zc::mv(base)), domainValue(domain), featureValues(zc::mv(features)) {}
ResolvedPackageSelection ResolvedPackageSelection::from(identity::PackageBaseKey&& base,
                                                        FeatureActivationDomain domain,
                                                        identity::SortedFeatureSet&& features) {
  return ResolvedPackageSelection(zc::mv(base), domain, zc::mv(features));
}
const identity::PackageBaseKey& ResolvedPackageSelection::base() const noexcept {
  return baseValue;
}
FeatureActivationDomain ResolvedPackageSelection::domain() const noexcept { return domainValue; }
zc::ArrayPtr<const identity::FeatureName> ResolvedPackageSelection::features() const noexcept {
  return featureValues.values();
}
identity::PackageKey ResolvedPackageSelection::packageKey() const {
  auto name = identity::PackageName::fromCanonical(baseValue.name());
  auto version = identity::ResolvedVersion::fromCanonical(baseValue.version());
  ZC_IF_SOME(nameValue, name) {
    ZC_IF_SOME(versionValue, version) {
      return identity::PackageKey::from(baseValue.source().clone(), zc::mv(nameValue),
                                        zc::mv(versionValue), featureValues.clone());
    }
  }
  ZC_UNREACHABLE
}
void ResolvedPackageSelection::encode(identity::CanonicalEncoder& encoder) const {
  baseValue.encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(domainValue));
  featureValues.encode(encoder);
}

PackageResolution::PackageResolution(
    zc::Vector<ResolvedPackageSelection>&& packages,
    zc::Vector<identity::PackageDependencyEdgeKey>&& edges) noexcept
    : packageValues(zc::mv(packages)), edgeValues(zc::mv(edges)) {}
PackageResolution PackageResolution::from(zc::Vector<ResolvedPackageSelection>&& packages,
                                          zc::Vector<identity::PackageDependencyEdgeKey>&& edges) {
  return PackageResolution(zc::mv(packages), zc::mv(edges));
}
zc::ArrayPtr<const ResolvedPackageSelection> PackageResolution::packages() const noexcept {
  return packageValues;
}
zc::ArrayPtr<const identity::PackageDependencyEdgeKey> PackageResolution::edges() const noexcept {
  return edgeValues;
}
void PackageResolution::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeByteString("zom.package-resolution.v0"_zc.asBytes());
  encoder.encodeUint8(0);
  encoder.encodeSequenceSize(packageValues.size());
  for (const auto& package : packageValues) { package.encode(encoder); }
  encoder.encodeSequenceSize(edgeValues.size());
  for (const auto& edge : edgeValues) { edge.encode(encoder); }
}
zc::Array<uint8_t> PackageResolution::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

PackageResolutionResult PackageResolver::resolve(zc::ArrayPtr<const ResolverRoot> roots,
                                                 zc::ArrayPtr<const ResolverRelease> releases) {
  auto releaseGroups = buildReleaseGroups(releases);
  zc::Vector<Selection> selections;
  for (const auto& root : roots) {
    size_t found = releases.size();
    for (size_t index = 0; index < releases.size(); ++index) {
      if (releases[index].base().encode().asPtr() == root.base().encode().asPtr()) {
        found = index;
        break;
      }
    }
    if (found == releases.size()) {
      return failure(ResolverIssue::InvalidRoot, coordinateBytes(root.base()));
    }
    const auto coordinate = coordinateBytes(root.base());
    if (findSelection(selections, coordinate) == selections.size()) {
      addSelection(selections, Selection{coordinateBytes(root.base()), found});
    }
  }

  zc::Maybe<Analysis> solved;
  zc::Maybe<PackageResolverFailure> lastFailure;
  if (!solveGreedy(roots, releases, releaseGroups, selections, solved)) {
    selections.clear();
    for (const auto& root : roots) {
      size_t found = releases.size();
      for (size_t index = 0; index < releases.size(); ++index) {
        if (releases[index].base().encode().asPtr() == root.base().encode().asPtr()) {
          found = index;
          break;
        }
      }
      if (found == releases.size()) {
        return failure(ResolverIssue::InvalidRoot, coordinateBytes(root.base()));
      }
      const auto coordinate = coordinateBytes(root.base());
      if (findSelection(selections, coordinate) == selections.size()) {
        addSelection(selections, Selection{coordinateBytes(root.base()), found});
      }
    }
    solved = zc::none;
  }
  if (solved == zc::none &&
      !solve(roots, releases, releaseGroups, selections, solved, lastFailure)) {
    ZC_IF_SOME(value, lastFailure) { return zc::mv(value); }
    return failure(ResolverIssue::NoVersionSatisfiesConstraints, {});
  }
  ZC_IF_SOME(analysis, solved) {
    auto cycle = detectDependencyCycle(selections, analysis.edges);
    ZC_IF_SOME(value, cycle) { return zc::mv(value); }
    zc::Vector<ResolvedPackageSelection> packages;
    for (const auto& selection : selections) {
      const auto& release = releases[selection.releaseIndex];
      for (const auto& activation : analysis.activations) {
        if (activation.coordinate.asPtr() != selection.coordinate.asPtr()) { continue; }
        packages.add(ResolvedPackageSelection::from(
            release.base().clone(), activation.domain,
            sortedFeatures(featuresFor(analysis, selection.coordinate, activation.domain))));
      }
    }
    canonicalSort(packages);

    zc::Vector<identity::PackageDependencyEdgeKey> edges;
    for (const auto& fact : analysis.edges) {
      const size_t consumerSelection = findSelection(selections, fact.consumer);
      const size_t providerSelection = findSelection(selections, fact.provider);
      if (consumerSelection == selections.size() || providerSelection == selections.size()) {
        continue;
      }
      const auto& consumer = releases[selections[consumerSelection].releaseIndex];
      const auto& provider = releases[selections[providerSelection].releaseIndex];
      if (!provider.manifest().hasLibrary()) {
        return failure(ResolverIssue::DependencyLibraryTargetMissing, fact.provider);
      }
      auto alias = identity::DependencyAlias::fromCanonical(fact.alias);
      ZC_IF_SOME(aliasValue, alias) {
        auto edge = identity::PackageDependencyEdgeKey::from(
            packageKey(consumer, featuresFor(analysis, fact.consumer, fact.consumerActivation)),
            zc::mv(aliasValue), fact.domain,
            packageKey(provider, featuresFor(analysis, fact.provider, fact.providerActivation)));
        ZC_IF_SOME(value, edge) { edges.add(zc::mv(value)); }
      }
    }
    canonicalSort(edges);
    zc::Vector<identity::PackageDependencyEdgeKey> uniqueEdges;
    zc::Array<uint8_t> previous;
    for (auto& edge : edges) {
      const auto encoded = edge.encode();
      if (previous != nullptr && previous.asPtr() == encoded.asPtr()) { continue; }
      previous = copyBytes(encoded);
      uniqueEdges.add(zc::mv(edge));
    }
    return PackageResolution::from(zc::mv(packages), zc::mv(uniqueEdges));
  }
  ZC_UNREACHABLE
}

PackageResolutionResult PackageResolver::resolve(zc::ArrayPtr<const ResolverRoot> roots,
                                                 zc::ArrayPtr<const ResolverRelease> releases,
                                                 PackageResolverMetrics& metrics) {
  metrics = {};
  auto result = resolve(roots, releases);
  if (result.is<PackageResolution>()) {
    const auto& resolution = result.get<PackageResolution>();
    metrics.selectedPackages = resolution.packages().size();
    metrics.emittedEdges = resolution.edges().size();
    zc::Array<uint8_t> previousCoordinate;
    for (const auto& package : resolution.packages()) {
      auto coordinate = coordinateBytes(package.base());
      if (previousCoordinate == nullptr || previousCoordinate.asPtr() != coordinate.asPtr()) {
        ++metrics.decisions;
        previousCoordinate = zc::mv(coordinate);
      }
    }
  }
  return result;
}

}  // namespace zomlang::compiler::driver::package
