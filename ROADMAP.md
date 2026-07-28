# Roadmap Janus 0.5.1 → 0.8.0

Statut : roadmap de développement proposée après Janus 0.5.0.

Cette roadmap organise la maturation de Janus en petites releases vérifiables. Elle s'arrête volontairement à **0.8.0** : la décision de préparer une 1.0 sera prise seulement après retour d'expérience sur la 0.8.0 et validation du [contrat de stabilité proposé](docs/stability-contract.md).

## Vision

Janus doit devenir un langage natif fortement typé qui reste :

- explicite sur la propriété, les déplacements et la destruction ;
- agréable pour les outils CLI, les applications et les jeux 2D ;
- prédictible sur Linux x86_64, macOS ARM64 et Windows x86_64 ;
- pédagogique dans ses diagnostics ;
- reproductible dans sa résolution de dépendances et sa distribution ;
- documenté par des exemples réellement compilés.

La priorité n'est pas d'élargir rapidement la syntaxe. De 0.5.1 à 0.8.0, l'effort porte d'abord sur les fondations : documentation exacte, diagnostics, valeurs propriétaires dans les collections, gestion d'erreur, bibliothèque système, documentation d'API, expérience éditeur, temps de compilation et paquets.

## Principes de pilotage

1. **Petites releases** : chaque version doit avoir un objectif principal et des critères de sortie observables.
2. **TDD et compatibilité** : toute évolution commence par une régression ou un test de contrat.
3. **Trois plateformes** : une fonctionnalité publique n'est terminée que lorsqu'elle passe la matrice de niveau 1 pertinente.
4. **Aucun crash compilateur** : une entrée invalide produit un diagnostic, jamais un abort, un segfault ou une exception non interceptée.
5. **Propriété d'abord** : les API de stdlib doivent expliciter copie, déplacement, consommation et destruction.
6. **Erreurs récupérables avec `Result`** : `panic` reste réservé aux invariants rompus et situations irrécupérables.
7. **Documentation exécutable** : les exemples autonomes sont compilés en CI et les liens du site sont crawlés.
8. **Mesurer avant d'optimiser** : les travaux de cache et de performance commencent par des métriques reproductibles.
9. **Pas de promesse 1.0 implicite** : 0.8.0 reste une version pré-1.0 et peut encore préparer des migrations documentées.

## Hors périmètre jusqu'à 0.8.0

Sauf nécessité démontrée par une fonctionnalité prioritaire, cette roadmap ne prévoit pas :

- d'API graphique 3D ;
- de système général de macros ;
- d'`async`/`await` ou de runtime asynchrone ;
- de modèle complet de concurrence ;
- d'ABI stable entre modules Janus précompilés ;
- de backend WebAssembly ;
- de ramasse-miettes.

Ces sujets pourront être évalués après 0.8.0 sans bloquer la maturation du cœur.

## Vue d'ensemble

| Release | Thème | Résultat attendu |
| --- | --- | --- |
| 0.5.1 | Documentation exacte | documentation et site alignés automatiquement sur la surface 0.5.x |
| 0.5.2 | Diagnostics structurés | modèle commun, codes et rendus humain/JSON |
| 0.5.3 | Paniques observables | origine et traces de développement sans casser les nettoyages |
| 0.6.0 | `Array` propriétaire | contrat des conteneurs puis valeurs non-`Copy` dans `Array` |
| 0.6.1 | Collections propriétaires | hachage, builders et itérateurs move-aware |
| 0.6.2 | Combinateurs `Option`/`Result` | enrichissement des enums existants et composition sûre avec propriété |
| 0.6.3 | Dérivations | génération sûre et explicite de `Copy`, égalité, hachage et `Debug` |
| 0.7.0 | Chemins et fichiers | erreur système portable, `std.path` et `std.fs` |
| 0.7.1 | Flux et processus | `std.io`, environnement, arguments et processus |
| 0.7.2 | Documentation d'API | commentaires publics et `janus doc` |
| 0.7.3 | Doctests | exemples documentaires compilés par `janus test` |
| 0.7.4 | Navigation LSP | renommage, signatures, jetons et navigation de traits |
| 0.7.5 | Extension VS Code existante | code actions supplémentaires et publication Marketplace reproductible |
| 0.7.6 | Timings | phases de compilation mesurées et benchmarks suivis |
| 0.7.7 | Build incrémental | cache correct et invalidation par interface |
| 0.7.8 | Protocole du registre | format et modèle de sécurité versionnés |
| 0.7.9 | Client et registre | recherche, publication distante et service de référence avec provenance |
| 0.7.10 | Modernisation de la stdlib | surface auditée, implémentation réécrite avec les idiomes récents et documentation générée |
| 0.8.0 | Audit et gel | audit de la surface publique et rapport de préparation 1.0 |

## Ordre de publication

