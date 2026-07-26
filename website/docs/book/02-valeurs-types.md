<span class="chapter-kicker">CHAPITRE 02 / CONTRATS</span>
# Valeurs et types

## Objectifs

- choisir entre `val` et `var` ;
- connaître les types primitifs courants ;
- effectuer une conversion numérique explicite.

## Immuable ou réassignable

`val` crée une liaison non réassignable ; `var` autorise une nouvelle valeur.

```janus
def main() : int {
    val depart : int = 40
    var resultat : int = depart
    resultat = resultat + 2
    println(resultat)
    return 0
}
```

Les types usuels incluent `int`, `uint`, `long`, `ulong`, `float`, `double`, `byte`, `char`, `bool`, `string`, `isize`, `usize` et `Unit`. Leur taille est définie par le langage.

## Conversions explicites

Janus ne transforme pas silencieusement un `int` en `double` :

```janus
val total : int = 5
val ratio : double = double(total) / 2.0
```

Cette contrainte rend les changements de représentation visibles lors de la relecture.

!!! note "Entiers"
    Les littéraux entiers sans cast ont le type `int`. Consultez la référence avant de dépendre des règles de débordement ou des limites d’un type.

## Exercice

Déclarez un nombre de secondes immuable, calculez le nombre de minutes en `double`, puis affichez-le.

??? success "Correction"
    ```janus
    def main() : int {
        val secondes : int = 90
        val minutes : double = double(secondes) / 60.0
        println(minutes)
        return 0
    }
    ```

<div class="lesson-nav"><a href="../01-premiers-pas/">← Premiers pas</a><a href="../03-controle-fonctions/">Contrôle et fonctions →</a></div>
