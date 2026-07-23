# Changelog

Les changements notables de Janus sont documentés dans ce fichier. Le projet
utilise le versionnage sémantique à partir de sa première version publique.

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

[0.3.0]: https://github.com/cyril103/janus/releases/tag/v0.3.0
[0.2.1]: https://github.com/cyril103/janus/releases/tag/v0.2.1
[0.2.0]: https://github.com/cyril103/janus/releases/tag/v0.2.0
[0.1.0]: https://github.com/cyril103/janus/releases/tag/v0.1.0
