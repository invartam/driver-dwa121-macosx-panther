# P0.3.2 — sélection et association natives

## Phase A — audit avant modification du code

Base : `30518c9`, branche `v1/airport-p032`. Le prototype ne remplace pas
la référence installée v0.15.0. Aucun push autorisé pour ce jalon.

Le profil lu par `dwa121-connect --config` contient un SSID de longueur explicite,
un canal et une PMK dérivée pour ce SSID. `src/config/config.c` exige un fichier
ordinaire root 0600, sans liens multiples, vérifie l'inode entre lstat/open et
efface la structure après utilisation. Il ne faut jamais appliquer cette PMK
à un autre SSID. Le superviseur installé configure le canal dans son bundle
avant chargement ; il ne propose actuellement aucune entrée de sélection native.

`src/tools/dwa121-associate.c` ouvre le contrôle exclusif du propriétaire USB,
recherche une balise fraîche compatible avec le SSID sur le canal courant,
puis sélectionne son BSSID. `DWA_ASSOC_BEGIN` prépare le matériel/CAM ; une
nouvelle balise du même BSSID doit ensuite confirmer la cible. `rtl_station`
construit l'authentification ouverte 802.11 et l'association RSN ; il contrôle
les réponses, les adresses, les codes de statut, les délais et les réessais.
Authentification ouverte 802.11 ne signifie pas réseau sans chiffrement.

`wpa_probe.inc` réutilise hostap pour EAPOL et le handshake WPA2. Les clés CCMP
sont installées dans le chemin logiciel et la CAM vérifiée. Le démarrage de
la session réseau est conditionné au handshake vérifié ; DHCP est lancé
ensuite. Le lien opérationnel et l'adresse IP sont donc deux étapes distinctes.
Le heartbeat de 500 ms est surveillé par un délai noyau de 5 s. La correction
de l'horloge Mach P0.3.1 reste indispensable et doit être conservée.

Les déauthentifications et pertes de session font échouer le moteur ; le
renouvellement EAPOL entraîne actuellement une reconnexion complète. Le
superviseur historique arrête son fils, décharge le cœur et recommence après
5 s. `stopLive` arrête RX/TX, efface les clés et ferme l'interface USB : il ne
suffit pas de relancer un fils pour retrouver une radio passive utilisable.

Les méthodes du client administrateur sont contrôle (0), association (1),
WPA2 (2), réseau (3). Elles partagent le même propriétaire USB, ses verrous
et ses files. Le compagnon de scan passe par `callPlatformFunction`, sans
devenir un second propriétaire. Le scan refuse un propriétaire de contrôle
ou une émission active ; le contrôle refuse un balayage en cours.

Point d’adaptation identifié lors de l’audit : une demande native bornée doit
réserver une cible fraîche (SSID/BSSID/canal/sécurité) et une génération, puis
déclencher **le même** programme de connexion. Le plan initial utilisait le
profil existant ; la décision utilisateur ci-dessous le remplace par la saisie réelle.
Le programme doit vérifier explicitement le BSSID demandé au lieu de prendre
la première balise de même SSID. Aucun second supplicant n'est nécessaire.
La récupération après arrêt impose également une coordination explicite avec
le cycle de vie matériel. Les traces natives et l’implémentation validée
sont décrites dans les sections suivantes.

## Phase B — contrat natif et preuves statiques

Binaires originaux Panther conservés uniquement dans les artefacts ignorés.
`WirelessAssociate` (`0x91f29da0`) sans mot de passe appelle `_airPortExec`
avec l'opération 17, 56 octets de charge utile et aucune sortie. Le transport
ajoute un octet d'opération : entrée totale 57. Mots 16 bits big-endian à +0
(mode), +2 (longueur de clé), +20 (authentification), SSID à +22 dans un tampon
initialement nul. Aucun BSSID ni canal dans cette variante. Les octets de clé
potentiels ne doivent jamais être journalisés.

`ClientController join:` (`0x2f78`) appelle `WirelessAssociate` avec mot de passe
nul à `0x31b8`. Le chemin `joinWEP:` est distinct ; les essais ci-dessous
confirment son emploi pour NAEBA. La présence du bit Privacy dans le scan ne doit pas être masquée.

Après acceptation, `_waitForJoin` (`0x91f298e8`) interroge `WirelessGetInfo`
jusqu'à 20 fois, et consulte le mot +16 du résultat. Ce mot provient de +54
de la réponse brute 18 : **ce n'est pas le canal**. Valeur 0 : attente puis
erreur `0x88001006` ; 1 : succès ; 2 : `0x88001008` ; 3 : `0x8800100d` ;
4 : erreur générique ; 5 : `0x88001010` ; 6 : `0x88001011` ; 7 : `0x88001012`.
Le pilote Apple écrit à +52 un entier 32 bits de résultat d'association,
dont +54 est la moitié basse PPC big-endian. Ne pas confondre ce résultat
avec le statut de port +2.

