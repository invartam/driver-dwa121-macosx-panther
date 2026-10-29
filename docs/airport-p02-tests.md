# P0.2 — compilation, essais et retour arrière

Prototype 0.2.0, branche `v1/airport-p02`. Reconnaissance et contrôle natif en
lecture seule démontrés dans Panther ; menu encore incomplet (affiche Off).
Ce n'est pas une release V1 permettant de se connecter depuis AirPort.
Ne pas installer le compagnon ou son relais au démarrage.

## Compilation native

Panther 10.3.9 PowerPC, Xcode 1.5, GCC 3.3 :

```sh
sh scripts/build-airport-panther.sh
```

Résultat dans `build/airport-p02/` : DWA121AirPortP02.kext,
dwa121-airport, dwa121-airport-service, dwa121-airport-monitor et
airport-preferences-test. PPC 750, sans AltiVec ; compilation `-Wall -Werror`.
Les frameworks utilisés sont ceux de Panther, jamais redistribués.
La compilation ne remplace aucun composant du pilote v0.15.0.

Depuis l'hôte, avec les scripts de la VM existante :

```sh
COPYFILE_DISABLE=1 tar --format ustar -czf /tmp/airport-p02-source.tgz \
  src/airport src/tools/dwa121-airport.c src/tools/dwa121-airport-service.c \
  src/tools/dwa121-airport-monitor.c tests/airport_preferences_test.c \
  scripts/build-airport-panther.sh
scripts/vm-ssh.sh 'mkdir -p /Users/builder/dwa121/airport-p02; tar -xzf - -C /Users/builder/dwa121/airport-p02; cd /Users/builder/dwa121/airport-p02; sh scripts/build-airport-panther.sh' < /tmp/airport-p02-source.tgz
```

## Chargement et présentation temporaires

Démarrer le pilote v0.15.0 avec sa configuration existante. Attendre son
initialisation asynchrone et la publication du contrôleur réseau. Ne pas
modifier les secrets ni interrompre l'interface d'administration de la VM.
Les commandes suivantes sont exécutées dans Panther depuis la racine du projet.
Le protocole privé découvre le premier service AirPort : pour ce test, ne pas
avoir une carte Apple active simultanément. Le relais refuse cette coexistence.

```sh
sudo chown -R root:wheel build/airport-p02/DWA121AirPortP02.kext
sudo chmod -R go-w build/airport-p02/DWA121AirPortP02.kext
sudo kextload build/airport-p02/DWA121AirPortP02.kext
build/airport-p02/dwa121-airport
ioreg -c AirPortDWA121 -w 120
kextstat | egrep 'local.dwa121|local.airportp02'
```

Avant le premier `add`, conserver une copie locale protégée des préférences,
sans remplacer une sauvegarde existante. Elle peut contenir des secrets : ne
pas l'exporter. Exemple avec un répertoire nouvellement créé :

```sh
sudo mkdir -m 700 /var/root/dwa121-p02-backup
sudo cp /Library/Preferences/SystemConfiguration/preferences.plist /var/root/dwa121-p02-backup/preferences.plist
sudo build/airport-p02/dwa121-airport-service add
sudo build/airport-p02/dwa121-airport-monitor --watch
```

Le relais reste au premier plan ; utiliser un autre terminal pour la suite.
Il lit la radio toutes les secondes et possède une clé Dynamic Store temporaire.
Le service ajouté est uniquement une présentation AirPort, sans configuration
IP ni priorité sur les services existants. Une seconde création échoue sans
modification. Ne pas utiliser « Apply Now » pour modifier d'autres services
pendant le test de retour arrière.

```sh
open '/System/Library/CoreServices/Menu Extras/AirPort.menu'
open '/Applications/Internet Connect.app'
open /System/Library/PreferencePanes/Network.prefPane
dmesg | tail -100
sh scripts/inspect-airport-panther.sh > airport-p02-inventory.txt
```

