# Registre Janus de référence

Ce répertoire contient le service déployable qui implémente le protocole Janus
Registry v1. Il n'utilise que la bibliothèque standard Python, conserve les
archives dans un magasin de blobs immuables et les index dans SQLite.

Le lancement local minimal, la configuration de production, la gestion des
jetons, les sauvegardes, la restauration et la réponse à incident sont décrits
dans [la procédure d'exploitation](../docs/reference-registry.md).

Le test d'interopérabilité peut être exécuté depuis la racine du dépôt :

```sh
ctest --test-dir build --output-on-failure -R registry.reference
```
