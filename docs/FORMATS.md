# Query formats

## Explicit specification for `fdsolveir`

```text
x y number_of_required_vertices number_of_allowed_vertices
required_vertex_ids
allowed_vertex_ids
```

All IDs are zero-based. Lists have no duplicates. Required vertices must be in
the allowed list, and neither endpoint may occur in either list. The solver
loads the complete topology; the specification does not induce a subgraph.
Costs are uniformly 1 in this interface. For arbitrary integer costs or
multiple treatment/outcome vertices, use FDG input with `fdtool solve`.

## Compact specification

Pass the extra argument `compact` to `fdsolveir`:

```text
x y number_of_required_vertices prefix_size
required_vertex_ids
permutation_seed anchor_count
anchor_vertex_ids
```

The allowed sequence starts with `anchor`. Append the seeded Feistel
permutation of all nonendpoint IDs, skipping IDs already in `anchor`, and
take `prefix_size` entries. Required IDs are explicit; do not assume they are
the first eight permutation entries. This is the representation tagged
`witness-prefix-feistel-skip-v1` by the E3 witness generator.

`scripts/query_to_spec.py` accepts explicit `r` arrays and this representation
only. Unknown encodings fail rather than silently being interpreted as an old
query format. It selects by the actual `query_id`, not the line number.

## Topology input

TXT: `n m` followed by `m` ordered endpoint pairs. BIN: `FDGBIN01`, followed
by `n m` and endpoint pairs, all little-endian unsigned 64-bit integers.
Use sorted unique directed edges, no self-loops and no directed cycles.
`graph_convert` checks file structure; graph-format validity alone is not a
proof that the graph is acyclic.

The solver's in-memory backend uses narrower indices and explicitly rejects
unsupported sizes. It never silently samples or truncates a large graph.
