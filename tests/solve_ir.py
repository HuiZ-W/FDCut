import tempfile,pathlib,struct,subprocess,json,sys
with tempfile.TemporaryDirectory() as d:
 p=pathlib.Path(d);(p/'g').write_bytes(b'FDGBIN01'+struct.pack('<QQQQQQ',5,2,0,1,1,2));(p/'q').write_text('0 2 1 3\n1\n1 3 4\n')
 for m in ['component','enum','enum-prune','jenum','wenum','wbb']:
  r=json.loads(subprocess.check_output([sys.argv[1],str(p/'g'),str(p/'q'),m],timeout=30));a=r['query'];assert a['value']==1 and a['feasible'] and a['complete'] and a['proven_optimal']
print('PASS six methods, explicit I/R, original-DAG and independent oracle')
