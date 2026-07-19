# Janus

Janus est un langage de programmation fortement typé dont le compilateur est
écrit en C++ et utilise LLVM comme backend.

La version 0.1.0 est expérimentale : le langage, sa bibliothèque standard et
le format des paquets peuvent encore évoluer sans compatibilité ascendante
avant 1.0. Le compilateur produit des exécutables natifs à partir d'un backend
LLVM.

## Exemple

```janus
def main() : int {
    val x : int = 5
    return 0
}
```

Tout programme Janus doit déclarer exactement un point d'entrée
`def main() : int`.

`val` crée une liaison immuable : après sa déclaration, `x` ne peut pas être
réaffecté.

Janus possède actuellement sept types primitifs et le type de retour `Unit` :

| Type Janus | Signification | Représentation LLVM |
| --- | --- | --- |
| `int` | entier signé sur 32 bits | `i32` |
| `double` | nombre flottant sur 64 bits | `double` |
| `byte` | entier signé sur 8 bits | `i8` |
| `char` | scalaire Unicode sur 32 bits | `i32` |
| `bool` | valeur `true` ou `false` | `i1` |
| `string` | chaîne Unicode UTF-8 immuable | `{ ptr, i64 }` |
| `usize` | entier non signé pour les tailles mémoire | `i64` |
| `Unit` | absence de valeur de retour | `void` |

`Unit` s'utilise comme retour d'une fonction ou d'une méthode qui ne produit
pas de valeur. Il ne peut pas servir de type à une variable, un champ ou un
paramètre.

LLVM ne distingue pas directement les entiers signés des entiers non signés
dans ses types. Cette information est donc conservée par le système de types
de Janus.

Les chaînes `string` sont immuables et encodées en UTF-8. Leur représentation
contient un pointeur vers les octets et une longueur `i64` exprimée en octets.
Les littéraux sont stockés dans des constantes globales LLVM. Un zéro terminal
est ajouté pour faciliter une future interopérabilité avec C, mais il n'est pas
compté dans la longueur Janus.

## Prérequis

- un compilateur compatible C++20, comme Clang ou GCC ;
- CMake 3.21 ou plus récent ;
- Ninja ;
- LLVM et ses fichiers de développement ;
- LLD, recommandé pour l'édition de liens.

Sur Ubuntu :

```bash
sudo apt install \
  build-essential \
  cmake \
  ninja-build \
  clang \
  llvm \
  llvm-21-dev \
  libzstd-dev \
  lld
```

Les outils suivants sont également recommandés pour le développement :

```bash
sudo apt install gdb clang-format clang-tidy clangd
```

## Compiler Janus

Depuis la racine du projet :

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=clang++

cmake --build build
```

Les outils sont alors disponibles dans `build/janus`, `build/janusc` et
`build/janusup`.

Pour l'usage quotidien, le pilote `janus` prend directement en charge la
vérification, la génération du fichier objet, l'édition de liens avec LLD et
l'exécution :

```bash
./build/janus check examples/value.janus
./build/janus build examples/value.janus -o value
./value
./build/janus run examples/output.janus
```

`--release` active les optimisations et `--emit llvm-ir|object` permet de
s'arrêter avant l'édition de liens.

`janus fmt` indente tous les fichiers `.janus` des dossiers `src` et `tests`
et normalise les espaces de fin de ligne. `janus fmt --check` ne modifie rien
et retourne un code non nul si un fichier doit être reformaté, ce qui permet
de l'utiliser en CI. Un fichier `.janusfmt` placé à la racine du projet peut
configurer `indent_width` et `max_blank_lines`, par exemple :

```toml
indent_width = 2
max_blank_lines = 1
```

Le paquet fournit aussi `janus-lsp`, un premier serveur LSP utilisable sur
l'entrée/sortie standard. Il implémente le cycle de vie JSON-RPC
`initialize`/`shutdown`/`exit` et annonce la synchronisation des documents
ainsi que le formatage. Les diagnostics incrémentaux seront ajoutés dans une
prochaine étape.

## Installer la chaîne d'outils

La première distribution officielle, Janus 0.1.0, cible Linux x86_64. Les
constructions suivantes produisent également des archives macOS ARM64 et
Windows x86_64. Elles sont fournies sous forme d'archives autonomes.
L'installation Unix se fait avec :

```bash
curl --proto '=https' --tlsv1.2 -fsSL \
  https://raw.githubusercontent.com/cyril103/janus/main/scripts/install.sh | sh
```

Sous Windows PowerShell :

```powershell
irm https://raw.githubusercontent.com/cyril103/janus/main/scripts/install.ps1 | iex
```

L'installation est placée dans `~/.janus` sous Unix et dans
`%LOCALAPPDATA%\Janus` sous Windows. `janusup` gère les chaînes installées :

```bash
janusup list
janusup default 0.1.0
janusup install stable
janusup install beta
janusup install nightly
janusup install 0.1.0
janusup update
janusup uninstall 0.0.9
janusup home
```

Les téléchargements de `janusup` sont contrôlés avec le SHA-256 publié avec
chaque archive. Une mise à jour n'écrase la chaîne active qu'après le
téléchargement, la vérification et l'extraction du nouveau paquet.
Lorsque GitHub CLI est disponible, l'attestation Sigstore de l'archive est
également vérifiée automatiquement. `janusup verify <archive>` impose cette
vérification, et `JANUS_REQUIRE_ATTESTATION=1` la rend obligatoire dans les
installateurs.
Le canal `stable` suit les versions finales, `beta` les préversions et
`nightly` la construction hebdomadaire issue de `main`.

Le compilateur recherche automatiquement son runtime et la bibliothèque
standard relativement à son dossier d'installation. `JANUS_CC` permet de
choisir explicitement le pilote Clang employé pour l'édition de liens.

Pour construire une archive redistribuable depuis les sources :

```bash
cmake --build build --target dist
```

Le paquet contient `janus`, le compilateur historique `janusc`, `janusup`, le
runtime natif, la bibliothèque standard, Clang, LLD et leurs dépendances LLVM.
La cible produit aussi le fichier `.sha256` contrôlé par les installateurs.
L'utilisateur d'une archive officielle n'a donc pas à installer LLVM
séparément ; seul le SDK natif du système reste nécessaire.

Les changements de chaque version sont détaillés dans
[CHANGELOG.md](CHANGELOG.md).

## Projets et dépendances

Un projet Janus est décrit par `janus.toml` :

```toml
[package]
name = "application"
version = "0.1.0"
entry = "src/main.janus"

