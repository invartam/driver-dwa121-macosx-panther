# Étude et état du portage — 16 octobre 2026

## Pourquoi l’archive officielle Tiger ne suffit pas

L’archive officielle D-Link `DWA-121_REVA_DRIVERS_1.9.4_MAC.ZIP` contient des variantes 10.4, 10.5 et 10.6. La variante Tiger contient une extension universelle i386/PowerPC `RTL8192CUt.kext` et un utilitaire également PowerPC. Le bundle reconnaît explicitement `2001:3308`.

Dans Panther, la version originale est rejetée à cause de ses dépendances KPI 8.0.0b2 et d’une différence entre la version de bundle et celle du module. Une copie d’étude a été limitée à PowerPC et ses métadonnées adaptées à Panther pour poursuivre l’analyse. Cette opération ne modifie pas le code du pilote et ne rend pas son ABI compatible.

La liaison à blanc avec `kextload -n -t`, génération de symboles et adresses fictives explicites a abouti à :

```text
17RtlWlanDevice92CU is not compatible with its superclass,
20IOEthernetController superclass changed?
```

La résolution des symboles montre aussi des écarts autour de `mbuf`, des verrous `lck_*`, `msleep` et de certaines méthodes IOKit. Les noms C++ et la disposition des classes changent entre les systèmes. Remplacer des numéros de version, ajouter des alias ou forcer le chargement ne constitue donc pas une adaptation fiable. Le pilote Tiger n’a pas été chargé ni installé.

## Ce qui est réellement développé et validé

Le composant original `DWA121Diagnostic` est construit avec les en-têtes de Panther et sa bibliothèque `libkmodc++`. Il utilise les branchements longue portée PowerPC (`-mlong-branch`), requis pour joindre les services noyau depuis l’adresse de chargement de l’extension.

Version 0.2.0 : le cœur de lecture eFuse d’OpenBSD est maintenant porté dans `src/rtl8188cu/identity.c`. Les callbacks d’accès aux registres le rendent indépendant d’IOKit ; le backend Panther réalise des requêtes USB vendor `0x05` en lecture et en écriture de 1, 2 ou 4 octets, contrôle la longueur reçue et propage les erreurs. Les données sont explicitement converties en little-endian.

La séquence implémentée :

1. Vérifier SYS_CFG et refuser les puces de test et la famille 8192C.
2. Sauvegarder SYS_ISO_CTRL, SYS_FUNC_EN et SYS_CLKR avant toute modification.
3. Activer uniquement les bits d’alimentation et d’horloge nécessaires à la lecture eFuse.
4. Lire les enregistrements compacts dans la limite de 512 octets physiques, reconstruire une ROM logique de 128 octets et lire le paramètre PA à l’adresse physique 0x1fa.
5. Restaurer les registres modifiés en ordre inverse et relire les trois valeurs. Une erreur de restauration empêche de publier l’identité comme valide.
6. Vérifier la signature ROM 0x8129 et une adresse MAC unicast non nulle ; extraire VID/PID, channel plan, board type, regulatory, thermal meter et conserver les octets bruts de calibration.
7. Sélectionner le nom de firmware OpenBSD selon le silicium : U pour 8188CU UMC A-cut, T pour les autres 8188CU pris en charge. Le firmware n’est pas chargé à ce stade.

Les commandes de lecture eFuse effacent le bit de commande 31 ; aucun chemin de programmation OTP n’est fourni. Les erreurs USB, délais, fin absente ou enregistrement tronqué sont des échecs, jamais des octets 0xff silencieusement acceptés. Les paramètres réglementaires restent bruts : ils ne sont pas encore utilisés pour autoriser une émission.

### Validation de cette étape

- GCC 3.3 sous Panther : construction du kext v0.2.0 et exécution des tests C sur PowerPC.
- Hôte : mêmes tests avec AddressSanitizer et UndefinedBehaviorSanitizer.
- Bus simulé : image compacte complète et clairsemée, mises à jour d’un même bloc, MAC/signature invalides, limite physique, délai dépassé et 920 injections de fautes (avant ou après application d’une écriture). Les défauts de restauration sont également signalés.
- Clé physique via QEMU : ROM logique de 128 octets, marqueur de fin après 168 octets physiques, VID/PID 2001:3308, MAC valide, firmware requis `urtwn-rtl8192cT`, restauration des registres confirmée par relecture.
- Retrait/réinsertion virtuelle : même ROM, même identité, même sélection de firmware ; le service disparaît au retrait. Déchargement réussi.

Les fautes pendant une transaction sont simulées dans le cœur C. Un retrait physique au milieu d’un transfert noyau n’a pas encore été validé. À la fin de la v0.2.0, le transfert bulk, le firmware, la radio et WPA2 restaient non testés/non implémentés. La validation firmware v0.3.0 est décrite ci-dessous.

## Version 0.3.0 : démarrage du firmware

Le cœur C `firmware.c` porte les séquences USB d’alimentation et de téléchargement d’OpenBSD. Les images T et U sont extraites des tableaux officiels de `sys/dev/microcode/urtwn/microcode.h`, avec la licence de redistribution Realtek. `firmware/manifest.json` en fixe les tailles et SHA-256 ; le script de génération les vérifie avant chaque construction. L’image comprend un en-tête de 32 octets, retiré avant transfert. Aucune image n’est téléchargée à la compilation.

