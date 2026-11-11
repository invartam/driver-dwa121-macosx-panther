# DWA-121 AirPort P0.3.4 — Panther PPC Preview

**Candidat validé en VM Panther le 11 novembre 2026 ; publication GitHub en attente.**
La qualification physique du PowerBook G3 USB 1.1 reste à effectuer après
validation et publication. Les essais VM EHCI ne la remplacent pas.

## Installation prévue

Mac OS X **10.3.9 PowerPC**, compte administrateur, DWA-121 **A1**
(2001:3308), au moins 64 Mio libres. Conserver une copie de l'installation
stable 0.15.0 et un accès local au Mac. Aucun Xcode, Python, Homebrew ou
téléchargement complémentaire ne doit être nécessaire sur le PowerBook.

Après publication seulement, télécharger depuis la préversion GitHub
`airport-p034-preview1` le DMG, `SHA256SUMS` et les instructions. Sur un Mac
récent, dans le dossier de téléchargement, vérifier :

```sh
shasum -a 256 -c SHA256SUMS
```

Transférer ensuite le DMG vérifié vers le PowerBook. L'archive TGZ est une
alternative contenant le même bundle PKG complet. Ne pas copier seulement
un fichier interne du package.

Ouvrir le DMG, puis `DWA121-AirPort-P0.3.4-Panther-PPC.pkg` avec Installer.
Choisir le volume système démarré. L'installation doit laisser le réseau
stable actif : elle n'active aucun pilote et n'installe aucun démarrage
automatique. La clé peut être absente pendant l'installation.

## Activation et connexion

Dans `/Applications/DWA121 AirPort Preview`, ouvrir `Activer.command`.
Le mot de passe administrateur autorise l'opération locale. L'activation
arrête explicitement le superviseur stable si celui-ci est propriétaire du
pilote, puis charge le prototype. Ne pas lancer les deux superviseurs.

Le menu AirPort peut nécessiter le rafraîchissement de SystemUIServer après
le premier chargement. Le lanceur d'activation doit effectuer ce
rafraîchissement dans votre session. La barre de menus disparaît brièvement.

Choisir le réseau WPA2 dans AirPort et saisir **`<mot-de-passe>`**, chevrons
compris, dans la fenêtre historique WEP. Laisser **Add to Keychain décoché**.
La transmission utilise ce texte pour WPA2-PSK/AES-CCMP ; aucune connexion
WEP/TKIP ou réseau ouvert n'est prise en charge. Une saisie sans chevrons
est refusée. Le secret n'est pas enregistré par le prototype.

Vérifier la coche native et ouvrir `Etat.command`. Une coche confirme l'état
d'association, pas l'accès Internet : vérifier aussi DHCP, DNS et un transfert
réel. Le débit radio reste fixé à 1 Mbit/s ; les barres ne sont pas une mesure
RSSI disponible. Après une rupture, une nouvelle saisie peut être nécessaire.

## Diagnostic hors ligne

`Diagnostic.command` recueille l'état du projet sans changer la connexion.
Il affiche le chemin d'une archive protégée dans `/var/db/dwa121-airport`.
La copier avec les droits administrateur pour la transmettre. Elle contient
les versions, l'état USB/radio/association, SSID/BSSID, compteurs disponibles,
journaux récents du projet et un résumé des références du projet dans les
panics. Les profils Wi-Fi, passphrases et clés ne sont pas collectés.

## Désactivation et retour arrière

Ouvrir `Desactiver.command`. Le programme arrête le broker et son supplicant,
retire uniquement le service réseau du prototype et tente un déchargement
normal des extensions. Si le déchargement échoue, suivre le message demandant
un redémarrage ; ne pas forcer le noyau. Le démarrage automatique expérimental
est retiré et le superviseur stable est relancé s'il fonctionnait avant
l'activation. Les autres interfaces et les profils personnels sont préservés.

`Restaurer.command` fait également cela et restaure les fichiers d'une
installation précédente du prototype si un remplacement a été sauvegardé.
Les préférences sont restaurées par retrait du seul service appartenant au
prototype. Si d'autres réglages ont changé entre-temps, ils sont conservés ;
la sauvegarde protégée reste disponible pour comparaison.

En cas de problème du Finder ou sans Wi-Fi, ouvrir Terminal :

```sh
sudo /usr/local/libexec/dwa121-airport/0.3.4/manage.sh rollback
```

`Desinstaller.command` retire les fichiers du prototype après désactivation,
en conservant les sauvegardes protégées et la stable. Aucun accès Internet
n'est nécessaire au retour arrière. Ne pas utiliser le désinstalleur de la
stable pour retirer cette préversion.

## Démarrage automatique facultatif

Après validation des premiers essais, `Activer-au-demarrage.command` installe
le StartupItem Panther. Le StartupItem stable est déplacé dans une sauvegarde
protégée pour éviter deux propriétaires USB concurrents. Au démarrage, le
prototype attend une saisie native du mot de passe après ouverture de session.
`Desactiver-au-demarrage.command` retire cette option et restaure le StartupItem
stable. L'installation initiale n'active jamais cette option.

## Limites de sécurité

Les mutations sont permises à root et au compte de la session console active.
Un autre programme de **ce même compte** peut donc aussi demander une
déconnexion. Cette version ne prétend pas distinguer fortement SystemUIServer
de tous les processus du même UID sous Panther. Les autres UID et les tâches
recevant une référence transférée sont soumis aux contrôles noyau documentés.
