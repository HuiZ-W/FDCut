#!/usr/bin/env python3
"""Exercise Benchmark12 planning, capacity skips and FDG materialization."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile


binary = str(Path(sys.argv[1]).resolve())
script = Path(__file__).resolve().parent / "helpers" / "make_smoke_workloads.py"
runner = Path(__file__).resolve().parent / "helpers" / "run_smoke_workloads.py"
graph_text = "4 3\n0 1\n1 2\n2 3\n"

with tempfile.TemporaryDirectory(prefix="fd-benchmark12-") as directory:
    root = Path(directory)
    release = root / "release"
    graphs = release / "txt"
    graphs.mkdir(parents=True)
    datasets = []
    for index in range(11):
        dataset_id = f"tiny-{index}"
        datasets.append({"id": dataset_id, "nodes": 4, "edges": 3})
        (graphs / f"{dataset_id}.txt").write_text(graph_text)
    datasets.append({"id": "too-large", "nodes": 100_000_000,
                     "edges": 1 << 31})
    (graphs / "too-large.txt").write_text("100000000 2147483648\n")
    (release / "manifest.json").write_text(json.dumps({
        "complete": True,
        "completed": 12,
        "expected": 12,
        "datasets": datasets,
    }))

    output = root / "workloads"
    subprocess.run([
        sys.executable, str(script), binary, str(release), str(output),
        "--materialize", "--include", "tiny-0", "--candidate-limit", "2",
    ], check=True, capture_output=True, text=True)
    manifest = json.loads((output / "manifest.json").read_text())
    assert manifest["status_counts"] == {
        "materialized": 1,
        "not_selected": 10,
        "unsupported": 1,
    }
    records = {entry["id"]: entry for entry in manifest["datasets"]}
    assert "edge count" in records["too-large"]["reason"]
    workload = output / records["tiny-0"]["file"]
    assert workload.is_file() and records["tiny-0"]["allowed"] == [1, 3]

    full_output = root / "full-workloads"
    subprocess.run([
        sys.executable, str(script), binary, str(release), str(full_output),
        "--materialize", "--include", "tiny-0", "--full-domain",
    ], check=True, capture_output=True, text=True)
    full_manifest = json.loads((full_output / "manifest.json").read_text())
    full_record = next(
        item for item in full_manifest["datasets"] if item["id"] == "tiny-0"
    )
    assert full_manifest["full_domain"]
    assert full_manifest["candidate_limit"] is None
    assert full_record["allowed"] == "all_non_endpoints"
    assert full_record["allowed_count"] == 2
    full_lines = (full_output / full_record["file"]).read_text().splitlines()
    vertex_marks = [int(line.split()[0]) for line in full_lines[1:5]]
    assert sorted(vertex_marks) == [1, 2, 8, 8]

    values = []
    for method in ["base", "sweep", "FDCut", "Brute", "Prune",
                   "ListJ", "ListW", "PruneW"]:
        result = json.loads(subprocess.check_output(
            [binary, "solve", str(workload), method], text=True
        ))["query"]
        assert result["complete"] and result["feasible"]
        values.append(result["value"])
    assert len(set(values)) == 1

    runs = root / "runs"
    subprocess.run([
        sys.executable, str(runner), binary, str(output), str(runs),
    ], check=True, capture_output=True, text=True)
    run_manifest = json.loads((runs / "manifest.json").read_text())
    assert run_manifest["datasets"] == 1 and run_manifest["rows"] == 5
    assert len((runs / "results.jsonl").read_text().splitlines()) == 5

print(json.dumps({"status": "PASS", "methods": 8, "datasets": 12,
                  "runner_rows": 5}))
