# Décision de langage : emprunts lexicaux sûrs

Statut : acceptée et validée. Les emprunts partagés de l'issue #260, les
emprunts mutables exclusifs de l'issue #261, les invalidations lexicales de
l'issue #262, les transmissions bornées de l'issue #263, les vues contiguës de
l'issue #264 et la matrice de validation de l'issue #265 sont implémentés. Les
retours empruntés partagés et les effets d'emprunt des types de fonction sont
également pris en charge. Les projections fines, les retours empruntés
mutables et les régions à la dernière utilisation restent différés.

Cette décision définit le premier modèle général d'emprunts de Janus. Elle
étend les garanties de [propriété des conteneurs](container-ownership.md) sans
modifier le principe fondamental : une valeur propriétaire possède seule sa
ressource jusqu'à un `move` ou sa destruction.

Les issues #260 à #265 suivent l'implémentation, les vues contiguës et la
validation de cette décision. La syntaxe et les règles décrites ici sont
normatives pour ces travaux, mais ne font pas encore partie du contrat stable
du langage.

## Problème

Janus sait transférer une ressource, la détruire exactement une fois et
l'observer dans quelques contextes bornés. Il possède aussi des contrats
`borrow` spécialisés pour les pointeurs externes, les alias pointeurs locaux et
certains champs observants de classe.

Ces mécanismes ne permettent pas encore de transmettre uniformément une vue
sur une valeur propriétaire entre plusieurs fonctions, de modifier
temporairement un élément sans l'extraire, ni de construire une `Slice[T]`
sûre. Les callbacks contextuels des collections compensent partiellement cette
absence, au prix d'API spécifiques et de copies supplémentaires.

Le modèle retenu doit donc :

- ne créer aucun second propriétaire implicite ;
- distinguer observation partagée et mutation exclusive ;
- inférer les durées sans les exposer dans les signatures publiques initiales ;
- rejeter statiquement toute utilisation après invalidation ;
- rester compatible avec `move`, `delete`, `defer` et les nettoyages sur
  panique ;
- ne nécessiter ni comptage de références ni contrôle de durée à l'exécution.

## Vocabulaire

- **propriétaire** : liaison ou emplacement responsable de la destruction
  d'une valeur non `Copy` ;
- **place** : stockage identifiable, par exemple une liaison locale, un champ
  ou un élément de conteneur ;
- **emprunt partagé** : droit temporaire d'observer une place sans la modifier
  ni la consommer ;
- **emprunt mutable** : droit temporaire et exclusif d'observer et modifier une
  place sans la consommer ;
- **région** : portion du graphe de contrôle pendant laquelle un emprunt peut
  encore être utilisé ;
- **projection** : emprunt d'une sous-place, par exemple `document.title` ;
- **réemprunt** : emprunt plus court créé depuis un emprunt existant.

Un emprunt est une capacité d'accès suivie par le compilateur, pas un nouveau
propriétaire. Il conserve le type nominal de la valeur et n'ajoute aucune
allocation, destruction ou opération de comptage à l'exécution.

## Syntaxe retenue

### Liaisons locales

```janus
borrow val view : Document = document
borrow var editable : Document = document
```

`borrow val` crée un emprunt partagé. `borrow var` crée un emprunt mutable ; le
mot `var` décrit ici la capacité de modifier la valeur visée, pas la possibilité
de faire pointer `editable` vers une autre place. Une liaison empruntée ne peut
jamais être réassignée.

Le type peut être inféré comme pour les autres déclarations :

```janus
borrow val view = document
borrow var editable = document
```

### Paramètres

```janus
def inspect(borrow document : Document) : int { ... }
def edit(borrow var document : Document) : Unit { ... }
```

Un paramètre `borrow` reçoit un emprunt partagé. `borrow var` reçoit un emprunt
mutable. L'emprunt transmis couvre au minimum l'appel ; le corps peut le
réemprunter pour des appels plus courts.

Une signature générique conserve les mêmes règles :

```janus
def inspectAny[T](borrow value : T) : Unit { ... }
```

Le qualificateur est un effet de paramètre et ne participe pas à l'identité
nominale de `T`.

