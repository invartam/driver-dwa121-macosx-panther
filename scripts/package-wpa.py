#!/usr/bin/env python3
"""Bundle the native userland library/tests with reproducible sources/notices."""
import argparse
import tarfile
from pathlib import Path

root = Path(__file__).resolve().parent.parent
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--native', type=Path, required=True)
args = ap.parse_args()
output = root / 'artifacts/DWA121-WPA2-core-panther-ppc.tgz'
temporary = output.with_suffix('.packaging.tmp')
native_files = ('build/libdwa-wpa2.a', 'build/station-test', 'build/wpa2-test', 'build/data-test')
try:
    with tarfile.open(args.native, 'r:gz') as native, tarfile.open(temporary, 'w:gz', format=tarfile.USTAR_FORMAT) as bundle:
        for name in native_files:
            member = native.getmember(name)
            if not member.isfile():
                raise ValueError('Expected a regular native binary: ' + name)
            bundle.addfile(member, native.extractfile(member))
        for name in ('LICENSE', 'docs/association-wpa2.md', 'docs/wpa-hardware.md', 'docs/wpa-upstream.json',
                     'src/net80211', 'src/supplicant', 'src/control/frame_queue.h', 'src/rtl8188cu/identity.h',
                     'tests/station_test.c', 'tests/wpa2_test.c', 'tests/data_test.c', 'docs/ccmp-data.md', 'docs/test-machine-physique.md',
                     'scripts/build-wpa-core.sh', 'scripts/test-wpa.sh',
                     'vendor/wpa_supplicant-2.12', 'vendor/patches/hostap-2.12-panther-endian.patch'):
            bundle.add(root / name, arcname=name)
    temporary.replace(output)
finally:
    temporary.unlink(missing_ok=True)
print('Packaged native userland protocol core (no USB backend): ' + str(output))
