#!/usr/bin/env python3

import hashlib
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RFC_DIR = ROOT / "docs" / "rfc"
README = RFC_DIR / "README.md"
MANIFEST = ROOT / ".agents" / "subagents" / "manifest.yaml"

ALLOWED_STATUSES = {
    "DRAFT",
    "REVIEW",
    "ACCEPTED",
    "IMPLEMENTING",
    "LANDED",
    "RETURNED",
    "REJECTED",
    "WITHDRAWN",
    "SUPERSEDED",
}

REVIEWED_STATUSES = {
    "REVIEW",
    "ACCEPTED",
    "IMPLEMENTING",
    "LANDED",
    "RETURNED",
    "REJECTED",
    "SUPERSEDED",
}

ACCEPTED_STATUSES = {
    "ACCEPTED",
    "IMPLEMENTING",
    "LANDED",
    "SUPERSEDED",
}

IMPLEMENTATION_STATUSES = {
    "IMPLEMENTING",
    "LANDED",
}

TRANSITIONS = {
    "DRAFT": {"REVIEW", "WITHDRAWN"},
    "REVIEW": {"ACCEPTED", "RETURNED", "REJECTED", "WITHDRAWN"},
    "RETURNED": {"DRAFT"},
    "ACCEPTED": {"IMPLEMENTING", "SUPERSEDED"},
    "IMPLEMENTING": {"LANDED", "SUPERSEDED"},
    "LANDED": {"SUPERSEDED"},
    "REJECTED": set(),
    "WITHDRAWN": set(),
    "SUPERSEDED": set(),
}

REQUIRED_FRONTMATTER = [
    "rfc",
    "title",
    "type",
    "status",
    "author",
    "review-manager",
    "required-owners",
    "approvers",
    "created",
    "updated",
    "area",
    "requires",
    "supersedes",
    "superseded-by",
    "discussion",
    "decision",
    "implementation",
    "tracking-issue",
]

REQUIRED_SECTIONS = [
    "Summary",
    "Motivation",
    "Goals",
    "Non-Goals",
    "Prior Art",
    "Guide-Level Explanation",
    "Reference-Level Design",
    "Repository Impact",
    "Security And Safety Impact",
    "Drawbacks And Risks",
    "Alternatives Considered",
    "Compatibility And Rollout",
    "Documentation And Teaching Plan",
    "Operational Readiness",
    "Acceptance Criteria",
    "Implementation Plan",
    "Test Plan",
    "Open Questions",
    "Status History",
]

VALID_AREAS = {
    "language",
    "compiler",
    "runtime",
    "tooling",
    "testing",
    "docs",
    "process",
    "agents",
}

VALID_TYPES = {
    "language",
    "compiler",
    "runtime",
    "testing",
    "process",
    "informational",
}

DISALLOWED_GOVERNANCE_REFERENCES = [
    "docs/dev/rfcs/",
    "rfcs/attributes/",
    "rfcs/stdlib-markers/",
    "rfcs/stdlib/",
]


