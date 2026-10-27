#!/usr/bin/env python3
"""Verify a stopped persistent-RX snapshot, not a live non-atomic snapshot."""
import argparse
import plistlib
from pathlib import Path
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--expect-watchdog', action='store_true')
ap.add_argument('files', nargs='+')
args = ap.parse_args()
for filename in args.files:
    p = plistlib.loads(Path(filename).read_bytes())
    assert p['PortStage'] == 'persistent-rx-stopped', filename
    for k in ('PersistentRXImplemented', 'PersistentStopVerified', 'RXHardwareStopVerified',
              'RXRequestsDrained', 'RadioQuiesced', 'FirmwareQuiesced', 'USBTransportConsistent',
              'BSSSnapshotValid'):
        assert p[k], (filename, k)
    for k in ('RXRunning', 'PersistentRXActive', 'RadioRunning', 'FirmwareRunning', 'WiFiImplemented', 'WPA2Implemented'):
        assert not p[k], (filename, k)
    assert p['RXUSBSubmitted'] == p['RXUSBCompleted'] >= (1 if args.expect_watchdog else 2), filename
    for k in ('RXUSBErrors', 'RXWatchdogs'):
        assert p[k] == int(args.expect_watchdog), (filename, k, p[k])
    if args.expect_watchdog:
        assert p['RXUSBAborted'] == p['RXAbortRequests'] == 1, filename
    for k in ('RXSubmissionsRejected', 'RXMalformed', 'RXHardwareCRCErrors', 'RXICVErrors', 'RXFCSErrors'):
        assert p[k] == 0, (filename, k, p[k])
    assert p['RXAbortRequests'] <= 1, filename
    assert p['RXControlAtProbe'] == 0x2fa and p['LLTEntriesVerified'] == 256, filename
    assert p['BSSObservations'] + p['BSSRejected'] == p['RXBeacons'], filename
    assert p['BSSObservations'] - p['BSSDropped'] == sum(b['Observations'] for b in p['DiscoveredBSS']), filename
    assert (p['USBLinkSpeedMbps'], p['USBBulkMaxPacket']) in ((12, 64), (480, 512)), filename
    print(f"PASS: {filename}: {p['RXUSBCompleted']} drained callbacks, {p['RXBeacons']} beacons, {p['RXUSBTimeouts']} timeouts, {p['RXAbortRequests']} abort requests")
