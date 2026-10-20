"""Additional checks for already-generated experimental builds."""
import json
import os
import subprocess
from prepare import ROOT, OUT
from run import compile_exe, run


def check(cc='cc'):
    report = {}
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
    for name, scope in [('baseline', 'full'), ('packed64', 'full'), ('packed32', 'tiny')]:
        dest = OUT/name
        exe = dest/'state-sanitized'
        compile_exe(cc, OUT/f'state-{scope}.c', dest, exe,
                    ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all'])
        result = subprocess.run([str(exe)], text=True, capture_output=True, env=env)
        assert result.returncode == 0, (name, result.stdout, result.stderr)
        report[name+'-sanitized'] = result.stdout.strip()
        print('PASS', name, 'ASan/UBSan', flush=True)
    for name in ('baseline', 'packed64'):
        dest = OUT/name
        exe = dest/'exmars'
        compile_exe(cc, dest/'pmars.c', dest, exe)
        report[name+'-cli'] = run(['python3', ROOT/'tests/pmars_compat/run.py', exe]).strip()
        print('PASS', name, 'CLI regressions', flush=True)
    # Validate the bitfield bridge, maximum small core, and explicit rejection
    # of unsupported core sizes independently of battle outcomes.
    probe = OUT/'layout.c'
    probe.write_text('''#include <stdlib.h>
#include "exhaust.h"
#include "sim.h"
int main(void) {
    for (unsigned a=0; a<256; ++a) for (unsigned b=0; b<256; ++b) {
        insn_t p = {0}; p.a=a; p.b=b; p.in=0xffff;
        if(cell_a(&p)!=a || cell_b(&p)!=b || cell_in(&p)!=65535) return 1;
        cell_set_a(&p, 255-a); cell_set_b(&p, 255-b); cell_set_in(&p, 0x1234);
        if(p.a!=255-a || p.b!=255-b || p.in!=0x1234) return 2;
    }
    mars_t *m=calloc(1, sizeof(*m)); m->coresize=257;
    if(sim_alloc_bufs(m)) return 3;
    free(m);
    return 0;
}
''')
    dest=OUT/'packed32'; exe=dest/'layout-check'
    compile_exe(cc, probe, dest, exe); run([exe])
    report['packed32-layout-and-limit'] = '65536 field combinations and core=257 rejection passed'
    return report

if __name__ == '__main__':
    print(json.dumps(check(), indent=2))
