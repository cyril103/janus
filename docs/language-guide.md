# Guide du langage Janus

> **Statut des API 0.8.** La syntaxe et la bibliothèque restent pré-1.0.
> L'[inventaire de stabilité 0.8](stability-inventory-0.8.md) classe les
> surfaces candidates et expérimentales. L'[inventaire machine de la surface
> publique](public-surface-0.5.json), dont le nom historique est conservé pour
> compatibilité avec les outils, relie chaque module à sa source canonique et à
> sa documentation.

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

Dans une fonction, une déclaration initialisée peut omettre son annotation
lorsque l'initialiseur détermine un type unique :

```janus
val result = compute()
var state = 0
val resource = new Resource()
```

La grammaire des déclarations locales est
`("borrow"? ("val" | "var") identifier (":" type)? "=" expression)` ou,
uniquement pour une `var` annotée, `("var" identifier ":" type)`. Le type
inféré d'une `var` est fixé à sa déclaration ; une affectation ne le modifie
jamais. Les littéraux entiers, réels et caractères gardent respectivement les
types par défaut `int`, `double` et `char`.

### Littéraux entiers

Un entier s'écrit en décimal (`42`), en hexadécimal avec `0x` ou `0X`
(`0xA2_0A`), ou en binaire avec `0b` ou `0B` (`0b1111_0000`). Les chiffres
hexadécimaux ne sont pas sensibles à la casse. `_` peut séparer deux chiffres
valides de la base, mais ne peut ni suivre le préfixe, ni terminer le littéral,
ni être doublé.

La notation ne change ni la valeur ni le typage. Sans contexte, un littéral a
le type `int`; une annotation ou un argument attendu peut sélectionner
`byte`, `ubyte`, `short`, `ushort`, `int`, `uint`, `long`, `ulong`, `isize` ou
`usize`. La valeur doit tenir dans ce type. La négation autorise donc exactement
les minima signés, par exemple `-0x80 : byte` et `-0x8000_0000 : int`. Avant le
typage contextuel, toute magnitude doit tenir dans un `uint64`.

L'annotation reste obligatoire pour les globales, champs, paramètres et types
de retour. Elle est également nécessaire pour `null()` sans argument de type,
une collection vide sans type d'élément, des branches incompatibles ou un
générique dont tous les paramètres ne sont pas contraints. Le diagnostic
`cannot infer type of 'name'` propose alors d'ajouter une annotation.

Une `var` peut être déclarée sans valeur, mais elle doit être initialisée avant
sa première lecture :

```janus
var result : int
result = 42
```

`val` et `var` peuvent aussi être déclarées au niveau du module. Une globale
doit toujours avoir un type explicite et un initialiseur :

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

### Opérateurs bit à bit et décalages

`&`, `^` et `|` acceptent deux opérandes du même type entier et conservent ce
type. `<<` et `>>` acceptent un entier à gauche et un `usize` à droite ; un
littéral de compte est contextuellement typé `usize`. Le résultat conserve le
type gauche. `>>` est logique pour les types non signés et arithmétique pour
les types signés.

Priorité décroissante : `* / %`, `+ -`, `<< >>`, `< <= > >=`, `== !=`, `&`,
`^`, `|`, `&&`, `||`. Le compte valide va de zéro à `largeur - 1` pour les
largeurs 8, 16, 32 et 64. Une constante hors borne est diagnostiquée ; une
valeur hors borne à l'exécution déclenche un `panic` avant le décalage. Une
valeur négative n'est pas un `usize` valide.

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

La forme historique rend tous les exports publics disponibles sans
qualification :

```janus
import std.fs
```

Un module peut aussi être importé sous un alias sans injecter ses symboles, ou
exposer seulement une liste explicite. Le point est l'unique séparateur
d'accès qualifié :

```janus
import std.fs as fs
import std.result.{Result, Ok as Success, Error}

val loaded : fs.FileData = fs.readFile(path)
val result : Result[int, string] = Success(42)
```

Les alias sont locaux au module importeur et ne changent pas l'identité
canonique du symbole. Un symbole absent ou `private`, une liste vide et toute
collision entre deux noms importés produisent un diagnostic. Il faut qualifier
ou renommer les exports concurrents ; aucune collision n'est résolue
silencieusement.

Un export public peut également être utilisé avec le nom canonique de son
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

Après `=>`, une closure accepte soit l'expression historique, soit un bloc
d'instructions complet. Dans un bloc, le type résultat est inféré à partir des
`return`; leurs types doivent être cohérents et tout chemin d'un bloc non-`Unit`
doit retourner une valeur. Un bloc sans `return` produit `Unit`.

