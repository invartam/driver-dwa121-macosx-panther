#!/usr/bin/env python3
"""Package the native build, its exact transferred sources and development notes."""
import io
import plistlib
import tarfile
from pathlib import Path
root = Path(__file__).resolve().parent.parent
native = root / 'artifacts/DWA121Diagnostic-panther-ppc.tgz'
with tarfile.open(native, 'r:gz') as src:
    info = plistlib.loads(src.extractfile('DWA121Diagnostic.kext/Contents/Info.plist').read())
    version = info['CFBundleVersion']
    if not version or any(c not in '0123456789.' for c in version):
        raise ValueError('Invalid bundle version')
    output = root / ('artifacts/DWA121Diagnostic-v%s-panther-ppc.tgz' % version)
    temporary = output.with_suffix('.tgz.tmp')
    try:
        with tarfile.open(temporary, 'w:gz', format=tarfile.USTAR_FORMAT) as dst:
            for member in src:
                dst.addfile(member, src.extractfile(member) if member.isfile() else None)
            for name in ('README.md', 'docs/tx-preparation.md', 'docs/association-wpa2.md', 'docs/control-bridge.md', 'docs/async-tx.md', 'docs/station-hw.md', 'docs/wpa-hardware.md', 'docs/ccmp-data.md', 'docs/ccmp-hardware.md', 'docs/wpa-upstream.json', 'docs/upstream.json', 'docs/test-machine-physique.md', 'scripts/test-physical.sh'):
                dst.add(root / name, arcname=name)
            dst.add(root / 'artifacts/source-transfer.tgz', arcname='source-transfer.tgz')
        temporary.replace(output)
    finally:
        if temporary.exists():
            temporary.unlink()
print(output)
