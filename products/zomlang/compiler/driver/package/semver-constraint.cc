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

#include "zomlang/compiler/driver/package/semver-constraint.h"

#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::driver::package {
namespace {

struct VersionParts final {
  zc::String major;
  zc::String minor;
  zc::String patch;
  zc::String prerelease;
};

enum class ComparatorKind : uint8_t {
  Equal,
  Greater,
  GreaterEqual,
  Less,
  LessEqual,
  Caret,
  Tilde,
};

struct Comparator final {
  ComparatorKind kind;
  identity::ResolvedVersion version;
};

bool hasBuildMetadata(zc::StringPtr version) { return version.findFirst('+') != zc::none; }

VersionParts splitVersion(zc::StringPtr version) {
  size_t firstDot = 0;
  while (version[firstDot] != '.') { ++firstDot; }
  size_t secondDot = firstDot + 1;
  while (version[secondDot] != '.') { ++secondDot; }
  size_t coreEnd = version.size();
  for (size_t index = secondDot + 1; index < version.size(); ++index) {
    if (version[index] == '-' || version[index] == '+') {
      coreEnd = index;
      break;
    }
  }
  size_t prereleaseEnd = version.size();
  for (size_t index = coreEnd + 1; index < version.size(); ++index) {
    if (version[index] == '+') {
      prereleaseEnd = index;
      break;
    }
  }
  zc::String prerelease;
  if (coreEnd < version.size() && version[coreEnd] == '-') {
    prerelease = zc::heapString(version.slice(coreEnd + 1, prereleaseEnd));
  } else {
    prerelease = zc::heapString(""_zc);
  }
  return VersionParts{zc::heapString(version.first(firstDot)),
                      zc::heapString(version.slice(firstDot + 1, secondDot)),
                      zc::heapString(version.slice(secondDot + 1, coreEnd)), zc::mv(prerelease)};
}

int compareDecimal(zc::StringPtr left, zc::StringPtr right) {
  if (left.size() < right.size()) { return -1; }
  if (left.size() > right.size()) { return 1; }
  if (left < right) { return -1; }
  if (right < left) { return 1; }
  return 0;
}

bool numericIdentifier(zc::StringPtr value) {
  for (char byte : value) {
    if (byte < '0' || byte > '9') { return false; }
  }
  return value.size() != 0;
}

int comparePrerelease(zc::StringPtr left, zc::StringPtr right) {
  if (left.size() == 0) { return right.size() == 0 ? 0 : 1; }
  if (right.size() == 0) { return -1; }

  size_t leftStart = 0;
  size_t rightStart = 0;
  while (true) {
    size_t leftEnd = leftStart;
    while (leftEnd < left.size() && left[leftEnd] != '.') { ++leftEnd; }
    size_t rightEnd = rightStart;
    while (rightEnd < right.size() && right[rightEnd] != '.') { ++rightEnd; }
    const zc::String leftIdentifier = zc::heapString(left.slice(leftStart, leftEnd));
    const zc::String rightIdentifier = zc::heapString(right.slice(rightStart, rightEnd));
    const bool leftNumeric = numericIdentifier(leftIdentifier);
    const bool rightNumeric = numericIdentifier(rightIdentifier);
    int result = 0;
    if (leftNumeric && rightNumeric) {
      result = compareDecimal(leftIdentifier, rightIdentifier);
    } else if (leftNumeric != rightNumeric) {
      result = leftNumeric ? -1 : 1;
    } else if (leftIdentifier < rightIdentifier) {
      result = -1;
    } else if (rightIdentifier < leftIdentifier) {
      result = 1;
    }
    if (result != 0) { return result; }

    const bool leftDone = leftEnd == left.size();
    const bool rightDone = rightEnd == right.size();
    if (leftDone || rightDone) {
      if (leftDone && rightDone) { return 0; }
      return leftDone ? -1 : 1;
    }
    leftStart = leftEnd + 1;
    rightStart = rightEnd + 1;
  }
}

int compareVersions(const identity::ResolvedVersion& left, const identity::ResolvedVersion& right) {
  auto leftParts = splitVersion(left.text());
  auto rightParts = splitVersion(right.text());
  int result = compareDecimal(leftParts.major, rightParts.major);
  if (result != 0) { return result; }
  result = compareDecimal(leftParts.minor, rightParts.minor);
  if (result != 0) { return result; }
  result = compareDecimal(leftParts.patch, rightParts.patch);
  if (result != 0) { return result; }
  return comparePrerelease(leftParts.prerelease, rightParts.prerelease);
}

identity::ResolvedVersion resolvedVersion(zc::StringPtr text) {
  auto result = identity::ResolvedVersion::fromCanonical(text);
  ZC_IF_SOME(admitted, result) { return zc::mv(admitted); }
  ZC_IREQUIRE(false, "internally constructed semantic version must be valid");
  ZC_UNREACHABLE
}

SemVerBound bound(identity::ResolvedVersion&& version, bool inclusive) {
  auto result = SemVerBound::from(zc::mv(version), inclusive);
  ZC_IF_SOME(admitted, result) { return zc::mv(admitted); }
  ZC_IREQUIRE(false, "constraint bound must not contain build metadata");
  ZC_UNREACHABLE
}

zc::String incrementDecimal(zc::StringPtr value) {
  bool allNines = true;
  for (char byte : value) {
    if (byte != '9') {
      allNines = false;
      break;
    }
  }
  zc::Vector<char> result(value.size() + 2);
  if (allNines) {
    result.add('1');
    for (size_t index = 0; index < value.size(); ++index) { result.add('0'); }
  } else {
    result.addAll(value);
    for (size_t index = result.size(); index > 0; --index) {
      if (result[index - 1] == '9') {
        result[index - 1] = '0';
      } else {
        ++result[index - 1];
        break;
      }
    }
  }
  result.add('\0');
  return zc::String(result.releaseAsArray());
}

identity::ResolvedVersion releaseVersion(zc::StringPtr major, zc::StringPtr minor,
                                         zc::StringPtr patch) {
  return resolvedVersion(zc::str(major, ".", minor, ".", patch));
}

zc::Maybe<Comparator> parseComparator(zc::StringPtr source) {
  if (source.size() == 0) { return zc::none; }
  ComparatorKind kind = ComparatorKind::Caret;
  size_t prefix = 0;
  if (source.startsWith(">="_zc)) {
    kind = ComparatorKind::GreaterEqual;
    prefix = 2;
  } else if (source.startsWith("<="_zc)) {
    kind = ComparatorKind::LessEqual;
    prefix = 2;
  } else if (source[0] == '=') {
    kind = ComparatorKind::Equal;
    prefix = 1;
  } else if (source[0] == '>') {
    kind = ComparatorKind::Greater;
    prefix = 1;
  } else if (source[0] == '<') {
    kind = ComparatorKind::Less;
    prefix = 1;
  } else if (source[0] == '^') {
    kind = ComparatorKind::Caret;
    prefix = 1;
  } else if (source[0] == '~') {
    kind = ComparatorKind::Tilde;
    prefix = 1;
  }
  if (prefix == source.size()) { return zc::none; }
  auto version = identity::ResolvedVersion::fromCanonical(source.slice(prefix));
  ZC_IF_SOME(admitted, version) {
    if (hasBuildMetadata(admitted.text())) { return zc::none; }
    return Comparator{kind, zc::mv(admitted)};
  }
  return zc::none;
}

void intersectLower(zc::Maybe<SemVerBound>& current, SemVerBound&& candidate) {
  ZC_IF_SOME(existing, current) {
    const auto existingVersion = resolvedVersion(existing.version());
    const auto candidateVersion = resolvedVersion(candidate.version());
    const int order = compareVersions(existingVersion, candidateVersion);
    if (order < 0) {
      current = zc::mv(candidate);
    } else if (order == 0 && existing.inclusive() && !candidate.inclusive()) {
      current = zc::mv(candidate);
    }
    return;
  }
  current = zc::mv(candidate);
}

void intersectUpper(zc::Maybe<SemVerBound>& current, SemVerBound&& candidate) {
  ZC_IF_SOME(existing, current) {
    const auto existingVersion = resolvedVersion(existing.version());
    const auto candidateVersion = resolvedVersion(candidate.version());
    const int order = compareVersions(existingVersion, candidateVersion);
    if (order > 0) {
      current = zc::mv(candidate);
    } else if (order == 0 && existing.inclusive() && !candidate.inclusive()) {
      current = zc::mv(candidate);
    }
    return;
  }
  current = zc::mv(candidate);
}

zc::Maybe<SemVerBound> cloneBound(const zc::Maybe<SemVerBound>& source) {
  ZC_IF_SOME(value, source) { return value.clone(); }
  return zc::none;
}

zc::Array<uint8_t> encodeCore(const SemVerCore& core) {
  identity::CanonicalEncoder encoder;
  core.encode(encoder);
  return encoder.finish();
}

void addPrereleaseCore(zc::Vector<SemVerCore>& cores, const identity::ResolvedVersion& version) {
  auto parts = splitVersion(version.text());
  if (parts.prerelease.size() == 0) { return; }
  auto core = SemVerCore::from(version);
  auto encoded = encodeCore(core);
  for (const auto& existing : cores) {
    if (encodeCore(existing).asPtr() == encoded.asPtr()) { return; }
  }
  cores.add(zc::mv(core));
}

void sortCores(zc::Vector<SemVerCore>& cores) {
  for (size_t index = 1; index < cores.size(); ++index) {
    auto current = zc::mv(cores[index]);
    size_t insertion = index;
    while (insertion > 0 &&
           encodeCore(current).asPtr() < encodeCore(cores[insertion - 1]).asPtr()) {
      cores[insertion] = zc::mv(cores[insertion - 1]);
      --insertion;
    }
    cores[insertion] = zc::mv(current);
  }
}

}  // namespace

