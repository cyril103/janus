# Guide du langage Janus

> **Statut des API 0.8.** La syntaxe et la bibliothèque restent pré-1.0.
> L'[inventaire de stabilité courant](stability-inventory-current.md) classe les
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
count += 1
```

Les affectations composées `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`,
`<<=` et `>>=` mettent à jour une cible mutable avec le contrat de l'opérateur
binaire correspondant. La cible est résolue une fois, puis l'expression droite
est évaluée exactement une fois. Elles n'ajoutent aucune conversion implicite,
ne produisent pas de valeur et conservent les contrôles de division, de reste et
de décalage de l'opérateur ordinaire.

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
    requests += 1
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
`^`, `|`, `&&`, `||`, `|>`. Le compte valide va de zéro à `largeur - 1` pour les
largeurs 8, 16, 32 et 64. Une constante hors borne est diagnostiquée ; une
valeur hors borne à l'exécution déclenche un `panic` avant le décalage. Une
valeur négative n'est pas un `usize` valide.

### Pipeline fonctionnel

La forme « value |> f(arguments) » transmet la valeur comme premier argument
et se désucre en « f(value, arguments) ». L'opérateur est associatif à gauche,
n'évalue chaque expression qu'une fois et n'insère jamais de `move` : une
valeur propriétaire s'écrit donc `move value |> consume`. `std.functional`
fournit `identity`, `constant`, `compose`, `andThen`, `flip` et `tap`.

```janus
import std.functional

val output : int = 20
    |> identity[int]()
    |> tap[int]((borrow value : int) => println(value))
```

Les parenthèses rendent explicite un postfixe appliqué au résultat complet :
`(result |> normalize)?` ou `(value |> normalize).validate()`. La
[spécification du pipeline](design/functional-pipeline.md) détaille la
précédence, l'ordre d'évaluation et la propriété des closures composées.

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
settings.requestCount += 1
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
    return move value
}
```

Les arguments de type omis sont inférés avant la génération de code. Le type
substitué d'un appel peut donc être utilisé directement par un appel englobant,
à autant de niveaux que nécessaire : `println(identity(identity(71)))` est
équivalent à la forme explicite `println(identity[int](identity[int](71)))` et
à une version qui lie chaque résultat intermédiaire. La même règle s'applique
aux méthodes génériques, aux closures et aux types génériques propriétaires.
Une inférence incomplète est refusée pendant l'analyse ; si le contrat entre
l'analyse et le backend est incohérent, la compilation s'arrête avec un
diagnostic backend structuré au lieu de produire une IR invalide ou de planter.

Lorsqu'une fonction ou une méthode ne fait que retourner une expression, son
corps peut s'écrire avec `=>`. Cette forme est exactement équivalente à un
bloc contenant `return` : elle conserve le typage, la propriété, les emprunts
et les règles `tailrec`, sans créer de retour implicite dans les blocs.

```janus
def square(value : int) : int => value * value
const def twice(value : int) : int => value * 2

class Counter(val value : int) {
    borrow def current() : int => value
}
```

Le type de retour reste obligatoire. `=> move value` est requis dans les mêmes
cas que `return move value`, et un retour `borrow` doit provenir de la même
source valide que dans la forme bloc. Les méthodes déclarées dans un trait
restent des signatures abstraites sans corps ; la classe qui étend le trait
fournit leur implémentation. Pour `Unit`, l'expression doit elle-même
avoir le type `Unit`. Une déclaration ou plusieurs instructions exigent
toujours `{ ... }`. Les flèches des closures et des branches de `match`
conservent leur sens propre, déterminé par leur contexte.

La provenance d'un retour emprunté fait partie du contrat analysé de la
fonction ou de la méthode. Elle désigne soit `this`, soit un paramètre emprunté
précis : dans `borrow def select(borrow other : T) : borrow T`, un résultat
issu de `other` verrouille `other`, pas le receveur. Cette identité est
conservée à travers les méthodes et fonctions relais, les spécialisations
génériques et les projections de membre. Tous les chemins de retour doivent
désigner la même source ; des branches qui retournent tantôt `this`, tantôt un
paramètre sont refusées avec `JANA0026`. Invalider la source effective pendant
que le résultat vit produit `JANA0025`, dont le diagnostic nomme la source et
l'emprunteur.

Les fonctions sont des valeurs de première classe. Une closure peut capturer
les valeurs qui l'entourent :

```janus
val threshold : int = 10
val isLarge : (int) => bool =
    (value : int) => value > threshold
```

`pure def` ajoute un contrat runtime vérifié transitivement : pas de globale
mutable, d'I/O, d'heure, d'aléatoire, de FFI ou de méthode non marquée pure, ni
de mutation visible par l'appelant. Les allocations locales et `panic` sont
autorisés. Détruire une valeur locale reste autorisé seulement si son
destructeur et les cleanups imbriqués respectent eux aussi ce contrat, y compris
via `defer`, les agrégats et les spécialisations génériques. `const def`
implique ce noyau mais conserve les restrictions plus fortes de l'évaluation à
la compilation. Un callback appelable depuis ce contexte s'écrit
`pure (T) => U`. Le contrat complet, y compris les règles des méthodes et de la
FFI, est défini dans
[Contrat `pure def`](design/pure-functions.md).

Les effets d'emprunt font partie du type d'une fonction. Ils s'écrivent sur
les paramètres du type et de la closure :

```janus
val inspect : (borrow Document) => int =
    (borrow document : Document) => document.revision()
val edit : (borrow var Document) => Unit =
    (borrow var document : Document) => document.touch()
```

