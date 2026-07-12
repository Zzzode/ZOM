#!/usr/bin/env python3
"""Run the RFC 0012 resolver fixture with an OS-enforced peak-RSS gate."""

from __future__ import annotations

import argparse
import resource
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True)
    parser.add_argument("--max-rss-bytes", type=int, required=True)
    arguments = parser.parse_args()

    completed = subprocess.run([arguments.executable], check=False)
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    peak_rss = int(usage.ru_maxrss)
    if sys.platform != "darwin":
        peak_rss *= 1024
    print(f"package resolver peak RSS: {peak_rss} bytes")
    if completed.returncode != 0:
        return completed.returncode
    if peak_rss > arguments.max_rss_bytes:
        print(
            f"peak RSS exceeded limit: {peak_rss} > {arguments.max_rss_bytes}",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
