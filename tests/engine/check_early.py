from pathlib import Path
import subprocess
r=Path(__file__).resolve().parents[2]
out=r/'build/early-validation';out.mkdir(parents=True,exist_ok=True)
for test in ('early','pspace_endgame','mul','results'):
    outputs=[]
    for early in (False,True) if test=='results' else (False,):
        exe=out/(test+('-early' if early else '')+'.exe')
        cmd=['gcc','-std=gnu17','-O2','-ftrapv',*(['-DENABLE_EARLY'] if early else []),'-I',str(r),str(r/'tests/engine'/ (test+'.c')),str(r/'sim.c'),str(r/'pspace.c'),'-o',str(exe)]
        subprocess.run(cmd,check=True)
        outputs.append(subprocess.check_output([str(exe)],text=True))
    assert len(set(outputs))==1,outputs
    print(outputs[0].strip())
