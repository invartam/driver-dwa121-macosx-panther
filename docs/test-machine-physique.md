> **Paquet réseau v0.15.0 :** suivre [l’installation et les essais de la version utilisable](installation-driver.txt). Cette page conserve les essais de diagnostic historiques, à exécuter uniquement après arrêt et désinstallation du service réseau.

# Essayer la DWA-121 A1 sur un PowerBook sous Panther

Kit v0.14.0 — Mac OS X **10.3.9 PowerPC** uniquement. Aucun Xcode, Python,
accès Internet ou accès SSH n'est nécessaire sur le PowerBook.

Cette version est un **diagnostic expérimental**, pas encore une connexion Wi-Fi
utilisable. Elle permet de vérifier l'USB, le firmware, la réception des balises,
l'authentification ouverte 802.11, l'association et la réception d'un premier
message WPA2 compatible (M1). M1 ne possède pas de MIC : sa réception ne prouve
ni la connaissance du mot de passe ni l'authenticité du point d'accès.
Il n'y a pas encore d'interface réseau, d'adresse IP configurée ou d'accès Internet.
La v0.13 ajoute une sonde WPA2 réelle : vérification de M3, émission de M4,
installation/relecture et effacement des clés. La v0.14 ajoute un essai borné
de données CCMP avec découverte DHCP, sans demander de bail ni configurer Panther.

## 1. Préparer le PowerBook et le kit

- Sauvegarder le travail en cours et fermer les applications importantes :
  l'essai charge un composant dans le noyau d'une machine physique.
- Brancher l'alimentation, garder le capot ouvert et éviter la veille pendant
  les essais. Le script ne change pas les réglages de veille.
- Vérifier **10.3.9** dans « À propos de ce Mac ». Les autres versions et les
  machines Intel/Apple Silicon sont refusées par le script.
- Utiliser la **D-Link DWA-121 révision A1**, USB `2001:3308`, directement sur le
  port USB du PowerBook. Retirer les autres clés Wi-Fi identiques.
- Aucun autre pilote ne doit prendre en charge cette clé. Si un ancien pilote
  Realtek/D-Link est installé, utiliser sa procédure de désinstallation avant
  l'essai ; ne pas supprimer au hasard des extensions système.
- Quitter les applications Mac OS 9 puis arrêter **Classic** dans les Préférences
  Système. Fermer aussi les outils qui ouvrent la clé, comme USB Prober.
  Un client utilisateur peut empêcher l'ouverture exclusive de l'interface USB.
- Rapprocher la machine de NAEBA. L'essai vise uniquement le **2,4 GHz**, canal
  **11**, avec WPA2-Personal/PSK et AES/CCMP, sans obligation PMF. Le 5 GHz
  n'est pas utilisé. RX et association ne demandent pas le mot de passe Wi-Fi ;
  la sonde WPA2 le demande séparément, sans écho.

Copier `DWA121Diagnostic-v0.14.0-panther-ppc.tgz` et son fichier `.sha256` sur
le PowerBook, par Ethernet, support amovible ou un autre moyen disponible.
Le fichier d'empreinte permet de contrôler le transfert sur une machine munie
de SHA-256 ; Panther n'a pas nécessairement cet outil d'origine.

Dans Terminal, en supposant que l'archive se trouve sur le Bureau :

```sh
mkdir -p "$HOME/Desktop/DWA121-test-v0.14"
cd "$HOME/Desktop/DWA121-test-v0.14"
tar -xzf "$HOME/Desktop/DWA121Diagnostic-v0.14.0-panther-ppc.tgz"
```

Le dossier doit contenir `DWA121Diagnostic.kext`, `dwa121-info`, `dwa121-check`,
`dwa121-rx`, `dwa121-associate`, `dwa121-wpa2` et `scripts/test-physical.sh`. Garder ensemble
les outils et le module de cette version. Le module est copié dans un dossier
temporaire pour chaque essai ; **rien n'est installé dans
`/System/Library/Extensions` ni chargé au démarrage**.

## 2. Premier essai : réception en USB 1.1

Brancher la clé, puis, depuis le dossier extrait :

