# Canal de réception utilisateur — v0.9.0

Ce composant relie la réception USB persistante à un processus administrateur
sur Panther. Il prépare le raccordement du moteur d’association/WPA2 ; il
n’active ni émission USB OUT, ni association radio, ni interface réseau.

## Chemin de réception

`PantherRXEngine` vérifie les descripteurs USB, les erreurs CRC/ICV et le FCS
logiciel. Les balises continuent d’alimenter la table de découverte. Lorsqu’un
client est ouvert, une copie bornée des trames admissibles rejoint sa file :

- balises complètes et non fragmentées ;
- authentification ouverte, réponse d’association, désassociation et
  désauthentification unicast du BSSID choisi vers la MAC eFuse ;
- trames de données FromDS, non protégées, avec LLC/SNAP EAPOL exact et
  enveloppe EAPOL-Key RSN complète, provenant de ce même BSSID.

Les fragments, A-MSDU, en-têtes HT, adresses inattendues et trames protégées
sont refusés. Les EAPOL QoS sans A-MSDU sont acceptés. Le moteur de station et
le supplicant doivent encore vérifier leur état, les codes de réponse, les
MIC, compteurs de rejeu, longueurs internes et annonces RSN. Le filtre RX
ne constitue pas une authentification cryptographique du point d’accès.

Le filtre matériel laisse passer les données pour permettre cette sélection
logicielle : `RXFLTMAP2=ffff`, `RCR=9000680f`, valeurs relues. `CR=02fa`,
`TXPAUSE=ff` et `SECCFG=0` restent les préconditions : DMA TX désactivé,
files bloquées, déchiffrement désactivé. Cette configuration provient du mode
monitor des sources OpenBSD épinglées dans `docs/upstream.json`. Les données
ordinaires ne sont jamais transmises au client. La configuration station
MAC/BSSID/AID et la génération des ACK restent une étape distincte.

## Protocole IOKit privé

`IOServiceOpen` utilise le type `0x44574131` sur `DWA121Diagnostic`.
`IOConnectMethodStructureIStructureO`, index 0, échange les structures de
`src/control/protocol.h` : requête 16 octets, réponse 2364 octets, version 1.
Les entiers sont en ordre natif PowerPC ; aucune adresse noyau ni structure
contenant un pointeur ne traverse cette interface. Aucun mapping de DMA
n’est proposé. Les tailles, version, champs réservés et adresses sont vérifiés.

Les commandes sont `STATUS`, `SELECT` (BSSID), `RESET` et `NEXT`.
`NEXT` est non bloquante : le drapeau `FRAME` distingue une trame d’une file
vide. `age_ms` donne son ancienneté, mesurée à partir de la livraison USB
par une horloge monotone ; ce n’est pas l’horodatage radio TSF. La reprise
après veille n’est pas implémentée : arrêter et rouvrir le pilote après veille.

Le noyau vérifie le privilège administrateur à l’ouverture et à chaque RPC.
Un seul client peut être propriétaire. Fermeture, décès du processus et arrêt
du pilote révoquent la sélection et effacent la file. Le verrou de cycle de
vie protège le propriétaire et l’existence du moteur ; la gate du moteur
sérialise le filtre et la file avec les callbacks RX. Le callback USB conserve
sa gestion antérieure des références et ne manipule pas directement la file.

La file préallouée contient 16 trames de 2304 octets au maximum. Elle ne réside
pas sur la pile du noyau. Un débordement ne bloque jamais USB : une nouvelle
trame de contrôle évince la plus ancienne balise, ou est refusée si toutes les
places contiennent déjà du contrôle. Les nouvelles balises n’évincent rien.
Une trame âgée de plus de 5000 ms est effacée avant livraison. Les sorties sont
initialisées même si la file est vide. Les compteurs `dropped`, `expired` et
`delivered` sont propres à la sélection et remis à zéro avec celle-ci.

Ni trames EAPOL brutes ni clés ne sont publiées dans IORegistry. Ce canal ne
prend pas de mot de passe. Les annonces de réseaux déjà publiées par la sonde
restent accessibles comme auparavant.

## Utilitaire et essais

`dwa121-control selftest` teste les tailles/index/version invalides, l’accès
exclusif, la sélection et sa remise à zéro, les balises réelles, le débordement,
l’expiration et la récupération après décès d’un processus client. Un processus
enfant ouvre le canal comme root, passe à l’UID non privilégié 501 et vérifie
que sa connexion existante ne permet plus d’exécuter une commande. Le mode
`stop-check` arrête le matériel avec une connexion encore ouverte, vérifie sa
révocation ainsi que le refus d’une nouvelle ouverture, puis la ferme.
`watch` affiche seulement des compteurs pendant une observation bornée.
`prepare NAEBA` cherche une balise récente et compatible PSK/CCMP, appelle le
cœur de station et prépare un descripteur d’authentification pour les profils
USB 64 et 512 octets. Il sélectionne ensuite le BSSID et ferme le client.
**Aucune trame préparée n’est soumise au contrôleur USB.**

```sh
sh scripts/test-host.sh
sh scripts/build-vm.sh
python3 scripts/exercise-persistent.py --channel 11 --seconds 5 \
  --control-bridge --prepare-ssid NAEBA --tx-preparation \
  --output artifacts/control-v0.9-final
python3 scripts/check-control-probe.py artifacts/control-v0.9-final
```

Les tests portables couvrent les filtres d’adresse/FCS/EAPOL, les troncatures,
10 000 entrées USB arbitraires, la priorité de file, son effacement, l’expiration
et les débordements d’horloge/index. Les tests de registres injectent des
erreurs avant et après les écritures du filtre matériel.

Les échanges auth/association/EAPOL entrants sont vérifiés sur des vecteurs
synthétiques. La réception d’EAPOL après une vraie association, les écritures
USB OUT asynchrones/ZLP, le raccordement de la CAM au supplicant, les données
CCMP et l’interface réseau restent à réaliser. La validation physique USB 1.1
reste également en attente du PowerBook ou d’un hub Full Speed.

## Résultat du 19 octobre 2026

La compilation native GCC 3.3, les tests PowerPC et la suite hôte C89 sous
ASan/UBSan passent. L’essai final sur la DWA-121 transmise à Panther en EHCI
livre 74 balises au client pendant son observation ; le pilote en compte 254
au total avant arrêt. Les 301 requêtes USB sont drainées, avec une annulation
d’arrêt et aucune erreur USB, CRC, ICV ou FCS. La puissance TX et les clés
synthétiques CAM sont de nouveau vérifiées et effacées.

Le programme prépare l’authentification de NAEBA depuis une balise âgée de
72 ms : canal 11, 12 débits reconnus, trame de 30 octets et bloc USB préparé
de 62 octets pour l’endpoint 2. Il ne soumet aucun de ces octets à USB.
L’accès non privilégié, la seconde ouverture, les requêtes invalides et la
réutilisation après perte des privilèges sont refusés. Le décès du client
permet une nouvelle ouverture sans ancienne sélection. L’arrêt matériel
révoque une connexion encore ouverte. Le module est ensuite déchargé ; la VM
reste disponible et la clé reste transmise à QEMU.

Preuves : `artifacts/control-v0.9-final/`,
`artifacts/build-panther-v0.9-final.log`, `artifacts/test-host-v0.9.log` et
`artifacts/validation-v0.9.0.json`. Le premier essai de développement est
conservé séparément dans `artifacts/control-v0.9-trial1/`.