Quand un contexte fournit déjà un type de fonction complet et unique, les
annotations des paramètres de lambda peuvent être omises : `value => ...`,
`(left, right) => ...` et `() => ...`. Les contrats explicites restent visibles
avec `(borrow value) => ...` et `(borrow var value) => ...`. Cette omission est
réservée aux lambdas : les paramètres d'une déclaration `def` restent annotés.

```janus
def apply(value : int, operation : (int) => int) : int {
    return operation(value)
}

def main() : int {
    val increment : (int) => int = value => value + 1
    val answer : int = apply(41, value => value + 1)
    delete increment
    return answer - 42
}
```

Le type attendu est fixé avant l'analyse du corps. Un paramètre nu sans
contexte, une arité différente, ou un `borrow`/`borrow var` explicite
incompatible est donc rejeté avec une demande d'annotation ; le corps ne sert
jamais à choisir une surcharge ou à reconstruire la signature.

Ces callbacks ne prennent pas possession de leur argument. `borrow var`
transmet l'emplacement mutable lui-même : une modification effectuée par le
callback reste visible après l'appel. Deux types de fonction qui diffèrent par
ces effets ne sont pas interchangeables.

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

Le contrat couvre les retours `Unit`, qui sont émis avec `musttail` comme les
retours scalaires et références de classe. Les retours de structs, enums,
chaînes et types génériques dont l'ABI finale ne peut pas être prouvée sont
actuellement refusés avec `JANA0032`. Le même diagnostic est produit si une
valeur propriétaire locale reste vivante sur une arête récursive : elle doit
être détruite ou transférée avant l'appel. Le backend vérifie en dernier recours
que chaque arête du cycle acceptée par l'analyseur porte réellement `musttail`.

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

## Enums, `match`, `Option`, `Result` et `Validated`

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
est défini par le protocole `Try`, dont les types associés `Output` et
`Residual` décrivent respectivement la valeur poursuivie et la valeur propagée.
Le type de retour doit implémenter `Try` avec le même `Residual`.

Un enum utilisateur peut adopter le protocole sans noms de variantes imposés :

```janus
import std.option

enum Attempt[T, E] extends Try {
    type Output = T
    type Residual = E
    Continue(T), Stop(E)
}
```

La première version du protocole exige deux variantes : l'une transporte
exactement `Output`, l'autre exactement `Residual` (ou aucune charge utile pour
`Residual = Unit`).

Dans une spécialisation générique où le résidu devient `Unit`, le payload est
effacé de la représentation. Son expression de construction est néanmoins
évaluée une fois pour ses effets, et `?` propage seulement la variante vide sans
tenter de lire une valeur `Unit`. Les résidus non identiques restent refusés avant
la génération de code.

Le module `std.option` fournit un noyau de combinateurs génériques :

| Fonction | Effet |
| --- | --- |
| `isSome[T](value)` | observe si la variante est `Some` |
| `isNone[T](value)` | observe si la variante est `None` |
| `mapBorrowed[T, U](value, transform)` | transforme un emprunt du contenu sans consommer l'option |
| `map[T, U](value, transform)` | transforme le contenu de `Some` |
| `andThen[T, U](value, transform)` | chaîne une fonction qui retourne une `Option[U]` |
| `flatten[T](value)` | supprime un niveau `Option[Option[T]]` |
| `filter[T](value, predicate)` | conserve un contenu satisfaisant un prédicat emprunté |
| `fold[T, U](value, fallback, transform)` | réduit les deux variantes vers `U` |
| `contains[T](value, expected)` | compare par égalité dérivée sans transfert |
| `zip[T, U](left, right)` | regroupe deux contenus dans `OptionPair[T, U]` |
| `map2[T, U, V](left, right, combine)` | combine deux contenus sans paire intermédiaire |
| `inspect[T](value, action)` | observe `Some` puis retransmet l'option |
| `orElse[T](value, fallback)` | conserve `value` si présente, sinon retourne `fallback` |
| `unwrapOr[T](value, fallback)` | extrait la valeur présente, sinon retourne `fallback` |
| `unwrapOrElse[T](value, fallback)` | calcule paresseusement une valeur pour `None` |
| `orElseWith[T](value, fallback)` | calcule paresseusement une option pour `None` |

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

`mapBorrowed` étend cette observation aux contenus non `Copy`. Sa closure
reçoit `borrow T`, produit une valeur indépendante et ne peut pas s'échapper de
l'appel grâce à `scoped`; l'option reste ensuite utilisable et propriétaire de
son contenu.

Les opérations autres que les observations sont consommantes. Une variable `Option[Resource]`
doit leur être passée avec `move`; le résultat possède alors la valeur
transférée. `map` et `andThen` n'appellent leur closure que pour `Some`.
`orElse` détruit la valeur de repli lorsque la première option est présente ;
`unwrapOr` détruit de même le repli inutilisé. Pour `None`, le repli est
transféré au résultat. Une closure reçue par `map` ou `andThen` est détruite
après l'appel, y compris lorsque l'option est vide.

`zip` et `map2` inspectent la gauche avant la droite. Les fallbacks de
`unwrapOrElse` et `orElseWith` ne sont jamais appelés pour `Some`. La
[matrice complète](design/option-result-combinators.md) documente ownership,
paresse, coûts, lois et ordre de destruction.

Les fonctions sont aussi accessibles avec leur nom qualifié, par exemple
`std.option.map`, ce qui évite toute ambiguïté avec les combinateurs similaires
de `std.result`.

Le module `std.result` propose les opérations correspondantes sur les deux
variantes :

