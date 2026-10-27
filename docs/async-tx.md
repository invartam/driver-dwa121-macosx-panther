# Transport USB OUT asynchrone — v0.10.0

`PantherTXEngine` implémente un transport borné pour les descripteurs et trames
préparés par `rtl8188cu_tx_prepare`. Il est compilé avec les véritables API IOKit
de Panther. Il n’est pas encore raccordé au client utilisateur, au coordinateur
de session ou à une interface réseau. Aucun échange WPA2 radio n’est revendiqué.

## Transaction et durée de vie

Le moteur copie une trame à la fois dans son tampon DMA préalloué. Une nouvelle
requête reçoit `Busy` jusqu’à la consommation du résultat précédent. Les trames
de gestion passent par l’endpoint 2, EAPOL par l’endpoint 3. Les profils USB à
paquets maximaux de 64 et 512 octets sont acceptés ; leur incohérence est refusée.

Le transport ne prend pas de descripteur arbitraire : `sendFrame` utilise le
validateur existant pour les adresses, longueurs et types auth/association/EAPOL.
La configuration de la station, du pays/canal, de la puissance, des clés et du
port contrôlé reste à la charge du futur appelant noyau. Aucune commande IPC
ne permet actuellement d’envoyer une trame.

Une transaction contient une écriture DATA et, si sa longueur est un multiple
du paquet USB maximal, une seconde écriture de longueur zéro (ZLP). Le résultat
ne devient positif qu’après la confirmation de la dernière écriture. Une
confirmation avec des octets restants est une erreur, même si le statut USB
est positif. Une confirmation USB ne prouve pas la réception ou l’ACK radio.

**Particularité Panther EHCI vérifiée pendant le portage :** la méthode
`UIMCreateBulkTransfer` utilise la longueur du descripteur mémoire pour créer
les transactions, indépendamment de `reqCount`. Le moteur règle donc aussi
`IOBufferMemoryDescriptor::setLength` avant chaque soumission, y compris à zéro
pour un ZLP. Sans cela, le premier essai a tenté une écriture de 2112 octets
nuls au lieu d’un ZLP ; la surveillance l’a annulée, avec les DMA radio bloqués.
Le test IOKit simulé reproduit désormais cette sémantique, et la trace QEMU
doit confirmer les longueurs effectives au-delà des seuls compteurs du pilote.

La machine portable `tx_lifecycle.c` interdit la réutilisation du tampon avant
consommation du callback et la répétition d’une annulation. Une erreur, un délai
dépassé ou un arrêt interdit les nouvelles soumissions. Il n’y a aucune reprise
automatique d’une trame dont la livraison pourrait être partielle. Avant une
nouvelle session, le futur propriétaire devra remettre le matériel au repos,
réinitialiser son état et recréer le transport.

Chaque requête USB acceptée possède une référence indépendante au moteur,
donc au tampon, aux pipes et au service. Le callback, potentiellement exécuté
pendant `Write` ou `Abort`, enregistre seulement son statut et réveille la boucle
de travail. La préparation des trames, la suite ZLP et la publication du résultat ont lieu sous
la gate propre au moteur. Aucune attente ne conserve cette gate.

La surveillance demande l’annulation après 3 secondes sans callback. `shutdown`
interdit d’abord les soumissions, demande au plus une annulation et attend au
plus 5 secondes hors gate. Si le callback ne vient pas, le drainage reste faux
et la référence de requête conserve les ressources jusqu’à son arrivée. Le
tampon n’est ni effacé ni libéré pendant qu’USB peut encore le lire. Les octets
préparés sont effacés après un résultat terminal ou lors de la libération sûre.
Le champ `drained` d’un instantané décrit seulement l’état observé ; l’appelant
doit utiliser le résultat terminal et `shutdown`, qui interdit le réarmement,
avant de considérer le transport comme arrêté.

