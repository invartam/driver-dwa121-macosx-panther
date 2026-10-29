# P0.2.1 — validation Panther et réversibilité

## Environnement et portée

Essais du 29 octobre 2026 : Panther 10.3.9 7W98 / Darwin 7.9.0, VM PPC750,
GCC 3.3, Xcode 1.5, vraie DWA-121 2001:3308 transmise en EHCI 480 Mb/s.
Les essais ne valident pas le PowerBook G3 en USB1.1 ni l'endurance.
Référence P0.2 : `8e48fe5` ; branche dédiée `v1/airport-p021`.

Le changement fonctionnel est dans `dwa121-airport-service.c` : marqueur
propriétaire dans l'entité AirPort pour empêcher son omission par configd.
Les réponses 13/18 et le relais restent ceux de P0.2. Aucune modification de
sécurité, de TX/RX ou de WPA2. Commande 12 volontairement refusée.

## Compilation et sauvegarde

Depuis les sources complètes sur Panther :

```sh
sh scripts/build-airport-panther.sh
```

Le répertoire de sortie reste `build/airport-p02/`, pour conserver la structure
existante ; le bundle porte la version 0.2.1. Le script produit aussi
`airport-service-test`, nouveau test du Dynamic Store effectivement appliqué.

Transfert depuis l'hôte :

```sh
COPYFILE_DISABLE=1 tar --format ustar -czf /tmp/airport-p021-source.tgz \
  src/airport src/tools/dwa121-airport.c src/tools/dwa121-airport-service.c \
  src/tools/dwa121-airport-monitor.c tests/airport_preferences_test.c \
  tests/airport_service_test.c scripts/build-airport-panther.sh
scripts/vm-ssh.sh 'mkdir -p /Users/builder/dwa121/airport-p021; tar -xzf - -C /Users/builder/dwa121/airport-p021; cd /Users/builder/dwa121/airport-p021; sh scripts/build-airport-panther.sh' < /tmp/airport-p021-source.tgz
```

Avant toute modification, créer une sauvegarde protégée **hors de /tmp**,
avec un chemin neuf. Ne pas l'exporter : elle peut contenir des secrets.

```sh
sudo mkdir -m 700 /var/root/dwa121-p021-backup
sudo cp /Library/Preferences/SystemConfiguration/preferences.plist /var/root/dwa121-p021-backup/preferences.plist
```

Pour comparer exactement les préférences après retrait, ne pas modifier
d'autres services ni valider d'autres changements dans les préférences réseau
pendant ce cycle. Ne pas écraser une sauvegarde existante.

## Installation temporaire

Démarrer le superviseur original et attendre le contrôleur réseau, puis :

```sh
sudo chown -R root:wheel build/airport-p02/DWA121AirPortP02.kext
sudo chmod -R go-w build/airport-p02/DWA121AirPortP02.kext
sudo kextload build/airport-p02/DWA121AirPortP02.kext
sudo build/airport-p02/dwa121-airport-service add
build/airport-p02/airport-service-test present
sudo build/airport-p02/dwa121-airport-monitor --watch
```

Attendre la propagation configd avant le test de service. Le relais reste au
premier plan ; utiliser un autre terminal. Aucun StartupItem n'est installé.
Si le service P0.2 existe déjà, le retirer avec `remove` avant `add` ; l'outil
refuse de remplacer un service existant. Les services et priorités existants
ne sont pas réordonnés. La présentation n'ajoute aucune configuration IP.
L'application des préférences peut invalider un bail en cours : vérifier DHCP
et la route, puis relancer le superviseur original si nécessaire.

```sh
build/airport-p02/dwa121-airport
ioreg -c AirPortDWA121 -w 120
kextstat | egrep 'local.airportp02|local.dwa121'
open '/System/Library/CoreServices/Menu Extras/AirPort.menu'
dmesg | tail -100
```

Dans `sudo scutil`, en adaptant en2 au BSD Name réel :

```text
open
show Setup:/Network/Service/DWA121-P02/Interface
show Setup:/Network/Service/DWA121-P02/AirPort
show State:/Network/Interface/en2/AirPort
quit
```

Attendu : Hardware=AirPort ; entité AirPort non vide avec DWA121P02Owner ;
état Power Status cohérent avec RadioRunning. Aucun PowerEnabled artificiel.
Un état Link Status=0 n'impose plus Off : l'association native reste inconnue.

## Reproduire la cause sans changer la radio

Sur le prototype P0.2 original, l'entité AirPort vide n'apparaît pas dans le
Dynamic Store. Le nouveau `airport-service-test present` échoue malgré
Hardware=AirPort dans Interface. Dans une VM de test, publier temporairement
la clé permet l'expérience discriminante :