### Récepteurs de méthodes

Les méthodes distinguent trois effets sur `this` :

```janus
class Document(private var revision : int) {
    borrow def currentRevision() : int { return revision }

    def touch() : Unit {
        revision = revision + 1
    }

    consume def close() : Unit { delete this }
}
```

| Déclaration | Capacité exigée sur `this` | Effet autorisé |
| --- | --- | --- |
| `borrow def` | emprunt partagé | observer, sans modifier ni consommer |
| `def` | emprunt mutable | observer et modifier, sans consommer |
| `consume def` | propriété | déplacer ou détruire `this` |

Un propriétaire peut appeler les trois catégories. Un emprunt partagé peut
uniquement appeler une méthode `borrow def`. Un emprunt mutable peut appeler
une méthode `borrow def` ou `def`, mais jamais `consume def`.

Les appels existants effectués directement sur un propriétaire conservent leur
sens. Les méthodes d'observation destinées à être appelées à travers un emprunt
devront déclarer `borrow def`. Cette annotation évite une analyse d'effets
implicite fragile et rend le contrat visible dans la documentation d'API.

## Invariants d'aliasing

Pour une même place et une même région d'exécution :

| État actif | Nouvel emprunt partagé | Nouvel emprunt mutable | `move`/destruction |
| --- | --- | --- | --- |
| aucun emprunt | autorisé | autorisé | autorisé |
| un ou plusieurs partagés | autorisé | interdit | interdit |
| un mutable | interdit, sauf réemprunt borné | interdit, sauf réemprunt borné | interdit |

Plus précisément :

1. plusieurs emprunts partagés peuvent coexister ;
2. un emprunt mutable doit être l'unique accès actif à la place ;
3. le propriétaire est gelé pour toute la région d'un emprunt partagé et ne
   peut qu'être observé ;
4. l'accès direct au propriétaire est suspendu pendant un emprunt mutable ;
5. aucun emprunt ne peut déplacer, détruire ou transmettre en `consume` la
   valeur visée ;
6. la fin du dernier emprunt rend automatiquement ses capacités au propriétaire.

Ces règles s'appliquent aussi aux alias transitifs. Copier le nom d'un emprunt
ou le réemprunter ne permet jamais de contourner l'exclusivité de sa source.

## Création et réemprunt

Un emprunt peut être créé depuis un propriétaire vivant ou depuis un emprunt
offrant une capacité au moins aussi forte :

- propriétaire vers partagé ou mutable ;
- emprunt partagé vers partagé ;
- emprunt mutable vers partagé ou mutable plus court.

Un réemprunt mutable suspend l'emprunt mutable parent jusqu'à sa fin. Un
réemprunt partagé créé depuis un emprunt mutable suspend les mutations du
parent tant que le réemprunt partagé est vivant.

```janus
borrow var editable = document
{
    borrow val snapshot = editable
    inspect(snapshot)
}
editable.touch()
```

Une valeur temporaire propriétaire ne peut pas être la source d'un emprunt qui
survit à l'expression complète. Elle doit d'abord être liée à un propriétaire
dont la région est assez longue.

## Projection des champs

L'emprunt d'un champ crée un prêt sur cette sous-place :

```janus
borrow val title = document.title
borrow var cursor = document.cursor
```

Deux champs nommés et distincts d'un même agrégat peuvent être empruntés
mutablement en même temps lorsque le compilateur prouve qu'ils ne se
recouvrent pas. Pendant ces prêts, les autres champs restent accessibles avec
une capacité compatible.

Le propriétaire complet ne peut toutefois pas être déplacé, détruit ou passé à
une opération susceptible de modifier un champ emprunté. Une union, une
variante d'enum ou une projection dont le recouvrement n'est pas prouvable est
traitée conservativement comme un emprunt de la valeur entière.

Dans la première version, deux indexations d'un conteneur sont supposées
pouvoir désigner le même emplacement, même lorsque leurs indices semblent
différents. Une API de conteneur peut fournir des opérations spécialisées pour
prouver des places disjointes.

## Inférence des régions

