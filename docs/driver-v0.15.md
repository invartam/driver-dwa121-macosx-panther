# Pilote réseau DWA121 0.15.0

Le paquet installe le module IOKit PowerPC, un propriétaire WPA2/CCMP, un superviseur et un StartupItem Panther. Le propriétaire ouvre une seule connexion administrateur au module. Une interface Ethernet transporte les paquets BSD ; son lien reste inactif jusqu’à l’échange WPA2 initial vérifié. Les adresses, les types Ethernet, les longueurs et les compteurs de paquets CCMP sont contrôlés avant émission et livraison.

Deux files de 32 trames bornent les échanges avec le noyau. La réception CCMP contrôle le MIC et le rejeu avant de livrer un paquet à Panther. EAPOL reste séparé des données BSD. Le propriétaire renouvelle un heartbeat ; après cinq secondes sans renouvellement, le noyau bloque les émissions, vide les files, draine l’USB et efface les clés matérielles. La fermeture du propriétaire suit le même chemin.

Le superviseur charge le module, attend sa publication, lance le propriétaire avec une configuration privée et recharge une nouvelle session après une interruption. Il utilise un verrou `fcntl` détenu par le processus, pas un PID provenant d’un fichier modifiable. Le renouvellement de clés entraîne actuellement une association et un échange WPA2 complets ; il n’est pas transparent.

Les essais ont révélé deux particularités de Panther :

- `ipconfig set` retourne 1 même en cas de succès. Le propriétaire vérifie donc l’obtention d’une adresse IPv4, au lieu de couper la session sur ce seul code. [Source Apple, bootp-110.9, S_set](https://github.com/apple-oss-distributions/bootp/blob/bootp-110.9/ipconfig.tproj/client.c).
- Sur la VM, `open(O_NOFOLLOW)` a suivi un lien symbolique dans le test natif. La lecture de configuration utilise aussi `lstat`, puis vérifie le type et l’identité de l’inode ouvert, son propriétaire, son mode 0600, son nombre de liens et sa taille. Le répertoire installé est réservé à root.

L’initialisation du module est sérialisée avec sa fermeture. Cela évite qu’un déchargement précoce détache un service alors que sa calibration USB continue. Trois arrêts précoces ont été testés sans module résiduel. Lors du retrait USB, les messages de fermeture du fournisseur déclenchent un nettoyage sur la boucle propre au pilote ; le fournisseur est ensuite fermé explicitement. Le déchargement est contrôlé dans `kextstat`, car le code de retour de l’ancien utilitaire ne suffit pas. Le retrait et le rebranchement de la vraie clé dans QEMU ont rétabli automatiquement WPA2, DHCP et le ping. Un retrait ne prétend pas prouver un effacement matériel de clés devenu impossible à relire.

Voir [l’installation](installation-driver.txt). Les rapports et journaux bruts de validation restent locaux dans `artifacts/` et ne font pas partie du dépôt public.

## Limites

Le paquet vise exclusivement Panther 10.3.9 PowerPC et la révision A1 USB 2001:3308. Il exige un réseau visible, WPA2-PSK CCMP, 2,4 GHz, canal 1–11 configuré explicitement. L’émission utilise CCK 1 Mbit/s ; l’interface ne propose ni HT, ni WPA3, ni WPA-Enterprise, ni PMF obligatoire, ni roaming. Les réseaux se configurent avec l’outil fourni, pas avec le menu AirPort.

La validation du paquet réseau complet utilise la vraie clé dans QEMU/EHCI à 480 Mbit/s. Les essais matériels antérieurs sur PowerBook ont validé WPA2, CCMP et une offre DHCP en USB 1.1. L’utilisateur confirme désormais que le paquet réseau complet fonctionne sur ce PowerBook, avec des téléchargements FTP et un débit jugé correct. Cette confirmation est un retour d’usage, sans journaux instrumentés dans le dépôt. La veille/reprise, le renouvellement réel des clés par une box sur une longue session et l’endurance sur plusieurs jours restent à vérifier.
