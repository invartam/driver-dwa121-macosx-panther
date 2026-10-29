# P0.2.1 — statut du menu AirPort de Panther

Branche `v1/airport-p021`, créée depuis `8e48fe5`. Étude du 29 octobre 2026.
Le jalon P0.2 et v0.15.0 restent dans leurs commits et branches de référence.
Aucun composant Apple ni chemin USB/TX/RX/WPA2 n'est modifié.

**Statut : partiellement validé.** Le défaut Off est corrigé et les états
On/Off sont démontrés avec radio réelle, sans association puis sous WPA2.
La validation complète du retrait à chaud reste ouverte après le crash QEMU ;
le contrôle depuis le bouton et les essais PowerBook restent non réalisés.

## Cause établie

Le service P0.2 contenait une entité **AirPort vide** dans SCPreferences.
Panther configd ne publie pas cette entité dans le Dynamic Store :
`Setup:/Network/Service/DWA121-P02/Interface` existe (Hardware=AirPort), mais
`Setup:/Network/Service/DWA121-P02/AirPort` est absent.

ClientController.bundle cherche les clés `Setup:/Network/Service/.*/AirPort`.
Sans résultat, il produit `AirPort Interface Exists = false`. Le menu stocke
cette information dans `_forcedOff`, indépendamment de `_powerOn`. Ainsi,
une réponse correcte à la commande 13 et une radio réellement active ne
suffisent pas : le menu affiche Off et propose seulement les préférences réseau.

La correction consiste à mettre le marqueur propriétaire existant
`DWA121P02Owner = local.airportp02.DWA121` **aussi dans l'entité AirPort**.
Celle-ci devient non vide et configd la publie. Il ne s'agit ni d'un état radio
positif, ni d'une capacité inventée : cette métadonnée identifie le service
réel du prototype. Aucun PowerEnabled, SSID, BSSID, RSSI ou état d'association
n'est ajouté. Le marqueur est retiré avec le service par l'outil existant.

## Preuves et expérience discriminante

Binaires originaux de la VM 10.3.9 7W98, inspectés par `otool -tvV`, `otool -ov`,
`otool -Iv` et lecture des sections Mach-O. Adresses propres à cette version,
jamais utilisées par le prototype à l'exécution :

| Composant | Observation vérifiée |
| --- | --- |
| ClientController, fonction 0x67c8 | Construit un motif Setup / Network / Service / regex / AirPort avec SCDynamicStoreKeyCreateNetworkServiceEntity ; CopyMultiple puis CFDictionaryGetCount > 0 |
| Pointeurs indirects 0xc7e8, 0xc7e4, 0xc7d0 | kSCDynamicStoreDomainSetup, kSCCompAnyRegex, kSCEntNetAirPort |
| ClientController, CFString 0xc534 | « AirPort Interface Exists » |
| AirPortExtra `airPortInterfaceCountChanged:` 0x2d0c | Lit cette clé, boolValue ; zéro positionne `_forcedOff` à +0x49 |
| AirPortExtra `menu` 0x1c50 | Teste `_forcedOff` à 0x1ce0 avant `_powerOn` à 0x1d78 ; branche forcée affiche Off et préférences réseau |
| AirPortExtra `airPortStatusChanged:` 0x2a2c | Lit PowerState (CFString 0x9070), puis éventuellement LinkStatus ; met `_powerOn` à +0x48 |
| StatusController `currentStatus` 0x84d8 (dans ClientController.bundle) | Appelle WirelessGetInfo ; lit le mot power à +14 du buffer natif et le convertit en booléen |

A/B sur une même radio active et un même SystemUIServer :

1. Service P0.2 inchangé : entité AirPort absente dans le Dynamic Store.
2. Publication temporaire de la seule entité portant le marqueur : menu On.
3. Retrait de cette seule entité, radio toujours active : retour à Off et
   disparition de « Turn AirPort Off » au profit des préférences réseau.
4. Correction persistante via SCPreferencesCommitChanges/ApplyChanges :
   l'entité est publiée et le menu affiche On.

Ni le contenu des commandes 13/18, ni Link Status=0, ni le relais et son
ordre de notifications n'ont changé entre les cas discriminants. Le nouveau
test `airport-service-test present` échoue sur P0.2, puis passe après la
correction ; il interroge le Dynamic Store appliqué, pas simplement le plist.
Les captures et traces locales sont listées dans le rapport de tests.