| Fonction | Effet |
| --- | --- |
| `isOk[T, E](value)` | observe si la variante est `Ok` |
| `isError[T, E](value)` | observe si la variante est `Error` |
| `mapBorrowed[T, U, E](value, transform)` | transforme un emprunt de la valeur `Ok` |
| `mapErrorBorrowed[T, E, F](value, transform)` | transforme un emprunt de l'erreur |
| `map[T, U, E](value, transform)` | transforme la valeur de `Ok` |
| `mapError[T, E, F](value, transform)` | transforme l'erreur |
| `andThen[T, U, E](value, transform)` | chaîne une opération qui peut échouer |
| `flatten[T, E](value)` | supprime un niveau de résultat imbriqué homogène |
| `fold[T, E, U](value, onOk, onError)` | réduit la branche active vers `U` |
| `zip[T, U, E](left, right)` | regroupe deux succès dans `ResultPair[T, U]` |
| `map2[T, U, E, V](left, right, combine)` | combine deux succès ou conserve la première erreur |
| `inspect[T, E](value, action)` | observe `Ok` puis retransmet le résultat |
| `inspectError[T, E](value, action)` | observe `Error` puis retransmet le résultat |
| `orElse[T, E, F](value, transform)` | récupère une erreur avec une autre opération |
| `unwrapOr[T, E](value, fallback)` | extrait la valeur ou retourne le repli |
| `unwrapOrElse[T, E](value, fallback)` | calcule le repli uniquement pour `Error` |
| `toOption[T, E](value)` | conserve `Ok` comme `Some` et élimine l'erreur |
| `fromOption[T, E](value, error)` | convertit `Some` en `Ok` et `None` en `Error` |
| `toResult[T, E](value, error)` | forme orientée `Option` équivalente à `fromOption` |
| `transpose[T, E](value)` | distribue `Result[Option[T], E]` vers `Option[Result[T, E]]` |

`isOk`, `isError`, `mapBorrowed` et `mapErrorBorrowed` observent une variable
locale propriétaire sans la consommer. Les autres opérations sont consommantes : la closure n'est appelée
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

Une lambda à corps bloc peut également employer `?` lorsque son type de
fonction attendu fixe un retour `Option` ou `Result`. La propagation quitte la
lambda elle-même, jamais la fonction qui la contient :

```janus
val normalize : (Result[int, string]) => Result[int, string] = input => {
    val value : int = input?
    return Result.Ok[int, string](value * 2)
}
```

Sans type de fonction contextuel, annotez la liaison de la lambda afin que le
compilateur connaisse le type propagé avant d'analyser son bloc. Les mêmes
règles de compatibilité d'erreur, de `move` et de nettoyage s'appliquent aux
lambdas et aux fonctions ordinaires.

Pour un `Result` propriétaire, `?` consomme l'agrégat et exige un transfert
explicite : `val resource : Resource = (move pending)?`. `pending` devient
ensuite inutilisable. Une propagation d'erreur exécute les nettoyages actifs
avant de transférer l'erreur à l'appelant. Les conversions `toOption` et
`fromOption` transfèrent uniquement la variante active.

Le module `std.validated` complète `Result` pour les contrôles indépendants.
`Validated[T, E]` contient `Valid(T)` ou
`Invalid(ValidatedErrors[E])`. Le constructeur de `ValidatedErrors` est
`internal` : les applications utilisent `invalid(error)`, ou
`invalidFromArray(errors)` qui renvoie `None` pour un tableau vide. Ses
opérations `map2`, `map3`, `zip` et `collectValidated` accumulent toutes les
erreurs de gauche à droite et n'appellent leur constructeur que si toutes les
entrées sont valides. Elles consomment leurs entrées et transfèrent ou
détruisent chaque valeur propriétaire exactement une fois.

```janus
val checked : Validated[int, string] =
    std.validated.map2[int, int, string, int](
        validateWidth(width),
        validateHeight(height),
        (checkedWidth : int, checkedHeight : int) =>
            checkedWidth * checkedHeight
    )
```

`Validated` n'a pas d'`andThen` : une validation dépendante ne peut pas
accumuler les erreurs d'une étape qui n'a pas été exécutée. Utilisez
`andThen` de `std.result` pour ce flux court-circuité. `fromResult` ne perd aucune
information ; `toResult` conserve la première erreur et détruit les suivantes,
sans cas vide constructible par l'API publique. Après une déstructuration
`Invalid(errors)`, la méthode `ValidatedErrors.intoArray` récupère le tableau
non vide.
Le [contrat complet](design/validated.md) précise l'ordre, l'allocation et les
cas formulaire, configuration et itérateur.

Le module `std.error` fournit deux erreurs communes aux couches basses de la
stdlib. `AccessError.Empty` signale les retraits impossibles et
`AccessError.OutOfBounds` les indices invalides; `CapacityError` distingue un
dépassement de capacité d'un échec d'allocation. Les variantes `tryPop`,
`tryDequeue` et `tryReserve` retournent ces erreurs dans `Result`, tandis que les
anciennes formes paniquantes ou en `Option` restent disponibles. La réservation
récupérable est uniforme sur `Array`, `ByteBuffer`, `Deque`, `Queue`, `HashMap`,
`HashSet` et `PriorityQueue`; les tables hachées interprètent son argument comme
un nombre d’insertions supplémentaires, conformément à leur méthode `reserve`.

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

Une implémentation de trait ne peut pas réduire la visibilité effective de son
contrat. Si le trait et la classe sont publics, la méthode d'implémentation doit
donc être publique : une méthode `internal` est refusée, car un importateur
pourrait sinon l'appeler indirectement depuis une fonction générique contrainte
par le trait. Une implémentation `internal` reste permise lorsque le trait ou le
type est `private`, puisque le contrat effectif ne sort alors pas du module.
Une méthode `private`, limitée à sa classe, ne peut jamais implémenter un trait.

