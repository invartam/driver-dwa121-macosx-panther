# WPA2 sur la clé — diagnostic v0.13

La v0.14 conserve ce mode d’échange initial et ajoute une [sonde de données
CCMP et DHCP](ccmp-hardware.md), activée séparément. Les limites ci-dessous
décrivent le mode WPA2 simple ; les opérations de données sont documentées
dans cette extension.

`dwa121-wpa2` raccorde le moteur RSN de wpa_supplicant 2.12 à la DWA-121 sous
Panther. La sonde effectue l'association, reçoit M1, envoie M2, vérifie le MIC
et le contenu de M3, envoie M4, puis installe et relit les clés CCMP pairwise et
group dans la CAM. Elle termine en effaçant les clés et en arrêtant la radio.
**Le port de données reste fermé : aucun trafic IP ni connexion Internet.**

## Résultat matériel

Le 24 octobre 2026, l'essai sur NAEBA, canal 11, avec le mot de passe fourni
séparément par l'utilisateur a réussi dans Panther avec la clé physique via
QEMU EHCI : un M1 et un M3 reçus, M2 et M4 terminés en USB, clé pairwise dans
l'entrée 4 et clé de groupe dans l'entrée 1, écritures relues, puis effacement
et arrêt vérifiés. Le PMK calculé sous Panther correspond également à une
dérivation indépendante sur l'hôte. Aucun secret n'est inclus dans ce document.

Le résultat est reproduit avec le binaire final (AID 23, quatre transferts OUT
terminés, 515 octets au total, 282/282 callbacks RX, aucune erreur USB). Le test
de mort du processus après l'installation de la clé pairwise passe aussi :
les cinq emplacements sont effacés et relus par le noyau, les 244 callbacks RX
sont drainés, puis radio et firmware sont arrêtés et le module est déchargé.

Cela prouve la vérification cryptographique de M3 et la terminaison locale de
M4. Une terminaison USB ne constitue pas un acquittement radio de M4, ni une
preuve de trafic chiffré accepté par la box. La validation réseau complète
nécessitera le chemin CCMP, les compteurs PN/rejeu, l'interface réseau et IP.
L'USB 1.1 physique reste à tester.

## Correction de temporisation

Les premiers essais s'arrêtaient après M2. L'effacement initial des 32 entrées
CAM était fait après association et prenait jusqu'à 7,8 secondes dans Panther.
La box envoyait alors une désauthentification de raison 15, sans M3 accepté.

Cet effacement se fait désormais **avant la première authentification**,
avec TX bloqué. L'utilitaire exige ensuite une nouvelle balise du même BSSID,
âgée d'au plus une seconde, avant d'envoyer l'authentification. La préparation
après association ne fait plus que configurer le mode station et la file BE ;
elle a pris 82 ms lors du premier succès et 18 ms lors de la répétition
avec le binaire final. Aucun changement de mot de passe
n'a été nécessaire.

## Contrat de contrôle

Le mode exige `AssociationTXEnabled=true` et `WPA2ProbeEnabled=true`, absents
de la personnalité livrée par défaut. Le propriétaire reste un seul client
administrateur et le privilège est revérifié à chaque appel. Le nouvel index
IOUserClient 2 reçoit une structure fixe de 2 112 octets, version 1 :

- `ENTER` : après une association déclarée par le client, AID 1–2007 et
  résultats USB de gestion consommés. Exige la CAM déjà préparée.
- `SEND` : uniquement EAPOL-Key M2/M4 non chiffrés, ToDS, vers le BSSID choisi ;
  jusqu'à huit envois. Le descripteur vérifie aussi les adresses et LLC.
- `KEY` : CCMP 16 octets, pairwise entrée 4 ou groupe 0–3, après drainage TX.
  Pause des files, écriture/relecture CAM, réouverture de MGNT/BE seulement.
  Une seconde installation dans le même emplacement est refusée.
- `VERIFIED` : rapport du supplicant après M3 vérifié, M4 terminé et clés
  installées. Le noyau ne calcule pas le MIC et ne transforme pas ce rapport
  en autorisation de trafic.

`POLL` et `END` restent sur l'index 1. La limite de gestion reste dix secondes ;
une entrée dans la phase WPA2 l'étend à trente secondes depuis le début de la
session TX, avec minuterie indépendante. Les buffers de requête contenant une
clé sont effacés après l'appel. Aucune clé n'est publiée dans IOKit.

L'arrêt bloque les files, draine les callbacks USB, efface et relit les cinq
emplacements de session, puis arrête RX, la radio et le firmware. Il s'applique
aussi à la fermeture du client, à sa mort et à l'expiration. Un défaut de
nettoyage est rapporté explicitement ; aucune nouvelle session ne réutilise
ces files. `SECCFG` reste désactivé pendant ce diagnostic. Les RSC sont conservés
en mémoire de session et effacés à l'arrêt ; leur présence ne signifie pas
qu'un moteur anti-rejeu de données est déjà implémenté.

Le callback utilisateur facultatif `handshake_complete` permet de constater
la fin de l'échange sans appeler `port(1)`. Dans ce mode,
`dwa_wpa2_handshake_verified()` peut être vrai, mais
`dwa_wpa2_authorized()` reste faux. Le coordinateur réseau normal conserve son
contrat d'autorisation et refuse ce callback de diagnostic.

## Exécuter l'essai

Sur le PowerBook, utiliser la [procédure physique](test-machine-physique.md) :

```sh
sudo sh scripts/test-physical.sh wpa 11 'NAEBA' --usb11
```

Le mot de passe Wi-Fi est saisi à l'invite sans écho ; il ne doit pas être placé
dans la commande. Le programme désactive les core dumps et efface ses buffers
de passphrase/PMK. Les journaux n'enregistrent ni clé, ni nonce, ni paquet EAPOL
brut. Ils indiquent les étapes et les compteurs.

Depuis l'hôte de développement, avec Panther démarré et la clé transmise :

```sh
python3 scripts/exercise-wpa.py --ssid NAEBA --channel 11 --output artifacts/wpa-new
```

Le script demande le secret sans écho et l'envoie par stdin à travers SSH,
avec une dérivation PBKDF2 indépendante pour détecter une erreur de transport
du secret. Aucun mot de passe, PMK ou secret dérivé n'est placé dans argv,
l'environnement, les sources ou les fichiers de résultats. La comparaison ne
publie que PASS/FAIL. L'ancien sudo de Panther nécessite d'invalider son cache
avec une commande séparée avant de lire son mot de passe sur stdin.

Le scénario de développement `--abandon-after-key` fait mourir volontairement
le processus après la première installation de clé pour contrôler l'effacement
par le noyau. Il ne doit pas être interprété comme un échange WPA2 terminé.

## Références et limites

Les registres de mode station, de pause BE et de CAM suivent les sources
[OpenBSD rtwn](https://github.com/openbsd/src/blob/master/sys/dev/ic/rtwn.c)
et [r92creg](https://github.com/openbsd/src/blob/master/sys/dev/ic/r92creg.h),
déjà épinglées dans `upstream.json`. La politique cryptographique et les
références hostap sont décrites dans [association-wpa2.md](association-wpa2.md)
et `wpa-upstream.json`. Les nouvelles séquences de préparation et de clé sont
testées avec injection de défauts de registre ; le moteur cryptographique est
testé contre un point d'accès OpenSSL indépendant sur l'hôte et sur PowerPC.

Ce mode ne gère pas les renouvellements de clés sur une connexion persistante,
les EAPOL protégés, l'itinérance, la veille, les données chiffrées ou IP. Il ne
crée aucune interface réseau et n'est pas installé au démarrage.
