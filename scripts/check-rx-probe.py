#!/usr/bin/env python3
"""Verify passive hardware RX, including drained requests and software FCS."""
import plistlib
import sys
from pathlib import Path
if len(sys.argv) < 2:
    raise SystemExit('Usage: check-rx-probe.py probe.plist [probe.plist ...]')
reference = None
for filename in sys.argv[1:]:
    p = plistlib.loads(Path(filename).read_bytes())
    assert p['PortStage'] == 'passive-rx-probe-complete', filename
    for key in ('PassiveRXImplemented', 'RXFramesVerified', 'RXRequestsDrained',
                'RadioCalibrationVerified', 'RadioQuiesced', 'LCCalibrationVerified',
                'FirmwareBootVerified', 'FirmwareQuiesced', 'EfusePowerRestored'):
        assert p[key], (filename, key)
    for key in ('RXRunning', 'RadioRunning', 'FirmwareRunning', 'WiFiImplemented', 'WPA2Implemented'):
        assert not p[key], (filename, key)
    assert p['LLTEntriesWritten'] == p['LLTEntriesVerified'] == 256, filename
    assert p['RXControlAtProbe'] == 0x2fa, filename
    if 'USBTransportConsistent' in p:
        assert p['USBTransportConsistent'], filename
        assert (p['USBLinkSpeedMbps'], p['USBBulkMaxPacket']) in ((12, 64), (480, 512)), filename
    # A positive probe completes its configured reads plus the Abort probe.
    assert p['RXUSBSubmitted'] == p['RXUSBCompleted'] == p.get('RXReadLimit', 8) + 1, filename
    assert p['RXUSBAborted'] >= 1, filename
    for key in ('RXUSBErrors', 'RXMalformed', 'RXFCSErrors', 'RXHardwareCRCErrors', 'RXICVErrors'):
        assert p[key] == 0, (filename, key, p[key])
    assert p['RXDescriptors'] >= p['RXBeacons'] > 0, filename
    assert p['RXBeacons'] * 36 <= p['RXBytes'] <= p['RXBeacons'] * 4092, filename
    assert p['RFChannelAtCalibration'] == 0x7401, filename
    assert 1 <= p['RXChannel'] <= 11, filename
    assert p['RFChannelAtRX'] == 0x7400 | p['RXChannel'], filename
    assert p['BasebandRFModeAtCalibration'] & 0xffffffff == 0x83040000, filename
    assert len(p['EfuseROM']) == 128, filename
    if reference is not None:
        for key in ('EfuseROM', 'MACAddress', 'ROMVendorID', 'ROMProductID', 'BoardType'):
            assert p[key] == reference[key], (filename, key)
    reference = p
    print(f"PASS: {filename}: {p['RXBeacons']} CRC-verified beacons, {p['RXUSBCompleted']} callbacks, {p['RXUSBAborted']} aborted")
