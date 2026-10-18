from pathlib import Path
import subprocess,json,time,hashlib,argparse
p=argparse.ArgumentParser();p.add_argument('--passes',type=int,default=1);p.add_argument('--nano-repeats',type=int,default=1);p.add_argument('--suite',choices=['94top50','nanobm07','both'],default='both');p.add_argument('--output',default='calibration.json');a=p.parse_args()
r=Path(__file__).resolve().parents[2];h=Path(__file__).resolve().parent;out=h/a.output
if out.exists():raise SystemExit('Refusing to overwrite '+str(out))
if a.passes<1 or a.nano_repeats<1:p.error('passes and nano-repeats must be positive')
engines=['pmars','original-exmars','exhaust','exhaust-ma','qmars','fmars','fmars-corrected','exact-exmars','optimized-exmars']
report={'runs':[],'binary_sha256':{e:hashlib.sha256((r/'dist/comparison-build'/f'{e}.exe').read_bytes()).hexdigest() for e in engines},'nano_repeats':a.nano_repeats,'passes':a.passes}
for suite in (['94top50','nanobm07'] if a.suite=='both' else [a.suite]):
 expected=None
 for trial in range(a.passes):
  order=engines if trial%2==0 else engines[::-1]
  for e in order:
   cmd=[str(r/'dist/comparison-build'/f'{e}.exe'),str(h/f'{suite}.txt'),str(a.nano_repeats if suite=='nanobm07' else 1)]
   t=time.perf_counter();proc=subprocess.run(cmd,capture_output=True,text=True)
   try:
    if proc.returncode:raise ValueError('Engine failed')
    result=json.loads(proc.stdout)
   except Exception:
    result={'error':proc.stdout[-1500:]+proc.stderr[-1500:],'exit_code':proc.returncode}
   result.update(engine=e,suite=suite,trial=trial,command=cmd,wall_seconds=time.perf_counter()-t)
   if 'scores' in result:
    if expected is None:
     if e!='pmars':raise RuntimeError('pMARS reference did not complete successfully')
     expected=result['scores']
    result['mismatching_pairs']=[i for i,(x,y) in enumerate(zip(expected,result['scores'])) if x!=y]
    pairs=1225 if suite=='nanobm07' else 1711
    rounds=pairs*(142*a.nano_repeats if suite=='nanobm07' else 32)
    if len(expected)!=pairs or len(result['scores'])!=pairs or result['rounds']!=rounds:
     raise RuntimeError('Incorrect pair/round count')
    if not all(len(row)==3 and sum(row)==rounds//pairs for row in result['scores']):
     raise RuntimeError('Invalid score totals')
    print(suite,trial,e,round(result['seconds'],3),'mismatches',len(result['mismatching_pairs']),flush=True)
   else:print(suite,e,result,flush=True)
   report['runs'].append(result);out.write_text(json.dumps(report,indent=2)+'\n')
   if 'error' in result:raise RuntimeError('Engine failed; partial results saved to '+str(out))
print('Wrote',out)
