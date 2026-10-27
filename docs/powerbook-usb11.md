# Validation physique USB 1.1 — 26 octobre 2026

Le kit **v0.14.0 inchangé** fonctionne avec la DWA-121 A1 directement branchée
sur le port USB 1.1 du PowerBook. Réception, association NAEBA, échange WPA2
initial et découverte DHCP chiffrée réussissent. Ce résultat valide le
transport Full Speed de la sonde ; il ne fournit pas encore une interface
réseau utilisable dans Panther.

Machine : PowerBook3,1, PowerPC 750 à 400 MHz, 192 Mo, Mac OS X 10.3.9
(7W98), Darwin 7.9.0, AppleUSBOHCI 2.1.4 et IOUSBFamily 2.1.5.
Clé USB `2001:3308`. Réseau NAEBA en 2,4 GHz, canal 11.
Les sessions SSH passent par Ethernet ; aucun réglage réseau n'a été modifié.

## Résultats

Chaque commande utilise `scripts/test-physical.sh` du kit d'origine avec
`--usb11`. Le contrôleur natif vérifie **12 Mb/s et 64 octets bulk** dans les
instantanés de démarrage et d'arrêt.

| Essai | Résultat | Transferts USB terminés |
| --- | --- | --- |
| `rx 11 --usb11` | 97 balises NAEBA vérifiées | 113/113 IN |
| `associate 11 NAEBA --usb11` | Association AID 23 ; deux M1 reconnus | 29/29 IN, 2/2 OUT |
| `wpa 11 NAEBA --usb11` | M3 vérifié, M4 transmis, PTK et GTK installées/relues | 177/177 IN, 4/4 OUT |
| `data 11 NAEBA --usb11` | Un DHCPDISCOVER chiffré, offre DHCP authentifiée | 252/252 IN, 5/5 OUT |

Les quatre essais passent les contrôles de résultat, d'arrêt et de
déchargement. Aucune erreur USB, expiration de surveillance ou donnée USB
malformée n'est signalée. L'annulation normale de la lecture encore en attente
à l'arrêt est comptabilisée séparément.

Dans l'essai de données, cinq trames de groupe sont authentifiées, sans rejet.
L'offre annonce `10.12.89.127`, serveur `10.12.89.1`. Aucun DHCPREQUEST n'est
émis : cette adresse **n'est pas attribuée au PowerBook**. Le noyau compte six
trames CCMP reçues, ce qui ne remplace pas la vérification cryptographique
effectuée par l'outil utilisateur. Le test confirme également le refus
d'ouverture avant la fin de WPA2 et de réouverture de la fenêtre active.
Les clés sont effacées, les requêtes USB drainées, le firmware et la radio
arrêtés, puis le module déchargé. Ethernet reste actif à `10.12.89.126`.

## Blocage initial résolu

Le premier chargement échouait avant l'initialisation radio :
`USB interface busy`. Le journal Apple précisait
`someone already has it open`. Classic était actif et un
`IOUSBInterfaceUserClient` était attaché à l'interface de la clé.
L'arrêt de Classic par l'utilisateur a fait disparaître ce client.
Un deuxième chargement n'a pas démarré de service ; après déchargement puis
débranchement/rebranchement de la clé, les quatre essais ont réussi.

La procédure demande désormais d'arrêter Classic. Le script du dépôt conserve
les messages USB du noyau et l'arbre des clients en cas d'expiration au
démarrage. Sa syntaxe a été contrôlée avec le shell de Panther. Cette
amélioration du script n'est pas incorporée à l'archive v0.14.0 conservée ;
les essais ci-dessus utilisent tous son script original.

## Preuves et limites

[Rapport JSON](../artifacts/validation-powerbook-v0.14.0.json), avec compteurs,
empreintes et chemins des journaux. Les résultats extraits et les archives
brutes se trouvent dans `artifacts/powerbook-v0.14/`. Sur le PowerBook, le kit
et les résultats restent dans `/Users/benjy/dwa121-test-v0.14-20261026`.
Les tests logiciels `dhcp-test`, `control-test` et `tx-hardware-test` passent
également sur ce G3. Le MD5 après transfert correspond à l'archive locale ;
aucun binaire n'a été recompilé pour obtenir ces résultats.

Il reste à implémenter l'interface Ethernet Panther, le bail DHCP complet et
le trafic ARP/IP général. La réception CCMP unicast réelle, le renouvellement
des clés, les longues sessions, la veille, le retrait à chaud et la terminaison
brutale du client ne sont pas validés par ces essais physiques. Les 12 Mb/s
décrivent la liaison USB, pas un débit Wi-Fi mesuré.

La [procédure de test](test-machine-physique.md) permet de reproduire ces essais
sans installation permanente d'extension système.
