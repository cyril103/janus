# Audit de la bibliothèque standard — Janus 0.7.10

État mesuré au 28 juillet 2026. Ce fichier est généré de façon déterministe :

```bash
python3 scripts/audit_stdlib.py --write
python3 scripts/audit_stdlib.py --check
```

La source de vérité de la surface reste [`docs/public-surface-0.5.json`](../public-surface-0.5.json). Le générateur refuse une divergence entre cet inventaire et les sources.

## Résumé mesuré

- **28 modules**, **5661 lignes** et **637 symboles publics** inventoriés ;
- **0 blocs `///` publics pour 637 symboles** (le lot #115 porte l'objectif de couverture à 100 %) ;
- **123 sites d'allocation**, **189 marqueurs de nettoyage**, **177/14/35** occurrences `move`/`consume`/destructeur ;
- **21/28 modules** importés directement par au moins une fixture ou un test, soit **94 couples module-fichier de test** ;
- **12 motifs textuels intermodules** principaux consignés ci-dessous.

Ces métriques sont des indicateurs de risque et non des objectifs d'optimisation isolés. Un marqueur de nettoyage peut apparaître dans un nom d'API ; les tests sanitizers restent l'autorité sur les fuites et doubles destructions.

## Statuts et décisions

- `conservation` : surface publique conservée ; une refonte interne ne doit pas modifier sa signature ni sa sémantique observable.
- `refonte-interne` : détail d'implémentation réécrit sans devenir une API utilisateur.
- `dépréciation` : surface maintenue pendant une migration N/N+1 avant retrait.
- `remplacement-public` : nouvelle surface accompagnée d'une migration et d'une fixture N/N+1.

L'audit ne propose **aucune rupture publique** : tous les symboles `stable-proposed` et `experimental` sont classés `conservation`. Les symboles `internal-detail` de `std.hash_probe` sont classés `refonte-interne`. Toute dépréciation ou tout remplacement découvert pendant #111–#114 doit donc modifier ce rapport avant le code et ajouter : justification, entrée de migration, fixture N/N+1 et mention au changelog.

## Budgets obligatoires des lots #111 à #115

| Dimension | Budget | Contrôle de sortie |
| --- | --- | --- |
| Compatibilité | zéro suppression, renommage ou dérive de signature non inventoriée ; zéro changement observable des surfaces conservées | `scripts/check_public_surface.py` et fixtures N/N+1 |
| Propriété | zéro copie implicite, fuite ou double destruction ; tout transfert reste explicite | fixtures `Copy`/non-`Copy`, ASan et chemins succès/erreur/panique |
| Taille | croissance nette maximale de 5 % par lot, hors `///`, fixtures et code généré ; tout dépassement est justifié dans la PR | diff de lignes contre la base de ce rapport |
| Performance | régression médiane maximale de 5 % sur les pipelines et collections concernés ; aucune régression asymptotique | benchmarks versionnés avec environnement et variance |
| Documentation | 100 % des symboles non expérimentaux documentés ; au moins un doctest de succès par module et un `compile_fail` par famille d'erreur structurée | `janus doc`, `janus test --doc` et crawler de liens |

## Inventaire par module

Les colonnes « Propriété M/C/D » comptent `move`, méthodes `consume` et destructeurs. « Erreurs R/O/P » compte les mentions de `Result`, `Option` et les appels à `panic`. « Nettoyages » compte `delete`, `defer`, destructeurs, fermetures et libérations natives.

| Module | Surface | Décision | Propriétaire | Symboles | Lignes | Blocs `///` | Propriété M/C/D | Erreurs R/O/P | Alloc. | Nettoyages | Imports | Fixtures | Documentation |
| --- | --- | --- | --- | ---: | ---: | ---: | --- | --- | ---: | ---: | --- | ---: | --- |
| `std.array` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) | 25 | 331 | 0 | 13/1/2 | 0/6/9 | 8 | 13 | `std.iterator`, `std.option` | 21 | `docs/language-guide.md` |
| `std.array_builder` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) | 7 | 46 | 0 | 2/0/1 | 0/0/0 | 3 | 4 | `std.array`, `std.builder`, `std.iterator` | 4 | `docs/language-guide.md` |
| `std.builder` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) | 3 | 6 | 0 | 0/0/0 | 0/0/0 | 0 | 0 | — | 0 | `docs/language-guide.md` |
| `std.c` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) | 5 | 11 | 0 | 0/0/0 | 0/0/0 | 0 | 0 | — | 2 | `docs/language-guide.md`, `docs/stability-contract.md` |
| `std.graphics` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) | 0 | 7 | 0 | 0/0/0 | 0/0/0 | 0 | 0 | `std.graphics.audio`, `std.graphics.drawing`, `std.graphics.input`, `std.graphics.resources`, `std.graphics.types` | 4 | `docs/graphics.md`, `docs/stability-contract.md` |
| `std.graphics.audio` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) | 22 | 135 | 0 | 0/0/2 | 0/0/0 | 2 | 2 | `std.c` | 0 | `docs/graphics.md` |
| `std.graphics.drawing` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) | 51 | 400 | 0 | 0/0/0 | 0/0/0 | 0 | 0 | `std.c`, `std.graphics.types`, `std.time` | 0 | `docs/graphics.md` |
| `std.graphics.input` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) | 23 | 141 | 0 | 0/0/0 | 0/0/0 | 0 | 0 | `std.graphics.types` | 0 | `docs/graphics.md` |
| `std.graphics.resources` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) | 45 | 446 | 0 | 0/0/4 | 0/0/0 | 6 | 4 | `std.c`, `std.graphics.types` | 0 | `docs/graphics.md` |
| `std.graphics.types` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) | 176 | 239 | 0 | 0/0/0 | 0/0/0 | 8 | 1 | — | 0 | `docs/graphics.md` |
| `std.fs` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) | 24 | 355 | 0 | 0/0/2 | 11/5/3 | 10 | 21 | `std.option`, `std.path`, `std.result`, `std.system` | 3 | `docs/language-guide.md`, `docs/design/path-filesystem.md`, `docs/stability-contract.md` |
| `std.hash_probe` | `internal-detail` | `refonte-interne` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) | 6 | 39 | 0 | 0/0/0 | 0/0/0 | 1 | 0 | — | 0 | `docs/public-surface-0.5.json` |
| `std.hashing` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) | 21 | 70 | 0 | 0/0/0 | 0/0/0 | 0 | 0 | — | 8 | `docs/language-guide.md`, `docs/stability-contract.md` |
| `std.hashmap` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) | 22 | 648 | 0 | 68/4/3 | 0/31/0 | 14 | 16 | `std.array`, `std.builder`, `std.hash_probe`, `std.hashing`, `std.iterator`, `std.option` | 6 | `docs/language-guide.md`, `docs/stability-contract.md` |
| `std.hashset` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) | 15 | 390 | 0 | 25/1/3 | 0/11/0 | 8 | 11 | `std.array`, `std.builder`, `std.hash_probe`, `std.hashing`, `std.iterator`, `std.option` | 5 | `docs/language-guide.md`, `docs/stability-contract.md` |
| `std.io` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) | 33 | 743 | 0 | 1/0/4 | 21/9/0 | 17 | 21 | `std.option`, `std.result`, `std.system` | 2 | `docs/language-guide.md`, `docs/design/io-streams.md`, `docs/stability-contract.md` |
| `std.iterator` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) | 18 | 295 | 0 | 29/8/7 | 0/19/0 | 14 | 29 | `std.builder`, `std.option` | 2 | `docs/language-guide.md`, `docs/stability-contract.md` |
| `std.math` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) | 4 | 79 | 0 | 0/0/0 | 0/0/1 | 1 | 0 | `std.array` | 5 | `docs/language-guide.md`, `docs/stability-contract.md` |
| `std.option` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) | 9 | 89 | 0 | 13/0/1 | 0/15/0 | 2 | 11 | — | 8 | `docs/language-guide.md`, `docs/stability-contract.md` |
| `std.path` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) | 8 | 181 | 0 | 0/0/1 | 3/0/2 | 6 | 13 | `std.result`, `std.system` | 2 | `docs/language-guide.md`, `docs/design/path-filesystem.md`, `docs/stability-contract.md` |
| `std.random` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) | 6 | 51 | 0 | 0/0/0 | 0/0/2 | 1 | 0 | — | 2 | `docs/language-guide.md`, `docs/stability-contract.md` |
| `std.process` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) | 13 | 199 | 0 | 0/0/2 | 2/5/0 | 6 | 6 | `std.array`, `std.option`, `std.result`, `std.system` | 1 | `docs/language-guide.md`, `docs/design/process-runtime.md`, `docs/stability-contract.md` |
| `std.range` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) | 1 | 29 | 0 | 0/0/0 | 0/1/0 | 2 | 1 | `std.iterator`, `std.option` | 2 | `docs/language-guide.md` |
| `std.result` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) | 12 | 128 | 0 | 26/0/1 | 17/3/0 | 2 | 17 | `std.option` | 10 | `docs/language-guide.md`, `docs/stability-contract.md` |
| `std.system` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) | 28 | 182 | 0 | 0/0/1 | 5/0/0 | 3 | 4 | `std.result` | 2 | `docs/language-guide.md`, `docs/design/system-runtime.md`, `docs/stability-contract.md` |
| `std.text` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) | 42 | 334 | 0 | 0/0/1 | 14/0/5 | 3 | 15 | `std.result` | 2 | `docs/text.md`, `docs/stability-contract.md` |
| `std.time` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) | 13 | 66 | 0 | 0/0/0 | 0/0/2 | 5 | 0 | — | 2 | `docs/language-guide.md`, `docs/stability-contract.md` |
| `std.wall_time` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) | 5 | 21 | 0 | 0/0/0 | 0/0/0 | 1 | 0 | — | 1 | `docs/language-guide.md`, `docs/stability-contract.md` |

