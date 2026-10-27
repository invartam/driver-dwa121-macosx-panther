#!/usr/bin/env python3
"""Require both driver completion evidence and actual zero-length USB traces."""
import argparse
import json
import plistlib
import re
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('directory', type=Path)
args = parser.parse_args()
root = args.directory
summary = json.loads((root / 'summary.json').read_text())
assert summary.get('passed') and summary.get('tx_usb_empty_probe_verified')
p = plistlib.loads((root / 'stopped.plist').read_bytes())
for key in ('TXUSBProbeVerified', 'TXUSBProbeDrained', 'TXUSBProbeNoRadioTXVerified',
            'PersistentStopVerified', 'RadioQuiesced', 'FirmwareQuiesced'):
    assert p.get(key), key
assert p['TXUSBProbeSubmitted'] == p['TXUSBProbeCompleted'] == p['TXUSBProbeZLPWrites'] == 5
assert p['TXUSBProbeControlAtTransfer'] == 1
assert p['TXUSBProbeSucceeded'] >= 4
assert p['TXUSBProbeSucceeded'] + p['TXUSBProbeCancelled'] == 5
assert p['TXUSBProbeAbortRequests'] <= 1
for key in ('TXUSBProbeDataWrites', 'TXUSBProbeBytesSubmitted', 'TXUSBProbeFailed',
            'TXUSBProbeRejected', 'TXUSBProbeWatchdogs', 'TXUSBProbeLastError', 'RXUSBErrors'):
    assert p[key] == 0, key
assert not p['WiFiImplemented'] and not p['WPA2Implemented']

requests = []
pending = {}
for line in (root / 'qemu-usb.log').read_text().splitlines():
    data = re.search(r'usb_host_req_data dev ([^,]+), packet ([^,]+), in (\d+), ep (\d+), size (\d+)', line)
    if data and data[3] == '0':
        device, packet, _, endpoint, length = data.groups()
        assert int(endpoint) in (2, 3), line
        assert int(length) == 0, 'not a ZLP: ' + line
        assert (device, packet) not in pending
        entry = {'endpoint': int(endpoint), 'length': 0, 'result': None}
        requests.append(entry)
        pending[device, packet] = entry
    done = re.search(r'usb_host_req_complete dev ([^,]+), packet ([^,]+), status (-?\d+), length (\d+)', line)
    if done and (done[1], done[2]) in pending:
        entry = pending.pop((done[1], done[2]))
        entry['result'] = 'success' if done[3] == '0' and done[4] == '0' else 'error'
    cancelled = re.search(r'usb_host_req_canceled dev ([^,]+), packet (\S+)', line)
    if cancelled and (cancelled[1], cancelled[2]) in pending:
        pending.pop((cancelled[1], cancelled[2]))['result'] = 'cancelled'
# The fifth request may be aborted by Panther before it reaches QEMU.
assert 4 <= len(requests) <= 5, requests
assert not pending, pending
assert [r['endpoint'] for r in requests[:4]] == [2, 3, 2, 3], requests
assert all(r['result'] == 'success' for r in requests[:4]), requests
assert all(r['result'] in ('success', 'cancelled') for r in requests), requests
result = {'out_requests': requests, 'verified_zero_length': True,
          'driver_submitted': 5, 'driver_completed': 5,
          'hardware_data_frames_tested': False}
(root / 'tx-usb-trace-summary.json').write_text(json.dumps(result, indent=2) + '\n')
print('PASS: 5/5 driver callbacks; %d traced OUT requests, all zero length, both pipes verified; no Wi-Fi frame' % len(requests))
