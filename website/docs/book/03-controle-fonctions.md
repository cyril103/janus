<span class="chapter-kicker">CHAPITRE 03 / DÉCOMPOSER</span>
# Contrôle et fonctions

## Objectifs

- écrire une fonction typée ;
- choisir avec `if` / `else if` / `else` ;
- répéter avec `while` et parcourir avec `for`.

## Des fonctions aux contrats explicites

```janus
def maximum(left : int, right : int) : int {
    if left > right {
        return left
    }
    return right
}

def main() : int {
    println(maximum(21, 42))
    return 0
}
```

Les paramètres et le retour portent un type. Une fonction sans valeur utile retourne `Unit`.

## Branches et boucles

```janus
def signe(value : int) : int {
    if value > 0 {
        return 1
    } else if value < 0 {
        return -1
    }
    return 0
}

def main() : int {
    var courant : int = 3
    while courant > 0 {
        println(courant)
        courant = courant - 1
    }
    return 0
}
```

`break` quitte une boucle et `continue` passe à l’itération suivante. `for item in collection` s’appuie sur le protocole d’itération.

## Exercice

Écrivez `sumTo(limit : int)` qui additionne les entiers de `1` à `limit` avec une boucle `while`.

??? success "Correction"
    ```janus
    def sumTo(limit : int) : int {
        var index : int = 1
        var total : int = 0
        while index <= limit {
            total = total + index
            index = index + 1
        }
        return total
    }

    def main() : int {
        println(sumTo(10))
        return 0
    }
    ```

<div class="lesson-nav"><a href="../02-valeurs-types/">← Valeurs et types</a><a href="../04-modeliser-donnees/">Modéliser les données →</a></div>