La séquence vérifie la famille et la longueur de l’image, attend l’autoload, active l’alimentation nécessaire, effectue si besoin un reset du microcontrôleur et transfère la RAM par pages de 4 096 octets, avec blocs USB de 196, 4 et 1 octet. Elle attend l’indicateur matériel de checksum, démarre le microcontrôleur puis attend son indicateur d’initialisation. Les accès little-endian sont explicites et les longueurs USB contrôlées. Chaque boucle a un nombre d’essais borné ; chaque requête IOKit a ses propres délais USB. Ce n’est pas encore une échéance globale de démarrage.

Ce composant reste un test de démarrage : après l’acquittement, il arrête le microcontrôleur, désactive MAC/RF et relit les registres d’arrêt avant de libérer l’interface. Les erreurs déclenchent aussi un arrêt au mieux, dont l’échec est signalé séparément. Cet arrêt conserve un état alimenté au repos ; il ne rétablit pas tous les rails d’alimentation. `FirmwareLoaded=true` signifie que l’image a été transférée ; `FirmwareRunning=false` et `FirmwareQuiesced=true` précisent l’état final.

### Résultats observés

- Compilation GCC 3.3 et tests exécutés dans Panther PowerPC ; mêmes tests sur l’hôte avec ASan/UBSan.
- Bus simulé : contenu exact et limites des pages/blocs, en-têtes invalides sans accès matériel, quatre délais dépassés, reset à chaud, 574 fautes de transfert et 12 fautes pendant l’arrêt.
- Clé physique : firmware T version 80.0, 16 084 octets sur quatre pages, `MCUFWDL=0x000300c6`, puis arrêt vérifié.
- Déchargement et rechargement sans débrancher : second démarrage réussi et identité eFuse identique. SYS_CFG passe de `0x04400735` à `0x04400737` : seul le bit d’état ACLK change. Les preuves sont dans `artifacts/firmware-probe.plist` et `firmware-probe-reload.plist`.
- Après redémarrage de Panther et rebranchement physique par l’utilisateur : troisième démarrage réussi, même firmware et même ROM. Le module a été déchargé avec succès, puis l’absence du service confirmée (code 2). Preuves : `artifacts/firmware-probe-reconnected.plist` et `vm-validation-firmware-reconnected.log`. La clé reste transmise à la VM, microcontrôleur arrêté. Ce test ne valide pas un cycle `device_del`/`device_add` dans une même session.
- Aucun transfert réseau, aucune initialisation baseband/RF ni calibration radio n’ont été validés à cette étape.

### Incident au retrait USB virtuel

Le 16 octobre, `device_del` via QMP a expiré puis QEMU s’est terminé par SIGSEGV. Le rapport macOS situe le défaut d’accès à l’adresse zéro dans `darwin_get_active_config_descriptor` (libusb), appelé par `usb_host_attach_kernel`, `usb_host_close`, puis `qmp_marshal_device_del`. L’extrait utile est conservé dans `artifacts/qemu-usb-detach-crash.json`. Le plantage observé est dans le processus hôte ; cela ne permet pas d’en déduire la cause complète ni de déclarer le cycle de retrait/réinsertion v0.3.0 réussi. Panther a redémarré et SSH fonctionne, sans chargement automatique du composant.

## Version 0.4.0 : initialisation et calibration radio

Le nouveau cœur `radio.c` et les tables `radio_tables.h` prennent en charge la variante observée : USB `2001:3308`, famille RTL8188CU, TSMC, carte de type 0, une chaîne d’émission et une de réception. Les autres variantes sont refusées avant les écritures radio. Les tableaux proviennent sans modification du `r92creg.h` référencé dans `upstream.json` : MAC `rtl8192cu_mac`, baseband `rtl8192ce_bb_regs` / `rtl8188cu_bb_vals`, AGC `rtl8188ce_agc_vals`, RF `rtl8192ce_rf1_regs` / `rtl8188cu_rf_vals`.

Après démarrage du firmware, le diagnostic applique 86 écritures MAC, 187 baseband, 160 AGC et 137 RF. Il implémente la lecture série RF via HSSI/LSSI avec sélection de la voie PI, vérifie le registre de canal et calibre la chaîne 0. Les tables RF déclenchent déjà une calibration LC sur le canal 1 ; les comparaisons de canal masquent ce bit de commande. La calibration LC explicite suivante attend sa retombée, avec une limite de 100 essais, et vérifie la conservation des autres bits. Chaque lecture RF implique des requêtes USB et des délais : cette limite n’est pas une échéance globale de 100 ms.

Les changements par rapport aux routines OpenBSD sont explicites :

- Chaque accès USB propage ses erreurs. Le type RF_ENV est restauré par effacement puis réinsertion du bit sauvegardé.
- Les 39 registres temporaires de calibration IQ sont tous lus avant modification, restaurés et relus après chaque série, y compris en cas d’erreur. Les accès aux registres de commande IQ et RF sont arrêtés explicitement.
- Jusqu’à trois séries sont autorisées, avec deux tentatives au maximum par série. Deux séries consécutives doivent avoir quatre composantes TX/RX valides et proches à cinq unités près. Chaque composante compte ; les valeurs signées de dix bits sont comparées après extension du signe. Les indicateurs matériels d’échec, les résultats sentinelles et les amplitudes nulles sont refusés.
- Les corrections IQ utilisent des calculs signés bornés et une division arrondie vers moins l’infini, sans dépendre de la taille de `long` ou d’un décalage signé propre au compilateur.
- Les files réseau restent suspendues, MAC TX/RX et DMA désactivés. Les calibrations activent temporairement la radio. Aucun scan, association, transport bulk de trames ni trafic utilisateur n’est lancé.
- Avant de fermer l’interface, le diagnostic quitte le mode IQ, désactive CCK/OFDM, place le baseband en reset, puis désactive RF et MCU. Les registres d’arrêt accessibles sont relus. Les alimentations ne sont pas entièrement coupées ; la gestion d’un pilote réseau actif et son arrêt complet restent à porter.