class Checker:
    def __init__(self):
        self.errors = []
        self.subagents = self._load_subagents()

    def fail(self, path, message):
        self.errors.append(f"{self._rel(path)}: {message}")

    def _rel(self, path):
        return str(Path(path).relative_to(ROOT))

    def _load_subagents(self):
        if not MANIFEST.exists():
            self.fail(MANIFEST, "missing subagent manifest")
            return set()
        text = MANIFEST.read_text(encoding="utf-8")
        return set(re.findall(r"^\s+- id:\s*([A-Za-z0-9_-]+)\s*$", text, re.MULTILINE))

    def run(self):
        proposals = sorted(
            path
            for path in RFC_DIR.glob("*.md")
            if path.name not in {"README.md", "0000-template.md"}
        )
        self._check_index(proposals)
        self._check_rfc_directory()
        self._check_markdown_fences(RFC_DIR / "README.md")
        self._check_markdown_fences(RFC_DIR / "0000-template.md")
        for path in proposals:
            self._check_proposal(path)
        self._check_disallowed_governance_references()
        if self.errors:
            print("RFC checks failed:", file=sys.stderr)
            for error in self.errors:
                print(f"  - {error}", file=sys.stderr)
            return 1
        print(f"RFC checks passed ({len(proposals)} proposal RFCs).")
        return 0

    def _check_rfc_directory(self):
        template = RFC_DIR / "0000-template.md"
        if not template.exists():
            self.fail(template, "missing RFC template")

    def _check_index(self, proposals):
        if not README.exists():
            self.fail(README, "missing RFC process README")
            return
        text = README.read_text(encoding="utf-8")
        if "## RFC Index" not in text:
            self.fail(README, "missing RFC Index section")
            return
        for path in proposals:
            frontmatter, _ = self._parse_frontmatter(path)
            if not frontmatter:
                continue
            link = f"({path.name})"
            if link not in text:
                self.fail(README, f"RFC index does not link {path.name}")
                continue
            line = next((line for line in text.splitlines() if link in line), "")
            title = str(frontmatter.get("title", ""))
            status = str(frontmatter.get("status", ""))
            if f"| {title} |" not in line:
                self.fail(README, f"RFC index title for {path.name} does not match frontmatter")
            if f"| {status} |" not in line:
                self.fail(README, f"RFC index status for {path.name} does not match frontmatter")

    def _check_proposal(self, path):
        match = re.fullmatch(r"(\d{4})-[a-z0-9]+(?:-[a-z0-9]+)*\.md", path.name)
        if not match:
            self.fail(path, "proposal filename must be NNNN-lowercase-kebab-case.md")
            return
        number = int(match.group(1))
        frontmatter, body = self._parse_frontmatter(path)
        if not frontmatter:
            return

        for field in REQUIRED_FRONTMATTER:
            if field not in frontmatter:
                self.fail(path, f"missing frontmatter field: {field}")

        rfc_number = frontmatter.get("rfc")
        if rfc_number != number:
            self.fail(path, f"frontmatter rfc {rfc_number!r} does not match filename number {number}")

        status = str(frontmatter.get("status", ""))
        if status not in ALLOWED_STATUSES:
            self.fail(path, f"invalid status: {status!r}")

        area = str(frontmatter.get("area", ""))
        if area not in VALID_AREAS:
            self.fail(path, f"invalid area: {area!r}")

        rfc_type = str(frontmatter.get("type", ""))
        if rfc_type not in VALID_TYPES:
            self.fail(path, f"invalid type: {rfc_type!r}")

        expected_heading = f"# RFC {number:04d}: {frontmatter.get('title')}"
        if expected_heading not in body.splitlines()[:5]:
            self.fail(path, f"missing heading: {expected_heading}")

        headings = re.findall(r"^## ([^\n]+)$", body, re.MULTILINE)
        if headings != REQUIRED_SECTIONS:
            self.fail(path, "required sections are missing or out of order")

        required_owners = set(self._as_list(frontmatter.get("required-owners")))
        approvers = set(self._as_list(frontmatter.get("approvers")))
        impact_owners = self._parse_repository_impact_owners(path, body)

        for owner in required_owners | impact_owners:
            if owner not in self.subagents:
                self.fail(path, f"unknown owner in RFC metadata or Repository Impact: {owner}")

        if required_owners != impact_owners:
            self.fail(
                path,
                "required-owners must exactly match Repository Impact owners "
                f"(frontmatter={sorted(required_owners)}, table={sorted(impact_owners)})",
            )

        self._check_review_state(path, frontmatter, body, status, required_owners, approvers)
        self._check_status_history(path, frontmatter, body, status)
        self._check_links(path, frontmatter)
        self._check_review_snapshot(path, frontmatter, status)
        self._check_markdown_fences(path)

    def _check_review_state(self, path, frontmatter, body, status, required_owners, approvers):
        review_manager = str(frontmatter.get("review-manager", ""))
        if status in REVIEWED_STATUSES:
            for field in ("review-manager", "discussion", "tracking-issue"):
                if self._is_tbd(frontmatter.get(field)):
                    self.fail(path, f"{field} must be set before status {status}")
            if not required_owners:
                self.fail(path, f"required-owners must be non-empty before status {status}")
            if review_manager not in self.subagents and not self._looks_like_person(review_manager):
                self.fail(path, f"review-manager must be a subagent id or person handle: {review_manager!r}")

        if status in ACCEPTED_STATUSES:
            if self._is_tbd(frontmatter.get("decision")):
                self.fail(path, f"decision must be set before status {status}")
            missing = required_owners - approvers
            if missing:
                self.fail(path, f"approvers do not cover required owners: {sorted(missing)}")
            self._check_open_questions_for_acceptance(path, body)

        if status in IMPLEMENTATION_STATUSES and self._is_tbd(frontmatter.get("implementation")):
            self.fail(path, f"implementation must be set before status {status}")

    def _check_open_questions_for_acceptance(self, path, body):
        questions = self._section_body(body, "Open Questions").strip()
        if questions == "None":
            return
        for line in questions.splitlines():
            stripped = line.strip()
            if not stripped:
                continue
            if not stripped.startswith("- "):
                self.fail(path, "Open Questions must be None or a bullet list")
                continue
            lower = stripped.lower()
            if "non-blocking" not in lower or "follow-up" not in lower:
                self.fail(path, "accepted RFC open questions must be non-blocking and assigned to follow-up")

    def _check_status_history(self, path, frontmatter, body, status):
        history = self._section_body(body, "Status History")
        rows = []
        for line in history.splitlines():
            match = re.match(r"^\|\s*(\d{4}-\d{2}-\d{2})\s*\|\s*([A-Z]+)\s*\|", line)
            if match:
                rows.append(match.groups())
        if not rows:
            self.fail(path, "Status History must contain at least one dated status row")
            return
        if rows[0][1] != "DRAFT":
            self.fail(path, "Status History must start at DRAFT")
        if rows[-1][1] != status:
            self.fail(path, "last Status History row must match frontmatter status")
        updated = str(frontmatter.get("updated", ""))
        if updated < rows[-1][0]:
            self.fail(path, "frontmatter updated date must be on or after the last Status History date")
        for (_, left), (_, right) in zip(rows, rows[1:]):
            if right == left:
                continue
            if right not in TRANSITIONS.get(left, set()):
                self.fail(path, f"illegal status transition: {left} -> {right}")

    def _check_links(self, path, frontmatter):
        for field in ("discussion", "decision", "implementation", "tracking-issue"):
            value = frontmatter.get(field)
            if self._is_tbd(value):
                continue
            text = str(value)
            if text.startswith(("http://", "https://", "#")):
                continue
            local = text.split("#", 1)[0]
            target = (ROOT / local).resolve()
            if not str(target).startswith(str(ROOT)) or not target.exists():
                self.fail(path, f"{field} must be an existing local path, anchor, or URL: {text}")

    def _check_review_snapshot(self, path, frontmatter, status):
        if status != "REVIEW":
            return
        tracking = frontmatter.get("tracking-issue")
        if self._is_tbd(tracking):
            return
        tracking_text = str(tracking)
        if tracking_text.startswith(("http://", "https://", "#")):
            return
        tracking_path = (ROOT / tracking_text.split("#", 1)[0]).resolve()
        if not tracking_path.exists():
            return
        snapshot = re.search(
            r"^\|\s*Proposal SHA-256\s*\|\s*`([0-9a-f]{64})`\s*\|\s*$",
            tracking_path.read_text(encoding="utf-8"),
            re.MULTILINE,
        )
        if snapshot is None:
            return
        expected = hashlib.sha256(path.read_bytes()).hexdigest()
        if snapshot.group(1) != expected:
            self.fail(
                tracking_path,
                f"REVIEW snapshot does not match {path.name} SHA-256 {expected}",
            )

    def _check_markdown_fences(self, path):
        if not path.exists():
            return
        lines = path.read_text(encoding="utf-8").splitlines()
        in_fence = False
        fence_lang = ""
        fence_start = 0
        block = []
        for index, line in enumerate(lines, start=1):
            match = re.match(r"^```([A-Za-z0-9_-]*)\s*$", line)
            if not match:
                if in_fence:
                    block.append(line)
                continue
            if not in_fence:
                in_fence = True
                fence_lang = match.group(1)
                fence_start = index
                block = []
                continue
            if self._looks_like_mermaid(block) and fence_lang != "mermaid":
                self.fail(path, f"diagram-like code fence at line {fence_start} must use ```mermaid")
            in_fence = False
            fence_lang = ""
            block = []
        if in_fence:
            self.fail(path, f"unclosed code fence at line {fence_start}")

    def _check_disallowed_governance_references(self):
        for base in (ROOT / "docs", ROOT / ".agents"):
            for path in base.rglob("*.md"):
                text = path.read_text(encoding="utf-8")
                for needle in DISALLOWED_GOVERNANCE_REFERENCES:
                    if needle in text:
                        self.fail(path, f"disallowed obsolete RFC location reference: {needle}")

    def _parse_frontmatter(self, path):
        text = path.read_text(encoding="utf-8")
        lines = text.splitlines()
        if not lines or lines[0] != "---":
            self.fail(path, "missing YAML frontmatter")
            return None, text
        try:
            end = lines.index("---", 1)
        except ValueError:
            self.fail(path, "unclosed YAML frontmatter")
            return None, text
        frontmatter = {}
        for line in lines[1:end]:
            if not line.strip():
                continue
            if ":" not in line:
                self.fail(path, f"unsupported frontmatter line: {line!r}")
                continue
            key, value = line.split(":", 1)
            frontmatter[key.strip()] = self._parse_value(value.strip())
        return frontmatter, "\n".join(lines[end + 1 :])

    def _parse_value(self, value):
        if value == "[]":
            return []
        if value.startswith("[") and value.endswith("]"):
            inner = value[1:-1].strip()
            if not inner:
                return []
            return [self._strip_quotes(part.strip()) for part in inner.split(",")]
        value = self._strip_quotes(value)
        if re.fullmatch(r"\d+", value):
            return int(value)
        return value

    def _strip_quotes(self, value):
        if len(value) >= 2 and value[0] == value[-1] and value[0] in {'"', "'"}:
            return value[1:-1]
        return value

    def _parse_repository_impact_owners(self, path, body):
        table = self._section_body(body, "Repository Impact")
        owners = set()
        for line in table.splitlines():
            if not line.startswith("|") or "---" in line or "Owner" in line:
                continue
            cells = [cell.strip() for cell in line.strip("|").split("|")]
            if len(cells) < 3:
                continue
            owner_cell = cells[-1]
            matches = re.findall(r"`([^`]+)`", owner_cell)
            if not matches and owner_cell:
                matches = [part.strip() for part in owner_cell.split(",")]
            for owner in matches:
                if owner:
                    owners.add(owner)
        if not owners:
            self.fail(path, "Repository Impact must list at least one owner")
        return owners

    def _section_body(self, body, section):
        pattern = rf"^## {re.escape(section)}\n(?P<body>.*?)(?=^## |\Z)"
        match = re.search(pattern, body, re.MULTILINE | re.DOTALL)
        if not match:
            return ""
        return match.group("body")

    def _looks_like_mermaid(self, block):
        joined = "\n".join(block).strip()
        return bool(
            re.search(
                r"^(flowchart|graph|stateDiagram|stateDiagram-v2|sequenceDiagram|classDiagram|erDiagram|journey|gantt|gitGraph|mindmap|timeline)\b",
                joined,
                re.MULTILINE,
            )
        )

    def _looks_like_person(self, value):
        return bool(re.fullmatch(r"[A-Za-z][A-Za-z0-9_.-]*", value))

    def _is_tbd(self, value):
        if value is None:
            return True
        if isinstance(value, list):
            return False
        return str(value).strip().upper() in {"", "TBD"}

    def _as_list(self, value):
        if value is None:
            return []
        if isinstance(value, list):
            return value
        return [str(value)]


if __name__ == "__main__":
    sys.exit(Checker().run())