```text
0.5.1 → 0.5.2 → 0.5.3
                    ↓
0.6.0 → 0.6.1 → 0.6.2 → 0.6.3
                              ↓
0.7.0 → 0.7.1 → 0.7.2 → 0.7.3 → 0.7.4
                                      ↓
0.7.5 → 0.7.6 → 0.7.7 → 0.7.8 → 0.7.9 → 0.7.10 → 0.8.0
```

Une release peut être préparée en parallèle, mais elle ne doit pas être publiée avant la réussite des critères de sortie de la release précédente.

---

<a id="release-0-5-1"></a>
## Janus 0.5.1 — Documentation exacte

**Objectif :** éliminer les contradictions entre la documentation, la stdlib, le compilateur, les exemples et le site public.

**Motivation :** la documentation graphique 0.5.0 mentionne encore comme absentes certaines fonctions déjà présentes, notamment le temps de trame. Une documentation obsolète ralentit les utilisateurs et rend les futures garanties difficiles à auditer.

### R051-1 — Auditer et corriger la surface documentée 0.5.x

- inventorier les fonctions, types, modules, commandes et options réellement publics ;
- corriger les limites devenues obsolètes dans les guides ;
- distinguer clairement API stable proposée, API expérimentale et détails internes ;
- vérifier les liens README, guides, Book et référence générée.

**Critères d'acceptation :**

- l'inventaire versionné `docs/public-surface-0.5.json` couvre les symboles publics de `stdlib/std/`, les commandes/options de `janus --help` et leurs documents de référence ;
- chaque entrée de cet inventaire indique sa source canonique et son statut stable proposé ou expérimental ;
- tous les extraits autonomes du site compilent avec la version ciblée ;
- tous les liens internes du site construit répondent sans 404 ;
- le changelog 0.5.1 énumère les corrections documentaires notables.

### R051-2 — Automatiser la détection de dérive documentaire

- étendre les tests de cohérence existants ;
- vérifier les signatures/API citées dans les guides à partir d'un inventaire versionné ;
- exécuter le checker d'extraits et le crawler de liens dans la CI Pages ;
- produire une erreur ciblée indiquant le document et la surface divergente.

**Critères d'acceptation :**

- une fixture volontairement obsolète fait échouer le test ;
- les tests restent indépendants du réseau lorsqu'ils vérifient l'artefact local ;
- `ctest` et le build MkDocs strict passent sur Linux ;
- le mécanisme est documenté dans `docs/development.md`.

**Gate de release 0.5.1 :** CI verte, site public déployé, crawl interne sans erreur et aucune modification de sémantique du langage.

---

<a id="release-0-5-2"></a>
## Janus 0.5.2 — Diagnostics structurés

**Objectif :** faire des diagnostics une interface structurée commune au CLI, au compilateur et au LSP.

### R052-1 — Introduire un modèle de diagnostic structuré et des codes

- définir une structure avec sévérité, code, message, emplacement principal, notes et emplacements secondaires ;
- attribuer des familles de codes au lexer, parseur, analyseur, résolveur de modules et backend ;
- convertir progressivement les `CompileError` sans changer les règles du langage ;
- documenter la stabilité prévue des codes avant 1.0.

**Critères d'acceptation :**

- les diagnostics migrés possèdent un code unique testé ;
- les chemins conservent la forme `fichier:ligne:colonne` ;
- le LSP consomme le même modèle sans parser le texte humain ;
- le corpus versionné `tests/diagnostics/invalid/` passe sans crash ni exception non interceptée ;
- les campagnes lexer/parseur nightly exécutent chacune au moins 15 minutes sans crash reproductible.

### R052-2 — Ajouter rendus humain/JSON, suggestions et récupération

- fournir un rendu terminal avec extrait de source et repères ;
- ajouter `--diagnostic-format human|json` aux commandes d'analyse et construction ;
- représenter les suggestions de correction de manière structurée ;
- récupérer après certaines erreurs indépendantes afin d'en signaler plusieurs sans cascade illisible.

**Critères d'acceptation :**

- le JSON possède un schéma et des fixtures versionnées, compatibles de 0.5.2 jusqu'à la prochaine version mineure ;
- stdout/stderr et codes de sortie restent conformes au contrat CLI ;
- les snapshots sans couleur à 80 et 120 colonnes passent sur Linux et Windows ;
- les suggestions ne modifient jamais automatiquement les sources.

**Gate de release 0.5.2 :** diagnostics structurés utilisés par CLI et LSP, codes testés et rendus humain/JSON cohérents sur les plateformes supportées.

---

<a id="release-0-5-3"></a>
## Janus 0.5.3 — Paniques observables

**Objectif :** rendre les erreurs irrécupérables localisables sans modifier le contrat de nettoyage.

### R053-1 — Ajouter contexte source et trace aux paniques de développement

