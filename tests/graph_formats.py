#!/usr/bin/env python3
import pathlib, struct, subprocess, sys, tempfile
binary = str(pathlib.Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix='fd-graph-formats-') as tmp:
    p=pathlib.Path(tmp)
    # Includes an isolate and IDs beyond 32 bits without allocating a vertex array.
    txt='4294967300 3\n0 4294967296\n2 3\n4294967296 4294967298\n'
    (p/'in.txt').write_text(txt)
    subprocess.run([binary,'txt-to-bin',str(p/'in.txt'),str(p/'out.bin')],check=True)
    data=(p/'out.bin').read_bytes()
    assert data[:8]==b'FDGBIN01'
    assert struct.unpack('<QQ',data[8:24])==(4294967300,3)
    assert list(struct.iter_unpack('<QQ',data[24:]))==[(0,4294967296),(2,3),(4294967296,4294967298)]
    subprocess.run([binary,'bin-to-txt',str(p/'out.bin'),str(p/'back.txt')],check=True)
    assert (p/'back.txt').read_text()==txt
    (p/'raw').write_bytes(data[24:])
    subprocess.run([binary,'raw-to-txt',str(p/'raw'),str(p/'raw.txt'),'4294967300','ascending'],check=True)
    assert (p/'raw.txt').read_text()==txt
    # Cross the output buffer boundary and independently check every record.
    many=[(i,i+1) for i in range(100000)]
    (p/'many.raw').write_bytes(b''.join(struct.pack('<QQ',*e) for e in many))
    subprocess.run([binary,'raw-to-txt',str(p/'many.raw'),str(p/'many.txt'),'100002','ascending'],check=True)
    lines=(p/'many.txt').read_text().splitlines()
    assert lines[0]=='100002 100000'
    assert [tuple(map(int,s.split())) for s in lines[1:]]==many
    subprocess.run([binary,'txt-to-bin',str(p/'many.txt'),str(p/'many.bin')],check=True)
    subprocess.run([binary,'bin-to-txt',str(p/'many.bin'),str(p/'many-back.txt')],check=True)
    assert (p/'many-back.txt').read_bytes()==(p/'many.txt').read_bytes()
    (p/'boundary.txt').write_text(' '*((1<<20)-5)+'4294967300 0\n')
    subprocess.run([binary,'txt-to-bin',str(p/'boundary.txt'),str(p/'boundary.bin')],check=True)
    assert struct.unpack('<QQ',(p/'boundary.bin').read_bytes()[8:])==(4294967300,0)
    for i,bad in enumerate(['2 1\n0 2\n','2 1\n0','2 0\n0 1','-1 0','18446744073709551616 0','2 0x']):
        (p/'bad.txt').write_text(bad)
        assert subprocess.run([binary,'txt-to-bin',str(p/'bad.txt'),str(p/f'bad{i}.bin')],capture_output=True).returncode
    for i,bad in enumerate([data[:-1],data+b'X',b'NOTMAGIC'+data[8:]]):
        (p/'bad.bin').write_bytes(bad)
        assert subprocess.run([binary,'bin-to-txt',str(p/'bad.bin'),str(p/f'bad{i}.txt')],capture_output=True).returncode
    for i, (mode, source, args) in enumerate([
        ('txt-to-bin', 'in.txt', ['extra']),
        ('bin-to-txt', 'out.bin', ['extra']),
        ('raw-to-txt', 'raw', []),
        ('raw-to-txt', 'raw', ['4294967300', 'invalid']),
    ]):
        dest = p / f'bad-args-{i}'
        assert subprocess.run([binary, mode, str(p/source), str(dest), *args], capture_output=True).returncode
        assert not dest.exists() and not pathlib.Path(str(dest)+'.partial').exists()
    (p/'empty.txt').write_text('5 0\n')
    subprocess.run([binary,'txt-to-bin',str(p/'empty.txt'),str(p/'empty.bin')],check=True)
    subprocess.run([binary,'bin-to-txt',str(p/'empty.bin'),str(p/'empty-back.txt')],check=True)
    assert (p/'empty-back.txt').read_text()=='5 0\n'
print('PASS: TXT/BIN equivalence, 64-bit IDs, isolates, empty graph, malformed/truncated/trailing data')
