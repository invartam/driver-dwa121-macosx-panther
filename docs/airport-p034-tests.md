# P0.3.4 — journal de validation

Validation locale du candidat du 11 novembre 2026 ; publication GitHub non effectuée.
Base `494a7b5`, branche `v1/airport-p034`. VM 10.3.9 7W98 PPC, vraie
DWA-121 A1 via EHCI. USB 1.1 physique non validé.

## Préparation

VM arrêtée à la reprise : démarrage du disque existant avec
`scripts/boot-ehci.mjs`. Installation historique 0.15.0 et superviseur actif
vérifiés. Copie protégée des préférences avant modification :
`/var/root/dwa121-p034-backup/preferences.plist`.
L'installation stable et son profil n'ont pas été remplacés.

## Compilation et tests déjà exécutés

- `sh scripts/test-host.sh` : succès (ASan/UBSan et suites existantes).
- Compilation native du compagnon avec GCC Panther : succès.
- Reconstruction complète `build-panther.sh` dans un nouveau répertoire
  `airport-p034-fresh` : succès, bibliothèque WPA2 reconstruite avec le
  correctif d'horloge présent dans les sources transférées.
- Reconstruction du moteur après ajout des contrôles du client privilégié :
  succès. Versions chargées vérifiées : moteur 0.15.3, compagnon 0.3.4.
- Test pur PPC de statut et sonde réelle 13/18/GetInfo2/3 : succès.

Les essais préliminaires utilisaient le moteur P0.3.2 de référence. Les essais
`access-final-*` ont été rejoués avec les nouveaux kexts 0.15.3/0.3.4.
Les binaires anciens ne constituent pas des candidats à la distribution.

## Accès natif

Test reproductible : `build/airport-p02/airport-access-test` dans Panther,
radio déconnectée (le test refuse un état associé).

| Scénario exécuté | Résultat |
| --- | --- |
| `allow`, root | Lecture et mutations permises ; 12 reste non implémentée |
| `allow`, console UID 501, processus distinct du menu | Permis, frontière du même UID explicitement assumée |
| `deny`, UID nobody (4294967294) | Lecture permise ; 8/12/17/26 refusées |
| Requêtes 17 malformées | BadArgument pour autorisé, NotPrivileged pour non autorisé |
| `reject-publish`, UID 501 | Republication de la table console refusée |
| `hold`, UID 501, puis `republish` par root | Ancienne référence révoquée même avec contenu de table identique |
| `transfer`, UID 501 vers une autre tâche du même UID | Lectures permises, mutations refusées, fermeture distante sans effet, propriétaire encore utilisable |

Le test Mach utilise un rendez-vous bootstrap temporaire. Son port de
réception est détruit en fin de test ; une éventuelle inscription résiduelle
Panther ne référence qu'un endpoint mort. Ce mécanisme appartient uniquement
au test et ne fait pas partie du runtime livré.

Particularité constatée et vérifiée dans XNU : `io_service_close` ignore la
valeur de retour de `clientClose`. Le test ne peut donc pas conclure depuis
le seul code de retour utilisateur. Il vérifie les appels du propriétaire
après la tentative distante. Les premiers essais du test ont échoué sur
cette hypothèse et sur la gestion des ports bootstrap après fork ; ils ne
sont pas comptés comme validations.

## Points restant à valider

Le changement de session graphique réel et la publication restent non exécutés.
Les validations effectuées depuis cette première série sont décrites ci-dessous. Les preuves locales sont conservées sous
`artifacts/airport-p034`, ignoré par Git. Aucune réussite n'est extrapolée
à ces étapes depuis les seuls tests d'accès.


## Validation du paquet installé — 11 novembre 2026