### Preuves et limites

La compilation GCC 3.3 et les tests C passent sous Panther PowerPC. Les mêmes tests passent sur l’hôte avec AddressSanitizer et UndefinedBehaviorSanitizer. Les tests radio injectent 1 944 fautes de transaction (avant/après écriture) et 36 fautes d’arrêt. Ils couvrent aussi les mesures IQ invalides ou instables, la correction signée, le bus RF invalide, l’écriture ignorée, la déconnexion simulée et le délai LC dépassé. Ce modèle ne remplace pas les essais de retrait physique pendant un accès noyau.

Deux essais matériels, avec déchargement/rechargement entre les deux, donnent :

```text
radio calibrated IQ runs=2 TX=101,f RX=fe,2 LC=0x7401 BB=0x83040000
radio calibration verified, RF/BB/MCU quiesced; no Wi-Fi
```

`artifacts/radio-probe.plist` et `radio-probe-reload.plist` conservent les résultats ; `scripts/check-radio-probe.py` les vérifie, sans exiger des mesures analogiques identiques d’un démarrage à l’autre. Panther sérialise le registre 32 bits `0x83040000` en entier négatif dans le plist ; le vérificateur normalise ce champ en non signé. Le module a ensuite été déchargé ; `vm-validation-radio.log` confirme l’absence du service (code 2). La clé reste transmise à QEMU afin de ne pas répéter le retrait virtuel défaillant de la v0.3.0.

Ces résultats valident l’application des tables et les acquittements/mesures de calibration sur cette clé. Ils ne valident pas encore la réception radio, les niveaux de puissance d’émission, le respect du domaine réglementaire en fonctionnement réseau, ni une connexion Wi-Fi. Les valeurs eFuse de puissance et de température ne sont pas encore exploitées pour un trafic réseau.

## Version 0.5.0 : DMA et réception passive

`dma.c` porte la topologie LLT et la configuration DMA pour les deux endpoints OUT. Il écrit puis relit les 256 liens : liste réservée sur les pages 0 à 248, anneau sur 249 à 255. Les files reçoivent 12 pages haute priorité, 2 normale et 234 publiques ; la limite RX est `0x27ff`, les pages font 128 octets. L’agrégation DMA utilise le réglage OpenBSD (48 pages, temporisation `USB_DMA_AGG_TO=4`) ; l’agrégation USB supplémentaire est désactivée. Aucun transfert OUT de trame n’est soumis.

La sonde active HCI_RXDMA, RXDMA, protocole, ordonnanceur, les deux blocs MAC et l’horloge de sécurité (`CR=0x02fa`). Le DMA TX et toutes les files TX restent bloqués ; aucun descripteur TX ni transfert OUT de trame n’est soumis. `SECCFG=0` désactive le chiffrement matériel. Le mode 20 MHz et le gain d’écoute sont réglés explicitement. Le filtre accepte les trames de gestion ; le parseur ne conserve que les compteurs de balises valides ; les données et trames de contrôle sont rejetées. Les informations PHY et le FCS accompagnent les trames. La propriété IOKit `PassiveRXChannel` choisit le canal de 1 à 11 (11 dans le livrable, selon le réseau indiqué par l’utilisateur). Le registre RF est relu. Ce réglage RX ne comprend pas la configuration de puissance nécessaire à l’émission.

`PassiveRX.cpp` utilise `IOUSBPipe::Read` asynchrone et un tampon de 16 Kio, avec une seule requête en cours. L’essai comporte jusqu’à huit lectures, avec arrêt après deux délais dépassés sans balise valide, puis désactive RX et soumet une dernière requête pour tester `Abort`. Les buffers sont initialisés à zéro. Les descripteurs de transferts partiels signalés par Panther sont examinés séparément à titre de diagnostic ; leurs résultats ne sont pas ajoutés aux compteurs de réception validée. Délais USB : 1 s sans données, 2 s pour la requête. Après 3 s sans callback, une annulation de secours est suivie de 2 s d’attente. Le tampon, le pipe et le service sont retenus par deux références sous verrou : appelant et callback. Un retour tardif ne peut donc pas accéder à un tampon libéré. Si la callback ne revient toujours pas, le diagnostic échoue et conserve les ressources en vol jusqu’à son retour ; cela peut empêcher le déchargement. Ce défaut du contrôleur n’a pas été provoqué sur le matériel.

Il s’agit d’une **sonde bornée exécutée pendant `start`**, avec attente dans le thread de démarrage, pas encore d’une boucle RX persistante sur workloop pour une interface réseau active. Les callbacks ne reprogramment pas de requêtes et ne décodent pas les trames. Le retrait physique pendant une requête et les courses de terminaison IOKit restent à tester avant cette intégration persistante.

