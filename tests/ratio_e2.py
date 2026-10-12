import json,pathlib,struct,subprocess,sys,tempfile
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
from validate_ratio_e2 import validate
with tempfile.TemporaryDirectory() as tmp:
    p=pathlib.Path(tmp)
    for n in (71,300,2100):
        edges=[(x,v) for x in range(5) for v in range(10,18)]+[(v,y) for v in range(10,18) for y in range(20,25)]
        (p/'g').write_bytes(b'FDGBIN01'+struct.pack('<QQ',n,len(edges))+b''.join(struct.pack('<QQ',*e) for e in edges))
        (p/'ep').write_text(''.join(json.dumps(dict(x=x,y=y))+'\n' for x in range(5) for y in range(20,25)))
        out=p/str(n);cmd=[sys.argv[1],str(p/'g'),str(p/'ep'),str(out),'20260926']
        subprocess.run(cmd,check=True,timeout=180,capture_output=True)
        report=validate(out,n,20260926);data=out.read_bytes()
        for line in data.splitlines():assert set(range(10,18))<=set(json.loads(line)['r'])
        if n>=2050:assert report['rows']==100 and set(report['k_counts'].values())=={20}
        out.unlink();subprocess.run(cmd,check=True,timeout=180,capture_output=True);assert out.read_bytes()==data
        print(n,report)
print('PASS exact ratio, small-graph maximum k, nesting, fixed witness, replay')
