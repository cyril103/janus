# Contribuer à Janus

Merci de contribuer. Les changements de langage, de surface publique ou de
format persistant commencent par une issue qui décrit le cas d'usage, la
migration et la gate 1.0 concernée. Une correction locale peut directement
ouvrir une pull request.

## Préparer le dépôt

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Avant une pull request, exécutez au minimum les tests du composant modifié,
`git diff --check` et les vérifications documentaires décrites dans
[`docs/development.md`](docs/development.md). Ajoutez un test de régression et
une entrée au changelog pour tout comportement utilisateur observable.

Les commits suivent de préférence `type(scope): résumé`, par exemple
`fix(lsp): discard diagnostics for stale documents`. Gardez chaque commit
compilable et limité à une intention.

## Compatibilité

Ne promouvez pas une surface expérimentale sans mettre à jour l'inventaire
courant, les fixtures N/N+1 et les preuves de la gate associée. Une suppression
publique doit passer par `@deprecated use [[remplacement]]` et une migration
documentée.
