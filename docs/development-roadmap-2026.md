# Rapport pour la suite du développement de Janus

Date de l'audit : 24 juillet 2026

Version observée : 0.4.0

## Résumé

Janus possède désormais un socle crédible de langage natif : frontend,
analyse sémantique, backend LLVM, modèle de propriété, modules, collections,
gestionnaire de versions, LSP et graphisme 2D. La roadmap Project Euler a
rempli son rôle : ses 20 issues GitHub sont fermées et ses principaux objectifs
sont visibles dans les versions 0.3.0 et 0.4.0.

La priorité ne devrait plus être d'ajouter rapidement des constructions de
langage isolées. Le prochain cycle doit rendre l'expérience quotidienne
prévisible :

1. supprimer les courses et collisions de fichiers temporaires ;
2. fournir les briques de bibliothèque standard nécessaires aux applications ;
3. améliorer les petites frictions syntaxiques et les outils d'édition ;
4. définir explicitement le contrat de stabilité menant à Janus 1.0.

## État vérifié

### Points solides

- Compilation native LLVM et exécutables autonomes.
- Typage explicite et couverture large des types numériques.
- Valeurs immuables et mutables, classes, structs, enums, traits et génériques.
- Modèle de propriété explicite avec `move`, `delete`, `defer` et `Copy`.
- Modules, dépendances locales/Git/registre, lockfile et mode hors ligne.
- Commandes `check`, `build`, `run`, `test` et `fmt`.
- LSP avec diagnostics, survol, définition, références, complétion et formatage.
- API graphique couvrant déjà fenêtres, entrées, audio, textures, polices,
  caméras, render textures, shaders et manettes.
- Corpus de tests conséquent et suite Project Euler automatisée.

### Résultat de validation

La suite de `build-release` contient 81 tests CTest. Lancée avec quatre workers,
elle termine avec 80 réussites et un échec :

```text
cli.project_creation:
ld.lld: error: cannot open /tmp/janus-1804289383.o
```

Le même test passe seul en 0,56 seconde. La cause est visible dans
`tools/janus/main.cpp` : les objets et exécutables temporaires utilisent
`std::rand()` sans graine ni garantie d'exclusivité. Plusieurs processus Janus
produisent donc le même premier nom et peuvent supprimer mutuellement leurs
artefacts.

Ce défaut doit bloquer la prochaine release corrective : une chaîne de
compilation ne doit pas devenir non déterministe dès que les tests ou builds
sont parallélisés.

## Roadmap recommandée

## Phase 0 — Correctif 0.4.1

### P0. Fichiers temporaires sûrs

Remplacer tous les chemins fondés sur `std::rand()` par une abstraction
portable créant atomiquement un fichier ou dossier unique. L'abstraction doit
nettoyer ses artefacts avec RAII, y compris après une erreur de compilation ou
d'édition de liens.

Critères de sortie :

- `ctest --test-dir build-release -j 8 --repeat until-fail:20` est stable ;
- deux commandes `janus build` ou `janus run` concurrentes ne partagent aucun
  artefact ;
- le nettoyage est testé sur succès et erreur ;
- Linux, macOS et Windows utilisent le même contrat.

### P0. Cohérence documentaire immédiate

Le README annonce encore « Janus 0.2 » alors que la version courante est 0.4.0.
La fin de `docs/graphics.md` affirme que les polices personnalisées et manettes
ne sont pas disponibles, après avoir documenté précisément ces deux API.

Ces contradictions doivent être corrigées et protégées, lorsque possible, par
des tests de snippets ou des contrôles de version automatisés.

## Phase 1 — Janus 0.5 : applications utiles sans interop C

### P1. Formatage et conversion de texte

Un programme graphique ne peut pas simplement transformer un score numérique
en `string`. Il doit dessiner ses propres chiffres ou appeler une fonction C
variadique. Une bibliothèque standard moderne doit fournir au minimum :

- conversion des primitives vers `string` ;
- parsing avec `Result` ;
- concaténation ou builder de chaînes ;
- formatage sûr et typé pour les cas courants ;
- tests UTF-8, valeurs limites et erreurs.

La conception doit clarifier la propriété des chaînes produites et éviter de
faire de `std.c.printf` l'API normale.

### P1. Temps et nombres pseudo-aléatoires

Le Snake d'exemple doit simuler le temps avec un compteur à 60 FPS et intégrer
un générateur congruentiel local. Ajouter :

- une horloge monotone et une durée ;
- une source de temps murale séparée ;
- un PRNG déterministe initialisable pour les tests ;
- une source initialisée automatiquement pour les applications ;
- des méthodes bornées sans biais de modulo documentées.

