# Rich Markdown visual stress — 2026-10-15

Dedicated devices: StreamText Markdown (iPhone 17 Pro Max, iOS 26.5,
3151717D-25E3-40D9-8D93-4BB90C4C6C18) and StreamText_Markdown_Tests
(Pixel 8, Android 15, emulator-5556). No physical-device claims.

## Reproduce

Start `node server/rich-markdown-server.mjs`, run the example, then choose
Markdown → Stress test. Next/Prev select isolated cases; All streams the eight
cases as one document. Replay starts a fresh native request. The Slow setting
splits the UTF-8 source into seven-byte writes every 35 ms. Toggle the theme,
220 dp width, 22 pt text, and explicit 28 pt line height; inspect Top/End and
scroll through content. Source terminal status does not wait for image loading.

The fixture includes all six heading levels; combined emphasis, strike, links,
escapes/entities; nested ordered/unordered/task lists and quotes; aligned,
empty-cell, six-column and quoted tables; JS/TS/JSON/Python/unknown code; a
600-character unbroken code line; inline/linked/large and failed/blocked images;
CJK, RTL, combining characters and emoji; late-closing syntax and references;
and a 60-row table with a final sentinel. All is approximately 4.6 KB.

## Observed findings

- The test screen initially used screen width without its parent padding. The
  harness now uses its actual parent width, avoiding a misleading right-edge clip.
- Android syntax tokens each added a code-block leading margin. A JSON line could
  collapse into a one-character column. Regression fix/verification recorded below.
- iOS explicit line height clipped an image with text before it in the same
  paragraph. Standalone images did not expose this. Regression fix/verification
  recorded below.
- The rich fixture lacked `X-Content-Type-Options: nosniff`; URLSession buffered
  initial bytes for MIME detection. Added the header to make tiny streaming
  writes visible promptly, matching the main streaming fixture.

## iOS visual observations

Inspected every isolated case and the combined message. Full-width typography,
list nesting, aligned tables, code colors, Unicode, image loading/fallbacks and
final sentinels remained visible. Tables at 220 dp and 22 pt wrapped inside their
cells; long unknown-language code wrapped without overlap. Light theme was also
checked. Explicit 28 pt line height exposed the inline-image bug above.

Inspected the 60-row table at the top, rows 22–39, and rows 46–60. Scrolling did not
leave missing rows. The mixed message was inspected across typography/lists,
table/code, image/Unicode/reference, and final-table boundaries, including while
streaming after the nosniff correction. These are manual visual observations,
not a claim of exhaustive CommonMark conformance or a performance benchmark.

## Announcement

`example/index.markdown-announce.js` opens a clean English demo. Its simulated
product-agent response turns a focus-app idea into a Daylight launch plan, with
a local UI concept, checklist, nested steps, comparison table, quote and code.
The response travels over native HTTP in timed byte chunks; it does not call a
live AI service. The video preserves real-time streaming, without speed-up.

The Daylight concept PNG was created locally for this demo. It is not an existing
app or a third-party image. The example.com link is illustrative.

## Android visual observations and final gates

All isolated cases and the combined message were inspected on the dedicated
emulator, including 220 dp, 22 pt, Light and Pitch 28. After the final production
edit, structure and highlighted code were checked again on the rebuilt APK.
The code-column defect no longer reproduced. Root additionally inspected code
and swiped through table rows 18–42 and 32–56; the grid and text remained present.
A saved screenshot is `media/markdown-announcement/android-table-scroll.png`.

Android uses one code/quote/list margin per paragraph instead of one per syntax
run. Ordinary documents skip the code-paragraph scan when no code flags exist.
The iOS fix resets line-height constraints for the entire attachment paragraph,
retains its effective indentation, and uses the remaining width for attachments.

Current focused checks: **42/42 Android JVM tests**, **22/22 iOS rich native tests**,
**one real-socket fixture test**, and the example TypeScript check passed. Debug
Android and Debug/Release iOS builds succeeded. Counts and individual test names
are in [validation JSON](benchmarks/markdown-visual-validation-2026-10-15.json).
These counts are for this pass, not a re-run of every historical suite. Earlier
plain-text Android resize failures remain outside this Markdown visual task.

The fixed iOS inline-image scenario was visually rechecked in Debug with Pitch 28:
the entire 160×80 checkerboard remains visible between Before/after. Evidence:
`media/markdown-announcement/ios-inline-image-fixed.png`. The final Release app
was rebuilt and installed after this check, then used for the announcement.
