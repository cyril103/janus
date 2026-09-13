# Vecteur persistant `PersistentVector[T]`

## Représentation expérimentale (#314)

`std.persistent_vector` utilise un arbre radix de facteur **B = 32**, sans queue
séparée. Une feuille contient de 0 à 32 handles `Shared[T]`; un nœud intérieur
contient de 1 à 32 handles `Shared[PersistentVectorNode[T]]`. Deux tableaux privés
séparent valeurs et enfants : un seul est non vide. Les tableaux deviennent
inaccessibles en mutation une fois leur nœud publié dans `Shared`.

Une version possède une racine partagée, une longueur et une capacité logique,
puissance de 32. Le vecteur vide possède une feuille vide de capacité 32.
Tous les sous-arbres sauf le dernier sont pleins, toutes les feuilles sont à la
même profondeur. Dans un nœud, `span` désigne la capacité d'un enfant (1 pour une
feuille); l'indice choisit l'enfant `index / span`, puis `index % span`.

`set` reconstruit sa feuille et ses ancêtres, en clonant les handles des frères.
`push` suit le bord droit; lorsque la racine est pleine, il crée une nouvelle
racine et un chemin vers une nouvelle feuille. `pop` reconstruit ce même bord,
supprime les enfants devenus vides et réduit la racine lorsqu'il reste un seul
enfant. Les frontières de profondeur sont 32, 1 024, 32 768, etc.

Ni les nœuds, ni leurs constructeurs, ni la racine ne sont publics. L'API ne peut
modifier un nœud publié ou former un cycle. La destruction récursive descend
seulement O(log_32 n) niveaux; elle n'a pas la profondeur linéaire d'une liste.
Le partage est non atomique, conformément au contrat de [Shared](shared-immutable.md).

## Surface et propriété

- `persistentVectorEmpty[T]()` crée le vide; `clone()` partage sa racine.
- `persistentVectorFromArray[T](source)` consomme le tableau et transfère chaque
  valeur, y compris les types propriétaires non `Copy`.
- `get(index)` retourne un nouveau `Shared[T]` à détruire. `set(index, value)`
  et `push(value)` transfèrent leur valeur dans une nouvelle version.
- `pop()` retourne une version raccourcie; pour conserver la dernière valeur,
  appeler `get(size() - 1)` avant. La source reste inchangée.
- `get` et `set` paniquent si `index >= size()`, comme `Array`; `pop` panique
  sur le vide. Une valeur transférée à un `set` invalide est détruite.
- `iterator()` produit des handles dans l'ordre des indices, avec une pile de
  nœuds en attente et un curseur dans la feuille. Aucun tableau de valeurs n'est
  matérialisé. Le vérificateur conserve l'emprunt lexical de la source;
  `intoIterator()` consomme l'enveloppe et retourne un parcours autonome.
- `IntoIterable[Shared[T]]`, les adaptateurs `Iterator` et `collectWith` vers les
  builders existants sont pris en charge. `Iterable[T]` exige `Copy` et ne décrit
  donc pas ce parcours de handles. Aucun builder transitoire public n'est ajouté.
- `map`, `filter` et `fold` observent `borrow T`. `filter` conserve l'identité des
  allocations retenues. `equalsBy` compare les éléments dans l'ordre, avec
  court-circuit; `equals` utilise l'égalité dérivée de `T`.
- `toArray()` exige `T <: Copy`, car une version ne peut céder ses éléments.

## Complexités et compromis

| Opération | Temps | Mémoire supplémentaire |
| --- | --- | --- |
| `empty`, `size`, `isEmpty`, `clone` | O(1) | O(1) |
| `get` | O(log_32 n) | un handle de valeur |
| `set`, `push`, `pop` | O(32 log_32 n) = O(log_32 n), B fixé | O(log_32 n) nœuds, chacun de largeur bornée |
| parcours complet, `fold`, `equals*` | O(n) | pile O(32 log_32 n) |
| `map`, `filter`, `fromArray` | O(n) | O(n) résultat et handles intermédiaires |
| `toArray` | O(n) | tableau de sortie O(n) et pile |

La construction complète calcule la profondeur, consomme les handles dans
l'ordre et assemble chaque nœud une seule fois. Elle n'utilise pas une succession
de `push` persistants. Les `ArrayBuilder` internes permettent de protéger chaque
préfixe avec `defer`, puis de transférer les tableaux finalisés.

