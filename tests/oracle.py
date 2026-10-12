"""Independent stdlib-only original-DAG FD oracle and exact subset enumeration."""
import itertools
import json
from pathlib import Path
import random
import subprocess
import sys
import tempfile


def separated(n, edges, a, b, given):
    parents = [set() for _ in range(n)]
    for u, v in edges:
        parents[v].add(u)
    ancestor = set(a) | set(b) | set(given)
    todo = list(ancestor)
    for v in todo:
        for p in parents[v] - ancestor:
            ancestor.add(p)
            todo.append(p)
    moral = [set() for _ in range(n)]
    for v in ancestor:
        for u in parents[v]:
            moral[u].add(v)
            moral[v].add(u)
        for u, w in itertools.combinations(parents[v], 2):
            moral[u].add(w)
            moral[w].add(u)
    seen = set(a) - set(given)
    todo = list(seen)
    for v in todo:
        if v in b:
            return False
        for w in moral[v] - seen - set(given):
            seen.add(w)
            todo.append(w)
    return True


def fd(n, edges, x, y, z):
    seen = set(x)
    todo = list(x)
    children = [set() for _ in range(n)]
    for u, v in edges:
        children[u].add(v)
    for v in todo:
        if v in y:
            return False
        for w in children[v] - seen - z:
            seen.add(w)
            todo.append(w)
    return separated(n, [(u,v) for u,v in edges if u not in x], x, z, set()) and separated(n, [(u,v) for u,v in edges if u not in z], z, y, x)


def main(binary):
    rng = random.Random(20260914)
    cases = []
    # Exhaust all labeled topological DAGs on four nodes: varied I/R and zero costs.
    arcs = list(itertools.combinations(range(4), 2))
    for bits in range(1 << len(arcs)):
        edges = [e for j,e in enumerate(arcs) if bits >> j & 1]
        for allowed in [{1,2}, {1}, set()]:
            for required in [set(), set(allowed)]:
                cases.append((4, edges, {0}, {3}, required, allowed, [0, 0, 7, 0]))
    # Arbitrary labels/order of topological nodes, multi-X/Y and latent nodes.
    for _ in range(220):
        n = rng.randrange(3, 9)
        order = list(range(n)); rng.shuffle(order)
        edges = [(order[i],order[j]) for i in range(n) for j in range(i+1,n) if rng.random() < .3]
        ids = list(range(n));rng.shuffle(ids)
        nx = rng.randint(1, min(2,n-1));ny = rng.randint(1,min(2,n-nx))
        x,y = set(ids[:nx]),set(ids[nx:nx+ny])
        allowed = {v for v in set(range(n))-x-y if rng.random()<.85}
        required = {v for v in allowed if rng.random()<.2}
        weights = [rng.randrange(11) for _ in range(n)]
        cases.append((n,edges,x,y,required,allowed,weights))
    # Nonempty FD and required redundancy: hidden U -> X,Y; X -> a -> b -> Y.
    for weights in [[0,0,0,2,5], [0,0,0,0,0], [0,0,0,9,1]]:
        for required in [set(),{3},{4},{3,4}]:
            cases.append((5,[(2,0),(2,1),(0,3),(3,4),(4,1)],{0},{1},required,{3,4},weights))
    feasible = 0
    # Phi must retain required nodes outside An(Y), including zero-cost nodes.
    for required in [set(), {3}, {4}, {3, 4}]:
        cases.append((5, [(0, 2), (2, 1), (0, 3)], {0}, {1},
                      required, {2, 3, 4}, [0, 0, 1, 0, 2]))
    # An allowed ancestor with an open backdoor path cannot be selected;
    # requiring it must report infeasible, not drop the requirement.
    for required in [set(), {2}]:
        cases.append((4, [(2, 0), (2, 1), (0, 3), (3, 1)], {0}, {1},
                      required, {2, 3}, [0, 0, 0, 1]))
    with tempfile.TemporaryDirectory(prefix='fd-oracle-') as directory:
        path = Path(directory)/'query.fdg'
        for n,edges,x,y,required,allowed,weights in cases:
            choices = list(allowed-required);best = None
            for bits in range(1<<len(choices)):
                z=required|{v for j,v in enumerate(choices) if bits>>j&1}
                if fd(n,edges,x,y,z):
                    c=sum(weights[v] for v in z)
                    best=c if best is None else min(best,c)
            marks=[int(v in x)+2*int(v in y)+4*int(v in required)+8*int(v in allowed) for v in range(n)]
            path.write_text(f'{n} {len(edges)}\n'+''.join(f'{m} {w}\n' for m,w in zip(marks,weights))+''.join(f'{u} {v}\n' for u,v in edges))
            methods = ['base','sweep','FDCut','Brute','Prune',
                       'ListJ','ListW','PruneW']
            for method in methods:
                run=subprocess.run([binary,'solve',str(path),method],capture_output=True,text=True,check=True)
                ans=json.loads(run.stdout)['query']
                assert ans['value']==best,(n,edges,method,ans,best)
                assert ans['feasible']==(best is not None)
                if best is not None:
                    feasible+=1;z=set(ans['selected'])
                    assert required<=z<=allowed and fd(n,edges,x,y,z)
                    assert sum(weights[v] for v in z)==best
        for text in ['2 0\n1 0\n2 -1\n', '2 2\n1 0\n2 0\n0 1\n1 0\n', '2 1\n1 0\n2 0\n0 8\n', '2 0\n1 0\n3 0\n', '9223372036854775807 9223372036854775807\n']:
            path.write_text(text)
            assert subprocess.run([binary,'solve',str(path),'base'],capture_output=True).returncode!=0
    print(json.dumps(dict(status='PASS',queries=len(cases),solver_configs=8*len(cases),feasible_configs=feasible,invalid_inputs_rejected=5)))


if __name__ == '__main__':
    main(sys.argv[1])
