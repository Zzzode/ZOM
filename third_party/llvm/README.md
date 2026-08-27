# LLVM Toolchain Lock

ZOM's target-aware backend (RFC 0021) and the RFC 0016 fail-closed discovery
gate require LLVM `22.1.8`. Following the Rust/Swift self-managed model, the
dependency is locked to an exact upstream **source commit**, not merely a
version number, so it is reproducible to the commit and can carry ZOM-specific
LLVM patches later. The locked source -- not any system package
(apt.llvm.org / Homebrew) -- is the source of truth.

## Locked source

| Field | Value |
|---|---|
| Repository | <https://github.com/llvm/llvm-project> |
| Tag | `llvmorg-22.1.8` |
| Commit (peeled) | `ca7933e47d3a3451d81e72ac174dcb5aa28b59d1` |
| Annotated tag object | `e013073558445169e8732e25fa86e9913bfdd24e` |
| Version | `22.1.8` |
| Targets | `X86;AArch64` |

The machine-readable lock is `third_party/llvm/llvm-lock.json`. The version
string there is required to equal `ZOM_LLVM_REQUIRED_VERSION` in
`cmake/utils/llvm.cmake`; `scripts/check-llvm-lock.py` enforces that they cannot
drift.

## Required components

The exact backend component set required by the RFC 0016 contract (mirrored in
`cmake/utils/llvm.cmake` as `ZOM_LLVM_REQUIRED_COMPONENTS`):

`Core`, `Support`, `Target`, `TargetParser`, `MC`, `CodeGen`, `AsmParser`,
`AsmPrinter`, `BitWriter`, `X86`, `AArch64`.

## Provisioning

`scripts/provision-llvm.py` reads this lock, shallow-clones the locked tag,
configures an `X86;AArch64` Release with all optional dependencies off
(`zlib`, `zstd`, `libxml2`, `z3`, `libedit`, `terminfo`), builds, installs to a
prefix (default `$HOME/toolchains/llvm22`), and verifies that
`llvm-config --version` equals the locked version with both targets present and
all eleven components resolvable. It is idempotent: if a valid install already
exists at the prefix it prints `already provisioned` and exits without
rebuilding.

Point the build at the provisioned install with
`-DZOM_ENABLE_LLVM_BACKEND=ON -DLLVM_DIR=<prefix>/lib/cmake/llvm`. The frontend
build leaves `ZOM_ENABLE_LLVM_BACKEND` OFF and links no LLVM.

## Notes

- The host of record for the initial provision is Debian glibc 2.36. The
  apt.llvm.org prebuilt is glibc 2.38 and will not run there, so a from-source
  build is required on that host; this is itself an argument for a self-managed
  source lock rather than a system package.
- CI provisions the same locked commit on the fixed `ubuntu-24.04` runner
  (glibc 2.39), preferring a cached prebuilt of the locked commit keyed on the
  commit hash, and falling back to `scripts/provision-llvm.py`. See
  `.github/workflows/CI.yml` (`check-backend-llvm`) and the RFC 0016 "LLVM build
  and CI contract".
