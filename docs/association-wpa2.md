# Association et WPA2 — cœur utilisateur expérimental

Ce composant ajoute une machine d’association 802.11b/g et le moteur RSN de **wpa_supplicant 2.12**, raccordés par un coordinateur de session. Le coordinateur est testé avec un point d’accès OpenSSL simulé. La [sonde matérielle v0.13](wpa-hardware.md) utilise désormais directement l’adaptateur WPA2 avec la station et le transport IOKit : l’échange initial sur NAEBA, la vérification de M3, l’émission de M4, l’installation/relecture et l’effacement des clés ont réussi sous Panther. Le mode de diagnostic conserve le port de données fermé. Le coordinateur de connexion persistante, le trafic CCMP et IP restent à raccorder ; aucune connexion réseau utilisable n’est encore revendiquée.

Les versions antérieures validaient séparément la CAM (v0.8), le canal RX (v0.9), l’association (v0.11) et l’extraction EAPOL (v0.12).

## Composants

Depuis la v0.12, `dwa_session_receive_80211` reçoit les trames complètes issues
de la file IOKit et leur âge, après vérification FCS par le pilote. Le pont
`src/net80211/eapol.c` vérifie les trois adresses, la direction FromDS, les
longueurs EAPOL et Key Data, la version du descripteur et l'absence de fragments,
de protection ou d'A-MSDU. Il extrait un paquet emprunté de 99 à 2048 octets,
avec ou sans QoS, puis le livre au supplicant pendant cet appel synchrone.
Les trames trop anciennes et EAPOL avant association sont refusés.
La couche CCMP pour les messages protégés reste à ajouter.

La même extraction sert à la sonde matérielle `dwa121-associate SSID --require-m1`.
Elle contrôle la syntaxe de M1 (flags, longueur de clé CCMP, nonce non nul,
champs réservés/MIC nuls et longueurs des éléments optionnels), sans afficher
les données de clé ou le nonce. **Ce classement ne valide ni MIC ni identité :
M1 n'est pas authentifié.** Le compteur `M1` ne remplace pas un échange WPA2.
Les tests de session passent désormais M1/M3 dans de vraies enveloppes 802.11
synthétiques, vérifient M2/M4 avec OpenSSL et testent les répétitions de M3 sans
réinstallation. Ces tests ne prétendent pas émettre sur le matériel.

`src/net80211/station.c` sélectionne une balise récente (au plus 5 secondes), un SSID exact, un BSSID unicast et un canal 1–11 explicite. La balise doit annoncer un réseau infrastructure avec PSK et CCMP ; PMF obligatoire est refusé. Les débits de base inconnus sont refusés. Le canal annoncé doit correspondre au canal d’écoute. Les réseaux masqués, HT, WPA/TKIP, WPA3 et itinérance ne sont pas implémentés.

Le moteur produit l’authentification ouverte puis la requête d’association avec un élément RSN négocié PSK/CCMP, sans recopier toutes les suites annoncées par le point d’accès. Il vérifie les adresses, le type, les bornes, le statut, les débits et l’AID des réponses. Les réponses d’authentification ouverte peuvent comporter des éléments optionnels après les 30 octets fixes ; les TLV tronqués sont refusés, les éléments complets sont ignorés. Les essais de gestion sont limités à trois émissions espacées d’une seconde, avec le même numéro de séquence et le bit Retry. Une association réussie passe à l’attente des clés ; elle n’autorise pas les données.

`src/supplicant/wpa2.c` adapte le cœur RSN upstream. Le PMK est fourni comme 32 octets ou dérivé d’une passphrase de 8 à 63 octets par PBKDF2-HMAC-SHA1. Le SNonce utilise `/dev/urandom` via `os_get_random` ; aucun générateur déterministe de test n’est compilé. Les journaux upstream sont désactivés à la compilation, y compris les dumps de clés. L’adaptateur est limité à RSN, PSK, CCMP, descripteur EAPOL version 2 et au BSSID sélectionné. Il contrôle les longueurs EAPOL et impose un délai initial de 10 secondes.

Les callbacks d’installation de clés doivent confirmer le succès matériel avant de retourner. Le trafic n’est autorisé qu’après confirmation du supplicant et installation des clés pairwise et group. Une erreur d’émission, de clé ou d’ouverture du trafic ferme la session. La préinstallation facultative `KEY_FLAG_NEXT` de hostap 2.12 est refusée : notre contrat ne possède pas d’opération d’activation distincte. Cela évite de transformer cette optimisation en deux installations réinitialisant les compteurs CCMP. Les retransmissions M3 et GTK sont traitées par les protections upstream. Le renouvellement pairwise PTK0 impose une nouvelle connexion ; le renouvellement GTK est pris en charge par le moteur.

