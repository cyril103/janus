# Changelog

Les changements notables de Janus sont documentés dans ce fichier. Le projet
utilise le versionnage sémantique à partir de sa première version publique.

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

[0.1.0]: https://github.com/cyril103/janus/releases/tag/v0.1.0
