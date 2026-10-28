# P0.2 — compilation, essais et retour arrière

Statut : prototype partiel, pas une release V1 utilisable via le menu.
Référence v0.15.0 intacte ; ne pas installer ce compagnon au démarrage.

## Compilation native

Sur Panther 10.3.9 PowerPC avec Xcode 1.5, depuis la racine des sources :

```sh
sh scripts/build-airport-panther.sh
```

Produit `build/airport-p02/DWA121AirPortP02.kext` et `dwa121-airport`.
Aucun binaire Apple n'est inclus. L'outil utilise le framework privé installé
sur la machine ; ses signatures sont documentées dans le rapport d'architecture.
La compilation est indépendante de `scripts/build-vm.sh` et ne remplace pas
le kext réseau, les outils WPA2 ou les archives de release.

Transfert vers la VM existante depuis l'hôte :

```sh
COPYFILE_DISABLE=1 tar --format ustar -czf /tmp/airport-p02-source.tgz \
  src/airport src/tools/dwa121-airport.c scripts/build-airport-panther.sh
scripts/vm-ssh.sh 'mkdir -p /Users/builder/dwa121/airport-p02; tar -xzf - -C /Users/builder/dwa121/airport-p02; cd /Users/builder/dwa121/airport-p02; sh scripts/build-airport-panther.sh' < /tmp/airport-p02-source.tgz
```

## Chargement temporaire

Démarrer d'abord le pilote v0.15.0 et attendre la publication du fournisseur
DWA121Diagnostic : son initialisation USB est asynchrone. La sonde peut afficher
WirelessIsAvailable=0 si elle est exécutée trop tôt. Ne pas charger un second
pilote USB ni arrêter le port Ethernet utilisé pour l'administration de la VM.

Depuis le répertoire de compilation dans Panther :

```sh
sudo chown -R root:wheel build/airport-p02/DWA121AirPortP02.kext
sudo chmod -R go-w build/airport-p02/DWA121AirPortP02.kext
sudo kextload build/airport-p02/DWA121AirPortP02.kext
build/airport-p02/dwa121-airport
ioreg -c AirPortDWA121 -w 120
kextstat | egrep "local.dwa121|local.airportp02"
dmesg | tail -40
```

La sonde fonctionne sans root : elle ne lit que l'état radio public. Sur une
machine avec une carte AirPort Apple présente, la découverte native prend le
premier service commençant par AirPort : ne pas attribuer automatiquement ses
résultats au DWA-121. Vérifier la filiation et les traces de processus.
Les tests rejettent commande inconnue, taille d'entrée incorrecte, sortie
inattendue et index de méthode inconnu. La sonde compare le RPC brut et les
résultats du framework avec RadioRunning ; un simple retour zéro du framework
ne suffit pas. Éviter une transition radio pendant cette comparaison.

## Applications et diagnostics natifs

```sh
open '/System/Library/CoreServices/Menu Extras/AirPort.menu'
open '/Applications/Internet Connect.app'
open /System/Library/PreferencePanes/Network.prefPane
sh scripts/inspect-airport-panther.sh > airport-p02-inventory.txt
```

Après rechargement du compagnon, le menu peut conserver une ancienne connexion.
Dans la VM de développement, `killall SystemUIServer` force sa relance par le
système. Ouvrir ensuite le menu et conserver les lignes `caller=SystemUIServer`.
Ne pas confondre ces lignes avec `caller=dwa121-airport`.
`ioreg -r` n'existe pas sous Panther ; utiliser les commandes ci-dessus.
`screencapture` produit par défaut un PDF, même avec une extension .png ;
convertir avec `sips -s format png capture.pdf --out capture.png` si nécessaire.

L'essai Dynamic Store détaillé dans le rapport était volatil. Il ne constitue
pas une procédure d'installation et n'a pas permis d'obtenir un service natif
complet. Ne pas présenter un renommage Ethernet en AirPort comme une réussite.

## Non-régression et résultats observés

Le 28 octobre 2026, vraie DWA-121 transmise à QEMU en EHCI 480 Mb/s :

- Compilation finale GCC 3.3 PPC, `-Wall -Werror` : réussie.
- Chargement/déchargement du compagnon : réussis, kext v0.15.0 conservé.
- Témoin sans compagnon : WirelessIsAvailable=0, Attach=0x88001003.
- Radio arrêtée : état natif 0, RPC brut 0, contrôles de rejet réussis.
- Radio active : état natif 1, RPC brut 3, contrôles de rejet réussis.
- Menu réel : SystemUIServer PID 1281, commande 18, in=1/out=56 tracée.
  Non implémentée ; ce n'est pas un échange fonctionnel complet du menu.
- Préférences Réseau : « Ethernet Adaptor (en2) », pas AirPort.
- Internet Connect : Summary/VPN, pas d'onglet AirPort.
- Reprise v0.15.0 avec compagnon : association, M2/M4, clés pairwise/group
  installées et relues, WPA2/CCMP actif, DHCP 10.12.89.127 sur en2.
