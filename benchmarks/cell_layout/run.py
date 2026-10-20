"""Quick, interleaved benchmark against the current optimized source tree."""
import argparse
import hashlib
import json
import math
import platform
import random
import statistics
import subprocess
from pathlib import Path
from prepare import ROOT, OUT, prepare

HERE = Path(__file__).resolve().parent
VARIANTS = {'baseline': 0, 'packed64': 64, 'packed32': 32}
FLAGS = ['-std=gnu17', '-O3', '-fomit-frame-pointer']

def run(cmd):
    result = subprocess.run([str(x) for x in cmd], text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f'{cmd}\n{result.stdout}\n{result.stderr}')
    return result.stdout

def compile_exe(cc, source, dest, exe, flags=()):
    run([cc, *FLAGS, *flags, '-I', dest, source, dest/'sim.c',
         dest/'pspace.c', '-o', exe])

def validate(cc, paths):
    report = {}
    for tiny in (False, True):
        variants = list(paths) if tiny else ['baseline', 'packed64']
        for test in ('state', 'results', 'early', 'pspace_endgame', 'mul'):
            if tiny and test == 'mul':
                continue  # this regression specifically uses a large core
            source = (ROOT/'tests/engine'/f'{test}.c').read_text()
            if tiny:
                source = source.replace('{80, 800, 8000, 8192, 32768}', '{80, 127, 128, 255, 256}')
                source = source.replace('size <= 8000', 'size <= 80')
            testfile = OUT/f'{test}-{"tiny" if tiny else "full"}.c'
            testfile.write_text(source)
            for early in ([False, True] if test == 'results' else [False]):
                key = f'{test}-{"tiny" if tiny else "full"}-{"early" if early else "exact"}'
                outputs = {}
                for name in variants:
                    exe = paths[name]/key
                    compile_exe(cc, testfile, paths[name], exe, ['-DENABLE_EARLY'] if early else [])
                    outputs[name] = run([exe]).strip()
                assert len(set(outputs.values())) == 1, (key, outputs)
                report[key] = outputs
                print('PASS', key, flush=True)
    for scope in ('full', 'tiny'):
        assert report[f'results-{scope}-exact'] == report[f'results-{scope}-early']
    return report

def measure(exe, core, early, repeats, files):
    return json.loads(run([exe, core, early, repeats, *files]).splitlines()[-1])

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cc', default='cc')
    ap.add_argument('--passes', type=int, default=5)
    ap.add_argument('--target-seconds', type=float, default=0.4)
    ap.add_argument('--cpu-label', default='unspecified')
    ap.add_argument('--output', type=Path, default=HERE/'results.json')
    a = ap.parse_args()
    if a.passes < 3 or a.target_seconds <= 0:
        ap.error('use at least 3 passes and a positive target duration')
    if a.output.exists():
        ap.error('output already exists; choose a new path')
    paths = {name: prepare(name, bits) for name, bits in VARIANTS.items()}
    report = {'machine': platform.machine(), 'platform': platform.platform(), 'cpu': a.cpu_label,
              'compiler': run([a.cc, '--version']), 'flags': FLAGS,
              'commit': run(['git', '-C', ROOT, 'rev-parse', 'HEAD']).strip(),
              'source_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                for p in [ROOT/'sim.c', ROOT/'sim_loop.inc', ROOT/'exhaust.h']},
              'validation': validate(a.cc, paths), 'runs': [], 'summary': []}
    for name, dest in paths.items():
        compile_exe(a.cc, HERE/'bench.c', dest, dest/'bench',
                    [f'-DPACKED_BITS={VARIANTS[name]}'] if VARIANTS[name] else [])
    nano = {
        'imp': 'mov.i 0, 1',
        'dwarf': 'add.ab #4, 3\nmov.i 2, @2\njmp -2\ndat 0, 0',
        'paper': 'spl 1\nspl @0, 17\nmov.i }-1, >-1\nmov.i 1, <13\ndat <1, <1',
        'scanner': 'add.ab #7, 1\njmz.f -1, 9\nmov.i 2, @-1\njmp -3\ndat 0, 0',
        'clear': 'spl #0, 0\nmov.i 2, >3\ndjn.f -1, >2\ndat 0, 5\ndat 0, 8',
        'arithmetic': 'mul.ab #3, 4\nmod.ab #17, 3\nmov.i 2, @2\njmp -3\ndat 0, 7',
    }
    fixtures = OUT/'nano'; fixtures.mkdir(exist_ok=True)
    for name, body in nano.items():
        (fixtures/f'{name}.red').write_text(';redcode-94\n;assert CORESIZE == 80\n' + body + '\nend 0\n')
    suites = {
        '94nop': (8000, [ROOT/'warriors/94nop'/f'{n}.red' for n in
                         ['machine', 'nPaperII', 'sonofvain', 'reepicheep', 'silvertalon12', 'QuickSilver', 'blade', 'uninvited']]),
        'pspace': (8000, [ROOT/'warriors/pspace'/f'{n}.red' for n in
                          ['dawn2', 'unheardof', 'devilish', 'hammerhead']]),
        'nano-synthetic': (80, sorted(fixtures.glob('*.red'))),
    }
    report['suites'] = {name: {'core': core, 'warriors': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}}
                        for name, (core, files) in suites.items()}
    report['nano_sources'] = nano
    rng = random.Random(2381)
    for suite, (core, files) in suites.items():
        variants = list(paths) if core == 80 else ['baseline', 'packed64']
        early_digest = None
        for early in (1, 0):
            # Warm up all variants, validate one identical workload, then choose
            # equal repeat counts from baseline time only.
            warm = {name: measure(paths[name]/'bench', core, early, 1, files) for name in variants}
            assert len({v['digest'] for v in warm.values()}) == 1, warm
            if early:
                early_digest = warm['baseline']['digest']
            else:
                assert warm['baseline']['digest'] == early_digest, suite
            repeats = max(1, math.ceil(a.target_seconds / warm['baseline']['seconds']))
            group = []
            for trial in range(a.passes):
                order = variants.copy(); rng.shuffle(order)
                batch = []
                for name in order:
                    row = dict(suite=suite, early=bool(early), trial=trial, variant=name, repeats=repeats,
                               **measure(paths[name]/'bench', core, early, repeats, files))
                    batch.append(row); group.append(row); report['runs'].append(row)
                assert len({r['digest'] for r in batch}) == 1, batch
                a.output.write_text(json.dumps(report, indent=2) + '\n')
            base = {r['trial']: r['seconds'] for r in group if r['variant'] == 'baseline'}
            for name in variants:
                rows = [r for r in group if r['variant'] == name]
                ratios = [base[r['trial']] / r['seconds'] for r in rows]
                summary = dict(suite=suite, early=bool(early), variant=name,
                               median_seconds=statistics.median(r['seconds'] for r in rows),
                               median_speedup=statistics.median(ratios),
                               min_speedup=min(ratios), max_speedup=max(ratios),
                               battles_per_pass=rows[0]['battles'])
                report['summary'].append(summary)
                print(summary, flush=True)
            a.output.write_text(json.dumps(report, indent=2) + '\n')

if __name__ == '__main__':
    main()
