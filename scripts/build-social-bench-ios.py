"""Build one Release binary for matched streaming + mount social-card measurements."""
from pathlib import Path
import os
import subprocess

root = Path(__file__).resolve().parents[1]
config = root / 'example/src/bench/config.ts'
original = config.read_text()
replacements = {
    '} = { reps: 2 };': "} = { scenarios: ['S1', 'S2', 'S4'], variants: ['A1', 'A2', 'B1', 'B2', 'D'], reps: 3 };",
    "export const LAYOUT: 'box' | 'bubble' | 'block' = 'block';": "export const LAYOUT: 'box' | 'bubble' | 'block' = 'bubble';",
    "export const TAG = 'current';": "export const TAG = 'social-ios-v2';",
}
modified = original
for before, after in replacements.items():
    if modified.count(before) != 1:
        raise RuntimeError(f'Config changed; inspect before building: {before}')
    modified = modified.replace(before, after)
config.write_text(modified)
try:
    subprocess.run([
        'rtk', 'proxy', 'xcodebuild',
        '-workspace', 'TextStreamExample.xcworkspace', '-scheme', 'TextStreamExample',
        '-configuration', 'Release', '-sdk', 'iphonesimulator',
        '-destination', 'generic/platform=iOS Simulator', '-derivedDataPath', 'build',
        'ARCHS=arm64', 'ONLY_ACTIVE_ARCH=YES', 'CODE_SIGNING_ALLOWED=NO', 'build',
    ], cwd=root / 'example/ios', env={**os.environ, 'ENTRY_FILE': 'index.social.js', 'FORCE_BUNDLING': '1'}, check=True)
finally:
    if config.read_text() != modified:
        raise RuntimeError('Config changed during build; refusing to overwrite it.')
    config.write_text(original)
