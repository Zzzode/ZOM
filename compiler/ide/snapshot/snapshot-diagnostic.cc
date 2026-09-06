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

#include "compiler/ide/snapshot/snapshot-diagnostic.h"

#include "compiler/diagnostics/core/diagnostic-info.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::ide {
namespace {

zc::Array<zc::String> copyArguments(zc::ArrayPtr<const zc::String> arguments) {
  auto copies = zc::heapArray<zc::String>(arguments.size());
  for (size_t index = 0; index < arguments.size(); ++index) {
    copies[index] = zc::heapString(arguments[index]);
  }
  return copies;
}

bool equalArguments(zc::ArrayPtr<const zc::String> left,
                    zc::ArrayPtr<const zc::String> right) noexcept {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index] != right[index]) { return false; }
  }
  return true;
}

}  // namespace

SnapshotDiagnosticLocation SnapshotDiagnosticLocation::source(zc::Array<uint8_t>&& identityBytes,
                                                              SnapshotRange range) {
  return SnapshotDiagnosticLocation(SnapshotDiagnosticResourceKind::Source, zc::mv(identityBytes),
                                    range);
}

SnapshotDiagnosticLocation SnapshotDiagnosticLocation::document(zc::Array<uint8_t>&& identityBytes,
                                                                SnapshotRange range) {
  return SnapshotDiagnosticLocation(SnapshotDiagnosticResourceKind::Document, zc::mv(identityBytes),
                                    range);
}

SnapshotDiagnosticLocation SnapshotDiagnosticLocation::clone() const {
  return SnapshotDiagnosticLocation(
      resourceKindValue, zc::heapArray<uint8_t>(resourceIdentityBytesValue.asPtr()), rangeValue);
}

bool SnapshotDiagnosticLocation::operator==(
    const SnapshotDiagnosticLocation& other) const noexcept {
  return resourceKindValue == other.resourceKindValue &&
         resourceIdentityBytesValue == other.resourceIdentityBytesValue &&
         rangeValue == other.rangeValue;
}

SnapshotDiagnosticRelated SnapshotDiagnosticRelated::highlight(
    SnapshotDiagnosticLocation&& location) {
  return SnapshotDiagnosticRelated(SnapshotDiagnosticRelatedRole::Highlight, zc::none,
                                   zc::mv(location), zc::Array<zc::String>());
}

SnapshotDiagnosticRelated SnapshotDiagnosticRelated::note(
    diagnostics::DiagID code, SnapshotDiagnosticLocation&& location,
    zc::ArrayPtr<const zc::String> arguments) {
  return SnapshotDiagnosticRelated(SnapshotDiagnosticRelatedRole::Note, code, zc::mv(location),
                                   copyArguments(arguments));
}

SnapshotDiagnosticRelated SnapshotDiagnosticRelated::previousDeclaration(
    SnapshotDiagnosticLocation&& location) {
  return SnapshotDiagnosticRelated(SnapshotDiagnosticRelatedRole::PreviousDeclaration,
                                   diagnostics::DiagID::PreviousDeclarationHere, zc::mv(location),
                                   zc::Array<zc::String>());
}

SnapshotDiagnosticRelated SnapshotDiagnosticRelated::clone() const {
  return SnapshotDiagnosticRelated(roleValue, codeValue, locationValue.clone(),
                                   copyArguments(argumentValues.asPtr()));
}

bool SnapshotDiagnosticRelated::operator==(const SnapshotDiagnosticRelated& other) const noexcept {
  return roleValue == other.roleValue && codeValue == other.codeValue &&
         locationValue == other.locationValue &&
         equalArguments(argumentValues.asPtr(), other.argumentValues.asPtr());
}

SnapshotDiagnostic SnapshotDiagnostic::project(diagnostics::DiagID code,
                                               SnapshotDiagnosticLocation&& primary,
                                               zc::ArrayPtr<const zc::String> arguments,
                                               zc::Array<SnapshotDiagnosticRelated>&& related) {
  return SnapshotDiagnostic(code, diagnostics::getDiagnosticInfo(code).severity, zc::mv(primary),
                            copyArguments(arguments), zc::mv(related));
}

SnapshotDiagnostic SnapshotDiagnostic::clone() const {
  zc::Vector<SnapshotDiagnosticRelated> related(relatedValues.size());
  for (const auto& item : relatedValues) { related.add(item.clone()); }
  return SnapshotDiagnostic(codeValue, severityValue, primaryValue.clone(),
                            copyArguments(argumentValues.asPtr()), related.releaseAsArray());
}

bool SnapshotDiagnostic::operator==(const SnapshotDiagnostic& other) const noexcept {
  if (codeValue != other.codeValue || severityValue != other.severityValue ||
      primaryValue != other.primaryValue ||
      !equalArguments(argumentValues.asPtr(), other.argumentValues.asPtr()) ||
      relatedValues.size() != other.relatedValues.size()) {
    return false;
  }
  for (size_t index = 0; index < relatedValues.size(); ++index) {
    if (relatedValues[index] != other.relatedValues[index]) { return false; }
  }
  return true;
}

}  // namespace zomlang::compiler::ide
