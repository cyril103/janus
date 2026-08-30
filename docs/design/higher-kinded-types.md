# Évaluation des types de rang supérieur

Statut : RFC d'évaluation pour l'issue #302 ; recommandation **différer**.

Cette RFC part de la première version implémentée des
[types associés](associated-types.md). Elle ne réserve aucune API de la
bibliothèque standard et ne rend stables ni `Functor`, ni `Applicative`, ni
`Monad`.

## Résumé de la décision

Janus a un besoin plausible de paramètres constructeurs de type pour écrire un
algorithme comme `traverse` une seule fois pour `Option[_]` et
`Result[_, E]`. En revanche, les opérations homonymes de la bibliothèque
actuelle n'ont pas un contrat de propriété commun : `Array.map` et
`PersistentList.map` empruntent leur source, `Option.map` et `Result.map` la
consomment, tandis que `Iterator.map` la consomme et conserve sa callback pour
une exécution paresseuse. Une interface `Functor` unique masquerait aujourd'hui
ces différences ou demanderait plusieurs variantes.

La recommandation est donc de ne pas implémenter les HKT maintenant. Le modèle
minimal décrit ci-dessous reste une piste expérimentale, à rouvrir lorsqu'au
moins deux algorithmes aval substantiels exigent de reconstruire un constructeur
abstrait. `traverse` constitue le premier candidat, pas une preuve suffisante à
lui seul.

## Périmètre étudié

Un type ordinaire a le kind `*`. Un constructeur unaire comme `Option` a le
kind `* -> *`; `Result` a le kind `* -> * -> *`. La syntaxe de travail est :

```janus
trait Functor[F[_]] {
    consume def map[A, B](
        value : F[A],
        scoped transform : (A) => B
    ) : F[B]
}

// Le premier paramètre varie, le type d'erreur est fixé.
class ResultFunctor[E]() extends Functor[Result[_, E]] { ... }
```

`_` désigne ici une place de constructeur, pas un type inféré persistant. Cette
première étape éventuelle serait limitée aux kinds d'ordre 1, aux arguments de
constructeur explicites et à l'application complète `F[T]`. Elle exclurait :

- les variables de kind arbitraire (`K` dont le kind serait lui-même générique) ;
- l'unification d'ordre supérieur et l'inférence de `F` à travers plusieurs
  applications imbriquées ;
- les lambdas de types et la currification implicite de constructeurs ;
- les implémentations globales ou chevauchantes de typeclasses.

Une implémentation de trait passerait par une valeur témoin explicite, comme
`ResultFunctor[E]`. Le modèle actuel de Janus attache en effet un trait à une
classe et ne possède pas d'instance globale de trait. Attacher
`Functor[Result[_, E]]` à chaque `Result[T, E]` rendrait l'implémentation
artificiellement dépendante de `T` et compliquerait la cohérence.

## Cas d'usage observés

### Transformation conservant la famille

La stdlib possède des `map` sur `Option`, `Result`, `Validated`, `Iterator`,
`Array` et `PersistentList`. Le patron de types `F[A] -> F[B]` est commun, mais
pas le comportement :

| Famille | Source | Callback | Évaluation |
|---|---|---|---|
| `Option`, `Result`, `Validated` | consommée | `scoped`, consommatrice | immédiate |
| `Iterator` | consommée | possédée par le résultat | paresseuse |
| `Array`, `PersistentList` | empruntée | `scoped`, emprunte l'élément | immédiate |

Un `Functor[F]` propriétaire factoriserait la première ligne, mais pas les deux
autres sans copie, clone implicite ou perte de paresse. Il faudrait au minimum
des contrats distincts (`OwnedFunctor`, `BorrowFunctor`, et un contrat de
callback échappante). Ce cas démontre l'expressivité des HKT, mais ne justifie
pas encore leur coût.

### `Foldable` et parcours