[dependencies]
outil = { path = "../outil" }
reseau = { git = "https://example.com/reseau.git", rev = "0123456789abcdef0123456789abcdef01234567" }
```

Une dépendance Git exige un hash de commit complet afin qu'une construction
reste reproductible. Les clones sont conservés une seule fois dans le cache
global `~/.janus/cache/git/<commit>` et sont partagés entre les projets.
`JANUS_CACHE` permet de déplacer ce cache. `janus.lock` enregistre exactement
les sources résolues. Le fichier lock doit être versionné, tandis que
`target/` reste ignoré.

## Registre de paquets

Le premier format de registre Janus est un répertoire local immuable. Son
emplacement par défaut est `~/.janus/registry` et peut être remplacé avec
`JANUS_REGISTRY`.

```bash
janus publish
janus add collections@^1.2.0
janus add utilitaires --path ../utilitaires --version '^2.0.0'
janus add protocole --git https://example.com/protocole.git \
  --rev 0123456789abcdef0123456789abcdef01234567
janus remove collections
```

`publish` copie le manifeste et `src/` dans le registre et refuse d'écraser
une version existante. La résolution choisit la version la plus récente qui
satisfait la contrainte SemVer, l'inscrit dans `janus.lock`, puis la conserve
dans le cache global pour les constructions `--offline`.

## Tests

Chaque fichier `.janus` placé sous `tests/` est un programme de test autonome :
son `main` retourne `0` en cas de succès et une autre valeur en cas d'échec.

```bash
janus test
janus test tableau
janus test --release
```

La commande découvre récursivement les tests, continue après un échec et place
les exécutables sous `target/debug/tests` ou `target/release/tests`. Un filtre
facultatif limite l'exécution aux chemins qui contiennent le texte donné.

## Générer de l'IR LLVM

Un programme d'exemple est fourni dans `examples/value.janus` :

```bash
./build/janusc examples/value.janus
```

Le fichier `examples/types.janus` présente tous les types primitifs :

```bash
./build/janusc examples/types.janus
```

Le fichier `examples/generics.janus` présente une fonction générique :

```bash
./build/janusc examples/generics.janus
```

Le fichier `examples/functions.janus` présente les lambdas, captures et
fonctions d'ordre supérieur génériques :

```bash
./build/janusc examples/functions.janus
```

Le fichier `examples/generic_classes.janus` présente une classe générique :

```bash
./build/janusc examples/generic_classes.janus
```

Le fichier `examples/variables.janus` présente les variables mutables :

```bash
./build/janusc examples/variables.janus
```

Le fichier `examples/point.janus` présente une classe allouée sur le tas :

```bash
./build/janusc examples/point.janus
```

Le fichier `examples/operators.janus` présente les opérations primitives :

```bash
./build/janusc examples/operators.janus
```

Le fichier `examples/control_flow.janus` présente `if`/`else` et `while` :

```bash
./build/janusc examples/control_flow.janus
```

Le fichier `examples/loop_jumps.janus` présente `break`, `continue` et leur
interaction avec `defer` :

```bash
./build/janusc examples/loop_jumps.janus
```

Le fichier `examples/unit.janus` présente `Unit` et les appels utilisés comme
instructions :

```bash
./build/janusc examples/unit.janus
```

Le fichier `examples/constructor.janus` présente les paramètres de constructeur
temporaires et l'initialisation ordonnée des champs :

```bash
./build/janusc examples/constructor.janus
```

Le fichier `examples/destructor.janus` présente un destructeur qui libère un
objet possédé :

```bash
./build/janusc examples/destructor.janus
```

Le fichier `examples/usize.janus` présente les tailles et conversions
explicites :

```bash
./build/janusc examples/usize.janus
```

Le fichier `examples/casts.janus` présente les conversions explicites entre
scalaires, adresses et pointeurs :

```bash
./build/janusc examples/casts.janus
```

Le fichier `examples/enums.janus` présente les types énumérés :

```bash
./build/janusc examples/enums.janus
```

Le fichier `examples/pointers.janus` présente la mémoire manuelle typée :

```bash
./build/janusc examples/pointers.janus
```

Le fichier `examples/c_interop.janus` appelle directement la bibliothèque C :

```bash
./build/janusc examples/c_interop.janus > c_interop.ll
clang c_interop.ll -o c_interop
./c_interop
```

Le fichier `examples/panic.janus` présente un contrôle d'exécution :

```bash
./build/janusc examples/panic.janus
```

Le fichier `examples/output.janus` présente l'affichage sur la sortie standard :

```bash
./build/janusc examples/output.janus
```

Le fichier `examples/array.janus` importe et utilise le tableau dynamique de la
bibliothèque standard :

```bash
./build/janusc examples/array.janus
```

`janusc` écrit actuellement l'IR LLVM sur la sortie standard. Pour le
conserver dans un fichier :

```bash
./build/janusc examples/value.janus > value.ll
```

L'IR obtenu peut ensuite être transformé en fichier objet puis en exécutable :

```bash
llc -relocation-model=pic -filetype=obj value.ll -o value.o
clang value.o -fuse-ld=lld -o value
./value
```

Pour l'exemple précédent, le backend produit notamment :

```llvm
define i32 @main() {
entry:
  %x = alloca i32, align 4
  store i32 5, ptr %x, align 4
  ret i32 0
}
```

## Point d'entrée

Le point d'entrée d'un programme utilise le mot-clé `def` :

```text
def main() : int {
    <instructions>
    return <expression>
}
```

`main` ne reçoit actuellement aucun paramètre et doit retourner un `int`. Une
instruction `return` est obligatoire et doit être la dernière instruction
exécutée.

## Syntaxe actuellement supportée

Une valeur immuable doit être initialisée pendant sa déclaration :

```text
val <identifiant> : <type> = <expression>
```

Une variable mutable peut être initialisée immédiatement ou ultérieurement :

```janus
var x : int = 5
x = 6

