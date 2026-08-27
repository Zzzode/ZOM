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

#include <climits>

#include "zc/core/encoding.h"
#include "zc/core/memory.h"
#include "compiler/diagnostics/core/diagnostic-engine.h"
#include "compiler/diagnostics/core/diagnostic.h"
#include "compiler/source/manager.h"

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

bool sameDocument(const InputDocumentKey& left, const InputDocumentKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

struct ResolvedSpan final {
  source::SourceLoc start;
  source::SourceLoc end;
};

zc::Maybe<ResolvedSpan> resolveSpan(source::SourceManager& sourceManager,
                                    zc::ArrayPtr<const PackageDiagnosticDocument> documents,
                                    const ManifestSpan& span) {
  for (const auto& document : documents) {
    if (!sameDocument(document.key(), span.document())) { continue; }
    const auto& view = document.sourceView();
    if (span.byteEnd() > view.originalSize()) { return zc::none; }
    const auto start = view.escapedOffset(span.byteStart());
    const auto end = view.escapedOffset(span.byteEnd());
    if (start > UINT_MAX || end > UINT_MAX) { return zc::none; }
    const auto buffer =
        sourceManager.addMemBufferCopy(view.escapedSource().asBytes(), document.displayName());
    return ResolvedSpan{sourceManager.getLocForOffset(buffer, static_cast<unsigned>(start)),
                        sourceManager.getLocForOffset(buffer, static_cast<unsigned>(end))};
  }
  return zc::none;
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

zc::StringPtr buildScriptLimitInvariantDisplay(BuildScriptLimitInvariantIssue issue) noexcept {
  switch (issue) {
    case BuildScriptLimitInvariantIssue::CpuRange:
      return "cpu-range"_zc;
    case BuildScriptLimitInvariantIssue::CpuGranularity:
      return "cpu-granularity"_zc;
    case BuildScriptLimitInvariantIssue::WallRange:
      return "wall-range"_zc;
    case BuildScriptLimitInvariantIssue::MemoryRange:
      return "memory-range"_zc;
    case BuildScriptLimitInvariantIssue::FileDescriptorRange:
      return "file-descriptor-range"_zc;
    case BuildScriptLimitInvariantIssue::FileCountRange:
      return "file-count-range"_zc;
    case BuildScriptLimitInvariantIssue::OutputRange:
      return "output-range"_zc;
    case BuildScriptLimitInvariantIssue::RequestFrameRange:
      return "request-frame-range"_zc;
    case BuildScriptLimitInvariantIssue::ResponseFrameRange:
      return "response-frame-range"_zc;
    case BuildScriptLimitInvariantIssue::EnvironmentValueRange:
      return "environment-value-range"_zc;
    case BuildScriptLimitInvariantIssue::ExportedEnvironmentRange:
      return "exported-environment-range"_zc;
    case BuildScriptLimitInvariantIssue::FrameRelation:
      return "frame-relation"_zc;
  }
  ZC_UNREACHABLE;
}

zc::StringPtr trustedRuntimeInvariantDisplay(TrustedRuntimeInvariantIssue issue) noexcept {
  switch (issue) {
    case TrustedRuntimeInvariantIssue::EmptyObjectSet:
      return "empty-object-set"_zc;
    case TrustedRuntimeInvariantIssue::DuplicateObjectDigest:
      return "duplicate-object-digest"_zc;
    case TrustedRuntimeInvariantIssue::RuntimeAbiMismatch:
      return "runtime-abi-mismatch"_zc;
    case TrustedRuntimeInvariantIssue::ObjectDigestMismatch:
      return "object-digest-mismatch"_zc;
    case TrustedRuntimeInvariantIssue::SymbolManifestMismatch:
      return "symbol-manifest-mismatch"_zc;
    case TrustedRuntimeInvariantIssue::RelocationManifestMismatch:
      return "relocation-manifest-mismatch"_zc;
    case TrustedRuntimeInvariantIssue::OperationManifestMismatch:
      return "operation-manifest-mismatch"_zc;
    case TrustedRuntimeInvariantIssue::InvalidManifestRecord:
      return "invalid-manifest-record"_zc;
    case TrustedRuntimeInvariantIssue::UnmanifestedSymbol:
      return "unmanifested-symbol"_zc;
    case TrustedRuntimeInvariantIssue::UnmanifestedRelocation:
      return "unmanifested-relocation"_zc;
    case TrustedRuntimeInvariantIssue::WeakFallback:
      return "weak-fallback"_zc;
    case TrustedRuntimeInvariantIssue::UnexpectedInitializer:
      return "unexpected-initializer"_zc;
  }
  ZC_UNREACHABLE;
}

void PackageDiagnosticAdapter::emitInvocationIssue(diagnostics::DiagnosticEngine& diagnostics,
                                                   InvocationIssue issue) {
  diagnostics.diagnose<diagnostics::DiagID::PackageInvocationInvalid>(
      source::SourceLoc(), invocationIssueDisplay(issue));
}

void PackageDiagnosticAdapter::emitMaterializationIssue(diagnostics::DiagnosticEngine& diagnostics,
                                                        MaterializationIssue issue) {
  diagnostics.diagnose<diagnostics::DiagID::PackageMaterializationInvalid>(
      source::SourceLoc(), materializationIssueDisplay(issue));
}

void PackageDiagnosticAdapter::emitBuildScriptIssue(diagnostics::DiagnosticEngine& diagnostics,
                                                    BuildScriptIssue issue) {
  diagnostics.diagnose<diagnostics::DiagID::PackageBuildScriptFailed>(
      source::SourceLoc(), buildScriptIssueDisplay(issue));
}

void PackageDiagnosticAdapter::emitBuildScriptLimitInvariant(
    diagnostics::DiagnosticEngine& diagnostics, BuildScriptLimitInvariantIssue issue) {
  diagnostics.diagnose<diagnostics::DiagID::BuildScriptLimitInvariantViolation>(
      source::SourceLoc(), buildScriptLimitInvariantDisplay(issue));
}

void PackageDiagnosticAdapter::emitTrustedRuntimeInvariant(
    diagnostics::DiagnosticEngine& diagnostics, TrustedRuntimeInvariantIssue issue) {
  diagnostics.diagnose<diagnostics::DiagID::TrustedBuildRuntimeInvariantViolation>(
      source::SourceLoc(), trustedRuntimeInvariantDisplay(issue));
}

bool PackageDiagnosticAdapter::emitManifestFailure(
    diagnostics::DiagnosticEngine& diagnostics,
    zc::ArrayPtr<const PackageDiagnosticDocument> documents, const ManifestFailure& failure) {
  const auto& primaryAnchor = failure.provenance().primary();
  if (primaryAnchor.kind() != DiagnosticAnchorKind::Manifest) { return false; }
  auto primary =
      resolveSpan(diagnostics.getSourceManager(), documents, primaryAnchor.manifestSpan());
  zc::Maybe<ResolvedSpan> related;
  if (failure.issue() == ManifestIssue::DuplicateWorkspacePackageName &&
      failure.provenance().related().size() == 1) {
    const auto& relatedAnchor = failure.provenance().related()[0];
    if (relatedAnchor.kind() != DiagnosticAnchorKind::Manifest) { return false; }
    related = resolveSpan(diagnostics.getSourceManager(), documents, relatedAnchor.manifestSpan());
    if (related == zc::none) { return false; }
  }
  ZC_IF_SOME(primaryValue, primary) {
    auto diagnostic = diagnostics.diagnose<diagnostics::DiagID::PackageManifestInvalid>(
        primaryValue.start, manifestIssueDisplay(failure.issue()));
    diagnostic.addRange(
        source::CharSourceRange::getCharRange(primaryValue.start, primaryValue.end));
    ZC_IF_SOME(relatedValue, related) {
      auto child = zc::heap<diagnostics::Diagnostic>(
          diagnostics::DiagID::PreviousWorkspacePackageHere, relatedValue.start);
      child->addRange(source::CharSourceRange::getCharRange(relatedValue.start, relatedValue.end));
      diagnostic.addChild(zc::mv(child));
    }
    diagnostic.emit();
    return true;
  }
  return false;
}

bool PackageDiagnosticAdapter::emitToolchainModuleRootFailure(
    diagnostics::DiagnosticEngine& diagnostics,
    zc::ArrayPtr<const PackageDiagnosticDocument> documents,
    const PackageToolchainModuleRootFailure& failure) {
  const auto& primaryAnchor = failure.provenance().primary();
  if (primaryAnchor.kind() != DiagnosticAnchorKind::Manifest ||
      failure.provenance().related().size() != 0 || failure.argument().path().size() != 1) {
    return false;
  }
  auto primary =
      resolveSpan(diagnostics.getSourceManager(), documents, primaryAnchor.manifestSpan());
  ZC_IF_SOME(primaryValue, primary) {
    auto diagnostic = diagnostics.diagnose<diagnostics::DiagID::ToolchainModuleRootReserved>(
        primaryValue.start, zc::str(failure.argument().path()[0].text()));
    diagnostic.addRange(
        source::CharSourceRange::getCharRange(primaryValue.start, primaryValue.end));
    diagnostic.emit();
    return true;
  }
  return false;
}

}  // namespace zomlang::compiler::driver::package
