import sys,pathlib,tempfile,struct,subprocess,json
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
from query_encoding import permuted_rank,vertex_from_rank
with tempfile.TemporaryDirectory() as d:
 p=pathlib.Path(d);(p/'g').write_bytes(b'FDGBIN01'+struct.pack('<QQQQQQ',12,2,0,1,1,2))
 for anchor in ([],[1,5,8]):
  seed=987654321;n=10;r=list(anchor)
  for j in range(n):
   v=vertex_from_rank(permuted_rank(j,n,seed),0,2)
   if v not in r:r.append(v)
  # Include mediator 1 and exercise prefix stopping and anchor skipping.
  nr=max(6,r.index(1)+1);r=r[:nr]
  (p/'explicit').write_text(f'0 2 1 {nr}\n1\n'+' '.join(map(str,r)))
  (p/'compact').write_text(f'0 2 1 {nr}\n1\n{seed} {len(anchor)}\n'+' '.join(map(str,anchor)))
  for m in ['FDCut','Brute','Prune','ListJ','ListW','PruneW']:
   def solve(name,extra):return json.loads(subprocess.check_output([sys.argv[1],str(p/'g'),str(p/name),m]+extra,timeout=30))['query']
   a=solve('explicit',[]);b=solve('compact',['compact'])
   assert a['value']==b['value']==1 and all(b[k] for k in ['feasible','complete','proven_optimal'])
print('PASS compact/explicit equivalence: both encodings, six methods, independent oracle')