var y : int
y = x
```

Une variable locale non initialisée ne reçoit aucune valeur par défaut. Toute
lecture avant sa première affectation est rejetée à la compilation :

```janus
var value : int
return value // erreur : utilisation avant initialisation
```

Fonctionnalités disponibles :

- mot-clé `def` pour déclarer une fonction ;
- point d'entrée obligatoire et unique `main() : int` ;
- mot-clé `return` pour retourner un entier ;
- type de retour `Unit`, retour nu `return` et retour implicite en fin de bloc ;
- appels de fonctions et de méthodes utilisés comme instructions ;
- mot-clé `val` pour déclarer une valeur immuable ;
- mot-clé `var` pour déclarer une variable mutable ;
- déclarations `var` sans initialiseur et analyse de l'initialisation ;
- affectations fortement typées ;
- commentaires de fin de ligne introduits par `//` ;
- identifiants commençant par une lettre ou `_`, puis composés de lettres,
  chiffres et `_` ;
- type `int`, signé sur 32 bits ;
- type `double` et littéraux comme `3.14` ;
- type `byte`, signé sur 8 bits, avec contrôle de débordement des littéraux ;
- type `char` et littéraux Unicode comme `'é'` ou `'😀'` ;
- type `bool` et littéraux `true` et `false` ;
- type `string` et littéraux Unicode UTF-8 comme `"Bonjour 🌍"` ;
- littéraux entiers décimaux positifs ;
- point-virgule optionnel après une déclaration ;
- plusieurs déclarations dans le corps d'une fonction ;
- détection des identifiants déclarés plusieurs fois ;
- détection d'une fonction sans `return` et du code après `return` ;
- diagnostics avec ligne et colonne ;
- validation de l'IR généré par LLVM.

### Contrôle de flux

Les conditions exigent une expression de type `bool` :

```janus
if value < 10 {
    value = value + 1
} else {
    value = 0
}

while value < 10 {
    value = value + 1
}
```

`break` quitte la boucle `while` ou `for` la plus proche. `continue` abandonne
le reste de l'itération courante et reprend à sa prochaine condition ou valeur :

```janus
while index < limit {
    index = index + 1
    if index % 2 == 0 {
        continue
    }
    if index > 10 {
        break
    }
}
```

Les deux instructions sont interdites hors d'une boucle. Dans des boucles
imbriquées, elles ciblent toujours la boucle la plus proche.

Lorsqu'un saut abandonne une ou plusieurs portées, leurs actions `defer` sont
exécutées en ordre LIFO avant le branchement. Les actions enregistrées dans
une portée englobante qui reste active ne sont pas exécutées prématurément :

```janus
while condition {
    val buffer : Ptr[int] = alloc[int](usize(1))
    defer free(buffer)
    if shouldSkip {
        continue // buffer est libéré avant l'itération suivante
    }
}
```

Les déclarations d'un bloc restent locales à ce bloc. Une variable non
initialisée avant un `if` n'est considérée comme initialisée après celui-ci que
si les deux branches l'affectent. Le corps d'un `while` pouvant ne jamais être
exécuté, une affectation effectuée uniquement dans la boucle ne suffit pas.

## Opérations primitives

Les opérateurs disponibles sont :

| Types | Opérateurs | Type du résultat |
| --- | --- | --- |
| `int`, `byte` | `+`, `-`, `*`, `/`, `%` | type des opérandes |
| `double` | `+`, `-`, `*`, `/` | `double` |
| `int`, `byte`, `double`, `char` | `<`, `<=`, `>`, `>=` | `bool` |
| tous les types primitifs | `==`, `!=` | `bool` |
| `bool` | `!`, `&&`, `||` | `bool` |
| `int`, `byte`, `double` | `-` unaire | type de l'opérande |

Les parenthèses permettent de contrôler l'évaluation. Sans parenthèses, la
priorité est, de la plus forte à la plus faible : opérateurs unaires, `* / %`,
`+ -`, comparaisons, égalité, `&&`, puis `||`.

Janus n'effectue aucune conversion implicite : `1 + 2.0` est rejeté, car les
deux opérandes n'ont pas le même type. `&&` et `||` utilisent une évaluation
court-circuitée. L'égalité des `string` compare leur longueur puis leurs octets
UTF-8.

Le débordement de `int` et `byte` suit une arithmétique modulo 2^32 et 2^8.
Comme pour les entiers C, une division ou un reste par zéro n'est pas une
opération valide.

`usize` suit une arithmétique non signée modulo 2^64. Janus ne convertit pas
implicitement les valeurs.

### Conversions explicites

La syntaxe `Type(expression)` effectue une conversion explicite :

```janus
val integer : int = int(12.75)
val floating : double = double(integer)
val truth : bool = bool(integer)
val address : usize = usize(pointer)
val bytes : Ptr[byte] = Ptr[byte](address)
```