- transmettre fichier, ligne et fonction aux appels de panique générés ;
- produire une pile symbolisée lorsque la plateforme le permet ;
- conserver les nettoyages `defer` et destructeurs garantis ;
- définir une option pour désactiver/raccourcir les traces en release.

**Critères d'acceptation :**

- division par zéro, index hors limites et `panic` explicite indiquent leur origine ;
- les tests vérifient le nettoyage avant terminaison ;
- les trois plateformes terminent sans deadlock ni crash du mécanisme de trace ;
- le format exact reste déclaré expérimental avant 1.0.

**Gate de release 0.5.3 :** paniques de debug localisables, nettoyages garantis conservés et fixtures de trace portables validées.

---

<a id="release-0-6-0"></a>
## Janus 0.6.0 — Contrat des conteneurs et `Array` propriétaire

**Objectif :** permettre aux collections de stocker des valeurs non-`Copy` avec une sémantique explicite de déplacement et de destruction.

Cette release est le principal changement sémantique de la roadmap. Son contrat doit être arrêté avant l'implémentation générale.

### R060-1 — Spécifier le contrat de propriété des conteneurs

- écrire une décision d'architecture comparant emprunt limité, accès par callback, vues et opérations consommantes ;
- définir `push`, `get`, `set`, `remove`, `clear`, itération et destruction pour les valeurs propriétaires ;
- préciser invalidation, aliasing, durée de vie et diagnostics attendus ;
- fournir des exemples avec classes, structs propriétaires et enums.

**Critères d'acceptation :**

- aucun comportement de copie implicite d'une ressource ;
- chaque opération indique si elle observe, déplace, remplace ou détruit ;
- le design couvre réallocation, panique et sortie anticipée ;
- la décision est validée par des fixtures compilables minimales.

### R060-2 — Rendre `Array` compatible avec les éléments non-`Copy`

- séparer les opérations d'observation des opérations consommantes ;
- prendre en charge insertion, retrait, remplacement et destruction de valeurs propriétaires ;
- préserver l'API 0.5.x lorsque son sens reste sûr ;
- ajouter une migration lorsque la signature doit changer.

**Critères d'acceptation :**

- un `Array[OwnedType]` fonctionne sans fuite ni double destruction ;
- réallocation, `clear`, `remove` et destructeur sont couverts ;
- les tests incluent succès, panique et sortie de portée ;
- la suite de compatibilité `Array` 0.5.x reste entièrement verte pour les types `Copy`, signatures conservées incluses.

**Gate de release 0.6.0 :** contrat documenté, `Array` move-aware, tests sanitizer et matrice multiplateforme verts, première partie du guide de migration 0.5 → 0.6 publiée.

---

<a id="release-0-6-1"></a>
## Janus 0.6.1 — Collections propriétaires et itérateurs

**Objectif :** appliquer le contrat 0.6.0 aux collections de hachage, builders et parcours.

### R061-1 — Étendre `HashSet`, `HashMap` et builders aux valeurs propriétaires

- appliquer le contrat aux clés et valeurs ;
- traiter correctement remplacement, suppression, tombstones et rehash ;
- définir le contrat de hachage/égalité pour l'observation non propriétaire ;
- garantir le nettoyage lors d'une allocation ou d'un rehash échoué.

**Critères d'acceptation :**

- insertions et suppressions de clés/valeurs propriétaires sont testées ;
- chaque élément est détruit exactement une fois ;
- rehash et collisions possèdent des tests de stress déterministes ;
- les collections passent sous ASan/UBSan sur les runners compatibles.

### R061-2 — Ajouter itérateurs observants et consommateurs

- distinguer itération qui observe et itération qui transfère les éléments ;
- adapter `map`, `filter`, `flatMap`, `take`, `fold` et builders ;
- garantir le nettoyage des éléments non consommés lors d'un arrêt anticipé ;
- documenter le coût et l'état final du conteneur.

**Critères d'acceptation :**

- `for` et les adaptateurs ne copient pas implicitement une valeur propriétaire ;
- `break`, `continue`, `return`, `?` et panique nettoient correctement l'état ;
- les chaînes d'itérateurs fonctionnent avec types `Copy` et propriétaires ;
- la suite de compatibilité contient les comportements 0.6.0 retenus.

**Gate de release 0.6.1 :** collections principales move-aware, itérateurs consommateurs testés et guide de migration 0.5 → 0.6 finalisé.

---

<a id="release-0-6-2"></a>
## Janus 0.6.2 — Combinateurs et propriété pour `Option` et `Result`

**Objectif :** réduire les `match` répétitifs sans masquer la propriété ni encourager les paniques.

### R062-1 — Enrichir `Option[T]`

Ajouter une surface minimale cohérente, notamment `isSome`, `isNone`, `map`, `andThen`, `orElse`, `unwrapOr` et variantes consommantes nécessaires.

**Critères d'acceptation :**

