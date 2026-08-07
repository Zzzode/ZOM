#!/usr/bin/env python3
"""Record and compare the RFC 0017 incremental-query performance corpus."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any


EXPECTED_PROTOCOL = {
    "warmup_runs": 5,
    "measured_runs": 21,
    "maximum_elapsed_mad_ratio": 0.03,
    "maximum_elapsed_ratio": 1.05,
    "maximum_peak_rss_ratio": 1.15,
}


class BenchmarkError(RuntimeError):
    pass


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--worker-count", type=int, required=True)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--record-baseline", action="store_true")
    mode.add_argument("--compare", action="store_true")
    return parser.parse_args()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def command_output(command: list[str], cwd: Path | None = None) -> str:
    completed = subprocess.run(
        command,
        cwd=cwd,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if completed.returncode != 0:
        raise BenchmarkError(
            f"command failed with exit code {completed.returncode}: {' '.join(command)}\n"
            f"{completed.stdout}"
        )
    return completed.stdout.strip()


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise BenchmarkError(f"cannot read JSON from {path}: {error}") from error
    if not isinstance(value, dict):
        raise BenchmarkError(f"{path}: top-level JSON value must be an object")
    return value


def resolve_argument_path(path: Path, repository: Path) -> Path:
    if path.is_absolute():
        return path.resolve()
    repository_candidate = (repository / path).resolve()
    if repository_candidate.exists():
        return repository_candidate
    return path.resolve()


def require_within(path: Path, root: Path, description: str) -> Path:
    resolved = path.resolve()
    try:
        resolved.relative_to(root.resolve())
    except ValueError as error:
        raise BenchmarkError(f"{description} escapes {root}: {path}") from error
    return resolved


def load_corpus(path: Path) -> dict[str, Any]:
    corpus = load_json(path)
    if corpus.get("protocol") != EXPECTED_PROTOCOL:
        raise BenchmarkError(f"{path}: protocol must exactly match RFC 0017")
    cases = corpus.get("cases")
    if not isinstance(cases, list) or not cases:
        raise BenchmarkError(f"{path}: cases must be a non-empty array")
    identifiers: set[str] = set()
    required_coverage = {
        "definition-heavy-clean-compilation",
        "equal-body-only-edit",
        "definition-add-remove-rename",
        "unrelated-module-edit",
    }
    observed_coverage: set[str] = set()
    for case in cases:
        if not isinstance(case, dict):
            raise BenchmarkError(f"{path}: every case must be an object")
        identifier = case.get("id")
        if not isinstance(identifier, str) or not identifier or identifier in identifiers:
            raise BenchmarkError(f"{path}: case ids must be unique non-empty strings")
        identifiers.add(identifier)
        if not isinstance(case.get("executable"), str):
            raise BenchmarkError(f"{path}: {identifier} requires executable")
        arguments = case.get("arguments", [])
        if not isinstance(arguments, list) or not all(isinstance(item, str) for item in arguments):
            raise BenchmarkError(f"{path}: {identifier} arguments must be strings")
        coverage = case.get("coverage", [])
        if not isinstance(coverage, list) or not all(isinstance(item, str) for item in coverage):
            raise BenchmarkError(f"{path}: {identifier} coverage must be strings")
        observed_coverage.update(coverage)
        sources = case.get("source_files", [])
        if not isinstance(sources, list) or not sources or not all(
            isinstance(item, str) for item in sources
        ):
            raise BenchmarkError(f"{path}: {identifier} source_files must be non-empty strings")
        cold_invocations = case.get("cold_invocations_per_sample")
        if not isinstance(cold_invocations, int) or cold_invocations < 1:
            raise BenchmarkError(
                f"{path}: {identifier} cold_invocations_per_sample must be positive"
            )
        if case.get("elapsed_measurement") not in {"process-monotonic", "ztest-monotonic-sum"}:
            raise BenchmarkError(
                f"{path}: {identifier} has an unsupported elapsed_measurement"
            )
    missing_coverage = sorted(required_coverage - observed_coverage)
    if missing_coverage:
        raise BenchmarkError(f"{path}: missing required coverage: {', '.join(missing_coverage)}")
    return corpus


def repository_revision(repository: Path) -> str:
    return command_output(["git", "rev-parse", "HEAD"], cwd=repository)


def require_clean_repository(repository: Path) -> None:
    status = command_output(["git", "status", "--porcelain=v1"], cwd=repository)
    if status:
        raise BenchmarkError(f"baseline repository is not clean: {repository}\n{status}")


def parse_cmake_cache(path: Path) -> dict[str, str]:
    if not path.is_file():
        raise BenchmarkError(f"missing CMake cache: {path}")
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8", errors="strict").splitlines():
        if not line or line.startswith(("#", "//")) or "=" not in line or ":" not in line:
            continue
        key_and_type, value = line.split("=", 1)
        key, _ = key_and_type.split(":", 1)
        values[key] = value
    required = ("CMAKE_BUILD_TYPE", "CMAKE_CXX_COMPILER")
    for key in required:
        if key not in values:
            raise BenchmarkError(f"{path}: missing {key}")
    if values["CMAKE_BUILD_TYPE"] != "Release":
        raise BenchmarkError(f"{path}: benchmark build must use the release preset")
    selected_prefixes = ("CMAKE_CXX_FLAGS", "ZOM_ENABLE_")
    selected_keys = sorted(
        key
        for key in values
        if key in required
        or key in {"CMAKE_CXX_COMPILER_ID", "CMAKE_CXX_COMPILER_VERSION"}
        or key.startswith(selected_prefixes)
    )
    return {key: values[key] for key in selected_keys}


def compiler_identity(cache: dict[str, str]) -> dict[str, Any]:
    configured = Path(cache["CMAKE_CXX_COMPILER"])
    resolved = str(configured) if configured.is_absolute() else shutil.which(str(configured))
    if resolved is None:
        raise BenchmarkError(
            f"configured C++ compiler is not available: {cache['CMAKE_CXX_COMPILER']}"
        )
    compiler = Path(resolved).resolve()
    if not compiler.is_file():
        raise BenchmarkError(f"configured C++ compiler does not exist: {compiler}")
    version = command_output([str(compiler), "--version"]).splitlines()
    return {
        "path": str(compiler),
        "sha256": sha256_file(compiler),
        "version": version[0] if version else "",
    }


def sysctl_value(name: str) -> str:
    return command_output(["sysctl", "-n", name])


def machine_identity() -> dict[str, Any]:
    if sys.platform == "darwin":
        cpu_model = sysctl_value("machdep.cpu.brand_string")
        physical_memory = int(sysctl_value("hw.memsize"))
    elif sys.platform.startswith("linux"):
        cpu_model = ""
        for line in Path("/proc/cpuinfo").read_text(encoding="utf-8").splitlines():
            if line.lower().startswith("model name"):
                cpu_model = line.split(":", 1)[1].strip()
                break
        physical_memory = 0
        for line in Path("/proc/meminfo").read_text(encoding="utf-8").splitlines():
            if line.startswith("MemTotal:"):
                physical_memory = int(line.split()[1]) * 1024
                break
    else:
        raise BenchmarkError(f"unsupported benchmark operating system: {sys.platform}")
    logical_cores = os.cpu_count()
    if logical_cores is None or logical_cores < 1 or physical_memory < 1 or not cpu_model:
        raise BenchmarkError("cannot determine complete machine identity")
    return {
        "architecture": platform.machine(),
        "cpu_model": cpu_model,
        "logical_cores": logical_cores,
        "operating_system": platform.system(),
        "operating_system_release": platform.release(),
        "physical_memory_bytes": physical_memory,
    }


def corpus_identity(
    corpus_path: Path, corpus: dict[str, Any], repository: Path
) -> dict[str, Any]:
    files: dict[str, str] = {}
    for case in corpus["cases"]:
        for relative_source in case["source_files"]:
            source = require_within(repository / relative_source, repository, "corpus source")
            if not source.is_file():
                raise BenchmarkError(f"missing corpus source: {source}")
            files[relative_source] = sha256_file(source)
    manifest_digest = sha256_file(corpus_path)
    combined = json.dumps(
        {"manifest_sha256": manifest_digest, "source_sha256": files},
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return {
        "manifest_sha256": manifest_digest,
        "source_sha256": dict(sorted(files.items())),
        "combined_sha256": sha256_bytes(combined),
    }


def parse_peak_rss(path: Path) -> int:
    text = path.read_text(encoding="utf-8", errors="strict")
    if sys.platform == "darwin":
        for line in text.splitlines():
            if "maximum resident set size" in line:
                return int(line.split()[0])
    elif sys.platform.startswith("linux"):
        for line in text.splitlines():
            if line.strip().startswith("Maximum resident set size (kbytes):"):
                return int(line.rsplit(":", 1)[1].strip()) * 1024
    raise BenchmarkError(f"cannot parse peak RSS output:\n{text}")


def linux_process_peak_rss(process_id: int) -> int | None:
    try:
        status = Path(f"/proc/{process_id}/status").read_text(encoding="utf-8", errors="strict")
    except OSError:
        return None
    for line in status.splitlines():
        if not line.startswith("VmHWM:"):
            continue
        fields = line.split()
        if len(fields) != 3 or fields[2] != "kB":
            return None
        return int(fields[1]) * 1024
    return None


def run_linux_without_time(
    command: list[str], repository: Path, environment: dict[str, str]
) -> tuple[int, str, int]:
    with tempfile.TemporaryFile(prefix="zom-incremental-output-") as output:
        process = subprocess.Popen(
            command,
            cwd=repository,
            env=environment,
            stdout=output,
            stderr=subprocess.STDOUT,
        )
        peak_rss = 0
        while process.poll() is None:
            observed_rss = linux_process_peak_rss(process.pid)
            if observed_rss is not None:
                peak_rss = max(peak_rss, observed_rss)
            time.sleep(0.001)
        observed_rss = linux_process_peak_rss(process.pid)
        if observed_rss is not None:
            peak_rss = max(peak_rss, observed_rss)
        output.seek(0)
        command_output = output.read().decode("utf-8", errors="strict")
    if peak_rss == 0:
        raise BenchmarkError("cannot observe Linux peak RSS without an external time command")
    return process.returncode, command_output, peak_rss


def parse_ztest_elapsed(output: str) -> int:
    unit_nanoseconds = {"ns": 1, "μs": 1_000, "ms": 1_000_000, "s": 1_000_000_000}
    elapsed = 0.0
    matches = 0
    for line in output.splitlines():
        if not line.startswith("[ PASS ]"):
            continue
        match = re.search(r"\(([0-9]+(?:\.[0-9]+)?)(ns|μs|ms|s)\)$", line)
        if match is None:
            raise BenchmarkError(f"cannot parse ztest monotonic duration: {line}")
        elapsed += float(match.group(1)) * unit_nanoseconds[match.group(2)]
        matches += 1
    if matches == 0 or elapsed <= 0:
        raise BenchmarkError("ztest output did not contain passing monotonic durations")
    return int(elapsed)


def run_once(
    command: list[str], repository: Path, worker_count: int, elapsed_measurement: str
) -> tuple[int, int]:
    environment = os.environ.copy()
    environment["ZOM_INCREMENTAL_QUERY_WORKER_COUNT"] = str(worker_count)
    started = time.monotonic_ns()
    if sys.platform == "darwin":
        time_command = "/usr/bin/time"
    elif sys.platform.startswith("linux"):
        time_command = shutil.which("gtime") or shutil.which("time")
    else:
        raise BenchmarkError(f"unsupported benchmark operating system: {sys.platform}")
    if time_command is None:
        return_code, output, peak_rss = run_linux_without_time(command, repository, environment)
    else:
        with tempfile.NamedTemporaryFile(prefix="zom-incremental-rss-") as measurement:
            time_flag = "-l" if sys.platform == "darwin" else "-v"
            completed = subprocess.run(
                [time_command, time_flag, "-o", measurement.name, *command],
                cwd=repository,
                env=environment,
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
            return_code = completed.returncode
            output = completed.stdout
            peak_rss = parse_peak_rss(Path(measurement.name))
    process_elapsed = time.monotonic_ns() - started
    if return_code != 0:
        raise BenchmarkError(
            f"benchmark command failed with exit code {return_code}: {' '.join(command)}\n{output}"
        )
    if elapsed_measurement == "ztest-monotonic-sum":
        elapsed = parse_ztest_elapsed(output)
    else:
        elapsed = process_elapsed
    return elapsed, peak_rss


def median_absolute_deviation(samples: list[int], median: float) -> float:
    return statistics.median(abs(sample - median) for sample in samples)


def benchmark_case(
    case: dict[str, Any], repository: Path, build_dir: Path, worker_count: int
) -> dict[str, Any]:
    executable = require_within(build_dir / case["executable"], build_dir, "case executable")
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise BenchmarkError(f"benchmark executable is missing or not executable: {executable}")
    command = [str(executable), *case.get("arguments", [])]
    cold_invocations = case["cold_invocations_per_sample"]
    elapsed_measurement = case["elapsed_measurement"]

    def run_sample() -> tuple[int, int]:
        elapsed_total = 0
        peak_rss_maximum = 0
        for _ in range(cold_invocations):
            elapsed, peak_rss = run_once(
                command, repository, worker_count, elapsed_measurement
            )
            elapsed_total += elapsed
            peak_rss_maximum = max(peak_rss_maximum, peak_rss)
        return elapsed_total, peak_rss_maximum

    print(f"benchmark: {case['id']}", flush=True)
    for _ in range(EXPECTED_PROTOCOL["warmup_runs"]):
        run_sample()
    elapsed_samples: list[int] = []
    rss_samples: list[int] = []
    for _ in range(EXPECTED_PROTOCOL["measured_runs"]):
        elapsed, peak_rss = run_sample()
        elapsed_samples.append(elapsed)
        rss_samples.append(peak_rss)
    elapsed_median = statistics.median(elapsed_samples)
    elapsed_mad = median_absolute_deviation(elapsed_samples, elapsed_median)
    elapsed_mad_ratio = elapsed_mad / elapsed_median if elapsed_median else 0.0
    if elapsed_mad_ratio > EXPECTED_PROTOCOL["maximum_elapsed_mad_ratio"]:
        elapsed_milliseconds = ", ".join(
            f"{sample / 1_000_000:.3f}" for sample in sorted(elapsed_samples)
        )
        raise BenchmarkError(
            f"{case['id']}: elapsed MAD ratio {elapsed_mad_ratio:.6f} exceeds "
            f"{EXPECTED_PROTOCOL['maximum_elapsed_mad_ratio']:.6f}; "
            f"samples in milliseconds: {elapsed_milliseconds}; rerun in an idle environment"
        )
    result = {
        "elapsed_mad_nanoseconds": int(elapsed_mad),
        "elapsed_mad_ratio": elapsed_mad_ratio,
        "elapsed_median_nanoseconds": int(elapsed_median),
        "elapsed_samples_nanoseconds": elapsed_samples,
        "cold_invocations_per_sample": cold_invocations,
        "peak_rss_median_bytes": int(statistics.median(rss_samples)),
        "peak_rss_samples_bytes": rss_samples,
    }
    print(
        f"  elapsed={result['elapsed_median_nanoseconds'] / 1_000_000:.3f} ms "
        f"mad={elapsed_mad_ratio * 100:.2f}% "
        f"rss={result['peak_rss_median_bytes'] / (1024 * 1024):.2f} MiB",
        flush=True,
    )
    return result


def aggregate(results: dict[str, dict[str, Any]]) -> dict[str, int]:
    return {
        "elapsed_median_nanoseconds": sum(
            result["elapsed_median_nanoseconds"] for result in results.values()
        ),
        "peak_rss_median_bytes": sum(
            result["peak_rss_median_bytes"] for result in results.values()
        ),
    }


def comparable_metadata(metadata: dict[str, Any]) -> dict[str, Any]:
    return {
        "build": metadata["build"],
        "compiler": metadata["compiler"],
        "corpus": metadata["corpus"],
        "machine": metadata["machine"],
        "worker_count": metadata["worker_count"],
    }


def compare_results(current: dict[str, Any], baseline: dict[str, Any]) -> None:
    if baseline.get("protocol") != EXPECTED_PROTOCOL:
        raise BenchmarkError("baseline protocol does not match RFC 0017")
    if comparable_metadata(current["metadata"]) != comparable_metadata(baseline["metadata"]):
        raise BenchmarkError("baseline machine, build, worker-count, or corpus metadata mismatch")
    baseline_results = baseline.get("results")
    if not isinstance(baseline_results, dict) or set(baseline_results) != set(current["results"]):
        raise BenchmarkError("baseline case set does not match the current corpus")
    elapsed_ratio = (
        current["aggregate"]["elapsed_median_nanoseconds"]
        / baseline["aggregate"]["elapsed_median_nanoseconds"]
    )
    rss_ratio = (
        current["aggregate"]["peak_rss_median_bytes"]
        / baseline["aggregate"]["peak_rss_median_bytes"]
    )
    print(f"aggregate elapsed ratio: {elapsed_ratio:.6f}")
    print(f"aggregate peak RSS ratio: {rss_ratio:.6f}")
    failures: list[str] = []
    if elapsed_ratio > EXPECTED_PROTOCOL["maximum_elapsed_ratio"]:
        failures.append(
            f"elapsed ratio {elapsed_ratio:.6f} exceeds "
            f"{EXPECTED_PROTOCOL['maximum_elapsed_ratio']:.6f}"
        )
    if rss_ratio > EXPECTED_PROTOCOL["maximum_peak_rss_ratio"]:
        failures.append(
            f"peak RSS ratio {rss_ratio:.6f} exceeds "
            f"{EXPECTED_PROTOCOL['maximum_peak_rss_ratio']:.6f}"
        )
    if failures:
        raise BenchmarkError("; ".join(failures))


def main() -> int:
    arguments = parse_arguments()
    if arguments.worker_count < 1:
        raise BenchmarkError("worker count must be positive")
    repository = arguments.repository.resolve()
    build_dir = resolve_argument_path(arguments.build_dir, repository)
    corpus_path = resolve_argument_path(arguments.corpus, repository)
    baseline_path = arguments.baseline.resolve()
    if not repository.is_dir():
        raise BenchmarkError(f"repository does not exist: {repository}")
    if not build_dir.is_dir():
        raise BenchmarkError(f"build directory does not exist: {build_dir}")
    if arguments.record_baseline:
        require_clean_repository(repository)
    corpus = load_corpus(corpus_path)
    cache = parse_cmake_cache(build_dir / "CMakeCache.txt")
    metadata = {
        "build": {"cmake_cache": cache, "preset": "release"},
        "compiler": compiler_identity(cache),
        "corpus": corpus_identity(corpus_path, corpus, repository),
        "machine": machine_identity(),
        "repository_revision": repository_revision(repository),
        "worker_count": arguments.worker_count,
    }
    results = {
        case["id"]: benchmark_case(case, repository, build_dir, arguments.worker_count)
        for case in corpus["cases"]
    }
    report = {
        "protocol": EXPECTED_PROTOCOL,
        "metadata": metadata,
        "results": results,
        "aggregate": aggregate(results),
    }
    if arguments.record_baseline:
        baseline_path.parent.mkdir(parents=True, exist_ok=True)
        baseline_path.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        print(f"recorded baseline: {baseline_path}")
    else:
        baseline = load_json(baseline_path)
        compare_results(report, baseline)
        print("incremental-query performance comparison passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except BenchmarkError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
