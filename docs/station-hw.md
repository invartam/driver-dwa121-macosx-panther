# Authentification et association radio — v0.11.0

Cette étape raccorde la machine 802.11b/g utilisateur au transport USB OUT
asynchrone et au canal RX privilégié. L’outil `dwa121-associate SSID` attend
une balise récente PSK/CCMP, sélectionne son BSSID, émet l’authentification
ouverte puis la demande d’association et vérifie les réponses. Il s’arrête
à l’état d’attente des clés. Il ne demande pas de mot de passe, ne réalise
pas l’échange WPA2 et ne fournit pas d’interface réseau.

## Configuration matérielle

`station_hw.c` reprend un sous-ensemble de l’initialisation de
[OpenBSD rtwn.c](https://github.com/openbsd/src/blob/master/sys/dev/ic/rtwn.c)
et les définitions de
[r92creg.h](https://github.com/openbsd/src/blob/master/sys/dev/ic/r92creg.h).
Les sources de référence locales sont celles déjà utilisées par le portage ;
les notices restent dans le code et `NOTICE-OpenBSD.txt`.

Le matériel doit déjà être initialisé en réception persistante, sur le canal
explicitement configuré entre 1 et 11, à 20 MHz. La configuration vérifie
CR=0x2fa, TXPAUSE=0xff, SECCFG=0 et le filtre RX attendu. Elle calcule et relit
les indices de puissance eFuse du modèle pris en charge, programme MAC/BSSID,
les délais SIFS, le slot long, CCK1 avec préambule long et six tentatives
matérielles au maximum. Ces indices ne sont pas une mesure en dBm ni une
implémentation générale de la réglementation par pays.

Les réservations de pages RQPN sont rechargées avec les horloges TX actives, avant toute soumission, dans l’ordre power-on → DMA-init du pilote de référence.

La file de gestion s’ouvre en dernier : CR=0x2ff et TXPAUSE=0x6f. Les files
AC, high et beacon restent bloquées ; aucun trafic de données n’est permis.
La réception conserve les balises et les trames sélectionnées par le filtre
logiciel, avec contrôle FCS. Le mode promiscuous est retiré après installation
de l’adresse MAC. Le matériel ne passe pas encore en état associé avec
synchronisation TSF, AID, adaptation de débit ou chiffrement.

## API et durée de vie

La méthode IOUserClient 1 utilise une requête fixe de 536 octets et une réponse
de 48 octets, version 1. Elle est réservée à l’administrateur, avec vérification
des privilèges à chaque appel. La méthode RX 0 et son ABI restent inchangées.
La propriété `AssociationTXEnabled` doit être explicitement activée dans la
copie de test du kext ; elle est absente de l’archive normale.

- `BEGIN` exige le BSSID déjà sélectionné par le propriétaire exclusif et le
  canal déjà réglé. Il prépare le matériel et crée le transport.
- `SEND` accepte seulement les trames d’authentification ouverte et
  d’association, validées par le constructeur de descripteurs avec les adresses
  de la session. EAPOL, données, déauthentifications et descripteurs arbitraires
  sont refusés dans cette étape.
- `POLL` consomme une confirmation USB terminale. Une confirmation USB ne vaut
  ni ACK radio ni réponse d’association.
- `END` bloque les émissions, annule et draine USB, puis arrête aussi la
  réception, la radio et le firmware. Une nouvelle session exige un rechargement
  du kext pour ne pas réutiliser d’éventuelles trames présentes dans le FIFO.

Le noyau limite la session à trois requêtes d’authentification puis trois
requêtes d’association ; un retour à l’authentification après l’association
est refusé. La fenêtre d’admission utilise une horloge monotone, bornée à dix
secondes. Un timer indépendant arrête le matériel si le client cesse de
poller. La fermeture du client, sa mort, l’arrêt explicite, une erreur de
transport observée et le retrait du service utilisent le même nettoyage.
Un callback USB tardif conserve ses ressources conformément au moteur v0.10.

L’outil utilisateur n’émet la requête suivante qu’après confirmation USB.
Les délais de réponse sont d’une seconde ; les retransmissions conservent la
séquence et positionnent Retry. La réception est validée par la machine de
station existante. Une éventuelle trame EAPOL reçue après association est
comptée sans affichage de contenu et sans réponse.

## Reconstruction et essais

```sh
sh scripts/test-host.sh
caffeinate -i sh scripts/build-vm.sh
python3 scripts/exercise-association.py --ssid NAEBA --channel 11 \
  --output artifacts/association-v0.11-final
```

Le dernier script active réellement des émissions de gestion vers le réseau
indiqué, dans une copie de test du pilote. Il décharge le kext et conserve les
journaux même si l’essai échoue. Il ne modifie pas le réseau principal du Mac.

Les tests portables vérifient l’ABI, les adresses et le padding, les limites de
trames, le budget de tentatives et le débordement de l’horloge. Le faux bus
injecte une erreur à chaque lecture/écriture de configuration, avant et après
l’application d’une écriture, et vérifie le blocage des émissions. Les tests
existants de station, de descripteur et d’USB conservent leurs rôles respectifs.

## Résultat matériel du 24 octobre 2026

L’essai `artifacts/association-v0.11-auth-ie/` réussit sur la DWA-121 physique,
transmise à Panther par QEMU EHCI à 480 Mb/s :

- authentification : requête de 30 octets, réponse de 41 octets, statut 0 ;
- association : requête de 73 octets, réponse de 112 octets, statut 0, AID 28 ;
- deux transferts USB OUT confirmés, 62 et 105 octets (descripteur compris) ;
- une trame EAPOL reçue, sans réponse et sans validation d’un échange WPA2 ;
- 31/31 callbacks RX drainés, 36 balises, aucune erreur USB, CRC ou FCS ;
- files TX bloquées, radio et firmware arrêtés, kext déchargé après l’essai.

Le contrôle de traces `scripts/check-association-probe.py` exige les réponses
radio, les confirmations USB de même longueur et le nettoyage matériel. Une
simple confirmation USB ne suffit pas à déclarer l’association réussie.

Les premiers essais sont conservés : `trial1` et `trial2` ont expiré sur le
premier transfert USB non vide ; après un démarrage à froid de la VM et le
rechargement RQPN, `trial3` et `trial4` confirment les trois transferts mais
échouent à l’authentification. Les compteurs de `trial4` montrent quatre réponses
de 41 octets, du BSSID choisi vers notre MAC, avec FCS valide. Le filtre exigeait
exactement 30 octets. Le correctif accepte les TLV optionnels complets après le
corps fixe, sans assouplir les vérifications d’adresses, d’algorithme, de séquence
ou de statut. Les longueurs tronquées restent refusées dans les tests RX et station.
L’effet du redémarrage à froid et celui du rechargement RQPN n’ont pas été isolés.

La répétition finale réussit aussi (AID 29, deux transferts USB, une trame EAPOL).
Les trois gardes sont validées sur la même version : mort du client, perte de
privilèges après ouverture et expiration automatique à dix secondes. Chaque
essai bloque TX, draine les callbacks et arrête radio/firmware. Les résultats
sont consignés dans
[`validation-v0.11.0.json`](../artifacts/validation-v0.11.0.json).
L’USB 1.1 physique, le WPA2, les clés/compteurs CCMP et le trafic IP restent à
valider ou à raccorder.