- les opérations fonctionnent avec `T : Copy` et avec `T` propriétaire ;
- aucune extraction ne duplique une ressource ;
- les méthodes non consommantes et consommantes sont distinguées ;
- exemples, signatures et cas limites sont documentés et compilés.

### R062-2 — Enrichir `Result[T, E]` et ses interactions avec `?`

Ajouter `isOk`, `isError`, `map`, `mapError`, `andThen`, `orElse`, `unwrapOr` et conversions documentées entre `Option` et `Result`; documenter l'interaction des nouveaux combinateurs avec l'opérateur `?` déjà existant. La correction du nettoyage de contrôle de flux nécessaire aux itérateurs propriétaires reste un critère de R061-2 et ne dépend pas de cette release.

**Critères d'acceptation :**

- succès et erreur nettoient exactement les variantes actives ;
- chaîner les nouveaux combinateurs avant ou après `?` transfère correctement `T` ou `E` sans réutilisation après move ;
- les API système futures peuvent retourner un `Result` sans allocation obligatoire ;
- les tests couvrent fonctions, closures, boucles et `defer`.

**Gate de release 0.6.2 :** API documentée, exemples compilés, propriété et propagation couvertes par la suite de compatibilité.

---

<a id="release-0-6-3"></a>
## Janus 0.6.3 — Dérivations sûres

**Objectif :** réduire le code répétitif pour les capacités structurelles courantes sans introduire un système général de macros.

### R063-1 — Définir syntaxe et règles de dérivation

- choisir une syntaxe explicite pour demander les dérivations ;
- définir l'éligibilité champ par champ ;
- spécifier diagnostics, visibilité et interactions avec génériques/traits ;
- interdire les dérivations qui compromettraient la propriété.

**Critères d'acceptation :**

- le design couvre structs, enums et classes lorsque pertinent ;
- une dérivation impossible produit un diagnostic indiquant le champ fautif ;
- aucune capacité n'est dérivée implicitement sans demande ;
- la syntaxe est protégée par parser, formatter et LSP.

### R063-2 — Implémenter `Copy`, égalité, hachage et `Debug`

- générer les implémentations structurelles ;
- intégrer égalité/hachage aux collections ;
- fournir une représentation `Debug` déterministe adaptée aux diagnostics/tests ;
- empêcher `Copy` pour tout agrégat propriétaire.

**Critères d'acceptation :**

- égalité et hachage respectent le même contrat ;
- `HashSet`/`HashMap` acceptent un type utilisateur dérivé sans boilerplate manuel ;
- la sortie `Debug` est testée mais reste distincte du format utilisateur ;
- la matrice CI et la suite de compatibilité couvrent les quatre capacités.

**Gate de release 0.6.3 :** dérivations minimales stabilisées pour la série 0.6.x, formatter/LSP/doc alignés, aucune macro générale ajoutée.

---

<a id="release-0-7-0"></a>
## Janus 0.7.0 — Chemins et fichiers multiplateformes

**Objectif :** rendre possibles des outils CLI et applications de fichiers sans écrire directement une couche C.

### R070-1 — Définir erreurs système et primitives runtime portables

- créer une erreur système structurée transportable dans `Result` ;
- normaliser les opérations nécessaires sans masquer le code natif utile ;
- isoler les différences POSIX/Windows dans le runtime ;
- définir encodage, chemins et limites de taille.

**Critères d'acceptation :**

- aucune erreur récupérable de fichier/processus ne provoque un `panic` ;
- les erreurs exposent opération, catégorie et contexte utile ;
- les ressources natives sont fermées exactement une fois ;
- les contrats sont testés sur les trois plateformes.

### R070-2 — Ajouter `std.path` et `std.fs`

- chemins, jointure, normalisation et composants ;
- lecture/écriture atomique de fichiers simples ;
- création, parcours et suppression de répertoires ;
- métadonnées et distinction fichier/répertoire/lien selon contrat documenté.

**Critères d'acceptation :**

- les chemins Unicode et séparateurs de plateforme sont testés ;
- toutes les opérations récupérables retournent `Result` ;
- les tests utilisent des répertoires temporaires uniques et nettoyés ;
- le comportement des liens symboliques est explicite.

**Gate de release 0.7.0 :** chemins et fichiers utilisables sans FFI utilisateur, API `Result` cohérente et tests temporaires multiplateformes.

---

<a id="release-0-7-1"></a>
## Janus 0.7.1 — Flux, environnement et processus

**Objectif :** compléter la bibliothèque système pour les outils CLI et les pipelines de données.

### R071-1 — Ajouter `std.io` et les flux tamponnés

- lecture/écriture séquentielle ;
- buffers, EOF, flush et fermeture ;
- stdin, stdout et stderr ;
- intégration texte/UTF-8 sans supposer qu'un flux binaire est valide.

**Critères d'acceptation :**

