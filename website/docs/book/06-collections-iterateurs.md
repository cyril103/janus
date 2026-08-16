<span class="chapter-kicker">CHAPITRE 06 / TRAITER DES SÉQUENCES</span>
# Collections et itérateurs

## Objectifs

- créer et libérer un `Array` ;
- parcourir des éléments copiables ;
- construire un pipeline `map` / `filter` ;
- choisir entre littéral, fabrique et tri sur place.

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

## Littéraux de tableaux

`[e1, e2]` produit un `Array[T]`. Le type `T` est fourni par une annotation
`Array[T]` ou inféré d'éléments non vides homogènes ; `[]` exige une annotation.
Les éléments sont convertis vers `T`, évalués une seule fois de gauche à droite
et transférés au tableau avec les règles de propriété de `Array.push`.

```janus
val scores : Array[int] = [12, 8, 19]
defer delete scores
val empty : Array[int] = []
defer delete empty
```

Une constante globale ne peut pas employer cette syntaxe (`JANA0023`), car le
tableau possède un stockage dynamique construit à l'exécution.

## Fabriquer et trier

`filledArray[T](count, value)` répète une valeur `Copy`.
`generateArray[T](count, factory)` appelle la fabrique une fois pour chaque
index croissant et accepte aussi une valeur propriétaire produite à chaque
appel. Les deux fonctions nettoient le préfixe déjà construit si une panique
interrompt la création.

```janus
// doctest: doctest name=array-factories-and-sort
import std.array

def main() : int {
    val values : Array[usize] = generateArray[usize](
        usize(5),
        (index : usize) => usize(5) - index
    )
    defer delete values
    values.sortWith((left : usize, right : usize) => left < right)

    val flags : Array[bool] = filledArray[bool](usize(3), true)
    defer delete flags
    return if values.get(usize(0)) == usize(1) && flags.size() == usize(3) { 0 } else { 1 }
}
```

`sortWith` trie sur place selon le comparateur. Le tri est stable, coûte
`O(n log n)` au pire et utilise `O(n)` de mémoire auxiliaire. Comme
l'implémentation copie temporairement les éléments, `T` doit satisfaire
`Copy`.

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

<div class="lesson-nav"><a href="../05-erreurs-propriete/">← Erreurs et propriété</a><a href="../07-generiques-closures/">Génériques et closures →</a></div>
