# Benchmark social card v2

This directory contains a deterministic, editable renderer for the compact
social card. It deliberately does not copy the old Android values: `data.example.json`
contains only placeholders and can create a visibly watermarked draft.

The checked-in result files are a complete measured dataset. From this directory,
recompute `data.json` from the raw observations and render the full table:

```sh
rtk proxy python3 summarize.py
rtk proxy python3 render.py data.json --out benchmark-table.svg --png
```

The main social card deliberately uses the focused three-column version:

```sh
rtk proxy python3 render.py data.json --focused --out benchmark-table-focused.svg --png
rtk proxy python3 render-methodology.py
```

To experiment with an empty draft without replacing the measured data:

```sh
rtk proxy python3 render.py data.example.json --draft --out benchmark-table-draft.svg --png
```

The renderer refuses a non-draft output if a number is missing. It also keeps
the metrics visually separate:

| Field | Meaning |
| --- | --- |
| `shortReplyP95Ms` | p95 text-update lag for the 5 KB stream, in ms |
| `longReplyP95Ms` | p95 text-update lag for the 100 KB stream, in ms |
| `busyJsP95Ms` | p95 text-update lag for the 100 KB stream under the defined JS load, in ms |
| `mountMs` | open-chat/mount measurement, in ms; it is **not** a streaming p95 metric |
| `busyJsHitches` | count of UI stutters during the busy-JavaScript streaming run |

The exact fixture, device/simulator, build type, aggregation, and number of
runs belong in `meta`. Add `caveat` only when it is required to keep a
comparison honest (for example, a renderer with an early completion marker).
For the focused mount run, duplicate the verified React `<Text>` mount value in
both React rows so the published table remains comparable without implying two
separate mount experiments.

`--focused` uses `busyJsP95Ms`, `busyJsHitches`, and `mountMs` only. It is the
primary social-card layout: three direct measures of text responsiveness,
UI frame gaps, and chat-mount time. The full layout preserves the short and
long streaming scenarios for readers who want the broader comparison.
The optional `meta.punchline` is reserved for a verified comparison statement;
leave it `null` when the benchmark has not substantiated one.

The auditable result set is [`raw-streaming.json`](raw-streaming.json),
[`raw-mount.json`](raw-mount.json), and [`summary.json`](summary.json): 57
verified runs in total (45 streaming and 12 mount), with three repetitions per
reported measurement. The measurement definitions, including the UI-stutter
threshold of more than 1.5 frame budgets, are documented in
[`methodology.html`](methodology.html).
