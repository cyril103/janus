# Benchmark du cœur stdlib 0.7.4

Ce relevé compare le commit de base `2fdb08f` et la refonte R074-2 avec le
même programme [`benchmarks/sequence_pipeline.janus`](../../benchmarks/sequence_pipeline.janus).
Le pipeline transfère 20 000 entiers depuis un `Array`, filtre un élément sur
trois, le transforme puis calcule la somme.

## Environnement

- Linux 6.18 sous WSL2, x86_64 ;
- Intel Core i5-10300H, 8 processeurs logiques ;
- Clang 21.1.8 ;
- exécutable instrumenté avec AddressSanitizer ;
- 11 exécutions par variante, médiane du temps mural mesurée avec
  `time.perf_counter()`.

## Résultats

| Variante | Médiane | Minimum | Maximum |
| --- | ---: | ---: | ---: |
| base `2fdb08f` | 439,321 ms | 431,905 ms | 447,718 ms |
| parcours linéaire R074-2 | 7,387 ms | 7,201 ms | 8,175 ms |

La médiane baisse de **98,32 %** (facteur 59,47). Le budget de #110 autorise
au plus 5 % de régression ; cette refonte le respecte largement. Le gain vient
du remplacement des suppressions répétées en tête de tableau, quadratiques,
par un curseur qui transfère chaque emplacement une seule fois.

Les six modules du lot passent de 869 à 895 lignes hors documentation et
fixtures, soit **+2,99 %**, sous le budget de croissance de 5 % fixé par #110.

## Reproduction

Construire `janusc` et `janus_runtime`, puis compiler le benchmark avec le
harnais sanitizer :

```bash
cmake \
  -DJANUSC="$PWD/build/janusc" \
  -DCLANG=/usr/bin/clang \
  -DSOURCE="$PWD/benchmarks/sequence_pipeline.janus" \
  -DRUNTIME="$PWD/build/libjanus_runtime.a" \
  -DOUTPUT_DIR="$PWD/build/sequence-pipeline-benchmark" \
  -DEXPECTED_OUTPUT="$PWD/benchmarks/sequence_pipeline.expected.txt" \
  -P tests/runtime/run_janus_example.cmake
```

Ces temps restent une mesure historique de 0.7.4. La CI exécute
`benchmarks.sequence_pipeline_smoke` afin de verrouiller le résultat et le
passage sous ASan, sans réévaluer le budget de performance à chaque run.