`Array`, `PersistentList` et `Iterator` dupliquent la forme générale de `fold`.
Le résultat est pourtant un type ordinaire, pas une reconstruction `F[B]`.
Après migration de `Iterable[T]`/`IntoIterable[T]` vers un type associé `Item`,
un algorithme libre peut prendre un parcours et un accumulateur sans connaître
le constructeur de la collection. `Foldable[F]` n'est donc pas nécessaire pour
ce besoin Janus.

La différence emprunt/consommation reste observable : un parcours emprunté
exige `Item <: Copy` dans le contrat actuel, tandis qu'un `IntoIterable`
transfère les valeurs. Les HKT ne résolvent pas cette différence d'ownership.

### Builders et collections

`Builder[T, C]` relie déjà un élément au résultat concret, et
`Iterator.collectWith[C, B <: Builder[T, C]]` est générique. Des types associés
`Builder.Item` et `Builder.Output` supprimeraient même ces paramètres transmis
partout. Aucun HKT n'est requis tant que le builder produit un seul type final.

Un HKT devient utile seulement pour une famille de builders devant produire
`C[T]` pour plusieurs `T` dans le même algorithme. Aucun builder actuel de la
stdlib n'a ce besoin. Les types associés sont donc l'alternative retenue ici.

### Traversée faillible : algorithme substantiel

`std.array_builder` contient deux boucles séparées, `collectOption` et
`collectResult`. Elles épuisent un `Iterator`, accumulent les succès, s'arrêtent
au premier résidu et détruisent correctement le préfixe partiel ainsi que le
suffixe. La différence de constructeur empêche d'exprimer aujourd'hui leur
algorithme commun.

La généralisation utile n'est pas une hiérarchie académique : elle permettrait
à un validateur aval de choisir entre arrêt à la première erreur (`Result`),
absence (`Option`) et, avec un contrat séparé, accumulation (`Validated`) sans
réécrire la boucle et ses chemins de nettoyage.

Une surface expérimentale possible serait :

```janus
trait Applicative[F[_]] {
    def pure[A](value : A) : F[A]
    def map2[A, B, C](
        left : F[A],
        right : F[B],
        scoped combine : (A, B) => C
    ) : F[C]
}

def traverseArray[F[_], A, B, AF <: Applicative[F]](
    values : Iterator[A],
    borrow applicative : AF,
    scoped transform : (A) => F[B]
) : F[Array[B]] {
    // Une seule boucle, avec destruction du builder, du suffixe et des effets
    // non retenus sur chaque sortie anticipée.
    ...
}
```

Cette signature montre aussi les problèmes restant à résoudre. `map2` strict
ne peut pas garantir le court-circuit de `Option` et `Result` si son argument
droit est déjà évalué. Il faut une primitive paresseuse ou une opération de
branche proche de `Try`. `Validated`, qui accumule les erreurs, n'obéit pas au
même contrat. Le témoin doit enfin préciser s'il est emprunté, consommé ou
sans état. Le prototype de kind ne prétend pas résoudre ces questions.

## `Functor`, `Applicative`, `Monad` et `Traversable`

- `Functor` est techniquement exprimable, mais doit être scindé selon ownership
  et durée de vie de la callback.
- `Applicative` est la capacité minimale intéressante pour `traverse`; l'ordre
  d'évaluation, le court-circuit et la destruction doivent faire partie du
  contrat, pas seulement des lois algébriques.
- `Monad` ajouterait `flatMap : (F[A], (A) => F[B]) -> F[B]`. Les implémentations
  actuelles d'`Option`, `Result` et `Iterator` diffèrent notamment par paresse et
  capture de callback. Aucune surface commune n'est proposée.
