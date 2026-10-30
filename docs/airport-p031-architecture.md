# P0.3.1 — Scan passif Panther

## Audit préalable et décision

Base : `8f28875`, branche locale `v1/airport-p031`. Les constats et le correctif
SCPreferences de P0.2.1 sont conservés. La référence installée v0.15.0 reste séparée
des extensions expérimentales.

Chemin matériel : `radio.c` (MAC/BB/RF, IQ/LC) → `set_rx_channel` (RF 0x18,
MAC/DMA arrêtés, 1..11) → `dma.c` (LLT et filtres) → bulk IN →
`PantherRXEngine::completed` (notification seule) → workloop RX `consume` →
`rtl8188cu_rx_parse_control` (descripteurs LE, CRC/ICV/FCS) → `rtl_bss_observe`
(validation des IE, BSSID, SSID borné, DS/HT, RSN) → table et IORegistry.
Le RCR 0x9000600f autorise les management/broadcast avec PHY/FCS, sans filtrage
BSSID ; 0x9000680f ouvre aussi les données filtrées en logiciel pour WPA2.
Les queues TX restent arrêtées en réception passive. Le parseur retient les
Beacon, pas les Probe Response. Aucun constructeur Probe Request n'existe ;
le TX actuel est strictement associé à l'authentification/association/WPA2.
Le bloc PHY est sauté, sans conversion RSSI établie : aucun RSSI ne sera inventé.

Preuve avant modification du compagnon : réception réelle non associée dans la VM,
canal 11, `RXBeacons`/`BSSObservations` 54 puis 75 en deux secondes ; BSSID
58:1d:d8:78:bb:b8, SSID NAEBA, RSN PSK/CCMP, intervalle 100 TU. Journal local
ignoré : `artifacts/airport-p031/passive-baseline.log`.

