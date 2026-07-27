# Construire un compteur CLI

## Prérequis

- Janus 0.6.0 installé et disponible dans le `PATH` ;
- un terminal ;
- aucune dépendance externe.

## Résultat

Un exécutable natif qui affiche `étape: 1` à `étape: 5`, une ligne à la fois, puis retourne `0`.

## 1. Créer le projet

```bash
janus new compteur
cd compteur
```

## 2. Décrire la progression

Remplacez `src/main.janus` :

```janus
def printStep(step : int) : Unit {
    print("étape: ")
    println(step)
}

def main() : int {
    var step : int = 1
    while step <= 5 {
        printStep(step)
        step = step + 1
    }
    return 0
}
```

`var` est nécessaire car `step` est réaffectée. La fonction `printStep` retourne `Unit`. `print` écrit le libellé sans terminer la ligne ; `println` écrit la valeur et la fin de ligne logique.

## 3. Vérifier puis exécuter

```bash
janus check
janus run
janus build --release
```

Sortie attendue :

```text
étape: 1
étape: 2
étape: 3
étape: 4
étape: 5
```

L’exécutable optimisé est écrit sous `target/release`.

## Vérification automatique

Créez `tests/count.janus` :

```janus
def countSteps(limit : int) : int {
    var step : int = 0
    while step < limit {
        step = step + 1
    }
    return step
}

def main() : int {
    if countSteps(5) == 5 {
        return 0
    }
    return 1
}
```

```bash
janus test count
janus fmt --check
```

## Prolongements

- passez la limite à une `val` globale initialisée ;
- ignorez l’étape 3 avec `continue` ;
- quittez à l’étape 4 avec `break` et observez la sortie.