Les conversions entre `int`, `double`, `byte`, `char`, `bool` et `usize`
suivent les règles scalaires de C : extension, troncature, conversion
entier/flottant et comparaison avec zéro pour `bool`.

Les références de classes et les `Ptr[T]` peuvent être convertis entre eux ou
vers/depuis les types entiers. Une conversion vers `bool` teste si la référence
est non nulle. Ces conversions ne vérifient pas que l'adresse, le type cible ou
l'alignement sont valides : leur utilisation est aussi dangereuse qu'un cast de
pointeur en C. `string` et `Unit` ne sont pas des cibles de cast.

### Types énumérés et filtrage par motif

Un `enum` définit un type nominal distinct. Une variante simple contient
uniquement un discriminant signé de 32 bits :

```janus
enum ExitCode {
    Success,
    InvalidArgument = 2,
    InternalError = 10
}

val result : ExitCode = ExitCode.InvalidArgument
if result != ExitCode.Success {
    println(int(result))
}
```

Le premier discriminant implicite vaut zéro. Les suivants sont incrémentés à
partir du précédent, y compris après une valeur explicite. Deux valeurs du même
enum peuvent être comparées avec `==` et `!=`, mais deux enums différents
restent incompatibles. Les casts explicites permettent de convertir un enum
vers ou depuis les types scalaires ; comme en C, un cast entier vers un enum ne
vérifie pas que la valeur correspond à un cas déclaré.

Les enums peuvent aussi être génériques et transporter des valeurs :

```janus
enum Option[T] {
    Some(T),
    None
}

val option : Option[int] = Option.Some[int](42)
val value : int = match option {
    Some(number) => number,
    None => 0
}
```

`match` est une expression : toutes ses branches doivent produire le même
type. Les valeurs d’une variante sont déstructurées dans des bindings
immuables, limités à la branche concernée. Le compilateur rejette les cas
inconnus, dupliqués ou absents ; chaque `match` doit donc être exhaustif. Les
enums avec payload sont représentés en ligne par un discriminant suivi de leurs
champs, sans allocation implicite.

### Pointeurs et mémoire brute

`Ptr[T]` représente un pointeur brut vers des éléments de type `T` :

```janus
var data : Ptr[int] = alloc[int](usize(4))
data.store(usize(0), 42)
val value : int = data.load(usize(0))
data = realloc[int](data, usize(8))
free(data)
```

`null[T]()` crée un pointeur nul. `sizeof[T]()` et `alignof[T]()` retournent
respectivement la taille et l'alignement de `T` sous forme de `usize`.
`alloc` et `realloc` prennent un nombre d'éléments et calculent eux-mêmes le
nombre d'octets. Comme en C, les accès après `free`, doubles libérations et
indices hors de la zone allouée relèvent de la responsabilité du programmeur.

### Interopérabilité C

Une déclaration `extern def` décrit une fonction fournie par un objet ou une
bibliothèque native liée au programme :

```janus
import std.c

extern("abs") def absolute(value : int) : int

def main() : int {
    printf(cstr("valeur : %d\n"), absolute(-42))
    return 0
}
```

Elle ne possède pas de corps Janus. Le backend émet une déclaration LLVM à
liaison externe. Sans alias, il conserve exactement le nom Janus.
`extern("abs")` permet de choisir un autre symbole natif tout en utilisant le
nom `absolute` dans le programme. Sur la cible 64 bits actuellement prise en
charge, les correspondances sont :

| Janus | Prototype C compatible |
| --- | --- |
| `int` | `int32_t` |
| `double` | `double` |
| `byte` | `int8_t` |
| `char` | `uint32_t` |
| `bool` | `_Bool` / `bool` |
| `usize` | `size_t` |
| `Ptr[T]` | pointeur C compatible vers `T` |
| `Unit` en retour | `void` |

`Unit` n'est jamais accepté comme paramètre. Les types `string`, classes,
enums et fonctions Janus ne peuvent pas traverser l'ABI par valeur ; un
pointeur brut peut servir à définir explicitement une représentation
partagée.

`cstr(text)` expose sans copie le buffer UTF-8 terminé par zéro d'un `string`
sous forme de `Ptr[byte]`. Le pointeur est emprunté : il ne doit pas être
libéré, ni conservé au-delà de la durée de vie de la chaîne. Un octet nul
présent dans la chaîne sera interprété par C comme sa fin.

Une fonction externe peut être variadique :

```janus
extern def printf(format : Ptr[byte], ...) : int

printf(cstr("%d %d %.2f\n"), byte(-7), true, 2.5)
```

Au-delà des paramètres fixes, `byte` est étendu avec son signe vers `int` et
`bool` est étendu vers `int`, conformément aux promotions C. `int`, `char`,
`usize`, `double` et `Ptr[T]` conservent leur représentation. Les chaînes,
objets, enums et closures sont rejetés comme arguments variadiques.

Le module `std.c` déclare actuellement `puts`, `strlen`, `memcmp`, `exit` et
`printf`. Les fonctions externes ne peuvent pas encore être génériques ni
membres d'une classe.

Janus ne lit pas les en-têtes C et ne vérifie pas la définition trouvée à
l'édition de liens. La déclaration `extern def` doit reproduire exactement le
prototype natif : une signature incorrecte relève de la même responsabilité
qu'un mauvais prototype en C.

### Arrêt sur erreur

`panic(message)` écrit un message UTF-8 sur la sortie d'erreur puis termine
immédiatement le processus avec `abort`. Cette primitive sert à construire les
contrôles d'exécution des structures de la bibliothèque :

```janus
if index >= size {
    panic("index out of bounds\n")
}
```

### Sortie standard

`print(value)` écrit une valeur sans retour à la ligne et `println(value)`
ajoute un retour à la ligne. Les deux primitives retournent `Unit` et acceptent
les types primitifs `int`, `double`, `byte`, `char`, `bool`, `string` et
`usize`. Les caractères sont encodés en UTF-8.