L’utilisation de `IOUSBPipe::Write` avec `reqCount` explicite a été vérifiée dans
la [source Apple IOUSBFamily 215.4.4](https://github.com/apple-oss-distributions/IOUSBFamily/blob/IOUSBFamily-215.4.4/IOUSBFamily/Classes/IOUSBPipe.cpp).
Cette référence est conservée séparément, avec empreinte dans
`docs/lifecycle-references.json` ; elle n’est pas incorporée au pilote.
Le choix de longueur est confirmé dans
[AppleUSBEHCI_UIM.cpp](https://github.com/apple-oss-distributions/IOUSBFamily/blob/IOUSBFamily-215.4.4/AppleUSBEHCI/Classes/AppleUSBEHCI_UIM.cpp).

## Vérification

Trois niveaux restent distincts :

- `tx_lifecycle_test` explore **2 470 629** ordres de soumission, callback,
  consommation, rejet et arrêt. Il vérifie les références, la séquence DATA/ZLP,
  le refus d’une nouvelle trame et le résultat terminal unique.
- `async_tx_test` compile le **même fichier `AsyncTX.cpp`** contre des doubles
  IOKit déterministes en espace utilisateur. Il vérifie USB64/512, les copies,
  l’authentification sans ZLP, EAPOL sur l’endpoint 3, les callbacks immédiats,
  les écritures partielles, les erreurs de temporisateur/soumission/ZLP, le
  watchdog, l’arrêt répété et la survie du tampon après libération du propriétaire.
  Ce test ne reproduit ni le contrôleur USB ni l’ordonnanceur réel du noyau.
- `TXUSBProbe`, désactivé par défaut, exerce exclusivement des écritures USB
  **de longueur nulle** sur la clé. Il ne transmet aucun descripteur ni trame
  Wi-Fi et exige `CR=0`, `TXPAUSE=ff`, `SECCFG=0` avant et après l’essai.
  Pendant les écritures, seul `HCI_TXDMA` est activé (`CR=1`) pour permettre
  la réception USB OUT par la clé ; le DMA radio et les deux blocs MAC restent
  désactivés. La valeur est relue avant les transferts puis restaurée à zéro.
  Quatre écritures alternent entre les deux pipes ; une cinquième est suivie
  immédiatement de l’arrêt. Elle peut déjà avoir réussi ou être annulée :
  le rapport distingue ces deux issues et exige tous les callbacks attendus.

Les tests de descripteurs existants vérifient séparément 3 900 longueurs USB64/512.
La réussite des écritures vides ne valide pas une trame DATA suivie de ZLP sur
le matériel, ni une transmission radio, ni le fonctionnement physique USB 1.1.

```sh
sh scripts/test-host.sh
sh scripts/build-vm.sh
python3 scripts/exercise-persistent.py --channel 11 --seconds 5 \
  --tx-preparation --tx-usb-probe --control-bridge --prepare-ssid NAEBA \
  --output artifacts/tx-usb-v0.10-final
python3 scripts/check-tx-usb-probe.py artifacts/tx-usb-v0.10-final
```

La prochaine étape fonctionnelle reste la configuration station MAC/BSSID/AID
et son cycle d’arrêt, puis le raccordement du transport au moteur d’association
et au supplicant. Les données CCMP, compteurs PN/RSC et l’interface réseau
restent également à implémenter et à vérifier sur le matériel.

## Résultat du 20 octobre 2026

La compilation Panther/GCC 3.3, les tests natifs PowerPC et la suite hôte
ASan/UBSan passent. Sur la DWA-121 en EHCI, le pilote reçoit **5 callbacks sur
5 requêtes** : quatre succès et une annulation d’arrêt, sans échec ni watchdog.
La trace QEMU confirme quatre requêtes OUT de taille zéro, successivement sur
les endpoints 2, 3, 2, 3 ; la cinquième est annulée avant d’atteindre QEMU.
Les registres sont vérifiés avec `CR=1` pendant la sonde et `CR=0` après.

L’essai continue avec 197 balises reçues et 269/269 callbacks RX drainés, sans
erreur USB/CRC/ICV/FCS. Le canal administrateur conserve ses contrôles d’accès,
d’expiration et de fermeture. Une demande d’authentification NAEBA est préparée
depuis une balise âgée de 1 ms, sans être émise. Le pilote est déchargé et la
radio arrêtée à la fin ; la VM reste disponible.

Preuves : `artifacts/tx-usb-v0.10-final/`,
`artifacts/build-panther-v0.10-final.log`, `artifacts/test-host-v0.10-final.log`
et `artifacts/validation-v0.10.0.json`. Les essais infructueux sont conservés :
`tx-usb-v0.10-trial1` (accès USB perdu avant le test, récupéré par démarrage
à froid), `tx-usb-v0.10-length-trial` (longueur du descripteur incorrecte) et
`tx-usb-v0.10-clock-off-trial` (HCI_TXDMA arrêté). Les deux derniers ont aussi
vérifié l’annulation par watchdog et le drainage d’un callback tardif ; leur
échec ne compte pas comme réussite de la sonde ZLP.
