# StreamText rich Markdown announcement

Final local export: `streamtext-rich-markdown-twitter.mp4` — 34.03 seconds,
1080×1920, 30 fps, H.264/yuv420p, 3.58 MB. Full decoding verified without errors.

English simulated product-agent answer: “Turn my focus app idea into a launch
plan, with a UI direction and starter code.” It develops the fictional Daylight
focus app, including a local concept illustration, formatted prose, checklist,
nested launch steps, comparison table, quote, link and highlighted TS/JSON code.

The app uses the production native `streamFetch` → `StreamText` Markdown path.
The loopback server emits eight UTF-8 bytes every 150 ms (about 28 seconds for
this fixture); no live AI generation is claimed. Auto-scrolling belongs to the
example screen, while Markdown parsing/layout/image loading are native.

Build the iOS example in Release with `ENTRY_FILE=index.markdown-announce.js`,
start `node server/rich-markdown-server.mjs`, and open the dedicated simulator.
The recording starts before tapping Generate launch plan and ends after COMPLETE.
No playback speed-up or static-frame trimming is used.

The export crops only the system safe areas, scales the app to 1920 pixels high,
and pads to 1080×1920. Encoding is H.264/yuv420p with fast-start MP4. No audio was
requested. Raw capture and exported video remain local, untracked artifacts.

See `docs/markdown-visual-stress.md` for the wider iOS/Android test matrix and
renderer defects caught before recording.
