# Préparation matérielle TX et CCMP — v0.8.0

Cette étape ajoute les primitives de préparation de l’émission et de la mémoire
de clés. Elle ne fournit pas encore une connexion Wi-Fi. Le diagnostic ne
soumet aucun transfert USB OUT : TXDMA reste désactivé, les files TX sont
suspendues et le chiffrement matériel est désactivé pendant le test.

## Implémentation

- `src/rtl8188cu/txpower.c` décode les calibrations eFuse de la chaîne A, pour
  RTL8188CU TSMC, carte 0, 20 MHz et canaux 1–11. Le calcul suit les tables
  OpenBSD et les modes EEPROM 0–3 ; les champs effacés/non supportés sont
  refusés. Six registres TXAGC sont programmés puis relus, en préservant les
  bits étrangers. Les valeurs précédentes sont restaurées en cas d’échec.
  Ce sont des indices calibrés, pas une mesure de puissance en dBm. La
  politique pays/canal et les réductions locales restent à raccorder avant TX.
- `src/rtl8188cu/cam.c` installe les clés CCMP pairwise en entrée 4 et group en
  entrées 0–3, avec encodage little endian explicite. L’entrée est invalidée
  avant remplacement et validée en dernier. Chaque mot est relu (tous les bits définis du contrôle, tous les bits
  d’adresse et de clé), l’attente du
  contrôleur est bornée et les erreurs déclenchent l’effacement. Les 32 entrées
  peuvent être effacées et vérifiées. Un échec d’effacement impose une remise
  à zéro/quiescence du matériel ; il ne permet jamais d’ouvrir le trafic.
- `src/rtl8188cu/tx.c` prépare le descripteur USB de 32 octets, son checksum et
  les trames d’authentification, association ou EAPOL vers le BSSID sélectionné.
  Les autres données sont refusées. La séquence et le bit Retry restent gérés
  par le logiciel. Le débit provisoire est CCK1, préambule long : le futur
  backend devra vérifier sa présence dans les débits annoncés par le réseau.
  Une trame complète sur un multiple de 64 ou 512 octets signale explicitement
  au backend qu’un paquet USB de longueur nulle est nécessaire pour terminer
  le transfert. La file USB OUT et ce traitement ZLP ne sont pas implémentés.

Le code CAM exige TXPAUSE=ff, SECCFG=0 et les blocs protocole, ordonnanceur,
MAC et sécurité actifs (CR bits 0x2f0). Les quatre DMA restent désactivées
pendant le test. Cela n’active ni l’émission de trames ni leur chiffrement.
Chaque entrée CAM a un pas d’adresse de huit mots, mais seuls les six mots
définis (contrôle/adresse MAC/clé) sont lus et écrits ; les deux mots réservés
ne sont pas traités comme de la RAM. Les bits réservés 5–14 du contrôle
ne participent pas à la comparaison : le dongle renvoie 0x7f00 pour les
entrées 0–3 même après effacement. Le masque 0xffff801f conserve tous les
bits définis (MAC, validité, algorithme, identifiant de clé). Les tests
simulent cette particularité ; les mots contenant la clé restent comparés
sur leurs 32 bits. Sur ces mêmes entrées 0–3, le champ KeyID du contrôle
se relit à zéro : le numéro d’emplacement porte l’identifiant de la clé de
groupe. Le code utilise ce mode de clés par défaut et exigera
SECCFG.RXBCKEY_DEF lors du futur raccordement RX ; la sélection effective
de GTK sur des trames chiffrées reste à tester. Ce contrat convient au test isolé ;
le futur backend doit fermer le port, drainer les transferts et synchroniser
RX avant une installation/remplacement de clé. Les compteurs CCMP PN/RSC et
l’antirejeu des données ne sont **pas** implémentés par ces primitives CAM.
Les réponses aux renouvellements de clé nécessitant une protection 802.11
exigent encore un chemin d’émission CCMP.

Les sources OpenBSD, versions et empreintes figurent dans `upstream.json` ;
leurs licences sont conservées dans chaque fichier et dans
`src/rtl8188cu/NOTICE`. Le mapping des files USB est aussi vérifié dans la
[source Linux rtl8192cu](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/wireless/realtek/rtlwifi/rtl8192cu/trx.c).
Aucun code Linux n’est incorporé.

## Vérification reproductible

```sh
sh scripts/test-host.sh
caffeinate -i sh scripts/build-vm.sh
python3 scripts/exercise-persistent.py --tx-preparation --channel 11 \
  --seconds 10 --output artifacts/tx-preparation-live
```

Le dernier essai charge un module temporaire dans la VM puis le décharge.
`TXPreparationProbe` est désactivé par défaut. Les clés du test sont publiques
(octets 00 à 0f), sans lien avec un réseau utilisateur. Le test vérifie les
registres, efface toutes les entrées CAM, puis contrôle réception passive,
refus d’arrêt non privilégié, arrêt répété et déchargement.

`tests/tx_hardware_test.c` couvre les quatre modes de puissance, trois groupes
de canaux, écrêtage, préservation et restauration des registres, 144 erreurs
I/O injectées, CAM occupée/perte d’écriture, checksum, ordre des octets,
troncatures et 3 900 combinaisons de longueur EAPOL/paquet USB. Ces tests
passent sous ASan/UBSan sur l’hôte et nativement en PowerPC/GCC 3.3.

## Suite nécessaire

Raccorder les primitives à `dwa_session_ops` par une file USB OUT asynchrone
bornée, livrer les réponses d’association/EAPOL depuis RX à un processus
privilégié, programmer MAC/BSSID/AID et les filtres, puis gérer PN/RSC,
chiffrement des données et port contrôlé. L’échange WPA2 sur un point d’accès
réel et le fonctionnement physique USB 1.1 restent à valider.

## Résultat matériel final

`artifacts/tx-v0.8-final-live/` valide les six registres de puissance, une clé
pairwise et les quatre emplacements de groupe avec relecture, l’effacement
complet des champs définis, puis 113 balises NAEBA sur le canal 11. Les
114 requêtes USB se terminent, sans erreur, avec une annulation lors de
l’arrêt. Le refus de la commande non privilégiée, deux arrêts successifs,
la quiescence RX/radio/MCU et le déchargement sont vérifiés. Le fichier
`artifacts/validation-v0.8.0.json` référence les empreintes et journaux.

Les essais précédents sont conservés : ils ont échoué sur les comparaisons
des bits réservés CAM puis du champ KeyID des emplacements par défaut.
Les essais initiaux d’horloges seules n’ont pas résolu ces écarts. Le code
final utilise les blocs MAC/protocole/sécurité actifs avec toutes les DMA
désactivées pendant la préparation. Ces résultats ne prouvent pas la
sélection effective de GTK, le chiffrement CCMP ou une association réelle.
