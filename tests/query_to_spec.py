import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

script = Path(__file__).resolve().parents[1] / 'scripts' / 'query_to_spec.py'
with tempfile.TemporaryDirectory() as folder:
    p = Path(folder)
    (p/'g.bin').write_bytes(b'FDGBIN01' + struct.pack('<QQQQQQ', 5, 2, 0, 1, 1, 2))
    common = dict(query_id=7, x=0, y=2, i=[1], i_size=1, r_size=3)
    explicit = dict(common, r=[1, 3, 4])
    compact = dict(common, universe_size=3, r_representation=dict(
        type='witness-prefix-feistel-skip-v1', anchor=[1], prefix_size=3, seed=42))
    for name, row, extra in [('explicit', explicit, []), ('compact', compact, ['compact'])]:
        (p/'q.jsonl').write_text(json.dumps(row)+'\n')
        command = [sys.executable, str(script), str(p/'q.jsonl'), '7', str(p/name)]
        subprocess.run(command, check=True)
        answer = json.loads(subprocess.check_output([sys.argv[1], str(p/'g.bin'), str(p/name), 'component']+extra))['query']
        assert answer['value'] == 1
        assert all(answer[k] for k in ['feasible', 'complete', 'proven_optimal'])
        assert subprocess.run(command, capture_output=True).returncode != 0
    (p/'q.jsonl').write_text(json.dumps(explicit)+'\n'+json.dumps(explicit)+'\n')
    assert subprocess.run([sys.executable, str(script), str(p/'q.jsonl'), '7', str(p/'duplicate')], capture_output=True).returncode != 0
    assert not (p/'duplicate').exists()
print('PASS query export, explicit/compact solve, overwrite and duplicate guards')
