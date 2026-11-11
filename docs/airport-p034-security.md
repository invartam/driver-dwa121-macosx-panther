# P0.3.4 — contrôle d'accès

Base : `494a7b5`. Ce document décrit le correctif en cours, pas une release
qualifiée. Les résultats effectivement exécutés sont consignés séparément.

## Défaut et frontière

P0.3.3 accepte les mutations du type IOKit 12 sans autorisation. La commande
17 de déconnexion contourne notamment la présence du broker : elle efface
le secret en attente et programme une annulation dans le moteur. La commande
8 modifie temporairement le canal radio ; 26 conserve un secret en mémoire.
Le contrôle d'accès du broker seul ne protège donc pas ces opérations.

La politique P0.3.4 autorise root et l'utilisateur de la session console
active, avec UID réel égal à l'UID effectif pour ce dernier. Elle ne prétend
pas isoler SystemUIServer des autres programmes de ce même utilisateur.
Un programme du compte console peut toujours demander une déconnexion.
L'identité du binaire, son nom, son PID et son chemin ne servent pas de preuve.

## Contrat noyau

Le client retient le `task_t` d'ouverture avec `task_reference`, puis le
libère avec `task_deallocate`. Chaque mutation exige que `current_task()`
soit cette tâche. Un transfert du droit Mach n'accorde donc pas le droit
de mutation à la tâche destinataire, y compris si elle est root.
Les lectures restent accessibles par une référence transférée.

Les identités courantes viennent de `current_proc()` et des credentials BSD
lus sous `pcred_readlock`. La session est tirée de `IOConsoleUsers` à la
racine du registre noyau : UID, `kCGSSessionOnConsoleKey` et
`kCGSSessionIDKey`. L'UID et le numéro de session sont mémorisés à l'ouverture,
puis comparés au moment de la mutation. Le client retient aussi l'objet
`IOConsoleUsers` publié à l'ouverture et exige la même identité d'objet.
Chaque republication invalide ainsi les anciens clients, même si l'UID et
le numéro de session redeviennent identiques. Conserver cette référence
empêche la réutilisation de son adresse pendant la vie du client. Un échec d'identité/session révoque
définitivement ce client ; fermer puis rouvrir est nécessaire. Une fermeture
révoque également le client et efface le secret en attente.

Panther ne définit pas `kIOClientPrivilegeConsoleUser`. Son privilège
`kIOClientPrivilegeLocalUser` compare l'UID à toutes les sessions du registre,
sans exiger la session au premier plan. Le correctif ne l'utilise donc pas.
Référence : [IOUserClient.cpp, XNU 517.12.7](https://github.com/apple-oss-distributions/xnu/blob/xnu-517.12.7/iokit/Kernel/IOUserClient.cpp).
Les en-têtes 10.3.9 et la table réelle de la VM confirment les API/champs.

Un client sans session console à l'ouverture peut lire, mais ne gagne pas
de droits utilisateur par une connexion ultérieure. Root reste une autorité
explicite, vérifiée à chaque appel ; il n'est pas soumis à la session console.
Le broker conserve son ouverture exclusive et ses contrôles root à chaque
RPC ; il exige désormais aussi la tâche d'ouverture. Aucun jeton partagé,
nouveau socket ou secret permanent n'est introduit.

## Matrice native

| Commande | Effet | Autorisation |
| --- | --- | --- |
| 3 | Qualité indisponible, réponse bornée de 12 octets | Lecture publique |
| 8 | Démarre le scan passif, modifie les canaux | Tâche ouvrante + root ou session console liée |
| 9 | Lit le résultat du scan du client | Lecture publique |
| 12 | Écriture radio non implémentée | Même contrôle, puis Unsupported |
| 13 | État réel de la radio | Lecture publique |
| 17 | Sélection/annulation de connexion | Tâche ouvrante + root ou session console liée |
| 18 | État réel d'association et identité | Lecture publique |
| 26 | Secret provisoire pour la sélection native | Tâche ouvrante + root ou session console liée |

Les commandes inconnues restent non implémentées. L'ouverture type 12 seule
n'accorde aucune mutation. La saisie ordinaire WEP reste refusée ; les chevrons
transportent la passphrase WPA2 sans ajout de prise en charge WEP/TKIP.

## Limites à vérifier et à ne pas masquer

Le contrôle compare la session et sa publication au moment de chaque
mutation. Une ancienne référence doit échouer après une republication de
la table, y compris un aller-retour vers une autre session sans appel
intermédiaire. Ce choix échoue fermé : une republication sans changement
d'utilisateur exige aussi une nouvelle ouverture pour les mutations.
La compatibilité du menu avec cette révocation doit être testée.
Le même UID reste dans la frontière de confiance et root peut modifier le
système. Les appels déjà acceptés ne sont pas rétroactivement annulés par
ce seul contrôle. La validation doit mesurer ces comportements avant toute
publication ; aucun résultat de test n'est déduit de la seule lecture du code.

La publication de cette table passe par `IOResources::setProperties`,
qui exige le privilège root avant de remplacer la propriété et incrémenter
son compteur. Référence : [IOService.cpp, XNU 517.12.7](https://github.com/apple-oss-distributions/xnu/blob/xnu-517.12.7/iokit/Kernel/IOService.cpp).
Le client ne peut pas fournir lui-même cette identité interne.


Les clients privilégiés du moteur retiennent eux aussi leur tâche ouvrante.
Les cinq méthodes vérifient cette tâche et le privilège root courant.
La fermeture par une autre tâche n'a pas d'effet ; clientDied conserve le
nettoyage noyau. Les transferts réels du broker et du client moteur vers une
autre tâche root ont été testés : RPC refusée, fermeture distante sans effet,
propriétaire encore utilisable. Les résultats du paquet installé sont dans
le rapport de tests. Le même UID console reste explicitement autorisé.
