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

#include "zomlang/compiler/driver/package/manifest-parser.h"

#include "zc/core/encoding.h"
#include "zomlang/compiler/driver/package/semver-constraint.h"

// std:: required - the vendored toml++ API exposes std::string_view and node pointers.
#include <string_view>

#include "toml++/toml.hpp"
#include "zc/core/vector.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/canonical-scalar.h"
#include "zomlang/compiler/identity/semantic-version.h"
#include "zomlang/compiler/identity/sha256.h"
#include "zomlang/compiler/identity/unicode-normalization.h"

namespace zomlang::compiler::driver::package {
namespace {

bool isAllowedTopLevel(std::string_view key) {
  return key == "package" || key == "workspace" || key == "lib" || key == "bin" || key == "test" ||
         key == "bench" || key == "example" || key == "build" || key == "dependencies" ||
         key == "dev-dependencies" || key == "build-dependencies" || key == "features";
}

bool isPackageKey(std::string_view key) {
  return key == "name" || key == "version" || key == "edition";
}

bool isWorkspaceKey(std::string_view key) { return key == "members"; }

bool isTargetKey(std::string_view key) { return key == "name" || key == "path"; }

bool isBuildKey(std::string_view key) {
  return key == "path" || key == "inputs" || key == "outputs" || key == "environment" ||
         key == "exported-environment";
}

bool isDependencyKey(std::string_view key) {
  return key == "package" || key == "version" || key == "registry" ||
         key == "trust-domain-sha256" || key == "git" || key == "rev" || key == "tag" ||
         key == "branch" || key == "subdirectory" || key == "path" || key == "features" ||
         key == "default-features" || key == "optional";
}

uint64_t byteOffset(zc::StringPtr source, toml::source_position position) {
  uint32_t line = 1;
  uint32_t column = 1;
  for (size_t index = 0; index < source.size(); ++index) {
    if (line == position.line && column == position.column) { return index; }
    if (source[index] == '\n') {
      ++line;
      column = 1;
    } else if ((static_cast<uint8_t>(source[index]) & 0xc0) != 0x80) {
      ++column;
    }
  }
  return source.size();
}

ManifestParseError errorAt(ManifestIssue issue, zc::StringPtr source,
                           const toml::source_region& region) {
  return ManifestParseError{issue, byteOffset(source, region.begin),
                            byteOffset(source, region.end)};
}

ManifestParseError wholeDocumentError(ManifestIssue issue, zc::StringPtr source) {
  return ManifestParseError{issue, 0, source.size()};
}

DiagnosticAnchor manifestOrigin(const InputDocumentKey& document, zc::StringPtr source,
                                const toml::source_region& region) {
  auto span = ManifestSpan::from(document.clone(), source.size(), byteOffset(source, region.begin),
                                 byteOffset(source, region.end));
  ZC_IF_SOME(admitted, span) { return DiagnosticAnchor::manifest(zc::mv(admitted)); }
  ZC_IREQUIRE(false, "toml source region must fit the input document");
  ZC_UNREACHABLE
}

zc::Maybe<ManifestParseError> validateTopLevel(const toml::table& root, zc::StringPtr source) {
  for (const auto& [key, node] : root) {
    if (!isAllowedTopLevel(key.str())) {
      return errorAt(ManifestIssue::UnknownTable, source, key.source());
    }
  }
  return zc::none;
}

zc::Maybe<ManifestParseError> validatePackageKeys(const toml::table& package,
                                                  zc::StringPtr source) {
  for (const auto& [key, node] : package) {
    if (!isPackageKey(key.str())) {
      return errorAt(ManifestIssue::UnknownKey, source, key.source());
    }
  }
  return zc::none;
}

zc::Maybe<ManifestParseError> validateKeys(const toml::table& table, zc::StringPtr source,
                                           bool (*isAllowed)(std::string_view)) {
  for (const auto& [key, node] : table) {
    if (!isAllowed(key.str())) { return errorAt(ManifestIssue::UnknownKey, source, key.source()); }
  }
  return zc::none;
}

bool isHex(zc::StringPtr value, size_t length) {
  if (value.size() != length) { return false; }
  for (char byte : value) {
    if (!((byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f'))) { return false; }
  }
  return true;
}

bool isSelectorText(zc::StringPtr value) {
  if (value.size() == 0) { return false; }
  for (char byte : value) {
    const auto unsignedByte = static_cast<uint8_t>(byte);
    if (unsignedByte == 0 || unsignedByte < 0x20 || unsignedByte == 0x7f) { return false; }
  }
  return true;
}

bool isCanonicalRelativePath(zc::StringPtr value, bool allowLeadingParents) {
  if (value.size() == 0 || value[0] == '/') { return false; }
  size_t start = 0;
  bool sawSegment = false;
  for (size_t index = 0; index <= value.size(); ++index) {
    if (index < value.size() && value[index] == '\\') { return false; }
    if (index < value.size() && value[index] != '/') { continue; }
    if (index == start) { return false; }
    const zc::String segmentText = zc::heapString(value.slice(start, index));
    const zc::StringPtr segment(segmentText);
    if (segment == "."_zc) { return false; }
    if (segment == ".."_zc) {
      if (!allowLeadingParents || sawSegment) { return false; }
    } else {
      if (identity::CanonicalPathSegment::fromSource(segment) == zc::none) { return false; }
      sawSegment = true;
    }
    start = index + 1;
  }
  return sawSegment;
}

zc::Maybe<identity::CanonicalWorkspaceRelativePath> parseWorkspacePath(zc::StringPtr value,
                                                                       bool allowLeadingParents) {
  if (!isCanonicalRelativePath(value, allowLeadingParents)) { return zc::none; }
  zc::Vector<identity::CanonicalPathSegment> segments;
  uint32_t leadingParents = 0;
  size_t start = 0;
  for (size_t index = 0; index <= value.size(); ++index) {
    if (index < value.size() && value[index] != '/') { continue; }
    const zc::String segmentText = zc::heapString(value.slice(start, index));
    if (segmentText == ".."_zc) {
      ++leadingParents;
    } else {
      auto segment = identity::CanonicalPathSegment::fromSource(segmentText);
      ZC_IF_SOME(admitted, segment) { segments.add(zc::mv(admitted)); }
    }
    start = index + 1;
  }
  return identity::CanonicalWorkspaceRelativePath::from(leadingParents, zc::mv(segments));
}

identity::CanonicalRelativePath parseRelativePath(zc::StringPtr value) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  if (value.size() != 0) {
    size_t start = 0;
    for (size_t index = 0; index <= value.size(); ++index) {
      if (index < value.size() && value[index] != '/') { continue; }
      const zc::String segmentText = zc::heapString(value.slice(start, index));
      auto segment = identity::CanonicalPathSegment::fromSource(segmentText);
      ZC_IF_SOME(admitted, segment) { segments.add(zc::mv(admitted)); }
      start = index + 1;
    }
  }
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

template <typename Value>
zc::Array<uint8_t> encodeValue(const Value& value) {
  identity::CanonicalEncoder encoder;
  value.encode(encoder);
  return encoder.finish();
}

zc::Maybe<ManifestParseError> validateStringArray(const toml::node& node, zc::StringPtr source) {
  const auto* array = node.as_array();
  if (array == nullptr) { return errorAt(ManifestIssue::WrongValueType, source, node.source()); }
  for (const auto& element : *array) {
    if (!element.is_string()) {
      return errorAt(ManifestIssue::WrongValueType, source, element.source());
    }
  }
  return zc::none;
}

zc::Maybe<ManifestParseError> validateWorkspace(const toml::table& workspace, bool hasPackage,
                                                zc::StringPtr source) {
  ZC_IF_SOME(issue, validateKeys(workspace, source, isWorkspaceKey)) { return issue; }
  const auto* membersNode = workspace.get("members");
  if (membersNode == nullptr) {
    if (!hasPackage) { return wholeDocumentError(ManifestIssue::MissingRequiredKey, source); }
    return zc::none;
  }
  ZC_IF_SOME(issue, validateStringArray(*membersNode, source)) { return issue; }
  const auto& members = *membersNode->as_array();
  if (!hasPackage && members.size() == 0) {
    return errorAt(ManifestIssue::MissingRequiredKey, source, membersNode->source());
  }
  for (const auto& member : members) {
    const auto& text = member.as_string()->get();
    if (!isCanonicalRelativePath(zc::StringPtr(text.data(), text.size()), false)) {
      return errorAt(ManifestIssue::InvalidPath, source, member.source());
    }
  }
  return zc::none;
}

zc::Maybe<ManifestParseError> validateTargetTable(const toml::table& target, bool requiresName,
                                                  zc::StringPtr source) {
  ZC_IF_SOME(issue, validateKeys(target, source, isTargetKey)) { return issue; }
  const auto* nameNode = target.get("name");
  if (requiresName && nameNode == nullptr) {
    return wholeDocumentError(ManifestIssue::MissingRequiredKey, source);
  }
  if (nameNode != nullptr) {
    const auto* name = nameNode->as_string();
    if (name == nullptr) {
      return errorAt(ManifestIssue::WrongValueType, source, nameNode->source());
    }
    const auto& text = name->get();
    if (identity::TargetName::fromSource(zc::StringPtr(text.data(), text.size())) == zc::none) {
      return errorAt(ManifestIssue::InvalidStrongScalar, source, nameNode->source());
    }
  }
  const auto* pathNode = target.get("path");
  if (pathNode != nullptr) {
    const auto* path = pathNode->as_string();
    if (path == nullptr) {
      return errorAt(ManifestIssue::WrongValueType, source, pathNode->source());
    }
    const auto& text = path->get();
    const zc::StringPtr pathText(text.data(), text.size());
    if (!isCanonicalRelativePath(pathText, false) || !pathText.endsWith(".zom"_zc)) {
      return errorAt(ManifestIssue::InvalidPath, source, pathNode->source());
    }
  }
  return zc::none;
}

zc::Maybe<ManifestParseError> validateRepeatedTargets(const toml::node& node,
                                                      zc::StringPtr source) {
  const auto* targets = node.as_array();
  if (targets == nullptr) { return errorAt(ManifestIssue::WrongValueType, source, node.source()); }
  for (const auto& targetNode : *targets) {
    const auto* target = targetNode.as_table();
    if (target == nullptr) {
      return errorAt(ManifestIssue::WrongValueType, source, targetNode.source());
    }
    ZC_IF_SOME(issue, validateTargetTable(*target, true, source)) { return issue; }
  }
  return zc::none;
}

zc::Maybe<ManifestParseError> validateBuild(const toml::table& build, zc::StringPtr source) {
  ZC_IF_SOME(issue, validateKeys(build, source, isBuildKey)) { return issue; }
  for (const auto required : {"path", "inputs", "outputs"}) {
    if (!build.contains(required)) {
      return wholeDocumentError(ManifestIssue::MissingRequiredKey, source);
    }
  }
  const auto* path = build.get("path");
  if (!path->is_string()) { return errorAt(ManifestIssue::WrongValueType, source, path->source()); }
  const auto& pathText = path->as_string()->get();
  const zc::StringPtr buildPath(pathText.data(), pathText.size());
  if (!isCanonicalRelativePath(buildPath, false) || !buildPath.endsWith(".zom"_zc)) {
    return errorAt(ManifestIssue::InvalidPath, source, path->source());
  }
  for (const auto key : {"inputs", "outputs"}) {
    const auto* node = build.get(key);
    ZC_IF_SOME(issue, validateStringArray(*node, source)) { return issue; }
    zc::Vector<identity::CanonicalRelativePath> admittedPaths;
    for (const auto& element : *node->as_array()) {
      const auto& text = element.as_string()->get();
      const zc::StringPtr value(text.data(), text.size());
      if (!isCanonicalRelativePath(value, false) ||
          (zc::StringPtr(key) == "outputs"_zc && !value.endsWith(".zom"_zc))) {
        return errorAt(ManifestIssue::InvalidPath, source, element.source());
      }
      auto pathValue = parseRelativePath(value);
      const auto encoded = encodeValue(pathValue);
      for (const auto& existing : admittedPaths) {
        if (encodeValue(existing).asPtr() == encoded.asPtr()) {
          return errorAt(ManifestIssue::DuplicateCanonicalValue, source, element.source());
        }
      }
      admittedPaths.add(zc::mv(pathValue));
    }
  }
  for (const auto key : {"environment", "exported-environment"}) {
    const auto* node = build.get(key);
    if (node == nullptr) { continue; }
    ZC_IF_SOME(issue, validateStringArray(*node, source)) { return issue; }
    zc::Vector<identity::SemanticEnvironmentName> admittedNames;
    for (const auto& element : *node->as_array()) {
      const auto& text = element.as_string()->get();
      auto name =
          identity::SemanticEnvironmentName::fromSource(zc::StringPtr(text.data(), text.size()));
      if (name == zc::none) {
        return errorAt(ManifestIssue::InvalidStrongScalar, source, element.source());
      }
      ZC_IF_SOME(admitted, name) {
        for (const auto& existing : admittedNames) {
          if (existing == admitted) {
            return errorAt(ManifestIssue::DuplicateCanonicalValue, source, element.source());
          }
        }
        admittedNames.add(zc::mv(admitted));
      }
    }
  }
  return zc::none;
}

zc::Maybe<ManifestParseError> validateDependencyTable(const toml::table& dependencies,
                                                      bool optionalAllowed, zc::StringPtr source) {
  for (const auto& [aliasKey, dependencyNode] : dependencies) {
    const auto aliasText = aliasKey.str();
    if (identity::DependencyAlias::fromSource(zc::StringPtr(aliasText.data(), aliasText.size())) ==
        zc::none) {
      return errorAt(ManifestIssue::InvalidStrongScalar, source, aliasKey.source());
    }
    const auto* dependency = dependencyNode.as_table();
    if (dependency == nullptr || !dependency->is_inline()) {
      return errorAt(ManifestIssue::WrongValueType, source, dependencyNode.source());
    }
    ZC_IF_SOME(issue, validateKeys(*dependency, source, isDependencyKey)) { return issue; }

    for (const auto key : {"package", "version", "registry", "trust-domain-sha256", "git", "rev",
                           "tag", "branch", "subdirectory", "path"}) {
      const auto* node = dependency->get(key);
      if (node != nullptr && !node->is_string()) {
        return errorAt(ManifestIssue::WrongValueType, source, node->source());
      }
    }
    for (const auto key : {"default-features", "optional"}) {
      const auto* node = dependency->get(key);
      if (node != nullptr && !node->is_boolean()) {
        return errorAt(ManifestIssue::WrongValueType, source, node->source());
      }
    }
    const auto* featureNode = dependency->get("features");
    if (featureNode != nullptr) {
      ZC_IF_SOME(issue, validateStringArray(*featureNode, source)) { return issue; }
      zc::Vector<identity::FeatureName> admittedFeatures;
      for (const auto& feature : *featureNode->as_array()) {
        const auto& text = feature.as_string()->get();
        auto admitted = identity::FeatureName::fromSource(zc::StringPtr(text.data(), text.size()));
        if (admitted == zc::none) {
          return errorAt(ManifestIssue::InvalidStrongScalar, source, feature.source());
        }
        ZC_IF_SOME(value, admitted) {
          for (const auto& existing : admittedFeatures) {
            if (existing == value) {
              return errorAt(ManifestIssue::DuplicateCanonicalValue, source, feature.source());
            }
          }
          admittedFeatures.add(zc::mv(value));
        }
      }
    }

    const bool local = dependency->contains("path");
    const bool vcs = dependency->contains("git");
    const bool registry =
        dependency->contains("registry") || dependency->contains("trust-domain-sha256");
    if (static_cast<uint8_t>(local) + static_cast<uint8_t>(vcs) + static_cast<uint8_t>(registry) !=
        1) {
      return errorAt(ManifestIssue::DependencySourceConflict, source, dependencyNode.source());
    }
    if (local) {
      if (dependency->contains("registry") || dependency->contains("trust-domain-sha256") ||
          dependency->contains("git") || dependency->contains("rev") ||
          dependency->contains("tag") || dependency->contains("branch") ||
          dependency->contains("subdirectory")) {
        return errorAt(ManifestIssue::DependencySourceConflict, source, dependencyNode.source());
      }
      const auto& text = dependency->get("path")->as_string()->get();
      if (!isCanonicalRelativePath(zc::StringPtr(text.data(), text.size()), true)) {
        return errorAt(ManifestIssue::InvalidPath, source, dependency->get("path")->source());
      }
    } else if (vcs) {
      const uint8_t selectors = static_cast<uint8_t>(dependency->contains("rev")) +
                                static_cast<uint8_t>(dependency->contains("tag")) +
                                static_cast<uint8_t>(dependency->contains("branch"));
      if (selectors != 1 || dependency->contains("registry") ||
          dependency->contains("trust-domain-sha256") || dependency->contains("path")) {
        return errorAt(ManifestIssue::DependencySourceConflict, source, dependencyNode.source());
      }
      const auto& git = dependency->get("git")->as_string()->get();
      if (identity::CanonicalUrl::fromSource(zc::StringPtr(git.data(), git.size())) == zc::none) {
        return errorAt(ManifestIssue::InvalidStrongScalar, source,
                       dependency->get("git")->source());
      }
      if (dependency->contains("rev")) {
        const auto& rev = dependency->get("rev")->as_string()->get();
        const zc::StringPtr revision(rev.data(), rev.size());
        if (!isHex(revision, 40) && !isHex(revision, 64)) {
          return errorAt(ManifestIssue::InvalidVcsSelector, source,
                         dependency->get("rev")->source());
        }
      }
      for (const auto key : {"tag", "branch"}) {
        const auto* selector = dependency->get(key);
        if (selector != nullptr) {
          const auto& text = selector->as_string()->get();
          if (!isSelectorText(zc::StringPtr(text.data(), text.size()))) {
            return errorAt(ManifestIssue::InvalidVcsSelector, source, selector->source());
          }
        }
      }
      if (const auto* subdirectory = dependency->get("subdirectory")) {
        const auto& text = subdirectory->as_string()->get();
        if (!isCanonicalRelativePath(zc::StringPtr(text.data(), text.size()), false)) {
          return errorAt(ManifestIssue::InvalidPath, source, subdirectory->source());
        }
      }
    } else {
      if (!dependency->contains("version") || !dependency->contains("registry") ||
          !dependency->contains("trust-domain-sha256") || dependency->contains("git") ||
          dependency->contains("rev") || dependency->contains("tag") ||
          dependency->contains("branch") || dependency->contains("subdirectory") ||
          dependency->contains("path")) {
        return errorAt(ManifestIssue::DependencySourceConflict, source, dependencyNode.source());
      }
      const auto& url = dependency->get("registry")->as_string()->get();
      if (identity::CanonicalUrl::fromSource(zc::StringPtr(url.data(), url.size())) == zc::none) {
        return errorAt(ManifestIssue::InvalidStrongScalar, source,
                       dependency->get("registry")->source());
      }
      const auto& trust = dependency->get("trust-domain-sha256")->as_string()->get();
      if (!isHex(zc::StringPtr(trust.data(), trust.size()), 64)) {
        return errorAt(ManifestIssue::InvalidStrongScalar, source,
                       dependency->get("trust-domain-sha256")->source());
      }
    }
    const auto* packageNode = dependency->get("package");
    if (packageNode != nullptr) {
      const auto& text = packageNode->as_string()->get();
      if (identity::PackageName::fromSource(zc::StringPtr(text.data(), text.size())) == zc::none) {
        return errorAt(ManifestIssue::InvalidStrongScalar, source, packageNode->source());
      }
    } else if (identity::PackageName::fromSource(
                   zc::StringPtr(aliasText.data(), aliasText.size())) == zc::none) {
      return errorAt(ManifestIssue::InvalidStrongScalar, source, dependencyNode.source());
    }
    const auto* versionNode = dependency->get("version");
    if (versionNode != nullptr) {
      const auto& text = versionNode->as_string()->get();
      if (SemVerConstraint::parse(zc::StringPtr(text.data(), text.size())) == zc::none) {
        return errorAt(ManifestIssue::InvalidVersionConstraint, source, versionNode->source());
      }
    }
    const auto* optionalNode = dependency->get("optional");
    if (!optionalAllowed && optionalNode != nullptr && optionalNode->as_boolean()->get()) {
      return errorAt(ManifestIssue::DependencySourceConflict, source, optionalNode->source());
    }
  }
  return zc::none;
}

bool canonicalTextEquals(zc::StringPtr left, zc::StringPtr right) {
  auto canonicalLeft = identity::normalizeNfc(left);
  auto canonicalRight = identity::normalizeNfc(right);
  ZC_IF_SOME(leftValue, canonicalLeft) {
    ZC_IF_SOME(rightValue, canonicalRight) { return leftValue == rightValue; }
  }
  return false;
}

const toml::node* findCanonicalEntry(const toml::table& table, zc::StringPtr name) {
  for (const auto& [key, node] : table) {
    const auto keyText = key.str();
    if (canonicalTextEquals(zc::StringPtr(keyText.data(), keyText.size()), name)) { return &node; }
  }
  return nullptr;
}

bool dependencyIsOptional(const toml::node& dependency) {
  const auto* table = dependency.as_table();
  if (table == nullptr) { return false; }
  const auto* optional = table->get("optional");
  return optional != nullptr && optional->is_boolean() && optional->as_boolean()->get();
}

bool containsCanonical(zc::ArrayPtr<const zc::String> values, zc::StringPtr value) {
  for (const auto& candidate : values) {
    if (canonicalTextEquals(candidate, value)) { return true; }
  }
  return false;
}

bool hasFeatureCycle(const toml::table& features, zc::StringPtr feature,
                     zc::Vector<zc::String>& active, zc::Vector<zc::String>& complete) {
  if (containsCanonical(complete.asPtr(), feature)) { return false; }
  if (containsCanonical(active.asPtr(), feature)) { return true; }
  active.add(zc::heapString(feature));
  const auto* node = findCanonicalEntry(features, feature);
  ZC_IREQUIRE(node != nullptr, "validated local feature must exist");
  for (const auto& edgeNode : *node->as_array()) {
    const auto& edgeText = edgeNode.as_string()->get();
    const zc::StringPtr edge(edgeText.data(), edgeText.size());
    if (!edge.startsWith("dep:"_zc) && edge.findFirst('/') == zc::none &&
        hasFeatureCycle(features, edge, active, complete)) {
      return true;
    }
  }
  active.removeLast();
  complete.add(zc::heapString(feature));
  return false;
}

zc::Maybe<ManifestParseError> validateFeatures(const toml::table& features,
                                               const toml::table* dependencies,
                                               zc::StringPtr source) {
  zc::Vector<zc::String> canonicalFeatureNames;
  for (const auto& [featureKey, edgesNode] : features) {
    const auto featureText = featureKey.str();
    if (identity::FeatureName::fromSource(zc::StringPtr(featureText.data(), featureText.size())) ==
        zc::none) {
      return errorAt(ManifestIssue::InvalidStrongScalar, source, featureKey.source());
    }
    const zc::StringPtr featureName(featureText.data(), featureText.size());
    if (containsCanonical(canonicalFeatureNames.asPtr(), featureName)) {
      return errorAt(ManifestIssue::DuplicateCanonicalValue, source, featureKey.source());
    }
    canonicalFeatureNames.add(zc::heapString(featureName));
    ZC_IF_SOME(issue, validateStringArray(edgesNode, source)) { return issue; }
    zc::Vector<zc::String> canonicalEdges;
    for (const auto& edgeNode : *edgesNode.as_array()) {
      const auto& edgeText = edgeNode.as_string()->get();
      const zc::StringPtr edge(edgeText.data(), edgeText.size());
      bool valid = false;
      if (edge.startsWith("dep:"_zc)) {
        valid = identity::DependencyAlias::fromSource(edge.slice(4)) != zc::none;
      } else {
        ZC_IF_SOME(slash, edge.findFirst('/')) {
          const zc::String aliasText = zc::heapString(edge.first(slash));
          valid = slash > 0 && slash + 1 < edge.size() &&
                  edge.slice(slash + 1).findFirst('/') == zc::none &&
                  identity::DependencyAlias::fromSource(aliasText) != zc::none &&
                  identity::FeatureName::fromSource(edge.slice(slash + 1)) != zc::none;
        }
        if (edge.findFirst('/') == zc::none) {
          valid = identity::FeatureName::fromSource(edge) != zc::none;
        }
      }
      if (!valid) { return errorAt(ManifestIssue::InvalidFeatureEdge, source, edgeNode.source()); }
      if (containsCanonical(canonicalEdges.asPtr(), edge)) {
        return errorAt(ManifestIssue::DuplicateCanonicalValue, source, edgeNode.source());
      }
      canonicalEdges.add(zc::heapString(edge));

      if (edge.startsWith("dep:"_zc)) {
        const auto* dependency =
            dependencies == nullptr ? nullptr : findCanonicalEntry(*dependencies, edge.slice(4));
        if (dependency == nullptr || !dependencyIsOptional(*dependency)) {
          return errorAt(ManifestIssue::InvalidFeatureEdge, source, edgeNode.source());
        }
      } else {
        ZC_IF_SOME(slash, edge.findFirst('/')) {
          const zc::String alias = zc::heapString(edge.first(slash));
          if (dependencies == nullptr || findCanonicalEntry(*dependencies, alias) == nullptr) {
            return errorAt(ManifestIssue::InvalidFeatureEdge, source, edgeNode.source());
          }
        }
        if (edge.findFirst('/') == zc::none && findCanonicalEntry(features, edge) == nullptr) {
          return errorAt(ManifestIssue::InvalidFeatureEdge, source, edgeNode.source());
        }
      }
    }
  }

  zc::Vector<zc::String> active;
  zc::Vector<zc::String> complete;
  for (const auto& feature : canonicalFeatureNames) {
    if (hasFeatureCycle(features, feature, active, complete)) {
      const auto* node = findCanonicalEntry(features, feature);
      return errorAt(ManifestIssue::FeatureCycle, source, node->source());
    }
  }
  return zc::none;
}

zc::OneOf<PackageManifest, ManifestParseError> parsePackage(const toml::table& package,
                                                            zc::StringPtr source) {
  ZC_IF_SOME(issue, validatePackageKeys(package, source)) { return issue; }

  const auto* nameNode = package.get("name");
  const auto* versionNode = package.get("version");
  const auto* editionNode = package.get("edition");
  if (nameNode == nullptr || versionNode == nullptr || editionNode == nullptr) {
    return wholeDocumentError(ManifestIssue::MissingRequiredKey, source);
  }
  const auto* name = nameNode->as_string();
  const auto* version = versionNode->as_string();
  const auto* edition = editionNode->as_string();
  if (name == nullptr) {
    return errorAt(ManifestIssue::WrongValueType, source, nameNode->source());
  }
  if (version == nullptr) {
    return errorAt(ManifestIssue::WrongValueType, source, versionNode->source());
  }
  if (edition == nullptr) {
    return errorAt(ManifestIssue::WrongValueType, source, editionNode->source());
  }

  const auto& nameText = name->get();
  auto canonicalName =
      identity::PackageName::fromSource(zc::StringPtr(nameText.data(), nameText.size()));
  if (canonicalName == zc::none) {
    return errorAt(ManifestIssue::InvalidStrongScalar, source, nameNode->source());
  }
  const auto& versionText = version->get();
  auto canonicalVersion = identity::ResolvedVersion::fromCanonical(
      zc::StringPtr(versionText.data(), versionText.size()));
  if (canonicalVersion == zc::none) {
    return errorAt(ManifestIssue::InvalidStrongScalar, source, versionNode->source());
  }
  const auto& editionText = edition->get();
  if (editionText != "2026") {
    return errorAt(ManifestIssue::UnsupportedEdition, source, editionNode->source());
  }
  ZC_IF_SOME(nameValue, canonicalName) {
    ZC_IF_SOME(versionValue, canonicalVersion) {
      return PackageManifest::from(zc::mv(nameValue), zc::mv(versionValue), 2026);
    }
  }
  ZC_IREQUIRE(false, "validated package scalars must remain present");
  ZC_UNREACHABLE
}

zc::OneOf<WorkspaceManifest, ManifestParseError> parseWorkspace(const toml::table& workspace,
                                                                zc::StringPtr source) {
  zc::Vector<identity::CanonicalWorkspaceRelativePath> members;
  const auto* membersNode = workspace.get("members");
  if (membersNode != nullptr) {
    for (const auto& member : *membersNode->as_array()) {
      const auto& text = member.as_string()->get();
      auto path = parseWorkspacePath(zc::StringPtr(text.data(), text.size()), false);
      ZC_IF_SOME(admitted, path) { members.add(zc::mv(admitted)); }
    }
  }
  auto result = WorkspaceManifest::from(zc::mv(members));
  ZC_IF_SOME(admitted, result) { return zc::mv(admitted); }
  return errorAt(ManifestIssue::DuplicateCanonicalValue, source, membersNode->source());
}

FeatureEdge parseFeatureEdge(zc::StringPtr edge) {
  if (edge.startsWith("dep:"_zc)) {
    auto alias = identity::DependencyAlias::fromSource(edge.slice(4));
    ZC_IF_SOME(admitted, alias) { return FeatureEdge::enableDependency(zc::mv(admitted)); }
  } else {
    ZC_IF_SOME(slash, edge.findFirst('/')) {
      const zc::String aliasText = zc::heapString(edge.first(slash));
      auto alias = identity::DependencyAlias::fromSource(aliasText);
      auto feature = identity::FeatureName::fromSource(edge.slice(slash + 1));
      ZC_IF_SOME(aliasValue, alias) {
        ZC_IF_SOME(featureValue, feature) {
          return FeatureEdge::enableDependencyFeature(zc::mv(aliasValue), zc::mv(featureValue));
        }
      }
    }
    auto feature = identity::FeatureName::fromSource(edge);
    ZC_IF_SOME(admitted, feature) { return FeatureEdge::local(zc::mv(admitted)); }
  }
  ZC_IREQUIRE(false, "validated feature edge must remain constructible");
  ZC_UNREACHABLE
}

zc::Vector<FeatureManifest> parseFeatures(const toml::table& features,
                                          const InputDocumentKey& document, zc::StringPtr source) {
  zc::Vector<FeatureManifest> result;
  for (const auto& [featureKey, edgesNode] : features) {
    const auto keyText = featureKey.str();
    auto name = identity::FeatureName::fromSource(zc::StringPtr(keyText.data(), keyText.size()));
    zc::Vector<FeatureEdgeRecord> edges;
    for (const auto& edgeNode : *edgesNode.as_array()) {
      const auto& edgeText = edgeNode.as_string()->get();
      edges.add(
          FeatureEdgeRecord::from(parseFeatureEdge(zc::StringPtr(edgeText.data(), edgeText.size())),
                                  manifestOrigin(document, source, edgeNode.source())));
    }
    ZC_IF_SOME(nameValue, name) {
      auto manifest = FeatureManifest::from(zc::mv(nameValue), zc::mv(edges));
      ZC_IF_SOME(admitted, manifest) { result.add(zc::mv(admitted)); }
    }
  }
  for (size_t index = 1; index < result.size(); ++index) {
    auto current = zc::mv(result[index]);
    size_t insertion = index;
    while (insertion > 0 && (current.name().size() < result[insertion - 1].name().size() ||
                             (current.name().size() == result[insertion - 1].name().size() &&
                              current.name() < result[insertion - 1].name()))) {
      result[insertion] = zc::mv(result[insertion - 1]);
      --insertion;
    }
    result[insertion] = zc::mv(current);
  }
  return result;
}

identity::TargetName parseTargetName(const toml::table& target, zc::StringPtr defaultName) {
  zc::StringPtr text = defaultName;
  if (const auto* node = target.get("name")) {
    const auto& stored = node->as_string()->get();
    text = zc::StringPtr(stored.data(), stored.size());
  }
  auto result = identity::TargetName::fromSource(text);
  ZC_IF_SOME(admitted, result) { return zc::mv(admitted); }
  ZC_IREQUIRE(false, "validated target name must remain constructible");
  ZC_UNREACHABLE
}

zc::String defaultTargetPath(identity::CrateTargetKind kind, zc::StringPtr name) {
  switch (kind) {
    case identity::CrateTargetKind::Library:
      return zc::heapString("src/lib.zom"_zc);
    case identity::CrateTargetKind::Binary:
      return zc::str("src/bin/", name, ".zom");
    case identity::CrateTargetKind::Test:
      return zc::str("tests/", name, ".zom");
    case identity::CrateTargetKind::Benchmark:
      return zc::str("benches/", name, ".zom");
    case identity::CrateTargetKind::Example:
      return zc::str("examples/", name, ".zom");
    case identity::CrateTargetKind::BuildScript:
      break;
  }
  ZC_IREQUIRE(false, "build-script path has no implicit default");
  ZC_UNREACHABLE
}

TargetManifest parseTarget(const toml::table& target, identity::CrateTargetKind kind,
                           zc::StringPtr defaultName, const InputDocumentKey& document,
                           zc::StringPtr source) {
  auto name = parseTargetName(target, defaultName);
  zc::String path = defaultTargetPath(kind, name.text());
  if (const auto* node = target.get("path")) {
    const auto& stored = node->as_string()->get();
    path = zc::heapString(zc::StringPtr(stored.data(), stored.size()));
  }
  auto result = TargetManifest::from(kind, zc::mv(name), parseRelativePath(path), false,
                                     manifestOrigin(document, source, target.source()));
  ZC_IF_SOME(admitted, result) { return zc::mv(admitted); }
  ZC_IREQUIRE(false, "validated target record must remain constructible");
  ZC_UNREACHABLE
}

zc::Vector<TargetManifest> parseRepeatedTargets(const toml::array& targets,
                                                identity::CrateTargetKind kind,
                                                const InputDocumentKey& document,
                                                zc::StringPtr source) {
  zc::Vector<TargetManifest> result;
  for (const auto& node : targets) {
    result.add(parseTarget(*node.as_table(), kind, ""_zc, document, source));
  }
  ZC_IREQUIRE(sortTargetManifests(result), "validated targets must remain canonically unique");
  return result;
}

zc::Vector<identity::CanonicalRelativePath> parseBuildPaths(const toml::table& build,
                                                            zc::StringPtr key) {
  zc::Vector<identity::CanonicalRelativePath> result;
  for (const auto& node : *build.get(key.cStr())->as_array()) {
    const auto& text = node.as_string()->get();
    result.add(parseRelativePath(zc::StringPtr(text.data(), text.size())));
  }
  return result;
}

zc::Vector<identity::SemanticEnvironmentName> parseBuildEnvironment(const toml::table& build,
                                                                    zc::StringPtr key) {
  zc::Vector<identity::SemanticEnvironmentName> result;
  const auto* values = build.get(key.cStr());
  if (values == nullptr) { return result; }
  for (const auto& node : *values->as_array()) {
    const auto& text = node.as_string()->get();
    auto name =
        identity::SemanticEnvironmentName::fromSource(zc::StringPtr(text.data(), text.size()));
    ZC_IF_SOME(admitted, name) { result.add(zc::mv(admitted)); }
  }
  return result;
}

BuildScriptManifest parseBuildScript(const toml::table& build, const InputDocumentKey& document,
                                     zc::StringPtr source) {
  const auto& pathText = build.get("path")->as_string()->get();
  auto name = identity::TargetName::fromSource("build"_zc);
  ZC_IF_SOME(admittedName, name) {
    auto target =
        TargetManifest::from(identity::CrateTargetKind::BuildScript, zc::mv(admittedName),
                             parseRelativePath(zc::StringPtr(pathText.data(), pathText.size())),
                             false, manifestOrigin(document, source, build.source()));
    ZC_IF_SOME(admittedTarget, target) {
      auto result = BuildScriptManifest::from(
          zc::mv(admittedTarget), parseBuildPaths(build, "inputs"_zc),
          parseBuildPaths(build, "outputs"_zc), parseBuildEnvironment(build, "environment"_zc),
          parseBuildEnvironment(build, "exported-environment"_zc));
      ZC_IF_SOME(admitted, result) { return zc::mv(admitted); }
    }
  }
  ZC_IREQUIRE(false, "validated build-script record must remain constructible");
  ZC_UNREACHABLE
}

DiagnosticAnchor wholeManifestOrigin(const InputDocumentKey& document, zc::StringPtr source) {
  auto span = ManifestSpan::from(document.clone(), source.size(), 0, source.size());
  ZC_IF_SOME(admitted, span) { return DiagnosticAnchor::manifest(zc::mv(admitted)); }
  ZC_IREQUIRE(false, "whole manifest span must fit its document");
  ZC_UNREACHABLE
}

TargetManifest implicitTarget(identity::CrateTargetKind kind, identity::TargetName&& name,
                              identity::CanonicalRelativePath&& path,
                              const InputDocumentKey& document, zc::StringPtr source) {
  auto result = TargetManifest::from(kind, zc::mv(name), zc::mv(path), true,
                                     wholeManifestOrigin(document, source));
  ZC_IF_SOME(admitted, result) { return zc::mv(admitted); }
  ZC_IREQUIRE(false, "implicit target must satisfy closed target invariants");
  ZC_UNREACHABLE
}

struct SeenTarget final {
  identity::CrateTargetKind kind;
  zc::String name;
  zc::Array<uint8_t> path;
};

zc::Maybe<ManifestIssue> validateTarget(const TargetManifest& target,
                                        const PackageSourceInventory& inventory,
                                        zc::Vector<SeenTarget>& seen) {
  if (!inventory.containsRegularFile(target.path())) { return ManifestIssue::MissingTargetPath; }
  auto encodedPath = encodeValue(target.path());
  for (const auto& existing : seen) {
    if (existing.kind == target.kind() && existing.name == target.name()) {
      return ManifestIssue::TargetCollision;
    }
    if (existing.path.asPtr() == encodedPath.asPtr()) { return ManifestIssue::TargetPathCollision; }
  }
  seen.add(SeenTarget{target.kind(), zc::heapString(target.name()), zc::mv(encodedPath)});
  return zc::none;
}

zc::Maybe<ManifestIssue> validateTargets(zc::Maybe<TargetManifest>& library,
                                         zc::Vector<TargetManifest>& binaries,
                                         zc::Vector<TargetManifest>& tests,
                                         zc::Vector<TargetManifest>& benchmarks,
                                         zc::Vector<TargetManifest>& examples,
                                         const zc::Maybe<BuildScriptManifest>& buildScript,
                                         const PackageSourceInventory& inventory) {
  zc::Vector<SeenTarget> seen;
  ZC_IF_SOME(target, library) {
    ZC_IF_SOME(issue, validateTarget(target, inventory, seen)) { return issue; }
  }
  for (const auto& targets :
       {binaries.asPtr(), tests.asPtr(), benchmarks.asPtr(), examples.asPtr()}) {
    for (const auto& target : targets) {
      ZC_IF_SOME(issue, validateTarget(target, inventory, seen)) { return issue; }
    }
  }
  ZC_IF_SOME(build, buildScript) {
    ZC_IF_SOME(issue, validateTarget(build.target(), inventory, seen)) { return issue; }
    for (const auto& input : build.inputs()) {
      if (!inventory.containsRegularFile(input)) { return ManifestIssue::MissingTargetPath; }
    }
  }
  return zc::none;
}

uint8_t hexNibble(char value) {
  if (value >= '0' && value <= '9') { return static_cast<uint8_t>(value - '0'); }
  ZC_IREQUIRE(value >= 'a' && value <= 'f', "validated hexadecimal input must be lowercase");
  return static_cast<uint8_t>(value - 'a' + 10);
}

zc::Array<uint8_t> decodeLowerHex(zc::StringPtr text) {
  auto result = zc::heapArray<uint8_t>(text.size() / 2);
  for (size_t index = 0; index < result.size(); ++index) {
    result[index] =
        static_cast<uint8_t>((hexNibble(text[index * 2]) << 4) | hexNibble(text[index * 2 + 1]));
  }
  return result;
}

identity::SortedFeatureSet parseRequestedFeatures(const toml::table& dependency) {
  zc::Vector<identity::FeatureName> features;
  const auto* node = dependency.get("features");
  if (node != nullptr) {
    for (const auto& featureNode : *node->as_array()) {
      const auto& text = featureNode.as_string()->get();
      auto feature = identity::FeatureName::fromSource(zc::StringPtr(text.data(), text.size()));
      ZC_IF_SOME(admitted, feature) { features.add(zc::mv(admitted)); }
    }
  }
  auto result = identity::SortedFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(admitted, result) { return zc::mv(admitted); }
  ZC_IREQUIRE(false, "validated dependency features must remain unique");
  ZC_UNREACHABLE
}

VcsSelector parseVcsSelector(const toml::table& dependency) {
  if (const auto* revisionNode = dependency.get("rev")) {
    const auto& text = revisionNode->as_string()->get();
    const zc::StringPtr revisionText(text.data(), text.size());
    auto bytes = decodeLowerHex(revisionText);
    auto revision = identity::VcsRevision::from(revisionText.size() == 40
                                                    ? identity::VcsRevisionAlgorithm::Sha1
                                                    : identity::VcsRevisionAlgorithm::Sha256,
                                                bytes.asPtr());
    ZC_IF_SOME(admitted, revision) { return VcsSelector::revision(zc::mv(admitted)); }
  }
  if (const auto* tagNode = dependency.get("tag")) {
    const auto& text = tagNode->as_string()->get();
    auto selector = VcsSelector::tag(zc::StringPtr(text.data(), text.size()));
    ZC_IF_SOME(admitted, selector) { return zc::mv(admitted); }
  }
  const auto& text = dependency.get("branch")->as_string()->get();
  auto selector = VcsSelector::branch(zc::StringPtr(text.data(), text.size()));
  ZC_IF_SOME(admitted, selector) { return zc::mv(admitted); }
  ZC_IREQUIRE(false, "validated VCS selector must remain constructible");
  ZC_UNREACHABLE
}

identity::CanonicalWorkspaceRelativePath resolveDependencyPath(
    const InputDocumentKey& document, const identity::CanonicalWorkspaceRelativePath& dependency) {
  const auto& documentPath = document.path().workspacePath();
  zc::Vector<identity::CanonicalPathSegment> segments;
  for (size_t index = 0; index + 1 < documentPath.segments().size(); ++index) {
    segments.add(documentPath.segments()[index].clone());
  }
  uint32_t remainingParents = dependency.leadingParents();
  while (remainingParents != 0 && segments.size() != 0) {
    segments.removeLast();
    --remainingParents;
  }
  for (const auto& segment : dependency.segments()) { segments.add(segment.clone()); }
  return identity::CanonicalWorkspaceRelativePath::from(
      documentPath.leadingParents() + remainingParents, zc::mv(segments));
}

PackageSourceConstraint parseSourceConstraint(const toml::table& dependency,
                                              const InputDocumentKey& document) {
  if (const auto* pathNode = dependency.get("path")) {
    const auto& text = pathNode->as_string()->get();
    auto path = parseWorkspacePath(zc::StringPtr(text.data(), text.size()), true);
    ZC_IF_SOME(admitted, path) {
      return PackageSourceConstraint::localPath(resolveDependencyPath(document, admitted));
    }
  }
  if (const auto* gitNode = dependency.get("git")) {
    const auto& text = gitNode->as_string()->get();
    auto repository = identity::CanonicalUrl::fromSource(zc::StringPtr(text.data(), text.size()));
    identity::CanonicalRelativePath subdirectory = parseRelativePath(""_zc);
    if (const auto* subdirectoryNode = dependency.get("subdirectory")) {
      const auto& subdirectoryText = subdirectoryNode->as_string()->get();
      subdirectory =
          parseRelativePath(zc::StringPtr(subdirectoryText.data(), subdirectoryText.size()));
    }
    ZC_IF_SOME(admitted, repository) {
      return PackageSourceConstraint::vcs(zc::mv(admitted), parseVcsSelector(dependency),
                                          zc::mv(subdirectory));
    }
  }
  const auto& urlText = dependency.get("registry")->as_string()->get();
  auto url = identity::CanonicalUrl::fromSource(zc::StringPtr(urlText.data(), urlText.size()));
  const auto& trustText = dependency.get("trust-domain-sha256")->as_string()->get();
  auto trustBytes = decodeLowerHex(zc::StringPtr(trustText.data(), trustText.size()));
  auto trust = identity::Sha256Digest::fromBytes(trustBytes.asPtr());
  ZC_IF_SOME(admittedUrl, url) {
    ZC_IF_SOME(admittedTrust, trust) {
      return PackageSourceConstraint::registry(
          identity::RegistryIdentity::from(zc::mv(admittedUrl), admittedTrust));
    }
  }
  ZC_IREQUIRE(false, "validated package source must remain constructible");
  ZC_UNREACHABLE
}

DependencyRequirement parseDependency(const toml::key& aliasKey, const toml::node& node,
                                      identity::DependencyDomain domain,
                                      const InputDocumentKey& document, zc::StringPtr source) {
  const auto aliasText = aliasKey.str();
  auto alias =
      identity::DependencyAlias::fromSource(zc::StringPtr(aliasText.data(), aliasText.size()));
  const auto& dependency = *node.as_table();

  auto requiredPackage =
      identity::PackageName::fromSource(zc::StringPtr(aliasText.data(), aliasText.size()));
  if (const auto* packageNode = dependency.get("package")) {
    const auto& text = packageNode->as_string()->get();
    requiredPackage = identity::PackageName::fromSource(zc::StringPtr(text.data(), text.size()));
  }

  zc::Maybe<SemVerConstraint> versionCheck;
  if (const auto* versionNode = dependency.get("version")) {
    const auto& text = versionNode->as_string()->get();
    versionCheck = SemVerConstraint::parse(zc::StringPtr(text.data(), text.size()));
  }
  const auto* defaultFeaturesNode = dependency.get("default-features");
  const bool useDefaultFeatures =
      defaultFeaturesNode == nullptr || defaultFeaturesNode->as_boolean()->get();
  const auto* optionalNode = dependency.get("optional");
  const bool optional = optionalNode != nullptr && optionalNode->as_boolean()->get();

  ZC_IF_SOME(aliasValue, alias) {
    ZC_IF_SOME(packageValue, requiredPackage) {
      auto value = DependencyRequirementWithoutOrigin::from(
          zc::mv(aliasValue), zc::mv(packageValue), domain,
          parseSourceConstraint(dependency, document), zc::mv(versionCheck),
          parseRequestedFeatures(dependency), useDefaultFeatures, optional);
      ZC_IF_SOME(admitted, value) {
        return DependencyRequirement::from(zc::mv(admitted),
                                           manifestOrigin(document, source, node.source()));
      }
    }
  }
  ZC_IREQUIRE(false, "validated dependency must remain constructible");
  ZC_UNREACHABLE
}

zc::Vector<DependencyRequirement> parseDependencies(const toml::table& dependencies,
                                                    identity::DependencyDomain domain,
                                                    const InputDocumentKey& document,
                                                    zc::StringPtr source) {
  zc::Vector<DependencyRequirement> result;
  for (const auto& [alias, dependency] : dependencies) {
    result.add(parseDependency(alias, dependency, domain, document, source));
  }
  ZC_IREQUIRE(sortDependencyRequirements(result),
              "validated dependency table must remain canonically unique");
  return result;
}

}  // namespace

DiagnosticProvenance::DiagnosticProvenance(DiagnosticAnchor&& primary,
                                           zc::Vector<DiagnosticAnchor>&& related) noexcept
    : primaryValue(zc::mv(primary)), relatedValues(zc::mv(related)) {}

zc::Maybe<DiagnosticProvenance> DiagnosticProvenance::from(DiagnosticAnchor&& primary,
                                                           zc::Vector<DiagnosticAnchor>&& related) {
  for (size_t index = 1; index < related.size(); ++index) {
    auto current = zc::mv(related[index]);
    size_t insertion = index;
    while (insertion > 0 && current.encode().asPtr() < related[insertion - 1].encode().asPtr()) {
      related[insertion] = zc::mv(related[insertion - 1]);
      --insertion;
    }
    related[insertion] = zc::mv(current);
  }
  const auto primaryBytes = primary.encode();
  for (size_t index = 0; index < related.size(); ++index) {
    const auto relatedBytes = related[index].encode();
    if (relatedBytes.asPtr() == primaryBytes.asPtr() ||
        (index > 0 && relatedBytes.asPtr() == related[index - 1].encode().asPtr())) {
      return zc::none;
    }
  }
  return DiagnosticProvenance(zc::mv(primary), zc::mv(related));
}

DiagnosticProvenance DiagnosticProvenance::clone() const {
  zc::Vector<DiagnosticAnchor> related(relatedValues.size());
  for (const auto& anchor : relatedValues) { related.add(anchor.clone()); }
  return DiagnosticProvenance(primaryValue.clone(), zc::mv(related));
}

const DiagnosticAnchor& DiagnosticProvenance::primary() const noexcept { return primaryValue; }
zc::ArrayPtr<const DiagnosticAnchor> DiagnosticProvenance::related() const noexcept {
  return relatedValues.asPtr();
}

void DiagnosticProvenance::encode(identity::CanonicalEncoder& encoder) const {
  primaryValue.encode(encoder);
  encoder.encodeSequenceSize(relatedValues.size());
  for (const auto& anchor : relatedValues) { anchor.encode(encoder); }
}

ManifestFailure::ManifestFailure(DiagnosticProvenance&& provenance, ManifestIssue issue) noexcept
    : provenanceValue(zc::mv(provenance)), issueValue(issue) {}

ManifestFailure ManifestFailure::invalid(DiagnosticProvenance&& provenance, ManifestIssue issue) {
  ZC_IREQUIRE(issue >= ManifestIssue::ReadFailed && issue <= ManifestIssue::FeatureCycle,
              "manifest failure requires a closed issue");
  return ManifestFailure(zc::mv(provenance), issue);
}

ManifestFailure ManifestFailure::clone() const {
  return ManifestFailure(provenanceValue.clone(), issueValue);
}
const DiagnosticProvenance& ManifestFailure::provenance() const noexcept { return provenanceValue; }
ManifestIssue ManifestFailure::issue() const noexcept { return issueValue; }
void ManifestFailure::encode(identity::CanonicalEncoder& encoder) const {
  encoder.encodeUint8(0x01);
  provenanceValue.encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(issueValue));
}

struct NormalizedManifest::Impl final {
  explicit Impl(InputDocumentKey&& document) noexcept : document(zc::mv(document)) {}

  InputDocumentKey document;
  zc::Maybe<PackageManifest> package;
  zc::Maybe<WorkspaceManifest> workspace;
  zc::Maybe<DiagnosticAnchor> packageNameOrigin;
  zc::Maybe<DiagnosticAnchor> workspaceOrigin;
  zc::Vector<DiagnosticAnchor> workspaceMemberOrigins;
  zc::Maybe<TargetManifest> library;
  zc::Vector<TargetManifest> binaries;
  zc::Vector<TargetManifest> tests;
  zc::Vector<TargetManifest> benchmarks;
  zc::Vector<TargetManifest> examples;
  zc::Maybe<BuildScriptManifest> buildScript;
  zc::Vector<DependencyRequirement> targetDependencies;
  zc::Vector<DependencyRequirement> developmentDependencies;
  zc::Vector<DependencyRequirement> buildDependencies;
  zc::Vector<FeatureManifest> features;
};

NormalizedManifest::NormalizedManifest(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
NormalizedManifest::~NormalizedManifest() noexcept(false) = default;
NormalizedManifest::NormalizedManifest(NormalizedManifest&&) noexcept = default;
NormalizedManifest& NormalizedManifest::operator=(NormalizedManifest&&) noexcept = default;

NormalizedManifest NormalizedManifest::clone() const {
  auto copy = zc::heap<Impl>(impl->document.clone());
  ZC_IF_SOME(value, impl->package) { copy->package = value.clone(); }
  ZC_IF_SOME(value, impl->workspace) { copy->workspace = value.clone(); }
  ZC_IF_SOME(value, impl->packageNameOrigin) { copy->packageNameOrigin = value.clone(); }
  ZC_IF_SOME(value, impl->workspaceOrigin) { copy->workspaceOrigin = value.clone(); }
  for (const auto& value : impl->workspaceMemberOrigins) {
    copy->workspaceMemberOrigins.add(value.clone());
  }
  ZC_IF_SOME(value, impl->library) { copy->library = value.clone(); }
  for (const auto& value : impl->binaries) { copy->binaries.add(value.clone()); }
  for (const auto& value : impl->tests) { copy->tests.add(value.clone()); }
  for (const auto& value : impl->benchmarks) { copy->benchmarks.add(value.clone()); }
  for (const auto& value : impl->examples) { copy->examples.add(value.clone()); }
  ZC_IF_SOME(value, impl->buildScript) { copy->buildScript = value.clone(); }
  for (const auto& value : impl->targetDependencies) {
    copy->targetDependencies.add(value.clone());
  }
  for (const auto& value : impl->developmentDependencies) {
    copy->developmentDependencies.add(value.clone());
  }
  for (const auto& value : impl->buildDependencies) { copy->buildDependencies.add(value.clone()); }
  for (const auto& value : impl->features) { copy->features.add(value.clone()); }
  return NormalizedManifest(zc::mv(copy));
}

bool NormalizedManifest::hasPackage() const noexcept { return impl->package != zc::none; }
bool NormalizedManifest::hasWorkspace() const noexcept { return impl->workspace != zc::none; }

zc::StringPtr NormalizedManifest::packageName() const noexcept {
  ZC_IF_SOME(package, impl->package) { return package.name(); }
  ZC_IREQUIRE(false, "packageName requires a package manifest");
  ZC_UNREACHABLE
}

zc::StringPtr NormalizedManifest::packageVersion() const noexcept {
  ZC_IF_SOME(package, impl->package) { return package.version(); }
  ZC_IREQUIRE(false, "packageVersion requires a package manifest");
  ZC_UNREACHABLE
}

uint32_t NormalizedManifest::editionYear() const noexcept {
  ZC_IF_SOME(package, impl->package) { return package.editionYear(); }
  ZC_IREQUIRE(false, "editionYear requires a package manifest");
  ZC_UNREACHABLE
}

size_t NormalizedManifest::workspaceMemberCount() const noexcept {
  ZC_IF_SOME(workspace, impl->workspace) { return workspace.members().size(); }
  ZC_IREQUIRE(false, "workspaceMemberCount requires a workspace manifest");
  ZC_UNREACHABLE
}

zc::ArrayPtr<const identity::CanonicalWorkspaceRelativePath> NormalizedManifest::workspaceMembers()
    const noexcept {
  ZC_IF_SOME(workspace, impl->workspace) { return workspace.members(); }
  ZC_IREQUIRE(false, "workspaceMembers requires a workspace manifest");
  ZC_UNREACHABLE
}

const DiagnosticAnchor& NormalizedManifest::packageNameOrigin() const {
  ZC_IF_SOME(origin, impl->packageNameOrigin) { return origin; }
  ZC_IREQUIRE(false, "packageNameOrigin requires a package manifest");
  ZC_UNREACHABLE
}

const DiagnosticAnchor& NormalizedManifest::workspaceOrigin() const {
  ZC_IF_SOME(origin, impl->workspaceOrigin) { return origin; }
  ZC_IREQUIRE(false, "workspaceOrigin requires a workspace manifest");
  ZC_UNREACHABLE
}

const DiagnosticAnchor& NormalizedManifest::workspaceMemberOrigin(size_t index) const {
  ZC_IREQUIRE(index < impl->workspaceMemberOrigins.size(), "workspace member index out of bounds");
  return impl->workspaceMemberOrigins[index];
}

bool NormalizedManifest::hasLibrary() const noexcept { return impl->library != zc::none; }
const TargetManifest& NormalizedManifest::library() const {
  ZC_IF_SOME(value, impl->library) { return value; }
  ZC_IREQUIRE(false, "library requires a present target");
  ZC_UNREACHABLE
}
zc::ArrayPtr<const TargetManifest> NormalizedManifest::binaries() const noexcept {
  return impl->binaries.asPtr();
}
zc::ArrayPtr<const TargetManifest> NormalizedManifest::tests() const noexcept {
  return impl->tests.asPtr();
}
zc::ArrayPtr<const TargetManifest> NormalizedManifest::benchmarks() const noexcept {
  return impl->benchmarks.asPtr();
}
zc::ArrayPtr<const TargetManifest> NormalizedManifest::examples() const noexcept {
  return impl->examples.asPtr();
}
bool NormalizedManifest::hasBuildScript() const noexcept { return impl->buildScript != zc::none; }
const BuildScriptManifest& NormalizedManifest::buildScript() const {
  ZC_IF_SOME(value, impl->buildScript) { return value; }
  ZC_IREQUIRE(false, "buildScript requires a present contract");
  ZC_UNREACHABLE
}

zc::ArrayPtr<const DependencyRequirement> NormalizedManifest::targetDependencies() const noexcept {
  return impl->targetDependencies.asPtr();
}
zc::ArrayPtr<const DependencyRequirement> NormalizedManifest::developmentDependencies()
    const noexcept {
  return impl->developmentDependencies.asPtr();
}
zc::ArrayPtr<const DependencyRequirement> NormalizedManifest::buildDependencies() const noexcept {
  return impl->buildDependencies.asPtr();
}
size_t NormalizedManifest::featureCount() const noexcept { return impl->features.size(); }
zc::ArrayPtr<const FeatureManifest> NormalizedManifest::features() const noexcept {
  return impl->features;
}

namespace {

template <typename Value>
void encodeOptional(identity::CanonicalEncoder& encoder, const zc::Maybe<Value>& value) {
  ZC_IF_SOME(admitted, value) {
    encoder.encodeSome();
    admitted.encode(encoder);
  }
  else { encoder.encodeNone(); }
}

zc::Maybe<PackageManifest> clonePackage(const zc::Maybe<PackageManifest>& value) {
  ZC_IF_SOME(admitted, value) { return admitted.clone(); }
  return zc::none;
}

zc::Maybe<WorkspaceManifest> cloneWorkspace(const zc::Maybe<WorkspaceManifest>& value) {
  ZC_IF_SOME(admitted, value) { return admitted.clone(); }
  return zc::none;
}

zc::Maybe<CanonicalTargetManifest> canonicalTarget(const zc::Maybe<TargetManifest>& value) {
  ZC_IF_SOME(admitted, value) { return CanonicalTargetManifest::from(admitted); }
  return zc::none;
}

zc::Maybe<CanonicalTargetManifest> cloneCanonicalTarget(
    const zc::Maybe<CanonicalTargetManifest>& value) {
  ZC_IF_SOME(admitted, value) { return admitted.clone(); }
  return zc::none;
}

zc::Maybe<CanonicalBuildScriptManifest> canonicalBuildScript(
    const zc::Maybe<BuildScriptManifest>& value) {
  ZC_IF_SOME(admitted, value) { return CanonicalBuildScriptManifest::from(admitted); }
  return zc::none;
}

zc::Maybe<CanonicalBuildScriptManifest> cloneCanonicalBuildScript(
    const zc::Maybe<CanonicalBuildScriptManifest>& value) {
  ZC_IF_SOME(admitted, value) { return admitted.clone(); }
  return zc::none;
}

zc::Vector<CanonicalTargetManifest> canonicalTargets(zc::ArrayPtr<const TargetManifest> source) {
  zc::Vector<CanonicalTargetManifest> result(source.size());
  for (const auto& target : source) { result.add(CanonicalTargetManifest::from(target)); }
  return result;
}

zc::Vector<CanonicalTargetManifest> cloneCanonicalTargets(
    zc::ArrayPtr<const CanonicalTargetManifest> source) {
  zc::Vector<CanonicalTargetManifest> result(source.size());
  for (const auto& target : source) { result.add(target.clone()); }
  return result;
}

zc::Vector<DependencyRequirementWithoutOrigin> canonicalDependencies(
    zc::ArrayPtr<const DependencyRequirement> source) {
  zc::Vector<DependencyRequirementWithoutOrigin> result(source.size());
  for (const auto& requirement : source) { result.add(requirement.withoutOrigin().clone()); }
  return result;
}

zc::Vector<DependencyRequirementWithoutOrigin> cloneCanonicalDependencies(
    zc::ArrayPtr<const DependencyRequirementWithoutOrigin> source) {
  zc::Vector<DependencyRequirementWithoutOrigin> result(source.size());
  for (const auto& requirement : source) { result.add(requirement.clone()); }
  return result;
}

zc::Vector<CanonicalFeatureManifest> canonicalFeatures(zc::ArrayPtr<const FeatureManifest> source) {
  zc::Vector<CanonicalFeatureManifest> result(source.size());
  for (const auto& feature : source) { result.add(CanonicalFeatureManifest::from(feature)); }
  return result;
}

zc::Vector<CanonicalFeatureManifest> cloneCanonicalFeatures(
    zc::ArrayPtr<const CanonicalFeatureManifest> source) {
  zc::Vector<CanonicalFeatureManifest> result(source.size());
  for (const auto& feature : source) { result.add(feature.clone()); }
  return result;
}

template <typename Value>
void encodeSequence(identity::CanonicalEncoder& encoder, zc::ArrayPtr<const Value> values) {
  encoder.encodeSequenceSize(values.size());
  for (const auto& value : values) { value.encode(encoder); }
}

}  // namespace

CanonicalManifestRecord::CanonicalManifestRecord(
    zc::Maybe<PackageManifest>&& package, zc::Maybe<WorkspaceManifest>&& workspace,
    zc::Maybe<CanonicalTargetManifest>&& library, zc::Vector<CanonicalTargetManifest>&& binaries,
    zc::Vector<CanonicalTargetManifest>&& tests, zc::Vector<CanonicalTargetManifest>&& benchmarks,
    zc::Vector<CanonicalTargetManifest>&& examples,
    zc::Maybe<CanonicalBuildScriptManifest>&& buildScript,
    zc::Vector<DependencyRequirementWithoutOrigin>&& targetDependencies,
    zc::Vector<DependencyRequirementWithoutOrigin>&& developmentDependencies,
    zc::Vector<DependencyRequirementWithoutOrigin>&& buildDependencies,
    zc::Vector<CanonicalFeatureManifest>&& features) noexcept
    : packageValue(zc::mv(package)),
      workspaceValue(zc::mv(workspace)),
      libraryValue(zc::mv(library)),
      binaryValues(zc::mv(binaries)),
      testValues(zc::mv(tests)),
      benchmarkValues(zc::mv(benchmarks)),
      exampleValues(zc::mv(examples)),
      buildScriptValue(zc::mv(buildScript)),
      targetDependencyValues(zc::mv(targetDependencies)),
      developmentDependencyValues(zc::mv(developmentDependencies)),
      buildDependencyValues(zc::mv(buildDependencies)),
      featureValues(zc::mv(features)) {}

CanonicalManifestRecord CanonicalManifestRecord::from(const NormalizedManifest& source) {
  return CanonicalManifestRecord(
      clonePackage(source.impl->package), cloneWorkspace(source.impl->workspace),
      canonicalTarget(source.impl->library), canonicalTargets(source.impl->binaries.asPtr()),
      canonicalTargets(source.impl->tests.asPtr()),
      canonicalTargets(source.impl->benchmarks.asPtr()),
      canonicalTargets(source.impl->examples.asPtr()),
      canonicalBuildScript(source.impl->buildScript),
      canonicalDependencies(source.impl->targetDependencies.asPtr()),
      canonicalDependencies(source.impl->developmentDependencies.asPtr()),
      canonicalDependencies(source.impl->buildDependencies.asPtr()),
      canonicalFeatures(source.impl->features.asPtr()));
}

CanonicalManifestRecord CanonicalManifestRecord::forResolver(
    PackageManifest&& package, zc::Maybe<CanonicalTargetManifest>&& library,
    zc::Vector<DependencyRequirementWithoutOrigin>&& targetDependencies,
    zc::Vector<DependencyRequirementWithoutOrigin>&& developmentDependencies,
    zc::Vector<DependencyRequirementWithoutOrigin>&& buildDependencies,
    zc::Vector<CanonicalFeatureManifest>&& features) {
  zc::Vector<CanonicalTargetManifest> binaries;
  zc::Vector<CanonicalTargetManifest> tests;
  zc::Vector<CanonicalTargetManifest> benchmarks;
  zc::Vector<CanonicalTargetManifest> examples;
  return CanonicalManifestRecord(zc::mv(package), zc::none, zc::mv(library), zc::mv(binaries),
                                 zc::mv(tests), zc::mv(benchmarks), zc::mv(examples), zc::none,
                                 zc::mv(targetDependencies), zc::mv(developmentDependencies),
                                 zc::mv(buildDependencies), zc::mv(features));
}

CanonicalManifestRecord CanonicalManifestRecord::clone() const {
  return CanonicalManifestRecord(
      clonePackage(packageValue), cloneWorkspace(workspaceValue),
      cloneCanonicalTarget(libraryValue), cloneCanonicalTargets(binaryValues.asPtr()),
      cloneCanonicalTargets(testValues.asPtr()), cloneCanonicalTargets(benchmarkValues.asPtr()),
      cloneCanonicalTargets(exampleValues.asPtr()), cloneCanonicalBuildScript(buildScriptValue),
      cloneCanonicalDependencies(targetDependencyValues.asPtr()),
      cloneCanonicalDependencies(developmentDependencyValues.asPtr()),
      cloneCanonicalDependencies(buildDependencyValues.asPtr()),
      cloneCanonicalFeatures(featureValues.asPtr()));
}

bool CanonicalManifestRecord::hasLibrary() const noexcept { return libraryValue != zc::none; }
zc::ArrayPtr<const DependencyRequirementWithoutOrigin> CanonicalManifestRecord::targetDependencies()
    const noexcept {
  return targetDependencyValues;
}
zc::ArrayPtr<const DependencyRequirementWithoutOrigin>
CanonicalManifestRecord::developmentDependencies() const noexcept {
  return developmentDependencyValues;
}
zc::ArrayPtr<const DependencyRequirementWithoutOrigin> CanonicalManifestRecord::buildDependencies()
    const noexcept {
  return buildDependencyValues;
}
zc::ArrayPtr<const CanonicalFeatureManifest> CanonicalManifestRecord::features() const noexcept {
  return featureValues;
}

void CanonicalManifestRecord::encode(identity::CanonicalEncoder& encoder) const {
  encodeOptional(encoder, packageValue);
  encodeOptional(encoder, workspaceValue);
  encodeOptional(encoder, libraryValue);
  encodeSequence(encoder, binaryValues.asPtr());
  encodeSequence(encoder, testValues.asPtr());
  encodeSequence(encoder, benchmarkValues.asPtr());
  encodeSequence(encoder, exampleValues.asPtr());
  encodeOptional(encoder, buildScriptValue);
  encodeSequence(encoder, targetDependencyValues.asPtr());
  encodeSequence(encoder, developmentDependencyValues.asPtr());
  encodeSequence(encoder, buildDependencyValues.asPtr());
  encodeSequence(encoder, featureValues.asPtr());
}

zc::Array<uint8_t> CanonicalManifestRecord::encode() const {
  identity::CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

struct ManifestParser::Impl final {};

ManifestParser::ManifestParser() : impl(zc::heap<Impl>()) {}
ManifestParser::~ManifestParser() noexcept(false) = default;
ManifestParser::ManifestParser(ManifestParser&&) noexcept = default;
ManifestParser& ManifestParser::operator=(ManifestParser&&) noexcept = default;

ManifestParseResult ManifestParser::parseWorkspaceManifest(
    identity::CanonicalWorkspaceRelativePath&& documentPath, zc::StringPtr source,
    const PackageSourceInventory& inventory) const {
  auto failurePath = documentPath.clone();
  auto result = parseWorkspaceManifestRaw(zc::mv(documentPath), source, inventory);
  if (result.is<NormalizedManifest>()) { return zc::mv(result.get<NormalizedManifest>()); }

  zc::Vector<uint8_t> digestInput(source.size());
  for (char value : source) { digestInput.add(static_cast<uint8_t>(value)); }
  auto digest = identity::sha256(digestInput.asPtr());
  ZC_IREQUIRE(digest != zc::none, "manifest byte length must fit SHA-256");
  const auto error = result.get<ManifestParseError>();
  ZC_IF_SOME(digestValue, digest) {
    auto document =
        InputDocumentKey::from(InputDocumentKind::Manifest,
                               DiagnosticDocumentPath::workspace(zc::mv(failurePath)), digestValue);
    ZC_IF_SOME(documentValue, document) {
      auto span =
          ManifestSpan::from(zc::mv(documentValue), source.size(), error.byteStart, error.byteEnd);
      ZC_IF_SOME(spanValue, span) {
        zc::Vector<DiagnosticAnchor> related;
        auto provenance = DiagnosticProvenance::from(DiagnosticAnchor::manifest(zc::mv(spanValue)),
                                                     zc::mv(related));
        ZC_IF_SOME(provenanceValue, provenance) {
          return ManifestFailure::invalid(zc::mv(provenanceValue), error.issue);
        }
      }
    }
  }
  ZC_IREQUIRE(false, "manifest parse error must produce bounded diagnostic provenance");
  ZC_UNREACHABLE
}

RawManifestParseResult ManifestParser::parseWorkspaceManifestRaw(
    identity::CanonicalWorkspaceRelativePath&& documentPath, zc::StringPtr source,
    const PackageSourceInventory& inventory) const {
  if (source.size() >= 3 && static_cast<uint8_t>(source[0]) == 0xef &&
      static_cast<uint8_t>(source[1]) == 0xbb && static_cast<uint8_t>(source[2]) == 0xbf) {
    return ManifestParseError{ManifestIssue::ByteOrderMarkPresent, 0, 3};
  }
  if (identity::isNfc(source) == zc::none) {
    return wholeDocumentError(ManifestIssue::InvalidUtf8, source);
  }

  const std::string_view view(source.begin(), source.size());
  auto parsed = toml::parse(view);
  if (!parsed) { return errorAt(ManifestIssue::TomlSyntax, source, parsed.error().source()); }
  const toml::table& root = parsed.table();
  ZC_IF_SOME(issue, validateTopLevel(root, source)) { return issue; }

  zc::Vector<uint8_t> digestInput(source.size());
  for (char value : source) { digestInput.add(static_cast<uint8_t>(value)); }
  auto digest = identity::sha256(digestInput.asPtr());
  ZC_IREQUIRE(digest != zc::none, "manifest byte length must fit SHA-256");
  zc::Own<NormalizedManifest::Impl> normalized;
  ZC_IF_SOME(digestValue, digest) {
    auto document = InputDocumentKey::from(InputDocumentKind::Manifest,
                                           DiagnosticDocumentPath::workspace(zc::mv(documentPath)),
                                           digestValue);
    ZC_IF_SOME(documentValue, document) {
      normalized = zc::heap<NormalizedManifest::Impl>(zc::mv(documentValue));
    }
  }

  const auto* packageTable = root.get_as<toml::table>("package");
  if (root.contains("package") && packageTable == nullptr) {
    return errorAt(ManifestIssue::WrongValueType, source, root["package"].node()->source());
  }
  if (packageTable != nullptr) {
    auto package = parsePackage(*packageTable, source);
    if (package.is<ManifestParseError>()) { return package.get<ManifestParseError>(); }
    normalized->package = zc::mv(package.get<PackageManifest>());
    normalized->packageNameOrigin =
        manifestOrigin(normalized->document, source, packageTable->get("name")->source());
  }

  const auto* workspaceTable = root.get_as<toml::table>("workspace");
  if (root.contains("workspace") && workspaceTable == nullptr) {
    return errorAt(ManifestIssue::WrongValueType, source, root["workspace"].node()->source());
  }
  if (normalized->package == zc::none && workspaceTable == nullptr) {
    return wholeDocumentError(ManifestIssue::MissingRequiredKey, source);
  }
  if (workspaceTable != nullptr) {
    ZC_IF_SOME(issue, validateWorkspace(*workspaceTable, normalized->package != zc::none, source)) {
      return issue;
    }
    auto workspace = parseWorkspace(*workspaceTable, source);
    if (workspace.is<ManifestParseError>()) { return workspace.get<ManifestParseError>(); }
    normalized->workspace = zc::mv(workspace.get<WorkspaceManifest>());
    normalized->workspaceOrigin =
        manifestOrigin(normalized->document, source, workspaceTable->source());
    const auto* membersNode = workspaceTable->get("members");
    if (membersNode != nullptr) {
      ZC_IF_SOME(workspaceValue, normalized->workspace) {
        for (const auto& canonicalMember : workspaceValue.members()) {
          const auto canonicalBytes = encodeValue(canonicalMember);
          bool found = false;
          for (const auto& memberNode : *membersNode->as_array()) {
            const auto& text = memberNode.as_string()->get();
            auto memberPath = parseWorkspacePath(zc::StringPtr(text.data(), text.size()), false);
            ZC_IF_SOME(admittedPath, memberPath) {
              if (encodeValue(admittedPath).asPtr() == canonicalBytes.asPtr()) {
                normalized->workspaceMemberOrigins.add(
                    manifestOrigin(normalized->document, source, memberNode.source()));
                found = true;
                break;
              }
            }
          }
          if (!found) { ZC_UNREACHABLE }
        }
      }
    }
  }

  if (normalized->package == zc::none) {
    for (const auto key : {"lib", "bin", "test", "bench", "example", "build", "dependencies",
                           "dev-dependencies", "build-dependencies", "features"}) {
      if (const auto* node = root.get(key)) {
        return errorAt(ManifestIssue::MissingRequiredKey, source, node->source());
      }
    }
  }

  const auto* libraryNode = root.get("lib");
  if (libraryNode != nullptr) {
    const auto* library = libraryNode->as_table();
    if (library == nullptr) {
      return errorAt(ManifestIssue::WrongValueType, source, libraryNode->source());
    }
    ZC_IF_SOME(issue, validateTargetTable(*library, false, source)) { return issue; }
    ZC_IF_SOME(package, normalized->package) {
      if (library->get("name") == nullptr &&
          identity::TargetName::fromSource(package.name()) == zc::none) {
        return errorAt(ManifestIssue::InvalidStrongScalar, source, libraryNode->source());
      }
      normalized->library = parseTarget(*library, identity::CrateTargetKind::Library,
                                        package.name(), normalized->document, source);
    }
  }
  for (const auto key : {"bin", "test", "bench", "example"}) {
    const auto* targets = root.get(key);
    if (targets != nullptr) {
      ZC_IF_SOME(issue, validateRepeatedTargets(*targets, source)) { return issue; }
      identity::CrateTargetKind kind = identity::CrateTargetKind::Binary;
      if (zc::StringPtr(key) == "test"_zc) {
        kind = identity::CrateTargetKind::Test;
      } else if (zc::StringPtr(key) == "bench"_zc) {
        kind = identity::CrateTargetKind::Benchmark;
      } else if (zc::StringPtr(key) == "example"_zc) {
        kind = identity::CrateTargetKind::Example;
      }
      auto parsedTargets =
          parseRepeatedTargets(*targets->as_array(), kind, normalized->document, source);
      if (kind == identity::CrateTargetKind::Binary) {
        normalized->binaries = zc::mv(parsedTargets);
      } else if (kind == identity::CrateTargetKind::Test) {
        normalized->tests = zc::mv(parsedTargets);
      } else if (kind == identity::CrateTargetKind::Benchmark) {
        normalized->benchmarks = zc::mv(parsedTargets);
      } else {
        normalized->examples = zc::mv(parsedTargets);
      }
    }
  }
  const auto* buildNode = root.get("build");
  if (buildNode != nullptr) {
    const auto* build = buildNode->as_table();
    if (build == nullptr) {
      return errorAt(ManifestIssue::WrongValueType, source, buildNode->source());
    }
    ZC_IF_SOME(issue, validateBuild(*build, source)) { return issue; }
    normalized->buildScript = parseBuildScript(*build, normalized->document, source);
  }

  ZC_IF_SOME(package, normalized->package) {
    auto packageTargetName = identity::TargetName::fromSource(package.name());
    if (normalized->library == zc::none) {
      auto path = parseRelativePath("src/lib.zom"_zc);
      if (inventory.containsRegularFile(path)) {
        ZC_IF_SOME(name, packageTargetName) {
          normalized->library = implicitTarget(identity::CrateTargetKind::Library, name.clone(),
                                               zc::mv(path), normalized->document, source);
        }
      }
    }
    auto mainPath = parseRelativePath("src/main.zom"_zc);
    if (inventory.containsRegularFile(mainPath)) {
      ZC_IF_SOME(name, packageTargetName) {
        bool alreadyNamed = false;
        for (const auto& binary : normalized->binaries) {
          if (binary.name() == name.text()) {
            alreadyNamed = true;
            break;
          }
        }
        if (!alreadyNamed) {
          normalized->binaries.add(implicitTarget(identity::CrateTargetKind::Binary, name.clone(),
                                                  zc::mv(mainPath), normalized->document, source));
          ZC_IREQUIRE(sortTargetManifests(normalized->binaries),
                      "implicit binary must remain canonically unique");
        }
      }
    }
  }
  ZC_IF_SOME(issue, validateTargets(normalized->library, normalized->binaries, normalized->tests,
                                    normalized->benchmarks, normalized->examples,
                                    normalized->buildScript, inventory)) {
    return wholeDocumentError(issue, source);
  }

  const auto* dependencies = root.get_as<toml::table>("dependencies");
  if (root.contains("dependencies") && dependencies == nullptr) {
    return errorAt(ManifestIssue::WrongValueType, source, root["dependencies"].node()->source());
  }
  if (dependencies != nullptr) {
    ZC_IF_SOME(issue, validateDependencyTable(*dependencies, true, source)) { return issue; }
  }
  if (dependencies != nullptr) {
    normalized->targetDependencies = parseDependencies(
        *dependencies, identity::DependencyDomain::Target, normalized->document, source);
  }
  for (const auto key : {"dev-dependencies", "build-dependencies"}) {
    const auto* dependencyNode = root.get(key);
    if (dependencyNode == nullptr) { continue; }
    const auto* dependencyTable = dependencyNode->as_table();
    if (dependencyTable == nullptr) {
      return errorAt(ManifestIssue::WrongValueType, source, dependencyNode->source());
    }
    ZC_IF_SOME(issue, validateDependencyTable(*dependencyTable, false, source)) { return issue; }
    auto values = parseDependencies(*dependencyTable,
                                    zc::StringPtr(key) == "dev-dependencies"_zc
                                        ? identity::DependencyDomain::Development
                                        : identity::DependencyDomain::Build,
                                    normalized->document, source);
    if (zc::StringPtr(key) == "dev-dependencies"_zc) {
      normalized->developmentDependencies = zc::mv(values);
    } else {
      normalized->buildDependencies = zc::mv(values);
    }
  }
  const auto* features = root.get_as<toml::table>("features");
  if (root.contains("features") && features == nullptr) {
    return errorAt(ManifestIssue::WrongValueType, source, root["features"].node()->source());
  }
  if (features != nullptr) {
    ZC_IF_SOME(issue, validateFeatures(*features, dependencies, source)) { return issue; }
    normalized->features = parseFeatures(*features, normalized->document, source);
  }
  return NormalizedManifest(zc::mv(normalized));
}

}  // namespace zomlang::compiler::driver::package
