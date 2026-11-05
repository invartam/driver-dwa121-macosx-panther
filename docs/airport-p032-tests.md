# P0.3.2 — journal de validation

La sélection native avec saisie réelle entre chevrons est validée en VM Panther
avec la vraie DWA-121 : WPA2, DHCP, DNS, 30/30 pings et 256 Kio TCP dans
chaque sens. Le rendu du réseau courant dans le menu et la validation USB 1.1
restent incomplets. Les essais et limites sont détaillés ci-dessous.

## Préparation du 30 octobre 2026

- Branche `v1/airport-p032`, base `30518c9` ; aucune modification préexistante.
- VM Panther 10.3.9 7W98, référence installée 0.15.0 et superviseur fonctionnels
  au début de l'intervention.
- Sauvegarde originale conservée dans
  `/var/root/dwa121-p032-backup/preferences.plist`, répertoire root 0700.
- Superviseur historique arrêté ; chargement temporaire des binaires P0.3.1
  déjà compilés. Aucun remplacement des binaires ni du profil installés.
- Service AirPort temporaire ajouté et relais démarré (PID 2187).

## Incident avant toute modification du moteur

Après une longue période d'inactivité, le premier clic de scan à 14:01:48
a rencontré une erreur USB RX, puis `DWA121 scan: failed; stopping radio`.
La clé s'est réénumérée ; un nouveau fournisseur recevait des balises NAEBA
et RETRO sur le canal 11, mais les références du compagnon et du menu étaient
restées attachées à l'ancien fournisseur inactif. `WirelessAttach` retournait
`e00002d9`. Aucun clic d'association n'a pu être validé dans cet état.

L'arrêt du relais et de SystemUIServer n'a pas permis de décharger normalement
les extensions. Le service temporaire a été supprimé, les préférences originales
recopiées, puis `sync` exécuté avant `shutdown -r now`. L'arrêt invité est resté
bloqué. Une réinitialisation QMP sans retrait USB a ensuite bloqué au démarrage ;
QEMU a été quitté et relancé avec `scripts/boot-ehci.mjs`, même disque existant.
Aucun `device_del` n'a été utilisé. Restauration finale à vérifier après reprise.

La seule modification de code préparée à ce stade instrumente la commande 17
(longueurs, mode, longueur de clé, authentification, SSID en hexadécimal) sans
journaliser le contenu de clé. La commande reste explicitement non prise en charge.
Cette instrumentation n'était pas chargée lors de l'incident.

## Reprise et caractérisation native

Après redémarrage complet de QEMU le 30 octobre, la référence v0.15.0 retrouve
en2, DHCP 10.12.89.127 et 5/5 pings vers 10.12.89.1. Le test CFEqual confirme
que toutes les valeurs des préférences correspondent à la sauvegarde.
Le compagnon instrumenté compile avec GCC Panther sans erreur. Après son
chargement temporaire, le scan retrouve NAEBA (deux BSSID) et RETRO.
Le clic natif NAEBA ouvre une fenêtre de mot de passe avant toute commande 17.

Le 4 novembre, rebranchement physique confirmé par l'utilisateur ; présence
de la clé vérifiée avec `ioreg -p IOUSB` sur l'hôte et dans Panther. Le scan
retrouve NAEBA sur les canaux 1 et 11. RETRO n'est pas visible dans ce relevé.

- Saisie publique `p032test`, option trousseau décochée : à 10:24:36,
  SystemUIServer transmet 17, entrée 57/sortie 0, mode 1, longueur 13,
  auth 2, SSID 4e41454241. Rejet `unsupported` et erreur native visibles.
- Saisie publique `<p032test>`, option trousseau décochée : à 10:33:44,
  commande 26 entrée 10/sortie 0 puis 17 entrée 57/sortie 0, mode 1,
  longueur 5, auth 3, même SSID. Rejet conservé, aucun essai WPA2 réel.