Un constructeur peut être réservé à son espace de noms racine avec `internal`
placé avant ses paramètres. Le type reste public, mais seul un module partageant
la même racine qualifiée peut l'instancier. La stdlib utilise cette forme pour
exposer des wrappers natifs sans permettre aux applications de fabriquer des
pointeurs ou des handles invalides :

```janus
module std.native

class NativeHandle internal(private val handle : isize) {}
```

Les modules `std.*` peuvent construire `NativeHandle`; un module applicatif doit
passer par une fabrique publique qui garantit les invariants du wrapper.

Une méthode de classe ou de trait peut restreindre un paramètre générique
uniquement pour l'opération qui en a besoin :

```janus
borrow def peekCopy() : T where T <: Copy { ... }
```

Un trait peut aussi déclarer un type associé, défini par chaque classe qui
l'implémente. Il est utilisé directement dans les méthodes du trait et projeté
avec le type `Item` associé à `T` dans une abstraction générique :

```janus
trait Producer {
    type Item
    def next() : Item
}

class IntProducer() extends Producer {
    type Item = int
    def next() : int { return 1 }
}

def produce[P <: Producer](producer : P) : P.Item {
    return producer.next()
}

// La projection est normalisée de la même façon avec ou sans liaison locale.
val producer : IntProducer = new IntProducer()
println(produce[IntProducer](producer))
delete producer
```

La classe doit définir chaque type associé attendu. Les définitions manquantes,
dupliquées, étrangères au trait ou cycliques sont rejetées. Une projection
spécialisée est normalisée vers son type concret avant la sélection ABI, y
compris lorsqu'un appel est directement imbriqué dans un argument, un
constructeur, un retour ou un callback. Une projection qui reste ambiguë ou non
contrainte est diagnostiquée pendant l'analyse et n'atteint pas le backend. Le contrôle couvre signatures, champs, annotations
locales, lambdas et arguments génériques, même dans `Ptr[P.Item]`. Un trait unique doit fournir `Item`; les bornes génériques ou
`where`, imports et alias sont équivalents. Les types associés génériques, leurs bornes et les égalités de projection dans
`where` ne font pas partie de cette première version. Voir [la RFC des types associés](design/associated-types.md).

Une méthode qui implémente un trait doit reprendre exactement son contrat après
normalisation des paramètres génériques, des types associés et des noms
importés. Cette identité couvre les types et l'ownership des paramètres et du
retour, `scoped`, le receveur (`borrow` ou `consume`), la pureté et les
contraintes génériques propres à la méthode. L'ordre des contraintes `where`
n'est pas significatif, mais en ajouter, en retirer ou en remplacer une rend la
signature incompatible. Aucune variance ni aucun renforcement implicite n'est
actuellement autorisé. Le diagnostic précise le champ contractuel qui diffère.

La classe reste utilisable avec un `T` propriétaire, tandis que l'appel de
`peekCopy` est disponible seulement lorsque `T` satisfait `Copy`. Plusieurs
contraintes se combinent avec `&` et plusieurs paramètres avec `,`.

## Gestion manuelle de la mémoire

Les objets créés avec `new` sont placés sur le tas. Janus n'a pas de
ramasse-miettes : le programmeur doit les libérer.

```janus
val point : Point = new Point(1, 2)
delete point
```

`defer` programme un nettoyage à la sortie de la portée, y compris lors d'un
`return`, `break`, `continue` ou d'une `panic` propagée depuis un appel :

```janus
val point : Point = new Point(1, 2)
defer delete point
```

Les destructeurs exécutent le nettoyage propre à une classe avant la
libération de sa mémoire. Lors d'une panique, les nettoyages de toutes les
fonctions Janus traversées s'exécutent exactement une fois en ordre LIFO. Une
seconde panique depuis un destructeur n'interrompt pas les nettoyages locaux
restants. Le contrat ABI et les limites FFI/globales sont détaillés dans
[le design du déroulement des paniques](design/panic-unwinding.md).

Cet ordre LIFO fait partie de l'analyse des emprunts. Une action différée qui
lit un emprunt doit être enregistrée après le nettoyage de sa source, afin de
s'exécuter avant lui :

```janus
val resource : Resource = new Resource(424242)
defer delete resource
borrow val view : Resource = resource
defer println(view.value)
```

Inverser les deux `defer` est refusé avec `JANA0025` : `delete resource`
s'exécuterait avant `println(view.value)`. Le même contrôle s'applique aux mutations,
aux moves et consommations, aux closures qui capturent un emprunt, aux portées
imbriquées et à toute sortie (`return`, `break`, `continue`, `?` ou panique).
L'analyse reste conservative pour les callbacks dont les effets ou captures ne
peuvent pas être établis localement.

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
être transmis à un paramètre propriétaire. Une fonction peut en revanche
retourner un emprunt explicitement avec `: borrow T` :

```janus
def identity(borrow document : Document) : borrow Document {
    return document
}

borrow val view : Document = identity(document)
```

Une fonction libre ainsi annotée possède exactement un paramètre emprunté,
qui devient la source de durée de vie. Une méthode doit être `borrow def` et
son résultat provient de `this`. Le résultat doit être lié par `borrow val` ;
le propriétaire source reste gelé pendant la portée de cette liaison. Les
durées de vie nommées ne sont pas encore prises en charge. Les régions restent
lexicales : placez un emprunt local dans un bloc plus court pour réutiliser
ensuite le propriétaire.
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

Une méthode mutante — `def` sur un type valeur ou `borrow var def` dans une
extension — exige cette capacité sur son receveur au site d'appel. Une liaison
propriétaire `var` et un alias `borrow var` conviennent. Une liaison `val` ou
un alias `borrow val` sont refusés avec `JANA0027`; déclarez le propriétaire
avec `var` ou demandez explicitement un accès `borrow var`. La règle est
propagée à travers les champs et les spécialisations génériques. Les conflits
avec un autre emprunt restent signalés par `JANA0024`/`JANA0025` selon que
l'accès crée un emprunt concurrent ou invalide un emprunt déjà vivant.

