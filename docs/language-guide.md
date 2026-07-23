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

`val` et `var` peuvent aussi être déclarées au niveau du module. Une globale
doit toujours avoir un initialiseur :

```janus
val answer : int = 42
var requests : int = 0

def recordRequest() : Unit {
    requests = requests + 1
}
```

Une globale `private` n'est visible que depuis son module :

```janus
private val internalName : string = "janus"
```

Les expressions constantes sont calculées à la compilation. Elles peuvent
combiner des littéraux, des opérateurs purs et d'autres `val` globales :

```janus
val minute : int = 60
val hour : int = minute * 60
val ready : bool = hour == 3600 && !false
```

Le compilateur signale les cycles de dépendances, débordements et divisions par
zéro. Un initialiseur non constant est exécuté avant `main`, dans l'ordre des
imports puis des déclarations :

```janus
val configuration : Configuration = loadConfiguration()
private val callback : () => int = () => configuration.status()
```

Les valeurs globales possédantes doivent être déclarées avec `val`. Elles sont
détruites automatiquement, en ordre inverse, après `main` et ne peuvent pas
être déplacées ou supprimées manuellement. Les enums et structures globales
sont pris en charge, y compris lorsqu'elles contiennent récursivement des
ressources possédées. Contrairement à une `var` locale, une `var` globale ne
peut pas être déclarée sans initialiseur.

Un export public peut être utilisé sans qualification ou avec le nom de son
module :

```janus
settings.requestCount = settings.requestCount + 1
```

Deux modules peuvent déclarer le même nom privé, tandis que deux exports
publics de même nom produisent un diagnostic de collision.

Types primitifs :

| Type | Utilisation |
| --- | --- |
| `int` | entier signé sur 32 bits |
| `uint` | entier non signé sur 32 bits |
| `long` | entier signé sur 64 bits |
| `ulong` | entier non signé sur 64 bits |
| `float` | nombre flottant sur 32 bits |
| `double` | nombre flottant sur 64 bits |
| `byte` | entier signé sur 8 bits |
| `ubyte` | entier non signé sur 8 bits |
| `short` | entier signé sur 16 bits |
| `ushort` | entier non signé sur 16 bits |
| `char` | caractère Unicode sur 32 bits |
| `bool` | `true` ou `false` |
| `string` | chaîne UTF-8 immuable |
| `usize` | taille ou adresse non signée |
| `isize` | taille ou différence d'adresses signée |
| `Unit` | fonction qui ne retourne aucune valeur |

Les entiers ont une taille fixe et portable :

| Type | Plage |
| --- | --- |
| `byte` | `-128` à `127` |
| `ubyte` | `0` à `255` |
| `short` | `-32768` à `32767` |
| `ushort` | `0` à `65535` |
| `int` | `-2147483648` à `2147483647` |
| `uint` | `0` à `4294967295` |
| `long` | `-9223372036854775808` à `9223372036854775807` |
| `ulong` | `0` à `18446744073709551615` |
| `isize` | `-9223372036854775808` à `9223372036854775807` |
| `usize` | `0` à `18446744073709551615` |

Les littéraux entiers sans cast ont le type `int`. La plage complète de `int`
est acceptée, y compris `-2147483648`; `2147483648` et `-2147483649` sont
rejetés. Un `usize` supérieur à `2147483647` doit venir d'un calcul ou d'un cast
explicite. Un littéral peut initialiser directement `byte`, `ubyte`, `short` ou
`ushort` lorsque sa valeur tient dans le type. Les autres conversions numériques
restent explicites.

Pour les types entiers, les opérations `+`, `-` et `*` s'enroulent modulo
`2^largeur`. Le moins unaire suit la même règle pour les types signés. Les casts
entier-vers-entier conservent les bits de poids faible lors d'un rétrécissement;
l'élargissement étend le signe depuis une source signée et étend avec des zéros
depuis une source non signée.

