# P0.2 — architecture et preuves Panther

Branche `v1/airport-p02`, référence conservée : `v0.15.0` (d0baecb).
Étude commencée le 27 octobre 2026, poursuivie les 28 et 29. Aucun fichier Apple
n'est modifié ou distribué. Les extractions locales restent dans `artifacts/`.

## Audit préalable du pilote

| Composant | Rôle et réutilisation |
| --- | --- |
| `src/kext/DWA121Diagnostic.{h,cpp}` | IOService propriétaire de IOUSBInterface 2001:3308, firmware, radio, verrou de cycle de vie, publication des états et BSS |
| `src/kext/NetworkInterface.{h,cpp}` | DWA121NetworkInterface : IOEthernetController ; attachInterface, registerService publient IOEthernetInterface/enN ; activation conditionnée par NetworkSessionEnabled |
| `src/kext/NetworkBridge.cpp`, `src/control/network.*` | Files Ethernet bornées, heartbeat, lien uniquement sous session autorisée |
| `src/kext/ControlClient.*` | IOUserClient exclusif administrateur, type 0x44574131 ; méthodes 0 contrôle, 1 association, 2 WPA, 3 réseau |
| `src/kext/AssociationControl.cpp` | Vérification des opérations, budgets, sélection matériel, clés CAM et fermeture du port |
| `src/kext/PersistentRX.*`, `AsyncTX.*` | Moteurs USB asynchrones, annulations et drainage ; à conserver |
| `src/control/{protocol,association,wpa}.*` | ABI PPC native sans pointeurs, tailles et versions contrôlées |
| `src/net80211/`, `src/rtl8188cu/` | Balises, station, EAPOL, CCMP et opérations matérielles |
| `src/tools/dwa121-associate.c`, `wpa_probe.inc`, `network_runtime.inc` | Association, moteur hostap WPA2, pont données et connexion persistante |
| `src/tools/dwa121-service.c`, `packaging/DWA121/` | StartupItem Panther et superviseur root, verrou fcntl, reconnexion via arrêt/rechargement |
| `src/config/`, `packaging/prepare.sh` | SSID/canal/PSK, génération de personnalité ; configuration secrète non collectée |
| `src/netkext/` | Ancien compagnon Ethernet de diagnostic refusant l'activation ; ne pas le confondre avec le chemin actif v0.15 |

Le scan actuel est une collecte passive de balises sur le canal configuré,
pas un scan multicanal natif. La sélection BSSID se fait par SELECT ; SSID et
association sont traités par le programme utilisateur. Le canal est choisi
au démarrage ; END ferme aussi RX/radio et exige un rechargement.
RadioRunning, PersistentRXActive, RXChannel, DiscoveredBSS, compteurs USB,
CRC/ICV/FCS, NetworkSessionActive et WPA2DataPortAuthorized sont publiés.
Aucun RSSI calibré n'a été trouvé dans le parseur actuel : ne pas inventer de
niveau de signal. Les clés ne sont pas publiées.
Dépendances noyau : IOUSBFamily, IONetworkingFamily, kernel bsd/iokit/libkern/mach.
Le compagnon ajoute seulement kernel.iokit/libkern/bsd ; aucune API IO80211 moderne.

## Observations locales vérifiées avant implémentation

VM : Mac OS X 10.3.9, build 7W98, Darwin 7.9.0, PowerPC.
AppleAirPort.kext 3.1 : classe AirPortDriver, fournisseur AppleMacIODevice/radio.
AppleAirPort2.kext 3.1.2 : classe AirPortPCI, fournisseur IOPCIDevice.
AppleAirPortFW.kext est présent. IO80211Family.kext est absent.
Apple80211.framework est présent et exporte les fonctions **Wireless***.
AirPort.menu lie ce framework et SystemConfiguration ; Apple80211Monitor.bundle
est présent dans SystemConfiguration. Cela invalide toute hypothèse préalable
sur l'absence du framework sous cette installation de Panther.

Preuves reproductibles : `nm`, `otool -tvV`, `otool -L`, plists de la VM ;
extractions `artifacts/airport-p02/`. Les adresses ci-dessous concernent
exclusivement le binaire inspecté et ne sont jamais utilisées comme adresses
codées en dur.

