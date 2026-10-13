"""Build the linked evidence report from the same raw files as the social card."""
import hashlib
import html
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent
summary = json.loads((ROOT / 'summary.json').read_text())
streaming = json.loads((ROOT / 'raw-streaming.json').read_text())
mount = json.loads((ROOT / 'raw-mount.json').read_text())
env = json.loads((ROOT / 'environment.json').read_text())
names = {'D': 'StreamText', 'A1': 'React <Text> · every update', 'A2': 'React <Text> · once per frame', 'B1': 'Reanimated · TextInput', 'B2': 'streamdown · Markdown'}
mount_names = {'streamText': 'StreamText', 'reactText': 'React <Text>', 'reanimated': 'Reanimated · TextInput', 'streamdown': 'streamdown · Markdown'}

def number(value):
    return f'{value:.1f}'.rstrip('0').rstrip('.')

def value(stats):
    return f'<b>{number(stats["median"])}</b> <small>({number(stats["min"])}–{number(stats["max"])})</small>'

def stream_table(scenario):
    rows = []
    for variant, name in names.items():
        cell = summary['streaming'][variant][scenario]
        values = [cell, cell['uiHitches'], cell['uiDroppedFrames'], cell['uiMaxFrameMs']]
        rows.append(f'<tr class="{"ours" if variant == "D" else ""}"><th>{html.escape(name)}</th>' + ''.join(f'<td>{value(v)}</td>' for v in values) + '</tr>')
    return '<table><thead><tr><th>Implementation</th><th>Estimated p95 delay, ms</th><th>UI stutters</th><th>Estimated missed intervals</th><th>Longest UI frame, ms</th></tr></thead><tbody>' + ''.join(rows) + '</tbody></table>'

mount_rows = []
for variant, name in mount_names.items():
    cell = summary['mount'][variant]
    mount_rows.append(f'<tr class="{"ours" if variant == "streamText" else ""}"><th>{html.escape(name)}</th><td>{value(cell)}</td><td>{value(cell["maxFrameMs"])}</td></tr>')
