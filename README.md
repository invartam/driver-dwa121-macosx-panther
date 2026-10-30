# D-Link DWA-121 A1 pour Mac OS X Panther

Pilote Wi-Fi expérimental **0.15.0** pour Mac OS X 10.3.9 PowerPC, DWA-121 **révision A1** (USB `2001:3308`, RTL8188CU). Il crée une interface réseau Ethernet `enN` et prend en charge WPA2-Personal/PSK avec AES-CCMP, DHCP et le trafic IP.

## Télécharger et installer

Télécharger le fichier `DWA121-0.15.0-Panther-PPC.tgz` de la [release v0.15.0](https://github.com/invartam/driver-dwa121-macosx-panther/releases/tag/v0.15.0), vérifier son fichier `.sha256`, extraire l'archive sur le Mac cible et ouvrir `Install.command`. Le programme demande le SSID, le canal et la phrase de passe ; Xcode n'est pas requis sur la machine cible. Voir la [procédure complète](docs/installation-driver.txt) pour l'installation, le démarrage, le diagnostic et la désinstallation.

La clé dérivée est conservée dans `/Library/Application Support/DWA121/network.conf` sous root avec les permissions 0600. Ce fichier est un secret : ne pas le joindre aux rapports de diagnostic. Le paquet ne contient aucune configuration Wi-Fi personnelle.

## Prototype AirPort P0.3.1 (branche v1)

Cette branche ajoute un scan passif réel des canaux 1 à 11 et l'affichage des SSID
dans le menu AirPort natif de Panther. Prototype validé en VM avec la vraie clé ;
association native, RSSI et validation PowerBook USB 1.1 restent ouverts.
Le scan est refusé pendant une connexion WPA2. Un correctif de l'horloge Mach du
supplicant évite aussi un débordement sur Panther PPC. Ces modifications ne sont
pas incluses dans le paquet v0.15.0 ci-dessus.

Voir l'[architecture](docs/airport-p031-architecture.md) et les
[tests, preuve visuelle et restauration](docs/airport-p031-tests.md). Le compagnon
et le cœur expérimental se chargent séparément, sans remplacer la référence.

## Portée du MVP v0.15.0

- DWA-121 A1 uniquement, bandes 2,4 GHz et canaux 1 à 11 saisis manuellement.
- WPA2-PSK/AES-CCMP sans PMF obligatoire. Pas de WPA3, WPA-Enterprise, TKIP, SSID caché ou roaming.
- Émission CCK 1 Mbit/s ; le renouvellement de clé passe par une reconnexion. La veille et l'endurance prolongée restent à valider.
- Le pilote ne s'intègre pas au menu AirPort ; l'interface apparaît comme Ethernet.

Dans la VM Panther avec la vraie clé USB 2.0, les essais ont couvert association WPA2, bail DHCP, ping, HTTP, SSH, transferts TCP bidirectionnels, reconnexion et installation du paquet. L'utilisateur a aussi installé le paquet sur son PowerBook USB 1.1 et téléchargé des fichiers par FTP avec un débit jugé correct ; cet essai physique est rapporté sans journal de mesure. Détails dans [l'état de validation](docs/driver-v0.15.md).

## Sources et releases

Les sources du kext, des outils et des tests sont dans `src/`, `tests/`, `scripts/` et `packaging/`. Le port utilise le code hostap/wpa_supplicant 2.12 conservé dans `vendor/` avec ses licences ; les notices Realtek sont également incluses. `LICENSE` décrit les conditions du projet.

Le kext PowerPC est compilé dans la VM Panther avec `scripts/build-vm.sh`. L'[archive native figée](release/README.md) et son SHA-256 servent d'entrée au [workflow GitHub Actions](.github/workflows/release.yml), qui construit le paquet installable et le publie avec ses empreintes lors du push d'un tag `vX.Y.Z`. Exemple pour la version actuelle :

```sh
python3 scripts/package-driver.py --version 0.15.0
git tag v0.15.0
git push origin main v0.15.0
```

Le tag doit être créé après le commit de release. La machine GitHub ne recompile pas le pilote Panther : la provenance et les limites de l'archive native sont précisées dans `release/README.md`.
