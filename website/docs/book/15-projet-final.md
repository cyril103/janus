<span class="chapter-kicker">CHAPITRE 15 / ASSEMBLER</span>
# Projet final : analyser des scores

## Objectifs

- structurer une application en modèle, calcul et présentation ;
- combiner dérivation, collection, closure et gestion des ressources ;
- produire une sortie testable ;
- appliquer la chaîne d’outils complète.

## Créer le projet

```bash
janus new score-report
cd score-report
```

Remplacez `src/main.janus` par ce programme :

```janus
// doctest: doctest name=final-project
import std.array

struct Summary(
    val count : int,
    val total : int,
    val best : int,
    val average : double
) derives Copy, Equality, Debug {}

def summarize(values : Array[int]) : Summary {
    var count : int = 0
    var total : int = 0
    var best : int = 0

    for value in values {
        count = count + 1
        total = total + value
        if value > best {
            best = value
        }
    }

    if count == 0 {
        return new Summary(0, 0, 0, 0.0)
    }
    return new Summary(
        count,
        total,
        best,
        double(total) / double(count)
    )
}

def main() : int {
    val scores : Array[int] = new Array(4)
    defer delete scores
    scores.push(12)
    scores.push(27)
    scores.push(19)
    scores.push(42)

    val summary : Summary = summarize(scores)
    print("sessions: ")
    println(summary.count)
    print("total: ")
    println(summary.total)
    print("record: ")
    println(summary.best)
    print("moyenne: ")
    println(summary.average)
    debug(summary)
    return 0
}
```

## Lire l’architecture

`Summary` est un type de valeur. `Copy` autorise sa copie sûre, `Equality` permet de l’utiliser dans les assertions et `Debug` offre une représentation diagnostique sans écrire de formatter manuel.

`Array[int]` est une ressource propriétaire même si ses éléments sont copiables. `defer delete scores` libère son allocation à toute sortie de `main`. La fonction `summarize` observe le tableau : le tableau reste donc utilisable après l’appel.

La conversion vers `double` est explicite et le cas vide évite une division par zéro. Les sorties portent un libellé stable, donc un test d’intégration peut les comparer.

## Vérifier toutes les couches

```bash
janus fmt
janus fmt --check
janus check --locked
janus test --locked
janus run --locked
janus build --release --locked
```

Sortie principale attendue :

```text
sessions: 4
total: 100
record: 42
moyenne: 25
Summary { count: 4, total: 100, best: 42, average: 25 }
```

Le format exact d’un `double` et de `debug` est déterministe, mais reste destiné respectivement à la sortie primitive et au diagnostic. Pour un protocole public, construisez un format dédié.

## Aller plus loin

- Parsez les scores depuis un fichier avec `std.fs` et `std.text`.
- Retournez un `Result[Summary, Error]` et propagez avec `?`.
- Ajoutez une table de scores par joueur avec `HashMap`.
- Affichez le rapport dans une fenêtre avec `std.graphics`.
- Écrivez une API publique documentée et générez-la avec `janus doc`.

## Exercice

Ajoutez une fonction générique `countMatching[T <: Copy]` qui reçoit un tableau et une closure `(T) => bool`, puis compte les éléments acceptés. Utilisez-la pour compter les scores supérieurs ou égaux à 20.

??? success "Correction"
    ```janus
    import std.array

    def countMatching[T <: Copy](
        values : Array[T],
        predicate : (T) => bool
    ) : int {
        var count : int = 0
        for value in values {
            if predicate(value) {
                count = count + 1
            }
        }
        delete predicate
        return count
    }

    // Dans main :
    val accepted : int = countMatching[int](
        scores,
        (score : int) => score >= 20
    )
    println(accepted)
    ```

!!! success "Vous avez la carte complète"
    Vous savez maintenant lire la syntaxe Janus, concevoir des types, gérer les ressources, utiliser les capacités dérivées, composer la stdlib et vérifier un projet natif. Revenez à la [référence des mots-clés](14-reference-mots-cles.md), aux [tutoriels](../tutorials/index.md) ou à l’[index API](../reference/stdlib/index.html) selon votre besoin.

<div class="lesson-nav"><a href="../14-reference-mots-cles/">← Tous les mots-clés</a><a href="../../tutorials/">Tutoriels →</a></div>
