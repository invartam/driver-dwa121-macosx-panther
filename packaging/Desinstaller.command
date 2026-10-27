#!/bin/sh
set -eu
sudo /bin/sh /usr/local/libexec/dwa121/uninstall.sh
printf 'Appuyer sur Entree pour fermer. '
read -r ignored
