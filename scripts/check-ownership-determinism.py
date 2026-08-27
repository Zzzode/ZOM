#!/usr/bin/env python3
"""Enforce byte-identical ownership compiler output across repeated processes."""

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
BASELINE = ROOT / "tests/coverage/ownership-determinism-baseline.json"
BASELINE_SCHEMA = "zom.rfc0007.ownership-determinism-baseline"
DEFAULT_REPEATS = 3
FULL_OID = re.compile(r"[0-9a-f]{40}")

# Corpus sources that reach the ownership pipeline.  Each produces ownership
# diagnostics (ZOM4093/ZOM4094) from the initialization verifier, so the
# full session pipeline including ownership finalization is exercised.
INPUTS = (
    "05-statements/uninitialized_local_return_neg_01.zom",
    "05-statements/uninitialized_aggregate_field_return_neg_01.zom",
    "05-statements/uninitialized_aggregate_sibling_field_return_neg_01.zom",
)


class DeterminismError(Exception):
    """One rejected determinism invariant."""

    def __init__(self, category: str, message: str):
        super().__init__(message)
        self.category = category


def run_once(zomc: Path, source: Path) -> tuple[int, bytes]:
    command = [
        sys.executable,
        str(PACKAGE_RUNNER),
        "--zomc",
        str(zomc),
        "compile",
        "--check",
        str(source),
    ]
    completed = subprocess.run(command, cwd=ROOT, check=False, capture_output=True)
    output = completed.stdout + completed.stderr
    normalized = output.replace(str(zomc).encode(), b"<zomc>")
    return completed.returncode, normalized


def hash_output(exit_code: int, output: bytes) -> str:
    digest = hashlib.sha256()
    digest.update(str(exit_code).encode("ascii"))
    digest.update(b"\n")
    digest.update(output)
    return digest.hexdigest()


def collect_outputs(zomc: Path, repeats: int) -> dict[str, dict[str, object]]:
    results: dict[str, dict[str, object]] = {}
    for relative in INPUTS:
        source = CORPUS / relative
        if not source.is_file():
            raise DeterminismError("missing-input", f"corpus source does not exist: {relative}")
        runs = [run_once(zomc, source) for _ in range(repeats)]
        first_exit, first_output = runs[0]
        for index, (exit_code, output) in enumerate(runs[1:], start=1):
            if exit_code != first_exit or output != first_output:
                raise DeterminismError(
                    "non-deterministic-output",
                    f"{relative}: run {index} differs from run 0 "
                    f"(exit {first_exit} vs {exit_code})",
                )
        results[relative] = {
            "exitCode": first_exit,
            "sha256": hash_output(first_exit, first_output),
        }
    return results


def load_baseline() -> dict[str, object]:
    value = json.loads(BASELINE.read_text(encoding="utf-8"))
    if value.get("schema") != BASELINE_SCHEMA:
        raise ValueError("invalid ownership determinism baseline schema")
    inputs = value.get("inputs")
    outputs = value.get("outputs")
    repeats = value.get("repeats")
    if not isinstance(inputs, list) or not isinstance(outputs, dict):
        raise ValueError("ownership determinism baseline must contain inputs and outputs")
    if not isinstance(repeats, int) or repeats < 2:
        raise ValueError("ownership determinism baseline repeats must be an integer >= 2")
    if list(inputs) != list(INPUTS):
        raise ValueError("ownership determinism baseline inputs do not match the gate input set")
    for relative in INPUTS:
        entry = outputs.get(relative)
        if not isinstance(entry, dict):
            raise ValueError(f"ownership determinism baseline missing output: {relative}")
        exit_code = entry.get("exitCode")
        digest = entry.get("sha256")
        if not isinstance(exit_code, int) or not isinstance(digest, str):
            raise ValueError(f"ownership determinism baseline entry is invalid: {relative}")
        if re.fullmatch(r"[0-9a-f]{64}", digest) is None:
            raise ValueError(f"ownership determinism baseline digest is invalid: {relative}")
    for key in ("headRevision", "baseRevision"):
        revision = value.get(key)
        if not isinstance(revision, str) or FULL_OID.fullmatch(revision) is None:
            raise ValueError(f"ownership determinism baseline {key} is invalid")
    return value


def compare_baseline(current: dict[str, dict[str, object]], baseline: dict[str, object]) -> None:
    outputs = baseline["outputs"]
    assert isinstance(outputs, dict)
    for relative, entry in current.items():
        expected = outputs[relative]
        assert isinstance(expected, dict)
        if entry["exitCode"] != expected["exitCode"] or entry["sha256"] != expected["sha256"]:
            raise DeterminismError(
                "baseline-drift",
                f"{relative}: output differs from the recorded baseline "
                f"(expected exit {expected['exitCode']} {expected['sha256']}, "
                f"got exit {entry['exitCode']} {entry['sha256']})",
            )