`AirPortDriver::getWirelessStatus` (`0x2698`) fournit SSID C à +6 (34 octets),
MAC à +40, BSSID à +46, mode à +4, statut de port HCF FD40 à +2 et résultat
d'association à +52. Le framework ne lit les mesures supplémentaires que si
le statut de port vaut 4. La sémantique complète des mesures doit rester
indisponible tant qu'elle n'est pas caractérisée.

`WirelessDisassociate` sur cette famille de contrôleurs envoie aussi 17 avec
56 octets nuls, authentification 1 à +20 et quatre octets FF à +22. Une autre
famille de référence emploie 29 ; ne pas généraliser à notre façade.

## Contrat observé et décision du 4 novembre 2026

Le clic natif NAEBA ouvre `APPasswordDialog` avant tout appel 17. Le menu
teste le bit Privacy 0x10 du tag à `networkSelectedCallback:` (`0x655c`),
construit un dictionnaire DialogName/NetworkName/Raw SSID/JoinType et le passe
directement à `ClientController execute:`. Il ne publie pas ici une demande
de sélection que le pilote pourrait consommer avant la fenêtre.

Avec la valeur publique de test `p032test`, sans ajout au trousseau,
SystemUIServer émet réellement 17 : entrée 57, sortie 0, mode 1,
longueur de clé 13, authentification 2, SSID hexadécimal 4e41454241.
L'instrumentation refuse encore cette requête. Aucune association n'est
revendiquée par cet essai.

Le périmètre a été étendu à la demande de l'utilisateur : le mot de passe
saisi doit alimenter réellement WPA2. Aucun repli vers la PMK du profil
historique n'est autorisé dans le mode natif. Le chemin `--config` historique
reste disponible séparément et inchangé.

Le parseur borné `airport_join.c` conserve uniquement opération/SSID/type de
fenêtre, refuse tailles, modes, authentifications ou chaînes ambiguës et
reconnaît la sentinelle de déconnexion. Le sélecteur exige SSID exact,
RSN PSK/CCMP, infrastructure, absence de PMF obligatoire et observation fraîche.
Le mode natif privilégie le canal courant, puis le BSSID lexicographiquement
le plus petit parmi les candidats compatibles. Cette politique déterministe
évite un changement de canal inutile ; aucune mesure RSSI n’est inventée.
Le premier essai sur le BSSID du canal 1 avait expiré sans réponse à
l’authentification ; le BSSID observé sur le canal 11 répond et permet WPA2.

## Saisie réelle du mot de passe : piste native vérifiée

`WirelessAssociate` passe la saisie ordinaire à `WirelessCreateEncryptedKey`
(`0x91f2a0ec`). Ce dernier effectue une dérivation MD5 et ne renvoie que 13
octets pour le chemin observé : ce résultat ne permet pas de retrouver une
passphrase WPA2 arbitraire ni sa PMK liée au SSID.

En revanche `_leapPW` (`0x91f29744`) reconnaît une chaîne qui commence par
`<` et finit par `>`. `WirelessAssociate` retire ces deux caractères, envoie
le texte terminé par NUL dans la commande 26, puis une commande 17 de mode 1,
longueur de clé 5, authentification 3. Le test natif du 4 novembre à 10:33:44
avec la seule valeur publique `<p032test>` confirme : commande 26 entrée 10
(opération + huit caractères + NUL), sortie 0 ; puis 17 entrée 57, sortie 0,
mode 1, longueur 5, auth 3, SSID NAEBA. Aucun contenu de la commande 26 n'est
journalisé. Pendant cette première caractérisation, les commandes étaient
encore refusées. Le raccordement décrit ci-dessous est désormais implémenté.

L'utilisateur a accepté la convention `<mot-de-passe>`. Le transport reprend
cette branche historique du framework ; le protocole radio reste WPA2-PSK/CCMP.
Aucune authentification LEAP ou WEP n'est implémentée.

## Architecture du prototype 0.15.2 / 0.3.2

Le client natif conserve au plus 63 octets ASCII imprimables pendant 10 secondes.
La commande 26 exige 8 à 63 caractères suivis d'un seul NUL. La commande 17
consomme cette saisie sur **le même client**, même si la sélection est refusée.
Le temporisateur, la fermeture du client et toute nouvelle saisie effacent le
précédent tampon. Les octets de commande 26 copiés par Mach sont effacés avant
le retour. Ni registre IOKit, ni journal, ni préférences ne contiennent le secret.

Le compagnon réserve une cible fraîche auprès de l'unique propriétaire USB.
Le cœur attribue une génération, interdit les scans et les propriétaires de
contrôle historiques pendant cette réservation, et vérifie de nouveau BSSID
et canal à `DWA_ASSOC_BEGIN`. Une réservation expire après 15 secondes si
l'association n'a pas pris le relais du temporisateur. Les états internes sont :

| Valeur | État | Preuve ou transition |
| --- | --- | --- |
| 0 | Idle | Aucune demande native |
| 1 | Requested | Cible compatible et fraîche réservée |
| 2 | Prepared | Canal réglé par le propriétaire USB |
| 3 | Running | Client natif exclusif attaché ; auth/assoc/EAPOL dans les journaux du moteur |
| 4 | Connected | `networkAliveLocked` : handshake vérifié, CCMP autorisé et heartbeat vivant |
| 5 | Failed | Arrêt du moteur, expiration ou perte du lien |
| 6 | Disconnected | Annulation ou fin de nettoyage |

