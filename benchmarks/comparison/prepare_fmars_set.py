from pathlib import Path
root=Path(__file__).resolve().parents[2];here=Path(__file__).resolve().parent
combos={(0,0,0,0)}
for suite in ['94top50','nanobm07']:
    for line in (here/(suite+'.txt')).read_text().splitlines()[1:]:
        v=list(map(int,line.split()))
        if len(v)==6:combos.add((v[0],v[1],v[2],v[4]))
ops='dat spl mov djn add jmz sub seq sne slt jmn jmp nop mul mod div'.split();mods='f a b ab ba x i'.split();modes='$#@<>*{}'
text='\n'.join(f'{ops[o]}.{mods[m]} {modes[a]} {modes[b]}' for o,m,a,b in sorted(combos))+'\n'
for name in ['fmars-0.0.207','fmars-corrected']:(root/'dist/comparison-sources'/name/'corpus.set').write_text(text)
