# StreamText rich Markdown announcement

Final local export: `streamtext-rich-markdown-twitter-v4.mp4` — 24.00 seconds,
1080×1920, 30 fps, H.264/yuv420p, 2.70 MB. Full decoding verified without errors.

Recaptured on 2026-10-16 with the current reviewed native code in a Release build.
The first frame shows READY before the request; the final frames show COMPLETE
and the complete closing sentence. Image loading, checklist/nested lists, table
header/rows and TypeScript/JSON code are all visible. No recording-tool watermark.
See `v4-verification.json`, `v4-contact-sheet.jpg`, and `v4-export-preview.png`.

English simulated product-agent answer: “Turn my focus app idea into a launch
plan, with a UI direction and starter code.” It develops the fictional Daylight
focus app, including a local concept illustration, formatted prose, checklist,
nested launch steps, comparison table, quote, link and highlighted TS/JSON code.

The app uses the production native `streamFetch` → `StreamText` Markdown path.
The loopback server emits eight UTF-8 bytes every 100 ms (about 19 seconds for
this fixture); no live AI generation is claimed. Auto-scrolling belongs to the
example screen, while Markdown parsing/layout/image loading are native.

Build the iOS example in Release with `ENTRY_FILE=index.markdown-announce.js`,
start `node server/rich-markdown-server.mjs`, and open the dedicated simulator.
The v4 capture starts before Generate launch plan and ends after COMPLETE.
Only static time after completion is cut; every streaming interval is retained
at 1× playback speed. Static-frame trimming is disabled. The original 26-second
capture is saved under `.argent/recordings/` (exact path in `v4-verification.json`).

The export crops only the system safe areas, scales the app to 1920 pixels high,
and pads to 1080×1920. Encoding is H.264/yuv420p with fast-start MP4. No audio was
requested. Raw capture and exported video remain local, untracked artifacts.

See `docs/markdown-visual-stress.md` for the wider iOS/Android test matrix and
renderer defects caught before recording.

The v2 pass adds inset language headers, a 6–8 pt/dp label-to-code gap and 4 pt/dp
corners, fixes transitional emphasis/table syntax, and respects manual scrolling.
Review evidence and limits: `docs/markdown-streaming-polish.md`. The earlier export
and intermediate takes are retained locally.

The v3 pass exposes the table header and growing rows immediately after recognition,
and shows an image placeholder while the destination is still arriving. Image requests
only begin after the URL is complete. The final capture has no recording-tool watermark.
Reviewed transition frames and the final COMPLETE/code frame; fully decoded the export.
The historical v3 capture is retained. See `v3-verification.json` for its export settings
and `docs/markdown-progressive-blocks.md` for implementation, tests and limitations.

Historical v3 review sheets: `v3-export-contact.jpg`, `v3-export-image-seam.jpg`,
`v3-export-table-seam.jpg`, and `v3-export-preview.png`.
