import collections,hashlib,json,pathlib,struct,sys
from query_encoding import mix

def levels(n,i):
    cap=min(256,(n-2)//i)
    ks=[k for k in [16,32,64,128,256] if k<=cap]
    if not ks or ks[-1]!=cap:ks.append(cap)
    return ks

def schedule(seed):
    a=[1,2,3,4,5,6,7,8,1,2,3,4,5,6,7,8,1,3,5,7]
    for size in range(len(a),1,-1):
        j=mix(seed+size)%size;a[size-1],a[j]=a[j],a[size-1]
    return a

def validate(p,n,seed,sampling='paired-wienoebst-witness-ratio-e2-v1'):
    rows=[json.loads(s) for s in p.read_text().splitlines()];sizes=schedule(seed)
    assert len(rows)==sum(len(levels(n,i)) for i in sizes)
    ids=set();pairs=set();groups=collections.defaultdict(list)
    for r in rows:
        assert r['sampling']==sampling
        assert r['k_definition']=='r_size/i_size' and r['seed']==seed
        assert r['feasible'] is True and r['nonempty'] is True
        assert r['query_id'] not in ids;ids.add(r['query_id'])
        assert 0<=r['pair_id']<20 and r['i_size']==sizes[r['pair_id']]
        assert len(set(r['i']))==len(r['i'])==r['i_size']
        assert len(set(r['r']))==len(r['r'])==r['r_size']==r['i_size']*r['k']
        assert set(r['i'])<=set(r['r'])
        assert 0<=r['x']<n and 0<=r['y']<n and r['x']!=r['y']
        assert all(0<=v<n and v not in (r['x'],r['y']) for v in r['r'])
        groups[r['pair_id']].append(r);pairs.add((r['x'],r['y']))
    assert len(groups)==len(pairs)==20
    for slot,group in groups.items():
        group.sort(key=lambda r:r['k']);assert [r['k'] for r in group]==levels(n,sizes[slot])
        for j,r in enumerate(group):
            assert r['query_id']==slot*5+j
            assert (r['x'],r['y'],r['i'])==(group[0]['x'],group[0]['y'],group[0]['i'])
        for a,b in zip(group,group[1:]):assert b['r'][:len(a['r'])]==a['r']
    return dict(rows=len(rows),pairs=20,k_definition='r_size/i_size',k_counts=dict(collections.Counter(r['k'] for r in rows)),sha256=hashlib.sha256(p.read_bytes()).hexdigest())

if __name__=='__main__':
    p=pathlib.Path(sys.argv[1])
    with open(sys.argv[2],'rb') as f:
        assert f.read(8)==b'FDGBIN01';n=struct.unpack('<Q',f.read(8))[0]
    result=validate(p,n,int(sys.argv[3]),sys.argv[4] if len(sys.argv)>4 else 'paired-wienoebst-witness-ratio-e2-v1');p.with_suffix('.validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
