# Interface Ethernet Panther — module compagnon expérimental

`DWA121Ethernet.kext` ajoute un contrôleur `IOEthernetController` au service
`DWA121Diagnostic` existant. Le module USB v0.14.0 et son ABI restent inchangés.
Le compagnon se charge séparément ; les essais WPA2/CCMP n'en ont pas besoin.

Cette première étape publie une véritable interface BSD `enN`, son adresse MAC
provenant de l'eFuse de la clé, et un MTU de 1500. **Le lien reste inactif et
l'activation est refusée** : le pont de paquets entre l'interface et le moteur
WPA2/CCMP utilisateur reste à implémenter. Un résultat positif de la sonde WPA2
bornée n'autorise pas cette interface. Elle ne fournit pas encore Internet.

## Contrat et cycle de vie

- Appariement uniquement sur `DWA121Diagnostic`, avec réception persistante
  active et MAC unicast valide. Aucun accès USB depuis le compagnon.
- Boucle de travail réseau distincte de celles du diagnostic et de l'USB.
  Les appels réseau ne prennent pas le verrou de cycle de vie du pilote USB.
- `EthernetDataPathReady=false`, `EthernetPortAuthorized=false`, lien valide
  mais inactif, vitesse annoncée zéro, seul média disponible `none`.
- `enable()` retourne toujours `kIOReturnNotReady` et compte les refus.
  `outputPacket()` libère le paquet et retourne `kIOReturnOutputDropped`.
  Aucun paquet ne peut être transmis en clair par ce chemin.
- Le retrait de l'interface est asynchrone : ne pas attendre BSD sous le
  verrou de la boucle de travail du fournisseur. IONetworkingFamily conserve
  ses références jusqu'à la fin du détachement.

Le compteur de refus ne constitue pas une interface de commande. Il sert à
vérifier qu'une demande de mise en service a réellement atteint le pilote.
`dwa121-netcheck` vérifie aussi l'unicité du contrôleur, sa filiation, la MAC,
le nom BSD, le MTU, les indicateurs réseau et le lien. `--deny-up` exerce
`SIOCSIFFLAGS` puis restaure les indicateurs d'origine. Aucun SSID ou secret
n'est demandé par cet essai.

Panther peut renvoyer zéro pour `SIOCSIFFLAGS` même lorsque l'activation échoue :
dans `ifioctl`, l'erreur renvoyée par `dlil_ioctl` n'est pas propagée après ce
cas du `switch`. Le vérificateur exige donc un nouveau refus compté par le
pilote, l'absence de `IFF_RUNNING`, un lien inactif et la restauration des
indicateurs d'origine. Le seul indicateur administratif `IFF_UP` ne prouve pas
que le pilote fonctionne.

## Compilation

Dans Panther 10.3.9 PowerPC avec Xcode 1.5, depuis les sources :

```sh
sh scripts/build-ethernet.sh
```

Résultats : `build/ethernet/DWA121Ethernet.kext` et
`build/ethernet/dwa121-netcheck`. Aucun chargement ni installation n'est
effectué par cette commande. Les dépendances sont celles de Panther, dont
IONetworkingFamily 1.4.0 ; aucun SDK récent n'est utilisé.

Le fournisseur synthétique `EthernetTestProvider.kext` est réservé à la VM et
construit séparément. Sa copie temporaire du diagnostic n'a **aucune
personnalité USB**, pour que la présence d'une clé sur l'hôte ne crée pas une
deuxième interface. Il ne doit pas être copié sur le PowerBook ni livré comme
extension du pilote.

## Essai physique

Arrêter Classic et vérifier d'abord la réception USB 1.1 avec le kit v0.14.0.
Placer le compagnon et `dwa121-netcheck` à côté des outils du kit. Utiliser
le script mis à jour fourni avec le compagnon, depuis ce même dossier :

```sh
tar -xzf DWA121Ethernet-v0.1.0-panther-ppc.tgz
sudo sh scripts/test-physical-ethernet.sh interface 11 --usb11
```

Le script charge temporairement le diagnostic en réception seule, puis le
compagnon. Il contrôle l'interface et le refus d'activation, décharge le
compagnon, attend la disparition de `enN`, puis arrête et décharge le pilote
USB. Les journaux `ethernet-attachment.log`, `ethernet-guard.log`,
`ethernet-ifconfig.txt`, `ethernet-unload.log`, `ethernet-absent.log` et les
contrôles RX doivent être conservés ensemble. Aucune extension n'est installée
dans `/System/Library/Extensions`.

## Validation du 26 octobre 2026

Le compilateur natif GCC 3.3 de Xcode 1.5 accepte le module et le vérificateur
avec `-Wall -Werror`. Les sept fichiers sources de production correspondent
exactement à ceux présents dans la VM lors de la compilation.

Dans la VM, le fournisseur synthétique publie `en3`, MAC de test
`02:44:57:41:12:01`, MTU 1500. Le compteur de refus passe de zéro à un lors
de la demande d'activation, sans `IFF_RUNNING`. Décharger le fournisseur
retire aussi l'interface BSD ; les trois modules sont ensuite absents.
L'outil refuse `--deny-up` lorsqu'il est exécuté sans privilèges.

Sur le **PowerBook3,1 G3 400 MHz**, le même module publie `en1` avec la MAC
réelle de la clé. Le refus d'activation et le retrait passent. Le diagnostic
reçoit simultanément **51 balises**, avec **76/76 lectures USB**, zéro erreur,
zéro expiration de surveillance et liaison **12 Mb/s / bulk 64 octets**.
La radio est arrêtée et les deux modules sont déchargés. L'empreinte MD5 du
fichier de préférences réseau est identique avant et après l'essai ; `en0`
reste connecté en Ethernet.

Le [rapport détaillé](../artifacts/validation-ethernet-v0.1.0.json) référence
les journaux et les empreintes des binaires. Ce succès valide l'attachement
réseau et ses garde-fous, **pas le passage de paquets IP par cette interface**.

## Étape suivante

Raccorder une file Ethernet bornée au client utilisateur propriétaire de la
session WPA2, injecter uniquement les trames CCMP authentifiées dans BSD, et
fermer immédiatement le lien lors d'une erreur, de la perte du propriétaire
ou de l'arrêt USB. Il faudra ensuite valider DHCP REQUEST/ACK, ARP et la
réception unicast réelle avant d'annoncer une connexion réseau utilisable.

## Références de portage

Les méthodes d'attachement et de détachement sont vérifiées dans
[IONetworkController 15.3](https://github.com/apple-oss-distributions/IONetworkingFamily/blob/IONetworkingFamily-15.3/IONetworkController.cpp).
Les indicateurs administratifs et le démarrage effectif sont distincts dans
[IOEthernetInterface 15.3](https://github.com/apple-oss-distributions/IONetworkingFamily/blob/IONetworkingFamily-15.3/IOEthernetInterface.cpp).
Les en-têtes utilisés proviennent directement du SDK installé dans la VM.
Le retour de `SIOCSIFFLAGS` est vérifié dans
[if.c de xnu-517.12.7](https://github.com/apple-oss-distributions/xnu/blob/xnu-517.12.7/bsd/net/if.c).