```janus
val adjust : (int) => int = (value : int) => {
    val next : int = value + threshold
    if next < 0 {
        return 0
    }
    return next
}
```

### Récursivité terminale

Un appel retourné directement par une fonction ou une méthode est en position
terminale : aucun calcul ne doit dépendre de son résultat avant de quitter la
fonction. Lorsque la signature d'appel est compatible, le compilateur remplace
le retour conventionnel par un saut terminal qui réutilise la pile d'exécution
courante. La profondeur de la pile reste donc constante, y compris dans une
construction sans optimisations.

```janus
tailrec def countDown(remaining : int, result : int) : int {
    if remaining == 0 {
        return result
    }
    return countDown(remaining - 1, result + 1)
}
```

Le mot-clé `tailrec` est obligatoire lorsqu'une fonction appartient à un cycle
récursif entièrement terminal que le backend peut garantir avec `musttail`. Il
transforme cette propriété en contrat vérifié : l'oublier produit une erreur de
compilation, tout comme l'ajouter à une récursion qui n'est pas réellement
terminale. La récursion ordinaire reste légale sans annotation.

La même règle s'applique aux méthodes et à la récursivité mutuelle lorsque les
signatures ABI sont compatibles :

```janus
class Counter() {
    tailrec def countDown(remaining : int, result : int) : int {
        if remaining == 0 {
            return result
        }
        return this.countDown(remaining - 1, result + 1)
    }
}
```

L'optimisation ne s'applique pas lorsqu'une opération reste à effectuer après
l'appel. Par exemple, l'appel récursif de `return value + sum(value - 1)` n'est
pas terminal et `sum` ne doit donc pas porter `tailrec`. Elle ne s'applique pas
non plus si un `defer` ou un nettoyage de possession doit encore être exécuté au
retour : chaque appel conserve alors son propre cadre de pile afin de respecter
l'ordre observable des nettoyages.

## Contrôle de flux

```janus
if value > 0 {
    println("positif")
} else if value < 0 {
    println("négatif")
} else {
    println("nul")
}

while value < 10 {
    value = value + 1
}

for item in collection {
    println("élément")
}
```

`break` quitte la boucle la plus proche et `continue` passe à l'itération
suivante. Une chaîne peut contenir autant de branches `else if` que nécessaire ;
chaque branche possède la même portée locale qu'un bloc `if` indépendant.

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

Un motif peut aussi être un littéral entier, booléen ou chaîne, ou `_` pour le
cas de repli. Une garde `if` est évaluée seulement après la correspondance du
motif ; elle voit donc les bindings du motif. Les bras sont essayés de haut en
bas et l'expression du premier motif dont la garde vaut `true` est évaluée. Les
bindings restent limités à la garde et à l'expression de leur bras.

```janus
def classify(value : uint) : int {
    return match value {
        uint(1) => 10,
        uint(2) => 20,
        _ => 0
    }
}

enum Opcode { Family(uint) }

def decode(opcode : Opcode) : int {
    return match opcode {
        Family(bits) if (bits & uint(0xF000)) == uint(0x8000) => 8,
        Family(bits) if bits == uint(0) => 0,
        Family(bits) => -1
    }
}
```

Une garde doit avoir le type `bool`. Un bras gardé ne contribue pas à
l'exhaustivité puisqu'une garde peut échouer ; un motif non gardé couvrant les
cas restants est donc nécessaire. Un motif `_` non gardé rend les bras suivants
inatteignables.

La bibliothèque standard fournit `Option[T]` pour une valeur éventuellement
absente et `Result[T, E]` pour une opération qui peut échouer. L'opérateur `?`
propage automatiquement une absence ou une erreur depuis une fonction
compatible.

Le module `std.option` fournit six combinateurs génériques :

| Fonction | Effet |
| --- | --- |
| `isSome[T](value)` | observe si la variante est `Some` |
| `isNone[T](value)` | observe si la variante est `None` |
| `map[T, U](value, transform)` | transforme le contenu de `Some` |
| `andThen[T, U](value, transform)` | chaîne une fonction qui retourne une `Option[U]` |
| `orElse[T](value, fallback)` | conserve `value` si présente, sinon retourne `fallback` |
| `unwrapOr[T](value, fallback)` | extrait la valeur présente, sinon retourne `fallback` |

Pour un type `Copy`, ces fonctions s'utilisent directement :

```janus
val value : Option[int] = Option.Some[int](40)
val incremented : Option[int] =
    map[int, int](value, (item : int) => item + 1)
val result : int = unwrapOr[int](incremented, 0)
```