La division signée `/` tronque vers zéro et le reste signé `%` prend le signe du
dividende. Les types non signés utilisent les règles non signées. Diviser ou
prendre le reste par zéro provoque un `panic` déterministe. Pour les types
signés, `MIN / -1` et `MIN % -1` provoquent aussi un `panic` déterministe.

Janus ne convertit pas automatiquement `int` en `double`. Utilisez un cast :

```janus
val ratio : double = double(5) / 2.0
```

Les littéraux flottants ont le type `double`. Construisez explicitement un
`float` lorsqu'une valeur sur 32 bits est souhaitée :

```janus
val opacity : float = float(0.75)
```

Les casts depuis `double` vers un entier sont définis seulement pour les valeurs
finies, représentables dans le type cible après troncature vers zéro. Les autres
cas ne sont pas vérifiés par le langage.

## Sortie canonique

`print(value)` et `println(value)` écrivent sur la sortie standard. Les deux
builtins acceptent les valeurs primitives imprimables : `int`, `usize`,
`double`, `byte`, `char`, `bool` et `string`.

Pour les programmes et les outils dont la sortie est vérifiée par des tests,
préférez une ligne par valeur :

```janus
def main() : int {
    println(42)
    println(usize(2147483648.0))
    println(3.5)
    println("done")
    return 0
}
```

La sortie exacte est :

```text
42
2147483648
3.5
done
```

Chaque appel à `println(value)` ajoute une fin de ligne logique après la valeur.
Sur les systèmes POSIX, cette fin de ligne est écrite sous forme LF ; en mode
texte Windows, elle peut être émise sous forme CRLF. Les tests de programmes
doivent donc normaliser CRLF vers LF avant de comparer la sortie texte complète,
newline final inclus. Utilisez `print(label)` seulement pour préfixer une ligne,
puis `println(value)` pour terminer cette ligne :

```janus
print("answer: ")
println(42)
```

Les entiers `int` et `usize` sont imprimés en base 10. Les chaînes sont écrites
telles quelles, sans guillemets ajoutés. Les `double` sont imprimés avec un
format flottant stable du runtime ; ne les utilisez pas comme représentation
d'entiers lorsque la précision peut être perdue.

Les littéraux entiers Janus sans cast sont limités à la plage de `int`. Si vous
casteez depuis un `double`, choisissez uniquement une valeur entière finie et
représentable après troncature, comme `2147483648.0`, et n'en déduisez pas une
garantie pour tous les grands entiers.

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
    internal val identifier : int = 7

    def move(dx : int, dy : int) : Unit {
        x = x + dx
        y = y + dy
    }

    destructor {
    }
}
```

Les classes et les traits peuvent recevoir des paramètres génériques. Le mot
clé `private` réserve un champ ou une méthode à sa classe. `internal` autorise
les autres déclarations du même module à y accéder, tout en interdisant son
utilisation depuis les modules importateurs.

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

Les structures et enums qui contiennent une ressource deviennent eux-mêmes
propriétaires. Leur transfert doit employer `move`, et `delete` détruit
récursivement leur contenu :

```janus
struct Box(val resource : Resource) {}
enum MaybeBox { Some(Box), None }

val box : Box = new Box(new Resource())
val optional : MaybeBox = MaybeBox.Some(move box)
val extracted : Box = match move optional {
    Some(value) => move value,
    None => new Box(new Resource())
}
defer delete extracted
```

Une contrainte générique `T <: Copy` garantit qu'un type peut être recopié
sans dupliquer une ressource. Les tableaux, ensembles, tables de hachage,
builders et itérateurs de la bibliothèque standard utilisent cette contrainte.

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

Un pipeline paresseux est matérialisé avec `collectArray`, fourni par
`std.array_builder` :

```janus
import std.array_builder

