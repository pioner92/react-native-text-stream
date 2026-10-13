#!/usr/bin/env python3
"""Render the compact benchmark social card as editable SVG (and optional PNG)."""
from __future__ import annotations

import argparse
import html
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent
W, H = 2000, 1200


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def load(path: Path, draft: bool, focused: bool) -> dict:
    data = json.loads(path.read_text())
    require(data.get("schemaVersion") == 1, "schemaVersion must be 1")
    rows = data.get("rows")
    require(isinstance(rows, list) and len(rows) == 5, "exactly five rows are required")
    expected = ["StreamText", "React <Text>", "React <Text>", "Reanimated", "streamdown*"]
    require([row.get("name") for row in rows] == expected,
            "rows must use the documented order and names")
    for row in rows:
        values = row.get("values")
        require(isinstance(values, dict), f"{row['name']}: values is required")
        all_keys = ("shortReplyP95Ms", "longReplyP95Ms", "busyJsP95Ms", "busyJsHitches", "mountMs")
        required_keys = ("busyJsP95Ms", "busyJsHitches", "mountMs") if focused else (
            "shortReplyP95Ms", "longReplyP95Ms", "busyJsP95Ms", "mountMs")
        for key in all_keys:
            value = values.get(key)
            require(value is None or (isinstance(value, (int, float)) and value >= 0),
                    f"{row['name']}: {key} must be a non-negative number or null")
            if not draft and key in required_keys:
                require(value is not None, f"{row['name']}: {key} is missing")
    return data


def esc(value: object) -> str:
    return html.escape(str(value), quote=True)


def fmt(value: float | None) -> str:
    if value is None:
        return "—"
    # Social-card numbers are deliberately whole milliseconds/counts. The raw
    # JSON and summary retain their original precision for auditability.
    return str(int(float(value) + 0.5))


def render(data: dict, destination: Path, draft: bool, focused: bool) -> None:
    meta = data["meta"]
    def text(x, y, label, size=28, color="#F5F3FF", weight=400, anchor="start"):
        return (f'<text x="{x}" y="{y}" fill="{color}" font-family="Arial, Helvetica, sans-serif" '
                f'font-size="{size}" font-weight="{weight}" text-anchor="{anchor}">{esc(label)}</text>')
    def rect(x, y, width, height, fill, radius=14, stroke=None):
        attr = f' stroke="{stroke}" stroke-width="1"' if stroke else ""
        return f'<rect x="{x}" y="{y}" width="{width}" height="{height}" rx="{radius}" fill="{fill}"{attr}/>'

    parts = [f'''<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">
<defs><radialGradient id="glow" cx="42%" cy="0%" r="70%"><stop offset="0" stop-color="#7350b7" stop-opacity=".22"/><stop offset="1" stop-color="#080B13" stop-opacity="0"/></radialGradient></defs>
<rect width="2000" height="1200" fill="#080B13"/><rect width="2000" height="610" fill="url(#glow)"/>''']
    for index, height in enumerate([28, 48, 36, 56]):
        parts.append(rect(86 + index * 14, 58 + (56 - height) / 2, 8, height, "#B69CFF", 4))
    title = "Fast text. Smooth UI." if focused else "Text rendering, measured."
    subtitle = "Same text. Five implementations. Lower is better ↓" if focused else "Same input. Five implementations."
    parts += [text(160, 97, "StreamText.", 43, weight=700),
              text(1912, 96, str(meta["environment"]).upper(), 19, "#9AA6BB", 700, "end"),
              text(86, 200, title, 67 if focused else 72, weight=700),
              text(88, 272, subtitle, 32, "#9AA6BB"),
              rect(86, 320, 1460 if focused else 1578, 66, "#161927", 13),
              text(112, 362, "STREAMING  ·  100 KB REPLY  ·  BUSY JAVASCRIPT" if focused else "STREAMING UPDATE LAG  ·  p95 ms (estimated)  ·  lower is better", 25, "#D9CEF4", 700),
              rect(1560 if focused else 1678, 320, 352 if focused else 234, 66, "#1D1830", 13),
              text(1736 if focused else 1795, 362, "MOUNT  ·  20 MESSAGES" if focused else "MOUNT TIME · ms", 21, "#D9CEF4", 700, "middle"),
              text(112, 455, "IMPLEMENTATION", 22, "#9AA6BB", 700)]
    centers = [1080, 1400, 1740] if focused else [950, 1240, 1530, 1770]
    headers = ([("Update delay", "est. p95 · ms"), ("UI stutters", "long frames"),
                ("Mount time", "20 × 5 KB · ms")]
               if focused else [("Short reply", "5 KB"), ("Long reply", "100 KB"),
                                 ("Busy JS", "100 KB + JS load"), ("Mount", "open chat · 20 × 5 KB")])
    for x, (title, detail) in zip(centers, headers):
        parts += [text(x, 450, title, 32, weight=700, anchor="middle"),
                  text(x, 484, detail, 21, "#9AA6BB", anchor="middle")]
    keys = (["busyJsP95Ms", "busyJsHitches", "mountMs"] if focused else
            ["shortReplyP95Ms", "longReplyP95Ms", "busyJsP95Ms", "mountMs"])
    for index, row in enumerate(data["rows"]):
        y = 508 + index * 106
        primary = index == 0
        parts.append(rect(86, y, 1826, 94, "#251D37" if primary else "#10141E", 14,
                          "#695695" if primary else "#252B38"))
        parts.append(text(112, y + 42, row["name"], 38,
                          "#B69CFF" if primary else "#F5F3FF", 700))
        if row.get("qualifier"):
            parts.append(text(112, y + 76, row["qualifier"], 25,
                              "#B9A6DA" if primary else "#9AA6BB"))
        for x, key in zip(centers, keys):
            parts.append(text(x, y + 70, fmt(row["values"][key]), 78 if focused else 68,
                              "#B69CFF" if primary else "#E8EAF0", 700, "middle"))
    parts += [text(88, 1100, f"{meta['sample']}  ·  {meta['releaseLabel']}", 25, "#9AA6BB")]
    if data.get("caveat"):
        parts.append(text(88, 1137, "* Markdown renderer; completion marker may arrive early.", 23, "#9AA6BB"))
    parts.append(text(1910, 1167, "DRAFT — VALUES PENDING VERIFICATION" if draft else meta["sourceLabel"],
                      21, "#B69CFF" if draft else "#9AA6BB", 700 if draft else 400, "end"))
    parts.append("</svg>\n")
    destination.write_text("\n".join(parts))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("data", nargs="?", type=Path, default=ROOT / "data.json")
    parser.add_argument("--out", type=Path, default=ROOT / "benchmark-table.svg")
    parser.add_argument("--draft", action="store_true", help="allow null values and watermark the output")
    parser.add_argument("--png", action="store_true", help="also rasterize the SVG with rsvg-convert")
    parser.add_argument("--focused", action="store_true", help="render the three-column social-card variant")
    args = parser.parse_args()
    render(load(args.data, args.draft, args.focused), args.out, args.draft, args.focused)
    if args.png:
        png = args.out.with_suffix(".png")
        subprocess.run(["rsvg-convert", "-w", str(W), "-h", str(H), str(args.out), "-o", str(png)], check=True)


if __name__ == "__main__":
    main()
