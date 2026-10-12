# Workload generation

E1, E2 and E3 are synthetic query workloads over a supplied DAG, not estimates
of causal effects in real observations. Keep the original topology fixed.

## E1

`fdtool sample-bin-full GRAPH OUTPUT COUNT SEED MAX_ATTEMPTS COST`
generates feasible endpoint queries. Optional modes include `root-x` and
`random-pair`; they have different distributions and must be reported.
See the CLI source for the exact mode semantics. Generation is feasibility
screening; optimization is performed when the queries are solved.

## E2

`fdratioe2 GRAPH ENDPOINTS OUTPUT SEED` reads endpoint candidates from JSONL
(`x`, `y`). It seeks 20 unique feasible endpoint pairs. Its shuffled required
size schedule contains three pairs each for sizes 1, 3, 5, 7 and two pairs each
for 2, 4, 6, 8. A full five-level workload contains 100 rows.

For each pair, `cap = min(256, floor((n-2)/|I|))`. Keep standard ratios no
larger than `cap`, then append `cap` if necessary. Thus small-graph row counts
and ratio values vary; group by the stored `k=|R|/|I|`, not by query ID modulo
5, and do not call a capped ratio 256. IDs may be noncontiguous.

`fdfirsthop GRAPH OUTPUT SEED ATTEMPTS SECONDS` is an alternative guided
sampler for large graphs (`n >= 2050`). It uses a treatment's outgoing
neighbors as a small allowed pool, constructs a witness on the complete
original DAG, selects required vertices from the witness, and expands the
allowed pool to the five ratios. It checks the fixed witness again. Its tag
is `paired-firsthop-witness-ratio-e2-v2`; it changes the query distribution.
Validate a completed output with:

```bash
python3 scripts/validate_ratio_e2.py queries.jsonl graph.bin SEED \
  paired-firsthop-witness-ratio-e2-v2
```

No generator guarantees that an arbitrary graph contains enough feasible
pairs. Incomplete first-hop generation exits nonzero and retains `.parts`;
its attempt/time limits include reading the graph and are soft between checks.

## E3

`fde3gen GRAPH ENDPOINTS OUTPUT SEED` fixes eight required variables and
produces five nested allowed sets per accepted endpoint pair. Sizes are
`floor((n-2)*percent/100)` for percent in 10, 20, 30, 40, 50. The witness
must fit the smallest level. The compact representation anchors the witness
and then extends it with a seeded permutation, skipping the anchor.

The legacy `k` field in these E3 rows is `r_size-8`, **not** the E2 ratio.
Group E3 by `percent`, and E2 by its ratio `k`.

The included general E3 generator rejects graphs smaller than 82 vertices.
Special tiny-graph repeated/capped workloads used in exploratory experiments
are not implicitly reproduced by this command.

## Validation and interpretation

Generation checks fixed-witness feasibility on the original DAG; small inputs
also use the independent FD oracle. Python validators check IDs, pairing,
sizes, nesting and SHA-256. Large compact E3 sets are checked structurally
without expanding every set. That is not an independent large-graph
feasibility proof.

Do not pool different samplers, seeds, repeats or capped levels without
preserving their labels. The publication code does not bundle every historical
query snapshot or reproduce machine-specific cutoff decisions. Record those
inputs and decisions separately when comparing against published figures.
