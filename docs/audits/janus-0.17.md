# Audit technique de Janus 0.17

Date : 20 août 2026  
Révision auditée : `94669447ed4038f802cc01f26f7df2b798b0c24e`  
Décision : **NO-GO pour Janus 1.0, GO pour une phase de consolidation ciblée**

## Résumé exécutif

Janus 0.17 n'est plus un prototype. Le dépôt fournit un compilateur natif LLVM,
une bibliothèque standard étendue, un gestionnaire de dépendances et de
toolchains, un registre de référence, un serveur LSP riche, une extension VS
Code et une chaîne de release multiplateforme défensive. La suite locale de 225
tests passe intégralement et Janus Studio démontre que le langage peut porter
une application graphique réelle.

Le principal risque avant 1.0 n'est pas une régression de correction observée.
Il est concentré dans trois écarts :

1. le modèle d'emprunt reste trop limité pour manipuler naturellement des
   graphes d'objets propriétaires et des ressources stockées dans les
   conteneurs ;
2. le contrat, l'inventaire et la décision de préparation 1.0 décrivent encore
   principalement l'état 0.8 malgré neuf versions mineures supplémentaires ;
3. plusieurs garanties importantes sont présentes mais pas encore pilotées par
   des objectifs mesurables : migration des diagnostics, latence LSP,
   performance bloquante, utilisation du registre et validation par des projets
   aval.

La priorité recommandée est donc de consolider la sémantique de propriété et
les preuves de stabilité avant d'ajouter de grandes familles de fonctionnalités.
La [roadmap vers Janus 1.0](../roadmap-1.0.md) traduit cet audit en jalons.

## Périmètre et méthode

L'audit couvre :

- la syntaxe, l'analyse sémantique, la propriété et le backend LLVM ;
- la bibliothèque standard et le runtime natif ;
- les commandes `janus`, `janusup`, le registre et la distribution ;
- `janus-lsp`, l'extension VS Code et l'intégration avec Janus Studio ;
- les tests, benchmarks, campagnes de fuzzing et documents de stabilité.

Vérifications exécutées :

```text
cmake --build build -j2
ctest --test-dir build --output-on-failure -j2
../janus/build/janus check --all --deny-warnings  # dans Janus Studio
```

Résultats observés :

- build complet réussi sur Linux x86_64 avec LLVM 21.1.8 ;
- 225 tests CTest réussis sur 225 en 127,51 secondes ;
- 385 fichiers de test suivis, 47 fichiers documentaires et six workflows CI ;
- environ 78 800 lignes C, C++ et Janus suivies dans le dépôt ;
- aucun ticket GitHub ouvert au moment de l'audit ;
- Janus Studio est accepté par le compilateur 0.17, avec un avertissement
  `JANA0014` sous `--deny-warnings` qui empêche encore d'en faire un canari
  aval bloquant sans correction préalable.

Les résultats locaux ne remplacent pas les matrices Linux, macOS et Windows de
la CI. Ils servent à confirmer que les constats portent sur la conception et la
stabilisation, pas sur un dépôt local déjà en échec.

## État par domaine

| Domaine | État | Conclusion |
| --- | --- | --- |
| Correction et tests | solide | Suite large, entièrement verte, nombreux cas de nettoyage et panique. |
| Distribution | solide | Archives tier-1, identité, attestations, canaris et mises à jour sont fortement contrôlés. |
| Sécurité de la supply chain | solide | Actions épinglées, archives validées, publications et caches transactionnels. |
| Langage de base | solide | Génériques, traits, enums, closures, constantes et ownership forment un ensemble cohérent. |
| Ergonomie de propriété | bloquante pour 1.0 | Pas d'emprunt général mutable ou de projection sûre d'un élément propriétaire de conteneur. |
| Architecture du compilateur | à consolider | Quelques unités concentrent une grande part de la complexité et dupliquent des analyses. |
| Diagnostics | à consolider | Modèle structuré réussi, mais la migration hors `J0000` est très incomplète. |
| LSP | avancé mais non stabilisé | Capacités riches, mais synchronisation complète et absence de budget de latence/cancellation. |
| Stdlib cœur | proche du candidat | Surface large et auditée, sous réserve d'une nouvelle revue depuis 0.8. |
| Graphisme | expérimental utile | Janus Studio apporte du dogfooding, mais le contrat tier-1 final reste à décider. |
| Registre et écosystème | insuffisamment éprouvé | Protocole et défense solides, manque de cycles d'usage et de paquets aval observables. |
| Gouvernance 1.0 | à reconstruire | Documents 0.8 obsolescents et backlog ouvert vide malgré des écarts connus. |

