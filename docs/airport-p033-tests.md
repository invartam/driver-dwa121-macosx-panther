# P0.3.3 — validation Panther

Base `685b6c6`, branche `v1/airport-p033`. VM Panther 10.3.9 PPC et vraie
DWA-121 A1 EHCI. Cœur 0.15.2 et client WPA2/horloge P0.3.2 inchangés ;
compagnon final 0.3.3. Référence installée v0.15.0 préservée.

## Préparation et reproduction

Sauvegarde originale, répertoire root 0700 :
`/var/root/dwa121-p033-backup/preferences.plist`.
Sources/compilations expérimentales sous `/Users/builder/dwa121/airport-p033`.
Le cœur inchangé est chargé depuis le répertoire de construction P0.3.2.

```sh
sh scripts/test-host.sh                 # hôte, ASan/UBSan
sh scripts/build-airport-panther.sh    # Panther GCC 3.3 PPC
build/airport-p02/airport-status-test
build/airport-p02/airport-status-probe
```

Arrêter le superviseur installé, charger le cœur expérimental après contrôle
des opt-ins de référence, attendre sa calibration, puis charger le compagnon
root non modifiable par groupe/autres. Ajouter le service temporaire avec
`dwa121-airport-service add`. Démarrer un seul relais `dwa121-airport-monitor
--watch` et le broker privilégié P0.3.2 avec le client protégé existant :

```sh
/var/root/dwa121-p032-runtime/dwa121-airport-broker \
  --connect /var/root/dwa121-p032-runtime/dwa121-connect
```

Cela ne déclenche pas de connexion. Ouvrir le menu Apple, sélectionner NAEBA,
saisir `<mot-de-passe>` et laisser Add to Keychain décoché. Aucun secret n'est
inclus dans les commandes, fichiers de test ou captures conservées.

La sonde de statut affiche seulement 13, la réponse 18 et WirelessGetInfo2,
puis teste les bornes et le caractère non mesuré des champs de qualité 3. Elle
fonctionne sans root. Pour SC : ouvrir une session scutil, puis
`show State:/Network/Interface/en2/AirPort`. Ne pas afficher les préférences
complètes ou le profil WPA2 protégé.

## Expériences discriminantes

| Essai | Résultat observé |
| --- | --- |
| C1, P0.3.2 non associé | radio 3, port 0, identité vide, résultat 0 |
| C2, WPA2 réel | SSID NAEBA/BSSID 58:1d:d8:78:bb:b8/canal 11 corrects, handshake et DHCP ; port 0, résultat 1 |
| C3, ouverture connecté | aucun SSID dans le menu, scan refusé |
| C5, SystemUIServer relancé connecté | même absence de SSID/coche |
| C4, déconnexion native | identité effacée, résultat 4, nouveau scan permis |
| A, statut associé et lecture 3 | coche NAEBA réelle, persistante à la réouverture, sans changement du scan ni du relais |
| A, déconnexion | coche disparue ; NAEBA et RETRO redécouverts par scan réel |

Preuves locales ignorées dans `artifacts/airport-p033` : `c1-status.log`,
`c2-status.log`, `c2-network.log`, `c2-store.log`, `c3-menu.png`, `c5-menu.png`,
`c4-disconnect.log`, `a-status.log`, `a-menu.png`, `a-menu-reopened.png`,
`a-disconnected-menu.png`. Binaires et désassemblages Apple non distribués.
La lecture HCF FD44 expérimentale de A n'a pas été appelée et a été retirée.

## Tests automatisés et contrôle local

Suite hôte complète réussie, y compris le nouveau test C89 ASan/UBSan : états
Idle/Requested/Prepared/Running jamais connectés, Connected avec identité
exacte, Failed/Disconnected sans identité, BE et canaris, tailles invalides,
génération nulle, SSID ambigu, BSSID multicast, canal invalide.
Compilation et exécution du même test réussies sur PPC (`build-final-version.log`).
La sonde matérielle contrôle aussi la lecture 3, ses tailles, ses paramètres,
l’absence de débordement, sans ajouter de commande de mutation.

Le test P0.3.2 `airport-join-transport-test NAEBA --active-disconnect` exécuté
sans sudo réussit : cela **confirme le défaut de contrôle local préexistant**,
pas une nouvelle permission de P0.3.3. Il vérifie aussi le refus du scan et de
la sélection concurrente sans perte de session, puis l'identité effacée après
la commande native de déconnexion. Voir la proposition de correction dans
l'architecture ; aucun changement silencieux de politique d'accès.

## Incident à la reprise après interruption

Après une interruption prolongée, le scan a rencontré une erreur USB RX et
s'est arrêté avant toute nouvelle association. La clé est encore présente
dans IOUSB hôte. L'essai réseau lancé sans association est **exclu** : ses
pings pouvaient emprunter en1. Le déchargement normal des deux extensions
réussit ; récupération par rechargement, sans `device_del` QMP. Cet incident
est distinct de la conversion d'état et analogue à celui déjà décrit en P0.3.2.

## Qualification restant distincte

Les essais VM ne qualifient pas le PowerBook USB 1.1. Sur le Lombard : conserver
un accès local et une sauvegarde, vérifier 12 Mb/s, reproduire sélection/coche,
réouverture, mauvaise saisie, déconnexion et reconnexion, puis DHCP/DNS/route,
30 pings et 256 Kio dans chaque sens. Contrôler les erreurs USB et restaurer
les préférences/v0.15.0 en fin d'essai.

