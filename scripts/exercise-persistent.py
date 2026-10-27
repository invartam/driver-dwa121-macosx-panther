#!/usr/bin/env python3
"""Exercise real persistent RX without USB detach or transmitting any frame."""
import argparse
import json
import os
import plistlib
import re
import shlex
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REMOTE = '/Users/builder/dwa121/test-live/DWA121Diagnostic.kext'
INFO = '/Users/builder/dwa121/native/build/dwa121-info'
CONTROL = '/Users/builder/dwa121/native/build/dwa121-rx stop'


def run(script, command, **kw):
    return subprocess.run([str(ROOT / 'scripts' / script), command], timeout=90, **kw)


def command(script, text, **kw):
    return run(script, text, check=True, **kw)


def snapshot():
    result = command('vm-ssh.sh', INFO, capture_output=True)
    return result.stdout, plistlib.loads(result.stdout)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--channel', type=int, default=11, choices=range(1, 12))
    ap.add_argument('--seconds', type=int, default=10, choices=range(5, 31))
    ap.add_argument('--allow-empty', action='store_true')
    ap.add_argument('--expect-watchdog', action='store_true',
                    help='verify a missing USB completion stops RX and still permits clean shutdown')
    ap.add_argument('--unload-running', action='store_true')
    ap.add_argument('--tx-preparation', action='store_true',
                    help='verify TX power and synthetic CAM keys with transmission disabled')
    ap.add_argument('--tx-usb-probe', action='store_true',
                    help='test asynchronous empty USB OUT on both pipes; no radio frame')
    ap.add_argument('--control-bridge', action='store_true',
                    help='exercise exclusive root user client, live queue, expiry and process death')
    ap.add_argument('--prepare-ssid', help='prepare (never transmit) auth from a live PSK/CCMP beacon')
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    if args.prepare_ssid and not args.control_bridge:
        ap.error('--prepare-ssid requires --control-bridge')
    if args.tx_usb_probe and not args.tx_preparation:
        ap.error('--tx-usb-probe requires --tx-preparation to establish stopped hardware preconditions')
    if args.expect_watchdog and args.unload_running:
        ap.error('--expect-watchdog requires the command-stop snapshot')
    args.output.mkdir(parents=True, exist_ok=True)
    if b'local.dwa121.Diagnostic' in command('vm-ssh.sh', 'kextstat', capture_output=True).stdout:
        ap.error('unload the existing diagnostic first')
    source = '/Users/builder/dwa121/native/build/DWA121Diagnostic.kext'
    info = plistlib.loads(command('vm-ssh.sh', 'cat ' + source + '/Contents/Info.plist', capture_output=True).stdout)
    personality = info['IOKitPersonalities']['DWA-121 A1 Diagnostic']
    personality['PersistentRX'] = True
    personality['PassiveRXChannel'] = args.channel
    personality['TXPreparationProbe'] = args.tx_preparation
    personality['TXUSBProbe'] = args.tx_usb_probe
    command('vm-ssh.sh', 'cat > /Users/builder/dwa121/live-info.plist', input=plistlib.dumps(info))
    summary = {'channel': args.channel, 'mode': 'unload-running' if args.unload_running else 'command-stop',
               'expected_watchdog': args.expect_watchdog}
    with (args.output / 'kernel.log').open('wb') as log:
        try:
            command('vm-root.py', 'set -e; mkdir -p /Users/builder/dwa121/test-live; cp -R ' + source + ' /Users/builder/dwa121/test-live/; cp /Users/builder/dwa121/live-info.plist ' + REMOTE + '/Contents/Info.plist; chown -R root:wheel ' + REMOTE + '; kextload -c -t ' + REMOTE, stdout=log, stderr=subprocess.STDOUT)
            # Wait for initialization and registration, not for completion of RX.
            deadline = time.monotonic() + 60
            while True:
                probe = run('vm-ssh.sh', INFO, capture_output=True)
                if probe.returncode == 0:
                    first = plistlib.loads(probe.stdout)
                    if first.get('PersistentRXActive') and first.get('RXRunning'):
                        (args.output / 'running-first.plist').write_bytes(probe.stdout)
                        if args.control_bridge:
                            assert first.get('ControlRXFilterVerified'), 'hardware control RX filter not verified'
                        if args.tx_preparation:
                            for key in ('TXPowerReadbackVerified', 'CAMCCMPReadbackVerified',
                                        'CAMProbeErasureVerified', 'TXPreparationNoTXVerified',
                                        'TXPreparationVerified'):
                                assert first.get(key), key
                            summary['tx_preparation_verified'] = True
                        if args.tx_usb_probe:
                            for key in ('TXUSBProbeVerified', 'TXUSBProbeDrained', 'TXUSBProbeNoRadioTXVerified'):
                                assert first.get(key), key
                            assert first['TXUSBProbeSubmitted'] == first['TXUSBProbeCompleted'] == 5
                            assert first['TXUSBProbeZLPWrites'] == 5
                            for key in ('TXUSBProbeDataWrites', 'TXUSBProbeBytesSubmitted',
                                        'TXUSBProbeFailed', 'TXUSBProbeRejected', 'TXUSBProbeWatchdogs'):
                                assert first[key] == 0, key
                            summary['tx_usb_empty_probe_verified'] = True
                        break
                if time.monotonic() > deadline:
                    raise RuntimeError('persistent service did not become active')
                time.sleep(1)
            print('Persistent RX running on channel %d' % args.channel, flush=True)
            if args.control_bridge:
                tool = '/Users/builder/dwa121/native/build/dwa121-control'
                denied = run('vm-ssh.sh', tool + ' watch', capture_output=True)
                (args.output / 'unprivileged-control.log').write_bytes(denied.stdout + denied.stderr)
                assert denied.returncode != 0 and b'Control open rejected:' in denied.stderr
                checked = run('vm-root.py', tool + ' selftest', capture_output=True)
                (args.output / 'control-selftest.log').write_bytes(checked.stdout + checked.stderr)
                assert checked.returncode == 0, 'control bridge selftest failed'
                assert b'PASS: process death' in checked.stdout
                summary['control_bridge_verified'] = True
                summary['unprivileged_control_rejected'] = True
                if args.prepare_ssid:
                    prepared = run('vm-root.py', tool + ' prepare ' + shlex.quote(args.prepare_ssid), capture_output=True)
                    (args.output / 'control-prepare.log').write_bytes(prepared.stdout + prepared.stderr)
                    assert prepared.returncode == 0 and b'PASS: live beacon' in prepared.stdout
                    summary['auth_prepared_from_live_beacon'] = True
            time.sleep(args.seconds)
            raw, later = snapshot()
            (args.output / 'running-later.plist').write_bytes(raw)
            assert not later['RadioQuiesced'] and not later['FirmwareQuiesced']
            if args.expect_watchdog:
                assert not later['RXRunning'] and later['RXRequestsDrained']
                assert later['RXUSBErrors'] == 1 and later['RXWatchdogs'] == 1
                assert later['RXUSBAborted'] == 1 and later['RXAbortRequests'] == 1
                assert later['RXSubmissionsRejected'] == 0
            else:
                assert later['RXRunning'], 'RX stopped unexpectedly'
                assert later['RXUSBErrors'] == 0 and later['RXWatchdogs'] == 0
            if not args.allow_empty and not args.expect_watchdog:
                assert later['RXBeacons'] > 64, 'RX did not continue past the old probe limit'
                assert later['RXBeacons'] > first['RXBeacons'], 'beacon count did not advance'
            # Root check must reject a user command without stopping the engine.
            denied = run('vm-ssh.sh', CONTROL, capture_output=True)
            (args.output / 'unprivileged-stop.log').write_bytes(denied.stdout + denied.stderr)
            assert denied.returncode != 0, 'unprivileged stop unexpectedly succeeded'
            _, still = snapshot()
            assert still['RXRunning'] == later['RXRunning'], 'unprivileged command changed RX state'
            summary.update({'first_beacons': first['RXBeacons'], 'later_beacons': later['RXBeacons'], 'later_callbacks': later['RXUSBCompleted'], 'unprivileged_stop_rejected': True})
            if args.unload_running:
                command('vm-root.py', 'kextunload ' + REMOTE, stdout=log, stderr=subprocess.STDOUT)
            else:
                if args.control_bridge:
                    checked = run('vm-root.py', '/Users/builder/dwa121/native/build/dwa121-control stop-check', capture_output=True)
                    (args.output / 'control-stop.log').write_bytes(checked.stdout + checked.stderr)
                    assert checked.returncode == 0 and b'PASS: hardware stop' in checked.stdout
                    summary['control_stop_revocation_verified'] = True
                else:
                    command('vm-root.py', CONTROL, stdout=log, stderr=subprocess.STDOUT)
                raw, stopped = snapshot()
                (args.output / 'stopped.plist').write_bytes(raw)
                assert stopped['PortStage'] == 'persistent-rx-stopped'
                assert stopped['PersistentStopVerified'] and stopped['RXHardwareStopVerified']
                assert stopped['RXRequestsDrained'] and stopped['RadioQuiesced'] and stopped['FirmwareQuiesced']
                assert not stopped['RXRunning']
                assert stopped['RXUSBSubmitted'] == stopped['RXUSBCompleted']
                assert stopped['RXAbortRequests'] <= 1
                assert stopped['RXUSBErrors'] == int(args.expect_watchdog)
                assert stopped['RXWatchdogs'] == int(args.expect_watchdog)
                command('vm-root.py', CONTROL, stdout=log, stderr=subprocess.STDOUT)
                raw, twice = snapshot()
                (args.output / 'stopped-twice.plist').write_bytes(raw)
                for key in ('RXUSBSubmitted', 'RXUSBCompleted', 'RXBeacons', 'RXAbortRequests'):
                    assert twice[key] == stopped[key], ('stop not idempotent', key)
                command('vm-root.py', 'kextunload ' + REMOTE, stdout=log, stderr=subprocess.STDOUT)
                summary['stopped_callbacks'] = stopped['RXUSBCompleted']
                summary['stopped_beacons'] = stopped['RXBeacons']
            assert b'local.dwa121.Diagnostic' not in command('vm-ssh.sh', 'kextstat', capture_output=True).stdout
            assert run('vm-ssh.sh', INFO, capture_output=True).returncode == 2
            summary['passed'] = True
        except BaseException as error:
            summary['passed'] = False
            summary['error'] = repr(error)
            raise
        finally:
            # Persist failure evidence even if SSH/recovery is unavailable.
            result_file = args.output / 'summary.json'
            result_file.write_text(json.dumps(summary, indent=2) + '\n')
            try:
                if b'local.dwa121.Diagnostic' in command('vm-ssh.sh', 'kextstat', capture_output=True).stdout:
                    run('vm-root.py', CONTROL, stdout=log, stderr=subprocess.STDOUT)
                    probe = run('vm-ssh.sh', INFO, capture_output=True)
                    if probe.returncode == 0:
                        (args.output / 'cleanup-stopped.plist').write_bytes(probe.stdout)
                    command('vm-root.py', 'kextunload ' + REMOTE, stdout=log, stderr=subprocess.STDOUT)
                dmesg = command('vm-ssh.sh', "dmesg | grep '^DWA121Diagnostic:' | tail -40", capture_output=True).stdout
                log.write(dmesg)
                if summary.get('passed'):
                    cleanup = [line for line in dmesg.splitlines() if b'persistent cleanup' in line]
                    assert cleanup and cleanup[-1].endswith(b'drained=1 RX=0 radio=0'), 'hardware cleanup not verified'
                    stopped = re.findall(rb'persistent RX stopped USB=(\d+)/(\d+) aborts=(\d+) drained=1 errors=(\d+) beacons=(\d+)', dmesg)
                    assert stopped and stopped[-1][0] == stopped[-1][1], 'drain log inconsistent'
                    assert int(stopped[-1][3]) == int(args.expect_watchdog), 'unexpected transport error count'
                    summary['stopped_callbacks'] = int(stopped[-1][0])
                    summary['stopped_beacons'] = int(stopped[-1][4])
                    summary['hardware_cleanup_verified'] = True
            except BaseException as error:
                summary['passed'] = False
                summary['cleanup_error'] = str(error)
                raise
            finally:
                result_file.write_text(json.dumps(summary, indent=2) + '\n')

    print('PASS: %s, privilege check, verified hardware stop/unload and service removal' %
          ('watchdog stop' if args.expect_watchdog else 'persistent RX'), flush=True)


if __name__ == '__main__':
    # A host idle sleep suspends QEMU and can invalidate USB deadlines. Scope
    # the assertion to this test process; explicit user sleep still wins.
    awake = None
    try:
        if sys.platform == 'darwin':
            awake = subprocess.Popen(['/usr/bin/caffeinate', '-i', '-w', str(os.getpid())])
        main()
    finally:
        if awake is not None:
            awake.terminate()
            awake.wait(timeout=5)
