import json,pathlib,struct,subprocess,sys,tempfile
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
from validate_witness_e3 import validate,expand
with tempfile.TemporaryDirectory() as tmp:
    p=pathlib.Path(tmp);n=300
    edges=[(x,v) for x in range(5) for v in range(10,18)]+[(v,y) for v in range(10,18) for y in range(20,25)]
    (p/'g').write_bytes(b'FDGBIN01'+struct.pack('<QQ',n,len(edges))+b''.join(struct.pack('<QQ',*e) for e in edges))
    (p/'ep').write_text(''.join(json.dumps(dict(x=x,y=y))+'\n' for x in range(5) for y in range(20,25)))
    cmd=[sys.argv[1],str(p/'g'),str(p/'ep'),str(p/'out'),'20260925']
    subprocess.run(cmd,check=True,timeout=120)
    validate(p/'out');data=(p/'out').read_bytes()
    for line in data.splitlines():
        r=json.loads(line);assert set(range(10,18))<=set(expand(r))
    (p/'out').unlink();subprocess.run(cmd,check=True,timeout=120)
    assert data==(p/'out').read_bytes()
print('PASS E3 100 rows, exact fixture witness, compact reconstruction, nesting, resume')
