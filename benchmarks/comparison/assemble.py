# Assemble once using the verified binding; all simulators get identical fields.
from pathlib import Path
import sys,json,hashlib,argparse
root=Path(__file__).resolve().parents[2]
ap=argparse.ArgumentParser()
ap.add_argument('--corpus-root',type=Path,default=root)
ap.add_argument('--module-dir',type=Path,default=(root/'bin' if (root/'sim.c').exists() else root/'exmars-0.01python/bin'))
a=ap.parse_args()
sys.path.insert(0,str(a.module_dir.resolve()))
import Mars
out=Path(__file__).resolve().parent
manifest=json.loads((out/'corpora.json').read_text())
for suite,core,cycles,procs,sep,length in [('94top50',8000,80000,8000,100,100),('nanobm07',80,800,80,5,5)]:
 names=manifest[suite]['warriors']
 m=Mars.Mars(2,core,cycles,procs,length,sep)
 lines=[f'{core} {cycles} {procs} {sep} {len(names)}'];inputs={}
 for name in names:
  digest=hashlib.sha256((a.corpus_root/suite/name).read_bytes()).hexdigest()
  if digest != manifest[suite]['input_sha256'][name]:
   raise ValueError('Corpus source differs from recorded input: '+suite+'/'+name)
  w=m.parse_file(str(a.corpus_root/suite/name));lines.append(f'{len(w.instructions)} {w.startPos}')
  for ins in w.instructions:
   data=ins.data
   if data[0] in (16,17):raise ValueError('P-space excluded from no-P-space comparison')
   lines.append(' '.join(map(str,data)))
  inputs[name]=digest
 p=out/(suite+'.txt');p.write_text('\n'.join(lines)+'\n')
 if hashlib.sha256(p.read_bytes()).hexdigest()!=manifest[suite]['assembled_sha256']:
  raise ValueError('Assembled input differs from the measured Windows input: '+suite)
 print('Verified',suite,len(names),'warriors')
