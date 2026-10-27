# Diagnostic de données CCMP v0.14

Le mode `--dhcp-discover` prolonge l’échange WPA2 initial par un essai de
données chiffrées. Il utilise le [module CCMP utilisateur](ccmp-data.md), puis
le vrai moteur USB asynchrone. Il ne crée pas d’interface réseau et n’applique
aucune adresse IP à Panther. Le diagnostic WPA2 simple reste disponible.

## Séquence et limites

1. Authentification/association sur le BSSID choisi, puis M1–M4 comme en v0.13.
2. Installation et relecture CAM des clés, et installation des mêmes clés dans
   le contexte CCMP utilisateur. `SECCFG` reste à zéro : le chiffrement est
   logiciel. Aucun mot de passe ni clé n’est journalisé.
3. Après M3 vérifié et M4 terminé côté USB, rapport `VERIFIED`, puis ouverture
   de la sonde de données. Le noyau vérifie CR, TXPAUSE et SECCFG avant de
   capturer les trames chiffrées. Une ouverture avant cet échange est testée
   et doit être refusée avec `kIOReturnNotPermitted`.
4. Au plus trois DHCPDISCOVER, identifiant aléatoire de 32 bits commun à
   l’essai, espacés d’au moins deux secondes. Destination Ethernet broadcast,
   IPv4 `0.0.0.0` vers `255.255.255.255`, UDP 68 vers 67. La transmission
   radio ToDS utilise toujours PTK et un nouveau PN.
5. Attente d’une DHCPOFFER déchiffrée et authentifiée, correspondant à la MAC
   et à l’identifiant de transaction. Contrôle des longueurs IPv4/UDP, des
   sommes de contrôle, de l’adresse de destination et des options DHCP.
   Aucun DHCPREQUEST n’est émis : aucune attribution de bail n’est confirmée.
6. Arrêt et effacement des clés côté utilisateur et CAM, drainage USB,
   arrêt radio/firmware et déchargement. Une absence de réponse est un échec
   de la sonde, même si l’échange WPA2 initial a réussi.

La boucle de données dure au plus neuf secondes, sous la limite noyau
globale de 30 secondes commencée à l’authentification (après l’effacement
initial de la CAM). Les transferts ont également leurs délais USB bornés.
Une fin USB prouve seulement le transfert vers la clé. Une DHCPOFFER avec
MIC, transaction et MAC valides fournit une preuve supplémentaire du
transport radio bidirectionnel ; elle ne prouve pas la connectivité Internet.

## Frontières du noyau

Les opérations 5, 6 et 7 de la méthode 2 exigent `CCMPDataProbeEnabled=true`,
en plus de `AssociationTXEnabled` et `WPA2ProbeEnabled`, l’administrateur
propriétaire de la session, les clés pairwise/group et le rapport WPA.
Ces trois propriétés d’activation sont absentes du bundle distribué et ne
sont ajoutées qu’à une copie temporaire pour l’essai.

`DATA_OPEN` ne peut être appelé qu’une fois. `DATA_SEND` accepte des trames
ToDS CCMP non-QoS de 48 à 1548 octets, Addr1=BSSID et Addr2=MAC de la clé,
KeyID pairwise zéro, avec un PN strictement croissant. Un PN est réservé
avant la soumission ; aucun retour arrière après erreur n’est possible.
Le budget est de trois trames. La voie d’émission initiale conserve son
filtrage gestion/EAPOL et refuse ces trames chiffrées.

Les descripteurs chiffrés utilisent BE/endpoint 3 et `SEC_TYPE=0` : il ne
faut pas chiffrer une seconde fois des octets déjà protégés par logiciel.
Les paquets USB terminant sur un multiple de 64 ou 512 octets conservent
la terminaison ZLP du moteur existant.

La réception chiffrée n’est activée que dans cette fenêtre. Le parseur
exige le BSSID sélectionné, la destination unicast locale ou un groupe,
une enveloppe CCMP bornée, le FCS correct et le bit `SWDEC` indiquant que
le matériel n’a pas déchiffré. Le MIC et le rejeu sont vérifiés ensuite en
espace utilisateur. Les données ne peuvent pas évincer les messages de
gestion/EAPOL de la file bornée. L’ouverture et la fermeture purgent la file.

Les rapports `WPA2HandshakeReported` et `CCMPDHCPOfferReported` proviennent
du programme administrateur : ce ne sont pas des vérifications cryptographiques
faites dans le noyau. `WPA2DataPortAuthorized`, `WiFiImplemented` et
`WPA2Implemented` restent faux puisqu’aucune interface réseau utilisable
n’est fournie par cette sonde.

## Lancer et contrôler

Depuis l’hôte de développement :

```sh
python3 scripts/exercise-wpa.py --ssid NAEBA --channel 11 \
    --dhcp-discover --output artifacts/ccmp-live
```

Le secret est demandé sans écho et transmis sur stdin, sans fichier, argument
de commande ou variable d’environnement. Le contrôleur vérifie également
le PMK indépendamment sur l’hôte, puis contrôle l’arrêt et le déchargement.

