# Streaming Markdown polish — 2026-10-15

This pass addresses the intermediate-state defects visible in the first announcement,
not just the final rendered document. The demo remains a simulated English product-agent
response over real native HTTP streaming.

## Implementation

- Progressive and final native parsing are separate. An exact final parse also runs
  when the terminal status arrives without another byte. Prepared content uses final mode.
- Tail recognition is limited to an 8 KiB window, including prior table lines. Applying
  a projection can copy the accumulated prefix and scan full-source fence context.
  MD4C still consumes the full prefix; this is not an incremental Markdown parser.
- Fenced code receives language metadata without an extra per-token string in the ABI.
- Native task markers and code surfaces preserve the production native rendering path.
- Suffix updates rewind to the enclosing fenced-code section so retained header/background
  spans cannot overlap new spans. Cluster discovery is a linear run scan; Android does not
  allocate paragraph lists or rescan the full source text for this UI-thread boundary check.
- Code surfaces use 4 pt/dp corners, 8 pt/dp horizontal insets and a separate header
  band with approximately 6–8 pt/dp between the language label and first code glyph.
  Android header metrics are set independently for each visual line so wrapping
  cannot carry the header ascent into later lines. Interior code line spacing stays
  unchanged. Token contrast follows
  the requested foreground color, including explicit light/dark example themes.
- iOS syntax palettes are immutable shared values; non-token runs skip palette resolution,
  and documents without fenced code skip code-paragraph spacing work.
- The example coalesces follow-scroll work to one animation frame, suspends it during
  manual scrolling, and offers a Latest button. It does not restart a scroll animation
  for every incoming chunk. Parsing/network chunks do not travel through this JS logic.
- The announcement has a compact header, a larger response viewport, stable bounds at
  start, shorter table cells and readable multiline source examples.
- Demo pacing is eight bytes every 100 ms, 1.5 times the former 150 ms rate. The image
  request begins when its Markdown URL is complete; hiding a partial URL can otherwise
  look like an idle pause. This changes the fixture's delivery rate, not video playback.

## Verification record

- Dedicated iOS simulator: iPhone 17 Pro Max, iOS 26.5,
  `3151717D-25E3-40D9-8D93-4BB90C4C6C18`; all five Markdown harness suites passed,
  56 tests, zero failures/skips, on the final native implementation.
- Dedicated Android emulator: Pixel 8 / Android 15, `StreamText_Markdown_Tests`,
  `emulator-5556`; all five Markdown harness suites passed, 54 tests, zero failures.
  Two iOS-only image/line-height cases were skipped. The 51 Android JVM tests passed.
  A focused device layout regression also passed across four combinations: line height
  0/24 and split/unsplit syntax runs. It verifies a single 20 dp header band and
  continuation pitch equal to a fence without a language label.
- C++ Markdown/parser and syntax suites passed normally and with ASan/UBSan.
  Regressions inspect every byte prefix of the announcement's emphasis and table
  sections, including split closers, spaced delimiters and two tables separated by prose.
  Suffix replay also deep-compares block shape, cell content and every cell-run field
  against a fresh parse, including retained prefix metadata.
- Package JavaScript tests: 153 passed across six suites. Example TypeScript check,
  four follow-scroll state tests and the real-socket Markdown fixture test passed.
- The iOS code-header screenshot was inspected after the final layout change:
  [header gap](../media/markdown-announcement/ios-code-header-gap-v2.png).
- Android final code surfaces were inspected in [dark mode](../media/markdown-announcement/android-code-dark-v2.png)
  and [light mode at 220 dp](../media/markdown-announcement/android-code-light-narrow-v2.png):
  the language gap is visible, wrapped lines retain normal pitch and the surface is uniform.
  The [completed announcement](../media/markdown-announcement/android-announcement-v2.png)
  also retains the header gap with explicit 24 dp line height.
- During a manual Release stream, scrolling away held the content origin at
  y=287 pt while native content grew from 841 to 1039 pt. Latest resumed following
  and disappeared. [Observed frames](benchmarks/markdown-follow-scroll-ios-2026-10-15.json).

These are scoped Markdown checks. Existing unrelated Android line-height edits in
`StreamTextStyle.kt` remain in the validation workspace and are excluded from this
commit; no clean-checkout or physical-device result is implied.
An initial unfiltered Gradle instrumentation command also ran the temporary library
test APK on connected Pixel 8a and Samsung SM-A135F devices. Those failing diagnostic
runs are excluded from validation evidence. Subsequent instrumentation explicitly
targeted `emulator-5556`; no example/user app was launched on either physical device.

### Video review

The final local export is `media/markdown-announcement/streamtext-rich-markdown-twitter-v2.mp4`:
23.00 seconds, 1080×1920, 30 fps, H.264/yuv420p, 2,432,437 bytes, no audio.
The complete file decodes without errors. The recording preserves real time;
only system safe areas were cropped, followed by scaling/padding for the export.
No frame-speed change or automatic static-frame removal was used.

The intermediate take exposed raw emphasis closers and a raw table-header flash;
these triggered native regressions and were corrected before the final take.
Inspected the final contact sheet and full-resolution code frame, plus dense transition
frames from the same native build. No raw URL, emphasis closer, table-header flash or
code-label overlap was observed in those reviewed scenarios. This is visual sampling,
not proof that every possible Markdown document is defect-free.

[Final preview](../media/markdown-announcement/v2-export-preview.png).

### Release renderer measurement

36 runs completed on the dedicated iPhone 17 Pro Max / iOS 26.5 simulator, arm64
Release, using `index.markdown-rich-bench.js`. Each case has three repetitions;
the native producer emits 512 bytes every 4 ms while JavaScript spends 40 ms busy
every 100 ms. Images are excluded. Native display/layout inspection happens after
timing. [Raw results](benchmarks/markdown-polish-ios-release-2026-10-15.json).

| Markdown workload | Status median ms | Settling marker median ms | Maximum frame median ms | Hitches, sum |
| --- | ---: | ---: | ---: | ---: |
| 5 KiB stream | 61 | 67.05 | 16.67 | 0 |
| 100 KiB stream | 857 | 867.04 | 16.67 | 0 |
| One finished 5 KiB message, cold | 17 | 33.70 | 16.67 | 0 |
| One finished 5 KiB message, warm | 17 | 33.39 | 16.67 | 0 |
| Twenty finished 5 KiB messages, cold | 100 | 134.04 | 72.53 | 6 |
| Twenty finished 5 KiB messages, warm | 91 | 117.06 | 41.05 | 7 |

Status includes producer pacing and JS callback delivery. Settling is the next
three-on-time-frame streak reported by the native monitor; these are simulator
observations, not physical-device guarantees. Bulk mounting still has measurable
hitches. No universal maximum-performance or hitch-free claim is made.

Host evidence: [baseline/current snapshots](benchmarks/markdown-rich-parser-host-2026-10-15.json)
and [streaming/held-tail measurements](benchmarks/markdown-streaming-projection-host-2026-10-15.txt).
The latter measured 83.40 MB/s for a 102,423-byte source with an active incomplete tail
on this host. These are parser-only timings, collected with other development work
running, and do not establish a UI frame rate or a speedup. The deliberately uncoalesced
17-byte workload scans 3,012.74 times the final source size at 100 KiB, illustrating why
native update coalescing remains necessary.
