"""Paper-name spelling variants agree; obsolete CLI aliases are rejected."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

tool, ir = sys.argv[1:]
root = Path(__file__).resolve().parents[1]
names = [('FDCut', 'component'), ('Brute', 'enum'), ('Prune', 'enum-prune'),
         ('ListJ', 'jenum'), ('ListW', 'wenum'), ('PruneW', 'wbb')]
keys = ('feasible', 'complete', 'proven_optimal', 'value', 'selected')

def run(args):
    return json.loads(subprocess.check_output(args, text=True, timeout=30))

for binary, args in ((tool, ['--help']), (ir, ['--help'])):
    output = subprocess.run([binary, *args], capture_output=True, text=True, check=True)
    assert all(paper in output.stdout + output.stderr for paper, _ in names)

with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    graph = p / 'g.bin'
    graph.write_bytes(b'FDGBIN01' + struct.pack('<QQQQQQ', 5, 2, 0, 1, 1, 2))
    (p / 'explicit').write_text('0 2 1 3\n1\n1 3 4\n')
    (p / 'compact').write_text('0 2 1 3\n1\n42 1\n1\n')
    factories = [
        lambda m: [tool, 'solve', str(root / 'examples/tiny.fdg'), m],
        lambda m: [tool, 'solve-bin-full', str(graph), '0', '2', '1', m],
        lambda m: [ir, str(graph), str(p / 'explicit'), m],
        lambda m: [ir, str(graph), str(p / 'compact'), m, 'compact'],
    ]
    for paper, legacy in names:
        for factory in factories:
            answers = []
            for supplied in (paper, paper.lower(), paper.upper()):
                answer = run(factory(supplied))['query']
                assert answer['method'] == paper
                assert all(answer[k] for k in ('feasible', 'complete', 'proven_optimal'))
                answers.append({k: answer[k] for k in keys})
            assert all(answer == answers[0] for answer in answers)
        if paper != 'FDCut':
            for command in (factories[0], factories[1]):
                a = run(command(paper) + ['1', '0'])['query']
                b = run(command(paper.lower()) + ['1', '0'])['query']
                assert {k: a[k] for k in keys} == {k: b[k] for k in keys}

    (p / 'queries').write_text('{"query_id":0,"x":0,"y":2,"cost":1}\n')
    for supplied in ('FDCut', 'fdcut', 'FDCUT'):
        label = 'FDCut'
        out = p / f'batch-{supplied}'
        summary = run([tool, 'solve-bin-batch', str(graph), str(p / 'queries'), str(out), supplied])
        assert summary['method'] == label
        assert json.loads(out.read_text())['query']['method'] == label
    for factory in factories:
        for invalid in ['not-a-method'] + [legacy for _, legacy in names]:
            failed = subprocess.run(factory(invalid), capture_output=True, text=True)
            assert failed.returncode != 0 and 'unknown method' in failed.stderr
print('PASS paper names, case handling, rejected legacy aliases, limits, explicit/compact I/R and batch')
