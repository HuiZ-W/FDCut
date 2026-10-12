#!/usr/bin/env python3
"""Run Benchmark12 FD workloads sequentially across exact methods."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(8 << 20), b""):
            result.update(block)
    return result.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("workload_dir", type=Path)
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--methods", nargs="+", default=[
        "base", "sweep", "component", "enum", "enum-prune"
    ])
    parser.add_argument("--include", nargs="*", metavar="DATASET_ID")
    parser.add_argument("--enum-max-states", type=int, default=100_000)
    parser.add_argument("--enum-timeout-ms", type=int, default=250)
    parser.add_argument("--process-timeout-s", type=int, default=0)
    args = parser.parse_args()
    allowed_methods = {
        "base", "sweep", "component", "enum", "enum-prune",
        "jenum", "wenum", "wbb",
    }
    if not args.methods or set(args.methods) - allowed_methods:
        parser.error("unknown or empty --methods")
    if min(args.enum_max_states, args.enum_timeout_ms, args.process_timeout_s) < 0:
        parser.error("limits must be nonnegative")
    if args.output_dir.exists():
        raise SystemExit(f"refusing existing output directory: {args.output_dir}")

    workload_manifest = json.loads(
        (args.workload_dir / "manifest.json").read_text()
    )
    available = {
        item["id"]: item for item in workload_manifest["datasets"]
        if item["status"] == "materialized"
    }
    requested = set(args.include or available)
    unknown = requested - set(available)
    if unknown:
        raise SystemExit(
            f"requested datasets are not materialized: {', '.join(sorted(unknown))}"
        )
    materialized = [available[name] for name in sorted(requested)]
    if not materialized:
        raise SystemExit("workload manifest has no materialized datasets")
    args.output_dir.mkdir(parents=True)
    results_path = args.output_dir / "results.jsonl"
    rows = 0
    with results_path.open("x") as output:
        for item in materialized:
            query = args.workload_dir / item["file"]
            expected = None
            expected_set = False
            for method in args.methods:
                command = [str(args.binary), "solve", str(query), method]
                if method in {"enum", "enum-prune", "jenum", "wenum", "wbb"}:
                    command += [str(args.enum_max_states), str(args.enum_timeout_ms)]
                run = subprocess.run(
                    command,
                    capture_output=True,
                    text=True,
                    timeout=args.process_timeout_s or None,
                )
                if run.returncode:
                    raise RuntimeError(
                        f"{item['id']} {method} failed ({run.returncode}): {run.stderr}"
                    )
                answer = json.loads(run.stdout)["query"]
                if method == "base":
                    expected = answer["value"]
                    expected_set = True
                elif answer["complete"] and expected_set:
                    if answer["value"] != expected:
                        raise RuntimeError(
                            f"{item['id']} {method} disagrees with base"
                        )
                row = {"dataset": item["id"], "method": method,
                       "workload_sha256": item["sha256"], "query": answer}
                output.write(json.dumps(row, sort_keys=True) + "\n")
                output.flush()
                rows += 1

    run_manifest = {
        "schema_version": 1,
        "workload_manifest": str((args.workload_dir / "manifest.json").resolve()),
        "binary": str(args.binary.resolve()),
        "binary_sha256": digest(args.binary),
        "methods": args.methods,
        "enum_max_states": args.enum_max_states,
        "enum_timeout_ms": args.enum_timeout_ms,
        "datasets": len(materialized),
        "rows": rows,
        "results": results_path.name,
    }
    (args.output_dir / "manifest.json").write_text(
        json.dumps(run_manifest, indent=2, sort_keys=True) + "\n"
    )
    print(json.dumps({"status": "PASS", "rows": rows}, sort_keys=True))


if __name__ == "__main__":
    main()
