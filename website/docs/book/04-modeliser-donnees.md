<span class="chapter-kicker">CHAPITRE 04 / MODÉLISER</span>
# Modéliser les données

## Objectifs

- regrouper des valeurs dans un `struct` ;
- représenter plusieurs cas avec un `enum` ;
- extraire les données associées avec `match`.

## Structs copiés par valeur

```janus
struct Point(var x : int, var y : int) {
    def translate(dx : int, dy : int) : Unit {
        x = x + dx
        y = y + dy
    }
}

def main() : int {
    val origine : Point = new Point(2, 3)
    var copie : Point = origine
    copie.translate(4, 5)
    println(origine.x)
    println(copie.x)
    return 0
}
```

Ici, modifier `copie` ne modifie pas `origine`.

## Enums algébriques et `match`

```janus
enum Temperature {
    Known(int),
    Missing
}

def valeurOrZero(input : Temperature) : int {
    return match input {
        Known(value) => value,
        Missing => 0
    }
}
```

Le `match` doit traiter les variantes nécessaires et produit ici une valeur `int`.

## Exercice

Créez un enum `Command` avec `Start`, `Stop` et `Move(int)`, puis une fonction qui retourne `1`, `0` ou la distance associée.

??? success "Correction"
    ```janus
    enum Command {
        Start,
        Stop,
        Move(int)
    }

    def code(command : Command) : int {
        return match command {
            Start => 1,
            Stop => 0,
            Move(distance) => distance
        }
    }

    def main() : int {
        println(code(Command.Move(4)))
        return 0
    }
    ```

<div class="lesson-nav"><a href="../03-controle-fonctions/">← Contrôle et fonctions</a><a href="../05-erreurs-propriete/">Erreurs et propriété →</a></div>
