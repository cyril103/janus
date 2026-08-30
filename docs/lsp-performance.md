# Performance et cohérence du LSP

Le serveur associe chaque publication de diagnostics à la version du document
analysé. Un client peut donc ignorer un résultat antérieur à son buffer
courant. Les requêtes annulées avec `$/cancelRequest` retournent l'erreur LSP
`-32800` et leur résultat calculé est supprimé, y compris lorsque l'annulation
arrive pendant le calcul.

Le binaire traite le protocole sur une file de travail dédiée. Le lecteur peut
ainsi enregistrer une annulation pendant qu'une requête coûteuse est en cours,
sans exécuter simultanément deux analyses sur l'état mutable du workspace.

## Baseline reproductible

Le benchmark mesure ensemble le démarrage à froid, l'indexation du workspace et
une complétion. Il produit les échantillons ainsi que p50 et p95 en JSON :

```bash
python3 scripts/benchmark_lsp.py \
  --janus-lsp build-release/janus-lsp \
  --workspace benchmarks/compilation/medium \
  --document benchmarks/compilation/medium/src/main.janus \
  --snapshot benchmarks/compilation/medium/src/main.janus \
  --line 0 --character 0 --samples 20 \
  --output build/lsp-performance.json
```

Les résultats ne deviennent bloquants qu'après quatre semaines sur un runner
identique. Les budgets p50/p95 seront alors fixés depuis la variance observée,
et non depuis une valeur arbitraire.
