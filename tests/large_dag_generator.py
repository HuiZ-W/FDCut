#!/usr/bin/env python3
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory() as t:
    root = Path(t)
    for family in ['uniform', 'hub']:
        for n,m,layers in [(1000,6123,10),(10,0,2),(10,25,2),(5,4,5)]:
            dest = root / ('%s-%s-%s' % (family,n,m))
            subprocess.run([binary,family,str(n),str(m),str(layers),'20260914',str(dest),'1'],check=True)
            edges = list(struct.iter_unpack('<QQ',(dest/'edges.u64le').read_bytes()))
            assert len(edges)==m and len(set(edges))==m and edges==sorted(edges)
            w=n//layers
            assert all(0<=u<v<n and v//w==u//w+1 for u,v in edges)
            s=json.loads((dest/'stats.json').read_text())
            assert s['nodes']==n and s['edges']==m and s['synthetic'] and s['is_dag']
            second = dest.with_name(dest.name+'-repeat')
            subprocess.run([binary,family,str(n),str(m),str(layers),'20260914',str(second),'1'],check=True)
            assert (dest/'edges.u64le').read_bytes()==(second/'edges.u64le').read_bytes()
    for args in [('bad',10,5,2),('uniform',10,26,2),('uniform',11,3,2),('uniform',0,0,2),('hub',10,18446744073709551615,2)]:
        r=subprocess.run([binary,*map(str,args),'1',str(root/'invalid'),'1'],capture_output=True)
        assert r.returncode and not (root/'invalid').exists()
print('large DAG generator: topology, unique edges, counts, determinism, boundaries passed')