## Forces confirmées

### Chaîne de validation

La qualité de la chaîne de livraison est le point le plus mûr du projet. La CI
couvre les trois plateformes tier-1, plusieurs versions LLVM, les archives, les
installateurs, les builds concurrents, les lockfiles, le registre, les doctests,
les exemples et la compatibilité. Les campagnes hebdomadaires de 60 minutes
sous ASan/UBSan ciblent lexer, parseur, manifeste et résolveur.

### Propriété et nettoyage

Les règles de `move`, `delete`, `defer`, les agrégats propriétaires, les
conteneurs et les nettoyages en présence de panique sont largement spécifiés et
testés. La 0.15 empêche aussi la libération d'un propriétaire encore emprunté.
Cette fondation mérite d'être étendue, pas remplacée.

### Outillage utilisateur

Les commandes `check`, `build`, `run`, `test`, `fmt`, `doc`, `clean`, les
timings et les formats structurés forment une expérience cohérente. Le LSP
propose déjà diagnostics, complétion, hover, définition, références, renommage,
implémentations, quick fixes, formatage, tokens sémantiques, inlay hints et
symboles du workspace.

### Validation par une application réelle

Janus Studio exerce des surfaces rarement couvertes ensemble par de petits
exemples : graphisme, UTF-8, presse-papiers, processus persistants, filesystem,
LSP, état interactif et structures propriétaires. Il constitue un excellent
candidat pour devenir un canari aval officiel.

## Constats prioritaires

### A01 — P0 — Généraliser les emprunts sûrs

`Array[T]` peut posséder des valeurs non `Copy`, mais leur observation est
limitée à des callbacks bornés et il n'existe pas d'emprunt mutable général
d'un élément. Un appelant ne peut pas conserver lexicalement une vue sur le
document actif et la transmettre naturellement aux couches d'entrée, de rendu
et d'outillage.

Janus Studio doit actuellement sérialiser les buffers inactifs lors d'un
changement d'onglet. Cette solution copie le contenu et ne conserve pas
l'historique d'annulation de chaque buffer. Le problème se reproduira dans les
arbres d'interface, scènes, caches, graphes et tables de symboles.

Recommandation : spécifier des emprunts lexicaux immuables et mutables, la
projection de champs et une API d'emprunt de conteneur, sans autoriser dans un
premier temps les retours empruntés ni les captures persistantes.

### A02 — P0 — Rebaser le contrat 1.0 sur la 0.17

`readiness-1.0.md`, `known-limitations-0.8.md`, l'inventaire et la politique de
sévérité restent ancrés en 0.8. Depuis, Janus a ajouté entre autres les
constantes, les tests natifs, de nombreuses capacités LSP, des garanties de
distribution et de nouvelles primitives graphiques.

Le backlog GitHub ouvert est vide alors que ces documents déclarent encore des
écarts. Un backlog vide ne constitue donc pas une preuve de préparation.

Recommandation : produire un inventaire 0.18, convertir chaque écart en ticket
avec propriétaire, dépendances et gate, puis rendre la revue de préparation
générée à partir de cet état.

### A03 — P1 — Réduire la concentration architecturale

Les plus grandes unités observées sont :

- `semantic/analyzer.cpp` : environ 6 435 lignes ;
- `lsp/server.cpp` : environ 3 508 lignes ;
- `tools/janus/main.cpp` : environ 2 667 lignes ;
- `frontend/parser.cpp` : environ 1 799 lignes ;
- `constant/evaluator.cpp` : environ 1 560 lignes.

Le LSP reconstruit par ailleurs ses propres index et inférences autour du
parseur et de l'analyseur. Cette concentration augmente le coût de toute
évolution du borrowing, des diagnostics ou de l'incrémentalité.

Recommandation : introduire une session de compilation et un modèle sémantique
typé partagé entre CLI et LSP, puis séparer progressivement résolution,
typage, ownership et vérification des flux.

### A04 — P1 — Terminer les diagnostics structurés

Le modèle structuré, le JSON et les suggestions sont de bonnes fondations, mais
seuls 28 codes sont déclarés alors que les sources contiennent plusieurs
centaines de constructions de `CompileError`. Le constructeur historique
produit encore `J0000`, explicitement temporaire dans la documentation.

Recommandation : interdire les nouveaux `J0000`, mesurer leur nombre en CI,
faire décroître ce compteur à chaque release puis supprimer le constructeur
non classifié avant la première RC.

