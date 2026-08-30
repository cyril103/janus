# Contrat des pipelines `Iterator`

Ce document fixe la surface étendue par l'issue #293. Un `Iterator[T]` possède
sa source, ses callbacks et l'état minimal de chaque adaptateur. Un adaptateur
ne demande une valeur qu'à l'appel de `next`; un consommateur demande les
valeurs séquentiellement jusqu'à sa fin normale ou son court-circuit.

## Adaptateurs paresseux

| Adaptateur | État conservé | Comportement |
|---|---|---|
| `drop(n)` | compteur et source | détruit au plus les `n` premières valeurs lors de la première demande |
| `chain(other)` | deux sources et un booléen | n'interroge `other` qu'après l'épuisement de la première source |
| `filterMap(transform)` | source et callback | consomme une valeur par appel de callback et produit uniquement les `Some` |
| `takeWhile(predicate)` | source, prédicat et booléen de fin | détruit la première valeur refusée puis ne demande plus rien |
| `skipWhile(predicate)` | source, prédicat et booléen de préfixe | détruit le préfixe accepté et retransmet la première valeur refusée |
| `scan(initial, transform)` | source, état `S`, callback et booléen de fin | prête `borrow var S`, consomme `T`; `None` termine le parcours |

Tous ces adaptateurs occupent `O(1)` hors état utilisateur et sources qu'ils
possèdent. Ils n'allouent aucune collection intermédiaire. La création de
l'objet d'état et des fermetures suit le modèle existant de `map`, `filter` et
`flatMap`. Détruire le pipeline détruit les callbacks, l'état de `scan`, les
sources et toutes les valeurs non produites exactement une fois.

Les prédicats de `takeWhile` et `skipWhile` reçoivent `borrow T`. Cet emprunt est
limité à l'appel : il ne peut ni transférer ni détruire la valeur. La première
valeur refusée par `skipWhile` reste donc disponible et peut être produite sans
copie. `scan` reçoit au contraire la valeur par ownership et doit la transférer
ou la détruire, même lorsqu'il retourne `None`.

## Consommateurs

| Consommateur | Court-circuit | Résultat |
|---|---|---|
| `find(predicate)` | premier `true` | `Option[T]` propriétaire |
| `any(predicate)` | premier `true` | `bool` |
| `all(predicate)` | premier `false` | `bool`; vrai à vide |
| `count()` | aucun | nombre de valeurs détruites |
| `reduce(combine)` | aucun | `Some` de la réduction, `None` à vide |
| `tryFold(initial, combine)` | premier `Error` | accumulateur final ou première erreur |
| `partitionWith(predicate, accepted, rejected)` | aucun | `PartitionResult` des deux builders |

`find`, `any`, `all` et `tryFold` ne demandent aucune valeur après leur résultat
décisif. La destruction du parcours libère immédiatement le suffixe. Les
callbacks sont `scoped` et ne peuvent pas s'échapper. `reduce` et `tryFold`
placent leur accumulateur propriétaire dans un état mobile afin de respecter
l'interdiction de déplacer directement une variable extérieure depuis une
boucle.

`partitionWith` emprunte deux `Builder` mutables explicites. Chaque valeur est
testée une fois puis déplacée vers le builder correspondant; l'ordre relatif
est conservé dans chaque résultat. Les builders restent réutilisables selon
leur propre contrat après l'appel à `result`.

## Collectes faillibles

`collectResult` et `collectOption` vivent dans `std.array_builder`, car
`std.array` dépend déjà de `std.iterator`. Elles créent un `ArrayBuilder`
interne et s'arrêtent respectivement au premier `Error` ou `None`. En cas
d'échec, le builder détruit les succès partiels et le parcours détruit le
suffixe sans demander de nouvelle valeur. En cas de succès, le tableau conserve
l'ordre source.

## Paniques et destruction

Les mêmes règles que pour les autres ressources Janus s'appliquent : inscrivez
avec `defer` toute ressource locale qui doit être libérée pendant une panique.
Les boucles internes des consommateurs enregistrent le parcours actif; les
tests de `tryFold` vérifient qu'une panique de callback détruit l'accumulateur,
la valeur courante, le suffixe, la callback et les états actifs. Les tests sous
sanitizer couvrent aussi les fins normales et les courts-circuits avec des
éléments propriétaires instrumentés.

## Coûts

Chaque valeur est demandée au plus une fois par étage. `drop`, `takeWhile` et
`skipWhile` peuvent demander plusieurs valeurs pour produire leur première
sortie, mais leur coût total reste `O(n)`. Tous les consommateurs sont `O(k)`,
où `k` est le nombre de valeurs effectivement demandé avant la fin ou le
court-circuit. Le benchmark `fallible_sequence_pipeline` exerce une longue
chaîne `drop` → `takeWhile` → `filterMap` → `tryFold`; le benchmark historique
`sequence_pipeline` protège toujours `filter` → `map` → `fold`.
