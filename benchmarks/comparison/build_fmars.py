from pathlib import Path
import subprocess,json,sys
r=Path(__file__).resolve().parents[2];label='fmars-corrected' if '--corrected' in sys.argv else 'fmars';d=r/'dist/comparison-sources'/('fmars-corrected' if '--corrected' in sys.argv else 'fmars-0.0.207');out=r/'dist/comparison-build';h=Path(__file__).resolve().parent
pp=subprocess.check_output(['gcc','-E','-P',str(d/'fm_sim_tmp.c')])
(d/'fm_sim.c').write_bytes((d/'fm_includes.c').read_bytes()+pp)
cmd=['gcc','-std=gnu17','-O3','-fomit-frame-pointer','-fno-strict-aliasing','-I'+str(d),str(h/'fmars_adapter.c'),str(d/'fmars.c'),str(d/'fm_sim.c'),str(d/'fm_asm.c'),'-o',str(out/(label+'.exe'))]
(out/(label+'-command.json')).write_text(json.dumps(cmd,indent=2));subprocess.run(cmd,check=True)
