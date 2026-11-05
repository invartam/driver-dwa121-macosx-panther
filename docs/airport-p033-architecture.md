# P0.3.3 — état natif d’association

Base propre `685b6c6`, branche `v1/airport-p033`. Aucun changement du moteur
WPA2 ni du transport du secret n'est envisagé ; la référence VM reste v0.15.0.

## Source de vérité avant modification

`AirPortControl.cpp` réserve SSID exact, BSSID et canal issus d'une balise
compatible fraîche. Le client vérifie cette génération et cette identité,
puis impose le BSSID/canal à la sélection de balise et à `DWA_ASSOC_BEGIN`.
Requested → Prepared → Running décrit la réservation et l'ouverture du client,
pas le succès de WPA2. La station utilisateur vérifie séparément les réponses
d'authentification/association 802.11. Le supplicant traite ensuite EAPOL.

`NetworkBridge.cpp::networkAliveLocked` exige simultanément controlOwner,
liveRX, liveTX, wpaVerified, dataProbeActive, fournisseur actif non terminé
et `dwa_net_active(networkBridge, now)` (port ouvert et heartbeat vivant).
`DWA_NET_OPEN` n'est permis qu'après vérification WPA2/CCMP ; DHCP vient après.
Le statut AirPort passe à Connected uniquement si cette condition est vraie.
Le timer, la fermeture du propriétaire et les erreurs arrêtent le matériel,
ferment le port et invalident la réservation. Une annulation correspond à la
génération courante, puis produit Disconnected. Les anciennes réponses TAKE
ou CANCEL ne peuvent pas viser une nouvelle génération.

Le compagnon lit cette cible par l'appel interne STATUS sous le verrou du
cœur. P0.3.2 ne copie SSID/BSSID/résultat 1 dans la commande 18 qu'en Connected,
mais laisse le statut de port à 0. Le relais SC ne publie que la radio,
Card Mode=1 et Link Status=0. Aucun état IP ne sert à inférer l'association.

## Preuves statiques Panther 10.3.9

Les adresses ci-dessous identifient les binaires Apple originaux locaux ;
aucune adresse ni aucun binaire Apple ne sera intégré au pilote.

- `WirelessGetInfo2` 0x91f21c98 : réponse 18 de 56 octets convertie en 64,
  port +2 → +8, mode +4 → +10, SSID +6 → +24, MAC +40 → +58,
  BSSID +46 → +18 et résultat +54 → +16. Commande 13 → power +14.
- Si power non nul et port=4, le framework exige ensuite HCF FD44 (débit)
  puis commande 3 (qualité). Refuser ces lectures fait échouer toute la
  lecture WirelessGetInfo, même si la commande 18 est correcte.
- `StatusController currentStatus` 0x84d8 : copie le port dans `_linkStatus`
  (+0xa). Pour 0, 1 ou 2, remplace le réseau par une représentation sans SSID.
  Pour les autres états, copie le SSID et le BSSID natifs. Un échec de l'API
  efface également l'identité. Les noms exacts des états restent à vérifier.
- `scanResultDictionary` 0x721c : compare le titre du résultat avec
  `_networkName` (0x74c4–0x74d8), exclut le mode de lien 3 pour le chemin
  infrastructure, appelle **setState:1** et ajoute le tag 0x100 à 0x74e8–0x7500.
- Point discriminant : si aucun résultat n'a été marqué, 0x76d0 teste
  `_linkStatus > 2`, crée lui-même une entrée à partir de `_rawSSID` et
  `_networkName`, puis appelle **setState:1** à 0x7734–0x7744. Ainsi, le menu
  Apple sait représenter un réseau courant absent du scan ; il n'est pas
  nécessaire de fabriquer une entrée de scan pour cette situation.
- Références de sélecteurs résolues dans le Mach-O : 0xd560 `setState:`,
  0xd55c `title`, 0xd3fc `isEqualToString:`, 0xd564 `setTag:`.

Hypothèse issue de cet audit, puis confirmée par les expériences ci-dessous :
exposer le statut d’association et sa lecture annexe permet l’approche A
(état courant uniquement), sans modifier le scan ni conserver un vieux cache.

## Expériences et décision

C1 sans association : radio 3, port 0, résultat 0, identité vide. C2 après
vraie saisie native : handshake/CCMP et DHCP réussis, SSID/BSSID corrects et
résultat 1 dans la réponse 18, mais port toujours 0. C3 : le menu connecté
n'affiche aucun réseau, le scan étant refusé. C5 : relancer SystemUIServer
ne résout pas cette absence. C4 : une déconnexion native remet résultat 4
et efface SSID/BSSID ; le scan réel redevient possible.

Variante A : conversion du seul statut port en 4 après Connected, avec la
lecture de qualité 3 nécessaire à WirelessGetInfo2. Aucun changement de scan
ni du relais SC. **Coche native NAEBA observée**, conservée à la réouverture,
puis disparue après déconnexion ; NAEBA et RETRO sont à nouveau découverts.
Le chemin d'insertion native sans résultat de scan est donc confirmé.

La variante A comportait également une lecture HCF FD44 expérimentale. Elle
n'a jamais été appelée : `_isClient` est faux pour cette façade, débit natif
+12 reste nul. Ce handler a été retiré de la version finale. Les commandes
concernées sont uniquement **18 et 3** ; 13, 8/9, 26/17 restent inchangées.

## Qualité indisponible et limites du contrat

