#!/usr/bin/env python3
"""Package the optional offline interface, without the VM test provider."""
import hashlib
import io
import plistlib
import tarfile
from pathlib import Path

root = Path(__file__).resolve().parent.parent
native = root / 'artifacts/DWA121Ethernet-native-panther-ppc.tgz'
source = root / 'artifacts/ethernet-production-source.tgz'
with tarfile.open(native, 'r:gz') as src:
    info = plistlib.loads(src.extractfile('DWA121Ethernet.kext/Contents/Info.plist').read())
    version = info['CFBundleVersion']
    if not version or any(c not in '0123456789.' for c in version):
        raise ValueError('Invalid version')
    output = root / ('artifacts/DWA121Ethernet-v%s-panther-ppc.tgz' % version)
    temporary = output.with_suffix('.tgz.tmp')
    try:
        with tarfile.open(temporary, 'w:gz', format=tarfile.USTAR_FORMAT) as dst:
            for member in src:
                if member.name != 'dwa121-netcheck' and not (
                    member.name == 'DWA121Ethernet.kext' or
                    member.name.startswith('DWA121Ethernet.kext/')
                ):
                    raise ValueError('Unexpected native member: ' + member.name)
                dst.addfile(member, src.extractfile(member) if member.isfile() else None)
            dst.add(root / 'scripts/test-physical.sh', arcname='scripts/test-physical-ethernet.sh')
            dst.add(root / 'docs/ethernet-interface.md', arcname='docs/ethernet-interface.md')
            dst.add(root / 'LICENSE', arcname='LICENSE-Ethernet.txt')
            dst.add(source, arcname='ethernet-source.tgz')
            readme = ('Compagnon Ethernet expérimental v%s pour Panther PowerPC.\n'
                      'Extraire dans le dossier du kit DWA121Diagnostic v0.14.0.\n'
                      'Le lien reste inactif : aucune connexion Internet.\n'
                      'Procédure : docs/ethernet-interface.md\n'
                      'Essai : sudo sh scripts/test-physical-ethernet.sh interface 11 --usb11\n'
                      'Le script original scripts/test-physical.sh reste intact.\n' % version).encode('utf-8')
            member = tarfile.TarInfo('README-Ethernet.txt')
            member.size = len(readme)
            member.mode = 0o644
            dst.addfile(member, io.BytesIO(readme))
        temporary.replace(output)
    finally:
        if temporary.exists():
            temporary.unlink()
digest = hashlib.sha256(output.read_bytes()).hexdigest()
Path(str(output) + '.sha256').write_text(digest + '  ' + output.name + '\n')
print(output)
print(digest)