Une fonction peut transférer cet accès exclusif sans transférer la propriété
avec un retour `: borrow var T`. Une fonction libre exige alors exactement un
paramètre `borrow var` comme source. Une méthode qui retourne un tel emprunt est
mutante (elle ne porte pas `borrow def`) et lie la durée de vie à son récepteur :

```janus
def identity(borrow var cursor : Cursor) : borrow var Cursor {
    return cursor
}

borrow var editable : Cursor = identity(cursor)
editable.position = 4
```

Le résultat mutable peut être lié par `borrow var` pour écrire ou être dégradé
en `borrow val` pour une simple observation. Un retour partagé ne peut jamais
être renforcé en emprunt mutable.

Un seul emprunt mutable peut viser une valeur à la fois. Aucun emprunt partagé,
accès direct au propriétaire, déplacement ou destruction ne peut lui être
concurrent. Les modifications sont visibles par le propriétaire parce que
l'alias utilise le même stockage. Une place peut être un identifiant local ou
une suite de champs nommés statiquement :

```janus
class Pair(var left : int, var right : int) {}

var pair : Pair = new Pair(1, 2)
borrow var left : int = pair.left
borrow var right : int = pair.right
left = 3
right = 4
```

Deux chemins de champs frères, comme `pair.left` et `pair.right`, sont
disjoints et peuvent donc être empruntés simultanément. En revanche, la racine
`pair`, un chemin ancêtre et le même champ se chevauchent : tout emprunt ou
accès incompatible est refusé avec `JANA0024` ou `JANA0025`. Cette provenance
complète est conservée à travers les projections imbriquées, les types
génériques, les alias et les appels qui retournent un emprunt. Les indices
dynamiques et les unions ne sont pas considérés comme des places disjointes ;
ils restent traités conservativement par leurs API d'emprunt explicites.

Un emprunt actif interdit aussi les invalidations indirectes : appel d'une
méthode mutante ou consommatrice sur le propriétaire, passage à un paramètre
`consume`, et `realloc` ou `reallocPreserving` d'un pointeur emprunté. Un objet
avec un champ constructeur `borrow val` ou `borrow var` maintient respectivement
un emprunt partagé ou mutable jusqu'à sa
destruction ou la fin de son bloc. Cet objet ne peut pas être copié dans un
champ ou une globale, ni sortir de la fonction par `return`. Ces règles sont
conservatrices et lexicales ; un bloc court permet de terminer explicitement
la durée de vie observante.

Le module `std.slice` fournit des vues contiguës sur `Array[T]`, y compris pour
les éléments propriétaires non `Copy`. `Slice[T]` permet la lecture partagée ;
`MutableSlice[T]` réserve un accès exclusif et ajoute `set`. `getBorrowed`
retourne une observation liée à la vue et `getMutable` un emprunt exclusif sur
un élément. `Array.getMutable` fournit le même accès directement. Seules les opérations qui produisent
une copie (`get`, `getOption` et `iterator`) demandent `T <: Copy` au niveau de
la méthode. Les indices sont relatifs à la vue et contrôlés à l'exécution. La
plage est validée à la construction et le stockage n'est jamais copié :

```janus
import std.array
import std.slice

val values : Array[int] = new Array(3)
values.push(10)
values.push(20)
values.push(30)
if true {
    val middle : MutableSlice[int] =
    new MutableSlice(values, 1, 2)
    borrow var item : int = middle.getMutable(0)
    item = 25
    delete middle
}
// `values` redevient accessible après la destruction de la vue.
```

Un paramètre `borrow var` peut être réemprunté temporairement comme `borrow`
par un appel imbriqué, puis retrouver son accès mutable au retour. Une closure
locale peut capturer un alias partagé ou mutable ; elle prolonge alors son
emprunt jusqu'à sa destruction ou la fin de son bloc. La capture mutable permet
la mutation mais reste exclusive. Retourner cette closure, la placer dans un
champ, ou la transmettre à une fonction sans contrat synchrone est refusé.
Les callbacks annotés par un type comme `(borrow T) => U` ou
`(borrow var T) => U` expriment directement ce contrat. `Array.withValue`,
`Array.withValues`, `Array.withMutable`, `HashSet.withValue` et
`HashMap.withValue` les utilisent pour observer ou modifier un élément sans le
copier. Les deux collections hachées retournent `false` lorsque la clé est
absente et appellent sinon une fois la closure avec la valeur canonique stockée.

Un paramètre fonctionnel préfixé par `scoped` garantit que la fonction appelée
ne conserve ni ne retourne la closure. Cette garantie autorise une closure à
capturer un emprunt tout en transférant sa propre propriété au callee, qui peut
donc la détruire après l'appel :

```janus
def invoke(scoped callback : () => int) : int {
    defer delete callback
    return callback()
}
```

Le corps de la fonction est vérifié transitivement. La valeur `scoped`, ses
alias déplacés et les closures qui la capturent ne peuvent pas être retournés,
rangés dans un champ, une globale, un enum, un tableau ou un autre conteneur,
ni transmis à un paramètre ordinaire. Ces échappements produisent
`JANA0026`. La callback peut être invoquée, détruite localement, ou transférée
avec `move` à un autre paramètre `scoped`; ce dernier relais est vérifié selon
les mêmes règles, y compris dans les branches et les fonctions génériques. Les
sorties par `panic` exécutent normalement les nettoyages différés et ne
relâchent pas ce contrat.