`isSome` et `isNone` sont des observations bornées. Une `Option` propriétaire
doit être stockée dans une variable locale pendant l'observation et reste
ensuite utilisable :

```janus
val resource : Option[Resource] =
    Option.Some[Resource](new Resource())
if isSome[Resource](resource) {
    println("présente")
}
delete resource
```

Les quatre autres fonctions sont consommantes. Une variable `Option[Resource]`
doit leur être passée avec `move`; le résultat possède alors la valeur
transférée. `map` et `andThen` n'appellent leur closure que pour `Some`.
`orElse` détruit la valeur de repli lorsque la première option est présente ;
`unwrapOr` détruit de même le repli inutilisé. Pour `None`, le repli est
transféré au résultat. Une closure reçue par `map` ou `andThen` est détruite
après l'appel, y compris lorsque l'option est vide.

Les fonctions sont aussi accessibles avec leur nom qualifié, par exemple
`std.option.map`, ce qui évite toute ambiguïté avec les combinateurs similaires
de `std.result`.

Le module `std.result` propose les opérations correspondantes sur les deux
variantes :

| Fonction | Effet |
| --- | --- |
| `isOk[T, E](value)` | observe si la variante est `Ok` |
| `isError[T, E](value)` | observe si la variante est `Error` |
| `map[T, U, E](value, transform)` | transforme la valeur de `Ok` |
| `mapError[T, E, F](value, transform)` | transforme l'erreur |
| `andThen[T, U, E](value, transform)` | chaîne une opération qui peut échouer |
| `orElse[T, E, F](value, transform)` | récupère une erreur avec une autre opération |
| `unwrapOr[T, E](value, fallback)` | extrait la valeur ou retourne le repli |
| `toOption[T, E](value)` | conserve `Ok` comme `Some` et élimine l'erreur |
| `fromOption[T, E](value, error)` | convertit `Some` en `Ok` et `None` en `Error` |

`isOk` et `isError` observent une variable locale propriétaire sans la
consommer. Les autres opérations sont consommantes : la closure n'est appelée
que pour sa variante active, et la valeur inactive, le repli et la closure
sont détruits exactement une fois. Les noms qualifiés, comme
`std.result.map`, évitent les collisions avec `std.option`.

Les combinateurs peuvent précéder ou suivre `?` tant que les types d'erreur
restent compatibles :

```janus
def normalize(
input : Result[int, string]
) : Result[int, string] {
    val mapped : Result[int, string] =
    std.result.map[int, int, string](
    input,
    (value : int) => value + 1
    )
    val value : int = mapped?
    return Result.Ok[int, string](value * 2)
}
```

Pour un `Result` propriétaire, `?` consomme l'agrégat et exige un transfert
explicite : `val resource : Resource = (move pending)?`. `pending` devient
ensuite inutilisable. Une propagation d'erreur exécute les nettoyages actifs
avant de transférer l'erreur à l'appelant. Les conversions `toOption` et
`fromOption` transfèrent uniquement la variante active.

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

### Emprunts immuables

`borrow val` crée un alias temporaire en lecture seule sans copier la valeur et
sans lui transférer sa propriété. Le propriétaire ne peut être ni déplacé ni
détruit avant la fin de la portée de l'alias. Plusieurs emprunts immuables
peuvent coexister :

```janus
borrow val first : Document = document
borrow val second : Document = document
```

Une fonction reçoit le même type d'emprunt avec un paramètre `borrow`. Une
méthode accessible à travers cet emprunt déclare son récepteur avec
`borrow def` :

```janus
def inspect(borrow document : Document) : int {
    return document.revision()
}

class Document(val version : int) {
    borrow def revision() : int { return version }
}
```

Le corps d'une méthode `borrow def` et les paramètres `borrow` ne peuvent ni
modifier, ni déplacer, ni détruire la valeur observée. Un emprunt ne peut pas
être retourné ou transmis à un paramètre propriétaire. Dans cette première
implémentation, sa région est la portée lexicale de la liaison ; placez un
emprunt local dans un bloc plus court pour réutiliser ensuite le propriétaire.
`borrow var` crée un emprunt mutable exclusif. Le mot `var` donne le droit de
modifier la valeur visée ; il ne permet pas de réassigner l'alias :

```janus
def advance(borrow var cursor : Cursor) : Unit {
    cursor.position = cursor.position + 1
}

if ready {
    borrow var editable : Cursor = cursor
    editable.advance()
}
// `cursor` est de nouveau accessible ici.
```

