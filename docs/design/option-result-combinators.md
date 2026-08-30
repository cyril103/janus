# Contrat des combinateurs `Option` et `Result`

Ce document fixe la surface introduite par l’issue #291. Les fonctions libres
restent la forme canonique et les méthodes d’extension fournissent la même
sémantique pour les chaînes. Toutes les opérations sont monomorphisées et ont
un coût constant hors coût des callbacks et destruction des valeurs.

## Matrice `Option`

| Opération | Receiver/entrées | Callback | Paresse | Coût et propriété |
|---|---|---|---|---|
| `flatten` | consomme `Option[Option[T]]` | aucun | — | `O(1)`, déplace l’option intérieure |
| `filter` | consomme `Option[T]` | `scoped (borrow T) => bool` | appelée seulement pour `Some` | `O(1)`, détruit une valeur refusée |
| `fold` | consomme l’option et un fallback `U` | `scoped (T) => U` | callback seulement pour `Some`; fallback strict | `O(1)`, détruit la branche inactive |
| `contains` | emprunte l’option et la valeur attendue | égalité dérivée | comparaison seulement pour `Some` | `O(1)`, aucun transfert |
| `zip` | consomme gauche puis droite | aucun | — | `O(1)`, produit `OptionPair`; détruit toute branche sans partenaire |
| `map2` | consomme gauche puis droite | `scoped (T, U) => V` | appelée seulement pour deux `Some` | `O(1)`, détruit les branches inactives |
| `inspect` | consomme puis retransmet l’option | `scoped (borrow T) => Unit` | appelée seulement pour `Some` | `O(1)`, contenu non déplacé vers le callback |
| `unwrapOrElse` | consomme l’option | `scoped () => T` | appelée seulement pour `None` | `O(1)`, closure toujours détruite |
| `orElseWith` | consomme l’option | `scoped () => Option[T]` | appelée seulement pour `None` | `O(1)`, closure toujours détruite |
| `toResult` | consomme l’option et une erreur stricte | aucun | erreur stricte | `O(1)`, défini dans `std.result` pour éviter un cycle de modules |

## Matrice `Result`

| Opération | Receiver/entrées | Callback | Paresse | Coût et propriété |
|---|---|---|---|---|
| `flatten` | consomme `Result[Result[T,E],E]` | aucun | — | `O(1)`, déplace le résultat intérieur ou l’erreur extérieure |
| `fold` | consomme le résultat | deux callbacks `scoped` | seule la branche active est appelée | `O(1)`, les deux closures sont détruites |
| `zip` | consomme gauche puis droite | aucun | — | `O(1)`, première erreur gauche-droite; produit `ResultPair` |
| `map2` | consomme gauche puis droite | `scoped (T, U) => V` | appelée seulement pour deux `Ok` | `O(1)`, première erreur gauche-droite |
| `inspect` | consomme puis retransmet | `scoped (borrow T) => Unit` | appelée seulement pour `Ok` | `O(1)`, succès conservé |
| `inspectError` | consomme puis retransmet | `scoped (borrow E) => Unit` | appelée seulement pour `Error` | `O(1)`, erreur conservée |
| `unwrapOrElse` | consomme le résultat | `scoped (E) => T` | appelée seulement pour `Error` | `O(1)`, erreur transférée au callback |
| `transpose` | consomme `Result[Option[T],E]` | aucun | — | `O(1)`, distribue vers `Option[Result[T,E]]` |

## Ordre et lois

Les arguments sont évalués dans l’ordre source. `zip` et `map2` examinent
ensuite la gauche avant la droite. Pour `Result`, la première erreur dans cet
ordre est retournée; l’autre résultat est détruit. Pour `Option`, la première
absence suffit et toute valeur sans partenaire est détruite.

Les tests runtime verrouillent, pour `int` et pour une classe propriétaire
instrumentée : identité et composition de `map`, identités gauche/droite et
associativité de `andThen`, non-appel des callbacks inactifs, et destruction
exactement une fois des branches, fallbacks et closures.

## Exemples de surface

| Fonction | Exemple minimal |
|---|---|
| `std.option.flatten` | `flatten[int](Option.Some(Option.Some(1)))` |
| `std.option.filter` | `value.filter((borrow item) => item > 0)` |
| `std.option.fold` | `value.fold(0, item => item)` |
| `std.option.contains` | `value.contains(42)` |
| `std.option.zip` | `left.zip(right)` |
| `std.option.map2` | `left.map2(right, (a, b) => a + b)` |
| `std.option.inspect` | `value.inspect((borrow item) => println(item))` |
| `std.option.unwrapOrElse` | `value.unwrapOrElse(() => computeDefault())` |
| `std.option.orElseWith` | `value.orElseWith(() => loadAlternative())` |
| `std.result.toResult` | `std.result.toResult(value, error)` |
| `std.result.flatten` | `flatten[int, E](nested)` |
| `std.result.fold` | `value.fold(ok => ok, error => 0)` |
| `std.result.zip` | `left.zip(right)` |
| `std.result.map2` | `left.map2(right, (a, b) => a + b)` |
| `std.result.inspect` | `value.inspect((borrow item) => println(item))` |
| `std.result.inspectError` | `value.inspectError((borrow error) => log(error))` |
| `std.result.unwrapOrElse` | `value.unwrapOrElse(error => recover(error))` |
| `std.result.transpose` | `nested.transpose()` |
