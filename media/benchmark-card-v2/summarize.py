"""Validate the complete matched matrix and derive the card without hand-entered numbers."""
import json
import math
from pathlib import Path
from statistics import median

ROOT = Path(__file__).resolve().parent
streaming = json.loads((ROOT / 'raw-streaming.json').read_text())
mount = json.loads((ROOT / 'raw-mount.json').read_text())
variants = ['D', 'A1', 'A2', 'B1', 'B2']
scenarios = ['S1', 'S2', 'S4']
mount_variants = ['streamText', 'reactText', 'reanimated', 'streamdown']
assert len(streaming['runs']) == 45
assert len(mount['runs']) == 12
assert mount['workload']['messages'] == 20
assert mount['workload']['requestedBytesEach'] == 5000

def cell(runs, **keys):
    selected = [r for r in runs if all(r[k] == v for k, v in keys.items())]
    assert len(selected) == 3, keys
    assert sorted(r['rep'] for r in selected) == [1, 2, 3], keys
    assert all(r.get('completed', r.get('complete', False)) and not r.get('error') for r in selected), selected
    return selected

def stats(runs, key):
    values = [r[key] for r in runs]
    assert all(isinstance(v, (int, float)) and math.isfinite(v) and v >= 0 for v in values), key
    return {'values': values, 'median': median(values), 'min': min(values), 'max': max(values)}

summary = {'streaming': {}, 'mount': {}}
for variant in variants:
    summary['streaming'][variant] = {}
    for scenario in scenarios:
        runs = cell(streaming['runs'], variant=variant, scenario=scenario)
        summary['streaming'][variant][scenario] = stats(runs, 'latP95Ms')
        for key in ['uiHitches', 'uiDroppedFrames', 'uiMaxFrameMs', 'jsChunks']:
            summary['streaming'][variant][scenario][key] = stats(runs, key)
for variant in mount_variants:
    runs = cell(mount['runs'], variant=variant)
    summary['mount'][variant] = {**stats(runs, 'shownMs'), 'maxFrameMs': stats(runs, 'maxFrameMs')}

data = json.loads((ROOT / 'data.example.json').read_text())
mapping = {'D': 'streamText', 'A1': 'reactText', 'A2': 'reactText', 'B1': 'reanimated', 'B2': 'streamdown'}
for row, variant in zip(data['rows'], variants):
    row['values'] = dict(zip(['shortReplyP95Ms', 'longReplyP95Ms', 'busyJsP95Ms'], [summary['streaming'][variant][s]['median'] for s in scenarios]))
    row['values']['mountMs'] = summary['mount'][mapping[variant]]['median']
    row['values']['mountMaxFrameMs'] = summary['mount'][mapping[variant]]['maxFrameMs']['median']
    row['values']['busyJsHitches'] = summary['streaming'][variant]['S4']['uiHitches']['median']
    row['values']['busyJsMaxFrameMs'] = summary['streaming'][variant]['S4']['uiMaxFrameMs']['median']
data['meta'].update({'environment': 'iOS Simulator · Release', 'releaseLabel': 'median of 3 runs', 'sample': 'Plain text', 'device': 'iPhone 17 Pro Max · iOS 26.5', 'host': 'Apple M3 Max · 96 GB', 'date': '13 Oct 2026'})
data['caveat'] = '*Markdown; optimistic completion marker. Timing estimates.'
(ROOT / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
(ROOT / 'data.json').write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n')
print(json.dumps([{'name': row['name'], 'variant': variant, **row['values']} for row, variant in zip(data['rows'], variants)], indent=2))
