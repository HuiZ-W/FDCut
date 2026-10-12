#!/usr/bin/env python3
"""Validate compact paired E2/E3 Benchmark12 query files and write a manifest."""

import argparse
import hashlib
import json
from pathlib import Path


MASK64 = (1 << 64) - 1


def mix(value: int) -> int:
    value = (value + 0x9E3779B97F4A7C15) & MASK64
    value = ((value ^ (value >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    value = ((value ^ (value >> 27)) * 0x94D049BB133111EB) & MASK64
    return value ^ (value >> 31)


def permuted_rank(position: int, n: int, seed: int) -> int:
    if n == 1:
        return 0
    bits = (n - 1).bit_length()
    bits += bits & 1
    half = bits // 2
    mask = (1 << half) - 1
    value = position
    while True:
        left, right = value >> half, value & mask
        for round_id in range(8):
            f = mix(right ^ mix(seed + round_id * 0x9E3779B97F4A7C15)) & mask
            left, right = right, left ^ f
        value = (left << half) | right
        if value < n:
            return value


def vertex_from_rank(rank: int, x: int, y: int) -> int:
    value = rank
    first, second = sorted((x, y))
    if value >= first:
        value += 1
    if value >= second:
        value += 1
    return value


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(8 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def expected_e2_levels(nodes: int) -> list[int]:
    maximum = nodes - 2 - 8
    levels = [k for k in (16, 32, 64, 128, 256) if k <= maximum]
    if maximum < 256 and levels and levels[-1] != maximum:
        levels.append(maximum)
    return levels or [maximum]


def validate_file(path: Path, dataset: dict, workload: str) -> dict:
    rows = [json.loads(line) for line in path.read_text().splitlines() if line]
    nodes = dataset["nodes"]
    universe = nodes - 2
    levels = expected_e2_levels(nodes) if workload == "E2" else [10, 20, 30, 40, 50]
    assert len(rows) == 20 * len(levels), (dataset["id"], workload, len(rows))
    assert [row["query_id"] for row in rows] == list(range(len(rows)))
    groups = {}
    for row in rows:
        assert row["workload"] == workload and row["feasible"] is True
        assert row["nonempty"] is True and row["cost"] == 1
        assert row["screen"] == "fd-compiler-hard-constraints"
        assert row["universe_size"] == universe
        assert row["level_value"] == levels[row["level_index"]]
        assert row["r_size"] - row["i_size"] == row["k"]
        assert 0 <= row["x"] < nodes and 0 <= row["y"] < nodes and row["x"] != row["y"]
        rep = row["r_representation"]
        assert rep["scheme"] == "feistel-cyclewalk-v1"
        assert rep["prefix_size"] == row["r_size"] <= universe
        positions = list(range(row["i_size"]))
        if row["sampling"] == "paired-random-R-feasible-growth-I-v1":
            positions = row["i_positions"]
            assert len(positions) == row["i_size"] and len(set(positions)) == len(positions)
            assert all(0 <= pos < row["r_size"] for pos in positions)
        expected_i = [
            vertex_from_rank(permuted_rank(pos, universe, rep["seed"]), row["x"], row["y"])
            for pos in positions
        ]
        assert row["i"] == expected_i and len(set(expected_i)) == row["i_size"]
        assert row["x"] not in expected_i and row["y"] not in expected_i
        if workload == "E2":
            assert row["k"] == row["level_value"] and 1 <= row["i_size"] <= 8
        else:
            assert row["i_size"] == 8
            assert row["r_size"] == max(8, universe * row["level_value"] // 100)
        key = row["pair_id"]
        signature = (row["x"], row["y"], tuple(row["i"]), rep["seed"], row["i_size"])
        groups.setdefault(key, []).append((row["level_index"], row["r_size"], signature))
    assert sorted(groups) == list(range(20))
    endpoints = set()
    for items in groups.values():
        items.sort()
        assert [index for index, _, _ in items] == list(range(len(levels)))
        assert len({signature for _, _, signature in items}) == 1
        assert all(left[1] <= right[1] for left, right in zip(items, items[1:]))
        endpoints.add(items[0][2][:2])
    assert len(endpoints) == 20
    return {
        "file": str(path), "sha256": sha256(path), "rows": len(rows),
        "pairs": 20, "levels": levels, "validated": True,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("query_directory", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    source = json.loads(args.manifest.read_text())
    report = {"schema_version": 1, "seed": 20260923, "datasets": []}
    for dataset in source["datasets"]:
        identifier = dataset["id"]
        e2 = args.query_directory / "e2" / f"{identifier}.jsonl"
        e3 = args.query_directory / "e3" / f"{identifier}.jsonl"
        if not (e2.exists() and e3.exists()):
            continue
        report["datasets"].append({
            "id": identifier, "nodes": dataset["nodes"], "edges": dataset["edges"],
            "E2": validate_file(e2, dataset, "E2"),
            "E3": validate_file(e3, dataset, "E3"),
        })
    report["completed_datasets"] = len(report["datasets"])
    report["total_rows"] = sum(
        item[workload]["rows"] for item in report["datasets"] for workload in ("E2", "E3")
    )
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"status": "PASS", "datasets": report["completed_datasets"],
                      "rows": report["total_rows"], "output": str(args.output)}))


if __name__ == "__main__":
    main()
