# Historique de document persistant

Ce projet consommateur utilise `std.persistent_vector` pour conserver des
révisions de lignes propriétaires. Une édition remplace une ligne, undo/redo
échangent les versions, et une édition après undo abandonne la branche redo.
Les snapshots déjà sauvegardés restent lisibles.

Depuis ce répertoire :

```bash
../../build/janus check --all --deny-warnings
../../build/janus test --fail-if-empty
../../build/janus run
```

Le programme affiche `world` puis `Janus`. Le test couvre 1 050 lignes, donc
plusieurs niveaux de l'arbre, et vérifie l'identité d'une ligne non modifiée.
Il est enregistré dans CTest sous `downstream.persistent_editor`.
