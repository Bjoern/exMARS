from pathlib import Path
import shutil,subprocess,json,hashlib,argparse
root=Path(__file__).resolve().parents[2];out=root/'dist/comparison-build';out.mkdir(exist_ok=True)
src=root/'dist/comparison-sources';h=Path(__file__).resolve().parent;records=[]
ap=argparse.ArgumentParser()
ap.add_argument('--exmars-source',type=Path,default=root if (root/'sim.c').exists() else root/'dist/upstream-stage')
ap.add_argument('--pmars-source',type=Path,default=src/'pmars-local')
a=ap.parse_args()
def run(cmd,cwd=None):
 print(' '.join(map(str,cmd)),flush=True);subprocess.run(list(map(str,cmd)),cwd=cwd,check=True);records.append(list(map(str,cmd)))
def native(label,folder,defines=[]):
 run(['gcc','-std=gnu17','-O3','-fomit-frame-pointer',*defines,'-I'+str(folder),h/'exhaust_adapter.c',folder/'sim.c',folder/'pspace.c','-o',out/(label+'.exe')])
native('original-exmars',src/'original-exmars',['-DEXMARS'])
native('optimized-exmars',a.exmars_source,['-DEXMARS','-DEARLY=1'])
native('exact-exmars',a.exmars_source,['-DEXMARS','-DEARLY=0'])
native('exhaust',src/'exhaust-1.9.2')
native('exhaust-ma',src/'exhaust-ma-master')
# pMARS keeps its interpreter; only round placement is supplied by the harness.
pdir=out/'pmars';shutil.copytree(a.pmars_source,pdir,dirs_exist_ok=True)
p=pdir/'sim.c';s=p.read_text();s=s.replace('warrior[1].position = separation + seed % positions;', 'warrior[1].position = benchmark_position(round_num - 1);');s='extern unsigned benchmark_position(unsigned);\n'+s;p.write_text(s)
flags=['-std=gnu17','-O3','-fomit-frame-pointer','-DEXT94','-DPERMUTATE','-DRWLIMIT','-DSERVER','-I'+str(pdir)]
run(['gcc',*flags,'-Dmain=pmars_cli_main','-c',pdir/'pmars.c','-o',pdir/'pmars.o'])
run(['gcc',*flags,h/'pmars_adapter.c',pdir/'pmars.o',*[pdir/f for f in ['asm.c','eval.c','disasm.c','cdb.c','sim.c','pos.c','clparse.c','global.c','token.c','str_eng.c']],'-o',out/'pmars.exe'])
# qMARS: modern C++ compiler compatibility and externally supplied placements.
qdir=out/'qmars';shutil.copytree(src/'qmars-master',qdir,dirs_exist_ok=True)
p=qdir/'warrior.hpp';p.write_text(p.read_text().replace('Warrior::Warrior()', 'Warrior()'))
p=qdir/'qmars.hpp';s=p.read_text().replace('Clip(rounds);','/* benchmark rounds are explicit */');a=s.index('        if (round < ePositions)');b=s.index('\n    }',a);s=s[:a]+'''        warriors[0].startPosition=0;
        warriors[1].startPosition=benchmark_position(round);
        currWarrior=warriors+(round%2);
'''+s[b:];p.write_text(s)
run(['g++','-std=gnu++03','-O3','-fpermissive','-include','cstdlib',qdir/'code.cpp',qdir/'genbigswitch.cpp','-o',qdir/'gen.exe'])
with (qdir/'sw.cpp').open('w') as f:subprocess.run([str(qdir/'gen.exe')],stdout=f,check=True)
run(['g++','-std=gnu++03','-O3','-fomit-frame-pointer','-fpermissive','-include','cstdlib','-I'+str(qdir),h/'qmars_adapter.cpp',qdir/'code.cpp','-o',out/'qmars.exe'])
(out/'commands.json').write_text(json.dumps(records,indent=2))