Après rechargement, les applications peuvent conserver un ancien client.
Fermer puis rouvrir Internet Connect et les Préférences Réseau. Dans la VM,
`killall SystemUIServer` relance le menu. Ouvrir celui-ci puis capturer les
traces `caller=SystemUIServer`, commandes 18 et 13 et leurs réponses ; les
lignes `caller=dwa121-airport` seules ne prouvent pas l'origine UI native.
`ioreg -r` n'existe pas sous Panther. Pour une capture : `screencapture -x
/tmp/capture.pdf`, puis `sips -s format png /tmp/capture.pdf --out /tmp/capture.png`.

Diagnostic d'état via `sudo scutil` :

```text
open
show State:/Network/Interface/en2/AirPort
show Setup:/Network/Service/DWA121-P02/Interface
quit
```

Remplacer en2 par le nom BSD réellement découvert. Power Status suit
RadioRunning ; Card Mode=1, Link Status=0 (association indisponible dans P0.2).
Les Préférences Réseau peuvent donc afficher « not connected » alors que le
moteur WPA2 fonctionne. Le menu affiche encore Off : ne pas l'utiliser pour
conclure à l'état matériel. Les boutons de connexion/activation ne sont pas
implémentés ; les commandes d'écriture sont refusées.

## Contrôles et non-régression

`dwa121-airport` fonctionne sans root. Il utilise WirelessAttach/GetPower/
GetEnabled/GetInfo2/Detach du framework Apple original, puis compare les RPC
bruts et les propriétés réelles. Il vérifie MAC, mode, radio, bornes des
buffers et rejets : commande inconnue, index inconnu, mauvaises tailles.
Exécuter sans transition radio concurrente. Le framework peut masquer une
erreur de transport ; son seul code de retour ne suffit pas.

Pour tester un client survivant au retrait : lancer
`build/airport-p02/dwa121-airport --hold-until-detached`, puis arrêter le
service v0.15.0 depuis un autre terminal. Le test attend la révocation du
port et le conserve encore dix secondes. Pendant ce délai, `kextstat` doit
montrer l'absence du kext USB. Recharger le compagnon si Panther l'a déchargé
automatiquement, puis redémarrer le service et attendre WPA2/DHCP.

Pour les tests réseau, attendre **DHCP et la route**, pas seulement le lien :

```sh
ifconfig en2
ipconfig getifaddr en2
route -n get 10.12.89.1
ping -c 30 10.12.89.1
route -n get 10.12.89.1
```

Adapter l'adresse à la passerelle Wi-Fi. Exiger `interface: en2` avant et
après ; sinon le réseau d'administration QEMU peut répondre à la place de
la clé et le test est invalide. Comparer avec/sans compagnon. La suite locale
`sh scripts/test-host.sh` couvre les moteurs portables ; elle ne remplace
pas les essais USB physiques.

## Résultats réellement observés, 28–29 octobre 2026

VM 10.3.9 build 7W98, Darwin 7.9.0, vraie DWA-121 transmise à QEMU en EHCI
480 Mb/s. Cela ne constitue pas un essai USB1.1 sur PowerBook.

| Contrôle | Résultat |
| --- | --- |
| Compilation native finale 0.2.0 | Réussie, GCC 3.3 PPC, Wall/Werror |
| Chargement / retrait du compagnon | Réussis ; core v0.15.0 distinct |
| Framework original, radio arrêtée/active | 0 puis 1 cohérents, RPC brut 0 puis 3 ; état arrêté vérifié avant la dernière modification de Card Mode |
| Version finale active | GetInfo2 radio=1, mode=1, MAC réelle, statut indisponible ; transport et rejets PASS |
| Origine UI réelle | SystemUIServer PID 3722 : op18 in1/out56 success puis op13 RadioRunning=1 |
| Préférences Réseau | DWA-121 AirPort P0.2, radio activée, association native non disponible |
| Internet Connect | Onglet AirPort DWA-121 AirPort P0.2 ; « Status not available » |
| Menu AirPort | Reçoit les réponses, mais affiche encore Off : limitation ouverte |
| Client survivant au retrait | Port révoqué 0x10000003, core USB déchargé pendant les 10 secondes de maintien ; reprise réussie |
| WPA2 / DHCP après reprise finale | Interface en2 active, 10.12.89.127 ; moteur existant conservé |
| Trafic final via en2 | 30/30 réponses, 0 % perte ; route en2 vérifiée avant/après |
| Comparaison courte précédente | 10/10 sans compagnon puis 10/10 avec ; pas un essai d'endurance |
| Propriété du service et état | Doubles instances refusées, mutation non-root refusée |
| Préférences après remove | CFEqual de toutes les valeurs avec la sauvegarde initiale : PASS |
| Arrêt brutal du relais | Clé temporaire absente après SIGKILL ; absence revérifiée avec scutil ouvert |
| Retrait final | IOClass original restauré, seul local.dwa121.Diagnostic 0.15.0 chargé |
| Réseau v0.15.0 restauré | Après relance : DHCP, route en2, 5/5 réponses ; seul core 0.15.0 chargé |
| Tests hôte | Suite scripts/test-host.sh réussie |

Preuves locales ignorées dans `artifacts/airport-p02/` : final-v02-wifi.log,
final-v02-native.log, final-v02-menu.png, final-v02-internet-connect.png,
held-client-final.log, final-ownership.log, final-rollback.log,
final-restored-network.log et final-host-tests.log. Ne pas distribuer les
binaires Apple ou désassemblages locaux avec ces preuves.

Incidents conservés : un premier ping ancien a perdu 1/4 paquet ; des
expirations EHCI et un drainage RX en erreur sont survenus en session longue.
L'ancien identifiant local.dwa121.AirPortP02 déclenchait le garde-fou USB du
superviseur ; il est remplacé par local.airportp02.DWA121 sans modifier ce
garde-fou. Les références fournisseur conservées jusqu'à free bloquaient
également la reprise : corrigées par libération sous verrou à stop et test
avec ancien client. Ces corrections ne démontrent pas l'endurance.
Le ping de final-v02-network.log a démarré avant DHCP sur en1 : **exclu** des
résultats Wi-Fi ; seul final-v02-wifi.log valide les 30 paquets finaux.

## Retour à v0.15.0

Arrêter le relais par Ctrl-C, puis retirer le service avant le compagnon :

```sh
sudo build/airport-p02/dwa121-airport-service remove
sudo build/airport-p02/airport-preferences-test /var/root/dwa121-p02-backup/preferences.plist /Library/Preferences/SystemConfiguration/preferences.plist
sudo kextunload build/airport-p02/DWA121AirPortP02.kext
kextstat | egrep 'local.dwa121|local.airportp02'
ioreg -c DWA121NetworkInterface -w 120
```

Fermer les clients natifs si nécessaire avant déchargement. Vérifier l'absence
des deux clés scutil ci-dessus et IOClass=DWA121NetworkInterface. La comparaison
sémantique doit passer si aucune autre préférence n'a été modifiée depuis
la sauvegarde ; ne pas écraser aveuglément des changements utilisateur.

Le retrait de la présentation AirPort peut invalider la configuration réseau
en cours. Dans la VM, la route Wi-Fi a disparu au retour arrière ; relancer
le superviseur original et attendre DHCP avant de vérifier le trafic :

```sh
sudo /usr/local/libexec/dwa121/dwa121-service stop
sudo /usr/local/libexec/dwa121/dwa121-service start
```

Aucun remplacement de kext original n'a lieu. Aucun StartupItem P0.2 n'est
installé. L'archive v0.15.0 est inchangée, SHA-256 :
`ada8451afb1346e90d4c07197c70c9273f35347f38c45f761e81a321f7b71374`.

## À tester sur matériel réel

PowerBook G3 Lombard / USB1.1 : chargement, reconnaissance native, lecture
radio, coexistence WPA2, retrait/rebranchement, anciens clients, veille,
reconnexion et endurance restent **À tester sur matériel réel**. Vérifier
séparément la présence d'une carte AirPort interne ; le prototype ne gère
pas la sélection entre plusieurs radios.

À récupérer : sortie de `scripts/inspect-airport-panther.sh`, sortie de
`dwa121-airport`, traces dmesg DWA121 AirPort, captures des trois interfaces,
route/interface et résultats ping. Ne transmettre ni network.conf, ni
préférences sauvegardées, ni mot de passe. Aucun binaire Apple supplémentaire
n'est nécessaire à ce stade. P0.3 doit compléter état du menu et scan ; P0.4
reste conditionné par la caractérisation sécurité et l'arbitrage du supplicant.