`std.option.map`, les combinateurs synchrones de `std.result`, `generateArray`,
`Iterator.fold`, les callbacks de parcours et de tri d’`Array`, ainsi que
`PriorityQueue.withFirst` déclarent ce contrat directement; l'analyseur ne
dépend plus de leurs noms pour borner les captures. Une closure conservée dans
une variable est transférée explicitement avec `move`; une lambda écrite dans
l’appel est transférée directement.

La provenance d'une capture est transitive. Capturer un objet qui contient un
champ `borrow val` ou `borrow var` maintient en vie cet objet et sa source
racine jusqu'à la destruction de la closure. La même règle s'applique lorsque
le porteur a été déplacé dans un `Option`, un `Result` ou un autre enum, puis
extrait par un motif, y compris dans des motifs imbriqués. Détruire ou muter un
porteur encore référencé produit `JANA0025`. Une closure formée dans un bras de
`match` sur un payload emprunté ne peut pas sortir de la fonction : le binding
reste borné à son bras et l'échappement produit `JANA0026`. Détruire la closure
met fin à cette dépendance ; le porteur puis sa source peuvent alors être
détruits dans cet ordre.

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

val hashing : DerivedHashing[Point] = new DerivedHashing()
defer delete hashing
val points : HashSet[Point, DerivedHashing[Point]] =
new HashSet(8, hashing)
defer delete points
```

Le [design des dérivations structurelles](https://github.com/cyril103/janus/blob/main/docs/design/derivations.md)
définit l'éligibilité champ par champ, les génériques, la visibilité, les
diagnostics et les interdictions liées aux valeurs propriétaires.

Les structs peuvent déclarer des méthodes dans leur corps. Leur receveur
implicite `this` désigne le stockage en ligne de la valeur : `borrow def`
l'observe, tandis qu'une méthode sans `borrow` peut modifier ses champs si le
receveur est mutable. Les appels ne changent pas la représentation par valeur
d'un struct et n'allouent pas de copie sur le tas.

### Méthodes d'extension statiques

Un bloc `extend` ajoute des méthodes résolues statiquement à une classe, un
struct ou un enum, sans modifier sa représentation :

```janus
extend[T] Option[T] {
    consume def map[U](scoped transform : (T) => U) : Option[U] {
        return match move this {
            Some(value) => Option.Some[U](transform(move value)),
            None => Option.None[U]()
        }
    }
}
```

Le receveur implicite s'appelle `this`. Chaque méthode choisit explicitement
`borrow def`, `borrow var def` ou `consume def`. Une méthode native est
prioritaire et ne peut pas être remplacée. Plusieurs extensions visibles de
même nom sont rejetées comme ambiguës.

Une extension publique doit vivre dans le module qui définit le type. Un autre
module peut déclarer une `private extend` locale sur un type importé. La
visibilité `private` porte sur l'identité du module, pas sur le fichier : tous
les fichiers qui déclarent le même module voient l'extension, tandis qu'un
module différent ne la voit jamais. Cette règle est également appliquée par la
complétion, le hover, l'aide de signature et la navigation LSP, y compris pour
les buffers non sauvegardés et les URI normalisées.

```janus
// a.janus
module lexer
private extend Token { borrow def code() : int { return 7 } }

// b.janus, également dans le module lexer
module lexer
val n : int = token.code()
```

Un fichier sans déclaration `module` conserve une identité limitée à son URI
canonique : il ne partage donc pas ses extensions privées avec un autre fichier.
En cas de nom de module absent, invalide ou ambigu, l'extension reste masquée et
le diagnostic de résolution habituel est produit. Seul un import simple active
les extensions publiques ; un import qualifié ou sélectif ne les injecte pas
dans la résolution des appels. `Option` et `Result` exposent leurs combinateurs
sous forme de méthodes tout en conservant leurs fonctions libres pour la
compatibilité.

La [RFC des méthodes d'extension](design/static-extension-methods.md) détaille
la cohérence, la visibilité, l'ownership et l'abaissement sans vtable.

## Collections et itérateurs

La bibliothèque standard comprend notamment :

- `Array[T]`, tableau dynamique ;
- `Deque[T]`, tampon circulaire à deux extrémités, et `Queue[T]`, façade FIFO ;
- `HashSet[T, H]`, ensemble sans doublons ;
- `HashMap[K, V, H]`, table associative ;
- `PriorityQueue[T]`, file de priorité stable FIFO fondée sur un tas binaire ;
- builders de tableaux, ensembles et tables ;
- `Range` et itérateurs paresseux ;
- `Option[T]` et `Result[T, E]`.

### Inférence bidirectionnelle des constructeurs

Pour `new Type(...)`, Janus cherche les paramètres génériques à la fois dans
les arguments du constructeur et dans le type attendu. Un argument typé suffit
souvent :

```janus
class Box[T](val value : T) {}
val box = new Box(42) // Box[int], inféré depuis l'argument
```

Une annotation complète peut à l’inverse fournir le type au constructeur. Il
est alors inutile de le répéter, et un littéral positif adopte le `usize`
attendu :

```janus
import std.array
val box : Box[int] = new Box(42)
val values : Array[int] = new Array(8)
```

Si aucun argument ni type attendu ne contraint un paramètre, rendez-le
explicite. `val factory = new Factory()` produit le diagnostic
`cannot infer type of 'factory'; help: add an explicit type annotation; note:
generic type parameter 'T' is not constrained by constructor arguments or
context; help: add explicit type arguments`; écrivez par exemple
`val factory = new Factory[int]()`.

Exemple :

```janus
import std.array

val values : Array[int] = new Array(4)
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

### Indexation sûre