## Invariants de propriété recensés

1. Une valeur non-`Copy` est déplacée explicitement à l'entrée et à la sortie d'un conteneur ; aucun emplacement ne possède deux fois la même valeur.
2. `Array`, `HashMap`, `HashSet`, builders et itérateurs détruisent exactement une fois les éléments encore possédés lors de `clear`, d'une sortie anticipée, d'une panique ou de leur destructeur.
3. Les opérations d'observation ne transfèrent pas la propriété ; les opérations `into*`, `remove`, `replace`, `pop` et les méthodes `consume` la transfèrent à l'appelant.
4. Les branches `Option.None` et `Result.Error` détruisent les fallbacks, closures et payloads non retournés, y compris avec `?`.
5. Un handle de fichier, processus, répertoire, texture, fonte, shader, cible de rendu, son ou musique est soit invalide, soit détenu par une unique valeur qui le ferme ou le décharge exactement une fois.
6. Une réallocation transfère les éléments initialisés avant de libérer le stockage brut ; un échec conserve l'ancien état et nettoie les arguments déjà consommés.
7. Les buffers et chaînes issus d'une frontière native conservent leur longueur, leur encodage et leur propriétaire jusqu'à la conversion ou libération explicite.

Les contrats détaillés existants restent normatifs : [conteneurs](../design/container-ownership.md), [flux](../design/io-streams.md), [chemins et fichiers](../design/path-filesystem.md) et [processus](../design/process-runtime.md).