Perte physique de l'AP, changement effectif vers un autre BSSID de même SSID,
roaming, veille et retrait USB physique restent à qualifier. Aucun AP n'est
éteint à distance, aucun retrait QMP n'est utilisé. Les tests de génération
P0.3.2 et les tests purs d'invalidation ne remplacent pas ces essais matériels.

## Validation finale après récupération USB

Le rechargement retrouve trois BSSID réels : NAEBA canal 1 et canal 11,
RETRO canal 11 (`usb-recovery.log`). Nouvelle sélection native NAEBA, session 1,
BSSID 58:1d:d8:78:bb:b8 canal 11 : M2/M4, clés pairwise/group relues,
CCMP actif, DHCP 10.12.89.127. `WirelessGetInfo2` réussit avec port 4,
SSID/BSSID exacts, résultat 1 ; Dynamic Store Link Status=4, Power Status=1.
La sonde matérielle de bornes réussit comme utilisateur non-root.

[Capture de la coche native](images/airport-p033-native-connected.png).
Le menu est fermé puis rouvert pendant le trafic ; aucune reconnexion ni
scan hors canal n'est déclenché. Le SSID courant provient exclusivement du
statut réel ; le menu Apple crée lui-même son entrée cochée.

G2 : **30/30 pings**, DNS NOERROR lié à la source en2, **262144 octets TCP
par direction**, route en2 vérifiée avant/après. SHA1 reçu
`37ef77696fc255bf53b4cdd014b223676f2dc8bb`, SHA256 reçu sur l'hôte
`2312394bd99545d9de131c24efb781e765ac1aec243f2ed9347597a793a415e9`.
Preuves locales : `recovered-connected-status.log`, `recovered-network.log`,
`tcp-server.log`, `recovered-connected-menu.png`, `final-reopened-menu.png`.

La [capture finale après déconnexion volontaire](images/airport-p033-native-disconnected.png)
montre NAEBA et RETRO sans coche ; il s’agit d’un scan réel, sans ancienne
identité maintenue artificiellement (`final-disconnected-menu.png`).


## Défaut transitoire détecté et corrigé

L'arrêt brutal de la première validation finale invalide bien le port,
l'identité et Link Status, mais le menu affiche « Internet Sharing: On »
(`supplicant-death-menu.png`). Cette capture n'est **pas** une validation
complète de la transition. La lecture de l'API échouait momentanément pendant
la calibration ; le polling Apple traite son erreur comme `is Client=false`.
Le correctif conserve une lecture non associée sans identité pour Busy et
rend la lecture de qualité indisponible indépendante de l'association.


Contre-épreuve après correctif : session 2 Connected, SIGKILL, phases 5 puis 6,
statut brut/natif port 0 et identité vide, Link Status=0. Menu normal avec
NAEBA sans coche, **aucun faux partage**, sans relance de SystemUIServer
(`transition-death.log`, `transition-death-menu.png`).

Mauvaise saisie publique ensuite, session 3 : association 802.11, M2 répétés,
pas de M3, désauthentification AP raison 15 ; aucune phase Connected pour
cette génération. Erreur native attendue, port 0, identité vide, résultat 4,
Link Status=0, puis scan récupéré (`wrong-key.log`, `wrong-key-error.png`,
`wrong-key-menu.png`). L'ancienne connexion n'est pas réutilisée.

Nouvelle connexion correcte après l'échec, sans relance de SystemUIServer :
coche native revenue. G2 répété sur le correctif de transition final :
**30/30 pings**, DNS NOERROR, route en2 avant/après, **262144 octets dans chaque
sens** avec les mêmes empreintes (`final-g2.log`). Capture finale :
`final-verified-menu.png`, reprise dans la documentation.


Déconnexion volontaire finale : refus de sélection/scan concurrents sans
perte de la session existante, commande native de déconnexion acceptée,
identité effacée, résultat 4, Link Status=0 et menu sans coche. Test complet
P0.3.2 de transport également réussi après arrêt du broker : privilèges,
exclusivité, expiration du secret/bail, SSID inconnu, contrôle concurrent,
génération incorrecte, remise unique, annulation pendant l'attente et rescan
(`final-disconnect-transport.log`). Pas de relance de SystemUIServer pendant
ces transitions de session.

Les files USB observées après G2 se drainent sans erreur : TX 353/353 et
RX 1645/1645 (`supplicant-death.log`). Le défaut après inactivité prolongée
reste séparé de cette mesure et n'est pas masqué par ce résultat. Aucune
mesure exhaustive de fuite mémoire ou d'endurance longue n'est revendiquée.

## Restauration finale

Relais et clients expérimentaux arrêtés, service temporaire retiré, compagnon
0.3.3 puis cœur 0.15.2 déchargés normalement. CFEqual confirme toutes les
valeurs des préférences identiques à la sauvegarde initiale ; aucune recopie
aveugle nécessaire. Le test du service appliqué absent réussit.

Le superviseur installé est relancé : seul `local.dwa121.Diagnostic (0.15.0)`
est chargé, avec le client installé `--config`. DHCP 10.12.89.127, route en2,
**5/5 pings**, DNS NOERROR et contrôle de route après trafic. Aucun processus
expérimental orphelin. Serveur TCP temporaire de l'hôte arrêté. Preuve locale :
`restore.log`. Pas d'installation permanente ni de modification de composant
Apple ; le correctif d'horloge reste celui de la branche, sans remplacement
du client v0.15.0 installé. Aucun push P0.3.3.
