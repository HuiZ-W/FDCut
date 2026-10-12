#!/usr/bin/env python3
"""Check source semantics, binary round trip, cyclic inputs, and failed publication."""
import bz2
import json
import random
from pathlib import Path
import sqlite3
import struct
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
packer = Path(sys.argv[1]).resolve()
prepare = root / 'scripts/prepare_large_graph.py'
with tempfile.TemporaryDirectory() as td:
    d = Path(td)
    def run(kind, source, name, good=True):
        out = d / name
        r = subprocess.run([sys.executable, str(prepare), kind, str(source), str(out), '--packer', str(packer), '--source-url', 'fixture://test'], capture_output=True, text=True)
        if not good:
            assert r.returncode != 0 and not out.exists(), r.stdout + r.stderr
            return
        assert r.returncode == 0, r.stderr
        names = [json.loads(x) for x in (out / 'nodes.jsonl').read_text().splitlines()]
        edges = {(names[u], names[v]) for u, v in struct.iter_unpack('<QQ', (out / 'edges.u64le').read_bytes())}
        return edges, json.loads((out / 'stats.json').read_text())
    source = d / 'cause.jsonl.bz2'
    pairs = [('a\t中', 'b'), ('b', 'c'), ('c', 'b'), ('a\t中', 'b'), ('d', 'd')]
    with bz2.open(source, 'wt') as f:
        for a, b in pairs:
            f.write(json.dumps({'causal_relation': {'cause': {'concept': a}, 'effect': {'concept': b}}}) + '\n')
    es, s = run('causenet', source, 'cause')
    assert es == set(pairs) and s['nodes'] == 4 and s['edges'] == 4
    assert not s['is_dag'] and s['self_loops_retained'] == 1 and s['weak_components'] == 2
    assert s['largest_weak_component_nodes'] == 3 and s['kahn_removed_nodes'] == 1
    assert s['strong_components'] == 3 and s['largest_strong_component_nodes'] == 2 and s['nodes_on_directed_cycles'] == 3
    sql = d / 'KG.db'
    with sqlite3.connect(sql) as db:
        db.execute('CREATE TABLE Relations(hid TEXT,tid TEXT,Reason REAL,Result REAL,Condition REAL)')
        db.executemany('INSERT INTO Relations VALUES(?,?,?,?,?)', [('a','b',2,0,0), ('b','c',0,3,0), ('x','y',0,0,4), ('a','b',1,0,0)])
    es, s = run('aser', sql, 'aser')
    assert es == {('b','a'),('b','c')} and s['is_dag'] and s['nodes'] == 3
    tsv = d / 'causal.tsv'
    tsv.write_text('a\tb\t10\nb\tc\t2\n')
    es, s = run('causalnet', tsv, 'causal')
    assert es == {('a','b'),('b','c')} and s['is_dag']
    ceg = d / 'ceg.txt'
    ceg.write_text('rain->flood\t40\t0.3\t1.4\nflood->damage\t2\t0\t0.2\nfuture->cap->lock\t11\t0\t0\n')
    es, s = run('ceg', ceg, 'ceg')
    assert es == {('rain','flood'),('flood','damage')} and s['is_dag']
    assert json.loads((d / 'ceg/provenance.json').read_text())['quarantined_records'] == 1
    ceg.write_text('rain->flood\t40\tNaN\t0.2\n')
    run('ceg', ceg, 'bad-ceg', False)
    import hashlib
    chunks = d / 'chunks'
    chunks.mkdir()
    (chunks / 'part.db').write_bytes(sql.read_bytes())
    (chunks / 'manifest.json').write_text(json.dumps({'kind': 'aser-relation-chunks', 'files': [{'name': 'part.db', 'sha256': hashlib.sha256(sql.read_bytes()).hexdigest()}]}))
    es, s = run('aser', chunks, 'aser-chunks')
    assert es == {('b','a'),('b','c')}
    import tarfile
    import io
    cb = d / 'causalbank'
    cb.mkdir()
    files = []
    for mode, data in [('because_mode', b'because\tflood\train\n'), ('therefore_mode', b'therefore\tflood\tdamage\n')]:
        path = cb / (mode + '.tar.gz')
        with tarfile.open(path, 'w:gz') as tar:
            member = tarfile.TarInfo(mode + '/fixture')
            member.size = len(data)
            tar.addfile(member, io.BytesIO(data))
        files.append({'name': path.name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    (cb / 'manifest.json').write_text(json.dumps({'files': files}))
    es, s = run('causalbank', cb, 'cb')
    assert es == {('rain','flood'),('flood','damage')}
    path.write_bytes(path.read_bytes()[:-5])
    run('causalbank', cb, 'cb-truncated', False)
    tsv.write_text('a\tb\t10\nBROKEN\n')
    run('causalnet', tsv, 'broken', False)
    rng = random.Random(20260914)
    for trial in range(30):
        vertices = list(range(9))
        pairs = {(u,v) for u in vertices for v in vertices if rng.random() < .12}
        active = set(x for e in pairs for x in e)
        reach = set(pairs)
        for k in vertices:
            reach |= {(i,j) for i in vertices for j in vertices if (i,k) in reach and (k,j) in reach}
        expected = {tuple(sorted(v for v in active if v==u or ((u,v) in reach and (v,u) in reach))) for u in active}
        fixture = d / ('random%d.tsv' % trial)
        fixture.write_text(''.join('%s\t%s\t1\n' % (u,v) for u,v in sorted(pairs)))
        es, s = run('causalnet', fixture, 'random%d' % trial)
        assert s['strong_components'] == len(expected)
        assert s['largest_strong_component_nodes'] == max(map(len, expected), default=0)
        assert s['nodes_on_directed_cycles'] == sum((u,u) in reach for u in active)
        assert s['is_dag'] == (not any((u,u) in reach for u in active))
    truncated = d / 'truncated.bz2'
    truncated.write_bytes(source.read_bytes()[:-5])
    run('causenet', truncated, 'truncated', False)
print('large graph I/O: source direction, dedup, self-loops, components, DAG, failure publication passed')
