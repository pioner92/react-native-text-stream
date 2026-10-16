# Early rich blocks during streaming

The native renderer now exposes rich content before the entire Markdown construct
has arrived. This addresses a presentation pause: the network can keep delivering
bytes while a previously withheld table or image produces no visible change.

- A leading-pipe GFM table header is previewed while its first cell is still
  arriving, before its delimiter exists. Subsequent cells appear as they arrive;
  the real delimiter confirms the table and supplies alignment. Invalid
  continuation or a terminal status restores the ordinary source interpretation.
  The active body row fills as its cell text arrives. Column widths divide the
  available width: adding a newly discovered column can change that division,
  but typing another letter within an existing column does not.
- A recognized fenced code block has a visible empty code line immediately,
  including before its language or first body line arrives. This uses MD4C's
  existing block callback, not another source projection or a JavaScript timer.
  An unfinished language token is not guessed: `py` → `pyt` → `python` keeps the
  same empty surface, then the completed info token supplies the header label.
- Inline emphasis, inline code, link labels, headings, list items and quotes
  already expose their readable text while open. Native seam tests now exercise
  these prefixes without sending a closing marker or a later transport chunk.
- An unfinished image produces an image block with a pending state and no URL.
  Its placeholder appears while the alt text or destination is still arriving.
  Only a completed destination can enter the native image loader.
- Pending and loading placeholders retain their size. Loading does not prevent
  subsequent text from rendering. The loaded image can still change height: plain
  Markdown supplies no intrinsic dimensions before the resource is fetched.
- Terminal statuses reparse the exact source, even without a new byte. An
  unfinished image then follows ordinary final Markdown semantics rather than
  retaining an artificial pending attachment indefinitely.

The network, parser and rich block presentation remain native. No JavaScript
Markdown renderer or animation loop was added. Existing image caching,
deduplication and cancellation continue to apply after a real URL becomes available.
This is still a full-prefix MD4C parser with bounded tail recognition, not a new
incremental parsing engine. Existing completed blocks are retained when their
metadata and the changed suffix allow it; the active table still requires native
cell layout when its content changes.

Early image recognition is limited to the active final line within an 8 KiB
window. An unterminated line longer than that window, or an image opener on an
earlier line, can fall back to ordinary parsing until enough syntax arrives.
The final parse always uses the exact source. This bounded policy is not a
promise that every pathological Markdown prefix will have a placeholder.

Android additionally retains one acknowledged styled snapshot per worker. When
only a pending URL grows and the parsed document, width, color and text style are
unchanged, it reuses that snapshot instead of rebuilding earlier table cell
layouts. Clear/source reset releases it. iOS shares one pending/loading raster at display resolution;
alt text is retained for accessibility and drawn once if the load fails.

## Reproduce the visual seams

Start `node server/rich-markdown-server.mjs`, then open the example's Markdown
visual stress screen and select **Early blocks**. The `first`, `cells`, `separator`,
`header`, `row`, `rollback`, `fence`, `language`, `code`, `image`, `label`, `url`
and `loading` buttons hold a real native HTTP stream at that stage
for 30 seconds. The server subsequently finishes the same source. **Replay seam**
restarts the hold. Light/dark and full-width/220 dp controls allow layout checks
without changing the source. No final status is sent during the hold, since final
parsing intentionally restores ordinary Markdown semantics.

`example/__tests__/markdown-progressive.harness.tsx` checks the platform boundary:
visible pending images, zero requests before URL completion, exactly one request
after it, later text during loading, header-only tables, growing cells, resize,
accessibility, source replacement and terminal statuses without new bytes.
`tests/native/markdown_test.cpp` covers parser byte boundaries and suffix replay.

## Provisional table and empty-code update

This update replaces the hidden-header policy described in the historical v3
comparison below. An isolated `|` is still ambiguous; a readable leading-pipe
cell starts the preview. Headers without a leading pipe remain readable text
until their real delimiter confirms the table. A source-pointer check ensures
that MD4C accepted the actual provisional header, rather than an unrelated
earlier table, before publishing a projected document. Literal HTML/code contexts
and terminal states retain ordinary Markdown interpretation.

On the same 8-byte announcement fixture, the first table now appears at byte
896 instead of 944: six delivery intervals earlier (600 ms at the demo's
100 ms interval). This is parser availability, not measured mobile render time.
[Appearance check](benchmarks/markdown-provisional-appearance-2026-10-15.json).

