# Liste persistante `PersistentList[T]`

## Décision

`std.persistent_list` introduit une liste immutable simplement chaînée. Chaque
valeur et chaque cellule sont placées derrière un `Shared`; une version ne
possède qu'un handle de racine et sa taille. Cette représentation accepte les
éléments propriétaires sans exiger `Copy` et détruit leur contenu une seule fois
après la disparition de toutes les versions et de tous les handles observants.

`prepend` conserve la liste source utilisable et retient sa racine en O(1).
`tail` retient directement la cellule suivante. `concat` reconstruit seulement
la partie gauche et partage intégralement la racine droite. `filter` partage les
allocations des valeurs retenues, même si leurs cellules doivent être recréées.

## Surface et propriété

- `persistentListEmpty[T]()` construit la version vide ;
- `prepend(value)` transfère `value` dans une nouvelle tête ;
- `head()` retourne un `Shared[T]` propriétaire, à détruire explicitement ;
- `tail()` et `clone()` retournent de nouveaux objets liste partageant leurs
  cellules ;
- `iterator()` produit des `Shared[T]` au fil de l'eau, sans tableau ni copie de
  `T` intermédiaire ;
- `persistentListFromArray` consomme son tableau, tandis que `toArray` est
  disponible seulement pour `T <: Copy` ;
- `equals` utilise l'égalité dérivée de `T`; `equalsBy` reçoit un comparateur
  pour les autres types.

Toutes les méthodes de transformation empruntent leur source. Les callbacks de
`map`, `filter`, `fold` et `equalsBy` n'observent que des emprunts immutables.
Une panique détruit le préfixe en construction, les handles d'itération et les
callbacks déjà transférées pendant le dépilage.

## Complexité

| Opération | Temps | Cellules nouvelles | Partage |
| --- | ---: | ---: | --- |
| `empty`, `size`, `isEmpty` | O(1) | 0 | — |
| `clone`, `head`, `tail` | O(1) | 0 | racine, valeur ou queue |
| `prepend` | O(1) | 1 | queue complète |
| `iterator.next` | O(1) | 0 | handle de valeur et prochaine cellule |
| `reverse`, `map`, `filter`, `fold`, `equals*`, `toArray` | O(n) | selon l'opération | valeurs pour `reverse`/`filter` |
| `concat(left, right)` | O(taille gauche) | taille gauche | toute la racine droite et toutes les valeurs |
| `persistentListFromArray` | O(n) | n | transfert des valeurs |

Une insertion crée une allocation partagée de valeur, une cellule partagée et
une enveloppe de version. Ce coût constant est supérieur au stockage contigu
d'`Array`, en échange des anciennes versions conservées et des suffixes communs.
Le compteur de références est non atomique et suit le contrat de
[`Shared[T]`](shared-immutable.md).

## Mesures reproductibles

`benchmarks/persistent_list.janus` construit et parcourt 20 000 entiers dans un
`Array[int]`, puis dans un `PersistentList[int]`. Le checksum publié est
`199990000` pour chaque structure. Sur la machine de développement Linux x86-64
du 30 août 2026, compilée avec `clang -O3`, l'exécution combinée a pris environ
0,01 s et un RSS maximal de 6 892 KiB. Ces valeurs sont informatives : le smoke
test ne verrouille que les checksums, et une comparaison locale se relance avec :

```bash
build/janusc benchmarks/persistent_list.janus > /tmp/persistent-list.ll
clang -O3 /tmp/persistent-list.ll build/libjanus_runtime.a \
  -o /tmp/persistent-list
/usr/bin/time -f 'elapsed=%e max_rss_kib=%M' /tmp/persistent-list
```

Le modèle d'allocation, plus stable que le temps mur, explique le compromis :
`Array` amortit ses insertions dans un tampon, alors que chaque tête persistante
alloue une valeur et une cellule partagées mais conserve toutes les versions.
Aucun vecteur, map ou set persistant n'est ajouté avant des mesures dédiées.

## Profondeur, diamants et cycles

La fixture `persistent_list_deep_destruction.janus` couvre 4 096 cellules. La
fixture principale conserve deux branches ayant la même queue, filtre une
branche, puis vérifie que les trois objets propriétaires sont détruits une fois.

Les cycles sont impossibles via l'API publique : `PersistentListNode` est privé,
ses champs sont immutables et aucun constructeur public n'accepte une cellule ou
un handle de racine. Un futur mécanisme de références faibles devrait précéder
toute API capable de relier des cellules existantes.
