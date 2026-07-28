<span class="chapter-kicker">CHAPITRE 05 / CONTRÔLER LES SORTIES</span>
# Erreurs et propriété

## Objectifs

- distinguer `Option[T]` et `Result[T, E]` ;
- propager une absence ou une erreur avec `?` ;
- raisonner sur `move`, `delete` et `defer`.

## Absence et erreur typées

```janus
import std.option
import std.result

def convert(input : Option[int]) : Option[double] {
    val value : int = input?
    return Option.Some[double](double(value) + 1.0)
}
```

`Option` représente une valeur présente ou absente. `Result` distingue un succès `Ok` d’une erreur `Error`. L’opérateur `?` retourne tôt dans une fonction de type compatible.

## Ressources possédées

```janus doctest name=ownership
class Resource(val identifier : int) {
    destructor {
        println(identifier)
    }
}

def main() : int {
    val resource : Resource = new Resource(42)
    defer delete resource
    println("utilisation")
    return 0
}
```

`new` crée une valeur possédée. `delete` déclenche sa destruction ; `defer` garantit l’action à la sortie de la portée. `move` transfère explicitement la propriété lorsque le type l’exige. Une valeur déplacée ne doit plus être utilisée.

!!! danger "Durée de vie"
    Une closure possédée et les collections allouées sont aussi des ressources. Suivez les exemples canoniques et libérez-les dans l’ordre inverse de leurs dépendances.

## Exercice

Écrivez une fonction qui transforme `Result[int, string]` en `Result[double, string]` en doublant la valeur de succès.

??? success "Correction"
    ```janus
    import std.result

    def doubleValue(input : Result[int, string]) : Result[double, string] {
        val value : int = input?
        return Result.Ok[double, string](double(value) * 2.0)
    }

    def main() : int {
        val result : Result[int, string] = Result.Ok[int, string](21)
        val converted : Result[double, string] = doubleValue(result)
        return 0
    }
    ```

<div class="lesson-nav"><a href="../04-modeliser-donnees/">← Modéliser les données</a><a href="../06-collections-iterateurs/">Collections et itérateurs →</a></div>