The focused native harness includes 20 tests, including provisional header
growth, alignment confirmation, subsequent rows, invalid continuation, all four
terminal statuses without new bytes, resize/replacement, empty code surfaces,
language-prefix stability, and eight other early-content families.

Normal C++ tests, ASan/UBSan, the example TypeScript check, and all 13 held HTTP
prefixes pass. The parser source hash for this update is
`b8a58c77079cb55b3f3d4a590b09eb77405f7e7ae0e71fe7ad80aec881c28c18`.

The native parser update passes all six Markdown suites on the dedicated iOS
simulator (76 tests), and the dedicated Android emulator (74 passed, two
iOS-only tests skipped). The 20 focused progressive tests pass on both platforms.
These checks exercise the native renderer, including status-only changes and
chunk boundaries; they are not a physical-device frame-pacing measurement.
Saved validation summaries: [iOS](benchmarks/markdown-provisional-ios-validation-2026-10-15.json)
and [Android](benchmarks/markdown-provisional-table-android-2026-10-15.json).

The iOS visual pass inspected the [first partial cell](../media/markdown-announcement/provisional-table-ios/first-dark-clean.png),
[growing header](../media/markdown-announcement/provisional-table-ios/cells-dark.png),
partial delimiter, first body row, invalid-header rollback, the
[empty code surface](../media/markdown-announcement/provisional-table-ios/fence-dark.png),
unfinished language, code content, and a light 220-point column. These held real
HTTP prefixes showed the intended early surfaces without exposed delimiters,
overlapping cells or clipped code labels in the inspected frames.

The Android visual pass caught a gap in the assertions: an empty code run had
height and monospaced styling, but no painted background. Android's paragraph
range helper had excluded newline-only code paragraphs. The renderer now retains
that drawable line and distinguishes its inclusive newline from a plain separator
between separate fences. Unit regressions cover these boundaries. After this
additional fix, the full six-suite Android rerun again passed 74 tests with two
iOS-only skips (43.179 seconds). The final
[empty-fence frame](../media/markdown-announcement/provisional-table-android/fence-dark-full-final.png)
visibly contains the rounded background before any code body arrives. Android
visual checkpoints also cover the first header cell, a growing second cell and
the partial delimiter. Because the input bridge did not deliver taps, these
real-HTTP screens were mounted directly through a temporary example entry;
that entry and its private Metro configuration were removed afterward. This
Android pass does not claim successful interactive navigation or a full visual
matrix of every stage and theme.

A dedicated active-table workload caught a failed-projection/reparse path once
the real delimiter fell outside the 8 KiB recognition window. Consecutive pipe
rows now bypass that impossible-header projection. The final 100 KiB active-table
median is 2.60 ms versus 2.94 ms for the saved pre-change parser in the same run;
the intermediate faulty version took about 5.10 ms in the earlier run. Other
100 KiB retained-parser workloads are approximately within 3% of their baseline.
These are host measurements with development processes running, not a general
speedup or mobile FPS claim. [Raw comparison](benchmarks/markdown-provisional-parser-host-2026-10-15.json).

## Rejected projection cache (review finding 1)

The [original review probe](benchmarks/markdown-rejected-projection-audit-2026-10-15.json)
confirmed that rejected projections inside HTML, indented code, or a later table
row performed two full MD4C parses on every chunk. The parser now remembers a
rejected context only after MD4C proves that the candidate is literal code/HTML
or a suppressed image inside a table cell, and the exact-source fallback succeeds.
Subsequent growing snapshots of that same line skip the rejected projection and
perform one full parse. The first rejection in a new ambiguous context can still
perform two parses; unproven contexts keep the conservative fallback.

The cache compares the entire saved source prefix exactly and invalidates on
source replacement, truncation, CR/LF, dialect or BOM-policy changes, and terminal
parsing. Table-cell suppression ends when the unfinished image closes. This
avoids guessing whether indentation belongs to a code block or a lazy paragraph
continuation. The tradeoff is one retained source snapshot per parser after a
rejection; cache hits compare bytes but do not copy the prefix on every chunk.