def write_baseline(current: dict[str, dict[str, object]], repeats: int) -> None:
    head = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, check=True, capture_output=True, text=True
    ).stdout.strip()
    base = (ROOT / "tests/coverage/implementation-series-base.txt")
    base_revision = base.read_text(encoding="ascii").strip()
    payload = {
        "schema": BASELINE_SCHEMA,
        "inputs": list(INPUTS),
        "repeats": repeats,
        "outputs": current,
        "headRevision": head,
        "baseRevision": base_revision,
    }
    BASELINE.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def run_check(zomc: Path, repeats: int) -> int:
    try:
        baseline = load_baseline()
        current = collect_outputs(zomc, repeats)
        compare_baseline(current, baseline)
    except (DeterminismError, ValueError, OSError) as error:
        category = getattr(error, "category", "baseline-error")
        print(f"Ownership determinism check failed [{category}]: {error}", file=sys.stderr)
        return 1
    print(f"Ownership determinism check passed ({len(INPUTS)} inputs x {repeats} repeats)")
    return 0


def run_record(zomc: Path, repeats: int) -> int:
    try:
        current = collect_outputs(zomc, repeats)
    except DeterminismError as error:
        print(f"Ownership determinism record failed [{error.category}]: {error}", file=sys.stderr)
        return 1
    write_baseline(current, repeats)
    print(f"Ownership determinism baseline recorded: {BASELINE}")
    return 0


def write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def make_fake_zomc(directory: Path, name: str, body: str) -> Path:
    path = directory / name
    path.write_text(body, encoding="utf-8")
    path.chmod(0o755)
    return path


def run_self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="zom-determinism-") as directory:
        root = Path(directory)
        source = root / "input.zom"
        write(source, "fun entry() -> i32 { return 0; }\n")

        stable = make_fake_zomc(
            root,
            "stable-zomc",
            "#!/bin/sh\necho 'Error [ZOM4093]: deterministic failure' >&2\nexit 1\n",
        )
        runs = [run_once(stable, source) for _ in range(3)]
        if any(runs[0] != run for run in runs[1:]):
            print("self-test: stable fake compiler was not deterministic", file=sys.stderr)
            return 1
        current = {
            "input.zom": {
                "exitCode": runs[0][0],
                "sha256": hash_output(runs[0][0], runs[0][1]),
            }
        }
        baseline = {
            "schema": BASELINE_SCHEMA,
            "inputs": ["input.zom"],
            "repeats": 3,
            "outputs": current,
            "headRevision": "a" * 40,
            "baseRevision": "b" * 40,
        }
        compare_baseline(current, baseline)

        flaky = make_fake_zomc(
            root,
            "flaky-zomc",
            "#!/bin/sh\n"
            'COUNT_FILE="${TMPDIR:-/tmp}/zom-determinism-flaky-counter"\n'
            "COUNT=$(cat \"$COUNT_FILE\" 2>/dev/null || echo 0)\n"
            "COUNT=$((COUNT + 1))\n"
            "echo \"$COUNT\" > \"$COUNT_FILE\"\n"
            'if [ "$COUNT" -eq 1 ]; then\n'
            "  echo 'Error [ZOM4093]: first run' >&2\n"
            "else\n"
            "  echo 'Error [ZOM4093]: second run' >&2\n"
            "fi\n"
            "exit 1\n",
        )
        counter = Path(tempfile.gettempdir()) / "zom-determinism-flaky-counter"
        counter.unlink(missing_ok=True)
        try:
            collect_outputs_from(flaky, source, 3)
        except DeterminismError as error:
            if error.category != "non-deterministic-output":
                print(f"self-test: flaky compiler produced {error.category}", file=sys.stderr)
                return 1
        else:
            print("self-test: flaky compiler was not rejected", file=sys.stderr)
            return 1
        counter.unlink(missing_ok=True)

        drifted = dict(current)
        drifted["input.zom"] = {"exitCode": 1, "sha256": "0" * 64}
        try:
            compare_baseline(drifted, baseline)
        except DeterminismError as error:
            if error.category != "baseline-drift":
                print(f"self-test: drift produced {error.category}", file=sys.stderr)
                return 1
        else:
            print("self-test: baseline drift was not rejected", file=sys.stderr)
            return 1

    print("Ownership determinism self-test passed")
    return 0


def collect_outputs_from(zomc: Path, source: Path, repeats: int) -> dict[str, dict[str, object]]:
    runs = [run_once(zomc, source) for _ in range(repeats)]
    first_exit, first_output = runs[0]
    for index, (exit_code, output) in enumerate(runs[1:], start=1):
        if exit_code != first_exit or output != first_output:
            raise DeterminismError(
                "non-deterministic-output",
                f"run {index} differs from run 0 (exit {first_exit} vs {exit_code})",
            )
    return {"input.zom": {"exitCode": first_exit, "sha256": hash_output(first_exit, first_output)}}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--record", action="store_true")
    mode.add_argument("--self-test", action="store_true")
    parser.add_argument("--zomc", type=Path)
    parser.add_argument("--repeats", type=int, default=DEFAULT_REPEATS)
    arguments = parser.parse_args()
    if arguments.self_test:
        if arguments.zomc is not None:
            parser.error("--self-test does not accept --zomc")
        return run_self_test()
    if arguments.zomc is None:
        parser.error("--check and --record require --zomc")
    if arguments.repeats < 2:
        parser.error("--repeats must be at least 2")
    if arguments.check:
        return run_check(arguments.zomc, arguments.repeats)
    return run_record(arguments.zomc, arguments.repeats)


if __name__ == "__main__":
    raise SystemExit(main())