```sh
sudo sh scripts/test-physical.sh rx 11 --usb11
```

`sudo` demande le mot de passe du **compte administrateur du PowerBook**,
pas celui du Wi-Fi. Sa saisie n'affiche aucun caractère.

Le script collecte la description USB, charge le module, attend son démarrage,
écoute dix secondes, arrête la radio et décharge le module. Une erreur entraîne
également une tentative d'arrêt et de déchargement. Il affiche le chemin d'un
dossier `dwa121-results-DATE-HEURE-PID` et un résultat `PASS` ou `FAIL`.

Pour valider cet essai, il faut notamment :

- `running-check.log` : `PASS`, **USB=12 Mb/s bulk=64**. C'est la vérification
  du fonctionnement effectif en Full Speed, pas seulement de la présence d'un
  port ou hub déclaré USB 1.1.
- `result-check.log` : `PASS: rx`, avec au moins une balise vérifiée et aucune
  erreur USB, surveillance expirée, soumission refusée ou donnée USB malformée.
- `cleanup-check.log` : `PASS: stopped`, requêtes RX terminées, radio et
  firmware arrêtés.
- `result.txt` : `exit=0`, et absence de `local.dwa121.Diagnostic` dans
  `kextstat-after.txt`.

Une réception sans balise est un **échec de validation de réception**, même si
l'arrêt fonctionne. Les erreurs CRC/FCS, le nombre de balises et les réseaux
observés sont conservés dans `stopped.plist` pour l'analyse.

Sur une autre machine PowerPC dotée d'USB 2.0, omettre `--usb11` : le script
accepte alors 12/64 ou 480/512. Une réussite en 480/512 ne valide pas l'USB 1.1.
Ne pas retirer cette option pour contourner un échec sur le PowerBook cible.

## 3. Deuxième essai : association et premier message WPA2

Faire cet essai après une réception réussie, sans débrancher la clé :

```sh
sudo sh scripts/test-physical.sh associate 11 'NAEBA' --usb11
```

Le script recharge une copie fraîche et autorise explicitement les émissions
de gestion pour cet essai. Il choisit une balise récente compatible avec le SSID,
envoie l'authentification ouverte puis la demande d'association, et inspecte les
trames EAPOL reçues. La session d'émission possède une limite de dix secondes
dans le noyau, indépendante du processus utilisateur.

Résultat attendu dans `association.log` :

```text
Association probe: associated=1 ... AID=... ... stopped=1
EAPOL inspection: M1=1 ... required=1 (M1 is unauthenticated)
```

`M1` peut être supérieur à 1 si le point d'accès retransmet. L'AID doit être
compris entre 1 et 2007. Le script exige au moins un M1 compatible, le succès
des émissions USB et un arrêt vérifié avant d'afficher `PASS`.
Les messages M2/M4 ne sont pas émis par cette sonde ; aucun secret n'est saisi.
**Une association réussie n'est pas une authentification WPA2 réussie.**

Pour RETRO, si sa configuration actuelle annonce bien WPA2-PSK et CCMP :

```sh
sudo sh scripts/test-physical.sh associate 11 'RETRO' --usb11
```

La sonde refuse les annonces incompatibles. Elle ne se rabat pas sur un réseau
ouvert ou TKIP. Si plusieurs points d'accès annoncent le même SSID sur le canal,
elle utilise le premier compatible reçu ; le BSSID sélectionné est conservé
dans le résultat IOKit.

## 4. Troisième essai : échange WPA2 avec le mot de passe

Après un essai d'association réussi :

```sh
sudo sh scripts/test-physical.sh wpa 11 'NAEBA' --usb11
```

Saisir le mot de passe Wi-Fi à l'invite `WPA2 passphrase`. La saisie ne
s'affiche pas. Ne pas ajouter le mot de passe à la commande et ne pas le placer
dans un fichier. Si `sudo` demande d'abord un mot de passe, cette première
invite concerne le compte administrateur du Mac.

La préparation initiale efface la CAM avant l'authentification ; elle peut
prendre plusieurs secondes. La sonde exige ensuite une nouvelle balise fraîche,
puis lance l'association et l'échange WPA2. Dans `wpa.log`, attendre :