Références examinées : sources OpenBSD référencées par `docs/upstream.json`,
notamment [rtwn.c](https://github.com/openbsd/src/blob/master/sys/dev/ic/rtwn.c),
et [Linux rtl8xxxu/core.c](https://github.com/torvalds/linux/blob/master/drivers/net/wireless/realtek/rtl8xxxu/core.c)
(`rtl8xxxu_config`, `rtl8xxxu_configure_filter`). Linux délègue les canaux à
mac80211 et retire les vérifications BSSID pour les balises/probe responses
promiscuous. Cela ne constitue pas une autorisation réglementaire supplémentaire.
Le prototype réutilise les primitives limitées à 1..11 du dépôt, sans émission
active, sans toucher au débit 1 Mbit/s.

Décision : balayage passif asynchrone sous l'unique propriétaire USB, seulement
sans client de contrôle. Chaque changement attend le drainage RX, arrête MAC/DMA,
programme/vérifie RF, recrée RX puis observe les balises. Le canal initial est
restauré avant de livrer les résultats. Aucune suspension implicite d'une session
WPA2 ; claimControl et scan s'excluent sous lifecycleLock. Les délais de séjour
sont gérés par timer sur le workloop propre du service, distinct de l'USB et du RX.

## Contrat natif identifié (désassemblage Panther 10.3.9)

Apple80211.framework installé, `WirelessDirectedScan2` 0x91f215b4,
`WirelessScan` 0x91f22694, `WirelessScanSplit` 0x91f226a0. IOServiceOpen type 12,
méthode 0 structure/structure, préfixe commande d'un octet, sentinelles variables
0xffffffff conservées. Aucun appel Tiger/IO80211Family.

| Commande | Entrée totale | Sortie | Convention |
| --- | --- | --- | --- |
| 8 | 34 : opcode + 33 octets SSID optionnel | 0 | 0 = recherche acceptée |
| 9 | 5 : opcode + index BE32 | 50 | 0 = entrée, 0x20000108 = en cours, 0x20000109 = fin |

La commande 9 est consultée par le framework après 8 ; pas de commande de compte
séparée. L'index commence à zéro et augmente jusqu'à la sentinelle de fin. Le
framework attend/réessaie le résultat en cours (borne de 120). Pas de notification
additionnelle nécessaire pour ce contrat de consultation.

Résultat de 50 octets, accès par octets sans cast non aligné : canal BE16 +0,
bruit +2, signal +4 (anciens indicateurs normalisés Hermes, pas dBm), BSSID +6,
intervalle Beacon BE16 +12, capacités BE16 +14, longueur SSID BE16 +16,
SSID 32 octets +18. Preuves : `AirPortDriver::lastScanResult` 0x2f24 (copie 50,
index/sentinelles), traitement indication HCF 0x4bcc–0x4d84 (conversion des mots,
BSSID/SSID), `WirelessDirectedScan2` (longueur, déduplication SSID/IBSS),
`WirelessScanSplit` (capacité IBSS 0x2). Les binaires/désassemblages Apple restent
locaux et ne sont pas distribués.

Le framework traite ensuite le SSID comme une chaîne C malgré sa longueur :
les SSID avec NUL incorporé ou de 32 octets sans terminaison exigent une politique
explicite de compatibilité, jamais une lecture hors limites ni une troncature
présentée comme le SSID réel. La table interne conserve toujours les octets exacts.


## Implémentation retenue et validation du contrat

Le cœur 0.15.1 expose deux appels **internes au noyau** par `callPlatformFunction` :
`DWA121PassiveScanStart` (génération) et `DWA121PassiveScanCopy` (copie bornée).
Ce ne sont pas des structures Apple supposées ni une nouvelle API utilisateur.
Le compagnon 0.3.1 traduit uniquement les commandes historiques caractérisées.
Une ancienne version du cœur refuse ces appels ; aucun cache factice de secours.

Le démarrage ne fait aucune opération USB dans le callback natif : timer sur le
workloop du propriétaire, `IOLockTryLock` côté RPC, progression asynchrone.
Une recherche concurrente rejoint la génération en cours ; une recherche terminée
peut être réutilisée pendant deux secondes pour coalescer les clients natifs.
Sinon une nouvelle génération vide l'ancien résultat et reçoit de nouvelles balises.
Un contrôle WPA2 existant fait refuser la commande 8 (Busy). Pendant le balayage,
claimControl est refusé ; le client historique possède déjà une attente bornée.
Le rejet vaut pendant connexion, association établie et reconnexion sous propriétaire.

Le délai observé de `WirelessScan` est d'environ 3,3 secondes (120 consultations).
Le séjour initial de 550 ms/canal dépassait ce délai. Le séjour retenu est 120 ms,
un peu plus que l'intervalle courant de 100 TU ; ce n'est pas une garantie de
réception de chaque AP, notamment avec balises lentes ou pertes radio.
`rtl8188cu_rx_scan_gate` lit et vérifie MAC/DMA, queues TX, sécurité et filtres
avant suspension/reprise. Seul le MAC/DMA est fermé pour programmer RF : aucun
réglage BB, de sécurité ou de filtre n'est inutilement réinitialisé à chaque saut.
Le RX USB précédent est drainé ; son moteur est libéré et un nouveau moteur
reçoit sur le canal suivant. La copie de table est sérialisée par le gate RX.
Le dernier saut de retour est omis si le dernier canal est déjà le canal initial.
Mesure VM : balayage complet reçu par Apple80211 en 2,518 secondes.

Une erreur USB RX, un drainage incomplet ou un accès registre défaillant arrête
la radio, invalide la recherche et nécessite un rechargement explicite. Elle ne
se transforme pas en résultat vide « réussi ». La commande 9 termine néanmoins
par 0x20000109 après invalidation : le framework incrémente l'index sur une erreur
arbitraire, ce qui peut produire une boucle sans fin ; la sentinelle évite ce piège.
Le diagnostic propriétaire conserve l'échec. Le retrait/finalize annule le timer
avant de libérer RX/USB ; le compagnon libère ses providers dans stop, comme P0.2.1.

## Données et limites de présentation

`rtl_bss` conserve les octets exacts du SSID (0..32), BSSID, canal annoncé et canal
de réception distincts, intervalle, capacités, horodatage monotone 32 bits en ms,
IE RSN et WPA bornés, statut de validation de chaque IE. Les champs PHY/RSSI ne
sont pas décodés. `P031RSSIAvailable=false` ; les anciens mots bruit/signal natifs
restent à zéro, sans prétendre à un niveau en dBm ou à une mesure de qualité.
Le canal annoncé reste zéro si absent : le canal d'écoute n'est pas substitué.

La table consolidée est bornée à 16 BSSID, dédupliquée par BSSID et remplacée à
chaque génération. Plusieurs BSSID du même SSID sont conservés. Les entrées âgées
de 15 secondes sont éliminées, avec soustraction modulo 32 bits. Chaque client
obtient une copie immuable : ses indices restent stables jusqu'à expiration de
l'observation la plus ancienne, puis la liste entière devient vide. Aucun pointeur
vers un buffer USB ou une propriété mutable n'est conservé.

Sécurité diagnostique : `open`, `WPA`, `RSN`, `WPA+RSN`,
`privacy-unspecified`, `unknown-invalid-IE`, `unknown-inconsistent`.
Les IE vendor WPA 00:50:f2:01 sont distingués des RSN 48 ; longueurs, version,
listes et doublons sont vérifiés. Privacy sans IE ne prouve pas WEP et n'est
jamais présenté comme WPA2. RSN seul ne prouve pas PSK/CCMP : le booléen distinct
WPA2PSKCCMPAdvertised garde sa validation existante. Référence syntaxique WPA :
`vendor/wpa_supplicant-2.12/src/common/wpa_common.c`, `wpa_parse_wpa_ie_wpa`.
Le record natif de 50 octets transporte les capacités, pas les IE : le menu peut
indiquer la confidentialité, mais aucune classification WPA2 détaillée n'est inventée.

Les SSID cachés/vides, avec NUL incorporé, ou longs de 32 octets sont conservés en
interne et exclus de la traduction native, car Panther utilise des chaînes C sans
borne dans ses consommateurs. Aucune troncature ni nom de substitution. Les SSID
non ASCII sans NUL et de moins de 32 octets sont transmis tels quels ; leur rendu
suit les conversions UTF-8 puis MacRoman du framework original.

Le filtre optionnel de la commande 8 est appliqué aux SSID reçus passivement ;
aucune Probe Request n'est émise. Le scan actif, les Probe Response, les canaux
12/13, le scan pendant WPA2 et le RSSI restent non implémentés. La sélection d'un
SSID, les commandes de connexion et le contrôle natif d'alimentation restent hors
périmètre ; 12 et autres commandes non caractérisées restent Unsupported.
Les réponses 13/18 et le marqueur SCPreferences non vide de P0.2.1 sont préservés.

## Suite P0.3.2

L'affichage et l'énumération native sont faisables et vérifiés ; la sélection ne
l'est pas encore. Il faudra caractériser les commandes d'association propres à
Panther, leur modèle d'identifiants/secrets et le passage de contrôle au client
WPA2 existant, sans second propriétaire USB. Une entrée de menu visible n'établit
ni l'authentification, ni le stockage sûr des identifiants, ni la compatibilité WPA2
native de Panther. Cette partie exige son propre audit et ses tests.

## Correction de temporisation découverte en non-régression

Le client historique installé a cessé ses heartbeats après 27 des 30 pings,
malgré RX/TX USB sans erreur. Le noyau a arrêté proprement la session après
5 secondes (`AssociationDeadlineExpired`). L'échantillonnage du client bloqué
montre `run_wpa_probe → eloop_run → select`.

Cause vérifiée indépendamment du scan : hostap `os_get_reltime` calcule
`mach_absolute_time() * numer / denom`. Panther/QEMU retourne 1000000000/25000000 ;
le produit intermédiaire 64 bits déborde tous les 737,87 s. À 1480,768 s de temps
noyau (dernier RX de l'essai), la deuxième frontière 1475,74 s venait d'être passée,
plus le délai de heartbeat. Un relevé ultérieur donne 525 s avec l'ancienne formule
contre 49224 s avec la conversion exacte. `now_ms` de notre client utilisait déjà
la bonne conversion ; le défaut était resté dans la boucle d'événements hostap.

Le correctif local `vendor/patches/hostap-2.12-panther-clock.patch` utilise quotient
et reste avant multiplication, sans perte de précision. Il est recensé avec son
SHA-256 dans `docs/wpa-upstream.json`. Le test lie **le véritable os_unix.c** avec
seulement les deux fonctions Mach remplacées par des vecteurs : l'ancienne source
échoue à 738 s, la source corrigée passe les frontières et des durées de 1 et
30 jours, sur hôte et PPC. Ce sont des tests d'horloge synthétiques, pas 30 jours
d'endurance matérielle. Aucun changement des clés, du protocole WPA2 ou du débit.
La validation réseau finale utilise le client corrigé dans le répertoire de test ;
le client v0.15.0 installé n'est pas écrasé lors de la restauration.
