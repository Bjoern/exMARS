#!/usr/bin/env python3
"""Build isolated browser/Node ES modules; never modify simulator sources."""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ORIGINAL = 'f7dbc5ef962b4b23547b9532f1750dbf1671e8ac'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--emcc', default='emcc')
p.add_argument('--pmars-source', type=Path, help='pMARS src directory; enables pMARS comparison')
p.add_argument('--original-source', type=Path, help='Optional original exMARS source instead of pinned local Git object')
p.add_argument('--output', type=Path, default=ROOT / 'dist/wasm')
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
work = ROOT / 'build/wasm'
work.mkdir(parents=True, exist_ok=True)
commands = []
source_hashes = {}

def run(cmd):
    cmd = list(map(str, cmd))
    commands.append(cmd)
    subprocess.run(cmd, check=True)

def build(name, folder, sources, defines=()):
    for file in sources:
        file = Path(file)
        source_hashes[str(file)] = hashlib.sha256(file.read_bytes()).hexdigest()
    run([a.emcc, '-std=gnu17', '-O3', '-Wno-incompatible-pointer-types',
         *defines, '-I' + str(folder), *sources,
         '-sMODULARIZE=1', '-sEXPORT_ES6=1', '-sENVIRONMENT=web,worker,node',
         '-sINVOKE_RUN=0', '-sEXIT_RUNTIME=1', '-sFORCE_FILESYSTEM=1',
         '-sALLOW_MEMORY_GROWTH=1', '-sMAXIMUM_MEMORY=536870912', '-sSTACK_SIZE=2097152',
         '-sEXPORTED_RUNTIME_METHODS=["callMain","FS"]', '-o', out / (name + '.mjs')])

h = ROOT / 'benchmarks/comparison'
for name, defines in [('exmars', []), ('reference-exmars', ['-DEXMARS_REFERENCE'])]:
    build(name, ROOT, [ROOT / f for f in ['pmars.c', 'sim.c', 'pspace.c']], defines)
for name, defines in [('optimized-exmars', ['-DEARLY=1']), ('exact-exmars', ['-DEARLY=0'])]:
    build('bench-' + name, ROOT, [h / 'exhaust_adapter.c', ROOT / 'sim.c', ROOT / 'pspace.c'], ['-DEXMARS', *defines])
original = a.original_source
if original is None:
    original = work / 'original'
    original.mkdir(exist_ok=True)
    for name in ['sim.c', 'sim.h', 'exhaust.h', 'insn.h', 'insn_help.h', 'pspace.c', 'pspace.h', 'README.md']:
        data = subprocess.check_output(['git', '-C', str(ROOT), 'show', ORIGINAL + ':' + name])
        (original / name).write_bytes(data)
build('bench-original-exmars', original, [h / 'exhaust_adapter.c', original / 'sim.c', original / 'pspace.c'], ['-DEXMARS'])
if a.pmars_source:
    folder = a.pmars_source.resolve()
    names = ['pmars.c', 'asm.c', 'eval.c', 'disasm.c', 'cdb.c', 'sim.c', 'pos.c', 'clparse.c', 'global.c', 'token.c', 'str_eng.c']
    flags = ['-DEXT94', '-DPERMUTATE', '-DRWLIMIT', '-DSERVER']
    build('pmars', folder, [folder / f for f in names], flags)
    # Only the benchmark copy changes placement scheduling; interpreter unchanged.
    patched = work / 'pmars'
    shutil.copytree(folder, patched, dirs_exist_ok=True)
    file = patched / 'sim.c'
    src = file.read_text()
    needle = 'warrior[1].position = separation + seed % positions;'
    if src.count(needle) != 1:
        raise RuntimeError('pMARS placement hook differs from the expected source')
    file.write_text('extern unsigned benchmark_position(unsigned);\n' + src.replace(needle, 'warrior[1].position = benchmark_position(round_num - 1);'))
    obj = patched / 'pmars-wasm.o'
    run([a.emcc, '-std=gnu17', '-O3', *flags, '-Dmain=pmars_cli_main', '-c', patched / 'pmars.c', '-o', obj])
    build('bench-pmars', patched, [h / 'pmars_adapter.c', obj, *[patched / f for f in names if f != 'pmars.c']], flags)
for name in ['client.mjs', 'worker.mjs', 'benchmark.mjs', 'README.md', 'build.py', 'browser-smoke.html']:
    shutil.copy2(ROOT / 'wasm' / name, out / name)
# Hash headers too, since they affect instruction representation and hot loops.
for folder in [ROOT, h, original] + ([a.pmars_source.resolve()] if a.pmars_source else []):
    for file in list(folder.glob('*.h')) + list(folder.glob('*.inc')):
        source_hashes[str(file)] = hashlib.sha256(file.read_bytes()).hexdigest()
# Ship corresponding source and notices beside the binaries.
for label, folder in [('exmars', ROOT), ('benchmark', h), ('original-exmars', original)] + ([('pmars', a.pmars_source.resolve())] if a.pmars_source else []):
    target = out / 'sources' / label
    target.mkdir(parents=True, exist_ok=True)
    for pattern in ['*.c', '*.h', '*.inc', 'README*', 'COPYING*', 'AUTHORS*']:
        for file in folder.glob(pattern):
            shutil.copy2(file, target / file.name)
    if label == 'pmars':
        for name in ['COPYING', 'AUTHORS', 'README']:
            if (folder.parent / name).is_file():
                shutil.copy2(folder.parent / name, target / name)
(out / 'manifest.json').write_text(json.dumps({
    'compiler': subprocess.check_output([a.emcc, '--version'], text=True),
    'originalCommit': ORIGINAL if a.original_source is None else None,
    'commands': commands, 'sources': source_hashes,
    'artifacts': {f.name: hashlib.sha256(f.read_bytes()).hexdigest() for f in out.iterdir() if f.suffix in ('.wasm', '.mjs')},
}, indent=2) + '\n')
print('Built:', out)