SemVerBound::SemVerBound(identity::ResolvedVersion&& version, bool inclusive) noexcept
    : versionValue(zc::mv(version)), inclusiveValue(inclusive) {}

zc::Maybe<SemVerBound> SemVerBound::from(identity::ResolvedVersion&& version, bool inclusive) {
  if (hasBuildMetadata(version.text())) { return zc::none; }
  return SemVerBound(zc::mv(version), inclusive);
}

SemVerBound SemVerBound::clone() const { return SemVerBound(versionValue.clone(), inclusiveValue); }

zc::StringPtr SemVerBound::version() const noexcept { return versionValue.text(); }
bool SemVerBound::inclusive() const noexcept { return inclusiveValue; }

void SemVerBound::encode(identity::CanonicalEncoder& encoder) const {
  versionValue.encode(encoder);
  encoder.encodeBool(inclusiveValue);
}

SemVerCore::SemVerCore(zc::String&& major, zc::String&& minor, zc::String&& patch) noexcept
    : majorValue(zc::mv(major)), minorValue(zc::mv(minor)), patchValue(zc::mv(patch)) {}

SemVerCore SemVerCore::from(const identity::ResolvedVersion& version) {
  auto parts = splitVersion(version.text());
  return SemVerCore(zc::mv(parts.major), zc::mv(parts.minor), zc::mv(parts.patch));
}

