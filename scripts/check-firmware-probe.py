#!/usr/bin/env python3
"""Check completed hardware probes, allowing SYS_CFG clock-ready status changes."""
import plistlib, sys
from pathlib import Path
if len(sys.argv) < 2:
    raise SystemExit('Usage: check-firmware-probe.py probe.plist [probe.plist ...]')
reference = None
for name in sys.argv[1:]:
    probe = plistlib.loads(Path(name).read_bytes())
    assert probe['PortStage'] == 'firmware-boot-verified', name
    assert probe['FirmwareBootVerified'] and probe['FirmwareQuiesced'], name
    assert not probe['FirmwareRunning'] and not probe['WiFiImplemented'] and not probe['WPA2Implemented'], name
    assert probe['FirmwareRequired'] == 'urtwn-rtl8192cT', name
    assert probe['FirmwareBytesWritten'] == 16084 and probe['FirmwarePagesWritten'] == 4, name
    assert probe['FirmwareVersion'] == 80 and probe['FirmwareSubversion'] == 0, name
    assert probe['FirmwareReadyRegisterAtBoot'] & 0x40, name
    assert len(probe['EfuseROM']) == 128 and probe['EfusePowerRestored'], name
    if reference is not None:
        # SYS_CFG bits 0..5: clock/voltage/reset status, not silicon identity.
        assert (reference['SYS_CFG'] ^ probe['SYS_CFG']) & ~0x3f == 0, name
        for key in set(reference) | set(probe):
            if key != 'SYS_CFG':
                assert reference.get(key) == probe.get(key), (name, key)
    reference = probe
    print('PASS: ' + name)
