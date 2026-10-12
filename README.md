# FDCut

Research implementation of **Exact and Scalable Minimum-Cost Front-Door on
Causal Graphs**. Given a DAG, treatment and outcome sets, required variables
`I`, allowed variables `R`, and nonnegative additive costs, the solver finds
a minimum-cost classical front-door adjustment set `Z` satisfying
`I ⊆ Z ⊆ R`, or reports infeasibility.

This repository contains the C++ implementation, five comparison methods,
query generators, graph-format utilities, and small correctness tests.
It is a clean source snapshot: production-machine controllers, credentials,
historical logs, large datasets, and the full paper-result archive are not
included. The examples below are smoke tests, not the paper's full workload.

## Requirements and build

- Linux, a C++17 compiler and C compiler (tested with GCC).
- CMake 3.16 or newer.
- Python 3.9 or newer for test and query-conversion utilities; no third-party Python
  packages are required for the included tests.
- HPF 3.23, fetched from its official upstream with a pinned SHA-256 checksum.
  See [NOTICE.md](NOTICE.md) for its separate research-use license.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
(cd build && ctest --output-on-failure -j1)
```

For an offline build, obtain the official HPF source separately and configure:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DFD_FETCH_HPF=OFF -DFD_HPF_SOURCE=/path/to/pseudo.c
```

## Quick start

```bash
# Weighted example: the optimum is vertex 3, with cost 2.
./build/fdtool solve examples/tiny.fdg component

# Run the five comparison methods on the same small query.
for method in enum enum-prune jenum wenum wbb; do
  ./build/fdtool solve examples/tiny.fdg "$method"
done

# Convert a topology-only graph and solve an explicit I/R query.
mkdir -p work
./build/graph_convert txt-to-bin examples/tiny.txt work/tiny.bin
./build/fdsolveir work/tiny.bin examples/tiny.spec component
```

The topology-only example has uniform costs; its optimum value is 1, unlike
the weighted FDG example above.

## Methods

| Paper name | Command-line method | Implementation |
|---|---|---|
| FDCut | `component` | FD compilation, exact reduction, HPF cut and recovery |
| Brute | `enum` | Subset enumeration with original-DAG feasibility checks |
| Prune | `enum-prune` | Enumeration with incumbent cost pruning |
| ListJ | `jenum` | Enumeration using the Jeong et al. constrained constructor |
| ListW | `wenum` | Enumeration using the Wienöbst et al. constrained constructor |
| PruneW | `wbb` | Linear-constructor feasibility and cost branch-and-bound |

`base` and `sweep` are additional FDCut-family configurations available through
`fdtool`. The five comparison methods do not call FDCut's compiler or HPF.
They preprocess the candidate set before searching, while feasibility checks
continue to use the complete original DAG. The Jeong constructor rebuilds its
moral graph; this is not an incremental port of another implementation.

## Input and output

Vertex IDs are contiguous and zero-based. The complete original DAG is retained
even for vertices that cannot be selected.

**FDG text** (`fdtool solve`): first `n m`, then `n` rows of `marks cost`, then
`m` directed edges `u v`. Marks are bit flags: `X=1`, `Y=2`, `I=4`, `R=8`.
Required vertices therefore have mark `12`; unselectable vertices have mark
`0`. See `examples/tiny.fdg`.

**Topology text** (`graph_convert`): first `n m`, then `m` rows `u v`.
**Topology binary**: eight bytes `FDGBIN01`, little-endian unsigned 64-bit
`n,m`, then unsigned 64-bit endpoint pairs. See [docs/FORMATS.md](docs/FORMATS.md)
for explicit and compact query specifications.

The returned JSON includes a `query` object with `feasible`, `complete`,
`proven_optimal`, `value`, `selected`, and timing/search statistics.
An incomplete run is not a proof of infeasibility or optimality. For a
successful optimum, check all three flags; a complete infeasible result has
`feasible=false`.

## E1, E2 and E3 workloads

- **E1**: empty `I`; `R` contains every nonendpoint vertex.
- **E2**: stratify `|I|` over 1 through 8 and use
  `k = |R| / |I|` in `{16, 32, 64, 128, 256}`. Twenty endpoint pairs produce
  twenty queries per ratio when every ratio fits. Small graphs cap ratios at
  their actual capacity, so the output may contain fewer than 100 rows and
  additional nonstandard ratios.
- **E3**: the general witness generator fixes `|I|=8`, with `R` occupying
  10%, 20%, 30%, 40%, and 50% of the **nonendpoint** vertices. It requires a
  suitable witness and graph size; it does not manufacture missing feasible
  pairs or silently relax `I` for tiny graphs.

Within each accepted endpoint pair, `I` is fixed and the five `R` sets are
nested. Generation certifies the witness on the original DAG. Structural
validators do not replace this feasibility check.