```text
EAPOL USB complete: message=M2 ...
EAPOL USB complete: message=M4 ...
CCMP key installed and read back: pairwise slot=4
CCMP key installed and read back: group slot=...
WPA2 result: ... handshake_verified=1 data_authorized=0
```

Le script exige aussi `WPA2KeysErased=true`, radio arrêtée et module déchargé.
`result-check.log` doit contenir `PASS: wpa`. Le port de données reste fermé :
**ce test ne permet pas encore de naviguer sur Internet**. La fin USB de M4 ne
prouve pas à elle seule son acquittement radio. Le [diagnostic WPA2](wpa-hardware.md)
détaille les preuves et les limites. Si M1 arrive mais pas M3, conserver le
journal ; ne pas conclure automatiquement à un mauvais mot de passe.

## 5. Si aucun réseau n'est reçu

Si `dwa121-info` ne trouve aucun service malgré un `kextload` réussi, consulter
`kernel-driver.log` et `dmesg`. Le message `USB interface busy`, accompagné de
`someone already has it open`, indique un accès concurrent. Quitter les
applications Mac OS 9, arrêter Classic, puis attendre le déchargement du module
avant de débrancher et rebrancher la clé. Relancer ensuite le test RX. Le script
du dépôt conserve aussi `startup-kernel.log` et `usb-interface-clients.txt` en cas
d'expiration au démarrage ; ces deux fichiers ne sont pas produits par le script
original de l'archive v0.14.0.

Commencer par rapprocher le PowerBook du point d'accès, vérifier le canal et
répéter la réception. Chaque commande ci-dessous constitue un essai distinct,
avec arrêt et rechargement entre les canaux :

```sh
sudo sh scripts/test-physical.sh rx 1 --usb11
sudo sh scripts/test-physical.sh rx 6 --usb11
sudo sh scripts/test-physical.sh rx 11 --usb11
```

Le canal est obligatoire et doit être compris entre 1 et 11. Il n'y a pas de
balayage automatique ni de changement de canal durant une association.
Sur un canal silencieux, la surveillance USB peut arrêter l'écoute après
environ trois secondes ; conserver ce résultat même si aucune balise n'arrive.
Ne pas conclure à un problème WPA2 si la réception USB n'est pas encore validée.

## 6. Arrêt, récupération et désinstallation

En fonctionnement normal, le script arrête et décharge automatiquement le
module, y compris après une erreur et après Ctrl+C. Pour vérifier :

```sh
kextstat | grep local.dwa121
```

Aucune ligne ne doit apparaître. Si une ligne subsiste et que Terminal répond,
depuis le dossier du kit :

```sh
sudo ./dwa121-rx stop
```

Lire `temporary-kext.txt` dans le dossier de résultats du dernier essai et
utiliser **exactement le chemin qui y figure**, par exemple :

```sh
sudo kextunload /private/tmp/dwa121-test.XXXXXX/DWA121Diagnostic.kext
```

`XXXXXX` est un exemple à remplacer, pas un chemin à copier tel quel. Le script
conserve cette copie lorsque le module reste chargé. Ne pas débrancher la clé
pendant les premiers essais de trafic. Une fois le module déchargé, elle peut
être retirée. Le retrait en activité et la reprise après veille ne sont pas
encore validés sur le PowerBook.

Si la machine est bloquée ou affiche un kernel panic, redémarrer, retirer la
clé pendant le redémarrage et conserver le rapport éventuel dans
`/Library/Logs/panic.log`. Le module n'étant pas installé au démarrage, il ne
sera pas rechargé automatiquement. Ne pas répéter un essai qui provoque un panic
avant d'en analyser le rapport.

Après déchargement, supprimer le dossier extrait suffit à retirer le kit.
Conserver les résultats ; aucune configuration réseau système n'a été créée.

## 7. Retour de test utile

Depuis le dossier du kit, remplacer le nom de dossier par celui affiché :

```sh
tar -czf resultat-powerbook.tgz dwa121-results-DATE-HEURE-PID
```

