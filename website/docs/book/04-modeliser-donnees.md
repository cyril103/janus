<span class="chapter-kicker">CHAPITRE 04 / MODÉLISER</span>
# Modéliser les données

## Objectifs

- regrouper des valeurs dans un `struct` ;
- représenter plusieurs cas avec un `enum` ;
- extraire les données associées avec `match` ;
- affiner une branche avec un motif littéral ou une garde.

## Structs copiés par valeur

```janus
// doctest: doctest name=structs
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

## Motifs littéraux et gardes

Un `match` peut aussi comparer directement des littéraux. Le motif `_` couvre
les valeurs restantes. Sur un enum, une garde placée après `if` peut utiliser
les données extraites par le motif :

```janus
// doctest: doctest name=literal-match-guards
enum Opcode {
    Family(uint),
    Halt
}

def commandCode(command : string) : int {
    return match command {
        "start" => 1,
        "stop" => 0,
        _ => -1
    }
}

def decode(opcode : Opcode) : int {
    return match opcode {
        Family(bits) if (bits & uint(0xF000)) == uint(0x8000) => 8,
        Family(bits) if bits == uint(0) => 0,
        Family(bits) => -2,
        Halt => -1
    }
}

def main() : int {
    return if commandCode("start") == 1 &&
        decode(Opcode.Family(uint(0x8001))) == 8 { 0 } else { 1 }
}
```

Les littéraux booléens, entiers, flottants, caractères et chaînes sont admis
lorsque leur type correspond à la valeur examinée. Une garde doit être un
`bool` et ne rend pas à elle seule le `match` exhaustif : prévoyez une branche
sans garde ou `_`. Elle peut observer une liaison de motif, mais pas déplacer
ni détruire une ressource qui devra peut-être servir à la branche suivante.

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
