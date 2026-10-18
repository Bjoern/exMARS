from pathlib import Path
import subprocess
r=Path(__file__).resolve().parents[2]
out=r/'build/validation';out.mkdir(parents=True,exist_ok=True)
for test,variants in [('state', [('reference',['-DEXMARS_REFERENCE']),('optimized',[])]),('results',[('exact',[]),('early',['-DENABLE_EARLY'])])]:
    results=[]
    for name,flags in variants:
        exe=out/(test+'-'+name+'.exe')
        subprocess.run(['gcc','-std=gnu17','-O3',*flags,'-I',str(r),str(r/'tests/engine'/ (test+'.c')),str(r/'sim.c'),str(r/'pspace.c'),'-o',str(exe)],check=True)
        results.append(subprocess.check_output([str(exe)],text=True))
    assert results[0]==results[1],results
    print(test,results[0].strip())
