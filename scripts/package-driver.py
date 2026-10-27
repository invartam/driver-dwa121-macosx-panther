#!/usr/bin/env python3
"""Build the Panther installable kit; never embeds a network credential."""
from pathlib import Path
import argparse, hashlib, tarfile, io, gzip, subprocess, plistlib
root=Path(__file__).resolve().parent.parent
parser=argparse.ArgumentParser()
parser.add_argument('--version', default='0.15.0')
args=parser.parse_args()
version=args.version
if not version or any(c not in '0123456789.' for c in version):
    raise SystemExit('Invalid release version')
plist=plistlib.loads((root/'src/kext/Info.plist').read_bytes())
if plist['CFBundleVersion'] != version or plist['CFBundleShortVersionString'] != version:
    raise SystemExit('Tag/version does not match the kext Info.plist')
source=root/f'release/native/{version}.tgz'
expected=(root/f'release/native/{version}.tgz.sha256').read_text().split()[0]
if hashlib.sha256(source.read_bytes()).hexdigest() != expected:
    raise SystemExit('Native archive checksum mismatch')
output=root/f'artifacts/DWA121-{version}-Panther-PPC.tgz'
output.parent.mkdir(exist_ok=True)
prefix=f'DWA121-{version}-Panther-PPC'
entries={}
with tarfile.open(source) as src:
    for item in src:
        if item.isdir(): continue
        if not item.isfile() or item.name.startswith('/') or '..' in Path(item.name).parts:
            raise SystemExit(f'Unexpected native member: {item.name}')
        if item.name.startswith('DWA121Diagnostic.kext/') or item.name in ('dwa121-connect','dwa121-config','dwa121-service'):
            dest='payload/'+item.name
        else: dest='Licences/'+item.name
        entries[dest]=(src.extractfile(item).read(),0o755 if item.name.startswith('dwa121-') or '/MacOS/' in item.name else 0o644)
for required in ('payload/DWA121Diagnostic.kext/Contents/MacOS/DWA121Diagnostic', 'payload/dwa121-connect', 'payload/dwa121-config', 'payload/dwa121-service'):
    if required not in entries:
        raise SystemExit(f'Missing native payload: {required}')
native_plist=plistlib.loads(entries['payload/DWA121Diagnostic.kext/Contents/Info.plist'][0])
if native_plist['CFBundleVersion'] != version:
    raise SystemExit('Native kext version mismatch')
for p in sorted((root/'packaging').rglob('*')):
    if p.is_file():
        dest=p.name if p.suffix=='.command' else 'packaging/'+p.relative_to(root/'packaging').as_posix()
        entries[dest]=(p.read_bytes(),0o755 if p.suffix in ('.command','.sh') or p.name=='DWA121' else 0o644)
# Include the corresponding source tree and every retained upstream notice.
source_bytes=io.BytesIO()
with gzip.GzipFile(filename='',fileobj=source_bytes,mode='wb',mtime=0) as zg,tarfile.open(fileobj=zg,mode='w',format=tarfile.USTAR_FORMAT) as ts:
    tracked=subprocess.check_output(['git','ls-files','-z','--','src','tests','scripts','firmware','packaging','docs','vendor','LICENSE','README.md'],cwd=root).split(b'\0')
    paths=[root/bytes_name.decode('utf-8') for bytes_name in tracked if bytes_name]
    if not paths:
        raise SystemExit('No tracked sources; stage the release files first')
    for p in sorted(paths):
        if not p.is_file() or p.is_symlink():
            raise SystemExit(f'Missing or linked source: {p}')
        data=p.read_bytes();info=tarfile.TarInfo(p.relative_to(root).as_posix());info.size=len(data);info.mode=0o644;info.mtime=0
        ts.addfile(info,io.BytesIO(data))
entries['Sources.tgz']=(source_bytes.getvalue(),0o644)
entries['Licences/README.hostap']=((root/'vendor/wpa_supplicant-2.12/README').read_bytes(),0o644)
entries['Licences/README.wpa_supplicant']=((root/'vendor/wpa_supplicant-2.12/wpa_supplicant/README').read_bytes(),0o644)
ccmp=(root/'src/supplicant/data.c').read_text().split('*/',1)[0]+'*/\n'
entries['Licences/NOTICE-CCMP.txt']=(ccmp.encode(),0o644)
entries['LIRE-MOI.txt']=((root/'docs/installation-driver.txt').read_bytes(),0o644)
manifest={name:hashlib.sha256(data).hexdigest() for name,(data,_) in entries.items()}
entries['SHA256SUMS.txt']=(''.join(f'{sha}  {name}\n' for name,sha in sorted(manifest.items())).encode(),0o644)
with output.open('wb') as raw,gzip.GzipFile(filename='',fileobj=raw,mode='wb',mtime=0) as gz,tarfile.open(fileobj=gz,mode='w',format=tarfile.USTAR_FORMAT) as dst:
    for name,(data,mode) in sorted(entries.items()):
        item=tarfile.TarInfo(prefix+'/'+name);item.size=len(data);item.mode=mode;item.uid=item.gid=0;item.uname='root';item.gname='wheel';item.mtime=0
        dst.addfile(item,io.BytesIO(data))
raw=output.read_bytes()
for algorithm in ('sha256','md5'):
    output.with_suffix(output.suffix+'.'+algorithm).write_text(hashlib.new(algorithm,raw).hexdigest()+'  '+output.name+'\n')
print(output)
