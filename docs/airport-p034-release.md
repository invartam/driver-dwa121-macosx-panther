# P0.3.4 — préparation de la préversion

**Aucune release publiée. Critères de publication encore incomplets.**

Branche : `v1/airport-p034`, base `494a7b5`.
Tag prévu : `airport-p034-preview1`.
Titre : `DWA-121 AirPort P0.3.4 — Panther PPC Preview`.
`main` et la release stable v0.15.0 ne sont pas modifiés.

Les candidats sont compilés nativement dans Panther 10.3.9 7W98, GCC 3.3,
PowerPC 32 bits, CPU 750. Moteur 0.15.3, compagnon AirPort 0.3.4. La bibliothèque
WPA2 a été reconstruite à partir de hostap 2.12 et des adaptations Panther,
y compris la correction d'horloge. Aucun ancien binaire n'est renommé.

Runtime prévu : deux kexts, client d'association/supplicant statiquement lié,
broker, relais d'état, superviseur Panther, utilitaire de service SCPreferences,
diagnostic et sonde de statut en lecture seule, comparateur de préférences,
scripts d'activation/restauration/diagnostic, StartupItem facultatif, lanceurs
Finder, notices et licences. Aucun composant Apple, profil ou secret personnel.

Artefacts à produire et qualifier :

- `DWA121-AirPort-P0.3.4-Panther-PPC.dmg` contenant le bundle PKG historique.
- `DWA121-AirPort-P0.3.4-Panther-PPC.tgz` contenant exactement le même bundle.
- `SHA256SUMS`.
- Manifeste de provenance avec commit exact, versions, dépendances et hashes.
- Instructions d'installation et de restauration.

Les empreintes et URL ne seront ajoutées qu'après construction, installation
effective depuis le DMG par Installer.app, tests réseau et retour arrière.
Les échecs RX/transport et les périodes de veille de l'hôte observés pendant
le développement doivent rester dans le rapport de tests. Aucun résultat VM
ne sera présenté comme une qualification PowerBook G3 USB 1.1.
