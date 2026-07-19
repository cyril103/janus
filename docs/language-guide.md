# Guide du langage Janus

Janus est fortement typé : chaque variable, paramètre et retour possède un
type connu à la compilation. Les conversions entre types sont explicites.

## Point d'entrée

Un programme exécutable possède une fonction `main` sans paramètre :

```janus
def main() : int {
    return 0
}
```

La valeur retournée est le code de sortie du programme. Zéro indique
généralement un succès.

## Valeurs, variables et types

`val` crée une valeur qui ne peut plus être réaffectée :

```janus
val answer : int = 42
```

`var` crée une variable modifiable :

```janus
var count : int = 0
count = count + 1
```

Une `var` peut être déclarée sans valeur, mais elle doit être initialisée avant
sa première lecture :

```janus
var result : int
result = 42
```

Types primitifs :

| Type | Utilisation |
| --- | --- |
| `int` | entier signé sur 32 bits |
| `double` | nombre flottant sur 64 bits |
| `byte` | entier signé sur 8 bits |
| `char` | caractère Unicode sur 32 bits |
| `bool` | `true` ou `false` |
| `string` | chaîne UTF-8 immuable |
| `usize` | taille ou adresse non signée |
| `Unit` | fonction qui ne retourne aucune valeur |

Janus ne convertit pas automatiquement `int` en `double`. Utilisez un cast :

```janus
val ratio : double = double(5) / 2.0
```

## Fonctions et généricité

```janus
def maximum(left : int, right : int) : int {
    if left > right {
        return left
    }
    return right
}

def identity[T](value : T) : T {
    return value
}
```

Les fonctions sont des valeurs de première classe. Une closure peut capturer
les valeurs qui l'entourent :

```janus
val threshold : int = 10
val isLarge : (int) => bool =
    (value : int) => value > threshold
```

Une closure possédée doit être libérée avec `delete` lorsqu'elle n'est plus
utilisée.

## Contrôle de flux

```janus
if value > 0 {
    println("positif")
} else {
    println("nul ou négatif")
}

while value < 10 {
    value = value + 1
}

for item in collection {
    println("élément")
}
```

`break` quitte la boucle la plus proche et `continue` passe à l'itération
suivante.

## Enums, `match`, `Option` et `Result`

Un enum peut transporter des données et être générique :

```janus
enum Option[T] {
    Some(T),
    None
}

val result : int = match option {
    Some(value) => value,
    None => 0
}
```

La bibliothèque standard fournit `Option[T]` pour une valeur éventuellement
absente et `Result[T, E]` pour une opération qui peut échouer. L'opérateur `?`
propage automatiquement une absence ou une erreur depuis une fonction
compatible.

## Classes, traits et visibilité

Les paramètres `val` et `var` du constructeur deviennent des champs :

```janus
class Point(var x : int, var y : int) {
    private val secret : int = 42

    def move(dx : int, dy : int) : Unit {
        x = x + dx
        y = y + dy
    }

    destructor {
    }
}
```

Les classes et les traits peuvent recevoir des paramètres génériques. Le mot
clé `private` protège un champ ou une méthode contre les accès extérieurs.

## Gestion manuelle de la mémoire

Les objets créés avec `new` sont placés sur le tas. Janus n'a pas de
ramasse-miettes : le programmeur doit les libérer.

```janus
val point : Point = new Point(1, 2)
delete point
```

`defer` programme un nettoyage à la sortie de la portée, y compris lors d'un
`return`, `break` ou `continue` :

```janus
val point : Point = new Point(1, 2)
defer delete point
```

Les destructeurs exécutent le nettoyage propre à une classe avant la
libération de sa mémoire.

## Collections et itérateurs

La bibliothèque standard comprend notamment :

- `Array[T]`, tableau dynamique ;
- `HashSet[T, H]`, ensemble sans doublons ;
- `HashMap[K, V, H]`, table associative ;
- builders de tableaux, ensembles et tables ;
- `Range` et itérateurs paresseux ;
- `Option[T]` et `Result[T, E]`.

Exemple :

```janus
import std.array

val values : Array[int] = new Array[int](usize(4))
defer delete values
values.push(10)
values.push(20)

val doubled : Array[int] =
    values.map[int]((value : int) => value * 2)
defer delete doubled
```

Les opérations `map`, `filter`, `fold`, `foreach`, `find`, `any`, `all` et
`count` acceptent des closures.

## Modules

Les fonctionnalités de la bibliothèque standard sont importées explicitement :

```janus
import std.array
import std.option
import std.result
```

Consultez les fichiers du dossier [`stdlib/std`](../stdlib/std) pour les
modules actuellement disponibles.

## Programmation bas niveau et C

Janus propose des pointeurs typés `Ptr[T]`, `alloc`, `free`, des casts
explicites et des fonctions C externes :

```janus
import std.c

extern("abs") def absolute(value : int) : int
```

Ces opérations peuvent produire des adresses invalides, des fuites ou des
accès mémoire incorrects. Elles sont volontairement réservées au code qui doit
contrôler précisément sa représentation et sa mémoire.

Des programmes complets sont disponibles dans [`examples`](../examples).