Le parseur `rx.c` lit les descripteurs little-endian de 24 octets sans accès non alignés. Il contrôle les tailles PHY, décalages, nombres de paquets et alignements de 128 octets. Il rejette les erreurs CRC/ICV matérielles, vérifie le CRC32 FCS des balises, leur en-tête et les bornes des éléments d’information. Seuls les compteurs sont conservés : aucun SSID, BSSID distant ou contenu reçu n’est publié ou journalisé. Si un agrégat devient malformé après un préfixe valide, les compteurs de ce préfixe restent acquis ; aucun paquet n’est livré à une pile réseau.

Les tests C couvrent la topologie LLT, 2 145 fautes DMA/RX, le blocage du DMA et des files TX, les délais, les troncatures, décalages, agrégats, le FCS et 10 000 essais de buffers malformés. Le premier essai sur le canal 1 a confirmé 256 liens LLT, huit callbacks de délai USB et une callback d’annulation ; aucune balise n’y a été reçue (`rx-probe-no-frames.plist`). Cet essai seul ne valide pas le chemin radio/RX.

### Diagnostic du 17 octobre 2026

Les sondes `rx-sweep-baseline` ont vérifié les canaux 1, 6 et 11, avec le canal RF relu et trois callbacks par essai (deux délais dépassés et une annulation). Aucun descripteur n’a été reçu. `kextload -c` est nécessaire : sans cette option, Panther avait conservé la personnalité du canal 11 malgré la modification du plist. Le script `sweep-rx.py` contrôle le canal effectif et conserve les échecs au lieu de les compter comme des essais RX réussis.

L’activation de MAC TX en complément de MAC RX (`0x2ba` → `0x2fa`) fait progresser des compteurs matériels et les pointeurs FIFO. Cela ne prouve pas à lui seul la réception de balises. Les essais suivants activent l’agrégation DMA avec temporisation, sans modifier le DMA TX ni les files suspendues.

Sous OHCI, Panther rapporte ensuite 12 288 octets restants sur 16 384, mais les buffers préalablement mis à zéro restent vides. La trace QEMU contient des lectures bulk de 4 096 octets suivies de complétions de longueur nulle. Ces 4 Kio apparents ne sont donc pas comptés comme des données reçues (`rx-partial/`, `qemu-usb-rx-partial.log`). Deux essais ont aussi échoué avant RX : stabilité IQ insuffisante, puis restauration IQ non vérifiée. La comparaison `CR=0x2ff` n’a pas atteint RX et ne permet aucune conclusion sur cette valeur ; elle n’est pas conservée dans le code.

Une configuration de démarrage distincte, `scripts/boot-ehci.mjs`, ajoute le contrôleur USB 2.0 sans modifier les scripts fournis par l’utilisateur. Panther reconnaît AppleUSBEHCI 2.2.0. Après le rebranchement demandé à l’utilisateur, la clé est énumérée à 480 Mb/s, contre 12 Mb/s sous OHCI. L’essai RX dans cette configuration reçoit huit balises sur le canal 11, avec FCS logiciel correct, neuf callbacks sur neuf, zéro délai dépassé et zéro erreur USB/CRC/ICV/FCS. Les buffers USB de 512 octets contiennent les descripteurs attendus ; la sonde publie `RXFramesVerified=true`. Les preuves sont dans `artifacts/rx-ehci/`, vérifiées par `scripts/check-rx-probe.py`. Cette réussite suit aussi un redémarrage et un rebranchement : elle ne démontre pas, à elle seule, une cause unique du défaut OHCI. Le résultat définitif et l’état de nettoyage figurent dans `artifacts/validation.json`.

La répétition après déchargement/rechargement confirme huit balises sur le canal 1 et huit sur le canal 11. Le canal 6 ne fournit aucune trame lors de cette sonde ; toutes ses requêtes reviennent, y compris une annulation de secours. Le binaire final, avec contrôle de cohérence de vitesse USB, reçoit encore huit balises sur 11 et publie `USBLinkSpeedMbps=480`, `USBBulkMaxPacket=512`. Les quatre preuves positives passent le vérificateur strict ; `rx-ehci-final/channel-11.plist` correspond au livrable final. Le diagnostic est déchargé, RF/BB/MCU arrêtés et les traces QEMU désactivées.

