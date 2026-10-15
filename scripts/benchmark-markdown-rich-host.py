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
int main() {
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
  volatile size_t sink = 0;
  for (const auto& item : cases) for (bool fresh : {false, true}) {
    auto* retained = stmd_create();
    for (int warmup = 0; warmup < 5; ++warmup) stmd_parse(retained, item.second.data(), item.second.size(), true);
    for (int batch = 0; batch < 5; ++batch) {
      const auto start = Clock::now();
      for (int iteration = 0; iteration < 20; ++iteration) {
        auto* parser = fresh ? stmd_create() : retained;
        const auto* doc = stmd_parse(parser, item.second.data(), item.second.size(), true);
        if (!doc || doc->error) return 2;
        sink += doc->textLength + doc->runCount;
        if (fresh) stmd_destroy(parser);
      }
      const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count() / 20;
      std::cout << item.first << ' ' << item.second.size() << ' ' << fresh << ' ' << ms << '\n';
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
    parser.add_argument('--baseline', default='867c265')
    parser.add_argument('--output', type=Path, default=ROOT / 'docs/benchmarks/markdown-rich-parser-host-2026-10-15.json')
    args = parser.parse_args()
    baseline = run('git', 'rev-parse', args.baseline).strip()
    with tempfile.TemporaryDirectory(prefix='streamtext-rich-bench-') as directory:
        temp = Path(directory)
        base = temp / 'base'
        for relative in ['cpp/markdown/StreamMarkdown.cpp', 'cpp/markdown/StreamMarkdown.h', 'ios/StreamMarkdown.h']:
            target = base / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(run('git', 'show', f'{baseline}:{PACKAGE / relative}'))
        (base / 'cpp/markdown/vendor').symlink_to(ROOT / PACKAGE / 'cpp/markdown/vendor', target_is_directory=True)
        objects = []
        for source in ['md4c', 'entity']:
            obj = temp / f'{source}.o'
            run('cc', '-std=c99', '-O3', '-DNDEBUG', '-c', ROOT / PACKAGE / f'cpp/markdown/vendor/md4c/{source}.c', '-o', obj)
            objects.append(obj)
        driver = temp / 'driver.cpp'
        driver.write_text(DRIVER)
        executables = {}
        for label, package in [('baseline', base), ('current', ROOT / PACKAGE)]:
            executable = temp / f'{label}-parser'
            run('c++', '-std=c++17', '-O3', '-DNDEBUG', '-I', package / 'cpp/markdown', driver,
                package / 'cpp/markdown/StreamMarkdown.cpp', *objects, '-o', executable)
            executables[label] = executable
        measurements = {}
        for label, executable in executables.items():
            grouped = {}
            for line in run(executable).splitlines():
                name, size, fresh, duration = line.split()
                key = (name, int(size), fresh == '1')
                grouped.setdefault(key, []).append(float(duration))
            measurements[label] = [dict(case=name, sourceBytes=size, freshParser=fresh,
                medianMs=statistics.median(values), batchesMs=values)
                for (name, size, fresh), values in grouped.items()]
    source = ROOT / PACKAGE / 'cpp/markdown/StreamMarkdown.cpp'
    report = dict(host=platform.platform(), architecture=platform.machine(), compiler=run('c++', '--version').splitlines()[0],
        baselineCommit=baseline, currentCommit=run('git', 'rev-parse', 'HEAD').strip(),
        currentParserSha256=hashlib.sha256(source.read_bytes()).hexdigest(),
        iterationsPerBatch=20, batches=5, warmupParses=5, compileTimeExcluded=True, measurements=measurements,
        limitations='Host parser snapshot timing only, not mobile rendering or network latency. Baseline flattens tables and images and does not highlight code; feature workloads do different work. Fresh includes parser creation/destruction, not a cold process or cold CPU cache.')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(args.output)

if __name__ == '__main__':
    main()