Un seul emprunt mutable peut viser une valeur à la fois. Aucun emprunt partagé,
accès direct au propriétaire, déplacement ou destruction ne peut lui être
concurrent. Les modifications sont visibles par le propriétaire parce que
l'alias utilise le même stockage. Les projections de champs seront ajoutées
séparément.

Un emprunt actif interdit aussi les invalidations indirectes : appel d'une
méthode mutante ou consommatrice sur le propriétaire, passage à un paramètre
`consume`, et `realloc` ou `reallocPreserving` d'un pointeur emprunté. Un objet
avec un champ constructeur `borrow val` ou `borrow var` maintient respectivement
un emprunt partagé ou mutable jusqu'à sa
destruction ou la fin de son bloc. Cet objet ne peut pas être copié dans un
champ ou une globale, ni sortir de la fonction par `return`. Ces règles sont
conservatrices et lexicales ; un bloc court permet de terminer explicitement
la durée de vie observante.

Le module `std.slice` fournit des vues contiguës sur `Array[T]` pour les types
`Copy`. `Slice[T]` permet la lecture partagée ; `MutableSlice[T]` réserve un
accès exclusif et ajoute `set`. Les indices sont relatifs à la vue et contrôlés
à l'exécution. La plage est validée à la construction, le stockage n'est pas
copié et les deux vues implémentent `Iterable[T]` :

```janus
import std.array
import std.slice

val values : Array[int] = new Array[int](usize(3))
values.push(10)
values.push(20)
values.push(30)
if true {
    val middle : MutableSlice[int] =
    new MutableSlice[int](values, usize(1), usize(2))
    middle.set(usize(0), 25)
    delete middle
}
// `values` redevient accessible après la destruction de la vue.
```

Un paramètre `borrow var` peut être réemprunté temporairement comme `borrow`
par un appel imbriqué, puis retrouver son accès mutable au retour. Une closure
locale peut capturer un alias partagé ou mutable ; elle prolonge alors son
emprunt jusqu'à sa destruction ou la fin de son bloc. La capture mutable permet
la mutation mais reste exclusive. Retourner cette closure, la placer dans un
champ, ou la transmettre à une fonction sans contrat synchrone est refusé. Les
combinateurs synchrones connus de `Array`, `Option`, `Result` et
`Iterator.fold` acceptent une closure littérale empruntée car ils l'exécutent et
la détruisent avant leur retour.

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
sans dupliquer une ressource. Les API qui produisent une copie, par exemple
`Array.get` et `iterator()`, utilisent cette contrainte. Depuis 0.6.0, les
collections principales acceptent aussi les types propriétaires : leurs
opérations d'insertion et d'extraction transfèrent alors les valeurs avec
`move`, tandis que `clear` et les destructeurs détruisent les éléments
restants. Le [contrat de propriété des
conteneurs](design/container-ownership.md) détaille cette séparation.

### Dérivations explicites

Le lot 0.6.3, publié avec Janus 0.7.4, réserve la clause `derives` pour demander
les capacités structurelles `Copy`, `Equality`, `Hashing` et `Debug` :

```janus
struct Point(val x : int, val y : int)
derives Copy, Equality, Hashing, Debug {}
```

La demande est toujours explicite ; les capacités inconnues ou répétées sont
rejetées. La clause est également disponible sur les enums et, sauf pour
`Copy`, sur les classes. `Equality` active `==` et `!=`, tandis que
`debug(value)` écrit une représentation diagnostique déterministe distincte
de `print`.

Pour utiliser un type qui dérive `Hashing` comme clé, `std.hashing` fournit
`DerivedHashing[T]` :

```janus
import std.hashing
import std.hashset

val hashing : DerivedHashing[Point] = new DerivedHashing[Point]()
defer delete hashing
val points : HashSet[Point, DerivedHashing[Point]] =
new HashSet[Point, DerivedHashing[Point]](usize(8), hashing)
defer delete points
```