Les régions ne sont pas écrites dans le code source. Le compilateur les infère
à partir du graphe de contrôle et des utilisations possibles.

Une région :

- commence à la création de l'emprunt ;
- inclut toutes ses utilisations accessibles ;
- se termine après sa dernière utilisation possible sur chaque chemin ;
- ne dépasse jamais la portée lexicale de la liaison empruntée ni celle de sa
  source ;
- est étendue lorsqu'une closure ou un `defer` non échappant peut encore
  utiliser l'emprunt.

Le propriétaire peut donc être réutilisé après la dernière utilisation
prouvée, sans bloc de portée artificiel :

```janus
val document : Document = new Document(0)
borrow val view = document
inspect(view)

val archived : Document = move document
defer delete archived
```

Cette analyse est sensible au contrôle de flux. À la jonction de plusieurs
branches, un emprunt reste actif s'il peut encore être utilisé sur au moins un
chemin. Les boucles sont analysées jusqu'à un point fixe : une itération ne
peut pas conserver un emprunt invalidé ou créer des accès exclusifs qui se
chevauchent avec l'itération suivante.

`return`, `break`, `continue` et `?` terminent les régions qui ne sont plus
utilisables sur le chemin quitté. Ils ne permettent pas à un emprunt de sortir
de la portée de sa source.

## Opérations d'invalidation

Une opération est rejetée dès qu'elle pourrait invalider une place encore
empruntée.

| Opération | Places invalidées |
| --- | --- |
| `move value`, `delete value`, `free(value)` | la valeur entière et toutes ses projections |
| affectation ou remplacement d'un champ | ce champ et ses sous-projections |
| changement de variante d'un enum | la valeur entière |
| `set` ou `replace` | l'élément remplacé ; conservativement le conteneur si l'identité de place n'est pas prouvée |
| `remove` | l'élément retiré et les éléments dont la position peut changer |
| `push`, `reserve` ou croissance | toutes les vues du stockage en cas de réallocation possible |
| `clear`, consommation ou destruction d'un conteneur | tous ses éléments et toutes ses vues |

L'absence de réallocation à l'exécution ne suffit pas : si le contrat d'une
opération autorise une réallocation, l'invalidation est considérée possible.
Une API garantissant explicitement la stabilité des places peut déclarer un
effet plus précis.

## Fonctions et appels

Un argument passé à un paramètre partagé crée ou prolonge un emprunt partagé
pour la durée nécessaire à l'appel. Un paramètre mutable exige une place
mutable et exclusive. Le callee ne peut ni stocker ni retourner cet emprunt
dans la première version.

L'ordre d'évaluation des arguments ne change pas les règles : tous les prêts
nécessaires à un appel doivent être compatibles entre eux. Passer deux emprunts
mutables de la même place au même appel est interdit.

Un appel inconnu ou une frontière sans contrat ne reçoit jamais implicitement
un emprunt. Le programme doit choisir explicitement entre copie, `move`,
`borrow` et `borrow var` selon la signature.

## Closures

Une closure peut capturer un emprunt uniquement lorsque le compilateur prouve
qu'elle n'échappe pas à sa région. Sont notamment permis les callbacks
synchrones dont le contrat garantit qu'ils ne sont ni stockés ni retournés.

La région d'un emprunt capturé couvre toutes les invocations possibles de la
closure. Une capture partagée interdit la mutation de la source pendant cette
région ; une capture mutable exige l'exclusivité et rend la closure elle-même
non réentrante sur cette capture.

Dans la première version, une closure capturant un emprunt ne peut pas :

- être retournée ;
- être stockée dans un champ, un conteneur ou une globale ;
- être convertie en callback asynchrone ;
- survivre à l'appel synchrone auquel elle est transmise.

Les callbacks contextuels historiques de `Array` et des itérateurs deviennent
à terme des cas ordinaires de cette règle. Ils restent compatibles pendant la
migration.