## Imports de fixtures et couverture

Un module sans import direct n'est pas nécessairement non testé (il peut être atteint par réexport ou par un test C++/runtime), mais il doit recevoir une fixture explicite dans son lot propriétaire.

| Module | Fixtures ou tests qui importent directement le module |
| --- | --- |
| `std.array` | `tests/compatibility/stdlib.janus`, `tests/fixtures/project-euler/production/problem11.janus`, `tests/fixtures/project-euler/production/problem13.janus`, `tests/fixtures/project-euler/production/problem16.janus`, `tests/fixtures/project-euler/production/problem18.janus`, `tests/fixtures/project-euler/production/problem20.janus`, `tests/fixtures/project-euler/production/problem8.janus`, `tests/fixtures/runtime/array_out_of_bounds.janus`, `tests/fixtures/runtime/functional_sequence_cleanup.janus`, `tests/fixtures/runtime/owned_array.janus`, `tests/fixtures/runtime/owned_array_copy_error.janus`, `tests/fixtures/runtime/owned_array_move_error.janus`, `tests/fixtures/runtime/owned_array_panic.janus`, `tests/fixtures/runtime/owned_iterator_borrow_error.janus`, `tests/fixtures/runtime/owned_iterator_for_copy_error.janus`, `tests/fixtures/runtime/owned_iterator_panic.janus`, `tests/fixtures/runtime/owned_iterators.janus`, `tests/fixtures/runtime/prime_factors.janus`, `tests/fixtures/runtime/processes.janus`, `tests/fixtures/runtime/sequence_pipeline_stress.janus`, `tests/frontend/parser_top_level_declaration_test.cpp` |
| `std.array_builder` | `tests/compatibility/stdlib.janus`, `tests/fixtures/runtime/functional_sequence_cleanup.janus`, `tests/fixtures/runtime/owned_hash_collections.janus`, `tests/fixtures/runtime/owned_iterators.janus` |
| `std.builder` | aucune |
| `std.c` | `tests/fixtures/runtime/text_api.janus`, `tests/interop/c_abi.janus` |
| `std.graphics` | `tests/fixtures/runtime/graphics_resource_move_error.janus`, `tests/fixtures/runtime/graphics_resource_ownership.janus`, `tests/fixtures/runtime/time_random.janus`, `tests/language/graphics_module_test.cpp` |
| `std.graphics.audio` | aucune |
| `std.graphics.drawing` | aucune |
| `std.graphics.input` | aucune |
| `std.graphics.resources` | aucune |
| `std.graphics.types` | aucune |
| `std.fs` | `tests/fixtures/runtime/io_streams.janus`, `tests/fixtures/runtime/path_fs_result.janus`, `tests/fixtures/runtime/processes.janus` |
| `std.hash_probe` | aucune |
| `std.hashing` | `tests/fixtures/runtime/derived_capabilities.janus`, `tests/fixtures/runtime/hash_table_invariants.janus`, `tests/fixtures/runtime/hashing_borrow_error.janus`, `tests/fixtures/runtime/owned_hash_collections.janus`, `tests/fixtures/runtime/owned_hash_copy_error.janus`, `tests/fixtures/runtime/owned_hash_move_error.janus`, `tests/fixtures/runtime/owned_hash_panic.janus`, `tests/fixtures/runtime/owned_iterators.janus` |
| `std.hashmap` | `tests/fixtures/runtime/derived_capabilities.janus`, `tests/fixtures/runtime/hash_table_invariants.janus`, `tests/fixtures/runtime/owned_hash_collections.janus`, `tests/fixtures/runtime/owned_hash_copy_error.janus`, `tests/fixtures/runtime/owned_hash_panic.janus`, `tests/fixtures/runtime/owned_iterators.janus` |
| `std.hashset` | `tests/fixtures/runtime/derived_capabilities.janus`, `tests/fixtures/runtime/hash_table_invariants.janus`, `tests/fixtures/runtime/owned_hash_collections.janus`, `tests/fixtures/runtime/owned_hash_move_error.janus`, `tests/fixtures/runtime/owned_iterators.janus` |
| `std.io` | `tests/fixtures/runtime/io_streams.janus`, `tests/fixtures/runtime/processes.janus` |
| `std.iterator` | `tests/fixtures/runtime/owned_iterator_borrow_error.janus`, `tests/fixtures/runtime/sequence_pipeline_stress.janus` |
| `std.math` | `tests/compatibility/stdlib.janus`, `tests/fixtures/runtime/lcm_overflow.janus`, `tests/fixtures/runtime/lcm_overflow_reversed.janus`, `tests/fixtures/runtime/math_helpers.janus`, `tests/fixtures/runtime/prime_factors.janus` |
| `std.option` | `tests/compatibility/stdlib.janus`, `tests/fixtures/runtime/functional_sequence_cleanup.janus`, `tests/fixtures/runtime/option_combinators.janus`, `tests/fixtures/runtime/owned_iterators.janus`, `tests/fixtures/runtime/owned_option_combinators.janus`, `tests/fixtures/runtime/owned_option_move_error.janus`, `tests/fixtures/runtime/owned_option_observe_temporary_error.janus`, `tests/fixtures/runtime/processes.janus` |
| `std.path` | `tests/fixtures/runtime/io_streams.janus`, `tests/fixtures/runtime/path_fs_result.janus` |
| `std.random` | `tests/fixtures/runtime/random_zero_bound.janus`, `tests/fixtures/runtime/time_random.janus` |
| `std.process` | `tests/fixtures/runtime/processes.janus` |
| `std.range` | `tests/fixtures/project-euler/production/problem1.janus`, `tests/fixtures/project-euler/production/problem5.janus` |
| `std.result` | `tests/compatibility/stdlib.janus`, `tests/fixtures/runtime/functional_sequence_cleanup.janus`, `tests/fixtures/runtime/owned_result_combinators.janus`, `tests/fixtures/runtime/owned_result_move_error.janus`, `tests/fixtures/runtime/owned_result_observe_temporary_error.janus`, `tests/fixtures/runtime/owned_result_try.janus`, `tests/fixtures/runtime/owned_result_try_move_error.janus`, `tests/fixtures/runtime/owned_result_try_reuse_error.janus`, `tests/fixtures/runtime/result_combinators.janus`, `tests/fixtures/runtime/text_api.janus` |
| `std.system` | `tests/fixtures/runtime/processes.janus`, `tests/fixtures/runtime/system_result.janus` |
| `std.text` | `tests/compatibility/stdlib.janus`, `tests/fixtures/runtime/text_api.janus` |
| `std.time` | `tests/fixtures/runtime/duration_overflow.janus`, `tests/fixtures/runtime/time_random.janus` |
| `std.wall_time` | `tests/fixtures/runtime/time_random.janus` |

