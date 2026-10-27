#!/usr/bin/env python3
"""Check discovery evidence; run check-rx-probe.py separately for USB/FCS proof."""
import plistlib
import sys
from pathlib import Path

if len(sys.argv) < 2:
    raise SystemExit('Usage: check-bss-probe.py probe.plist [probe.plist ...]')
for filename in sys.argv[1:]:
    p = plistlib.loads(Path(filename).read_bytes())
    assert p['BeaconDiscoveryImplemented'] and p['RXFramesVerified'], filename
    entries = p['DiscoveredBSS']
    assert 0 < len(entries) == p['BSSCount'] <= 16, filename
    assert p['BSSObservations'] + p['BSSRejected'] == p['RXBeacons'], filename
    assert p['BSSObservations'] - p['BSSDropped'] == sum(b['Observations'] for b in entries), filename
    assert len({b['BSSID'] for b in entries}) == len(entries), filename
    for b in entries:
        assert len(b['BSSID']) == 6 and not (b['BSSID'][0] & 1) and any(b['BSSID']), filename
        assert len(b['SSID']) <= 32 and b['HiddenSSID'] == (not any(b['SSID'])), filename
        assert 0 <= b['AdvertisedChannel'] <= 14, filename
        assert b['ReceivedChannel'] == p['RXChannel'] and b['BeaconIntervalTU'] > 0, filename
        assert b['RSNStatus'] in (0, 1, 2, 3) and b['Observations'] > 0, filename
        rsn = b['RSNInformationElement']
        if b['RSNStatus'] == 0:
            assert not rsn and not b['WPA2PSKCCMPAdvertised'], filename
        elif b['RSNStatus'] == 1:
            assert 4 <= len(rsn) <= 257 and rsn[0] == 48 and rsn[1] == len(rsn) - 2, filename
            assert rsn[2:4] == bytes([1, 0]), filename
        else:
            assert not b['WPA2PSKCCMPAdvertised'], filename
        assert b['PMFRequired'] == bool(b['RSNCapabilities'] & 0x40), filename
        assert b['PMFCapable'] == bool(b['RSNCapabilities'] & 0x80), filename
    print(f"PASS: {filename}: {len(entries)} BSSIDs, {p['BSSObservations']} observations, {p['BSSRejected']} rejected, {p['BSSDropped']} table drops")
