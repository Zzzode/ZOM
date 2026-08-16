#!/usr/bin/env python3
"""Enforce retained ownership-overlay leases and production publication."""

from __future__ import annotations

import argparse
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OVERLAY = Path("products/zomlang/compiler/ownership/ownership-event-overlay.cc")
ADMISSION_HEADER = Path("products/zomlang/compiler/ownership/surface-admission.h")
ADMISSION_SOURCE = Path("products/zomlang/compiler/ownership/surface-admission.cc")
MOVE_PATHS = Path("products/zomlang/compiler/ownership/facts/paths.cc")
MOVE_PATHS_HEADER = Path("products/zomlang/compiler/ownership/facts/paths.h")
FLOW = Path("products/zomlang/compiler/ownership/facts/flow.cc")
FLOW_HEADER = Path("products/zomlang/compiler/ownership/facts/flow.h")
INITIALIZATION = Path("products/zomlang/compiler/ownership/facts/init.cc")
INITIALIZATION_HEADER = Path("products/zomlang/compiler/ownership/facts/init.h")
LOANS = Path("products/zomlang/compiler/ownership/facts/loans.cc")
LOANS_HEADER = Path("products/zomlang/compiler/ownership/facts/loans.h")
POINTS_HEADER = Path("products/zomlang/compiler/ownership/facts/points.h")
REFERENCES = Path("products/zomlang/compiler/ownership/facts/refs.cc")
REFERENCES_HEADER = Path("products/zomlang/compiler/ownership/facts/refs.h")
REGIONS = Path("products/zomlang/compiler/ownership/facts/regions.cc")
REGIONS_HEADER = Path("products/zomlang/compiler/ownership/facts/regions.h")
STATES = Path("products/zomlang/compiler/ownership/facts/states.cc")
STATES_HEADER = Path("products/zomlang/compiler/ownership/facts/states.h")
RESOURCES = Path("products/zomlang/compiler/ownership/facts/resources.cc")
RESOURCES_HEADER = Path("products/zomlang/compiler/ownership/facts/resources.h")
INPUTS = Path("products/zomlang/compiler/ownership/facts/inputs.cc")
INPUTS_HEADER = Path("products/zomlang/compiler/ownership/facts/inputs.h")
BORROW_EVIDENCE_HEADER = Path("products/zomlang/compiler/driver/borrow-evidence.h")
BORROW_EVIDENCE_SOURCE = Path("products/zomlang/compiler/driver/borrow-evidence.cc")
HIR = Path("products/zomlang/compiler/hir/hir-module.cc")
HIR_HEADER = Path("products/zomlang/compiler/hir/hir-module.h")
CHECKED_MODULE = Path("products/zomlang/compiler/hir/checked-module.cc")
CHECKED_MODULE_HEADER = Path("products/zomlang/compiler/hir/checked-module.h")
MIR = Path("products/zomlang/compiler/mir/built-mir.cc")
MIR_HEADER = Path("products/zomlang/compiler/mir/built-mir.h")
OWNERSHIP_CMAKE = Path("products/zomlang/compiler/ownership/CMakeLists.txt")
SESSION = Path("products/zomlang/compiler/driver/compiler-session.cc")
TEST = Path("products/zomlang/tests/unittests/compiler/ownership/ownership-event-overlay-test.cc")
HIR_TEST = Path("products/zomlang/tests/unittests/compiler/hir/hir-module-test.cc")
SESSION_TEST = Path(
    "products/zomlang/tests/unittests/compiler/driver/compiler-session-package-test.cc"
)
COMPILER_SESSION_TEST = Path(
    "products/zomlang/tests/unittests/compiler/driver/compiler-session-test.cc"
)
TEST_CMAKE = Path("products/zomlang/tests/conformance/CMakeLists.txt")
REQUIRED = (
    OVERLAY,
    ADMISSION_HEADER,
    ADMISSION_SOURCE,
    MOVE_PATHS,
    MOVE_PATHS_HEADER,
    FLOW,
    FLOW_HEADER,
    INITIALIZATION,
    INITIALIZATION_HEADER,
    LOANS,
    LOANS_HEADER,
    POINTS_HEADER,
    REFERENCES,
    REFERENCES_HEADER,
    REGIONS,
    REGIONS_HEADER,
    STATES,
    STATES_HEADER,
    RESOURCES,
    RESOURCES_HEADER,
    INPUTS,
    INPUTS_HEADER,
    BORROW_EVIDENCE_HEADER,
    BORROW_EVIDENCE_SOURCE,
    HIR,
    HIR_HEADER,
    CHECKED_MODULE,
    CHECKED_MODULE_HEADER,
    MIR,
    MIR_HEADER,
    OWNERSHIP_CMAKE,
    SESSION,
    TEST,
    HIR_TEST,
    SESSION_TEST,
    COMPILER_SESSION_TEST,
    TEST_CMAKE,
)


def files() -> dict[Path, str]:
    return {
        path: (ROOT / path).read_text(encoding="utf-8")
        for path in REQUIRED
        if (ROOT / path).is_file()
    }


