#!/usr/bin/env python3
"""Run bounded passive probes on channels 1..11 in the development VM.

Requires a completed build-vm.sh and an attached key. Never detaches USB,
installs a system extension, sends frames, or treats an empty probe as RX proof.
"""
import argparse
import json
from pathlib import Path
import plistlib
import shlex
import subprocess

ROOT = Path(__file__).resolve().parent.parent
REMOTE = '/Users/builder/dwa121/test-rx/DWA121Diagnostic.kext'


def run(script, command=None, **kwargs):
    args = [str(ROOT / 'scripts' / script)]
    if command is not None:
        args.append(command)
    return subprocess.run(args, check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--channels', nargs='+', type=int, default=list(range(1, 12)))
    parser.add_argument('--output', type=Path, default=ROOT / 'artifacts' / 'rx-sweep')
    args = parser.parse_args()
    if any(c < 1 or c > 11 for c in args.channels) or len(set(args.channels)) != len(args.channels):
        parser.error('channels must be distinct integers from 1 to 11')
    args.output.mkdir(parents=True, exist_ok=True)
    if b'local.dwa121.Diagnostic' in run('vm-ssh.sh', 'kextstat', capture_output=True).stdout:
        parser.error('unload the existing diagnostic before starting a sweep')
    # Use the actual compiled bundle's plist, not potentially newer source.
    raw = run('vm-ssh.sh', 'cat /Users/builder/dwa121/native/build/DWA121Diagnostic.kext/Contents/Info.plist', capture_output=True).stdout
    info = plistlib.loads(raw)
    run('vm-root.py', 'set -e; mkdir -p /Users/builder/dwa121/test-rx; cp -R /Users/builder/dwa121/native/build/DWA121Diagnostic.kext /Users/builder/dwa121/test-rx/; chown -R root:wheel ' + REMOTE)
    info['IOKitPersonalities']['DWA-121 A1 Diagnostic']['PersistentRX'] = False
    results = []
    for channel in args.channels:
        info['IOKitPersonalities']['DWA-121 A1 Diagnostic']['PassiveRXChannel'] = channel
        run('vm-ssh.sh', 'cat > /Users/builder/dwa121/probe-channel.plist', input=plistlib.dumps(info))
        print('Probing channel %d...' % channel, flush=True)
        loaded = False
        with (args.output / ('channel-%02d.log' % channel)).open('wb') as log:
            try:
                run('vm-root.py', 'set -e; cp /Users/builder/dwa121/probe-channel.plist ' + REMOTE + '/Contents/Info.plist; chown root:wheel ' + REMOTE + '/Contents/Info.plist; kextload -c -t ' + REMOTE, stdout=log, stderr=subprocess.STDOUT)
                loaded = True
                raw = run('probe-vm.sh', capture_output=True).stdout
                (args.output / ('channel-%02d.plist' % channel)).write_bytes(raw)
                p = plistlib.loads(raw)
                assert p['RXChannel'] == channel and p['RFChannelAtRX'] == (0x7400 | channel), 'requested and actual channel differ'
                assert p['RXRequestsDrained'] and p['RadioQuiesced'] and p['FirmwareQuiesced']
                assert p['RXUSBSubmitted'] == p['RXUSBCompleted'] and p['RXUSBErrors'] == 0
                assert p['LLTEntriesVerified'] == 256
                row = {k: p[k] for k in ('RXChannel', 'RFChannelAtRX', 'RXControlAtProbe', 'RXDescriptors', 'RXBeacons', 'RXBytes', 'RXUSBSubmitted', 'RXUSBCompleted', 'RXUSBTimeouts', 'RXUSBAborted', 'RXFramesVerified')}
                results.append(row)
                print('Channel %d: %d descriptors, %d verified beacons, %d/%d callbacks' % (channel, p['RXDescriptors'], p['RXBeacons'], p['RXUSBCompleted'], p['RXUSBSubmitted']), flush=True)
            except (subprocess.CalledProcessError, AssertionError) as error:
                if isinstance(error, subprocess.CalledProcessError) and error.stderr:
                    log.write(error.stderr)
                results.append({'RXChannel': channel, 'error': str(error)})
                print('Channel %d: probe failed: %s' % (channel, error), flush=True)
            finally:
                (args.output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
                run('vm-ssh.sh', "dmesg | grep '^DWA121Diagnostic:' | tail -64", stdout=log, stderr=subprocess.STDOUT)
                if loaded:
                    run('vm-root.py', 'kextunload ' + shlex.quote(REMOTE), stdout=log, stderr=subprocess.STDOUT)
                if b'local.dwa121.Diagnostic' in run('vm-ssh.sh', 'kextstat', capture_output=True).stdout:
                    raise RuntimeError('diagnostic is still loaded; stopping sweep')
    print('Probe sweep complete. Hardware RX validated: %s' % any(p.get('RXFramesVerified', False) for p in results), flush=True)
    if any('error' in p for p in results):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