Joindre cette archive avec le modèle exact du PowerBook, la quantité de RAM,
le port utilisé, la présence éventuelle d'un hub et le comportement observé.
Pour comparer deux essais, joindre leurs deux dossiers. Les journaux incluent
SSID, BSSID, MAC et une description USB de la machine ; ils ne contiennent ni
mot de passe Wi-Fi ni contenu brut des messages EAPOL.
Le mode `data` ajoute l’adresse offerte et celle du serveur DHCP, ainsi que
les compteurs de trames chiffrées ; aucun contenu brut de données n’est enregistré.

Les quatre modes (`rx`, `associate`, `wpa`, `data`) ont été validés le
26 octobre 2026 sur un **PowerBook3,1, G3 400 MHz, 192 Mo, Panther 10.3.9**,
avec la clé directement sur son port USB 1.1 : **12 Mb/s, bulk 64 octets**.
L'essai de données reçoit une offre DHCP authentifiée après un DHCPDISCOVER
chiffré ; les clés sont effacées et le module déchargé. Classic a dû être arrêté
et la clé reconnectée avant le premier essai réussi. Voir les
[résultats physiques](powerbook-usb11.md) et le
[rapport détaillé](../artifacts/validation-powerbook-v0.14.0.json).

La procédure fournie a été exécutée le 24 octobre 2026 dans Panther avec la clé
physique transmise par QEMU EHCI : 185 balises reçues, puis association NAEBA
(AID 35) et deux M1 reconnus. L'arrêt et le déchargement passent dans les deux
cas. Le refus volontaire de `--usb11` sur cette liaison à 480 Mb/s a également
été testé, avec arrêt et déchargement vérifiés.

## 8. Test logiciel du nouveau module CCMP, sans clé USB

Le cœur CCMP en développement est fourni séparément dans
`DWA121-WPA2-core-panther-ppc.tgz`. Ce paquet ne remplace pas le diagnostic
matériel v0.13.0. Le copier sur le PowerBook puis, dans un dossier neuf :

```sh
mkdir dwa121-ccmp
tar -xzf DWA121-WPA2-core-panther-ppc.tgz -C dwa121-ccmp
cd dwa121-ccmp
./build/station-test > test-ccmp.log 2>&1
./build/wpa2-test >> test-ccmp.log 2>&1
./build/data-test >> test-ccmp.log 2>&1
cat test-ccmp.log
```

Chaque exécutable doit terminer avec un code de sortie zéro et ses lignes
`PASS`. Aucune compilation, aucun `sudo`, aucun mot de passe Wi-Fi et aucun
chargement de kext ne sont nécessaires. Les exécutables utilisent des clés
de test synthétiques ; les tests WPA comportent quelques délais volontaires.
Conserver `test-ccmp.log` et signaler tout arrêt sur une assertion.

Ce test vérifie le logiciel sur le processeur réel, mais pas le transport
USB 1.1 ni les données chiffrées sur la box. Voir [le contrat et les limites
du module](ccmp-data.md).

## 9. Données chiffrées et découverte DHCP (kit v0.14)

Après un essai WPA2 réussi, lancer :

```sh
sudo sh scripts/test-physical.sh data 11 'NAEBA' --usb11
```

Saisir le mot de passe Wi-Fi au prompt masqué. La sonde vérifie d’abord WPA2,
puis émet au maximum trois DHCPDISCOVER chiffrés. Un succès exige une réponse
DHCPOFFER déchiffrée avec MIC valide, pour la MAC et la transaction de cet
essai. `dhcp_offer=1`, `PASS: data` et le déchargement réussi doivent apparaître
dans les journaux. Une offre ne constitue pas un bail actif : aucun
DHCPREQUEST n’est émis, aucune adresse n’est appliquée et Internet reste
indisponible via cette clé.

En cas d’échec, conserver les journaux de cet essai, même si WPA2 a réussi.
La section [diagnostic CCMP matériel](ccmp-hardware.md) décrit les contrôles
et les limites. L’option `--usb11` ne doit être retirée que pour un essai
explicitement fait sur une liaison USB 2.0.