- lectures partielles et écritures partielles sont gérées ;
- fermer deux fois ou utiliser après fermeture produit un diagnostic/erreur défini ;
- les buffers propriétaires sont nettoyés sur tous les chemins ;
- exemples de copie de fichier et traitement ligne par ligne compilent et s'exécutent.

### R071-2 — Ajouter arguments, environnement et processus

- arguments du programme et variables d'environnement ;
- lancement de processus avec arguments séparés ;
- code de sortie, stdout/stderr capturables et répertoire de travail ;
- contrat explicite sans passage implicite par un shell.

**Critères d'acceptation :**

- les arguments contenant espaces/Unicode restent intacts ;
- le code de sortie enfant est observable ;
- absence de commande et refus d'accès retournent `Result` ;
- les tests n'abandonnent aucun processus enfant.

**Gate de release 0.7.1 :** exemple CLI complet sans FFI utilisateur, flux et processus testés, documentation de sécurité publiée.

---

<a id="release-0-7-2"></a>
## Janus 0.7.2 — Documentation d'API

**Objectif :** rendre une bibliothèque Janus découvrable depuis le terminal, le site et l'éditeur.

### R072-1 — Ajouter les commentaires de documentation et `janus doc`

- définir la syntaxe des commentaires publics ;
- les conserver dans l'AST/index public ;
- générer une documentation HTML statique par paquet ;
- fournir `janus doc` et `janus doc --open`.

**Critères d'acceptation :**

- modules, types, variantes, traits, fonctions et membres publics sont indexés ;
- les éléments privés sont exclus par défaut ;
- les liens entre symboles sont résolus ou signalés ;
- la génération est déterministe et fonctionne hors ligne.

**Gate de release 0.7.2 :** documentation API déterministe, liens résolus et commande `janus doc` validée hors ligne.

---

<a id="release-0-7-3"></a>
## Janus 0.7.3 — Doctests

**Objectif :** rendre les exemples de documentation exécutables et vérifiables par la chaîne de test.

### R073-1 — Compiler les exemples de documentation comme doctests

- identifier les blocs Janus exécutables et ceux volontairement incomplets ;
- compiler les exemples avec le contexte du paquet ;
- permettre les attentes compile-fail avec code de diagnostic ;
- intégrer les doctests à `janus test` et à la CI.

**Critères d'acceptation :**

- une API renommée casse le doctest correspondant ;
- les exemples d'erreur vérifient un code, pas une ponctuation fragile ;
- les tests sont filtrables et leurs échecs indiquent document/ligne ;
- le site Janus utilise le même mécanisme pour ses extraits.

**Gate de release 0.7.3 :** doctests actifs dans `janus test` et le site, diagnostics compile-fail vérifiés par code.

---

<a id="release-0-7-4"></a>
## Janus 0.7.4 — Navigation LSP

**Objectif :** compléter les fonctions de compréhension et refactoring du workspace.

### R074-1 — Compléter navigation et assistance LSP

- renommage à l'échelle du workspace ;
- aide à la signature ;
- jetons sémantiques ;
- types déduits/inlay hints configurables ;
- navigation vers les implémentations de traits.

**Critères d'acceptation :**

- le renommage respecte modules et visibilités ;
- aucune modification n'est appliquée en cas de collision ;
- les réponses restent sous les budgets documentés du workspace de référence ;
- les capacités sont annoncées correctement au client LSP.

**Gate de release 0.7.4 :** renommage et navigation sûrs, budgets LSP respectés et capacités correctement annoncées.

---

<a id="release-0-7-5"></a>
## Janus 0.7.5 — Enrichissement et publication de l'extension VS Code existante

**Objectif :** transformer les diagnostics structurés en corrections sûres et distribuer l'intégration officielle.

### R075-1 — Ajouter des code actions et publier l'extension VS Code existante

- actions pour imports manquants, branches `match` manquantes et corrections sûres ;
- affichage riche des diagnostics structurés ;
- matrice de compatibilité extension/toolchain ;
- publication documentée sur la Marketplace.

**Critères d'acceptation :**

- chaque action propose un `WorkspaceEdit` testé ;
- aucune action ambiguë ne s'applique automatiquement ;
- VSIX et version Marketplace proviennent du même commit/tag ;
- installation, mise à jour et choix de `janus-lsp` sont testés.

**Gate de release 0.7.5 :** code actions testées, extension publiée depuis le tag et matrice extension/toolchain documentée.

---

<a id="release-0-7-6"></a>
## Janus 0.7.6 — Timings et benchmarks de compilation

**Objectif :** réduire le temps de feedback sans compromettre la correction ni la reproductibilité.

### R076-1 — Exposer des timings et benchmarks de compilation

- mesurer chargement, parsing, analyse, génération LLVM, optimisation et lien ;
- ajouter une sortie humaine et JSON ;
- versionner plusieurs projets de benchmark petits/moyens ;
- publier une tendance sans transformer chaque variation en échec CI.

