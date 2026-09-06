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
    val values : Array[int] = new Array(4)
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

La capacité initiale attend un `usize`; le littéral positif est converti par ce
contexte. Le tableau est possédé et doit être
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

## Indexation sûre

Une lecture `values[index]` utilise le contrat `std.index.Index[Key]`, copie
l'élément `Output` et exige donc `Copy`. Une affectation utilise
`std.index.IndexMut[Key]` et remplace l'élément comme `set`; une ressource
nommée exige `move`. Une affectation composée exige les deux contrats avec des
types `Key` et `Output` identiques. Le conteneur puis l'index sont évalués
chacun une fois, de gauche à droite, y compris pour une affectation composée.

```janus
// doctest: doctest name=array-indexing
import std.array
def main() : int {
    val scores : Array[int] = [12, 8, 19]
    scores[1] += 2
    val middle : int = scores[1]
    delete scores
    return if middle == 10 { 0 } else { 1 }
}
```

Pour un élément non `Copy`, utilisez `withValue` ou `getBorrowed` : les
crochets n'introduisent jamais d'emprunt implicite.

Un type utilisateur participe à la même syntaxe en implémentant les contrats.
Les noms courts `Index`/`IndexMut` ne sont jamais suffisants par eux-mêmes : le
compilateur résout l'identité canonique de `std.index`, même derrière un alias.

```janus
import std.index

class Cell[T](var value : T) extends Index[int], IndexMut[int] {
    type Output = T
    borrow def get(key : int) : T where T <: Copy { return value }
    def set(key : int, replacement : T) : Unit { value = move replacement }
}
```

`Array` et `Slice` utilisent `usize`; `MutableSlice` fournit aussi
`IndexMut[usize]`. Les contrôles de bornes restent définis par `get` et `set` :
leurs diagnostics et leur comportement de `panic` sont donc ceux du conteneur.

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
        5,
        (index : usize) => usize(5) - index
    )
    defer delete values
    values.sortWith((left : usize, right : usize) => left < right)

    val flags : Array[bool] = filledArray[bool](3, true)
    defer delete flags
    return if values.get(0) == usize(1) && flags.size() == usize(3) { 0 } else { 1 }
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
        val values : Array[int] = new Array(3)
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
