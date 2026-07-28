# Gérer une erreur sans valeur sentinelle

## Prérequis

- connaître fonctions et `match` ;
- Janus 0.6.0 ;
- les modules standard `std.option` et `std.result`.

## Résultat

Une conversion typée qui double une valeur valide et propage une erreur sans exception ni code magique.

## 1. Construire un `Result`

```janus
import std.result

val ok : Result[int, string] = Result.Ok[int, string](21)
val failed : Result[int, string] =
    Result.Error[int, string]("mesure absente")
```

Le type encode à la fois la réussite `int` et l’erreur `string`.

## 2. Propager avec `?`

Extrait de l’exemple réel [`examples/try_operator.janus`](https://github.com/cyril103/janus/blob/v0.6.0/examples/try_operator.janus) :

```janus
def convertResult(input : Result[int, string]) : Result[double, string] {
    val value : int = input?
    return Result.Ok[double, string](double(value) * 2.0)
}
```

Si `input` est `Error`, `?` quitte immédiatement `convertResult` avec cette erreur. Sinon, il extrait l’`int`.

## 3. Traiter toutes les variantes

```janus
// doctest: doctest name=result-pipeline
import std.result

def convertResult(input : Result[int, string]) : Result[double, string] {
    val value : int = input?
    return Result.Ok[double, string](double(value) * 2.0)
}

def main() : int {
    val result : Result[double, string] =
        convertResult(Result.Ok[int, string](21))
    return match result {
        Ok(value) => 0,
        Error(message) => 1
    }
}
```

Le `match` est exhaustif : aucune erreur n’est silencieusement convertie en succès.

## Vérifier

```bash
janus check
janus run
```

Le code de sortie vaut `0`. Remplacez `Ok` par `Result.Error[int, string]("échec")` : le code doit valoir `1`.

## Option ou Result ?

- `Option[T]` : l’absence suffit à expliquer le cas (`Some` / `None`) ;
- `Result[T, E]` : l’appelant a besoin d’une erreur (`Ok` / `Error`).

## Prolongements

- écrivez l’équivalent avec `Option[int]` ;
- retirez `?` et traitez l’entrée avec `match` ;
- parsez du texte avec `std.text`, dont les fonctions retournent `Result[T, ParseError]`.
