# P0.3.4 — préversion expérimentale Panther PPC

Commit des sources runtime : `d4dcaff` (identifiant complet
dans le manifeste). Branche `v1/airport-p034`. `main` et v0.15.0 préservés.

Tag : `airport-p034-preview1`.
Titre : `DWA-121 AirPort P0.3.4 — Panther PPC Preview`.

Pièces jointes : DMG et TGZ du bundle
PKG historique construit par PackageMaker 10.3.9, SHA256SUMS, manifeste
BINARY-PROVENANCE.json, INSTALL-RESTORE.md et TEST-REPORT.md.
Le DMG est exactement celui installé et testé depuis Installer.app.

| Archive | SHA-256 |
| --- | --- |
| DMG | `5e79c78dfdc14b20aa367ab8d2af79628eae52d2b34d6d4c43ba5d671ce99900` |
| TGZ | `73da6e515d3fd91446ba1c52fe693229a46147ec07dd0813007e226bbe7a9a15` |

Sources runtime et fichiers du payload sont listés avec SHA-256 dans le
manifeste. Construction native Panther 10.3.9 7W98, GCC 3.3, PowerPC32 CPU750 ;
moteur 0.15.3, compagnon 0.3.4, hostap 2.12 avec adaptations Panther/horloge.
Dépendances binaires inspectées par file/otool dans COMPONENTS.txt du payload.
Aucun profil, secret personnel ou composant Apple distribué.

Validations : Installer depuis DMG, permissions et installation inactive,
activation explicite, menu/coche, vraie saisie WPA2, CCMP, DHCP/DNS, 30/30
pings, TCP 256 Kio dans chaque sens, autre UID refusé sur lien actif,
concurrence, mauvaise clé, reconnexion, SIGKILL supplicant et récupération,
redémarrage broker, ajout/retrait StartupItem, diagnostic, désactivation et
désinstallation. Préférences restaurées CFEqual, stable retrouvée avec 5/5
pings. Détails et échecs initiaux conservés dans le rapport de tests.

Limites : autres programmes du même UID console autorisés ; pas de test de
logout graphique réel, reboot du StartupItem prototype, injection kext ou
endurance prolongée. PowerBook G3 USB 1.1 non qualifié. RSSI indisponible,
scan refusé pendant WPA2, reprise/renouvellement non transparents.
Le guide embarqué dans le PKG est la version de préparation : conserver les
instructions externes à jour avec le paquet pour connaître la validation.


Publication effectuée le 11 novembre 2026 :
https://github.com/invartam/driver-dwa121-macosx-panther/releases/tag/airport-p034-preview1

Statut GitHub vérifié : prerelease, non draft. Tag sur `a1e1057`.
Toutes les pièces jointes ont été retéléchargées puis vérifiées avec
SHA256SUMS (7 fichiers : succès). Le DMG et le TGZ sont identiques aux
archives VM testées ; aucun binaire n'a changé lors de la publication.
