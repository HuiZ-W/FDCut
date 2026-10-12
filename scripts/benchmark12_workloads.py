#!/usr/bin/env python3
"""Plan or materialize deterministic synthetic FD workloads for Benchmark12.

The release graphs contain only topology.  This script deliberately labels its
X/Y/R choices and costs as a synthetic integration workload; it does not claim
that they are measured treatment, outcome, observability, or cost data.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


MASK64 = (1 << 64) - 1


def deterministic_cost(vertex, seed):
    value = (vertex + seed + 0x9E3779B97F4A7C15) & MASK64
    value = ((value ^ (value >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    value = ((value ^ (value >> 27)) * 0x94D049BB133111EB) & MASK64
    value ^= value >> 31
    return 1 + value % 100


def capacity(binary, nodes, edges):
    run = subprocess.run(
        [binary, "capacity", str(nodes), str(edges)],
        capture_output=True,
        text=True,
        check=True,
    )
    return json.loads(run.stdout)


def graph_header(path):
    with path.open("r", encoding="ascii", newline="") as source:
        fields = source.readline().split()
    if len(fields) != 2 or any(not item.isdecimal() for item in fields):
        raise RuntimeError(f"invalid graph header: {path}")
    return tuple(map(int, fields))


def edges(path, limit=None):
    with path.open("r", encoding="ascii", newline="") as source:
        source.readline()
        for index, line in enumerate(source):
            if limit is not None and index >= limit:
                break
            fields = line.split()
            if len(fields) != 2:
                raise RuntimeError(f"invalid edge record {index}: {path}")
            yield tuple(map(int, fields))


def find_two_hop(path, search_edge_limit):
    # Whichever of a->b or b->c appears second discovers the path.  The maps
    # are bounded by search_edge_limit rather than graph size.
    first_in, first_out = {}, {}
    for u, v in edges(path, search_edge_limit):
        if u in first_in:
            x = first_in[u]
            if len({x, u, v}) == 3:
                return x, u, v
        if v in first_out:
            y = first_out[v]
            if len({u, v, y}) == 3:
                return u, v, y
        first_out.setdefault(u, v)
        first_in.setdefault(v, u)
    return None


def choose_candidates(path, x, y, mediator, limit):
    chosen, seen = [mediator], {x, y, mediator}
    for u, v in edges(path):
        for vertex in (u, v):
            if vertex not in seen:
                seen.add(vertex)
                chosen.append(vertex)
                if len(chosen) == limit:
                    return chosen
    return chosen


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(8 << 20), b""):
            result.update(block)
    return result.hexdigest()


def materialize(graph, output, nodes, edges_count, candidates, x, y, seed):
    partial = output.with_suffix(output.suffix + ".partial")
    if output.exists() or partial.exists():
        raise RuntimeError(f"refusing existing workload output: {output}")
    allowed = None if candidates is None else set(candidates)
    with partial.open("x", encoding="ascii", newline="") as target:
        target.write(f"{nodes} {edges_count}\n")
        for vertex in range(nodes):
            if vertex == x:
                target.write("1 0\n")
            elif vertex == y:
                target.write("2 0\n")
            elif allowed is None or vertex in allowed:
                target.write(f"8 {deterministic_cost(vertex, seed)}\n")
            else:
                target.write("0 0\n")
        with graph.open("r", encoding="ascii", newline="") as source:
            source.readline()
            shutil.copyfileobj(source, target, length=8 << 20)
    os.replace(partial, output)
    return digest(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", help="fdtool executable")
    parser.add_argument("dataset_dir", type=Path,
                        help="Benchmark12 directory containing manifest.json and txt/")
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--materialize", action="store_true",
                        help="write supported FDG files; default is plan-only")
    parser.add_argument("--candidate-limit", type=int, default=20)
    parser.add_argument(
        "--full-domain", action="store_true",
        help=("allow every non-X/Y vertex and record only the candidate count; "
              "this treats every non-endpoint vertex as observable"),
    )
    parser.add_argument("--search-edge-limit", type=int, default=1_000_000)
    parser.add_argument("--seed", type=int, default=20260917)
    parser.add_argument("--include", nargs="*", metavar="DATASET_ID")
    args = parser.parse_args()
    if args.candidate_limit < 1:
        parser.error("--candidate-limit must be positive")
    if args.search_edge_limit < 1:
        parser.error("--search-edge-limit must be positive")
    if args.output_dir.exists():
        raise SystemExit(f"refusing existing output directory: {args.output_dir}")

    manifest = json.loads((args.dataset_dir / "manifest.json").read_text())
    records = manifest.get("datasets", [])
    if not manifest.get("complete") or len(records) != 12:
        raise SystemExit("expected a complete 12-dataset release manifest")
    known = {entry["id"] for entry in records}
    requested = set(args.include or known)
    unknown = requested - known
    if unknown:
        raise SystemExit(f"unknown dataset IDs: {', '.join(sorted(unknown))}")

    args.output_dir.mkdir(parents=True)
    report = {
        "schema_version": 1,
        "source_manifest": str((args.dataset_dir / "manifest.json").resolve()),
        "materialized": args.materialize,
        "seed": args.seed,
        "candidate_limit": None if args.full_domain else args.candidate_limit,
        "full_domain": args.full_domain,
        "semantics": (
            "Synthetic integration workload: deterministic two-hop X->Z->Y, "
            + ("I empty, every non-endpoint vertex treated as observable and allowed, "
               if args.full_domain else "I empty, bounded allowed R, ")
            + "deterministic integer costs. Not measured domain data."
        ),
        "datasets": [],
    }
    for entry in records:
        dataset_id = entry["id"]
        item = {
            "id": dataset_id,
            "nodes": entry["nodes"],
            "edges": entry["edges"],
        }
        check = capacity(args.binary, entry["nodes"], entry["edges"])
        item["capacity"] = check
        graph = args.dataset_dir / "txt" / f"{dataset_id}.txt"
        if not graph.is_file():
            item.update(status="missing_graph", reason=str(graph))
        elif graph_header(graph) != (entry["nodes"], entry["edges"]):
            item.update(status="header_mismatch")
        elif not check["supported"]:
            item.update(status="unsupported", reason=check["reason"])
        elif dataset_id not in requested:
            item.update(status="not_selected")
        elif not args.materialize:
            item.update(status="ready")
        else:
            path = find_two_hop(graph, args.search_edge_limit)
            if path is None:
                item.update(status="no_two_hop_within_search_limit")
            else:
                x, mediator, y = path
                candidates = None if args.full_domain else choose_candidates(
                    graph, x, y, mediator, args.candidate_limit
                )
                output = args.output_dir / f"{dataset_id}.fdg"
                sha256 = materialize(
                    graph, output, entry["nodes"], entry["edges"],
                    candidates, x, y, args.seed,
                )
                item.update(
                    status="materialized",
                    file=output.name,
                    sha256=sha256,
                    x=x,
                    mediator=mediator,
                    y=y,
                    allowed=("all_non_endpoints" if args.full_domain
                             else candidates),
                    allowed_count=(entry["nodes"] - 2 if args.full_domain
                                   else len(candidates)),
                    required=[],
                )
        report["datasets"].append(item)
        print(json.dumps(item, sort_keys=True), flush=True)

    counts = {}
    for item in report["datasets"]:
        counts[item["status"]] = counts.get(item["status"], 0) + 1
    report["status_counts"] = counts
    (args.output_dir / "manifest.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n"
    )
    print(json.dumps({"status": "PASS", "counts": counts}, sort_keys=True))


if __name__ == "__main__":
    main()