Commande 3 : entrée totale 9, deux entiers BE32 0 et 92, sortie 12. Constante
originale à 0x91f2be90 : `00000000 0000005c`. `getCardStatistics` Apple produit
un triplet d'entiers normalisés ; WirelessGetInfo2 convertit le premier en
niveau d'icône et recopie les demi-mots bas des autres. Cela n'est pas une
API de RSSI en dBm. Le prototype conserve des champs de qualité nuls comme
représentation minimale indisponible, sans annoncer de signal positif ni
prétendre avoir mesuré un signal nul. `P031RSSIAvailable=false` est conservé.
Les barres de signal restent absentes : **seule la coche constitue la preuve
d'association**, pas l'intensité de l'icône. Aucun décodeur PHY n'est ajouté.

La lecture 3 renvoie uniquement ces métriques indisponibles, indépendamment
du lien, et ne modifie aucun état. Tailles et paramètres différents sont
refusés. La perte de lien est exprimée par la prochaine lecture 18, sans
faire échouer artificiellement la lecture de métriques sans identité.

Les consommateurs étudiés groupent les ports 0/1/2 comme non associés, 3
comme réseau ad hoc, et 4 comme associé infrastructure. Le polling connaît
aussi 5/6, mais leurs significations matérielles complètes ne sont pas
établies : ils ne sont jamais émis. 0 reste l'état non associé/indisponible ;
la radio Off est distinguée par 13, l'attente par résultat 0, et l'échec ou
la déconnexion par résultat 4. Aucun nouveau code hypothétique n'est émis.

## Implémentation et synchronisation

`airport_status.c` convertit une copie bornée de cible réelle vers 56 octets,
valide version/génération/longueur/identité/canal avant de publier Connected,
et efface toujours les champs d'association pour les autres phases. Les
entiers Apple sont encodés par octets BE ; aucune structure non alignée n'est
interprétée par cast. Le compagnon ne met pas en cache l'identité et publie un instantané vide non associé lorsque le cœur est temporairement
Busy pendant la calibration ; aucune identité précédente n'est réutilisée.

Le relais existant lit maintenant 18 en plus de 13 et publie Link Status=4
uniquement pour un statut/résultat connecté avec radio active, sinon 0.
Les trois clés SC gardent leurs noms et CFNumberShort originaux. Il écrit
avant les notifications, seulement lors d'une transition radio/lien. Aucune
notification distribuée hypothétique n'est ajoutée : le polling original
Apple et les mises à jour SC suffisent. Aucun redémarrage de SystemUIServer
n'est requis pour connexion, déconnexion ou reconnexion ; seuls les essais
de rechargement du bundle utilisent une relance explicite.

Le cache de scan, son TTL, ses générations et le moteur matériel ne changent
pas. Le menu construit lui-même l'entrée courante depuis la véritable
association ; ni scan hors canal, ni nouvelle entrée de scan, ni prolongation
des anciennes balises ne sont nécessaires. DHCP reste indépendant.

## Contrôle local : défaut préexistant confirmé

Le test de déconnexion native exécuté comme `builder`, sans sudo, réussit sur
une session active. Le type natif 12 de P0.3.2 ne contrôle pas l'UID de la
commande 17 ; le relais est root mais accepte cette demande de déconnexion.
Les commandes 3/18 de ce jalon sont strictement des lectures et n'élargissent
pas cette capacité. Ce défaut n'est pas corrigé silencieusement ici.

Correction minimale proposée pour P0.4 : mémoriser l'identité de l'initiateur
autorisé lors de la sélection, vérifier côté noyau que les commandes de
mutation viennent de root ou de cette session console autorisée, et faire
valider les changements de session par le relais privilégié. Le test devra
inclure un second UID local, la fermeture de session et le transfert d'un
port Mach. Restreindre simplement à root casserait SystemUIServer utilisateur.

Autres limites : changement d'AP/roaming non ajouté, perte physique AP et
reset USB à chaud à qualifier, USB 1.1 séparé. Les générations précédentes
restent contrôlées par le protocole P0.3.2 ; un ancien client ne se rattache
pas implicitement à un nouveau fournisseur.

## Contre-épreuve : erreur API et faux mode de partage

L'arrêt brutal du supplicant a exposé un piège du polling Apple :
`pollWirelessGetInfo` (0x7d14), branche d'erreur 0x7d5c–0x7da8, fixe
`is Client=false` et PowerState=true. `AirPortExtra airPortStatusChanged:`
(0x2af0–0x2b38) interprète cela comme Internet Sharing. Le CFString
0x92b0 est exactement `is Client`. Une erreur Busy durant la recalibration
pouvait donc afficher « Internet Sharing: On », malgré l'absence de partage.
La mise à jour suivante peut conserver ce mode si port/nom restent identiques.

Correction : Busy donne une réponse 18 non associée, identité vide, résultat
inconnu ; la commande 3 reste une lecture indépendante de qualité indisponible
et n'échoue pas simplement parce que le lien a cessé entre les deux appels.
Le framework distingue alors le lien perdu d'une API défaillante. Les erreurs
réelles de fournisseur disparu restent des erreurs ; aucun provider de secours
n'est créé. La contre-épreuve après recompilation réussit : SIGKILL du supplicant,
phase Failed puis Disconnected, identité effacée et menu sans coche ni faux
mode de partage, sans relancer SystemUIServer.

Le canal réel reste disponible dans la cible privée du moteur (11 dans les
essais). Aucun champ de la réponse 18 n'est détourné pour le publier : la
position +54 est un résultat d'association, pas un canal. Les informations
IP restent exclusivement celles de la configuration réseau. La validation
et la restauration sont détaillées dans [le rapport de tests](airport-p033-tests.md).