```bash
# Supply a BIN graph and enough candidate endpoints for the generators.
# Endpoints are JSONL rows containing integer x and y fields.
./build/fdratioe2 graph.bin endpoints.jsonl e2.jsonl 20260926
python3 scripts/validate_ratio_e2.py e2.jsonl graph.bin 20260926

./build/fde3gen graph.bin endpoints.jsonl e3.jsonl 20260925
python3 scripts/validate_witness_e3.py e3.jsonl

# Convert one generated JSONL query into the solver's text specification.
python3 scripts/query_to_spec.py e2.jsonl 0 work/e2-q0.spec
./build/fdsolveir graph.bin work/e2-q0.spec component

python3 scripts/query_to_spec.py e3.jsonl 0 work/e3-q0.spec
./build/fdsolveir graph.bin work/e3-q0.spec component compact

# E1 on a topology-only graph, singleton endpoints, uniform cost 1.
./build/fdtool solve-bin-full graph.bin 0 2 1 component
```

Generator failures preserve checkpoints and must not be counted as completed
workloads. `fdfirsthop` is an alternative **guided** E2 sampler; its changed
sampling distribution is recorded in its output tag. It is not interchangeable
with the general witness sampler. See [docs/WORKLOADS.md](docs/WORKLOADS.md).

## Timing, limits and parallel execution

- `query.total_s` is algorithm time on an already loaded query. It includes
  preprocessing and, for FDCut, compilation, reduction, network construction,
  cut computation and solution recovery.
- `read_s` is separate. Independent post-solution validation is outside the
  algorithm timer. `fdsolveir` additionally reports `validation_s`.
- Validation is enabled by default. `FD_SKIP_POSTVALIDATION=1` disables only
  the additional post-solution check, not the algorithm's feasibility work.
- `FD_TIMING_MARKERS=1` makes `fdsolveir` emit algorithm start/end monotonic
  timestamps on stderr, for external budget accounting.
- Enumeration limits for `fdtool solve` and `solve-bin-full` are optional
  trailing `MAX_STATES TIMEOUT_MS` arguments. Zero means unlimited. These
  limits are soft: a long constructor invocation may exceed the deadline.
  `fdsolveir` uses a seven-day soft enumeration deadline.
- Paper-level E2/E3 budgets are **200,000 accumulated algorithm seconds per
  method/dataset/level**, not 200,000 seconds for each query or each worker.
  The executables do not themselves enforce an aggregate multi-process
  budget. The machine-specific controllers are not part of this release;
  reproducing those stopping rules requires an external supervisor.
- Solvers run single-threaded. HPF has process-global state: use separate
  processes for concurrent queries, never concurrent calls in one process.
  Record machine/load and memory limits for timing comparisons.
- Do not convert OOM, interruption or user-inferred cutoffs into measured
  timeout results. Keep these statuses separate when aggregating results.

Graphs are processed in memory. Large graphs may require substantial RAM,
and graph readers accepting 64-bit IDs do not remove backend index limits.
Use `./build/fdtool capacity NODES EDGES` to inspect static input limits.

## Repository layout

```text
include/    Public solver and graph-I/O interfaces
src/        FDCut, comparison methods, kernels and HPF adapter
tools/      Query generators and graph/IR input utilities
scripts/    Current query validators/conversion and raw-data preprocessing
tests/      Independent small-DAG oracle and regression tests
  helpers/  Synthetic integration-test workloads and short-timeout runner
examples/   Small, self-contained inputs
docs/       Formats, workload semantics and source checksums
```

## Current-version boundaries

The default build contains only the current workload entry points: `fdratioe2`
for ratio E2, `fde3gen` for witness E3, and the explicitly tagged alternative
`fdfirsthop` sampler. E2 always uses `k = |R| / |I|`. The E3 JSONL `k` field is
auxiliary `|R|-8` metadata; E3 levels are selected by `percent`, not by this field.
The guided E2 sampler changes the sampling distribution and must be reported
separately rather than silently mixed with general-witness queries.

The obsolete difference-based E2 generator and validator are retained only in
Git history. Current C++ tools share `tools/query_common.hpp`; current Python
validators share `scripts/query_encoding.py`. Neither shared module contains
an old workload generator or validator.

`tests/helpers/` contains synthetic integration-test helpers, not experiment
controllers. Their tiny candidate pools and millisecond timeouts are test
settings, not the paper workload or per-level time budget. The Python tests
are not invoked by the C++ solvers and do not enter algorithm timings.

## Data, scope and licensing

Full benchmark graphs and measured results are not bundled. Use only datasets
you are entitled to redistribute; the synthetic smoke tests are self-contained.
The source snapshot does not imply that rerunning a generator reproduces every
paper query: historical seeds, guided-sampling variants, small-graph exceptions
and selected workload files must also match.

This release does not adopt a blanket open-source license for the research
code. Third-party notices remain applicable. See [NOTICE.md](NOTICE.md).
Source checksums are recorded in `docs/SOURCE_SHA256SUMS`.

For double-anonymous review, share an inspected anonymized mirror rather than
the account-bearing GitHub URL. An anonymous mirror does not remove information
from an already public source repository.
