# Migration de Janus 0.5 vers 0.6

## `Array` et valeurs propriétaires

En 0.5.x, `Array[T]` exigeait `T <: Copy`. En 0.6, la contrainte du tableau
lui-même disparaît afin de permettre `Array[Resource]`, ainsi que les structs
et enums qui possèdent récursivement une ressource.

Les programmes utilisant `Array[int]` ou un autre élément `Copy` conservent
leurs signatures et leur comportement. Aucun changement n'est requis pour
`get`, `getOption`, `set`, `push`, `pop`, `popOption`, `reserve`, `clear`,
`foreach`, `map`, `filter`, `find`, `fold`, `any`, `all`, `count` et
`iterator`.

Pour un élément propriétaire :

- passez la valeur avec `move` à `push`, `set` et `replace` ;
- récupérez la propriété avec `remove`, `pop` ou `replace` ;
- détruisez toute valeur récupérée, ou transférez-la à un autre propriétaire ;
- utilisez `clear` pour détruire les éléments sans libérer la capacité ;
- détruisez le tableau, généralement avec `defer delete array`, afin de
  détruire les éléments restants puis le stockage.

```janus
val resources : Array[Resource] = new Array[Resource](usize(2))
defer delete resources

val first : Resource = new Resource()
resources.push(move first)

val replacement : Resource = new Resource()
val previous : Resource =
    resources.replace(usize(0), move replacement)
defer delete previous
```

`get`, `getOption` et `iterator()` restent limités aux éléments `Copy`.
Pour observer un élément propriétaire sans le retirer, utilisez une lambda
littérale avec `withValue` ou `foreach`. Pour transférer les éléments, consommez
le tableau avec `intoIterator()`.

## Collections de hachage et builders

`HashSet[T, H]`, `HashMap[K, V, H]`, `ArrayBuilder`, `SetBuilder`,
`MapBuilder` et le trait `Builder` n'imposent plus `Copy` aux éléments stockés.
Pour une valeur propriétaire, utilisez `move` avec `add` et `put`.

Un `HashSet` possède la valeur après `add`, même lorsqu'une clé équivalente
existe déjà ; la valeur entrante est alors détruite. `HashMap.put` possède la
nouvelle clé et la nouvelle valeur, détruit l'ancienne clé lors d'un
remplacement et retourne l'ancienne valeur dans `Option[V]`.

```janus
val key : Resource = new Resource(1)
val value : Resource = new Resource(2)
val previous : Option[Resource] =
    resources.put(move key, move value)
defer delete previous
```

`contains`, `containsKey`, `getOption` et les clés passées à `remove` sont
observés et restent la propriété de l'appelant. Une implémentation de
`Hashing[T]` ne peut pas déplacer, détruire ou retourner ses paramètres
propriétaires. `clear`, les tombstones, le rehash et le destructeur détruisent
exactement les clés et valeurs qui restent stockées.

`HashMap.getOption` et les itérateurs observants de set/map restent disponibles
uniquement lorsque les éléments qu'ils retournent sont `Copy`.

## Itérateurs propriétaires

Les pipelines 0.5.x et 0.6.0 portant sur des types `Copy` restent valides :

```janus
val doubled : Array[int] = collectArray[int](
    values.iterator().map[int]((value : int) => value * 2)
)
```

Pour un élément propriétaire, l'entrée du pipeline doit être explicitement
consommante :

```janus
val retained : Array[Resource] = collectArray[Resource](
    resources.intoIterator()
    .filter((value : Resource) => value.isValid())
    .map[Resource]((value : Resource) => normalize(value))
)
```

`filter` observe son paramètre dans une lambda littérale bornée et détruit les
éléments refusés. `map`, `flatMap`, `fold` et les builders reçoivent la
propriété de chaque élément. `take` transfère au plus le nombre demandé puis
détruit le reste avec sa source. Un `for` direct sur un conteneur propriétaire
est rejeté afin d'éviter toute copie implicite ; utilisez `intoIterator()` pour
`Array`/`HashSet` et `intoEntries()` pour `HashMap`.

Le conteneur est consommé dès la création de l'itérateur. Une terminaison
normale ou anticipée (`break`, `continue`, `return`, `?`, panique) détruit
l'itérateur et les éléments non produits. L'élément déjà produit appartient au
corps de boucle et doit être transféré ou détruit.

Le parcours consommant d'un `Array` préserve l'ordre mais coûte `O(n)` par
élément produit et `O(n²)` pour un parcours complet. Un parcours consommant de
`HashSet` ou `HashMap` coûte `O(capacity)` au total. À la destruction de
l'itérateur, le stockage et toutes les valeurs restantes sont libérés ; le
conteneur d'origine demeure consommé et ne peut pas être réutilisé.

## Combinateurs `Option`

Janus 0.6.2 ajoute `isSome`, `isNone`, `map`, `andThen`, `orElse` et
`unwrapOr` au module `std.option`. Les anciens `match` restent valides et leur
comportement ne change pas.

Pour une `Option[T]` propriétaire, `isSome` et `isNone` observent une variable
locale sans la consommer. Les autres combinateurs transfèrent leur entrée :

```janus
val normalized : Option[Resource] = map[Resource, Resource](
    move pending,
    (resource : Resource) => normalize(resource)
)
```

Après ce `move`, `pending` est inutilisable. `orElse` et `unwrapOr` prennent
également possession du repli et le détruisent si la variante `Some` le rend
inutile. Le résultat devient la responsabilité de l'appelant. Avec un type
`Copy`, aucun `move` n'est requis.
