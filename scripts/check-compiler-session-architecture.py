#!/usr/bin/env python3

import argparse
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
COMPILER_ROOT = ROOT / "products" / "zomlang" / "compiler"
UTILS_ROOT = ROOT / "products" / "zomlang" / "utils"
SESSION_HEADER = Path("products/zomlang/compiler/driver/compiler-session.h")
SESSION_SOURCE = Path("products/zomlang/compiler/driver/compiler-session.cc")
DRIVER_CMAKE = Path("products/zomlang/compiler/driver/CMakeLists.txt")
CLI_SOURCE = Path("products/zomlang/utils/zomc/zomc.cc")
THREAD_POOL_HEADER = Path("products/zomlang/compiler/basic/thread-pool.h")
THREAD_POOL_SOURCE = Path("products/zomlang/compiler/basic/thread-pool.cc")
BRAND_SOURCE = Path("products/zomlang/compiler/identity/brand.cc")
BRAND_HEADER = Path("products/zomlang/compiler/identity/brand.h")
REGISTRY_SET_SOURCE = Path(
    "products/zomlang/compiler/identity/semantic-identity-registry-set.cc"
)
VERIFIED_VENDOR_ROOT = Path("products/zomlang/compiler/driver/package/vendor")

EXPECTED_DRIVER_FILES = {
    DRIVER_CMAKE,
    SESSION_HEADER,
    SESSION_SOURCE,
}

SESSION_HEADER_MARKERS = (
    "class CompilerSession",
    "CompilerSession(identity::SemanticContextFactory& contextFactory,",
    "identity::SemanticContextBrand getSemanticContextBrand() const noexcept;",
    "zc::Maybe<const identity::SemanticIdentityRegistrySet&> getIdentityRegistries() const noexcept;",
    "zc::Maybe<binder::DefinitionInventory> getDefinitionInventory(",
)

SESSION_SOURCE_MARKERS = (
    "identity::SemanticContextBrand contextBrand;",
    "zc::Maybe<identity::SemanticIdentityRegistrySet> identityRegistries;",
    "zc::Own<basic::StringPool> stringPool;",
    "zc::Own<source::SourceManager> sourceManager;",
    "zc::Own<diagnostics::DiagnosticEngine> diagnosticEngine;",
    "zc::Own<symbol::SymbolTable> symbolTable;",
    "zc::MutexGuarded<zc::HashMap<source::BufferId, binder::DefinitionInventory>>",
    "binder::DefinitionInventory::collect(ast);",
    "diagnostics::DiagID::IdentityBrandExhausted",
    "diagnostics::DiagID::IdentityDuplicateSingletonStore",
    "basic::ThreadPool threadPool;",
    "threadPool.enqueue(",
)

CLI_MARKERS = (
    "identity::SemanticContextFactory contextFactory;",
    "zc::Own<driver::CompilerSession> session;",
    "zc::SpaceFor<driver::CompilerSession> sessionSpace;",
    "session = sessionSpace.construct(contextFactory, langOpts, compilerOpts);",
)


def relative(path: Path) -> Path:
    return path.relative_to(ROOT)


def production_sources() -> dict[Path, str]:
    result: dict[Path, str] = {}
    for base in (COMPILER_ROOT, UTILS_ROOT):
        for suffix in ("*.h", "*.cc"):
            for path in base.rglob(suffix):
                relative_path = relative(path)
                if VERIFIED_VENDOR_ROOT in relative_path.parents:
                    continue
                result[relative_path] = path.read_text(encoding="utf-8")
    result[DRIVER_CMAKE] = (ROOT / DRIVER_CMAKE).read_text(encoding="utf-8")
    return result


def strip_cpp_comments_and_literals(text: str) -> str:
    output: list[str] = []
    index = 0
    state = "code"
    quote = ""
    while index < len(text):
        current = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""

        if state == "code":
            if current == "/" and following == "/":
                output.extend("  ")
                index += 2
                state = "line-comment"
                continue
            if current == "/" and following == "*":
                output.extend("  ")
                index += 2
                state = "block-comment"
                continue
            if current in {'"', "'"}:
                output.append(" ")
                quote = current
                index += 1
                state = "literal"
                continue
            output.append(current)
            index += 1
            continue

        if state == "line-comment":
            if current == "\n":
                output.append("\n")
                state = "code"
            else:
                output.append(" ")
            index += 1
            continue

        if state == "block-comment":
            if current == "*" and following == "/":
                output.extend("  ")
                index += 2
                state = "code"
            else:
                output.append("\n" if current == "\n" else " ")
                index += 1
            continue

        if current == "\\" and following:
            output.extend("  ")
            index += 2
        elif current == quote:
            output.append(" ")
            index += 1
            state = "code"
        else:
            output.append("\n" if current == "\n" else " ")
            index += 1

    return "".join(output)