Le script `qmp-usb-trace.py` active seulement les métadonnées de requêtes et complétions (pas les payloads), puis désactive la trace. Le choix EHCI suit la [documentation USB officielle de QEMU](https://www.qemu.org/docs/master/system/devices/usb.html). Cette comparaison ne remplace pas les essais sur un Mac PowerPC physique.

### Contrainte PowerBook USB 1.1

Le 17 octobre, l’utilisateur précise que la machine cible n’a que l’USB 1.1. Le manuel constructeur de la révision A indique explicitement « USB 2.0 (1.1 compatible) » dans les caractéristiques, page 58, et avertit de la réduction de débit page 5. Le WPA2 reste une fonction de la liaison Wi-Fi ; la version USB n’impose pas à elle seule de renoncer à cette fonction. Le pilote Panther demandé reste à terminer.

La sonde utilise désormais `IOUSBDevice::GetSpeed()` et contrôle les tailles de tous les endpoints bulk avant tout accès eFuse/firmware : Full Speed et 64 octets, ou High Speed et 512 octets. Le mélange observé sous OHCI (12 Mb/s annoncés, descripteurs de 512 octets) est refusé explicitement. Ces contrôles ne forcent pas la vitesse physique du périphérique ; émuler un contrôleur OHCI ne suffit pas à établir que la clé, branchée sur l’hôte moderne, a réellement négocié le Full Speed. Le comportement de ce montage reste distinct de celui du PowerBook.

Les deux profils et leurs incohérences sont testés sur l’hôte et dans Panther PowerPC. Il reste à essayer un véritable lien à 12 Mb/s, sur le PowerBook ou via un hub exclusivement USB 1.1. L’utilisateur ne dispose actuellement d’aucun de ces deux moyens de test accessibles. Le code et la documentation ne présentent donc pas l’USB 1.1 comme validé sur matériel.

Source : [manuel officiel DWA-121 révision A](https://support.dlink.com/resource/products/DWA-121/REVA/DWA-121_MANUAL_1.00_EN.PDF).

## Version 0.6.0 : découverte des BSS et annonces RSN

Le cœur original `src/net80211/beacon.c`, sous la licence MIT du projet, décode les balises sans dépendance à Panther. La fonction `rtl8188cu_rx_parse_beacons` appelle un consommateur synchrone seulement après validation des bornes, des indicateurs matériels et du FCS logiciel. Le tampon est emprunté pendant cet appel ; aucune adresse du tampon USB n’est conservée. Les transferts partiels en délai dépassé et la requête de test d’annulation n’alimentent pas la découverte. Un préfixe valide d’un agrégat demeure compté si un descripteur ultérieur est malformé.

Une table de 16 entrées, allouée sur le tas pour préserver la pile noyau Panther, regroupe par BSSID. Chaque nouvelle observation remplace entièrement le dernier état d’un BSSID connu et incrémente son compteur. Une table pleine abandonne les nouveaux BSSID avec un compteur explicite. Les éléments SSID ou canal dupliqués, les canaux DS/HT contradictoires, les en-têtes fragmentés et les adresses incohérentes sont rejetés. Le SSID reste une suite d’au plus 32 octets, y compris NUL et octets non UTF-8. Les SSID vides ou entièrement nuls sont marqués masqués. Le canal annoncé peut être absent (0) ou différent du canal d’écoute ; ces deux informations sont publiées séparément. La présence d’un élément HT ne signifie pas que le mode 802.11n est implémenté.

Le parseur RSN vérifie version, longueurs, listes de sélecteurs, capacités, liste PMKID et sélecteur optionnel de protection de groupe. Les champs omis utilisent les valeurs par défaut RSN (CCMP/CCMP/802.1X) ; PSK doit être explicite, avec le sélecteur exact `00:0f:ac:02`. Les sélecteurs inconnus ne sont jamais assimilés à PSK ou CCMP. Les éléments RSN multiples produisent un état ambigu/malformé, sans reprendre les capacités du premier. La propriété `WPA2PSKCCMPAdvertised` décrit uniquement les suites annoncées et le bit Privacy ; elle ne promet ni compatibilité complète ni authentification réussie. PMF requis et capable sont exposés séparément. Le WPA ancien via IE constructeur n’est pas décodé : l’absence de RSN ne signifie pas réseau ouvert, et le champ de capacités est conservé.

Il s’agit d’observations non authentifiées, pas d’un cache autorisant une connexion. Une observation structurellement rejetée ne remplace pas la précédente. Avant une association future, il faudra revalider une observation récente, les suites choisies, les débits, le mode infrastructure, le domaine réglementaire et les exigences PMF. Le RSN original est conservé comme octets d’annonce ; il ne doit pas être recopié tel quel dans une demande d’association, qui devra contenir les suites négociées.

`DiscoveredBSS` publie les observations dans IOKit sous forme de dictionnaires et `OSData`. Aucun SSID n’est converti en chaîne noyau. `scripts/show-bss.py` affiche un JSON échappé et l’hexadécimal exact du SSID. **Les fichiers plist de cette version contiennent les noms et adresses des points d’accès voisins.** Ils ne contiennent aucun mot de passe. Le journal noyau reste limité aux compteurs/descripteurs de la sonde existante.

La limite par défaut passe de 8 à 32 lectures (1 à 64 configurables), avec l’arrêt anticipé après deux délais sans aucune balise. Le test d’annulation et l’arrêt RF/BB/MCU sont conservés. Le délai de collecte du résultat côté hôte passe à 360 secondes pour couvrir la borne maximale des requêtes et de leurs annulations. L’extension reste une sonde exécutée dans `start`, sans RX persistant, émission réseau, interface Ethernet ou supplicant.

Les tests couvrent les suites enterprise/SAE/PSK, les OUI étrangers, les mélanges de suites, les exigences PMF, les listes PMKID, les troncatures, les chaînes binaires, les doublons, le débordement de table et les mises à jour. Un vecteur FCS calculé indépendamment vérifie que le consommateur ne voit pas de trame corrompue ou tronquée. 10 000 essais déterministes mêlent mutations de balises structurées, données arbitraires et éléments RSN arbitraires. Les mêmes tests s’exécutent sous ASan/UBSan sur l’hôte et dans le binaire PowerPC GCC 3.3.

Premier essai matériel v0.6 : `artifacts/bss-ehci/channel-11.plist` contient 32 balises valides du même BSSID NAEBA, canal annoncé 11, RSN valide avec PSK/CCMP pour pairwise et group, PMF capable mais non requis. Les 33 callbacks (32 lectures et une annulation) sont revenus, sans erreur USB, CRC/ICV/FCS ni rejet du décodeur BSS. Le module a été déchargé avec succès. RETRO n’a pas été observé pendant cette sonde ; cela ne prouve pas son absence ni son incompatibilité.

Répétition du 18 octobre 2026 : `bss-ehci-reload/channel-01.plist` contient 55 balises vérifiées réparties entre NAEBA (29) et RETRO (26), deux BSSID distincts annonçant le canal 1. NAEBA y annonce également PSK/CCMP sans PMF. Les balises RETRO ne contiennent pas de RSN et leur bit Privacy est à zéro ; ce constat décrit ces annonces reçues, sans authentifier le point d’accès ni prouver la configuration de tous les appareils portant ce SSID. Le retour au canal 11 retrouve le BSSID NAEBA de la première sonde et les mêmes capacités RSN (32 balises). Les trois sondes v0.6 ont 33/33 callbacks, zéro erreur USB/CRC/ICV/FCS, zéro rejet BSS et aucun débordement de table. `verify-bss.log` et `vm-validation-beacon.log` valident les résultats et l’absence finale du service. La table distingue donc plusieurs points d’accès portant le même SSID, sans fusionner leurs paramètres.

Référence de format consultée : [parseur RSN du projet hostap](https://w1.fi/hostapd/devel/wpa__common_8c_source.html), fonction `wpa_parse_wpa_ie_rsn`. L’implémentation du projet est originale ; aucune dépendance à la bibliothèque hostap n’est introduite.

## Version 0.7.0 : moteur RX persistant

`PantherRXEngine`, une source d’événements IOKit, utilise sa propre `IOWorkLoop`, un `IOCommandGate` et un `IOTimerEventSource` de surveillance. Une seule lecture bulk de 16 Kio est en cours ; le même tampon est réutilisé après consommation du résultat. Le callback USB n’effectue ni décodage, ni soumission USB, ni acquisition du gate : il enregistre le résultat sous un verrou bref, réveille la workloop et libère sa référence. Le décodage des balises et le réarmement se font sur la workloop. Une complétion immédiate à l’intérieur de `Read` ne provoque donc pas une récursion de nouvelles lectures.

`rx_lifecycle.c` définit les transitions partagées avec les tests : réservation, complétion, consommation, rejet de soumission et arrêt. `pending` demeure vrai jusqu’à la consommation, même si le callback est déjà arrivé. Le verrou protège les transitions du callback ; le gate sérialise les décisions de soumission, d’arrêt et de traitement. Le moteur respecte le contrat USB : exactement un callback pour une soumission acceptée, aucun pour une soumission refusée. Il ne prétend pas tolérer un contrôleur qui réutilise un ancien callback pour une autre requête.

La commande administrateur `dwa121-rx stop` passe par `IORegistryEntrySetCFProperties` avec le seul paramètre accepté, `RXStop=true`. Le noyau vérifie lui-même le privilège administrateur. L’arrêt ferme d’abord la possibilité de réarmer, annule au plus une fois la lecture en cours et attend le drainage en dehors du gate. Les notifications sont ensuite interdites sous le verrou du callback avant le retrait des sources d’événements. Une requête dont le callback manque conserve une référence indépendante au moteur, au tampon, au pipe et au service, même après l’échec du drainage ; le module peut ainsi rester occupé. Le callback tardif ne reprend aucune réception et n’accède pas au contexte matériel libéré. L’arrêt matériel désactive ensuite RX puis RF/BB/MCU avant de fermer l’interface USB. Le même chemin est appelé depuis `IOService::stop` lors du déchargement du module. Le service possède une seconde workloop, dédiée au cycle de vie et distincte de celles du moteur RX et du contrôleur USB. Son `getWorkLoop()` la retourne explicitement pour la finalisation Panther. `finalize()` réalise l’arrêt matériel sous cette workloop avant de déléguer à `IOService::finalize` ; la phase ultérieure `stop`, exécutée par Panther sous la workloop du fournisseur USB, ne répète qu’un nettoyage déjà terminé.

Le délai de surveillance de chaque lecture est de 3 secondes, en complément des délais USB de 1/2 secondes déjà utilisés. Si aucune complétion n’est arrivée, la surveillance arrête le réarmement et demande l’annulation. Les erreurs de transport arrêtent aussi le réarmement. Une commande d’arrêt ou le déchargement reste nécessaire pour la mise au repos RF après une telle erreur. Les dépassements de délai USB ordinaires sont comptés, leurs données partielles sont ignorées et le toggle est resynchronisé avant de poursuivre. L’absence de balises ne suffit pas à elle seule à arrêter le moteur, mais un canal silencieux déclenche actuellement sa surveillance sous QEMU/Panther : aucune callback de délai USB ordinaire n’arrive avant les 3 secondes. L’écoute continue sur un tel canal n’est donc pas validée. Le rejet d’une trame malformée ne réutilise jamais son contenu comme annonce de réseau.

Le mode `PersistentRX=true` devient le mode par défaut du bundle. La propriété `PassiveRXReadLimit` reste réservée à l’ancienne sonde ; elle ne limite pas la boucle persistante. `sweep-rx.py` force explicitement `PersistentRX=false` pour conserver les comparaisons avec les versions précédentes. La réception demeure passive, sur un canal fixe de 1 à 11 : aucune interface réseau, aucun transfert de trame OUT, aucune association et aucune authentification WPA2 ne sont ajoutés. Les deux profils USB Full/64 et High/512 sont conservés.

Les propriétés IOKit sont actualisées après chaque transfert USB traité. Un instantané lu pendant l’activité peut chevaucher une mise à jour de plusieurs propriétés ; les contrôles stricts de cohérence portent donc sur l’instantané **après arrêt**. `RXRunning` décrit le moteur, `RXRequestsDrained` l’absence de requête en cours, et `PersistentStopVerified` le drainage et l’arrêt matériel confirmés. `RXStopping` mémorise la demande d’arrêt dans le rapport du moteur. La table BSS reste bornée à 16 entrées sans vieillissement ; elle est une observation et non un cache authentifié utilisable directement pour une association.

Un premier essai de déchargement en activité a drainé 130/130 callbacks et 127 balises, puis s’est bloqué avant la fin de l’arrêt matériel (`artifacts/persistent-unload/failure.json`). Les sources Apple de Panther montrent que `_workLoopAction` exécute `finalize` sous la workloop du service, puis `actionStop` sous celle du fournisseur USB. Cette prise du gate USB explique le blocage des requêtes synchrones dans `stop`. Le correctif fournit une workloop dédiée au service et déplace l’arrêt matériel dans `finalize`, avant la phase `stop` du fournisseur. La récupération a d’abord utilisé `sync; reboot`, puis `system_reset` ; ces démarrages à chaud sont restés à l’écran de boot. Un redémarrage à froid du processus QEMU avec le même disque/configuration EHCI a restauré Panther et la clé à 480 Mb/s sans rebranchement utilisateur. Aucun `device_del` n’a été utilisé. Les références Apple et leurs empreintes sont dans `docs/lifecycle-references.json`. L’essai initial bloqué n’est pas compté comme une validation réussie.

Le binaire corrigé reçoit 213 balises sur le canal 11 avant son déchargement en activité : 140/140 callbacks drainés, une annulation, zéro erreur USB, arrêt RX et radio confirmés, module et service absents ensuite (`artifacts/persistent-unload-retry/`). La commande d’arrêt non privilégiée est refusée. Un essai préalable de ce même binaire a échoué pendant la calibration IQ, avant de créer le moteur RX ; son journal est conservé dans `artifacts/persistent-unload-final/`. Cet échec intermittent d’initialisation n’est pas compté comme un succès RX et les critères de calibration restent inchangés.

L’arrêt administrateur du binaire corrigé est également vérifié, avec refus de l’utilisateur non privilégié et deux arrêts successifs sans modification des compteurs : 236 balises, 142/142 callbacks, zéro erreur USB, RX/RF/BB/MCU au repos (`persistent-stop-awake/`). NAEBA et RETRO sont cette fois tous deux observés sur le canal 11, respectivement 140 et 96 fois. Un essai précédent (`persistent-stop-final/`) s’est interrompu après 132 balises, avec surveillance et arrêt matériel non vérifié. Le journal `host-sleep-persistent.log` confirme une veille de l’hôte pendant cet essai, et aussi pendant l’échec d’initialisation précédent. Cette corrélation est compatible avec une interruption de QEMU/USB ; elle ne prouve pas une reprise après veille correcte. Le script d’essai maintient désormais une assertion `caffeinate -i` limitée à sa propre durée. Aucune politique de veille permanente n’est modifiée.

Le scénario `persistent-watchdog-verified/`, sur le canal 6 sans balise, confirme un seul déclenchement de surveillance, une seule annulation et 1/1 callback drainé. Le réarmement s’arrête ; la commande administrateur met ensuite RX/RF/BB/MCU au repos, demeure idempotente et le module se décharge. `exercise-persistent.py --expect-watchdog` et `check-persistent-probe.py --expect-watchdog` exigent cet état précis, avec un compteur d’erreur égal à un. L’échec initial de l’exigence d’écoute continue reste conservé dans `persistent-timeout/`. La régression en mode borné (`persistent-legacy/`) reçoit 59 balises et draine 33/33 callbacks, sans erreur ; les deux vérificateurs RX/BSS passent. Au terme des essais, le module est déchargé, le service absent, RX/RF/BB/MCU arrêtés et la VM reste démarrée avec la clé transmise en EHCI. L’assertion temporaire de veille est retirée.

Les tests du protocole de cycle de vie explorent 279 936 séquences déterministes, en plus des placements explicites d’un arrêt avant/après soumission, callback immédiat ou différé, consommation et rejet de soumission. Ils vérifient l’absence de réarmement après arrêt, l’absence de réutilisation du tampon avant consommation et la conservation de la référence de callback manquant. Ce sont des tests du modèle utilisé par le backend, pas une preuve exhaustive des courses internes d’IOKit. Le retrait physique en activité, un contrôleur qui ne rend jamais ses callbacks même après annulation, la reprise après veille et les essais USB 1.1 restent à valider sur matériel adapté.

## Travail nécessaire pour obtenir le pilote demandé

Le [cœur utilisateur association/WPA2](association-wpa2.md) est désormais implémenté et testé avec un point d’accès simulé, y compris dans Panther PowerPC. Le raccordement matériel et les fonctions suivantes restent à terminer :

1. **Initialisation RTL8188CUS restante** : politique du domaine réglementaire (calcul de puissance eFuse et relecture TXAGC portés en v0.8), réglage de canal pour l’émission, compensation thermique, arrêt complet et gestion du cycle de vie matériel. Le démarrage du firmware, les tables MAC/baseband/RF et les calibrations initiales IQ/LC sont désormais portés pour la variante testée.
2. **Transport de trames restant** : transferts USB OUT et files TX bornées (descripteurs de gestion/EAPOL portés en v0.8), retrait physique en activité et reprise. Le moteur RX sur workloop, sa surveillance et son arrêt sont implémentés en v0.7 ; les essais de déconnexion physique et de défauts du contrôleur restent nécessaires. La sonde bulk RX bornée, le parseur de balises et une annulation explicite sont validés ; ils ne constituent pas encore le transport d’une interface réseau.
3. **Station 802.11** : raccorder au transport USB la machine d’authentification ouverte, d’association RSN et de déconnexion désormais implémentée dans `station.c`/`session.c`. Ajouter scan, vieillissement/sélection des observations et reconnexion. Le cœur actuel accepte une balise fraîche explicitement sélectionnée en 802.11b/g 2,4 GHz ; le mode 802.11n reste un travail distinct.
4. **Intégration réseau Panther** : sous-classe native `IOEthernetController`, conversion Ethernet/802.11, gestion des mbufs Panther, lien actif seulement après autorisation, multicast et DHCP. Aucun accès aux API AirPort privées n’a été établi.
5. **WPA2-Personal** : raccorder le moteur hostap 2.12 porté à la livraison EAPOL et aux primitives CAM vérifiées en v0.8, puis au chiffrement CCMP des données et à leurs compteurs PN/RSC. L’échange M1–M4, le renouvellement GTK, les rejouements, la non-réinstallation de clés et le contrôle d’autorisation sont testés avec un backend simulé. Ils doivent encore être validés sur la clé physique. Les données doivent rester bloquées tant que l’authentification et les installations matérielles n’ont pas abouti.
6. **Configuration utilisateur** : SSID, mot de passe/PSK, statut et erreurs, sans écrire les secrets dans les journaux. La connexion finale devra être testée sur un point d’accès contrôlé et autorisé par l’utilisateur.

Le pilote OpenBSD `urtwn` et son cœur `rtwn` sont une base de référence libre pour le matériel. Leur code dépend toutefois des API USB, des mbufs et de la pile `net80211` BSD : ce ne sont pas des sources directement compilables avec IOKit Panther. Un vrai portage doit remplacer ces dépendances et conserver les licences des composants repris. Les séquences eFuse/identification, firmware et initialisation/calibration radio sont portées ici. La pile réseau BSD ne l’est pas.

## Critères pour déclarer WPA2-PSK opérationnel

La compilation et la découverte USB seules ne suffisent pas. Il faudra observer une association RSN avec CCMP, un échange EAPOL complet, l’obtention d’une adresse IP et du trafic bidirectionnel sur la clé. Il faudra aussi vérifier le mauvais mot de passe, le renouvellement de clé, les retransmissions/rejeux, la perte du point d’accès et le retrait USB pendant une activité réseau. Les premiers essais VM ne remplacent pas les essais sur le Mac PowerPC physique cible.

## Sources consultées

- Archives constructeur : https://support.dlink.com/resource/products/dwa-121/REVA/
- Identifiant A1 : https://support.dlink.com/resource/products/DWA-121/REVA/DWA-121_REVA_RELEASENOTES_UTITLY_1.02_EN_US.PDF
- Source USB de référence : https://github.com/openbsd/src/blob/master/sys/dev/usb/if_urtwn.c
- Registres de référence : https://github.com/openbsd/src/blob/master/sys/dev/ic/r92creg.h
- API effectives : en-têtes IOKit et Kernel.framework installés dans la VM Panther.

Les fichiers de référence téléchargés restent dans `research/`, avec leurs propres droits. Le livrable contient les portages eFuse, firmware et radio sous licence OpenBSD, les images binaires sous licence Realtek, leurs notices et preuves de test. `docs/upstream.json` conserve les révisions et SHA-256 des références utilisées. La LLT, le parseur et la sonde bulk bornée sont implémentés. La réception de balises et le moteur RX persistant sont implémentés sous EHCI ; la suite est le transport TX et la pile station 802.11. La configuration de puissance/réglementation doit précéder toute émission réseau. Le retrait USB reste à revalider sur un transport hôte stable.

## Préparation TX/CCMP v0.8 — 19 octobre 2026

La [préparation matérielle](tx-preparation.md) vérifie sur la clé la puissance
eFuse/TXAGC, une clé pairwise et les quatre emplacements de groupe, puis leur
effacement. Les essais ont identifié les bits de contrôle CAM réservés lus à
1 et le champ KeyID lu à zéro dans les quatre emplacements de clés par défaut.
Les tests reproduisent ces particularités. Après préparation sans émission,
113 balises NAEBA sont reçues, 114/114 callbacks drainés sans erreur, puis le
pilote est arrêté et déchargé. Le port réseau, le transport USB OUT et le
chiffrement réel des données restent à raccorder ; aucun succès WPA2 radio
n’est revendiqué. Les preuves sont dans `artifacts/validation-v0.8.0.json`.
