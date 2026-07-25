#!/usr/bin/env python3
"""Generate the canonical RFC 0012 package-resolver performance edge fixture."""

from __future__ import annotations

import argparse
import hashlib
import heapq
import os
import struct
from pathlib import Path


DOMAIN = b"zom.performance-edge\0"


def edge_key(consumer: int, provider: int) -> tuple[int, int]:
    encoded = struct.pack(">II", consumer, provider)
    return int.from_bytes(hashlib.sha256(DOMAIN + encoded).digest(), "big"), int.from_bytes(
        encoded, "big"
    )


def generate(package_count: int, edge_count: int) -> list[tuple[int, int]]:
    if package_count < 2:
        raise ValueError("package count must be at least two")
    chain_count = package_count - 1
    extra_count = edge_count - chain_count
    if extra_count < 0:
        raise ValueError("edge count cannot be smaller than the required chain")

    selected: list[tuple[int, int, int, int]] = []
    for consumer in range(package_count):
        for provider in range(consumer + 1, package_count):
            if provider == consumer + 1:
                continue
            digest, encoded = edge_key(consumer, provider)
            item = (-digest, -encoded, consumer, provider)
            if len(selected) < extra_count:
                heapq.heappush(selected, item)
                continue
            worst_digest = -selected[0][0]
            worst_encoded = -selected[0][1]
            if (digest, encoded) < (worst_digest, worst_encoded):
                heapq.heapreplace(selected, item)

    extras = [(item[2], item[3]) for item in selected]
    extras.sort(key=lambda pair: edge_key(*pair))
    chain = [(index, index + 1) for index in range(package_count - 1)]
    result = chain + extras
    if len(result) != edge_count or len(set(result)) != edge_count:
        raise RuntimeError("generated edge fixture is not unique and complete")
    return result


def write_fixture(output: Path, package_count: int, edges: list[tuple[int, int]]) -> None:
    payload = bytearray(struct.pack(">II", package_count, len(edges)))
    for consumer, provider in edges:
        payload.extend(struct.pack(">II", consumer, provider))
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.write_bytes(payload)
    os.replace(temporary, output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--packages", type=int, default=10_000)
    parser.add_argument("--edges", type=int, default=50_000)
    arguments = parser.parse_args()
    write_fixture(
        arguments.output,
        arguments.packages,
        generate(arguments.packages, arguments.edges),
    )


if __name__ == "__main__":
    main()
