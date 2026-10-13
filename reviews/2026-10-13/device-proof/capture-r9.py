"""Capture the native screen while the R9 device test holds its failing state."""
import json
import datetime
import pathlib
import subprocess
import sys
import time

platform, device_id = sys.argv[1:3]
directory = pathlib.Path(__file__).resolve().parent
evidence = directory / 'evidence.jsonl'
started = time.time()
while time.time() - started < 90:
    records = evidence.read_text().splitlines() if evidence.exists() else []
    for line in reversed(records):
        record = json.loads(line)
        if record.get('platform') != platform:
            continue
        if record.get('label') != 'R9 switch to an empty streamId':
            continue
        recorded = datetime.datetime.fromisoformat(record['timestamp'].replace('Z', '+00:00')).timestamp()
        if time.time() - recorded > 18:
            continue
        output = directory / f'{platform}-r9.png'
        if platform == 'ios':
            subprocess.run(['xcrun', 'simctl', 'io', device_id, 'screenshot', str(output)], check=True)
        else:
            with output.open('wb') as file:
                subprocess.run(['/Users/alex/Library/Android/sdk/platform-tools/adb', '-s', device_id,
                                'exec-out', 'screencap', '-p'], stdout=file, check=True)
        print(output, flush=True)
        sys.exit(0)
    time.sleep(1)
raise SystemExit('R9 capture stage was not reached within 90 seconds')