Test-only instrumentation counts actual MD4C invocations. Eight contexts each
exercise 40 growing snapshots: two calls initially, then one call for each of
the next 39 snapshots, with output compared against ordinary exact-source parsing.
Additional seams cover context exit, line endings, a source replacement outside
the last 8 KiB, terminal state, dialect/BOM changes, valid lazy continuation and
resumption of inline formatting after a table image closes. The normal native
tests, ASan/UBSan and example TypeScript check pass.

The [host comparison](benchmarks/markdown-rejected-projection-performance-2026-10-15.json)
measures five rejected-tail workloads with growing bytes and a retained parser.
At roughly 100 KiB, medians fall from 1.46–1.55 ms to 0.77–0.83 ms (45–49%).
Other retained streaming workloads in the same run vary by about −3% to +3.4%.
These are host parser measurements, excluding mobile layout, network and frame
pacing. The parser remains a full-prefix parser. Source SHA-256:
`a467f6224086bbf0509d5f294c270566fcb4aa9a415b0baa4966413fd779e85a`.

Both dedicated native builds pass all six Markdown suites with this source:
[iOS](benchmarks/markdown-rejected-projection-ios-2026-10-15.json) passes 77/77;
[Android](benchmarks/markdown-rejected-projection-android-2026-10-15.json) passes
75 with two iOS-only skips. The progressive suite now has 21 tests, including
native chunk delivery through four rejected contexts, Unicode growth and
terminal status. This internal parser change was validated through native
output assertions; no new visual recording or physical-device profiling was
performed for this review fix.

## References

