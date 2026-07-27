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

`get`, `getOption`, l'itération et les opérations à callback qui copient `T`
restent limitées aux éléments `Copy`. Le compilateur propose les opérations de
transfert lorsqu'elles sont appelées sur un tableau propriétaire. Les
observateurs bornés et l'itérateur consommant seront ajoutés dans l'étape
suivante de la roadmap 0.6.

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

Les itérateurs de set/map et `HashMap.getOption` restent disponibles uniquement
lorsque les éléments qu'ils retournent sont `Copy`.
