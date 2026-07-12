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

#include "zomlang/compiler/driver/package/package-diagnostic.h"

#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/diagnostics/diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang::compiler::driver::package {
namespace {

struct ManifestIssueCase final {
  ManifestIssue issue;
  zc::StringPtr display;
};

constexpr ManifestIssueCase ISSUE_CASES[] = {
    {ManifestIssue::ReadFailed, "read-failed"_zc},
    {ManifestIssue::InvalidUtf8, "invalid-utf8"_zc},
    {ManifestIssue::ByteOrderMarkPresent, "byte-order-mark-present"_zc},
    {ManifestIssue::TomlSyntax, "toml-syntax"_zc},
    {ManifestIssue::UnknownTable, "unknown-table"_zc},
    {ManifestIssue::UnknownKey, "unknown-key"_zc},
    {ManifestIssue::MissingRequiredKey, "missing-required-key"_zc},
    {ManifestIssue::WrongValueType, "wrong-value-type"_zc},
    {ManifestIssue::InvalidStrongScalar, "invalid-strong-scalar"_zc},
    {ManifestIssue::UnsupportedEdition, "unsupported-edition"_zc},
    {ManifestIssue::InvalidPath, "invalid-path"_zc},
    {ManifestIssue::PathOutsideRoot, "path-outside-root"_zc},
    {ManifestIssue::DuplicateCanonicalValue, "duplicate-canonical-value"_zc},
    {ManifestIssue::WorkspaceMemberMissing, "workspace-member-missing"_zc},
    {ManifestIssue::NestedWorkspace, "nested-workspace"_zc},
    {ManifestIssue::DuplicateWorkspacePackageName, "duplicate-workspace-package-name"_zc},
    {ManifestIssue::TargetCollision, "target-collision"_zc},
    {ManifestIssue::TargetPathCollision, "target-path-collision"_zc},
    {ManifestIssue::MissingTargetPath, "missing-target-path"_zc},
    {ManifestIssue::DependencySourceConflict, "dependency-source-conflict"_zc},
    {ManifestIssue::InvalidVersionConstraint, "invalid-version-constraint"_zc},
    {ManifestIssue::InvalidVcsSelector, "invalid-vcs-selector"_zc},
    {ManifestIssue::InvalidFeatureEdge, "invalid-feature-edge"_zc},
    {ManifestIssue::FeatureCycle, "feature-cycle"_zc},
};

identity::CanonicalWorkspaceRelativePath workspacePath(uint32_t parents, zc::StringPtr segment) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  auto value = identity::CanonicalPathSegment::fromCanonical(segment);
  ZC_IF_SOME(admitted, value) { segments.add(zc::mv(admitted)); }
  ZC_REQUIRE(segments.size() == 1);
  return identity::CanonicalWorkspaceRelativePath::from(parents, zc::mv(segments));
}

InputDocumentKey documentKey(zc::StringPtr path, zc::ArrayPtr<const zc::byte> source) {
  auto digest = identity::sha256(source);
  ZC_IF_SOME(digestValue, digest) {
    auto key = InputDocumentKey::from(InputDocumentKind::Manifest,
                                      DiagnosticDocumentPath::workspace(workspacePath(0, path)),
                                      digestValue);
    ZC_IF_SOME(admitted, key) { return zc::mv(admitted); }
  }
  ZC_FAIL_REQUIRE("invalid package diagnostic document fixture");
}

DiagnosticAnchor anchor(const PackageDiagnosticDocument& document, uint64_t start, uint64_t end) {
  auto span =
      ManifestSpan::from(document.key().clone(), document.sourceView().originalSize(), start, end);
  ZC_IF_SOME(admitted, span) { return DiagnosticAnchor::manifest(zc::mv(admitted)); }
  ZC_FAIL_REQUIRE("invalid package diagnostic span fixture");
}

ManifestFailure failure(const PackageDiagnosticDocument& document, ManifestIssue issue,
                        zc::Vector<DiagnosticAnchor>&& related = {}) {
  auto provenance = DiagnosticProvenance::from(anchor(document, 0, 1), zc::mv(related));
  ZC_IF_SOME(admitted, provenance) { return ManifestFailure::invalid(zc::mv(admitted), issue); }
  ZC_FAIL_REQUIRE("invalid package diagnostic provenance fixture");
}

PackageDiagnosticDocument document(zc::StringPtr path, zc::ArrayPtr<const zc::byte> source) {
  auto value = PackageDiagnosticDocument::from(documentKey(path, source), source);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("package diagnostic document admission failed");
}