## Principaux motifs dupliqués

| Motif normalisé | Modules | Occurrences |
| --- | --- | ---: |
| `index = index + usize(1)` | `std.array`, `std.hashmap`, `std.hashset`, `std.io`, `std.iterator`, `std.process`, `std.text` | 24 |
| `var index : usize = usize(0)` | `std.array`, `std.hashmap`, `std.hashset`, `std.io`, `std.process`, `std.text` | 15 |
| `private var index : usize` | `std.array`, `std.hashmap`, `std.hashset`, `std.iterator` | 9 |
| `private var length : usize = usize(0)` | `std.array`, `std.hashmap`, `std.hashset`, `std.text` | 4 |
| `return match move value {` | `std.iterator`, `std.option`, `std.result` | 14 |
| `return Result.Error[bool, SystemError](` | `std.fs`, `std.io`, `std.system` | 13 |
| `if nativeHandle < isize(0) {` | `std.fs`, `std.io`, `std.system` | 11 |
| `return Result.Ok[bool, SystemError](true)` | `std.fs`, `std.io`, `std.system` | 9 |
| `length = length - usize(1)` | `std.array`, `std.hashmap`, `std.hashset` | 8 |
| `nativeHandle = isize(-1)` | `std.fs`, `std.io`, `std.system` | 8 |
| `val handle : isize = nativeHandle` | `std.fs`, `std.io`, `std.system` | 8 |
| `def next() : Option[T] {` | `std.array`, `std.hashset`, `std.iterator` | 7 |

Ces répétitions orientent les lots sans autoriser une abstraction aveugle : #111 mutualise parcours et fallbacks, #112 les sondes et croissances de tables, #113 les conversions d'erreurs/buffers/handles et #114 les wrappers de ressources et paires begin/end.

## Registre exhaustif des symboles

Chaque symbole hérite ici d'une décision explicite et d'un propriétaire de migration. Le registre contient exactement la surface extraite des sources ; le test `docs.stdlib_audit` détecte tout ajout, retrait ou changement non régénéré.

