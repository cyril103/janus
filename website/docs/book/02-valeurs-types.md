<span class="chapter-kicker">CHAPITRE 02 / CONTRATS</span>
# Valeurs et types

## Objectifs

- choisir entre `val` et `var` ;
- connaître les types primitifs courants ;
- effectuer une conversion numérique explicite et interpréter `JANA0013`.

## Immuable ou réassignable

`val` crée une liaison non réassignable ; `var` autorise une nouvelle valeur.

```janus
// doctest: doctest name=values-and-types
def main() : int {
    val depart : int = 40
    var resultat : int = depart
    resultat = resultat + 2
    println(resultat)
    return 0
}
```

Les types usuels incluent `int`, `uint`, `long`, `ulong`, `float`, `double`, `byte`, `char`, `bool`, `string`, `isize`, `usize` et `Unit`. Leur taille est définie par le langage.

## Inférence des variables locales

Une `val` ou `var` locale initialisée peut omettre son type si l'expression
produit un type unique. Une `var` conserve ce type pour toutes ses affectations.

```janus
// doctest: doctest name=inferred-locals
def answer() : int { return 42 }
def main() : int {
    val result = answer()
    var doubled = result + result
    doubled = doubled + 1
    return doubled - 85
}
```

Les globales, champs, paramètres et retours restent annotés. Ajoutez aussi une
annotation pour `null()` sans type, une collection vide, des branches de types
différents ou un générique insuffisamment contraint. Les littéraux seuls
gardent leurs types par défaut (`int`, `double`, `char`, `bool` et `string`).

## Conversions explicites

Janus ne transforme pas silencieusement un `int` en `double` :

```janus
val total : int = 5
val ratio : double = double(total) / 2.0
```

Cette contrainte rend les changements de représentation visibles lors de la relecture.
Le cast ordinaire `T(value)` reste valide lorsqu'il peut perdre de la plage ou
de la précision, mais le compilateur émet alors `JANA0013`.

Après avoir vérifié la plage — ou lorsqu'une constante est volontairement
arrondie — `numericCast[T](value)` exprime que cette perte est acceptée :

```janus
// doctest: doctest name=numeric-cast
def main() : int {
    val source : double = 1.25
    val compact : float = numericCast[float](source)
    return if compact == numericCast[float](1.25) { 0 } else { 1 }
}
```

`numericCast` ne contrôle aucune borne à l'exécution. Il supprime uniquement
l'avertissement, et ne doit donc pas remplacer une validation nécessaire sur
une donnée externe.

!!! note "Entiers"
    Les littéraux entiers sans cast ont le type `int`. Consultez la référence avant de dépendre des règles de débordement ou des limites d’un type.

## Exercice

Déclarez un nombre de secondes immuable, calculez le nombre de minutes en `double`, puis affichez-le.

??? success "Correction"
    ```janus
    def main() : int {
        val secondes : int = 90
        val minutes : double = double(secondes) / 60.0
        println(minutes)
        return 0
    }
    ```

<div class="lesson-nav"><a href="../01-premiers-pas/">← Premiers pas</a><a href="../03-controle-fonctions/">Contrôle et fonctions →</a></div>
# Constantes évaluées à la compilation

`const` est distinct de `val` et `var` : une constante doit être calculable par
le compilateur, tandis qu'un `val` peut être initialisé à l'exécution et qu'un
`var` reste réaffectable.

```janus
const width : int = 80
const height : int = 25
const pixels : int = width * height

const def align(value : usize, boundary : usize) : usize {
    return ((value + boundary - usize(1)) / boundary) * boundary
}

staticAssert(pixels > 0, "dimensions invalides")
```

Une `const def` est aussi une fonction ordinaire appelable à l'exécution. Dans
la première version de l'évaluateur, son corps constant contient exactement un
`return` et n'est pas générique. Les appels récursifs sont bornés à 128 niveaux
et une évaluation à 10 000 appels; le dépassement est diagnostiqué comme une
limite de ressources, indépendamment d'une erreur de programme.

Les expressions admises sont les littéraux, références à d'autres constantes,
conversions numériques explicites, opérateurs arithmétiques/logiques et de
comparaison, `if`, ainsi que `match` exhaustif sur un enum sans ressource. Les
booléens, entiers, flottants finis, caractères, chaînes statiques, structs sans
ressource et enums sans ressource sont admissibles au niveau module. Les
tableaux, collections, pointeurs, classes propriétaires, valeurs avec
destructeur et génériques constants sont volontairement exclus tant que leur
identité, stockage statique et destruction ne sont pas spécifiés. Une constante
locale suit les mêmes règles scalaires mais, dans cette version, ne référence
pas encore une autre constante locale.

Les entiers suivent la largeur du type Janus, non celle de la machine hôte : un
débordement, une division par zéro ou une conversion hors plage est une erreur.
`isize` et `usize` sont actuellement définis sur 64 bits pour toutes les cibles
prises en charge, ce qui rend la compilation croisée indépendante de l'hôte.
Les flottants utilisent IEEE-754 (`float` binaire32, `double` binaire64); les
résultats non finis sont refusés. Les opérations bit à bit et décalages ne font
pas partie de la syntaxe Janus actuelle et ne sont donc pas admises dans cette
première version.

Une constante publique appartient à l'interface : son initialiseur normalisé,
son type, la version de l'évaluateur et la cible participent au cache. Les
constantes scalaires de module sont substituées dans le code LLVM sans stockage
global. `staticAssert` ne produit aucun code et exige un booléen constant.