struct CaptureState final {
  zc::Vector<uint32_t> primaryIds;
  zc::Vector<uint32_t> childIds;
  zc::Vector<zc::String> displayNames;
};

class CaptureConsumer final : public diagnostics::DiagnosticConsumer {
public:
  explicit CaptureConsumer(CaptureState& state) noexcept : state(state) {}

  void handleDiagnostic(const source::SourceManager& sourceManager,
                        const diagnostics::Diagnostic& diagnostic) override {
    state.primaryIds.add(static_cast<uint32_t>(diagnostic.getId()));
    if (diagnostic.getLoc().isValid()) {
      state.displayNames.add(zc::str(sourceManager.getDisplayNameForLoc(diagnostic.getLoc())));
    }
    for (const auto& child : diagnostic.getChildDiagnostics()) {
      state.childIds.add(static_cast<uint32_t>(child->getId()));
      state.displayNames.add(zc::str(sourceManager.getDisplayNameForLoc(child->getLoc())));
    }
  }

private:
  CaptureState& state;
};

}  // namespace

ZC_TEST("PackageDiagnosticTest.SanitizesSourceAndProjectsOriginalOffsets") {
  const zc::byte sourceBytes[] = {'A', '\\', '\n', 0xc3, 0xa9, 0xff};
  auto view = SanitizedSourceView::from(sourceBytes);

  ZC_EXPECT(view.escapedSource() == "A\\\\\\u{A}\\u{E9}\\xFF"_zc);
  ZC_EXPECT(view.originalSize() == 6);
  ZC_EXPECT(view.escapedOffset(0) == 0);
  ZC_EXPECT(view.escapedOffset(1) == 1);
  ZC_EXPECT(view.escapedOffset(2) == 3);
  ZC_EXPECT(view.escapedOffset(3) == 8);
  ZC_EXPECT(view.escapedOffset(4) == 8);
  ZC_EXPECT(view.escapedOffset(5) == 14);
  ZC_EXPECT(view.escapedOffset(6) == 18);
}

ZC_TEST("PackageDiagnosticTest.VerifiesDigestAndUsesHostPathFreeDisplayName") {
  const auto source = "[package]"_zc.asBytes();
  auto admitted = PackageDiagnosticDocument::from(documentKey("Zom.toml"_zc, source), source);
  ZC_IF_SOME(documentValue, admitted) { ZC_EXPECT(documentValue.displayName() == "Zom.toml"_zc); }
  else { ZC_FAIL_EXPECT("valid diagnostic document was rejected"); }

  auto wrongKey = documentKey("Zom.toml"_zc, "different"_zc.asBytes());
  ZC_EXPECT(PackageDiagnosticDocument::from(zc::mv(wrongKey), source) == zc::none);
}

ZC_TEST("PackageDiagnosticTest.EmitsEveryManifestIssueThroughClosedDiagnostic") {
  const auto source = "x"_zc.asBytes();
  zc::Vector<PackageDiagnosticDocument> documents;
  documents.add(document("Zom.toml"_zc, source));
  CaptureState capture;
  source::SourceManager sourceManager;
  diagnostics::DiagnosticEngine engine(sourceManager);
  engine.addConsumer(zc::heap<CaptureConsumer>(capture));

  for (const auto& issueCase : ISSUE_CASES) {
    ZC_EXPECT(manifestIssueDisplay(issueCase.issue) == issueCase.display);
    auto manifestFailure = failure(documents[0], issueCase.issue);
    ZC_EXPECT(PackageDiagnosticAdapter::emitManifestFailure(engine, documents, manifestFailure));
  }

  ZC_EXPECT(capture.primaryIds.size() == zc::size(ISSUE_CASES));
  for (const auto id : capture.primaryIds) { ZC_EXPECT(id == 7001); }
  for (const auto& displayName : capture.displayNames) { ZC_EXPECT(displayName == "Zom.toml"_zc); }
}

