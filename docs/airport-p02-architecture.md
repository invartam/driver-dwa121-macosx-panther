# P0.2 — architecture et preuves Panther

Branche `v1/airport-p02`, référence conservée : `v0.15.0` (d0baecb).
Étude commencée le 27 octobre 2026, poursuivie le 28. Aucun fichier Apple
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
Le compagnon envisagé n'ajoute qu'IOKit/libkern ; aucune API IO80211 moderne.

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
| Informations | WirelessGetInfo2 : commande 18, sortie 56 octets puis conversions ; structure complète non établie, non implémentée |
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

Choix : B, module séparé attaché à DWA121Diagnostic avec catégorie propre,
lecture seule de RadioRunning via copyProperty. A seule ne peut satisfaire
la découverte de classe observée. Ne pas modifier les préférences réseau pour
masquer ce manque. La lecture native sera d'abord exercée par un petit outil
lié au framework original ; cela ne vaut pas preuve d'une action du menu.

WPA/WPA2 natifs : présence de WirelessSetWPAKey/ WirelessCreatePSK vérifiée,
mais ni WPA2 natif ni capacités UI tierces démontrés. Conserver le supplicant
existant. P0.3 exige scan multicanal, structures de résultats et notifications ;
P0.4 exige arbitrage avec le propriétaire WPA, authentification/clé et cycle de
vie radio complet. Aucun indicateur de capacité non implémentée ne sera annoncé.

## Résultats du prototype et limites établies le 28 octobre

Le module séparé `src/airport/` est compilé avec le SDK réel, PPC 750 sans
AltiVec. Il conserve une référence au fournisseur pendant la vie des clients,
refuse les opérations sur un service inactif et ne prend pas possession du
client WPA. Seule la commande 13 est implémentée. Une dépendance kernel.bsd
est ajoutée pour tracer `current_proc()->p_pid/p_comm`, dont les déclarations
ont été vérifiées dans sys/proc.h et sys/vm.h du SDK Panther.

La table IOExternalMethod doit utiliser `0xffffffff` comme taille variable de
sortie même lorsque la réponse attendue a zéro octet. Avec count1=0, le noyau
choisit une autre signature d'appel. La table d'AirPortUserClient::start
(0x6448–0x6474) confirme les deux sentinelles et le flag 3. Le prototype
vérifie ensuite lui-même les tailles. Premier essai erroné conservé dans
`load-native.log` ; il n'est pas compté comme validation. De plus,
WirelessGetPower/GetEnabled peuvent masquer une erreur RPC : la sonde compare
maintenant le retour brut et les valeurs natives à RadioRunning.

Échanges réussis avec le framework original : radio arrêtée (retour brut 0,
power=enabled=0), puis radio active (retour brut 3, power=enabled=1).
Les traces distinguent l'outil `dwa121-airport` du vrai `SystemUIServer`.
Après relance du menu, SystemUIServer PID 1281 a envoyé deux commandes 18,
entrée 1 octet, sortie 56 octets. Elles atteignent le compagnon mais ne sont
pas implémentées. La version finale renvoie Unsupported pour cette opération.

Essai A+B : ajout temporaire de deux clés Dynamic Store
`Setup:/Network/Service/DWA121-P02/{Interface,AirPort}` avec Hardware=AirPort,
Type=Ethernet, DeviceName=en2 et PowerEnabled=0. Aucune préférence persistante
n'a été écrite. Internet Connect est resté sans onglet AirPort. Le menu affiche
AirPort: Off ; cela ne prouve pas son bon fonctionnement, notamment lorsque
la radio réelle est active. Les deux clés ont été supprimées après l'essai.
La relance de SystemUIServer est un facteur confondant : cet essai ne démontre
pas que les clés temporaires sont nécessaires aux commandes 18 observées.
Les Préférences Réseau détectent encore « Ethernet Adaptor (en2) ».

Blocage précis : compléter la structure WirelessStatus de 56 octets et les
conventions d'état, puis les événements/configuration attendus par le menu.
`AirPortDriver::getWirelessStatus` (0x2698) confirme des champs (adresse à
+0x28, données BSSID à +0x2e, chaîne à +6, états à +2/+4/+0x34), mais les
enums et leur correspondance avec la station RTL ne sont pas suffisamment
établis. Ils ne sont donc pas simulés. Aucun RSSI, association, SSID courant
ou capacité WPA2 native fictifs ne sont renvoyés.

**P0.2 reste partiellement réalisé, pas validé au sens des critères complets.**
D1 : service de contrôle reconnu par le framework, interface réseau toujours
Ethernet. D2 : menu visible mais incomplet. D3 : demande réelle du menu tracée
et refus explicite ; lecture complète via outil lié au framework original.
D4 : lecture de l'état du moteur existant, sans duplication matérielle.
Les phases P0.3/P0.4 sont plausibles pour le transport, encore non démontrées
pour les structures, notifications, sécurité et orchestration du supplicant.

Le bundle final s'appelle `local.airportp02.DWA121` : son nom initial
`local.dwa121.AirPortP02` déclenchait à tort le refus de concurrence USB du
superviseur lors d'une reprise. Aucun fichier de supervision v0.15.0 n'a été
modifié. Cette correction et l'incident EHCI sont détaillés dans les tests.

Contre-épreuve finale sans clés Dynamic Store : SystemUIServer PID 1630 envoie
bien deux commandes 18 au compagnon (reconnect-final.log). Les clés de l'essai A
ne sont donc pas nécessaires à ce transport. La reprise recrée le fournisseur,
mais la connexion s'arrête avant le contrôle réseau suivant : stabilité de
reconnexion non validée, en plus du blocage d'intégration UI.
