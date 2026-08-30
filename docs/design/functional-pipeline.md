# Pipeline fonctionnel et `std.functional`

## Syntaxe et précédence

`|>` est un opérateur binaire associatif à gauche. Sa précédence est plus
faible que `||`, ce qui en fait le dernier niveau de la grammaire des
expressions :

```text
pipeline := logical-or ("|>" logical-or)*
```

Ainsi « a || b |> f |> g(c) » signifie « g(f(a || b), c) ». Des parenthèses sont
requises pour appliquer `?` ou une méthode au résultat complet d'un pipeline :
`(result |> normalize)?` et `(value |> normalize).validate()`.

## Désucrage et ordre d'évaluation

La partie droite doit être un nom de fonction, un appel de fonction ou un
appel qualifié. Le désucrage est strictement :

```text
value |> function             => function(value)
value |> function(arguments)  => function(value, arguments)
```

Le parseur produit le même AST d'appel que la forme développée. Le backend
évalue donc la valeur injectée une seule fois, avant les arguments explicites,
puis invoque la fonction. Un nom qualifié ou le receveur syntaxique d'un appel
qualifié ne contient aucun calcul implicite. Toute autre partie droite produit
le diagnostic `pipeline right-hand side must be a function or function call`.

Le pipeline n'ajoute aucune conversion, aucun placeholder et aucun `move`.
Une valeur propriétaire destinée à un paramètre possédant doit conserver la
forme explicite `move value |> consume` ; les contrôles d'emprunt et de
consommation sont ceux de l'appel développé.

## Matrice de propriété de `std.functional`

| Helper | Valeur / callbacks reçus | Résultat et allocation |
|---|---|---|
| `identity` | consomme `T` | retourne le même `T` |
| `constant` | consomme `A` et détruit `B` | retourne `A` |
| `compose` | consomme `A`; callbacks outer et inner `scoped` | applique inner puis outer |
| `andThen` | consomme `A`; callbacks first et after `scoped` | applique first puis after |
| `flip` | consomme `A` et `B`; callback binaire `scoped` | applique immédiatement la callback |
| `tap` | consomme `T`, callback `scoped` empruntant `T` | retourne `T` |

Tous ces helpers sont synchrones et n'allouent aucun état propre. Les callbacks
`scoped` sont détruites à la fin de l'appel et peuvent capturer des emprunts.
Cette surface ne prétend pas retourner des closures composées : le contrat
actuel de `owningCapture` reste réservé aux closures de nettoyage sans argument
appelées explicitement par leur propriétaire englobant.

`curry2` et `uncurry2` ne font pas partie de cette surface minimale. Un
currying propriétaire sûr doit distinguer une fonction réutilisable d'une
fonction affine appelée une seule fois (`FnOnce`); Janus ne possède pas encore
ce contrat, et une implémentation anticipée pourrait dupliquer ou prolonger
incorrectement un propriétaire.