def check(values: dict[Path, str]) -> list[str]:
    errors: list[str] = []
    for path in REQUIRED:
        if path not in values:
            errors.append(f"missing ownership architecture artifact: {path}")

    overlay = values.get(OVERLAY, "")
    for marker in (
        "OwnershipAdmittedBoundModule boundModule;",
        "builtMir.retainAdmittedBoundModule()",
        "const auto& admitted = input.admitted;",
        "const auto& admittedBound = admitted.boundModule();",
        "MarkerProofInput::from(input.body)",
        "markerUses",
        "OwnershipEventOverlayBuilder::build(",
        "OwnershipEventOverlayVerifier::verify(",
    ):
        if marker not in overlay:
            errors.append(f"{OVERLAY}: missing required retained-overlay contract: {marker}")

    admission_header = values.get(ADMISSION_HEADER, "")
    for marker in (
        "class OwnershipSurfaceSourceRejected final",
        "class OwnershipAdmittedBoundModule final",
        "friend class OwnershipSurfaceAdmissionBuilder;",
        "using OwnershipSurfaceAdmissionResult =",
        "OwnershipSurfaceAdmissionBuilder final",
    ):
        if marker not in admission_header:
            errors.append(f"{ADMISSION_HEADER}: missing ownership admission contract: {marker}")

    admission_source = values.get(ADMISSION_SOURCE, "")
    for marker in (
        "ast::visitTreePreOrder(",
        "ast::SyntaxKind::SpawnExpression",
        "ast::SyntaxKind::SuspendStatement",
        "OwnershipSurfaceSourceRejected(",
        "OwnershipAdmittedBoundModule(",
    ):
        if marker not in admission_source:
            errors.append(f"{ADMISSION_SOURCE}: missing ownership admission implementation: {marker}")

    move_paths = values.get(MOVE_PATHS, "")
    for marker in (
        "MovePathBuilder::build(",
        "MovePathVerifier::verify(",
        "MovePathPair{",
        "bool sortFacts(",
        "auto less = lessKey(",
        "if (!sortFacts(facts, identities)) return zc::none;",
        "bool matchesPlace(const mir::MirFunction& function, const mir::MirPlace& place) {\n  if (!place.hasConsistentTypeChain()) return false;",
        "for (const auto& projection : place.projections()) {\n      if (!projection.isStructurallyValid()) return false;",
        "if (projection.kind() != mir::MirProjectionKind::Index) continue;",
        "if (!foundIndex) return false;",
    ):
        if marker not in move_paths:
            errors.append(f"{MOVE_PATHS}: missing move-path production contract: {marker}")

    move_paths_header = values.get(MOVE_PATHS_HEADER, "")
    for marker in (
        "struct MovePathKey final",
        "struct MovePathFact final",
        "struct MovePathPair final",
        "zc::Maybe<MovePathKey> parent",
        "bool conflicts(const MovePathKey& first, const MovePathKey& second) const noexcept;",
    ):
        if marker not in move_paths_header:
            errors.append(f"{MOVE_PATHS_HEADER}: missing move-path fact contract: {marker}")

    flow = values.get(FLOW, "")
    for marker in (
        "FlowBuilder::build(",
        "FlowVerifier::verify(",
        "MirPoint::edge(block.id, 0, call.normalTarget)",
        "hasAllSlotPoints(flow, overlay)",
        "sameFunctions(candidate.functions",
    ):
        if marker not in flow:
            errors.append(f"{FLOW}: missing ownership-flow production contract: {marker}")
    flow_header = values.get(FLOW_HEADER, "")
    for marker in (
        "struct FlowEdge final",
        "struct FlowFunction final",
        "zc::Vector<OwnershipPoint> points;",
        "zc::Vector<FlowEdge> edges;",
        "class VerifiedFlow final",
    ):
        if marker not in flow_header:
            errors.append(f"{FLOW_HEADER}: missing ownership-flow contract: {marker}")

    initialization = values.get(INITIALIZATION, "")
    for marker in (
        "InitializationBuilder::build(",
        "InitializationVerifier::verify(",
        "factsUseFlow(value, flow.functions()[index])",
        "rootKey(",
        "states[index].state != InitializationState::dead()",
        "states[index].state != InitializationState::uninitialized()",
        "bool hasOperandRead(const OwnershipFunctionEventOverlay& overlay, const MirEventKey& event)",
        "if (!hasOperandRead(overlay, primary)) return false;",
    ):
        if marker not in initialization:
            errors.append(f"{INITIALIZATION}: missing initialization production contract: {marker}")

    initialization_header = values.get(INITIALIZATION_HEADER, "")
    for marker in (
        "MovePathKey key;",
        "class InitializationSourceVerificationResult final",
        "friend class InitializationSourceVerifier;",
        "ownership proof validation",
    ):
        if marker not in initialization_header:
            errors.append(f"{INITIALIZATION_HEADER}: missing ownership source-result contract: {marker}")
    if "using InitializationSourceVerificationResult" in initialization_header:
        errors.append(f"{INITIALIZATION_HEADER}: must not reuse a feature-boundary result")
    source_result_start = initialization_header.find("class InitializationSourceVerificationResult final")
    source_result_private = initialization_header.find("private:", source_result_start)
    if source_result_start == -1 or source_result_private == -1:
        errors.append(f"{INITIALIZATION_HEADER}: missing sealed ownership source-result boundary")
    else:
        source_result_public = initialization_header[source_result_start:source_result_private]
        for marker in (
            "static InitializationSourceVerificationResult verified(",
            "static InitializationSourceVerificationResult sourceRejected(",
            "static InitializationSourceVerificationResult identityInvariantRejected(",
            "static InitializationSourceVerificationResult irInvariantRejected(",
        ):
            if marker in source_result_public:
                errors.append(f"{INITIALIZATION_HEADER}: ownership source-result factory must be sealed")

    loans = values.get(LOANS, "")
    for marker in (
        "LoanBuilder::build(",
        "LoanVerifier::verify(",
        "OwnershipEventRole::BorrowIssue",
        "hasBorrowCommit(overlay, function.owner, point)",
        "builtMir.borrowEvidenceRevision()",
        "hasBorrowIssue(overlay, function.owner, point)",
    ):
        if marker not in loans:
            errors.append(f"{LOANS}: missing verified loan production contract: {marker}")
    loans_header = values.get(LOANS_HEADER, "")
    for marker in (
        "struct LoanFact final",
        "MirEventKey issue;",
        "MirEventKey commit;",
        "OwnershipPoint activeFrom;",
        "MovePathKey source;",
        "MovePathKey destination;",
        "BorrowEvidenceRevision borrowEvidenceRevision;",
        "class VerifiedLoanFacts final",
    ):
        if marker not in loans_header:
            errors.append(f"{LOANS_HEADER}: missing loan fact contract: {marker}")
    if "LoanActivation" in loans_header:
        errors.append(
            f"{LOANS_HEADER}: loan activation must use the authoritative ownership point"
        )

    points_header = values.get(POINTS_HEADER, "")
    for marker in (
        "enum class OwnershipPointKind : uint8_t { Cfg = 0x01, BeforeEvent = 0x02, AfterEvent = 0x03 };",
        "struct OwnershipCfgPoint final",
        "struct OwnershipBeforeEventPoint final",
        "struct OwnershipAfterEventPoint final",
        "class OwnershipPoint final",
        "static OwnershipPoint cfg(MirPoint point) noexcept",
        "static OwnershipPoint beforeEvent(MirEventKey event) noexcept",
        "static OwnershipPoint afterEvent(MirEventKey event) noexcept",
        "zc::OneOf<OwnershipCfgPoint, OwnershipBeforeEventPoint, OwnershipAfterEventPoint>",
    ):
        if marker not in points_header:
            errors.append(f"{POINTS_HEADER}: missing event cutpoint contract: {marker}")

    references = values.get(REFERENCES, "")
    for marker in (
        "ReferenceDefinitionBuilder::build(",
        "ReferenceDefinitionVerifier::verify(",
        "loan.commit",
        "loan.issue",
        "hasEntryRoot(overlay, loan.owner, ordinal)",
        "hasDirectRootParameter(evidence, loan.owner, ordinal)",
        "builtMir.borrowEvidence()",
        "livePoints(loan.commit, returnEvent)",
        "returnedFrom(function, loan.destination.place, overlay)",
    ):
        if marker not in references:
            errors.append(f"{REFERENCES}: missing reference-definition production contract: {marker}")
    references_header = values.get(REFERENCES_HEADER, "")
    for marker in (
        "struct ReferenceDefinition final",
        "MirEventKey introduction;",
        "MirEventKey loan;",
        "ReferenceInputOrigin origin;",
        "OwnershipPoint activation;",
        "uint32_t rootParameter;",
        "MovePathKey referent;",
        "ReferenceLivePoints livePoints;",
        "MirEventKey returned;",
        "MovePathKey destination;",
        "class VerifiedReferenceDefinitions final",
    ):
        if marker not in references_header:
            errors.append(f"{REFERENCES_HEADER}: missing reference-definition contract: {marker}")

    regions = values.get(REGIONS, "")
    for marker in (
        "ReborrowRegionBuilder::build(",
        "ReborrowRegionVerifier::verify(",
        "flowContainsMembers(flow, reference.owner, members.asPtr())",
        "reference.livePoints.afterCommit",
        "reference.livePoints.afterReturn",
        "sameRegions(candidate.regions",
    ):
        if marker not in regions:
            errors.append(f"{REGIONS}: missing bounded region production contract: {marker}")
    regions_header = values.get(REGIONS_HEADER, "")
    for marker in (
        "struct ReborrowRegion final",
        "MirEventKey entry;",
        "MirEventKey loan;",
        "uint32_t inputParameter;",
        "zc::Vector<OwnershipPoint> members;",
        "class VerifiedReborrowRegions final",
    ):
        if marker not in regions_header:
            errors.append(f"{REGIONS_HEADER}: missing bounded region contract: {marker}")

    states = values.get(STATES, "")
    for marker in (
        "ReborrowStateBuilder::build(",
        "ReborrowStateVerifier::verify(",
        "regionFor(regions, reference)",
        "value.members.size() != 6",
        "sameStates(candidate.states",
    ):
        if marker not in states:
            errors.append(f"{STATES}: missing bounded reference-state production contract: {marker}")
    states_header = values.get(STATES_HEADER, "")
    for marker in (
        "struct ReborrowState final",
        "OwnershipPoint point;",
        "MirEventKey loan;",
        "uint32_t inputParameter;",
        "MovePathKey destination;",
        "class VerifiedReborrowStates final",
    ):
        if marker not in states_header:
            errors.append(f"{STATES_HEADER}: missing bounded reference-state contract: {marker}")

    resources = values.get(RESOURCES, "")
    for marker in (
        "OwnershipResourceBuilder::build(",
        "OwnershipResourceVerifier::verify(",
        "value.logicalDropPlans",
        "zc::Maybe<DropRequirement> requirement(",
        "positive(component.copyDecision, overlay)",
        "positive(component.linearDecision, overlay)",
        "component.dropAction != zc::none && copy",
        "DropRequirement::LinearLogical",
        "moveTransferInitialization(",
        "isParameterRootTransfer(",
        "resourceAt(",
        "sameTransfers(",
        "sameFunctions(candidate.functions",
    ):
        if marker not in resources:
            errors.append(f"{RESOURCES}: missing logical-resource production contract: {marker}")
    transfer_marker = "DropTransfer{MovePathKey{mirFunction.owner, transferValue.source.clone()},"
    if resources.count(transfer_marker) != 2:
        errors.append(f"{RESOURCES}: missing complete logical-resource transfer coverage")
    resources_header = values.get(RESOURCES_HEADER, "")
    for marker in (
        "struct DropResourceSubject final",
        "MirEventKey introduction;",
        "MovePathKey origin;",
        "identity::SemanticTypeId originType;",
        "enum class DropRequirement : uint8_t",
        "struct OwnershipResourceFact final",
        "DropResourceSubject subject;",
        "DropRequirement requirement;",
        "zc::Maybe<LogicalDropAction> dropAction;",
        "struct DropTransfer final",
        "MovePathKey from;",
        "MovePathKey to;",
        "MirEventKey event;",
        "class VerifiedOwnershipResourceFacts final",
    ):
        if marker not in resources_header:
            errors.append(f"{RESOURCES_HEADER}: missing logical-resource fact contract: {marker}")

    mir_header = values.get(MIR_HEADER, "")
    if "BorrowEvidenceLookupResult borrowEvidence() const noexcept;" not in mir_header:
        errors.append(f"{MIR_HEADER}: missing ownership-only borrow-evidence lookup")

    mir = values.get(MIR, "")
    for marker in (
        "VerifiedBuiltMir::borrowEvidence()",
        "impl->borrowEvidenceLease.matches(lease)",
        "impl->borrowEvidenceCapability.matches(capability)",
    ):
        if marker not in mir:
            errors.append(f"{MIR}: missing ownership borrow-evidence resolution: {marker}")

    borrow_evidence = values.get(BORROW_EVIDENCE_SOURCE, "")
    if "repository == other.repository && state == other.state" not in borrow_evidence:
        errors.append(
            f"{BORROW_EVIDENCE_SOURCE}: missing exact borrow-evidence capability identity"
        )

    inputs = values.get(INPUTS, "")
    for marker in (
        "OwnershipInputVerifier::verify(",
        "bool matches(",
        "builtMir.borrowEvidenceRevision()",
        "VerifiedReborrowRegions&& regions",
        "VerifiedReborrowStates&& states",
        "VerifiedOwnershipResourceFacts&& resources",
        "VerifiedFlow&& flow",
        "builtMir.matchesBorrowEvidenceInput(lease, capability)",
        "InputRevisionMismatch",
    ):
        if marker not in inputs:
            errors.append(f"{INPUTS}: missing ownership-input verification contract: {marker}")
    inputs_header = values.get(INPUTS_HEADER, "")
    for marker in (
        "class VerifiedOwnershipInputs final",
        "const VerifiedMovePaths& movePaths() const noexcept;",
        "const VerifiedFlow& flow() const noexcept;",
        "const VerifiedInitializationFacts& initialization() const noexcept;",
        "const VerifiedLoanFacts& loans() const noexcept;",
        "const VerifiedReferenceDefinitions& references() const noexcept;",
        "const VerifiedReborrowRegions& regions() const noexcept;",
        "const VerifiedReborrowStates& states() const noexcept;",
        "const VerifiedOwnershipResourceFacts& resources() const noexcept;",
        "const driver::borrow_evidence::VerifiedBorrowEvidenceLease& lease,",
        "const driver::borrow_evidence::BorrowEvidenceRepositoryCapability& capability);",
        "class OwnershipInputVerifier final",
    ):
        if marker not in inputs_header:
            errors.append(f"{INPUTS_HEADER}: missing ownership-input bundle contract: {marker}")

    compiler_session_test = values.get(COMPILER_SESSION_TEST, "")
    for marker in (
        "PublishesOwnershipFacts",
        "WithoutPublishingOwnershipFacts",
    ):
        if marker in compiler_session_test:
            errors.append(
                f"{COMPILER_SESSION_TEST}: must name the current output VerifiedOwnershipInputs"
            )

    ownership_cmake = values.get(OWNERSHIP_CMAKE, "")
    for marker in (
        "${CMAKE_CURRENT_SOURCE_DIR}/facts/inputs.cc",
        "${CMAKE_CURRENT_SOURCE_DIR}/facts/flow.cc",
        "${CMAKE_CURRENT_SOURCE_DIR}/facts/loans.cc",
        "${CMAKE_CURRENT_SOURCE_DIR}/facts/resources.cc",
        "${CMAKE_CURRENT_SOURCE_DIR}/facts/regions.cc",
        "${CMAKE_CURRENT_SOURCE_DIR}/facts/refs.cc",
        "${CMAKE_CURRENT_SOURCE_DIR}/facts/states.cc",
    ):
        if marker not in ownership_cmake:
            errors.append(f"{OWNERSHIP_CMAKE}: missing ownership fact production source: {marker}")

    borrow_evidence_header = values.get(BORROW_EVIDENCE_HEADER, "")
    capability_marker = "class BorrowEvidenceRepositoryCapability final"
    repository_marker = "class BorrowEvidenceRepository final"
    lease_marker = "class VerifiedBorrowEvidenceLease final"
    capability_start = borrow_evidence_header.find(capability_marker)
    repository_start = borrow_evidence_header.find(repository_marker)
    lease_start = borrow_evidence_header.find(lease_marker)
    capability_header = ""
    repository_header = ""
    if capability_start != -1 and repository_start != -1 and capability_start < repository_start:
        capability_header = borrow_evidence_header[capability_start:repository_start]
        repository_header = borrow_evidence_header[repository_start:]
    for marker in (
        capability_marker,
        "BorrowEvidenceLookupResult\n  lookup(const VerifiedBorrowEvidenceLease& lease) const noexcept;",
    ):
        if marker not in capability_header:
            errors.append(f"{BORROW_EVIDENCE_HEADER}: missing evidence capability contract: {marker}")
    if "BorrowEvidenceRepositoryCapability capability() const noexcept;" not in repository_header:
        errors.append(f"{BORROW_EVIDENCE_HEADER}: missing repository capability factory")
    if "lookup(const VerifiedBorrowEvidenceLease& lease) const noexcept;" in repository_header:
        errors.append(f"{BORROW_EVIDENCE_HEADER}: must not expose lease-only evidence lookup")
    if lease_start == -1:
        errors.append(f"{BORROW_EVIDENCE_HEADER}: missing evidence lease contract")
    else:
        lease_public = borrow_evidence_header[lease_start:borrow_evidence_header.find("private:", lease_start)]
        if "VerifiedBorrowEvidenceLease clone()" in lease_public:
            errors.append(f"{BORROW_EVIDENCE_HEADER}: must not expose lease cloning")
    if capability_start != -1:
        capability_public = borrow_evidence_header[
            capability_start:borrow_evidence_header.find("private:", capability_start)
        ]
        if "BorrowEvidenceRepositoryCapability clone()" in capability_public:
            errors.append(f"{BORROW_EVIDENCE_HEADER}: must not expose capability cloning")

    borrow_evidence_source = values.get(BORROW_EVIDENCE_SOURCE, "")
    for marker in (
        "BorrowEvidenceRepository::capability() const noexcept",
        "lease.context != capability.context || lease.repository != capability.repository",
    ):
        if marker not in borrow_evidence_source:
            errors.append(f"{BORROW_EVIDENCE_SOURCE}: missing evidence capability enforcement: {marker}")

    hir = values.get(HIR, "")
    for marker in (
        "checkedModule.borrowEvidenceCapability();",
        "borrowCapability.lookup(checkedModule.borrowEvidenceLease());",
        "impl->borrowEvidenceCapability.clone();",
    ):
        if marker not in hir:
            errors.append(f"{HIR}: missing explicit evidence capability use: {marker}")
    if "borrowEvidenceRepository()" in hir:
        errors.append(f"{HIR}: must not recover the evidence repository")

    hir_header = values.get(HIR_HEADER, "")
    if "borrowEvidenceRepository()" in hir_header or "borrowEvidenceCapability()" not in hir_header:
        errors.append(f"{HIR_HEADER}: must expose only an evidence capability")

    checked_module = values.get(CHECKED_MODULE, "")
    if "VerifiedCheckedModule::borrowEvidenceCapability() const noexcept" not in checked_module:
        errors.append(f"{CHECKED_MODULE}: missing checked-module evidence capability")

    checked_module_header = values.get(CHECKED_MODULE_HEADER, "")
    if (
        "borrowEvidenceRepository()" in checked_module_header
        or "borrowEvidenceCapability()" not in checked_module_header
    ):
        errors.append(f"{CHECKED_MODULE_HEADER}: must expose only an evidence capability")

    mir = values.get(MIR, "")
    for marker in (
        "hirModule.borrowEvidenceCapability();",
        "borrowCapability.lookup(hirModule.borrowEvidenceLease());",
        "hirModule.borrowEvidenceLease().clone()",
    ):
        if marker not in mir:
            errors.append(f"{MIR}: missing explicit evidence capability use: {marker}")

    session = values.get(SESSION, "")
    for marker in (
        "zc::Vector<ownership::OwnershipAdmittedBoundModule> checkerModules(",
        "ownership::OwnershipSurfaceAdmissionBuilder::admit(boundModule.retain())",
        "admission.is<ownership::OwnershipSurfaceSourceRejected>()",
        "diagnostics::DiagID::ConcurrencySemanticsUnavailable",
        "diagnostics::DiagID::ControlFlowSemanticsUnavailable",
        "stagedOwnershipEventOverlays.add(zc::mv(verifiedOwnership).takeVerified());",
        "impl->ownershipEventOverlays = zc::mv(stagedOwnershipEventOverlays);",
        "zc::Vector<ownership::OwnershipAdmittedBoundModule> ownershipAdmittedModules;",
        "stagedOwnershipAdmittedModules.add(checkerBound.retain());",
        "impl->ownershipAdmittedModules = zc::mv(stagedOwnershipAdmittedModules);",
        "ownershipAdmittedModules.clear();",
        "ownershipEventOverlays.clear();",
        "builtMirModules.clear();",
        "ReferenceDefinitionBuilder::build(",
        "ReferenceDefinitionVerifier::verify(",
        "ReborrowRegionBuilder::build(",
        "ReborrowRegionVerifier::verify(",
        "InitializationSourceVerifier::verify(",
        "if (initializationSource.isSourceRejected())",
        "zc::mv(initializationSource).takeSourceFailures()",
        "verifiedFlow.verifiedValue(), verifiedLoans.verifiedValue()",
        "ReborrowStateBuilder::build(",
        "ReborrowStateVerifier::verify(",
        "OwnershipResourceBuilder::build(",
        "OwnershipResourceVerifier::verify(",
        "FlowBuilder::build(",
        "FlowVerifier::verify(",
        "verifiedFlow.verifiedValue(), verifiedMovePaths.verifiedValue()",
        "OwnershipInputVerifier::verify(",
        "stagedOwnershipInputs.add(zc::mv(verifiedOwnershipInputs).takeVerified());",
        "impl->ownershipInputs = zc::mv(stagedOwnershipInputs);",
        "ownershipInputs.clear();",
    ):
        if marker not in session:
            errors.append(f"{SESSION}: missing ownership publication contract: {marker}")
    inputs_release = session.find("ownershipInputs.clear();")
    overlay_release = session.find("ownershipEventOverlays.clear();")
    mir_release = session.find("builtMirModules.clear();")
    if (
        inputs_release != -1
        and overlay_release != -1
        and mir_release != -1
        and not inputs_release < overlay_release < mir_release
    ):
        errors.append(f"{SESSION}: ownership facts must release before their retained inputs")
    inputs_publish = session.find("impl->ownershipInputs = zc::mv(stagedOwnershipInputs);")
    overlay_publish = session.find("impl->ownershipEventOverlays = zc::mv(stagedOwnershipEventOverlays);")
    mir_publish = session.find("impl->builtMirModules = zc::mv(stagedBuiltMirModules);")
    if (
        min(inputs_publish, overlay_publish, mir_publish) != -1
        and not mir_publish < overlay_publish < inputs_publish
    ):
        errors.append(f"{SESSION}: ownership facts must publish after their retained inputs")
    source_verifier = session.find("InitializationSourceVerifier::verify(")
    source_rejection = session.find("if (initializationSource.isSourceRejected())")
    loan_builder = session.find("LoanBuilder::build(")
    if source_verifier == -1 or source_rejection == -1 or loan_builder == -1 or not (
        source_verifier < source_rejection < loan_builder
    ):
        errors.append(f"{SESSION}: initialization source rejection must precede loan publication")

    mir = values.get(MIR, "")
    for marker in (
        "bool validLocalAggregateReturnFunction(",
        "ZC_IF_SOME(reference, localReference)",
        "valid = validLocalAggregateReturnFunction(",
    ):
        if marker not in mir:
            errors.append(f"{MIR}: missing whole-aggregate return lowering contract: {marker}")

    test = values.get(TEST, "")
    for marker in (
        "Ownership event overlay verifier rejects a tampered function slot count",
        "Ownership event overlay verifier rejects a tampered slot role",
        "Ownership event overlay verifier rejects a foreign event owner",
        "CompilerSession publishes verified ownership event overlays",
        "Ownership event overlay projects standard marker decisions on resource roots",
        "Ownership event overlay verifier rejects a missing marker use",
        "Ownership event overlay verifier rejects a missing logical drop plan",
        "Ownership event overlay verifier rejects a spurious scalar drop component",
        "Move-path verifier rejects a tampered root path type chain",
        "Move-path verifier rejects a root path with a parent",
        "Move-path verifier rejects a self conflict pair",
        "Move-path verifier rejects a reversed aggregate path order",
        "Move-path verifier rejects a reversed aggregate conflict pair",
        "Move-path verifier rejects a missing aggregate field conflict",
        "Initialization verifier rejects a tampered local state",
        "ownershipInputs(foreignFixture.compilerSession()).flow()",
        "Initialization verifier rejects a tampered field path state",
        "Loan verifier rejects a tampered active point, issue, commit, and foreign lineage",
        "Reference definition verifier rejects tampered definition inputs",
        "Parameter reborrow region verifier rejects tampered members",
        "Parameter reborrow reference-state verifier rejects tampered point",
        "Flow verifier rejects a tampered direct-call continuation",
        "Flow inventory connects direct-call continuation cutpoints",
        "Ownership input verifier rejects facts from a foreign analysis snapshot",
        "foreignFixture.builtMir().borrowEvidenceLease()",
        "Ownership input verifier rejects a foreign borrow evidence capability",
        "foreignFixture.compilerSession().getBorrowEvidenceRepository()",
        "Ownership input snapshots retain live borrow evidence",
        "Ownership facts lower a noncopy aggregate local return as a move",
    ):
        if marker not in test:
            errors.append(f"{TEST}: missing ownership mutation or production test: {marker}")

    hir_test = values.get(HIR_TEST, "")
    for marker in (
        "movePaths.conflicts(ZC_REQUIRE_NONNULL(root).key, ZC_REQUIRE_NONNULL(root).key)",
        "movePaths.conflicts(ZC_REQUIRE_NONNULL(root).key, ZC_REQUIRE_NONNULL(field).key)",
        "movePaths.conflicts(ZC_REQUIRE_NONNULL(field).key, ZC_REQUIRE_NONNULL(root).key)",
    ):
        if marker not in hir_test:
            errors.append(f"{HIR_TEST}: missing move-path conflict query regression: {marker}")

    session_test = values.get(SESSION_TEST, "")
    for marker in (
        "CompilerSession rejects unadmitted frontend syntax before Checker publication",
        "diagnostics::DiagID::ConcurrencySemanticsUnavailable",
        "diagnostics::DiagID::ControlFlowSemanticsUnavailable",
    ):
        if marker not in session_test:
            errors.append(f"{SESSION_TEST}: missing ownership admission regression test: {marker}")

    cmake = values.get(TEST_CMAKE, "")
    for marker in ("ownership-architecture", "ownership-architecture-negative"):
        if marker not in cmake:
            errors.append(f"{TEST_CMAKE}: missing ownership architecture gate: {marker}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    values = files()
    if args.self_test:
        mutated = dict(values)
        mutated[OVERLAY] = mutated.get(OVERLAY, "").replace(
            "builtMir.retainAdmittedBoundModule()", "builtMir.detachedBoundModule()", 1
        )
        marker_mutation = dict(values)
        marker_mutation[OVERLAY] = marker_mutation.get(OVERLAY, "").replace(
            "MarkerProofInput::from(input.body)", "MarkerProofInput::from(staleBodyInput)"
        )
        if not check(mutated):
            print("ownership architecture self-test escaped")
            return 1
        if not check(marker_mutation):
            print("ownership marker architecture self-test escaped")
            return 1
        admitted_input_mutation = dict(values)
        admitted_input_mutation[OVERLAY] = admitted_input_mutation.get(OVERLAY, "").replace(
            "const auto& admitted = input.admitted;", "const auto& admitted = detachedInput;", 1
        )
        if not check(admitted_input_mutation):
            print("ownership admitted-input architecture self-test escaped")
            return 1
        capability_mutation = dict(values)
        capability_mutation[HIR] = capability_mutation.get(HIR, "").replace(
            "checkedModule.borrowEvidenceCapability()",
            "checkedModule.leaseOnlyCapability()",
        )
        if not check(capability_mutation):
            print("ownership evidence capability architecture self-test escaped")
            return 1
        evidence_clone_mutation = dict(values)
        evidence_clone_mutation[BORROW_EVIDENCE_HEADER] = evidence_clone_mutation.get(
            BORROW_EVIDENCE_HEADER, ""
        ).replace("private:\n  ZC_NODISCARD VerifiedBorrowEvidenceLease clone() const;",
                  "public:\n  ZC_NODISCARD VerifiedBorrowEvidenceLease clone() const;", 1)
        if not check(evidence_clone_mutation):
            print("ownership evidence lease-clone architecture self-test escaped")
            return 1
        admission_mutation = dict(values)
        admission_mutation[SESSION] = admission_mutation.get(SESSION, "").replace(
            "ownership::OwnershipSurfaceAdmissionBuilder::admit(boundModule.retain())",
            "ownership::OwnershipSurfaceAdmissionBuilder::admit(unverifiedBoundModule.retain())",
            1,
        )
        if not check(admission_mutation):
            print("ownership admission architecture self-test escaped")
            return 1
        admitted_publication_mutation = dict(values)
        admitted_publication_mutation[SESSION] = admitted_publication_mutation.get(SESSION, "").replace(
            "impl->ownershipAdmittedModules = zc::mv(stagedOwnershipAdmittedModules);", "", 1
        )
        if not check(admitted_publication_mutation):
            print("ownership admitted-publication architecture self-test escaped")
            return 1
        move_path_fact_mutation = dict(values)
        move_path_fact_mutation[MOVE_PATHS_HEADER] = move_path_fact_mutation.get(
            MOVE_PATHS_HEADER, ""
        ).replace("struct MovePathPair final", "struct RemovedMovePathPair final", 1)
        if not check(move_path_fact_mutation):
            print("ownership move-path fact architecture self-test escaped")
            return 1
        move_path_conflict_mutation = dict(values)
        move_path_conflict_mutation[MOVE_PATHS_HEADER] = move_path_conflict_mutation.get(
            MOVE_PATHS_HEADER, ""
        ).replace(
            "bool conflicts(const MovePathKey& first, const MovePathKey& second) const noexcept;",
            "bool hasConflict(const MovePathKey& first, const MovePathKey& second) const noexcept;",
            1,
        )
        if not check(move_path_conflict_mutation):
            print("ownership move-path conflict architecture self-test escaped")
            return 1
        move_path_order_mutation = dict(values)
        move_path_order_mutation[MOVE_PATHS] = move_path_order_mutation.get(MOVE_PATHS, "").replace(
            "if (!sortFacts(facts, identities)) return zc::none;",
            "if (!sortFacts(facts, staleIdentities)) return zc::none;",
            1,
        )
        if not check(move_path_order_mutation):
            print("ownership move-path order architecture self-test escaped")
            return 1
        move_path_query_mutation = dict(values)
        move_path_query_mutation[HIR_TEST] = move_path_query_mutation.get(HIR_TEST, "").replace(
            "movePaths.conflicts(ZC_REQUIRE_NONNULL(root).key, ZC_REQUIRE_NONNULL(field).key)",
            "movePaths.conflicts(ZC_REQUIRE_NONNULL(field).key, ZC_REQUIRE_NONNULL(field).key)",
            1,
        )
        if not check(move_path_query_mutation):
            print("ownership move-path query architecture self-test escaped")
            return 1
        projection_chain_mutation = dict(values)
        projection_chain_mutation[MOVE_PATHS] = projection_chain_mutation.get(
            MOVE_PATHS, ""
        ).replace(
            "bool matchesPlace(const mir::MirFunction& function, const mir::MirPlace& place) {\n  if (!place.hasConsistentTypeChain()) return false;",
            "bool matchesPlace(const mir::MirFunction& function, const mir::MirPlace& place) {\n  if (true) return false;",
            1,
        )
        if not check(projection_chain_mutation):
            print("ownership projection-chain architecture self-test escaped")
            return 1
        projection_validation_mutation = dict(values)
        projection_validation_mutation[MOVE_PATHS] = projection_validation_mutation.get(
            MOVE_PATHS, ""
        ).replace(
            "for (const auto& projection : place.projections()) {\n      if (!projection.isStructurallyValid()) return false;",
            "for (const auto& projection : place.projections()) {\n      if (true) return false;",
            1,
        )
        if not check(projection_validation_mutation):
            print("ownership projection-validation architecture self-test escaped")
            return 1
        index_resolution_mutation = dict(values)
        index_resolution_mutation[MOVE_PATHS] = index_resolution_mutation.get(MOVE_PATHS, "").replace(
            "if (!foundIndex) return false;", "if (foundIndex) return false;", 1
        )
        if not check(index_resolution_mutation):
            print("ownership index-resolution architecture self-test escaped")
            return 1
        initialization_key_mutation = dict(values)
        initialization_key_mutation[INITIALIZATION_HEADER] = initialization_key_mutation.get(
            INITIALIZATION_HEADER, ""
        ).replace("MovePathKey key;", "mir::MirLocalId local;", 1)
        if not check(initialization_key_mutation):
            print("ownership initialization-key architecture self-test escaped")
            return 1
        initialization_result_class_mutation = dict(values)
        initialization_result_class_mutation[INITIALIZATION_HEADER] = (
            initialization_result_class_mutation.get(INITIALIZATION_HEADER, "").replace(
                "class InitializationSourceVerificationResult final",
                "class RemovedInitializationSourceVerificationResult final",
                1,
            )
        )
        if not check(initialization_result_class_mutation):
            print("ownership source-result class architecture self-test escaped")
            return 1
        initialization_result_alias_mutation = dict(values)
        initialization_result_alias_mutation[INITIALIZATION_HEADER] = (
            initialization_result_alias_mutation.get(INITIALIZATION_HEADER, "").replace(
                "class InitializationSourceVerificationResult final",
                "using InitializationSourceVerificationResult =\n"
                "    ir::FeatureBoundaryVerificationResult<InitializationSourceAccepted,\n"
                "                                        InitializationSourceFailure,\n"
                "                                        InitializationSourceFailureOrdering>;\n\n"
                "class RemovedInitializationSourceVerificationResult final",
                1,
            )
        )
        if not check(initialization_result_alias_mutation):
            print("ownership source-result alias architecture self-test escaped")
            return 1
        initialization_result_factory_mutation = dict(values)
        initialization_result_factory_mutation[INITIALIZATION_HEADER] = (
            initialization_result_factory_mutation.get(INITIALIZATION_HEADER, "").replace(
                "private:\n  struct Verified final",
                "public:\n  struct Verified final",
                1,
            )
        )
        if not check(initialization_result_factory_mutation):
            print("ownership source-result factory architecture self-test escaped")
            return 1
        initialization_result_friend_mutation = dict(values)
        initialization_result_friend_mutation[INITIALIZATION_HEADER] = (
            initialization_result_friend_mutation.get(INITIALIZATION_HEADER, "").replace(
                "friend class InitializationSourceVerifier;",
                "friend class RemovedInitializationSourceVerifier;",
                1,
            )
        )
        if not check(initialization_result_friend_mutation):
            print("ownership source-result verifier authority self-test escaped")
            return 1
        initialization_source_session_mutation = dict(values)
        initialization_source_session_mutation[SESSION] = initialization_source_session_mutation.get(
            SESSION, ""
        ).replace(
            "if (initializationSource.isSourceRejected())",
            "if (initializationSource.isStaleSourceRejected())",
            1,
        )
        if not check(initialization_source_session_mutation):
            print("ownership source-result session architecture self-test escaped")
            return 1
        operand_read_mutation = dict(values)
        operand_read_mutation[INITIALIZATION] = operand_read_mutation.get(INITIALIZATION, "").replace(
            "if (!hasOperandRead(overlay, primary)) return false;",
            "if (!hasExactOperandRead(overlay, primary)) return false;",
            1,
        )
        if not check(operand_read_mutation):
            print("ownership source-slot role architecture self-test escaped")
            return 1
        loan_issue_mutation = dict(values)
        loan_issue_mutation[LOANS] = loan_issue_mutation.get(LOANS, "").replace(
            "OwnershipEventRole::BorrowIssue", "OwnershipEventRole::BorrowActivation", 1
        )
        if not check(loan_issue_mutation):
            print("ownership loan-issue architecture self-test escaped")
            return 1
        loan_lineage_mutation = dict(values)
        loan_lineage_mutation[LOANS_HEADER] = loan_lineage_mutation.get(LOANS_HEADER, "").replace(
            "BorrowEvidenceRevision borrowEvidenceRevision;",
            "BorrowEvidenceRevision staleBorrowEvidenceRevision;",
            1,
        )
        if not check(loan_lineage_mutation):
            print("ownership loan-lineage architecture self-test escaped")
            return 1
        loan_activation_mutation = dict(values)
        loan_activation_mutation[LOANS_HEADER] = loan_activation_mutation.get(
            LOANS_HEADER, ""
        ).replace("OwnershipPoint activeFrom;", "LoanActivation activation;\n  OwnershipPoint activeFrom;", 1)
        if not check(loan_activation_mutation):
            print("ownership redundant loan-activation architecture self-test escaped")
            return 1
        loan_active_from_mutation = dict(values)
        loan_active_from_mutation[LOANS_HEADER] = loan_active_from_mutation.get(
            LOANS_HEADER, ""
        ).replace("OwnershipPoint activeFrom;", "OwnershipPoint staleActiveFrom;", 1)
        if not check(loan_active_from_mutation):
            print("ownership loan-active-from architecture self-test escaped")
            return 1
        loan_source_mutation = dict(values)
        loan_source_mutation[LOANS_HEADER] = loan_source_mutation.get(LOANS_HEADER, "").replace(
            "MovePathKey source;", "MovePathKey staleSource;", 1
        )
        if not check(loan_source_mutation):
            print("ownership loan-source architecture self-test escaped")
            return 1
        loan_commit_mutation = dict(values)
        loan_commit_mutation[LOANS_HEADER] = loan_commit_mutation.get(LOANS_HEADER, "").replace(
            "MirEventKey commit;", "MirEventKey staleCommit;", 1
        )
        if not check(loan_commit_mutation):
            print("ownership loan-commit architecture self-test escaped")
            return 1
        reference_introduction_mutation = dict(values)
        reference_introduction_mutation[REFERENCES_HEADER] = reference_introduction_mutation.get(
            REFERENCES_HEADER, ""
        ).replace("MirEventKey introduction;", "MirEventKey staleIntroduction;", 1)
        if not check(reference_introduction_mutation):
            print("ownership reference-introduction architecture self-test escaped")
            return 1
        reference_origin_mutation = dict(values)
        reference_origin_mutation[REFERENCES_HEADER] = reference_origin_mutation.get(
            REFERENCES_HEADER, ""
        ).replace("ReferenceInputOrigin origin;", "ReferenceInputOrigin staleOrigin;", 1)
        if not check(reference_origin_mutation):
            print("ownership reference-origin architecture self-test escaped")
            return 1
        reference_activation_mutation = dict(values)
        reference_activation_mutation[REFERENCES_HEADER] = reference_activation_mutation.get(
            REFERENCES_HEADER, ""
        ).replace("OwnershipPoint activation;", "OwnershipPoint staleActivation;", 1)
        if not check(reference_activation_mutation):
            print("ownership reference-activation architecture self-test escaped")
            return 1
        reference_root_mutation = dict(values)
        reference_root_mutation[REFERENCES_HEADER] = reference_root_mutation.get(
            REFERENCES_HEADER, ""
        ).replace("uint32_t rootParameter;", "uint32_t staleRootParameter;", 1)
        if not check(reference_root_mutation):
            print("ownership reference-root architecture self-test escaped")
            return 1
        reference_referent_mutation = dict(values)
        reference_referent_mutation[REFERENCES_HEADER] = reference_referent_mutation.get(
            REFERENCES_HEADER, ""
        ).replace("MovePathKey referent;", "MovePathKey staleReferent;", 1)
        if not check(reference_referent_mutation):
            print("ownership reference-referent architecture self-test escaped")
            return 1
        reference_return_mutation = dict(values)
        reference_return_mutation[REFERENCES_HEADER] = reference_return_mutation.get(
            REFERENCES_HEADER, ""
        ).replace("MirEventKey returned;", "MirEventKey staleReturned;", 1)
        if not check(reference_return_mutation):
            print("ownership reference-return architecture self-test escaped")
            return 1
        reference_destination_mutation = dict(values)
        reference_destination_mutation[REFERENCES_HEADER] = reference_destination_mutation.get(
            REFERENCES_HEADER, ""
        ).replace("MovePathKey destination;", "mir::MirPlace destination;", 1)
        if not check(reference_destination_mutation):
            print("ownership reference-destination architecture self-test escaped")
            return 1
        reference_liveness_mutation = dict(values)
        reference_liveness_mutation[REFERENCES_HEADER] = reference_liveness_mutation.get(
            REFERENCES_HEADER, ""
        ).replace("ReferenceLivePoints livePoints;", "ReferenceLivePoints staleLivePoints;", 1)
        if not check(reference_liveness_mutation):
            print("ownership reference-liveness architecture self-test escaped")
            return 1
        region_members_mutation = dict(values)
        region_members_mutation[REGIONS_HEADER] = region_members_mutation.get(
            REGIONS_HEADER, ""
        ).replace("zc::Vector<OwnershipPoint> members;", "zc::Vector<OwnershipPoint> staleMembers;", 1)
        if not check(region_members_mutation):
            print("ownership region-members architecture self-test escaped")
            return 1
        region_session_mutation = dict(values)
        region_session_mutation[SESSION] = region_session_mutation.get(SESSION, "").replace(
            "ReborrowRegionVerifier::verify(", "ReborrowRegionVerifier::staleVerify(", 1
        )
        if not check(region_session_mutation):
            print("ownership region-session architecture self-test escaped")
            return 1
        state_point_mutation = dict(values)
        state_point_mutation[STATES_HEADER] = state_point_mutation.get(
            STATES_HEADER, ""
        ).replace("OwnershipPoint point;", "OwnershipPoint stalePoint;", 1)
        if not check(state_point_mutation):
            print("ownership reference-state point architecture self-test escaped")
            return 1
        loan_cmake_mutation = dict(values)
        loan_cmake_mutation[OWNERSHIP_CMAKE] = loan_cmake_mutation.get(
            OWNERSHIP_CMAKE, ""
        ).replace("${CMAKE_CURRENT_SOURCE_DIR}/facts/loans.cc", "", 1)
        if not check(loan_cmake_mutation):
            print("ownership loan-build architecture self-test escaped")
            return 1
        flow_cmake_mutation = dict(values)
        flow_cmake_mutation[OWNERSHIP_CMAKE] = flow_cmake_mutation.get(
            OWNERSHIP_CMAKE, ""
        ).replace("${CMAKE_CURRENT_SOURCE_DIR}/facts/flow.cc", "", 1)
        if not check(flow_cmake_mutation):
            print("ownership flow build architecture self-test escaped")
            return 1
        reference_cmake_mutation = dict(values)
        reference_cmake_mutation[OWNERSHIP_CMAKE] = reference_cmake_mutation.get(
            OWNERSHIP_CMAKE, ""
        ).replace("${CMAKE_CURRENT_SOURCE_DIR}/facts/refs.cc", "", 1)
        if not check(reference_cmake_mutation):
            print("ownership reference-build architecture self-test escaped")
            return 1
        region_cmake_mutation = dict(values)
        region_cmake_mutation[OWNERSHIP_CMAKE] = region_cmake_mutation.get(
            OWNERSHIP_CMAKE, ""
        ).replace("${CMAKE_CURRENT_SOURCE_DIR}/facts/regions.cc", "", 1)
        if not check(region_cmake_mutation):
            print("ownership region-build architecture self-test escaped")
            return 1
        state_cmake_mutation = dict(values)
        state_cmake_mutation[OWNERSHIP_CMAKE] = state_cmake_mutation.get(
            OWNERSHIP_CMAKE, ""
        ).replace("${CMAKE_CURRENT_SOURCE_DIR}/facts/states.cc", "", 1)
        if not check(state_cmake_mutation):
            print("ownership reference-state build architecture self-test escaped")
            return 1
        resource_path_mutation = dict(values)
        resource_path_mutation[RESOURCES_HEADER] = resource_path_mutation.get(
            RESOURCES_HEADER, ""
        ).replace("MovePathKey origin;", "MovePathKey staleOrigin;", 1)
        if not check(resource_path_mutation):
            print("ownership resource-subject architecture self-test escaped")
            return 1
        resource_transfer_mutation = dict(values)
        resource_transfer_mutation[RESOURCES] = resource_transfer_mutation.get(RESOURCES, "").replace(
            "DropTransfer{MovePathKey{mirFunction.owner, transferValue.source.clone()},",
            "DropTransfer{MovePathKey{mirFunction.owner, staleTransfer.source.clone()},",
            1,
        )
        if not check(resource_transfer_mutation):
            print("ownership resource-transfer architecture self-test escaped")
            return 1
        resource_cmake_mutation = dict(values)
        resource_cmake_mutation[OWNERSHIP_CMAKE] = resource_cmake_mutation.get(
            OWNERSHIP_CMAKE, ""
        ).replace("${CMAKE_CURRENT_SOURCE_DIR}/facts/resources.cc", "", 1)
        if not check(resource_cmake_mutation):
            print("ownership resource-build architecture self-test escaped")
            return 1
        resource_session_mutation = dict(values)
        resource_session_mutation[SESSION] = resource_session_mutation.get(SESSION, "").replace(
            "OwnershipResourceVerifier::verify(", "OwnershipResourceVerifier::staleVerify(", 1
        )
        if not check(resource_session_mutation):
            print("ownership resource-session architecture self-test escaped")
            return 1
        aggregate_move_mutation = dict(values)
        aggregate_move_mutation[MIR] = aggregate_move_mutation.get(MIR, "").replace(
            "bool validLocalAggregateReturnFunction(",
            "bool removedLocalAggregateReturnFunction(",
            1,
        )
        if not check(aggregate_move_mutation):
            print("ownership whole-aggregate return architecture self-test escaped")
            return 1
        input_bundle_mutation = dict(values)
        input_bundle_mutation[INPUTS_HEADER] = input_bundle_mutation.get(INPUTS_HEADER, "").replace(
            "class VerifiedOwnershipInputs final", "class RemovedOwnershipInputs final", 1
        )
        if not check(input_bundle_mutation):
            print("ownership input-bundle architecture self-test escaped")
            return 1
        input_name_mutation = dict(values)
        input_name_mutation[COMPILER_SESSION_TEST] = input_name_mutation.get(
            COMPILER_SESSION_TEST, ""
        ).replace("PublishesVerifiedOwnershipInputs", "PublishesOwnershipFacts", 1)
        if not check(input_name_mutation):
            print("ownership input-publication naming architecture self-test escaped")
            return 1
        explicit_input_mutation = dict(values)
        explicit_input_mutation[INPUTS] = explicit_input_mutation.get(INPUTS, "").replace(
            "builtMir.matchesBorrowEvidenceInput(lease, capability)",
            "true",
            1,
        )
        if not check(explicit_input_mutation):
            print("ownership explicit-borrow-input architecture self-test escaped")
            return 1
        capability_identity_mutation = dict(values)
        capability_identity_mutation[BORROW_EVIDENCE_SOURCE] = capability_identity_mutation.get(
            BORROW_EVIDENCE_SOURCE, ""
        ).replace(" && state == other.state", "", 1)
        if not check(capability_identity_mutation):
            print("ownership capability-identity architecture self-test escaped")
            return 1
        for marker in (
            "ReferenceDefinitionBuilder::build(",
            "ReferenceDefinitionVerifier::verify(",
            "OwnershipInputVerifier::verify(",
            "stagedOwnershipInputs.add(zc::mv(verifiedOwnershipInputs).takeVerified());",
            "impl->ownershipInputs = zc::mv(stagedOwnershipInputs);",
        ):
            publication_mutation = dict(values)
            publication_mutation[SESSION] = publication_mutation.get(SESSION, "").replace(
                marker, "", 1
            )
            if not check(publication_mutation):
                print("ownership publication architecture self-test escaped")
                return 1
        release_mutation = dict(values)
        release_mutation[SESSION] = release_mutation.get(SESSION, "").replace(
            "ownershipInputs.clear();\n    ownershipEventOverlays.clear();",
            "ownershipEventOverlays.clear();\n    ownershipInputs.clear();",
            1,
        )
        if not check(release_mutation):
            print("ownership teardown architecture self-test escaped")
            return 1
        print("ownership architecture self-test passed")
        return 0
    errors = check(values)
    if errors:
        print("\n".join(errors))
        return 1
    print("ownership architecture check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
