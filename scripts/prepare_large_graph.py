#!/usr/bin/env python3
"""Publish a converted graph only after BOTH extraction and packing succeed."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('kind', choices=['causenet', 'causalnet', 'ceg', 'aser', 'causalbank'])
p.add_argument('source', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--packer', type=Path, required=True)
p.add_argument('--source-url', required=True)
a = p.parse_args()
if a.output.exists():
    p.error('output already exists')
a.output.parent.mkdir(parents=True, exist_ok=True)
staging = a.output.with_name(a.output.name + '.partial')
if staging.exists():
    p.error('staging directory exists; inspect previous failure before choosing a new output')
staging.mkdir()
started = time.time()
extract = subprocess.Popen([sys.executable, str(Path(__file__).with_name('extract_large_graph.py')), a.kind, str(a.source), '--audit-file', str(staging / 'source-audit.jsonl')], stdout=subprocess.PIPE)
pack = None
try:
    pack = subprocess.Popen([str(a.packer.resolve()), str(staging)], stdin=extract.stdout)
    extract.stdout.close()
    rc_pack = pack.wait()
    rc_extract = extract.wait()
except BaseException:
    if pack is not None:
        pack.terminate()
        pack.wait()
    extract.terminate()
    extract.wait()
    raise
if rc_extract or rc_pack:
    (staging / 'stats.json').unlink(missing_ok=True)
    raise SystemExit('conversion failed: extractor=%s packer=%s; partial files not published' % (rc_extract, rc_pack))
def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(8 * 1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()
if a.source.is_dir():
    manifest = json.loads((a.source / 'manifest.json').read_text())
    source_hashes = {item['name']: digest(a.source / item['name']) for item in manifest['files']}
    if any(source_hashes[item['name']] != item['sha256'] for item in manifest['files']):
        raise ValueError('source chunk digest mismatch; output not published')
else:
    source_hashes = {a.source.name: digest(a.source)}
metadata = {
    'source_kind': a.kind, 'source_url': a.source_url,
    'source_file': str(a.source.resolve()),
    'source_sha256': source_hashes[a.source.name] if a.source.is_file() else None,
    'source_files_sha256': source_hashes,
    'edges_sha256': digest(staging / 'edges.u64le'),
    'nodes_sha256': digest(staging / 'nodes.jsonl'),
    'scc_sha256': digest(staging / 'scc.u64le'),
    'seconds': time.time() - started,
    'node_policy': 'only endpoints of selected causal edges; original labels remain distinct',
    'relation_policy': {'aser': 'Result: hid->tid; Reason: tid->hid; positive weights only', 'causalbank': 'because_mode: right->left; therefore_mode: left->right; verbatim text nodes', 'ceg': 'unambiguous cause->effect pairs; records with multiple unescaped arrows quarantined in source-audit.jsonl'}.get(a.kind, 'source cause->effect'),
    'source_audit_sha256': digest(staging / 'source-audit.jsonl'),
    'quarantined_records': sum(1 for _ in (staging / 'source-audit.jsonl').open()),
    'evidence_policy': 'source retained verbatim; binary graph deduplicates directed pairs',
    'is_structural_causal_model': 'not established',
    'fd_preserving_transformation_claimed': False,
}
(staging / 'provenance.json').write_text(json.dumps(metadata, indent=2) + '\n')
staging.rename(a.output)
print((a.output / 'stats.json').read_text())
