# Roadmap Janus vers 1.0

Statut : proposition issue de l'[audit technique 0.17](audit-0.17.md).
Principe : les numéros de version indiquent un ordre de livraison, pas des dates
contractuelles.

## Objectif 1.0

Janus 1.0 doit être un langage natif cohérent, sûr et suffisamment ergonomique
pour construire des outils réels, avec une surface cœur stable et une chaîne de
distribution reproductible. La 1.0 ne signifie pas que tout l'écosystème est
achevé ; elle signifie que les contrats annoncés peuvent être maintenus.

Les priorités sont ordonnées ainsi :

1. corriger les décisions de langage qui seraient très coûteuses après le gel ;
2. partager et stabiliser le cœur sémantique utilisé par le compilateur et le
   LSP ;
3. transformer les garanties existantes en gates mesurables ;
4. figer seulement les surfaces éprouvées par des projets aval.

## 0.18 — Rebaseline et pilotage

### Livrables

- produire un inventaire de stabilité 0.18 couvrant toutes les surfaces ajoutées
  depuis 0.8 ;
- remplacer les mentions de limites 0.8 par un état courant, sans effacer les
  rapports historiques ;
- convertir chaque écart 1.0 en issue GitHub avec priorité, dépendances, critère
  de sortie et milestone ;
- publier une matrice `stable-candidate` / `experimental` / `internal-detail`
  pour langage, CLI, manifestes, stdlib, LSP et graphisme ;
- établir les baselines compilation, mémoire et LSP sur les benchmarks actuels
  et Janus Studio ;
- rendre Janus Studio propre sous `janus check --all --deny-warnings` et définir
  son harnais de canari aval.

### Gate de sortie

- aucun écart du rapport de préparation ne reste sans ticket ;
- l'inventaire automatique et les documents décrivent la même surface ;
- les métriques sont produites de façon reproductible mais restent encore non
  bloquantes ;
- Janus Studio réussit check, tests et build release avec la toolchain candidate.

## 0.19 — Emprunts lexicaux sûrs

### Décision de langage

La [spécification des emprunts lexicaux](design/lexical-borrowing.md) fixe le
modèle cible pour l'implémentation 0.19. Elle couvre :

- emprunt immuable et emprunt mutable exclusif d'une valeur propriétaire ;
- durée lexicale inférée et interdiction d'échapper à la portée ;
- projection sûre des champs ;
- invalidation lors de `move`, `delete`, `remove`, `set`, `replace` ou
  réallocation d'un conteneur ;
- règles de passage aux fonctions et de capture par closure ;
- interaction avec `defer`, branches, boucles, `Option`, `Result` et panique ;
- diagnostics et suggestions de migration.

Les retours empruntés, durées de vie explicites publiques, captures asynchrones
et emprunts stockés sont différés après cette première version.

### Implémentation

- [x] ajouter l'analyse de régions lexicales au suivi de propriété existant ;
- [x] fournir `Slice[T]` et `MutableSlice[T]` pour l'accès contigu emprunté, en
  lecture puis en mutation exclusive ;
- [x] permettre de transmettre l'emprunt à plusieurs couches d'appel sans
  transfert de propriété ;
- couvrir aliasing, réallocation, panique, contrôle de flux et erreurs croisées
  par tests positifs et négatifs ;
- migrer les onglets de Janus Studio vers un `EditorBuffer` possédé par chaque
  document, sans sérialisation au changement d'onglet.

### Gate de sortie

- aucune lecture après libération, double libération ou réallocation invalide
  dans les campagnes sanitizer dédiées ;
- Janus Studio conserve contenu, curseur, défilement et historique d'annulation
  indépendamment par onglet ;
- les callbacks historiques des conteneurs restent compatibles ou disposent
  d'une migration documentée ;
- le contrat de stabilité 1.0 intègre la sémantique retenue.

## 0.20 — Cœur sémantique et diagnostics

### Livrables

- introduire une session de compilation réutilisable par CLI, documentation et
  LSP ;
- produire un index sémantique typé commun au lieu de recalculer des modèles
  parallèles dans le serveur ;
- séparer progressivement résolution des noms, typage, ownership et analyse de
  flux sans réécriture globale risquée ;
- ajouter une gate interdisant tout nouveau diagnostic `J0000` ;
- publier le compteur des diagnostics non classifiés et le réduire release
  après release ;
- améliorer la récupération du parseur et la conservation de plusieurs erreurs
  sans cascades trompeuses.

### Gate de sortie

- CLI et LSP produisent les mêmes codes et positions sur un corpus partagé ;
- aucun nouveau chemin utilisateur n'emploie `J0000` ;
- le nombre de diagnostics non classifiés atteint zéro avant la première RC ;
- les principales unités sémantiques ont des responsabilités et tests isolés,
  même si leur découpage physique continue après 1.0.

## 0.21 — Outillage interactif et performance

### Livrables