`values[index]` est résolu sur l'identité canonique de `std.array.Array`, y
compris avec un alias de module. Un type local homonyme et `HashMap` ne sont pas
reconnus. La lecture équivaut à `get` : elle copie et exige `T <: Copy`. Pour
une ressource non copiable, utilisez explicitement `withValue` ou
`getBorrowed`; les crochets ne créent aucun emprunt implicite.

Une cible `values[index] = replacement` équivaut à `set`, respecte la
mutabilité et les emprunts actifs, exige `move` pour une valeur propriétaire et
détruit exactement une fois l'ancien élément. Les formes composées (`+=`,
`-=`, etc.) réutilisent la même place. Le conteneur est évalué une fois, puis
l'index une fois, de gauche à droite. Un index hors limites produit le même
panic et le même contexte que `get`/`set`.

Le protocole reste interne dans cette livraison : le frontend lie séparément
les capacités canoniques de lecture et de remplacement à la déclaration
`std.array.Array`. Cela permet une extension future sans figer un trait public.
L'indexation ne consomme jamais implicitement le conteneur ou l'élément :
`consume`, `delete` et `defer` continuent de s'appliquer au propriétaire complet,
et `move` reste obligatoire au point précis d'un transfert propriétaire.

Les constantes globales n'acceptent pas les littéraux de tableaux : `Array[T]`
utilise un stockage dynamique propriétaire et le compilateur n'a pas de
représentation constante équivalente. Le diagnostic `JANA0023` demande de
construire le tableau à l'exécution. Les `val` et `var` globaux ordinaires
restent des initialisations d'exécution.

Les opérations directes de tableau qui retournent un élément par copie restent
réservées aux éléments `Copy`. `getBorrowed`, `getMutable`, `withValue`, `withValues`,
`foreach`, `map`, `fold`, `any`, `all` et `count` observent en revanche leurs
éléments avec des paramètres `(borrow T)`. Une lambda ou une fonction nommée
portant ce type ne peut ni déplacer, ni détruire, ni transmettre l'élément à un
paramètre propriétaire. `map` peut produire des valeurs propriétaires non
`Copy`; `filter` et `find` exigent encore `Copy` puisqu'ils rendent des éléments
du tableau sans le consommer. `swap` échange deux emplacements par déplacement
interne, sans exiger `Copy`.

Un tableau propriétaire reçoit et rend ses éléments par transfert explicite :

```janus
val resources : Array[Resource] = new Array(1)
defer delete resources

val resource : Resource = new Resource()
resources.push(move resource)
val recovered : Resource = resources.remove(0)
defer delete recovered
```

`set` détruit l'élément remplacé ; `replace` le retourne à l'appelant.

`retain` filtre un tableau sur place en détruisant les éléments refusés;
`extend` transfère toutes les valeurs d'un `Iterator[T]`; `drain` consomme le
tableau et rend son parcours propriétaire. `sortBy` reçoit une implémentation
du trait `Ordering[T]`, dont `IntOrdering` et `USizeOrdering` fournissent les
ordres croissants usuels. La comparaison trichotomique retourne `Order.Less`,
`Order.Equal` ou `Order.Greater` et peut aussi être interrogée avec
`orderedBefore`.

`Deque[T]` ajoute et retire aux deux extrémités en temps amorti constant avec
`pushFront`, `pushBack`, `popFront` et `popBack`. Les variantes suffixées
`Option` évitent une panique sur une deque vide; `withFront` et `withBack`
observent une ressource sans exiger `Copy`. `Queue[T]` restreint cette surface à
`enqueue`, `dequeue` et `withFirst` pour garantir un ordre FIFO. Les deux types
implémentent `IntoIterable[T]` et leur parcours consommant suit l'ordre de
sortie, de l'avant vers l'arrière. Elles proposent aussi `reserve`, `extend` et
`drain`; la capacité réservée n'initialise aucune valeur utilisateur.

`HashSet`, `HashMap`, `ArrayBuilder`, `SetBuilder` et `MapBuilder` acceptent
également les éléments propriétaires. `add`, `put` et `Builder.add` consomment
ces éléments avec `move`. Un doublon de `HashSet` détruit la valeur entrante ;
`HashMap.put` détruit l'ancienne clé équivalente et retourne l'ancienne valeur.
`remove` détruit la clé stockée et transfère la valeur retirée.

`HashSet` et `HashMap` exposent désormais leur `capacity` et `reserve` réserve
des insertions supplémentaires en respectant le seuil de charge. `retain`
supprime sur place les membres ou associations refusés, `extend` consomme un
parcours de valeurs ou de `MapEntry`, et les drains transfèrent la totalité du
contenu. `HashMap.withEntry` observe simultanément la clé canonique et sa valeur
sans imposer `Copy`.

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

Le trait `Iterable[T]` décrit `iterator()`, qui observe le conteneur et produit
des copies ; il est donc disponible uniquement quand les éléments produits
satisfont `Copy`. Le trait distinct `IntoIterable[T]` décrit
`intoIterator()`, qui consomme sa source sans imposer `Copy`. `Array`,
`HashSet` et `Iterator` implémentent cette capacité et transfèrent chaque
élément. Pour une table, `intoEntries()` transfère des `MapEntry[K, V]` ;
`intoKey()` ou `intoValue()` permet ensuite de conserver une moitié en
détruisant l'autre.

```janus
val resources : Array[Resource] = new Array(2)
resources.push(move first)
resources.push(move second)

for resource in move resources {
    defer delete resource
    resource.inspect()
}
```