```janus
print("résultat : ")
println(42)
println('λ')
```

### Tableau dynamique générique

`Array[T]` est écrit entièrement en Janus dans
`stdlib/std/array.janus`. Sa représentation contient un `Ptr[T]`, une longueur
et une capacité. Le buffer est contigu, sa capacité double lors d'un `push`
quand il est plein, et son destructeur appelle `free`.

L'API initiale comprend :

- `size()` et `capacity()` ;
- `isEmpty()` ;
- `get(index)` et `set(index, value)` avec contrôle de limites ;
- `push(value)` et `pop()` ;
- `getOption(index)` et `popOption()` comme alternatives sans `panic` ;
- `reserve(capacity)` et `clear()` ;
- `foreach((T) => Unit)` ;
- `map[U]((T) => U) : Array[U]` ;
- `filter((T) => bool) : Array[T]` ;
- `find((T) => bool) : Option[T]` ;
- `fold[U](U, (U, T) => U) : U` ;
- `any`, `all` et `count` avec un prédicat `(T) => bool`.

Les méthodes fonctionnelles empruntent la closure reçue et ne la détruisent
pas. L'appelant conserve sa responsabilité : une closure capturante doit être
supprimée après l'appel. `map` et `filter` allouent un nouveau tableau dont
l'appelant devient propriétaire.

### Builders de collections

Le trait `Builder[T, C]` sépare la production de valeurs de la collection
finale qui les stocke :

```janus
trait Builder[T, C] {
    def add(value : T) : Unit
    def result() : C
}
```

La bibliothèque fournit `ArrayBuilder[T]`. Il peut accumuler des éléments
individuellement ou emprunter n'importe quel `Iterable[T]` :

```janus
import std.builder

val builder : ArrayBuilder[int] =
    new ArrayBuilder[int](usize(16))
builder.add(10)
builder.addAll[Array[int]](values)

val result : Array[int] = builder.result()
delete builder

// result appartient maintenant à l'appelant
delete result
```

`size()` expose le nombre d'éléments accumulés et `clear()` réinitialise le
contenu. `result()` transfère la propriété du tableau terminé puis laisse un
tableau vide dans le builder. Le builder peut donc être supprimé sans
invalider le résultat, ou être réutilisé pour une nouvelle construction.

### Ensemble de hachage

`std.hashset` fournit `HashSet[T, H <: Hashing[T]]`. Il utilise un adressage
ouvert, conserve des tombstones après les suppressions et double sa capacité
à partir de 75 % d'occupation :

```janus
val hashing : IntHashing = new IntHashing()
val values : HashSet[int, IntHashing] =
    new HashSet[int, IntHashing](usize(8), hashing)

values.add(10)
values.add(20)
values.add(10) // false : déjà présent
values.remove(20)

for value in values {
    println(value)
}

delete values
delete hashing
```

L'ensemble emprunte sa stratégie `Hashing[T]` : elle doit rester vivante
jusqu'à sa destruction. `SetBuilder[T, H]` implémente
`Builder[T, HashSet[T, H]]` et élimine naturellement les doublons pendant une
collecte.

### Table associative

`std.hashmap` fournit `HashMap[K, V, H <: Hashing[K]]` avec le même adressage
ouvert :

```janus
val hashing : IntHashing = new IntHashing()
val scores : HashMap[int, int, IntHashing] =
    new HashMap[int, int, IntHashing](usize(8), hashing)

scores.put(1, 10)
scores.put(2, 20)
val previous : Option[int] = scores.put(1, 11)
val score : Option[int] = scores.getOption(1)
val removed : Option[int] = scores.remove(2)

for key in scores.keys() {
    println(key)
}

delete scores
delete hashing
```

`put` retourne l'ancienne valeur lorsqu'une clé existante est remplacée.
`containsKey`, `size`, `isEmpty` et `clear` complètent l'API. `keys()`,
`values()` et `entries()` produisent des itérateurs paresseux ; une entrée est
représentée par `MapEntry.Entry(key, value)`.

`MapBuilder[K, V, H]` accepte des `MapEntry[K, V]` et transfère la table
terminée avec `result()`. Comme pour `HashSet`, la stratégie de hachage est
empruntée et doit être détruite après toutes les tables qui l'utilisent.

`get` et `pop` conservent leur comportement strict et appellent `panic` en cas
d’erreur. Leurs variantes `getOption` et `popOption`, ainsi que `find`,
retournent `None` lorsque aucune valeur n’est disponible. `std.array` importe
automatiquement `std.option`.

### Itérateurs paresseux

`Array.iterator()` crée un `Iterator[T]` qui emprunte le tableau source. Ses
adaptateurs ne créent aucun tableau intermédiaire :

```janus
val selected : Array[int] =
    values.iterator()
        .map[int]((value : int) => value * 2)
        .filter((value : int) => value > 10)
        .take(usize(5))
        .collect()
```

`map`, `filter` et `take` sont paresseux : une valeur ne traverse la chaîne que
lorsque `next()` ou `collect()` la demande. Chaque étage possède l’itérateur
précédent ; supprimer l’itérateur final détruit récursivement toute la chaîne.
`map` et `filter` prennent également possession de la closure reçue.

`collect()` est une opération terminale fondée sur `ArrayBuilder[T]` : elle
construit un nouvel `Array[T]`, consomme puis détruit automatiquement
l’itérateur. L’appelant devient propriétaire du tableau retourné et doit le
supprimer. L’`Array` source reste emprunté et doit vivre jusqu’à la destruction
ou la consommation de l’itérateur.

La variante générique
`collectWith[C, B <: Builder[T, C]](builder : B) : C` permet à un itérateur de
produire une autre collection sans connaître sa représentation. Le builder
est emprunté pendant l'opération ; l'appelant reste responsable de sa
destruction.