### A05 — P1 — Définir la performance interactive du LSP

Le serveur annonce une synchronisation complète (`change = 1`) et traite les
requêtes dans un serveur monolithique. Aucun support de `$/cancelRequest` ni
budget de latence versionné n'a été identifié. La richesse fonctionnelle est
donc supérieure aux garanties opérationnelles.

Recommandation : mesurer démarrage, mémoire, diagnostics, complétion et
renommage sur des workspaces versionnés ; ajouter cancellation et
incrémentalité seulement après ces mesures, avec seuils bloquants issus de la
baseline plutôt que choisis arbitrairement.

### A06 — P1 — Étendre la recherche de crash au semantic/backend

Les campagnes longues couvrent lexer, parseur, manifeste et résolveur. Elles ne
ciblent pas directement l'analyse sémantique, l'évaluateur constant, la
génération LLVM et le chargement de projets complets, alors que ces composants
portent la majorité des invariants complexes.

Recommandation : ajouter des modes de fuzzing semantic et compilation complète,
avec réduction/reproduction des cas et conservation des artefacts, sans
allonger la CI de chaque pull request.

### A07 — P1 — Transformer Janus Studio en canari aval

Le corpus Project Euler valide efficacement calcul, collections et runtime,
mais pas une application interactive multi-module. Janus Studio expose déjà un
avertissement sous `--deny-warnings`, preuve que ce canari apporterait un signal
différent.

Recommandation : rendre Studio propre sous `check --all --deny-warnings`,
l'épingler à une révision dans un workflow aval et exiger check, tests, build
release et démarrage court sur Linux. Les plateformes graphiques restantes
peuvent être ajoutées après stabilisation du harnais.

### A08 — P2 — Décider les surfaces expérimentales

Le graphisme, `std.testing`, `std.numeric`, le registre distant et le LSP sont
encore classés expérimentaux dans les documents 0.8. Leur maturité réelle a
divergé : `std.testing` et `std.numeric` disposent désormais de contrats et de
tests détaillés, tandis que le graphisme et le registre demandent encore du
retour multiplateforme et utilisateur.

Recommandation : promouvoir, maintenir expérimental ou retirer explicitement
chaque surface. La 1.0 ne doit pas conserver une catégorie indéterminée par
simple inertie documentaire.

### A09 — P2 — Obtenir des preuves d'usage du registre

Le protocole, le service de référence et la sécurité des archives sont
substantiels, mais le dépôt ne montre pas encore plusieurs cycles de
publication, résolution, yank, cache offline et récupération opérés par des
utilisateurs distincts.

Recommandation : publier un petit ensemble de paquets de référence, documenter
des scénarios d'exploitation et recueillir au moins deux cycles de retour avant
de figer la politique de cache 1.0.

### A10 — P2 — Rendre les tendances de performance actionnables

Le dashboard hebdomadaire est utile mais explicitement non bloquant. Il mesure
deux projets de compilation et ne couvre pas encore la mémoire, le LSP ou un
projet aval de taille réaliste.

Recommandation : conserver le dashboard exploratoire, ajouter Janus Studio au
corpus et ne transformer en gate que les métriques dont la variance a été
observée sur plusieurs semaines.

## Risques de programme

- **Vitesse de release** : neuf versions mineures entre 0.8 et 0.17 en moins de
  trois semaines apportent beaucoup de valeur, mais peu de temps de stabilisation
  et de retour aval entre deux surfaces.
- **Absence de backlog** : sans tickets ouverts, les écarts documentés ne sont
  ni ordonnés ni traçables jusqu'à une gate de release.
- **Élargissement prématuré** : async, réseau haut niveau, 3D, JIT ou
  auto-hébergement dilueraient les efforts nécessaires au contrat 1.0.
- **Contrat avant ergonomie** : figer le modèle de propriété actuel avant de
  résoudre l'emprunt des conteneurs créerait une dette de compatibilité majeure.

## Recommandation de décision

Janus doit rester pré-1.0 tant que A01 et A02 ne sont pas clos. Une fois le
modèle d'emprunt décidé et l'inventaire remis à jour, les autres constats peuvent
être traités par gates successives sans bloquer toute livraison mineure.

Le projet peut raisonnablement viser une 1.0 après une série courte de releases
de consolidation, suivie d'une RC avec période d'observation. Il ne devrait pas
viser la 1.0 en ajoutant d'abord davantage de surface publique.
