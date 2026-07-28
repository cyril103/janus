# Changelog

Les changements notables de Janus sont documentés dans ce fichier. Le projet
utilise le versionnage sémantique à partir de sa première version publique.

## [0.6.2] - Non publiée

### Bibliothèque standard

- ajout des combinateurs `isSome`, `isNone`, `map`, `andThen`, `orElse` et
  `unwrapOr` dans `std.option`, avec observation bornée et variantes
  consommantes pour les valeurs propriétaires ;
- destruction exacte des valeurs de repli et closures inutilisées, sans
  extraction ni copie implicite d'une ressource.

## [0.6.1] - Non publiée

### Langage et bibliothèque standard

- ajout des parcours consommateurs `intoIterator()` pour `Array` et `HashSet`
  et `intoEntries()` pour `HashMap`, avec transfert explicite des valeurs
  propriétaires et destruction des éléments non visités ;
- adaptation des pipelines `map`, `filter`, `flatMap`, `take`, `fold` et des
  builders aux éléments propriétaires, sans copie implicite ;
- nettoyage des itérateurs lors des sorties `break`, `continue`, `return`, `?`
  et des paniques, et diagnostics des tentatives d'échappement depuis une
  observation bornée.

## [0.6.0] - 2026-07-27

Cette version établit le contrat de propriété des conteneurs et permet aux
collections principales de stocker, déplacer et détruire correctement des
valeurs propriétaires.

### Langage et bibliothèque standard

- définition du contrat de propriété des conteneurs : observation bornée,
  transferts explicites, invalidation, réallocation et nettoyage des valeurs
  propriétaires ;
- prise en charge des classes, structs et enums propriétaires dans `Array[T]`,
  avec transferts explicites, destruction exacte lors de `set`, `clear` et de
  la destruction du tableau, et nouvelles opérations `replace` et `remove`.
- extension de `HashSet`, `HashMap` et des builders aux éléments propriétaires,
  avec rehash par déplacement, tombstones non propriétaires, remplacement et
  suppression sans double destruction, et observation bornée pour le hachage.

## [0.5.2] - Non publiée

### Diagnostics

- ajout des rendus terminal avec extraits et repères, et JSON conforme à un
  schéma 0.5.2 versionné pour `janus check` et `janus build` ;
- représentation structurée des suggestions sans modification automatique des
  sources ;
- récupération du parseur entre déclarations indépendantes afin de publier
  plusieurs diagnostics sans cascade.

## [0.5.1] - Non publiée

### Documentation

- ajout d'un inventaire JSON versionné de la surface publique 0.5.x couvrant
  les modules et symboles de la bibliothèque standard ainsi que les commandes
  et options affichées par `janus --help` ;
- classement explicite des API candidates à la stabilité, des API
  expérimentales et du module d'implémentation `std.hash_probe` ;
- publication de l'inventaire avec la référence du site et vérification des
  exemples Janus autonomes et des liens internes ;
- détection automatique des dérives de symboles, signatures et options CLI,
  avec fixture obsolète ciblée et exécution des extraits et du crawler dans la
  CI Pages.

## [0.5.0] - 2026-07-25

Cette version enrichit la bibliothèque standard et le graphisme, étend
l'analyse des projets complets dans les outils et prépare les garanties de
compatibilité attendues pour Janus 1.0.

### Langage et bibliothèque standard

- ajout des chaînes `else if` au parseur et à l'analyse sémantique ;
- ajout des modules `std.time`, `std.wall_time` et `std.random`, avec contrôles
  d'erreur et primitives natives associées ;
- ajout des conversions de texte sûres, du formatage des valeurs primitives et
  d'une API de construction de texte.

### Graphisme et exemples

- ajout du temps de trame, de la limitation du nombre d'images par seconde et
  des modes de fusion au module graphique ;
- ajout d'un exemple complet de jeu Snake néon et de sa documentation.

### Outils et fiabilité

- indexation LSP de l'ensemble d'un projet, y compris les fichiers non ouverts,
  les tests et les dépendances locales ;
- création d'artefacts temporaires sûre en présence de compilations
  concurrentes, avec nettoyage automatique ;
- ajout d'un contrat de stabilité 1.0 et d'un harnais de compatibilité N/N+1 ;
- mise à jour du client de langage VS Code et de ses dépendances afin de
  corriger les alertes de sécurité connues.

## [0.4.0] - 2026-07-24

Cette version renforce le modèle de propriété, consolide les symboles entre
modules et rend les outils plus précis sur les programmes répartis dans
plusieurs fichiers.

### Langage et modèle de propriété

- ajout de la contrainte générique intrinsèque `Copy` ;
- déplacement et destruction récursive des structs et enums propriétaires ;
- initialisation et finalisation sûres des agrégats globaux, y compris en cas
  de panique ;
- évaluation constante des conversions numériques primitives et des
  initialiseurs d'agrégats ;
- contextualisation des littéraux entiers pleine largeur.

### Modules et outils

- qualification cohérente des valeurs, fonctions, types et constructeurs
  d'enums entre modules ;
- visibilité privée étendue aux fonctions externes, types et membres internes ;
- index LSP enrichi pour les imports, symboles masqués et références.

### Bibliothèque standard et graphisme

- sécurisation des collections et itérateurs autour des valeurs copiables ;
- extraction du probing commun aux tables de hachage ;
- découpage de `std.graphics` en sous-modules et uniformisation des couleurs
  autour du type `Color` ;
- masquage des primitives natives internes du backend graphique.

## [0.3.0] - 2026-07-23

Cette version étend le langage numérique, introduit un module graphique 2D
typé et renforce les outils de validation de programmes Janus.

### Langage et bibliothèque standard

