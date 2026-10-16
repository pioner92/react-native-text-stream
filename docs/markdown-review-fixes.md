# Markdown review follow-up

This follow-up checks the second review against the working tree rather than
assuming its proposed causes and patches are correct. It preserves the existing
native rendering architecture and does not introduce a JavaScript parsing loop.

| Review item | Verification / disposition |
| --- | --- |
| 1 | Confirmed list/emphasis and whitespace defects; exact-output regressions and parser fix. |
| 2 | Confirmed CommonMark/GFM mismatch; flavor-aware projection. |
| 3 | Confirmed iOS attribute/attachment loss; complete attributed rebuild and image-state preservation. |
| 4 | Confirmed Android plain-text preparation regression; Markdown-only asynchronous guard restored. |
| 5 | Confirmed stale-span accumulation, but not the proposed zero-length mechanism; suffix cleanup preserves watchers. |
| 6 | Confirmed inconsistent Android fence header; stable reserved band and `CODE` fallback. |
| 7 | Repeated double parsing was already cached; remaining raw table-image syntax fixed, including header/first-row seams. |
| 8 | Confirmed fence recognition defects; opening/closing tail validation fixed. |
| 9 | Confirmed separator inherited code flags; flag ordering fixed. |
| 10 | Semantic task flags and VoiceOver spacing fixed; blanket double-indent diagnosis not established. |
| 11 | Table strike drawing, heading metrics, unused Android history and JNI lookups addressed; permanent JS-thread retention claim not established. |
| 12 | Ignore rules added; inflated media-size estimate corrected; unrelated artifacts preserved. |

## Confirmed parser defects

- A streaming `* hello` list item and `$5 * 2` could acquire a synthetic trailing
  star. Opening emphasis delimiters now require a non-whitespace payload when
  the next byte is available; closing delimiters can still precede whitespace.
  Exact-output tests replace the weak substring check for the multiplication case.
- CommonMark streaming no longer adds or hides strike delimiters as if GFM were
  enabled. `~~gone` remains literal in CommonMark.
- The fence guard rejects backticks inside a backtick opening fence's info
  string and requires a closing fence to have only trailing whitespace.
- A list-to-code separator is emitted before code flags are enabled, so the
  preceding list line does not acquire the code background/header attributes.
- Incomplete table-cell images now expose their alt text without showing their
  partial destination. No image attachment or image request is created inside
  the cell. Terminal parsing still uses the exact source. The prior rejection
  cache had already removed repeated double parsing; this change addresses the
  remaining visible raw-image syntax and accepts the cell projection directly,
  including provisional headers and the first body row. Pending image projection
  no longer scans the complete document separately for fences: MD4C validates
  acceptance. A literal fenced candidate pays one speculative parse plus exact
  fallback on its first snapshot, then one parse per same-line continuation.
- Generated task-marker runs carry explicit task/checked bits. A literal `[x]`
  after a list hard break or in a later list paragraph is no longer enough to
  create a native checkbox. The run structure and UTF-16 offsets are unchanged.

The streaming tail recognizer remains bounded and intentionally narrower than a
complete CommonMark inline parser. Final parsing remains MD4C over exact source.

## Platform fixes

iOS rebuilds the complete attributed Markdown document when a prepared canvas
is promoted to TextKit after a style/width change. This restores task attachments,
table attachments and code-header paragraph spacing together. Compatible image
attachments are reused using their actual content width, preserving loaded or
in-flight state. When content width changes, a new attachment adopts the decoded
image and recalculates geometry without returning to a loading placeholder. Task accessibility text includes a real separator before its
label, with offset-preserving zero-width padding excluded from VoiceOver. Heading baseline offset scales with the heading font under fixed line
height. Table strikethrough geometry is prepared with each immutable cell layout;
drawing visits only the visible rows and their precomputed strike rectangles.

Android defers main-thread preparation only for Markdown; plain text retains its
synchronous measurement path. Code fences reserve the same header band before
and after the language token completes, including the `CODE` fallback. Heading
line height is calculated when paragraph spans are built, avoiding another span
query on every measured line.

The span-accumulation issue was reproduced on Android, but the review's proposed
zero-length mechanism was inaccurate: stale nonzero formatting spans survived
replacement. Suffix-owned formatting is removed before replacement, while
`TextWatcher` and `SpanWatcher` objects stay attached for DynamicLayout. A device
regression exercises 100 consecutive replacements.

## Performance and scope

Android already computes its per-view document diff in Kotlin. Its JNI parser
now disables the redundant C++ diff/history snapshot; other consumers retain
the default behavior. Tests compare parsing output with tracking enabled and
disabled and exercise re-enabling it. JNI classes and constructors are resolved
once during library initialization instead of on every parse call.