- mesurer p50/p95 et mémoire pour démarrage LSP, diagnostic, complétion,
  définition, références et renommage ;
- ajouter `$/cancelRequest` et empêcher une analyse obsolète de publier des
  résultats après une nouvelle version du document ;
- évaluer la synchronisation incrémentale LSP à partir des profils, sans la
  rendre obligatoire si le gain n'est pas démontré ;
- compléter les capacités à forte valeur manquantes, notamment les symboles de
  document, seulement après stabilisation du modèle sémantique partagé ;
- étendre les campagnes longues à l'analyse sémantique, l'évaluation constante
  et une compilation complète jusqu'à LLVM ;
- ajouter Janus Studio au dashboard de compilation et au canari aval.

### Gate de sortie

- budgets de performance ratifiés à partir d'au moins quatre semaines de
  baseline ;
- aucune régression supérieure au budget n'est fusionnée sans justification et
  nouvelle baseline explicite ;
- quatre campagnes hebdomadaires consécutives semantic/backend se terminent
  sans crash ni erreur sanitizer non triée ;
- les requêtes LSP annulées ne publient pas de résultat périmé.

## 0.22 — Décision sur les surfaces expérimentales

### Bibliothèque standard

- réauditer `std.testing` et `std.numeric` pour promotion dans le cœur 1.0 ;
- auditer les ajouts aux modules stables depuis 0.8 ;
- décider si `std.graphics.*` reste expérimental en 1.0 ou devient un paquet à
  versionnement indépendant ;
- documenter effets, erreurs et matrice tier-1 des ressources graphiques
  conservées.

### Registre et dépendances

- publier des paquets de référence qui exercent dépendances, lockfiles, yank,
  cache offline et récupération ;
- réaliser au moins deux cycles documentés de retour utilisateur ;
- figer ou versionner explicitement la politique de cache ;
- tester restauration, rotation des clés et migration du service de référence.

### LSP et éditeurs

- définir quelles capacités et quels réglages relèvent du contrat 1.x ;
- publier une matrice de compatibilité éditeur/LSP ;
- décider le cycle de versionnement et de remise de l'extension VS Code.

### Gate de sortie

- chaque surface expérimentale possède une décision : promotion, maintien
  explicite hors contrat, extraction ou retrait ;
- aucune surface publique n'est figée sans fixture de compatibilité et guide ;
- la politique du registre a été utilisée dans des scénarios non simulés.

## 1.0.0-rc.1 — Gel et observation

### Conditions d'entrée

- les gates 0.18 à 0.22 sont closes ;
- aucun ticket `severity:critical` n'est ouvert ;
- l'inventaire, le contrat, la référence stdlib et le changelog sont cohérents ;
- les trois plateformes tier-1 construisent, testent et exécutent leurs archives ;
- la suite N/N+1 utilise une release publiée réelle, pas seulement le compilateur
  courant des deux côtés.

### Période RC

- minimum 30 jours sans nouvelle surface stable ;
- minimum quatre campagnes hebdomadaires longues réussies et archivées ;
- validation de Janus Studio, Project Euler et d'au moins un troisième projet
  aval indépendant ;
- corrections limitées aux bugs, diagnostics, documentation et performances
  sans changement sémantique non annoncé.

Toute rupture nécessaire redémarre une RC et met à jour les fixtures de
compatibilité et le guide de migration.

## 1.0.0 — Contrat activé

La 1.0 peut être publiée lorsque :

- la revue finale du contrat conclut GO et est datée ;
- chaque garantie a un test, une preuve de release ou une procédure vérifiable ;
- les surfaces exclues sont clairement marquées expérimentales ;
- les notes de release décrivent support, compatibilité, dépréciation et voie de
  migration ;
- les canaux, archives, checksums et attestations sont validés par les canaris
  avant promotion.

## Après 1.0

Les chantiers suivants sont utiles mais ne doivent pas retarder le gel du cœur
s'ils restent hors contrat :

- async/await et runtime asynchrone ;
- bibliothèque réseau haut niveau ;
- graphisme 3D ;
- debugger/DAP ;
- nouvelles architectures tier-1 ;
- JIT ou auto-hébergement du compilateur.

Ils doivent être évalués par RFC séparée, avec budget de maintenance et cas
d'usage aval explicite.

## Tableau de suivi synthétique

| Jalon | Résultat principal | Bloque 1.0 |
| --- | --- | --- |
| 0.18 | Baseline actuelle, backlog et canari Studio | oui |
| 0.19 | Emprunts lexicaux et conteneurs ergonomiques | oui |
| 0.20 | Modèle sémantique partagé et diagnostics classifiés | oui |
| 0.21 | LSP mesuré, cancellable et fuzz semantic/backend | oui |
| 0.22 | Décision sur toutes les surfaces expérimentales | oui |
| 1.0 RC | Gel, compatibilité et observation aval | oui |
| Après 1.0 | Async, réseau, 3D, DAP, JIT, auto-hébergement | non |
