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

#include "compiler/driver/package/package-diagnostic.h"

#include "compiler/identity/canonical/canonical-encoder.h"
#include "zc/core/encoding.h"
#include "zc/core/memory.h"

namespace zomlang::compiler::driver::package {
namespace {

zc::String escapePath(zc::ArrayPtr<const identity::CanonicalPathSegment> segments,
                      uint32_t leadingParents) {
  zc::Vector<char> output;
  for (uint32_t index = 0; index < leadingParents; ++index) { output.addAll("../"_zc); }
  bool needsSeparator = false;
  for (const auto& segment : segments) {
    if (needsSeparator) { output.add('/'); }
    const auto escaped = diagnostics::escapeDiagnosticText(segment.text().asBytes());
    output.addAll(escaped);
    needsSeparator = true;
  }
  return zc::str(output.releaseAsArray());
}

zc::String documentDisplayName(const DiagnosticDocumentPath& path) {
  if (path.kind() == DiagnosticDocumentPathKind::Workspace) {
    const auto& workspace = path.workspacePath();
    return escapePath(workspace.segments(), workspace.leadingParents());
  }
  return zc::str("package:"_zc, zc::encodeHex(path.packageSourceDigest().bytes()), "/"_zc,
                 escapePath(path.packageRelativePath().segments(), 0));
}

zc::Array<uint8_t> manifestOccurrence(const ManifestFailure& failure) {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString("zom.package-manifest-diagnostic"_zc.asBytes());
  failure.encode(encoder);
  return encoder.finish();
}

zc::Array<uint8_t> toolchainRootOccurrence(const PackageToolchainModuleRootFailure& failure) {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString("zom.package-toolchain-root-diagnostic"_zc.asBytes());
  encoder.encodeUint8(static_cast<uint8_t>(failure.producer()));
  failure.package().encode(encoder);
  failure.fieldPath().encode(encoder);
  failure.argument().encode(encoder);
  return encoder.finish();
}

zc::Maybe<uint64_t> documentLength(zc::ArrayPtr<const PackageDiagnosticDocument> documents,
                                   const InputDocumentKey& key) {
  const auto encoded = key.encode();
  for (const auto& document : documents) {
    if (document.key().encode().asPtr() == encoded.asPtr()) {
      return document.sourceView().originalSize();
    }
  }
  return zc::none;
}

bool addDocumentAuthority(PackageDiagnosticFactProjection& projection,
                          zc::ArrayPtr<const PackageDiagnosticDocument> documents,
                          const InputDocumentKey& key) {
  const auto encoded = key.encode();
  auto length = documentLength(documents, key);
  if (length == zc::none) { return false; }
  for (const auto& authority : projection.documents) {
    if (authority.identity.asPtr() == encoded.asPtr()) {
      return authority.byteLength == ZC_ASSERT_NONNULL(length);
    }
  }
  projection.documents.add(diagnostics::DiagnosticDocumentAuthority{
      zc::heapArray<uint8_t>(encoded.asPtr()), ZC_ASSERT_NONNULL(length)});
  return true;
}

zc::Maybe<diagnostics::DiagnosticProvenanceKey> documentSite(
    const InputDocumentKey& document, zc::ArrayPtr<const uint8_t> occurrence,
    diagnostics::DocumentDiagnosticSiteRole role, uint32_t ordinal) {
  return diagnostics::DiagnosticProvenanceKey::documentSite(
      document.encode(), zc::heapArray<uint8_t>(occurrence), role, ordinal);
}

zc::Maybe<diagnostics::DiagnosticProvenanceKey> addSpan(
    PackageDiagnosticFactProjection& projection,
    zc::ArrayPtr<const PackageDiagnosticDocument> documents, const ManifestSpan& span,
    zc::ArrayPtr<const uint8_t> occurrence, diagnostics::DocumentDiagnosticSiteRole role,
    uint32_t ordinal) {
  auto length = documentLength(documents, span.document());
  if (length == zc::none || span.byteStart() > span.byteEnd() ||
      span.byteEnd() > ZC_ASSERT_NONNULL(length) ||
      !addDocumentAuthority(projection, documents, span.document())) {
    return zc::none;
  }
  auto key = documentSite(span.document(), occurrence, role, ordinal);
  if (key == zc::none) { return zc::none; }
  auto output = ZC_ASSERT_NONNULL(key).clone();
  const bool ranged = role == diagnostics::DocumentDiagnosticSiteRole::Highlight;
  projection.provenance.add(diagnostics::SourceDiagnosticProvenanceEntry{
      zc::mv(ZC_ASSERT_NONNULL(key)),
      diagnostics::DiagnosticSourceRange{span.byteStart(),
                                         ranged ? span.byteEnd() : span.byteStart(), ranged}});
  return zc::mv(output);
}

}  // namespace

SanitizedSourceView::SanitizedSourceView(zc::String&& source,
                                         zc::Vector<uint64_t>&& offsets) noexcept
    : sourceValue(zc::mv(source)), escapedOffsets(zc::mv(offsets)) {}

SanitizedSourceView SanitizedSourceView::from(zc::ArrayPtr<const zc::byte> source) {
  zc::Vector<char> output;
  zc::Vector<uint64_t> offsets(source.size() + 1);
  offsets.add(0);
  size_t offset = 0;
  while (offset < source.size()) {
    const auto escapedStart = static_cast<uint64_t>(output.size());
    ZC_IF_SOME(decoded, diagnostics::decodeDiagnosticScalar(source, offset)) {
      diagnostics::appendDiagnosticScalar(output, decoded.value,
                                          diagnostics::DiagnosticQuote::None);
      for (uint8_t index = 1; index < decoded.length; ++index) { offsets.add(escapedStart); }
      offset += decoded.length;
      offsets.add(static_cast<uint64_t>(output.size()));
      continue;
    }
    diagnostics::appendDiagnosticByteEscape(output, static_cast<uint8_t>(source[offset++]));
    offsets.add(static_cast<uint64_t>(output.size()));
  }
  return SanitizedSourceView(zc::str(output.releaseAsArray()), zc::mv(offsets));
}

zc::StringPtr SanitizedSourceView::escapedSource() const noexcept { return sourceValue; }

uint64_t SanitizedSourceView::escapedOffset(uint64_t originalOffset) const {
  ZC_IREQUIRE(originalOffset < escapedOffsets.size(), "Original source offset is out of range.");
  return escapedOffsets[originalOffset];
}

uint64_t SanitizedSourceView::originalSize() const noexcept { return escapedOffsets.size() - 1; }
SanitizedSourceView SanitizedSourceView::clone() const {
  zc::Vector<uint64_t> offsets(escapedOffsets.size());
  for (const auto offset : escapedOffsets) offsets.add(offset);
  return SanitizedSourceView(zc::str(sourceValue), zc::mv(offsets));
}

PackageDiagnosticDocument::PackageDiagnosticDocument(InputDocumentKey&& key,
                                                     SanitizedSourceView&& source,
                                                     zc::String&& displayName) noexcept
    : keyValue(zc::mv(key)), sourceValue(zc::mv(source)), displayNameValue(zc::mv(displayName)) {}

zc::Maybe<PackageDiagnosticDocument> PackageDiagnosticDocument::from(
    InputDocumentKey&& key, zc::ArrayPtr<const zc::byte> source) {
  auto digest = identity::sha256(source);
  ZC_IF_SOME(digestValue, digest) {
    if (digestValue != key.contentDigest()) { return zc::none; }
    auto displayName = documentDisplayName(key.path());
    return PackageDiagnosticDocument(zc::mv(key), SanitizedSourceView::from(source),
                                     zc::mv(displayName));
  }
  return zc::none;
}

const InputDocumentKey& PackageDiagnosticDocument::key() const noexcept { return keyValue; }
const SanitizedSourceView& PackageDiagnosticDocument::sourceView() const noexcept {
  return sourceValue;
}
zc::StringPtr PackageDiagnosticDocument::displayName() const noexcept { return displayNameValue; }
PackageDiagnosticDocument PackageDiagnosticDocument::clone() const {
  return PackageDiagnosticDocument(keyValue.clone(), sourceValue.clone(),
                                   zc::str(displayNameValue));
}

zc::StringPtr manifestIssueDisplay(ManifestIssue issue) noexcept {
  switch (issue) {
    case ManifestIssue::ReadFailed:
      return "read-failed"_zc;
    case ManifestIssue::InvalidUtf8:
      return "invalid-utf8"_zc;
    case ManifestIssue::ByteOrderMarkPresent:
      return "byte-order-mark-present"_zc;
    case ManifestIssue::TomlSyntax:
      return "toml-syntax"_zc;
    case ManifestIssue::UnknownTable:
      return "unknown-table"_zc;
    case ManifestIssue::UnknownKey:
      return "unknown-key"_zc;
    case ManifestIssue::MissingRequiredKey:
      return "missing-required-key"_zc;
    case ManifestIssue::WrongValueType:
      return "wrong-value-type"_zc;
    case ManifestIssue::InvalidStrongScalar:
      return "invalid-strong-scalar"_zc;
    case ManifestIssue::UnsupportedEdition:
      return "unsupported-edition"_zc;
    case ManifestIssue::InvalidPath:
      return "invalid-path"_zc;
    case ManifestIssue::PathOutsideRoot:
      return "path-outside-root"_zc;
    case ManifestIssue::DuplicateCanonicalValue:
      return "duplicate-canonical-value"_zc;
    case ManifestIssue::WorkspaceMemberMissing:
      return "workspace-member-missing"_zc;
    case ManifestIssue::NestedWorkspace:
      return "nested-workspace"_zc;
    case ManifestIssue::DuplicateWorkspacePackageName:
      return "duplicate-workspace-package-name"_zc;
    case ManifestIssue::TargetCollision:
      return "target-collision"_zc;
    case ManifestIssue::TargetPathCollision:
      return "target-path-collision"_zc;
    case ManifestIssue::MissingTargetPath:
      return "missing-target-path"_zc;
    case ManifestIssue::DependencySourceConflict:
      return "dependency-source-conflict"_zc;
    case ManifestIssue::InvalidVersionConstraint:
      return "invalid-version-constraint"_zc;
    case ManifestIssue::InvalidVcsSelector:
      return "invalid-vcs-selector"_zc;
    case ManifestIssue::InvalidFeatureEdge:
      return "invalid-feature-edge"_zc;
    case ManifestIssue::FeatureCycle:
      return "feature-cycle"_zc;
  }
  ZC_UNREACHABLE
}

zc::StringPtr targetSelectionIssueDisplay(TargetSelectionIssue issue) noexcept {
  switch (issue) {
    case TargetSelectionIssue::UnknownWorkspacePackage:
      return "unknown-workspace-package"_zc;
    case TargetSelectionIssue::UnknownTarget:
      return "unknown-target"_zc;
    case TargetSelectionIssue::UnknownRootFeature:
      return "unknown-root-feature"_zc;
  }
  ZC_UNREACHABLE;
}

zc::StringPtr buildScriptIssueDisplay(BuildScriptIssue issue) noexcept {
  switch (issue) {
    case BuildScriptIssue::SandboxUnavailable:
      return "sandbox-unavailable"_zc;
    case BuildScriptIssue::SandboxSetupFailed:
      return "sandbox-setup-failed"_zc;
    case BuildScriptIssue::ForbiddenBuildCapability:
      return "forbidden-build-capability"_zc;
    case BuildScriptIssue::SeccompPolicyViolation:
      return "seccomp-policy-violation"_zc;
    case BuildScriptIssue::OutputTreePolicyViolation:
      return "output-tree-policy-violation"_zc;
    case BuildScriptIssue::ExecutableIdentityMismatch:
      return "executable-identity-mismatch"_zc;
    case BuildScriptIssue::UndeclaredInput:
      return "undeclared-input"_zc;
    case BuildScriptIssue::UndeclaredEnvironment:
      return "undeclared-environment"_zc;
    case BuildScriptIssue::UndeclaredExport:
      return "undeclared-export"_zc;
    case BuildScriptIssue::MissingOutput:
      return "missing-output"_zc;
    case BuildScriptIssue::UndeclaredOutput:
      return "undeclared-output"_zc;
    case BuildScriptIssue::CpuLimit:
      return "cpu-limit"_zc;
    case BuildScriptIssue::WallLimit:
      return "wall-limit"_zc;
    case BuildScriptIssue::MemoryLimit:
      return "memory-limit"_zc;
    case BuildScriptIssue::FileDescriptorLimit:
      return "file-descriptor-limit"_zc;
    case BuildScriptIssue::FileCountLimit:
      return "file-count-limit"_zc;
    case BuildScriptIssue::OutputSizeLimit:
      return "output-size-limit"_zc;
    case BuildScriptIssue::ExecutionFailed:
      return "execution-failed"_zc;
    case BuildScriptIssue::MalformedResponse:
      return "malformed-response"_zc;
    case BuildScriptIssue::RequestFrameLimit:
      return "request-frame-limit"_zc;
    case BuildScriptIssue::ResponseFrameLimit:
      return "response-frame-limit"_zc;
    case BuildScriptIssue::EnvironmentValueLimit:
      return "environment-value-limit"_zc;
    case BuildScriptIssue::ExportedEnvironmentLimit:
      return "exported-environment-limit"_zc;
    case BuildScriptIssue::InvalidGeneratedSource:
      return "invalid-generated-source"_zc;
    case BuildScriptIssue::NondeterministicOutput:
      return "nondeterministic-output"_zc;
    case BuildScriptIssue::SandboxTeardownFailed:
      return "sandbox-teardown-failed"_zc;
    case BuildScriptIssue::BuildResultIntegrityViolation:
      return "build-result-integrity-violation"_zc;
  }
  ZC_UNREACHABLE;
}

zc::StringPtr materializationIssueDisplay(MaterializationIssue issue) noexcept {
  switch (issue) {
    case MaterializationIssue::UnsupportedArchiveFormat:
      return "unsupported-archive-format"_zc;
    case MaterializationIssue::ArchiveDecodeFailed:
      return "archive-decode-failed"_zc;
    case MaterializationIssue::TrailingArchiveData:
      return "trailing-archive-data"_zc;
    case MaterializationIssue::FreshDirectoryCreateFailed:
      return "fresh-directory-create-failed"_zc;
    case MaterializationIssue::SourceReadFailed:
      return "source-read-failed"_zc;
    case MaterializationIssue::DestinationCreateFailed:
      return "destination-create-failed"_zc;
    case MaterializationIssue::DestinationWriteFailed:
      return "destination-write-failed"_zc;
    case MaterializationIssue::DestinationSyncFailed:
      return "destination-sync-failed"_zc;
    case MaterializationIssue::InvalidEntryEncoding:
      return "invalid-entry-encoding"_zc;
    case MaterializationIssue::AbsolutePath:
      return "absolute-path"_zc;
    case MaterializationIssue::ParentPath:
      return "parent-path"_zc;
    case MaterializationIssue::DotPath:
      return "dot-path"_zc;
    case MaterializationIssue::BackslashPath:
      return "backslash-path"_zc;
    case MaterializationIssue::EmptySegment:
      return "empty-segment"_zc;
    case MaterializationIssue::PathTooDeep:
      return "path-too-deep"_zc;
    case MaterializationIssue::PathTooLong:
      return "path-too-long"_zc;
    case MaterializationIssue::Symlink:
      return "symlink"_zc;
    case MaterializationIssue::HardLink:
      return "hard-link"_zc;
    case MaterializationIssue::SpecialFile:
      return "special-file"_zc;
    case MaterializationIssue::DuplicatePath:
      return "duplicate-path"_zc;
    case MaterializationIssue::UnicodeCollision:
      return "unicode-collision"_zc;
    case MaterializationIssue::CaseFoldCollision:
      return "case-fold-collision"_zc;
    case MaterializationIssue::FileTooLarge:
      return "file-too-large"_zc;
    case MaterializationIssue::CompressedSizeLimit:
      return "compressed-size-limit"_zc;
    case MaterializationIssue::DecoderWindowLimit:
      return "decoder-window-limit"_zc;
    case MaterializationIssue::DecoderMemoryLimit:
      return "decoder-memory-limit"_zc;
    case MaterializationIssue::ArchiveHeaderLimit:
      return "archive-header-limit"_zc;
    case MaterializationIssue::ArchiveMetadataLimit:
      return "archive-metadata-limit"_zc;
    case MaterializationIssue::FileCountLimit:
      return "file-count-limit"_zc;
    case MaterializationIssue::TotalSizeLimit:
      return "total-size-limit"_zc;
    case MaterializationIssue::LengthOverflow:
      return "length-overflow"_zc;
    case MaterializationIssue::SourceChangedDuringSnapshot:
      return "source-changed-during-snapshot"_zc;
    case MaterializationIssue::SourceTreeDigestMismatch:
      return "source-tree-digest-mismatch"_zc;
    case MaterializationIssue::SnapshotCleanupFailed:
      return "snapshot-cleanup-failed"_zc;
  }
  ZC_UNREACHABLE;
}

zc::StringPtr verifyFailureDisplay(const VerifyFailure& failure) noexcept {
  ZC_SWITCH_ONEOF(failure) {
    ZC_CASE_ONEOF(mismatch, RegistryRevisionMismatch) {
      switch (mismatch.kind) {
        case RegistryRevisionMismatchKind::HostRequestVersusVerified:
          return "registry-revision-mismatch-host-request"_zc;
        case RegistryRevisionMismatchKind::TargetRequestVersusVerified:
          return "registry-revision-mismatch-target-request"_zc;
        case RegistryRevisionMismatchKind::CrossVerified:
          return "registry-revision-mismatch-cross-verified"_zc;
      }
      ZC_UNREACHABLE;
    }
    ZC_CASE_ONEOF(mismatch, TargetSelectionMismatch) {
      switch (mismatch.side) {
        case TargetSelectionMismatchSide::Host:
          return "target-selection-mismatch-host"_zc;
        case TargetSelectionMismatchSide::Target:
          return "target-selection-mismatch-target"_zc;
      }
      ZC_UNREACHABLE;
    }
    ZC_CASE_ONEOF(_, GraphSnapshotMismatch) { return "graph-snapshot-mismatch"_zc; }
    ZC_CASE_ONEOF(_, RootPackageMissing) { return "root-package-missing"_zc; }
    ZC_CASE_ONEOF(_, BuildPlanFailed) { return "build-plan-failed"_zc; }
    ZC_CASE_ONEOF(_, CrateGraphExpansionFailed) { return "crate-graph-expansion-failed"_zc; }
  }
  ZC_UNREACHABLE;
}

PackageDiagnosticProjectionResult projectManifestFailure(
    zc::ArrayPtr<const PackageDiagnosticDocument> documents, const ManifestFailure& failure) {
  const auto& primaryAnchor = failure.provenance().primary();
  if (primaryAnchor.kind() != DiagnosticAnchorKind::Manifest) {
    return PackageDiagnosticProjectionFailure::InvalidProvenance;
  }
  const auto occurrenceBytes = manifestOccurrence(failure);
  PackageDiagnosticFactProjection projection;
  auto primary =
      addSpan(projection, documents, primaryAnchor.manifestSpan(), occurrenceBytes.asPtr(),
              diagnostics::DocumentDiagnosticSiteRole::Primary, 0);
  if (primary == zc::none) { return PackageDiagnosticProjectionFailure::MissingDocument; }
  zc::Vector<diagnostics::DiagnosticSecondary> secondary;
  auto primaryHighlight =
      addSpan(projection, documents, primaryAnchor.manifestSpan(), occurrenceBytes.asPtr(),
              diagnostics::DocumentDiagnosticSiteRole::Highlight, 0);
  if (primaryHighlight == zc::none) { return PackageDiagnosticProjectionFailure::MissingDocument; }
  auto highlight =
      diagnostics::DiagnosticSecondary::highlight(zc::mv(ZC_ASSERT_NONNULL(primaryHighlight)));
  if (highlight == zc::none) { return PackageDiagnosticProjectionFailure::InvalidFact; }
  secondary.add(zc::mv(ZC_ASSERT_NONNULL(highlight)));
  if (failure.issue() == ManifestIssue::DuplicateWorkspacePackageName &&
      failure.provenance().related().size() == 1) {
    const auto& relatedAnchor = failure.provenance().related()[0];
    if (relatedAnchor.kind() != DiagnosticAnchorKind::Manifest) {
      return PackageDiagnosticProjectionFailure::InvalidProvenance;
    }
    auto related =
        addSpan(projection, documents, relatedAnchor.manifestSpan(), occurrenceBytes.asPtr(),
                diagnostics::DocumentDiagnosticSiteRole::Note, 0);
    if (related == zc::none) { return PackageDiagnosticProjectionFailure::MissingDocument; }
    auto note = diagnostics::DiagnosticSecondary::note(
        diagnostics::DiagID::PreviousWorkspacePackageHere, zc::mv(ZC_ASSERT_NONNULL(related)), {});
    if (note == zc::none) { return PackageDiagnosticProjectionFailure::InvalidFact; }
    secondary.add(zc::mv(ZC_ASSERT_NONNULL(note)));
  } else if (failure.provenance().related().size() != 0) {
    return PackageDiagnosticProjectionFailure::InvalidProvenance;
  }
  auto occurrence = diagnostics::DiagnosticOccurrenceKey::document(
      primaryAnchor.manifestSpan().document().encode(),
      zc::heapArray<uint8_t>(occurrenceBytes.asPtr()));
  zc::Vector<zc::String> arguments;
  arguments.add(zc::str(manifestIssueDisplay(failure.issue())));
  auto fact = diagnostics::DiagnosticFact::from(
      zc::mv(ZC_ASSERT_NONNULL(occurrence)), diagnostics::DiagID::PackageManifestInvalid,
      zc::mv(arguments), zc::mv(ZC_ASSERT_NONNULL(primary)), zc::mv(secondary));
  if (fact == zc::none) { return PackageDiagnosticProjectionFailure::InvalidFact; }
  projection.facts.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
  return zc::mv(projection);
}

PackageDiagnosticProjectionResult projectToolchainModuleRootFailure(
    zc::ArrayPtr<const PackageDiagnosticDocument> documents,
    const PackageToolchainModuleRootFailure& failure) {
  const auto& primaryAnchor = failure.provenance().primary();
  if (primaryAnchor.kind() != DiagnosticAnchorKind::Manifest ||
      failure.provenance().related().size() != 0 || failure.argument().path().size() != 1) {
    return PackageDiagnosticProjectionFailure::InvalidProvenance;
  }
  const auto occurrenceBytes = toolchainRootOccurrence(failure);
  PackageDiagnosticFactProjection projection;
  auto primary =
      addSpan(projection, documents, primaryAnchor.manifestSpan(), occurrenceBytes.asPtr(),
              diagnostics::DocumentDiagnosticSiteRole::Primary, 0);
  if (primary == zc::none) { return PackageDiagnosticProjectionFailure::MissingDocument; }
  auto primaryHighlight =
      addSpan(projection, documents, primaryAnchor.manifestSpan(), occurrenceBytes.asPtr(),
              diagnostics::DocumentDiagnosticSiteRole::Highlight, 0);
  if (primaryHighlight == zc::none) { return PackageDiagnosticProjectionFailure::MissingDocument; }
  auto occurrence = diagnostics::DiagnosticOccurrenceKey::document(
      primaryAnchor.manifestSpan().document().encode(),
      zc::heapArray<uint8_t>(occurrenceBytes.asPtr()));
  zc::Vector<zc::String> arguments;
  arguments.add(zc::str(failure.argument().path()[0].text()));
  zc::Vector<diagnostics::DiagnosticSecondary> secondary;
  auto highlight =
      diagnostics::DiagnosticSecondary::highlight(zc::mv(ZC_ASSERT_NONNULL(primaryHighlight)));
  if (highlight == zc::none) { return PackageDiagnosticProjectionFailure::InvalidFact; }
  secondary.add(zc::mv(ZC_ASSERT_NONNULL(highlight)));
  auto fact = diagnostics::DiagnosticFact::from(
      zc::mv(ZC_ASSERT_NONNULL(occurrence)), diagnostics::DiagID::ToolchainModuleRootReserved,
      zc::mv(arguments), zc::mv(ZC_ASSERT_NONNULL(primary)), zc::mv(secondary));
  if (fact == zc::none) { return PackageDiagnosticProjectionFailure::InvalidFact; }
  projection.facts.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
  return zc::mv(projection);
}

}  // namespace zomlang::compiler::driver::package
