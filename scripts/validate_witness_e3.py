"""Validate compact E3 structure; feasibility certified by original-DAG generator.

R = anchor + Feistel permutation of all nonendpoints with anchor skipped.
Bijectivity plus prefix length proves cardinality/nesting without O(n) expansion.
"""
import collections, hashlib, json, pathlib, sys
from validate_ir_queries import permuted_rank, vertex_from_rank

def expand(row):
    rep=row['r_representation']; anchor=rep['anchor']; seen=set(anchor)
    r=list(anchor)
    for pos in range(row['universe_size']):
        if len(r)==row['r_size']:break
        v=vertex_from_rank(permuted_rank(pos,row['universe_size'],rep['seed']),row['x'],row['y'])
        if v not in seen:r.append(v)
    assert len(r)==row['r_size']
    return r

def validate(path):
    ids=set();pairs=set();count=0
    groups=collections.defaultdict(list)
    for line in path.open():
        r=json.loads(line);count+=1
        assert r['query_id'] not in ids;ids.add(r['query_id']);pairs.add((r['x'],r['y']))
        assert r['sampling']=='paired-wienoebst-witness-e3-v1'
        assert r['feasible'] is True and r['nonempty'] is True
        n=r['universe_size']+2; rep=r['r_representation']; a=rep['anchor']
        assert 0<=r['x']<n and 0<=r['y']<n and r['x']!=r['y']
        assert len(r['i'])==len(set(r['i']))==r['i_size']==8
        assert rep['type']=='witness-prefix-feistel-skip-v1'
        assert len(a)==len(set(a)) and set(r['i'])<=set(a)
        assert all(0<=v<n and v not in (r['x'],r['y']) for v in a)
        assert len(a)<=r['r_size']==rep['prefix_size']==(n-2)*r['percent']//100<=n-2
        assert r['k']==r['r_size']-8
        signature=(r['x'],r['y'],r['i'],r['universe_size'],rep['seed'],hashlib.sha256(json.dumps(a).encode()).hexdigest())
        expanded=set(expand(r)) if n<10000 else None
        if expanded is not None:assert len(expanded)==r['r_size'] and set(a)<=expanded
        groups[r['pair_id']].append((r['percent'],r['query_id'],signature,expanded))
    assert count==100 and ids==set(range(100))
    assert set(groups)==set(range(20)) and len(pairs)==20
    for pair,group in groups.items():
        group.sort(key=lambda r:r[0])
        assert [r[0] for r in group]==[10,20,30,40,50]
        for j,r in enumerate(group):
            assert r[1]==pair*5+j and r[2]==group[0][2]
        if group[0][3] is not None:
            sets=[r[3] for r in group]
            assert all(a<=b for a,b in zip(sets,sets[1:]))
    sha=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(1048576),b''):sha.update(block)
    return dict(rows=100,pairs=20,sha256=sha.hexdigest(),validation='structure and compact bijection; original-DAG fixed witness certified during generation')

if __name__=='__main__':
    p=pathlib.Path(sys.argv[1]); result=validate(p)
    p.with_suffix('.validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))
