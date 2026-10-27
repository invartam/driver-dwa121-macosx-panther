#!/usr/bin/env python3
"""Enable/disable QEMU USB request metadata tracing; no packet payloads."""
import argparse
import json
import os
from pathlib import Path
import socket

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('action', choices=('start', 'stop'))
parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parent.parent / 'artifacts' / 'qemu-usb-rx.log')
args = parser.parse_args()
vm = Path(os.environ.get('PANTHER_VM_DIR', str(Path.home() / 'local/ppc-vms/panther')))
with socket.socket(socket.AF_UNIX) as s:
    s.settimeout(10)
    s.connect(str(vm / 'install-qmp.sock'))
    with s.makefile('rb') as f:
        assert 'QMP' in json.loads(f.readline())

        def command(name, arguments):
            s.sendall((json.dumps({'execute': name, 'arguments': arguments, 'id': 'trace'}) + '\n').encode())
            while True:
                reply = json.loads(f.readline())
                if reply.get('id') == 'trace':
                    if 'error' in reply:
                        raise RuntimeError(reply['error'])
                    return reply['return']

        command('qmp_capabilities', {})
        if args.action == 'start':
            output = str(args.output.resolve())
            if any(c in output for c in ('"', '\n', '\r', '\\')):
                parser.error('output path cannot contain quotes, backslashes or newlines')
            print(command('human-monitor-command', {'command-line': 'logfile "' + output + '"'}))
        for event in ('usb_host_req_data', 'usb_host_req_complete', 'usb_host_req_canceled'):
            command('trace-event-set-state', {'name': event, 'enable': args.action == 'start'})
        log = ('log trace:usb_host_req_data,trace:usb_host_req_complete,trace:usb_host_req_canceled'
               if args.action == 'start' else 'log none')
        print(command('human-monitor-command', {'command-line': log}))
print('USB metadata tracing ' + ('enabled' if args.action == 'start' else 'disabled'))