## Machine d'état : notions distinctes

Ces noms sont des concepts internes, pas des propriétés Apple inventées :

| Concept | Source réellement disponible | Utilisation P0.2.1 |
| --- | --- | --- |
| DevicePresent | Service AirPortDWA121 actif et fournisseur réel ; WirelessIsAvailable | Découverte native, clients liés au fournisseur |
| RadioEnabled | Autorisation administrative souhaitée | Pas d'état administratif distinct implémenté ; aucun faux drapeau |
| RadioRunning | Propriété réelle du core, positionnée après initialisation et remise à zéro après arrêt matériel | Commande 13 et Power Status |
| Associated | Machine de station utilisateur, réponses d'association validées | Non exporté dans le contrat AirPort minimal |
| LinkUp | Session réseau autorisée, état IOEthernetInterface, ifconfig | Vérifié séparément ; ne détermine pas l'état On du menu |
| IPConfigured | DHCP et adresse/route effectives sur enN | Test réseau séparé ; ne détermine pas l'état radio |

RadioRunning n'est pas une télémétrie matérielle indépendante : c'est l'état
observé et publié par le moteur. Lors du test Off, RadioRunning=No,
RadioQuiesced=Yes, PersistentStopVerified=Yes et RXHardwareStopVerified=Yes
corroborent l'arrêt effectif. En cas d'échec matériel, cette propriété ne doit
pas être assimilée à une intention utilisateur.

| Situation | Réponse et comportement établis |
| --- | --- |
| Radio active sans supplicant | Radio=1, mode infrastructure=1, association native indisponible=0 ; menu On, interface Ethernet inactive et sans IP |
| WPA2 connecté | Même état AirPort minimal ; menu On, trafic réel par le moteur existant |
| Arrêt propre du supplicant, kext présent | Radio=0, matériel arrêté et clés libérées ; menu Off sans relance SystemUIServer |
| Absence au démarrage | WirelessIsAvailable=0, Attach échoue ; « No AirPort card installed » observé |
| Réinitialisation du fournisseur | Les anciens ports sont invalidés ; le relais annonce la nouvelle présence et le framework rouvre une référence |

Le menu peut afficher des actions « Other… » et « Create Network… » parce que
c'est son interface standard. Cela ne valide pas ces fonctionnalités : scan,
IBSS et association restent Unsupported dans le compagnon.

## Contrats natifs conservés

IOServiceOpen type 12 ; méthode externe 0 structure/structure ; préfixe de
commande d'un octet. Flags=3 et tailles variables 0xffffffff dans la table
IOExternalMethod ; contrôle exact des tailles dans le handler. ABI PPC32,
valeurs multioctets big-endian, aucune structure de pointeurs partagée.

**Commande 13** : entrée 1, sortie 0, état encodé dans IOReturn.
WirelessGetPower et `_isPowerOn` prennent le bit 0. WirelessGetEnabled teste
si les bits 0..1 sont non nuls. Les valeurs peuvent donc diverger pour un
retour 2 ; le prototype retourne seulement 0 ou 3 suivant RadioRunning.
Les fonctions natives peuvent masquer une erreur de RPC : la sonde compare
le résultat brut et l'état du fournisseur, y compris après retrait.

**Commande 18** : entrée 1, sortie exactement 56. Conversions vérifiées dans
WirelessGetInfo2 (0x91f21c98), vers 64 octets :

| Source brute | Destination native | Sens établi / traitement |
| --- | --- | --- |
| +2, BE16 | +8, BE16 | Statut de port ; 0 = information indisponible dans le sous-ensemble P0.2 |
| +4, BE16 | +10, BE16 | Mode 1 infrastructure ; inchangé |
| +6, 34 octets | +24, 34 octets | SSID ; non renseigné |
| +40, 6 octets | +58, 6 octets | MAC réelle du fournisseur |
| +46, 6 octets | +18, 6 octets | BSSID ; non renseigné |
| +54, BE16 | +16, BE16 | Champ copié, sémantique complète non établie ; reste zéro |
| Commande 13 séparée | +14, BE16 | État power réellement lu |

