# Migration de Janus 0.5 vers 0.8

> Guide historique conservé pour les anciens projets.

Cette synthèse complète les entrées détaillées du
[changelog](../../CHANGELOG.md).
Après chaque étape, régénérez `janus.lock`, lancez `janus fmt --check`, puis
`janus check --locked --offline` une fois les dépendances mises en cache.

## Langage et propriété

- Remplacez les copies implicites de valeurs possédées par `move`, un emprunt
  ou une reconstruction explicite. La réutilisation après transfert est
  désormais rejetée.
- Adaptez les destructeurs et `defer` à la destruction récursive exactement une
  fois des classes, closures, structs, enums et conteneurs.
- Déclarez explicitement les capacités structurelles avec `derives`; une
  ressemblance de signature seule ne satisfait plus un contrat.
- Traitez les nouveaux diagnostics d'overflow, de déplacement et de branches
  `match` comme des erreurs de source, sans dépendre de leur texte exact.

## Bibliothèque et projets

- Préférez les opérations sûres renvoyant `Option` ou `Result` pour les
  collections, chemins, fichiers, flux, processus et services système.
- Les `match` servant uniquement à transformer ou combiner `Option`/`Result`
  peuvent migrer vers `map2`, `fold`, `filter`, `flatten` ou `transpose`. Les
  fallbacks coûteux doivent utiliser `unwrapOrElse`/`orElseWith`; `unwrapOr` et
  `orElse` restent stricts et évaluent leur argument avant l'appel. `zip` et
  `map2` sélectionnent la branche gauche en premier.
- Les manifestes exigent `[package]` avec `name`, `version` et `entry`.
  Convertissez les dépendances en entrée `path`, `git` avec révision complète,
  ou registre avec contrainte de version.
- Régénérez les anciens lockfiles au format 1 sans `--locked`, examinez la
  source et les empreintes résolues, puis réactivez `--locked`.
- Les imports graphiques restent acceptés, mais sont explicitement
  expérimentaux : isolez-les derrière votre propre façade.

## Outils et automatisation

- Utilisez `janus test --doc` pour les exemples exécutables et
  `janus doc --offline` pour une génération reproductible.
- Les builds automatisés doivent employer `--locked --offline` après
  amorçage, et peuvent utiliser `--no-cache` pour diagnostiquer une divergence.
- Consommez les diagnostics structurés plutôt que le texte humain et validez
  les archives par leur fichier SHA-256.

Le [tableau de stabilité 0.8](../stability-inventory-0.8.md) indique quelles
surfaces peuvent encore changer. Aucun élément expérimental ne doit être traité
comme une promesse 1.0.