SemVerCore SemVerCore::clone() const {
  return SemVerCore(zc::heapString(majorValue), zc::heapString(minorValue),
                    zc::heapString(patchValue));
}

zc::StringPtr SemVerCore::major() const noexcept { return majorValue; }
zc::StringPtr SemVerCore::minor() const noexcept { return minorValue; }
zc::StringPtr SemVerCore::patch() const noexcept { return patchValue; }

void SemVerCore::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeByteString(majorValue.asBytes());
  encoder.encodeByteString(minorValue.asBytes());
  encoder.encodeByteString(patchValue.asBytes());
}

SemVerInterval::SemVerInterval(zc::Maybe<SemVerBound>&& lower,
                               zc::Maybe<SemVerBound>&& upper) noexcept
    : lowerValue(zc::mv(lower)), upperValue(zc::mv(upper)) {}

zc::Maybe<SemVerInterval> SemVerInterval::from(zc::Maybe<SemVerBound>&& lower,
                                               zc::Maybe<SemVerBound>&& upper) {
  if (lower != zc::none && upper != zc::none) {
    ZC_IF_SOME(lowerBound, lower) {
      ZC_IF_SOME(upperBound, upper) {
        const auto lowerVersion = resolvedVersion(lowerBound.version());
        const auto upperVersion = resolvedVersion(upperBound.version());
        const int order = compareVersions(lowerVersion, upperVersion);
        if (order > 0 || (order == 0 && (!lowerBound.inclusive() || !upperBound.inclusive()))) {
          return zc::none;
        }
      }
    }
  }
  return SemVerInterval(zc::mv(lower), zc::mv(upper));
}

SemVerInterval SemVerInterval::clone() const {
  return SemVerInterval(cloneBound(lowerValue), cloneBound(upperValue));
}

bool SemVerInterval::hasLower() const noexcept { return lowerValue != zc::none; }
bool SemVerInterval::hasUpper() const noexcept { return upperValue != zc::none; }

const SemVerBound& SemVerInterval::lower() const {
  ZC_IF_SOME(value, lowerValue) { return value; }
  ZC_IREQUIRE(false, "lower requires a present SemVer bound");
  ZC_UNREACHABLE
}

const SemVerBound& SemVerInterval::upper() const {
  ZC_IF_SOME(value, upperValue) { return value; }
  ZC_IREQUIRE(false, "upper requires a present SemVer bound");
  ZC_UNREACHABLE
}