`src/supplicant/session.c` relie les deux machines : préparation du transport → authentification → association → configuration BSSID/AID → EAPOL → autorisation. EAPOL est refusé avant l’association. Déconnexion, rejet, erreur du backend et expiration arrêtent le transport et demandent l’effacement des clés. Une nouvelle connexion exige un nouvel objet. Les callbacks sont synchrones, bornés, ne doivent pas rappeler le coordinateur ni le détruire pendant leur exécution, et doivent copier les buffers empruntés. Le processus propriétaire doit initialiser et exécuter `eloop` sur un seul thread.

## Portage Panther

Le cœur utilise la cryptographie interne de hostap ; il ne dépend pas de l’ancien OpenSSL système pour la production. OpenSSL sert uniquement d’implémentation indépendante dans le faux point d’accès des tests.

Un premier test PowerPC a trouvé une dérivation PMK incorrecte. Panther définit `BYTE_ORDER=BIG_ENDIAN=4321`, alors que GCC définit aussi `_BIG_ENDIAN=1` comme indicateur d’architecture. Le code Apple upstream attend les constantes modernes `_BYTE_ORDER`, `_LITTLE_ENDIAN` et `_BIG_ENDIAN`. Un correctif dans `src/utils/common.h` utilise les anciens noms lorsque `_BYTE_ORDER` manque. Le contrôle de compilation `platform.h` refuse les déclarations ambiguës ; le test vérifie aussi les octets d’une conversion réseau et compare le PMK à OpenSSL. Aucun algorithme cryptographique upstream n’est modifié.

La source officielle, ses empreintes et le seul fichier modifié sont consignés dans [wpa-upstream.json](wpa-upstream.json). Le correctif reproductible est `vendor/patches/hostap-2.12-panther-endian.patch`. Les notices BSD et les attributions des primitives sont conservées dans l’arbre upstream.

## Tests et reconstruction

```sh
# Hôte, bibliothèque et tests instrumentés
WPA_EXTRA_CFLAGS='-fsanitize=address,undefined -g' \
  WPA_BUILD_DIR=artifacts/wpa-core-2.12-sanitized sh scripts/test-wpa.sh

# Panther, compilation GCC 3.3 puis exécution native PowerPC
caffeinate -i sh scripts/build-wpa-vm.sh
```

Les tests vérifient les messages d’association, les réponses mal adressées ou tronquées, les débits et suites refusés, les retransmissions bornées et la fermeture du trafic. Le faux point d’accès calcule indépendamment avec OpenSSL PBKDF2, la dérivation PTK, les MIC et AES Key Wrap. Il vérifie M1–M4, TK/GTK, le mauvais MIC, un Key Wrap corrompu mais signé, les rejouements, les répétitions M3/GTK sans réinstallation, le renouvellement GTK, les erreurs de callbacks, le mauvais PSK et son expiration. La suite complète traverse aussi le coordinateur association → WPA2 et teste une désauthentification.

Les paramètres et clés du faux point d’accès sont des données de test publiques. Aucune clé utilisateur n’est demandée ou utilisée par ces tests. Les résultats natifs et les empreintes de l’archive sont consignés dans `artifacts/validation-wpa2-core.json`.

## Raccordement matériel restant

La sonde v0.13 raccorde EAPOL TX/RX et CAM à l’adaptateur WPA2 avec un port de données fermé. L’API `dwa_session_ops` définit encore les obligations du backend de connexion persistante. Pour passer du diagnostic à une interface réseau utilisable, il faut :

1. Généraliser le backend borné de la sonde au coordinateur, avec politique pays/canal, erreurs USB, reconnexion et renouvellement des clés. DATA puis ZLP reste à valider sur le matériel ; les trames réelles utilisées jusqu’ici ne nécessitent pas de ZLP.
2. Raccorder durablement la file RX à `dwa_session`, avec ordonnancement des délais et sélection de session. Le pont de décapsulation est testé et utilisé par la sonde ; la connexion persistante reste à implémenter.
3. Compléter l’état associé et les filtres RX, préserver les compteurs PN/RSC et valider le chiffrement/déchiffrement des données. La CAM est désormais raccordée au diagnostic et vérifiée sur de vraies clés ; la sécurité matérielle reste désactivée dans ce mode.
4. Implémenter le port de données, la conversion Ethernet/802.11, l’interface réseau Panther et DHCP.
5. Après l’échange initial réel validé sur NAEBA, tester le trafic chiffré, la perte radio, les délais sur canal silencieux, le retrait USB, la veille et l’USB 1.1 physique.

Une réussite du faux point d’accès ne remplace aucune de ces validations matérielles.