### `std.array`

| Symbole | Surface actuelle | Décision 0.7.10 | Propriétaire de migration |
| --- | --- | --- | --- |
| `Array` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Array.size` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Array.capacity` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Array.isEmpty` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Array.iterator` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Array.intoIterator` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Array.get` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Array.getOption` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Array.set` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Array.replace` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Array.push` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Array.pop` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Array.popOption` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Array.remove` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Array.reserve` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Array.clear` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Array.withValue` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Array.foreach` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Array.map` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Array.filter` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Array.find` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Array.fold` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Array.any` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Array.all` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Array.count` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |

### `std.array_builder`

| Symbole | Surface actuelle | Décision 0.7.10 | Propriétaire de migration |
| --- | --- | --- | --- |
| `ArrayBuilder` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `ArrayBuilder.add` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `ArrayBuilder.addAll` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `ArrayBuilder.size` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `ArrayBuilder.clear` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `ArrayBuilder.result` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `collectArray` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |

### `std.builder`

| Symbole | Surface actuelle | Décision 0.7.10 | Propriétaire de migration |
| --- | --- | --- | --- |
| `Builder` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Builder.add` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Builder.result` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |

### `std.c`

| Symbole | Surface actuelle | Décision 0.7.10 | Propriétaire de migration |
| --- | --- | --- | --- |
| `puts` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `strlen` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `memcmp` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `exit` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `printf` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |

### `std.graphics`

| Symbole | Surface actuelle | Décision 0.7.10 | Propriétaire de migration |
| --- | --- | --- | --- |
| *(aucun symbole propre ; réexports uniquement)* | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |

### `std.graphics.audio`

| Symbole | Surface actuelle | Décision 0.7.10 | Propriétaire de migration |
| --- | --- | --- | --- |
| `Sound` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Sound.isValid` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Sound.play` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Sound.stop` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Sound.isPlaying` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Sound.setVolume` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Sound.setPitch` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Sound.setPan` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Music` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Music.isValid` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Music.play` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Music.stop` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Music.update` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Music.isPlaying` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Music.setVolume` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Music.setPitch` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Music.setPan` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `initAudio` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `closeAudio` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `setMasterVolume` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `loadSound` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `loadMusic` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |

### `std.graphics.drawing`

| Symbole | Surface actuelle | Décision 0.7.10 | Propriétaire de migration |
| --- | --- | --- | --- |
| `Camera2D` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Camera2D.offsetX` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Camera2D.offsetY` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Camera2D.targetX` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Camera2D.targetY` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Camera2D.rotation` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Camera2D.zoom` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `graphicsAvailable` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `graphicsLastError` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `initWindow` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `windowShouldClose` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `closeWindow` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `isWindowFullscreen` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `isWindowHidden` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `isWindowMinimized` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `isWindowMaximized` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `isWindowFocused` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `isWindowResized` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `toggleFullscreen` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `maximizeWindow` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `minimizeWindow` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `restoreWindow` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `setWindowTitle` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `setWindowPosition` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `setWindowSize` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `setWindowOpacity` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `screenWidth` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `screenHeight` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `setTargetFps` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `beginBlend` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `endBlend` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `frameTime` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `elapsedTime` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `beginDrawing` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `endDrawing` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `beginCamera` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `endCamera` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `screenToWorld` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `worldToScreen` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `clearBackground` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `clearColor` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `drawPixel` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `drawPixelAt` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `drawLine` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `drawLineBetween` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `drawCircle` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `drawCircleAt` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `drawRectangle` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `drawRectangleArea` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `drawText` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `drawTextAt` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |

### `std.graphics.input`

| Symbole | Surface actuelle | Décision 0.7.10 | Propriétaire de migration |
| --- | --- | --- | --- |
| `isKeyDown` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `isKeyPressed` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `keyPressed` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `mouseX` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `mouseY` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `setMousePosition` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `mouseWheelMove` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `isMouseButtonDown` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `isMouseButtonPressed` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `showCursor` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `hideCursor` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `isCursorHidden` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `enableCursor` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `disableCursor` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `isGamepadAvailable` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `gamepadName` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `isGamepadButtonDown` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `isGamepadButtonPressed` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `isGamepadButtonReleased` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `gamepadButtonPressed` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `gamepadAxisCount` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `gamepadAxis` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `setGamepadVibration` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |

### `std.graphics.resources`