Ces valeurs privées ne sont pas les codes Apple80211. DHCP reste une étape
postérieure à Connected ; son succès doit être prouvé par l'adresse IPv4 et le
trafic. La correction d'horloge P0.3.1 est conservée.

`dwa121-airport-broker` est un processus root au premier plan. Son client IOKit
privé est exclusif ; il n'ouvre pas l'interface USB. `POLL` renouvelle son bail
et ne contient jamais le secret. `TAKE` exige la génération courante, prépare
le canal et remet une seule fois la saisie, puis efface le tampon du compagnon.
Le bail du courtier expire après cinq secondes sans appel : la saisie est effacée
et l'annulation est exécutée par le temporisateur du compagnon. Une génération
ancienne ne peut ni prendre ni annuler une nouvelle demande.

Le courtier lance **le client existant** `dwa121-connect --airport GENERATION
SSID_HEX` et remet le mot de passe par un pipe anonyme. Il exige un exécutable
et des répertoires parents root, sans écriture groupe/autres, désactive les
core dumps, efface sa copie et ne conserve aucun profil. Le client dérive la
PMK avec la fonction WPA2 existante, ouvre le type natif exclusif du même cœur,
lit sa cible par la méthode privée 4 et vérifie génération/SSID/BSSID/canal.
Authentification, association, EAPOL, installation des clés, CCMP, anti-rejeu,
DHCP et heartbeat réutilisent le moteur précédent.

L'ABI privée est bornée : requête de courtier 16 octets, cible 60 octets,
réponse de courtier 128 octets. Les entiers sont natifs PPC32 big-endian,
les SSID/BSSID/secrets sont des octets ; aucun pointeur ne traverse IOKit.
Le secret est présent uniquement dans une réponse TAKE administrateur.
Les autres réponses ont leur zone de secret initialisée à zéro.

## Arrêt et récupération

L'arrêt du client ferme la session réseau et efface les clés avant fermeture
USB. Le courtier attend la fin de son fils puis annule la génération ; le cœur
peut ensuite réinitialiser le matériel après vérification de l'arrêt précédent.
Le démarrage matériel est réutilisé sans rappeler le cycle de vie IOService
ni republier les interfaces existantes. La calibration s'effectue sur le thread
du courtier ; les commandes natives de sélection retournent Busy au lieu
d'attendre le verrou matériel. La réception passive et le scan reviennent
ensuite. Cette récupération a été observée après un échec WPA2 réel.

Le changement direct de réseau actif est refusé. Après un échec ou une
fermeture du supplicant, aucune reconnexion utilisant un ancien secret n'est
lancée : le secret a été consommé et une nouvelle sélection est nécessaire.
Le chemin historique garde sa politique de reconnexion propre.

## Retour à Panther et limites

La commande 18 publie SSID/BSSID et résultat d'association 1 uniquement si le
réseau WPA2 est réellement autorisé ; pendant l'attente le résultat vaut 0,
après échec/déconnexion il vaut 4. Le statut de port reste 0 (inconnu), faute de
contrat complet pour les requêtes de mesures supplémentaires. Aucun RSSI,
canal dans un champ non identifié, ni succès anticipé n'est fabriqué. Le relais
SystemConfiguration conserve pour l'instant les notifications radio P0.2.1 ;
les notifications détaillées d’association restent à compléter. Après succès,
la fenêtre se ferme sans erreur, mais le menu ne garde pas le SSID/la coche :
le scan refusé pendant WPA2 rend la liste vide. Le trafic reste opérationnel. Le framework attend au maximum 20 interrogations espacées de 100 ms
(constante 00000000/000186a0 à 0x91f2bfe4), soit environ deux secondes : le délai
provoquait une erreur visuelle malgré le succès WPA2 réel. Le wrapper
`ClientController joinWEP:` (0x36d4) lance `threadedWirelessJoinWEP:` (0x3288)
via `NSThread detachNewThreadSelector:toTarget:withObject:`. La commande 17
attend donc maintenant sur ce thread appelant, hors de tout verrou et de toute
workloop, le résultat réel de la génération acceptée (sondage 50 ms, maximum
15 secondes). Le relais indépendant continue la négociation ; un échec ou
une annulation termine aussi l’attente, et une expiration demande le nettoyage.
Le test matériel utilise deux threads pour vérifier TAKE/CANCEL pendant
cette attente, sans envoyer de trames d’authentification.

Open, WEP, TKIP, LEAP radio, trousseau et commande 12 restent hors périmètre.
Dans la fenêtre native laisser **Add to Keychain décoché**. Une saisie ordinaire
sans chevrons est refusée ; aucun profil enregistré ne la remplace. Un résultat
de scan âgé de 15 secondes ou plus est refusé : rouvrir le menu pour découvrir
à nouveau le réseau. La validation USB 1.1 PowerBook reste distincte des essais
EHCI VM. Les binaires Apple restent inchangés et ne sont pas distribués.
