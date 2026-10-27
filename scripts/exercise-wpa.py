#!/usr/bin/env python3
"""Run a bounded initial WPA2 handshake; prompt for the passphrase, never save it."""
import argparse
import getpass
import hashlib
import json
import os
import plistlib
import resource
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
    parser.add_argument('--abandon-after-key', action='store_true')
    parser.add_argument('--dhcp-discover', action='store_true')
    parser.add_argument('--abandon-after-data', action='store_true')
    args = parser.parse_args()
    if args.abandon_after_key and args.dhcp_discover:
        parser.error('key guard and data discovery are separate probes')
    if args.abandon_after_data and not args.dhcp_discover:
        parser.error('data guard requires --dhcp-discover')
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    os.umask(0o077)
    if not 1 <= len(args.ssid.encode()) <= 32:
        parser.error('SSID must contain 1..32 bytes')
    if b'local.dwa121.Diagnostic' in p.command('vm-ssh.sh', 'kextstat', capture_output=True).stdout:
        parser.error('an existing diagnostic is loaded')
    password = getpass.getpass('WPA2 passphrase (not saved): ').encode()
    if not 8 <= len(password) <= 63 or b'\n' in password or b'\0' in password:
        parser.error('Expected 8..63 bytes without newline or NUL')
    args.output.mkdir(parents=True, exist_ok=True)
    source = '/Users/builder/dwa121/native/build/DWA121Diagnostic.kext'
    info = plistlib.loads(p.command('vm-ssh.sh', 'cat ' + source + '/Contents/Info.plist', capture_output=True).stdout)
    info['IOKitPersonalities']['DWA-121 A1 Diagnostic'].update(
        PersistentRX=True, PassiveRXChannel=args.channel, AssociationTXEnabled=True, WPA2ProbeEnabled=True)
    if args.dhcp_discover:
        info['IOKitPersonalities']['DWA-121 A1 Diagnostic']['CCMPDataProbeEnabled'] = True
    p.command('vm-ssh.sh', 'cat > /Users/builder/dwa121/live-info.plist', input=plistlib.dumps(info))
    summary = {'ssid': args.ssid, 'channel': args.channel, 'handshake_verified': False,
               'data_authorized': False, 'passed': False}
    with (args.output / 'kernel.log').open('wb') as log:
        try:
            p.command('vm-root.py', 'set -e; mkdir -p /Users/builder/dwa121/test-live; cp -R ' + source +
                      ' /Users/builder/dwa121/test-live/; cp /Users/builder/dwa121/live-info.plist ' + p.REMOTE +
                      '/Contents/Info.plist; chown -R root:wheel ' + p.REMOTE + '; kextload -c -t ' + p.REMOTE,
                      stdout=log, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 60
            while time.monotonic() < deadline:
                raw = p.run('vm-ssh.sh', p.INFO, capture_output=True)
                if raw.returncode == 0 and plistlib.loads(raw.stdout).get('RXRunning'):
                    (args.output / 'running.plist').write_bytes(raw.stdout)
                    break
                time.sleep(1)
            else:
                raise RuntimeError('service did not start')
            vm = Path(os.environ.get('PANTHER_VM_DIR', str(Path.home() / 'local/ppc-vms/panther')))
            credentials = json.loads((vm / 'credentials.json').read_text())
            command = '/Users/builder/dwa121/native/build/dwa121-wpa2 ' + shlex.quote(args.ssid) + ' --verify-pmk-stdin'
            if args.abandon_after_key:
                command += ' --abandon-after-key'
            if args.dhcp_discover:
                command += ' --dhcp-discover'
            if args.abandon_after_data:
                command += ' --abandon-after-data'
            # sudo reads its first line; the program consumes the remaining line.
            # Neither secret appears in argv, environment, files, or log output.
            payload = (credentials['password'].encode() + b'\n' + password + b'\n' +
                       hashlib.pbkdf2_hmac('sha1', password, args.ssid.encode(), 4096, 32))
            result = subprocess.run([str(ROOT / 'scripts/vm-ssh.sh'),
                                     "sudo -k && exec sudo -S -p '' /bin/sh -c " + shlex.quote(command)],
                                    input=payload, capture_output=True, timeout=90)
            if password in result.stdout or password in result.stderr:
                raise RuntimeError('Unexpected credential echo; output suppressed')
            del payload, password, credentials
            (args.output / 'wpa.log').write_bytes(result.stdout + result.stderr)
            print(result.stdout.decode(), end='', flush=True)
            print(result.stderr.decode(), end='', flush=True)
            summary['tool_exit'] = result.returncode
            summary['handshake_verified'] = result.returncode == 0 and b'handshake_verified=1' in result.stdout
            for attempt in range(50):
                raw, stopped = p.snapshot()
                if stopped.get('PersistentStopVerified'):
                    break
                time.sleep(0.2)
            (args.output / 'stopped.plist').write_bytes(raw)
            summary['keys_installed_mask'] = stopped.get('WPA2KeysInstalledMask', 0)
            summary['keys_erased'] = stopped.get('WPA2KeysErased', False)
            summary['eapol_submitted'] = stopped.get('WPA2EAPOLSubmitted', 0)
            summary['hardware_stopped'] = all(stopped.get(k) for k in
                ('PersistentStopVerified', 'RadioQuiesced', 'FirmwareQuiesced', 'RXRequestsDrained', 'AssociationTXDrained'))
            summary['passed'] = (summary['handshake_verified'] and summary['keys_erased'] and
                summary['hardware_stopped'] and stopped.get('WPA2HandshakeReported', False) and
                stopped.get('WPA2DataPortAuthorized') is False and stopped.get('WPA2ProbeActive') is False and
                stopped.get('AssociationTXErrors') == 0 and stopped.get('RXUSBErrors') == 0)
            if args.dhcp_discover:
                summary['dhcp_offer'] = b'dhcp_offer=1' in result.stdout and stopped.get('CCMPDHCPOfferReported') is True
                summary['encrypted_submitted'] = stopped.get('CCMPDataSubmitted', 0)
                summary['encrypted_received'] = stopped.get('RXCCMP', 0)
                summary['passed'] = (summary['passed'] and summary['dhcp_offer'] and
                    1 <= summary['encrypted_submitted'] <= 3 and summary['encrypted_received'] > 0 and
                    stopped.get('CCMPDataProbeActive') is False and stopped.get('CCMPSoftwareCrypto') is True)
            if args.abandon_after_key:
                summary['guard'] = 'process death after first key installation'
                summary['passed'] = (result.returncode == 0 and b'Key guard: exiting' in result.stdout and
                    bool(summary['keys_installed_mask'] & 16) and summary['keys_erased'] and
                    summary['hardware_stopped'] and stopped.get('WPA2ProbeActive') is False and
                    stopped.get('WPA2DataPortAuthorized') is False and not stopped.get('WPA2HandshakeReported'))
            if args.abandon_after_data:
                summary['guard'] = 'process death after first encrypted USB transmission'
                summary['passed'] = (result.returncode == 0 and b'Data guard: exiting' in result.stdout and
                    summary['keys_installed_mask'] & 16 and summary['keys_installed_mask'] & 15 and
                    summary['keys_erased'] and summary['hardware_stopped'] and summary['encrypted_submitted'] == 1 and
                    stopped.get('AssociationTXErrors') == 0 and stopped.get('RXUSBErrors') == 0 and
                    stopped.get('CCMPDataProbeActive') is False and stopped.get('WPA2ProbeActive') is False and
                    stopped.get('WPA2DataPortAuthorized') is False and stopped.get('WPA2HandshakeReported') is True)
        finally:
            try:
                if b'local.dwa121.Diagnostic' in p.command('vm-ssh.sh', 'kextstat', capture_output=True).stdout:
                    p.run('vm-root.py', p.CONTROL, stdout=log, stderr=subprocess.STDOUT)
                    raw, stopped = p.snapshot()
                    (args.output / 'cleanup-stopped.plist').write_bytes(raw)
                    summary['cleanup_verified'] = all(stopped.get(k) for k in
                        ('PersistentStopVerified', 'RadioQuiesced', 'FirmwareQuiesced'))
                    p.command('vm-root.py', 'kextunload ' + p.REMOTE, stdout=log, stderr=subprocess.STDOUT)
                log.write(p.command('vm-ssh.sh', "dmesg | grep '^DWA121Diagnostic:' | tail -50", capture_output=True).stdout)
                summary['unloaded'] = b'local.dwa121.Diagnostic' not in p.command('vm-ssh.sh', 'kextstat', capture_output=True).stdout
            finally:
                (args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    return 0 if summary['passed'] and summary.get('unloaded') else 1


if __name__ == '__main__':
    awake = subprocess.Popen(['caffeinate', '-i'])
    try:
        raise SystemExit(main())
    finally:
        awake.terminate()
        awake.wait()
