# Contrat de `std.validated`

`Validated[T, E]` représente soit une valeur `Valid(T)`, soit un conteneur
`Invalid(ValidatedErrors[E])` garanti non vide par son API. Il sert aux
validations indépendantes pour lesquelles l'utilisateur doit recevoir toutes
les erreurs en une passe, par exemple un formulaire, une configuration ou une
collection d'entrées.

Ce type est applicatif, pas monadique. `map2`, `map3`, `zip` et
`collectValidated` peuvent examiner toutes leurs entrées et accumuler les
erreurs. Le module n'expose volontairement pas `andThen` : si la seconde étape
dépend du succès de la première, ses erreurs ne peuvent pas être produites
lorsque la première échoue. Utilisez alors `andThen` de `std.result`, ou séparez les
contrôles indépendants des étapes dépendantes.

## Surface et ordre

| Opération | Résultat et callback | Ordre |
|---|---|---|
| `valid(value)` | construit `Valid(value)` | aucun parcours |
| `invalid(error)` | alloue un tableau contenant une erreur | erreur unique |
| `invalidFromArray(errors)` | convertit dans `Option` sans copie | `None` pour un tableau vide |
| `map(value, transform)` | transforme seulement `Valid` | callback au plus une fois |
| `mapError(value, transform)` | transforme chaque erreur | indices croissants |
| `zip(left, right)` | produit `ValidatedPair` si les deux valeurs sont valides | erreurs gauche puis droite |
| `map2(left, right, combine)` | appelle `combine` seulement si les deux valeurs sont valides | erreurs gauche puis droite |
| `map3(first, second, third, combine)` | appelle `combine` seulement si les trois valeurs sont valides | erreurs first, second, third |
| `collectValidated(source)` | conserve tous les succès seulement si aucune entrée n'est invalide | ordre de l'itérateur |
| `errorsBorrowed(value, action)` | observe le tableau sans transfert | callback seulement pour `Invalid` |
| `intoErrors(value)` | transfère le tableau dans `Option` | détruit un éventuel succès |

Toutes les entrées sont consommées exactement une fois. Si au moins une entrée
est `Invalid`, `map2` et `map3` détruisent les succès sans partenaire, puis
transfèrent le tableau concaténé. `collectValidated` consomme toujours
l'itérateur entier ; en présence d'une erreur, il détruit tous les succès déjà
collectés. Les callbacks `combine` sont `scoped`, détruites dans tous les cas et
jamais appelées sur un chemin invalide.

L'ordre d'accumulation est stable, y compris lorsqu'une entrée `Invalid` porte
plusieurs erreurs. `ValidatedErrors` a un constructeur `internal` : une
application ne peut donc pas fabriquer le payload du constructeur public
`Invalid`. Elle passe par `invalid(error)` ou `invalidFromArray(errors)`, qui
retourne `None` et détruit le tableau lorsqu'il est vide. Pour migrer un ancien
appel `Validated.Invalid(move errors)`, utilisez cette dernière fonction et
traitez explicitement `None`.

## Allocation et paniques

`invalid` réserve une place. Le wrapper non vide ne provoque aucune copie des
éléments. `map2` et `map3` créent respectivement un tableau
d'une capacité initiale de deux ou trois éléments, puis `Array.extend` agrandit
ce tableau si une entrée transporte davantage d'erreurs. `collectValidated`
démarre avec deux tableaux vides, un pour les succès et un pour les erreurs.

Ces opérations reprennent le contrat d'`Array` : une allocation impossible
déclenche une panique. Le déroulage détruit les tableaux partiellement remplis,
les validations encore détenues, l'itérateur et les callbacks actifs. Aucun
élément propriétaire n'est copié.

## Conversion avec `Result`

`fromResult` est sans perte : `Ok(value)` devient `Valid(value)` et
`Error(error)` devient un conteneur d'une erreur. La conversion inverse ne peut
pas conserver plusieurs erreurs dans `Result[T, E]`. `toResult` transfère donc
la première erreur et détruit toutes les suivantes. Aucun état constructible
par l'API publique sûre ne peut faire paniquer cette conversion. N'utilisez-la
qu'à une frontière qui exige explicitement une erreur unique.

Un code qui déstructure directement `Invalid(errors)` reçoit désormais un
`ValidatedErrors[E]`; sa méthode `intoArray()` permet de retrouver un
`Array[E]` non vide. Les fonctions `errorsBorrowed` et `intoErrors` conservent
leurs signatures historiques basées sur `Array[E]`.

## Exemples

Validation de formulaire, avec accumulation de tous les champs invalides :

```janus
val form : Validated[ValidatedPair[string, int], string] =
    std.validated.zip[string, int, string](
        validateName(name),
        validateAge(age)
    )
```

Construction d'une configuration uniquement lorsque ses trois contrôles
indépendants réussissent :

```janus
val config : Validated[Config, string] =
    std.validated.map3[string, int, bool, string, Config](
        validateHost(host),
        validatePort(port),
        validateTls(tls),
        (checkedHost : string, checkedPort : int, checkedTls : bool) =>
            new Config(checkedHost, checkedPort, checkedTls)
    )
```

Validation exhaustive d'un itérateur :

```janus
val checked : Validated[Array[Entry], string] =
    std.validated.collectValidated[Entry, string](
        inputs.intoIterator()
            .map[Validated[Entry, string]](
                (input : Input) => validateEntry(input)
            )
    )
```

Les mêmes combinateurs, sauf les constructeurs et conversions, sont disponibles
comme méthodes d'extension après `import std.validated`.
