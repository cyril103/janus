# Map et set persistants (#315)

## Décision : HAMT de facteur 32

Un arbre équilibré permettrait un parcours trié et des bornes O(log n) même en
cas de collisions de hash, mais demanderait un ordre total public sur les clés,
absent du contrat `Hashing`/`Equality`. Un HAMT exploite directement ces capacités,
avec une faible profondeur et sans imposer de comparaison d'ordre. **Le HAMT est
l'unique représentation retenue**, sans variante ordonnée ni builder transitoire.

L'implémentation expérimentale utilise des branches compactes : tableau de slots
radix triés (0 à 31) et tableau parallèle de handles enfants. Une recherche de slot
coûte au plus 32 comparaisons entières; c'est une alternative simple au bitmap et
popcount, avec un coût mémoire supérieur mais la même complexité pour B fixé.
Les feuilles contiennent des associations ayant le même hash complet, mémorisé.
Deux hash différents sont séparés par leurs chiffres radix, des bits faibles aux
bits forts. Sur 64 bits, la profondeur est au plus 13 branches; les collisions
complètes restent dans une feuille sans poursuivre la récursion.

Les tableaux privés sont immutables après publication dans `Shared`. Chaque
association possède deux handles, clé et valeur. Un remplacement construit une
nouvelle association; aucune ancienne version ne change. Les frères du chemin
modifié sont partagés. Les feuilles de collision copient leurs handles, jamais
les ressources sous-jacentes. Une suppression retire les branches vides et remonte
une feuille unique; une branche unique ne remonte pas, pour préserver sa profondeur
radix. Les destructeurs ont une profondeur bornée par la largeur du hash.

`PersistentSet[T]` est une enveloppe de `PersistentMap[T, Unit]`. Tous les types
et le moteur sont dans `std.persistent_map` pour respecter la visibilité `internal`
au module. `std.persistent_set` importe ce module comme point d'entrée pratique;
il ne contient aucun second moteur. Le partage reste non atomique comme `Shared`.

## API et propriété

- `persistentMapEmpty[K, V]()` et `persistentSetEmpty[T]()` utilisent exactement
  `__derivedHash` et `__derivedEquals`, comme `DerivedHashing`.
- `persistentMapWith[K, V](hash, equals)` et `persistentSetWith[T](hash, equals)`
  possèdent des fonctions `Fn`, partagées entre versions. Leur contrat logique
  est celui de `Hashing` : égalité équivalente, clés égales de même hash, fonctions
  stables pendant la vie des versions. Les méthodes mutables de `Hashing[T]` ne
  sont pas stockées dans `Shared`; ces fonctions permettent la personnalisation
  sans emprunt mutable caché dans une version. Aucun hash d'une clé résidente
  n'est recalculé pendant une restructuration.
- `clone`, `size`, `isEmpty` sont O(1). Chaque enveloppe retournée est à détruire.
- `get(borrow key)` retourne `Option[Shared[V]]`, à détruire, et n'exige pas Copy.
  `containsKey` et `contains` observent leurs arguments. L'API n'expose aucune
  mutation d'une clé ou valeur publiée. Le contrat ne permet pas de modifier une
  identité de clé par un état externe partagé caché.
- `insert(key, value)` consomme les arguments et retourne une nouvelle version.
  Lors d'un remplacement, la nouvelle clé fournie remplace également l'ancienne,
  qui reste détenue par les anciennes versions.
- `remove(borrow key)` retourne `Option[PersistentMap[K, V]]` (ou set) : `None`
  indique une absence; `Some` possède la nouvelle version sans la clé. L'appelant
  peut employer `clone()` s'il souhaite une enveloppe même en cas d'absence.
- `alter(key, scoped transform)` consomme la clé; le callback reçoit et possède
  `Option[Shared[V]]`, qu'il doit détruire ou transférer. Son `Option[V]` de sortie
  insère/remplace avec `Some`, supprime avec `None`. Il est appelé exactement une
  fois. Les callbacks de `mapValues`, `filter`, `fold`, `equalsBy` sont `scoped` et
  empruntent les éléments. `fold` transfère l'accumulateur à chaque appel.
- `iterator`, `keys`, `values` possèdent leurs handles de parcours. Les entrées
  sont `MapEntry[Shared[K], Shared[V]]`. Un parcours peut être arrêté à tout moment.
  `intoIterator` consomme l'enveloppe et libère l'emprunt lexical de la source.
- `equals` utilise l'égalité dérivée de V; `equalsBy` accepte un comparateur.
  L'égalité et les opérations d'ensembles exigent le même contrat logique de clés
  sur les deux sources (les objets de stratégie peuvent être distincts).

## Hash, égalité, ordre et coûts

`get`/`contains` appellent le hash une fois, puis l'égalité dans la feuille
uniquement si le hash complet correspond, jusqu'au premier succès. `insert` et
`remove` appellent également le hash une fois : une recherche détermine la taille,
puis une seconde passe reconstruit la feuille. L'égalité est appelée au plus 2c
fois dans une feuille de c collisions; la seconde passe visite toute la feuille.
Les clés résidentes sont le premier argument de l'égalité. Une absence lors de
`remove` ne reconstruit rien. Les collisions conservent leur ordre interne lors
d'un remplacement ou retrait; une nouvelle collision est ajoutée en fin.