Le [design des dérivations structurelles](https://github.com/cyril103/janus/blob/main/docs/design/derivations.md)
définit l'éligibilité champ par champ, les génériques, la visibilité, les
diagnostics et les interdictions liées aux valeurs propriétaires.

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

### Littéraux de tableaux

La syntaxe `[e1, e2]` construit directement un `Array[T]`. Pour un littéral
non vide, `T` vient du contexte attendu (`val bytes : Array[ubyte] = [...]`)
ou, sans annotation, du type homogène des éléments. Chaque élément est vérifié
et converti contextuellement vers `T`; un littéral vide exige toujours une
annotation explicite, par exemple `val empty : Array[int] = []`. Les littéraux
imbriqués suivent les mêmes règles à chaque niveau.

```janus
val bytes : Array[ubyte] = [0xF0, 0x90, 0x90, 0x90, 0xF0]
defer delete bytes
val names = ["chip", "eight"]
defer delete names
val empty : Array[int] = []
defer delete empty
```

Les expressions sont évaluées exactement une fois, de gauche à droite. Le
littéral possède le tableau produit et transfère ses éléments selon les mêmes
règles que `Array.push`; une valeur non `Copy` déjà nommée doit donc être
écrite avec `move`. La destruction du tableau détruit les éléments construits
et libère son stockage; si une panique interrompt la construction, les éléments
déjà insérés suivent le mécanisme normal de nettoyage partiel de `Array`.

Les constantes globales n'acceptent pas les littéraux de tableaux : `Array[T]`
utilise un stockage dynamique propriétaire et le compilateur n'a pas de
représentation constante équivalente. Le diagnostic `JANA0023` demande de
construire le tableau à l'exécution. Les `val` et `var` globaux ordinaires
restent des initialisations d'exécution.

Les opérations directes de tableau qui retournent un élément par copie restent
réservées aux éléments `Copy`. `withValue` et `foreach` observent en revanche
une valeur propriétaire dans une lambda littérale bornée : son paramètre ne
peut être ni déplacé, ni détruit, ni transmis à un paramètre propriétaire.

Un tableau propriétaire reçoit et rend ses éléments par transfert explicite :

```janus
val resources : Array[Resource] = new Array[Resource](usize(1))
defer delete resources

val resource : Resource = new Resource()
resources.push(move resource)
val recovered : Resource = resources.remove(usize(0))
defer delete recovered
```

`set` détruit l'élément remplacé ; `replace` le retourne à l'appelant.

`HashSet`, `HashMap`, `ArrayBuilder`, `SetBuilder` et `MapBuilder` acceptent
également les éléments propriétaires. `add`, `put` et `Builder.add` consomment
ces éléments avec `move`. Un doublon de `HashSet` détruit la valeur entrante ;
`HashMap.put` détruit l'ancienne clé équivalente et retourne l'ancienne valeur.
`remove` détruit la clé stockée et transfère la valeur retirée.

Les paramètres de `Hashing.hash` et `Hashing.equals` sont des observations :
une implémentation ne doit ni les déplacer, ni les détruire, ni les retourner.
Le hash et l'égalité doivent rester stables tant que la clé appartient à la
collection. `HashMap.getOption` et les itérateurs observants `iterator()`,
`entries()`, `keys()` et `values()` restent réservés aux types retournés qui
satisfont `Copy`.

Les deux collections utilisent le même sondage linéaire et le même seuil de
charge de 75 %. Une insertion inspecte d'abord la table : un doublon de set ou
un remplacement de map ne redimensionne donc jamais la table. Lorsque le seuil
est franchi uniquement à cause des tombstones, la table est compactée à
capacité constante ; elle double seulement si les entrées vivantes imposent la
croissance. Les invariants, la complexité et les transferts propriétaires sont
détaillés dans le [design des collections hachées](design/hash-collections.md).

### Observer ou consommer un parcours

`iterator()` observe le conteneur et produit des copies ; il est donc
disponible uniquement quand les éléments produits satisfont `Copy`.
`intoIterator()` consomme un `Array` ou un `HashSet` et transfère chaque
élément. Pour une table, `intoEntries()` transfère des `MapEntry[K, V]` ;
`intoKey()` ou `intoValue()` permet ensuite de conserver une moitié en
détruisant l'autre.

```janus
val resources : Array[Resource] = new Array[Resource](usize(2))
resources.push(move first)
resources.push(move second)

for resource in resources.intoIterator() {
    defer delete resource
    resource.inspect()
}
```

Un `for` sur des valeurs propriétaires exige ce parcours consommant explicite :
le compilateur ne transforme jamais implicitement une observation en copie
propriétaire. Les adaptateurs `map`, `filter`, `flatMap`, `take`, `zip`,
`enumerate`, `fold` et `collectWith` transfèrent également leurs éléments.
La lambda de `filter` observe son paramètre pendant l'appel ; `map`, `flatMap`
et `fold` le consomment. Un élément refusé par `filter` est détruit
immédiatement.

La destruction de l'itérateur détruit sa source et tous les éléments qui n'ont
pas été produits. Cette garantie s'applique à la fin normale et après
`break`, `continue`, `return`, `?` ou une panique. Après l'appel à
`intoIterator()` ou `intoEntries()`, le conteneur original est consommé et
inutilisable, même si aucun élément n'est demandé.

Le parcours consommant d'un `Array` conserve l'ordre et avance par index :
chaque avancée coûte `O(1)` et un parcours complet `O(n)`. Les parcours
consommateurs de `HashSet` et `HashMap` visitent chaque
emplacement de la table au plus une fois, soit `O(capacity)` pour un parcours
complet. Dans tous les cas, la fin ou la destruction de l'itérateur libère le
stockage source ; `take(n)` détruit aussi la partie non visitée de sa source.

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

Le module `std.math` expose d’abord le noyau scalaire portable de la
bibliothèque mathématique C. Les fonctions sans suffixe prennent et retournent
un `double`; leur variante suffixée par `f` travaille exclusivement en
`float`. Janus ne réalise pas de conversion implicite entre ces deux familles.

- constantes : `PI`, `E`, `TAU` et leurs variantes `PIF`, `EF`, `TAUF` ;
- arrondis : `ceil`, `floor`, `trunc`, `round` ;
- puissances et logarithmes : `sqrt`, `cbrt`, `pow`, `hypot`, `exp`, `exp2`,
  `expm1`, `log`, `log2`, `log10`, `log1p` ;
- trigonométrie circulaire et hyperbolique, avec fonctions inverses ;
- restes, bornes et interpolation : `fmin`, `fmax`, `fdim`, `fmod`,
  `remainder`, `clamp`, `lerp` ;
- classification IEEE : `isfinite`, `isinf`, `isnan`, `isnormal`, `signbit`,
  ainsi que `copysign` et `nextafter` ;
- fonctions spéciales C99 : `erf`, `erfc`, `tgamma`, `lgamma`.

Les erreurs de domaine et de plage suivent la plateforme C : elles produisent
notamment des NaN ou des infinis. `std.math` ne publie pas `errno` et ne promet
pas une représentation textuelle identique des derniers bits entre toutes les
libc. Les prédicats de classification permettent de traiter ces résultats sans
les convertir en texte.

```janus
import std.math

val diagonal : double = hypot(3.0, 4.0)
val angle : double = atan2(1.0, 1.0)
val bounded : double = clamp(angle, 0.0, PI)
```

Le module conserve en parallèle ses helpers entiers non signés :

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

## Temps

`std.time` fournit une horloge monotone indépendante du module graphique :

```janus
import std.time

val start : Instant = monotonicNow()
// travail...
val elapsed : Duration = start.elapsed()
println(elapsed.milliseconds())
```

Une `Duration` stocke un nombre non signé de nanosecondes. Les fonctions
`nanoseconds`, `microseconds`, `milliseconds` et `seconds` construisent une
durée ; les trois dernières provoquent un `panic` avec `duration overflow` si
la conversion ne tient pas dans `usize`. Les méthodes de lecture tronquent vers
zéro pour les microsecondes et millisecondes entières. `Duration.seconds()`
retourne une valeur flottante.

`Instant.durationSince(earlier)` provoque un `panic` si le receveur précède
`earlier`. Les valeurs brutes d'un `Instant` ne sont volontairement pas
exposées : elles n'ont de sens que dans le processus courant. L'horloge ne
recule pas, mais deux lectures successives peuvent être égales.

Le temps civil est séparé dans `std.wall_time`. `wallNow()` retourne un
`WallTime` dont les méthodes `unixNanoseconds`, `unixMilliseconds` et
`unixSeconds` mesurent le temps depuis l'époque Unix. Cette horloge peut avancer
ou reculer à la suite d'une correction du système et ne doit pas servir à
mesurer une durée.

## Nombres pseudo-aléatoires

`std.random.Random` implémente SplitMix64. Une seed explicite produit toujours
la même suite sur toutes les plateformes :

```janus
import std.random

val random : Random = new Random(usize(42))
defer delete random

val any : usize = random.nextUSize()
val die : usize = random.nextBounded(usize(6)) + usize(1)
```

`nextBounded(upperExclusive)` retourne une valeur dans
`[0, upperExclusive)` et utilise un rejet pour éviter le biais de modulo. Une
borne nulle provoque un `panic` avec
`random upper bound must be positive`.

Pour les applications qui ne requièrent pas une suite reproductible,
`randomUSize()` et `randomBounded()` utilisent une source globale initialisée
automatiquement. `automaticRandom()` crée une source indépendante avec une seed
automatique. Ces sources ne sont pas cryptographiques ; une seed explicite doit
être conservée pour les tests, simulations reproductibles et sauvegardes.

## Modules

### Commentaires de documentation

`///` documente la déclaration publique qui suit. Plusieurs lignes
consécutives forment un seul texte. Placé avant `module`, le commentaire décrit
le module ; il peut aussi documenter les types, variantes, traits, fonctions,
globales, champs et méthodes.

```janus
/// Outils de présentation pour [[Message]].
module presentation

/// Un message affichable.
struct Message(val text : string) {
    /// Retourne le texte du message.
    def view() : string { return text }
}
```

Les liens `[[Message]]` ou `[[presentation.Message]]` sont résolus par
`janus doc`. Les liens inconnus ou ambigus sont signalés. Les commentaires
ordinaires `//` ne sont pas inclus dans la documentation. Le format complet et
la génération hors ligne sont définis dans le
[contrat de documentation d’API](api-documentation.md).

Les fonctionnalités de la bibliothèque standard sont importées explicitement :

```janus
import std.array
import std.math
import std.option
import std.random
import std.result
import std.time
import std.wall_time
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

Les paramètres pointeur d’une fonction externe peuvent déclarer leur contrat
de propriété :

```janus
extern def inspect(borrow data : Ptr[byte]) : Unit
extern def release(consume data : Ptr[byte]) : Unit
extern def lastError() : borrow Ptr[byte]
extern def allocateBuffer() : owned Ptr[byte]
```

`borrow` déclare que le code natif ne conserve ni ne libère le pointeur :
l’appelant garde donc la propriété et peut le réutiliser après l’appel.
`consume` transfère la propriété au code natif et
invalide immédiatement la valeur locale côté Janus. Sans l’un de ces
qualificateurs, le compilateur émet `JANA0020`, car le contrat de propriété ne
peut pas être vérifié. `consume` reste limité aux paramètres `Ptr[T]` des
déclarations `extern def`; `borrow` est également disponible sur les fonctions
Janus ordinaires avec la sémantique d'emprunt immuable décrite plus haut.

Un retour pointeur externe utilise `borrow` lorsque le stockage reste détenu
par le code natif, et `owned` lorsque Janus doit le libérer ou le transférer.
Une valeur retournée `borrow` ne peut pas être passée à `free`, `delete` ou à
un paramètre `consume`. Sans contrat sur un retour `Ptr[T]`, le compilateur
émet `JANA0022`. Les qualificateurs de retour sont limités aux déclarations
`extern def` et aux types `Ptr[T]`.

Un alias local peut être déclaré sans transfert de propriété avec
`borrow val view = owner`. Il est immutable et ne peut être ni libéré ni
déplacé. Une classe peut de même conserver une référence observante
dans un champ de constructeur immutable, par exemple
`private borrow val source : Collection`; ce champ n'est pas détruit avec la
classe et sa source doit donc vivre plus longtemps. Les structs ne peuvent pas
contenir de champ emprunté.

Le code de conteneur bas niveau dispose de contrats explicites supplémentaires :
la méthode Ptr.initialize écrit un emplacement neuf, et Ptr.overwrite remplace un
emplacement dont l'ancienne propriété a déjà été extraite,
`reallocPreserving[T]` déplace le stockage brut sans nettoyer ses éléments,
`adoptReallocation[T]` valide le remplacement après contrôle du résultat et
`freeStorage` libère une zone dont tous les éléments ont déjà été déplacés ou
détruits. `owningCapture[T]` transfère enfin un propriétaire à une fermeture
qui doit effectivement le capturer. Ces opérations suppriment uniquement les
warnings correspondant au contrat affirmé; leur précondition reste à la charge
du code appelant.

Les conversions numériques explicites distinguent désormais quatre contrats :
`checkedCast[T]` refuse toute altération dans un `Result`,
`saturatingCast[T]` borne le résultat, `truncatingCast[T]` rend la perte
prévisible et `numericCast[T]` conserve l'échappatoire native sans contrôle.
Le suffixe `f` construit directement un littéral `float`, par exemple `0.5f`.
La [matrice exhaustive des conversions](numeric-conversions.md) spécifie les
bornes, signes, fractions, `NaN`, infinis, zéros signés et règles d'arrondi.

Ces opérations peuvent produire des adresses invalides, des fuites ou des
accès mémoire incorrects. Elles sont volontairement réservées au code qui doit
contrôler précisément sa représentation et sa mémoire.

### Erreurs système portables

Le module `std.system` fournit la frontière bas niveau commune aux fichiers,
flux et processus. Toutes les erreurs récupérables sont retournées dans un
`Result` :

```janus
import std.system

val opened : Result[SystemFile, SystemError] =
openSystemFile("notes.txt", SystemOpenMode.Read)
```

Un `SystemError` expose l’opération, une catégorie portable, le code natif et
le chemin ou contexte concerné. `SystemFile.close()` invalide immédiatement le
handle ; le destructeur ne ferme que les handles encore ouverts. Les chemins
sont du UTF-8 strict sans NUL embarqué. Consultez le
[contrat du runtime système](design/system-runtime.md) pour les catégories,
limites de taille et règles POSIX/Windows.

### Chemins et fichiers

`std.path` fournit des chemins UTF-8 propriétaires. La normalisation est
lexicale et respecte les séparateurs natifs ; elle ne consulte pas le système
de fichiers. `std.fs` lit des fichiers dans un `FileData` propriétaire, écrit
par remplacement atomique, crée et parcourt des répertoires et retourne des
métadonnées typées.

```janus
import std.fs

val written : Result[bool, SystemError] =
writeTextFileAtomic("notes.txt", "Janus")
val data : Result[FileData, SystemError] =
readFile("notes.txt")
```

Les itérateurs de répertoire et les buffers lus possèdent leurs ressources et
les libèrent à la destruction. Les métadonnées ne suivent pas le dernier lien
symbolique. Le [contrat chemins et fichiers](design/path-filesystem.md)
documente l’écriture atomique, les liens et les différences POSIX/Windows.

### Flux tamponnés

`std.io` sépare les octets du texte. `InputStream` expose les lectures
séquentielles, EOF et les lignes sous forme de `ByteBuffer`.
`OutputStream` absorbe les écritures partielles, fournit `flush` et ferme les
handles propriétaires exactement une fois.

```janus
import std.io

val input : Result[InputStream, SystemError] =
openInputStream("source.txt")
val output : Result[OutputStream, SystemError] =
openOutputStream("copie.txt", false)
```

`standardInput`, `standardOutput` et `standardError` créent des wrappers qui ne
ferment pas les handles du processus. Un buffer binaire doit appeler `isUtf8`
ou `asText` avant d’être traité comme du texte. Le
[contrat des flux](design/io-streams.md) précise les lectures partielles, les
fins de ligne, la durée de vie des vues et le flush selon le type de handle.

### Arguments, environnement et processus

`std.process` donne accès aux arguments complets du programme et aux variables
d’environnement :

```janus
import std.process

val count : usize = programArgumentCount()
val home : Result[Option[EnvironmentValue], SystemError] =
environmentVariable("HOME")
```

`runProcess` reçoit l’exécutable et chaque argument séparément. Il ne passe
jamais par un shell. L’appel attend l’enfant, expose son code de sortie et peut
capturer stdout et stderr comme octets. Une chaîne vide conserve le répertoire
de travail courant ; une autre valeur sélectionne celui de l’enfant. Le
[contrat des processus](design/process-runtime.md) documente l’Unicode, la
durée de vie des vues, les erreurs et le nettoyage.

Pour un protocole persistant, `spawnProcess` retourne un `ChildProcess` dont
`writeText` alimente stdin et dont `read` consomme stdout sans relancer
l’exécutable. `closeInput` envoie une fin de flux. Pour une fermeture de GUI
sans attente, `terminate` demande l’arrêt immédiatement et `tryWait` permet un
polling borné jusqu’au code de sortie ; le destructeur conserve son nettoyage
bloquant historique lorsqu’il reste seul responsable de l’enfant.
`currentWorkingDirectory` fournit parallèlement une vue propriétaire du
répertoire courant.

Des programmes complets sont disponibles dans [`examples`](../examples).

## Structures copiables et propriétaires

Une `struct` regroupe des données par valeur. Sa syntaxe reprend celle des
champs de constructeur d'une classe :

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

Lorsque tous ses champs sont copiables, l'affectation et le passage à une
fonction copient la valeur : `original` reste donc inchangé lorsque `copy` est
modifié. La dérivation explicite `Copy` permet en plus de satisfaire une
contrainte générique `T <: Copy`.

Une structure qui contient une ressource devient en revanche propriétaire :
son transfert emploie `move` et `delete` détruit récursivement son contenu. Une
structure ne déclare pas son propre destructeur ; la propriété découle de ses
champs. Ceux-ci sont déclarés entre parenthèses avec `val` ou `var`.

## Graphisme 2D

Le module expérimental `std.graphics` permet de créer une fenêtre, dessiner des
formes et du texte, et lire le clavier ou la souris. Son backend raylib 6 est
chargé dynamiquement. Consultez le [guide du graphisme](graphics.md) pour
l'installation et un premier programme.