- [streaming-markdown](https://github.com/thetarnav/streaming-markdown/blob/main/smd.js)
  illustrates progressively creating table rows and cells.
- [Streamdown PR #504](https://github.com/vercel/streamdown/pull/504) proposes an
  image placeholder for an incomplete streaming destination. It was open when
  inspected; this implementation does not depend on that PR or its package.

These are architectural references. No external renderer is included in the native
pipeline, and their performance claims are not evidence for this implementation.

## Historical v3 comparison: remaining visual differences

The v3 pass is **not equivalent** to the referenced implementations, and passing
seam tests does not establish a polished, stable visual transition.

An executable callback comparison on the same announcement (8-byte chunks)
found that `streaming-markdown` emitted its table/header text at byte 896,
whereas the native parser first exposed a table at byte 944. At the demo pacing
this is six extra delivery intervals, or 600 ms. The v3 native projection hid the
candidate header until a complete separator is available. The reference parser
optimistically starts a table while the leading-pipe header is still arriving.
This measurement is parser availability, not device render latency.

The iOS pending placeholder is 120×48 points; the loaded block adopts the image
size constrained to the column. Android uses another compact placeholder sizing
policy. The v3 image transition visibly grows in width and height. Tests only
assert stable pending→loading geometry, not pending→loaded geometry.

Current Streamdown main (also inspected directly, not inferred from PR #504)
contains a fixed `h-24 w-48 animate-pulse` incomplete-image skeleton. With the
standard spacing scale it is 192×96 CSS pixels. It is replaced by an ordinary
`max-w-full` image, and without explicit dimensions this code does not guarantee
stable loaded geometry. The PR is a reference for early visibility, not proof
of a no-shift image experience. We did not run a browser visual comparison of
Streamdown; this part of the comparison is based on its component source.

This comparison motivated the provisional table-header update above. Image
geometry is a separate concern: exact no-shift sizing needs dimensions/aspect-ratio
metadata or a fixed container that remains fixed after load; simply guessing
16:9 cannot promise correct geometry for every image. Inline images need a
separate policy.

[Recorded comparison](benchmarks/markdown-reference-appearance-audit-2026-10-15.json).

## Parser measurements

The [host comparison](benchmarks/markdown-progressive-parser-host-2026-10-15.json)
uses the same workloads on `cf8bd00` and the final working-tree parser, compiled
with `-O3`. Five batches of 20 parses follow five warmups. Selected medians for a
retained parser and a roughly 100 KiB source are:

| Open streaming snapshot | Previous, ms | Current, ms |
| --- | ---: | ---: |
| Pending image label | 0.821 | 0.895 |
| Pending image URL | 0.928 | 0.846 |
| Table header | 0.850 | 0.784 |
| First table cell | 0.839 | 0.768 |

These are parser costs on the development Mac, with other development work
running. The small differences vary between runs and do not establish a general
speedup. The current renderer now produces visible content where the previous
version withheld it. These measurements exclude native layout, image download,
GPU work and device frame pacing; they are not a claim of hitch-free rendering.

## Historical v3 validation scope

The final parser source SHA-256 is
`d5ffc7b164204a352fe6e2766b2c60c46ed6bc61dbf80255ceec62cf9ac2ca79`.
The C++ Markdown and syntax checks passed normally and with ASan/UBSan.
The real-socket fixture check passed, as did the example TypeScript check
(with `--ignoreDeprecations 6.0` for the existing `baseUrl` setting).

The dedicated iOS 26.5 simulator (`3151717D-25E3-40D9-8D93-4BB90C4C6C18`,
StreamText Markdown) passed all six Markdown harness suites: 66 tests, no
failures or skips. Pending URL bytes, first-cell growth, source replacement and
status-only terminal updates are exercised through the native platform boundary.

The validation workspace retains unrelated Android line-height edits in
`StreamTextStyle.kt`; these results do not represent a clean-checkout build or
physical-device validation. Visual review samples specific held prefixes and
recorded frames, not every possible Markdown document.

The dedicated Android 15 emulator (`emulator-5556`, Pixel 8,
`StreamText_Markdown_Tests`) passed 64 of 66 Markdown tests across the same six
suites; two existing iOS-only tests were skipped and none failed. All ten new
progressive tests passed on both platforms.

The focused Android worker JVM suite passed 12 tests, including unchanged-document
style reuse and invalidation on style/color/reset changes.
[Worker summary](benchmarks/markdown-progressive-android-worker-2026-10-15.json).

Saved test summaries: [iOS](benchmarks/markdown-progressive-ios-harness-2026-10-15.json)
and [Android](benchmarks/markdown-progressive-android-harness-2026-10-15.json).

Visual checkpoints include a [header before body rows](../media/markdown-announcement/progressive-ios/table-header-dark.png),
a [pending image with an incomplete URL](../media/markdown-announcement/progressive-ios/image-pending-url-dark.png),
and a [loaded image with following text in a narrow light column](../media/markdown-announcement/progressive-ios/image-loaded-light-narrow.png).

The visual pass caught a low-resolution iOS placeholder raster. Both the shared
pending image and the one-time failure fallback now use UIKit display resolution;
this preserves their point size and avoids any per-chunk raster work. The ten
progressive iOS tests were repeated successfully after the pending-raster fix.

Android held-stream frames independently show the [header before body](../media/markdown-announcement/progressive-android/header-dark-full.png)
and [first cell before the row ends](../media/markdown-announcement/progressive-android/row-dark-full.png).

The Android visual pass also caught a long alt label clipped inside its compact
placeholder. Pending/loading now draw a fixed 12 dp `Image` label with a 4 dp
inset, while accessibility and terminal error fallback retain the alt text.
The [final narrow/light frame](../media/markdown-announcement/progressive-android/pending-url-light-220dp.png)
was inspected again, and the ten progressive Android tests passed after the change.
The initial development warning was the existing deprecated React Native deep
import in `src/StreamText.tsx`; it was dismissed locally, not globally suppressed.

Focused visual-fix retests: [iOS](benchmarks/markdown-progressive-ios-retina-2026-10-15.json)
and [Android](benchmarks/markdown-progressive-android-label-2026-10-15.json).

## Announcement video

[The v3 export](../media/markdown-announcement/streamtext-rich-markdown-twitter-v3.mp4)
is a 24.37-second, 1080×1920, 30 fps H.264/yuv420p recording of the native iOS
Release renderer. It uses a simulated English product-agent answer over real HTTP
streaming, eight UTF-8 bytes every 100 ms. No live model generation is implied.
The export trims 2.6 seconds of the previous completed answer before Replay; it
preserves every captured streaming interval at normal speed. System safe areas
were cropped and the app was scaled/padded. No watermark or audio is included.
The complete exported file decoded successfully.

Reviewed the table/header/first-row and image-placeholder transitions, the contact
sheet and the final COMPLETE frame. No raw URL, raw table delimiter, overlap or
clipped code label was seen in those samples. The loaded image still changes height
when its dimensions become known. [Export details](../media/markdown-announcement/v3-verification.json).

Final video review sheets: [image transition](../media/markdown-announcement/v3-export-image-seam.jpg),
[table growth](../media/markdown-announcement/v3-export-table-seam.jpg),
and [export overview](../media/markdown-announcement/v3-export-contact.jpg).
