#!/usr/bin/env python3
"""Check persisted evidence from an actual Panther control bridge exercise."""
import argparse
import json
import plistlib
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('directory', type=Path)
args = parser.parse_args()
root = args.directory
summary = json.loads((root / 'summary.json').read_text())
for key in ('passed', 'hardware_cleanup_verified', 'control_bridge_verified',
            'unprivileged_control_rejected', 'control_stop_revocation_verified'):
    assert summary.get(key), key
running = plistlib.loads((root / 'running-first.plist').read_bytes())
stopped = plistlib.loads((root / 'stopped.plist').read_bytes())
assert running['ControlRXFilterVerified'] and running['RXRunning']
assert stopped['PersistentStopVerified'] and not stopped['RXRunning']
assert stopped['RXUSBSubmitted'] == stopped['RXUSBCompleted']
assert stopped['RXUSBErrors'] == 0
denied = (root / 'unprivileged-control.log').read_text()
assert 'Control open rejected:' in denied
checks = (root / 'control-selftest.log').read_text()
for marker in ('PASS: exclusive/root ABI', 'PASS: process death', 'privilege rechecked after setuid'):
    assert marker in checks, marker
assert 'PASS: hardware stop' in (root / 'control-stop.log').read_text()
if summary.get('auth_prepared_from_live_beacon'):
    prepared = (root / 'control-prepare.log').read_text()
    assert 'PASS: live beacon -> station AUTH request prepared;' in prepared
    assert 'No frame transmitted;' in prepared
print('PASS: real USB -> privileged queue, ownership/expiry/credentials/death/stop; hardware cleanup verified')
