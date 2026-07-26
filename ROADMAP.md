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
| 0.5.2 | Diagnostics et paniques | erreurs structurées, actionnables et exploitables par les outils |
| 0.6.0 | Collections propriétaires | valeurs non-`Copy` stockées, déplacées et détruites sans fuite ni double libération |
| 0.6.1 | `Option` et `Result` | composition ergonomique des absences et erreurs, y compris avec propriété |
| 0.6.2 | Dérivations | génération sûre et explicite de `Copy`, égalité, hachage et `Debug` |
| 0.7.0 | Bibliothèque système | fichiers, chemins, flux, environnement et processus multiplateformes |
| 0.7.1 | Documentation et éditeur | documentation d'API générée, doctests et expérience LSP publiable |
| 0.7.2 | Construction mesurable | timings puis cache incrémental correct et invalidation reproductible |
| 0.8.0 | Écosystème et gel | registre distant sécurisé, audit public et candidat stable pré-1.0 |

## Graphe de dépendances

```text
0.5.1
  └── 0.5.2
        └── 0.6.0
              └── 0.6.1
                    └── 0.6.2
                          └── 0.7.0
                                └── 0.7.1
                                      └── 0.7.2
                                            └── 0.8.0
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

- aucune contradiction connue entre `docs/`, `stdlib/std/`, `janus --help` et le changelog ;
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
## Janus 0.5.2 — Diagnostics et paniques

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
- une entrée invalide ne provoque ni crash ni exception non interceptée.

### R052-2 — Ajouter rendus humain/JSON, suggestions et récupération

- fournir un rendu terminal avec extrait de source et repères ;
- ajouter `--diagnostic-format human|json` aux commandes d'analyse et construction ;
- représenter les suggestions de correction de manière structurée ;
- récupérer après certaines erreurs indépendantes afin d'en signaler plusieurs sans cascade illisible.

**Critères d'acceptation :**

- le JSON possède un schéma et des fixtures versionnées ;
- stdout/stderr et codes de sortie restent conformes au contrat CLI ;
- le rendu reste lisible sans couleur et sous Windows ;
- les suggestions ne modifient jamais automatiquement les sources.

### R052-3 — Ajouter contexte source et trace aux paniques de développement

- transmettre fichier, ligne et fonction aux appels de panique générés ;
- produire une pile symbolisée lorsque la plateforme le permet ;
- conserver les nettoyages `defer` et destructeurs garantis ;
- définir une option pour désactiver/raccourcir les traces en release.

**Critères d'acceptation :**

- division par zéro, index hors limites et `panic` explicite indiquent leur origine ;
- les tests vérifient le nettoyage avant terminaison ;
- les trois plateformes terminent sans deadlock ni crash du mécanisme de trace ;
- le format exact reste déclaré expérimental avant 1.0.

**Gate de release 0.5.2 :** diagnostics structurés utilisés par CLI et LSP, fixtures JSON stables pour la série 0.5.x, paniques de debug localisables.

---

<a id="release-0-6-0"></a>
## Janus 0.6.0 — Collections de valeurs propriétaires

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
- les types `Copy` ne subissent pas de régression fonctionnelle mesurable.

### R060-3 — Étendre `HashSet`, `HashMap` et builders aux valeurs propriétaires

- appliquer le contrat aux clés et valeurs ;
- traiter correctement remplacement, suppression, tombstones et rehash ;
- définir le contrat de hachage/égalité pour l'observation non propriétaire ;
- garantir le nettoyage lors d'une allocation ou d'un rehash échoué.

**Critères d'acceptation :**

- insertions et suppressions de clés/valeurs propriétaires sont testées ;
- chaque élément est détruit exactement une fois ;
- rehash et collisions possèdent des tests de stress déterministes ;
- les collections passent sous ASan/UBSan sur les runners compatibles.

### R060-4 — Ajouter itérateurs observants et consommateurs

- distinguer itération qui observe et itération qui transfère les éléments ;
- adapter `map`, `filter`, `flatMap`, `take`, `fold` et builders ;
- garantir le nettoyage des éléments non consommés lors d'un arrêt anticipé ;
- documenter le coût et l'état final du conteneur.

**Critères d'acceptation :**

- `for` et les adaptateurs ne copient pas implicitement une valeur propriétaire ;
- `break`, `continue`, `return`, `?` et panique nettoient correctement l'état ;
- les chaînes d'itérateurs fonctionnent avec types `Copy` et propriétaires ;
- la suite de compatibilité contient les comportements 0.6.0 retenus.

**Gate de release 0.6.0 :** contrat documenté, collections principales move-aware, tests sanitizer et matrice multiplateforme verts, guide de migration 0.5 → 0.6 publié.

---

<a id="release-0-6-1"></a>
## Janus 0.6.1 — Composition de `Option` et `Result`

**Objectif :** réduire les `match` répétitifs sans masquer la propriété ni encourager les paniques.

### R061-1 — Enrichir `Option[T]`

Ajouter une surface minimale cohérente, notamment `isSome`, `isNone`, `map`, `andThen`, `orElse`, `unwrapOr` et variantes consommantes nécessaires.

**Critères d'acceptation :**

- les opérations fonctionnent avec `T : Copy` et avec `T` propriétaire ;
- aucune extraction ne duplique une ressource ;
- les méthodes non consommantes et consommantes sont distinguées ;
- exemples, signatures et cas limites sont documentés et compilés.

### R061-2 — Enrichir `Result[T, E]` et fiabiliser `?`

Ajouter `isOk`, `isError`, `map`, `mapError`, `andThen`, `orElse`, `unwrapOr` et conversions documentées entre `Option` et `Result`; valider `?` sur valeurs propriétaires.

**Critères d'acceptation :**

- succès et erreur nettoient exactement les variantes actives ;
- `?` transfère correctement `T` ou `E` sans réutilisation après move ;
- les API système futures peuvent retourner un `Result` sans allocation obligatoire ;
- les tests couvrent fonctions, closures, boucles et `defer`.

**Gate de release 0.6.1 :** API documentée, exemples compilés, propriété et propagation couvertes par la suite de compatibilité.

---

<a id="release-0-6-2"></a>
## Janus 0.6.2 — Dérivations sûres

**Objectif :** réduire le code répétitif pour les capacités structurelles courantes sans introduire un système général de macros.

### R062-1 — Définir syntaxe et règles de dérivation

- choisir une syntaxe explicite pour demander les dérivations ;
- définir l'éligibilité champ par champ ;
- spécifier diagnostics, visibilité et interactions avec génériques/traits ;
- interdire les dérivations qui compromettraient la propriété.

**Critères d'acceptation :**

- le design couvre structs, enums et classes lorsque pertinent ;
- une dérivation impossible produit un diagnostic indiquant le champ fautif ;
- aucune capacité n'est dérivée implicitement sans demande ;
- la syntaxe est protégée par parser, formatter et LSP.

### R062-2 — Implémenter `Copy`, égalité, hachage et `Debug`

- générer les implémentations structurelles ;
- intégrer égalité/hachage aux collections ;
- fournir une représentation `Debug` déterministe adaptée aux diagnostics/tests ;
- empêcher `Copy` pour tout agrégat propriétaire.

**Critères d'acceptation :**

- égalité et hachage respectent le même contrat ;
- `HashSet`/`HashMap` acceptent un type utilisateur dérivé sans boilerplate manuel ;
- la sortie `Debug` est testée mais reste distincte du format utilisateur ;
- la matrice CI et la suite de compatibilité couvrent les quatre capacités.

**Gate de release 0.6.2 :** dérivations minimales stabilisées pour la série 0.6.x, formatter/LSP/doc alignés, aucune macro générale ajoutée.

---

<a id="release-0-7-0"></a>
## Janus 0.7.0 — Bibliothèque système multiplateforme

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

### R070-3 — Ajouter `std.io` et les flux tamponnés

- lecture/écriture séquentielle ;
- buffers, EOF, flush et fermeture ;
- stdin, stdout et stderr ;
- intégration texte/UTF-8 sans supposer qu'un flux binaire est valide.

**Critères d'acceptation :**

- lectures partielles et écritures partielles sont gérées ;
- fermer deux fois ou utiliser après fermeture produit un diagnostic/erreur défini ;
- les buffers propriétaires sont nettoyés sur tous les chemins ;
- exemples de copie de fichier et traitement ligne par ligne compilent et s'exécutent.

### R070-4 — Ajouter arguments, environnement et processus

- arguments du programme et variables d'environnement ;
- lancement de processus avec arguments séparés ;
- code de sortie, stdout/stderr capturables et répertoire de travail ;
- contrat explicite sans passage implicite par un shell.

**Critères d'acceptation :**

- les arguments contenant espaces/Unicode restent intacts ;
- le code de sortie enfant est observable ;
- absence de commande et refus d'accès retournent `Result` ;
- les tests n'abandonnent aucun processus enfant.

**Gate de release 0.7.0 :** exemple CLI complet sans FFI utilisateur, API `Result` cohérente, tests multiplateformes et documentation de sécurité.

---

<a id="release-0-7-1"></a>
## Janus 0.7.1 — Documentation d'API et expérience éditeur

**Objectif :** rendre une bibliothèque Janus découvrable depuis le terminal, le site et l'éditeur.

### R071-1 — Ajouter les commentaires de documentation et `janus doc`

- définir la syntaxe des commentaires publics ;
- les conserver dans l'AST/index public ;
- générer une documentation HTML statique par paquet ;
- fournir `janus doc` et `janus doc --open`.

**Critères d'acceptation :**

- modules, types, variantes, traits, fonctions et membres publics sont indexés ;
- les éléments privés sont exclus par défaut ;
- les liens entre symboles sont résolus ou signalés ;
- la génération est déterministe et fonctionne hors ligne.

### R071-2 — Compiler les exemples de documentation comme doctests

- identifier les blocs Janus exécutables et ceux volontairement incomplets ;
- compiler les exemples avec le contexte du paquet ;
- permettre les attentes compile-fail avec code de diagnostic ;
- intégrer les doctests à `janus test` et à la CI.

**Critères d'acceptation :**

- une API renommée casse le doctest correspondant ;
- les exemples d'erreur vérifient un code, pas une ponctuation fragile ;
- les tests sont filtrables et leurs échecs indiquent document/ligne ;
- le site Janus utilise le même mécanisme pour ses extraits.

### R071-3 — Compléter navigation et assistance LSP

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

### R071-4 — Ajouter code actions et publier l'extension VS Code

- actions pour imports manquants, branches `match` manquantes et corrections sûres ;
- affichage riche des diagnostics structurés ;
- matrice de compatibilité extension/toolchain ;
- publication documentée sur la Marketplace.

**Critères d'acceptation :**

- chaque action propose un `WorkspaceEdit` testé ;
- aucune action ambiguë ne s'applique automatiquement ;
- VSIX et version Marketplace proviennent du même commit/tag ;
- installation, mise à jour et choix de `janus-lsp` sont testés.

**Gate de release 0.7.1 :** docs API générées, doctests actifs, extension publiable et matrice LSP verte.

---

<a id="release-0-7-2"></a>
## Janus 0.7.2 — Construction mesurable et incrémentale

**Objectif :** réduire le temps de feedback sans compromettre la correction ni la reproductibilité.

### R072-1 — Exposer des timings et benchmarks de compilation

- mesurer chargement, parsing, analyse, génération LLVM, optimisation et lien ;
- ajouter une sortie humaine et JSON ;
- versionner plusieurs projets de benchmark petits/moyens ;
- publier une tendance sans transformer chaque variation en échec CI.

**Critères d'acceptation :**

- `janus build --timings` explique la totalité du temps mesuré ;
- le JSON est exploitable par CI ;
- le coût de la mesure est documenté ;
- un dashboard permet de détecter les régressions majeures.

### R072-2 — Ajouter cache incrémental et invalidation par interface

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

**Gate de release 0.7.2 :** métriques publiées, cache correct avant d'être rapide, corruption/collision couvertes et fallback build propre disponible.

---

<a id="release-0-8-0"></a>
## Janus 0.8.0 — Écosystème et gel pré-1.0

**Objectif :** livrer un écosystème de paquets distant minimal et transformer la surface publique en candidat crédible pour une future stabilisation.

### R080-1 — Versionner le protocole et le modèle de sécurité du registre

- spécifier index, métadonnées, archive, checksums et négociation de version ;
- définir noms, espaces de noms, immutabilité, yanking et résolution ;
- documenter authentification, autorisation, menaces et récupération ;
- garantir que le lockfile reste la source reproductible.

**Critères d'acceptation :**

- protocole `v1` documenté avec schémas et fixtures ;
- une version publiée ne peut pas être remplacée ;
- un paquet retiré reste reproductible via un lockfile existant selon politique ;
- confusion de dépendance et traversée de chemin possèdent des tests négatifs.

### R080-2 — Ajouter recherche, téléchargement et publication distante au CLI

- configurer un registre par défaut et des registres explicites ;
- implémenter authentification, `search`, résolution, téléchargement et `publish` ;
- préserver `--locked` et `--offline` ;
- utiliser cache et téléchargements atomiques vérifiés.

**Critères d'acceptation :**

- une publication puis installation fonctionne sur les trois plateformes ;
- archive ou checksum invalide est rejeté avant extraction ;
- aucun secret n'apparaît dans logs, lockfiles ou diagnostics ;
- interruption réseau ne laisse pas un cache considéré valide.

### R080-3 — Déployer un registre de référence avec provenance

- fournir un service de référence déployable et sauvegardable ;
- intégrer attestations de provenance/signatures selon le modèle retenu ;
- exposer yank, métadonnées et audit ;
- publier procédures d'administration et de réponse à incident.

**Critères d'acceptation :**

- déploiement de référence reproductible ;
- sauvegarde/restauration testée ;
- publication non autorisée et remplacement de version sont refusés ;
- disponibilité du registre n'est pas requise pour un build `--locked --offline` déjà mis en cache.

### R080-4 — Auditer et geler le candidat de surface publique 0.8

- inventorier syntaxe, sémantique, CLI, manifestes, lockfiles, ABI C et modules de stdlib ;
- étendre compatibilité N/N+1, fuzzing et sanitizers ;
- attribuer un statut stable-candidat ou expérimental à chaque surface ;
- publier migrations 0.5 → 0.8 et limites connues ;
- décider explicitement ce qui manque encore avant une éventuelle 1.0.

**Critères d'acceptation :**

- aucun crash connu du compilateur sur les corpus/fuzzers retenus ;
- matrice de niveau 1, archives, doctests, packages et compatibilité vertes ;
- documentation publique complète et crawlée ;
- toute surface non prête est marquée expérimentale ou retirée avant la release ;
- 0.8.0 n'est pas présentée comme 1.0 et ne promet pas encore sa compatibilité définitive.

**Gate de release 0.8.0 :** registre minimal opérationnel, builds reproductibles, audit public terminé, zéro issue critique ouverte et rapport de préparation 1.0 publié.

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