**Critères d'acceptation :**

- `janus build --timings` explique la totalité du temps mesuré ;
- le JSON est exploitable par CI ;
- le coût de la mesure est documenté ;
- le dashboard déclenche une alerte à partir d'une hausse de médiane de 15 % sur cinq exécutions, confirmée par deux jobs consécutifs.

**Gate de release 0.7.6 :** phases mesurées, sortie JSON exploitable et tendance de performance publiée sans gate bruitée.

---

<a id="release-0-7-7"></a>
## Janus 0.7.7 — Construction incrémentale

**Objectif :** réduire le temps de feedback en réutilisant uniquement des artefacts prouvés compatibles.

### R077-1 — Ajouter cache incrémental et invalidation par interface

- définir une empreinte incluant version, cible, options, source et interface des dépendances ;
- réutiliser uniquement les artefacts dont les entrées sont identiques ;
- invalider les dépendants lorsque l'interface publique change ;
- fournir une commande/option de nettoyage et un mode sans cache.

**Critères d'acceptation :**

- modification privée évite la recompilation inutile des consommateurs ;
- modification publique recompile tous les dépendants nécessaires ;
- aucun artefact n'est partagé entre cibles/options incompatibles ;
- builds froid, chaud, `--offline`, concurrents et interrompus sont testés ;
- la sortie binaire reste équivalente à un build propre.

**Gate de release 0.7.7 :** cache correct avant d'être rapide, corruption/collision couvertes et fallback build propre disponible.

---

<a id="release-0-7-8"></a>
## Janus 0.7.8 — Protocole du registre

**Objectif :** figer un protocole distant minimal et son modèle de sécurité avant d'écrire les clients et le service public.

### R078-1 — Versionner le protocole et le modèle de sécurité du registre

- spécifier index, métadonnées, archive, checksums et négociation de version ;
- définir noms, espaces de noms, immutabilité, yanking et résolution ;
- documenter authentification, autorisation, menaces et récupération ;
- garantir que le lockfile reste la source reproductible.

**Critères d'acceptation :**

- protocole `v1` documenté avec schémas et fixtures ;
- une version publiée ne peut pas être remplacée ;
- un paquet retiré reste reproductible via un lockfile existant selon politique ;
- confusion de dépendance et traversée de chemin possèdent des tests négatifs.

**Gate de release 0.7.8 :** protocole `v1`, schémas, fixtures et analyse de menaces publiés avant toute dépendance de production.

---

<a id="release-0-7-9"></a>
## Janus 0.7.9 — Client et registre distant de référence

**Objectif :** implémenter les opérations réseau reproductibles du CLI puis déployer le service de référence contre le protocole versionné.

### R079-1 — Ajouter recherche, téléchargement et publication distante au CLI

- configurer un registre par défaut et des registres explicites ;
- implémenter authentification, `search`, résolution, téléchargement et `publish` ;
- préserver `--locked` et `--offline` ;
- utiliser cache et téléchargements atomiques vérifiés.

**Critères d'acceptation :**

- une publication puis installation fonctionne sur les trois plateformes ;
- archive ou checksum invalide est rejeté avant extraction ;
- aucun secret n'apparaît dans logs, lockfiles ou diagnostics ;
- interruption réseau ne laisse pas un cache considéré valide.

### R079-2 — Déployer un registre de référence avec provenance

- fournir un service de référence déployable et sauvegardable ;
- intégrer attestations de provenance/signatures selon le modèle retenu ;
- exposer yank, métadonnées et audit ;
- publier procédures d'administration et de réponse à incident.

**Critères d'acceptation :**

- déploiement de référence reproductible ;
- sauvegarde/restauration testée ;
- publication non autorisée et remplacement de version sont refusés ;
- disponibilité du registre n'est pas requise pour un build `--locked --offline` déjà mis en cache.

**Gate de release 0.7.9 :** client distant et registre de référence interopèrent, le cache est atomique, sauvegarde/restauration et provenance sont testées, et aucun secret n'apparaît dans les sorties ou artefacts.

---

<a id="release-0-7-10"></a>
## Janus 0.7.10 — Modernisation de la bibliothèque standard

**Objectif :** auditer puis réécrire la stdlib avec les idiomes de propriété,
traits, dérivations, combinateurs, itérateurs et doctests disponibles après
0.7.3, avant de figer une surface candidate en 0.8.0.

**Point de départ mesuré au 28 juillet 2026 :** la stdlib contient 28 modules,
5 634 lignes et 637 symboles publics dans l'inventaire versionné. Aucun module
ne porte encore de commentaire `///`; la référence d'API générée ne peut donc
pas servir de contrat autonome. Les changements de surface doivent rester
intentionnels : les helpers de déplacement et de destruction ne sont supprimés
que lorsqu'un test démontre une sémantique de propriété équivalente.

