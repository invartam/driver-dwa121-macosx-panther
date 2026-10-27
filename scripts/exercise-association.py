#!/usr/bin/env python3
"""Bounded, explicitly enabled management TX probe on the user's Wi-Fi AP."""
import argparse
import json
import plistlib
import shlex
import subprocess
import time
from pathlib import Path
from importlib.util import spec_from_file_location, module_from_spec
ROOT = Path(__file__).resolve().parent.parent
spec = spec_from_file_location('persistent', ROOT / 'scripts/exercise-persistent.py')
p = module_from_spec(spec)
spec.loader.exec_module(p)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ssid', required=True)
    parser.add_argument('--channel', type=int, default=11, choices=range(1, 12))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--guard', choices=('deadline', 'abandon', 'drop'))
    args = parser.parse_args()
    if not 1 <= len(args.ssid.encode()) <= 32:
        parser.error('SSID must contain 1..32 bytes')
    args.output.mkdir(parents=True, exist_ok=True)
    if b'local.dwa121.Diagnostic' in p.command('vm-ssh.sh', 'kextstat', capture_output=True).stdout:
        parser.error('an existing diagnostic is loaded')
    source = '/Users/builder/dwa121/native/build/DWA121Diagnostic.kext'
    info = plistlib.loads(p.command('vm-ssh.sh', 'cat ' + source + '/Contents/Info.plist', capture_output=True).stdout)
    personality = info['IOKitPersonalities']['DWA-121 A1 Diagnostic']
    personality.update(PersistentRX=True, PassiveRXChannel=args.channel, AssociationTXEnabled=True)
    p.command('vm-ssh.sh', 'cat > /Users/builder/dwa121/live-info.plist', input=plistlib.dumps(info))
    summary = {'channel': args.channel, 'ssid': args.ssid, 'associated': False, 'passed': False}
    tool = '/Users/builder/dwa121/native/build/dwa121-associate '
    with (args.output / 'kernel.log').open('wb') as log:
        try:
            p.command('vm-root.py', 'set -e; mkdir -p /Users/builder/dwa121/test-live; cp -R ' + source + ' /Users/builder/dwa121/test-live/; cp /Users/builder/dwa121/live-info.plist ' + p.REMOTE + '/Contents/Info.plist; chown -R root:wheel ' + p.REMOTE + '; kextload -c -t ' + p.REMOTE, stdout=log, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 60
            while time.monotonic() < deadline:
                probe = p.run('vm-ssh.sh', p.INFO, capture_output=True)
                if probe.returncode == 0 and plistlib.loads(probe.stdout).get('RXRunning'):
                    (args.output / 'running.plist').write_bytes(probe.stdout)
                    break
                time.sleep(1)
            else:
                raise RuntimeError('service did not start')
            denied = p.run('vm-ssh.sh', tool + shlex.quote(args.ssid), capture_output=True)
            (args.output / 'unprivileged.log').write_bytes(denied.stdout + denied.stderr)
            assert denied.returncode != 0 and b'Control open rejected' in denied.stderr
            result = p.run('vm-root.py', tool + shlex.quote(args.ssid) + (' --' + args.guard if args.guard else ''), capture_output=True)
            (args.output / 'association.log').write_bytes(result.stdout + result.stderr)
            print(result.stdout.decode(), end='', flush=True)
            print(result.stderr.decode(), end='', flush=True)
            summary['associated'] = result.returncode == 0 and b'associated=1' in result.stdout
            summary['tool_exit'] = result.returncode
            # Process death cleanup can be delivered after SSH exits.
            for attempt in range(30):
                raw, stopped = p.snapshot()
                if stopped.get('PersistentStopVerified'):
                    break
                time.sleep(0.2)
            (args.output / 'stopped.plist').write_bytes(raw)
            summary['tx_configured'] = stopped.get('AssociationTXConfigured', False)
            summary['tx_submitted'] = stopped.get('AssociationTXSubmitted', 0)
            summary['tx_completed'] = stopped.get('AssociationTXCompleted', 0)
            summary['tx_bytes'] = stopped.get('AssociationTXBytes', 0)
            summary['rx_beacons'] = stopped.get('RXBeacons', 0)
            summary['tx_blocked'] = stopped.get('AssociationTXBlocked', False)
            summary['tx_drained'] = stopped.get('AssociationTXDrained', False)
            summary['hardware_stopped'] = all(stopped.get(k) for k in ('PersistentStopVerified', 'RadioQuiesced', 'FirmwareQuiesced'))
            summary['passed'] = summary['associated'] and summary['hardware_stopped'] and summary['tx_blocked'] and summary['tx_drained']
            if args.guard:
                summary['guard'] = args.guard
                summary['deadline_expired'] = stopped.get('AssociationDeadlineExpired', False)
                summary['passed'] = result.returncode == 0 and summary['hardware_stopped'] and summary['tx_blocked'] and summary['tx_drained'] and summary['tx_submitted'] == 0
                assert summary['deadline_expired'] == (args.guard == 'deadline')
            assert summary['passed'], summary
        finally:
            try:
                if b'local.dwa121.Diagnostic' in p.command('vm-ssh.sh', 'kextstat', capture_output=True).stdout:
                    p.run('vm-root.py', p.CONTROL, stdout=log, stderr=subprocess.STDOUT)
                    raw, stopped = p.snapshot()
                    (args.output / 'cleanup-stopped.plist').write_bytes(raw)
                    summary['cleanup_verified'] = all(stopped.get(k) for k in ('PersistentStopVerified', 'RadioQuiesced', 'FirmwareQuiesced'))
                    p.command('vm-root.py', 'kextunload ' + p.REMOTE, stdout=log, stderr=subprocess.STDOUT)
                log.write(p.command('vm-ssh.sh', "dmesg | grep '^DWA121Diagnostic:' | tail -40", capture_output=True).stdout)
                summary['unloaded'] = b'local.dwa121.Diagnostic' not in p.command('vm-ssh.sh', 'kextstat', capture_output=True).stdout
            finally:
                (args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')


if __name__ == '__main__':
    awake = subprocess.Popen(['caffeinate', '-i'])
    try:
        main()
    finally:
        awake.terminate()
        awake.wait()