| Symbole | Surface actuelle | Décision 0.7.10 | Propriétaire de migration |
| --- | --- | --- | --- |
| `Texture` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Texture.isValid` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Texture.width` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Texture.height` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Texture.draw` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Texture.drawAt` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Texture.drawPro` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Texture.drawFrame` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Texture.setFilter` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Font` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Font.isValid` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Font.draw` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Font.measure` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `RenderTexture` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `RenderTexture.isValid` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `RenderTexture.width` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `RenderTexture.height` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `RenderTexture.begin` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `RenderTexture.drawPro` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Shader` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Shader.isValid` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Shader.begin` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Shader.location` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Shader.setFloat` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Shader.setVector2` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Shader.setColor` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Shader.setInt` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `SpriteAnimation` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `SpriteAnimation.texture` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `SpriteAnimation.frameWidth` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `SpriteAnimation.frameHeight` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `SpriteAnimation.columns` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `SpriteAnimation.frameCount` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `SpriteAnimation.currentFrame` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `SpriteAnimation.advance` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `SpriteAnimation.reset` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `SpriteAnimation.draw` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `loadTexture` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `loadFont` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `loadFontUtf8` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `loadRenderTexture` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `endRenderTexture` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `loadFragmentShader` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `loadShader` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `endShader` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |

### `std.graphics.types`

