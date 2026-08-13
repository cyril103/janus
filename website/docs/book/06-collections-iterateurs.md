<span class="chapter-kicker">CHAPITRE 06 / TRAITER DES SÉQUENCES</span>
# Collections et itérateurs

## Objectifs

- créer et libérer un `Array` ;
- parcourir des éléments copiables ;
- construire un pipeline `map` / `filter`.

## Un tableau dynamique

```janus
// doctest: doctest name=arrays
import std.array

def main() : int {
    val values : Array[int] = new Array[int](usize(4))
    defer delete values
    values.push(10)
    values.push(20)
    values.push(30)

    for value in values {
        println(value)
    }
    return 0
}
```

La capacité initiale est un `usize`. Le tableau est possédé et doit être
détruit. `Array[T]` accepte aussi les valeurs propriétaires : les parcours
observants comme `iterator()` exigent des éléments `Copy`, tandis que
`intoIterator()` consomme le tableau et transfère ses éléments.

## Pipeline paresseux

```janus
import std.array
import std.array_builder

val doubled : Array[int] = collectArray[int](
    values.iterator()
        .map[int]((value : int) => value * 2)
        .filter((value : int) => value > 20)
)
defer delete doubled
```

Les adaptateurs sont évalués lors de la consommation. Pour `HashSet` et `HashMap`, fournissez une stratégie de hachage adaptée au type de clé.

## Exercice

Créez un tableau contenant `2`, `4`, `6`, parcourez-le et affichez leur somme.

??? success "Correction"
    ```janus
    import std.array

    def main() : int {
        val values : Array[int] = new Array[int](usize(3))
        defer delete values
        values.push(2)
        values.push(4)
        values.push(6)

        var total : int = 0
        for value in values {
            total = total + value
        }
        println(total)
        return 0
    }
    ```

# Littéraux de tableaux

`[e1, e2]` produit un `Array[T]`. Le type `T` est fourni par une annotation
`Array[T]` ou inféré d'éléments non vides homogènes; `[]` exige une annotation.
Les éléments sont convertis vers `T`, évalués une seule fois de gauche à droite
et transférés au tableau avec les règles d'ownership de `Array.push`. Détruisez
le tableau avec `delete` (souvent via `defer`). Les constantes globales ne
peuvent pas employer cette syntaxe (`JANA0023`), car `Array[T]` possède du
stockage dynamique construit à l'exécution.

<div class="lesson-nav"><a href="../05-erreurs-propriete/">← Erreurs et propriété</a><a href="../07-generiques-closures/">Génériques et closures →</a></div>
