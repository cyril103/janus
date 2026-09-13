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

Les formes immédiates sont synchrones et n'allouent aucun état propre. Leurs
callbacks `scoped` sont détruites à la fin de l'appel et peuvent capturer des
emprunts. Les formes suivantes construisent une closure propriétaire :

| Factory | Résultat | Capacité |
|---|---|---|
| `compose(outer, inner)` | `value => outer(inner(value))` | maximum des deux capacités |
| `andThen(first, after)` | `value => after(first(value))` | maximum des deux capacités |
| `partialFirst2(function, first)` | `second => function(first, second)` | capacité de `function` si `first` est `Copy`, sinon `FnOnce` |
| `curry2(function)` | première application produisant une application partielle | première closure `FnOnce`, seconde suivant `partialFirst2` |
| `uncurry2(function)` | applique une fonction curryfiée à `OptionPair[A, B]` | capacité de la première callback |

La première application de `curry2` transfère la callback à la seconde closure.
Elle ne la clone pas et ne crée aucun partage implicite. Chaque appel de
`uncurry2` crée une seconde callback fraîche, appelée une fois et détruite :
sa capacité ne limite donc pas la réutilisation de la première callback.
`OptionPair`, défini dans `std.option`, est la paire propriétaire existante
retenue pour cette API ; ses champs `left` et `right` sont déstructurés par
transfert.

Les surcharges conservent `pure` lorsque les callbacks et les captures ont un
contrat pur. Pour une valeur fixée générique, la borne `Copy` prouve l'absence
de nettoyage observable ; la forme propriétaire reste ordinaire. Pour `uncurry2`,
le second argument doit aussi être `Copy` pour conserver `pure` : sinon, sa
destruction lors d’une panique de la première étape peut être observable. Les capacités
ne sont jamais renforcées. Les signatures explicites sont générées par
`scripts/generate_functional_factories.py` afin de vérifier la matrice complète
sans ajouter d'intrinsèque aux factories.

## Évaluation et nettoyage

Les arguments sont évalués exactement une fois, de gauche à droite, à la
construction. `compose` appelle ensuite `inner` avant `outer` ; `andThen`
appelle `first` avant `after`. Une panique interrompt la composition avant
l'étape suivante. Les résultats intermédiaires sont transférés, sans copie
implicite. La valeur fixée par `partialFirst2` n'est jamais réévaluée.

Une callback possédée exige `move callback` à l'entrée de la factory. Une
fonction nommée représente une nouvelle valeur sans environnement et peut être
fournie directement. Une fonction générique, surchargée ou variadique exige
une lambda explicite pour être utilisée comme valeur ; le point d’entrée
`main` ne peut pas servir de callback. Les ressources capturées sont détruites avec la closure,
y compris lorsqu'elle n'est jamais appelée. Utiliser `defer delete` pour les
closures réutilisables ; appeler une `FnOnce` consomme sa valeur et désarme son
nettoyage différé.

Les captures empruntées restent réservées aux callbacks `scoped` des formes
immédiates. Les factories propriétaires rejettent une callback empruntante
échappante ; elles ne prolongent pas la durée de vie de sa source.

## Coûts

Une fonction nommée utilisée comme valeur, comme une lambda sans capture,
possède un environnement nul et n'alloue pas. Une composition ou une application
partielle utilise un environnement de closure supplémentaire : son état est
une structure stockée directement dans cet environnement, sans objet auxiliaire
alloué. Les environnements que les callbacks possédaient déjà sont transférés.
`curry2` crée un environnement pour la première application, puis un pour la
seconde ; le premier est libéré lors de son appel. `uncurry2` ajoute un
environnement et détruit chaque callback intermédiaire après son appel. Sa
forme ordinaire utilise également une closure de garde temporaire par appel
pour protéger le second argument si la première étape panique ; la surcharge
pure avec second argument `Copy` ne nécessite pas cette garde.

Les allocations utilisent le contrat `malloc`/`free` exposé à LLVM et peuvent
être éliminées lorsqu'après optimisation aucun usage ni nettoyage de panique
ne les fait échapper. Le benchmark `benchmarks/functional_factories.janus`
compare les checksums du helper, d'une lambda manuelle et des appels développés.

La [RFC des capacités d'appel](call-capabilities.md) définit le contrat de
`Fn`, `FnMut` et `FnOnce`.
