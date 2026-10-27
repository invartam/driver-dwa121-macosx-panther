#!/bin/sh
set -eu
cd "$(dirname "$0")"
echo 'DWA121 pour Mac OS X 10.3.9 PowerPC'
echo 'La cle doit etre branchee. Arreter Classic avant de continuer.'
echo 'Le mot de passe administrateur est demande par sudo.'
sudo /bin/sh packaging/install.sh
/bin/sh Configurer.command
