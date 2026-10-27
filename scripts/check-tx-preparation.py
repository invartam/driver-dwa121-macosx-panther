#!/usr/bin/env python3
"""Check a stopped v0.8 TX-preparation hardware probe; this is not a Wi-Fi test."""
import argparse
import plistlib
from pathlib import Path
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('files', nargs='+')
args = ap.parse_args()
for name in args.files:
    p = plistlib.loads(Path(name).read_bytes())
    for key in ('TXPowerReadbackVerified', 'CAMCCMPReadbackVerified',
                'CAMProbeErasureVerified', 'TXPreparationNoTXVerified',
                'TXPreparationVerified', 'PersistentStopVerified',
                'RXRequestsDrained', 'RadioQuiesced', 'FirmwareQuiesced'):
        assert p.get(key), (name, key)
    for key in ('RXRunning', 'WiFiImplemented', 'WPA2Implemented'):
        assert p.get(key) is False, (name, key)
    assert p['RXControlAtProbe'] == 0x2fa
    assert p['RXUSBSubmitted'] == p['RXUSBCompleted'] > 0
    assert p['RXUSBErrors'] == 0
    print('PASS: %s: TX power, synthetic PTK/GTK CAM readback/erasure, no TX, clean stop' % name)