def check_driver_surface(files: dict[Path, str], errors: list[str]) -> None:
    driver_files = {
        path for path in files if path.parent == SESSION_HEADER.parent
    }
    unexpected = sorted(driver_files - EXPECTED_DRIVER_FILES)
    missing = sorted(EXPECTED_DRIVER_FILES - driver_files)
    for path in unexpected:
        errors.append(f"{path}: unexpected compiler driver surface")
    for path in missing:
        errors.append(f"{path}: required CompilerSession surface is missing")

    for path, raw_text in sorted(files.items()):
        if path.suffix not in {".h", ".cc"}:
            continue
        text = strip_cpp_comments_and_literals(raw_text)
        if re.search(r"\bCompilerDriver\b", text):
            errors.append(f"{path}: forbidden CompilerDriver identifier remains")
        if "zomlang/compiler/driver/driver.h" in raw_text:
            errors.append(f"{path}: forbidden driver.h include remains")
        if path.parent == SESSION_HEADER.parent and re.search(
            r"\b(?:ZC_IREQUIRE|ZC_FAIL(?:_ASSERT)?)\b", text
        ):
            errors.append(f"{path}: raw session invariant assertion is forbidden")

    header_text = strip_cpp_comments_and_literals(files.get(SESSION_HEADER, ""))
    class_surface = re.sub(r"\bfriend\s+class\s+CompilerSession\s*;", "", header_text)
    if len(re.findall(r"\bclass\s+CompilerSession\b", class_surface)) != 1:
        errors.append(f"{SESSION_HEADER}: must declare exactly one CompilerSession class")
    if re.search(r"\busing\s+\w+\s*=\s*(?:driver::)?CompilerSession\b", class_surface):
        errors.append(f"{SESSION_HEADER}: CompilerSession compatibility alias is forbidden")
    if re.search(r"\btypedef\b[^;]*\bCompilerSession\b", class_surface):
        errors.append(f"{SESSION_HEADER}: CompilerSession typedef wrapper is forbidden")
    for match in re.finditer(
        r"\b(?:class|struct)\s+([A-Za-z_]\w*)[^;{]*\{(.*?)\};", class_surface, re.S
    ):
        if match.group(1) != "CompilerSession" and re.search(
            r"\bCompilerSession\b", match.group(2)
        ):
            errors.append(f"{SESSION_HEADER}: CompilerSession wrapper class is forbidden")


def check_session_ownership(files: dict[Path, str], errors: list[str]) -> None:
    header = files.get(SESSION_HEADER, "")
    source = files.get(SESSION_SOURCE, "")
    for marker in SESSION_HEADER_MARKERS:
        if marker not in header:
            errors.append(f"{SESSION_HEADER}: missing session contract marker: {marker}")
    for marker in SESSION_SOURCE_MARKERS:
        if marker not in source:
            errors.append(f"{SESSION_SOURCE}: missing session ownership marker: {marker}")

    if source.count("basic::ThreadPool threadPool;") != 1:
        errors.append(f"{SESSION_SOURCE}: must own exactly one frontend scheduler")
    if source.count("threadPool.enqueue(") != 1:
        errors.append(f"{SESSION_SOURCE}: parse scheduling must have exactly one enqueue site")


def check_single_scheduler(files: dict[Path, str], errors: list[str]) -> None:
    infrastructure = {THREAD_POOL_HEADER, THREAD_POOL_SOURCE}
    thread_pool_declaration = re.compile(r"\b(?:basic::)?ThreadPool\s+[A-Za-z_]\w*\s*[;({]")
    raw_scheduler = re.compile(r"\b(?:std::jthread|std::async|pthread_create|dispatch_async)\b")

    for path, raw_text in sorted(files.items()):
        if path.suffix not in {".h", ".cc"} or path in infrastructure:
            continue
        text = strip_cpp_comments_and_literals(raw_text)
        if path != SESSION_SOURCE and thread_pool_declaration.search(text):
            errors.append(f"{path}: secondary ThreadPool scheduler is forbidden")
        if path != SESSION_SOURCE and re.search(r"\bthreadPool\s*\.\s*enqueue\s*\(", text):
            errors.append(f"{path}: secondary frontend enqueue site is forbidden")
        if raw_scheduler.search(text):
            errors.append(f"{path}: raw secondary scheduler primitive is forbidden")


