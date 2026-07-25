# Suite de compatibilité

Ces fixtures représentent un échantillon minimal du contrat public décrit dans
[`docs/stability-contract.md`](../../docs/stability-contract.md) :

- `language` couvre la syntaxe et les résultats observables ;
- `ownership` couvre `defer` et la destruction récursive ;
- `stdlib` couvre des API publiques de collections, texte et mathématiques.

CTest lance la suite avec le compilateur courant en positions N et N+1. Pour
valider un candidat avec la dernière version publiée :

```bash
cmake \
  -DPREVIOUS_JANUS=/opt/janus-N/bin/janus \
  -DCURRENT_JANUS=build-release/janus \
  -DFIXTURE_DIR="$PWD/tests/compatibility" \
  -DOUTPUT_DIR="$PWD/build-release/compatibility-N-N+1" \
  -DEXECUTABLE_SUFFIX= \
  -P tests/compatibility/run_compatibility.cmake
```

Sous Windows, utilisez les chemins appropriés et
`-DEXECUTABLE_SUFFIX=.exe`.

Une fixture ne doit utiliser qu'une surface déclarée stable. Toute modification
d'une sortie attendue doit être justifiée selon la procédure de migration du
contrat.

