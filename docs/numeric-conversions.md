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

En cas de plusieurs motifs possibles, `checkedCast` choisit dans cet ordre :
`NonFinite`, incompatibilité de signe ou borne, puis perte fractionnaire ou de
précision. `checkedCast` ne renvoie donc jamais une branche `Ok` silencieusement
modifiée.

## Littéraux et évaluation constante

Un suffixe `f` construit directement un `float`; sans suffixe, un littéral à
virgule est un `double` (sauf contexte constant flottant déjà typé). Le suffixe
fait partie du littéral : `1.0ff` et `1.0foo` sont rejetés comme littéraux
`float` invalides.

Les globales constantes utilisant `saturatingCast` ou `truncatingCast` sont
repliées par le compilateur. Leur algorithme est le même que celui du backend
runtime : il n'utilise ni cast C hors plage ni comportement indéfini de l'hôte.
Un `checkedCast` produit le même `Result` pour une entrée constante ou calculée
à l'exécution ; sa construction reste actuellement émise dans l'IR.

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