val collected : Array[int] = collectArray[int](
    values.iterator().map[int]((value : int) => value * 2)
)
defer delete collected
```

Les exemples `array.janus`, `hash_collections.janus` et
`iterator_pipeline.janus` séparent respectivement les opérations de tableau,
les tables de hachage et les pipelines paresseux.

## Mathématiques

Le module `std.math` fournit des helpers entiers non signés :

```janus
import std.math

def gcd(left : usize, right : usize) : usize
def lcm(left : usize, right : usize) : usize
def is_prime(value : usize) : bool
def prime_factors(value : usize) : Array[usize]
```

Ces fonctions utilisent `usize` parce qu'elles ciblent les tailles, indices,
capacités et identifiants non négatifs qui dominent les usages bas niveau et
les exercices numériques. Cette API évite aussi les ambiguïtés liées au signe
pour le plus grand commun diviseur, le plus petit commun multiple et les tests
de primalité ou de factorisation. Il n'existe pas de surcharge `int` ni de
version `BigInt`.

`gcd` applique l'algorithme d'Euclide itératif. `gcd(0, 0)` retourne `0` ;
si un seul argument vaut zéro, le résultat est l'autre argument.

`lcm` retourne `0` si l'un des deux arguments vaut zéro. Sinon, il réduit
d'abord `left` par `gcd(left, right)` avant de multiplier, afin de limiter le
risque d'overflow intermédiaire. Si le résultat ne tient pas dans `usize`,
`lcm` provoque un `panic` déterministe avec le message `lcm overflow`.

`is_prime` retourne `false` pour les valeurs inférieures à `2`, `true` pour
`2`, et `false` pour les nombres pairs plus grands que `2`. Les diviseurs
impairs sont testés tant que `divisor <= value / divisor`, pour éviter les
multiplications qui pourraient s'enrouler avec les règles arithmétiques de
`usize`.

`prime_factors` retourne un nouveau `Array[usize]` possédé par l'appelant. Les
facteurs premiers sont en ordre croissant et les multiplicités sont conservées :
`prime_factors(usize(12))` retourne `2, 2, 3`. Pour `0` et `1`, le tableau est
vide. Pour une entrée première, le tableau contient seulement cette entrée.
L'appelant doit libérer le tableau avec `delete`.

```janus
import std.math
import std.array

def main() : int {
    val factors : Array[usize] = prime_factors(usize(49))
    defer delete factors

    println(factors.get(usize(0))) // 7
    println(factors.get(usize(1))) // 7
    return 0
}
```

L'implémentation utilise une division d'essai déterministe : elle divise d'abord
tous les facteurs `2`, puis teste les candidats impairs. La borne de boucle est
recalculée sur le reste réduit avec `candidate <= remaining / candidate`, ce qui
évite `candidate * candidate` et ses risques d'overflow. La complexité en temps
est `O(sqrt(n))` dans le pire cas, avec une allocation proportionnelle au nombre
de facteurs retournés.

## Modules

Les fonctionnalités de la bibliothèque standard sont importées explicitement :

```janus
import std.array
import std.math
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

## Structures copiées par valeur

Une `struct` regroupe de petites données sans allocation dynamique. Sa syntaxe
reprend celle des champs de constructeur d'une classe :

```janus
struct Point(var x : int, var y : int) {
    def translate(dx : int, dy : int) : Unit {
        x = x + dx
        y = y + dy
    }
}

val original : Point = new Point(10, 20)
var copy : Point = original
copy.translate(5, 0)
```

L'affectation et le passage à une fonction copient la valeur. `original` reste
donc inchangé lorsque `copy` est modifié. Une structure n'utilise ni `move` ni
`delete` et ne peut pas déclarer de destructeur. Dans cette première version,
ses champs doivent tous être déclarés entre parenthèses avec `val` ou `var`.

## Graphisme 2D

Le module expérimental `std.graphics` permet de créer une fenêtre, dessiner des
formes et du texte, et lire le clavier ou la souris. Son backend raylib 6 est
chargé dynamiquement. Consultez le [guide du graphisme](graphics.md) pour
l'installation et un premier programme.