WirelessGetInfo copie les 58 premiers octets de GetInfo2. La lecture du signal
est conditionnée au statut 4 et à power non nul dans GetInfo2 ; ce statut
n'est pas annoncé par le prototype. Le décodage complet des états associés
et des champs de qualité reste nécessaire pour P0.3, pas pour corriger Off.

## Dynamic Store, notifications et références

Le relais P0.2 est conservé. Sa clé temporaire State:/Network/Interface/enN/AirPort
contient trois CFNumberShort : Power Status=radio réelle, Card Mode=1,
Link Status=0. Ces types viennent de Apple80211Monitor::updateSC. La clé est
publiée avant les notifications ; retirée si le fournisseur disparaît ou
si le relais s'arrête. Une clé préexistante est refusée. La protection contre
une carte AirPort Apple simultanée est maintenue.

Le menu ne dépend pas uniquement des notifications distribuées : la
contre-épreuve A/B démontre sa réaction au changement du Dynamic Store.
Il distingue le service configuré (Setup) et l'état matériel (State/RPC).
Power Status=0 sans modification de Setup suffit au test On → Off réel.
L'association indisponible Link Status=0 n'est pas la cause du Off forcé.

AirPortDriverLoaded=1/0 reste la notification de présence/retrait ; le handler
original de ClientController (0x3e10) détache l'ancien WirelessRef et appelle
WirelessAttach lors de la valeur 1, détache et annule la référence pour 0.
Power_On=1 est conservé lors de l'activation réelle. Il n'est pas ajouté de
notification Off hypothétique : la publication du nouvel état et les lectures
natives ont suffi au passage Off observé. Le redémarrage du relais et celui
de SystemUIServer reconstruisent l'état On sans modifier les préférences.
Les correspondances exactes des classes Apple restent propres au moniteur
historique ; aucune classe Apple n'est usurpée.

## Commande 12 : caractérisée, non activée

La signature précédente était incomplète. Deux fonctions emploient la même
commande 12 avec un entier BE32 après l'octet de commande (entrée totale 5,
sortie 0) :

| Fonction Apple | Activer | Désactiver |
| --- | --- | --- |
| WirelessSetPower (0x91f22948) | 1 | 2 |
| WirelessSetEnabled (0x91f22854) | 3 | 4 |

StatusController `togglePower` (0x78c4, dans ClientController.bundle) utilise **WirelessSetPower**. Le clic
« Turn AirPort Off » a effectivement produit op12, in5/out0 depuis
SystemUIServer ; le compagnon a refusé l'opération et la radio est restée
active. Un succès matériel exige un retour IOReturn=0 ; le framework transforme
les erreurs en 0x88001003.

Le moteur actuel arrête proprement via le propriétaire WPA2 : fermeture du
port, drainage TX/RX, effacement des clés et quiescence radio. La réactivation
passe par le rechargement du core et une nouvelle session. Un appel synchrone
au compagnon, lui-même enfant du core, ne peut orchestrer cela sans protocole
supplémentaire avec le superviseur. Il faudrait une requête autorisée, un
acquittement, l'arbitrage avec le propriétaire existant, et une reprise même
quand le fournisseur a disparu. Aucun deuxième propriétaire USB n'est créé.
Cette évolution est isolée et reportée ; le contrôle en écriture n'est pas
présenté comme fonctionnel.

## Portée et réserves

La correction du statut est minimale : un champ de propriété du service,
une version de bundle 0.2.1, un test de publication et sa compilation.
Le compagnon de contrôle et le relais sont inchangés fonctionnellement.
Les détails des essais, de la récupération et du retour arrière figurent
dans `airport-p021-tests.md`.

Le retrait QMP a provoqué un SIGSEGV du processus hôte QEMU dans
`darwin_get_active_config_descriptor` → `usb_host_attach_kernel` →
`usb_host_close` → `qmp_marshal_device_del`. Ce n'est pas une validation du
retrait côté pilote. La VM a été redémarrée et l'utilisateur a rebranché la
clé. Aucun changement de libusb/QEMU n'entre dans ce jalon.

P0.3 peut réutiliser la découverte désormais correcte, mais doit encore
caractériser scan, état d'association réel et événements associés. P0.4 doit
résoudre les commandes administratives et l'arbitrage WPA2. Pas de nouvelles
capacités de sécurité annoncées, pas d'optimisation du débit à 1 Mbit/s.
La validation PowerBook G3 USB1.1, la veille et l'endurance restent à effectuer.
