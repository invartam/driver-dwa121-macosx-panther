#!/usr/bin/env python3
"""Attach/detach only the DWA-121 A1 on the existing Panther QMP socket."""
import json, os, pathlib, socket, sys
if len(sys.argv) != 2 or sys.argv[1] not in ('attach', 'detach', 'status'):
    sys.exit('Usage: vm-usb.py attach|detach|status')
vm = pathlib.Path(os.environ.get('PANTHER_VM_DIR', str(pathlib.Path.home() / 'local/ppc-vms/panther')))
s = socket.socket(socket.AF_UNIX)
s.settimeout(10)
s.connect(str(vm / 'install-qmp.sock'))
f = s.makefile('rb')
def receive():
    line = f.readline()
    if not line:
        raise RuntimeError('QMP closed the connection')
    return json.loads(line)
def command(name, arguments):
    s.sendall((json.dumps({'execute': name, 'arguments': arguments, 'id': name})+'\n').encode())
    while True:
        reply = receive()
        if reply.get('id') == name:
            if 'error' in reply:
                raise RuntimeError(str(reply['error']))
            return reply['return']
try:
    if 'QMP' not in receive():
        raise RuntimeError('Expected QMP greeting')
    command('qmp_capabilities', {})
    action = sys.argv[1]
    if action == 'attach':
        arguments = {'driver': 'usb-host', 'vendorid': 0x2001, 'productid': 0x3308, 'id': 'dwa121'}
        if os.environ.get('PANTHER_USB_BUS'):
            arguments['bus'] = os.environ['PANTHER_USB_BUS']
        result = command('device_add', arguments)
    elif action == 'detach':
        result = command('device_del', {'id': 'dwa121'})
    else:
        result = command('human-monitor-command', {'command-line': 'info usb'})
    print(result)
finally:
    f.close()
    s.close()