Il n'y a pas d'optimisation de queue : même une insertion dans une feuille non
pleine recopie un chemin. Chaque `Shared` possède son allocation de valeur, son
compteur et son enveloppe; le stockage coûte donc davantage qu'un tableau
contigu pour une version unique. La capacité maximale est la plus grande
puissance de 32 représentable dans `usize` (2^60 sur 64 bits); la croissance
suivante déclenche une panique avant multiplication.

## Paniques et preuves de destruction

Les builders et handles locaux exposés à une panique sont protégés par `defer`.
Les racines sont construites avant l'allocation de l'enveloppe finale, afin
qu'une panique de construction ne laisse pas cette enveloppe partielle allouée.
Les callbacks doivent respecter le contrat Janus de leurs propres valeurs
possédées; les paramètres `borrow T` ne leur transfèrent jamais les éléments.
Comme pour les autres collections, le consommateur emploie `defer delete` sur
les versions qu'il souhaite nettoyer en cas de panique.

Les tests vérifient les frontières jusqu'à 32 769 éléments, 1 100 versions
simultanées et leur suppression, les branches et diamants, l'identité partagée
hors du chemin modifié, les ressources non `Copy`, les parcours interrompus,
les comparaisons court-circuitées et la destruction de 100 000 ressources.
Les paniques de callbacks et de bornes contrôlent les compteurs de destruction.

Le test Linux d’injection des paniques du vecteur injecte un échec à
chacun des **445** points de stockage `Shared` ou de réallocation rencontrés
pendant conversion, remplacement, ajout (dont croissance de racine), retrait,
map, filter, itération et `toArray`. Il vérifie les sources pendant le dépilage,
les destructeurs et l'absence de blocs alloués restants, sous ASan/UBSan.
Le comptage des nœuds vérifie aussi que set/push/pop recréent deux nœuds pour
un arbre de deux niveaux, et une croissance de racine trois nœuds. Le shim
est uniquement lié aux tests : aucun point d'injection n'est ajouté au runtime.
Les allocations de classes/closures conservent le contrat fatal du runtime
existant; elles ne déclenchent actuellement pas de panique Janus récupérable et
ne sont pas incluses dans cette injection. Cette limite est aussi celle de
`Shared`; elle n'est pas présentée comme une garantie OOM générale.

```bash
cmake -S . -B build
ctest --test-dir build -R 'persistent_vector|persistent_editor' --output-on-failure
```

Le projet consommateur [persistent-editor](../../examples/persistent-editor/README.md)
valide un document de 1 050 lignes, une révision sauvegardée, undo/redo et la
création d'une branche invalidant redo. Il utilise seulement l'API publique.

## Mesures reproductibles

```bash
python3 benchmarks/run_persistent_vector.py --build-dir build --repeats 3
```

Charge : construction de 4 096 entiers, 32 versions conservées modifiant un indice
réparti dans le document, lecture du checksum source, destruction de toutes les
versions. `Array` copie avec `map`; `PersistentList` utilise son `map` actuel
(il n'a pas de `set`); `PersistentVector` appelle `set`. Il s'agit d'une
comparaison de ces API disponibles pour des snapshots indexés, pas d'une mesure
d'insertion en tête. Chaque exécution est un processus distinct.

Mesures Linux x86-64 du 13 septembre 2026, `clang -O3`, minimum de trois passages :

| Collection | Temps (ms, lancement inclus) | Allocations/réallocations | Octets demandés cumulés | Pic d'octets vivants |
| --- | ---: | ---: | ---: | ---: |
| Array | 3,29 | 87 | 558 372 | 541 744 |
| PersistentList | 173,68 | 2 535 772 | 37 905 060 | 11 650 288 |
| PersistentVector | 2,67 | 27 461 | 523 732 | 226 200 |

Les octets sont ceux demandés au runtime, hors métadonnées de l'allocateur, du
shim et de pile; ce n'est pas le RSS du processus. Le comptage inclut les handles,
les builders et les itérateurs. Le shim ajoute une surcharge commune; les temps
courts sont bruités et ne constituent pas un seuil de performance CI.
Le JSON brut est écrit dans `build/persistent_vector_benchmark/measurements.json`.
Le smoke test vérifie checksum `8386560`, 32 versions et zéro octet vivant final.
