<span class="chapter-kicker">CHAPITRE 08 / ASSEMBLER</span>
# Projet final : rapport de scores

## Objectifs

- combiner fonctions, struct, tableau et boucle ;
- produire une sortie déterministe ;
- valider un projet complet avec la chaîne d’outils.

## Le modèle

Nous allons produire un petit rapport natif à partir de scores connus. Créez un projet avec `janus new rapport`, puis remplacez `src/main.janus` :

```janus
// doctest: doctest name=final-project
import std.array

struct Summary(val count : int, val total : int, val best : int) {}

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
    return new Summary(count, total, best)
}

def main() : int {
    val scores : Array[int] = new Array[int](usize(4))
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
    return 0
}
```

Vérifiez chaque couche :

```bash
janus fmt
janus check
janus run
janus build --release
```

Sortie attendue :

```text
sessions: 4
total: 100
record: 42
```

## Exercice

Ajoutez `average : double` au rapport et affichez la moyenne. Gérez explicitement le cas d’un tableau vide.

??? success "Correction"
    ```janus
    struct Summary(
        val count : int,
        val total : int,
        val best : int,
        val average : double
    ) {}

    // À la fin de summarize :
    if count == 0 {
        return new Summary(0, 0, 0, 0.0)
    }
    return new Summary(count, total, best, double(total) / double(count))
    ```

!!! success "Vous avez le socle"
    Vous savez maintenant lire et construire un programme Janus, gérer ses ressources, utiliser ses outils et retrouver un contrat précis dans la référence. Continuez avec les [tutoriels](../tutorials/index.md) ou explorez les [exemples du dépôt](https://github.com/cyril103/janus/tree/v0.8.0/examples).

<div class="lesson-nav"><a href="../07-projets-tests-outils/">← Projets, tests et outils</a><a href="../../tutorials/">Tutoriels →</a></div>