- Premier ping via en2 : 3/4 réponses, une perte ; ne pas masquer ce résultat.
- Comparaison suivante : 10/10 réponses sans compagnon, puis 10/10 avec la
  version finale du chemin de lecture. Aucune conclusion d’endurance.
- Suite `sh scripts/test-host.sh` : réussie, dont files réseau, station, EAPOL,
  registres simulés et moteur AsyncTX USB64/512. Ce ne sont pas des tests USB1.1
  physiques ni un test d'endurance.

Preuves locales ignorées par Git : `artifacts/airport-p02/`, notamment
`transport-tests.log`, `live-read.log`, `sc-menu.log`, `network-regression.log`,
`final-build-baseline.log`, `final-validation.log`, `test-host.log`, captures UI.
Ne pas distribuer les désassemblages ou les composants Apple avec le prototype.

## Retour à v0.15.0

```sh
sudo kextunload build/airport-p02/DWA121AirPortP02.kext
kextstat | egrep "local.dwa121|local.airportp02"
```

Le compagnon n'a jamais remplacé le pilote installé. Le décharger suffit ;
retirer son dossier temporaire après les essais si souhaité. Le service
v0.15.0 peut rester actif. Si le menu conserve un client ancien, fermer les
applications AirPort et relancer SystemUIServer dans la VM.
Si les clés de l'essai manuel existent, les retirer avec scutil (root) :

```text
open
remove Setup:/Network/Service/DWA121-P02/AirPort
remove Setup:/Network/Service/DWA121-P02/Interface
quit
```

Les sources du pilote, packaging et release sont identiques au tag v0.15.0.
SHA-256 de `release/native/0.15.0.tgz` :
`ada8451afb1346e90d4c07197c70c9273f35347f38c45f761e81a321f7b71374`.

## À tester sur matériel réel

PowerBook : reconnaissance du compagnon, découverte avec/sans carte AirPort
interne, lecture radio, coexistence WPA2, retrait/rebranchement USB1.1,
fermeture des clients et déchargement, veille et endurance. Tous restent
**À tester sur matériel réel**, malgré la validation historique de v0.15.0.

Pour comparer son installation Panther à la VM, fournir la sortie de
`scripts/inspect-airport-panther.sh`, les lignes dmesg DWA121 AirPort et une
capture du menu. Aucun mot de passe, network.conf ni binaire Apple n'est
nécessaire à ce stade. La suite immédiate est de caractériser complètement
WirelessStatus/commande 18 et la découverte des services dans les préférences,
avant de prétendre valider D1/D2 et un contrôle complet depuis le menu.

## Incident de reprise et correction

Lors de la session prolongée, trois expirations EHCI ont été observées,
puis un drainage RX avec une erreur USB. La tentative de reprise a révélé
un conflit d'identifiant : `prepare.sh` refuse toute extension dont le nom
correspond à `local.dwa121.`. L'ancien identifiant du compagnon le déclenchait.
Le compagnon utilise désormais `local.airportp02.DWA121`, car il ne possède
pas le matériel USB. Le garde-fou du superviseur et ses fichiers restent
inchangés. Un arrêt/redémarrage explicite du service en présence du compagnon
corrigé a réussi (`reconnect-fix.log`). Les expirations EHCI ne sont pas
attribuées au compagnon sur cette seule observation ; l'endurance reste ouverte.
Le premier contrôle de restauration exécuté trop tôt, sans en2, a routé son
ping ailleurs : `restored-v015.log` ne valide donc PAS le Wi-Fi. Toujours
exiger `route -n get` indiquant en2 avant de compter un résultat réseau.

Résultat après correction de l'identifiant : la reprise charge bien le pilote
et recrée le compagnon, mais la connexion ne reste pas active lors du contrôle
suivant : RadioRunning=0, lien en2 inactif, 0/4 pings sur la route en2.
`reconnect-final.log` démontre donc la recréation et la lecture cohérente de
l'arrêt, **pas une reconnexion WPA2 durable réussie**. Il démontre aussi deux
commandes 18 de SystemUIServer PID 1630 sans les clés Dynamic Store temporaires.
Le compagnon a ensuite été retiré et le service original redémarré ; la
non-régression d'endurance/reconnexion demeure non validée. Les succès courts
WPA2/DHCP et 10/10 pings ne remplacent pas cette réserve.

État final vérifié (`final-state.log`) : seul local.dwa121.Diagnostic 0.15.0
reste chargé ; service running, WPA2/CCMP actif, DHCP acquis, en2 actif.
Les deux clés de test sont absentes. Les sources et l'archive v0.15.0 restent
intactes. Aucun résultat nouveau sur PowerBook physique n'est revendiqué.
