# Benchmark des collections hachées 0.7.4

Ce relevé compare le commit de base `57b47a1` et la refonte R074-3 avec le
même programme
[`benchmarks/hash_collections.janus`](../../benchmarks/hash_collections.janus).
Chaque série construit 5 000 sets et 5 000 maps à leur seuil de charge, puis
exécute respectivement un doublon et un remplacement.

## Environnement

- Linux 6.18 sous WSL2, x86_64 ;
- Intel Core i5-10300H, 8 processeurs logiques ;
- Clang 21.1.8 avec optimisation `-O2` ;
- exécutable instrumenté avec AddressSanitizer ;
- 11 exécutions par variante, médiane du temps mural monotone en nanosecondes.

## Résultats

| Variante | Médiane | Minimum | Maximum |
| --- | ---: | ---: | ---: |
| base `57b47a1` | 29,779 ms | 27,997 ms | 36,569 ms |
| refonte R074-3 | 20,861 ms | 19,376 ms | 34,811 ms |

La médiane baisse de **29,95 %** (facteur **1,43**). Le sondage effectué avant
la décision de croissance supprime cinq rehashs inutiles par set et par map
dans ce corpus. Le budget de #110 autorise au plus 5 % de régression.

Les quatre modules du lot passent de 1 135 à 1 147 lignes hors documentation
et fixtures, soit **+1,06 %**, sous le budget de croissance de 5 %.

## Reproduction

Construire `janusc` et `janus_runtime`, émettre l'IR puis compiler le programme
avec les mêmes options :

```bash
build/janusc benchmarks/hash_collections.janus > build/hash-collections.ll
clang -O2 -fsanitize=address -fno-omit-frame-pointer \
  build/hash-collections.ll build/libjanus_runtime.a \
  -o build/hash-collections
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 build/hash-collections
```

Ces temps restent une mesure historique de 0.7.4. La CI exécute
`benchmarks.hash_collections_smoke` afin de verrouiller le résultat déterministe
et le passage sous ASan, sans réévaluer le budget de performance à chaque run.