| Interface | Preuve et certitude |
| --- | --- |
| Découverte IOKit | `_getInterfaceWithName` 0x91f20a38 : parcours IOService, IOObjectGetClass puis memcmp des 7 premiers octets avec « AirPort » ; vérifié par désassemblage et section cstring |
| Ouverture | WirelessAttach 0x91f20f34 : premier argument pointeur de référence, second nul ; IOServiceOpen type 12 ; vérifié |
| Transport | `_airPortExec` 0x91f23184 : préfixe commande d'un octet, méthode 0 IOConnectMethodStructureIStructureO, données ensuite ; vérifié |
| Lecture radio | WirelessGetPower 0x91f2206c et WirelessGetEnabled 0x91f21ba0 : commande 13, aucun payload ni sortie ; bits 0 et 0..1 du retour IOReturn ; vérifié |
| Écriture radio | WirelessSetEnabled : commande 12 et entier 32 bits ; non implémentée, politique et transitions à analyser |
| Informations | WirelessGetInfo2 : commande 18, sortie 56 octets puis conversions ; sous-ensemble vérifié et implémenté : MAC, mode infrastructure, état radio séparé ; association indisponible |
| Pilote Apple | AirPortUserClient::userPostCommand reçoit commande et payload, transmet à AirPortDriver::postCommand ; signature et extraction de l'octet vérifiées |
| SystemConfiguration | SCSchemaDefinitions.h contient Hardware, Type, DeviceName, PowerEnabled (CFNumber), PreferredNetwork, JoinMode et ses valeurs ; AirPortServiceIDCopy filtre Hardware=AirPort dans les entités Interface du Dynamic Store |

L'existence de constantes ne démontre ni leur écriture automatique ni le
fonctionnement du menu. Le préfixe de classe est nécessaire à cette découverte,
mais ne suffit pas à reproduire les comportements AirPortDriver/AirPortPCI
que le framework distingue ensuite. Le prototype garde un nom propre.

## Choix avant prototype

| Critère | A : déclaration Ethernet/SC | B : compagnon de contrôle |
| --- | --- | --- |
| Panther | Configuration possible, contrôle absent | Protocole local identifié |
| Complexité | Faible pour l'affichage | Faible pour lecture radio, élevée pour scan/association |
| Fiabilité | Reconnaissance superficielle | Réponse issue du fournisseur réel, limites explicites |
| Risque noyau | Aucun si SC seulement | Nouveau client à borner ; aucune écriture matériel |
| Maintenance | Ne résout pas le protocole | Module isolé et désinstallable |
| Réutilisation/WPA2 | Moteur intact | Moteur intact, client WPA exclusif préservé |
| P0.3/P0.4 | Insuffisant seul | Base possible, structures et événements encore à établir |

Choix : B pour le contrôle, complété par A pour la présentation du service.
Le compagnon est attaché au contrôleur DWA121NetworkInterface, dont le parent
est DWA121Diagnostic. Il lit RadioRunning et MACAddress sur ce parent sans
ouvrir le client exclusif du supplicant. Le chemin Ethernet reste identique.
Une simple déclaration SystemConfiguration n'a pas suffi lors du premier
essai : le contrôle natif et la découverte des ports doivent aussi fonctionner.

WPA/WPA2 natifs : présence de WirelessSetWPAKey/ WirelessCreatePSK vérifiée,
mais ni WPA2 natif ni capacités UI tierces démontrés. Conserver le supplicant
existant. P0.3 exige scan multicanal, structures de résultats et notifications ;
P0.4 exige arbitrage avec le propriétaire WPA, authentification/clé et cycle de
vie radio complet. Aucun indicateur de capacité non implémentée ne sera annoncé.

## Découverte native et configuration vérifiées

Deux mécanismes distincts ont été établis dans les binaires Panther locaux :

- Apple80211 utilise le **nom de métaclasse C++** du service (préfixe AirPort).
  Le compagnon s'appelle réellement AirPortDWA121 ; son client s'appelle
  DWA121AirPortClient pour éviter une découverte parasite.
- NetworkConfig.framework, `-[PortScanner rescanPorts]` à 0x93a13d2c, lit
  la **propriété IOClass du contrôleur réseau** et teste `hasPrefix:@"AirPort"`.
  Le compagnon publie temporairement IOClass=AirPortDWA121 sur ce contrôleur,
  puis restaure sa valeur initiale à l'arrêt. Sa métaclasse reste
  DWA121NetworkInterface, dérivée de IOEthernetController ; aucune méthode
  TX/RX, classe Apple ou fonctionnalité matérielle n'est remplacée.

`dwa121-airport-service add` crée un service SCPreferences identifié et marqué
DWA121-P02 : Interface={Hardware=AirPort, Type=Ethernet, DeviceName=enN},
entité AirPort vide, nom « DWA-121 AirPort P0.2 ». Le nom BSD est découvert
via la filiation réelle. Le service est lié au CurrentSet et ajouté **en fin**
de ServiceOrder ; ce dernier point est nécessaire à sa visibilité dans
Internet Connect. Les configurations IP existantes et leur priorité relative
restent intactes. `remove` exige la marque propriétaire et retire uniquement
ce service, son lien et son entrée d'ordre, sous verrou SCPreferences.
PowerEnabled, PreferredNetwork et JoinMode existent dans le schéma Panther,
mais ne constituent pas des commandes matérielles ; P0.2 ne les utilise pas
pour prétendre associer le réseau ou activer une radio.