Les adaptateurs qui prennent possession de leur itérateur sont déclarés avec
`consume def`. Le compilateur invalide automatiquement une variable utilisée
comme receveur :

```janus
val iterator : Iterator[int] = values.iterator()
val selected : Iterator[int] =
    iterator.filter((value : int) => value > 0)

iterator.next() // erreur : iterator a été consommé
```

Une méthode ordinaire emprunte `this`. Une méthode `consume` prend possession
de `this` et peut le transférer ou le détruire. Ce contrat fait partie de la
signature d'un trait et doit être respecté par ses implémentations :

```janus
trait Disposable {
    consume def dispose() : Unit
}
```

Consommer un champ exige un `move` explicite, afin d'éviter un transfert
invisible depuis l'intérieur d'un objet. Les alias manuels restent sous la
responsabilité du programmeur.

Le mot-clé `move` rend ces transferts explicites pour les valeurs possédantes :

```janus
val first : Box = new Box(42)
val second : Box = move first
delete second
```

Après le déplacement, `first` est considéré comme non initialisé et ne peut
plus être lu ou supprimé. `move` s’applique aux objets, closures, pointeurs et
valeurs génériques, mais pas aux valeurs primitives copiables.

Une boucle `for` accepte un `Iterator[T]` ou un objet qui implémente
`Iterable[T]`. Dans le second cas, elle emprunte l'objet, crée son itérateur,
puis détruit uniquement cet itérateur à la sortie normale :

```janus
for value in values {
    println(value)
}
```

Lorsqu'elle reçoit directement un itérateur, la boucle le consomme et le
détruit :

```janus
for value in values.iterator().filter((value : int) => value > 0) {
    println(value)
}
```

`std.range` fournit un intervalle entier croissant, semi-ouvert :

```janus
import std.range

for value in range(0, 10) {
    println(value)
}
```

`range(start, end)` produit paresseusement les valeurs de `start` inclus à
`end` exclu et retourne directement un `Iterator[int]`.

Les adaptateurs supplémentaires restent eux aussi paresseux :

- `zip[U](Iterator[U]) : Iterator[ZipItem[T, U]]` ;
- `enumerate() : Iterator[Indexed[T]]` ;
- `flatMap[U]((T) => Iterator[U]) : Iterator[U]`.

`ZipItem` se déstructure avec `Zipped(left, right)` et `Indexed` avec
`IndexValue(index, value)`. `zip` s’arrête avec l’itérateur le plus court.
`flatMap` détruit chaque itérateur intérieur dès qu’il est épuisé.

### Propagation de `Option` et `Result`

L’opérateur postfixé `?` extrait `Some` ou `Ok` et retourne immédiatement
`None` ou `Error` depuis la fonction courante :

```janus
def convert(input : Result[int, string]) : Result[double, string] {
    val value : int = input?
    return Result.Ok[double, string](double(value))
}
```

La fonction englobante doit retourner le même type algébrique. Pour `Result`,
le type d’erreur doit être identique ; le type de succès peut changer. `?`
n’est pas encore autorisé dans une lambda littérale.

Le tableau possède son buffer, mais pas les objets éventuellement stockés. Un
`Array[Point]` copie les pointeurs vers les `Point` : le programmeur doit
continuer à supprimer chaque objet séparément.

## Modules et bibliothèque standard

Un fichier peut déclarer son nom de module puis importer des dépendances avec
des noms qualifiés :

```janus
module application.main
import std.array
```

Un import `project.collections` recherche
`project/collections.janus`, d'abord relativement au module d'entrée puis dans
les chemins de modules configurés. `janusc` ajoute automatiquement le
répertoire `stdlib` du projet à ces chemins.

Les déclarations importées sont actuellement accessibles sans qualification :

```janus
import std.array
import std.option
import std.result

val values : Array[int] = new Array[int](usize(4))
val maybeValue : Option[int] = Option.Some[int](42)
val result : Result[int, string] = Result.Ok[int, string](42)
```

La bibliothèque fournit `Option[T]` avec `Some(T)`/`None`, et `Result[T, E]`
avec `Ok(T)`/`Error(E)`.

Le chargeur traite les dépendances récursivement, ne charge un fichier qu'une
fois et vérifie que le nom déclaré par celui-ci correspond au nom importé.

`std.hashing` fournit le contrat explicite `Hashing[T]` :

```janus
trait Hashing[T] {
    def hash(value : T) : usize
    def equals(left : T, right : T) : bool
}
```

Les stratégies `IntHashing`, `USizeHashing`, `ByteHashing`, `CharHashing` et
`BoolHashing` couvrent les primitives correspondantes. Elles sont passées
explicitement aux collections de hachage, restent empruntées par celles-ci et
sont monomorphisées sans dispatch dynamique.

## Fonctions et généricité

Une fonction peut recevoir des paramètres fortement typés :

```janus
def select(value : int) : int {
    return value
}
```

Les paramètres de types utilisent une syntaxe inspirée de Scala :

```janus
def identity[T](value : T) : T {
    return value
}
```

Les arguments de types doivent actuellement être indiqués explicitement :

```janus
val integer : int = identity[int](5)
val floating : double = identity[double](2.5)
```

Le backend applique une monomorphisation. Une fonction LLVM spécialisée est
générée pour chaque combinaison de types utilisée :

```text
identity[int]    -> identity__int(i32) -> i32
identity[double] -> identity__double(double) -> double
```

### Traits et contraintes génériques

Un trait générique décrit un contrat de méthodes sans imposer de
représentation :

```janus
trait Iterable[T] {
    def iterator() : Iterator[T]
}

class Values[T]() extends Iterable[T] {
    def iterator() : Iterator[T] {
        // construction de l'itérateur
    }
}
```