### R0710-1 — Auditer la stdlib et figer les contrats de migration

- produire un inventaire versionné par module : symboles, propriété, erreurs,
  allocations, dépendances, tests et documentation ;
- classer les API en conservation, refonte interne, dépréciation ou
  remplacement public ;
- mesurer duplications, allocations, branches de nettoyage et couverture de
  tests ;
- définir les budgets de compatibilité, taille, performance et documentation
  des cinq lots suivants.

**Critères d'acceptation :**

- 100 % des modules et symboles publics possèdent un statut et un propriétaire
  de migration dans `docs/audits/stdlib-0.7.10.md` ;
- chaque rupture proposée possède une justification, une migration et une
  fixture N/N+1 avant implémentation ;
- les invariants de déplacement, consommation et destruction des types
  propriétaires sont recensés ;
- un rapport reproductible donne lignes, surface publique, couverture `///`,
  imports de fixtures et principaux motifs dupliqués.

### R0710-2 — Réécrire le cœur fonctionnel et les séquences

- moderniser `std.option`, `std.result`, `std.builder`, `std.iterator`,
  `std.array` et `std.array_builder` ;
- utiliser méthodes `consume`, combinateurs, traits et `?` lorsque leur
  sémantique de propriété est plus claire ;
- mutualiser les états de parcours et fallbacks seulement lorsque les
  nettoyages restent prouvés ;
- conserver des parcours observants et consommateurs explicites.

**Critères d'acceptation :**

- valeurs `Copy` et non-`Copy` couvrent succès, erreur, court-circuit et
  destruction anticipée sous ASan ;
- aucune copie, fuite, double destruction ou consommation implicite n'est
  introduite ;
- les anciens appels conservés restent compatibles, et tout remplacement
  public possède une migration compilée ;
- les benchmarks de pipelines ne régressent pas au-delà du budget fixé par
  R0710-1.

### R0710-3 — Mutualiser collections, hachage et dérivations

- réécrire `std.hash_probe`, `std.hashing`, `std.hashmap` et `std.hashset`
  autour d'invariants partagés ;
- employer `derives Equality, Hashing, Debug` pour les capacités structurelles
  au lieu de code manuel lorsque le contrat est identique ;
- réduire la duplication entre slots, redimensionnement, builders et
  itérateurs ;
- vérifier collisions, tombstones, transferts de clés/valeurs et paniques.

**Critères d'acceptation :**

- une suite commune teste les invariants de table pour map et set ;
- dérivations et implémentations explicites produisent les mêmes résultats sur
  le corpus versionné ;
- clés et valeurs propriétaires passent ASan lors des insertions,
  remplacements, suppressions, redimensionnements et destructions ;
- complexité asymptotique et seuils de croissance restent documentés et
  benchmarkés.

### R0710-4 — Unifier système, texte, flux et ressources natives

- moderniser `std.system`, `std.path`, `std.fs`, `std.io`, `std.process`,
  `std.text`, `std.time`, `std.wall_time`, `std.random` et `std.math` ;
- factoriser conversion des erreurs natives, paramètres de sortie, buffers et
  états fermé/ouvert ;
- utiliser `Result`, `Option`, `?`, destructeurs et types dérivés de façon
  cohérente ;
- préserver octets, UTF-8, codes natifs et fermetures exactement une fois.

**Critères d'acceptation :**

- Linux, macOS et Windows couvrent succès, erreurs, fermetures répétées,
  entrées partielles et chemins Unicode ;
- aucun handle, buffer ou résultat natif ne fuit sous sanitizers et outils de
  plateforme disponibles ;
- les erreurs récupérables ne paniquent pas et conservent opération, catégorie,
  code natif et contexte ;
- les API modifiées possèdent fixtures de compatibilité et migrations.

### R0710-5 — Réécrire les wrappers graphiques et audio

- auditer `std.graphics.*`, ses données copiables, handles propriétaires,
  alias historiques et appels runtime ;
- utiliser dérivations pour les valeurs structurelles et destructeurs pour les
  ressources natives ;
- factoriser les wrappers répétitifs sans masquer les transitions begin/end ;
- séparer clairement API stable proposée, surface expérimentale et détails
  raylib.

**Critères d'acceptation :**

- textures, fontes, shaders, rendertargets, sons et musiques sont libérés
  exactement une fois sur tous les chemins testables ;
- valeurs graphiques copiables utilisent des dérivations cohérentes ;
- tests avec backend factice couvrent chargement invalide, mouvement,
  destruction et begin/end déséquilibré ;
- les alias conservés ou retirés sont recensés dans la migration.

### R0710-6 — Documenter et publier toute la stdlib

- ajouter `///` aux modules, types, variantes, traits, fonctions et membres
  publics ;