L'ordre d'itération **n'est pas un contrat public**. Actuellement le parcours est
radix croissant puis ordre interne de collision; ni l'égalité structurelle ni les
lois d'ensembles ne dépendent de cet ordre. Les callbacks de parcours suivent cet
ordre, une fois par association visitée. `equalsBy` court-circuite aux tailles
inégales ou à la première association différente. `alter` effectue une recherche
puis une insertion/suppression, donc deux appels au hash. `mapValues` et `filter`
réinsèrent les associations retenues, avec un hash par association de sortie.

| Opération | Temps | Mémoire supplémentaire |
| --- | --- | --- |
| empty, clone, size, isEmpty | O(1) | O(1) |
| get, contains | O(log_32 n) attendu | O(1) handle |
| insert, remove | O(log_32 n) attendu | O(log_32 n) nœuds de largeur bornée |
| iterator, keys, values, fold | O(n) | pile O(32 log_32 n) |
| mapValues, filter, égalité | O(n log_32 n) attendu | résultat O(n), parcours pour égalité |
| union, intersection, difference | O((n+m) log_32(n+m)) attendu | résultat et parcours |

Avec c collisions complètes, recherche et mise à jour coûtent O(log_32 n + c),
et la copie de feuille ajoute O(c) handles. Une reconstruction par insertions
peut coûter O(n²) si tous les hash sont égaux. La profondeur a une borne matérielle
O(w/5), w étant le nombre de bits de `usize`; les bornes logarithmiques supposent
un hash bien réparti. La représentation favorise les versions conservées, pas le
stockage contigu d'une seule version. Pas de copie intégrale à chaque mise à jour.

## Paniques et validation

Les builders, handles et valeurs en attente sont protégés par `defer`. Les
opérations potentiellement faillibles sont évaluées avant d'allouer leur enveloppe
résultat. Les callbacks doivent respecter la propriété de leurs propres arguments.
Les versions publiées ne sont jamais modifiées, y compris pendant le dépilage.
Comme les autres collections, les consommateurs protègent leurs versions avec
`defer delete` s'ils veulent un nettoyage lors d'une panique.

Le test d’injection des paniques des collections persistantes utilise le shim d'allocation
existant du vecteur, sans modifier le runtime. Il valide **299 points d’échec** et injecte les échecs de stockage
Shared et de réallocation, vérifie l'absence de blocs restants et les destructeurs
exactement une fois, sous ASan/UBSan. Il couvre insertions, remplacements,
collisions, suppressions, mapValues, filter, alter, parcours partiels et opérations
d'ensembles. Les paniques de hash, égalité et callback sont aussi déclenchées;
une inspection différée vérifie la source pendant le dépilage. Les allocations
de classes/closures gardent le contrat fatal OOM du runtime; elles ne sont pas
récupérables comme paniques Janus et ne font pas partie de l'injection.

Les tests fonctionnels couvrent les frontières 32 et 1024, 1057 versions
simultanées, les diamants, les hash constants et préfixes jusqu'au dernier chiffre
radix sur 64 bits, les ressources propriétaires et l'identité des valeurs partagées.
Un corpus de 256 paires d’ensembles vérifie les lois de réunion, intersection et
différence avec des hash constants. Un test détruit 100 000 valeurs propriétaires.
Le projet [versioned-config](../../examples/versioned-config) valide des révisions
de configuration, un rollback et une branche indépendante.

Le benchmark `python3 benchmarks/run_persistent_map.py --build-dir build --repeats 3`
compare les copies d'Array de paires, HashMap et HashSet avec les deux collections
persistantes : 1024 associations et 32 versions de 1025 associations. Son JSON
publie temps, allocations, octets demandés, pic d'octets vivants et RSS (GNU time).
Le champ `rss_kib` est exprimé en Kio et vaut `null` lorsque le collecteur est
indisponible, avec `rss_status: "unavailable"` ; une mesure disponible porte le
statut `"available"`. Le [guide des benchmarks](../../benchmarks/README.md)
décrit la découverte du collecteur, sa configuration et le mode `--require-rss`
utilisé par la CI Linux.
Il vérifie checksum, nombre de versions et zéro octet vivant après destruction.
Les temps incluent construction initiale, conservation des versions et destruction;
il n'y a pas de seuil de performance imposé.

### Mesure locale de référence

Linux x86-64, 13 septembre 2026, Clang `-O3`, trois répétitions. Temps minimum
de processus (y compris GNU time), RSS de la dernière répétition; ces mesures
ne constituent pas un seuil portable.

| Collection | Temps (ms) | Allocations | Pic vivant Janus (octets) | RSS (Kio) |
| --- | ---: | ---: | ---: | ---: |
| Array[Pair] | 2.45 | 117 | 533552 | 2500 |
| HashMap | 3.56 | 207 | 813488 | 2948 |
| PersistentMap | 17.14 | 150657 | 370424 | 3780 |
| HashSet | 2.75 | 207 | 543152 | 2492 |
| PersistentSet | 17.70 | 151715 | 366472 | 3780 |

Dans ce scénario, le partage réduit le pic de stockage Janus des versions
conservées, mais ne compense pas le coût de construction initiale par insertions
persistantes : les allocations, le temps total et le RSS restent supérieurs aux
copies des collections mutables. La réduction du pic Janus ne garantit donc pas
une réduction du RSS du processus.
