"""Check pruning, exact completion, interrupted incumbents and input rejection."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

binary = sys.argv[1]
with tempfile.TemporaryDirectory() as d:
    p = Path(d) / 'q.fdg'
    # U->X,Y; X->a->b->Y. Exclude-first discovers cheap b before costly a.
    p.write_text('5 5\n1 0\n2 0\n0 0\n8 9\n8 1\n2 0\n2 1\n0 3\n3 4\n4 1\n')
    def run(method, *limits):
        return json.loads(subprocess.check_output([binary, 'solve', str(p), method, *map(str, limits)]))['query']
    full, pruned = run('Brute'), run('Prune')
    assert full['complete'] and pruned['complete']
    assert full['value'] == pruned['value'] == 1
    assert full['fd_checks'] == 4 and pruned['fd_checks'] < 4
    assert pruned['pruned_branches'] > 0
    for method in ['ListJ', 'ListW', 'PruneW']:
        answer = run(method)
        assert answer['complete'] and answer['proven_optimal']
        assert answer['value'] == 1 and answer['oracle_calls'] > 0
    for method in ['Brute', 'Prune']:
        a = run(method, 1, 0)
        assert not a['complete'] and not a['proven_optimal'] and a['status'] == 'state_limit'
        assert not a['feasible'] and a['value'] is None
        a = run(method, 4, 0)
        assert not a['complete'] and not a['proven_optimal'] and a['value'] == 1
        p.write_text('2 2\n1 0\n2 0\n0 1\n1 0\n')
        assert subprocess.run([binary, 'solve', str(p), method], capture_output=True).returncode == 2
        p.write_text('5 5\n1 0\n2 0\n0 0\n8 9\n8 1\n2 0\n2 1\n0 3\n3 4\n4 1\n')
    for method in ['ListJ', 'ListW']:
        a = run(method, 1, 0)
        assert not a['complete'] and not a['feasible']
        a = run(method, 4, 0)
        assert not a['complete'] and a['value'] == 1
    a = run('PruneW', 1, 0)
    assert not a['complete'] and a['feasible'] and a['value'] == 10
    assert not a['proven_optimal']
    # A wide candidate domain: a one-millisecond soft deadline must interrupt.
    n = 200
    # All optional nodes must survive Phi: isolated nodes are now removed.
    edges = [(0, 1), (0, 2)] + [(v, v+1) for v in range(2, n-1)] + [(n-1, 1)]
    p.write_text(f'{n} {len(edges)}\n1 0\n2 0\n' + '8 1\n'*(n-2) + ''.join(f'{u} {v}\n' for u,v in edges))
    for method in ['Brute', 'Prune']:
        a = run(method, 0, 1)
        assert a['status'] == 'timeout' and not a['complete'] and not a['proven_optimal']
    # Large R, but only two optional candidates in Phi. No induced subgraph:
    # the hidden common cause U is still needed by the independent validator.
    p.write_text('65 5\n1 0\n2 0\n0 0\n8 9\n8 1\n' + '8 0\n'*60
                 + '2 0\n2 1\n0 3\n3 4\n4 1\n')
    for method in ['Brute', 'Prune', 'ListJ', 'ListW', 'PruneW']:
        a = run(method, 100, 0)
        assert a['complete'] and a['proven_optimal'] and a['value'] == 1, (method, a)
        assert a['states'] <= 7, (method, a)
        if method == 'Brute': assert a['fd_checks'] == 4
    # Equal-cost zero incumbent: WBB prunes at the root, not after exploring
    # every zero-cost superset. Initialization must still return a witness.
    p.write_text('5 5\n1 0\n2 0\n0 0\n8 0\n8 0\n2 0\n2 1\n0 3\n3 4\n4 1\n')
    a = run('PruneW')
    assert a['complete'] and a['value'] == 0 and a['states'] == 1
    assert a['oracle_calls'] == 1 and a['pruned_branches'] == 1
print(json.dumps({'status': 'PASS', 'checks': 'pruning, zero-free limits, incumbent, DAG rejection, deadline'}))
