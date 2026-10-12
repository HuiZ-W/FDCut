#!/usr/bin/env python3
"""Keep public capacity reporting aligned with the FDG parser limits."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


binary = sys.argv[1]


def capacity(nodes, edges):
    output = subprocess.check_output(
        [binary, "capacity", str(nodes), str(edges)], text=True
    )
    return json.loads(output)


assert capacity(2, 0)["supported"]
assert capacity(579_968_996, 311_781_591)["supported"]
assert capacity(100_000_000, 1_200_000_000)["supported"]
assert capacity((1 << 31) - 3, 0)["supported"]

nodes = capacity((1 << 31) - 2, 0)
assert not nodes["supported"] and "node count" in nodes["reason"]

edges = capacity(2, 1 << 31)
assert not edges["supported"] and "edge count" in edges["reason"]

bad = subprocess.run([binary, "capacity", "-1", "0"], capture_output=True)
assert bad.returncode == 2

with tempfile.TemporaryDirectory(prefix="fd-binary-full-") as directory:
    graph = Path(directory) / "tiny.bin"
    graph.write_bytes(
        b"FDGBIN01"
        + struct.pack("<QQ", 4, 2)
        + struct.pack("<QQQQ", 0, 1, 1, 2)
    )
    values = []
    for method in ["base", "sweep", "FDCut", "Brute", "Prune",
                   "ListJ", "ListW", "PruneW"]:
        result = json.loads(subprocess.check_output([
            binary, "solve-bin-full", str(graph), "0", "2", "1", method,
        ], text=True))["query"]
        assert result["complete"] and result["feasible"]
        assert result["value"] == 1 and result["selected"] == [1]
        values.append(result["value"])
    assert len(set(values)) == 1

    samples = Path(directory) / "samples.jsonl"
    subprocess.run([
        binary, "sample-bin-full", str(graph), str(samples),
        "1", "20260917", "100", "1",
    ], check=True, capture_output=True, text=True)
    rows = [json.loads(line) for line in samples.read_text().splitlines()]
    assert len(rows) == 1
    assert rows[0]["x"] == 0 and rows[0]["mediator"] == 1
    assert rows[0]["y"] == 2 and rows[0]["feasible"]
    assert rows[0]["screen"] == "fd-compiler-hard-constraints"

    root_samples = Path(directory) / "root-samples.jsonl"
    subprocess.run([
        binary, "sample-bin-full", str(graph), str(root_samples),
        "1", "20260917", "100", "1", "root-x",
    ], check=True, capture_output=True, text=True)
    root_rows = [json.loads(line) for line in root_samples.read_text().splitlines()]
    assert len(root_rows) == 1 and root_rows[0]["x"] == 0
    assert root_rows[0]["sampling"] == "root-x" and root_rows[0]["feasible"]

    pair_samples = Path(directory) / "pair-samples.jsonl"
    subprocess.run([
        binary, "sample-bin-full", str(graph), str(pair_samples),
        "1", "20260917", "10000", "1", "random-pair",
    ], check=True, capture_output=True, text=True)
    pair_rows = [json.loads(line) for line in pair_samples.read_text().splitlines()]
    assert len(pair_rows) == 1 and pair_rows[0]["x"] == 0 and pair_rows[0]["y"] == 2
    assert "mediator" not in pair_rows[0]
    assert pair_rows[0]["sampling"] == "random-pair"
    assert pair_rows[0]["nonempty"] and pair_rows[0]["feasible"]

    parallel_samples = Path(directory) / "parallel-samples.jsonl"
    subprocess.run([
        binary, "sample-bin-full", str(graph), str(parallel_samples),
        "1", "20260917", "10000", "1", "random-pair", "2",
    ], check=True, capture_output=True, text=True)
    parallel_rows = [json.loads(line) for line in parallel_samples.read_text().splitlines()]
    assert len(parallel_rows) == 1
    assert parallel_rows[0]["x"] == 0 and parallel_rows[0]["y"] == 2
    assert parallel_rows[0]["threads"] == 2 and parallel_rows[0]["nonempty"]

    for row in rows + root_rows + pair_rows + parallel_rows:
        assert "method" not in row
    # Removed method arguments must fail, not shift silently into another option.
    for obsolete in ("FDCut", "base", "sweep"):
        failed_output = Path(directory) / f"obsolete-{obsolete}.jsonl"
        failed = subprocess.run([
            binary, "sample-bin-full", str(graph), str(failed_output),
            "1", "42", "100", "1", obsolete,
        ], capture_output=True, text=True)
        assert failed.returncode != 0 and "sampling mode" in failed.stderr
        assert not failed_output.exists()

    batch_queries = Path(directory) / "batch-queries.jsonl"
    batch_queries.write_text(
        json.dumps({"query_id": 7, "x": 0, "y": 2, "cost": 1}) + "\n"
        + json.dumps({"query_id": 9, "x": 0, "y": 2, "cost": 2}) + "\n"
    )
    batch_output = Path(directory) / "batch-results.jsonl"
    batch_run = json.loads(subprocess.check_output([
        binary, "solve-bin-batch", str(graph), str(batch_queries),
        str(batch_output), "FDCut",
    ], text=True))
    assert batch_run["status"] == "PASS" and batch_run["queries"] == 2
    batch_rows = [json.loads(line) for line in batch_output.read_text().splitlines()]
    assert [row["query_id"] for row in batch_rows] == [7, 9]
    assert [row["query"]["value"] for row in batch_rows] == [1, 2]
    assert all(row["query"]["feasible"] and row["query"]["complete"]
               for row in batch_rows)
    assert all("selected" not in row["query"] for row in batch_rows)

    lazy_output = Path(directory) / "batch-results-lazy.jsonl"
    lazy_env = os.environ.copy()
    lazy_env["FD_FORCE_LAZY_CUT"] = "1"
    lazy_run = json.loads(subprocess.check_output([
        binary, "solve-bin-batch", str(graph), str(batch_queries),
        str(lazy_output), "FDCut",
    ], text=True, env=lazy_env))
    assert lazy_run["status"] == "PASS" and lazy_run["queries"] == 2
    lazy_rows = [json.loads(line) for line in lazy_output.read_text().splitlines()]
    assert [row["query"]["value"] for row in lazy_rows] == [1, 2]
    assert all(row["query"]["implicit_cut"] for row in lazy_rows)
    assert all(row["query"]["cut_rounds"] >= 1 for row in lazy_rows)

print(json.dumps({"status": "PASS", "capacity_checks": 7,
                  "binary_methods": 8, "sample_queries": 1,
                  "batch_queries": 2}))