Apple80211Monitor ne recherche que les classes exactes AirPortDriver et
AirPortPCI. Le relais séparé `dwa121-airport-monitor --watch` est donc nécessaire
pour publier l'état du compagnon. Son `updateSC` original utilise des
CFNumberShort « Power Status », « Card Mode », « Link Status » dans
State:/Network/Interface/enN/AirPort. Le relais reproduit ce format avec
radio réelle, mode infrastructure 1 et association indisponible 0.
La clé est temporaire, liée à la session SCDynamicStore ; une clé préexistante
est refusée. Aucun service de démarrage n'est installé.

Les notifications distribuées ont été vérifiées dans `_notifyClients`
(0x34fc) : nom AirPort_Notification, objet AirPort Monitor, option 2,
dictionnaire de chaînes AirPortDriverLoaded=1/0 et Power_On=1. Le relais
les émet aux transitions correspondantes. Il refuse la coexistence d'une
carte Apple afin de ne pas ambiguïser ces notifications globales historiques.
Cela ne suffit pas à achever la logique d'affichage du menu, voir limites.

## Protocole et durée de vie

IOServiceOpen type 12, méthode 0 structure/structure, commande sur un octet :

| Commande | Contrat implémenté | Source du résultat |
| --- | --- | --- |
| 13 | entrée 1, sortie 0 ; IOReturn 0 ou 3 | RadioRunning réel, bits attendus par WirelessGetPower/GetEnabled |
| 18 | entrée 1, sortie exactement 56 | MAC réelle à +40, mode infrastructure BE16 à +4 ; autres champs indisponibles initialisés à zéro |
| Autres | Unsupported | Aucune écriture ni capacité annoncée |

`AirPortDriver::getWirelessStatus` (0x2698) initialise le premier mot à zéro,
utilise un port status nul lorsque l'état matériel est indisponible, place
MAC à +40, SSID à +6 et BSSID à +46. Le mode vaut 1 lorsque CREATE_IBSS
(HCF RID 0xfc81) est nul, 4 pour IBSS et 2 pour la station logicielle.
Le moteur RTL existant est uniquement station infrastructure. WirelessGetInfo2
convertit ces données vers un buffer de 64 octets : statut +8, mode +10,
radio +14 via commande 13 séparée, MAC +58. P0.2 ne remplit ni SSID/BSSID,
ni RSSI, ni association ; le zéro signifie information indisponible dans ce
contrat minimal, pas déconnexion démontrée du moteur WPA2.

IOExternalMethod utilise flags=3, count0=count1=0xffffffff ; les tailles sont
vérifiées dans le handler. count1=0 ferait sélectionner une autre signature
par le noyau. La table originale AirPortUserClient::start (0x6448–0x6474)
confirme ces sentinelles. WirelessGetPower/GetEnabled masquent certains échecs
RPC : l'outil compare donc aussi le retour brut, les propriétés réelles,
la MAC, les conversions et les bornes des deux structures.

Un verrou protège l'accès aux fournisseurs. `stop` restaure IOClass et libère
les références au contrôleur et au pilote USB avant la disparition des anciens
clients ; garder ces références jusqu'à `free` empêchait le superviseur de
recharger le kext. Le test avec port client conservé 10 secondes démontre
maintenant le déchargement USB et la révocation du port (MACH_SEND_INVALID_DEST).
Les traces p_comm/p_pid proviennent de current_proc, avec kernel.bsd et les
headers réels Panther. Elles distinguent les applications Apple du test.

## Résultat P0.2 et limites

Le prototype 0.2.0 démontre la reconnaissance native dans les Préférences
Réseau et Internet Connect, ainsi qu'un échange complet en lecture seule avec
le framework Apple original. SystemUIServer PID 3722 émet les commandes 18
et 13 ; le pilote répond avec succès et RadioRunning=1. La preuve ne repose
donc pas sur une icône ou un simple renommage de service.

D1 est démontré dans la VM. D2 reste incomplet : le menu reçoit les réponses,
mais affiche encore « AirPort: Off » lorsque la radio est active. Les
Préférences Réseau affichent correctement AirPort activé, non connecté selon
le statut natif incomplet. D3/D4 sont démontrés pour la lecture radio/MAC,
pas pour l'activation, le scan ou l'association. **P0.2 est livré comme
prototype de reconnaissance et de contrôle natif, avec cette réserve D2 ;
il ne constitue pas une intégration complète du menu.**

La reprise WPA2, DHCP et les tests courts de trafic utilisent exclusivement
le moteur v0.15.0. Les incidents EHCI et l'endurance restent à examiner ;
aucune validation USB1.1 physique nouvelle n'est revendiquée. Voir la procédure
de tests pour les résultats finaux et le retour arrière.

P0.3 est faisable au niveau transport, mais demande d'établir les structures
de scan, les notifications et la machine d'état du menu, puis de traduire
l'association réelle. P0.4 doit arbitrer les commandes natives avec le
supplicant exclusif, protéger les écritures et les clés, et caractériser
les capacités WPA/WPA2 de cette installation Panther. Leur fonctionnement
natif n'est pas encore démontré. Aucun binaire Apple n'est distribué.
