#!/bin/sh
set -eu
vm_directory=${PANTHER_VM_DIR:-$HOME/local/ppc-vms/panther}
exec /usr/bin/ssh -F /dev/null -p "${PANTHER_SSH_PORT:-2223}" \
  -o BatchMode=yes -o IdentitiesOnly=yes -o ConnectTimeout=10 \
  -o KexAlgorithms=+diffie-hellman-group1-sha1 \
  -o HostKeyAlgorithms=+ssh-rsa -o PubkeyAcceptedAlgorithms=+ssh-rsa \
  -o Ciphers=+aes128-cbc -o StrictHostKeyChecking=yes \
  -o "UserKnownHostsFile=$vm_directory/known_hosts" \
  -i "$vm_directory/id_rsa" builder@127.0.0.1 "$@"
