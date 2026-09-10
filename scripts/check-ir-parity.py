#!/usr/bin/env python3
"""Record and diff per-file compiler behavior over the conformance corpus.

RFC 0048 Phase 0. The recursive-IR refactor must preserve, at every phase, the
exact accept/reject decision and rendered diagnostic for every corpus source.
This tool provides the *process channel* of that guarantee: it runs each corpus
`.zom` through `zomc compile --check` with one build (``--record``) and diffs a
second build (``--check``) on exit code and normalized stdout+stderr.

A separate *IR channel* (canonical ``--dump-hir``/``--dump-mir`` output for
accepted programs) is layered on once the dump surface exists; this process
channel alone covers all diagnostics and accept/reject decisions.

Normalization mirrors ``scripts/check-ownership-determinism.py``: the compiler
binary path is replaced by ``<zomc>``. Internal-compiler-error incident
fingerprints are hashes that vary by run, so they are masked when present.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
CORPUS = ROOT / "tests/conformance/corpus"
PACKAGE_RUNNER = ROOT / "tests/tools/run-zomc-package.py"
SNAPSHOT_SCHEMA = "zom.rfc0048.corpus-process-parity"

# An internal compiler error renders a random incident fingerprint. It should
# never occur for a corpus source; when it does, parity must still detect the
# ICE presence/phase without tripping on the random hash.
HEX_RUN = re.compile(r"[0-9a-f]{40,64}")
INCIDENT_LINE = re.compile(r"incident fingerprint|incident:")


class ParityError(Exception):
    """One parity invariant failed."""

    def __init__(self, category: str, message: str):
        super().__init__(message)
        self.category = category


def corpus_sources(corpus: Path) -> list[str]:
    return sorted(
        str(path.relative_to(corpus))
        for path in corpus.rglob("*.zom")
        if path.is_file()
    )


def normalize(zomc: Path, output: bytes) -> bytes:
    normalized = output.replace(str(zomc).encode(), b"<zomc>")
    text = normalized.decode("utf-8", errors="replace")
    lines = []
    for line in text.splitlines():
        if INCIDENT_LINE.search(line.lower()):
            line = HEX_RUN.sub("<hash>", line)
        lines.append(line)
    return "\n".join(lines).encode("utf-8")


def run_once(zomc: Path, source: Path, emit: str = "check") -> tuple[int, bytes]:
    command = [
        sys.executable,
        str(PACKAGE_RUNNER),
        "--zomc",
        str(zomc),
        "compile",
        f"--{emit}",
        str(source),
    ]
    completed = subprocess.run(command, cwd=ROOT, check=False, capture_output=True)
    return completed.returncode, normalize(zomc, completed.stdout + completed.stderr)


def hash_output(exit_code: int, output: bytes) -> str:
    digest = hashlib.sha256()
    digest.update(str(exit_code).encode("ascii"))
    digest.update(b"\n")
    digest.update(output)
    return digest.hexdigest()


def collect(zomc: Path, corpus: Path, sources: list[str], ir_channel: bool) -> dict[str, dict[str, int | str]]:
    results: dict[str, dict[str, int | str]] = {}
    for relative in sources:
        source = corpus / relative
        if not source.is_file():
            raise ParityError("missing-input", f"corpus source does not exist: {relative}")
        exit_code, output = run_once(zomc, source)
        entry: dict[str, int | str] = {
            "exitCode": exit_code,
            "sha256": hash_output(exit_code, output),
        }
        # IR channel: for sources that check clean, capture deterministic HIR and
        # canonical MIR dumps so accepted-program IR byte drift is visible.
        if ir_channel and exit_code == 0:
            hir_exit, hir_output = run_once(zomc, source, "dump-hir")
            mir_exit, mir_output = run_once(zomc, source, "dump-mir")
            if hir_exit == 0:
                entry["hirSha256"] = hash_output(hir_exit, hir_output)
            if mir_exit == 0:
                entry["mirSha256"] = hash_output(mir_exit, mir_output)
        results[relative] = entry
    return results


def write_file(path: Path, content: str, executable: bool = False) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    if executable:
        path.chmod(0o755)
    return path


def make_fake(directory: Path, name: str, body: str) -> Path:
    return write_file(directory / name, body, executable=True)


def run_record(zomc: Path, corpus: Path, snapshot: Path, ir_channel: bool) -> int:
    sources = corpus_sources(corpus)
    try:
        results = collect(zomc, corpus, sources, ir_channel)
    except ParityError as error:
        print(f"corpus parity record failed [{error.category}]: {error}", file=sys.stderr)
        return 1
    payload = {
        "schema": SNAPSHOT_SCHEMA,
        "corpusRoot": str(corpus.relative_to(ROOT)) if corpus.is_relative_to(ROOT) else str(corpus),
        "irChannel": ir_channel,
        "count": len(results),
        "outputs": results,
    }
    ir_note = " with IR channel" if ir_channel else ""
    snapshot.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(f"corpus parity snapshot recorded{ir_note}: {snapshot} ({len(results)} sources)")
    return 0


def diff_snapshots(current: dict[str, dict[str, int | str]],
                  recorded: dict[str, dict[str, int | str]], limit: int) -> list[str]:
    differences: list[str] = []
    old = recorded.keys()
    new = current.keys()
    for relative in sorted(old - new):
        differences.append(f"{relative}: removed in new build")
    for relative in sorted(new - old):
        differences.append(f"{relative}: added in new build")
    for relative in sorted(old & new):
        before = recorded[relative]
        after = current[relative]
        if before["exitCode"] != after["exitCode"]:
            differences.append(
                f"{relative}: exit {before['exitCode']} -> {after['exitCode']}"
            )
            continue
        if before["sha256"] != after["sha256"]:
            differences.append(f"{relative}: output changed (same exit {after['exitCode']})")
        for channel in ("hirSha256", "mirSha256"):
            if before.get(channel) != after.get(channel):
                differences.append(f"{relative}: {channel} changed")
    return differences[:limit]


def run_check(zomc: Path, corpus: Path, snapshot: Path, limit: int, ir_channel: bool) -> int:
    if not snapshot.is_file():
        print(
            f"corpus parity check failed [no-snapshot]: {snapshot} (record it first)",
            file=sys.stderr,
        )
        return 1
    try:
        value = json.loads(snapshot.read_text(encoding="utf-8"))
        if value.get("schema") != SNAPSHOT_SCHEMA:
            raise ValueError("invalid corpus parity snapshot schema")
        recorded = value.get("outputs")
        if not isinstance(recorded, dict):
            raise ValueError("snapshot missing outputs object")
        if bool(value.get("irChannel")) != ir_channel:
            raise ValueError(
                f"snapshot irChannel={value.get('irChannel')} does not match --ir={ir_channel}"
            )
        sources = corpus_sources(corpus)
        current = collect(zomc, corpus, sources, ir_channel)
        differences = diff_snapshots(current, recorded, limit)
    except (ParityError, ValueError, OSError) as error:
        category = getattr(error, "category", "snapshot-error")
        print(f"corpus parity check failed [{category}]: {error}", file=sys.stderr)
        return 1
    if differences:
        print(
            f"corpus parity check failed: {len(differences)} shown difference(s) "
            f"between builds:",
            file=sys.stderr,
        )
        for line in differences:
            print(f"  - {line}", file=sys.stderr)
        return 1
    print(f"corpus parity check passed ({len(current)} sources byte-parallel)")
    return 0


def run_self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="zom-parity-") as directory:
        root = Path(directory)
        corpus = root / "corpus"
        sub = corpus / "pkg"
        sub.mkdir(parents=True)
        source = write_file(sub / "a.zom", "fun entry() -> i32 { return 0; }\n")

        compiler_a = make_fake(
            root,
            "zomc-a",
            "#!/bin/sh\n"
            'echo "Error [ZOM4099]: nope" >&2\n'
            "exit 1\n",
        )
        compiler_b = make_fake(
            root,
            "zomc-b",
            "#!/bin/sh\n"
            'echo "Error [ZOM4099]: nope" >&2\n'
            "exit 1\n",
        )
        compiler_c = make_fake(
            root,
            "zomc-c",
            "#!/bin/sh\n"
            'echo "Error [ZOM4029]: different" >&2\n'
            "exit 1\n",
        )

        snapshot = root / "parity.json"
        # The parity runner shells out through the real package wrapper, which
        # needs the actual compiler; the self-test therefore exercises the
        # snapshot diff logic directly rather than collect().
        recorded = {
            "pkg/a.zom": {
                "exitCode": 1,
                "sha256": hash_output(1, b"Error [ZOM4099]: nope\n"),
            }
        }
        same = dict(recorded)
        if diff_snapshots(same, recorded, 10):
            print("self-test: identical snapshots reported a difference", file=sys.stderr)
            return 1
        changed = {
            "pkg/a.zom": {
                "exitCode": 1,
                "sha256": hash_output(1, b"Error [ZOM4029]: different\n"),
            }
        }
        diffs = diff_snapshots(changed, recorded, 10)
        if len(diffs) != 1 or "output changed" not in diffs[0]:
            print("self-test: output change not detected", file=sys.stderr)
            return 1
        flipped = {"pkg/a.zom": {"exitCode": 0, "sha256": hash_output(0, b"")}}
        diffs = diff_snapshots(flipped, recorded, 10)
        if not any("exit 1 -> 0" in line for line in diffs):
            print("self-test: exit-code flip not detected", file=sys.stderr)
            return 1
        added = dict(recorded)
        added["pkg/b.zom"] = {"exitCode": 0, "sha256": hash_output(0, b"")}
        diffs = diff_snapshots(added, recorded, 10)
        if not any("added in new build" in line for line in diffs):
            print("self-test: added source not detected", file=sys.stderr)
            return 1
        # IR channel: same process result but a changed MIR dump must be detected.
        ir_before = {
            "pkg/c.zom": {
                "exitCode": 0,
                "sha256": hash_output(0, b""),
                "hirSha256": "aa",
                "mirSha256": "bb",
            }
        }
        ir_after = {
            "pkg/c.zom": {
                "exitCode": 0,
                "sha256": hash_output(0, b""),
                "hirSha256": "aa",
                "mirSha256": "cc",
            }
        }
        diffs = diff_snapshots(ir_after, ir_before, 10)
        if diffs != ["pkg/c.zom: mirSha256 changed"]:
            print(f"self-test: IR channel drift not detected, got {diffs}", file=sys.stderr)
            return 1
        # The fake compilers are only asserted to exist and be executable.
        for binary in (compiler_a, compiler_b, compiler_c, source):
            if not binary.exists():
                print(f"self-test: missing fixture {binary}", file=sys.stderr)
                return 1
    print("corpus parity self-test passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--record", action="store_true")
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--self-test", action="store_true")
    parser.add_argument("--zomc", type=Path)
    parser.add_argument("--snapshot", type=Path, default=ROOT / "tests/coverage/corpus-process-parity.json")
    parser.add_argument("--corpus", type=Path, default=CORPUS)
    parser.add_argument("--limit", type=int, default=20, help="differences to print")
    parser.add_argument(
        "--ir",
        action="store_true",
        help="also capture/diff deterministic --dump-hir/--dump-mir output for clean sources",
    )
    arguments = parser.parse_args()

    if arguments.self_test:
        return run_self_test()
    if arguments.zomc is None:
        parser.error("--record and --check require --zomc")
    if arguments.record:
        return run_record(arguments.zomc, arguments.corpus, arguments.snapshot, arguments.ir)
    return run_check(
        arguments.zomc, arguments.corpus, arguments.snapshot, arguments.limit, arguments.ir
    )


if __name__ == "__main__":
    raise SystemExit(main())
