# Audit de l’inférence des constructeurs génériques

Rapport généré. Reproduction :

```bash
python3 scripts/audit_constructor_inference.py --check
```

L’inventaire couvre les constructeurs génériques explicites, y compris les
formes multilignes et imbriquées, ainsi que tous les casts `usize(<littéral>)`
des surfaces publiques. Les occurrences simplifiables ont été migrées ; les
formes restantes sont pédagogiques ou constituent une couverture explicite.
Les miroirs générés ne sont pas dupliqués dans la table : leur identité avec
les sources canoniques est vérifiée séparément par les tests du site et de l’API.

| Fichier | Ligne:colonne | Nature | Occurrence | Classification | Justification |
|---|---:|---|---|---|---|
| `README.md` | 112:2 | constructeur | `new Factory[int](` | pédagogique explicite | pédagogique explicite |
| `docs/archive/migration-0.5-to-0.6.md` | 31:35 | constructeur | `new Array[Resource](` | pédagogique explicite | historique |
| `docs/archive/migration-0.5-to-0.6.md` | 31:55 | cast littéral | `usize(2)` | pédagogique explicite | historique |
| `docs/archive/migration-0.5-to-0.6.md` | 39:23 | cast littéral | `usize(0)` | pédagogique explicite | historique |
| `docs/audits/stdlib-0.7.4.md` | 156:20 | cast littéral | `usize(1)` | test de couverture | donnée d’audit historique |
| `docs/audits/stdlib-0.7.4.md` | 158:24 | cast littéral | `usize(0)` | test de couverture | donnée d’audit historique |
| `docs/design/container-ownership.md` | 90:21 | cast littéral | `usize(0)` | pédagogique explicite | pédagogique (index usize) |
| `docs/design/container-ownership.md` | 175:21 | cast littéral | `usize(0)` | pédagogique explicite | pédagogique (index usize) |
| `docs/design/container-ownership.md` | 176:45 | cast littéral | `usize(0)` | pédagogique explicite | pédagogique (index usize) |
| `docs/design/container-ownership.md` | 189:19 | cast littéral | `usize(0)` | pédagogique explicite | pédagogique (index usize) |
| `docs/design/lexical-borrowing.md` | 491:41 | cast littéral | `usize(0)` | pédagogique explicite | pédagogique (index usize) |
| `docs/language-guide.md` | 1290:16 | constructeur | `new Factory[int](` | pédagogique explicite | pédagogique explicite |
| `docs/language-guide.md` | 1649:43 | cast littéral | `usize(1)` | test de couverture | nécessaire (opérande binaire usize) |
| `docs/stdlib-reference.md` | 37:41 | cast littéral | `usize(2)` | test de couverture | nécessaire (opérande binaire usize) |
| `docs/stdlib-reference.md` | 355:44 | cast littéral | `usize(8)` | test de couverture | nécessaire (opérande binaire usize) |
| `docs/stdlib-reference.md` | 500:55 | cast littéral | `usize(5)` | test de couverture | nécessaire (opérande binaire usize) |
| `docs/stdlib-reference.md` | 527:49 | cast littéral | `usize(2)` | test de couverture | nécessaire (opérande binaire usize) |
| `docs/stdlib-reference.md` | 655:45 | cast littéral | `usize(6)` | test de couverture | nécessaire (opérande binaire usize) |
| `docs/stdlib-reference.md` | 667:32 | cast littéral | `usize(6)` | test de couverture | nécessaire (opérande binaire usize) |
| `examples/array.janus` | 29:41 | cast littéral | `usize(10)` | test de couverture | nécessaire (opérande binaire usize) |
| `examples/casts.janus` | 11:38 | cast littéral | `usize(1)` | pédagogique explicite | pédagogique (casts) |
| `examples/casts.janus` | 12:16 | cast littéral | `usize(0)` | pédagogique explicite | pédagogique (casts) |
| `examples/casts.janus` | 13:34 | cast littéral | `usize(0)` | pédagogique explicite | pédagogique (casts) |
| `examples/generic_classes.janus` | 17:31 | constructeur | `new Box[int](` | pédagogique explicite | pédagogique explicite |
| `examples/generic_classes.janus` | 22:30 | constructeur | `new Box[string](` | pédagogique explicite | pédagogique explicite |
| `examples/pointers.janus` | 2:38 | cast littéral | `usize(2)` | pédagogique explicite | pédagogique (API pointeur) |
| `examples/pointers.janus` | 3:16 | cast littéral | `usize(0)` | pédagogique explicite | pédagogique (API pointeur) |
| `examples/pointers.janus` | 4:16 | cast littéral | `usize(1)` | pédagogique explicite | pédagogique (API pointeur) |
| `examples/pointers.janus` | 6:33 | cast littéral | `usize(0)` | pédagogique explicite | pédagogique (API pointeur) |
| `examples/pointers.janus` | 7:34 | cast littéral | `usize(1)` | pédagogique explicite | pédagogique (API pointeur) |
| `examples/pointers.janus` | 10:34 | cast littéral | `usize(4)` | pédagogique explicite | pédagogique (API pointeur) |
| `examples/pointers.janus` | 17:16 | cast littéral | `usize(2)` | pédagogique explicite | pédagogique (API pointeur) |
| `examples/pointers.janus` | 24:34 | cast littéral | `usize(2)` | pédagogique explicite | pédagogique (API pointeur) |
| `examples/snake/main.janus` | 33:25 | cast littéral | `usize(1)` | test de couverture | nécessaire (opérande binaire usize) |
| `examples/snake/main.janus` | 107:19 | cast littéral | `usize(0)` | test de couverture | nécessaire (opérande binaire usize) |
| `examples/snake/main.janus` | 108:25 | cast littéral | `usize(1)` | test de couverture | nécessaire (opérande binaire usize) |
| `examples/snake/main.janus` | 110:39 | cast littéral | `usize(0)` | test de couverture | nécessaire (opérande binaire usize) |
| `examples/snake/main.janus` | 111:41 | cast littéral | `usize(0)` | test de couverture | nécessaire (opérande binaire usize) |
| `examples/snake/main.janus` | 308:36 | cast littéral | `usize(1)` | test de couverture | nécessaire (opérande binaire usize) |
| `examples/snake/main.janus` | 315:45 | cast littéral | `usize(1)` | test de couverture | nécessaire (opérande binaire usize) |
| `examples/snake/main.janus` | 323:68 | cast littéral | `usize(1)` | test de couverture | nécessaire (opérande binaire usize) |
| `examples/snake/main.janus` | 327:56 | cast littéral | `usize(1)` | test de couverture | nécessaire (opérande binaire usize) |
| `examples/snake/main.janus` | 328:35 | cast littéral | `usize(0)` | test de couverture | nécessaire (opérande binaire usize) |
| `examples/snake/main.janus` | 329:60 | cast littéral | `usize(1)` | test de couverture | nécessaire (opérande binaire usize) |
| `examples/snake/main.janus` | 330:41 | cast littéral | `usize(1)` | test de couverture | nécessaire (opérande binaire usize) |
| `examples/usize.janus` | 4:25 | cast littéral | `usize(0)` | pédagogique explicite | pédagogique (usize) |
| `examples/usize.janus` | 5:25 | cast littéral | `usize(0)` | pédagogique explicite | pédagogique (usize) |
| `examples/usize.janus` | 9:25 | cast littéral | `usize(1)` | pédagogique explicite | pédagogique (usize) |
| `stdlib/std/process.janus` | 8:16 | constructeur | `new Array[string](` | pédagogique explicite | pédagogique (inférence impossible) |
| `stdlib/std/random.janus` | 8:44 | cast littéral | `usize(1)` | test de couverture | nécessaire (opérande binaire usize) |
| `website/docs/book/02-valeurs-types.md` | 122:33 | cast littéral | `usize(1)` | pédagogique explicite | pédagogique (arithmétique usize) |
| `website/docs/book/06-collections-iterateurs.md` | 127:28 | cast littéral | `usize(5)` | test de couverture | nécessaire (opérande binaire usize) |
| `website/docs/book/06-collections-iterateurs.md` | 134:32 | cast littéral | `usize(1)` | test de couverture | nécessaire (opérande binaire usize) |
| `website/docs/book/06-collections-iterateurs.md` | 134:60 | cast littéral | `usize(3)` | test de couverture | nécessaire (opérande binaire usize) |
| `website/docs/book/07-generiques-closures.md` | 37:5 | constructeur | `new Pair[string, int](` | pédagogique explicite | pédagogique explicite |
| `website/docs/book/09-propriete-avancee.md` | 54:39 | cast littéral | `usize(16)` | pédagogique explicite | pédagogique (API pointeur) |
| `website/docs/book/10-modules-visibilite-ffi.md` | 121:34 | cast littéral | `usize(4)` | pédagogique explicite | pédagogique (FFI) |
| `website/docs/book/10-modules-visibilite-ffi.md` | 123:12 | cast littéral | `usize(0)` | pédagogique explicite | pédagogique (FFI) |
| `website/docs/book/10-modules-visibilite-ffi.md` | 124:30 | cast littéral | `usize(0)` | pédagogique explicite | pédagogique (FFI) |
| `website/docs/tutorials/collections.md` | 80:18 | cast littéral | `usize(1)` | pédagogique explicite | pédagogique (signature take) |
| `website/docs/tutorials/propriete-move-consume.md` | 94:39 | cast littéral | `usize(16)` | pédagogique explicite | pédagogique (API pointeur) |

Total : **61** occurrences restantes, dont **0** simplifiable.
