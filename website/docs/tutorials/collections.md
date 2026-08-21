# Transformer une collection

## Prérequis

- savoir créer et exécuter un projet Janus ;
- connaître `defer delete` ;
- Janus 0.19.0, sans paquet additionnel.

## Résultat

À partir de `10, 20, 30`, doubler les valeurs, ne garder que celles supérieures à `20`, matérialiser le pipeline et afficher `2`.

## 1. Créer le tableau possédé

```janus
import std.array
import std.array_builder

val values : Array[int] = new Array[int](usize(4))
defer delete values
values.push(10)
values.push(20)
values.push(30)
```

La capacité est un `usize`. Le `defer` garantit la libération de l’allocation à la sortie de la portée.

## 2. Composer sans matérialiser à chaque étape

Le code suivant est celui de l’exemple réel [`examples/iterator_pipeline.janus`](https://github.com/cyril103/janus/blob/v0.10.0/examples/iterator_pipeline.janus) :

```janus
val collected : Array[int] = collectArray[int](
    values.iterator()
        .map[int]((value : int) => value * 2)
        .filter((value : int) => value > 20)
)
defer delete collected
```

`iterator()` crée un parcours paresseux. `map` et `filter` composent des adaptateurs ; `collectArray` du module `std.array_builder` alloue le résultat final.

## 3. Programme complet

```janus
// doctest: doctest name=collection-pipeline
import std.array
import std.array_builder

def main() : int {
    val values : Array[int] = new Array[int](usize(4))
    defer delete values
    values.push(10)
    values.push(20)
    values.push(30)

    val collected : Array[int] = collectArray[int](
        values.iterator()
            .map[int]((value : int) => value * 2)
            .filter((value : int) => value > 20)
    )
    defer delete collected
    println(collected.size())
    return 0
}
```

## Vérifier

```bash
janus check
janus run
```

La sortie doit être `2` : les valeurs matérialisées sont `40` et `60`.

## Prolongements

- ajoutez `.take(usize(1))` avant la collecte ;
- remplacez le pipeline par `values.fold[int](...)` pour calculer une somme ;
- explorez `find`, `any`, `all` et `count` dans la [référence](../reference/generated/language-guide.md#collections-et-iterateurs).
