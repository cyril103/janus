<span class="chapter-kicker">CHAPITRE 07 / ABSTRAIRE</span>
# Génériques et closures

## Objectifs

- paramétrer une fonction ou un type avec `[T]` ;
- comprendre quand préciser les arguments génériques ;
- manipuler une fonction comme une valeur ;
- distinguer une closure sans capture d’une closure propriétaire.

## Fonctions génériques

Un paramètre de type remplace un type concret dans une déclaration. La fonction suivante conserve exactement le type reçu :

```janus
// doctest: doctest name=generic-identity
def identity[T](value : T) : T {
    return value
}

def main() : int {
    val number : int = identity[int](42)
    val text : string = identity[string]("Janus")
    println(text)
    return number - 42
}
```

Les arguments de types sont écrits entre crochets : `identity[int](42)`. Janus monomorphise le code : chaque spécialisation utilisée est vérifiée et compilée avec son type concret.

Un type peut lui aussi être générique :

```janus
struct Pair[L, R](val left : L, val right : R) {}

val result : Pair[string, int] =
    new Pair[string, int]("réponse", 42)
```

## Contraindre un paramètre

La syntaxe `T <: Trait` impose un contrat. `&` combine plusieurs contraintes :

```janus
trait Named {
    def name() : string
}

trait Sized {
    def size() : usize
}

def describe[T <: Named & Sized](value : T) : string {
    println(value.size())
    return value.name()
}
```

`T <: Copy` utilise la capacité intrinsèque `Copy`. C’est le contrat courant des opérations qui doivent produire une deuxième valeur sans transférer l’original.

## Fonctions de première classe

Le type `(int) => bool` désigne une fonction prenant un `int` et retournant un `bool` :

```janus
// doctest: doctest name=function-value
def apply(value : int, operation : (int) => int) : int {
    return operation(value)
}

def main() : int {
    val doubleIt : (int) => int =
        (value : int) => value * 2
    val answer : int = apply(21, doubleIt)
    delete doubleIt
    return answer - 42
}
```

Une closure est écrite `(paramètre : Type) => expression`. Elle peut capturer les valeurs visibles au lieu de les recevoir en paramètres :

```janus
val threshold : int = 10
val isLarge : (int) => bool =
    (value : int) => value > threshold
```

Une valeur closure est une ressource : conservez-la dans une liaison et libérez-la avec `delete`. Lorsqu’une API consomme la closure, comme de nombreux adaptateurs d’itérateurs, elle prend en charge cette destruction selon son contrat.

## Closures et propriété

Capturer une ressource ne l’autorise pas à être déplacée depuis un corps de closure. Cette restriction empêche une closure rappelée plusieurs fois de consommer deux fois la même valeur. Passez plutôt la ressource explicitement à une opération consommante, ou structurez le traitement autour d’un itérateur consommant.

!!! tip "Lire une signature"
    `def map[T, U](source : Iterator[T], transform : (T) => U) : Iterator[U]` se lit : « pour chaque `T`, appeler une fonction qui produit un `U`, puis retourner un parcours de `U` ».

## Exercice

Écrivez `twice[T]` qui applique deux fois une fonction `(T) => T` à une valeur `Copy`, puis utilisez-la pour obtenir `42` à partir de `40`.

??? success "Correction"
    ```janus
    def twice[T <: Copy](value : T, operation : (T) => T) : T {
        return operation(operation(value))
    }

    def main() : int {
        val increment : (int) => int =
            (value : int) => value + 1
        val answer : int = twice[int](40, increment)
        delete increment
        return answer - 42
    }
    ```

<div class="lesson-nav"><a href="../06-collections-iterateurs/">← Collections et itérateurs</a><a href="../08-traits-derivations/">Traits et dérivations →</a></div>