- Parseur et sélecteur préparatoires : tests C89 ASan/UBSan sur hôte réussis ;
  compilation GCC 3.3 et exécution PPC Panther réussies. Vérifications des
  tailles, alignement BE, bourrage, exclusion des clés, sentinelle de
  déconnexion, identité SSID, choix de BSSID, expiration/débordement et RSN.

Preuves locales ignorées : `artifacts/airport-p032/native-password-trace.log`,
`native-bracket-trace.log`, `join-parser-ppc.log`, captures des fenêtres.
Les binaires Apple analysés restent exclusivement dans les artefacts ignorés.
À cette date, l'intégration du transport de secrets et les tests G2 n'étaient
pas encore réalisés ; voir la validation d'intégration ci-dessous.

## Retour à la référence le 4 novembre

Tous les tests de `scripts/test-host.sh` réussissent, y compris les nouveaux
tests de sélection. Le relais temporaire PID 454 est arrêté, le service
AirPort temporaire supprimé, les préférences restaurées et vérifiées CFEqual.
Les deux extensions expérimentales se déchargent normalement cette fois.
Le superviseur v0.15.0 installé est redémarré. Le choix sur la convention de
saisie `<mot-de-passe>` reste en attente ; aucun mode ignorant le mot de passe
n'est activé. Aucun push n'est effectué.

Après calibration : en2 actif, DHCP 10.12.89.127, handshake M2/M4 et clés
pairwise/group vérifiés dans le journal historique. Les cinq pings du contrôle
initial avaient emprunté en1 avant cette initialisation et ne sont **pas**
comptés comme validation Wi-Fi. Le contrôle suivant vérifie explicitement
la route en2 avant et après les pings (`restored-wifi-nov04.log`). Panther
ne prend pas en charge l'option moderne `ping -S`, essai écarté également.

## Intégration du 5 novembre 2026

Le choix utilisateur remplace le profil préconfiguré par une vraie saisie native :
`<mot-de-passe>` dans la fenêtre WEP, **Add to Keychain décoché**. Le relais
utilise uniquement cette saisie pour dériver la PMK du SSID demandé. Aucun
mot de passe, PMK, PTK ou GTK ne figure dans les traces conservées.

Environnement : Panther 10.3.9 PPC, vraie DWA-121 A1 en EHCI 480 Mb/s,
cœur temporaire 0.15.2, compagnon 0.3.2. Sauvegarde d'intégration :
`/var/root/dwa121-p032-integration-backup/preferences.plist` (répertoire 0700).
Les bundles expérimentaux se trouvent sous
`/Users/builder/dwa121/airport-p032/build`, sans remplacement des installés.

### Compilation et lancement reproductibles

Depuis la copie du dépôt dans Panther :

```sh
sh scripts/build-kernel-panther.sh
sh scripts/build-airport-panther.sh
build/airport-p02/airport-join-test
```

Le client réseau P0.3.2 est compilé avec la bibliothèque WPA de P0.3.1 contenant
le correctif d'horloge, inchangé dans ce jalon :

```sh
gcc -arch ppc -mcpu=750 -Wall -Werror -O2 -DDWA_WPA2_PROBE -DDWA_NETWORK_BUILD \
  src/config/config.c -I../airport-p031/vendor/wpa_supplicant-2.12/src \
  src/tools/dwa121-associate.c src/rtl8188cu/tx.c \
  ../airport-p031/build/wpa/libdwa-wpa2.a -framework IOKit \
  -framework CoreFoundation -o build/dwa121-connect
```

Arrêter le superviseur historique, sauvegarder les préférences, charger les
bundles temporaires possédés par root et non modifiables par groupe/autres.
Attendre la fin de calibration du cœur avant de charger le compagnon : le
premier essai a démarré son test trop tôt ; après calibration, le rechargement
du compagnon a permis son rattachement et le test a réussi.
Conserver dans le bundle expérimental les opt-ins réseau de la référence.
Ajouter le service avec `dwa121-airport-service add`, lancer un seul
`dwa121-airport-monitor --watch`, puis relancer SystemUIServer après chargement.