La classe doit fournir chaque méthode publique avec exactement la signature
annoncée. Une borne `<:` permet ensuite d'utiliser ce contrat dans du code
générique :

```janus
def visit[C <: Iterable[int]](values : C) : int {
    for value in values {
        println(value)
    }
    return 0
}
```

Plusieurs contrats se combinent avec `&` :

```janus
def inspect[T <: Iterable[int] & Sized](value : T) : usize {
    return value.size()
}
```

Le type concret fourni doit satisfaire toutes les bornes. Si plusieurs traits
bornés déclarent une méthode de même nom, l'appel est refusé comme ambigu.

Les traits Janus sont résolus statiquement. La monomorphisation remplace `C`
par sa classe concrète et appelle directement sa méthode `iterator`, sans
objet de trait, vtable, boxing ni coût de dispatch à l'exécution.

### Fonctions de première classe

Une signature de fonction est un type comme les autres :

```janus
val increment : (int) => int = (value : int) => value + 1
val sum : (int, int) => int =
    (left : int, right : int) => left + right
```

Une lambda peut capturer les valeurs visibles, être affectée à une variable,
passée en argument ou retournée :

```janus
def apply[T](function : (T) => T, value : T) : T {
    return function(value)
}

def makeAdder(amount : int) : (int) => int {
    return (value : int) => value + amount
}
```

Les captures sont copiées lors de la création de la lambda. Une fonction est
représentée par un pointeur de code et un pointeur vers son environnement,
alloué sur le tas lorsqu'au moins une valeur est capturée. Une lambda sans
capture utilise un environnement nul et ne provoque aucune allocation. Comme
pour un objet, les copies d'une closure sont non propriétaires et le
propriétaire doit libérer manuellement l'environnement :

```janus
val addTen : (int) => int = makeAdder(10)
val result : int = addTen(32)
delete addTen
```

Une lambda utilise les paramètres de types de la fonction générique qui la
construit. Chaque utilisation produit donc une closure monomorphisée sans
boxing :

```janus
def makeIdentity[T]() : (T) => T {
    return (value : T) => value
}
```

Utiliser une copie après la suppression du propriétaire, oublier `delete` ou
supprimer deux copies du même environnement relève de la responsabilité du
programmeur.

Les paramètres restent immuables. Les valeurs locales peuvent être immuables
avec `val` ou mutables avec `var`. Le compilateur vérifie le nombre d'arguments
de types, le nombre d'arguments ordinaires et la compatibilité exacte de leurs
types.

### Classes génériques

Une classe peut déclarer un ou plusieurs paramètres de types avec la même
syntaxe :

```janus
class Box[T](var value : T) {
    def get() : T {
        return value
    }

    def set(next : T) : T {
        value = next
        return value
    }
}
```

Les arguments de types sont obligatoires dans une annotation et après `new` :

```janus
val integers : Box[int] = new Box[int](41)
val text : Box[string] = new Box[string]("Janus")
```

Les références de types peuvent être imbriquées, par exemple
`Box[Box[int]]`. Le compilateur vérifie l'arité, substitue les paramètres dans
les champs, les paramètres et les retours de méthodes, et refuse d'affecter
une spécialisation à une autre.

Le backend monomorphise également les classes. `Box[int]` et `Box[string]`
possèdent des layouts LLVM, des méthodes et des destructeurs distincts. Une
spécialisation qui n'est jamais utilisée ne génère pas de code.

## Classes et allocation manuelle

Un paramètre de constructeur sans `val` ou `var` est disponible pendant
l'initialisation, mais ne devient pas un champ :

```janus
class Counter(initialValue : int, private val step : int) {
    private val initial : int = initialValue
    private var current : int = initial
}
```

Les paramètres simples doivent précéder les paramètres `val`/`var`. Les champs
du corps sont initialisés dans leur ordre de déclaration et peuvent lire les
paramètres du constructeur ainsi que les champs déjà initialisés.

Les paramètres `val` et `var` d'une classe sont à la fois des paramètres du
constructeur et des champs publics :

```janus
class Point(var x : int, var y : int) {
    private val secret : int = 42

    private def secretValue() : int {
        return secret
    }

    def reveal() : int {
        return this.secretValue()
    }

    def setX(value : int) : int {
        x = value
        return x
    }

    def currentX() : int {
        return this.x
    }

    destructor {
    }
}
```

Une instance est toujours allouée explicitement sur le tas :

```janus
val point : Point = new Point(1, 2)
val changed : int = point.setX(6)
val result : int = point.currentX()
delete point
```

`new` utilise actuellement `malloc`. `delete` appelle le destructeur de la
classe puis `free`. Il n'existe ni ramasse-miettes, ni comptage de références.
Le programmeur reste responsable de chaque objet alloué, mais peut planifier
explicitement sa libération à la fin d'une portée avec `defer`.

### Nettoyage différé

`defer` enregistre une suppression ou un appel qui sera exécuté à la sortie de
la portée courante :

```janus
def compute() : int {
    val values : Array[int] = new Array[int](usize(4))
    defer delete values

    values.push(42)
    return values.get(usize(0))
}
```

Les actions sont exécutées dans l'ordre inverse de leur déclaration (LIFO),
y compris avant un `return` et lorsqu'un `?` propage `None` ou `Error`. Cela
permet de déclarer d'abord une ressource partagée, puis les objets qui
l'empruntent : ces derniers seront détruits avant elle.

Une action différée peut aussi être un appel de fonction ou de méthode :

```janus
defer flush()
defer resource.close()
```

Une valeur possédée planifiée pour une suppression ou une consommation
différée reste utilisable par emprunt, mais elle ne peut plus être déplacée,
consommée, supprimée à nouveau ou réassignée. `defer delete` exige donc une
variable locale possédante. L'opérateur `?` n'est pas accepté à l'intérieur
d'une action différée.