L'implémentation de l'issue #263 autorise une closure locale capturant un alias
`borrow val` ou `borrow var` : la closure devient elle-même un emprunteur
lexical et doit être détruite avant que sa source puisse être invalidée. Une
closure littérale peut également être transmise aux combinateurs synchrones
connus de `Array`, `Option`, `Result` et à `Iterator.fold`. Une fonction sans
contrat synchrone est traitée conservativement comme susceptible de conserver
la closure. Les états d'itérateur historiques couplés à `owningCapture` gardent
leur traitement de compatibilité jusqu'à leur migration vers un contrat
d'emprunt explicite.

## `defer`, panique et nettoyage

Un `defer` qui utilise un emprunt prolonge sa région jusqu'à l'exécution de
l'action différée. Le compilateur vérifie l'ordre LIFO réel des actions : la
source doit rester vivante jusqu'à la dernière utilisation différée.

```janus
val document : Document = new Document(0)
defer delete document

borrow val view = document
defer inspect(view)
```

Cet ordre est valide : `inspect(view)` s'exécute avant `delete document`. Si
les deux `defer` sont inversés, la destruction pourrait précéder l'observation
et le programme est rejeté.

Une panique exécute les `defer` selon les mêmes règles. Elle n'abrège jamais
artificiellement une région si une action de nettoyage peut encore utiliser
l'emprunt. Aucun chemin de panique accepté ne doit lire une valeur déjà
détruite ni la détruire deux fois.

## `Option`, `Result` et `match`

Une observation d'un enum propriétaire peut produire des bindings empruntés
vers le payload actif. Leur mode ne peut pas être plus fort que celui de
l'emprunt sur l'enum et leur région reste incluse dans la branche concernée.

```janus
borrow val current = result
match current {
    Ok(value) => inspect(value),
    Error(error) => report(error)
}
```

Le changement de variante, le déplacement ou la destruction de l'enum est
interdit tant qu'un de ses payloads est emprunté. `?` peut propager une valeur
possédée ou `Copy`, mais ne peut pas faire échapper un payload emprunté.

La première version n'autorise pas `Option[borrow T]`, `Result[borrow T, E]` ni
le stockage général d'un emprunt dans un enum. Une API utilise plutôt un
callback borné jusqu'à l'introduction éventuelle de durées publiques.

## Conteneurs et slices

Une collection peut exposer un élément par emprunt partagé ou mutable sans en
transférer la propriété. Le prêt d'un élément est aussi relié au stockage du
conteneur : toute opération pouvant déplacer ou remplacer cet élément est
interdite pendant la région.

`Slice[T]` et `MutableSlice[T]` sont des vues non propriétaires fondées sur ce
modèle. Elles observent une plage bornée d'un `Array[T]` sans copier son
stockage. `Slice[T]` conserve un emprunt partagé ; `MutableSlice[T]` conserve
un emprunt mutable exclusif et expose `set`. Leur validité dépend du tableau
source : son déplacement, sa destruction, sa mutation directe et toute
réallocation sont rejetés jusqu'à la destruction de la vue.

## Compatibilité avec les mécanismes existants

### Alias et champs `borrow val`

L'alias pointeur local existant devient le sous-ensemble `Ptr[T]` de l'emprunt
partagé général. Son type d'exécution et son absence de destruction ne changent
pas.

Les champs de constructeur `borrow val` et `borrow var` des classes conservent
respectivement un emprunt partagé ou mutable. Leur source doit vivre plus
longtemps que l'instance et le suivi bloque sa destruction ou tout accès
incompatible. Ces champs restent interdits aux structs et une instance qui en
contient ne peut pas s'échapper par retour ou stockage persistant sans contrat
de durée de vie public.

### Frontière native

Les qualificateurs `borrow` de `extern def` conservent leur contrat actuel. Un
paramètre externe est emprunté uniquement pendant l'appel, sauf contrat natif
plus restrictif impossible à exprimer dans cette première version.

Un retour externe `borrow Ptr[T]` reste une vue sur un stockage natif dont
Janus n'est pas propriétaire. Il ne devient pas automatiquement un emprunt sûr
ancré à une valeur Janus et ne peut jamais être consommé ou libéré.

## Diagnostics attendus

Chaque rejet doit identifier :

- la place concernée ;
- la création ou l'utilisation qui maintient l'emprunt actif ;
- l'opération incompatible ;
- lorsque c'est possible, l'endroit où la région peut être raccourcie.

