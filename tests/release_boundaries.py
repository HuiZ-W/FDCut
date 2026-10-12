"""Prevent obsolete workload implementations from leaking into current tools."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
for name in ('tools/generate_ir_queries.cpp', 'scripts/validate_ir_queries.py',
             'scripts/benchmark12_workloads.py', 'scripts/run_benchmark12_workloads.py'):
    assert not (root / name).exists(), name
for directory in ('scripts', 'tools'):
    for path in (root / directory).iterdir():
        if path.suffix not in ('.py', '.cpp', '.hpp'):
            continue
        content = path.read_text()
        assert 'validate_ir_queries' not in content, path
        assert 'generate_ir_queries.cpp' not in content, path
        assert '#define main' not in content, path
assert 'fdquerygen' not in (root / 'CMakeLists.txt').read_text()
sys.path.insert(0, str(root / 'scripts'))
from query_encoding import mix, permuted_rank, vertex_from_rank
assert mix(0) == 0xe220a8397b1dcdaf
for n in range(1, 80):
    for seed in (0, 1, 20260925):
        assert sorted(permuted_rank(i, n, seed) for i in range(n)) == list(range(n))
for n in range(3, 15):
    for x in range(n):
        for y in range(n):
            if x != y:
                assert [vertex_from_rank(i, x, y) for i in range(n - 2)] == [v for v in range(n) if v not in (x, y)]
print('PASS current-version boundaries and shared encoding')