- `Traversable` demande deux constructeurs (`T[_]` pour la structure et `F[_]`
  pour l'effet) ainsi qu'un ordre déterministe. C'est le test d'expressivité le
  plus exigeant, mais la stdlib ne possède aujourd'hui qu'un cas spécialisé sur
  `Iterator` vers `Array`.

Avant toute API stable, chaque implémentation devrait tester identité et
composition pour `Functor`, identité/homomorphisme/interchange pour
`Applicative`, et identités/associativité pour `Monad`, en plus des invariants
Janus de destruction exactement une fois. Une loi mathématique seule ne suffit
pas si elle change l'ordre des effets ou la durée des emprunts.

## Prototype de typage et de monomorphisation

Le modèle exécutable
[`scripts/prototype_higher_kinded_types.py`](../../scripts/prototype_higher_kinded_types.py)
représente trois notions : déclaration de constructeur, référence partiellement
appliquée et kind sous forme d'une arité. Il vérifie `Option[_]`, `Iterator[_]`,
`Array[_]` et `Result[_, IoError]` contre `* -> *`, puis construit des clés de
spécialisation pour `map` et `traverse`.

Exécution de référence :

```text
$ python3 scripts/prototype_higher_kinded_types.py
6 kind checks, 5 unique specializations from 7 requests
error: type constructor 'Result[_,_]' has kind * -> * -> *; expected * -> *; fix one additional parameter to a concrete type and keep '_' for the varying one, for example Result[_, E]
error: type constructor 'Option[int]' has kind *; expected * -> *
```

| Mesure | Résultat | Interprétation |
|---|---:|---|
| formes nouvelles du modèle | 3 | kind, déclaration et référence de constructeur |
| vérifications de kind | 6 | 4 succès, 2 diagnostics bornés |
| demandes de monomorphisation | 7 | inclut 2 demandes répétées |
| spécialisations uniques | 5 | déduplication par constructeur partiel et types concrets |

Le contrôle de kind visite chaque nœud de type une fois : `O(n)` en taille de
signature, avec une pile bornée par sa profondeur. Le registre de prototype
crée au plus une spécialisation par tuple canonique
`(fonction, constructeurs partiels, types concrets)`. Son nombre peut néanmoins
croître comme le produit des constructeurs et arguments concrets réellement
appelés ; les HKT ne suppriment donc pas l'explosion propre à la
monomorphisation.

Ce prototype ne modifie ni le parseur ni le backend Janus. Ses tests verrouillent
l'application partielle, le diagnostic de kind et la déduplication des clés. Il
mesure la mécanique proposée, pas le coût d'un compilateur complet ni son temps
de compilation.

## Impacts sur le compilateur et les outils

### Inférence et diagnostics

La première version devrait exiger `F` et tout argument fixé explicitement aux
frontières publiques. L'inférence locale peut déduire `A` dans `F[A]` seulement
après résolution de `F`; elle ne doit pas chercher une décomposition arbitraire
d'un type concret. Cette restriction conserve un problème de premier ordre.

Les diagnostics doivent afficher le kind trouvé et attendu, conserver les
arguments fixés, et proposer le trou manquant. Une limite de profondeur et un
ensemble de références actives doivent borner alias cycliques et applications
imbriquées, comme pour la normalisation des types associés.

### Cohérence

Les valeurs témoins utilisent les règles nominales existantes : une classe
implémente un trait une fois et le choix du témoin est explicite à l'appel. Il
n'y a donc pas de recherche globale d'instance. Si Janus ajoutait plus tard des
implémentations implicites, leur cohérence et leurs chevauchements devraient
faire l'objet d'une RFC distincte.

### Monomorphisation, mangling et ABI

Le backend doit substituer d'abord les paramètres ordinaires dans les arguments
fixés du constructeur, puis appliquer celui-ci. La clé canonique doit inclure
le symbole nominal du constructeur, la position de chaque trou et les types
fixés normalisés. `Result[_, E1]` et `Result[_, E2]` ne peuvent partager une clé.

Le mangling actuel concatène les noms des arguments génériques. Il faudrait un
encodage délimité et versionné des kinds, trous et applications pour éviter les
collisions. Toute exposition de ce mangling dans un symbole public serait un
changement d'ABI et suivrait le contrat de stabilité habituel. Aucun symbole
HKT ne doit entrer dans l'ABI stable pendant l'expérimentation.

### Cache incrémental

L'empreinte d'interface doit contenir : kind déclaré, constructeur nominal,
arguments fixés normalisés, positions des trous et contraintes. Changer
`Result[_, E]` en `Result[E, _]` invalide les consommateurs, même si le texte du
trait ne change pas ailleurs. Les clés de spécialisation utilisent la même
forme canonique afin que cache et mangling ne divergent pas.

### Documentation et LSP

L'index d'API doit distinguer `type` et `type-constructor`, publier le kind et
lier chaque application au constructeur nominal. Survol, complétion et aide de
signature affichent `F : * -> *`; une erreur sur `F[A]` souligne l'application,
pas seulement `A`. Renommage et recherche de références incluent les usages
partiels comme `Result[_, E]`. Le formateur conserve explicitement `_`.

### Temps de compilation

Le kind checking linéaire est faible devant l'analyse existante. Le risque vient
de l'inférence exploratoire et du nombre de spécialisations. Une expérimentation
devrait donc mesurer séparément temps de résolution, nombre de clés uniques,
octets LLVM et taux de réutilisation du cache sur une matrice d'au moins trois
constructeurs et cinq types d'éléments. Aucun budget chiffré honnête ne peut être
fixé avant un prototype intégré.

## Ownership

Un kind décrit la forme d'un type, pas la propriété de ses valeurs. Chaque
méthode de trait doit encore fixer :

- si `F[A]` est emprunté ou consommé ;
- si `A` est emprunté ou transféré à la callback ;
- si la callback est `scoped` ou possédée par un résultat paresseux ;
- l'ordre de destruction des branches inactives et résultats partiels ;
- les contraintes `Copy`, `Shared` ou de durée de vie nécessaires.

Une application partielle ne peut capturer un emprunt de type ou contourner sa
durée de vie. La normalisation de `F[A]` doit précéder la validation d'ownership,
afin que celle-ci voie toujours un type concret. Un échec de kind ne doit lancer
ni analyse de déplacement ni génération de nettoyage sur une forme incomplète.

## Alternatives

| Solution | Ce qu'elle couvre | Limite | Décision actuelle |
|---|---|---|---|
| Types associés seuls | `Try.Output`, `Try.Residual`, `Builder.Output`, élément d'un parcours | ne peut pas exprimer « même famille, nouvel élément » sans type associé générique | préférée pour `Try`, builders et fold |
| Traits concrets par famille | contrats précis pour `Option`, `Result`, `Iterator` | duplication des algorithmes inter-familles | préférée tant que les contrats d'ownership divergent |
| Génération de code ou macros futures | duplique mécaniquement `collectOption`/`collectResult` | diagnostics indirects, surface et nettoyage toujours répétés | acceptable pour quelques familles fermées, pas de macro ajoutée ici |
| Fonctions d'extension | chaînes ergonomiques et dispatch statique | aucune abstraction sur le constructeur | préférée pour les combinateurs propres à chaque famille |
| Types associés génériques `type Applied[T]` | encode une famille dans un témoin | réintroduit kinds, application et normalisation sous une autre syntaxe | à traiter comme HKT, pas comme raccourci simple |
| HKT explicites | `traverse` et transformations inter-familles réelles | coût transversal compilateur/outils et contrats d'ownership non unifiés | différée |

## Conditions de réouverture

La décision pourra être revue lorsque toutes les conditions suivantes seront
réunies :

1. au moins deux algorithmes substantiels de la stdlib ou de projets aval sont
   maintenus en plusieurs versions uniquement faute de `F[_]` ;
2. les variantes propriétaire, empruntée et paresseuse de `map` ont des noms et
   contrats distincts validés par des tests de destruction ;
3. un prototype intégré mesure diagnostics, temps de compilation, taille LLVM
   et réutilisation du cache sur ces algorithmes ;
4. mangling, index d'API et LSP partagent une représentation canonique testée ;
5. les lois proposées sont accompagnées d'invariants d'ordre, de court-circuit
   et d'ownership propres à Janus.

Jusque-là, aucune surface `Functor`, `Applicative`, `Monad`, `Foldable` ou
`Traversable` n'entre dans la stdlib stable.