mount_table = '<table><thead><tr><th>Implementation</th><th>Mount marker, ms</th><th>Longest UI frame, ms</th></tr></thead><tbody>' + ''.join(mount_rows) + '</tbody></table>'
rtts = [r['clockRttMs'] for r in streaming['runs']]
versions = ', '.join(f'{html.escape(name)} {html.escape(version)}' for name, version in env['versions'].items())
digests = ''.join(f'<li><a href="{name}">{name}</a><code>{hashlib.sha256((ROOT / name).read_bytes()).hexdigest()}</code></li>' for name in ['raw-streaming.json', 'raw-mount.json', 'summary.json', 'environment.json'])
report = f'''<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>StreamText — benchmark evidence, 13 October 2026</title>
<style>
:root{{color-scheme:dark}}*{{box-sizing:border-box}}body{{background:#0a0c14;color:#f0eef8;font:17px/1.6 system-ui,sans-serif;margin:0}}main{{max-width:1180px;margin:auto;padding:48px 28px}}h1{{font-size:42px;line-height:1.15}}h2{{margin-top:42px}}a{{color:#b89cff}}p,li{{max-width:1000px;color:#c7c4d3}}table{{border-collapse:collapse;width:100%;font-size:15px;margin:24px 0}}th,td{{padding:12px 14px;border-bottom:1px solid #303041;text-align:right}}th:first-child{{text-align:left}}thead{{color:#aca6be}}.ours{{background:#272038;color:#c6b0ff}}small{{display:block;color:#9995aa;font-size:12px}}code{{display:block;overflow-wrap:anywhere;font-size:12px;color:#9f9bae}}.scroll{{overflow:auto}}.lead{{font-size:21px;color:#e1d9f4}}.tag{{color:#b89cff;font-weight:700}}img{{width:100%;border-radius:12px}}pre{{overflow:auto;background:#151621;padding:18px;border-radius:12px}}pre code{{font-size:14px;display:inline;color:#dad6e6}}
</style><main>
<p class="tag">iOS SIMULATOR · RELEASE · 13 OCTOBER 2026</p>
<h1>StreamText: benchmark evidence</h1>
<p class="lead">45 streaming runs and 12 mount runs, all complete. Each cell shows the median of three repetitions; parentheses show the full observed range. Every run is included.</p>
<p><a href="benchmark-table-focused.png">Main social card (PNG)</a> · <a href="benchmark-table-focused.svg">Editable SVG</a> · <a href="benchmark-table.png">All latency scenarios + mount</a></p>
<img src="benchmark-table-focused.png" alt="StreamText comparison: update delay, UI stutters, and mount time">
<h2>Busy JavaScript: the main card</h2>
<p>S4: a 100 KB plain-text answer, 256-byte target chunks at 80 chunks/s (~20 KB/s), plus 40 ms of busy JS work every 100 ms. Every implementation uses the same native bottom-anchored chat container. The card selects this explicit workload; it does not combine each competitor’s worst scenario.</p>
<div class="scroll">{stream_table('S4')}</div>
<h2>Mount: 20 finished messages</h2>
<p>20 messages × 5 KB requested (5,012 actual UTF-8 bytes per message), 408-point container, font size 14 and line height 21. Text and buffers are loaded before timing. All 20 views mount in a clipped plain container without list virtualization. This measures mounting ready content, not network download or application startup.</p>
<div class="scroll">{mount_table}</div>
<p>The two React streaming strategies share the same finished-message mount implementation, so their mount value is repeated on the card. Reanimated uses the same animated TextInput approach as its streaming row.</p>
<h2>Other measured streaming workloads</h2>
<h3>S1 · Short reply, 5 KB at ~0.8 KB/s</h3><div class="scroll">{stream_table('S1')}</div>
<h3>S2 · Long reply, 100 KB at ~20 KB/s</h3><div class="scroll">{stream_table('S2')}</div>
<h2>What the columns mean</h2>
<ul>
<li><b>Update delay:</b> the estimated elapsed time from the server writing bytes to a native UI marker covering those bytes. p95 is calculated over server send events in each run; the card takes the median of three run-level p95 values. It is not a measured photon/pixel presentation timestamp.</li>
<li><b>UI stutters (hitches):</b> the count of CADisplayLink timestamp gaps greater than 1.5 times the expected frame interval. At 60 Hz that means a gap longer than about 25 ms. This is a UI-thread frame-gap proxy, not a compositor-measured count of dropped frames.</li>
<li><b>Estimated missed intervals:</b> for each hitch, rounded(gap / expected interval) − 1, summed over the run. Kept in this report, not labeled as actual dropped display frames on the card.</li>
<li><b>Longest UI frame:</b> the largest gap between display-link timestamps. This captures long UI stalls that a hitch count alone does not describe.</li>
<li><b>Mount time:</b> elapsed time from requesting the mounted tree to a native timestamp after two frame markers following React commit. StreamText additionally waits for all 20 views to report done. Frame monitoring continues until ten on-time frames, with bounded waits.</li>
</ul>
<h2>Comparison boundaries</h2>
<p>StreamText uses its own native streamFetch → native view path, with zero JS chunk callbacks. The four JS strategies use React Native XMLHttpRequest progress; nitro-fetch is not used. This compares complete implementations, including their transport paths, rather than isolating only the renderer. React’s frame-batched strategy is included.</p>
<p>streamdown also parses Markdown even though the input is plain text. Its asynchronous rendering can finish after the timing marker; its delay and mount times are optimistic estimates. A fast mount marker does not guarantee that all later Markdown work has finished. For example, one streamdown mount marker was 55 ms while its longest observed frame was 174 ms. The full range is retained above.</p>
<p>Mount order rotates per repetition. Message prefixes differ across messages and repetitions to avoid repeating identical layout-cache keys; the StreamText prepared-layout cache is cleared before each cell. All variants receive the same text within a repetition. Streaming variant order also rotates. No UI inspection occurred during timed runs.</p>
<p>Clock offset is sampled before each streaming run using the lowest-latency of 12 requests. Selected round-trip times were {min(rtts)}–{max(rtts)} ms, so small millisecond differences should not be treated as precise speed ratios. The card rounds times to whole milliseconds. Three repetitions give a descriptive result, not a population confidence interval.</p>
<h2>Environment and reproducibility</h2>
<p>{html.escape(env['device'])}, {html.escape(env['os'])}; {html.escape(env['host'])}. Same arm64 Release binary and production JS bundle for both suites. These are simulator results, not physical-iPhone measurements. Other host workloads and refresh behavior were not experimentally locked. Benchmark helpers and installed native devtools instrumentation were present.</p>
<p>{versions}.</p>
<p>Source base: <code>{env['head']}</code> plus the benchmark harness and existing chat-container changes identified by SHA-256 in <a href="environment.json">environment.json</a>.</p>
<pre><code>rtk proxy python3 scripts/build-social-bench-ios.py
# Install the resulting Release simulator app, then run Streaming and Mount once.
# Copy the two saved JSON files to raw-streaming.json and raw-mount.json.
rtk proxy python3 media/benchmark-card-v2/summarize.py
rtk proxy python3 media/benchmark-card-v2/render.py media/benchmark-card-v2/data.json --focused --out media/benchmark-card-v2/benchmark-table-focused.svg --png
rtk proxy python3 media/benchmark-card-v2/render-methodology.py</code></pre>
<p>The build script temporarily selects S1/S2/S4 × A1/A2/B1/B2/D × 3 and restores the original configuration. The local streaming server must be running on port 9877. Raw files preserve all observations, including outliers.</p>
<ul>{digests}</ul>
<p>Raw observations, source fingerprints, and rendering scripts are included with these results.</p>
</main></html>'''
(ROOT / 'methodology.html').write_text(report)
print(ROOT / 'methodology.html')