```text
open
d.init
d.add DWA121P02Owner local.airportp02.DWA121
set Setup:/Network/Service/DWA121-P02/AirPort
quit
```

Le menu passe On si la radio réelle est active. Retirer cette seule clé avec
`remove Setup:/Network/Service/DWA121-P02/AirPort` remet Off, sans changement
matériel. Ne pas appliquer cette expérience à un service autre que DWA121-P02.
La correction finale utilise SCPreferences, pas cette écriture manuelle.

## Transitions réellement exercées

- **On sans association** : superviseur arrêté, core installé chargé seul,
  sans processus de connexion. RadioRunning=Yes, interface inactive et sans
  IP ; menu On. Ne pas lancer un second supplicant.
- **WPA2 connecté** : lancement direct de l'outil original `dwa121-connect
  --config` avec sa configuration protégée, superviseur toujours arrêté.
  M2/M4, clés CCMP installées et relues, DHCP 10.12.89.127 sur en2 ; menu On.
- **Off avec périphérique présent** : SIGTERM adressé au PID observé de ce
  seul processus de connexion. Il exécute son nettoyage normal ; le core
  reste chargé. RadioRunning=No, RadioQuiesced=Yes, PersistentStopVerified=Yes,
  RXHardwareStopVerified=Yes ; menu Off sans relancer SystemUIServer.
- **Commande native 12** : clic Turn AirPort Off en état connecté ; trace
  SystemUIServer PID 4285 op12 in5/out0. Opération refusée, radio toujours 1.
  Le contrôle depuis le bouton n'est pas implémenté.
- **Relance SystemUIServer** : état On reconstruit. Relance du relais : clé
  temporaire supprimée à l'arrêt, recréée et état On reconstruit.
- **Client ancien** : `dwa121-airport --hold-until-detached` conserve un port
  pendant l'arrêt du superviseur. Core USB absent de kextstat avant fermeture
  du client ; RPC 0x10000003 et maintien de dix secondes, test réussi.

Ne pas assimiler l'arrêt coordonné du superviseur à un retrait USB physique.
Le contrat d'association native complète reste hors périmètre.

## DNS, routage et TCP

La VM garde en1 pour l'administration QEMU ; un ping via en1 ne valide pas
la clé. Exiger la route en2 avant et après les tests et une source Wi-Fi pour
DNS/TCP. Adresses de cette session : passerelle 10.12.89.1, clé 10.12.89.127,
Mac hôte Wi-Fi 10.12.89.108 ; les adapter à l'environnement.

```sh
ifconfig en2
ipconfig getifaddr en2
route -n get 10.12.89.108
dig -b 10.12.89.127 @10.12.89.1 example.com A
curl --interface 10.12.89.127 --max-time 60 -sS http://10.12.89.108:18021/p021 -o /tmp/p021-tcp.bin
md5 /tmp/p021-tcp.bin
curl --interface 10.12.89.127 --max-time 60 -sS --data-binary @/tmp/p021-tcp.bin http://10.12.89.108:18021/p021
ping -c 30 10.12.89.1
route -n get 10.12.89.108
```

Le serveur de test hôte est lié uniquement à son adresse Wi-Fi, port 18021.
Il fournit `bytes(range(256))*1024` en GET et compare exactement les octets
reçus en POST. Aucun fichier utilisateur n'est envoyé. Résultats P0.2.1 :

- DNS : réponse NOERROR de la passerelle, source liée à l'IP en2.
- TCP : **262144 octets dans chaque sens**, contenu comparé intégralement.
  SHA-256 `2312394bd99545d9de131c24efb781e765ac1aec243f2ed9347597a793a415e9`,
  MD5 du fichier reçu `d19215b1d714757e1fdb0060c52fd4c8`.
- Ping : **30/30**, 0 % de perte ; min/moy/max 17,425/32,780/48,373 ms.
- Bilan du supplicant après arrêt propre : TX=347, authenticated_RX=441,
  rejected_RX=1 ; USB 351/351, stopped=1. Le paquet rejeté reste signalé,
  pas assimilé à une perte IP. Aucun échec de transfert observé.
- Aucun changement ni gain de débit revendiqué ; ce test est court, pas
  une qualification de performances ou d'endurance.

Le DNS global de la VM reste celui d'en1 (10.0.2.3). Le test explicite `dig -b`
valide le chemin Wi-Fi sans modifier cette configuration d'administration.

## Incident de retrait et récupération

`python3 scripts/vm-usb.py detach` a fait tomber QEMU sur le Mac : SIGSEGV
libusb dans darwin_get_active_config_descriptor, via usb_host_close et
qmp_marshal_device_del. SSH et QMP ont disparu. Le retrait/rebranchement via
cette méthode est donc **non validé** ; aucun essai répété sur ce chemin.
Le résumé de pile est conservé localement. Aucun panic Panther n'est établi
par ce rapport hôte, mais le nettoyage invité pendant ce retrait ne peut pas
être conclu.

La VM a été redémarrée avec `node scripts/boot-ehci.mjs`. Sans clé disponible,
WirelessIsAvailable=0 / Attach=0x88001003 et le menu affiche « No AirPort card
installed ». Après rebranchement physique par l'utilisateur, QMP voit de
nouveau la DWA-121 en 480 Mb/s, le core publie le compagnon et le superviseur
original refait WPA2/DHCP. Les lectures natives sont à nouveau cohérentes.
Cela démontre une récupération, pas un cycle de retrait à chaud sans incident.

Le reboot a effacé la première sauvegarde dans `/tmp` : la comparaison globale
avec le fichier exact d'avant mission n'est plus possible. Le nettoyage retire
seulement le service marqué, son lien et son entrée d'ordre. Un nouveau cycle
ajout/retrait est contrôlé contre une sauvegarde protégée dans `/var/root`,
après ce nettoyage ; ne pas confondre cette vérification avec la sauvegarde
initiale perdue. Les procédures ci-dessus évitent ce défaut.

## Retour arrière

Arrêter le relais par Ctrl-C (ou SIGTERM au PID vérifié), puis :

```sh
sudo build/airport-p02/dwa121-airport-service remove
build/airport-p02/airport-service-test absent
sudo build/airport-p02/airport-preferences-test /var/root/dwa121-p021-backup/preferences.plist /Library/Preferences/SystemConfiguration/preferences.plist
sudo kextunload build/airport-p02/DWA121AirPortP02.kext
sudo /usr/local/libexec/dwa121/dwa121-service stop
sudo /usr/local/libexec/dwa121/dwa121-service start
```

Fermer les applications AirPort si elles retiennent un client. Vérifier
IOClass=DWA121NetworkInterface, absence du relais et des clés P0.2, seul
local.dwa121.Diagnostic 0.15.0 chargé. Attendre DHCP puis refaire DNS/TCP/ping
via en2. Ne pas écraser des préférences utilisateur pour forcer une comparaison.

## Bilan de validation

**Partiellement validé** : reconnaissance et cohérence On/Off démontrées,
commandes 13/18 et trafic WPA2 validés sur essais courts. Le retrait QMP a
échoué au niveau de l'émulateur ; la commande 12 n'est pas implémentée.
Le cycle protégé de préférences après récupération passe la comparaison
CFEqual de toutes les valeurs. Clés supprimées et IOClass original restauré.
La suite `sh scripts/test-host.sh` passe ; aucune source du moteur n'a changé.
Après retrait final, seul local.dwa121.Diagnostic 0.15.0 est chargé. DNS,
TCP 262144 octets dans chaque sens avec contenu vérifié et 30/30 pings passent
à nouveau via en2 (min/moy/max 20,677/31,158/39,407 ms). Aucun relais de test
ne reste actif. L'archive v0.15.0 conserve le SHA-256
`ada8451afb1346e90d4c07197c70c9273f35347f38c45f761e81a321f7b71374`.
Voir `final-restoration.log`, `final-state.log` et `host-tests.log`.

## Preuves locales et essais restants

Sous `artifacts/airport-p021/` (ignoré par Git) :

- `baseline-setup.log`, `discriminant-on.png`, `ab-remove.log/png` : cause A/B.
- `fixed-service.log` : échec ancien contrat puis succès corrigé.
- `unassociated.log`, `unassociated-menu.png` : On sans association.
- `network.log`, `tcp-server.log`, `direct-connection.log` : trafic WPA2 réel.
- `connected-native.log`, `relay-restart.png`, `radio-off.log/png` : lectures
  natives et transitions observées, PID SystemUIServer distinct de la sonde.
- `menu-write-rejected.log` : commande 12 issue du menu, refusée.
- `qemu-crash-summary.json`, `absent-menu.png`, `replug-ready.log` : incident,
  absence puis récupération avec intervention physique.
- `logical-removal.log`, `cleanup-first.log`, `clean-cycle.log` : durée de vie
  des clients et réversibilité du service.

Ne pas distribuer les extraits de binaires Apple, désassemblages ou sauvegardes
de préférences présents dans l'environnement d'étude.

PowerBook G3 / USB1.1 : **À tester sur matériel réel**. Sont également non
validés : veille/réveil, endurance, coexistence avec une carte Apple (refusée),
contrôle radio depuis le menu (non implémenté), retrait QMP à chaud sans crash.
À récupérer sur PowerBook : inventaire via `scripts/inspect-airport-panther.sh`,
lectures `dwa121-airport`, captures On/Off, traces DWA121, routes et essais
DNS/TCP. Aucun secret, network.conf ou plist de préférences à transmettre.