- ajouter exemples `// doctest:` pour usages normaux, propriété et erreurs ;
- générer une référence stdlib déterministe avec `janus doc` et la publier sur
  le site ;
- vérifier liens entre symboles, couverture et dérive entre inventaire,
  sources, tests et pages.

**Critères d'acceptation :**

- 100 % de la surface publique non expérimentale possède une documentation
  source et apparaît dans l'index généré ;
- chaque module possède au moins un doctest de succès et les modules à erreur
  structurée un doctest `compile_fail` pertinent ;
- aucun lien documentaire non résolu et aucune déclaration privée/interne
  n'apparaît dans la référence ;
- CI, site et archives génèrent la même référence hors ligne.

**Gate de release 0.7.10 :** inventaire et migrations complets, cinq lots de
modernisation validés sous compatibilité et sanitizers, référence stdlib
déterministe publiée, couverture `///` de 100 % de la surface
non expérimentale et tous les doctests verts.

---

<a id="release-0-8-0"></a>
## Janus 0.8.0 — Audit et gel pré-1.0

**Objectif :** auditer toute la surface publique et publier un candidat documenté pour une future stabilisation, sans ajouter de nouveau sous-système.

### R080-1 — Auditer et geler le candidat de surface publique 0.8

- inventorier syntaxe, sémantique, CLI, manifestes, lockfiles, ABI C et modules de stdlib ;
- étendre compatibilité N/N+1, fuzzing et sanitizers ;
- attribuer un statut stable-candidat ou expérimental à chaque surface ;
- publier migrations 0.5 → 0.8 et limites connues ;
- décider explicitement ce qui manque encore avant une éventuelle 1.0.

**Critères d'acceptation :**

- les fuzzers lexer, parseur, manifeste et résolution exécutent chacun une session sanitizer d'au moins 60 minutes sur leurs corpus versionnés sans crash reproductible ;
- matrice de niveau 1, archives, doctests, packages et compatibilité vertes ;
- l'inventaire versionné `docs/stability-inventory-0.8.md` attribue un statut à 100 % des surfaces publiques et tous ses liens passent le crawler ;
- toute surface non prête est marquée expérimentale ou retirée avant la release ;
- 0.8.0 n'est pas présentée comme 1.0 et ne promet pas encore sa compatibilité définitive.

**Gate de release 0.8.0 :** builds reproductibles, audit public terminé, aucune issue ouverte portant le label `severity:critical` selon la politique de sévérité versionnée, et rapport de préparation 1.0 publié.

---

## Critères communs à chaque issue

Sauf exception explicitement justifiée, une issue de cette roadmap est terminée lorsque :

- un test échoue avant l'implémentation et passe après ;
- `janus check`, `janus build` et l'exécution pertinente sont validés ;
- les tests CTest ciblés et de non-régression passent ;
- documentation, changelog et contrat de stabilité sont mis à jour si la surface publique change ;
- Linux, macOS et Windows sont couverts lorsqu'une frontière runtime/ABI/CLI est touchée ;
- les erreurs et chemins de nettoyage sont testés ;
- aucun secret, artefact généré ou dépendance non reproductible n'est ajouté ;
- la PR référence l'issue avec `Closes #N`.

Une issue est classée `severity:critical` lorsqu'elle démontre au moins un des cas suivants : corruption ou double libération de ressource, contournement de propriété menant à un comportement indéfini, exécution de code ou traversée de chemin via la chaîne de paquets, violation reproductible d'un lockfile, ou crash déterministe du compilateur sur une source Janus valide pour une plateforme de niveau 1. Cette définition est versionnée avec la roadmap afin que la gate 0.8.0 soit vérifiable.

## Politique de release

Pour chaque version :

1. toutes les issues du milestone sont fermées par des PR validées ;
2. le changelog contient une section datée et les migrations nécessaires ;
3. la version et les canaux `janusup` sont cohérents ;
4. les archives des plateformes de niveau 1 passent leur smoke test ;
5. la suite de compatibilité utilise aussi le dernier compilateur publié lorsque nécessaire ;
6. le site de documentation est reconstruit et vérifié ;
7. le tag n'est créé qu'après réussite des gates ;
8. les échecs post-release déclenchent une corrective ciblée plutôt qu'une nouvelle fonctionnalité.

## Gouvernance de la roadmap

- Les issues GitHub constituent l'état d'exécution ; ce document définit l'ordre et le périmètre.
- Chaque issue doit lier la section de release correspondante et reprendre l'identifiant `Rxxx-n`.
- Un changement de portée doit modifier ce document dans la même PR ou expliquer explicitement pourquoi la roadmap reste correcte.
- Les nouvelles idées sont placées après 0.8.0 tant qu'elles ne débloquent pas un critère existant.
- L'ordre peut évoluer avec les résultats techniques, mais une dépendance de propriété, compatibilité ou sécurité ne doit pas être contournée pour respecter une date arbitraire.
