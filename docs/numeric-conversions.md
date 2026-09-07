# Conversions numériques explicites

Janus sépare quatre intentions. `numericCast[T]` conserve la conversion native
historique et suppose que l'appelant en respecte les préconditions.
`checkedCast[T]`, `saturatingCast[T]` et `truncatingCast[T]` ont au contraire
une sémantique définie pour toute valeur numérique. Les trois primitives de
politique sont des builtins du compilateur ; `import std.numeric` fournit
`NumericCastError` et `import std.result` fournit `Result`.

```janus
import std.numeric
import std.result

val exact : Result[ubyte, NumericCastError] = checkedCast[ubyte](value)
val bounded : ubyte = saturatingCast[ubyte](value)
val lowBits : ubyte = truncatingCast[ubyte](value)
val ratio : float = 0.5f
```

## Matrice des politiques

min(T) et max(T) désignent les bornes finies de `T`. Une conversion
flottante emploie l'arrondi IEEE 754 au plus proche, liens vers le pair. Le
zéro signé est conservé entre flottants.

| Source → cible | `checkedCast[T]` | `saturatingCast[T]` | `truncatingCast[T]` |
|---|---|---|---|
| entier → entier | `Ok` seulement si la valeur est représentable | clamp dans [min(T), max(T)] | bits de poids faible, modulo `2^largeur(T)` ; extension usuelle si la cible est plus large |
| entier → flottant | `Ok` seulement après aller-retour exact | arrondi IEEE fini | identique à la politique saturante |
| flottant → entier | exige une valeur finie, dans la plage et intégrale | troncature vers zéro puis clamp | troncature vers zéro puis clamp |
| flottant → flottant | exige une valeur finie et un aller-retour exact lors d'un rétrécissement | arrondi IEEE, avec clamp aux bornes finies | identique à la politique saturante |

| Cas limite | `checkedCast` | `saturatingCast` / `truncatingCast` |
|---|---|---|
| au-dessus de max(T) | `Overflow` | max(T) |
| sous min(T) signé | `Underflow` | min(T) |
| négatif vers non signé | `IncompatibleSign` | `0` pour saturation ; modulo pour entier→entier tronquant |
| fraction vers entier | `FractionalLoss` | partie fractionnaire supprimée vers zéro |
| entier ou flottant fini non représentable exactement par un flottant | `PrecisionLoss` | valeur arrondie |
| `NaN` | `NonFinite` | `0` ou `+0.0` |
| `+∞` / `-∞` | `NonFinite` | max(T) / min(T) ; pour une cible flottante, ±maximum fini |
| `+0.0` / `-0.0` entre flottants | `Ok`, signe conservé | signe conservé |

Pour un rétrécissement flottant, les bornes sont les valeurs finies exactes de
la cible, comparées dans le type source avant toute conversion ou saturation.
Ainsi, le premier voisin `double` au-dessus du maximum fini de `float` produit
`Overflow`, et le premier voisin sous son opposé produit `Underflow`. Les
voisins encore dans cet intervalle mais arrondis restent classés
`PrecisionLoss`. `NaN` et les deux infinis restent toujours `NonFinite`.

```janus
val source : double = 3.402823466385289e38 // voisin après float.max
val result = checkedCast[float](source)     // Error(Overflow)
```

Cette garantie repose sur les formats IEEE 754 `float` (binary32) et `double`
(binary64) pris en charge par Janus. Elle ne distingue pas les valeurs normales
des sous-normales tant qu'elles restent finies et dans la plage : une valeur
arrondie dans cette zone est diagnostiquée uniformément par `PrecisionLoss`.

En cas de plusieurs motifs possibles, `checkedCast` choisit dans cet ordre :
`NonFinite`, incompatibilité de signe ou borne, puis perte fractionnaire ou de
précision. `checkedCast` ne renvoie donc jamais une branche `Ok` silencieusement
modifiée.

## Littéraux et évaluation constante

Un suffixe `f` construit directement un `float`; sans suffixe, un littéral à
virgule est un `double` (sauf contexte constant flottant déjà typé). Le suffixe
fait partie du littéral : `1.0ff` et `1.0foo` sont rejetés comme littéraux
`float` invalides.

Les valeurs sous-normales finies sont conservées, jusqu'à la plus petite valeur
IEEE 754 représentable (`1.40129846e-45f` pour `float` et
`4.9406564584124654e-324` pour `double`). La conversion est indépendante de la
locale : le séparateur décimal reste toujours `.`. Une valeur qui s'arrondit à
zéro est refusée avec `JPAR0002`; une valeur qui dépasserait la borne finie est
refusée avec `JPAR0003`. Les plateformes prises en charge doivent donc fournir
une conversion décimale correctement arrondie pour leurs types IEEE 754 ; Janus
l'effectue dans une locale C dédiée sans modifier la locale du processus.

Les globales constantes utilisant `saturatingCast` ou `truncatingCast` sont
repliées par le compilateur. Leur algorithme est le même que celui du backend
runtime : il n'utilise ni cast C hors plage ni comportement indéfini de l'hôte.
Un `checkedCast` produit le même `Result` pour une entrée constante ou calculée
à l'exécution ; sa construction reste actuellement émise dans l'IR.

## Diagnostic des casts natifs entier vers flottant

`float` et `double` disposent respectivement de 24 et 53 bits de précision
significative IEEE 754, bit implicite compris. JANA0013 compare cette précision
au domaine entier statique complet de la source, signe compris, et non à la
seule largeur de stockage :

| Source | vers `float` | vers `double` |
|---|---|---|
| `byte`, `ubyte`, `short`, `ushort` | exact sur tout le domaine | exact sur tout le domaine |
| `int`, `uint` | JANA0013 | exact sur tout le domaine |
| `long`, `ulong` | JANA0013 | JANA0013 |
| `isize`, `usize` sur cible 32 bits | JANA0013 | exact sur tout le domaine |
| `isize`, `usize` sur cible 64 bits | JANA0013 | JANA0013 |

Ainsi, `2^24` est exactement représentable par un `float`, mais `2^24 + 1`
est arrondi ; la même frontière vaut `2^53` pour un `double`. Comme le
diagnostic raisonne sur le type et non sur le suivi de plage d'une variable,
même une variable dont la valeur courante est exactement représentable avertit
si une autre valeur de son type pourrait perdre de la précision. Utilisez alors
une politique qui exprime l'invariant :

```janus
import std.numeric
import std.result

val source : int = 16777217
val exact : Result[float, NumericCastError] = checkedCast[float](source)
```

Un cast direct d'un littéral entier reste traité selon les règles contextuelles
existantes et ne produit pas ce warning ; JANA0013 ne tente donc pas encore de
distinguer chaque littéral autour de ces frontières. Pour les casts de
variables risqués, `janus check --deny-warnings` refuse le programme.

## Choisir une primitive

- `checkedCast` pour refuser toute altération et expliquer l'échec ;
- `saturatingCast` pour les compteurs, pixels et limites qui doivent rester
  bornés ;
- `truncatingCast` pour les formats binaires et pertes explicitement voulues ;
- `numericCast` seulement lorsque la plage est déjà prouvée ou lorsque les
  règles natives constituent délibérément le contrat.

`numericCast` ne vérifie rien à l'exécution. En particulier, son opérande doit
être fini et représentable avant une conversion flottant→entier ; les trois
primitives de politique n'ont pas cette précondition.