ZC_TEST("PackageDiagnosticTest.EmitsPreviousWorkspacePackageAsRelatedNote") {
  const auto source = "x"_zc.asBytes();
  zc::Vector<PackageDiagnosticDocument> documents;
  documents.add(document("first.toml"_zc, source));
  documents.add(document("second.toml"_zc, source));
  zc::Vector<DiagnosticAnchor> related;
  related.add(anchor(documents[0], 0, 1));
  auto manifestFailure =
      failure(documents[1], ManifestIssue::DuplicateWorkspacePackageName, zc::mv(related));
  CaptureState capture;
  source::SourceManager sourceManager;
  diagnostics::DiagnosticEngine engine(sourceManager);
  engine.addConsumer(zc::heap<CaptureConsumer>(capture));

  ZC_EXPECT(PackageDiagnosticAdapter::emitManifestFailure(engine, documents, manifestFailure));
  ZC_REQUIRE(capture.primaryIds.size() == 1);
  ZC_REQUIRE(capture.childIds.size() == 1);
  ZC_EXPECT(capture.primaryIds[0] == 7001);
  ZC_EXPECT(capture.childIds[0] == 7093);
  ZC_EXPECT(capture.displayNames[0] == "second.toml"_zc);
  ZC_EXPECT(capture.displayNames[1] == "first.toml"_zc);
}

ZC_TEST("PackageDiagnosticTest.EmitsEveryInvocationIssueThroughZOM7016") {
  for (uint8_t value = static_cast<uint8_t>(InvocationIssue::ManifestNotFound);
       value <= static_cast<uint8_t>(InvocationIssue::InvalidPanicStrategy); ++value) {
    CaptureState capture;
    source::SourceManager sourceManager;
    diagnostics::DiagnosticEngine engine(sourceManager);
    engine.addConsumer(zc::heap<CaptureConsumer>(capture));
    PackageDiagnosticAdapter::emitInvocationIssue(engine, static_cast<InvocationIssue>(value));
    ZC_REQUIRE(capture.primaryIds.size() == 1);
    ZC_EXPECT(capture.primaryIds[0] == 7016);
  }
}

ZC_TEST("PackageDiagnosticTest.EmitsEveryBuildScriptIssueThroughZOM7011") {
  for (uint8_t value = static_cast<uint8_t>(BuildScriptIssue::SandboxUnavailable);
       value <= static_cast<uint8_t>(BuildScriptIssue::BuildResultIntegrityViolation); ++value) {
    const auto issue = static_cast<BuildScriptIssue>(value);
    ZC_EXPECT(buildScriptIssueDisplay(issue).size() != 0);
    CaptureState capture;
    source::SourceManager sourceManager;
    diagnostics::DiagnosticEngine engine(sourceManager);
    engine.addConsumer(zc::heap<CaptureConsumer>(capture));
    PackageDiagnosticAdapter::emitBuildScriptIssue(engine, issue);
    ZC_REQUIRE(capture.primaryIds.size() == 1);
    ZC_EXPECT(capture.primaryIds[0] == 7011);
  }
}

ZC_TEST("PackageDiagnosticTest.EmitsEveryMaterializationIssueThroughZOM7010") {
  for (uint8_t value = static_cast<uint8_t>(MaterializationIssue::UnsupportedArchiveFormat);
       value <= static_cast<uint8_t>(MaterializationIssue::SnapshotCleanupFailed); ++value) {
    const auto issue = static_cast<MaterializationIssue>(value);
    ZC_EXPECT(materializationIssueDisplay(issue).size() != 0);
    CaptureState capture;
    source::SourceManager sourceManager;
    diagnostics::DiagnosticEngine engine(sourceManager);
    engine.addConsumer(zc::heap<CaptureConsumer>(capture));
    PackageDiagnosticAdapter::emitMaterializationIssue(engine, issue);
    ZC_REQUIRE(capture.primaryIds.size() == 1);
    ZC_EXPECT(capture.primaryIds[0] == 7010);
  }
}

ZC_TEST("PackageDiagnosticTest.ClosesBuildScriptInvariantDisplayAlgebras") {
  for (uint8_t value = static_cast<uint8_t>(BuildScriptLimitInvariantIssue::CpuRange);
       value <= static_cast<uint8_t>(BuildScriptLimitInvariantIssue::FrameRelation); ++value) {
    ZC_EXPECT(buildScriptLimitInvariantDisplay(static_cast<BuildScriptLimitInvariantIssue>(value))
                  .size() != 0);
  }
  for (uint8_t value = static_cast<uint8_t>(TrustedRuntimeInvariantIssue::EmptyObjectSet);
       value <= static_cast<uint8_t>(TrustedRuntimeInvariantIssue::UnexpectedInitializer);
       ++value) {
    ZC_EXPECT(
        trustedRuntimeInvariantDisplay(static_cast<TrustedRuntimeInvariantIssue>(value)).size() !=
        0);
  }
}

}  // namespace zomlang::compiler::driver::package