Copier `dwa121-connect` et `dwa121-airport-broker` dans un répertoire root 0700
(exemple `/var/root/dwa121-p032-runtime`), exécutables root 0700. Le relais
refuse un exécutable ou un parent modifiable par un autre utilisateur :

```sh
/var/root/dwa121-p032-runtime/dwa121-airport-broker \
  --connect /var/root/dwa121-p032-runtime/dwa121-connect
```

Il attend une sélection native ; ce lancement ne démarre pas de connexion.
Ouvrir le menu, cliquer NAEBA, saisir la convention ci-dessus et valider.
Les événements souris QMP doivent espacer pression et relâchement (150 ms dans
l'outil local) ; une paire instantanée peut ne pas être enregistrée par Panther.

### Résultats établis

- Tests hôte complets, C89 et ASan/UBSan : réussis (`host-final.log`).
- Compilation noyau et outils GCC 3.3 PPC : réussie ; test pur de sélection PPC réussi.
- Test matériel `airport-join-transport-test NAEBA` : privilège root du relais,
  exclusivité, bornes/expiration/consommation de saisie, SSID inconnu,
  exclusion scan/propriétaire historique, génération incorrecte, TAKE unique,
  annulation pendant la commande native en attente, puis nouveau scan : réussis.
- Test non-root : ouverture du relais interdite. Test scan P0.3.1 : réussi.
- Mauvaise saisie publique : association 802.11 réussie, cinq M1/M2, aucun M3,
  désauthentification AP raison 15 ; aucune autorisation de données. Nettoyage
  puis reprise de scan réussis (`native-wrong-password.log`).
- BSSID NAEBA canal 1 : trois tentatives d'authentification sans réponse,
  expiration et reprise (`native-target-first.log`). La sélection privilégie
  désormais le canal courant puis l'ordre BSSID ; aucune estimation RSSI.
- Vraie sélection depuis le menu : commandes 26 puis 17, SSID NAEBA,
  BSSID `58:1d:d8:78:bb:b8`, canal 11. Authentification et association 802.11,
  M2/M4, clés pairwise slot 4/group slot 1 vérifiées par relecture, autorisation
  WPA2/CCMP et DHCP sur en2, adresse 10.12.89.127.
- Première connexion réussie mais erreur visuelle après le délai Apple de
  deux secondes. Après attente bornée sur le thread d'association Apple :
  nouvelle sélection native, session 2, phase 4 (Connected), fenêtre fermée
  **sans erreur** (`native-wait-result.log`, `native-wait-desktop.png`).
- G2 répété après ce correctif : **30/30 pings**, DNS `NOERROR` via la source
  en2 et le serveur 10.12.89.1, **262144 octets TCP dans chaque sens**.
  Route en2 contrôlée avant/après ; l'interface d'administration en1 ne compte
  pas. SHA1 reçu `37ef77696fc255bf53b4cdd014b223676f2dc8bb`, SHA256 reçu par
  l'hôte `2312394bd99545d9de131c24efb781e765ac1aec243f2ed9347597a793a415e9`.
  Preuve : `native-final-network.log`, serveur lié à 10.12.89.108:18021.

### État visuel et limites

Après connexion, le menu reste « AirPort: On », sans erreur d'association.
L'ouverture du menu pendant le trafic ne coupe pas la session. Le scan actif
est toutefois refusé : la liste disparaît, et aucune coche de réseau connecté
n'est validée (`native-connected-menu.png`). La commande 18 expose le SSID,
BSSID et le résultat réel ; le statut de port reste inconnu (0), car les
requêtes supplémentaires de mesure déclenchées par le statut 4 ne sont pas
complètement caractérisées. Les notifications détaillées d'association et
l'affichage permanent du réseau courant sont à poursuivre en P0.4.
RETRO était visible dans les relevés précédents ; il n'est pas présent dans
le dernier relevé radio, qui ne contient que NAEBA. Aucun résultat n'est injecté.

Pas de support Open, WEP, TKIP, LEAP radio ni trousseau. Le parseur accepte
8 à 63 caractères ASCII imprimables entre chevrons ; les saisies ordinaires
transformées en clé WEP sont refusées. Les réseaux cachés/SSID binaires et le
changement de réseau pendant une session restent hors périmètre.
Les tests purs couvrent incompatibilité RSN/PMF et vieillissement des balises ;
ils ne remplacent pas des essais AP physiquement éteint ou timeout d'association.
Le reset fournisseur à chaud reste non validé ; le crash QEMU connu interdit
d'en faire un test de référence par `device_del`. L'incident historique de
réénumération décrit plus haut reste une limite à traiter.

### Procédure PowerBook USB 1.1 restant à exécuter

1. Sauvegarder préférences, profil protégé et version installée v0.15.0 ; garder
   un accès local permettant de revenir à la référence sans Wi-Fi.
2. Compiler PPC 750, sans AltiVec ; charger seulement les bundles temporaires,
   démarrer les deux relais et vérifier le lien USB **12 Mb/s** réel.
3. Répéter scan, saisie correcte/incorrecte, association, déconnexion et reprise ;
   vérifier l'absence de blocage du menu et les générations dans les traces.
4. Vérifier DHCP, DNS et route Wi-Fi, 30 pings, 256 Kio TCP dans chaque sens,
   erreurs USB, files TX/RX et clés effacées après arrêt du supplicant.
5. Arrêter les relais, retirer le service temporaire, restaurer les préférences,
   décharger les prototypes et revalider la référence. Aucun résultat EHCI
   ci-dessus ne constitue une validation USB 1.1.

### Arrêt du supplicant et déconnexion native

Après G2, SIGKILL du seul enfant expérimental (session 2) : phase Failed,
processus terminé status 9, TX USB 352/352 sans erreur, RX USB 1480/1480
sans erreur, nettoyage drainé puis recalibration et phase Disconnected.
Un scan pendant la recalibration a été refusé ; une fois « radio ready »
publié, le scan natif retrouve NAEBA (`supplicant-death-recovered.log`).

Nouvelle saisie depuis le menu, session 3, handshake et DHCP réussis.
`airport-join-transport-test NAEBA --active-disconnect` vérifie : résultat
connecté/SSID réel, seconde sélection et scan refusés sans perte de connexion,
commande 17 de déconnexion acceptée, résultat 4 et identité SSID/BSSID effacée.
Le client sort après fermeture du port, clés/radio libérées ; le relais
réinitialise le matériel et le scan retrouve NAEBA (`native-disconnect.log`).
L'arrêt volontaire est encore journalisé « session failed » par le client
historique ; il ne doit pas être interprété comme un échec de WPA2 initial.

### Restauration finale du 5 novembre

Relais et supplicant expérimentaux arrêtés, service temporaire supprimé,
compagnon puis cœur déchargés normalement. Le test CFEqual confirme toutes
les valeurs de préférences identiques à la sauvegarde d'intégration, sans
réécriture nécessaire. `airport-service-test absent` réussit.

Le superviseur installé est relancé ; seul `local.dwa121.Diagnostic (0.15.0)`
est chargé, avec le client installé `--config`. DHCP retrouve 10.12.89.127,
route **en2** avant/après, **5/5 pings**, DNS `NOERROR`. Aucun relais ni client
expérimental orphelin. Le serveur TCP temporaire sur l'hôte est arrêté.
Preuve locale : `restore-final.log`. Les binaires de test restent dans le
répertoire de construction ; aucun démarrage permanent n'est ajouté.

Le correctif d'horloge P0.3.1 reste dans la branche et le client de test ;
il n'est pas appliqué rétroactivement au client v0.15.0 installé.
Aucun push effectué pour P0.3.2.
