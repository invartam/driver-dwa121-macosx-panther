#!/bin/sh
set -eu
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
bin=/usr/local/libexec/dwa121
printf 'Nom exact du reseau 2,4 GHz (SSID) : '
IFS= read -r ssid
printf 'Canal du reseau (1 a 11) : '
IFS= read -r channel
case "$channel" in 1|2|3|4|5|6|7|8|9|10|11) ;; *) echo 'Canal invalide.'; exit 1 ;; esac
sudo "$bin/dwa121-service" stop
sudo "$bin/dwa121-config" "$ssid" "$channel"
sudo "$bin/dwa121-service" start
echo 'Connexion en cours. Le service redemarrera automatiquement avec le Mac.'
echo 'Pour consulter la connexion, ouvrir Etat.command.'
printf 'Appuyer sur Entree pour fermer. '
read -r ignored
