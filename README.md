# FDCut

Minimum-cost front-door adjustment on DAGs, with FDCut and five baselines.

## Installation

Requires Linux, C/C++17 compilers, CMake ≥ 3.16 and Python ≥ 3.9.
HPF 3.23 is downloaded automatically; its notice is in [NOTICE.md](NOTICE.md).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure -j1
```

For an offline build, add
`-DFD_FETCH_HPF=OFF -DFD_HPF_SOURCE=/path/to/pseudo.c` to the configure command.

## Data formats

Vertex IDs are contiguous and zero-based. Edges are directed; the graph must
be acyclic, with sorted unique edges and no self-loops.

- **Weighted query (`.fdg`):** `n m`, then `n` lines of `marks cost`, then
  `m` lines of `u v`. Marks combine `X=1`, `Y=2`, `I=4`, `R=8`; required
  vertices have mark `12`, unselectable vertices `0`. Costs are nonnegative
  integers. See [examples/tiny.fdg](examples/tiny.fdg).
- **Topology text:** `n m`, followed by `m` lines of `u v`.
- **Topology binary:** eight-byte magic `FDGBIN01`, then `n`, `m` and edge
  endpoints as little-endian unsigned 64-bit integers.
- **Explicit query (`.spec`):** `x y i_size r_size`, then the IDs in `I`, then
  the IDs in `R`. Lists contain no duplicates, exclude endpoints and satisfy
  `I ⊆ R`. This interface uses unit costs and the complete original graph.

Generated queries are JSONL, selected by `query_id`. E3 supports a compact
allowed-set encoding; see [docs/FORMATS.md](docs/FORMATS.md).

Solver JSON contains `query.feasible`, `complete`, `proven_optimal`, `value`
and `selected`. A successful optimum has all three flags true; a complete
infeasible result has `feasible=false`.

## Running

Methods: `FDCut`, `Brute`, `Prune`, `ListJ`, `ListW`, `PruneW`
(case-insensitive).

```bash
# Weighted query; replace FDCut with any method above.
./build/fdtool solve examples/tiny.fdg FDCut

# Convert topology and solve an explicit I/R query.
mkdir -p work
./build/graph_convert txt-to-bin examples/tiny.txt work/tiny.bin
./build/fdsolveir work/tiny.bin examples/tiny.spec FDCut

# E1: empty I, all nonendpoint vertices allowed, uniform cost 1.
./build/fdtool solve-bin-full graph.bin 0 2 1 FDCut

# E1 endpoint generation: count, seed, maximum attempts, cost.
./build/fdtool sample-bin-full graph.bin endpoints.jsonl 100 20260926 100000 1

# E2: k = |R|/|I| in 16,32,64,128,256; |I| stratified over 1..8.
./build/fdratioe2 graph.bin endpoints.jsonl e2.jsonl 20260926
python3 scripts/validate_ratio_e2.py e2.jsonl graph.bin 20260926
python3 scripts/query_to_spec.py e2.jsonl 0 work/e2-q0.spec
./build/fdsolveir graph.bin work/e2-q0.spec FDCut

# E3: |I|=8; R covers 10%,20%,30%,40%,50% of nonendpoint vertices.
./build/fde3gen graph.bin endpoints.jsonl e3.jsonl 20260925
python3 scripts/validate_witness_e3.py e3.jsonl
python3 scripts/query_to_spec.py e3.jsonl 0 work/e3-q0.spec
./build/fdsolveir graph.bin work/e3-q0.spec FDCut compact
```

E2 caps ratios on small graphs; E3 requires at least 82 vertices and suitable
witnesses. Both target 20 endpoint pairs with nested allowed sets. See
[docs/WORKLOADS.md](docs/WORKLOADS.md) for sampler modes and guided generation.

`fdtool solve` and `solve-bin-full` accept optional enumeration limits
`MAX_STATES TIMEOUT_MS` (zero means unlimited). `fdsolveir` uses a seven-day
soft enumeration deadline. Aggregate multi-query budgets require an external
controller. Run concurrent solver queries in separate processes.

`query.total_s` is algorithm time; graph reading and independent post-validation
are separate. Set `FD_SKIP_POSTVALIDATION=1` to disable the latter, or
`FD_TIMING_MARKERS=1` for `fdsolveir` start/end timestamps on stderr.
Run `./build/fdtool --help` for command syntax.
