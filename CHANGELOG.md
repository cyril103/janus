# Changelog

Les changements notables de Janus sont documentés dans ce fichier. Le projet
utilise le versionnage sémantique à partir de sa première version publique.

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

[0.2.0]: https://github.com/cyril103/janus/releases/tag/v0.2.0
[0.1.0]: https://github.com/cyril103/janus/releases/tag/v0.1.0