`defer` est un mécanisme lexical, pas une gestion automatique de la mémoire :
il faut toujours l'écrire explicitement. Un arrêt par `panic` termine le
processus immédiatement et n'exécute pas les actions différées.

Une liaison `val` empêche de remplacer le pointeur, mais les champs déclarés
avec `var` restent modifiables. Un champ `val` est immuable. Le compilateur
détecte les doubles suppressions et utilisations après `delete` les plus
directes sur une variable locale. Comme en C, les usages invalides passant par
des alias ne pourront pas tous être détectés statiquement.

Le bloc de classe accepte les champs `val` et `var`, les méthodes `def` et un
bloc `destructor`. Une méthode reçoit un paramètre `this` implicite. Elle peut
accéder aux champs directement avec `x` ou explicitement avec `this.x`, et
modifier les champs déclarés avec `var`.

Les corps de destructeurs acceptent les mêmes instructions qu'une fonction
`Unit`, notamment les conditions, les boucles, les appels et `delete`.

Une méthode peut déclarer ses propres paramètres de types, en plus de ceux de
sa classe. Les arguments sont explicites à l'appel et chaque combinaison est
monomorphisée :

```janus
class Converter[T]() {
    def convert[U](value : T, transform : (T) => U) : U {
        return transform(value)
    }
}

val result : double =
    converter.convert[double](42, (value : int) => double(value))
```

### Membres privés

Le mot-clé `private` peut précéder un champ `val`, un champ `var` ou une
méthode `def` :

```janus
class Vault() {
    private val secret : int = 42
    private var accesses : int = 0

    private def secretValue() : int {
        return secret
    }

    def reveal() : int {
        accesses = 1
        return this.secretValue()
    }
}
```

Les membres privés restent accessibles depuis les méthodes de leur classe,
directement ou via `this`. Leur lecture, leur mutation ou leur appel depuis
l'extérieur produit une erreur de compilation. Les méthodes privées utilisent
également une liaison interne dans l'IR LLVM.

Exemples d'erreurs détectées :

```janus
def main() : int {
    val x int = 5
    val y : inconnu = 1
    val trop_grand : int = 2147483648
    val x : int = 1
    val x : int = 2
    return 0
}
```

L'inférence des arguments génériques et les autres types ne sont pas encore
implémentés. Les littéraux `double` utilisent actuellement la forme décimale
simple, sans notation exponentielle.

## Tests

Après avoir compilé le projet :

```bash
ctest --test-dir build --output-on-failure
```

Les tests couvrent actuellement :

- les propriétés sémantiques du type `int` ;
- son abaissement vers LLVM `i32` ;
- les propriétés et l'abaissement LLVM de `double`, `byte`, `char` et `bool` ;
- les littéraux flottants, booléens et Unicode ;
- les littéraux `string`, leur validation UTF-8 et leur longueur en octets ;
- le contrôle de la plage des littéraux affectés à `byte` ;
- la reconnaissance et la validation de `def main() : int` ;
- l'analyse de `val x : int = 5` ;
- l'immutabilité d'une déclaration `val` ;
- les déclarations mutables `var` et leurs réaffectations ;
- l'absence de valeur par défaut pour une variable locale ;
- la détection des lectures avant initialisation ;
- les erreurs d'affectation et les commentaires `//` ;
- l'obligation de retourner une valeur depuis `main` ;
- les paramètres et références aux valeurs ;
- les appels de fonctions ;
- les déclarations `extern def`, leurs restrictions ABI et les appels C natifs ;
- le passage de scalaires et pointeurs entre Janus et une bibliothèque C ;
- la mutation d'un buffer Janus depuis C sous AddressSanitizer ;
- les types fonction, lambdas, captures et appels indirects ;
- le passage, le retour et la libération manuelle des closures ;
- les fonctions d'ordre supérieur génériques monomorphisées ;
- les paramètres de types génériques ;
- la vérification des appels génériques invalides ;
- la monomorphisation de fonctions pour plusieurs types ;
- les classes génériques et leurs références de types imbriquées ;
- la substitution des types dans les champs et les méthodes ;
- la monomorphisation de plusieurs spécialisations d'une classe ;
- les classes et leurs champs constructeurs ;
- l'allocation manuelle avec `new` ;
- l'accès et la mutation des champs `var` ;
- les méthodes, leur receveur `this` implicite et les appels sur une instance ;
- l'accès direct ou explicite aux membres depuis une méthode ;
- les champs et méthodes privés et leurs diagnostics d'accès externe ;
- les opérations arithmétiques, comparaisons, égalités et opérations logiques ;
- la priorité des opérateurs et l'évaluation court-circuitée ;
- l'égalité des chaînes par longueur et octets UTF-8 ;
- l'appel du destructeur et la libération avec `delete` ;
- plusieurs erreurs de construction et de durée de vie locale ;
- la table des symboles et les déclarations dupliquées ;
- la génération de l'allocation et du stockage LLVM ;
- plusieurs erreurs de syntaxe et de type.

## Organisation du projet

```text
include/janus/
  ast/          Arbre syntaxique abstrait
  backend/llvm/ Abaissement des types et génération d'IR
  diagnostics/ Erreurs de compilation et positions dans le source
  frontend/     Lexer, tokens et parser
  semantic/     Analyse sémantique et table des symboles
  types/        Système de types

src/            Implémentation des bibliothèques
tools/janusc/   Exécutable du compilateur
tests/          Tests du langage et du système de types
examples/       Programmes Janus d'exemple
stdlib/         Modules de la bibliothèque standard écrits en Janus
```

## Licence

Janus est distribué sous licence Apache-2.0. Consultez [LICENSE](LICENSE).
