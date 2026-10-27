#!/bin/sh
sudo /usr/local/libexec/dwa121/dwa121-service status
sudo tail -30 '/Library/Application Support/DWA121/session.log'
/sbin/ifconfig -a
printf 'Appuyer sur Entree pour fermer. '
read -r ignored
