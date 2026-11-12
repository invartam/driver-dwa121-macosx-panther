# Diagnostic du démarrage Lombard — 12 novembre 2026

PowerBook G3 Lombard, Panther 10.3.9 7W98, DWA-121 USB 1.1. Accès SSH
administrateur autorisé par l'utilisateur. Ethernet et Wi-Fi partagent le
LAN : une session SSH ou un ping non lié à l'interface Wi-Fi ne valide pas
le trafic sans fil. Les essais de cette investigation portent sur le
chargement et l'énumération USB, pas sur une nouvelle authentification WPA2.

## Résultats établis

- Scripts publiés comparés avant modification : identiques au payload testé.
  Instrumentation c175a4f posée sur la machine avec sauvegarde protégée,
  permissions et empreintes vérifiées. Aucun changement des binaires/kexts.
- Premier redémarrage distant : écran noir rapporté par l'utilisateur,
  Ethernet inaccessible. Une entrée d'accueil SystemStarter est néanmoins
  présente avant le redémarrage manuel : pas de preuve d'un blocage limité
  au chemin d'arrêt USB.
- Après relance manuelle : activation automatique complète à 09:36:55,
  résultat 0. Cœur, compagnon, sonde native, service SC et runtime réussissent.
  L'utilisateur confirme carte et NAEBA visibles sans réactivation manuelle.
  L'échec initial du 11 novembre après calibration n'est pas reproduit ici.
- Essai suivant : arrêt du runtime et déchargement normal des deux kexts,
  sans erreurs RX, puis redémarrage avec le StartupItem conservé. L'activation
  échoue à usb-check. OHCI rapporte des erreurs d'attribution d'adresse USB
  e0004051 avant le StartupItem ; après démarrage, la clé demeure absente
  du registre, diagnostic USB résultat 2. Cela ne peut pas être corrigé par
  une attente de calibration puisque le pilote n'est pas encore chargé.
- Rebranchement physique : diagnostic USB résultat 0 avant chargement du
  projet. Relance contrôlée du chemin boot-activate : toutes les étapes
  réussissent, résultat 0, radio active, aucune association annoncée.
- Témoin : prototype déchargé, démarrage automatique retiré, aucun StartupItem
  DWA-121 stable présent. Rebranchement physique puis redémarrage sans kext du
  projet chargé : bureau atteint, clé USB présente (résultat 0), aucun kext
  DWA-121 chargé. SSH a répondu tardivement à la même adresse Ethernet ;
  les délais de ping/SSH ne doivent pas être assimilés à une panne USB.

L'absence de la clé et les erreurs OHCI sont cohérentes mais les traces
MakeDevice ne nomment pas à elles seules le périphérique visé. Le
rebranchement rétablit l'énumération, sans distinguer état du périphérique,
contrôleur/hub ou alimentation. Le simple déchargement préalable n'a pas
suffi dans l'essai défaillant. Un arrêt plus complet du StartupItem pourrait
améliorer la symétrie démarrage/arrêt, mais n'est pas une correction démontrée.

## Investigations séparées

Le watchdog RX de 15:42:05 le 11 novembre et l'échec de récupération radio
ne prouvent pas la cause du refus initial du StartupItem. L'alternance
Link Status 0/4 du relais n'établit pas non plus des ruptures WPA2 réelles.
Aucun de ces problèmes n'est déclaré corrigé par l'instrumentation.

Les logs bruts et les détails réseau/matériels restent sous artifacts,
non versionnés. La préversion publiée n'est pas remplacée et son avertissement
sur le démarrage automatique reste applicable.

## Comparaison contrôlée du 12 novembre

| Essai | Conditions avant redémarrage | Résultat USB après démarrage |
| --- | --- | --- |
| Témoin 1 | Rebranchement, aucun kext ni StartupItem DWA-121 actif | Présent, résultat 0 |
| Témoin 2 | Second reboot consécutif, toujours aucun kext du projet | Présent, résultat 0 |
| Cœur seul | Chargement 0.15.3, RX sain, déchargement normal, aucun compagnon/broker/WPA2 ni StartupItem | Absent, résultat 2 ; nouvelles erreurs OHCI d'adressage |

Le cycle du cœur suffit donc à reproduire l'échec dans cette comparaison ;
le compagnon et WPA2 ne sont pas nécessaires. Cela ne désigne pas encore
une écriture fautive, ni une phase unique entre initialisation, RX passif et
quiescence. Aucune modification de registre n'est justifiée à ce stade.

Après une longue interruption, l'utilisateur a signalé une veille. Les logs
la situent à 10:16:22, avec réveil à 20:17:47, après l'échec USB mesuré à
10:12–10:13. Elle ne peut donc pas expliquer ce dernier résultat. `pmset -g`
indique déjà `sleep 0` ; aucun réglage d'énergie n'a été changé.

## Outil expérimental USB

`tests/usb_reset_probe.c`, non inclus dans le paquet : UID effectif root,
exactement une clé 2001:3308 et aucun service DWA121Diagnostic attaché.
Interface IOUSBDeviceInterface182 et signatures vérifiées dans les en-têtes
natifs Panther ; compilation GCC PPC avec -Wall -Werror réussie, dépendances
IOKit/CoreFoundation/libSystem inspectées. Sur PowerBook avec clé absente,
le mode --descriptor refuse l'opération avec résultat 2, sans reset.

Le mode --reset demande uniquement le reset standard de cette clé, jamais
du contrôleur. Il nécessite une lecture GET_DESCRIPTOR réelle, bornée par
des délais, avant et après ; un retour zéro de ResetDevice ne suffit pas.
L'essai de reset effectif et son effet sur un reboot restent à exécuter après
un nouveau rebranchement. Aucun correctif de production n'est déduit de la
seule compilation de cet outil.

## Résultat du reset ciblé — 12 novembre au soir

Après rebranchement, GET_DESCRIPTOR réel vérifié avant chargement. Cœur seul
chargé avec RX sain, puis déchargé normalement. Dès la mesure avant reset,
USBDeviceOpen réussit mais GET_DESCRIPTOR expire (e0004051, zéro octet).
L'échec des échanges de contrôle est donc observable avant tout reboot ;
l'entrée IORegistry seule est insuffisante comme contrôle de santé.

Le mode --reset initial a correctement abandonné sans reset ni reboot. Un
mode explicite --recover a ensuite été ajouté, gardant une unique cible
2001:3308, l'absence de pilote attaché, l'ouverture sans seize et un seul
ResetDevice. Il vérifie aussi l'identité de l'objet après reset. Le mode
--descriptor autorise désormais une mesure en lecture seule pendant RX,
sans contourner une éventuelle exclusivité d'ouverture.

Essai --recover : descripteur avant reset en timeout, ResetDevice retourne
zéro, mais descripteur après reset en erreur e0004061, zéro octet. Aucun
reboot n'est exécuté. Une nouvelle ouverture ne trouve ensuite plus la clé ;
OHCI rapporte de nouveaux échecs d'adressage. **Le reset USB standard ne
récupère donc pas la clé dans cet essai.** Il ne doit pas être intégré comme
solution ni son code zéro présenté comme réussite.

Compilation PPC native de l'outil modifié réussie, IOObjectIsEqualTo et ABI
182 confirmés dans les en-têtes Panther. Le PowerBook reste accessible par
Ethernet ; le prototype et son démarrage automatique restent désactivés.
La prochaine mesure nécessite un rebranchement : descripteur avant
chargement, pendant RX puis après déchargement, sans nouveau reset ni reboot.
Cela distinguera initialisation/fonctionnement actif et transition d'arrêt.
