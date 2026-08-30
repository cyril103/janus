# Comprendre `derives`, de `Copy` à `Debug`

## Prérequis

- avoir lu les chapitres sur les [types](../book/04-modeliser-donnees.md) et les [traits/dérivations](../book/08-traits-derivations.md) ;
- disposer de Janus 0.23.1.

## Résultat

Nous allons créer une clé métier copiable, comparable, hachable et affichable, puis l’utiliser dans un `HashSet` sans écrire manuellement les quatre comportements.

## 1. Déclarer la valeur

```janus
struct Coordinate(val row : int, val column : int)
derives Copy, Equality, Hashing, Debug {}
```

Cette clause est une demande au compilateur, pas une liste décorative :

- `Copy` prouve que dupliquer les deux entiers est sûr ;
- `Equality` compare `row`, puis `column` ;
- `Hashing` produit un hash cohérent avec cette égalité ;
- `Debug` génère une représentation structurée.

Le champ `row` ou `column` qui ne prendrait pas en charge une capacité ferait échouer la dérivation correspondante avec un chemin précis dans le diagnostic.

## 2. Observer les capacités

```janus
// doctest: doctest name=tutorial-derive-basic
struct Coordinate(val row : int, val column : int)
derives Copy, Equality, Hashing, Debug {}

def main() : int {
    val first : Coordinate = new Coordinate(4, 2)
    val copy : Coordinate = first
    if first != copy {
        return 1
    }
    debug(copy)
    return 0
}
```

`first` reste utilisable après l’affectation grâce à `Copy`. `==` et `!=` viennent d’`Equality`. `debug(copy)` écrit `Coordinate { row: 4, column: 2 }` à des fins de diagnostic.

## 3. Hacher la valeur

```janus
// doctest: doctest name=tutorial-derived-hashset
import std.hashing
import std.hashset

struct Coordinate(val row : int, val column : int)
derives Copy, Equality, Hashing, Debug {}

def main() : int {
    val hashing : DerivedHashing[Coordinate] =
        new DerivedHashing()
    defer delete hashing

    val visited : HashSet[Coordinate, DerivedHashing[Coordinate]] =
        new HashSet(
            8,
            hashing
        )
    defer delete visited

    val start : Coordinate = new Coordinate(4, 2)
    visited.add(start)
    if visited.contains(start) {
        return 0
    }
    return 1
}
```

`DerivedHashing[T]` est la stratégie standard qui relie la capacité intrinsèque à `HashSet` et `HashMap`. Elle est elle-même propriétaire et vit au moins aussi longtemps que la collection qui l’utilise. Grâce à l’ordre inverse des `defer`, `visited` est détruit avant `hashing`.

## Pourquoi certaines dérivations échouent

```janus
// doctest: incomplete
class Connection() {}

// Refusé : une classe propriétaire ne peut pas être dupliquée.
class Wrong() derives Copy {}

// Refusé : Hashing exige Equality dans la même clause.
struct Incomplete(val value : int) derives Hashing {}

// Refusé : le champ propriétaire empêche Copy.
struct Holder(val connection : Connection) derives Copy {}
```

Une classe peut dériver `Equality`, `Hashing` et `Debug` si tous ses champs sont éligibles. Cette égalité est structurelle : deux objets différents peuvent être égaux si leur contenu l’est.

## Vérifier

```bash
janus fmt
janus check
janus run
```

## Prolongements

- Remplacez `Coordinate` par un enum avec plusieurs variantes.
- Utilisez-le comme clé de `HashMap`.
- Retirez une capacité pour observer quelles opérations cessent de compiler.
- Lisez les règles détaillées dans la [référence des dérivations](../book/08-traits-derivations.md).
