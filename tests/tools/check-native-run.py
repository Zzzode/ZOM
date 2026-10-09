#!/usr/bin/env python3
"""RFC 0043 O5/KR5.4: `zomc run` executes an entry-compatible native program.

Run only when the LLVM backend is built. A program whose entry folds to the
reserved no-argument i32 `zom.module_init` symbol links and executes end to
end, exiting with the returned value. A program whose object keeps a
parameterized symbol (the boolean-conditional diamond fixture) is object-only,
so `zomc run` fails closed on it.

One or more positive programs are named with repeated
``--run-case manifest::package::binary::exit-code`` entries; the script asserts
each one runs with its exact exit code. Object-only reject cases are named with
repeated ``--reject-case manifest::package::binary`` entries; the script asserts
each one fails closed.

Cases run concurrently in a thread pool (`--jobs` workers, defaulting to
`min(8, cpu count)`): every case owns a temporary directory and a copied
environment, and zomc's only shared write is the PID-keyed run root, so no case
shares state with another. All case failures are collected and raised together
as one aggregated error instead of aborting at the first failure.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import tempfile
from collections.abc import Callable
from concurrent.futures import ThreadPoolExecutor, as_completed
from functools import partial
from pathlib import Path

ANSI = re.compile(r"\x1b\[[0-9;]*m")
LINK_FAILED = "Native linking or publication failed."


def run(zomc: str, manifest: str, package: str, binary: str) -> tuple[int, str]:
    resolved_manifest = str(Path(manifest).resolve(strict=True))
    with tempfile.TemporaryDirectory(prefix="zom-native-run-") as temporary:
        result = subprocess.run(
            [
                zomc,
                "run",
                "--manifest-path",
                resolved_manifest,
                "--package",
                package,
                "--bin",
                binary,
            ],
            cwd=Path(temporary),
            env=dict(os.environ),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
        )
        return result.returncode, ANSI.sub("", result.stdout)


def parse_case(spec: str) -> tuple[str, str, str, int]:
    parts = spec.split("::")
    if len(parts) != 4:
        raise ValueError(f"run case must be manifest::package::binary::exit, got: {spec!r}")
    manifest, package, binary, exit_text = parts
    return manifest, package, binary, int(exit_text)


def check_run_case(
    zomc: str, manifest: str, package: str, binary: str, expected_exit: int
) -> None:
    """Assert one entry-compatible program runs with its exact exit code."""
    exit_code, output = run(zomc, manifest, package, binary)
    if LINK_FAILED in output:
        raise RuntimeError(f"native run reported a link failure:\nrc={exit_code}\n{output}")
    if exit_code != expected_exit:
        raise RuntimeError(
            f"native run exit mismatch for {package}: expected {expected_exit}, "
            f"got {exit_code}\n{output}"
        )


def check_reject_case(zomc: str, manifest: str, package: str, binary: str) -> None:
    """Assert one object-only program fails closed instead of executing."""
    exit_code, output = run(zomc, manifest, package, binary)
    if exit_code == 0 and LINK_FAILED not in output:
        raise RuntimeError(
            "object-only program unexpectedly ran instead of failing closed:"
            f"\nrc={exit_code}\n{output}"
        )


def _run_parallel(jobs: int, tasks: list[tuple[str, Callable[[], None]]]) -> None:
    """Run case callables in a thread pool, raising one error for all failures.

    subprocess.run waits release the GIL, so a thread pool overlaps case wall
    time without multiprocessing overhead. Every failure is collected so a
    single run reports every broken case instead of only the first.
    """
    failures: list[str] = []
    with ThreadPoolExecutor(max_workers=jobs) as executor:
        futures = {executor.submit(callable_): label for label, callable_ in tasks}
        for future in as_completed(futures):
            label = futures[future]
            try:
                future.result()
            except Exception as error:  # noqa: BLE001 - aggregate every case failure
                failures.append(f"{label}: {error}")
    if failures:
        raise RuntimeError(
            f"{len(failures)} of {len(tasks)} cases failed:\n" + "\n---\n".join(failures)
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--zomc", required=True)
    parser.add_argument(
        "--run-case",
        action="append",
        required=True,
        metavar="manifest::package::binary::exit",
        help="Entry-compatible program and its expected exit code; repeatable.",
    )
    parser.add_argument(
        "--reject-case",
        action="append",
        required=True,
        default=[],
        metavar="manifest::package::binary",
        help="Object-only program that zomc run must reject; repeatable.",
    )
    parser.add_argument(
        "--jobs",
        type=int,
        default=min(8, os.cpu_count() or 1),
        help="parallel case workers (default: min(8, cpu count))",
    )
    arguments = parser.parse_args()
    zomc = str(Path(arguments.zomc).resolve(strict=True))

    # Parse every spec before running anything so a malformed command line
    # fails fast without starting the pool.
    tasks: list[tuple[str, Callable[[], None]]] = []
    for spec in arguments.run_case:
        manifest, package, binary, expected_exit = parse_case(spec)
        tasks.append(
            (f"run {package}", partial(check_run_case, zomc, manifest, package, binary, expected_exit))
        )
    for spec in arguments.reject_case:
        parts = spec.split("::")
        if len(parts) != 3:
            raise ValueError(f"reject case must be manifest::package::binary, got: {spec!r}")
        manifest, package, binary = parts
        tasks.append((f"reject {package}", partial(check_reject_case, zomc, manifest, package, binary)))

    _run_parallel(arguments.jobs, tasks)
    print("zomc run executed every entry-compatible program and rejected every object-only program")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
