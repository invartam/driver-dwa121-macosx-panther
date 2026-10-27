// Development-only alternative boot. Shut Panther down before launching.
// Reuses the user's existing VM arguments and disk; adds a USB 2.0 controller.
import { spawn } from 'node:child_process';
import { homedir } from 'node:os';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';

const vmScript = process.env.PPC_VM_SCRIPT || join(homedir(), 'local/ppc-vm-scripts/ppc-vm.mjs');
const { bootArgs, parseOptions } = await import(pathToFileURL(vmScript).href);
const args = bootArgs('panther', parseOptions(['--headless']));
args.push('-device', 'usb-ehci,id=dwaehci', '-device',
          'usb-host,bus=dwaehci.0,vendorid=0x2001,productid=0x3308,id=dwa121');
const qemu = process.env.QEMU_PPC || '/opt/homebrew/bin/qemu-system-ppc';
if (process.argv.includes('--dry-run')) {
    console.log(JSON.stringify({ executable: qemu, arguments: args }, null, 2));
} else {
    const child = spawn(qemu, args, { stdio: 'inherit' });
    child.on('error', error => { console.error(error.message); process.exitCode = 1; });
    child.on('exit', code => { process.exitCode = code === null ? 1 : code; });
}
