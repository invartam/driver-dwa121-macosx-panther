#!/usr/bin/env python3
"""Run one command as root in the local development VM; never print its password."""
import json, os, pathlib, shlex, subprocess, sys
if len(sys.argv) != 2:
    sys.exit('Usage: vm-root.py "remote shell command"')
root = pathlib.Path(__file__).resolve().parent
vm = pathlib.Path(os.environ.get('PANTHER_VM_DIR', str(pathlib.Path.home() / 'local/ppc-vms/panther')))
credentials = json.loads((vm / 'credentials.json').read_text())
command = "sudo -S -p '' /bin/sh -c " + shlex.quote(sys.argv[1])
result = subprocess.run([str(root / 'vm-ssh.sh'), command], input=(credentials['password']+'\n').encode())
sys.exit(result.returncode)
