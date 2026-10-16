#!/usr/bin/env python3
"""Reproducible host-only -O3 parser timings; no checkout/build artifacts enter the repository."""
import argparse
import hashlib
import json
import platform
import statistics
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = Path('packages/react-native-text-stream')
DRIVER = r'''
#include "StreamMarkdown.h"
#include <chrono>
#include <iostream>
#include <string>
#include <vector>
static STMarkdownParser* createParser() {
  auto* parser = stmd_create();
#ifdef NO_CHANGE_HISTORY
  stmd_set_track_changes(parser, false);
#endif
  return parser;
}
int main(int argc, char** argv) {
  using Clock = std::chrono::steady_clock;
  const std::vector<std::pair<std::string, std::string>> units = {
    {"ordinary", "# Heading\n\n**Bold** and *italic* text, `code`, words and more words.\n\n"},
    {"tables", "| Name | Value |\n| :--- | ---: |\n| **Alpha** | 42 |\n| Other | Long cell text that wraps |\n\n"},
    {"syntax", "```typescript\nconst answer: string = \"Hello\"; // comment\nreturn answer + 42;\n```\n\n"},
    {"images", "![image alt](https://example.test/image.png)\n\n"},
  };
  std::vector<std::pair<std::string, std::string>> cases;
  for (const auto& unit : units) for (size_t size : {5120u, 102400u}) {
    std::string text;
    while (text.size() < size) text += unit.second;
    cases.push_back({unit.first + "-" + std::to_string(size), text});
  }
  for (size_t count : {100u, 500u, 1000u}) {
    std::string text;
    for (size_t i = 0; i < count; ++i) text += "```js\nconst x = 42;\n```\n\n";
    cases.push_back({"fences-" + std::to_string(count), text});
  }
  // Keep an identical finalized prefix and vary only the active transport seam.
  // This measures the progressive path, rather than only finished documents.
  for (size_t size : {5120u, 102400u}) {
    std::string prefix;
    while (prefix.size() < size) prefix += units[0].second;
    for (const auto& tail : std::vector<std::pair<std::string, std::string>>{
      {"pending-image-label", "![A concept"},
      {"pending-image-url", "![A concept](https://example.test/concept.png?key=stream"},
      {"table-header", "| Feature | State |\n| :--- | ---: |\n"},
      {"table-first-cell", "| Feature | State |\n| :--- | ---: |\n| Foc"},
      {"table-provisional-first", "| Fea"},
      {"table-provisional-cells", "| Feature | Sta"},
      {"table-provisional-delimiter", "| Feature | State |\n| :--"},
      {"rejected-image-indent", "    ![alt"},
      {"rejected-table-comment", "<!--\n| Name"},
      {"rejected-table-pre", "<pre>\n| Name"},
      {"rejected-image-table", "| A |\n| --- |\n| first |\n| ![alt"},
      {"rejected-image-html", "<div>\n![alt"},
      {"fence-opening", "```"},
      {"fence-language", "```typescript"},
    }) cases.push_back({tail.first + "-" + std::to_string(size), prefix + tail.second});
    std::string longTable = "| Name | Value |\n| --- | --- |\n";
    while (longTable.size() < size) longTable += "| Alpha | Ready |\n";
    cases.push_back({"long-table-active-" + std::to_string(size), longTable + "| Par"});
  }
  volatile size_t sink = 0;
  for (const auto& item : cases) for (bool fresh : {false, true}) {
    if (argc > 1 && item.first != argv[1]) continue;
    auto* retained = createParser();
    for (int warmup = 0; warmup < 5; ++warmup) stmd_parse(retained, item.second.data(), item.second.size(), true);
    for (int mode = 0; mode <
#ifdef STREAMING_BENCH
         2
#else
         1
#endif
         ; ++mode) for (int batch = 0; batch < 5; ++batch) {
      const auto start = Clock::now();
      for (int iteration = 0; iteration < 20; ++iteration) {
        auto* parser = fresh ? createParser() : retained;
        // Growing bytes exercise a live rejected tail, not only identical snapshots.
        const std::string growing = item.first.rfind("rejected-", 0) == 0
            ? item.second + std::string(iteration, 'a') : std::string();
        const auto& source = growing.empty() ? item.second : growing;
        const auto* doc =
#ifdef STREAMING_BENCH
          mode ? stmd_parse_streaming(parser, source.data(), source.size(), true, false) :
#endif
          stmd_parse(parser, source.data(), source.size(), true);
        if (!doc || doc->error) return 2;
        sink += doc->textLength + doc->runCount;
        if (fresh) stmd_destroy(parser);
      }
      const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count() / 20;
      std::cout << item.first << ' ' << item.second.size() << ' ' << fresh << ' '
                << (mode ? "streaming" : "final") << ' ' << ms << '\n';
    }
    stmd_destroy(retained);
  }
  return sink ? 0 : 1;
}
'''

