# Benchmark des services de la stdlib 0.7.4

Ce relevé compare le commit de base `6cb6fb6` et la refonte R074-4 avec le
même programme
[`benchmarks/stdlib_services.janus`](../../benchmarks/stdlib_services.janus).
Le corpus exécute 20 000 conversions de texte, calculs mathématiques et tirages
pseudo-aléatoires déterministes, puis 1 000 cycles
ouverture/écriture/fermeture/suppression de fichier.

## Environnement

- Linux 6.18 sous WSL2, x86_64 ;
- Intel Core i5-10300H, 8 processeurs logiques ;
- Clang 21.1.8 avec optimisation `-O2` ;
- exécutables instrumentés avec AddressSanitizer ;
- 41 exécutions entrelacées par variante, médiane du temps mural monotone.

## Résultats

| Variante | Médiane | Moyenne | Minimum | Maximum |
| --- | ---: | ---: | ---: | ---: |
| base `6cb6fb6` | 27,062 ms | 27,368 ms | 25,116 ms | 33,027 ms |
| refonte R074-4 | 28,310 ms | 29,631 ms | 26,561 ms | 47,594 ms |

La médiane augmente de **4,61 %**, sous le budget maximal de 5 %. Le maximum de
la refonte contient un écart ponctuel ; l'alternance de l'ordre et la médiane
sur 41 mesures réduisent son influence.

Les dix modules du lot passent de 2 224 à 2 211 lignes hors documentation et
fixtures, soit **-0,58 %**. Les modules déjà concis (`std.path`, `std.text`,
`std.wall_time`, `std.random` et `std.math`) conservent leur implémentation et
leur surface après audit ; la refonte porte sur les états de ressources, les
erreurs répétées et les conversions de durée.

## Reproduction

Construire `janusc` et `janus_runtime`, émettre l'IR puis compiler le programme
avec les mêmes options :

```bash
build/janusc benchmarks/stdlib_services.janus > build/stdlib-services.ll
clang -O2 -fsanitize=address -fno-omit-frame-pointer \
  build/stdlib-services.ll build/libjanus_runtime.a \
  -o build/stdlib-services
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 build/stdlib-services
```

La sortie attendue est `248072075`. La CI exécute aussi
`benchmarks.stdlib_services_smoke` afin de verrouiller ce résultat, le
nettoyage du fichier temporaire et le passage sous ASan.