def check_cli_root(files: dict[Path, str], errors: list[str]) -> None:
    source = files.get(CLI_SOURCE, "")
    for marker in CLI_MARKERS:
        if marker not in source:
            errors.append(f"{CLI_SOURCE}: missing process-root session marker: {marker}")

    factory_position = source.find("identity::SemanticContextFactory contextFactory;")
    session_position = source.find("zc::Own<driver::CompilerSession> session;")
    if factory_position < 0 or session_position < 0 or factory_position > session_position:
        errors.append(f"{CLI_SOURCE}: context factory must outlive the CompilerSession")

    factory_declaration = re.compile(
        r"\b(?:identity::)?SemanticContextFactory\s+[A-Za-z_]\w*\s*(?:[;{]|\()"
    )
    for path, raw_text in sorted(files.items()):
        if path.suffix not in {".h", ".cc"} or path in {
            CLI_SOURCE,
            BRAND_HEADER,
            BRAND_SOURCE,
        }:
            continue
        text = strip_cpp_comments_and_literals(raw_text)
        if factory_declaration.search(text):
            errors.append(f"{path}: secondary process-root SemanticContextFactory is forbidden")

    for path, raw_text in sorted(files.items()):
        if path.suffix not in {".h", ".cc"} or path in {
            SESSION_SOURCE,
            REGISTRY_SET_SOURCE,
        }:
            continue
        text = strip_cpp_comments_and_literals(raw_text)
        if "SemanticIdentityRegistrySet::create(" in text:
            errors.append(f"{path}: identity registry family must be claimed by CompilerSession")


def check_build_wiring(files: dict[Path, str], errors: list[str]) -> None:
    cmake = files.get(DRIVER_CMAKE, "")
    if "file(GLOB DRIVER_SRC compiler-session.cc)" not in cmake:
        errors.append(f"{DRIVER_CMAKE}: driver target must compile compiler-session.cc directly")
    if re.search(r"\bdriver\.cc\b", cmake):
        errors.append(f"{DRIVER_CMAKE}: forbidden driver.cc build input remains")


def analyze(files: dict[Path, str]) -> list[str]:
    errors: list[str] = []
    check_driver_surface(files, errors)
    check_session_ownership(files, errors)
    check_single_scheduler(files, errors)
    check_cli_root(files, errors)
    check_build_wiring(files, errors)
    return errors


def run_check() -> int:
    errors = analyze(production_sources())
    if errors:
        print("CompilerSession architecture check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print("CompilerSession architecture check passed (one root, one context, one scheduler).")
    return 0


def expect_rejection(
    baseline: dict[Path, str], name: str, mutate, expected_fragment: str
) -> list[str]:
    fixture = dict(baseline)
    mutate(fixture)
    errors = analyze(fixture)
    if any(expected_fragment in error for error in errors):
        return []
    return [f"negative fixture {name!r} was not rejected for {expected_fragment!r}"]


def run_self_test() -> int:
    baseline = production_sources()
    errors = analyze(baseline)
    if errors:
        print("CompilerSession architecture self-test baseline failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    failures: list[str] = []
    failures += expect_rejection(
        baseline,
        "old driver",
        lambda files: files.__setitem__(
            Path("products/zomlang/compiler/driver/driver.h"), "class CompilerDriver {};"
        ),
        "forbidden CompilerDriver identifier remains",
    )
    failures += expect_rejection(
        baseline,
        "compatibility alias",
        lambda files: files.__setitem__(
            SESSION_HEADER, files[SESSION_HEADER] + "\nusing Driver = CompilerSession;\n"
        ),
        "CompilerSession compatibility alias is forbidden",
    )
    failures += expect_rejection(
        baseline,
        "wrapper surface",
        lambda files: files.__setitem__(
            SESSION_HEADER,
            files[SESSION_HEADER] + "\nclass SessionWrapper { CompilerSession session; };\n",
        ),
        "CompilerSession wrapper class is forbidden",
    )
    failures += expect_rejection(
        baseline,
        "second scheduler",
        lambda files: files.__setitem__(
            Path("products/zomlang/compiler/checker/parallel-checker.cc"),
            "void run() { basic::ThreadPool checkerPool; }",
        ),
        "secondary ThreadPool scheduler is forbidden",
    )
    failures += expect_rejection(
        baseline,
        "missing registry owner",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE].replace(
                "zc::Maybe<identity::SemanticIdentityRegistrySet> identityRegistries;", ""
            ),
        ),
        "missing session ownership marker",
    )
    failures += expect_rejection(
        baseline,
        "second context factory",
        lambda files: files.__setitem__(
            Path("products/zomlang/compiler/checker/checker-context.cc"),
            "void run() { identity::SemanticContextFactory checkerFactory; }",
        ),
        "secondary process-root SemanticContextFactory is forbidden",
    )
    failures += expect_rejection(
        baseline,
        "raw session assertion",
        lambda files: files.__setitem__(
            SESSION_SOURCE, files[SESSION_SOURCE] + "\nZC_IREQUIRE(false, \"bad\");\n"
        ),
        "raw session invariant assertion is forbidden",
    )

    if failures:
        print("CompilerSession architecture self-test failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("CompilerSession architecture negative fixtures passed (7/7).")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    return run_check() if args.check else run_self_test()


if __name__ == "__main__":
    raise SystemExit(main())
