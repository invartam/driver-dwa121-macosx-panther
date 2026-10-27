# Chemin de données CCMP en espace utilisateur

Ce module est construit dans `libdwa-wpa2.a`. Depuis la v0.14, une
[sonde matérielle bornée](ccmp-hardware.md) le raccorde aux transferts USB
pour une découverte DHCP chiffrée. **Aucune interface réseau Panther
n’est encore fournie.** Les tests logiciels décrits ici ne suffisent pas,
à eux seuls, à prouver le transport radio.

## Contrat

`src/supplicant/data.h` fournit un contexte par association, lié à la MAC
station et au BSSID. Un seul propriétaire sérialise tous les appels. Les clés
proviennent des callbacks du supplicant ; seul son callback `port(1)`, après
M3 vérifié et M4 transmis, peut ouvrir le chemin de données. Ce module ne
vérifie pas lui-même l’échange WPA. La sonde WPA2 simple garde le port fermé ;
la sonde de données ouvre uniquement son contexte logiciel de diagnostic
après le callback `handshake_complete` et les contrôles noyau supplémentaires.

Les trames Ethernet II de 14 à 1514 octets deviennent des trames 802.11 ToDS
non-QoS, LLC RFC1042, CCMP-128 (MIC 8 octets). L’adresse de destination
Ethernet est conservée dans Addr3 ; même une destination multicast passe par
la clé pairwise vers le point d’accès. La réception accepte FromDS non-QoS
ou QoS, unicast avec PTK ou multicast avec GTK. Addr3 est la source Ethernet,
qui peut être une autre machine du LAN, pas seulement le point d’accès.

Le module refuse les fragments, A-MSDU, WDS, HT-control, les trames déjà
déchiffrées et les trames non protégées. EAPOL ne sort pas vers Ethernet ; son
acheminement chiffré pour les renouvellements de clés reste à raccorder.
AARP/IPX, qui nécessitent une encapsulation différente, sont refusés. Il
n’ajoute ni FCS ni descripteur USB. Le transport doit vérifier FCS, fraîcheur,
longueurs et arrêt de session avant de lui remettre une trame.

## Compteurs, intégrité et durée de vie

Le PN TX est un entier 48 bits manipulé octet par octet. Il commence à 1 et
est consommé avant de retourner la trame, même si son émission USB est
ensuite abandonnée ou échoue. Il ne reboucle jamais : l’épuisement ferme le
contexte et efface les clés. Le numéro de séquence 802.11 est indépendant et
reboucle à 4096. Les nouvelles tentatives matérielles d’un même MPDU devront
conserver la trame ; on ne doit jamais restaurer un ancien PN.

Chaque clé dispose de 16 compteurs de réception. Les trames non-QoS partagent
le compteur du TID 0, car elles utilisent la même priorité dans le nonce.
Le RSC initial, au plus six octets little endian fourni par WPA,
initialise tous ces compteurs. Un PN doit être strictement supérieur au
compteur concerné. Le compteur avance seulement après validation du MIC et
de l’encapsulation LLC prise en charge. Les paquets hors ordre sont rejetés ;
aucun réordonnancement Block Ack n’est implémenté.

Réinstaller exactement la même clé dans son emplacement ne modifie aucun
compteur. Remplacer une clé dans un emplacement déjà occupé, ou placer la
même clé dans un autre emplacement, est refusé. Une nouvelle GTK distincte
peut occuper un emplacement libre ; la réutilisation d’un emplacement exige
une nouvelle association avec des clés fraîches. Il s’agit d’une limite
explicite de cette première version, pas d’un renouvellement GTK permanent.

Fermer/effacer après la première installation de clé est irréversible pour
ce contexte. Les fermetures initiales sans clé lors de la création du
supplicant sont sans effet. Détruire le contexte efface toute sa mémoire.

Le CCM de hostap reçoit uniquement des buffers internes alignés et arrondis
au bloc AES : son dernier bloc partiel écrit jusqu’à 16 octets. Le déchiffrement
reste dans un buffer privé tant que le MIC n’est pas vérifié. Sur erreur,
la longueur de sortie vaut zéro et le buffer du demandeur reste intact.
Les buffers temporaires contenant du clair sont effacés par écritures
volatiles. Les buffers d’entrée/sortie peuvent être non alignés mais ne
doivent pas se chevaucher ; aucun pointeur n’est conservé après l’appel.

## Tests reproductibles

Sur l’hôte :

```sh
WPA_EXTRA_CFLAGS='-fsanitize=address,undefined -g' \
WPA_BUILD_DIR=artifacts/wpa-core-data-sanitized sh scripts/test-wpa.sh
```

Dans la VM autorisée :

```sh
caffeinate -i sh scripts/build-wpa-vm.sh
```

Le paquet `artifacts/DWA121-WPA2-core-panther-ppc.tgz` contient la bibliothèque,
les sources et trois exécutables : `station-test`, `wpa2-test`, `data-test`.
Les tests utilisent uniquement des clés synthétiques et aucune clé USB.
Le moteur testé utilise l’AES interne de hostap ; l’oracle distinct utilise
le chiffrement d’un bloc AES fourni par OpenSSL et construit CCM séparément.

Les vérifications couvrent le vecteur CCM publié, les 1501 tailles Ethernet,
les buffers non alignés à capacité exacte, PTK/GTK et les TID, l’altération
des en-têtes/MIC/données, les troncatures, le rejeu et les RSC, les clés
réinstallées, les retenues/débordements du PN, 4100 émissions encodées puis
abandonnées et 10000 entrées malformées. Le test de session raccorde les
callbacks WPA réels à ce contexte et vérifie l’ouverture après M3/M4, la
continuité du PN lors d’un M3 répété et la fermeture sur désauthentification.

## Références et prochaine intégration

Le format AAD/nonce suit
[OpenBSD ieee80211_crypto_ccmp.c, révision 1.22](https://raw.githubusercontent.com/openbsd/src/master/sys/net80211/ieee80211_crypto_ccmp.c) ;
sa notice est conservée dans `data.c`. La construction CCM et le vecteur 1
proviennent de la [RFC 3610, sections 2 et 8](https://www.rfc-editor.org/rfc/rfc3610).
L’AES/CCM de production vient de hostap 2.12 et conserve sa licence BSD.
Le partage du compteur non-QoS/TID 0 est également vérifié dans
[Linux mac80211, `ieee80211_parse_qos`](https://github.com/torvalds/linux/blob/master/net/mac80211/rx.c).

La v0.14 ajoute la file RX chiffrée, l’opération TX bornée et une découverte
DHCP utilisant ce contexte logiciel, avec contrôle explicite contre le double
chiffrement matériel. Voir [la sonde et sa validation](ccmp-hardware.md).
Il reste à créer l’interface réseau, obtenir et gérer un bail DHCP, raccorder
ARP/IP et les renouvellements EAPOL chiffrés, puis valider le PowerBook USB 1.1.