- ajout des types `ubyte`, `uint`, `long`, `ulong`, `isize`, `float` et des
  entiers courts, avec une sémantique explicite des conversions et
  débordements ;
- ajout des structures définies et construites directement comme valeurs ;
- ajout des `val` et `var` globales qualifiées par module, avec expressions
  constantes, initialisation dynamique, visibilité privée et destruction
  automatique des valeurs possédées ;
- ajout d'utilitaires mathématiques et de factorisation entière ;
- amélioration des diagnostics pour les retours manquants et les déclarations
  non prises en charge au niveau supérieur.

### Graphisme 2D

- nouveau module graphique typé fondé sur un backend raylib chargé
  dynamiquement ;
- gestion des fenêtres, entrées, textures, sons, musiques, polices UTF-8,
  caméras, sprites, animations, render textures et shaders ;
- ajout d'un script d'installation de raylib et d'une documentation dédiée.

### Outils et fiabilité

- harmonisation de l'interface des commandes d'exécution ;
- ajout d'un corpus Project Euler 1 à 20 et d'un validateur produisant des
  résultats structurés, avec budgets et garde-fous d'exécution ;
- diagnostics LSP corrects pour les modules ne déclarant pas de point d'entrée.

## [0.2.1] - 2026-07-20

Cette version corrective fiabilise les diagnostics du serveur de langage,
l'empaquetage de l'extension VS Code et les mises à jour avec GitHub CLI.

### Serveur de langage

- résolution des imports lors de la production des diagnostics ;
- conservation sûre des messages de diagnostic pendant leur publication ;
- suppression des diagnostics lorsqu'un document est fermé ;
- réponse JSON `null` correcte lorsqu'un symbole demandé est introuvable.

### Extension VS Code

- suppression de l'événement d'activation devenu redondant ;
- ajout des métadonnées, de la licence et des mentions légales au VSIX ;
- bundle minifié limitant l'extension à quelques fichiers ;
- construction et archivage du VSIX dans l'intégration continue.

### Gestionnaire d'installation

- compatibilité avec les installations de GitHub CLI ne prenant pas en charge
  la vérification des attestations.

## [0.2.0] - 2026-07-19

Cette version rend la chaîne d'outils Janus réellement multiplateforme et
améliore fortement l'expérience dans les éditeurs.

### Plateformes et distribution

- ajout des archives officielles macOS ARM64 et Windows x86_64 ;
- validation de l'archive Linux sur Ubuntu 24.04, Fedora 42 et openSUSE
  Tumbleweed ;
- archives autonomes incluant les runtimes natifs nécessaires à Clang et LLD ;
- amélioration de l'installation, de l'activation et des lockfiles sous
  Windows ;
- vérification automatique des attestations de provenance par `janusup`
  lorsque GitHub CLI est disponible.

### Éditeurs et formatage

- ajout des diagnostics en direct dans `janus-lsp` ;
- ajout du survol, de la navigation vers la définition, des références et de
  l'autocomplétion ;
- prise en charge du formatage de document par LSP ;
- configuration de `janus fmt` avec `.janusfmt` et conservation des
  commentaires ;
- ajout d'une extension VS Code avec coloration syntaxique et détection
  automatique de `janus-lsp`.

### Documentation

- nouveau README destiné aux débutants ;
- guides séparés pour l'installation, le langage, les outils et la compilation
  depuis les sources.

### Limites connues

- l'index du serveur LSP reste principalement limité aux documents ouverts ;
- l'extension VS Code est fournie dans le dépôt mais n'est pas encore publiée
  sur la marketplace ;
- le langage, la bibliothèque standard et le format des paquets restent
  expérimentaux avant 1.0.

## [0.1.0] - 2026-07-19

Première version expérimentale de Janus, distribuée pour Linux x86_64.

### Langage

- types primitifs fortement typés, chaînes UTF-8 et conversions explicites ;
- fonctions, closures génériques et valeurs fonctionnelles de premier ordre ;
- classes et enums génériques, visibilité privée et filtrage par motif ;
- collections standard, itérateurs paresseux et builders ;
- contrôle de flux, `defer`, pointeurs bruts et gestion manuelle de la mémoire ;
- interopérabilité C avec les déclarations `extern def`.

### Outils

- compilation native fondée sur LLVM, Clang et LLD ;
- commandes `new`, `init`, `check`, `build`, `run`, `test` et gestion de projet ;
- dépendances locales, Git et registre, résolution SemVer, lockfile et mode
  hors-ligne ;
- gestionnaire de chaînes `janusup`, canaux de mise à jour et installateurs
  Unix/PowerShell ;
- archives autonomes accompagnées d'une somme SHA-256 et d'une attestation de
  provenance.

### Limites connues

- seule une archive Linux x86_64 est publiée dans cette version ;
- le langage, sa bibliothèque standard et le format des paquets restent
  expérimentaux et peuvent évoluer sans compatibilité ascendante avant 1.0.

[0.6.2]: https://github.com/cyril103/janus/compare/v0.6.1...HEAD
[0.6.1]: https://github.com/cyril103/janus/compare/v0.6.0...HEAD
[0.6.0]: https://github.com/cyril103/janus/releases/tag/v0.6.0
[0.5.0]: https://github.com/cyril103/janus/releases/tag/v0.5.0
[0.4.0]: https://github.com/cyril103/janus/releases/tag/v0.4.0
[0.3.0]: https://github.com/cyril103/janus/releases/tag/v0.3.0
[0.2.1]: https://github.com/cyril103/janus/releases/tag/v0.2.1
[0.2.0]: https://github.com/cyril103/janus/releases/tag/v0.2.0
[0.1.0]: https://github.com/cyril103/janus/releases/tag/v0.1.0