void SemVerInterval::encode(identity::CanonicalEncoder& encoder) const {
  ZC_IF_SOME(value, lowerValue) {
    encoder.encodeSome();
    value.encode(encoder);
  }
  else { encoder.encodeNone(); }
  ZC_IF_SOME(value, upperValue) {
    encoder.encodeSome();
    value.encode(encoder);
  }
  else { encoder.encodeNone(); }
}

zc::Array<uint8_t> SemVerInterval::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

SemVerConstraint::SemVerConstraint(zc::Vector<SemVerInterval>&& intervals,
                                   zc::Vector<SemVerCore>&& prereleaseCores) noexcept
    : intervalValues(zc::mv(intervals)), prereleaseCoreValues(zc::mv(prereleaseCores)) {}

zc::Maybe<SemVerConstraint> SemVerConstraint::parse(zc::StringPtr source) {
  if (source.size() == 0 || source.findFirst('+') != zc::none) { return zc::none; }
  zc::Maybe<SemVerBound> lower;
  zc::Maybe<SemVerBound> upper;
  zc::Vector<SemVerCore> prereleaseCores;

  size_t start = 0;
  while (start < source.size()) {
    size_t end = start;
    while (end < source.size() && source[end] != ',') { ++end; }
    if (end == start) { return zc::none; }
    const zc::String comparatorText = zc::heapString(source.slice(start, end));
    auto comparator = parseComparator(comparatorText);
    if (comparator == zc::none) { return zc::none; }
    ZC_IF_SOME(value, comparator) {
      addPrereleaseCore(prereleaseCores, value.version);
      auto parts = splitVersion(value.version.text());
      switch (value.kind) {
        case ComparatorKind::Equal:
          intersectLower(lower, bound(value.version.clone(), true));
          intersectUpper(upper, bound(value.version.clone(), true));
          break;
        case ComparatorKind::Greater:
          intersectLower(lower, bound(value.version.clone(), false));
          break;
        case ComparatorKind::GreaterEqual:
          intersectLower(lower, bound(value.version.clone(), true));
          break;
        case ComparatorKind::Less:
          intersectUpper(upper, bound(value.version.clone(), false));
          break;
        case ComparatorKind::LessEqual:
          intersectUpper(upper, bound(value.version.clone(), true));
          break;
        case ComparatorKind::Caret: {
          intersectLower(lower, bound(value.version.clone(), true));
          if (parts.major != "0"_zc) {
            intersectUpper(
                upper, bound(releaseVersion(incrementDecimal(parts.major), "0"_zc, "0"_zc), false));
          } else if (parts.minor != "0"_zc) {
            intersectUpper(
                upper, bound(releaseVersion("0"_zc, incrementDecimal(parts.minor), "0"_zc), false));
          } else {
            intersectUpper(
                upper, bound(releaseVersion("0"_zc, "0"_zc, incrementDecimal(parts.patch)), false));
          }
          break;
        }
        case ComparatorKind::Tilde:
          intersectLower(lower, bound(value.version.clone(), true));
          intersectUpper(
              upper,
              bound(releaseVersion(parts.major, incrementDecimal(parts.minor), "0"_zc), false));
          break;
      }
    }
    start = end + 1;
  }
  if (source[source.size() - 1] == ',') { return zc::none; }

  sortCores(prereleaseCores);
  zc::Vector<SemVerInterval> intervals;
  auto interval = SemVerInterval::from(zc::mv(lower), zc::mv(upper));
  ZC_IF_SOME(admitted, interval) { intervals.add(zc::mv(admitted)); }
  return SemVerConstraint(zc::mv(intervals), zc::mv(prereleaseCores));
}

SemVerConstraint SemVerConstraint::clone() const {
  zc::Vector<SemVerInterval> intervals(intervalValues.size());
  for (const auto& interval : intervalValues) { intervals.add(interval.clone()); }
  zc::Vector<SemVerCore> cores(prereleaseCoreValues.size());
  for (const auto& core : prereleaseCoreValues) { cores.add(core.clone()); }
  return SemVerConstraint(zc::mv(intervals), zc::mv(cores));
}