Candidat exact : `stage.Llg05F`, construit par PackageMaker natif, installé
par Installer.app depuis le DMG monté en lecture seule. Preflight/postflight
réussis, permissions root vérifiées, stable 0.15.0 restée chargée pendant
l'installation. Un reçu d'essais Copilot préexistait : Installer a traité
l'opération comme une mise à niveau. Ses sauvegardes protégées sont conservées.

Deux défauts de construction ont été identifiés et corrigés avant ce candidat :
`IFPkgFormatVersion=0.1` manquait (BOM introuvable malgré sa présence), puis
le preflight utilisait `stat`, absent de Panther. Les contrôles utilisent
maintenant `find`. O_NOFOLLOW, mktemp -d et trap EXIT ont été vérifiés dans
la VM ; les soupçons d'incompatibilité correspondants n'étaient pas fondés.

Activation via le manage.sh installé : arrêt explicite de la stable, cœur
0.15.3 et compagnon 0.3.4, service SC, broker et superviseur. Sélection Apple,
passphrase native avec chevrons, M2/M4, clés CCMP, coche native, DHCP en2.
**30/30 pings**, DNS NOERROR lié à la source Wi-Fi, **262144 octets TCP par
sens**, route en2 contrôlée avant/après ; SHA1 reçu
`37ef77696fc255bf53b4cdd014b223676f2dc8bb`, SHA256 POST
`2312394bd99545d9de131c24efb781e765ac1aec243f2ed9347597a793a415e9`.

Autre UID sur connexion active : mutations refusées, état/identité inchangés.
Concurrence scan/join refusée sans perte de lien, puis déconnexion native
réussie. Mauvaise clé : quatre M2, aucun M3, déauth reason=15, aucun trafic
autorisé, erreur Apple réelle. Reconnexion valide : handshake, coche/état et
DHCP retrouvés. SIGKILL du seul supplicant : association invalidée et radio
récupérée sans réutilisation du secret. Redémarrage du superviseur/broker
réussi. Option StartupItem activée puis retirée ; StartupItem stable restauré.
Diagnostic livré : archive locale protégée créée, sans profil ni clés.

Désactivation livrée : auxiliaires arrêtés, service propriétaire retiré,
deux kexts déchargés normalement, CFEqual de toutes les préférences réussi.
Stable 0.15.0 relancée, WPA2/DHCP retrouvés, route en2 et **5/5 pings**.

Preuves locales : `installer-success.png`, `installer-compatible-log.txt`,
`installed-activation.log`, `installed-network.log`,
`installed-connected-menu.png`, `installed-deny-live.log`,
`installed-disconnect-concurrency.log`, `installed-wrong-key.png`,
`installed-supplicant-kill.log`, `installed-startup-option.log`,
`installed-diagnostic.log`, `installed-rollback.log`,
`stable-after-installed-rollback.log`.

## Échecs et limites conservés

Les premières séries du 6 novembre ont donné 28/30 pings puis une perte de
session RX/transport et de route. La seconde série partiellement routée en1
est exclue de la validation Wi-Fi. Des veilles prolongées de l'hôte et des
pertes libusb NO_DEVICE/NOT_FOUND ont également interrompu les essais.
La stable a elle aussi échoué à s'initialiser avant rebranchement physique.
Ces incidents ne démontrent pas une cause unique de toutes les pertes RX.
Les diagnostics supplémentaires enregistrent désormais lastError/watchdogs
et le contexte des ruptures, sans changer les délais de sécurité.

Non exécutés sur ce candidat : logout/login graphique réel, reboot avec
StartupItem expérimental activé, installation graphique volontairement sans
clé (les préflights ne l'exigent pas), injection d'échec kext, restauration
de fichiers d'une ancienne préversion empaquetée, endurance sur plusieurs
jours, PowerBook USB 1.1. Les tests de publication d'identité console ne
remplacent pas un logout réel. La republication révoque les anciens clients ;
le même UID console demeure autorisé. Le renouvellement de clés n'est pas
transparent : une nouvelle saisie native peut être nécessaire après rupture.