def run(*args, cwd=ROOT):
    return subprocess.check_output(['rtk', 'proxy', *map(str, args)], cwd=cwd, text=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', default='964ee45')
    parser.add_argument('--baseline-source-dir', type=Path,
                        help='Optional saved package source snapshot instead of a Git revision')
    parser.add_argument('--current-no-change-history', action='store_true',
                        help='Measure current parser with unused C++ history disabled, as on Android')
    parser.add_argument('--rounds', type=int, default=1,
                        help='Repeat paired runs, reversing baseline/current order each round')
    parser.add_argument('--case', action='append', dest='cases',
                        help='Measure a named workload; repeat to pair baseline/current per case')
    parser.add_argument('--output', type=Path, default=ROOT / 'docs/benchmarks/markdown-rich-parser-host-2026-10-15.json')
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error('--rounds must be positive')
    baseline = None if args.baseline_source_dir else run('git', 'rev-parse', args.baseline).strip()
    with tempfile.TemporaryDirectory(prefix='streamtext-rich-bench-') as directory:
        temp = Path(directory)
        base = args.baseline_source_dir.resolve() if args.baseline_source_dir else temp / 'base'
        for relative in ['cpp/markdown/StreamMarkdown.cpp', 'cpp/markdown/StreamMarkdown.h',
                         'cpp/markdown/StreamMarkdownSyntax.h', 'ios/StreamMarkdown.h']:
            target = base / relative
            if args.baseline_source_dir:
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            try:
                target.write_text(run('git', 'show', f'{baseline}:{PACKAGE / relative}'))
            except subprocess.CalledProcessError:
                if relative.endswith('StreamMarkdownSyntax.h'):
                    continue
                raise
        if not args.baseline_source_dir:
            (base / 'cpp/markdown/vendor').symlink_to(ROOT / PACKAGE / 'cpp/markdown/vendor', target_is_directory=True)
        objects = []
        for source in ['md4c', 'entity']:
            obj = temp / f'{source}.o'
            run('cc', '-std=c99', '-O3', '-DNDEBUG', '-c', ROOT / PACKAGE / f'cpp/markdown/vendor/md4c/{source}.c', '-o', obj)
            objects.append(obj)
        driver = temp / 'driver.cpp'
        driver.write_text(DRIVER)
        executables = {}
        source_hashes = {}
        for label, package in [('baseline', base), ('current', ROOT / PACKAGE)]:
            source_hashes[label] = hashlib.sha256((package / 'cpp/markdown/StreamMarkdown.cpp').read_bytes()).hexdigest()
            executable = temp / f'{label}-parser'
            flags = ['-DSTREAMING_BENCH'] if 'stmd_parse_streaming' in (package / 'ios/StreamMarkdown.h').read_text() else []
            if label == 'current' and args.current_no_change_history:
                flags.append('-DNO_CHANGE_HISTORY')
            run('c++', '-std=c++17', '-O3', '-DNDEBUG', *flags, '-I', package / 'cpp/markdown', driver,
                package / 'cpp/markdown/StreamMarkdown.cpp', *objects, '-o', executable)
            executables[label] = executable
        groups = {label: {} for label in executables}
        for round_index in range(args.rounds):
            order = list(executables.items())
            if round_index % 2:
                order.reverse()
            for case in args.cases or [None]:
                for label, executable in order:
                    for line in run(executable, *([case] if case else [])).splitlines():
                        name, size, fresh, mode, duration = line.split()
                        key = (name, int(size), fresh == '1', mode)
                        groups[label].setdefault(key, []).append(float(duration))
        measurements = {}
        for label, grouped in groups.items():
            measurements[label] = [dict(case=name, sourceBytes=size, freshParser=fresh, mode=mode,
                medianMs=statistics.median(values), batchesMs=values)
                for (name, size, fresh, mode), values in grouped.items()]
        for label, package in [('baseline', base), ('current', ROOT / PACKAGE)]:
            actual = hashlib.sha256((package / 'cpp/markdown/StreamMarkdown.cpp').read_bytes()).hexdigest()
            if actual != source_hashes[label]:
                raise RuntimeError(f'{label} parser changed while benchmarking; repeat with stable sources')
    report = dict(host=platform.platform(), architecture=platform.machine(), compiler=run('c++', '--version').splitlines()[0],
        baselineCommit=baseline, currentCommit=run('git', 'rev-parse', 'HEAD').strip(),
        baselineSourceDirectory=str(args.baseline_source_dir) if args.baseline_source_dir else None,
        baselineParserSha256=source_hashes['baseline'],
        currentParserSha256=source_hashes['current'],
        currentChangeHistory=not args.current_no_change_history,
        selectedCases=args.cases, pairedPerCase=bool(args.cases),
        iterationsPerBatch=20, batches=5 * args.rounds, rounds=args.rounds,
        warmupParsesPerRound=5, compileTimeExcluded=True, measurements=measurements,
        limitations='Host parser snapshot timing only, not mobile rendering or network latency. Baseline and current use identical source workloads; progressive final=false mode is measured when supported by that revision. Markdown remains a full-prefix MD4C parse. When a bounded incomplete-tail projection is active it can additionally copy the retained prefix and scan source to avoid changing fenced-code semantics; this report does not claim incremental parsing. Fresh includes parser creation/destruction, not a cold process or cold CPU cache.')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(args.output)

if __name__ == '__main__':
    main()