[Paired per-case host timings](benchmarks/markdown-review-paired-parser-host-2026-10-15.json)
compare the exact saved pre-review parser (`a467f622…`) with the final parser
(`c737a590…`). Five alternating rounds pair each baseline/current workload before
moving to the next, reducing load drift between long runs. With default history,
retained streaming snapshots at roughly 100 KiB measured:

| Workload | Before (ms) | After (ms) | Change |
| --- | ---: | ---: | ---: |
| Ordinary Markdown | 0.7669 | 0.7729 | +0.8% |
| Table header | 0.7706 | 0.7812 | +1.4% |
| First table cell | 0.7841 | 0.7655 | −2.4% |
| Pending image in table | 0.7988 | 0.8068 | +1.0% |
| Long active table | 2.5101 | 2.5102 | approximately 0% |

The same [paired Android-mode comparison](benchmarks/markdown-review-paired-android-parser-host-2026-10-15.json),
with unused C++ history disabled, measured 14.4% less parser time for ordinary
Markdown, 13.5% for table headers, 13.9% for first cells, 9.7% for a pending
image in a table, and 2.8% for the long table. This is a host parser comparison,
not a measured increase in Android frame rate.

The broad [default-history](benchmarks/markdown-review-parser-host-2026-10-15.json)
and [Android-mode](benchmarks/markdown-review-android-parser-host-2026-10-15.json)
runs are retained as raw evidence. They include load-related outliers while
native builds/test processes were active; their largest apparent speedups and
slowdowns should not be treated as reliable effects of this patch. These host
measurements exclude JNI lookup savings, native layout, network and frame pacing.

The claim that all retained parser buffers live permanently on the JS thread
was not established. The current prepare and Markdown workers intentionally
reuse parser allocations. No buffer-lifetime rewrite was made on that assumption.

Nested indentation was not treated as an automatic cross-platform defect:
iOS's `headIndent` applies to continuation lines, so the presence of source
prefix spaces does not alone prove that the first line is indented twice.

The review's roughly 991 MB Git estimate included ignored dependencies. The
unignored media files at inspection totalled about 97 MiB. The two temporary
visual entry/config files mentioned in the review were already absent.
`.argent/`, `__pycache__/` and `*.pyc` are now ignored. Existing media and historical
benchmark evidence were preserved; there is no blanket deletion or staging.

## Validation

- Native C++ regression suite: passed normally and under ASan/UBSan on the final
  parser `c737a590…`; covers byte boundaries, exact terminal parsing, source
  replacement, dialect changes, projection acceptance/rejection and parse counts.
- iOS: final native build passed on the dedicated `StreamText Markdown` simulator.
  All 79 tests across 7 Markdown suites passed, including an explicit canvas-to-
  TextKit transition, preserved code/task attributes, table strike geometry and
  loaded quote-image resize with one network request. See the
  [iOS validation report](benchmarks/markdown-review-ios-validation-2026-10-15.json).
  The saved [iOS screenshot](../media/markdown-announcement/review-ios/current-heading-code-aligned-strike.png)
  verifies a wrapped heading, both code-header variants, and aligned table
  strikes alongside unstruck plain cells. A bare-CoreText before/after pixel
  comparison was not performed; this validates the final implementation, not
  an isolated platform-baseline diagnosis.
- Android: final native build and 5/5 dedicated-emulator instrumentation tests
  passed, including 100 suffix replacements with prefix preservation in both
  stored text and DynamicLayout. Focused JVM tests passed 27/27. The earlier
  broad harness passed 75 applicable Markdown tests plus 2 plain resize tests;
  its earlier parser hash is explicitly retained in the
  [Android validation report](benchmarks/markdown-review-android-validation-2026-10-15.json).
  A current-build visual harness passed 1/1; the saved
  [Android screenshot](../media/markdown-announcement/review-android/current-header-heading-tasks-table.png)
  shows the code header spacing, rounded code surface, task controls and table.
- Example TypeScript check, fixture-server regression and whitespace checks passed.

Visual inspection also identified a pre-existing Android task-list limitation:
a wrapped label continues under the checkbox rather than under the label text.
It is recorded in the Android report and is not presented as fixed by the
semantic task-marker change.

One initial Android instrumentation invocation unintentionally targeted all
three connected devices: the dedicated emulator, Pixel 8a and Samsung SM-A135F.
It installed/ran the library instrumentation APK, not the example app. Subsequent
instrumentation was restricted to `emulator-5556`; that rerun passed all five
device tests. This incident is not presented as authorized physical-device QA.