SemVerConstraint SemVerConstraint::intersect(const SemVerConstraint& left,
                                             const SemVerConstraint& right) {
  zc::Vector<SemVerInterval> intervals;
  for (const auto& leftInterval : left.intervalValues) {
    for (const auto& rightInterval : right.intervalValues) {
      zc::Maybe<SemVerBound> lower;
      if (!leftInterval.hasLower()) {
        if (rightInterval.hasLower()) { lower = rightInterval.lower().clone(); }
      } else if (!rightInterval.hasLower()) {
        lower = leftInterval.lower().clone();
      } else {
        const auto& leftBound = leftInterval.lower();
        const auto& rightBound = rightInterval.lower();
        auto leftVersion = identity::ResolvedVersion::fromCanonical(leftBound.version());
        auto rightVersion = identity::ResolvedVersion::fromCanonical(rightBound.version());
        ZC_IF_SOME(leftValue, leftVersion) {
          ZC_IF_SOME(rightValue, rightVersion) {
            if (leftValue < rightValue) {
              lower = rightBound.clone();
            } else if (rightValue < leftValue) {
              lower = leftBound.clone();
            } else {
              lower = SemVerBound::from(leftValue.clone(),
                                        leftBound.inclusive() && rightBound.inclusive());
            }
          }
        }
      }

      zc::Maybe<SemVerBound> upper;
      if (!leftInterval.hasUpper()) {
        if (rightInterval.hasUpper()) { upper = rightInterval.upper().clone(); }
      } else if (!rightInterval.hasUpper()) {
        upper = leftInterval.upper().clone();
      } else {
        const auto& leftBound = leftInterval.upper();
        const auto& rightBound = rightInterval.upper();
        auto leftVersion = identity::ResolvedVersion::fromCanonical(leftBound.version());
        auto rightVersion = identity::ResolvedVersion::fromCanonical(rightBound.version());
        ZC_IF_SOME(leftValue, leftVersion) {
          ZC_IF_SOME(rightValue, rightVersion) {
            if (leftValue < rightValue) {
              upper = leftBound.clone();
            } else if (rightValue < leftValue) {
              upper = rightBound.clone();
            } else {
              upper = SemVerBound::from(leftValue.clone(),
                                        leftBound.inclusive() && rightBound.inclusive());
            }
          }
        }
      }
      auto intersection = SemVerInterval::from(zc::mv(lower), zc::mv(upper));
      ZC_IF_SOME(value, intersection) { intervals.add(zc::mv(value)); }
    }
  }

  zc::Vector<SemVerCore> prereleases;
  for (const auto& leftCore : left.prereleaseCoreValues) {
    for (const auto& rightCore : right.prereleaseCoreValues) {
      if (leftCore.major() == rightCore.major() && leftCore.minor() == rightCore.minor() &&
          leftCore.patch() == rightCore.patch()) {
        prereleases.add(leftCore.clone());
        break;
      }
    }
  }
  return SemVerConstraint(zc::mv(intervals), zc::mv(prereleases));
}

zc::ArrayPtr<const SemVerInterval> SemVerConstraint::intervals() const noexcept {
  return intervalValues.asPtr();
}

zc::ArrayPtr<const SemVerCore> SemVerConstraint::prereleaseCores() const noexcept {
  return prereleaseCoreValues.asPtr();
}

bool SemVerConstraint::allows(const identity::ResolvedVersion& version) const {
  bool prerelease = false;
  for (char value : version.text()) {
    if (value == '+') { break; }
    if (value == '-') {
      prerelease = true;
      break;
    }
  }
  if (prerelease) {
    const auto core = SemVerCore::from(version);
    bool admitted = false;
    for (const auto& candidate : prereleaseCoreValues) {
      if (candidate.major() == core.major() && candidate.minor() == core.minor() &&
          candidate.patch() == core.patch()) {
        admitted = true;
        break;
      }
    }
    if (!admitted) { return false; }
  }

  for (const auto& interval : intervalValues) {
    bool aboveLower = true;
    if (interval.hasLower()) {
      auto lower = identity::ResolvedVersion::fromCanonical(interval.lower().version());
      ZC_IF_SOME(value, lower) {
        aboveLower = value < version || (value == version && interval.lower().inclusive());
      }
    }
    if (!aboveLower) { continue; }
    bool belowUpper = true;
    if (interval.hasUpper()) {
      auto upper = identity::ResolvedVersion::fromCanonical(interval.upper().version());
      ZC_IF_SOME(value, upper) {
        belowUpper = version < value || (value == version && interval.upper().inclusive());
      }
    }
    if (belowUpper) { return true; }
  }
  return false;
}

void SemVerConstraint::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeSequenceSize(intervalValues.size());
  for (const auto& interval : intervalValues) { interval.encode(encoder); }
  encoder.encodeSequenceSize(prereleaseCoreValues.size());
  for (const auto& core : prereleaseCoreValues) { core.encode(encoder); }
}

zc::Array<uint8_t> SemVerConstraint::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

}  // namespace zomlang::compiler::driver::package
