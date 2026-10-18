"""Losslessly deduplicate repeated score arrays to keep the published data small."""
import argparse
import json
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('input', type=Path)
p.add_argument('output', type=Path)
a = p.parse_args()
if a.output.exists():
    p.error('output must be a new file')
data = json.loads(a.input.read_text())
if 'score_sets' in data:
    p.error('input is already compacted')
original = json.loads(json.dumps(data))
sets = {}
for run in data['runs']:
    scores = run.pop('scores')
    name = next((key for key, value in sets.items() if value == scores), None)
    if name is None:
        name = run['suite'] + '-' + run['engine']
        sets[name] = scores
    run['score_set'] = name
data['score_sets'] = sets
# Expand once to prove all fields are retained exactly.
expanded = json.loads(json.dumps(data))
for run in expanded['runs']:
    run['scores'] = expanded['score_sets'][run.pop('score_set')]
del expanded['score_sets']
if expanded != original:
    raise RuntimeError('Compaction changed results')
a.output.write_text(json.dumps(data, indent=2) + '\n')
print(len(data['runs']), 'runs;', len(sets), 'distinct score arrays')
