#!/usr/bin/env python3
"""Streaming source adapters. Emit two JSON-string columns, cause then effect.
No pickle loading, cycle removal, node merging, or conversion to an SCM.
Source archives/DBs retain evidence; stdout contains structural edges only.
"""
import argparse
import bz2
import gzip
import json
import math
import sqlite3
import sys
import tarfile
from pathlib import Path

def emit(u, v):
    if not isinstance(u, str) or not isinstance(v, str) or not u or not v:
        raise ValueError('expected nonempty string node labels')
    print(json.dumps(u, ensure_ascii=True) + '\t' + json.dumps(v, ensure_ascii=True))

def run():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('kind', choices=['causenet', 'causalnet', 'ceg', 'aser', 'causalbank'])
    p.add_argument('source', type=Path)
    p.add_argument('--audit-file', type=Path)
    a = p.parse_args()
    count = edges = ambiguous = 0
    audit = a.audit_file.open('w') if a.audit_file else None
    if a.kind == 'causalbank':
        # Source columns are connective, left text, right text. Because reverses
        # text order; therefore preserves it. Parse archive streams, never extract.
        for mode in ['because_mode', 'therefore_mode']:
            with gzip.open(a.source / (mode + '.tar.gz'), 'rb') as gz:
                with tarfile.open(fileobj=gz, mode='r|') as archive:
                    for member in archive:
                        if member.isdir():
                            continue
                        if not member.isfile() or not member.name.startswith(mode + '/'):
                            raise ValueError('unexpected CausalBank archive member')
                        with archive.extractfile(member) as src:
                            for line in src:
                                fields = line.decode('utf-8').rstrip('\r\n').split('\t')
                                if len(fields) != 3 or not fields[0]:
                                    raise ValueError('CausalBank requires connective TAB left TAB right')
                                left, right = fields[1:]
                                emit(right, left) if mode == 'because_mode' else emit(left, right)
                                count += 1
                                if count % 1000000 == 0:
                                    print('source_records=%d' % count, file=sys.stderr)
                # Validate the gzip trailer, including truncation after tar EOF.
                while gz.read(8 * 1024 * 1024):
                    pass
        print(json.dumps({'source_records': count, 'emitted_edges': count, 'extraction_complete': True}), file=sys.stderr)
        return
    if a.kind == 'aser' and a.source.is_dir():
        # The official manifest enumerates every disjoint relation-weight chunk.
        manifest = json.loads((a.source / 'manifest.json').read_text())
        if manifest.get('kind') != 'aser-relation-chunks' or not manifest.get('files'):
            raise ValueError('missing ASER relation chunk manifest')
        import subprocess
        for item in manifest['files']:
            name = item['name']
            if Path(name).name != name:
                raise ValueError('invalid chunk name')
            subprocess.run([sys.executable, __file__, 'aser', str(a.source / name)], check=True)
        return
    if a.kind == 'aser':
        # Both full and conceptualized releases use this official schema.
        with sqlite3.connect(a.source.resolve().as_uri() + '?mode=ro', uri=True) as db:
            columns = {r[1] for r in db.execute('PRAGMA table_info(Relations)')}
            if not {'hid', 'tid', 'Reason', 'Result'} <= columns:
                raise ValueError('ASER Relations schema mismatch')
            for h, t, reason, result in db.execute('SELECT hid,tid,Reason,Result FROM Relations'):
                count += 1
                for u, v, weight in [(h, t, result), (t, h, reason)]:
                    weight = 0.0 if weight is None else float(weight)
                    if not math.isfinite(weight) or weight < 0:
                        raise ValueError('invalid ASER relation weight')
                    if weight > 0:
                        emit(u, v)
                        edges += 1
    else:
        opener = bz2.open if a.source.suffix == '.bz2' else gzip.open if a.source.suffix == '.gz' else open
        with opener(a.source, 'rt', encoding='utf-8') as src:
            for line in src:
                count += 1
                if a.kind == 'causenet':
                    rel = json.loads(line)['causal_relation']
                    emit(rel['cause']['concept'], rel['effect']['concept'])
                elif a.kind == 'ceg':
                    fields = line.rstrip('\r\n').split('\t')
                    if len(fields) != 4 or '->' not in fields[0]:
                        raise ValueError('CEG requires cause->effect TAB frequency TAB necessity TAB sufficiency')
                    values = list(map(float, fields[1:]))
                    if not all(math.isfinite(x) and x >= 0 for x in values) or values[0] <= 0:
                        raise ValueError('invalid CEG frequency/score')
                    if fields[0].count('->') != 1:
                        if audit is None:
                            raise ValueError('ambiguous CEG delimiter; require --audit-file to quarantine without guessing endpoints')
                        audit.write(json.dumps({'line': count, 'reason': 'ambiguous_unescaped_arrow', 'raw': line.rstrip('\r\n')}) + '\n')
                        ambiguous += 1
                        continue
                    emit(*fields[0].split('->'))
                else:
                    fields = line.rstrip('\r\n').split('\t')
                    if len(fields) != 3:
                        raise ValueError('CausalNet requires CAUSE_WORD TAB EFFECT_WORD TAB FREQUENCY')
                    weight = float(fields[2])
                    if not math.isfinite(weight) or weight <= 0:
                        raise ValueError('CausalNet frequency must be positive')
                    emit(fields[0], fields[1])
                edges += 1
                if count % 1000000 == 0:
                    print('source_records=%d emitted_edges=%d' % (count, edges), file=sys.stderr)
    if audit is not None:
        audit.close()
    print(json.dumps({'source_records': count, 'emitted_edges': edges, 'ambiguous_records_quarantined': ambiguous, 'extraction_complete': True}), file=sys.stderr)

if __name__ == '__main__':
    run()
