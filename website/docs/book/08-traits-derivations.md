<span class="chapter-kicker">CHAPITRE 08 / DÉCRIRE DES CAPACITÉS</span>
# Traits et dérivations

## Objectifs

- déclarer un contrat avec `trait` ;
- l’implémenter avec `extends` ;
- demander une génération structurelle avec `derives` ;
- choisir correctement `Copy`, `Equality`, `Hashing` et `Debug`.

## Traits et implémentations

Un trait décrit des méthodes sans corps. Une classe promet de les fournir avec `extends` :

```janus
// doctest: doctest name=trait-basic
trait Area {
    def area() : int
}

class Rectangle(val width : int, val height : int)
extends Area {
    def area() : int {
        return width * height
    }
}

def measured[T <: Area](shape : T) : int {
    return shape.area()
}

def main() : int {
    return measured[Rectangle](new Rectangle(6, 7)) - 42
}
```

Une déclaration peut étendre plusieurs traits, séparés par des virgules. La signature, la visibilité et le caractère consommant de chaque méthode doivent correspondre au contrat.

!!! note "Limite 0.10.0"
    L’implémentation de traits par `extends` est actuellement réservée aux classes. Les structs restent utilisables avec les capacités intrinsèques de `derives`, mais ne peuvent pas encore implémenter un trait utilisateur.

## Pourquoi `derives` existe

Comparer, hacher ou afficher récursivement tous les champs produit du code répétitif. La clause `derives` demande au compilateur de générer une capacité intrinsèque sûre :

```janus
// doctest: doctest name=derive-point
struct Point(val x : int, val y : int)
derives Copy, Equality, Hashing, Debug {}

def main() : int {
    val first : Point = new Point(20, 22)
    val second : Point = first
    if first != second {
        return 1
    }
    debug(first)
    return second.x + second.y - 42
}
```

La clause vient après les paramètres génériques et `extends`, mais avant le corps. Elle est explicite : sans `derives`, aucune capacité n’est ajoutée par ressemblance.

## Les quatre capacités

| Capacité | Ce qu’elle autorise | Conditions importantes |
| --- | --- | --- |
| `Copy` | réutiliser une valeur après une affectation ou un passage | structs/enums seulement ; tous les éléments doivent être `Copy` |
| `Equality` | `==` et `!=` | tous les éléments doivent prendre en charge l’égalité |
| `Hashing` | utiliser `DerivedHashing[T]` | exige aussi `Equality` dans la même clause |
| `Debug` | appeler `debug(value)` | tous les éléments doivent être affichables en diagnostic |

### `Copy` n’est pas un clonage

`Copy` garantit une duplication implicite sans risque. Une classe, une closure propriétaire, un pointeur propriétaire ou un agrégat qui en contient ne peut pas être `Copy`. Janus refuse donc de dupliquer accidentellement une ressource.

### `Equality` et `Hashing`

`Equality` compare les champs dans l’ordre structurel. Pour un enum, les variantes doivent d’abord être identiques. Une classe peut demander une égalité structurelle, mais ne peut jamais dériver `Copy`.

`Hashing` doit rester cohérent avec l’égalité, d’où l’obligation d’écrire les deux :

```janus
struct UserId(val value : int)
derives Copy, Equality, Hashing {}
```

Pour une collection hachée, instanciez `DerivedHashing[UserId]` depuis `std.hashing`.

### `Debug` n’est pas `println`

`debug(value)` écrit une représentation déterministe destinée au diagnostic, par exemple `Point { x: 20, y: 22 }`. `print` et `println` restent réservés aux types primitifs imprimables et ne changent pas de comportement.

## Enums, classes et génériques

```janus
enum Status[T] derives Equality, Debug {
    Ready(T),
    Failed
}

class Report(val message : string)
derives Equality, Hashing, Debug {}
```

Pour un type générique, l’éligibilité est vérifiée à la spécialisation. `Status[int]` peut utiliser les capacités ci-dessus parce que `int` les fournit. Les noms sont sensibles à la casse, fermés à ces quatre valeurs et ne désignent pas des traits utilisateur homonymes.

!!! warning "Règles à retenir"
    `Hashing` sans `Equality`, une capacité répétée, une capacité inconnue et `Copy` sur une classe sont des erreurs de compilation. Les champs `private` participent tout de même à la dérivation, sans devenir accessibles au programme.

## Exercice

Déclarez un enum `Command` avec `Stop` et `Move(int, int)`, dérivez `Copy`, `Equality` et `Debug`, puis comparez deux commandes.

??? success "Correction"
    ```janus
    enum Command derives Copy, Equality, Debug {
        Stop,
        Move(int, int)
    }

    def main() : int {
        val left : Command = Command.Move(2, 3)
        val right : Command = left
        debug(right)
        if left == right {
            return 0
        }
        return 1
    }
    ```

Pour un exemple guidé avec une collection hachée, continuez avec le [tutoriel sur `derives`](../tutorials/derives-copy-debug.md).

<div class="lesson-nav"><a href="../07-generiques-closures/">← Génériques et closures</a><a href="../09-propriete-avancee/">Propriété avancée →</a></div>