| Symbole | Surface actuelle | Décision 0.7.10 | Propriétaire de migration |
| --- | --- | --- | --- |
| `Key` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Space` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Apostrophe` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Comma` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Minus` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Period` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Slash` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Digit0` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Digit1` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Digit2` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Digit3` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Digit4` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Digit5` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Digit6` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Digit7` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Digit8` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Digit9` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Semicolon` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Equal` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.A` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.B` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.C` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.D` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.E` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.F` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.G` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.H` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.I` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.J` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.K` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.L` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.M` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.N` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.O` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.P` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Q` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.R` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.S` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.T` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.U` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.V` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.W` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.X` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Y` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Z` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.LeftBracket` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Backslash` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.RightBracket` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Grave` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Escape` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Enter` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Tab` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Backspace` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Insert` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Delete` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Right` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Left` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Down` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Up` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.PageUp` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.PageDown` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Home` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.End` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.CapsLock` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.ScrollLock` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.NumLock` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.PrintScreen` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Pause` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.F1` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.F2` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.F3` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.F4` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.F5` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.F6` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.F7` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.F8` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.F9` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.F10` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.F11` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.F12` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Keypad0` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Keypad1` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Keypad2` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Keypad3` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Keypad4` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Keypad5` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Keypad6` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Keypad7` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Keypad8` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Keypad9` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.KeypadDecimal` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.KeypadDivide` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.KeypadMultiply` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.KeypadSubtract` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.KeypadAdd` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.KeypadEnter` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.KeypadEqual` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.LeftShift` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.LeftControl` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.LeftAlt` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.LeftSuper` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.RightShift` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.RightControl` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.RightAlt` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.RightSuper` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Key.Menu` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `MouseButton` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `MouseButton.Left` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `MouseButton.Right` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `MouseButton.Middle` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `MouseButton.Side` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `MouseButton.Extra` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `MouseButton.Forward` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `MouseButton.Back` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `TextureFilter` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `TextureFilter.Point` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `TextureFilter.Bilinear` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `TextureFilter.Trilinear` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `TextureFilter.Anisotropic4x` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `TextureFilter.Anisotropic8x` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `TextureFilter.Anisotropic16x` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `BlendMode` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `BlendMode.Alpha` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `BlendMode.Additive` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `GamepadButton` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `GamepadButton.Unknown` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `GamepadButton.LeftFaceUp` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `GamepadButton.LeftFaceRight` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `GamepadButton.LeftFaceDown` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `GamepadButton.LeftFaceLeft` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `GamepadButton.RightFaceUp` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `GamepadButton.RightFaceRight` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `GamepadButton.RightFaceDown` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `GamepadButton.RightFaceLeft` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `GamepadButton.LeftTrigger1` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `GamepadButton.LeftTrigger2` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `GamepadButton.RightTrigger1` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `GamepadButton.RightTrigger2` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `GamepadButton.MiddleLeft` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `GamepadButton.Middle` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `GamepadButton.MiddleRight` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `GamepadButton.LeftThumb` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `GamepadButton.RightThumb` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `GamepadAxis` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `GamepadAxis.LeftX` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `GamepadAxis.LeftY` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `GamepadAxis.RightX` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `GamepadAxis.RightY` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `GamepadAxis.LeftTrigger` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `GamepadAxis.RightTrigger` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Vector2` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Vector2.x` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Vector2.y` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Rectangle` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Rectangle.x` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Rectangle.y` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Rectangle.width` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Rectangle.height` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Color` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Color.packed` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `rgba` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `vector2` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `rectangle` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `colorRgba` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `colorRgb` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `rgb` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Black` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `White` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Red` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Green` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `Blue` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `black` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `white` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `red` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `green` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |
| `blue` | `experimental` | `conservation` | [#114 / R0710-5](https://github.com/cyril103/janus/issues/114) |

### `std.fs`

| Symbole | Surface actuelle | Décision 0.7.10 | Propriétaire de migration |
| --- | --- | --- | --- |
| `FileType` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `FileType.File` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `FileType.Directory` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `FileType.SymbolicLink` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `FileType.Other` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `FileMetadata` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `FileMetadata.kind` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `FileMetadata.size` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `FileData` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `FileData.data` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `FileData.size` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `FileData.view` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `DirectoryIterator` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `DirectoryIterator.next` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `DirectoryIterator.close` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `readFile` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `writeFileAtomic` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `writeTextFileAtomic` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `createDirectory` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `createTemporaryDirectory` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `removeDirectory` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `removeFile` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `metadata` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `readDirectory` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |

### `std.hash_probe`

| Symbole | Surface actuelle | Décision 0.7.10 | Propriétaire de migration |
| --- | --- | --- | --- |
| `HashProbe` | `internal-detail` | `refonte-interne` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `HashProbe.hasNext` | `internal-detail` | `refonte-interne` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `HashProbe.next` | `internal-detail` | `refonte-interne` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `normalizedHashCapacity` | `internal-detail` | `refonte-interne` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `hashTableShouldGrow` | `internal-detail` | `refonte-interne` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `hashProbe` | `internal-detail` | `refonte-interne` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |

### `std.hashing`

| Symbole | Surface actuelle | Décision 0.7.10 | Propriétaire de migration |
| --- | --- | --- | --- |
| `Hashing` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `Hashing.hash` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `Hashing.equals` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `DerivedHashing` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `DerivedHashing.hash` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `DerivedHashing.equals` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `IntHashing` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `IntHashing.hash` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `IntHashing.equals` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `USizeHashing` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `USizeHashing.hash` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `USizeHashing.equals` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `ByteHashing` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `ByteHashing.hash` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `ByteHashing.equals` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `CharHashing` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `CharHashing.hash` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `CharHashing.equals` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `BoolHashing` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `BoolHashing.hash` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `BoolHashing.equals` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |

### `std.hashmap`

| Symbole | Surface actuelle | Décision 0.7.10 | Propriétaire de migration |
| --- | --- | --- | --- |
| `MapEntry` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `MapEntry.key` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `MapEntry.value` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `MapEntry.intoKey` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `MapEntry.intoValue` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `HashMap` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `HashMap.size` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `HashMap.isEmpty` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `HashMap.containsKey` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `HashMap.getOption` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `HashMap.put` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `HashMap.remove` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `HashMap.clear` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `HashMap.entries` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `HashMap.keys` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `HashMap.values` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `HashMap.intoEntries` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `MapBuilder` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `MapBuilder.add` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `MapBuilder.size` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `MapBuilder.clear` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `MapBuilder.result` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |

### `std.hashset`

| Symbole | Surface actuelle | Décision 0.7.10 | Propriétaire de migration |
| --- | --- | --- | --- |
| `HashSet` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `HashSet.size` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `HashSet.isEmpty` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `HashSet.contains` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `HashSet.add` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `HashSet.remove` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `HashSet.clear` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `HashSet.iterator` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `HashSet.intoIterator` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `SetBuilder` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `SetBuilder.add` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `SetBuilder.addAll` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `SetBuilder.size` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `SetBuilder.clear` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |
| `SetBuilder.result` | `stable-proposed` | `conservation` | [#112 / R0710-3](https://github.com/cyril103/janus/issues/112) |

### `std.io`

| Symbole | Surface actuelle | Décision 0.7.10 | Propriétaire de migration |
| --- | --- | --- | --- |
| `TextDecodeError` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `TextDecodeError.InvalidUtf8` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `ByteBuffer` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `ByteBuffer.data` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `ByteBuffer.size` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `ByteBuffer.capacity` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `ByteBuffer.isEmpty` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `ByteBuffer.clear` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `ByteBuffer.append` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `ByteBuffer.appendByte` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `ByteBuffer.appendText` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `ByteBuffer.isUtf8` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `ByteBuffer.asText` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `InputStream` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `InputStream.isOpen` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `InputStream.isEof` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `InputStream.read` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `InputStream.readLine` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `InputStream.close` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `OutputStream` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `OutputStream.isOpen` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `OutputStream.write` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `OutputStream.writeText` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `OutputStream.flush` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `OutputStream.close` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `openInputStreamBuffered` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `openInputStream` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `openOutputStreamBuffered` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `openOutputStream` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `standardInput` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `standardOutput` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `standardError` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `copyStream` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |

### `std.iterator`

| Symbole | Surface actuelle | Décision 0.7.10 | Propriétaire de migration |
| --- | --- | --- | --- |
| `Iterable` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Iterable.iterator` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `ZipItem` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `ZipItem.left` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `ZipItem.right` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Indexed` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Indexed.index` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Indexed.value` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Iterator` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Iterator.next` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Iterator.map` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Iterator.filter` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Iterator.take` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Iterator.zip` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Iterator.enumerate` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Iterator.flatMap` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Iterator.collectWith` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Iterator.fold` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |

### `std.math`

| Symbole | Surface actuelle | Décision 0.7.10 | Propriétaire de migration |
| --- | --- | --- | --- |
| `gcd` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `lcm` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `is_prime` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `prime_factors` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |

### `std.option`

| Symbole | Surface actuelle | Décision 0.7.10 | Propriétaire de migration |
| --- | --- | --- | --- |
| `Option` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Option.Some` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Option.None` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `isSome` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `isNone` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `map` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `andThen` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `orElse` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `unwrapOr` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |

### `std.path`

| Symbole | Surface actuelle | Décision 0.7.10 | Propriétaire de migration |
| --- | --- | --- | --- |
| `Path` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `Path.view` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `Path.size` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `Path.isAbsolute` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `Path.componentCount` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `Path.component` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `normalizePath` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `joinPath` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |

### `std.random`

| Symbole | Surface actuelle | Décision 0.7.10 | Propriétaire de migration |
| --- | --- | --- | --- |
| `Random` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `Random.nextUSize` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `Random.nextBounded` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `randomUSize` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `randomBounded` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `automaticRandom` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |

### `std.process`

| Symbole | Surface actuelle | Décision 0.7.10 | Propriétaire de migration |
| --- | --- | --- | --- |
| `programArgumentCount` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `programArgument` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `EnvironmentValue` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `EnvironmentValue.view` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `EnvironmentValue.size` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `environmentVariable` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `ProcessResult` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `ProcessResult.exitCode` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `ProcessResult.stdoutData` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `ProcessResult.stdoutSize` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `ProcessResult.stderrData` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `ProcessResult.stderrSize` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `runProcess` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |

### `std.range`

| Symbole | Surface actuelle | Décision 0.7.10 | Propriétaire de migration |
| --- | --- | --- | --- |
| `range` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |

### `std.result`

| Symbole | Surface actuelle | Décision 0.7.10 | Propriétaire de migration |
| --- | --- | --- | --- |
| `Result` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Result.Ok` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `Result.Error` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `isOk` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `isError` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `map` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `mapError` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `andThen` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `orElse` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `unwrapOr` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `toOption` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |
| `fromOption` | `stable-proposed` | `conservation` | [#111 / R0710-2](https://github.com/cyril103/janus/issues/111) |

### `std.system`

| Symbole | Surface actuelle | Décision 0.7.10 | Propriétaire de migration |
| --- | --- | --- | --- |
| `SystemErrorCategory` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `SystemErrorCategory.NotFound` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `SystemErrorCategory.PermissionDenied` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `SystemErrorCategory.AlreadyExists` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `SystemErrorCategory.InvalidInput` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `SystemErrorCategory.Interrupted` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `SystemErrorCategory.WouldBlock` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `SystemErrorCategory.OutOfSpace` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `SystemErrorCategory.TooLarge` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `SystemErrorCategory.Unsupported` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `SystemErrorCategory.ResourceExhausted` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `SystemErrorCategory.Other` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `SystemOpenMode` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `SystemOpenMode.Read` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `SystemOpenMode.Write` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `SystemOpenMode.Append` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `SystemError` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `SystemError.operation` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `SystemError.category` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `SystemError.nativeCode` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `SystemError.context` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `SystemFile` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `SystemFile.isOpen` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `SystemFile.read` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `SystemFile.write` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `SystemFile.close` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `openSystemFile` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `removeSystemFile` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |

### `std.text`

| Symbole | Surface actuelle | Décision 0.7.10 | Propriétaire de migration |
| --- | --- | --- | --- |
| `ParseError` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `ParseError.Empty` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `ParseError.Invalid` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `ParseError.Sign` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `ParseError.Overflow` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `ParseError.Underflow` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `ParseError.NonFinite` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `TextBuilder` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `TextBuilder.clear` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `TextBuilder.size` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `TextBuilder.view` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `TextBuilder.append` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `TextBuilder.appendInt` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `TextBuilder.appendUInt` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `TextBuilder.appendLong` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `TextBuilder.appendULong` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `TextBuilder.appendByte` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `TextBuilder.appendUByte` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `TextBuilder.appendShort` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `TextBuilder.appendUShort` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `TextBuilder.appendISize` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `TextBuilder.appendUSize` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `TextBuilder.appendFloat` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `TextBuilder.appendDouble` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `TextBuilder.appendBool` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `TextBuilder.appendChar` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `TextBuilder.appendHex` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `TextBuilder.appendFixed` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `parseInt` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `parseUInt` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `parseLong` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `parseULong` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `parseByte` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `parseUByte` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `parseShort` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `parseUShort` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `parseISize` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `parseUSize` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `parseFloat` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `parseDouble` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `parseBool` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `parseChar` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |

### `std.time`

| Symbole | Surface actuelle | Décision 0.7.10 | Propriétaire de migration |
| --- | --- | --- | --- |
| `Duration` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `Duration.nanoseconds` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `Duration.microseconds` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `Duration.milliseconds` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `Duration.seconds` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `Instant` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `Instant.durationSince` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `Instant.elapsed` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `nanoseconds` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `microseconds` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `milliseconds` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `seconds` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `monotonicNow` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |

### `std.wall_time`

| Symbole | Surface actuelle | Décision 0.7.10 | Propriétaire de migration |
| --- | --- | --- | --- |
| `WallTime` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `WallTime.unixNanoseconds` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `WallTime.unixMilliseconds` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `WallTime.unixSeconds` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
| `wallNow` | `stable-proposed` | `conservation` | [#113 / R0710-4](https://github.com/cyril103/janus/issues/113) |
