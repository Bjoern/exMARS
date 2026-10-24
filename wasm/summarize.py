#!/usr/bin/env python3
"""Summarize raw WASM measurements without discarding pair-score evidence."""
import argparse
import json
import statistics
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('input', type=Path)
a=p.parse_args()
data=json.loads(a.input.read_text())
labels={'pmars':'pMARS (local source)', 'original-exmars':'Original exMARS',
        'exact-exmars':'Optimized exMARS, exact', 'optimized-exmars':'Optimized exMARS, early ties'}
suites=['94top50','nanobm07']
medians={}
print('| Engine | 94top50 seconds | Nano seconds |')
print('|---|---:|---:|')
for engine,label in labels.items():
    cells=[]
    for suite in suites:
        runs=data['suites'][suite]['runs']
        values=[r['seconds'] for r in runs if r['engine']==engine]
        if len(values)!=data['suites'][suite]['passes']: raise ValueError('Incomplete run')
        median=statistics.median(values);medians[suite,engine]=median
        cells.append(f'{median:.3f} ({min(values):.3f}–{max(values):.3f})')
    print('| '+label+' | '+' | '.join(cells)+' |')
for suite in suites:
    best=medians[suite,'optimized-exmars']
    print(f'\n{suite}: default optimized vs pMARS {medians[suite,"pmars"]/best:.2f}×; '
          f'vs original {medians[suite,"original-exmars"]/best:.2f}×.')
