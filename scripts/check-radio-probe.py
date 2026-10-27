#!/usr/bin/env python3
"""Validate radio diagnostic results; calibration samples may vary per run."""
import plistlib
import sys
from pathlib import Path

if len(sys.argv) < 2:
    raise SystemExit('Usage: check-radio-probe.py probe.plist [probe.plist ...]')
reference = None
for filename in sys.argv[1:]:
    p = plistlib.loads(Path(filename).read_bytes())
    assert p['PortStage'] == 'radio-calibration-verified', filename
    for key in ('RadioCalibrationVerified', 'RadioQuiesced', 'LCCalibrationVerified',
                'FirmwareBootVerified', 'FirmwareQuiesced', 'EfusePowerRestored'):
        assert p[key], (filename, key)
    for key in ('RadioRunning', 'FirmwareRunning', 'WiFiImplemented', 'WPA2Implemented'):
        assert not p[key], (filename, key)
    for key, value in {'MACInitWrites': 86, 'BasebandInitWrites': 187,
                       'AGCInitWrites': 160, 'RFInitWrites': 137,
                       'RFChannelAtCalibration': 0x7401,
                       'BasebandRFModeAtCalibration': 0x83040000,
                       'FirmwareBytesWritten': 16084, 'FirmwarePagesWritten': 4,
                       'FirmwareVersion': 80, 'FirmwareSubversion': 0,
                       'FirmwareRequired': 'urtwn-rtl8192cT'}.items():
        # Panther serializes a 32-bit OSNumber with bit 31 set as negative.
        observed = p[key] & 0xffffffff if key == 'BasebandRFModeAtCalibration' else p[key]
        assert observed == value, (filename, key, p[key])
    assert p['FirmwareReadyRegisterAtBoot'] & 0xc6 == 0xc6, filename
    assert p['IQCalibrationRuns'] in (2, 3), filename
    for key in ('IQTXBefore', 'IQTXAfter', 'IQRXBefore', 'IQRXAfter'):
        assert 0 <= p[key] <= 1023, (filename, key)
    assert p['IQTXBefore'] not in (0, 0x142) and p['IQTXAfter'] != 0x42, filename
    assert p['IQRXBefore'] not in (0, 0x132) and p['IQRXAfter'] != 0x36, filename
    assert len(p['EfuseROM']) == 128, filename
    if reference is not None:
        for key in ('EfuseROM', 'MACAddress', 'ROMVendorID', 'ROMProductID',
                    'BoardType', 'PASetting', 'ChannelPlan', 'Regulatory'):
            assert p[key] == reference[key], (filename, key)
        assert (p['SYS_CFG'] ^ reference['SYS_CFG']) & ~0x3f == 0, filename
    reference = p
    print('PASS: ' + filename)
