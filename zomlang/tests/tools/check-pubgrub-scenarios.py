#!/usr/bin/env python3
"""Validate the checked-in PubGrub corpus and its C++ replay oracles."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


EXPECTED_IDS = {
    "greatest-eligible-release",
    "no-version-conflict",
    "backtrack-highest-conflict",
    "skip-yanked-release",
    "separate-activation-domains",
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--replay-source", type=Path, required=True)
    arguments = parser.parse_args()

    document = json.loads(arguments.corpus.read_text(encoding="utf-8"))
    if document.get("schema") != "zom.pubgrub-scenarios":
        raise SystemExit("invalid PubGrub scenario schema")
    scenarios = document.get("scenarios")
    if not isinstance(scenarios, list):
        raise SystemExit("PubGrub scenario list is missing")
    ids = [scenario.get("id") for scenario in scenarios]
    if len(ids) != len(set(ids)) or set(ids) != EXPECTED_IDS:
        raise SystemExit("PubGrub scenario IDs are incomplete or duplicated")

    replay_source = arguments.replay_source.read_text(encoding="utf-8")
    replay_hashes = set(re.findall(r'"([0-9a-f]{64})"', replay_source))
    for scenario in scenarios:
        if not scenario.get("root") or not scenario.get("available"):
            raise SystemExit(f"scenario {scenario.get('id')} has incomplete input")
        expected = scenario.get("expected")
        if not isinstance(expected, dict) or not expected:
            raise SystemExit(f"scenario {scenario.get('id')} has no expected result")
        for field, value in expected.items():
            if field.endswith("Sha256") and value not in replay_hashes:
                raise SystemExit(
                    f"scenario {scenario.get('id')} hash is not replayed by C++ tests"
                )
    print(f"PubGrub scenario corpus ok: {len(scenarios)} scenarios")


if __name__ == "__main__":
    main()
