#!/usr/bin/env python3
"""Check successful real association evidence and USB transport metadata."""
import argparse
import json
import plistlib
import re
from pathlib import Path
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('directory', type=Path)
args = ap.parse_args()
d = args.directory
s = json.loads((d / 'summary.json').read_text())
p = plistlib.loads((d / 'stopped.plist').read_bytes())
assert s['passed'] and s['associated'] and s['cleanup_verified'] and s['unloaded']
assert all(p[k] for k in ('AssociationTXConfigured', 'AssociationTXBlocked', 'AssociationTXDrained', 'PersistentStopVerified', 'RadioQuiesced', 'FirmwareQuiesced'))
assert not p.get('AssociationDeadlineExpired', False)
assert p['AssociationTXSubmitted'] == p['AssociationTXCompleted']
assert 2 <= p['AssociationTXSubmitted'] <= 6
assert p['AssociationTXErrors'] == p['AssociationTXWatchdogs'] == p['AssociationTXLastError'] == 0
assert p['RXManagement'] >= 2
assert p['RXUSBSubmitted'] == p['RXUSBCompleted']
for k in ('RXUSBErrors', 'RXHardwareCRCErrors', 'RXICVErrors', 'RXFCSErrors'):
    assert p[k] == 0, k
log = (d / 'association.log').read_text()
assert re.search(r'Management RX: subtype=0xb0 length=\d+ state=1->2 status=0', log)
assert re.search(r'Management RX: subtype=0x10 length=\d+ state=2->3 status=0', log)
m = re.search(r'associated=1 state=3 status=0 AID=(\d+)', log)
assert m and 1 <= int(m[1]) <= 2007
assert 'stopped=1' in log
pending, requests = {}, []
for line in (d / 'qemu-usb.log').read_text().splitlines():
    m = re.search(r'usb_host_req_data dev ([^,]+), packet ([^,]+), in 0, ep (\d+), size (\d+)', line)
    if m:
        assert m[3] == '2' and 0 <= int(m[4]) <= 544
        key = (m[1], m[2])
        assert key not in pending
        item = {'endpoint': 2, 'length': int(m[4]), 'completed': False}
        requests.append(item)
        pending[key] = item
    m = re.search(r'usb_host_req_complete dev ([^,]+), packet ([^,]+), status (-?\d+), length (\d+)', line)
    if m and (m[1], m[2]) in pending:
        item = pending.pop((m[1], m[2]))
        assert int(m[3]) == 0 and int(m[4]) == item['length']
        item['completed'] = True
    m = re.search(r'usb_host_req_canceled dev ([^,]+), packet (\S+)', line)
    assert not (m and (m[1], m[2]) in pending), 'cancelled management transfer'
assert not pending and len(requests) == p['AssociationTXSubmitted']
assert requests[0]['length'] == 62
assert sum(i['length'] for i in requests) == p['AssociationTXBytes']
result = {'requests': requests, 'associated': True, 'wpa2_handshake': False}
(d / 'tx-trace-summary.json').write_text(json.dumps(result, indent=2) + '\n')
print('PASS: actual auth/association responses, USB OUT completion, RX/TX drain and radio shutdown; WPA2 not performed')