Les API de temps ne doivent pas dépendre de `std.graphics`, même si le module
graphique expose ensuite `frameTime()` ou `elapsedTime()`.

### P1. Chaînes conditionnelles avec `else if`

Le parser exige actuellement un bloc immédiatement après `else`. Les branches
à choix multiples deviennent une série de `if` indépendants ou des blocs
imbriqués. Accepter `else if` améliorerait la lisibilité sans introduire une
nouvelle sémantique : il peut être représenté comme un `IfStatement` imbriqué.

La modification doit couvrir parser, formateur, diagnostics, LSP et tests.

### P1. Index LSP à l'échelle du workspace

Le serveur construit son index depuis le document actif, les documents ouverts
et les modules rencontrés via leurs imports. Il manque encore une vue stable de
l'ensemble du projet et de ses dépendances.

Objectifs :

- index initial du manifeste, de `src/`, de `tests/` et des dépendances ;
- invalidation incrémentale sur sauvegarde, création et suppression ;
- définition/références fiables sans ouvrir préalablement les fichiers ;
- symboles de workspace ;
- budgets mémoire et latence mesurés sur un projet de référence.

## Phase 2 — Janus 0.6 : graphisme et outillage

### P2. Boucle de jeu et composition graphique

`std.graphics` est déjà riche, mais il manque quelques primitives qui évitent
les contournements dans une application réelle :

- temps par image et temps écoulé ;
- modes de fusion, notamment alpha et additif pour les halos ;
- capture d'écran ;
- contrôle explicite du wrapping des textures ;
- diagnostic distinct entre shader invalide et shader de repli.

Chaque ajout doit rester indépendant des structures natives de raylib et avoir
un test avec le faux backend.

### P2. Distribution de l'expérience éditeur

Publier l'extension VS Code sur la Marketplace, automatiser sa validation et
documenter la compatibilité entre version de l'extension, `janus-lsp` et
toolchain. L'installation du langage ne doit pas exiger de trouver manuellement
un ancien fichier VSIX dans le dépôt.

## Phase 3 — Contrat Janus 1.0

Avant d'annoncer une stabilité 1.0, formaliser dans un document versionné :

- syntaxe et sémantique garanties ;
- règles de durée de vie, déplacement, destruction et panique ;
- représentation et ABI des types exposés à C ;
- politique d'overflow et comportement des conversions ;
- compatibilité des manifestes et lockfiles ;
- politique de dépréciation ;
- niveau de compatibilité de la bibliothèque standard ;
- plateformes officiellement supportées.

Ajouter une suite de compatibilité contenant de petits programmes compilés avec
la version N puis reconstruits ou exécutés avec N+1 selon le contrat retenu.

## Ordre recommandé

1. Publier 0.4.1 avec la correction des temporaires et la documentation alignée.
2. Livrer ensemble formatage de texte, temps et hasard dans 0.5 : ces modules
   débloquent beaucoup plus d'exemples que de nouvelles syntaxes complexes.
3. Ajouter `else if` et l'index LSP workspace dans le même cycle DX.
4. Polir `std.graphics` et distribuer officiellement l'extension en 0.6.
5. Geler progressivement le contrat public et lancer la suite de compatibilité
   avant 1.0.

## Indicateurs de progression

- Suite complète stable avec `-j 8` et répétition.
- Aucun exemple officiel ne dépend de `std.c` pour une opération courante.
- Temps de premier diagnostic LSP et consommation mémoire suivis en CI.
- Tous les snippets des guides compilés automatiquement.
- Liste des API expérimentales explicitement suivie à chaque release.
- Zéro contradiction entre version du README, changelog, extension et CLI.

## Issues proposées

1. [#41 — fix(cli): create race-safe temporary build artifacts](https://github.com/cyril103/janus/issues/41)
2. [#42 — docs: align README and graphics guide with Janus 0.4](https://github.com/cyril103/janus/issues/42)
3. [#43 — feat(std): add safe primitive string conversion and formatting](https://github.com/cyril103/janus/issues/43)
4. [#44 — feat(std): add monotonic time and deterministic random modules](https://github.com/cyril103/janus/issues/44)
5. [#45 — feat(parser): support else-if chains](https://github.com/cyril103/janus/issues/45)
6. [#46 — feat(lsp): index the complete project workspace](https://github.com/cyril103/janus/issues/46)
7. [#47 — feat(graphics): expose frame timing and blend modes](https://github.com/cyril103/janus/issues/47)
8. [#48 — docs(design): define the Janus 1.0 stability contract](https://github.com/cyril103/janus/issues/48)