Un `for` sur des valeurs propriétaires exige ce parcours consommant explicite,
soit avec `move source` pour une valeur `IntoIterable[T]`, soit avec
`source.intoIterator()`. Sans `move`, `for` sélectionne exclusivement
`Iterable[T]` et ne transforme jamais une observation en copie propriétaire.
Cette séparation fonctionne aussi dans les contraintes génériques. Les
adaptateurs `map`, `filter`, `filterMap`, `flatMap`, `take`, `drop`,
`takeWhile`, `skipWhile`, `scan`, `chain`, `zip` et `enumerate` transfèrent
également leurs éléments. La lambda de `filter`, `takeWhile` ou `skipWhile`
observe son paramètre pendant l'appel ; `map`, `filterMap`, `flatMap` et `scan`
le consomment. Un élément filtré ou ignoré est détruit immédiatement.

`find`, `any` et `all` court-circuitent au premier résultat décisif. `count`,
`reduce` et `fold` épuisent la source. `tryFold` retourne la première erreur
sans demander d'élément supplémentaire. `partitionWith` distribue chaque valeur
vers deux `Builder` explicites et retourne leurs collections dans un
`PartitionResult`.

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

`collectResult` et `collectOption`, dans le même module, arrêtent la collecte au
premier `Error` ou `None`. Les valeurs déjà collectées et le suffixe non demandé
sont alors détruits. Le pipeline faillible suivant ne crée aucun tableau
intermédiaire :

```janus
val checked : Result[int, string] = values.intoIterator()
    .drop(2)
    .filterMap[int]((value : int) => validate(value))
    .tryFold[int, string](
        0,
        (total : int, value : int) => checkedAdd(total, value)
    )
```

Le [contrat des pipelines](design/iterator-pipelines.md) précise l'état de
chaque adaptateur, les règles d'ownership, les courts-circuits et les coûts.

Les exemples `array.janus`, `hash_collections.janus` et
`iterator_adapters.janus` séparent respectivement les opérations de tableau,
les tables de hachage et les pipelines paresseux ou faillibles.

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
`prime_factors(12)` retourne `2, 2, 3`. Pour `0` et `1`, le tableau est
vide. Pour une entrée première, le tableau contient seulement cette entrée.
L'appelant doit libérer le tableau avec `delete`.

```janus
import std.math
import std.array

def main() : int {
    val factors : Array[usize] = prime_factors(49)
    defer delete factors

    println(factors.get(0)) // 7
    println(factors.get(1)) // 7
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

val random : Random = new Random(42)
defer delete random

val any : usize = random.nextUSize()
val die : usize = random.nextBounded(6) + usize(1)
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
émet `JANA0022`. Le retour `owned` reste réservé aux pointeurs des déclarations
`extern def`. Les fonctions Janus peuvent en revanche retourner `borrow T` ou
`borrow var T` : le résultat reste lié à la durée de vie du propriétaire et le
second contrat autorise sa mutation sans transférer sa propriété.

Le module `std.bytes` fournit `ByteView`, une vue binaire empruntée et bornée.
Les buffers et résultats natifs l'exposent uniquement dans une callback
`scoped`, via `ByteBuffer.withView`, `FileData.withView`,
`ProcessResult.withStdoutView` ou `withStderrView`. `get`, `getOption` et
`withSlice` contrôlent les bornes ; l'adresse native n'est accessible que dans
la callback de `withPointer`, accompagnée de son décalage et de sa longueur.

Une méthode `borrow def` peut également ancrer à `this` le pointeur `borrow`
retourné directement par une fonction externe. Ce contrat sert aux wrappers de
handles natifs : le pointeur devient inutilisable dès que son objet Janus est
détruit, même si le runtime natif conserve physiquement son stockage.

Un alias local peut être déclaré sans transfert de propriété avec
`borrow val view = owner`. Il est immutable et ne peut être ni libéré ni
déplacé. Une classe peut de même conserver une référence observante
dans un champ de constructeur immutable, par exemple
`private borrow val source : Collection`; ce champ n'est pas détruit avec la
classe et sa source doit donc vivre plus longtemps. Les structs ne peuvent pas
contenir de champ emprunté.

Les itérateurs observants de la bibliothèque standard conservent ce verrou
même si l'emprunt est encapsulé dans leur état privé. Tant que l'`Iterator`
reste vivant, détruire ou muter sa collection source (par exemple avec
`Array.push`) est refusé avec `JANA0025`. Le verrou suit les adaptateurs
paresseux tels que `map`, `filter`, `take`, `chain` et `zip`; il est libéré
lorsque l'itérateur est détruit ou consommé complètement.

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

Le contenu d’un fichier reste binaire. `FileData.asText()` valide l’UTF-8 et
retourne `Result[string, TextDecodeError]`; `FileData.withView()` permet une
inspection binaire bornée sans conversion implicite.

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
ferment pas les handles du processus. Une `ByteView` ou un buffer binaire doit
appeler `isUtf8` ou `asText` avant d’être traité comme du texte. Pour inspecter ses octets sans
copie, `withView` fournit temporairement une `ByteView` bornée qui ne peut pas
s'échapper de la callback. Le
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

## Transferts propriétaires explicites

Une classe et, plus généralement, toute valeur qui ne satisfait pas `Copy`
ne peuvent avoir qu'un propriétaire. Leur passage dans une autre variable, un
argument, un retour ou le champ d'un agrégat exige `move` :

```janus
def identity(document : Document) : Document {
    return move document
}

val original : Document = new Document(1)
val transferred : Document = identity(move original)
delete transferred
```

Après chaque `move`, la source est invalidée. Une omission produit le
diagnostic `JANA0035` et une suggestion `move <nom>`. La même règle s'applique
aux paramètres génériques non contraints ; une contrainte `T <: Copy` conserve
la copie implicite. Écrire `move` directement sur un type `Copy` concret reste
une erreur.
