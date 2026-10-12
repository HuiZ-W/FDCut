# Release validation

The clean publication snapshot was built and checked on Linux on 2026-10-12
with GCC 12.4.0 (C), G++ 13.3.0 (C++), and Python 3.12.3.

All **14 CTest cases passed** in a sequential run:

1. Native solver self-test.
2. IR query-generator self-test.
3. Enumeration limits and preprocessing regressions.
4. Independent small-DAG front-door oracle comparisons.
5. Input capacity checks.
6. Synthetic Benchmark12 workload integration.
7. Explicit I/R query reader across six methods.
8. Compact/explicit query-reader equivalence across six methods.
9. Graph-format conversions.
10. Large-graph source parsing and failure handling on small fixtures.
11. Synthetic DAG-generator fixtures.
12. E2 ratio, nesting, small-graph capacity and replay checks.
13. E3 witness structure, compact reconstruction and replay checks.
14. JSONL-to-spec conversion and duplicate/overwrite guards.

Additional checks exercised the README examples with all six methods and the
IR solver's timing markers and post-validation switch. The solver sources were
byte-compared to the experiment-host source snapshot; packaging did not change
the algorithm kernels. A filename/content identity scan and Markdown local-link
check were also performed.

These tests do not constitute a rerun of the full benchmark, a formal proof of
correctness, or a guarantee that no identifying information remains. The
account-bearing hosting URL is not anonymous. Large workloads, exact query
snapshots and machine-specific timing/cutoff controllers are separate from
this source release.