## Additional review checked on 2026-10-16

The later seven-domain review was checked against the current working tree.
The earlier validation above remains historical evidence for 2026-10-15.

- Confirmed literal-block preview corruption: generic inline projection could
  add closing markers inside indented code/HTML, or remove unfinished literal
  syntax. The parser now rejects these previews and retains exact source text.
  Terminal parsing still uses the original input. A rejected probe on an earlier
  HTML line is not cached onto a later normal Markdown paragraph. Regression cases include
  trailing whitespace, opener-only and half-link tails, and retained/fresh parser
  equivalence beyond 8 KiB with 17/128/1024-byte chunks. A mixed BOM/emoji/CJK,
  code and GFM-table fixture runs through both streaming C APIs and is compared
  to the matching exact API at finality.
- Confirmed C ABI hardening gap: generated projection length is checked against
  MD4C's 32-bit input size before conversion. This guard is not presented as a
  runtime test allocating a 4 GiB input.
- Confirmed iOS presentation resets: width changes now update rich attachments
  without replacing parser state; a color-only change restyles the delivered
  document without restarting parsing. A patch arriving with old presentation
  attributes is restyled before application. Loaded/in-flight image state is
  preserved through the same attachment reuse logic.
- Confirmed iOS long-paragraph patch expansion: the extra backward paragraph
  scan is bounded to 1024 UTF-16 units. The actual parser change boundary is
  never skipped. Code-block cluster invalidation remains intact for correct
  header and paragraph spacing. This reduces attributed-text replacement;
  TextKit can still reflow an entire long paragraph. No new frame-rate or
  200 KiB Split View drag benchmark is claimed.
- Confirmed JNI allocation handling gap: allocations and array writes are
  checked before further JNI calls. Materialization errors clear the pending
  exception and return null for the existing Kotlin plain-text fallback.
  Block/cell/run sizes are validated before Java integer conversion.
- Confirmed CI wiring gap: `test:fixtures` runs the HTTP fixture and scroll-follow
  suites using Node's test runner; Checks invokes it. The device harness script
  now starts, probes and cleans up the rich fixture server on port 9882 and adds
  Android port reversal. A native harness case exercises an authenticated stream
  with Cookie/Authorization and a response Set-Cookie, then asserts that the
  image request carries neither credential using the same expected fields on both platforms.
- Confirmed API documentation omissions: `recordDisplayLog`, `StreamTextFormat`
  and `StreamTextMarkdownFlavor` are documented. Raw media/build-log ignore rules
  are narrow; final MP4 exports remain eligible for Git. Benchmark JSON is
  ignored by default and cited evidence must be included explicitly.

The claims about appearance-dependent raster/token styling and per-patch
attachment scanning were not established as new regressions by these checks.
The task-marker cache already keys resolved RGBA values; dynamic UIColor trait
propagation on worker/prepared paths remains a separate test gap. No unsupported
claim about constant-time rendering, automatic appearance adaptation or a
measured frame-time improvement is made.

The review's "missing files" are present locally and their imports/relative
links resolve. They must travel with the feature when committing:
`server/fixtures/markdown-progressive.mjs`,
`example/src/MarkdownProgressiveStress.tsx`,
`example/__tests__/markdown-progressive.harness.tsx`,
`example/__tests__/markdown-review-ios.harness.tsx`,
`packages/react-native-text-stream/android/src/androidTest/java/com/textstream/StreamTextFlowTest.kt`,
`docs/markdown-progressive-blocks.md`, and this document. Referenced benchmark
JSON and screenshots are evidence dependencies too. A working-tree check does
not establish that an earlier incomplete commit is mergeable.

Validation for this follow-up:

- Native Markdown suite under ASan/UBSan: passed.
- Package build/type declarations: passed; package JS unit tests: 153/153.
- Example JS unit tests: 10/10; example/harness TypeScript check: passed with the
  existing `--ignoreDeprecations 6.0` CI option.
- Node HTTP/scroll fixture tests: 5/5.
- iOS Debug simulator build: passed; all seven Markdown harness suites: 84/84,
  including color-only updates, width-only attachment layout, long Unicode
  paragraph patch offsets, one in-flight image request across repeated width
  changes, and image credential isolation.
- Android native CMake build: all four ABIs passed; JVM tests: 62/62; focused
  instrumentation: 5/5 on `emulator-5556` (`StreamText_Markdown_Tests`) only.
  Connected physical phones were not used. The new credential-isolation harness
  case also passed on the dedicated Android emulator (1/1 selected, 22 skipped).
- Relative links in the three Markdown docs and whitespace checks: passed.