Les diagnostics structurés doivent distinguer au minimum :

- mutation pendant un emprunt partagé ;
- second accès pendant un emprunt mutable ;
- déplacement, consommation ou destruction pendant un emprunt ;
- emprunt survivant à sa source ;
- capture ou retour faisant échapper un emprunt ;
- invalidation par remplacement ou réallocation ;
- appel d'une méthode exigeant une capacité plus forte.

Les suggestions peuvent proposer de limiter la portée, déplacer la dernière
utilisation, employer un callback borné, extraire la valeur avec une opération
consommante ou différer la mutation.

## Exemples valides

Observations partagées :

```janus
val document : Document = new Document(0)
borrow val first = document
borrow val second = document
inspect(first)
inspect(second)
delete document
```

Mutation exclusive terminée avant le déplacement :

```janus
val document : Document = new Document(0)
borrow var editable = document
edit(editable)

val archived : Document = move document
defer delete archived
```

Projections disjointes :

```janus
borrow var cursor = state.cursor
borrow var output = state.output
updateCursor(cursor)
clearOutput(output)
```

## Exemples invalides

Mutation pendant une observation partagée :

```janus
borrow val view = document
document.touch() // interdit : `view` est encore utilisé ensuite
inspect(view)
```

Deux emprunts mutables concurrents :

```janus
borrow var left = document
borrow var right = document // interdit : `left` est toujours actif
edit(left)
edit(right)
```

Destruction d'une source encore empruntée :

```janus
borrow val view = document
delete document // interdit
inspect(view)
```

Capture échappante :

```janus
borrow val view = document
val callback : () => Unit = () => inspect(view)
return move callback // interdit : la closure survivrait à `document`
```

Invalidation d'un élément :

```janus
borrow val item = documents.getBorrowed(usize(0))
documents.push(move another) // interdit : une réallocation est possible
inspect(item)
```

## Fonctionnalités différées

La version actuelle exclut explicitement :

- les retours empruntés mutables ;
- les paramètres de durée de vie écrits dans les signatures publiques ;
- les emprunts stockés de manière générale dans classes, structs, enums,
  conteneurs ou globales ;
- les closures empruntées persistantes ou asynchrones ;
- les emprunts entre threads et un éventuel partage atomique ;
- la preuve générale que deux indices dynamiques désignent des éléments
  distincts ;
- les conversions implicites entre pointeurs bruts et emprunts sûrs.

Ces exclusions pourront être réévaluées après l'utilisation du modèle par la
bibliothèque standard et Janus Studio. Toute extension devra préserver les
invariants d'aliasing sans rendre nécessaire une collecte de mémoire cachée.

## Ordre d'implémentation

1. représenter les régions et places dans l'analyse de propriété ;
2. généraliser l'emprunt partagé et les effets `borrow def` ;
3. ajouter l'emprunt mutable exclusif ;
4. couvrir projections, contrôle de flux, `defer` et invalidations ;
5. prendre en charge paramètres et closures non échappantes ;
6. construire `Slice[T]` et `MutableSlice[T]` sur ces garanties ;
7. migrer les callbacks historiques et valider Janus Studio comme canari.

Les étapes 2 à 7 sont disponibles avec des régions lexicales conservatrices :
une liaison `borrow val` ou `borrow var` reste active jusqu'à la fin de son
bloc. L'analyse plus fine à la dernière utilisation et les projections ne font
donc pas encore partie de cette implémentation.

L'étape 4 protège également les invalidations de la valeur entière : transfert,
destruction, écrasement, méthode mutante, consommation native et réallocation
d'un `Ptr[T]`. Un objet qui contient un champ emprunté prolonge lexicalement
l'emprunt de sa source. Il peut être détruit ou déplacé dans la même portée,
mais ne peut pas être copié dans un autre stockage ni être retourné tant qu'un
contrat de durée de vie explicite n'existe pas.

La fonctionnalité est couverte par les tests positifs et négatifs, les
campagnes sanitizer ainsi que la validation d'une application aval prévues par
la roadmap 1.0.