Sur le PowerBook, depuis le kit v0.14 extrait :

```sh
sudo sh scripts/test-physical.sh data 11 'NAEBA' --usb11
```

Le vérificateur natif `dwa121-check ... data` exige le rapport d’offre, au
moins une réception CCMP, entre un et trois envois, les clés effacées et
le matériel arrêté. L’option USB 1.1 exige une liaison réelle à 12 Mb/s ;
un essai EHCI ne la remplace pas.

## Validation et essai d’arrêt du processus

Le premier essai sur NAEBA dans Panther/EHCI réussit le 25 octobre 2026 :
M1/M3 reçus, M2/M4 terminés côté USB, trois DHCPDISCOVER chiffrés et une
DHCPOFFER authentifiée. Deux trames Ethernet sont déchiffrées, sans rejet
CCMP. Le parseur noyau a reçu trois trames CCMP, dont une n’a pas été traitée
avant l’arrêt. Les sept transferts OUT (1739 octets) et les 155 transferts IN
se terminent sans erreur ; les clés sont effacées et le module déchargé.
Voir `artifacts/ccmp-v0.14-trial1/summary.json` et `wpa.log`.

Le 26 octobre, l’essai `ccmp-v0.14-fresh` confirme le binaire livré : une
requête chiffrée suffit, deux trames de groupe sont authentifiées sans rejet,
dont l’offre DHCP attendue. Les cinq transferts OUT totalisent 923 octets.
Les refus d’ouverture avant WPA et de réouverture de la fenêtre active sont
confirmés. Les clés sont effacées et le module déchargé. La réception CCMP
unicast reste couverte par les tests logiciels, pas par cet échange réel.

Deux essais intermédiaires se sont arrêtés avant toute émission, sans balise
assez récente après CAM ; leur nettoyage a réussi. L’attente d’une nouvelle
balise passe à trois secondes, mais son âge maximal reste une seconde. Le
dernier essai l’a obtenue en 50 ms (âge 48 ms) ; cela ne démontre pas à lui seul
que les absences intermittentes ont disparu. Les journaux `ccmp-v0.14-final`
et `ccmp-v0.14-final-retry` conservent ces échecs.

Le garde-fou de fermeture est reproductible depuis l’hôte avec
`--dhcp-discover --abandon-after-data`. Le programme termine volontairement
sans nettoyage utilisateur après le premier transfert chiffré. Le contrôleur
exige alors l’effacement des clés, la fermeture de la capture et l’arrêt du
matériel. Le succès de ce garde-fou ne vaut pas réception d’une offre DHCP.
L’ouverture prématurée et la réouverture de la fenêtre active doivent toutes
deux être refusées, sans remettre à zéro le budget ni le PN.

Cet essai d’arrêt est validé le 26 octobre avec le même binaire, dans
`artifacts/ccmp-v0.14-data-guard` : un transfert CCMP, cinq transferts OUT
au total (923 octets), 141 lectures terminées, aucune erreur USB, clés
effacées, capture fermée, matériel arrêté et module déchargé. Aucun rapport
de réussite DHCP n’est attendu après cette terminaison volontaire.

## Validation USB 1.1 physique

La validation sur machine physique du 26 octobre utilise le même kit v0.14.0,
sur **PowerBook3,1 G3 400 MHz, Panther 10.3.9, OHCI USB 1.1**. Les quatre modes
RX, association, WPA et données passent avec `--usb11` : liaison 12 Mb/s,
paquets bulk 64 octets. L'essai de données transmet un DHCPDISCOVER, authentifie
cinq trames de groupe dont l'offre DHCP, sans rejet, puis efface les clés et
décharge le module. Les cinq transferts OUT (923 octets) et les 252 lectures
se terminent sans erreur USB. Le compteur noyau RXCCMP vaut six ; seule la
validation utilisateur des cinq trames établit leur authenticité. Voir le
[compte rendu physique](powerbook-usb11.md) et le
[rapport](../artifacts/validation-powerbook-v0.14.0.json).

## Références

Le descripteur TX suit [OpenBSD r92creg.h](https://raw.githubusercontent.com/openbsd/src/master/sys/dev/ic/r92creg.h).
La signification de `SWDEC` est vérifiée dans le
[pilote Linux rtl8192cu](https://linux.googlesource.com/linux/kernel/git/torvalds/linux/+/9b043680446067358913edc2e9dd71bf8ffae208/drivers/net/wireless/realtek/rtlwifi/rtl8192cu/trx.c).
La découverte DHCP et la distinction OFFER/ACK suivent la
[RFC 2131](https://www.rfc-editor.org/rfc/rfc2131.html).
Les options de découverte, d’identification et de réponse suivent la
[RFC 2132](https://www.rfc-editor.org/rfc/rfc2132.html).

Les limites suivantes subsistent : pas d’interface Ethernet Panther, DHCP
complet, ARP/IP général, renouvellement EAPOL chiffré, fragmentation, A-MSDU,
reconnexion automatique. L'USB 1.1 physique est validé pour ces sondes bornées ;
le débit soutenu, la veille et le retrait à chaud restent à tester.
